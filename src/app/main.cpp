#include <lumora/diagnostics/Logging.hpp>
#include "SimulatorComposition.hpp"
#include <lumora/application/LivePipeline.hpp>
#include <lumora/camera/sim/SimulatedCameraProvider.hpp>
#include <lumora/configuration/StartupPreferencesService.hpp>
#include <lumora/ui/WorkstationController.hpp>
#include <lumora/ui/MainWindow.hpp>

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
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
        return nativePath(QDir::currentPath()) / "Logs";
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
        lumora::core::SystemClock clock;
        lumora::camera::sim::SimulatedCameraProvider provider(lumora::app::simulatorOptions(),clock);
        lumora::application::LivePipeline pipeline(provider,clock,lumora::app::simulatorConfiguration());
        lumora::configuration::StartupPreferencesService preferences{lumora::configuration::ConfigurationStore{}};
        lumora::ui::MainWindow window;
        lumora::ui::WorkstationController controller(pipeline,preferences,window.workstationView(),
            window.cameraStartupPanel(),clock,lumora::app::simulatorConfiguration());
        const auto reportFailure=[](const auto& result) {
            if(result.hasValue()) return false;
            qCritical().noquote()<<QString::fromStdString(result.error().diagnosticDetail);
            return true;
        };
        if(reportFailure(preferences.start()) || reportFailure(pipeline.start()) || reportFailure(controller.start()))
            exitCode=EXIT_FAILURE;
        else { window.show();exitCode=application.exec(); }
        controller.shutdown();
        preferences.requestStop();preferences.join();
        if(const auto status=preferences.latestStatus();status && status->warning)
            qWarning().noquote()<<QString::fromStdString(status->warning->operatorSummary);
    }

    lumora::diagnostics::Logging::shutdown();
    return exitCode;
}
