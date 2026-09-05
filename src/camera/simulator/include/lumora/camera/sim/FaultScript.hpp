#pragma once

#include <lumora/core/Result.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace lumora::camera::sim {

enum class SimulatedFault { Timeout, Disconnect, MalformedFrame, ConfigurationFailure };
enum class FaultPoint { Retrieval, Configuration };

struct FaultEvent final {
    std::uint64_t atFrame;
    SimulatedFault fault;
    std::uint32_t repeatCount{1U};
    // Exactly one trigger: atFrame > 0, or atFrame == 0 and atElapsed >= 0.
    std::optional<std::chrono::milliseconds> atElapsed{};
};

// Owned jointly by one simulated device and its test controller. Control calls
// are thread-safe. Deliberately sharing with several devices shares occurrences.
// Scripts retain consumed state and connection state across stop/start/open.
class FaultScript final {
public:
    struct Occurrence final {
        std::size_t index;
        SimulatedFault fault;
    };

    // Holds the script lock before constructing a caller's result. Destruction
    // without commit leaves state unchanged; commit performs scalar writes only.
    class PendingConsumption final {
    public:
        PendingConsumption(const PendingConsumption&) = delete;
        PendingConsumption& operator=(const PendingConsumption&) = delete;
        PendingConsumption(PendingConsumption&&) = delete;
        PendingConsumption& operator=(PendingConsumption&&) = delete;
        void commit() noexcept;

    private:
        friend class FaultScript;
        PendingConsumption(FaultScript& script, Occurrence occurrence);
        FaultScript* script_;
        Occurrence occurrence_;
        std::unique_lock<std::mutex> lock_;
        bool committed_{false};
    };

    [[nodiscard]] static core::Result<std::shared_ptr<FaultScript>> create(
        std::vector<FaultEvent> events);

    // Frame triggers match exact next publication ID. Elapsed triggers become
    // due at/after the threshold measured from each stream start; nullopt before
    // the first start. Events are evaluated on operations, with no timer thread.
    // Frame events sort before time events, then ascending trigger; equal
    // triggers retain input order. Only events applicable to point are selected.
    [[nodiscard]] std::optional<Occurrence> match(
        std::uint64_t nextFrameId,
        std::optional<std::chrono::milliseconds> elapsed,
        FaultPoint point) const;
    // Selection is non-consuming. Consume at the failed operation's commit
    // point, after cancellation checks. Repeated failures leave frame ID fixed.
    void consume(Occurrence occurrence);
    [[nodiscard]] PendingConsumption prepareConsumption(Occurrence occurrence);
    [[nodiscard]] bool disconnected() const;
    void restoreConnection();

private:
    explicit FaultScript(std::vector<FaultEvent> events);
    void consumeLocked(Occurrence occurrence) noexcept;
    mutable std::mutex mutex_;
    std::vector<FaultEvent> events_;
    bool disconnected_{false};
};

}  // namespace lumora::camera::sim
