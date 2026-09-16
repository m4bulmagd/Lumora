#include "VideoSourcesAdapter.hpp"
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QDir>
#include <QtTest/QTest>
#include <QSignalSpy>
#include <gtest/gtest.h>

namespace {
using lumora::qml::VideoSourcesAdapter;
using lumora::configuration::VideoSourceCatalog;
using lumora::configuration::VideoSourceDefinition;
using Sources = std::vector<VideoSourceDefinition>;
using Result = lumora::core::Result<void>;

TEST(QmlVideoSourcesAdapter, SavesAddressButKeepsCredentialsOnlyInRuntime) {
    QTemporaryDir temp;
    const auto path = temp.filePath("sources.json");
    Sources active;
    int refreshes = 0;
    VideoSourcesAdapter adapter(VideoSourceCatalog(path), [&](const Sources& sources) {
        active = sources; return Result::success();
    }, [] { return true; }, [&] { ++refreshes; });
    ASSERT_TRUE(adapter.addSource("Hall", "rtsp://camera/live", "operator", "private-password"));
    ASSERT_EQ(active.size(), 1U);
    EXPECT_EQ(QUrl(QString::fromStdString(active.front().url)).password(), "private-password");
    EXPECT_EQ(refreshes, 1);
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const auto bytes = file.readAll();
    EXPECT_FALSE(bytes.contains("private-password"));
    EXPECT_FALSE(bytes.contains("operator"));
    ASSERT_EQ(adapter.sources().size(), 1);
    EXPECT_EQ(adapter.sources().front().toMap().value("url").toString(), "rtsp://camera/live");
    Sources restored;
    VideoSourcesAdapter reopened(VideoSourceCatalog(path), [&](const Sources& sources) {
        restored = sources; return Result::success();
    }, [] { return true; }, [] {});
    ASSERT_EQ(restored.size(), 1U);
    EXPECT_EQ(restored.front().id, active.front().id);
    EXPECT_TRUE(QUrl(QString::fromStdString(restored.front().url)).userInfo().isEmpty());
}
TEST(QmlVideoSourcesAdapter, ConnectedSourcePolicyPreventsAddRemoveAndCredentialChanges) {
    QTemporaryDir temp;
    bool editable = true;
    VideoSourcesAdapter adapter(VideoSourceCatalog(temp.filePath("sources.json")),
        [](const Sources&) { return Result::success(); }, [&] { return editable; }, [] {});
    ASSERT_TRUE(adapter.addSource("Hall", "rtsp://camera/live", {}, {}));
    const auto id = adapter.sources().front().toMap().value("id").toString();
    editable = false;
    EXPECT_FALSE(adapter.editable());
    EXPECT_FALSE(adapter.removeSource(id));
    EXPECT_FALSE(adapter.setCredentials(id, "operator", "secret"));
    EXPECT_FALSE(adapter.addSource("Yard", "rtsp://yard/live", {}, {}));
    EXPECT_EQ(adapter.sources().size(), 1);
    editable = true;
    EXPECT_TRUE(adapter.removeSource(id));
    EXPECT_TRUE(adapter.sources().empty());
}
TEST(QmlVideoSourcesAdapter, RejectsEmbeddedCredentialsAndKeepsMalformedCatalog) {
    QTemporaryDir temp;
    const auto path = temp.filePath("sources.json");
    {
        VideoSourcesAdapter adapter(VideoSourceCatalog(path), [](const Sources&) { return Result::success(); }, [] { return true; }, [] {});
        EXPECT_FALSE(adapter.addSource("Hall", "rtsp://user:secret@camera/live", {}, {}));
        EXPECT_FALSE(adapter.error().contains("secret"));
        EXPECT_TRUE(adapter.sources().empty());
    }
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("{broken");
    file.close();
    VideoSourcesAdapter invalid(VideoSourceCatalog(path), [](const Sources&) { return Result::success(); }, [] { return true; }, [] {});
    EXPECT_FALSE(invalid.editable());
    EXPECT_FALSE(invalid.error().isEmpty());
    EXPECT_FALSE(invalid.addSource("Hall", "rtsp://camera/live", {}, {}));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll(), "{broken");
}
TEST(QmlVideoSourcesAdapter, FailedPersistenceRestoresRuntimeSourceList) {
    QTemporaryDir temp;
    Sources active;
    VideoSourcesAdapter adapter(VideoSourceCatalog(temp.filePath("missing/sources.json")),
        [&](const Sources& sources) { active = sources; return Result::success(); }, [] { return true; }, [] {});
    QFile obstruction(temp.filePath("missing"));
    ASSERT_TRUE(obstruction.open(QIODevice::WriteOnly));
    obstruction.close();
    EXPECT_FALSE(adapter.addSource("Hall", "rtsp://camera/live", {}, {}));
    EXPECT_TRUE(adapter.sources().empty());
    EXPECT_TRUE(active.empty());
}
TEST(QmlVideoSourcesAdapter, NetworkDialogAddsAndRemovesSourcesThroughItsControls) {
    QTemporaryDir temp;
    Sources active;
    VideoSourcesAdapter adapter(VideoSourceCatalog(temp.filePath("sources.json")),
        [&](const Sources& sources) { active = sources; return Result::success(); }, [] { return true; }, [] {});
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import Lumora.Workstation
        ApplicationWindow {
            width: 640; height: 800; visible: true
            palette.window: Theme.canvas
            palette.windowText: Theme.textPrimary
            palette.base: Theme.imageWell
            palette.text: Theme.textPrimary
            palette.button: Theme.surfaceRaised
            palette.buttonText: Theme.textPrimary
            palette.highlight: Theme.greenSoft
            required property VideoSourcesAdapter manager
            VideoSources { sources: manager; visible: true }
        }
    )", QUrl("inmemory:NetworkDialog.qml"));
    if (component.isLoading()) {
        QSignalSpy ready(&component, &QQmlComponent::statusChanged);
        ASSERT_TRUE(ready.wait(5000));
    }
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.createWithInitialProperties({{"manager", QVariant::fromValue(&adapter)}}));
    ASSERT_NE(root, nullptr) << component.errorString().toStdString();
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    ASSERT_NE(window, nullptr);
    ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
    QCoreApplication::processEvents();
    const auto find = [&](const char* name) {
        const auto descend = [&](const auto& self, QQuickItem* parent) -> QQuickItem* {
            if (parent->objectName() == QString::fromLatin1(name)) return parent;
            for (auto* child : parent->childItems()) if (auto* found = self(self, child)) return found;
            return nullptr;
        };
        return descend(descend, window->contentItem());
    };
    auto* name = find("networkSourceName");
    auto* address = find("networkSourceAddress");
    auto* add = find("addNetworkSource");
    ASSERT_NE(name, nullptr);
    ASSERT_NE(address, nullptr);
    ASSERT_NE(add, nullptr);
    name->setProperty("text", "Synthetic camera");
    address->setProperty("text", "rtsp://127.0.0.1:18554/lumora");
    EXPECT_TRUE(add->isEnabled());
    ASSERT_TRUE(QMetaObject::invokeMethod(add, "clicked"));
    ASSERT_EQ(active.size(), 1U);
    EXPECT_EQ(active.front().name, "Synthetic camera");
    EXPECT_TRUE(name->property("text").toString().isEmpty());
    QCoreApplication::processEvents();
    if (const auto directory = qEnvironmentVariable("LUMORA_QML_CAPTURE_DIR"); !directory.isEmpty()) {
        ASSERT_TRUE(QDir().mkpath(directory));
        EXPECT_TRUE(window->grabWindow().save(QDir(directory).filePath("network-sources.png")));
    }
    auto* saved = find("savedNetworkSources");
    auto* remove = find("removeNetworkSource");
    ASSERT_NE(saved, nullptr);
    ASSERT_NE(remove, nullptr);
    saved->setProperty("currentIndex", 0);
    ASSERT_TRUE(QMetaObject::invokeMethod(remove, "clicked"));
    EXPECT_TRUE(active.empty());
    EXPECT_TRUE(adapter.error().isEmpty());
}
}
