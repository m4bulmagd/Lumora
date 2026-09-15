#include <lumora/configuration/UiPreferencesCodec.hpp>

#include <cmath>
#include <limits>
#include <utility>

namespace lumora::configuration {
namespace {
core::Error invalid() {
    return {core::ErrorCategory::Configuration, "ui_preferences_invalid",
        "Window preferences were invalid; defaults are being used.",
        "Review the layout and make a deliberate change to replace the known layout fields.", true};
}

bool integer(const QJsonValue& value, int& result) {
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < std::numeric_limits<int>::min()
        || number > std::numeric_limits<int>::max()) return false;
    result = static_cast<int>(number);
    return true;
}

bool flag(const QJsonObject& layout, const char* key, bool& result) {
    const auto value = layout.value(QLatin1String(key));
    if (value.isUndefined()) return true;
    if (!value.isBool()) return false;
    result = value.toBool();
    return true;
}
}  // namespace

DecodedUiPreferences UiPreferencesCodec::decode(const QJsonObject& ui) {
    const auto value = ui.value("layout");
    if (value.isUndefined()) return {};
    const auto layout = value.toObject();
    const auto version = layout.value("version");
    if (version.isDouble() && std::isfinite(version.toDouble())
        && std::floor(version.toDouble()) == version.toDouble() && version.toDouble() > 1) {
        return {{}, false, core::Error{core::ErrorCategory::Configuration,
            "ui_preferences_future_version", "Window preferences use a newer version and will not be changed.",
            "The existing ui.layout object is preserved. Other preference sections remain independent.", true}};
    }
    if (!value.isObject() || !version.isDouble() || version.toDouble() != 1)
        return {{}, true, invalid()};
    application::UiPreferences preferences;
    if (!flag(layout, "panelsCollapsed", preferences.panelsCollapsed)
        || !flag(layout, "maximized", preferences.maximized)
        || !flag(layout, "fullscreen", preferences.fullscreen)
        || !flag(layout, "diagnosticsVisible", preferences.diagnosticsVisible))
        return {{}, true, invalid()};
    const auto geometryValue = layout.value("normalGeometry");
    if (!geometryValue.isUndefined() && !geometryValue.isNull()) {
        const auto geometry = geometryValue.toObject();
        application::WindowGeometry parsed;
        if (!geometryValue.isObject() || !integer(geometry.value("x"), parsed.x)
            || !integer(geometry.value("y"), parsed.y)
            || !integer(geometry.value("width"), parsed.width)
            || !integer(geometry.value("height"), parsed.height)) return {{}, true, invalid()};
        preferences.normalGeometry = parsed;
    }
    if (!validate(preferences).hasValue()) return {{}, true, invalid()};
    return {preferences, true, {}};
}

core::Result<void> UiPreferencesCodec::validate(const application::UiPreferences& preferences) {
    if (preferences.normalGeometry
        && (preferences.normalGeometry->width <= 0 || preferences.normalGeometry->height <= 0))
        return core::Result<void>::failure(invalid());
    return core::Result<void>::success();
}

core::Result<QJsonObject> UiPreferencesCodec::merge(
    const QJsonObject& ui, const application::UiPreferences& preferences) {
    const auto loaded = decode(ui);
    if (!loaded.writable) return core::Result<QJsonObject>::failure(*loaded.warning);
    const auto valid = validate(preferences);
    if (!valid.hasValue()) return core::Result<QJsonObject>::failure(valid.error());
    auto result = ui;
    auto layout = ui.value("layout").toObject();
    layout.insert("version", 1);
    layout.insert("panelsCollapsed", preferences.panelsCollapsed);
    layout.insert("maximized", preferences.maximized);
    layout.insert("fullscreen", preferences.fullscreen);
    layout.insert("diagnosticsVisible", preferences.diagnosticsVisible);
    if (preferences.normalGeometry) {
        auto geometry = layout.value("normalGeometry").toObject();
        geometry.insert("x", preferences.normalGeometry->x);
        geometry.insert("y", preferences.normalGeometry->y);
        geometry.insert("width", preferences.normalGeometry->width);
        geometry.insert("height", preferences.normalGeometry->height);
        layout.insert("normalGeometry", geometry);
    } else {
        layout.insert("normalGeometry", QJsonValue::Null);
    }
    result.insert("layout", layout);
    return core::Result<QJsonObject>::success(std::move(result));
}
}  // namespace lumora::configuration
