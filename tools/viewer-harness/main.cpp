#include "SimulatorFeed.hpp"

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

    std::cerr << "EVALUATION — NOT FOR CLINICAL USE\n";
    lumora::core::LatestValueSlot<lumora::core::FrameBundle> slot;
    lumora::core::SystemClock clock;
    lumora::ui::WorkstationView view;
    lumora::ui::FramePresenter presenter(slot, view, clock);
    lumora::tools::SimulatorFeed feed(slot, clock);
    view.setWindowTitle(QStringLiteral("Lumora Simulated Viewer"));
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
                view.setWindowTitle(QStringLiteral(
                    "Lumora Simulated Viewer — RETRIEVAL TIMEOUT OBSERVED"));
                std::cerr << "acquisition_timeout: transient retrieval timeout; "
                             "feed continues\n";
            }
            return;
        }
        errorMonitor.stop();
        QMessageBox::critical(
            &view,
            QStringLiteral("Simulated viewer error"),
            QString::fromStdString(
                result.error().operatorSummary + "\n\n" +
                result.error().diagnosticDetail));
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
