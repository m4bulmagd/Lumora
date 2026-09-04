#include <lumora/core/AppVersion.hpp>
#include <lumora/diagnostics/Logging.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace {

class TempDirectory final {
public:
    TempDirectory()
        : path_(std::filesystem::temp_directory_path()
                / ("lumora-logging-"
                   + std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count()))) {
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::string readLogFiles(const std::filesystem::path& directory) {
    std::ostringstream contents;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        std::ifstream input(entry.path(), std::ios::binary);
        contents << input.rdbuf();
    }
    return contents.str();
}

TEST(Logging, WritesVersionedStartupAndShutdownEvents) {
    TempDirectory temp;

    ASSERT_TRUE(lumora::diagnostics::Logging::start(temp.path()).hasValue());
    lumora::diagnostics::Logging::shutdown();

    const auto text = readLogFiles(temp.path());
    EXPECT_NE(text.find("application_started"), std::string::npos);
    EXPECT_NE(text.find("version=0.1.0"), std::string::npos);
    EXPECT_NE(text.find("release_class=EVALUATION"), std::string::npos);
    EXPECT_NE(text.find("application_stopped"), std::string::npos);
}

TEST(Logging, ReportsAnUnusableLogDirectory) {
    TempDirectory temp;
    const auto regularFile = temp.path() / "not-a-directory";
    std::ofstream(regularFile) << "occupied";

    const auto result = lumora::diagnostics::Logging::start(regularFile / "logs");

    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, "log_directory_unavailable");
    lumora::diagnostics::Logging::shutdown();
}

}  // namespace
