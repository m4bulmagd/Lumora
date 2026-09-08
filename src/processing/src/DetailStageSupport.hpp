#pragma once

#include <lumora/core/Error.hpp>
#include <lumora/core/ImageLayout.hpp>
#include <lumora/core/Result.hpp>
#include <lumora/processing/ImageView.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace lumora::processing::detail {

[[nodiscard]] core::Error stageError(
    std::string_view stage,
    core::ErrorCategory category,
    std::string code,
    std::string detail,
    bool recoverable = false);

[[nodiscard]] core::Result<void> validateProcessViews(
    std::string_view stage,
    const ImageView& source,
    const MutableImageView& destination,
    std::uint32_t preparedWidth,
    std::uint32_t preparedHeight);

[[nodiscard]] core::Result<std::size_t> gaussianScratchBytes(
    std::string_view stage,
    const core::ImageLayout& layout,
    std::size_t kernelSize);

[[nodiscard]] std::size_t gaussianKernelSize(double sigma) noexcept;

void prepareGaussianKernel(
    std::size_t kernelSize,
    double sigma,
    double* coefficients);

[[nodiscard]] std::size_t reflect101(
    std::int64_t coordinate,
    std::size_t extent) noexcept;

[[nodiscard]] std::uint16_t loadU16(
    const ImageView& image,
    std::size_t x,
    std::size_t y) noexcept;

[[nodiscard]] std::uint16_t loadU16(
    std::span<const std::byte> row,
    std::size_t x) noexcept;

void storeU16(
    MutableImageView image,
    std::size_t x,
    std::size_t y,
    std::uint16_t value) noexcept;

void storeU16(
    std::span<std::byte> row,
    std::size_t x,
    std::uint16_t value) noexcept;

void copyActivePixels(
    const ImageView& source,
    MutableImageView destination) noexcept;

void horizontalGaussian(
    const ImageView& source,
    double* intermediate,
    std::size_t width,
    std::size_t height,
    const double* coefficients,
    std::size_t kernelSize) noexcept;

void horizontalGaussianRow(
    std::span<const std::byte> sourceRow,
    double* destinationRow,
    std::size_t width,
    const double* coefficients,
    std::size_t kernelSize) noexcept;

using ReflectedGaussianRows = std::array<const double*, 31U>;

void prepareVerticalGaussianRows(
    const double* intermediate,
    std::size_t width,
    std::size_t height,
    std::size_t y,
    std::size_t kernelSize,
    ReflectedGaussianRows& rows) noexcept;

[[nodiscard]] double verticalGaussianAt(
    const ReflectedGaussianRows& rows,
    std::size_t x,
    const double* coefficients,
    std::size_t kernelSize) noexcept;

void writeGaussianRow(
    const ReflectedGaussianRows& rows,
    const double* coefficients,
    std::size_t kernelSize,
    std::size_t width,
    std::span<std::byte> destinationRow) noexcept;

void writeSharpenRow(
    const ReflectedGaussianRows& rows,
    const double* coefficients,
    std::size_t kernelSize,
    std::size_t width,
    std::span<const std::byte> sourceRow,
    std::span<std::byte> destinationRow,
    double amount,
    double threshold) noexcept;

// Private numerical/dispatch seam; unsupported platforms use the scalar path.
enum class DetailRowBackend : std::uint8_t { Scalar, BaselineSse2 };
using GaussianBlock8 = std::array<double, 8U>;

void accumulateVerticalBlock8(
    const ReflectedGaussianRows& rows,
    const double* coefficients,
    std::size_t kernelSize,
    std::size_t firstX,
    GaussianBlock8& output,
    DetailRowBackend backend) noexcept;

// Returns the number of blocks actually processed by SSE2 (zero for scalar).
std::size_t writeSharpenRow(
    const ReflectedGaussianRows& rows,
    const double* coefficients,
    std::size_t kernelSize,
    std::size_t width,
    std::span<const std::byte> sourceRow,
    std::span<std::byte> destinationRow,
    double amount,
    double threshold,
    DetailRowBackend backend) noexcept;

[[nodiscard]] std::uint16_t roundU16(double value) noexcept;

}  // namespace lumora::processing::detail
