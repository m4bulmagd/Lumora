#include "EvidenceWorkload.hpp"
#include "StageStorage.hpp"
#include <lumora/processing/NormalizeStage.hpp>
#include <lumora/processing/WindowLevelStage.hpp>
#include <lumora/processing/ToneStages.hpp>
#include <lumora/processing/ClaheStage.hpp>
#include <lumora/processing/DenoiseStage.hpp>
#include <lumora/processing/SharpenStage.hpp>
#include <lumora/processing/DisplayMapper.hpp>
#include <lumora/processing/OrientationTransform.hpp>
#include <cstring>
namespace lumora::evidence {
namespace {
template<class T> T require(core::Result<T> result) {if(!result.hasValue()) throw Error(4,result.error().code+": "+result.error().diagnosticDetail);return std::move(result).value();}
std::vector<std::byte> imageBytes(const Image& im) {std::vector<std::byte> b(im.pixels.size()*2);std::memcpy(b.data(),im.pixels.data(),b.size());return b;}
void fingerprintBytes(std::uint64_t& hash,std::span<const std::byte> b) noexcept {
    // Fixed16 samples per payload; no storage allocation. Included in per-cycle
    // duration, unlike the extra full SHA256 verification cycle.
    for(std::size_t i=0;i<16 && !b.empty();++i) {hash^=std::to_integer<unsigned char>(b[(b.size()-1)*i/15]);hash*=1099511628211ULL;}
}
}
core::ImageLayout layoutFor(std::uint32_t w,std::uint32_t h,bool gray8) {const auto stride=static_cast<std::uint64_t>(w)*(gray8?1U:2U);if(stride>SIZE_MAX || h>SIZE_MAX/stride) throw Error(2,"Image extent overflow");return require(core::ImageLayout::create(w,h,static_cast<std::size_t>(stride),gray8?core::StorageType::UInt8:core::StorageType::UInt16,static_cast<std::size_t>(stride)*h));}
core::SourcePixelFormat monoFormat(bool mono12) {return {mono12?"Mono12":"Mono16",mono12?0x01100005U:0x01100007U,static_cast<std::uint8_t>(mono12?12:16),static_cast<std::uint16_t>(mono12?4095:65535),core::SourcePacking::Unpacked,core::BitAlignment::LeastSignificant,core::StorageType::UInt16};}
std::vector<Case> referenceCases(bool smoke) {
    const auto rw=smoke?17U:257U,gw=smoke?16U:67U,gh=smoke?12U:53U,nw=smoke?15U:61U,nh=smoke?11U:47U;
    const Pattern gradient{"gradient_xy_u16_v1",gw,gh},noise{"xorshift32_u16_v1",nw,nh},asymmetric{"orientation_asymmetric_u16_v1",7,5};
    const core::Orientation nonidentity{true,false,core::Rotation::Degrees90};
    return {{"normalize_mono12_ramp",{"ramp_u16_v1",rw,1,4095},"normalize","standard",true},
        {"window_level_gradient",gradient,"window_level","standard"},
        {"brightness_contrast_gradient",gradient,"brightness_contrast","standard"},
        {"gamma_gradient",gradient,"gamma","standard"},
        {"invert_ramp",{"ramp_u16_v1",rw,1},"invert","standard"},
        {"orientation_asymmetric_gray8",asymmetric,"orientation","gray8",false,true,true,nonidentity},
        {"orientation_asymmetric_gray16",asymmetric,"orientation","gray16",false,false,true,{false,true,core::Rotation::Degrees270}},
        {"clahe_gradient",gradient,"clahe","standard",false,false,false},
        {"denoise_gaussian_noise",noise,"denoise_gaussian","gaussian",false,false,false},
        {"denoise_median_noise",noise,"denoise_median","median",false,false,false},
        {"sharpen_step_edges",{"step_edges_u16_v1",smoke?16U:64U,smoke?12U:48U},"sharpen","standard",false,false,false},
        {"full_standard_noise_identity",noise,"full_standard_identity","identity",false,true,false},
        {"full_standard_noise_nonidentity",noise,"full_standard_nonidentity","nonidentity",false,true,false,nonidentity}};
}
Standalone::Standalone(const std::string& row,const Image& im,bool mono12):layout_(layoutFor(im.width,im.height)),format_(monoFormat(mono12)),source_(imageBytes(im)),destination_(source_.size()),input_(require(processing::ImageView::create(layout_,source_,row=="normalize"?processing::ImageDomain::SensorNative:processing::ImageDomain::CanonicalU16))),output_(require(processing::MutableImageView::create(layout_,destination_,processing::ImageDomain::CanonicalU16))),definition(processing::standardPipeline()) {
    using namespace processing;StageId id=StageId::Normalize;
    for(auto& stage:definition.stages) {stage.enabled=stage.id==StageId::Normalize;if(stageId(stage.id)==qs(row) || (stage.id==StageId::Denoise && row.starts_with("denoise_"))) {id=stage.id;stage.enabled=true;}}
    auto& d=std::get<DenoiseParameters>(definition.stages[5].parameters);if(row=="denoise_median") d.mode=DenoiseMode::Median;
    switch(id) {
    case StageId::Normalize:stage_=std::make_unique<NormalizeStage>();fixed_=sizeof(NormalizeStage);break;
    case StageId::WindowLevel:stage_=std::make_unique<WindowLevelStage>(std::get<WindowLevelParameters>(definition.stages[1].parameters));fixed_=sizeof(WindowLevelStage);break;
    case StageId::BrightnessContrast:stage_=std::make_unique<BrightnessContrastStage>(std::get<BrightnessContrastParameters>(definition.stages[2].parameters));fixed_=sizeof(BrightnessContrastStage);break;
    case StageId::Gamma:stage_=std::make_unique<GammaStage>(std::get<GammaParameters>(definition.stages[3].parameters));fixed_=sizeof(GammaStage);break;
    case StageId::Clahe:{auto s=require(ClaheStage::create(std::get<ClaheParameters>(definition.stages[4].parameters),layout_));scratch_=s->scratchBytes();fixed_=detail::StageStorage::claheOwnerBytes();stage_=std::move(s);break;}
    case StageId::Denoise:{auto s=require(DenoiseStage::create(d,layout_));scratch_=s->scratchBytes();fixed_=detail::StageStorage::denoiseOwnerBytes();stage_=std::move(s);break;}
    case StageId::Sharpen:{auto s=require(SharpenStage::create(std::get<SharpenParameters>(definition.stages[6].parameters),layout_));scratch_=s->scratchBytes();fixed_=detail::StageStorage::sharpenOwnerBytes();stage_=std::move(s);break;}
    case StageId::Invert:stage_=std::make_unique<InvertStage>(std::get<InvertParameters>(definition.stages[7].parameters));fixed_=sizeof(InvertStage);break;
    }
}
bool Standalone::cycle() {return stage_->process(input_,output_,format_).hasValue();}
Image Standalone::output() const {Image im{layout_.width(),layout_.height(),std::vector<std::uint16_t>(destination_.size()/2)};std::memcpy(im.pixels.data(),destination_.data(),destination_.size());return im;}
QJsonObject Standalone::resources() const {return standaloneResources(scratch_,fixed_,source_.size()+destination_.size());}
processing::ProcessingPreparationAssessment Session::assess(std::uint32_t w,std::uint32_t h,core::Orientation orientation) {
    const auto layout=layoutFor(w,h);auto raw=core::BufferPool::plan(1,layout.payloadBytes());if(!raw.hasValue()) throw Error(4,"Raw pool plan rejected");
    processing::ProcessingPreparationOptions options;options.orientation=orientation;options.externalSessionStorageBytes=raw.value().requiredStorageBytes;
    return processing::FrameProcessingEngine::plan({9,layout.payloadBytes()},{16,static_cast<std::size_t>(w)*h},layout,processing::standardPipeline(),options);
}
Session::Session(const Image& im,core::Orientation orientation) {
    const auto layout=layoutFor(im.width,im.height);auto plan=assess(im.width,im.height,orientation);if(!plan.plan) throw Error(4,"Standard session resource admission rejected");
    rawPool_=require(core::BufferPool::create(1,layout.payloadBytes()));processingPool_=require(core::BufferPool::create(9,layout.payloadBytes()));displayPool_=require(core::BufferPool::create(16,im.pixels.size()));
    auto lease=rawPool_->tryAcquire();if(!lease) throw Error(4,"Raw lease unavailable");std::memcpy(lease->bytes().data(),im.pixels.data(),layout.payloadBytes());
    auto settings=require(core::AcquisitionSettingsSnapshot::create({"Lumora","Evidence","1","synthetic",{}},monoFormat(),{0,0,im.width,im.height},30,30,{},{}));
    raw_=require(core::RawFrame::create(1,layout,std::move(*lease).seal(),{{},{},{},{},std::move(settings)}));
    engine_=require(processing::FrameProcessingEngine::create(*processingPool_,*displayPool_,*plan.plan));sentinel_=verificationOutput();
}
bool Session::healthy() const {const auto s=engine_->status();return s.mode==processing::ProcessorMode::Enhanced && s.enhancementFailures==0 && !s.error && !s.failingOperation;}
bool Session::cycle(std::size_t index,std::uint64_t& hash) {
    retained_[index%retained_.size()].reset();auto output=engine_->process(raw_);
    if(!output.hasValue() || !output.value()->enhanced || !output.value()->originalDisplay || !output.value()->enhancedDisplay || !healthy()) return false;
    fingerprintBytes(hash,output.value()->enhanced->pixels.bytes());fingerprintBytes(hash,output.value()->originalDisplay->pixels.bytes());fingerprintBytes(hash,output.value()->enhancedDisplay->pixels.bytes());
    if(latest_.publish(std::move(output).value()).revision==0) return false;
    if(index%3!=0) {auto consumed=latest_.consumeAfter(revision_);if(!consumed) return false;revision_=consumed->revision;retained_[index%retained_.size()]=std::move(consumed->value);}return true;
}
void Session::releaseMeasured() {for(auto& frame:retained_) frame.reset();(void)latest_.publish(sentinel_);}
std::shared_ptr<const core::FrameBundle> Session::verificationOutput() {auto output=require(engine_->process(raw_));if(!output->enhanced || !output->originalDisplay || !output->enhancedDisplay || !healthy()) throw Error(4,"Standard output is not healthy paired Enhanced/Original");return output;}
QJsonObject Session::resources() const {return sessionResources(engine_->resources());}
Image displayImage(const core::DisplayFrame& frame) {
    Image im{frame.layout.width(),frame.layout.height(),std::vector<std::uint16_t>(static_cast<std::size_t>(frame.layout.width())*frame.layout.height())};
    for(std::uint32_t y=0;y<im.height;++y) for(std::uint32_t x=0;x<im.width;++x) {const auto i=static_cast<std::size_t>(y)*im.width+x;const auto offset=static_cast<std::size_t>(y)*frame.layout.strideBytes()+x*(frame.storage==core::DisplayStorage::Gray8?1U:2U);if(frame.storage==core::DisplayStorage::Gray8) im.pixels[i]=std::to_integer<std::uint8_t>(frame.pixels.bytes()[offset]);else std::memcpy(&im.pixels[i],frame.pixels.bytes().data()+offset,2);}
    return im;
}
std::string fullChecksum(const core::FrameBundle& frame) {
    Image enhanced{frame.enhanced->layout.width(),frame.enhanced->layout.height(),std::vector<std::uint16_t>(frame.enhanced->layout.payloadBytes()/2)};std::memcpy(enhanced.pixels.data(),frame.enhanced->pixels.bytes().data(),enhanced.pixels.size()*2);
    return sha256(encodePgm(enhanced)+encodePgm(displayImage(*frame.originalDisplay))+encodePgm(displayImage(*frame.enhancedDisplay)));
}
Image executeCase(const Case& c,const Image& im) {
    if(c.operation.starts_with("full_standard_")) {Session session(im,c.orientation);return displayImage(*session.verificationOutput()->enhancedDisplay);}
    if(c.operation!="orientation") {Standalone stage(c.operation,im,c.mono12);if(!stage.cycle()) throw Error(4,"Reference processing failed");return stage.output();}
    const auto sourceLayout=layoutFor(im.width,im.height),mappedLayout=layoutFor(im.width,im.height,c.gray8);const auto storage=c.gray8?core::DisplayStorage::Gray8:core::DisplayStorage::Gray16;
    auto source=imageBytes(im);std::vector<std::byte> mapped(mappedLayout.payloadBytes());auto input=require(processing::ImageView::create(sourceLayout,source,processing::ImageDomain::CanonicalU16));
    if(c.gray8) {
        if(!processing::DisplayMapper{}.map(input,mappedLayout,storage,mapped,0).hasValue()) throw Error(4,"Display mapping failed");
    } else mapped=source;
    const auto target=require(processing::OrientationTransform::outputLayout(mappedLayout,storage,c.orientation));std::vector<std::byte> result(target.payloadBytes());
    if(!processing::OrientationTransform{}.apply(mappedLayout,storage,mapped,target,result,c.orientation).hasValue()) throw Error(4,"Orientation failed");
    Image output{target.width(),target.height(),std::vector<std::uint16_t>(im.pixels.size())};for(std::size_t i=0;i<output.pixels.size();++i) {if(c.gray8) output.pixels[i]=std::to_integer<std::uint8_t>(result[i]);else std::memcpy(&output.pixels[i],result.data()+2*i,2);}return output;
}
QJsonObject caseMetadata(const Case& c) {
    QJsonObject params;QString id=qs(c.operation);bool full=c.operation.starts_with("full_standard_");
    if(!full && c.operation!="orientation") {auto p=processing::standardPipeline();for(auto& s:p.stages) if(stageId(s.id)==id || (s.id==processing::StageId::Denoise && c.operation.starts_with("denoise_"))) {if(c.variant=="median") std::get<processing::DenoiseParameters>(s.parameters).mode=processing::DenoiseMode::Median;params=parametersJson(s);id=stageId(s.id);}}
    if(c.operation=="orientation") params={{"terminalMapping",c.gray8?"nearest_positive_v_div_257":"identity_u16"},{"orientation",orientationJson(c.orientation)}};
    if(full) params=pipelineJson(processing::standardPipeline());
    const bool swapped=c.orientation.rotation==core::Rotation::Degrees90 || c.orientation.rotation==core::Rotation::Degrees270;
    QJsonObject stage={{"id",id},{"variant",qs(c.variant)},{"enabled",true},{"scope",full?"full_frame":c.operation=="orientation"?"presentation_operations":"standalone_stage"},{"fullStandardExecuted",full},{"sourceDomain",(c.mono12 || full)?"sensor_native":c.operation=="orientation" && !c.gray8?"display":"canonical_u16"},{"outputStorage",c.gray8?"gray8":"uint16"},{"parameters",params},{"implementation",c.exact?"Lumora exact scalar/coordinate implementation":"Lumora prepared CPU implementation"},{"provenance",c.exact?"Independent rational/identity/inverse-coordinate oracle; reviewed generator SHA256 89c056d22dc87584a75b080237b155ebdd3c90a38486f7c31f2836c41505ec30":"Lumora-owned code (Apache-2.0); adapted OpenCV 4.12.0 CLAHE source retains NVIDIA 2013/Itseez 2014 three-clause BSD terms; OpenCV coefficient generation and Lumora prepared separable-double Gaussian/detail execution; see THIRD-PARTY-LICENSES/OpenCV-CLAHE.txt"}};
    return {{"caseId",qs(c.id)},{"classification",c.exact?"exact_independent":"provisional_backend"},{"pattern",QJsonObject{{"id",qs(c.pattern.id)},{"version",1},{"width",integer(c.pattern.width)},{"height",integer(c.pattern.height)},{"maximum",integer(c.pattern.maximum)},{"seed",c.pattern.id=="xorshift32_u16_v1"?integer(c.pattern.seed):QJsonValue()}}},{"stage",stage},{"sourceDescriptor",descriptorJson(monoFormat(c.mono12),layoutFor(c.pattern.width,c.pattern.height))},{"orientation",orientationJson(c.orientation)},{"orientedDimensions",dimensions(swapped?c.pattern.height:c.pattern.width,swapped?c.pattern.width:c.pattern.height)}};
}
}
