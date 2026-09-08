#include <lumora/processing/ClaheStage.hpp>

#include <lumora/core/CheckedMath.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace lumora::processing {
namespace {

using StageResult = core::Result<std::unique_ptr<ClaheStage>>;

[[nodiscard]] core::Error claheError(
    core::ErrorCategory category,
    std::string code,
    std::string detail,
    bool recoverable = false) {
    return {category, std::move(code), "Local contrast processing failed.",
        std::move(detail), recoverable};
}

[[nodiscard]] StageResult factoryFailure(std::string code, std::string detail) {
    return StageResult::failure(claheError(
        core::ErrorCategory::Processing, std::move(code), std::move(detail)));
}

[[nodiscard]] core::Result<void> processFailure(
    std::string code,
    std::string detail) {
    return core::Result<void>::failure(claheError(
        core::ErrorCategory::Processing, std::move(code), std::move(detail)));
}

[[nodiscard]] bool canWrapU16(
    const std::byte* data,
    std::size_t strideBytes) noexcept {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return address % alignof(std::uint16_t) == 0U
        && strideBytes % sizeof(std::uint16_t) == 0U;
}

void copyToBridge(
    const ImageView& source,
    std::span<std::uint16_t> bridge) noexcept {
    const auto rowBytes = source.layout().rowBytes();
    const auto width = static_cast<std::size_t>(source.layout().width());
    for (std::uint32_t y = 0U; y < source.layout().height(); ++y) {
        std::memcpy(bridge.data() + static_cast<std::size_t>(y) * width,
            source.row(y).data(), rowBytes);
    }
}

void copyFromBridge(
    std::span<const std::uint16_t> bridge,
    MutableImageView destination) noexcept {
    const auto rowBytes = destination.layout().rowBytes();
    const auto width = static_cast<std::size_t>(destination.layout().width());
    for (std::uint32_t y = 0U; y < destination.layout().height(); ++y) {
        std::memcpy(destination.row(y).data(),
            bridge.data() + static_cast<std::size_t>(y) * width, rowBytes);
    }
}

}  // namespace

struct ClaheStage::Impl final {
    Impl(
        int widthValue,
        int heightValue,
        std::size_t rowBytesValue,
        cv::Ptr<cv::CLAHE> algorithmValue,
        std::vector<std::uint16_t> sourceBridgeValue,
        std::vector<std::uint16_t> destinationBridgeValue)
        : width(widthValue),
          height(heightValue),
          rowBytes(rowBytesValue),
          algorithm(std::move(algorithmValue)),
          sourceBridge(std::move(sourceBridgeValue)),
          destinationBridge(std::move(destinationBridgeValue)) {}

    int width;
    int height;
    std::size_t rowBytes;
    // One processing worker owns each instance. apply() mutates these caches
    // even though the public stage operation is logically const.
    cv::Ptr<cv::CLAHE> algorithm;
    std::vector<std::uint16_t> sourceBridge;
    std::vector<std::uint16_t> destinationBridge;
};

