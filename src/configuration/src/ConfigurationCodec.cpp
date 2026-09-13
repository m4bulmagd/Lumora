#include <lumora/configuration/ConfigurationCodec.hpp>
#include <lumora/configuration/CameraProfileCodec.hpp>
#include <lumora/configuration/PresetCodec.hpp>

#include <lumora/application/StartupPreferences.hpp>
#include <lumora/core/Error.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace lumora::configuration {
namespace {

[[nodiscard]] core::Error configurationError(
    std::string code,
    std::string operatorSummary,
    std::string diagnosticDetail) {
    return {core::ErrorCategory::Configuration, std::move(code),
        std::move(operatorSummary), std::move(diagnosticDetail), true};
}

template<typename T>
[[nodiscard]] core::Result<T> invalidStartup(std::string detail) {
    return core::Result<T>::failure(configurationError(
        "configuration_invalid_startup",
        "Saved startup preferences are invalid.", std::move(detail)));
}

[[nodiscard]] bool exactInteger(const QJsonValue& value, double maximum) {
    if (!value.isDouble()) {
        return false;
    }
    const auto number = value.toDouble();
    return std::isfinite(number) && std::floor(number) == number
        && number >= 0.0 && number <= maximum;
}

[[nodiscard]] core::Result<std::uint32_t> readUint32(
    const QJsonObject& object,
    const char* key) {
    const auto value = object.value(QLatin1String(key));
    if (!exactInteger(value, std::numeric_limits<std::uint32_t>::max())) {
        return invalidStartup<std::uint32_t>(
            std::string{"The startup field '"} + key + "' must be an unsigned integer.");
    }
    return core::Result<std::uint32_t>::success(
        static_cast<std::uint32_t>(value.toDouble()));
}

[[nodiscard]] core::Result<std::optional<double>> readOptionalDouble(
    const QJsonObject& object,
    const char* key) {
    const auto value = object.value(QLatin1String(key));
    if (value.isNull()) {
        return core::Result<std::optional<double>>::success(std::nullopt);
    }
    if (!value.isDouble() || !std::isfinite(value.toDouble())) {
        return invalidStartup<std::optional<double>>(
            std::string{"The startup field '"} + key + "' must be null or finite.");
    }
    return core::Result<std::optional<double>>::success(value.toDouble());
}

[[nodiscard]] core::Result<std::string> readString(
    const QJsonObject& object,
    const char* key) {
    const auto value = object.value(QLatin1String(key));
    if (!value.isString()) {
        return invalidStartup<std::string>(
            std::string{"The startup field '"} + key + "' must be a string.");
    }
    return core::Result<std::string>::success(value.toString().toStdString());
}

[[nodiscard]] core::Result<QJsonObject> readObject(
    const QJsonObject& object,
    const char* key) {
    const auto value = object.value(QLatin1String(key));
    if (!value.isObject()) {
        return invalidStartup<QJsonObject>(
            std::string{"The startup field '"} + key + "' must be an object.");
    }
    return core::Result<QJsonObject>::success(value.toObject());
}

template<typename Enum>
[[nodiscard]] core::Result<Enum> invalidEnum(const char* key) {
    return invalidStartup<Enum>(
        std::string{"The startup enum field '"} + key + "' is invalid.");
}

[[nodiscard]] QString storageName(core::StorageType value) {
    return value == core::StorageType::UInt8 ? QStringLiteral("UInt8")
                                             : QStringLiteral("UInt16");
}

[[nodiscard]] core::Result<core::StorageType> parseStorage(
    const QJsonObject& object,
    const char* key) {
    const auto text = object.value(QLatin1String(key));
    if (text == QLatin1String("UInt8")) {
        return core::Result<core::StorageType>::success(core::StorageType::UInt8);
    }
    if (text == QLatin1String("UInt16")) {
        return core::Result<core::StorageType>::success(core::StorageType::UInt16);
    }
    return invalidEnum<core::StorageType>(key);
}

[[nodiscard]] QString packingName(core::SourcePacking value) {
    return value == core::SourcePacking::Unpacked ? QStringLiteral("Unpacked")
                                                   : QStringLiteral("Packed");
}

[[nodiscard]] core::Result<core::SourcePacking> parsePacking(
    const QJsonObject& object,
    const char* key) {
    const auto text = object.value(QLatin1String(key));
    if (text == QLatin1String("Unpacked")) {
        return core::Result<core::SourcePacking>::success(core::SourcePacking::Unpacked);
    }
    if (text == QLatin1String("Packed")) {
        return core::Result<core::SourcePacking>::success(core::SourcePacking::Packed);
    }
    return invalidEnum<core::SourcePacking>(key);
}

[[nodiscard]] QString alignmentName(core::BitAlignment value) {
    return value == core::BitAlignment::LeastSignificant
        ? QStringLiteral("LeastSignificant")
        : QStringLiteral("MostSignificant");
}

[[nodiscard]] core::Result<core::BitAlignment> parseAlignment(
    const QJsonObject& object,
    const char* key) {
    const auto text = object.value(QLatin1String(key));
    if (text == QLatin1String("LeastSignificant")) {
        return core::Result<core::BitAlignment>::success(
            core::BitAlignment::LeastSignificant);
    }
    if (text == QLatin1String("MostSignificant")) {
        return core::Result<core::BitAlignment>::success(
            core::BitAlignment::MostSignificant);
    }
    return invalidEnum<core::BitAlignment>(key);
}

[[nodiscard]] QJsonObject encodePixelFormat(const core::SourcePixelFormat& value) {
    return {{"canonicalName", QString::fromStdString(value.canonicalName)},
        {"canonicalEncoding", static_cast<double>(value.canonicalEncoding)},
        {"validBits", value.validBits}, {"sampleMaximum", value.sampleMaximum},
        {"packing", packingName(value.packing)},
        {"alignment", alignmentName(value.alignment)},
        {"applicationStorage", storageName(value.applicationStorage)}};
}

[[nodiscard]] core::Result<core::SourcePixelFormat> decodePixelFormat(
    const QJsonObject& object) {
    auto name = readString(object, "canonicalName");
    auto encoding = readUint32(object, "canonicalEncoding");
    auto validBits = readUint32(object, "validBits");
    auto sampleMaximum = readUint32(object, "sampleMaximum");
    auto packing = parsePacking(object, "packing");
    auto alignment = parseAlignment(object, "alignment");
    auto storage = parseStorage(object, "applicationStorage");
    if (!name.hasValue() || !encoding.hasValue() || !validBits.hasValue()
        || !sampleMaximum.hasValue() || !packing.hasValue() || !alignment.hasValue()
        || !storage.hasValue() || validBits.value() > std::numeric_limits<std::uint8_t>::max()
        || sampleMaximum.value() > std::numeric_limits<std::uint16_t>::max()) {
        return invalidStartup<core::SourcePixelFormat>(
            "A startup pixel-format descriptor is invalid.");
    }
    return core::Result<core::SourcePixelFormat>::success({std::move(name).value(),
        encoding.value(), static_cast<std::uint8_t>(validBits.value()),
        static_cast<std::uint16_t>(sampleMaximum.value()), packing.value(),
        alignment.value(), storage.value()});
}

[[nodiscard]] QJsonObject encodeRegion(const core::RegionOfInterest& value) {
    return {{"x", static_cast<double>(value.x)}, {"y", static_cast<double>(value.y)},
        {"width", static_cast<double>(value.width)},
        {"height", static_cast<double>(value.height)}};
}

[[nodiscard]] core::Result<core::RegionOfInterest> decodeRegion(
    const QJsonObject& object) {
    auto x = readUint32(object, "x");
    auto y = readUint32(object, "y");
    auto width = readUint32(object, "width");
    auto height = readUint32(object, "height");
    if (!x.hasValue() || !y.hasValue() || !width.hasValue() || !height.hasValue()) {
        return invalidStartup<core::RegionOfInterest>("A startup ROI is invalid.");
    }
    return core::Result<core::RegionOfInterest>::success(
        {x.value(), y.value(), width.value(), height.value()});
}

[[nodiscard]] QString exposureModeName(camera::ExposureMode value) {
    return value == camera::ExposureMode::Manual ? QStringLiteral("Manual")
                                                  : QStringLiteral("Auto");
}

[[nodiscard]] core::Result<camera::ExposureMode> parseExposureMode(
    const QJsonValue& value) {
    if (value == QLatin1String("Manual")) {
        return core::Result<camera::ExposureMode>::success(camera::ExposureMode::Manual);
    }
    if (value == QLatin1String("Auto")) {
        return core::Result<camera::ExposureMode>::success(camera::ExposureMode::Auto);
    }
    return invalidEnum<camera::ExposureMode>("exposureMode");
}

[[nodiscard]] QString gainModeName(camera::GainMode value) {
    return value == camera::GainMode::Manual ? QStringLiteral("Manual")
                                              : QStringLiteral("Auto");
}

[[nodiscard]] core::Result<camera::GainMode> parseGainMode(const QJsonValue& value) {
    if (value == QLatin1String("Manual")) {
        return core::Result<camera::GainMode>::success(camera::GainMode::Manual);
    }
    if (value == QLatin1String("Auto")) {
        return core::Result<camera::GainMode>::success(camera::GainMode::Auto);
    }
    return invalidEnum<camera::GainMode>("gainMode");
}

[[nodiscard]] core::Result<std::optional<camera::ExposureMode>>
parseOptionalExposureMode(
    const QJsonObject& object,
    std::uint32_t fingerprintVersion) {
    const auto value = object.value("mode");
    if (fingerprintVersion == 2U && value.isNull()) {
        return core::Result<std::optional<camera::ExposureMode>>::success(
            std::nullopt);
    }
    auto mode = parseExposureMode(value);
    if (!mode.hasValue()) {
        return invalidStartup<std::optional<camera::ExposureMode>>(
            mode.error().diagnosticDetail);
    }
    return core::Result<std::optional<camera::ExposureMode>>::success(
        mode.value());
}

[[nodiscard]] core::Result<std::optional<camera::GainMode>>
parseOptionalGainMode(
    const QJsonObject& object,
    std::uint32_t fingerprintVersion) {
    const auto value = object.value("mode");
    if (fingerprintVersion == 2U && value.isNull()) {
        return core::Result<std::optional<camera::GainMode>>::success(
            std::nullopt);
    }
    auto mode = parseGainMode(value);
    if (!mode.hasValue()) {
        return invalidStartup<std::optional<camera::GainMode>>(
            mode.error().diagnosticDetail);
    }
    return core::Result<std::optional<camera::GainMode>>::success(mode.value());
}

[[nodiscard]] QString acquisitionModeName(camera::AcquisitionMode value) {
    return value == camera::AcquisitionMode::Continuous ? QStringLiteral("Continuous")
                                                         : QStringLiteral("Triggered");
}

[[nodiscard]] core::Result<camera::AcquisitionMode> parseAcquisitionMode(
    const QJsonValue& value) {
    if (value == QLatin1String("Continuous")) {
        return core::Result<camera::AcquisitionMode>::success(
            camera::AcquisitionMode::Continuous);
    }
    if (value == QLatin1String("Triggered")) {
        return core::Result<camera::AcquisitionMode>::success(
            camera::AcquisitionMode::Triggered);
    }
    return invalidEnum<camera::AcquisitionMode>("acquisitionMode");
}

[[nodiscard]] core::Result<QJsonObject> encodeConfiguration(
    const camera::CameraConfiguration& value,
    std::uint32_t fingerprintVersion) {
    if (fingerprintVersion != 1U && fingerprintVersion != 2U) {
        return invalidStartup<QJsonObject>(
            "The capability fingerprint version is unsupported.");
    }
    if (fingerprintVersion == 1U
        && (!value.exposure.mode || !value.gain.mode)) {
        return invalidStartup<QJsonObject>(
            "Legacy camera configurations require exposure and gain modes.");
    }
    const QJsonValue exposureMode = value.exposure.mode
        ? QJsonValue{exposureModeName(*value.exposure.mode)} : QJsonValue{};
    const QJsonValue gainMode = value.gain.mode
        ? QJsonValue{gainModeName(*value.gain.mode)} : QJsonValue{};
    return core::Result<QJsonObject>::success({
        {"pixelFormat", encodePixelFormat(value.pixelFormat)},
        {"roi", encodeRegion(value.roi)},
        {"requestedFps", value.requestedFps ? QJsonValue{*value.requestedFps} : QJsonValue{}},
        {"exposure", QJsonObject{{"mode", exposureMode},
                         {"requestedMicroseconds", value.exposure.requestedMicroseconds
                                 ? QJsonValue{*value.exposure.requestedMicroseconds}
                                 : QJsonValue{}}}},
        {"gain", QJsonObject{{"mode", gainMode},
                     {"requestedDb", value.gain.requestedDb
                             ? QJsonValue{*value.gain.requestedDb}
                             : QJsonValue{}}}},
        {"acquisitionMode", acquisitionModeName(value.acquisitionMode)}});
}

[[nodiscard]] core::Result<camera::CameraConfiguration> decodeConfiguration(
    const QJsonObject& object,
    std::uint32_t fingerprintVersion) {
    auto pixelJson = readObject(object, "pixelFormat");
    auto roiJson = readObject(object, "roi");
    auto fps = readOptionalDouble(object, "requestedFps");
    auto exposureJson = readObject(object, "exposure");
    auto gainJson = readObject(object, "gain");
    auto acquisitionMode = parseAcquisitionMode(object.value("acquisitionMode"));
    if (!pixelJson.hasValue() || !roiJson.hasValue() || !fps.hasValue()
        || !exposureJson.hasValue() || !gainJson.hasValue()
        || !acquisitionMode.hasValue()) {
        return invalidStartup<camera::CameraConfiguration>(
            "The startup camera configuration is incomplete.");
    }
    auto pixel = decodePixelFormat(pixelJson.value());
    auto roi = decodeRegion(roiJson.value());
    auto exposureMode = parseOptionalExposureMode(
        exposureJson.value(), fingerprintVersion);
    auto exposureValue = readOptionalDouble(exposureJson.value(), "requestedMicroseconds");
    auto gainMode = parseOptionalGainMode(gainJson.value(), fingerprintVersion);
    auto gainValue = readOptionalDouble(gainJson.value(), "requestedDb");
    if (!pixel.hasValue() || !roi.hasValue() || !exposureMode.hasValue()
        || !exposureValue.hasValue() || !gainMode.hasValue() || !gainValue.hasValue()) {
        return invalidStartup<camera::CameraConfiguration>(
            "A startup camera configuration value is invalid.");
    }
    return core::Result<camera::CameraConfiguration>::success({std::move(pixel).value(),
        roi.value(), fps.value(), {exposureMode.value(), exposureValue.value()},
        {gainMode.value(), gainValue.value()}, acquisitionMode.value()});
}

[[nodiscard]] QString rotationName(core::Rotation rotation) {
    switch (rotation) {
    case core::Rotation::Degrees0: return QStringLiteral("Degrees0");
    case core::Rotation::Degrees90: return QStringLiteral("Degrees90");
    case core::Rotation::Degrees180: return QStringLiteral("Degrees180");
    case core::Rotation::Degrees270: return QStringLiteral("Degrees270");
    }
    return {};
}

[[nodiscard]] core::Result<core::Rotation> decodeRotation(const QJsonValue& value) {
    if (value == QLatin1String("Degrees0")) {
        return core::Result<core::Rotation>::success(core::Rotation::Degrees0);
    }
    if (value == QLatin1String("Degrees90")) {
        return core::Result<core::Rotation>::success(core::Rotation::Degrees90);
    }
    if (value == QLatin1String("Degrees180")) {
        return core::Result<core::Rotation>::success(core::Rotation::Degrees180);
    }
    if (value == QLatin1String("Degrees270")) {
        return core::Result<core::Rotation>::success(core::Rotation::Degrees270);
    }
    return invalidStartup<core::Rotation>(
        "The installation profile rotation is invalid.");
}

[[nodiscard]] core::Result<std::uint64_t> readUint64String(
    const QJsonObject& object, const char* key) {
    const auto value = object.value(QLatin1String(key));
    if (!value.isString()) {
        return invalidStartup<std::uint64_t>(
            "The installation profile revision must be a decimal string.");
    }
    const auto text = value.toString();
    bool valid = false;
    const auto number = text.toULongLong(&valid);
    if (!valid || number == 0U
        || QString::number(static_cast<qulonglong>(number)) != text) {
        return invalidStartup<std::uint64_t>(
            "The installation profile revision must be a canonical positive uint64 string.");
    }
    return core::Result<std::uint64_t>::success(
        static_cast<std::uint64_t>(number));
}

[[nodiscard]] QJsonObject encodeInstallationReference(
    const application::InstallationProfileReference& value) {
    return {{"recordVersion", static_cast<double>(value.recordVersion)},
        {"revision", QString::number(static_cast<qulonglong>(value.revision))},
        {"orientation", QJsonObject{{"flipHorizontal", value.orientation.flipHorizontal},
                            {"flipVertical", value.orientation.flipVertical},
                            {"rotation", rotationName(value.orientation.rotation)}}}};
}

[[nodiscard]] core::Result<application::InstallationProfileReference>
decodeInstallationReference(const QJsonObject& object) {
    auto version = readUint32(object, "recordVersion");
    auto revision = readUint64String(object, "revision");
    auto orientationJson = readObject(object, "orientation");
    if (!version.hasValue() || !revision.hasValue() || !orientationJson.hasValue()) {
        return invalidStartup<application::InstallationProfileReference>(
            "The installation profile reference is incomplete.");
    }
    const auto horizontal = orientationJson.value().value("flipHorizontal");
    const auto vertical = orientationJson.value().value("flipVertical");
    auto rotation = decodeRotation(orientationJson.value().value("rotation"));
    if (!horizontal.isBool() || !vertical.isBool() || !rotation.hasValue()) {
        return invalidStartup<application::InstallationProfileReference>(
            "The installation profile orientation is invalid.");
    }
    application::InstallationProfileReference result{version.value(), revision.value(),
        {horizontal.toBool(), vertical.toBool(), rotation.value()}};
    if (result.recordVersion != 1U) {
        return invalidStartup<application::InstallationProfileReference>(
            "Only installation profile reference version 1 is supported.");
    }
    return core::Result<application::InstallationProfileReference>::success(
        std::move(result));
}

[[nodiscard]] core::Result<QJsonObject> encodeStartup(
    const application::StartupPreferences& value) {
    auto capabilities = encodeCameraCapabilities(
        value.confirmedCapabilities, value.capabilityFingerprintVersion);
    auto requested = encodeConfiguration(
        value.requested, value.capabilityFingerprintVersion);
    auto lastApplied = encodeConfiguration(
        value.lastApplied, value.capabilityFingerprintVersion);
    if (!capabilities.hasValue() || !requested.hasValue()
        || !lastApplied.hasValue()) {
        const auto& error = !capabilities.hasValue() ? capabilities.error()
            : !requested.hasValue() ? requested.error() : lastApplied.error();
        return invalidStartup<QJsonObject>(error.diagnosticDetail);
    }
    return core::Result<QJsonObject>::success({
        {"recordVersion", static_cast<double>(value.recordVersion)},
        {"cameraId", QString::fromStdString(value.cameraId.value)},
        {"identity", encodeCameraIdentity(value.identity)},
        {"capabilityFingerprintVersion",
            static_cast<double>(value.capabilityFingerprintVersion)},
        {"confirmedCapabilities", capabilities.value()},
        {"requested", requested.value()},
        {"lastApplied", lastApplied.value()},
        {"confirmed", value.confirmed},
        {"installationProfile", value.installationProfile
                ? QJsonValue{encodeInstallationReference(*value.installationProfile)}
                : QJsonValue{}}});
}

[[nodiscard]] core::Result<application::StartupPreferences> decodeStartup(
    const QJsonObject& object) {
    auto version = readUint32(object, "recordVersion");
    auto cameraId = readString(object, "cameraId");
    auto identityJson = readObject(object, "identity");
    auto fingerprintVersion = readUint32(object, "capabilityFingerprintVersion");
    auto capabilitiesJson = readObject(object, "confirmedCapabilities");
    auto requestedJson = readObject(object, "requested");
    auto lastAppliedJson = readObject(object, "lastApplied");
    const auto confirmed = object.value("confirmed");
    if (!version.hasValue() || !cameraId.hasValue() || !identityJson.hasValue()
        || !fingerprintVersion.hasValue()
        || !capabilitiesJson.hasValue() || !requestedJson.hasValue()
        || !lastAppliedJson.hasValue() || !confirmed.isBool()) {
        return invalidStartup<application::StartupPreferences>(
            "The startup record is incomplete.");
    }
    auto identity = decodeCameraIdentity(identityJson.value());
    auto capabilities = decodeCameraCapabilities(
        capabilitiesJson.value(), fingerprintVersion.value());
    auto requested = decodeConfiguration(
        requestedJson.value(), fingerprintVersion.value());
    auto lastApplied = decodeConfiguration(
        lastAppliedJson.value(), fingerprintVersion.value());
    std::optional<application::InstallationProfileReference> installationProfile;
    const auto installationJson = object.value("installationProfile");
    if (installationJson.isObject()) {
        auto decoded = decodeInstallationReference(installationJson.toObject());
        if (!decoded.hasValue()) {
            return invalidStartup<application::StartupPreferences>(
                decoded.error().diagnosticDetail);
        }
        installationProfile = std::move(decoded).value();
    } else if (!installationJson.isNull()) {
        return invalidStartup<application::StartupPreferences>(
            "The installation profile reference must be an object or null.");
    }
    if (!identity.hasValue() || !capabilities.hasValue() || !requested.hasValue()
        || !lastApplied.hasValue()) {
        return invalidStartup<application::StartupPreferences>(
            "A startup record value is invalid.");
    }

    application::StartupPreferences startup{version.value(),
        {std::move(cameraId).value()},
        std::move(identity).value(),
        std::move(capabilities).value(), std::move(requested).value(),
        std::move(lastApplied).value(), confirmed.toBool(), fingerprintVersion.value(),
        std::move(installationProfile)};
    const auto validated = application::validateStartupPreferences(startup);
    if (!validated.hasValue()) {
        return invalidStartup<application::StartupPreferences>(
            validated.error().diagnosticDetail);
    }
    return core::Result<application::StartupPreferences>::success(std::move(startup));
}

[[nodiscard]] core::Result<void> validateCameraPreferences(
    const application::CameraPreferences& preferences) {
    if (preferences.profiles.size() > application::CameraPreferences::MaximumProfiles) {
        return core::Result<void>::failure(configurationError(
            "configuration_camera_profile_capacity",
            "Too many camera profiles are saved.",
            "At most 64 distinct camera identities may be saved."));
    }
    if (preferences.lastSelectedCameraId
        && preferences.lastSelectedCameraId->value.empty()) {
        return core::Result<void>::failure(configurationError(
            "configuration_invalid_camera_profiles",
            "Saved camera preferences are invalid.",
            "The selected logical camera ID must not be empty."));
    }
    for (auto current = preferences.profiles.begin();
         current != preferences.profiles.end(); ++current) {
        const auto validated = application::validateStartupPreferences(*current);
        if (!validated.hasValue()) {
            return core::Result<void>::failure(configurationError(
                "configuration_invalid_camera_profiles",
                "Saved camera preferences are invalid.",
                validated.error().diagnosticDetail));
        }
        if (std::any_of(std::next(current), preferences.profiles.end(),
                [&](const auto& candidate) {
                    return application::cameraIdentityKeysEqual(
                        current->identity, candidate.identity);
                })) {
            return core::Result<void>::failure(configurationError(
                "configuration_duplicate_camera_profile",
                "Saved camera preferences are invalid.",
                "Only one preference record is allowed for each manufacturer/model/serial identity."));
        }
    }
    return core::Result<void>::success();
}

[[nodiscard]] core::Result<QJsonObject> encodeCameraPreferences(
    application::CameraPreferences preferences) {
    QJsonArray profiles;
    for (const auto& profile : preferences.profiles) {
        auto encoded = encodeStartup(profile);
        if (!encoded.hasValue()) {
            return core::Result<QJsonObject>::failure(encoded.error());
        }
        profiles.append(encoded.value());
    }
    return core::Result<QJsonObject>::success({
        {"lastSelectedCameraId", preferences.lastSelectedCameraId
                ? QJsonValue{QString::fromStdString(
                      preferences.lastSelectedCameraId->value)}
                : QJsonValue{}},
        {"profiles", profiles}});
}

[[nodiscard]] core::Result<application::CameraPreferences> decodeCameraPreferences(
    const QJsonObject& object) {
    const auto selectedJson = object.value("lastSelectedCameraId");
    std::optional<camera::CameraId> selected;
    if (selectedJson.isString()) {
        selected = camera::CameraId{selectedJson.toString().toStdString()};
    } else if (!selectedJson.isNull()) {
        return core::Result<application::CameraPreferences>::failure(configurationError(
            "configuration_invalid_camera_profiles",
            "Saved camera preferences are invalid.",
            "lastSelectedCameraId must be a string or null."));
    }
    const auto profilesJson = object.value("profiles");
    if (!profilesJson.isArray()) {
        return core::Result<application::CameraPreferences>::failure(configurationError(
            "configuration_invalid_camera_profiles",
            "Saved camera preferences are invalid.",
            "The camera profile collection must be an array."));
    }
    application::CameraPreferences result;
    result.lastSelectedCameraId = std::move(selected);
    for (const auto& value : profilesJson.toArray()) {
        if (!value.isObject()) {
            return core::Result<application::CameraPreferences>::failure(
                configurationError("configuration_invalid_camera_profiles",
                    "Saved camera preferences are invalid.",
                    "Each camera profile must be an object."));
        }
        auto decoded = decodeStartup(value.toObject());
        if (!decoded.hasValue()) {
            return core::Result<application::CameraPreferences>::failure(
                configurationError("configuration_invalid_camera_profiles",
                    "Saved camera preferences are invalid.",
                    decoded.error().diagnosticDetail));
        }
        result.profiles.push_back(std::move(decoded).value());
    }
    const auto validated = validateCameraPreferences(result);
    if (!validated.hasValue()) {
        return core::Result<application::CameraPreferences>::failure(
            validated.error());
    }
    return core::Result<application::CameraPreferences>::success(std::move(result));
}

[[nodiscard]] std::optional<application::StartupPreferences> deriveStartup(
    const application::CameraPreferences& preferences) {
    if (!preferences.lastSelectedCameraId) return std::nullopt;
    const auto found = std::find_if(preferences.profiles.rbegin(),
        preferences.profiles.rend(), [&](const auto& profile) {
            return profile.cameraId == *preferences.lastSelectedCameraId;
        });
    return found == preferences.profiles.rend()
        ? std::nullopt
        : std::optional<application::StartupPreferences>{*found};
}

[[nodiscard]] QJsonObject migrateOneToTwo(QJsonObject root) {
    root.insert("schemaVersion", 2);
    root.insert("startup", QJsonValue{});
    return root;
}

[[nodiscard]] core::Result<QJsonObject> migrateTwoToThree(QJsonObject root) {
    auto defaults = PresetCodec::loadDefaultRepository();
    if (!defaults.hasValue()) return core::Result<QJsonObject>::failure(defaults.error());
    const auto presets = PresetCodec::encode(defaults.value().snapshot(), root.value("presets").toObject());
    if (!presets.hasValue()) return core::Result<QJsonObject>::failure(presets.error());
    root.insert("schemaVersion", 3);
    root.insert("presets", presets.value());
    return core::Result<QJsonObject>::success(std::move(root));
}

[[nodiscard]] core::Result<QJsonObject> migrateThreeToFour(QJsonObject root) {
    const auto legacyProfiles = root.value("cameraProfiles").toObject();
    if (!root.contains("startup")
        || (!root.value("startup").isNull()
            && !root.value("startup").isObject())) {
        return core::Result<QJsonObject>::failure(configurationError(
            "configuration_invalid_startup",
            "Saved startup preferences are invalid.",
            "The schema 2/3 startup field must be present and contain an object or null."));
    }
    const auto startup = root.value("startup");
    QJsonArray profiles;
    QJsonValue selected;
    if (startup.isObject()) {
        auto migratedStartup = startup.toObject();
        migratedStartup.insert("capabilityFingerprintVersion", 1);
        migratedStartup.insert("installationProfile", QJsonValue{});
        profiles.append(migratedStartup);
        selected = migratedStartup.value("cameraId");
    }
    root.insert("schemaVersion", 4);
    root.insert("legacyCameraProfiles", legacyProfiles);
    root.insert("cameraProfiles", QJsonObject{{"lastSelectedCameraId", selected},
                                      {"profiles", profiles}});
    root.remove("startup");
    return core::Result<QJsonObject>::success(std::move(root));
}

[[nodiscard]] QJsonObject migrateFourToFive(QJsonObject root) {
    // Fingerprint version 1 records remain explicit legacy provenance. The
    // document migration changes only the envelope version and never promotes
    // a retained camera record to current access authority.
    root.insert("schemaVersion", 5);
    return root;
}

[[nodiscard]] core::Result<ApplicationConfiguration> decodeObject(QJsonObject root) {
    const auto schemaValue = root.value("schemaVersion");
    if (!schemaValue.isDouble()) {
        return core::Result<ApplicationConfiguration>::failure(configurationError(
            "configuration_missing_schema",
            "The configuration version is missing or invalid.",
            "The root schemaVersion must be an integer."));
    }
    const auto schemaNumber = schemaValue.toDouble();
    if (!std::isfinite(schemaNumber) || std::floor(schemaNumber) != schemaNumber
        || schemaNumber < static_cast<double>(std::numeric_limits<int>::min())
        || schemaNumber > static_cast<double>(std::numeric_limits<int>::max())) {
        return core::Result<ApplicationConfiguration>::failure(configurationError(
            "configuration_invalid_schema", "The configuration version is invalid.",
            "The root schemaVersion is not a representable integer."));
    }
    const auto schemaVersion = static_cast<int>(schemaNumber);
    if (schemaVersion > ApplicationConfiguration::CurrentSchemaVersion) {
        return core::Result<ApplicationConfiguration>::failure(configurationError(
            "configuration_future_schema",
            "This configuration was created by a newer Lumora version.",
            "The stored schemaVersion is newer than the supported schema."));
    }
    if (schemaVersion < 1) {
        return core::Result<ApplicationConfiguration>::failure(configurationError(
            "configuration_unsupported_schema",
            "This configuration version is not supported.",
            "No migration is available for the stored schemaVersion."));
    }

    constexpr std::array<std::string_view, 6> sectionNames{
        "application", "cameraProfiles", "processing", "presets", "capture", "ui"};
    for (const auto sectionName : sectionNames) {
        const auto section = root.value(QString::fromLatin1(
            sectionName.data(), static_cast<qsizetype>(sectionName.size())));
        if (!section.isObject()) {
            return core::Result<ApplicationConfiguration>::failure(configurationError(
                "configuration_invalid_section",
                "A configuration section is missing or invalid.",
                "The '" + std::string(sectionName) + "' section must be a JSON object."));
        }
    }

    const bool retainedLegacy = schemaVersion < 3 && !root.value("presets").toObject().isEmpty();
    if (schemaVersion == 1) root = migrateOneToTwo(std::move(root));
    if (root.value("schemaVersion").toInt() == 2) {
        auto migrated = migrateTwoToThree(std::move(root));
        if (!migrated.hasValue()) return core::Result<ApplicationConfiguration>::failure(migrated.error());
        root = std::move(migrated).value();
    }
    if (root.value("schemaVersion").toInt() == 3) {
        auto migrated = migrateThreeToFour(std::move(root));
        if (!migrated.hasValue()) {
            return core::Result<ApplicationConfiguration>::failure(
                migrated.error());
        }
        root = std::move(migrated).value();
    }
    if (root.value("schemaVersion").toInt() == 4) {
        root = migrateFourToFive(std::move(root));
    }
    if (!root.value("legacyCameraProfiles").isObject()) {
        return core::Result<ApplicationConfiguration>::failure(configurationError(
            "configuration_invalid_section",
            "A configuration section is missing or invalid.",
            "The 'legacyCameraProfiles' section must be a JSON object."));
    }
    auto presets = PresetCodec::decode(root.value("presets").toObject());
    if (!presets.hasValue()) return core::Result<ApplicationConfiguration>::failure(presets.error());

    ApplicationConfiguration configuration;
    configuration.schemaVersion = ApplicationConfiguration::CurrentSchemaVersion;
    configuration.application = root.value("application").toObject();
    auto cameraPreferences =
        decodeCameraPreferences(root.value("cameraProfiles").toObject());
    if (!cameraPreferences.hasValue()) {
        return core::Result<ApplicationConfiguration>::failure(
            cameraPreferences.error());
    }
    configuration.cameraProfiles = std::move(cameraPreferences).value();
    configuration.legacyCameraProfiles =
        root.value("legacyCameraProfiles").toObject();
    configuration.processing = root.value("processing").toObject();
    configuration.presets = std::move(presets.value().state);
    configuration.legacyPresets = std::move(presets.value().legacy);
    configuration.presetIssues = std::move(presets.value().issues);
    configuration.capture = root.value("capture").toObject();
    configuration.ui = root.value("ui").toObject();
    if (retainedLegacy) {
        configuration.presetIssues.push_back({{}, {}, configurationError(
            "configuration_presets_migrated", "Legacy preset metadata was retained.",
            "Opaque schema1/2 preset metadata was preserved under legacy; Original remains selected.")});
    }
    if (!configuration.presetIssues.empty()) {
        configuration.loadWarning = configurationError(
            "configuration_presets_recovered", "Saved preset settings were recovered.",
            std::to_string(configuration.presetIssues.size())
                + " preset issue(s) were recorded; valid settings remain available for saving.");
    }

    configuration.startup = deriveStartup(configuration.cameraProfiles);
    return core::Result<ApplicationConfiguration>::success(std::move(configuration));
}

}  // namespace

