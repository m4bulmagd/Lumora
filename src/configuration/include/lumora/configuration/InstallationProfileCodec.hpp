#pragma once
#include <lumora/application/InstallationProfiles.hpp>
#include <QByteArray>
namespace lumora::configuration {
class InstallationProfileCodec final {
public:
    [[nodiscard]] static core::Result<QByteArray> encode(
        const std::vector<application::InstallationCameraProfile>& profiles);
    [[nodiscard]] static core::Result<std::vector<application::InstallationCameraProfile>> decode(
        const QByteArray& bytes);
};
}
