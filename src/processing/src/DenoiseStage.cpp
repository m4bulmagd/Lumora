#include <lumora/processing/DenoiseStage.hpp>
#include "StageStorage.hpp"
#include "PreparedCpuExecutor.hpp"

#include "DetailStageSupport.hpp"

#include <opencv2/core.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace lumora::processing {
namespace {

using FactoryResult = core::Result<std::unique_ptr<DenoiseStage>>;

[[nodiscard]] core::Result<void> validateParameters(
    DenoiseParameters parameters,
    const core::ImageLayout& layout) {
    const auto fail = [](std::string code, std::string message) {
        return core::Result<void>::failure(detail::stageError("denoise",
            core::ErrorCategory::Processing, std::move(code), std::move(message)));
    };
    if (parameters.mode != DenoiseMode::Gaussian
        && parameters.mode != DenoiseMode::Median) {
        return fail("denoise_invalid_mode",
            "Denoise mode must be Gaussian or Median.");
    }
    if (!std::isfinite(parameters.sigma)
        || parameters.sigma < 0.0
        || parameters.sigma > 5.0) {
        return fail("denoise_invalid_sigma",
            "Denoise sigma must be finite and within [0, 5].");
    }
    if (parameters.mode == DenoiseMode::Gaussian
        && parameters.kernelSize != 3U
        && parameters.kernelSize != 5U
        && parameters.kernelSize != 7U) {
        return fail("denoise_invalid_kernel_size",
            "Gaussian denoise kernel size must be 3, 5, or 7.");
    }
    if (parameters.mode == DenoiseMode::Median
        && parameters.kernelSize != 3U
        && parameters.kernelSize != 5U) {
        return fail("denoise_invalid_kernel_size",
            "Median denoise kernel size must be 3 or 5.");
    }
    if (parameters.mode == DenoiseMode::Median && parameters.sigma != 0.0) {
        return fail("denoise_invalid_sigma",
            "Median denoise requires sigma to be exactly zero.");
    }
    if (layout.storage() != core::StorageType::UInt16) {
        return fail("denoise_layout_storage_mismatch",
            "Denoise preparation requires unsigned 16-bit storage.");
    }
    return core::Result<void>::success();
}

[[nodiscard]] FactoryResult factoryFailure(
    std::string code,
    std::string message,
    core::ErrorCategory category = core::ErrorCategory::Processing,
    bool recoverable = false) {
    return FactoryResult::failure(detail::stageError(
        "denoise", category, std::move(code), std::move(message), recoverable));
}

}  // namespace

struct DenoiseStage::Impl final {
    Impl(
        DenoiseParameters parametersValue,
        std::uint32_t widthValue,
        std::uint32_t heightValue,
        std::size_t scratchBytesValue,
        std::unique_ptr<double[]> intermediateValue,
        std::unique_ptr<double[]> coefficientsValue)
        : parameters(parametersValue),
          width(widthValue),
          height(heightValue),
          scratchBytes(scratchBytesValue),
          intermediate(std::move(intermediateValue)),
          coefficients(std::move(coefficientsValue)) {}

    detail::PreparedCpuExecutor* executor{};
    const DenoiseParameters parameters;
    const std::uint32_t width;
    const std::uint32_t height;
    const std::size_t scratchBytes;
    std::unique_ptr<double[]> intermediate;
    std::unique_ptr<double[]> coefficients;
};

core::Result<std::size_t> detail::StageStorage::denoiseScratchBytes(
    DenoiseParameters parameters, const core::ImageLayout& layout, std::size_t) {
    return DenoiseStage::requiredScratchBytes(parameters, layout);
}

std::size_t detail::StageStorage::denoiseOwnerBytes() noexcept {
    return sizeof(DenoiseStage) + sizeof(DenoiseStage::Impl);
}

