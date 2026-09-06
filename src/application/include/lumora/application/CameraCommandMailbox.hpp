#pragma once

#include <lumora/application/ApplicationState.hpp>
#include <lumora/application/CameraCommand.hpp>
#include <lumora/core/Result.hpp>

#include <array>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <stop_token>

namespace lumora::application {

// Multiple producers, one camera worker. Complete a popped Stop/Disconnect
// only after device cleanup; its admission fence survives dequeue. Until then,
// further pops yield no command except Shutdown. Request IDs identify worker
// completions and must not be reused while an earlier completion can arrive.
class CameraCommandMailbox final {
public:
    [[nodiscard]] core::Result<void> post(CameraCommand command);
    [[nodiscard]] std::optional<CameraCommand> tryPop();
    [[nodiscard]] std::optional<CameraCommand> waitPop(std::stop_token stopToken);
    void completeBarrier(std::uint64_t requestId) noexcept;
    void close() noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] CameraMailboxStats stats() const noexcept;

private:
    struct Entry final {
        CameraCommand command;
        // False for entries preceding an admitted lifecycle barrier, even if
        // that barrier has since been selected ahead of them.
        bool coalescible{true};
    };
    struct Barrier final {
        std::uint64_t requestId;
        bool disconnect;
    };
    void erase(std::size_t index) noexcept;
    [[nodiscard]] std::optional<CameraCommand> popLocked();
    std::array<std::optional<Entry>, 32U> entries_;
    std::size_t size_{0U};
    mutable std::mutex mutex_;
    std::condition_variable_any condition_;
    std::optional<Barrier> inFlightBarrier_;
    CameraMailboxStats stats_;
    bool sealed_{false};
    bool closed_{false};
};

}  // namespace lumora::application
