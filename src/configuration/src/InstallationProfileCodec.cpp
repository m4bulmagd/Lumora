#include <lumora/configuration/InstallationProfileCodec.hpp>
#include <lumora/configuration/CameraProfileCodec.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace lumora::configuration {
namespace {
using Profiles = std::vector<application::InstallationCameraProfile>;
core::Error invalid(std::string detail) {
    return {core::ErrorCategory::Configuration, "installation_invalid_document",
        "The installation file is invalid. Administrator review and explicit repair are required.",
        std::move(detail), true};
}
}

core::Result<QByteArray> InstallationProfileCodec::encode(const Profiles& profiles) {
    const auto valid = application::validateInstallationProfiles(profiles);
    if (!valid.hasValue()) return core::Result<QByteArray>::failure(valid.error());
    QJsonArray records;
    for (const auto& profile : profiles) {
        auto capabilities = encodeCameraCapabilities(
            profile.capabilities, profile.capabilityFingerprintVersion);
        if (!capabilities.hasValue()) {
            return core::Result<QByteArray>::failure(
                invalid(capabilities.error().diagnosticDetail));
        }
        records.append(QJsonObject{
            {"recordVersion", 1},
            // A decimal string preserves every uint64 revision through JSON double parsers.
            {"revision", QString::number(static_cast<qulonglong>(profile.revision))},
            {"identity", encodeCameraIdentity(profile.identity)},
            {"capabilityFingerprintVersion",
                static_cast<double>(profile.capabilityFingerprintVersion)},
            {"capabilities", capabilities.value()},
            {"orientation", QJsonObject{{"flipHorizontal", profile.orientation.flipHorizontal},
                {"flipVertical", profile.orientation.flipVertical},
                {"rotation", static_cast<int>(profile.orientation.rotation) * 90}}},
            {"confirmed", profile.confirmed}});
    }
    return core::Result<QByteArray>::success(QJsonDocument(QJsonObject{
        {"schemaVersion", 2}, {"profiles", records}}).toJson());
}

core::Result<Profiles> InstallationProfileCodec::decode(const QByteArray& bytes) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return core::Result<Profiles>::failure(invalid("Malformed installation JSON."));
    }
    const auto root = document.object();
    const auto schema = root.value("schemaVersion");
    if ((schema != QJsonValue(1) && schema != QJsonValue(2))
        || !root.value("profiles").isArray()) {
        return core::Result<Profiles>::failure(invalid("Unsupported schema or missing profiles array."));
    }
    const auto records = root.value("profiles").toArray();
    if (records.size() > static_cast<qsizetype>(application::MaximumInstallationProfiles)) {
        return core::Result<Profiles>::failure(invalid("Installation collection exceeds 64 identities."));
    }
    Profiles profiles;
    for (const auto& value : records) {
        const auto record = value.toObject();
        const auto orientation = record.value("orientation").toObject();
        const auto revisionText = record.value("revision").toString();
        const auto fingerprintValue = record.value("capabilityFingerprintVersion");
        const auto fingerprintVersion = fingerprintValue == QJsonValue(1) ? 1U
            : fingerprintValue == QJsonValue(2) ? 2U : 0U;
        bool revisionValid = false;
        const auto revision = revisionText.toULongLong(&revisionValid);
        const auto rotation = orientation.value("rotation");
        if (!value.isObject() || record.value("recordVersion") != QJsonValue(1)
            || fingerprintVersion == 0U
            || (schema == QJsonValue(1) && fingerprintVersion != 1U)
            || !record.value("identity").isObject() || !record.value("capabilities").isObject()
            || !record.value("confirmed").isBool() || !record.value("confirmed").toBool()
            || !revisionValid || revision == 0 || QString::number(revision) != revisionText
            || !orientation.value("flipHorizontal").isBool()
            || !orientation.value("flipVertical").isBool()
            || (rotation != QJsonValue(0) && rotation != QJsonValue(90)
                && rotation != QJsonValue(180) && rotation != QJsonValue(270))) {
            return core::Result<Profiles>::failure(invalid("Malformed installation record fields."));
        }
        auto identity = decodeCameraIdentity(record.value("identity").toObject());
        auto capabilities = decodeCameraCapabilities(
            record.value("capabilities").toObject(), fingerprintVersion);
        if (!identity.hasValue() || !capabilities.hasValue()) {
            return core::Result<Profiles>::failure(invalid("Invalid identity or capabilities."));
        }
        profiles.push_back({1U, revision, std::move(identity).value(),
            fingerprintVersion,
            std::move(capabilities).value(),
            {orientation.value("flipHorizontal").toBool(), orientation.value("flipVertical").toBool(),
                static_cast<core::Rotation>(rotation.toInt() / 90)}, true});
    }
    const auto valid = application::validateInstallationProfiles(profiles);
    if (!valid.hasValue()) return core::Result<Profiles>::failure(valid.error());
    return core::Result<Profiles>::success(std::move(profiles));
}
}  // namespace lumora::configuration
