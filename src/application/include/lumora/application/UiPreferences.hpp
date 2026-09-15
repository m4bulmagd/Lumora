#pragma once

#include <optional>

namespace lumora::application {
struct WindowGeometry final {
    int x{}, y{}, width{}, height{};
    bool operator==(const WindowGeometry&) const = default;
};

struct UiPreferences final {
    std::optional<WindowGeometry> normalGeometry;
    bool panelsCollapsed{false};
    bool maximized{false};
    bool fullscreen{false};
    bool diagnosticsVisible{false};
    bool operator==(const UiPreferences&) const = default;
};
// An absent field retains the loaded value. Geometry's present empty optional
// deliberately clears a saved normal rectangle; present geometry replaces it.
struct UiPreferencesUpdate final {
    std::optional<std::optional<WindowGeometry>> normalGeometry;
    std::optional<bool> panelsCollapsed;
    std::optional<bool> maximized;
    std::optional<bool> fullscreen;
    std::optional<bool> diagnosticsVisible;
    bool operator==(const UiPreferencesUpdate&) const = default;
};
}  // namespace lumora::application
