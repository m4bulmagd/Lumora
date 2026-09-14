#include "ViewerAdapter.hpp"
#include "QuickImageItem.hpp"
#include <lumora/presentation/FramePresenter.hpp>
#include <QDateTime>
#include <QTimeZone>
#include <cmath>

namespace lumora::qml {
ViewerAdapter::ViewerAdapter(QuickImageItem& item, QObject* parent)
    : QObject(parent), item_(item) {}
void ViewerAdapter::bindPresenter(presentation::FramePresenter* presenter) {
    presenter_ = presenter;
    commandError_.clear();
    refresh();
}
void ViewerAdapter::setClosing() { closing_ = true; refresh(); }
void ViewerAdapter::refresh() { emit stateChanged(); }
QuickImageItem& ViewerAdapter::imageItem() const noexcept { return item_; }
bool ViewerAdapter::reject(const QString& reason) {
    commandError_ = reason;
    refresh();
    return false;
}
bool ViewerAdapter::setDisplayMode(const QString& mode) {
    if (closing_ || !presenter_ || !presenter_->retirementComplete())
        return reject(tr("The viewer is not ready to change mode."));
    presentation::DisplayMode selected;
    bool available{};
    if (mode == "original") { selected = presentation::DisplayMode::Original; available = originalAvailable(); }
    else if (mode == "enhanced") { selected = presentation::DisplayMode::Enhanced; available = enhancedAvailable(); }
    else if (mode == "compare") { selected = presentation::DisplayMode::Compare; available = compareAvailable(); }
    else return reject(tr("Unknown display mode."));
    if (!available) return reject(tr("This display mode is not available for the current frame."));
    presenter_->setDisplayMode(selected);
    commandError_.clear();
    refresh();
    return true;
}
bool ViewerAdapter::pause() {
    if (closing_ || !presenter_ || !presenter_->retirementComplete() ||
        presenter_->status().viewerState != presentation::ViewerState::Live)
        return reject(tr("The viewer cannot pause now."));
    presenter_->pause();
    commandError_.clear();
    refresh();
    return true;
}
bool ViewerAdapter::resume() {
    if (closing_ || !presenter_ || !presenter_->retirementComplete() ||
        presenter_->status().viewerState != presentation::ViewerState::Paused)
        return reject(tr("The viewer cannot resume now."));
    presenter_->resume();
    commandError_.clear();
    refresh();
    return true;
}
bool ViewerAdapter::fit() {
    if (closing_ || !hasFrame()) return reject(tr("There is no displayed frame to fit."));
    item_.fit(); commandError_.clear(); refresh(); return true;
}
bool ViewerAdapter::actualPixels() {
    if (closing_ || !hasFrame()) return reject(tr("There is no displayed frame to inspect."));
    item_.actualPixels(); commandError_.clear(); refresh(); return true;
}
bool ViewerAdapter::zoomAt(double x, double y, double factor) {
    if (closing_ || !hasFrame() || !std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(factor) || factor <= 0)
        return reject(tr("Zoom requires a displayed frame, finite coordinates and a positive factor."));
    item_.zoomAt({x, y}, factor); commandError_.clear(); refresh(); return true;
}
bool ViewerAdapter::panBy(double x, double y) {
    if (closing_ || !hasFrame() || !std::isfinite(x) || !std::isfinite(y))
        return reject(tr("Pan requires a displayed frame and finite coordinates."));
    item_.panBy({x, y}); commandError_.clear(); refresh(); return true;
}
QString ViewerAdapter::displayMode() const {
    const auto mode = presenter_ ? presenter_->displayMode() : presentation::DisplayMode::Enhanced;
    switch (mode) {
    case presentation::DisplayMode::Original: return QStringLiteral("original");
    case presentation::DisplayMode::Enhanced: return QStringLiteral("enhanced");
    case presentation::DisplayMode::Compare: return QStringLiteral("compare");
    }
    return {};
}
QString ViewerAdapter::playbackState() const {
    const auto state = presenter_ ? presenter_->status().viewerState : presentation::ViewerState::Live;
    switch (state) {
    case presentation::ViewerState::Live: return QStringLiteral("Live");
    case presentation::ViewerState::Paused: return QStringLiteral("Paused");
    case presentation::ViewerState::Pausing: return QStringLiteral("Pausing");
    }
    return {};
}
QString ViewerAdapter::freshness() const {
    if (!presenter_) return QStringLiteral("Waiting for frame");
    switch (presenter_->status().freshness) {
    case presentation::FrameFreshness::WaitingForFrame: return QStringLiteral("Waiting for frame");
    case presentation::FrameFreshness::Current: return QStringLiteral("Current");
    case presentation::FrameFreshness::Stale: return QStringLiteral("Stale");
    }
    return {};
}
QString ViewerAdapter::orientation() const {
    if (!presenter_ || !presenter_->status().presentationOrientation) return tr("No displayed orientation");
    const auto value = *presenter_->status().presentationOrientation;
    return QStringLiteral("%1° · horizontal flip %2 · vertical flip %3")
        .arg(static_cast<int>(value.rotation) * 90)
        .arg(value.flipHorizontal ? tr("on") : tr("off"), value.flipVertical ? tr("on") : tr("off"));
}
QString ViewerAdapter::error() const {
    if (item_.initializationError()) return QString::fromStdString(item_.initializationError()->operatorSummary);
    if (presenter_ && presenter_->error()) return QString::fromStdString(presenter_->error()->operatorSummary);
    return commandError_;
}
QString ViewerAdapter::sourceFrameId() const {
    const auto frame = presenter_ ? presenter_->presentedBundle() : nullptr;
    return frame ? QString::number(frame->sourceFrameId()) : QString{};
}
qint64 ViewerAdapter::frameAgeMs() const {
    return presenter_ ? presenter_->status().frameAge.count() : 0;
}
QString ViewerAdapter::frameTimestamp() const {
    if (!presenter_ || !presenter_->status().frameUtc) return {};
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        presenter_->status().frameUtc->time_since_epoch()).count();
    return QDateTime::fromMSecsSinceEpoch(milliseconds, QTimeZone::UTC).toString(Qt::ISODateWithMs);
}
bool ViewerAdapter::hasFrame() const { return presenter_ && presenter_->presentedBundle() != nullptr; }
bool ViewerAdapter::originalAvailable() const { return !closing_ && presenter_ && presenter_->originalAvailable(); }
bool ViewerAdapter::enhancedAvailable() const { return !closing_ && presenter_ && presenter_->enhancedAvailable(); }
bool ViewerAdapter::compareAvailable() const { return !closing_ && presenter_ && presenter_->compareAvailable(); }
qulonglong ViewerAdapter::displayedFrameCount() const { return presenter_ ? presenter_->displayedFrameCount() : 0; }
}
