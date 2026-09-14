#include "QmlWorkstation.hpp"
#include "SimulatorComposition.hpp"
#include <lumora/camera/sim/SimulatedCameraProvider.hpp>
#include <lumora/configuration/InstallationProfilesService.hpp>
#include <lumora/configuration/StartupPreferencesService.hpp>
#include <lumora/diagnostics/Logging.hpp>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QStandardPaths>
#include <cstdlib>
#include <filesystem>

namespace {
// Application quit requests follow the same retirement path as window Close.
// The event loop must continue delivering scene-graph retirement receipts.
class RetirementGate final : public QObject {
public:
    explicit RetirementGate(lumora::qml::QmlWorkstation& workstation) : workstation_(workstation) {}
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::Quit && !workstation_.closed()) {
            workstation_.requestShutdown();
            return true;
        }
        return false;
    }
private:
    lumora::qml::QmlWorkstation& workstation_;
};
std::filesystem::path logPath() {
    const auto path=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString()) / "Logs";
#else
    return std::filesystem::path(path.toUtf8().constData()) / "Logs";
#endif
}
}

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Lumora"));
    QCoreApplication::setApplicationName(QStringLiteral("LumoraQmlPilot"));
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Lumora evaluation SIM-LIVE QML pilot"));
    parser.addHelpOption();
    parser.process(application);

    const auto logging=lumora::diagnostics::Logging::start(logPath());
    if(!logging.hasValue()) {
        qCritical().noquote()<<QString::fromStdString(logging.error().diagnosticDetail);
        return EXIT_FAILURE;
    }
    int exitCode=EXIT_SUCCESS;
    {
        lumora::core::SystemClock clock;
        lumora::camera::sim::SimulatedCameraProvider provider(lumora::app::simulatorOptions(),clock);
        lumora::configuration::InstallationProfilesService installations({false,
            lumora::application::InstallationProfilePolicy::SimulatorIdentityFallback});
        lumora::processing::ProcessingPreparationOptions options;
        const auto reserved=lumora::qml::reserveSimulatorRendererStorage(options);
        if(!reserved.hasValue()) {
            qCritical().noquote()<<QString::fromStdString(reserved.error().diagnosticDetail);
            lumora::diagnostics::Logging::shutdown();
            return EXIT_FAILURE;
        }
        lumora::application::LivePipeline pipeline(provider,clock,lumora::app::simulatorConfiguration(),{},options,&installations);
        lumora::configuration::StartupPreferencesService preferences{lumora::configuration::ConfigurationStore{}};
        lumora::qml::QmlWorkstation workstation(pipeline,preferences,clock,lumora::app::simulatorConfiguration());
        RetirementGate gate(workstation);
        application.installEventFilter(&gate);
        QQmlApplicationEngine engine;
        QObject::connect(&workstation,&lumora::qml::QmlWorkstation::shutdownComplete,&application,
            [&]{ application.exit(exitCode); },Qt::QueuedConnection);
        QObject::connect(&engine,&QQmlApplicationEngine::objectCreationFailed,&workstation,[&] {
            exitCode=EXIT_FAILURE;
            workstation.requestShutdown();
        });
        const auto failed=[&](const auto& result) {
            if(result.hasValue()) return false;
            exitCode=EXIT_FAILURE;
            qCritical().noquote()<<QString::fromStdString(result.error().diagnosticDetail);
            return true;
        };
        if(failed(installations.start()) || failed(preferences.start()) || failed(pipeline.start()) || failed(workstation.start())) {
            workstation.requestShutdown();
        } else {
            engine.setInitialProperties({{"workstation",QVariant::fromValue(&workstation)},
                {"camera",QVariant::fromValue(workstation.camera())},
                {"processing",QVariant::fromValue(workstation.processing())},
                {"viewer",QVariant::fromValue(workstation.viewer())}});
            engine.loadFromModule(QStringLiteral("Lumora.Workstation"),QStringLiteral("Main"));
            for(auto* root:engine.rootObjects())
                QObject::connect(root,&QObject::destroyed,&workstation,&lumora::qml::QmlWorkstation::requestShutdown);
        }
        exitCode=application.exec();
        preferences.requestStop();preferences.join();
        installations.requestStop();installations.join();
        application.removeEventFilter(&gate);
        if(const auto status=preferences.latestStatus();status && status->warning)
            qWarning().noquote()<<QString::fromStdString(status->warning->operatorSummary);
    }
    lumora::diagnostics::Logging::shutdown();
    return exitCode;
}
