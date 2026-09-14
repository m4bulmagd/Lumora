#pragma once

#include <lumora/presentation/WorkstationStatus.hpp>
#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace lumora::presentation { class FramePresenter; }
namespace lumora::qml {
class QuickImageItem;
class ViewerAdapter final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The workstation owns the viewer")
    Q_PROPERTY(QString displayMode READ displayMode NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString playbackState READ playbackState NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString freshness READ freshness NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString orientation READ orientation NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString sourceFrameId READ sourceFrameId NOTIFY stateChanged FINAL)
    Q_PROPERTY(qint64 frameAgeMs READ frameAgeMs NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString frameTimestamp READ frameTimestamp NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool originalAvailable READ originalAvailable NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool enhancedAvailable READ enhancedAvailable NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool compareAvailable READ compareAvailable NOTIFY stateChanged FINAL)
public:
    explicit ViewerAdapter(QuickImageItem&, QObject* parent = nullptr);
    void bindPresenter(presentation::FramePresenter*);
    void setClosing();
    void refresh();
    // C++ renderer attachment seam. QObject ownership stays with the runtime.
    [[nodiscard]] QuickImageItem& imageItem() const noexcept;
    Q_INVOKABLE bool setDisplayMode(const QString& mode);
    Q_INVOKABLE bool pause();
    Q_INVOKABLE bool resume();
    Q_INVOKABLE bool fit();
    Q_INVOKABLE bool actualPixels();
    Q_INVOKABLE bool zoomAt(double x, double y, double factor);
    Q_INVOKABLE bool panBy(double x, double y);
    [[nodiscard]] QString displayMode() const;
    [[nodiscard]] QString playbackState() const;
    [[nodiscard]] QString freshness() const;
    [[nodiscard]] QString orientation() const;
    [[nodiscard]] QString error() const;
    [[nodiscard]] QString sourceFrameId() const;
    [[nodiscard]] qint64 frameAgeMs() const;
    [[nodiscard]] QString frameTimestamp() const;
    [[nodiscard]] bool hasFrame() const;
    [[nodiscard]] bool originalAvailable() const;
    [[nodiscard]] bool enhancedAvailable() const;
    [[nodiscard]] bool compareAvailable() const;
    [[nodiscard]] qulonglong displayedFrameCount() const;
signals:
    void stateChanged();
private:
    bool reject(const QString& reason);
    QuickImageItem& item_;
    presentation::FramePresenter* presenter_{};
    bool closing_{};
    QString commandError_;
};
}
