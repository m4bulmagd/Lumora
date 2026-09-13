#include "ProcessingAdapter.hpp"

#include <QLocale>
#include <QStringList>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <type_traits>
#include <utility>

namespace lumora::qml {
namespace {
using Phase = presentation::ProcessingEditPhase;
using WindowLevel = processing::WindowLevelParameters;
using StageId = processing::StageId;

QString number(double value) {
    return QLocale::c().toString(value, 'g', QLocale::FloatingPointShortest);
}

QString summary(const std::optional<core::Error>& error) {
    return error ? QString::fromStdString(error->operatorSummary) : QString{};
}

auto windowLevelStage(processing::PipelineDefinition& pipeline) {
    return std::find_if(pipeline.stages.begin(), pipeline.stages.end(), [](const auto& stage) {
        return stage.id == StageId::WindowLevel
            && std::holds_alternative<WindowLevel>(stage.parameters);
    });
}

QString presetName(const application::PresetState& state,
    const presentation::ProcessingControlsModel& model) {
    const auto& id = state.selectedId.value;
    if (id == "original") return ProcessingAdapter::tr("Original");
    if (id == "standard") return ProcessingAdapter::tr("Standard");
    if (id == "high-contrast") return ProcessingAdapter::tr("High Contrast");
    if (id == "soft-detail") return ProcessingAdapter::tr("Soft Detail");
    if (id == "custom") return ProcessingAdapter::tr("Custom");
    for (const auto& preset : model.presets())
        if (preset.id == state.selectedId) return QString::fromStdString(preset.name);
    return QString::fromStdString(id);
}

QString stageLabel(StageId stage) {
    switch (stage) {
    case StageId::Normalize: return ProcessingAdapter::tr("Normalize");
    case StageId::WindowLevel: return ProcessingAdapter::tr("Window/level");
    case StageId::BrightnessContrast: return ProcessingAdapter::tr("Brightness/contrast");
    case StageId::Gamma: return ProcessingAdapter::tr("Gamma");
    case StageId::Clahe: return ProcessingAdapter::tr("Local contrast");
    case StageId::Denoise: return ProcessingAdapter::tr("Denoise");
    case StageId::Sharpen: return ProcessingAdapter::tr("Sharpen");
    case StageId::Invert: return ProcessingAdapter::tr("Invert");
    }
    return {};
}

QString stageDescription(const processing::StageDefinition& stage) {
    auto text = stageLabel(stage.id) + QStringLiteral(": ")
        + (stage.enabled ? ProcessingAdapter::tr("on") : ProcessingAdapter::tr("off"));
    // Window/level remains meaningful on Original even when disabled for the
    // Enhanced route. Its exact active values must remain visible in either case.
    if (!stage.enabled && stage.id != StageId::WindowLevel) return text;
    const auto parameters = std::visit([](const auto& value) -> QString {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, WindowLevel>)
            return ProcessingAdapter::tr("Window %1, level %2").arg(number(value.window), number(value.level));
        else if constexpr (std::is_same_v<T, processing::BrightnessContrastParameters>)
            return ProcessingAdapter::tr("Brightness %1, contrast %2").arg(number(value.brightness), number(value.contrast));
        else if constexpr (std::is_same_v<T, processing::GammaParameters>)
            return number(value.gamma);
        else if constexpr (std::is_same_v<T, processing::ClaheParameters>)
            return ProcessingAdapter::tr("Limit %1, grid %2").arg(number(value.clipLimit)).arg(value.tileGridSize);
        else if constexpr (std::is_same_v<T, processing::DenoiseParameters>)
            return ProcessingAdapter::tr("%1, kernel %2, sigma %3")
                .arg(value.mode == processing::DenoiseMode::Gaussian
                    ? ProcessingAdapter::tr("Gaussian") : ProcessingAdapter::tr("Median"))
                .arg(value.kernelSize).arg(number(value.sigma));
        else if constexpr (std::is_same_v<T, processing::SharpenParameters>)
            return ProcessingAdapter::tr("Amount %1, radius %2, threshold %3")
                .arg(number(value.amount), number(value.radius), number(value.threshold));
        else return {};
    }, stage.parameters);
    if (!parameters.isEmpty()) text += QStringLiteral(" · ") + parameters;
    return text;
}

QString activeDescription(const application::PresetState& state,
    const presentation::ProcessingControlsModel& model) {
    QStringList lines{presetName(state, model)};
    for (const auto& stage : state.activePipeline.stages)
        lines.push_back(stageDescription(stage));
    return lines.join(QLatin1Char('\n'));
}
} // namespace

ProcessingAdapter::ProcessingAdapter(presentation::WorkstationCoordinator& coordinator, QObject* parent)
    : QObject(parent), coordinator_(coordinator) {
    refresh();
}

