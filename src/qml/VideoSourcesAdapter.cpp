#include "VideoSourcesAdapter.hpp"
#include <QUrl>
#include <QUuid>
#include <algorithm>

namespace lumora::qml {
VideoSourcesAdapter::VideoSourcesAdapter(configuration::VideoSourceCatalog catalog, SourceUpdater updater,
    std::function<bool()> canEdit, std::function<void()> refreshDevices, QObject* parent)
    : QObject(parent), catalog_(std::move(catalog)), updater_(std::move(updater)),
      canEdit_(std::move(canEdit)), refreshDevices_(std::move(refreshDevices)) {
    const auto loaded = catalog_.load();
    if (!loaded.hasValue()) {
        writable_ = false;
        error_ = QString::fromStdString(loaded.error().operatorSummary);
        return;
    }
    sources_ = loaded.value();
    if (!updater_(sources_).hasValue()) {
        writable_ = false;
        error_ = tr("Saved network sources could not be registered.");
    }
}
QVariantList VideoSourcesAdapter::sources() const {
    QVariantList rows;
    for (const auto& source : sources_) rows.append(QVariantMap{
        {"id", QString::fromStdString(source.id)}, {"name", QString::fromStdString(source.name)},
        {"url", QString::fromStdString(source.url)}, {"sessionCredentials", credentials_.contains(source.id)}});
    return rows;
}
bool VideoSourcesAdapter::editable() const { return writable_ && canEdit_(); }
QString VideoSourcesAdapter::error() const { return error_; }
void VideoSourcesAdapter::refresh() { emit stateChanged(); }
bool VideoSourcesAdapter::fail(QString message) {
    error_ = std::move(message);
    emit stateChanged();
    return false;
}
VideoSourcesAdapter::Sources VideoSourcesAdapter::runtimeSources(
    const Sources& sources, const Credentials& credentials) const {
    auto runtime = sources;
    for (auto& source : runtime) {
        if (const auto found = credentials.find(source.id); found != credentials.end()) {
            QUrl url(QString::fromStdString(source.url));
            url.setUserName(found->second.first);
            url.setPassword(found->second.second);
            source.url = url.toString(QUrl::FullyEncoded).toStdString();
        }
    }
    return runtime;
}
bool VideoSourcesAdapter::commit(Sources sources, Credentials credentials, bool persist) {
    if (!writable_) return false;
    if (!editable()) return fail(tr("Disconnect the current source before editing network sources."));
    const auto valid = configuration::validateVideoSources(sources);
    if (!valid.hasValue()) return fail(QString::fromStdString(valid.error().operatorSummary));
    if (!updater_(runtimeSources(sources, credentials)).hasValue())
        return fail(tr("The network source list could not be updated."));
    if (persist) {
        const auto saved = catalog_.save(sources);
        if (!saved.hasValue()) {
            if (!updater_(runtimeSources(sources_, credentials_)).hasValue()) {
                writable_ = false;
                return fail(tr("The source update could not be rolled back. Restart Lumora before connecting."));
            }
            return fail(QString::fromStdString(saved.error().operatorSummary));
        }
    }
    sources_ = std::move(sources);
    credentials_ = std::move(credentials);
    error_.clear();
    emit sourcesChanged();
    emit stateChanged();
    refreshDevices_();
    return true;
}
bool VideoSourcesAdapter::addSource(const QString& name, const QString& address,
    const QString& username, const QString& password) {
    if (username.size() > 1024 || password.size() > 1024 || (!password.isEmpty() && username.isEmpty()))
        return fail(tr("Enter a username for password authentication (maximum 1024 characters per field)."));
    auto next = sources_;
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    next.push_back({id, name.trimmed().toStdString(), address.trimmed().toStdString()});
    auto credentials = credentials_;
    if (!username.isEmpty()) credentials[id] = {username, password};
    return commit(std::move(next), std::move(credentials), true);
}
bool VideoSourcesAdapter::removeSource(const QString& id) {
    auto next = sources_;
    if (std::erase_if(next, [&](const auto& source) { return source.id == id.toStdString(); }) == 0)
        return fail(tr("Select a saved network source."));
    auto credentials = credentials_;
    credentials.erase(id.toStdString());
    return commit(std::move(next), std::move(credentials), true);
}
bool VideoSourcesAdapter::setCredentials(const QString& id, const QString& username, const QString& password) {
    if (std::none_of(sources_.begin(), sources_.end(), [&](const auto& source) { return source.id == id.toStdString(); }))
        return fail(tr("Select a saved network source."));
    if (username.size() > 1024 || password.size() > 1024 || (!password.isEmpty() && username.isEmpty()))
        return fail(tr("Enter a username for password authentication (maximum 1024 characters per field)."));
    auto credentials = credentials_;
    if (username.isEmpty()) credentials.erase(id.toStdString());
    else credentials[id.toStdString()] = {username, password};
    return commit(sources_, std::move(credentials), false);
}
}
