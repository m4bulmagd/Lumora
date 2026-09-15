#include <lumora/configuration/UiPreferencesCodec.hpp>
#include <gtest/gtest.h>
#include <limits>

namespace lumora::configuration {
TEST(UiPreferences, MissingLayoutUsesWritableDefaultsWithoutWarning) {
    const auto loaded = UiPreferencesCodec::decode({{"other", "retained"}});
    EXPECT_EQ(loaded.preferences, application::UiPreferences{});
    EXPECT_TRUE(loaded.writable);
    EXPECT_FALSE(loaded.warning);
    EXPECT_TRUE(UiPreferencesCodec::validate(loaded.preferences).hasValue());
}

TEST(UiPreferences, RoundTripPreservesUnknownKeysAndNegativeCoordinates) {
    const QJsonObject original{{"other", "retained"}, {"layout", QJsonObject{
        {"version", 1}, {"extension", QJsonObject{{"keep", 7}}}}}};
    application::UiPreferences chosen{{application::WindowGeometry{-1920, -20, 1280, 800}},
        true, true, true, true};
    const auto merged = UiPreferencesCodec::merge(original, chosen);
    ASSERT_TRUE(merged.hasValue());
    EXPECT_EQ(merged.value().value("other"), original.value("other"));
    EXPECT_EQ(merged.value().value("layout").toObject().value("extension"),
        original.value("layout").toObject().value("extension"));
    const auto loaded = UiPreferencesCodec::decode(merged.value());
    EXPECT_EQ(loaded.preferences, chosen);
    EXPECT_TRUE(loaded.writable);
    EXPECT_FALSE(loaded.warning);
}

TEST(UiPreferences, MalformedCurrentLayoutRecoversButFutureVersionRejectsMerge) {
    for (const auto& malformed : {QJsonValue("bad"), QJsonValue(QJsonObject{
        {"version", 1}, {"panelsCollapsed", "false"}}), QJsonValue(QJsonObject{
        {"version", 1}, {"normalGeometry", QJsonObject{{"x", 0.5}, {"y", 0},
            {"width", 1280}, {"height", 800}}}})}) {
        const QJsonObject ui{{"layout", malformed}};
        const auto loaded = UiPreferencesCodec::decode(ui);
        EXPECT_EQ(loaded.preferences, application::UiPreferences{});
        EXPECT_TRUE(loaded.writable);
        EXPECT_TRUE(loaded.warning);
        EXPECT_TRUE(UiPreferencesCodec::merge(ui, {}).hasValue());
    }
    const QJsonObject future{{"other", 4}, {"layout", QJsonObject{
        {"version", 2}, {"panelsCollapsed", true}, {"newMeaning", "keep"}}}};
    const auto loaded = UiPreferencesCodec::decode(future);
    EXPECT_FALSE(loaded.writable);
    EXPECT_TRUE(loaded.warning);
    EXPECT_FALSE(UiPreferencesCodec::merge(future, {}).hasValue());
}

TEST(UiPreferences, RejectsInvalidGeometryBeforeConversionOrSave) {
    for (const auto geometry : {application::WindowGeometry{0, 0, 0, 800},
        application::WindowGeometry{0, 0, 900, -1}}) {
        application::UiPreferences value;
        value.normalGeometry = geometry;
        EXPECT_FALSE(UiPreferencesCodec::validate(value).hasValue());
        EXPECT_FALSE(UiPreferencesCodec::merge({}, value).hasValue());
    }
    for (const double coordinate : {0.5, 2147483648.0, -2147483649.0}) {
        const QJsonObject ui{{"layout", QJsonObject{{"version", 1},
            {"normalGeometry", QJsonObject{{"x", coordinate}, {"y", 0},
                {"width", 900}, {"height", 600}}}}}};
        const auto decoded = UiPreferencesCodec::decode(ui);
        EXPECT_FALSE(decoded.preferences.normalGeometry);
        EXPECT_TRUE(decoded.warning);
    }
}
}  // namespace lumora::configuration
