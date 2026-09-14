#include "ProcessingAdapter.hpp"

#include <QLocale>
#include <QStringList>
#include <QVariantMap>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <type_traits>
#include <utility>

namespace lumora::qml {
namespace {
using Phase = presentation::ProcessingEditPhase;
using WindowLevel = processing::WindowLevelParameters;
using BrightnessContrast = processing::BrightnessContrastParameters;
using Gamma = processing::GammaParameters;
using Clahe = processing::ClaheParameters;
using Denoise = processing::DenoiseParameters;
using DenoiseMode = processing::DenoiseMode;
using StageId = processing::StageId;

QString number(double value) {
    return QLocale::c().toString(value, 'g', QLocale::FloatingPointShortest);
}

QString denoiseModeName(DenoiseMode mode) {
    return mode == DenoiseMode::Gaussian
        ? QStringLiteral("gaussian") : QStringLiteral("median");
}

QVariantList makeDenoiseKernelOptions(DenoiseMode mode) {
    QVariantList options{
        QVariantMap{{QStringLiteral("name"), QStringLiteral("3")}, {QStringLiteral("value"), 3}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("5")}, {QStringLiteral("value"), 5}}};
    if (mode == DenoiseMode::Gaussian)
        options.push_back(QVariantMap{
            {QStringLiteral("name"), QStringLiteral("7")}, {QStringLiteral("value"), 7}});
    return options;
}

QString summary(const std::optional<core::Error>& error) {
    return error ? QString::fromStdString(error->operatorSummary) : QString{};
}

template<typename Parameters>
auto findStage(processing::PipelineDefinition& pipeline, StageId id) {
    return std::find_if(pipeline.stages.begin(), pipeline.stages.end(), [id](const auto& stage) {
        return stage.id == id && std::holds_alternative<Parameters>(stage.parameters);
    });
}

QString presetName(const application::PresetId& presetId, const QString& fallback) {
    const auto& id = presetId.value;
    if (id == "original") return ProcessingAdapter::tr("Original");
    if (id == "standard") return ProcessingAdapter::tr("Standard");
    if (id == "high-contrast") return ProcessingAdapter::tr("High Contrast");
    if (id == "soft-detail") return ProcessingAdapter::tr("Soft Detail");
    if (id == "custom") return ProcessingAdapter::tr("Custom");
    return fallback;
}

QString presetName(const application::PresetState& state,
    const presentation::ProcessingControlsModel& model) {
    for (const auto& preset : model.presets())
        if (preset.id == state.selectedId)
            return presetName(preset.id, QString::fromStdString(preset.name));
    return presetName(state.selectedId, QString::fromStdString(state.selectedId.value));
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

void ProcessingAdapter::refresh() { refreshState(false); }

void ProcessingAdapter::refreshState(bool draftWasReplaced) {
    State next;
    QVariantList nextPresets;
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
    retryContext_ = {
        workstation.cameraStatus ? workstation.cameraStatus->sessionGeneration : 0U,
        processor.activationSerial, workstation.contextBound,
        model && workstation.controlsEnabled && workstation.contextBound
            && !next.fallback && !next.retryPending && !processor.error};
    // A later healthy session/activation/recovery supersedes the failed attempt.
    // An unchanged refresh must still retain an immediately rejected command.
    if (retryContext_.healthy && retryErrorContext_ && retryContext_ != *retryErrorContext_) {
        retryError_.clear();
        retryErrorContext_.reset();
    }
    next.processingError = processor.error
        ? QString::fromStdString(processor.error->operatorSummary) : retryError_;
    if (model) {
        for (const auto& preset : model->presets()) {
            nextPresets.push_back(QVariantMap{
                {QStringLiteral("id"), QString::fromStdString(preset.id.value)},
                {QStringLiteral("name"), presetName(preset.id, QString::fromStdString(preset.name))},
                {QStringLiteral("description"), QString::fromStdString(preset.description)}});
        }
        auto draft = model->draft();
        next.selectedPresetId = QString::fromStdString(draft.selectedId.value);
        const auto stage = findStage<WindowLevel>(draft.activePipeline, StageId::WindowLevel);
        next.available = workstation.controlsEnabled && stage != draft.activePipeline.stages.end();
        if (stage != draft.activePipeline.stages.end()) {
            const auto& parameters = std::get<WindowLevel>(stage->parameters);
            next.stageEnabled = stage->enabled;
            next.window = parameters.window;
            next.level = parameters.level;
            next.windowText = number(parameters.window);
            next.levelText = number(parameters.level);
        }
        const auto toneStage = findStage<BrightnessContrast>(draft.activePipeline, StageId::BrightnessContrast);
        if (toneStage != draft.activePipeline.stages.end()) {
            const auto& parameters = std::get<BrightnessContrast>(toneStage->parameters);
            next.brightnessContrastEnabled = toneStage->enabled;
            next.brightness = parameters.brightness;
            next.contrast = parameters.contrast;
            next.brightnessText = number(parameters.brightness);
            next.contrastText = number(parameters.contrast);
        }
        const auto gammaStage = findStage<Gamma>(draft.activePipeline, StageId::Gamma);
        if (gammaStage != draft.activePipeline.stages.end()) {
            const auto& parameters = std::get<Gamma>(gammaStage->parameters);
            next.gammaEnabled = gammaStage->enabled;
            next.gamma = parameters.gamma;
            next.gammaText = number(parameters.gamma);
        }
        const auto localContrastStage = findStage<Clahe>(draft.activePipeline, StageId::Clahe);
        if (localContrastStage != draft.activePipeline.stages.end()) {
            const auto& parameters = std::get<Clahe>(localContrastStage->parameters);
            next.localContrastEnabled = localContrastStage->enabled;
            next.clipLimit = parameters.clipLimit;
            next.clipLimitText = number(parameters.clipLimit);
            next.tileGridSize = static_cast<int>(parameters.tileGridSize);
            next.tileGridSizeText = QString::number(parameters.tileGridSize);
        }
        const auto denoiseStage = findStage<Denoise>(draft.activePipeline, StageId::Denoise);
        if (denoiseStage != draft.activePipeline.stages.end()) {
            const auto& parameters = std::get<Denoise>(denoiseStage->parameters);
            next.denoiseEnabled = denoiseStage->enabled;
            next.denoiseMode = denoiseModeName(parameters.mode);
            next.denoiseKernelSize = static_cast<int>(parameters.kernelSize);
            next.denoiseKernelOptions = makeDenoiseKernelOptions(parameters.mode);
            next.denoiseSigma = parameters.sigma;
            next.denoiseSigmaText = number(parameters.sigma);
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
    const bool listChanged = nextPresets != presets_;
    if (listChanged) presets_ = std::move(nextPresets);
    const bool changed = next != state_;
    if (changed) state_ = std::move(next);
    // Cancel uncommitted editor text before changed enable flags can cause a
    // focus-loss commit. Observers already see the complete replacement snapshot.
    if (draftWasReplaced) emit draftReplaced();
    if (changed) emit stateChanged();
    if (listChanged) emit presetsChanged();
}

bool ProcessingAdapter::rejectInput(const QString& message) {
    inputError_ = message;
    refresh();
    return false;
}

bool ProcessingAdapter::selectPreset(const QString& id) {
    auto* model = coordinator_.processingControls();
    if (!model || !coordinator_.state().controlsEnabled)
        return rejectInput(tr("Processing controls are unavailable."));
    const auto result = model->selectPreset({id.toStdString()});
    if (result.hasValue()) inputError_.clear();
    refreshState(result.hasValue());
    return result.hasValue();
}

bool ProcessingAdapter::resetProcessing() {
    auto* model = coordinator_.processingControls();
    if (!model || !coordinator_.state().controlsEnabled)
        return rejectInput(tr("Processing controls are unavailable."));
    const auto result = model->reset();
    if (result.hasValue()) inputError_.clear();
    refreshState(result.hasValue());
    return result.hasValue();
}

template<typename Parameters>
bool ProcessingAdapter::setEnabled(StageId id, bool enabled) {
    auto* model = coordinator_.processingControls();
    if (!model || !coordinator_.state().controlsEnabled)
        return rejectInput(tr("Processing controls are unavailable."));
    auto pipeline = model->draft().activePipeline;
    const auto stage = findStage<Parameters>(pipeline, id);
    if (stage == pipeline.stages.end())
        return rejectInput(tr("%1 settings are unavailable.").arg(stageLabel(id)));
    stage->enabled = enabled;
    inputError_.clear();
    const auto result = model->edit(std::move(pipeline), Phase::Commit);
    refresh();
    return result.hasValue();
}

bool ProcessingAdapter::setStageEnabled(bool enabled) {
    return setEnabled<WindowLevel>(StageId::WindowLevel, enabled);
}

bool ProcessingAdapter::setBrightnessContrastEnabled(bool enabled) {
    return setEnabled<BrightnessContrast>(StageId::BrightnessContrast, enabled);
}

bool ProcessingAdapter::setGammaEnabled(bool enabled) {
    return setEnabled<Gamma>(StageId::Gamma, enabled);
}

bool ProcessingAdapter::setLocalContrastEnabled(bool enabled) {
    return setEnabled<Clahe>(StageId::Clahe, enabled);
}

bool ProcessingAdapter::setDenoiseEnabled(bool enabled) {
    return setEnabled<Denoise>(StageId::Denoise, enabled);
}

template<typename Parameters>
bool ProcessingAdapter::editStageValue(StageId id, double Parameters::* member, double value,
    double minimum, double maximum, const QString& label, Phase phase) {
    auto* model = coordinator_.processingControls();
    if (!model || !coordinator_.state().controlsEnabled)
        return rejectInput(tr("Processing controls are unavailable."));
    if (!std::isfinite(value) || value < minimum || value > maximum)
        return rejectInput(tr("%1 must be a finite number from %2 to %3.")
            .arg(label, number(minimum), number(maximum)));
    auto pipeline = model->draft().activePipeline;
    const auto stage = findStage<Parameters>(pipeline, id);
    if (stage == pipeline.stages.end())
        return rejectInput(tr("%1 settings are unavailable.").arg(stageLabel(id)));
    std::get<Parameters>(stage->parameters).*member = value;
    inputError_.clear();
    const auto result = model->edit(std::move(pipeline), phase);
    refresh();
    return result.hasValue();
}

bool ProcessingAdapter::editValue(double WindowLevel::* member, double value, Phase phase) {
    const bool isWindow = member == &WindowLevel::window;
    return editStageValue(StageId::WindowLevel, member, value,
        isWindow ? windowMinimum() : levelMinimum(), isWindow ? windowMaximum() : levelMaximum(),
        isWindow ? tr("Window") : tr("Level"), phase);
}

bool ProcessingAdapter::editValue(double BrightnessContrast::* member, double value, Phase phase) {
    const bool isBrightness = member == &BrightnessContrast::brightness;
    return editStageValue(StageId::BrightnessContrast, member, value,
        isBrightness ? brightnessMinimum() : contrastMinimum(),
        isBrightness ? brightnessMaximum() : contrastMaximum(),
        isBrightness ? tr("Brightness") : tr("Contrast"), phase);
}

bool ProcessingAdapter::editValue(double Gamma::* member, double value, Phase phase) {
    return editStageValue(StageId::Gamma, member, value, gammaMinimum(), gammaMaximum(), tr("Gamma"), phase);
}

bool ProcessingAdapter::editValue(double Clahe::* member, double value, Phase phase) {
    return editStageValue(StageId::Clahe, member, value,
        clipLimitMinimum(), clipLimitMaximum(), tr("Clip limit"), phase);
}

bool ProcessingAdapter::editValue(double Denoise::* member, double value, Phase phase) {
    auto* model = coordinator_.processingControls();
    if (!model || !coordinator_.state().controlsEnabled)
        return rejectInput(tr("Processing controls are unavailable."));
    if (!std::isfinite(value) || value < denoiseSigmaMinimum() || value > denoiseSigmaMaximum())
        return rejectInput(tr("Sigma must be a finite number from %1 to %2.")
            .arg(number(denoiseSigmaMinimum()), number(denoiseSigmaMaximum())));
    auto pipeline = model->draft().activePipeline;
    const auto stage = findStage<Denoise>(pipeline, StageId::Denoise);
    if (stage == pipeline.stages.end())
        return rejectInput(tr("%1 settings are unavailable.").arg(stageLabel(StageId::Denoise)));
    auto& parameters = std::get<Denoise>(stage->parameters);
    if (parameters.mode != DenoiseMode::Gaussian)
        return rejectInput(tr("Sigma is available only in Gaussian mode."));
    parameters.*member = value;
    inputError_.clear();
    const auto result = model->edit(std::move(pipeline), phase);
    refresh();
    return result.hasValue();
}

bool ProcessingAdapter::editTileGridSize(double value) {
    auto* model = coordinator_.processingControls();
    if (!model || !coordinator_.state().controlsEnabled)
        return rejectInput(tr("Processing controls are unavailable."));
    if (!std::isfinite(value) || std::trunc(value) != value
        || value < tileGridSizeMinimum() || value > tileGridSizeMaximum())
        return rejectInput(tr("Tile grid size must be a whole number from %1 to %2.")
            .arg(tileGridSizeMinimum()).arg(tileGridSizeMaximum()));
    auto pipeline = model->draft().activePipeline;
    const auto stage = findStage<Clahe>(pipeline, StageId::Clahe);
    if (stage == pipeline.stages.end())
        return rejectInput(tr("%1 settings are unavailable.").arg(stageLabel(StageId::Clahe)));
    std::get<Clahe>(stage->parameters).tileGridSize = static_cast<std::uint32_t>(value);
    inputError_.clear();
    const auto result = model->edit(std::move(pipeline), Phase::Commit);
    refresh();
    return result.hasValue();
}

template<typename Parameters>
bool ProcessingAdapter::commitText(double Parameters::* member, const QString& text) {
    const auto bytes = text.trimmed().toUtf8();
    const char* begin = bytes.constData();
    const char* end = begin + bytes.size();
    if (begin != end && *begin == '+') {
        ++begin;
        if (begin != end && (*begin == '+' || *begin == '-'))
            return rejectInput(tr("Enter a decimal number with at most one leading sign."));
    }
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

template<typename Parameters>
bool ProcessingAdapter::releaseStage(StageId id) {
    auto* model = coordinator_.processingControls();
    if (!model || !coordinator_.state().controlsEnabled)
        return rejectInput(tr("Processing controls are unavailable."));
    auto pipeline = model->draft().activePipeline;
    if (findStage<Parameters>(pipeline, id) == pipeline.stages.end())
        return rejectInput(tr("%1 settings are unavailable.").arg(stageLabel(id)));
    inputError_.clear();
    // A preset/reset may have replaced the drag. Flush only the model's current
    // whole draft, preserving its preset identity and exact stored values.
    const auto result = model->edit(std::move(pipeline), Phase::Release);
    refresh();
    return result.hasValue();
}

bool ProcessingAdapter::commitBrightness(double value) { return editValue(&BrightnessContrast::brightness, value, Phase::Commit); }
bool ProcessingAdapter::commitContrast(double value) { return editValue(&BrightnessContrast::contrast, value, Phase::Commit); }
bool ProcessingAdapter::commitGamma(double value) { return editValue(&Gamma::gamma, value, Phase::Commit); }
bool ProcessingAdapter::commitBrightnessText(const QString& text) { return commitText(&BrightnessContrast::brightness, text); }
bool ProcessingAdapter::commitContrastText(const QString& text) { return commitText(&BrightnessContrast::contrast, text); }
bool ProcessingAdapter::commitGammaText(const QString& text) { return commitText(&Gamma::gamma, text); }
bool ProcessingAdapter::dragBrightness(double value) { return editValue(&BrightnessContrast::brightness, value, Phase::Drag); }
bool ProcessingAdapter::dragContrast(double value) { return editValue(&BrightnessContrast::contrast, value, Phase::Drag); }
bool ProcessingAdapter::dragGamma(double value) { return editValue(&Gamma::gamma, value, Phase::Drag); }
bool ProcessingAdapter::releaseBrightness() { return releaseStage<BrightnessContrast>(StageId::BrightnessContrast); }
bool ProcessingAdapter::releaseContrast() { return releaseStage<BrightnessContrast>(StageId::BrightnessContrast); }
bool ProcessingAdapter::releaseGamma() { return releaseStage<Gamma>(StageId::Gamma); }

bool ProcessingAdapter::commitClipLimit(double value) {
    return editValue(&Clahe::clipLimit, value, Phase::Commit);
}
bool ProcessingAdapter::commitClipLimitText(const QString& text) {
    return commitText(&Clahe::clipLimit, text);
}
bool ProcessingAdapter::dragClipLimit(double value) {
    return editValue(&Clahe::clipLimit, value, Phase::Drag);
}
bool ProcessingAdapter::releaseClipLimit() { return releaseStage<Clahe>(StageId::Clahe); }
bool ProcessingAdapter::commitTileGridSize(double value) { return editTileGridSize(value); }
bool ProcessingAdapter::commitTileGridSizeText(const QString& text) {
    const auto bytes = text.trimmed().toUtf8();
    const char* begin = bytes.constData();
    const char* end = begin + bytes.size();
    if (begin != end && *begin == '+') ++begin;
    if (begin == end || !std::all_of(begin, end, [](char character) {
            return character >= '0' && character <= '9';
        }))
        return rejectInput(tr("Enter a whole decimal number with at most one leading plus sign."));
    std::uint64_t value{};
    const auto parsed = std::from_chars(begin, end, value, 10);
    if (parsed.ec != std::errc{} || parsed.ptr != end)
        return rejectInput(tr("Enter a whole decimal number from %1 to %2.")
            .arg(tileGridSizeMinimum()).arg(tileGridSizeMaximum()));
    return editTileGridSize(static_cast<double>(value));
}

bool ProcessingAdapter::setDenoiseMode(const QString& mode) {
    auto* model = coordinator_.processingControls();
    if (!model || !coordinator_.state().controlsEnabled)
        return rejectInput(tr("Processing controls are unavailable."));
    DenoiseMode requested;
    if (mode == QStringLiteral("gaussian")) requested = DenoiseMode::Gaussian;
    else if (mode == QStringLiteral("median")) requested = DenoiseMode::Median;
    else return rejectInput(tr("Denoise mode must be gaussian or median."));
    auto pipeline = model->draft().activePipeline;
    const auto stage = findStage<Denoise>(pipeline, StageId::Denoise);
    if (stage == pipeline.stages.end())
        return rejectInput(tr("%1 settings are unavailable.").arg(stageLabel(StageId::Denoise)));
    auto& parameters = std::get<Denoise>(stage->parameters);
    if (parameters.mode == requested) {
        inputError_.clear();
        refresh();
        return true;
    }
    parameters.mode = requested;
    if (requested == DenoiseMode::Median) {
        parameters.kernelSize = std::min(parameters.kernelSize, 5U);
        parameters.sigma = 0.0;
    }
    inputError_.clear();
    const auto result = model->edit(std::move(pipeline), Phase::Commit);
    refreshState(result.hasValue());
    return result.hasValue();
}

bool ProcessingAdapter::commitDenoiseKernelSize(double value) {
    auto* model = coordinator_.processingControls();
    if (!model || !coordinator_.state().controlsEnabled)
        return rejectInput(tr("Processing controls are unavailable."));
    if (!std::isfinite(value) || std::trunc(value) != value)
        return rejectInput(tr("Denoise kernel must be a supported whole number."));
    auto pipeline = model->draft().activePipeline;
    const auto stage = findStage<Denoise>(pipeline, StageId::Denoise);
    if (stage == pipeline.stages.end())
        return rejectInput(tr("%1 settings are unavailable.").arg(stageLabel(StageId::Denoise)));
    auto& parameters = std::get<Denoise>(stage->parameters);
    const bool supported = value == 3.0 || value == 5.0
        || (parameters.mode == DenoiseMode::Gaussian && value == 7.0);
    if (!supported)
        return rejectInput(tr("Select a kernel supported by the current denoise mode."));
    if (parameters.kernelSize == static_cast<std::uint32_t>(value)) {
        inputError_.clear();
        refresh();
        return true;
    }
    parameters.kernelSize = static_cast<std::uint32_t>(value);
    inputError_.clear();
    const auto result = model->edit(std::move(pipeline), Phase::Commit);
    refresh();
    return result.hasValue();
}

bool ProcessingAdapter::commitDenoiseSigma(double value) {
    return editValue(&Denoise::sigma, value, Phase::Commit);
}
bool ProcessingAdapter::commitDenoiseSigmaText(const QString& text) {
    return commitText(&Denoise::sigma, text);
}

bool ProcessingAdapter::retry() {
    refresh();
    if (!state_.retryEnabled) {
        retryError_ = tr("Processing retry is unavailable.");
        retryErrorContext_ = retryContext_;
        refresh();
        return false;
    }
    const auto result = coordinator_.retryProcessing();
    retryError_ = result.hasValue() ? QString{} : QString::fromStdString(result.error().operatorSummary);
    retryErrorContext_ = result.hasValue() ? std::nullopt : std::optional{retryContext_};
    refresh();
    return result.hasValue();
}
} // namespace lumora::qml
