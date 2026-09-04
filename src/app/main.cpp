#include <lumora/diagnostics/Logging.hpp>
#include <lumora/ui/MainWindow.hpp>

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QStandardPaths>
#include <QString>

#include <cstdlib>
#include <filesystem>

namespace {

[[nodiscard]] std::filesystem::path nativePath(const QString& path) {
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    const QByteArray utf8Path = path.toUtf8();
    return std::filesystem::path(utf8Path.constData());
#endif
}

[[nodiscard]] std::filesystem::path applicationLogDirectory() {
    const auto applicationData =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (applicationData.isEmpty()) {
        return std::filesystem::current_path() / "Logs";
    }
    return nativePath(applicationData) / "Logs";
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Lumora"));
    QCoreApplication::setApplicationName(QStringLiteral("Lumora"));

    const auto loggingResult =
        lumora::diagnostics::Logging::start(applicationLogDirectory());
    if (!loggingResult.hasValue()) {
        qCritical().noquote()
            << QString::fromStdString(loggingResult.error().diagnosticDetail);
        return EXIT_FAILURE;
    }

    int exitCode = EXIT_SUCCESS;
    {
        lumora::ui::MainWindow window;
        window.show();
        exitCode = application.exec();
    }

    lumora::diagnostics::Logging::shutdown();
    return exitCode;
}
