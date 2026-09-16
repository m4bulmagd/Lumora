#include <lumora/camera/CompositeCameraProvider.hpp>

#include <optional>
#include <utility>

namespace lumora::camera {
namespace {
core::Error discoveryError(std::string code, std::string summary,
    core::ErrorCategory category = core::ErrorCategory::CameraDiscovery) {
    return {category, std::move(code), std::move(summary), "", true};
}
}  // namespace

CompositeCameraProvider::CompositeCameraProvider(std::vector<ICameraProvider*> providers)
    : providers_(std::move(providers)) {}

core::Result<std::vector<CameraDescriptor>> CompositeCameraProvider::discover(
    std::stop_token stopToken) {
    using Result = core::Result<std::vector<CameraDescriptor>>;
    routes_.clear();
    std::map<CameraId, ICameraProvider*> routes;
    std::vector<CameraDescriptor> descriptors;
    std::optional<core::Error> firstFailure;
    bool providerSucceeded = false;
    const auto cancelled = [] {
        return Result::failure(discoveryError("cancelled", "Camera discovery was cancelled.",
            core::ErrorCategory::Cancelled));
    };
    if (stopToken.stop_requested()) return cancelled();
    for (auto* provider : providers_) {
        if (!provider) return Result::failure(discoveryError(
            "camera_provider_missing", "Camera discovery requires valid providers."));
        auto found = provider->discover(stopToken);
        if (stopToken.stop_requested()) return cancelled();
        if (!found.hasValue()) {
            if (found.error().category == core::ErrorCategory::Cancelled)
                return Result::failure(found.error());
            if (!firstFailure) firstFailure = found.error();
            continue;
        }
        providerSucceeded = true;
        for (auto& descriptor : found.value()) {
            if (!routes.emplace(descriptor.id, provider).second)
                return Result::failure(discoveryError("duplicate_camera_id",
                    "Camera providers returned duplicate source identifiers."));
            descriptors.push_back(std::move(descriptor));
        }
    }
    if (!providerSucceeded && firstFailure) return Result::failure(std::move(*firstFailure));
    routes_ = std::move(routes);
    return Result::success(std::move(descriptors));
}

core::Result<std::unique_ptr<ICameraDevice>> CompositeCameraProvider::create(const CameraId& id) {
    const auto found = routes_.find(id);
    if (found == routes_.end())
        return core::Result<std::unique_ptr<ICameraDevice>>::failure(discoveryError(
            "camera_not_found", "Refresh sources before connecting an available camera."));
    return found->second->create(id);
}

}  // namespace lumora::camera
