#include <lumora/processing/ProcessingDefaults.hpp>
#include <type_traits>
namespace lumora::processing {
PipelineDefinition standardPipeline() {
    return {{1,1,0},{{StageId::Normalize,true,NormalizationParameters{}},
        {StageId::WindowLevel,true,WindowLevelParameters{65535,32767.5}},
        {StageId::BrightnessContrast,true,BrightnessContrastParameters{0,1}},
        {StageId::Gamma,true,GammaParameters{1}}, {StageId::Clahe,true,ClaheParameters{2,8}},
        {StageId::Denoise,true,DenoiseParameters{DenoiseMode::Gaussian,3,0}},
        {StageId::Sharpen,true,SharpenParameters{1,1,0}}, {StageId::Invert,false,InvertParameters{}}}};
}
bool semanticallyEqualPipelineDefinitions(const PipelineDefinition& a,const PipelineDefinition& b) noexcept {
    if(a.version.schemaVersion!=b.version.schemaVersion || a.version.orderVersion!=b.version.orderVersion || a.stages.size()!=b.stages.size()) return false;
    for(std::size_t i=0;i<a.stages.size();++i) {
        const auto& x=a.stages[i]; const auto& y=b.stages[i];
        if(x.id!=y.id || x.enabled!=y.enabled || x.parameters.index()!=y.parameters.index()) return false;
        const bool equal=std::visit([&](const auto& p) {
            using T=std::decay_t<decltype(p)>; const auto& q=std::get<T>(y.parameters);
            if constexpr(std::is_same_v<T,WindowLevelParameters>) return p.window==q.window && p.level==q.level;
            else if constexpr(std::is_same_v<T,BrightnessContrastParameters>) return p.brightness==q.brightness && p.contrast==q.contrast;
            else if constexpr(std::is_same_v<T,GammaParameters>) return p.gamma==q.gamma;
            else if constexpr(std::is_same_v<T,ClaheParameters>) return p.clipLimit==q.clipLimit && p.tileGridSize==q.tileGridSize;
            else if constexpr(std::is_same_v<T,DenoiseParameters>) return p.mode==q.mode && p.kernelSize==q.kernelSize && p.sigma==q.sigma;
            else if constexpr(std::is_same_v<T,SharpenParameters>) return p.amount==q.amount && p.radius==q.radius && p.threshold==q.threshold;
            else return true;
        },x.parameters);
        if(!equal) return false;
    }
    return true;
}
}
