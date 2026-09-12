#include <lumora/configuration/CameraProfileCodec.hpp>

#include <lumora/application/CameraProfile.hpp>

#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace lumora::configuration {
namespace {

[[nodiscard]] core::Error codecError(std::string detail) {
    return {core::ErrorCategory::Configuration, "camera_profile_codec_invalid",
        "Saved camera profile data is invalid.", std::move(detail), true};
}

template<typename T>
[[nodiscard]] core::Result<T> invalid(std::string detail) {
    return core::Result<T>::failure(codecError(std::move(detail)));
}

[[nodiscard]] bool exactInteger(const QJsonValue& value, double maximum) {
    if (!value.isDouble()) return false;
    const auto number = value.toDouble();
    return std::isfinite(number) && std::floor(number) == number
        && number >= 0.0 && number <= maximum;
}

[[nodiscard]] core::Result<std::uint32_t> readUint32(
    const QJsonObject& object, const char* key) {
    const auto value = object.value(QLatin1String(key));
    if (!exactInteger(value, std::numeric_limits<std::uint32_t>::max())) {
        return invalid<std::uint32_t>(
            std::string{"The camera profile field '"} + key
            + "' must be an unsigned integer.");
    }
    return core::Result<std::uint32_t>::success(
        static_cast<std::uint32_t>(value.toDouble()));
}

[[nodiscard]] core::Result<double> readFiniteDouble(
    const QJsonObject& object, const char* key) {
    const auto value = object.value(QLatin1String(key));
    if (!value.isDouble() || !std::isfinite(value.toDouble())) {
        return invalid<double>(
            std::string{"The camera profile field '"} + key + "' must be finite.");
    }
    return core::Result<double>::success(value.toDouble());
}

[[nodiscard]] core::Result<std::string> readString(
    const QJsonObject& object, const char* key) {
    const auto value = object.value(QLatin1String(key));
    if (!value.isString()) {
        return invalid<std::string>(
            std::string{"The camera profile field '"} + key + "' must be a string.");
    }
    return core::Result<std::string>::success(value.toString().toStdString());
}

[[nodiscard]] core::Result<QJsonObject> readObject(
    const QJsonObject& object, const char* key) {
    const auto value = object.value(QLatin1String(key));
    if (!value.isObject()) {
        return invalid<QJsonObject>(
            std::string{"The camera profile field '"} + key + "' must be an object.");
    }
    return core::Result<QJsonObject>::success(value.toObject());
}

[[nodiscard]] core::Result<QJsonArray> readArray(
    const QJsonObject& object, const char* key) {
    const auto value = object.value(QLatin1String(key));
    if (!value.isArray()) {
        return invalid<QJsonArray>(
            std::string{"The camera profile field '"} + key + "' must be an array.");
    }
    return core::Result<QJsonArray>::success(value.toArray());
}

template<typename Enum>
[[nodiscard]] core::Result<Enum> invalidEnum(const char* key) {
    return invalid<Enum>(std::string{"The camera profile enum field '"} + key
        + "' is invalid.");
}

[[nodiscard]] QString storageName(core::StorageType value) {
    return value == core::StorageType::UInt8 ? QStringLiteral("UInt8")
                                             : QStringLiteral("UInt16");
}

[[nodiscard]] core::Result<core::StorageType> parseStorage(
    const QJsonObject& object, const char* key) {
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
    const QJsonObject& object, const char* key) {
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
    const QJsonObject& object, const char* key) {
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
        return invalid<core::SourcePixelFormat>(
            "A camera profile pixel format descriptor is invalid.");
    }
    core::SourcePixelFormat result{std::move(name).value(), encoding.value(),
        static_cast<std::uint8_t>(validBits.value()),
        static_cast<std::uint16_t>(sampleMaximum.value()), packing.value(),
        alignment.value(), storage.value()};
    const auto validated = core::validateSourcePixelFormat(result);
    if (!validated.hasValue()) {
        return invalid<core::SourcePixelFormat>(validated.error().diagnosticDetail);
    }
    return core::Result<core::SourcePixelFormat>::success(std::move(result));
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
        return invalid<core::RegionOfInterest>("A camera profile ROI is invalid.");
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
        return invalid<camera::NumericCapability>(
            "A camera profile numeric capability is invalid.");
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

[[nodiscard]] core::Result<camera::GainMode> parseGainMode(
    const QJsonValue& value) {
    if (value == QLatin1String("Manual")) {
        return core::Result<camera::GainMode>::success(camera::GainMode::Manual);
    }
    if (value == QLatin1String("Auto")) {
        return core::Result<camera::GainMode>::success(camera::GainMode::Auto);
    }
    return invalidEnum<camera::GainMode>("gainMode");
}

}  // namespace

QJsonObject encodeCameraIdentity(const core::CameraIdentity& identity) {
    return {{"manufacturer", QString::fromStdString(identity.manufacturer)},
        {"model", QString::fromStdString(identity.model)},
        {"serial", QString::fromStdString(identity.serial)},
        {"transport", QString::fromStdString(identity.transport)},
        {"firmware", identity.firmware
                ? QJsonValue{QString::fromStdString(*identity.firmware)}
                : QJsonValue{}}};
}

