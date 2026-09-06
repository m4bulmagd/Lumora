#include "SimulatorFeed.hpp"
#include "ViewerHarnessUi.hpp"

#include <lumora/core/Clock.hpp>
#include <lumora/core/Frame.hpp>
#include <lumora/core/LatestValueSlot.hpp>
#include <lumora/ui/FramePresenter.hpp>
#include <lumora/ui/WorkstationView.hpp>

#include <QApplication>
#include <QMessageBox>
#include <QTimer>

#include <algorithm>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    const bool startFeed = std::any_of(
        argv + 1, argv + argc,
        [](const char* argument) { return std::string_view{argument} == "--start"; });

    std::cerr << lumora::tools::ui_detail::evaluationBanner().toStdString()
              << '\n';
    lumora::core::LatestValueSlot<lumora::core::FrameBundle> slot;
    lumora::core::SystemClock clock;
    lumora::ui::WorkstationView view;
    lumora::ui::FramePresenter presenter(slot, view, clock);
    lumora::tools::SimulatorFeed feed(slot, clock);
    view.setWindowTitle(lumora::tools::ui_detail::viewerWindowTitle());
    view.resize(1280, 800);
    view.show();
    presenter.start();
    if (startFeed) {
        feed.start();
    }

    QTimer errorMonitor;
    bool timeoutReported = false;
    QObject::connect(&errorMonitor, &QTimer::timeout, &view, [&] {
        const auto result = feed.result();
        if (result.hasValue()) {
            return;
        }
        if (lumora::tools::detail::isRecoverableAcquisitionTimeout(result.error())) {
            if (!timeoutReported) {
                timeoutReported = true;
                view.setWindowTitle(
                    lumora::tools::ui_detail::timeoutWindowTitle());
                std::cerr
                    << lumora::tools::ui_detail::timeoutConsoleNotice()
                           .toStdString()
                    << '\n';
            }
            return;
        }
        errorMonitor.stop();
        QMessageBox::critical(
            &view,
            lumora::tools::ui_detail::errorDialogTitle(),
            lumora::tools::ui_detail::translatedOperatorSummary(result.error()) +
                QStringLiteral("\n\n") +
                QString::fromStdString(result.error().diagnosticDetail));
    });
    if (startFeed) {
        errorMonitor.start(100);
    }

    const int exitCode = application.exec();
    presenter.stop();
    feed.stop();
    slot.close();
    const auto result = feed.result();
    if (!result.hasValue()) {
        std::cerr << result.error().code;
        if (lumora::tools::detail::isRecoverableAcquisitionTimeout(result.error())) {
            std::cerr << " count=" << feed.timeoutCount();
        }
        std::cerr << ": " << result.error().diagnosticDetail << '\n';
        if (!lumora::tools::detail::isRecoverableAcquisitionTimeout(result.error())) {
            return 1;
        }
    }
    return exitCode;
}
