#pragma once

#include <lumora/application/UiPreferences.hpp>
#include <lumora/core/Error.hpp>
#include <lumora/core/Result.hpp>
#include <QJsonObject>

namespace lumora::configuration {
struct DecodedUiPreferences final {
    application::UiPreferences preferences;
    bool writable{true};
    std::optional<core::Error> warning;
};

class UiPreferencesCodec final {
public:
    [[nodiscard]] static DecodedUiPreferences decode(const QJsonObject& ui);
    [[nodiscard]] static core::Result<void> validate(const application::UiPreferences&);
    [[nodiscard]] static core::Result<QJsonObject> merge(
        const QJsonObject& ui, const application::UiPreferences&);
};
}  // namespace lumora::configuration
