#include "EvidenceWorkload.hpp"
#include <gtest/gtest.h>
using namespace lumora::evidence;
TEST(PipelineReferences, AllReviewedIndependentOraclesMatchProductionExactly) {
    for(bool smoke:{false,true}) {
        const auto cases=referenceCases(smoke);ASSERT_EQ(cases.size(),13U);
        const auto base=std::filesystem::path(LUMORA_EXACT_ORACLE_ROOT)/(smoke?"smoke":"ordinary");
        EXPECT_NO_THROW(validateArtifact(readFile(base/"manifest.json"),base));
        const auto manifest=strictObject(readFile(std::filesystem::path(LUMORA_EXACT_ORACLE_ROOT)/"independent-oracles.json"));
        for(const auto& c:cases) if(c.exact) {
            SCOPED_TRACE(c.id);const auto source=readFile(base/(c.id+"-source.pgm")),expected=readFile(base/(c.id+"-expected.pgm"));
            EXPECT_EQ(makePattern(c.pattern),decodePgm(source));
            EXPECT_EQ(encodePgm(executeCase(c,decodePgm(source))),expected);
            bool found=false;for(const auto& v:manifest["cases"].toArray()) {auto e=v.toObject();if(e["caseId"]==qs(c.id) && e["sizeProfile"]==(smoke?"smoke":"ordinary")) {found=true;EXPECT_EQ(qs(sha256(source)),e["source"].toObject()["sha256"].toString());EXPECT_EQ(qs(sha256(expected)),e["expected"].toObject()["sha256"].toString());}}
            EXPECT_TRUE(found);
        }
    }
}