core::Result<core::CameraIdentity> decodeCameraIdentity(
    const QJsonObject& object) {
    auto manufacturer = readString(object, "manufacturer");
    auto model = readString(object, "model");
    auto serial = readString(object, "serial");
    auto transport = readString(object, "transport");
    std::optional<std::string> firmware;
    const auto firmwareJson = object.value("firmware");
    if (firmwareJson.isString()) {
        firmware = firmwareJson.toString().toStdString();
    } else if (!firmwareJson.isNull()) {
        return invalid<core::CameraIdentity>(
            "The camera firmware field must be a string or null.");
    }
    if (!manufacturer.hasValue() || !model.hasValue() || !serial.hasValue()
        || !transport.hasValue()) {
        return invalid<core::CameraIdentity>("The camera identity is incomplete.");
    }
    return core::Result<core::CameraIdentity>::success(
        {std::move(manufacturer).value(), std::move(model).value(),
            std::move(serial).value(), std::move(transport).value(),
            std::move(firmware)});
}

QJsonObject encodeCameraCapabilities(camera::CameraCapabilities value) {
    std::sort(value.pixelFormats.begin(), value.pixelFormats.end(),
        [](const auto& left, const auto& right) {
            return std::tie(left.canonicalName, left.canonicalEncoding, left.validBits,
                       left.sampleMaximum, left.packing, left.alignment,
                       left.applicationStorage)
                < std::tie(right.canonicalName, right.canonicalEncoding,
                    right.validBits, right.sampleMaximum, right.packing,
                    right.alignment, right.applicationStorage);
        });
    std::sort(value.exposureModes.begin(), value.exposureModes.end());
    std::sort(value.gainModes.begin(), value.gainModes.end());

    QJsonArray formats;
    for (const auto& format : value.pixelFormats) formats.append(encodePixelFormat(format));
    QJsonArray exposureModes;
    for (const auto mode : value.exposureModes) {
        exposureModes.append(exposureModeName(mode));
    }
    QJsonArray gainModes;
    for (const auto mode : value.gainModes) gainModes.append(gainModeName(mode));
    return {{"pixelFormats", formats},
        {"roi", QJsonObject{{"minimum", encodeRegion(value.roi.minimum)},
                    {"maximum", encodeRegion(value.roi.maximum)},
                    {"increment", encodeRegion(value.roi.increment)}}},
        {"frameRate", encodeNumeric(value.frameRate)},
        {"exposure", encodeNumeric(value.exposure)},
        {"exposureModes", exposureModes}, {"gain", encodeNumeric(value.gain)},
        {"gainModes", gainModes}};
}

core::Result<camera::CameraCapabilities> decodeCameraCapabilities(
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
        return invalid<camera::CameraCapabilities>(
            "The camera capability snapshot is incomplete.");
    }

    std::vector<core::SourcePixelFormat> formats;
    for (const auto& value : formatsJson.value()) {
        if (!value.isObject()) {
            return invalid<camera::CameraCapabilities>(
                "A camera pixel format descriptor is not an object.");
        }
        auto format = decodePixelFormat(value.toObject());
        if (!format.hasValue()) {
            return invalid<camera::CameraCapabilities>(format.error().diagnosticDetail);
        }
        formats.push_back(std::move(format).value());
    }

    auto roiMinimumJson = readObject(roiJson.value(), "minimum");
    auto roiMaximumJson = readObject(roiJson.value(), "maximum");
    auto roiIncrementJson = readObject(roiJson.value(), "increment");
    if (!roiMinimumJson.hasValue() || !roiMaximumJson.hasValue()
        || !roiIncrementJson.hasValue()) {
        return invalid<camera::CameraCapabilities>(
            "The camera ROI capability is incomplete.");
    }
    auto roiMinimum = decodeRegion(roiMinimumJson.value());
    auto roiMaximum = decodeRegion(roiMaximumJson.value());
    auto roiIncrement = decodeRegion(roiIncrementJson.value());
    auto frameRate = decodeNumeric(frameRateJson.value());
    auto exposure = decodeNumeric(exposureJson.value());
    auto gain = decodeNumeric(gainJson.value());
    if (!roiMinimum.hasValue() || !roiMaximum.hasValue() || !roiIncrement.hasValue()
        || !frameRate.hasValue() || !exposure.hasValue() || !gain.hasValue()) {
        return invalid<camera::CameraCapabilities>(
            "A camera capability value is invalid.");
    }

    std::vector<camera::ExposureMode> exposureModes;
    for (const auto& value : exposureModesJson.value()) {
        auto mode = parseExposureMode(value);
        if (!mode.hasValue()) {
            return invalid<camera::CameraCapabilities>(mode.error().diagnosticDetail);
        }
        exposureModes.push_back(mode.value());
    }
    std::vector<camera::GainMode> gainModes;
    for (const auto& value : gainModesJson.value()) {
        auto mode = parseGainMode(value);
        if (!mode.hasValue()) {
            return invalid<camera::CameraCapabilities>(mode.error().diagnosticDetail);
        }
        gainModes.push_back(mode.value());
    }

    camera::CameraCapabilities result{std::move(formats),
        {roiMinimum.value(), roiMaximum.value(), roiIncrement.value()},
        frameRate.value(), exposure.value(), std::move(exposureModes), gain.value(),
        std::move(gainModes)};
    const auto validated = application::validateCameraCapabilities(result);
    if (!validated.hasValue()) {
        return invalid<camera::CameraCapabilities>(validated.error().diagnosticDetail);
    }
    return core::Result<camera::CameraCapabilities>::success(std::move(result));
}

}  // namespace lumora::configuration
