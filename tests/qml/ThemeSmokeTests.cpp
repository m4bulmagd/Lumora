#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRectF>
#include <QSize>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>
#include <QtTest/QTest>

#include <memory>

namespace {

[[nodiscard]] QString describeRect(const QRectF& rect) {
    return QStringLiteral("x=%1, y=%2, width=%3, height=%4")
        .arg(rect.x(), 0, 'f', 1)
        .arg(rect.y(), 0, 'f', 1)
        .arg(rect.width(), 0, 'f', 1)
        .arg(rect.height(), 0, 'f', 1);
}

[[nodiscard]] QRectF sceneBounds(const QQuickItem* item) {
    return item->mapRectToScene(item->boundingRect());
}

[[nodiscard]] bool contentContains(const QQuickWindow* window, const QQuickItem* item) {
    const QRectF contentBounds = sceneBounds(window->contentItem());
    return contentBounds.adjusted(-0.5, -0.5, 0.5, 0.5).contains(sceneBounds(item));
}

[[nodiscard]] bool textFits(const QQuickItem* label) {
    return label->width() + 0.5 >= label->implicitWidth()
        && label->height() + 0.5 >= label->implicitHeight();
}

class ThemeSmokeTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void loadsCompiledModuleWithoutWarnings();
    void keepsPreviewContentUsable_data();
    void keepsPreviewContentUsable();
    void exposesOnlyThePreviewDetailsCommand();
    void opensAndClosesPreviewDetailsFromTheKeyboard();
    void cleanupTestCase();

private:
    [[nodiscard]] QObject* object(const char* objectName) const;
    [[nodiscard]] QString diagnostics() const;
    void captureWindowIfRequested(const QSize& size);

    std::unique_ptr<QQmlApplicationEngine> engine_;
    QQuickWindow* window_{nullptr};
    QStringList warnings_;
    QUrl failedUrl_;
};

void ThemeSmokeTests::initTestCase() {
    engine_ = std::make_unique<QQmlApplicationEngine>();
    connect(engine_.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError>& warnings) {
        for (const auto& warning : warnings) {
            warnings_.append(warning.toString());
        }
    });
    connect(engine_.get(), &QQmlApplicationEngine::objectCreationFailed, this,
            [this](const QUrl& url) { failedUrl_ = url; });

    engine_->loadFromModule("Lumora.Workstation", "Main");

    const auto roots = engine_->rootObjects();
    QVERIFY2(failedUrl_.isEmpty(), qPrintable(diagnostics()));
    QCOMPARE(roots.size(), 1);
    window_ = qobject_cast<QQuickWindow*>(roots.constFirst());
    QVERIFY2(window_ != nullptr, "Lumora.Workstation/Main must create a QQuickWindow root");
    QCOMPARE(window_->objectName(), QStringLiteral("workstationWindow"));

    window_->show();
    QTRY_VERIFY_WITH_TIMEOUT(window_->isExposed(), 5000);
    QVERIFY2(warnings_.isEmpty(), qPrintable(diagnostics()));
}

void ThemeSmokeTests::loadsCompiledModuleWithoutWarnings() {
    QCOMPARE(QCoreApplication::applicationName(), QStringLiteral("LumoraQmlPreview"));
    QCOMPARE(QQuickStyle::name(), QStringLiteral("Basic"));
    QVERIFY(window_->isVisible());
    QVERIFY(window_->isExposed());
    QVERIFY2(warnings_.isEmpty(), qPrintable(diagnostics()));
}

void ThemeSmokeTests::keepsPreviewContentUsable_data() {
    QTest::addColumn<QSize>("windowSize");
    QTest::newRow("minimum-supported-size") << QSize(900, 600);
    QTest::newRow("default-size") << QSize(1280, 800);
}

