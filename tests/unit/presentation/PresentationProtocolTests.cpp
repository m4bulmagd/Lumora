#include <lumora/presentation/PresentationProtocol.hpp>
#include "PresentationTestFrames.hpp"
#include <gtest/gtest.h>
namespace {
using namespace lumora::presentation;
TEST(PresentationProtocol, AcceptsZeroSourceIdAndAllAvailableModes) {
    lumora::core::ManualClock clock; lumora::test::PresentationTestFrames frames;
    for (auto mode : {DisplayMode::Original, DisplayMode::Enhanced, DisplayMode::Compare}) {
        EXPECT_TRUE(validatePresentation({{1, 0, 1}, frames.frame(0, clock), mode}).hasValue());
    }
}
TEST(PresentationProtocol, RejectsMalformedTicketsNullBundleAndUnknownMode) {
    lumora::core::ManualClock clock; lumora::test::PresentationTestFrames frames;
    auto bundle = frames.frame(3, clock);
    for (auto ticket : {PresentationTicket{0, 3, 1}, PresentationTicket{1, 3, 0}, PresentationTicket{1, 4, 1}})
        EXPECT_FALSE(validatePresentation({ticket, bundle, DisplayMode::Original}).hasValue());
    EXPECT_FALSE(validatePresentation({{1, 3, 1}, {}, DisplayMode::Original}).hasValue());
    EXPECT_FALSE(validatePresentation({{1, 3, 1}, bundle, static_cast<DisplayMode>(99)}).hasValue());
}
TEST(PresentationProtocol, RequiresSecondPlaneForEnhancedAndCompare) {
    lumora::core::ManualClock clock; lumora::test::PresentationTestFrames frames;
    auto bundle = frames.frame(1, clock, false);
    EXPECT_TRUE(validatePresentation({{1, 1, 1}, bundle, DisplayMode::Original}).hasValue());
    for (auto mode : {DisplayMode::Enhanced, DisplayMode::Compare})
        EXPECT_FALSE(validatePresentation({{1, 1, 1}, bundle, mode}).hasValue());
}
TEST(PresentationProtocol, RejectsUnsupportedStorageEvenInUnselectedSecondPlane) {
    lumora::core::ManualClock clock; lumora::test::PresentationTestFrames frames;
    using lumora::core::DisplayStorage;
    for (auto bundle : {frames.frame(1, clock, false, 30, DisplayStorage::Gray16),
                       frames.frame(1, clock, true, 30, DisplayStorage::Gray8, DisplayStorage::Gray16)})
        EXPECT_FALSE(validatePresentation({{1, 1, 1}, bundle, DisplayMode::Original}).hasValue());
}
TEST(PresentationProtocol, RejectsInvalidActualFrameRates) {
    lumora::core::ManualClock clock; lumora::test::PresentationTestFrames frames;
    for (double fps : {0., -1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        EXPECT_FALSE(validatePresentation({{1, 1, 1}, frames.frame(1, clock, true, fps), DisplayMode::Enhanced}).hasValue());
}
TEST(PresentationProtocol, CoreFactoriesRejectMalformedSecondPlaneBeforePresentation) {
    lumora::core::ManualClock clock; lumora::test::PresentationTestFrames frames;
    auto first = frames.frame(1, clock); auto second = frames.frame(2, clock);
    EXPECT_FALSE(lumora::core::FrameBundle::create(first->raw, first->originalDisplay,
        first->enhanced, second->enhancedDisplay, *frames.objects).hasValue());
    EXPECT_FALSE(lumora::core::FrameBundle::create(first->raw, first->originalDisplay,
        first->enhanced, {}, *frames.objects).hasValue());
}
}
