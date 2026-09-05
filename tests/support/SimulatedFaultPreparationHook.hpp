#pragma once

namespace lumora::camera::sim::testing {

// Only linked when tests/CMakeLists.txt enables the private simulator test seam.
// Thread-local: one test's injection cannot affect unrelated camera operations.
// Invoked by the production commit helper before error preparation, before the
// final-result/cancellation boundary, and before translating a preparation error.
using FaultPreparationHook = void (*)();
[[nodiscard]] FaultPreparationHook exchangeFaultPreparationHook(
    FaultPreparationHook hook) noexcept;

class ScopedFaultPreparationHook final {
public:
    explicit ScopedFaultPreparationHook(FaultPreparationHook hook) noexcept
        : previous_(exchangeFaultPreparationHook(hook)) {}
    ~ScopedFaultPreparationHook() {
        static_cast<void>(exchangeFaultPreparationHook(previous_));
    }
    ScopedFaultPreparationHook(const ScopedFaultPreparationHook&) = delete;
    ScopedFaultPreparationHook& operator=(const ScopedFaultPreparationHook&) = delete;

private:
    FaultPreparationHook previous_;
};

}  // namespace lumora::camera::sim::testing
