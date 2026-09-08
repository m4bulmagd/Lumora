#pragma once
#include <array>
#include <cfenv>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
namespace lumora::processing::detail {
// Private fault/lifetime seam. Context outlives every callback that uses it;
// beforeThreadStart is constructor-only. No process globals.
enum class CpuEnvironmentOperation { CaptureCaller, SaveHelper, InstallHelper, RestoreHelper };
struct CpuExecutorTestHooks {
    void* context{};
    void (*beforeThreadStart)(void*,std::size_t){};
    void (*workerLifetime)(void*,std::size_t,bool) noexcept{};
    bool (*failEnvironment)(void*,CpuEnvironmentOperation,std::size_t) noexcept{};
};
class CpuExecutorStartupError final : public std::exception {
public: const char* what() const noexcept override { return "Prepared CPU helper startup failed"; }
};
// One synchronous submitting caller. Object bytes exclude thread stacks, TLS,
// thread-library and OS bookkeeping. Empty ranges never invoke callbacks.
// Each job propagates caller control modes; helper exception flags are not merged.
// Capture/setup failure falls back before mutation. Restore failure never replays
// completed work and makes future submissions serial.
class PreparedCpuExecutor final {
public:
    using Work = void (*)(void*,std::size_t,std::size_t,std::size_t) noexcept;
    explicit PreparedCpuExecutor(std::size_t slots,CpuExecutorTestHooks hooks={});
    ~PreparedCpuExecutor();
    PreparedCpuExecutor(const PreparedCpuExecutor&)=delete;
    PreparedCpuExecutor& operator=(const PreparedCpuExecutor&)=delete;
    std::size_t slots() const noexcept { return slots_; }
    void run(std::size_t itemCount,void* context,Work work) noexcept;
private:
    void worker(std::size_t slot) noexcept;
    void stop() noexcept;
    bool fail(CpuEnvironmentOperation,std::size_t) noexcept;
    void invoke(std::size_t slot) noexcept;
    const std::size_t slots_;
    CpuExecutorTestHooks hooks_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::array<std::thread,3> threads_;
    std::size_t ready_{},prepared_{},completed_{},generation_{};
    bool stopping_{},decided_{},commit_{},setupFailed_{},serialOnly_{};
    std::size_t itemCount_{};
    void* context_{};
    Work work_{};
    std::fenv_t callerEnvironment_{};
    unsigned callerSimdControl_{};
};
}
