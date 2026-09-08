#include <lumora/processing/ProcessingDefaults.hpp>
#include <gtest/gtest.h>
using namespace lumora::processing;
TEST(ProcessingDefaults, StandardIsExplicitAndStartupUnchanged) {
    const auto p=standardPipeline(); ASSERT_EQ(p.stages.size(),8U);
    for(std::size_t i=0;i<8;++i) EXPECT_EQ(p.stages[i].enabled,i!=7);
    EXPECT_EQ(p.version,(lumora::core::PipelineVersion{1,1,0}));
    EXPECT_DOUBLE_EQ(std::get<WindowLevelParameters>(p.stages[1].parameters).window,65535);
    EXPECT_DOUBLE_EQ(std::get<WindowLevelParameters>(p.stages[1].parameters).level,32767.5);
    EXPECT_DOUBLE_EQ(std::get<BrightnessContrastParameters>(p.stages[2].parameters).brightness,0);
    EXPECT_DOUBLE_EQ(std::get<BrightnessContrastParameters>(p.stages[2].parameters).contrast,1);
    EXPECT_DOUBLE_EQ(std::get<GammaParameters>(p.stages[3].parameters).gamma,1);
    EXPECT_DOUBLE_EQ(std::get<ClaheParameters>(p.stages[4].parameters).clipLimit,2);
    EXPECT_EQ(std::get<ClaheParameters>(p.stages[4].parameters).tileGridSize,8U);
    const auto d=std::get<DenoiseParameters>(p.stages[5].parameters);
    EXPECT_EQ(d.mode,DenoiseMode::Gaussian); EXPECT_EQ(d.kernelSize,3U); EXPECT_DOUBLE_EQ(d.sigma,0);
    const auto s=std::get<SharpenParameters>(p.stages[6].parameters);
    EXPECT_DOUBLE_EQ(s.amount,1); EXPECT_DOUBLE_EQ(s.radius,1); EXPECT_DOUBLE_EQ(s.threshold,0);
    const auto startup=defaultPipeline(); for(std::size_t i=2;i<8;++i) EXPECT_FALSE(startup.stages[i].enabled);
}
TEST(ProcessingDefaults, EqualityIgnoresOnlyRevision) {
    const auto p=standardPipeline(); auto q=p; q.version.configurationRevision=99;
    EXPECT_TRUE(semanticallyEqualPipelineDefinitions(p,q));
    q.version.orderVersion++; EXPECT_FALSE(semanticallyEqualPipelineDefinitions(p,q));
    q=p; q.version.schemaVersion++; EXPECT_FALSE(semanticallyEqualPipelineDefinitions(p,q));
    q=p; q.stages.pop_back(); EXPECT_FALSE(semanticallyEqualPipelineDefinitions(p,q));
    q=p; std::swap(q.stages[1],q.stages[2]); EXPECT_FALSE(semanticallyEqualPipelineDefinitions(p,q));
    for(std::size_t i=0;i<p.stages.size();++i) { q=p; q.stages[i].enabled=!q.stages[i].enabled; EXPECT_FALSE(semanticallyEqualPipelineDefinitions(p,q)); }
    q=p; q.stages[5].parameters=DenoiseParameters{DenoiseMode::Gaussian,3,0.1}; EXPECT_FALSE(semanticallyEqualPipelineDefinitions(p,q));
    q=p; q.stages[0].parameters=InvertParameters{}; EXPECT_FALSE(semanticallyEqualPipelineDefinitions(p,q));
}

TEST(ProcessingDefaults, EveryParameterParticipatesInSemanticEquality) {
    const auto p=standardPipeline();
    const std::vector<std::pair<std::size_t,StageParameters>> changes={
        {1,WindowLevelParameters{64000,32767.5}}, {1,WindowLevelParameters{65535,30000}},
        {2,BrightnessContrastParameters{0.1,1}}, {2,BrightnessContrastParameters{0,2}},
        {3,GammaParameters{2}}, {4,ClaheParameters{3,8}}, {4,ClaheParameters{2,4}},
        {5,DenoiseParameters{DenoiseMode::Median,3,0}}, {5,DenoiseParameters{DenoiseMode::Gaussian,5,0}},
        {5,DenoiseParameters{DenoiseMode::Gaussian,3,1}}, {6,SharpenParameters{2,1,0}},
        {6,SharpenParameters{1,2,0}}, {6,SharpenParameters{1,1,1}}};
    for(const auto& [index,parameters]:changes) {
        auto changed=p;changed.stages[index].parameters=parameters;
        EXPECT_FALSE(semanticallyEqualPipelineDefinitions(p,changed));
    }
}
