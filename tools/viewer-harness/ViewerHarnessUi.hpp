#pragma once

#include <lumora/core/Error.hpp>

#include <QCoreApplication>
#include <QString>

namespace lumora::tools::ui_detail {

[[nodiscard]] inline QString viewerWindowTitle() {
    return QCoreApplication::translate(
        "LumoraViewerHarness", "Lumora Simulated Viewer");
}

[[nodiscard]] inline QString timeoutWindowTitle() {
    return QCoreApplication::translate(
        "LumoraViewerHarness",
        "Lumora Simulated Viewer — RETRIEVAL TIMEOUT OBSERVED");
}

[[nodiscard]] inline QString errorDialogTitle() {
    return QCoreApplication::translate(
        "LumoraViewerHarness", "Simulated viewer error");
}

[[nodiscard]] inline QString evaluationBanner() {
    return QCoreApplication::translate(
        "LumoraViewerHarness", "EVALUATION — NOT FOR CLINICAL USE");
}

[[nodiscard]] inline QString timeoutConsoleNotice() {
    return QCoreApplication::translate(
        "LumoraViewerHarness",
        "acquisition_timeout: transient retrieval timeout; feed continues");
}

[[nodiscard]] inline QString translatedOperatorSummary(
    const core::Error& error) {
    if (error.code == "viewer_slot_closed") {
        return QCoreApplication::translate(
            "LumoraViewerHarness",
            "The simulated frame could not be published.");
    }
    if (error.code == "simulator_worker_start_failed") {
        return QCoreApplication::translate(
            "LumoraViewerHarness",
            "The simulated viewer worker could not start.");
    }
    if (error.code == "simulator_not_found") {
        return QCoreApplication::translate(
            "LumoraViewerHarness",
            "The simulated viewer camera was not found.");
    }
    if (error.code == "simulator_worker_allocation_failed") {
        return QCoreApplication::translate(
            "LumoraViewerHarness",
            "The simulated viewer ran out of memory.");
    }
    if (error.code == "simulator_worker_exception") {
        return QCoreApplication::translate(
            "LumoraViewerHarness",
            "The simulated viewer worker stopped unexpectedly.");
    }
    return QCoreApplication::translate(
        "LumoraViewerHarness", "The simulated viewer encountered an error.");
}

}  // namespace lumora::tools::ui_detail
