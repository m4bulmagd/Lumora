#include <lumora/diagnostics/Logging.hpp>

#include <lumora/core/AppVersion.hpp>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <exception>
#include <memory>
#include <string>
#include <system_error>

namespace lumora::diagnostics {
namespace {

constexpr auto loggerName = "lumora";
constexpr std::size_t maximumLogFileSize = 10U * 1024U * 1024U;
constexpr std::size_t maximumLogFileCount = 5U;

[[nodiscard]] core::Error loggingError(
    std::string code,
    std::string detail) {
    return core::Error{
        core::ErrorCategory::Storage,
        std::move(code),
        "Unable to start application logging",
        std::move(detail),
        false,
    };
}

}  // namespace

core::Result<void> Logging::start(const std::filesystem::path& directory) {
    if (spdlog::get(loggerName)) {
        return core::Result<void>::failure(loggingError(
            "logging_already_started",
            "The Lumora logger is already registered."));
    }

    std::error_code directoryError;
    std::filesystem::create_directories(directory, directoryError);
    std::error_code statusError;
    const auto isDirectory = std::filesystem::is_directory(directory, statusError);
    if (directoryError || statusError || !isDirectory) {
        std::string detail = "The requested log path is not a directory.";
        if (directoryError) {
            detail = directoryError.message();
        } else if (statusError) {
            detail = statusError.message();
        }
        return core::Result<void>::failure(
            loggingError("log_directory_unavailable", detail));
    }

    try {
        const auto logPath = directory / "lumora.log";
        auto logger = spdlog::rotating_logger_mt(
            loggerName,
            logPath.string(),
            maximumLogFileSize,
            maximumLogFileCount);
        logger->set_pattern("[%Y-%m-%dT%H:%M:%S.%e%z] [%l] %v");
        logger->info(
            "application_started version={} release_class={}",
            core::AppVersion::string(),
            core::AppVersion::releaseClass());
        logger->flush();
        return core::Result<void>::success();
    } catch (const std::exception& exception) {
        spdlog::drop(loggerName);
        return core::Result<void>::failure(
            loggingError("logger_initialization_failed", exception.what()));
    }
}

void Logging::shutdown() noexcept {
    try {
        if (const auto logger = spdlog::get(loggerName)) {
            logger->info("application_stopped");
            logger->flush();
            spdlog::drop(loggerName);
        }
    } catch (...) {
        spdlog::drop(loggerName);
    }
}

}  // namespace lumora::diagnostics