DenoiseStage::DenoiseStage(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
DenoiseStage::~DenoiseStage() = default;

core::Result<std::size_t> DenoiseStage::requiredScratchBytes(
    DenoiseParameters parameters,
    const core::ImageLayout& layout) {
    const auto validation = validateParameters(parameters, layout);
    if (!validation.hasValue()) {
        return core::Result<std::size_t>::failure(validation.error());
    }
    if (parameters.mode == DenoiseMode::Median) {
        return core::Result<std::size_t>::success(0U);
    }
    return detail::gaussianScratchBytes("denoise", layout, parameters.kernelSize);
}

core::Result<std::unique_ptr<DenoiseStage>> DenoiseStage::create(
    DenoiseParameters parameters,
    const core::ImageLayout& layout,
    std::size_t scratchBudgetBytes) {
    return detail::StageStorage::createDenoise(parameters, layout, scratchBudgetBytes, nullptr);
}

core::Result<std::unique_ptr<DenoiseStage>> detail::StageStorage::createDenoise(
    DenoiseParameters parameters,
    const core::ImageLayout& layout,
    std::size_t scratchBudgetBytes, detail::PreparedCpuExecutor* executor) {
    const auto required = DenoiseStage::requiredScratchBytes(parameters, layout);
    if (!required.hasValue()) return FactoryResult::failure(required.error());
    if (required.value() > scratchBudgetBytes) {
        return factoryFailure("denoise_scratch_budget_exceeded",
            "Denoise fixed scratch exceeds the supplied preparation budget.",
            core::ErrorCategory::ResourceExhaustion, true);
    }

    try {
        std::unique_ptr<double[]> intermediate;
        std::unique_ptr<double[]> coefficients;
        if (parameters.mode == DenoiseMode::Gaussian) {
            const auto sampleCount = static_cast<std::size_t>(layout.width())
                * static_cast<std::size_t>(layout.height());
            intermediate = std::make_unique<double[]>(sampleCount);
            std::fill_n(intermediate.get(), sampleCount, 0.0);
            coefficients = std::make_unique<double[]>(parameters.kernelSize);
            detail::prepareGaussianKernel(
                parameters.kernelSize, parameters.sigma, coefficients.get());
        }
        auto impl = std::make_unique<DenoiseStage::Impl>(parameters, layout.width(), layout.height(),
            required.value(), std::move(intermediate), std::move(coefficients));
        impl->executor = executor;
        return FactoryResult::success(
            std::unique_ptr<DenoiseStage>(new DenoiseStage(std::move(impl))));
    } catch (const std::bad_alloc&) {
        return factoryFailure("denoise_allocation_failed",
            "Denoise preparation could not allocate its fixed storage.",
            core::ErrorCategory::ResourceExhaustion, true);
    } catch (const cv::Exception& exception) {
        return factoryFailure("denoise_backend_error",
            std::string{"OpenCV rejected Gaussian kernel preparation: "}
                + exception.what());
    } catch (const std::exception& exception) {
        return factoryFailure("denoise_backend_error",
            std::string{"Denoise preparation failed: "} + exception.what());
    } catch (...) {
        return factoryFailure("denoise_backend_error",
            "Denoise preparation failed with an unknown backend exception.");
    }
}

std::size_t DenoiseStage::scratchBytes() const noexcept {
    return impl_->scratchBytes;
}

StageId DenoiseStage::id() const noexcept {
    return StageId::Denoise;
}

const StageTraits& DenoiseStage::traits() const noexcept {
    static constexpr StageTraits traits{StageId::Denoise,
        ImageDomain::CanonicalU16, ImageDomain::CanonicalU16, false, 4U, 0U, false,
        ExecutionBackend::Cpu};
    return traits;
}

core::Result<void> DenoiseStage::process(
    const ImageView& source,
    MutableImageView destination,
    const core::SourcePixelFormat&) const {
    const auto validation = detail::validateProcessViews(
        "denoise", source, destination, impl_->width, impl_->height);
    if (!validation.hasValue()) return validation;

    const auto width = static_cast<std::size_t>(impl_->width);
    const auto height = static_cast<std::size_t>(impl_->height);
    if (impl_->parameters.mode == DenoiseMode::Gaussian) {
        struct Context { Impl* impl; const ImageView& source; MutableImageView destination; } context{impl_.get(),source,destination};
        const auto horizontal = [](void* opaque, std::size_t, std::size_t begin, std::size_t end) noexcept {
            auto& job = *static_cast<Context*>(opaque);
            const auto& impl = *job.impl;
            for (auto y = begin; y < end; ++y)
                detail::horizontalGaussianRow(job.source.row(static_cast<std::uint32_t>(y)),
                    impl.intermediate.get() + y * impl.width, impl.width,
                    impl.coefficients.get(), impl.parameters.kernelSize);
        };
        const auto vertical = [](void* opaque, std::size_t, std::size_t begin, std::size_t end) noexcept {
            auto& job = *static_cast<Context*>(opaque);
            const auto& impl = *job.impl;
            detail::ReflectedGaussianRows rows{};
            for (auto y = begin; y < end; ++y) {
                detail::prepareVerticalGaussianRows(impl.intermediate.get(), impl.width,
                    impl.height, y, impl.parameters.kernelSize, rows);
                detail::writeGaussianRow(rows, impl.coefficients.get(), impl.parameters.kernelSize,
                    impl.width, job.destination.row(static_cast<std::uint32_t>(y)));
            }
        };
        if (impl_->executor && width * height >= 65536U) {
            impl_->executor->run(height, &context, horizontal, detail::CpuJobKind::GaussianHorizontal);
            impl_->executor->run(height, &context, vertical, detail::CpuJobKind::GaussianVertical);
        } else {
            horizontal(&context, 0U, 0U, height);
            vertical(&context, 0U, 0U, height);
        }
        return core::Result<void>::success();
    }

    const auto kernelSize = static_cast<std::size_t>(impl_->parameters.kernelSize);
    const auto radius = static_cast<std::int64_t>(kernelSize / 2U);
    const auto sampleCount = kernelSize * kernelSize;
    for (std::size_t y = 0U; y < height; ++y) {
        const auto destinationRow = destination.row(static_cast<std::uint32_t>(y));
        for (std::size_t x = 0U; x < width; ++x) {
            std::array<std::uint16_t, 25U> neighborhood{};
            std::size_t sample = 0U;
            for (std::size_t kernelY = 0U; kernelY < kernelSize; ++kernelY) {
                const auto sourceY = detail::reflect101(
                    static_cast<std::int64_t>(y)
                        + static_cast<std::int64_t>(kernelY) - radius,
                    height);
                for (std::size_t kernelX = 0U; kernelX < kernelSize; ++kernelX) {
                    const auto sourceX = detail::reflect101(
                        static_cast<std::int64_t>(x)
                            + static_cast<std::int64_t>(kernelX) - radius,
                        width);
                    neighborhood[sample++] = detail::loadU16(source, sourceX, sourceY);
                }
            }
            std::sort(neighborhood.begin(), neighborhood.begin()
                    + static_cast<std::ptrdiff_t>(sampleCount));
            detail::storeU16(
                destinationRow, x, neighborhood[sampleCount / 2U]);
        }
    }
    return core::Result<void>::success();
}

}  // namespace lumora::processing
