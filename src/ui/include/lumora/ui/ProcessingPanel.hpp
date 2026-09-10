#pragma once
#include <QWidget>
#include <memory>
#include <optional>
#include <lumora/core/Error.hpp>
namespace lumora::ui {
class ProcessingControlsModel;
class ProcessingPanel final : public QWidget {
    Q_OBJECT
public:
    explicit ProcessingPanel(ProcessingControlsModel& model,QWidget* parent=nullptr);
    ~ProcessingPanel() override;
    void refresh();
    void setPersistenceWarning(std::optional<core::Error> warning);
signals:
    void edited();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
