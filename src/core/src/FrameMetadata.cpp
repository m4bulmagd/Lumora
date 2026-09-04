#include <lumora/core/FrameMetadata.hpp>

#include <lumora/core/PixelFormat.hpp>

#include <optional>
#include <utility>

namespace lumora::core {

Result<AcquisitionSettingsSnapshot> AcquisitionSettingsSnapshot::create(
    CameraIdentity camera,
    SourcePixelFormat sourceFormat,
    RegionOfInterest roi,
    double requestedFps,
    double actualFps,
    std::optional<double> exposureMicroseconds,
    std::optional<double> gainDb) {
    const auto formatValidation = validateSourcePixelFormat(sourceFormat);
    if (!formatValidation.hasValue()) {
        return Result<AcquisitionSettingsSnapshot>::failure(formatValidation.error());
    }

    return Result<AcquisitionSettingsSnapshot>::success(AcquisitionSettingsSnapshot{
        std::move(camera),
        std::move(sourceFormat),
        roi,
        requestedFps,
        actualFps,
        exposureMicroseconds,
        gainDb,
    });
}

}  // namespace lumora::core
