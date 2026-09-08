#include <lumora/processing/SharpenStage.hpp>
#include <lumora/core/CheckedMath.hpp>
#include "StageStorage.hpp"
#include "PreparedCpuExecutor.hpp"

#include "DetailStageSupport.hpp"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace lumora::processing {
namespace {

using FactoryResult = core::Result<std::unique_ptr<SharpenStage>>;

[[nodiscard]] core::Result<void> validateParameters(
    SharpenParameters parameters,
    const core::ImageLayout& layout) {
    const auto fail = [](std::string code, std::string message) {
        return core::Result<void>::failure(detail::stageError("sharpen",
            core::ErrorCategory::Processing, std::move(code), std::move(message)));
    };
    if (!std::isfinite(parameters.amount)
        || parameters.amount < 0.0
        || parameters.amount > 5.0) {
        return fail("sharpen_invalid_amount",
            "Sharpen amount must be finite and within [0, 5].");
    }
    if (!std::isfinite(parameters.radius)
        || parameters.radius < 0.5
        || parameters.radius > 5.0) {
        return fail("sharpen_invalid_radius",
            "Sharpen radius must be finite and within [0.5, 5].");
    }
    if (!std::isfinite(parameters.threshold)
        || parameters.threshold < 0.0
        || parameters.threshold > 65535.0) {
        return fail("sharpen_invalid_threshold",
            "Sharpen threshold must be finite and within [0, 65535].");
    }
    if (layout.storage() != core::StorageType::UInt16) {
        return fail("sharpen_layout_storage_mismatch",
            "Sharpen preparation requires unsigned 16-bit storage.");
    }
    return core::Result<void>::success();
}

[[nodiscard]] FactoryResult factoryFailure(
    std::string code,
    std::string message,
    core::ErrorCategory category = core::ErrorCategory::Processing,
    bool recoverable = false) {
    return FactoryResult::failure(detail::stageError(
        "sharpen", category, std::move(code), std::move(message), recoverable));
}

[[nodiscard]] core::Result<std::size_t> ringScratchBytes(
    const core::ImageLayout& layout,
    std::size_t kernelSize, std::size_t executionSlots) {
    const auto fail = [] {
        return core::Result<std::size_t>::failure(detail::stageError("sharpen",
            core::ErrorCategory::ResourceExhaustion,
            "sharpen_scratch_size_overflow",
            "Computing the fixed Gaussian scratch storage overflowed size_t.",
            true));
    };
    const auto ringRows = std::min(
        static_cast<std::size_t>(layout.height()), kernelSize);
    const auto sampleCount = core::checkedMultiply(
        static_cast<std::size_t>(layout.width()), ringRows);
    if (!sampleCount.hasValue()) return fail();
    const auto intermediateBytes = core::checkedMultiply(
        sampleCount.value(), sizeof(double));
    if (!intermediateBytes.hasValue()) return fail();
    const auto allRingBytes = core::checkedMultiply(intermediateBytes.value(), executionSlots);
    if (!allRingBytes.hasValue()) return fail();
    const auto coefficientBytes = core::checkedMultiply(kernelSize, sizeof(double));
    if (!coefficientBytes.hasValue()) return fail();
    const auto total = core::checkedAdd(
        allRingBytes.value(), coefficientBytes.value());
    if (!total.hasValue()) return fail();
    return core::Result<std::size_t>::success(total.value());
}

}  // namespace

struct SharpenStage::Impl final {
    Impl(
        SharpenParameters parametersValue,
        std::uint32_t widthValue,
        std::uint32_t heightValue,
        std::size_t kernelSizeValue,
        std::size_t scratchBytesValue,
        std::unique_ptr<double[]> intermediateValue,
        std::unique_ptr<double[]> coefficientsValue)
        : parameters(parametersValue),
          width(widthValue),
          height(heightValue),
          kernelSize(kernelSizeValue),
          scratchBytes(scratchBytesValue),
          intermediate(std::move(intermediateValue)),
          coefficients(std::move(coefficientsValue)) {}

    detail::PreparedCpuExecutor* executor{};
    const SharpenParameters parameters;
    const std::uint32_t width;
    const std::uint32_t height;
    const std::size_t kernelSize;
    const std::size_t scratchBytes;
    std::unique_ptr<double[]> intermediate;
    std::unique_ptr<double[]> coefficients;
};

std::size_t detail::StageStorage::sharpenOwnerBytes() noexcept {
    return sizeof(SharpenStage) + sizeof(SharpenStage::Impl);
}

