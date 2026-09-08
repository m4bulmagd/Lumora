#pragma once
#include <cstddef>
#include <lumora/processing/ClaheStage.hpp>
#include <lumora/processing/DenoiseStage.hpp>
#include <lumora/processing/SharpenStage.hpp>
namespace lumora::processing::detail {
class PreparedCpuExecutor;
// Engine sizing accepts its already validated frozen slot count. Construction
// borrows the stopped-prepared executor, which must outlive the returned stage.
// A null executor is the public standalone serial path.
struct StageStorage final {
    static core::Result<std::size_t> claheScratchBytes(ClaheParameters, const core::ImageLayout&, std::size_t executionSlots);
    static core::Result<std::unique_ptr<ClaheStage>> createClahe(ClaheParameters, const core::ImageLayout&, std::size_t budget, PreparedCpuExecutor*);
    static core::Result<std::size_t> denoiseScratchBytes(DenoiseParameters, const core::ImageLayout&, std::size_t executionSlots);
    static core::Result<std::unique_ptr<DenoiseStage>> createDenoise(DenoiseParameters, const core::ImageLayout&, std::size_t budget, PreparedCpuExecutor*);
    static core::Result<std::size_t> sharpenScratchBytes(SharpenParameters, const core::ImageLayout&, std::size_t executionSlots);
    static core::Result<std::unique_ptr<SharpenStage>> createSharpen(SharpenParameters, const core::ImageLayout&, std::size_t budget, PreparedCpuExecutor*);
    static std::size_t denoiseOwnerBytes() noexcept;
    static std::size_t sharpenOwnerBytes() noexcept;
    static std::size_t claheOwnerBytes() noexcept;
};
}
