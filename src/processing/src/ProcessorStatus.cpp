#include "PreparedRuntime.hpp"
#include <limits>
namespace lumora::processing {
ProcessorStatus FrameProcessingEngine::status() const noexcept {
    std::lock_guard lock(state_->stateMutex);
    auto status=state_->processorStatus;
    status.retrySupported=true;
    status.retryPending=state_->retryPending.load(std::memory_order_acquire);
    return status;
}
bool FrameProcessingEngine::requestRetry() noexcept {
    state_->retryPending.store(true,std::memory_order_release);
    return true;
}
namespace detail {
bool recordEnhancementFailure(EngineState& state,ProcessingOperation operation,const core::Error& error) {
    // Original error payload is retained once; diagnostic allocation is outside
    // admission and successful processing. Never build a diagnostic on fallback.
    auto diagnostic=std::make_shared<const core::Error>(error);
    std::shared_ptr<const core::Error> released;
    bool latched{};
    {
        std::lock_guard lock(state.stateMutex);
        auto& status=state.processorStatus;
        if(status.consecutiveEnhancementFailures<3) ++status.consecutiveEnhancementFailures;
        if(status.enhancementFailures<std::numeric_limits<std::uint64_t>::max()) ++status.enhancementFailures;
        status.failingOperation=operation;
        released=std::move(status.error); status.error=std::move(diagnostic);
        latched=status.consecutiveEnhancementFailures==3;
        if(latched) status.mode=ProcessorMode::OriginalOnlyLatched;
    }
    return latched;
}
void recordEnhancedSuccess(EngineState& state) {
    std::shared_ptr<const core::Error> released;
    {
        std::lock_guard lock(state.stateMutex);
        auto& status=state.processorStatus;
        status.consecutiveEnhancementFailures=0;
        status.failingOperation.reset(); released=std::move(status.error);
    }
}
}
}