SharpenStage::SharpenStage(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SharpenStage::~SharpenStage() = default;

core::Result<std::size_t> SharpenStage::requiredScratchBytes(
    SharpenParameters parameters,
    const core::ImageLayout& layout) {
    return detail::StageStorage::sharpenScratchBytes(parameters, layout, 1U);
}

core::Result<std::size_t> detail::StageStorage::sharpenScratchBytes(
    SharpenParameters parameters, const core::ImageLayout& layout, std::size_t executionSlots) {
    const auto validation = validateParameters(parameters, layout);
    if (!validation.hasValue()) {
        return core::Result<std::size_t>::failure(validation.error());
    }
    return ringScratchBytes(
        layout, detail::gaussianKernelSize(parameters.radius), executionSlots);
}

core::Result<std::unique_ptr<SharpenStage>> SharpenStage::create(
    SharpenParameters parameters,
    const core::ImageLayout& layout,
    std::size_t scratchBudgetBytes) {
    return detail::StageStorage::createSharpen(parameters, layout, scratchBudgetBytes, nullptr);
}

core::Result<std::unique_ptr<SharpenStage>> detail::StageStorage::createSharpen(
    SharpenParameters parameters,
    const core::ImageLayout& layout,
    std::size_t scratchBudgetBytes, detail::PreparedCpuExecutor* executor) {
    const auto executionSlots = executor ? executor->slots() : 1U;
    const auto required = sharpenScratchBytes(parameters, layout, executionSlots);
    if (!required.hasValue()) return FactoryResult::failure(required.error());
    if (required.value() > scratchBudgetBytes) {
        return factoryFailure("sharpen_scratch_budget_exceeded",
            "Sharpen fixed scratch exceeds the supplied preparation budget.",
            core::ErrorCategory::ResourceExhaustion, true);
    }

    try {
        const auto kernelSize = detail::gaussianKernelSize(parameters.radius);
        const auto ringRows = std::min(
            static_cast<std::size_t>(layout.height()), kernelSize);
        const auto sampleCount = static_cast<std::size_t>(layout.width()) * ringRows * executionSlots;
        auto intermediate = std::make_unique<double[]>(sampleCount);
        std::fill_n(intermediate.get(), sampleCount, 0.0);
        auto coefficients = std::make_unique<double[]>(kernelSize);
        detail::prepareGaussianKernel(
            kernelSize, parameters.radius, coefficients.get());
        auto impl = std::make_unique<SharpenStage::Impl>(parameters, layout.width(), layout.height(),
            kernelSize, required.value(), std::move(intermediate),
            std::move(coefficients));
        impl->executor = executor;
        return FactoryResult::success(
            std::unique_ptr<SharpenStage>(new SharpenStage(std::move(impl))));
    } catch (const std::bad_alloc&) {
        return factoryFailure("sharpen_allocation_failed",
            "Sharpen preparation could not allocate its fixed storage.",
            core::ErrorCategory::ResourceExhaustion, true);
    } catch (const cv::Exception& exception) {
        return factoryFailure("sharpen_backend_error",
            std::string{"OpenCV rejected Gaussian kernel preparation: "}
                + exception.what());
    } catch (const std::exception& exception) {
        return factoryFailure("sharpen_backend_error",
            std::string{"Sharpen preparation failed: "} + exception.what());
    } catch (...) {
        return factoryFailure("sharpen_backend_error",
            "Sharpen preparation failed with an unknown backend exception.");
    }
}

std::size_t SharpenStage::scratchBytes() const noexcept {
    return impl_->scratchBytes;
}

StageId SharpenStage::id() const noexcept {
    return StageId::Sharpen;
}

const StageTraits& SharpenStage::traits() const noexcept {
    static constexpr StageTraits traits{StageId::Sharpen,
        ImageDomain::CanonicalU16, ImageDomain::CanonicalU16, false, 4U, 0U, false,
        ExecutionBackend::Cpu};
    return traits;
}

core::Result<void> SharpenStage::process(
    const ImageView& source,
    MutableImageView destination,
    const core::SourcePixelFormat&) const {
    const auto validation = detail::validateProcessViews(
        "sharpen", source, destination, impl_->width, impl_->height);
    if (!validation.hasValue()) return validation;
    if (impl_->parameters.amount == 0.0
        || impl_->parameters.threshold == 65535.0) {
        detail::copyActivePixels(source, destination);
        return core::Result<void>::success();
    }

    struct Context { Impl* impl; const ImageView& source; MutableImageView destination; } context{impl_.get(),source,destination};
    const auto stripe = [](void* opaque, std::size_t slot, std::size_t begin, std::size_t end) noexcept {
        auto& job = *static_cast<Context*>(opaque);
        auto* impl_ = job.impl;
        const auto& source = job.source;
        const auto destination = job.destination;
        const auto width = static_cast<std::size_t>(impl_->width);
        const auto height = static_cast<std::size_t>(impl_->height);
        const auto radius = impl_->kernelSize / 2U;
        const auto ringRows = std::min(height, impl_->kernelSize);
        auto* ring = impl_->intermediate.get() + slot * width * ringRows;
        // Each stripe primes only its own reflected halo, using global row modulo.
        std::size_t nextSourceRow = begin > radius ? begin - radius : 0U;
        detail::ReflectedGaussianRows rows{};
        for (auto y = begin; y < end; ++y) {
            const auto finalSourceRow = std::min(height - 1U, y + radius);
            while (nextSourceRow <= finalSourceRow) {
                detail::horizontalGaussianRow(
                    source.row(static_cast<std::uint32_t>(nextSourceRow)),
                    ring + (nextSourceRow % ringRows) * width,
                    width, impl_->coefficients.get(), impl_->kernelSize);
                ++nextSourceRow;
            }
            const auto signedRadius = static_cast<std::int64_t>(radius);
            for (std::size_t tap = 0U; tap < impl_->kernelSize; ++tap) {
                const auto offset = static_cast<std::int64_t>(tap) - signedRadius;
                const auto reflectedY = detail::reflect101(
                    static_cast<std::int64_t>(y) + offset, height);
                rows[tap] = ring
                    + (reflectedY % ringRows) * width;
            }
            const auto sourceRow = source.row(static_cast<std::uint32_t>(y));
            const auto destinationRow = destination.row(static_cast<std::uint32_t>(y));
            detail::writeSharpenRow(rows, impl_->coefficients.get(), impl_->kernelSize,
                width, sourceRow, destinationRow, impl_->parameters.amount,
                impl_->parameters.threshold);
        }
    };
    const auto height = static_cast<std::size_t>(impl_->height);
    if (impl_->executor && static_cast<std::size_t>(impl_->width) * height >= 65536U)
        impl_->executor->run(height, &context, stripe, detail::CpuJobKind::SharpenRows);
    else
        stripe(&context, 0U, 0U, height);
    return core::Result<void>::success();
}

}  // namespace lumora::processing