void ProcessingAdapter::refresh() {
    State next;
    const auto& processing = coordinator_.processingState();
    const auto& workstation = coordinator_.state();
    auto* model = coordinator_.processingControls();
    next.loadingState = !processing.loadCompleted ? Loading : (model ? Ready : Unavailable);
    next.loadError = summary(processing.loadError);
    next.persistenceWarning = summary(processing.persistenceWarning);
    const auto& processor = processing.processorStatus;
    next.fallback = processor.mode == processing::ProcessorMode::OriginalOnlyLatched;
    next.retryPending = processing.retryPending || processor.retryPending;
    next.retryEnabled = workstation.controlsEnabled && workstation.contextBound
        && next.fallback && processor.retrySupported && !next.retryPending;
    next.processingError = processor.error
        ? QString::fromStdString(processor.error->operatorSummary) : retryError_;
    if (model) {
        auto draft = model->draft();
        const auto stage = windowLevelStage(draft.activePipeline);
        next.available = workstation.controlsEnabled && stage != draft.activePipeline.stages.end();
        if (stage != draft.activePipeline.stages.end()) {
            const auto& parameters = std::get<WindowLevel>(stage->parameters);
            next.stageEnabled = stage->enabled;
            next.window = parameters.window;
            next.level = parameters.level;
            next.windowText = number(parameters.window);
            next.levelText = number(parameters.level);
        }
        next.draftPresetName = presetName(draft, *model);
        next.pending = model->pending();
        next.modelError = summary(model->error());
        if (const auto& accepted = model->acknowledged()) {
            next.hasAcknowledged = true;
            next.activeSummary = activeDescription(*accepted, *model);
            next.activeRevision = QString::number(
                static_cast<qulonglong>(accepted->activePipeline.version.configurationRevision));
        }
    }
    if (next.available && !state_.available) inputError_.clear();
    next.validationError = inputError_;
    if (next == state_) return;
    state_ = std::move(next);
    emit stateChanged();
}

bool ProcessingAdapter::rejectInput(const QString& message) {
    inputError_ = message;
    refresh();
    return false;
}

bool ProcessingAdapter::setStageEnabled(bool enabled) {
    auto* model = coordinator_.processingControls();
    if (!model || !coordinator_.state().controlsEnabled)
        return rejectInput(tr("Processing controls are unavailable."));
    auto pipeline = model->draft().activePipeline;
    const auto stage = windowLevelStage(pipeline);
    if (stage == pipeline.stages.end())
        return rejectInput(tr("Window/level settings are unavailable."));
    stage->enabled = enabled;
    inputError_.clear();
    const auto result = model->edit(std::move(pipeline), Phase::Commit);
    refresh();
    return result.hasValue();
}

bool ProcessingAdapter::editValue(double WindowLevel::* member, double value, Phase phase) {
    auto* model = coordinator_.processingControls();
    if (!model || !coordinator_.state().controlsEnabled)
        return rejectInput(tr("Processing controls are unavailable."));
    const bool isWindow = member == &WindowLevel::window;
    const auto minimum = isWindow ? windowMinimum() : levelMinimum();
    const auto maximum = isWindow ? windowMaximum() : levelMaximum();
    if (!std::isfinite(value) || value < minimum || value > maximum)
        return rejectInput(tr("%1 must be a finite number from %2 to %3.")
            .arg(isWindow ? tr("Window") : tr("Level"), number(minimum), number(maximum)));
    auto pipeline = model->draft().activePipeline;
    const auto stage = windowLevelStage(pipeline);
    if (stage == pipeline.stages.end())
        return rejectInput(tr("Window/level settings are unavailable."));
    std::get<WindowLevel>(stage->parameters).*member = value;
    inputError_.clear();
    const auto result = model->edit(std::move(pipeline), phase);
    refresh();
    return result.hasValue();
}

bool ProcessingAdapter::commitText(double WindowLevel::* member, const QString& text) {
    const auto bytes = text.trimmed().toUtf8();
    const char* begin = bytes.constData();
    const char* end = begin + bytes.size();
    if (begin != end && *begin == '+') ++begin;
    double value{};
    const auto parsed = std::from_chars(begin, end, value, std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != end)
        return rejectInput(tr("Enter a decimal number using a dot as the decimal separator."));
    return editValue(member, value, Phase::Commit);
}

bool ProcessingAdapter::commitWindow(double value) { return editValue(&WindowLevel::window, value, Phase::Commit); }
bool ProcessingAdapter::commitLevel(double value) { return editValue(&WindowLevel::level, value, Phase::Commit); }
bool ProcessingAdapter::commitWindowText(const QString& text) { return commitText(&WindowLevel::window, text); }
bool ProcessingAdapter::commitLevelText(const QString& text) { return commitText(&WindowLevel::level, text); }
bool ProcessingAdapter::dragWindow(double value) { return editValue(&WindowLevel::window, value, Phase::Drag); }
bool ProcessingAdapter::dragLevel(double value) { return editValue(&WindowLevel::level, value, Phase::Drag); }
bool ProcessingAdapter::releaseWindow(double value) { return editValue(&WindowLevel::window, value, Phase::Release); }
bool ProcessingAdapter::releaseLevel(double value) { return editValue(&WindowLevel::level, value, Phase::Release); }

bool ProcessingAdapter::retry() {
    refresh();
    if (!state_.retryEnabled) {
        retryError_ = tr("Processing retry is unavailable.");
        refresh();
        return false;
    }
    const auto result = coordinator_.retryProcessing();
    retryError_ = result.hasValue() ? QString{} : QString::fromStdString(result.error().operatorSummary);
    refresh();
    return result.hasValue();
}
} // namespace lumora::qml
