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
    Q_INVOKABLE bool retry();

signals:
    void stateChanged();
    void presetsChanged();

private:
    struct State final {
        LoadingState loadingState{Loading};
        bool available{false};
        bool stageEnabled{false};
        double window{0.0};
        double level{0.0};
        QString windowText, levelText;
        bool pending{false};
        bool hasAcknowledged{false};
        QString activeSummary, activeRevision, selectedPresetId, draftPresetName;
        QString loadError, validationError, modelError, persistenceWarning, processingError;
        bool fallback{false};
        bool retryPending{false};
        bool retryEnabled{false};
        bool operator==(const State&) const = default;
    };

    bool editValue(double processing::WindowLevelParameters::* member, double value,
        presentation::ProcessingEditPhase phase);
    bool commitText(double processing::WindowLevelParameters::* member, const QString& text);
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
