#pragma once
#include <cstddef>
namespace lumora::processing::detail {
// Private evidence tags identify real image jobs separately from synthetic controls.
enum class CpuJobKind { Unspecified, ClaheTiles, ClaheRows, GaussianHorizontal, GaussianVertical, SharpenRows };
using CpuWorkObserver = void (*)(void*,CpuJobKind,std::size_t slot,std::size_t begin,std::size_t end) noexcept;
}
