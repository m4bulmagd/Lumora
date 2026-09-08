#pragma once
#include "EvidenceJson.hpp"
#include <lumora/processing/IProcessingStage.hpp>
#include <lumora/core/LatestValueSlot.hpp>
#include <lumora/processing/FrameProcessingEngine.hpp>
#include <array>
namespace lumora::evidence {
core::ImageLayout layoutFor(std::uint32_t w,std::uint32_t h,bool gray8=false);
core::SourcePixelFormat monoFormat(bool mono12=false);
struct Case { std::string id; Pattern pattern; std::string operation,variant; bool mono12=false,gray8=false,exact=true;core::Orientation orientation{false,false,core::Rotation::Degrees0}; };
std::vector<Case> referenceCases(bool smoke);
Image executeCase(const Case&,const Image&);
QJsonObject caseMetadata(const Case&);
class Standalone {
    std::unique_ptr<processing::IProcessingStage> stage_;
    core::ImageLayout layout_;
    core::SourcePixelFormat format_;
    std::vector<std::byte> source_,destination_;
    processing::ImageView input_;
    processing::MutableImageView output_;
    std::size_t scratch_=0,fixed_=0;
public:
    Standalone(const std::string& row,const Image&,bool mono12=false);
    bool cycle();
    Image output() const;
    QJsonObject resources() const;
    processing::PipelineDefinition definition;
};
class Session {
    std::shared_ptr<core::BufferPool> rawPool_,processingPool_,displayPool_;
    std::shared_ptr<const core::RawFrame> raw_;
    std::unique_ptr<processing::FrameProcessingEngine> engine_;
    std::shared_ptr<const core::FrameBundle> sentinel_;
    core::LatestValueSlot<core::FrameBundle> latest_;
    std::array<std::shared_ptr<const core::FrameBundle>,5> retained_{};
    std::uint64_t revision_=0;
public:
    Session(const Image&,core::Orientation);
    static processing::ProcessingPreparationAssessment assess(std::uint32_t,std::uint32_t,core::Orientation);
    bool cycle(std::size_t,std::uint64_t& fingerprint);
    void releaseMeasured();
    std::shared_ptr<const core::FrameBundle> verificationOutput();
    QJsonObject resources() const;
    bool healthy() const;
};
Image displayImage(const core::DisplayFrame&);
std::string fullChecksum(const core::FrameBundle&);
}
