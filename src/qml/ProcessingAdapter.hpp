#pragma once

#include <lumora/presentation/WorkstationCoordinator.hpp>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace lumora::qml {

// Frontend snapshot and command boundary. The runtime alone polls the shared
// coordinator, which owns submission, acknowledgement and persistence.
class ProcessingAdapter final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("ProcessingAdapter is supplied by the workstation")
    Q_PROPERTY(LoadingState loadingState READ loadingState NOTIFY stateChanged)
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    Q_PROPERTY(bool stageEnabled READ stageEnabled NOTIFY stateChanged)
    Q_PROPERTY(double window READ window NOTIFY stateChanged)
    Q_PROPERTY(double level READ level NOTIFY stateChanged)
    Q_PROPERTY(QString windowText READ windowText NOTIFY stateChanged)
    Q_PROPERTY(QString levelText READ levelText NOTIFY stateChanged)
    Q_PROPERTY(double windowMinimum READ windowMinimum CONSTANT)
    Q_PROPERTY(double windowMaximum READ windowMaximum CONSTANT)
    Q_PROPERTY(double levelMinimum READ levelMinimum CONSTANT)
    Q_PROPERTY(double levelMaximum READ levelMaximum CONSTANT)
    Q_PROPERTY(double nominalStep READ nominalStep CONSTANT)
    Q_PROPERTY(bool brightnessContrastEnabled READ brightnessContrastEnabled NOTIFY stateChanged)
    Q_PROPERTY(bool gammaEnabled READ gammaEnabled NOTIFY stateChanged)
    Q_PROPERTY(double brightness READ brightness NOTIFY stateChanged)
    Q_PROPERTY(QString brightnessText READ brightnessText NOTIFY stateChanged)
    Q_PROPERTY(double brightnessMinimum READ brightnessMinimum CONSTANT)
    Q_PROPERTY(double brightnessMaximum READ brightnessMaximum CONSTANT)
    Q_PROPERTY(double contrast READ contrast NOTIFY stateChanged)
    Q_PROPERTY(QString contrastText READ contrastText NOTIFY stateChanged)
    Q_PROPERTY(double contrastMinimum READ contrastMinimum CONSTANT)
    Q_PROPERTY(double contrastMaximum READ contrastMaximum CONSTANT)
    Q_PROPERTY(double gamma READ gamma NOTIFY stateChanged)
    Q_PROPERTY(QString gammaText READ gammaText NOTIFY stateChanged)
    Q_PROPERTY(double gammaMinimum READ gammaMinimum CONSTANT)
    Q_PROPERTY(double gammaMaximum READ gammaMaximum CONSTANT)
    Q_PROPERTY(bool localContrastEnabled READ localContrastEnabled NOTIFY stateChanged)
    Q_PROPERTY(double clipLimit READ clipLimit NOTIFY stateChanged)
    Q_PROPERTY(QString clipLimitText READ clipLimitText NOTIFY stateChanged)
    Q_PROPERTY(double clipLimitMinimum READ clipLimitMinimum CONSTANT)
    Q_PROPERTY(double clipLimitMaximum READ clipLimitMaximum CONSTANT)
    Q_PROPERTY(int tileGridSize READ tileGridSize NOTIFY stateChanged)
    Q_PROPERTY(QString tileGridSizeText READ tileGridSizeText NOTIFY stateChanged)
    Q_PROPERTY(int tileGridSizeMinimum READ tileGridSizeMinimum CONSTANT)
    Q_PROPERTY(int tileGridSizeMaximum READ tileGridSizeMaximum CONSTANT)
    Q_PROPERTY(bool denoiseEnabled READ denoiseEnabled NOTIFY stateChanged)
    Q_PROPERTY(QString denoiseMode READ denoiseMode NOTIFY stateChanged)
    Q_PROPERTY(int denoiseKernelSize READ denoiseKernelSize NOTIFY stateChanged)
    Q_PROPERTY(QVariantList denoiseKernelOptions READ denoiseKernelOptions NOTIFY stateChanged)
    Q_PROPERTY(double denoiseSigma READ denoiseSigma NOTIFY stateChanged)
    Q_PROPERTY(QString denoiseSigmaText READ denoiseSigmaText NOTIFY stateChanged)
    Q_PROPERTY(double denoiseSigmaMinimum READ denoiseSigmaMinimum CONSTANT)
    Q_PROPERTY(double denoiseSigmaMaximum READ denoiseSigmaMaximum CONSTANT)
    Q_PROPERTY(bool sharpenEnabled READ sharpenEnabled NOTIFY stateChanged)
    Q_PROPERTY(double sharpenAmount READ sharpenAmount NOTIFY stateChanged)
    Q_PROPERTY(QString sharpenAmountText READ sharpenAmountText NOTIFY stateChanged)
    Q_PROPERTY(double sharpenAmountMinimum READ sharpenAmountMinimum CONSTANT)
    Q_PROPERTY(double sharpenAmountMaximum READ sharpenAmountMaximum CONSTANT)
    Q_PROPERTY(double sharpenRadius READ sharpenRadius NOTIFY stateChanged)
    Q_PROPERTY(QString sharpenRadiusText READ sharpenRadiusText NOTIFY stateChanged)
    Q_PROPERTY(double sharpenRadiusMinimum READ sharpenRadiusMinimum CONSTANT)
    Q_PROPERTY(double sharpenRadiusMaximum READ sharpenRadiusMaximum CONSTANT)
    Q_PROPERTY(double sharpenThreshold READ sharpenThreshold NOTIFY stateChanged)
    Q_PROPERTY(QString sharpenThresholdText READ sharpenThresholdText NOTIFY stateChanged)
    Q_PROPERTY(double sharpenThresholdMinimum READ sharpenThresholdMinimum CONSTANT)
    Q_PROPERTY(double sharpenThresholdMaximum READ sharpenThresholdMaximum CONSTANT)
    Q_PROPERTY(bool invertEnabled READ invertEnabled NOTIFY stateChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY stateChanged)
    Q_PROPERTY(bool hasAcknowledged READ hasAcknowledged NOTIFY stateChanged)
    Q_PROPERTY(QString activeSummary READ activeSummary NOTIFY stateChanged)
    Q_PROPERTY(QString activeRevision READ activeRevision NOTIFY stateChanged)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QString selectedPresetId READ selectedPresetId NOTIFY stateChanged)
    Q_PROPERTY(QString draftPresetName READ draftPresetName NOTIFY stateChanged)
    Q_PROPERTY(QString loadError READ loadError NOTIFY stateChanged)
    Q_PROPERTY(QString validationError READ validationError NOTIFY stateChanged)
    Q_PROPERTY(QString modelError READ modelError NOTIFY stateChanged)
    Q_PROPERTY(QString persistenceWarning READ persistenceWarning NOTIFY stateChanged)
    Q_PROPERTY(QString processingError READ processingError NOTIFY stateChanged)
    Q_PROPERTY(bool fallback READ fallback NOTIFY stateChanged)
    Q_PROPERTY(bool retryPending READ retryPending NOTIFY stateChanged)
    Q_PROPERTY(bool retryEnabled READ retryEnabled NOTIFY stateChanged)

