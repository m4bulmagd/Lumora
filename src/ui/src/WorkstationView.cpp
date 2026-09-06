#include <lumora/ui/WorkstationView.hpp>

#include <lumora/ui/ImageViewport.hpp>

#include <QAction>
#include <QColor>
#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequence>
#include <QPalette>
#include <QPushButton>
#include <QSizePolicy>
#include <QToolButton>
#include <QTimeZone>
#include <QVBoxLayout>

#include <utility>

namespace lumora::ui {
namespace {

constexpr auto overlayStyle =
    "QLabel { color: white; background: rgba(96, 63, 8, 220); "
    "border: 2px solid #ffcc66; border-radius: 4px; font-weight: 700; "
    "padding: 8px 12px; }";

QAction* makeViewerAction(
    const QString& text,
    const char* objectName,
    QObject* parent) {
    auto* action = new QAction(text, parent);
    action->setObjectName(QString::fromLatin1(objectName));
    action->setEnabled(false);
    return action;
}

void bindViewerShortcut(
    QAction& action,
    QKeySequence shortcut,
    ImageViewport& viewport) {
    action.setShortcut(std::move(shortcut));
    action.setShortcutContext(Qt::WidgetShortcut);
    viewport.addAction(&action);
}

QToolButton* makeActionButton(
    QAction* action,
    const QString& accessibleName,
    QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setDefaultAction(action);
    button->setAccessibleName(accessibleName);
    return button;
}

}  // namespace

WorkstationView::WorkstationView(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("workstationView"));
    setAutoFillBackground(true);
    auto palette = this->palette();
    palette.setColor(QPalette::Window, QColor{28, 31, 36});
    palette.setColor(QPalette::WindowText, QColor{238, 240, 243});
    setPalette(palette);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 10, 12, 12);
    rootLayout->setSpacing(10);

    auto* releaseClassBanner = new QLabel(
        tr("EVALUATION — NOT FOR CLINICAL USE"), this);
    releaseClassBanner->setObjectName(QStringLiteral("releaseClassBanner"));
    releaseClassBanner->setAccessibleName(tr("Evaluation release warning"));
    releaseClassBanner->setAlignment(Qt::AlignCenter);
    releaseClassBanner->setStyleSheet(QStringLiteral(
        "QLabel { color: #ffcc66; font-weight: 700; }"));
    rootLayout->addWidget(releaseClassBanner);

    auto* contentLayout = new QHBoxLayout;
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);
    rootLayout->addLayout(contentLayout, 1);

    sidebar_ = new QWidget(this);
    sidebar_->setObjectName(QStringLiteral("sidebar"));
    sidebar_->setAccessibleName(tr("Original display sidebar"));
    sidebar_->setFixedWidth(240);
    sidebar_->setStyleSheet(QStringLiteral(
        "QWidget#sidebar { background: #24282e; border-radius: 4px; }"));

    auto* sidebarLayout = new QVBoxLayout(sidebar_);
    sidebarLayout->setContentsMargins(16, 16, 16, 16);
    auto* originalLabel = new QLabel(tr("Original"), sidebar_);
    originalLabel->setAccessibleName(tr("Original display"));
    originalLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #eef0f3; font-size: 18px; font-weight: 600; }"));
    sidebarLayout->addWidget(originalLabel);
    sidebarLayout->addStretch(1);

    auto* pauseLiveButton = new QPushButton(tr("Pause"), sidebar_);
    pauseLiveButton->setObjectName(QStringLiteral("pauseLiveButton"));
    pauseLiveButton->setAccessibleName(tr("Pause live viewer"));
    pauseLiveButton->setEnabled(false);
    sidebarLayout->addWidget(pauseLiveButton);
    contentLayout->addWidget(sidebar_);

    auto* viewerColumn = new QWidget(this);
    auto* viewerColumnLayout = new QVBoxLayout(viewerColumn);
    viewerColumnLayout->setContentsMargins(0, 0, 0, 0);
    viewerColumnLayout->setSpacing(8);

    auto* viewerPane = new QWidget(viewerColumn);
    viewerPane->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* viewerLayout = new QGridLayout(viewerPane);
    viewerLayout->setContentsMargins(0, 0, 0, 0);

    imageViewport_ = new ImageViewport(viewerPane);
    imageViewport_->setObjectName(QStringLiteral("imageViewport"));
    imageViewport_->setAccessibleName(tr("Original image viewport"));
    imageViewport_->setFocusPolicy(Qt::StrongFocus);
    imageViewport_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewerLayout->addWidget(imageViewport_, 0, 0);

    auto* frameStateOverlay = new QLabel(tr("Waiting for image"), viewerPane);
    frameStateOverlay->setObjectName(QStringLiteral("frameStateOverlay"));
    frameStateOverlay->setAccessibleName(tr("Image state"));
    frameStateOverlay->setAlignment(Qt::AlignCenter);
    frameStateOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    frameStateOverlay->setStyleSheet(QString::fromLatin1(overlayStyle));
    frameStateOverlay->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    viewerLayout->addWidget(frameStateOverlay, 0, 0, Qt::AlignCenter);
    viewerColumnLayout->addWidget(viewerPane, 1);

    auto* footer = new QWidget(viewerColumn);
    footer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(6);
    footerLayout->addStretch(1);

    auto* fitAction = makeViewerAction(tr("Fit"), "fitAction", this);
    auto* actualPixelsAction =
        makeViewerAction(tr("100%"), "actualPixelsAction", this);
    auto* zoomOutAction =
        makeViewerAction(tr("−"), "zoomOutAction", this);
    auto* zoomInAction = makeViewerAction(tr("+"), "zoomInAction", this);

    bindViewerShortcut(*fitAction, QKeySequence{Qt::Key_F}, *imageViewport_);
    bindViewerShortcut(
        *actualPixelsAction, QKeySequence{Qt::Key_1}, *imageViewport_);
    bindViewerShortcut(
        *zoomOutAction, QKeySequence{Qt::Key_Minus}, *imageViewport_);
    bindViewerShortcut(
        *zoomInAction, QKeySequence{Qt::Key_Plus}, *imageViewport_);

    connect(fitAction, &QAction::triggered, imageViewport_, [this] {
        imageViewport_->setFitMode();
    });
    connect(actualPixelsAction, &QAction::triggered, imageViewport_, [this] {
        imageViewport_->setActualPixels();
    });
    connect(zoomOutAction, &QAction::triggered, imageViewport_, [this] {
        imageViewport_->zoomOut();
    });
    connect(zoomInAction, &QAction::triggered, imageViewport_, [this] {
        imageViewport_->zoomIn();
    });

    footerLayout->addWidget(
        makeActionButton(fitAction, tr("Fit image to viewer"), footer));
    footerLayout->addWidget(makeActionButton(
        actualPixelsAction, tr("Show image at 100 percent"), footer));
    footerLayout->addWidget(
        makeActionButton(zoomOutAction, tr("Zoom out"), footer));
    footerLayout->addWidget(
        makeActionButton(zoomInAction, tr("Zoom in"), footer));
    viewerColumnLayout->addWidget(footer);
    contentLayout->addWidget(viewerColumn, 1);

    auto* pauseLiveAction = new QAction(this);
    pauseLiveAction->setObjectName(QStringLiteral("pauseLiveShortcut"));
    pauseLiveAction->setShortcut(QKeySequence{Qt::Key_Space});
    pauseLiveAction->setShortcutContext(Qt::WidgetShortcut);
    pauseLiveAction->setEnabled(false);
    imageViewport_->addAction(pauseLiveAction);

    const auto requestViewerStateChange = [this] {
        if (status_.viewerState == ViewerState::Paused) {
            emit resumeRequested();
        } else {
            emit pauseRequested();
        }
    };
    connect(
        pauseLiveButton, &QPushButton::clicked,
        this, requestViewerStateChange);
    connect(
        pauseLiveAction, &QAction::triggered,
        this, requestViewerStateChange);
}

