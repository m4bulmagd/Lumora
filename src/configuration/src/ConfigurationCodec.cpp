#include <lumora/configuration/ConfigurationCodec.hpp>

#include <lumora/application/StartupPreferences.hpp>
#include <lumora/core/Error.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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

[[nodiscard]] core::Result<double> readFiniteDouble(
    const QJsonObject& object,
    const char* key) {
    const auto value = object.value(QLatin1String(key));
    if (!value.isDouble() || !std::isfinite(value.toDouble())) {
        return invalidStartup<double>(
            std::string{"The startup field '"} + key + "' must be finite.");
    }
    return core::Result<double>::success(value.toDouble());
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

[[nodiscard]] core::Result<QJsonArray> readArray(
    const QJsonObject& object,
    const char* key) {
    const auto value = object.value(QLatin1String(key));
    if (!value.isArray()) {
        return invalidStartup<QJsonArray>(
            std::string{"The startup field '"} + key + "' must be an array.");
    }
    return core::Result<QJsonArray>::success(value.toArray());
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

[[nodiscard]] QJsonObject encodeNumeric(const camera::NumericCapability& value) {
    return {{"minimum", value.minimum}, {"maximum", value.maximum},
        {"increment", value.increment},
        {"writableWhileStreaming", value.writableWhileStreaming}};
}

[[nodiscard]] core::Result<camera::NumericCapability> decodeNumeric(
    const QJsonObject& object) {
    auto minimum = readFiniteDouble(object, "minimum");
    auto maximum = readFiniteDouble(object, "maximum");
    auto increment = readFiniteDouble(object, "increment");
    const auto writable = object.value("writableWhileStreaming");
    if (!minimum.hasValue() || !maximum.hasValue() || !increment.hasValue()
        || !writable.isBool()) {
        return invalidStartup<camera::NumericCapability>(
            "A startup numeric capability is invalid.");
    }
    return core::Result<camera::NumericCapability>::success(
        {minimum.value(), maximum.value(), increment.value(), writable.toBool()});
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

[[nodiscard]] QJsonObject encodeCapabilities(camera::CameraCapabilities value) {
    std::sort(value.pixelFormats.begin(), value.pixelFormats.end(), [](const auto& left, const auto& right) {
        return std::tie(left.canonicalName, left.canonicalEncoding, left.validBits,
                   left.sampleMaximum, left.packing, left.alignment, left.applicationStorage)
            < std::tie(right.canonicalName, right.canonicalEncoding, right.validBits,
                right.sampleMaximum, right.packing, right.alignment, right.applicationStorage);
    });
    std::sort(value.exposureModes.begin(), value.exposureModes.end());
    std::sort(value.gainModes.begin(), value.gainModes.end());

    QJsonArray formats;
    for (const auto& format : value.pixelFormats) {
        formats.append(encodePixelFormat(format));
    }
    QJsonArray exposureModes;
    for (const auto mode : value.exposureModes) {
        exposureModes.append(exposureModeName(mode));
    }
    QJsonArray gainModes;
    for (const auto mode : value.gainModes) {
        gainModes.append(gainModeName(mode));
    }
    return {{"pixelFormats", formats},
        {"roi", QJsonObject{{"minimum", encodeRegion(value.roi.minimum)},
                    {"maximum", encodeRegion(value.roi.maximum)},
                    {"increment", encodeRegion(value.roi.increment)}}},
        {"frameRate", encodeNumeric(value.frameRate)},
        {"exposure", encodeNumeric(value.exposure)},
        {"exposureModes", exposureModes}, {"gain", encodeNumeric(value.gain)},
        {"gainModes", gainModes}};
}

[[nodiscard]] core::Result<camera::CameraCapabilities> decodeCapabilities(
    const QJsonObject& object) {
    auto formatsJson = readArray(object, "pixelFormats");
    auto roiJson = readObject(object, "roi");
    auto frameRateJson = readObject(object, "frameRate");
    auto exposureJson = readObject(object, "exposure");
    auto exposureModesJson = readArray(object, "exposureModes");
    auto gainJson = readObject(object, "gain");
    auto gainModesJson = readArray(object, "gainModes");
    if (!formatsJson.hasValue() || !roiJson.hasValue() || !frameRateJson.hasValue()
        || !exposureJson.hasValue() || !exposureModesJson.hasValue()
        || !gainJson.hasValue() || !gainModesJson.hasValue()) {
        return invalidStartup<camera::CameraCapabilities>(
            "The startup capability snapshot is incomplete.");
    }

    std::vector<core::SourcePixelFormat> formats;
    for (const auto& value : formatsJson.value()) {
        if (!value.isObject()) {
            return invalidStartup<camera::CameraCapabilities>(
                "A startup pixel-format descriptor is not an object.");
        }
        auto format = decodePixelFormat(value.toObject());
        if (!format.hasValue()) {
            return invalidStartup<camera::CameraCapabilities>(format.error().diagnosticDetail);
        }
        formats.push_back(std::move(format).value());
    }

    auto roiMinimumJson = readObject(roiJson.value(), "minimum");
    auto roiMaximumJson = readObject(roiJson.value(), "maximum");
    auto roiIncrementJson = readObject(roiJson.value(), "increment");
    if (!roiMinimumJson.hasValue() || !roiMaximumJson.hasValue()
        || !roiIncrementJson.hasValue()) {
        return invalidStartup<camera::CameraCapabilities>(
            "The startup ROI capability is incomplete.");
    }
    auto roiMinimum = decodeRegion(roiMinimumJson.value());
    auto roiMaximum = decodeRegion(roiMaximumJson.value());
    auto roiIncrement = decodeRegion(roiIncrementJson.value());
    auto frameRate = decodeNumeric(frameRateJson.value());
    auto exposure = decodeNumeric(exposureJson.value());
    auto gain = decodeNumeric(gainJson.value());
    if (!roiMinimum.hasValue() || !roiMaximum.hasValue() || !roiIncrement.hasValue()
        || !frameRate.hasValue() || !exposure.hasValue() || !gain.hasValue()) {
        return invalidStartup<camera::CameraCapabilities>(
            "A startup capability value is invalid.");
    }

    std::vector<camera::ExposureMode> exposureModes;
    for (const auto& value : exposureModesJson.value()) {
        auto mode = parseExposureMode(value);
        if (!mode.hasValue()) {
            return invalidStartup<camera::CameraCapabilities>(mode.error().diagnosticDetail);
        }
        exposureModes.push_back(mode.value());
    }
    std::vector<camera::GainMode> gainModes;
    for (const auto& value : gainModesJson.value()) {
        auto mode = parseGainMode(value);
        if (!mode.hasValue()) {
            return invalidStartup<camera::CameraCapabilities>(mode.error().diagnosticDetail);
        }
        gainModes.push_back(mode.value());
    }

    return core::Result<camera::CameraCapabilities>::success({std::move(formats),
        {roiMinimum.value(), roiMaximum.value(), roiIncrement.value()}, frameRate.value(),
        exposure.value(), std::move(exposureModes), gain.value(), std::move(gainModes)});
}

[[nodiscard]] QJsonObject encodeConfiguration(const camera::CameraConfiguration& value) {
    return {{"pixelFormat", encodePixelFormat(value.pixelFormat)},
        {"roi", encodeRegion(value.roi)},
        {"requestedFps", value.requestedFps ? QJsonValue{*value.requestedFps} : QJsonValue{}},
        {"exposure", QJsonObject{{"mode", exposureModeName(value.exposure.mode)},
                         {"requestedMicroseconds", value.exposure.requestedMicroseconds
                                 ? QJsonValue{*value.exposure.requestedMicroseconds}
                                 : QJsonValue{}}}},
        {"gain", QJsonObject{{"mode", gainModeName(value.gain.mode)},
                     {"requestedDb", value.gain.requestedDb
                             ? QJsonValue{*value.gain.requestedDb}
                             : QJsonValue{}}}},
        {"acquisitionMode", acquisitionModeName(value.acquisitionMode)}};
}

[[nodiscard]] core::Result<camera::CameraConfiguration> decodeConfiguration(
    const QJsonObject& object) {
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
    auto exposureMode = parseExposureMode(exposureJson.value().value("mode"));
    auto exposureValue = readOptionalDouble(exposureJson.value(), "requestedMicroseconds");
    auto gainMode = parseGainMode(gainJson.value().value("mode"));
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

[[nodiscard]] QJsonObject encodeStartup(const application::StartupPreferences& value) {
    QJsonObject identity{{"manufacturer", QString::fromStdString(value.identity.manufacturer)},
        {"model", QString::fromStdString(value.identity.model)},
        {"serial", QString::fromStdString(value.identity.serial)},
        {"transport", QString::fromStdString(value.identity.transport)},
        {"firmware", value.identity.firmware
                ? QJsonValue{QString::fromStdString(*value.identity.firmware)}
                : QJsonValue{}}};
    return {{"recordVersion", static_cast<double>(value.recordVersion)},
        {"cameraId", QString::fromStdString(value.cameraId.value)},
        {"identity", identity},
        {"confirmedCapabilities", encodeCapabilities(value.confirmedCapabilities)},
        {"requested", encodeConfiguration(value.requested)},
        {"lastApplied", encodeConfiguration(value.lastApplied)},
        {"confirmed", value.confirmed}};
}

[[nodiscard]] core::Result<application::StartupPreferences> decodeStartup(
    const QJsonObject& object) {
    auto version = readUint32(object, "recordVersion");
    auto cameraId = readString(object, "cameraId");
    auto identityJson = readObject(object, "identity");
    auto capabilitiesJson = readObject(object, "confirmedCapabilities");
    auto requestedJson = readObject(object, "requested");
    auto lastAppliedJson = readObject(object, "lastApplied");
    const auto confirmed = object.value("confirmed");
    if (!version.hasValue() || !cameraId.hasValue() || !identityJson.hasValue()
        || !capabilitiesJson.hasValue() || !requestedJson.hasValue()
        || !lastAppliedJson.hasValue() || !confirmed.isBool()) {
        return invalidStartup<application::StartupPreferences>(
            "The startup record is incomplete.");
    }
    auto manufacturer = readString(identityJson.value(), "manufacturer");
    auto model = readString(identityJson.value(), "model");
    auto serial = readString(identityJson.value(), "serial");
    auto transport = readString(identityJson.value(), "transport");
    std::optional<std::string> firmware;
    const auto firmwareJson = identityJson.value().value("firmware");
    if (firmwareJson.isString()) {
        firmware = firmwareJson.toString().toStdString();
    } else if (!firmwareJson.isNull()) {
        return invalidStartup<application::StartupPreferences>(
            "The startup firmware field must be a string or null.");
    }
    auto capabilities = decodeCapabilities(capabilitiesJson.value());
    auto requested = decodeConfiguration(requestedJson.value());
    auto lastApplied = decodeConfiguration(lastAppliedJson.value());
    if (!manufacturer.hasValue() || !model.hasValue() || !serial.hasValue()
        || !transport.hasValue() || !capabilities.hasValue() || !requested.hasValue()
        || !lastApplied.hasValue()) {
        return invalidStartup<application::StartupPreferences>(
            "A startup record value is invalid.");
    }

    application::StartupPreferences startup{version.value(),
        {std::move(cameraId).value()},
        {std::move(manufacturer).value(), std::move(model).value(),
            std::move(serial).value(), std::move(transport).value(), std::move(firmware)},
        std::move(capabilities).value(), std::move(requested).value(),
        std::move(lastApplied).value(), confirmed.toBool()};
    const auto validated = application::validateStartupPreferences(startup);
    if (!validated.hasValue()) {
        return invalidStartup<application::StartupPreferences>(
            validated.error().diagnosticDetail);
    }
    return core::Result<application::StartupPreferences>::success(std::move(startup));
}

[[nodiscard]] core::Result<ApplicationConfiguration> decodeObject(const QJsonObject& root) {
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

    ApplicationConfiguration configuration;
    configuration.schemaVersion = ApplicationConfiguration::CurrentSchemaVersion;
    configuration.application = root.value("application").toObject();
    configuration.cameraProfiles = root.value("cameraProfiles").toObject();
    configuration.processing = root.value("processing").toObject();
    configuration.presets = root.value("presets").toObject();
    configuration.capture = root.value("capture").toObject();
    configuration.ui = root.value("ui").toObject();
    if (schemaVersion == 1) {
        return core::Result<ApplicationConfiguration>::success(std::move(configuration));
    }

    const auto startupJson = root.value("startup");
    if (startupJson.isNull()) {
        return core::Result<ApplicationConfiguration>::success(std::move(configuration));
    }
    if (!startupJson.isObject()) {
        return core::Result<ApplicationConfiguration>::failure(configurationError(
            "configuration_invalid_startup", "Saved startup preferences are invalid.",
            "The startup field must be an object or null."));
    }
    auto startup = decodeStartup(startupJson.toObject());
    if (!startup.hasValue()) {
        return core::Result<ApplicationConfiguration>::failure(startup.error());
    }
    configuration.startup = std::move(startup).value();
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
    if (configuration.startup) {
        const auto validated = application::validateStartupPreferences(*configuration.startup);
        if (!validated.hasValue()) {
            return core::Result<QByteArray>::failure(configurationError(
                "configuration_invalid_startup", "Saved startup preferences are invalid.",
                validated.error().diagnosticDetail));
        }
    }
    QJsonObject root{{"schemaVersion", configuration.schemaVersion},
        {"application", configuration.application},
        {"cameraProfiles", configuration.cameraProfiles},
        {"processing", configuration.processing}, {"presets", configuration.presets},
        {"capture", configuration.capture}, {"ui", configuration.ui},
        {"startup", configuration.startup ? QJsonValue{encodeStartup(*configuration.startup)}
                                            : QJsonValue{}}};

    const auto validated = decodeObject(root);
    if (!validated.hasValue()) {
        return core::Result<QByteArray>::failure(validated.error());
    }
    return core::Result<QByteArray>::success(
        QJsonDocument(root).toJson(QJsonDocument::Indented));
}

}  // namespace lumora::configuration
