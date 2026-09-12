#pragma once

#include <lumora/ui/WorkstationStatus.hpp>
#include <lumora/ui/DisplayMode.hpp>
#include <lumora/processing/ProcessorStatus.hpp>

#include <QWidget>

class QVBoxLayout;

namespace lumora::ui {

class ImageViewport;

class WorkstationView final : public QWidget {
    Q_OBJECT

public:
    explicit WorkstationView(QWidget* parent = nullptr);

    [[nodiscard]] QWidget* sidebar() const noexcept;
    [[nodiscard]] ImageViewport* imageViewport() const noexcept;
    [[nodiscard]] ViewerState viewerState() const noexcept;
    [[nodiscard]] const WorkstationStatus& status() const noexcept;
    void addSidebarPanel(QWidget* panel);
    // Describes the display frame that completed painting, not pending processing.
    void setPreviewSource(bool enhanced);
    void setDisplayModeAvailability(bool hasImage, bool hasEnhanced);
    void setPresentedDisplayMode(DisplayMode mode);
    void setStatus(WorkstationStatus status);
    void setProcessingStatus(processing::ProcessorStatus status, bool retryPending = false);

signals:
    void pauseRequested();
    void resumeRequested();
    void processingRetryRequested();
    void displayModeRequested(lumora::ui::DisplayMode mode);

private:
    void updateStatusPresentation();

    QWidget* sidebar_{nullptr};
    QVBoxLayout* sidebarPanels_{nullptr};
    ImageViewport* imageViewport_{nullptr};
    DisplayMode presentedMode_{DisplayMode::Enhanced};
    bool enhancedImageAvailable_{false};
    WorkstationStatus status_;
    processing::ProcessorStatus processingStatus_;
};

}  // namespace lumora::ui
