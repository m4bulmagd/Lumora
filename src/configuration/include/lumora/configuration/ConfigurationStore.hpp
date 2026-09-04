#pragma once

#include <lumora/configuration/ApplicationConfiguration.hpp>
#include <lumora/core/Result.hpp>

#include <filesystem>

namespace lumora::configuration {

class ConfigurationStore final {
public:
    ConfigurationStore();
    explicit ConfigurationStore(std::filesystem::path configurationPath);

    [[nodiscard]] core::Result<ApplicationConfiguration> load() const;
    [[nodiscard]] core::Result<void> save(
        const ApplicationConfiguration& configuration) const;
    [[nodiscard]] const std::filesystem::path& path() const noexcept;

    [[nodiscard]] static std::filesystem::path defaultConfigurationPath();

private:
    std::filesystem::path configurationPath_;
};

}  // namespace lumora::configuration
