#pragma once

#include <lumora/configuration/ApplicationConfiguration.hpp>
#include <lumora/core/Result.hpp>

#include <QByteArray>

namespace lumora::configuration {

class ConfigurationCodec final {
public:
    [[nodiscard]] static core::Result<ApplicationConfiguration> decode(
        const QByteArray& contents);
    [[nodiscard]] static core::Result<QByteArray> encode(
        const ApplicationConfiguration& configuration);
};

}  // namespace lumora::configuration
