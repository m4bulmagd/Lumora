#pragma once

#include "CameraAdapter.hpp"
#include "InstallationAdapter.hpp"
#include "LayoutAdapter.hpp"
#include "ProcessingAdapter.hpp"
#include "ViewerAdapter.hpp"
#include <lumora/presentation/WorkstationCoordinator.hpp>
#include <QObject>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace lumora::qml {
// Reserve the maximum accepted SIM layout in Compare before constructing the
// real pipeline. The checked sum includes image replacement and texture sets.
[[nodiscard]] core::Result<void> reserveSimulatorRendererStorage(
    processing::ProcessingPreparationOptions& options,
    std::size_t width = 640, std::size_t height = 480);

// Application-owned composition. Borrowed services and clock, this object and
// its window must remain alive until shutdownComplete; the owner then drains
// preference/installation workers and may destroy the QML engine and clock.
class QmlWorkstation final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The application owns the workstation")
    Q_PROPERTY(lumora::qml::CameraAdapter* camera READ camera CONSTANT FINAL)
    Q_PROPERTY(lumora::qml::ViewerAdapter* viewer READ viewer CONSTANT FINAL)
    Q_PROPERTY(lumora::qml::ProcessingAdapter* processing READ processing CONSTANT FINAL)
    Q_PROPERTY(lumora::qml::InstallationAdapter* installation READ installation CONSTANT FINAL)
    Q_PROPERTY(lumora::qml::LayoutAdapter* layout READ layout CONSTANT FINAL)
    Q_PROPERTY(bool closing READ closing NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool closed READ closed NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged FINAL)
public:
    QmlWorkstation(application::LivePipeline&, configuration::StartupPreferencesService&,
        core::IClock&, camera::CameraConfiguration, QObject* parent = nullptr);
    ~QmlWorkstation() override;
    [[nodiscard]] core::Result<void> start();
    Q_INVOKABLE void poll();
    Q_INVOKABLE void requestShutdown();
    [[nodiscard]] CameraAdapter* camera() const noexcept;
    [[nodiscard]] ViewerAdapter* viewer() const noexcept;
    [[nodiscard]] ProcessingAdapter* processing() const noexcept;
    [[nodiscard]] InstallationAdapter* installation() const noexcept;
    [[nodiscard]] LayoutAdapter* layout() const noexcept;
    [[nodiscard]] presentation::WorkstationCoordinator& coordinator() noexcept;
    [[nodiscard]] bool closing() const noexcept;
    [[nodiscard]] bool closed() const noexcept;
    [[nodiscard]] QString error() const;
signals:
    void stateChanged();
    void shutdownComplete();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
