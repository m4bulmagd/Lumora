#include <lumora/camera/CompositeCameraProvider.hpp>

#include <gtest/gtest.h>

#include <functional>
#include <optional>

namespace {
using namespace lumora;

class Provider final : public camera::ICameraProvider {
public:
    explicit Provider(std::string id) : id_(std::move(id)) {}
    std::optional<core::Error> discoveryError;
    std::function<void(std::stop_token)> duringDiscovery;
    bool present{true};
    core::Result<std::vector<camera::CameraDescriptor>> discover(std::stop_token token) override {
        if (duringDiscovery) duringDiscovery(token);
        if (discoveryError) return core::Result<std::vector<camera::CameraDescriptor>>::failure(*discoveryError);
        std::vector<camera::CameraDescriptor> found;
        if (present) found.push_back({{id_}, {"Test", "Camera", id_, "Test", std::nullopt}, true});
        return core::Result<std::vector<camera::CameraDescriptor>>::success(std::move(found));
    }
    core::Result<std::unique_ptr<camera::ICameraDevice>> create(const camera::CameraId& id) override {
        return core::Result<std::unique_ptr<camera::ICameraDevice>>::failure({
            core::ErrorCategory::CameraConnection, "create_" + id_, "Device creation failed.", id.value, true});
    }
private:
    std::string id_;
};

TEST(CompositeCameraProvider, CombinesOpaqueIdsAndRoutesCreationToDiscoveredProvider) {
    Provider simulator("SIM-LIVE"), local("media:local:device");
    camera::CompositeCameraProvider composite({&simulator, &local});
    auto found = composite.discover({});
    ASSERT_TRUE(found.hasValue());
    ASSERT_EQ(found.value().size(), 2U);
    EXPECT_EQ(found.value()[0].id.value, "SIM-LIVE");
    EXPECT_EQ(found.value()[1].id.value, "media:local:device");
    for (const auto& id : {"SIM-LIVE", "media:local:device"}) {
        auto device = composite.create({id});
        ASSERT_FALSE(device.hasValue());
        EXPECT_EQ(device.error().code, std::string("create_") + id);
        EXPECT_EQ(device.error().diagnosticDetail, id);
    }
    EXPECT_EQ(composite.create({"unknown"}).error().code, "camera_not_found");
}

TEST(CompositeCameraProvider, FailedOptionalProviderDoesNotHideHealthySources) {
    Provider failed("media:local:device"), healthy("SIM-LIVE");
    failed.discoveryError = core::Error{core::ErrorCategory::CameraDiscovery,
        "backend_unavailable", "Backend unavailable.", "", true};
    camera::CompositeCameraProvider composite({&failed, &healthy});
    auto found = composite.discover({});
    ASSERT_TRUE(found.hasValue());
    ASSERT_EQ(found.value().size(), 1U);
    EXPECT_EQ(found.value()[0].id.value, "SIM-LIVE");
    EXPECT_EQ(composite.create({"SIM-LIVE"}).error().code, "create_SIM-LIVE");
}

TEST(CompositeCameraProvider, AllFailedProvidersReturnDiscoveryFailure) {
    Provider failed("media:local:device");
    failed.discoveryError = core::Error{core::ErrorCategory::CameraDiscovery,
        "backend_unavailable", "Backend unavailable.", "", true};
    camera::CompositeCameraProvider composite({&failed});
    auto found = composite.discover({});
    ASSERT_FALSE(found.hasValue());
    EXPECT_EQ(found.error().code, "backend_unavailable");
}

TEST(CompositeCameraProvider, DuplicateIdsRejectDiscoveryAndDoNotLeaveAmbiguousRoutes) {
    Provider first("SIM-LIVE"), second("SIM-LIVE");
    second.present = false;
    camera::CompositeCameraProvider composite({&first, &second});
    ASSERT_TRUE(composite.discover({}).hasValue());
    second.present = true;
    auto found = composite.discover({});
    ASSERT_FALSE(found.hasValue());
    EXPECT_EQ(found.error().code, "duplicate_camera_id");
    EXPECT_EQ(composite.create({"SIM-LIVE"}).error().code, "camera_not_found");
}

TEST(CompositeCameraProvider, CancellationPropagatesAndDiscardsPartialDiscovery) {
    Provider first("SIM-LIVE"), second("media:rtsp:network");
    camera::CompositeCameraProvider composite({&first, &second});
    ASSERT_TRUE(composite.discover({}).hasValue());
    std::stop_source stop;
    first.duringDiscovery = [&](std::stop_token token) {
        EXPECT_EQ(token, stop.get_token());
        stop.request_stop();
    };
    auto found = composite.discover(stop.get_token());
    ASSERT_FALSE(found.hasValue());
    EXPECT_EQ(found.error().category, core::ErrorCategory::Cancelled);
    EXPECT_EQ(composite.create({"SIM-LIVE"}).error().code, "camera_not_found");
}

TEST(CompositeCameraProvider, ProviderCancellationIsNotTreatedAsOptionalFailure) {
    Provider first("SIM-LIVE"), second("media:rtsp:network");
    second.discoveryError = core::Error{core::ErrorCategory::Cancelled,
        "cancelled", "Discovery cancelled.", "", true};
    camera::CompositeCameraProvider composite({&first, &second});
    auto found = composite.discover({});
    ASSERT_FALSE(found.hasValue());
    EXPECT_EQ(found.error().category, core::ErrorCategory::Cancelled);
}

TEST(CompositeCameraProvider, RefreshRemovesPreviouslyDiscoveredRoutes) {
    Provider source("SIM-LIVE");
    camera::CompositeCameraProvider composite({&source});
    ASSERT_TRUE(composite.discover({}).hasValue());
    source.present = false;
    ASSERT_TRUE(composite.discover({}).hasValue());
    EXPECT_EQ(composite.create({"SIM-LIVE"}).error().code, "camera_not_found");
}
}  // namespace
