#pragma once
#include <lumora/presentation/PresentationProtocol.hpp>
#include <utility>
#include <array>
#include <stdexcept>

namespace lumora::test {
// A bounded renderer boundary: consumption, completion, GUI delivery, and
// retirement are deliberately separate. No policy, source reads, or clock here.
class ControlledPresentationSink final : public presentation::IPresentationSink {
public:
    std::optional<presentation::PresentationSubmission> pending;
    std::optional<presentation::PresentationSubmission> visible;
    std::optional<std::uint64_t> retiring;
    bool consumed{false};
    bool reject{false};
    bool available{true};
    bool ready() const override { return available && !pending && !retiring; }
    core::Result<void> submit(presentation::PresentationSubmission submission) override {
        auto validation = presentation::validatePresentation(submission);
        if (!validation.hasValue()) return validation;
        if (!ready() || reject) return core::Result<void>::failure(failureError());
        pending = std::move(submission);
        consumed = false;
        return core::Result<void>::success();
    }
    bool cancelPending(presentation::PresentationTicket ticket) override {
        if (!pending || pending->ticket != ticket || consumed) return false;
        pending.reset();
        return true;
    }
    void retire(std::uint64_t id) override { retiring = id; }
    std::optional<presentation::PresentationEvent> takeEvent() override {
        auto result = std::exchange(delivered_[0], std::move(delivered_[1]));
        delivered_[1].reset();
        return result;
    }
    void consume() { if (pending) consumed = true; }
    void complete(std::chrono::steady_clock::time_point when) {
        if (!pending || !consumed) return;
        visible = std::move(pending);
        pending.reset();
        queue(rendered_, presentation::PresentationReceipt{visible->ticket, when});
    }
    void fail(bool retained) {
        if (!pending && !visible) return;
        const auto ticket = pending ? pending->ticket : visible->ticket;
        pending.reset();
        if (!retained) visible.reset();
        queue(rendered_, presentation::PresentationFailure{ticket, failureError(), retained});
    }
    void deliver() {
        for (auto& event : rendered_) {
            if (event) { inject(std::move(*event)); event.reset(); }
        }
    }
    void finishRetirement() {
        if (!retiring) return;
        pending.reset();
        visible.reset();
        rendered_ = {};
        delivered_ = {};
        inject(presentation::PresentationRetired{*retiring});
        retiring.reset();
    }
    void inject(presentation::PresentationEvent event) { queue(delivered_, std::move(event)); }
    static core::Error failureError() {
        return {core::ErrorCategory::Internal, "controlled_failure",
            "Presentation failed.", "Controlled renderer failure.", true};
    }
private:
    using Events = std::array<std::optional<presentation::PresentationEvent>, 2>;
    static void queue(Events& events, presentation::PresentationEvent event) {
        for (auto& entry : events) {
            if (!entry) { entry = std::move(event); return; }
        }
        throw std::logic_error("Controlled event capacity exceeded");
    }
    Events rendered_;
    std::array<std::optional<presentation::PresentationEvent>, 2> delivered_;
};
}  // namespace lumora::test
