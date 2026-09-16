#pragma once

#include <lumora/configuration/VideoSourceCatalog.hpp>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <map>

namespace lumora::qml {
class VideoSourcesAdapter final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The application owns video sources")
    Q_PROPERTY(QVariantList sources READ sources NOTIFY sourcesChanged FINAL)
    Q_PROPERTY(bool editable READ editable NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged FINAL)
public:
    using Sources = std::vector<configuration::VideoSourceDefinition>;
    using SourceUpdater = std::function<core::Result<void>(const Sources&)>;
    VideoSourcesAdapter(configuration::VideoSourceCatalog catalog, SourceUpdater updater,
        std::function<bool()> canEdit, std::function<void()> refreshDevices, QObject* parent = nullptr);
    [[nodiscard]] QVariantList sources() const;
    [[nodiscard]] bool editable() const;
    [[nodiscard]] QString error() const;
    void refresh();
    Q_INVOKABLE bool addSource(const QString& name, const QString& address,
        const QString& username, const QString& password);
    Q_INVOKABLE bool removeSource(const QString& id);
    Q_INVOKABLE bool setCredentials(const QString& id, const QString& username, const QString& password);
signals:
    void sourcesChanged();
    void stateChanged();
private:
    using Credentials = std::map<std::string, std::pair<QString, QString>>;
    [[nodiscard]] Sources runtimeSources(const Sources& sources, const Credentials& credentials) const;
    bool commit(Sources sources, Credentials credentials, bool persist);
    bool fail(QString message);
    configuration::VideoSourceCatalog catalog_;
    SourceUpdater updater_;
    std::function<bool()> canEdit_;
    std::function<void()> refreshDevices_;
    Sources sources_;
    Credentials credentials_;
    bool writable_{true};
    QString error_;
};
}