core::Result<ApplicationConfiguration> ConfigurationCodec::decode(const QByteArray& contents) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(contents, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return core::Result<ApplicationConfiguration>::failure(configurationError(
            "configuration_invalid_json", "The configuration file is not valid JSON.",
            "JSON parse error at byte " + std::to_string(parseError.offset) + ": "
                + parseError.errorString().toStdString()));
    }
    if (!document.isObject()) {
        return core::Result<ApplicationConfiguration>::failure(configurationError(
            "configuration_invalid_root", "The configuration file has an invalid structure.",
            "The JSON document root must be an object."));
    }
    return decodeObject(document.object());
}

core::Result<QByteArray> ConfigurationCodec::encode(
    const ApplicationConfiguration& configuration) {
    if (configuration.schemaVersion != ApplicationConfiguration::CurrentSchemaVersion) {
        return core::Result<QByteArray>::failure(configurationError(
            configuration.schemaVersion > ApplicationConfiguration::CurrentSchemaVersion
                ? "configuration_future_schema"
                : "configuration_unsupported_schema",
            "The configuration version is invalid.",
            "Only the current schema can be encoded."));
    }
    auto cameraPreferences = configuration.cameraProfiles;
    if (cameraPreferences.profiles.empty() && configuration.startup) {
        cameraPreferences.profiles.push_back(*configuration.startup);
        if (!cameraPreferences.lastSelectedCameraId) {
            cameraPreferences.lastSelectedCameraId = configuration.startup->cameraId;
        }
    }
    const auto cameraValidated = validateCameraPreferences(cameraPreferences);
    if (!cameraValidated.hasValue()) {
        return core::Result<QByteArray>::failure(cameraValidated.error());
    }
    const auto presets = PresetCodec::encode(configuration.presets, configuration.legacyPresets);
    if (!presets.hasValue()) return core::Result<QByteArray>::failure(presets.error());
    auto encodedCameraPreferences = encodeCameraPreferences(cameraPreferences);
    if (!encodedCameraPreferences.hasValue()) {
        return core::Result<QByteArray>::failure(
            encodedCameraPreferences.error());
    }
    QJsonObject root{{"schemaVersion", configuration.schemaVersion},
        {"application", configuration.application},
        {"cameraProfiles", encodedCameraPreferences.value()},
        {"legacyCameraProfiles", configuration.legacyCameraProfiles},
        {"processing", configuration.processing}, {"presets", presets.value()},
        {"capture", configuration.capture}, {"ui", configuration.ui}};

    const auto validated = decodeObject(root);
    if (!validated.hasValue()) {
        return core::Result<QByteArray>::failure(validated.error());
    }
    return core::Result<QByteArray>::success(
        QJsonDocument(root).toJson(QJsonDocument::Indented));
}

}  // namespace lumora::configuration