QWidget* WorkstationView::sidebar() const noexcept {
    return sidebar_;
}

ImageViewport* WorkstationView::imageViewport() const noexcept {
    return imageViewport_;
}

ViewerState WorkstationView::viewerState() const noexcept {
    return status_.viewerState;
}

const WorkstationStatus& WorkstationView::status() const noexcept {
    return status_;
}

void WorkstationView::setStatus(WorkstationStatus status) {
    status_ = std::move(status);
    updateStatusPresentation();
}

void WorkstationView::updateStatusPresentation() {
    auto* pauseLiveButton =
        findChild<QPushButton*>(QStringLiteral("pauseLiveButton"));
    auto* frameStateOverlay =
        findChild<QLabel*>(QStringLiteral("frameStateOverlay"));
    const bool frameAvailable =
        status_.freshness != FrameFreshness::WaitingForFrame;

    pauseLiveButton->setEnabled(frameAvailable);
    if (status_.viewerState == ViewerState::Paused) {
        pauseLiveButton->setText(tr("Live"));
        pauseLiveButton->setAccessibleName(tr("Resume live viewer"));
    } else {
        pauseLiveButton->setText(tr("Pause"));
        pauseLiveButton->setAccessibleName(tr("Pause live viewer"));
    }

    for (const auto* objectName : {
             "fitAction",
             "actualPixelsAction",
             "zoomInAction",
             "zoomOutAction",
             "pauseLiveShortcut",
         }) {
        findChild<QAction*>(QString::fromLatin1(objectName))
            ->setEnabled(frameAvailable);
    }

    if (status_.viewerState == ViewerState::Paused) {
        QString text = tr("PAUSED");
        if (status_.frameUtc) {
            const auto epochMilliseconds =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    status_.frameUtc->time_since_epoch())
                    .count();
            const auto timestamp = QDateTime::fromMSecsSinceEpoch(
                static_cast<qint64>(epochMilliseconds), QTimeZone::UTC)
                                       .toString(Qt::ISODateWithMs);
            text += tr("\nFrame: %1\nAge: %2 ms")
                        .arg(timestamp)
                        .arg(static_cast<qlonglong>(status_.frameAge.count()));
        }
        frameStateOverlay->setText(text);
        frameStateOverlay->show();
        return;
    }

    switch (status_.freshness) {
    case FrameFreshness::WaitingForFrame:
        frameStateOverlay->setText(tr("Waiting for image"));
        frameStateOverlay->show();
        break;
    case FrameFreshness::Current:
        frameStateOverlay->hide();
        break;
    case FrameFreshness::Stale:
        frameStateOverlay->setText(tr("STALE IMAGE / NOT LIVE"));
        frameStateOverlay->show();
        break;
    }
}

}  // namespace lumora::ui
