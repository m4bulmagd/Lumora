#pragma once

namespace lumora::presentation::testing {

// Linked only when tests/CMakeLists.txt enables the private coordinator seam.
// Thread-local so a deterministic acknowledgement race cannot leak to another
// frontend thread or test.
using ContextAcknowledgementHook = void (*)();
[[nodiscard]] ContextAcknowledgementHook exchangeContextAcknowledgementHook(
    ContextAcknowledgementHook hook) noexcept;

class ScopedContextAcknowledgementHook final {
public:
    explicit ScopedContextAcknowledgementHook(
        ContextAcknowledgementHook hook) noexcept
        : previous_(exchangeContextAcknowledgementHook(hook)) {}
    ~ScopedContextAcknowledgementHook() {
        static_cast<void>(exchangeContextAcknowledgementHook(previous_));
    }
    ScopedContextAcknowledgementHook(
        const ScopedContextAcknowledgementHook&) = delete;
    ScopedContextAcknowledgementHook& operator=(
        const ScopedContextAcknowledgementHook&) = delete;

private:
    ContextAcknowledgementHook previous_;
};

}  // namespace lumora::presentation::testing