void ThemeSmokeTests::keepsPreviewContentUsable() {
    QFETCH(QSize, windowSize);

    window_->resize(windowSize);
    QTRY_COMPARE(window_->size(), windowSize);
    QTRY_COMPARE_WITH_TIMEOUT(qRound(window_->contentItem()->width()), windowSize.width(), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(qRound(window_->contentItem()->height()), windowSize.height(), 1000);

    auto* evaluationBanner = qobject_cast<QQuickItem*>(object("evaluationBanner"));
    auto* previewLabel = qobject_cast<QQuickItem*>(object("previewLabel"));
    auto* waitingLabel = qobject_cast<QQuickItem*>(object("waitingLabel"));
    auto* imageArea = qobject_cast<QQuickItem*>(object("imageArea"));
    QVERIFY2(evaluationBanner != nullptr, "Missing evaluationBanner Label");
    QVERIFY2(previewLabel != nullptr, "Missing previewLabel Label");
    QVERIFY2(waitingLabel != nullptr, "Missing waitingLabel Label");
    QVERIFY2(imageArea != nullptr, "Missing imageArea Item");

    QTRY_VERIFY_WITH_TIMEOUT(evaluationBanner->isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(previewLabel->isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(waitingLabel->isVisible(), 1000);
    QVERIFY(evaluationBanner->property("text").toString().contains(
        QStringLiteral("EVALUATION"), Qt::CaseInsensitive));
    QVERIFY(evaluationBanner->property("text").toString().contains(
        QStringLiteral("NOT FOR CLINICAL USE"), Qt::CaseInsensitive));
    QVERIFY(previewLabel->property("text").toString().contains(
        QStringLiteral("Interface preview"), Qt::CaseInsensitive));
    QVERIFY(waitingLabel->property("text").toString().contains(
        QStringLiteral("Waiting"), Qt::CaseInsensitive));

    QTRY_VERIFY_WITH_TIMEOUT(imageArea->isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(imageArea->width() > 0.0, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(imageArea->height() > 0.0, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(contentContains(window_, imageArea), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(contentContains(window_, evaluationBanner), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(contentContains(window_, previewLabel), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(contentContains(window_, waitingLabel), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(sceneBounds(imageArea).adjusted(-0.5, -0.5, 0.5, 0.5)
                                 .contains(sceneBounds(waitingLabel)),
                             1000);
    QTRY_VERIFY_WITH_TIMEOUT(textFits(evaluationBanner), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(textFits(previewLabel), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(textFits(waitingLabel), 1000);

    const QRectF contentBounds = window_->contentItem()->mapRectToScene(
        window_->contentItem()->boundingRect());
    const QRectF imageBounds = imageArea->mapRectToScene(imageArea->boundingRect());
    QVERIFY2(contentBounds.adjusted(-0.5, -0.5, 0.5, 0.5).contains(imageBounds),
             qPrintable(QStringLiteral("imageArea %1 is outside content bounds %2 at %3x%4")
                            .arg(describeRect(imageBounds), describeRect(contentBounds))
                            .arg(windowSize.width())
                            .arg(windowSize.height())));

    captureWindowIfRequested(windowSize);
    QVERIFY2(warnings_.isEmpty(), qPrintable(diagnostics()));
}

void ThemeSmokeTests::exposesOnlyThePreviewDetailsCommand() {
    auto* sourceSelector = qobject_cast<QQuickItem*>(object("sourceSelector"));
    auto* connectButton = qobject_cast<QQuickItem*>(object("connectButton"));
    auto* startButton = qobject_cast<QQuickItem*>(object("startButton"));
    auto* windowSlider = qobject_cast<QQuickItem*>(object("windowSlider"));
    auto* levelSlider = qobject_cast<QQuickItem*>(object("levelSlider"));
    auto* previewDetailsButton = qobject_cast<QQuickItem*>(object("previewDetailsButton"));

    QVERIFY2(sourceSelector != nullptr, "Missing sourceSelector control");
    QVERIFY2(connectButton != nullptr, "Missing connectButton control");
    QVERIFY2(startButton != nullptr, "Missing startButton control");
    QVERIFY2(windowSlider != nullptr, "Missing windowSlider control");
    QVERIFY2(levelSlider != nullptr, "Missing levelSlider control");
    QVERIFY2(previewDetailsButton != nullptr, "Missing previewDetailsButton control");

    QVERIFY(!sourceSelector->isEnabled());
    QVERIFY(!connectButton->isEnabled());
    QVERIFY(!startButton->isEnabled());
    QVERIFY(!windowSlider->isEnabled());
    QVERIFY(!levelSlider->isEnabled());
    QVERIFY(previewDetailsButton->isEnabled());
    QVERIFY2(warnings_.isEmpty(), qPrintable(diagnostics()));
}

void ThemeSmokeTests::opensAndClosesPreviewDetailsFromTheKeyboard() {
    window_->resize(QSize(1280, 800));
    QTRY_COMPARE(window_->size(), QSize(1280, 800));

    auto* detailsButton = qobject_cast<QQuickItem*>(object("previewDetailsButton"));
    auto* detailsPopup = object("previewDetailsDialog");
    QVERIFY2(detailsButton != nullptr, "Missing previewDetailsButton standard Button");
    QVERIFY2(detailsPopup != nullptr, "Missing accessible previewDetailsDialog popup or dialog");
    QVERIFY(detailsButton->isVisible());
    QVERIFY(detailsButton->isEnabled());
    QVERIFY(!detailsPopup->property("visible").toBool());

    window_->contentItem()->forceActiveFocus(Qt::OtherFocusReason);
    bool reachedDetailsButton = false;
    for (int press = 0; press < 32 && !reachedDetailsButton; ++press) {
        QTest::keyClick(window_, Qt::Key_Tab);
        QCoreApplication::processEvents();
        reachedDetailsButton = window_->activeFocusItem() == detailsButton;
    }
    QVERIFY2(reachedDetailsButton,
             "Native Tab traversal did not focus previewDetailsButton");

    QTest::keyClick(window_, Qt::Key_Space);
    QTRY_VERIFY_WITH_TIMEOUT(detailsPopup->property("visible").toBool(), 1000);

    QTest::keyClick(window_, Qt::Key_Escape);
    QTRY_VERIFY_WITH_TIMEOUT(!detailsPopup->property("visible").toBool(), 1000);
    QVERIFY2(warnings_.isEmpty(), qPrintable(diagnostics()));
}

void ThemeSmokeTests::cleanupTestCase() {
    if (window_ != nullptr) {
        window_->close();
    }
    engine_.reset();
    window_ = nullptr;
}

QObject* ThemeSmokeTests::object(const char* objectName) const {
    return window_->findChild<QObject*>(QString::fromLatin1(objectName),
                                        Qt::FindChildrenRecursively);
}

QString ThemeSmokeTests::diagnostics() const {
    QStringList details = warnings_;
    if (!failedUrl_.isEmpty()) {
        details.prepend(QStringLiteral("QML object creation failed for %1")
                            .arg(failedUrl_.toString()));
    }
    if (details.isEmpty()) {
        return QStringLiteral("No QML diagnostics were emitted");
    }
    return details.join(QLatin1Char('\n'));
}

void ThemeSmokeTests::captureWindowIfRequested(const QSize& size) {
    const QString captureDirectory = qEnvironmentVariable("LUMORA_QML_CAPTURE_DIR");
    if (captureDirectory.isEmpty()) {
        return;
    }

    QDir directory(captureDirectory);
    QVERIFY2(directory.exists() || QDir().mkpath(captureDirectory),
             qPrintable(QStringLiteral("Could not create capture directory: %1")
                            .arg(captureDirectory)));

    const QImage capture = window_->grabWindow();
    QVERIFY2(!capture.isNull(), "QQuickWindow::grabWindow returned an empty capture");
    const QString path = directory.filePath(
        QStringLiteral("theme-smoke-%1x%2.png").arg(size.width()).arg(size.height()));
    QVERIFY2(capture.save(path),
             qPrintable(QStringLiteral("Could not save QML smoke capture: %1").arg(path)));
}

}  // namespace

int main(int argc, char** argv) {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QCoreApplication::setApplicationName(QStringLiteral("LumoraQmlPreview"));
    QStandardPaths::setTestModeEnabled(true);

    QGuiApplication application(argc, argv);
    QGuiApplication::setQuitOnLastWindowClosed(false);

    ThemeSmokeTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "ThemeSmokeTests.moc"
