#pragma once
#include <lumora/core/Frame.hpp>
#include <lumora/core/BufferPool.hpp>
#include <memory>
#include <optional>
namespace lumora::processing {
namespace detail { struct EnginePlan; }
struct ProcessingPreparationOptions final {
    core::Orientation orientation{false, false, core::Rotation::Degrees0};
    std::size_t storageBudgetBytes{512U * 1024U * 1024U};
    std::optional<std::size_t> activationEnvelopeBytes;
    std::size_t externalSessionStorageBytes{};
    // Stopped preparation only: includes the caller and is frozen by the plan.
    std::size_t cpuExecutionSlots{4};
};
struct ProcessingPoolSpecification final { std::size_t capacity; std::size_t bytesPerBuffer; };
// Accounted requested storage for one session, not RSS or an all-session cap.
// Excludes allocator headers, BufferPool's two and FrameObjectPool's three fixed
// STL owner controls, compiler/probe temporaries, and retained/error diagnostic
// strings, thread stacks, TLS, thread-library and OS bookkeeping.
// New prepared owner controls use enforced bounded reservations.
struct ProcessingResources final {
    // Dimensionless admitted capacity, independent of hardware concurrency.
    std::size_t cpuExecutionSlots{};
    std::size_t cpuHelperThreads{};
    // Separately allocated executor object, counted once in fixed storage.
    std::size_t cpuExecutorBytes{};
    std::size_t externalSessionBytes{};
    std::size_t processingPoolBytes{};
    std::size_t displayPoolBytes{};
    std::size_t frameObjectBytes{};
    std::size_t orientationBytes{};
    std::size_t engineStateBytes{};
    std::size_t fixedStorageBytes{};
    // Assessed candidate; FrameProcessingEngine::resources() reports its active definition.
    std::size_t candidateRequiredBytes{};
    std::size_t activationEnvelopeBytes{};
    std::size_t activationReserveBytes{};
    std::size_t gammaCacheReserveBytes{};
    std::size_t requiredStorageBytes{};
    std::size_t storageBudgetBytes{};
    // Distinct registered active/in-flight/cache stage ownership, including each
    // enforced control reservation. Excludes a candidate still being constructed.
    // This is accounted storage, not observed heap bytes or allocator usage.
    std::size_t actualRetainedStageBytes{};
    bool customProcessorStorageUnknown{false};
};
class ProcessingPreparationPlan final {
public:
    // Copy-only, including move expressions: no public operation invalidates a plan.
    ~ProcessingPreparationPlan() = default;
    ProcessingPreparationPlan(const ProcessingPreparationPlan&) noexcept = default;
    ProcessingPreparationPlan& operator=(const ProcessingPreparationPlan&) noexcept = default;
    [[nodiscard]] const ProcessingResources& resources() const noexcept;
private:
    friend class FrameProcessingEngine;
    explicit ProcessingPreparationPlan(std::shared_ptr<const detail::EnginePlan> impl) noexcept : impl_(std::move(impl)) {}
    std::shared_ptr<const detail::EnginePlan> impl_;
};
struct ProcessingPreparationAssessment final {
    ProcessingResources resources;
    std::optional<ProcessingPreparationPlan> plan;
    std::optional<core::Error> error;
};
}
