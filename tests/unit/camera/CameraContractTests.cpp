#include <lumora/camera/ICameraDevice.hpp>
#include <lumora/camera/ICameraProvider.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <stop_token>
#include <type_traits>
#include <utility>
#include <vector>

namespace lumora::camera {
namespace {

class ContractCamera final : public ICameraDevice {
public:
    core::Result<void> open() override;
    core::Result<CameraCapabilities> capabilities() override;
    core::Result<AppliedCameraConfiguration> applyConfiguration(
        const CameraConfiguration& configuration) override;
    core::Result<void> startStream() override;
    core::Result<std::shared_ptr<const core::RawFrame>> retrieve(
        std::chrono::milliseconds timeout,
        core::BufferPool& destination,
        std::stop_token stopToken) override;
    core::Result<void> stopStream() noexcept override;
    core::Result<void> close() noexcept override;
};

class ContractProvider final : public ICameraProvider {
public:
    core::Result<std::vector<CameraDescriptor>> discover(std::stop_token stopToken) override;
    core::Result<std::unique_ptr<ICameraDevice>> create(const CameraId& id) override;
};

using RetrieveResult = decltype(std::declval<ICameraDevice&>().retrieve(
    std::chrono::milliseconds{1}, std::declval<core::BufferPool&>()));

static_assert(!std::is_abstract_v<ContractCamera>);
static_assert(!std::is_abstract_v<ContractProvider>);
static_assert(noexcept(std::declval<ICameraDevice&>().stopStream()));
static_assert(noexcept(std::declval<ICameraDevice&>().close()));
static_assert(std::is_same_v<
              RetrieveResult,
              core::Result<std::shared_ptr<const core::RawFrame>>>);

TEST(CameraContract, ConcreteFakesSatisfyVendorNeutralPorts) {
    SUCCEED();
}

}  // namespace
}  // namespace lumora::camera