public:
    enum LoadingState { Loading, Ready, Unavailable };
    Q_ENUM(LoadingState)

    explicit ProcessingAdapter(presentation::WorkstationCoordinator& coordinator,
        QObject* parent = nullptr);
    void refresh();

    [[nodiscard]] LoadingState loadingState() const { return state_.loadingState; }
    [[nodiscard]] bool available() const { return state_.available; }
    [[nodiscard]] bool stageEnabled() const { return state_.stageEnabled; }
    [[nodiscard]] double window() const { return state_.window; }
    [[nodiscard]] double level() const { return state_.level; }
    [[nodiscard]] QString windowText() const { return state_.windowText; }
    [[nodiscard]] QString levelText() const { return state_.levelText; }
    [[nodiscard]] double windowMinimum() const { return 1.0; }
    [[nodiscard]] double windowMaximum() const { return 65535.0; }
    [[nodiscard]] double levelMinimum() const { return 0.0; }
    [[nodiscard]] double levelMaximum() const { return 65535.0; }
    [[nodiscard]] double nominalStep() const { return 100.0; }
    [[nodiscard]] bool brightnessContrastEnabled() const { return state_.brightnessContrastEnabled; }
    [[nodiscard]] bool gammaEnabled() const { return state_.gammaEnabled; }
    [[nodiscard]] double brightness() const { return state_.brightness; }
    [[nodiscard]] QString brightnessText() const { return state_.brightnessText; }
    [[nodiscard]] double brightnessMinimum() const { return -1.0; }
    [[nodiscard]] double brightnessMaximum() const { return 1.0; }
    [[nodiscard]] double contrast() const { return state_.contrast; }
    [[nodiscard]] QString contrastText() const { return state_.contrastText; }
    [[nodiscard]] double contrastMinimum() const { return 0.0; }
    [[nodiscard]] double contrastMaximum() const { return 4.0; }
    [[nodiscard]] double gamma() const { return state_.gamma; }
    [[nodiscard]] QString gammaText() const { return state_.gammaText; }
    [[nodiscard]] double gammaMinimum() const { return 0.1; }
    [[nodiscard]] double gammaMaximum() const { return 5.0; }
    [[nodiscard]] bool localContrastEnabled() const { return state_.localContrastEnabled; }
    [[nodiscard]] double clipLimit() const { return state_.clipLimit; }
    [[nodiscard]] QString clipLimitText() const { return state_.clipLimitText; }
    [[nodiscard]] double clipLimitMinimum() const { return 0.1; }
    [[nodiscard]] double clipLimitMaximum() const { return 40.0; }
    [[nodiscard]] int tileGridSize() const { return state_.tileGridSize; }
    [[nodiscard]] QString tileGridSizeText() const { return state_.tileGridSizeText; }
    [[nodiscard]] int tileGridSizeMinimum() const { return 2; }
    [[nodiscard]] int tileGridSizeMaximum() const { return 32; }
    [[nodiscard]] bool denoiseEnabled() const { return state_.denoiseEnabled; }
    [[nodiscard]] QString denoiseMode() const { return state_.denoiseMode; }
    [[nodiscard]] int denoiseKernelSize() const { return state_.denoiseKernelSize; }
    [[nodiscard]] QVariantList denoiseKernelOptions() const { return state_.denoiseKernelOptions; }
    [[nodiscard]] double denoiseSigma() const { return state_.denoiseSigma; }
    [[nodiscard]] QString denoiseSigmaText() const { return state_.denoiseSigmaText; }
    [[nodiscard]] double denoiseSigmaMinimum() const { return 0.0; }
    [[nodiscard]] double denoiseSigmaMaximum() const { return 5.0; }
    [[nodiscard]] bool sharpenEnabled() const { return state_.sharpenEnabled; }
    [[nodiscard]] double sharpenAmount() const { return state_.sharpenAmount; }
    [[nodiscard]] QString sharpenAmountText() const { return state_.sharpenAmountText; }
    [[nodiscard]] double sharpenAmountMinimum() const { return 0.0; }
    [[nodiscard]] double sharpenAmountMaximum() const { return 5.0; }
    [[nodiscard]] double sharpenRadius() const { return state_.sharpenRadius; }
    [[nodiscard]] QString sharpenRadiusText() const { return state_.sharpenRadiusText; }
    [[nodiscard]] double sharpenRadiusMinimum() const { return 0.5; }
    [[nodiscard]] double sharpenRadiusMaximum() const { return 5.0; }
    [[nodiscard]] double sharpenThreshold() const { return state_.sharpenThreshold; }
    [[nodiscard]] QString sharpenThresholdText() const { return state_.sharpenThresholdText; }
    [[nodiscard]] double sharpenThresholdMinimum() const { return 0.0; }
    [[nodiscard]] double sharpenThresholdMaximum() const { return 65535.0; }
    [[nodiscard]] bool invertEnabled() const { return state_.invertEnabled; }
    [[nodiscard]] bool pending() const { return state_.pending; }
    [[nodiscard]] bool hasAcknowledged() const { return state_.hasAcknowledged; }
    [[nodiscard]] QString activeSummary() const { return state_.activeSummary; }
    [[nodiscard]] QString activeRevision() const { return state_.activeRevision; }
    [[nodiscard]] QVariantList presets() const { return presets_; }
    [[nodiscard]] QString selectedPresetId() const { return state_.selectedPresetId; }
    [[nodiscard]] QString draftPresetName() const { return state_.draftPresetName; }
    [[nodiscard]] QString loadError() const { return state_.loadError; }
    [[nodiscard]] QString validationError() const { return state_.validationError; }
    [[nodiscard]] QString modelError() const { return state_.modelError; }
    [[nodiscard]] QString persistenceWarning() const { return state_.persistenceWarning; }
    [[nodiscard]] QString processingError() const { return state_.processingError; }
    [[nodiscard]] bool fallback() const { return state_.fallback; }
    [[nodiscard]] bool retryPending() const { return state_.retryPending; }
    [[nodiscard]] bool retryEnabled() const { return state_.retryEnabled; }

    Q_INVOKABLE bool selectPreset(const QString& id);
    Q_INVOKABLE bool resetProcessing();
    Q_INVOKABLE bool setStageEnabled(bool enabled);
    Q_INVOKABLE bool commitWindow(double value);
    Q_INVOKABLE bool commitLevel(double value);
    Q_INVOKABLE bool commitWindowText(const QString& text);
    Q_INVOKABLE bool commitLevelText(const QString& text);
    Q_INVOKABLE bool dragWindow(double value);
    Q_INVOKABLE bool dragLevel(double value);
    Q_INVOKABLE bool releaseWindow(double value);
    Q_INVOKABLE bool releaseLevel(double value);
    Q_INVOKABLE bool setBrightnessContrastEnabled(bool enabled);
    Q_INVOKABLE bool setGammaEnabled(bool enabled);
    Q_INVOKABLE bool commitBrightness(double value);
    Q_INVOKABLE bool commitBrightnessText(const QString& text);
    Q_INVOKABLE bool dragBrightness(double value);
    Q_INVOKABLE bool releaseBrightness();
    Q_INVOKABLE bool commitContrast(double value);
    Q_INVOKABLE bool commitContrastText(const QString& text);
    Q_INVOKABLE bool dragContrast(double value);
    Q_INVOKABLE bool releaseContrast();
    Q_INVOKABLE bool commitGamma(double value);
    Q_INVOKABLE bool commitGammaText(const QString& text);
    Q_INVOKABLE bool dragGamma(double value);
    Q_INVOKABLE bool releaseGamma();
    Q_INVOKABLE bool setLocalContrastEnabled(bool enabled);
    Q_INVOKABLE bool commitClipLimit(double value);
    Q_INVOKABLE bool commitClipLimitText(const QString& text);
    Q_INVOKABLE bool dragClipLimit(double value);
    Q_INVOKABLE bool releaseClipLimit();
    Q_INVOKABLE bool commitTileGridSize(double value);
    Q_INVOKABLE bool commitTileGridSizeText(const QString& text);
    Q_INVOKABLE bool setDenoiseEnabled(bool enabled);
    Q_INVOKABLE bool setDenoiseMode(const QString& mode);
    Q_INVOKABLE bool commitDenoiseKernelSize(double value);
    Q_INVOKABLE bool commitDenoiseSigma(double value);
    Q_INVOKABLE bool commitDenoiseSigmaText(const QString& text);
    Q_INVOKABLE bool setSharpenEnabled(bool enabled);
    Q_INVOKABLE bool commitSharpenAmount(double value);
    Q_INVOKABLE bool commitSharpenAmountText(const QString& text);
    Q_INVOKABLE bool dragSharpenAmount(double value);
    Q_INVOKABLE bool releaseSharpenAmount();
    Q_INVOKABLE bool commitSharpenRadius(double value);
    Q_INVOKABLE bool commitSharpenRadiusText(const QString& text);
    Q_INVOKABLE bool commitSharpenThreshold(double value);
    Q_INVOKABLE bool commitSharpenThresholdText(const QString& text);
    Q_INVOKABLE bool setInvertEnabled(bool enabled);
    Q_INVOKABLE bool retry();

