#pragma once

#include <lumora/ui/WorkstationStatus.hpp>

#include <QWidget>

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
    void setStatus(WorkstationStatus status);

signals:
    void pauseRequested();
    void resumeRequested();

private:
    void updateStatusPresentation();

    QWidget* sidebar_{nullptr};
    ImageViewport* imageViewport_{nullptr};
    WorkstationStatus status_;
};

}  // namespace lumora::ui