ClaheStage::ClaheStage(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

ClaheStage::~ClaheStage() = default;

core::Result<std::unique_ptr<ClaheStage>> ClaheStage::create(
    ClaheParameters parameters,
    const core::ImageLayout& layout) {
    if (!std::isfinite(parameters.clipLimit)
        || parameters.clipLimit < 0.1
        || parameters.clipLimit > 40.0) {
        return factoryFailure("clahe_invalid_clip_limit",
            "CLAHE clip limit must be finite and within [0.1, 40].");
    }
    if (parameters.tileGridSize < 2U || parameters.tileGridSize > 32U) {
        return factoryFailure("clahe_invalid_tile_grid",
            "CLAHE tile grid size must be an integer within [2, 32].");
    }
    if (layout.storage() != core::StorageType::UInt16) {
        return factoryFailure("clahe_layout_storage_mismatch",
            "CLAHE preparation requires unsigned 16-bit storage.");
    }
    if (layout.width() < parameters.tileGridSize
        || layout.height() < parameters.tileGridSize) {
        return factoryFailure("clahe_image_too_small",
            "Prepared width and height must each be at least the tile grid size.");
    }

    constexpr auto maximumCvDimension =
        static_cast<std::uint32_t>(std::numeric_limits<int>::max());
    if (layout.width() > maximumCvDimension
        || layout.height() > maximumCvDimension) {
        return factoryFailure("clahe_dimension_out_of_range",
            "Prepared dimensions do not fit OpenCV signed image dimensions.");
    }

    const auto grid = static_cast<std::uint64_t>(parameters.tileGridSize);
    std::uint64_t reflectedWidth = layout.width();
    std::uint64_t reflectedHeight = layout.height();
    const bool needsReflection = layout.width() % parameters.tileGridSize != 0U
        || layout.height() % parameters.tileGridSize != 0U;
    if (needsReflection) {
        // Pinned OpenCV 4.12.0 adds a complete grid span on an already-divisible
        // axis whenever the other axis needs reflection.
        reflectedWidth += grid - layout.width() % parameters.tileGridSize;
        reflectedHeight += grid - layout.height() % parameters.tileGridSize;
        if (reflectedWidth > maximumCvDimension
            || reflectedHeight > maximumCvDimension) {
            return factoryFailure("clahe_reflected_extent_overflow",
                "OpenCV's reflected CLAHE extent does not fit signed dimensions.");
        }
    }

    const auto tileWidth = reflectedWidth / grid;
    const auto tileHeight = reflectedHeight / grid;
    if (tileWidth != 0U
        && tileHeight > static_cast<std::uint64_t>(
               std::numeric_limits<int>::max()) / tileWidth) {
        return factoryFailure("clahe_tile_area_overflow",
            "CLAHE tile area does not fit OpenCV's signed accumulator.");
    }
    if (layout.width() > maximumCvDimension / 4U) {
        return factoryFailure("clahe_interpolation_size_overflow",
            "CLAHE interpolation width-times-four does not fit a signed integer.");
    }

    const auto sampleCount = core::checkedMultiply(
        static_cast<std::size_t>(layout.width()),
        static_cast<std::size_t>(layout.height()));
    if (!sampleCount.hasValue()) {
        return factoryFailure("clahe_bridge_size_overflow",
            "Computing the prepared U16 bridge image size overflowed size_t.");
    }
    const auto rowBytes = core::checkedMultiply(
        static_cast<std::size_t>(layout.width()), sizeof(std::uint16_t));
    if (!rowBytes.hasValue()) {
        return factoryFailure("clahe_bridge_size_overflow",
            "Computing the prepared U16 bridge row size overflowed size_t.");
    }

    try {
        auto sourceBridge = std::vector<std::uint16_t>(sampleCount.value(), 0U);
        auto destinationBridge = std::vector<std::uint16_t>(sampleCount.value(), 0U);
        auto algorithm = cv::createCLAHE(parameters.clipLimit,
            cv::Size(static_cast<int>(parameters.tileGridSize),
                static_cast<int>(parameters.tileGridSize)));
        const auto width = static_cast<int>(layout.width());
        const auto height = static_cast<int>(layout.height());
        cv::Mat warmSource(height, width, CV_16UC1,
            sourceBridge.data(), rowBytes.value());
        cv::Mat warmDestination(height, width, CV_16UC1,
            destinationBridge.data(), rowBytes.value());
        // Retain OpenCV's LUT/reflection buffers. The pinned backend still makes
        // other per-apply allocations; factory warming does not remove that debt.
        algorithm->apply(warmSource, warmDestination);
        auto impl = std::make_unique<Impl>(width, height, rowBytes.value(),
            std::move(algorithm), std::move(sourceBridge),
            std::move(destinationBridge));
        return StageResult::success(
            std::unique_ptr<ClaheStage>(new ClaheStage(std::move(impl))));
    } catch (const std::bad_alloc&) {
        return StageResult::failure(claheError(core::ErrorCategory::ResourceExhaustion,
            "clahe_allocation_failed",
            "CLAHE preparation could not allocate its fixed bridge or backend storage.",
            true));
    } catch (const cv::Exception& exception) {
        return factoryFailure("clahe_backend_error",
            std::string{"OpenCV rejected CLAHE preparation: "} + exception.what());
    } catch (const std::exception& exception) {
        return factoryFailure("clahe_backend_error",
            std::string{"CLAHE preparation failed: "} + exception.what());
    } catch (...) {
        return factoryFailure("clahe_backend_error",
            "CLAHE preparation failed with an unknown backend exception.");
    }
}

StageId ClaheStage::id() const noexcept {
    return StageId::Clahe;
}

const StageTraits& ClaheStage::traits() const noexcept {
    static constexpr StageTraits traits{StageId::Clahe,
        ImageDomain::CanonicalU16, ImageDomain::CanonicalU16, false, 2U, 0U, false,
        ExecutionBackend::Cpu};
    return traits;
}

core::Result<void> ClaheStage::process(
    const ImageView& source,
    MutableImageView destination,
    const core::SourcePixelFormat&) const {
    if (source.layout().storage() != core::StorageType::UInt16) {
        return processFailure("clahe_source_storage_mismatch",
            "CLAHE input must use unsigned 16-bit storage.");
    }
    if (destination.layout().storage() != core::StorageType::UInt16) {
        return processFailure("clahe_destination_storage_mismatch",
            "CLAHE output must use unsigned 16-bit storage.");
    }
    if (source.domain() != ImageDomain::CanonicalU16
        || destination.domain() != ImageDomain::CanonicalU16) {
        return processFailure("clahe_image_domain_mismatch",
            "CLAHE requires CanonicalU16 input and output.");
    }
    if (source.layout().width() != destination.layout().width()
        || source.layout().height() != destination.layout().height()) {
        return processFailure("clahe_image_extent_mismatch",
            "CLAHE input and output extents must match.");
    }
    if (source.layout().width() != static_cast<std::uint32_t>(impl_->width)
        || source.layout().height() != static_cast<std::uint32_t>(impl_->height)) {
        return processFailure("clahe_prepared_extent_mismatch",
            "CLAHE image extents must match the prepared stage extents.");
    }
    if (overlaps(source, destination)) {
        return processFailure("clahe_image_views_overlap",
            "CLAHE input and output payload storage must not overlap.");
    }

    try {
        cv::Mat sourceMat;
        if (canWrapU16(source.bytes().data(), source.layout().strideBytes())) {
            sourceMat = cv::Mat(impl_->height, impl_->width, CV_16UC1,
                const_cast<std::byte*>(source.bytes().data()),
                source.layout().strideBytes());
        } else {
            copyToBridge(source, impl_->sourceBridge);
            sourceMat = cv::Mat(impl_->height, impl_->width, CV_16UC1,
                impl_->sourceBridge.data(), impl_->rowBytes);
        }

        const bool bridgeDestination = !canWrapU16(
            destination.bytes().data(), destination.layout().strideBytes());
        cv::Mat destinationMat = bridgeDestination
            ? cv::Mat(impl_->height, impl_->width, CV_16UC1,
                  impl_->destinationBridge.data(), impl_->rowBytes)
            : cv::Mat(impl_->height, impl_->width, CV_16UC1,
                  destination.bytes().data(), destination.layout().strideBytes());

        impl_->algorithm->apply(sourceMat, destinationMat);
        if (bridgeDestination) {
            copyFromBridge(impl_->destinationBridge, destination);
        }
        return core::Result<void>::success();
    } catch (const std::bad_alloc&) {
        return core::Result<void>::failure(claheError(
            core::ErrorCategory::ResourceExhaustion, "clahe_allocation_failed",
            "OpenCV could not allocate its per-apply CLAHE working storage.", true));
    } catch (const cv::Exception& exception) {
        return processFailure("clahe_backend_error",
            std::string{"OpenCV CLAHE processing failed: "} + exception.what());
    } catch (const std::exception& exception) {
        return processFailure("clahe_backend_error",
            std::string{"CLAHE processing failed: "} + exception.what());
    } catch (...) {
        return processFailure("clahe_backend_error",
            "CLAHE processing failed with an unknown backend exception.");
    }
}

}  // namespace lumora::processing