signals:
    void stateChanged();
    void presetsChanged();
    void draftReplaced();

private:
    struct State final {
        LoadingState loadingState{Loading};
        bool available{false};
        bool stageEnabled{false};
        double window{0.0};
        double level{0.0};
        QString windowText, levelText;
        bool brightnessContrastEnabled{false};
        bool gammaEnabled{false};
        double brightness{0.0}, contrast{0.0}, gamma{0.0};
        QString brightnessText, contrastText, gammaText;
        bool localContrastEnabled{false};
        double clipLimit{0.0};
        QString clipLimitText;
        int tileGridSize{0};
        QString tileGridSizeText;
        bool denoiseEnabled{false};
        QString denoiseMode;
        int denoiseKernelSize{0};
        QVariantList denoiseKernelOptions;
        double denoiseSigma{0.0};
        QString denoiseSigmaText;
        bool sharpenEnabled{false};
        double sharpenAmount{0.0}, sharpenRadius{0.0}, sharpenThreshold{0.0};
        QString sharpenAmountText, sharpenRadiusText, sharpenThresholdText;
        bool invertEnabled{false};
        bool pending{false};
        bool hasAcknowledged{false};
        QString activeSummary, activeRevision, selectedPresetId, draftPresetName;
        QString loadError, validationError, modelError, persistenceWarning, processingError;
        bool fallback{false};
        bool retryPending{false};
        bool retryEnabled{false};
        bool operator==(const State&) const = default;
    };

    void refreshState(bool draftWasReplaced);
    bool editValue(double processing::WindowLevelParameters::* member, double value,
        presentation::ProcessingEditPhase phase);
    bool editValue(double processing::BrightnessContrastParameters::* member, double value,
        presentation::ProcessingEditPhase phase);
    bool editValue(double processing::GammaParameters::* member, double value,
        presentation::ProcessingEditPhase phase);
    bool editValue(double processing::ClaheParameters::* member, double value,
        presentation::ProcessingEditPhase phase);
    bool editValue(double processing::DenoiseParameters::* member, double value,
        presentation::ProcessingEditPhase phase);
    bool editValue(double processing::SharpenParameters::* member, double value,
        presentation::ProcessingEditPhase phase);
    bool editTileGridSize(double value);
    template<typename Parameters>
    bool editStageValue(processing::StageId id, double Parameters::* member, double value,
        double minimum, double maximum, const QString& label, presentation::ProcessingEditPhase phase);
    template<typename Parameters>
    bool commitText(double Parameters::* member, const QString& text);
    template<typename Parameters>
    bool setEnabled(processing::StageId id, bool enabled);
    template<typename Parameters>
    bool releaseStage(processing::StageId id);
    bool rejectInput(const QString& message);
    presentation::WorkstationCoordinator& coordinator_;
    State state_;
    QVariantList presets_;
    QString inputError_;
    QString retryError_;
    struct RetryContext final {
        std::uint64_t sessionGeneration{};
        std::uint64_t activationSerial{};
        bool contextBound{false};
        bool healthy{false};
        bool operator==(const RetryContext&) const = default;
    };
    RetryContext retryContext_;
    std::optional<RetryContext> retryErrorContext_;
};

} // namespace lumora::qml
