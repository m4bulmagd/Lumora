#pragma once

namespace lumora::camera::sim::testing {

// Only linked when tests/CMakeLists.txt enables the private simulator test seam.
// Thread-local: one test's injection cannot affect unrelated camera operations.
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
