#include "EvidenceSupport.hpp"
#include <gtest/gtest.h>
#include <limits>
using namespace lumora::evidence;
TEST(EvidencePrimitives, StrictCliWorkloadAndCounts) {
    auto parse=[](Tool t,std::initializer_list<std::string> a){return parseOptions(t,std::vector<std::string>(a));};
    EXPECT_THROW(parse(Tool::Benchmark,{}),Error);
    for(auto args:std::vector<std::vector<std::string>>{{"--output","x","--smoke","--smoke"},{"--output","x","--warm-up","2"},{"--output","x","--sizes","8,8","--warm-up","2","--measured","5"},{"--output","x","--sizes","8,16","--warm-up","2junk","--measured","5"},{"--output","x","--smoke","--measured","5"},{"--output","x","--sizes","4294967296","--warm-up","2","--measured","5"},{"--output","x","positional"},{"--output","x","--help"}}) EXPECT_THROW(parseOptions(Tool::Benchmark,args),Error);
    const auto smoke=parse(Tool::Benchmark,{"--output","x","--smoke"}); EXPECT_EQ(smoke.sizes,(std::vector<std::uint32_t>{64,128})); EXPECT_EQ(smoke.measured,5U);
    EXPECT_EQ(parse(Tool::Allocation,{"--output","x"}).measured,1000U);
    EXPECT_EQ(parse(Tool::Allocation,{"--output","x","--smoke"}).measured,20U);
}
TEST(EvidencePrimitives, PgmUsesLiteralBigEndianAndExactPayload) {
    Image expected{5,1,{0,1,255,256,65535}};
    const std::string bytes=std::string("P5\n5 1\n65535\n")+std::string("\0\0\0\1\0\xff\1\0\xff\xff",10);
    EXPECT_EQ(encodePgm(expected),bytes); EXPECT_EQ(decodePgm(bytes),expected);
    auto swapped=bytes; std::swap(swapped[swapped.size()-4],swapped[swapped.size()-3]); EXPECT_NE(decodePgm(swapped),expected);
    for(auto bad:std::vector<std::string>{"P2\n5 1\n65535\n","P5\n5 1\n255\n",bytes+"x",bytes.substr(0,bytes.size()-1),"P5\n0 1\n65535\n","P5\n999999999999999999999 1\n65535\n"}) EXPECT_THROW(decodePgm(bad),Error);
}
TEST(EvidencePrimitives, MidpointMedianNearestRankAndOverflow) {
    auto s=statistics(std::vector<std::uint64_t>{9,2,1,4},100); EXPECT_DOUBLE_EQ(s.median,3); EXPECT_EQ(s.p95,9U); EXPECT_DOUBLE_EQ(s.fps,40000000);
    EXPECT_DOUBLE_EQ(statistics(std::vector<std::uint64_t>{1,2},3).median,1.5);
    auto high=std::numeric_limits<std::uint64_t>::max(); EXPECT_GT(statistics(std::vector<std::uint64_t>{high-1,high},high).median,0);
    EXPECT_THROW(statistics({},1),Error);
    EXPECT_THROW(statistics(std::vector<std::uint64_t>{1},0),Error);
}
TEST(EvidencePrimitives, IntegerPatternsMatchLiteralDefinitions) {
    EXPECT_EQ(makePattern({"ramp_u16_v1",3,1,4095}).pixels,(std::vector<std::uint16_t>{0,2047,4095}));
    EXPECT_EQ(makePattern({"gradient_xy_u16_v1",2,2}).pixels,(std::vector<std::uint16_t>{0,32767,32767,65534}));
    EXPECT_EQ(makePattern({"step_edges_u16_v1",2,2}).pixels,(std::vector<std::uint16_t>{4096,61440,61440,4096}));
    EXPECT_THROW(makePattern({"unknown",8,8}),Error);
}
TEST(EvidencePrimitives, TraceCountsFailuresReallocAndStrictBoundaries) {
    const auto c=parseTrace("= Start\n@ caller + 0x1 0x10\n@ caller + (nil) 0xff\n@ caller < 0x1\n@ caller > 0x2 0x20\n@ caller ! 0x2 0xffff\n@ caller - 0x2\n= End\n");
    EXPECT_EQ(c.events,6U); EXPECT_EQ(c.allocations,1U); EXPECT_EQ(c.allocationFailures,1U); EXPECT_EQ(c.reallocOld,1U); EXPECT_EQ(c.reallocNew,1U); EXPECT_EQ(c.reallocFailures,1U); EXPECT_EQ(c.releases,1U); EXPECT_EQ(c.reportedBytes,48U);
    EXPECT_EQ(parseTrace("= Start\n= End\n").events,0U);
    for(auto bad:std::vector<std::string>{"","= Start\n","= End\n= Start\n","= Start\n?\n= End\n","= Start\n= End\n= End\n","= Start\n@ c + 0x1 0xffffffffffffffff\n@ c + 0x2 0x1\n= End\n"}) EXPECT_THROW(parseTrace(bad),Error);
}

#include "EvidenceJson.hpp"
TEST(EvidenceArtifacts, DuplicateKeysIncludingEscapesAreRejectedBeforeQtCollapsesThem) {
    EXPECT_THROW(strictObject("{\"a\":1,\"a\":2}"),Error);
    EXPECT_THROW(strictObject("{\"a\":1,\"\\u0061\":2}"),Error);
    EXPECT_THROW(strictObject("{\"nested\":{\"a\":1,\"a\":2}}"),Error);
    EXPECT_THROW(strictObject("{} trailing"),Error);
    EXPECT_NO_THROW(strictObject("{\"a\":{},\"b\":{\"a\":2}}"));
}
TEST(EvidenceArtifacts, TypedPipelineSerializationRejectsVariantMismatch) {
    auto p=lumora::processing::standardPipeline(); const auto o=strictObject(serializePipeline(p));
    EXPECT_EQ(o["stages"].toArray().size(),8);
    p.stages[0].parameters=lumora::processing::InvertParameters{};
    EXPECT_THROW(serializePipeline(p),Error);
}
TEST(EvidenceArtifacts, OwnedArtifactTypesRejectMissingAndUnknownFields) {
    EXPECT_THROW(validateArtifact("{}"),Error);
    EXPECT_THROW(validateArtifact("{\"schemaVersion\":1,\"artifactType\":\"unknown\"}"),Error);
}

#include "EvidenceWorkload.hpp"
#include <QTemporaryDir>
TEST(EvidenceValidation, BenchmarkCompletenessWorkloadAndUnknownKeysAreStrict) {
    QJsonObject root{{"schemaVersion",1},{"artifactType","lumora.processing.benchmark"},{"runStatus","incomplete"},{"complete",false},{"workload",QJsonObject{{"kind","smoke"},{"requestedSizes",QJsonArray{64,128}},{"warmUpFrames",2},{"measuredFrames",5}}},{"generatedUtc","2026-09-08T00:00:00.000Z"},{"rows",QJsonArray{}},{"failure",QJsonValue()}};
    EXPECT_NO_THROW(validateArtifact(json(root)));
    auto invalid=root;invalid["complete"]=true;invalid["runStatus"]="complete";
    EXPECT_THROW(validateArtifact(json(invalid)),Error);
    invalid=root;invalid["extra"]=1;
    EXPECT_THROW(validateArtifact(json(invalid)),Error);
    invalid=root;auto work=invalid["workload"].toObject();work["warmUpFrames"]="2";invalid["workload"]=work;
    EXPECT_THROW(validateArtifact(json(invalid)),Error);
}
TEST(EvidenceValidation, CandidatePayloadHashDimensionsAndAcceptanceAreValidated) {
    QTemporaryDir temp;ASSERT_TRUE(temp.isValid());const auto base=std::filesystem::path(temp.path().toStdU16String());
    const auto c=referenceCases(true).front();const auto input=makePattern(c.pattern);atomicWrite(base/"source.pgm",encodePgm(input));
    auto row=caseMetadata(c);row["source"]=payloadJson(base,"source.pgm",input);row["candidate"]=payloadJson(base,"source.pgm",input);
    row["comparison"]=QJsonObject{{"maxAbsoluteU16Error",QJsonValue()},{"changedPixelFraction",QJsonValue()},{"meanAbsoluteError",QJsonValue()}};
    row["acceptance"]=QJsonObject{{"status","candidate"},{"thresholds",QJsonValue()},{"review",QJsonValue()}};
    QJsonObject root{{"schemaVersion",1},{"artifactType","lumora.processing.reference-candidate"},{"status","candidate"},{"workloadKind","smoke"},{"complete",false},{"generatedUtc","2026-09-08T00:00:00.000Z"},{"provenance",provenance()},{"pipelineDefinition",pipelineJson(lumora::processing::standardPipeline())},{"cases",QJsonArray{row}},{"failure",QJsonValue()}};
    EXPECT_NO_THROW(validateArtifact(json(root),base));
    auto badRow=row;auto payload=badRow["candidate"].toObject();payload["sha256"]=QString(64,'0');badRow["candidate"]=payload;root["cases"]=QJsonArray{badRow};
    EXPECT_THROW(validateArtifact(json(root),base),Error);
    root["cases"]=QJsonArray{row,row};
    EXPECT_THROW(validateArtifact(json(root),base),Error);
    root["cases"]=QJsonArray{row};atomicWrite(base/"source.pgm",encodePgm(Image{1,1,{0}}));
    EXPECT_THROW(validateArtifact(json(root),base),Error);
}
TEST(EvidenceValidation, AcceptedWorkstationCannotOmitDesignationAndEvidence) {
    const auto pending=readFile(std::filesystem::path(LUMORA_WORKSTATION_TEMPLATE));
    EXPECT_NO_THROW(validateArtifact(pending));
    auto invalid=strictObject(pending);invalid["status"]="accepted";
    EXPECT_THROW(validateArtifact(json(invalid)),Error);
}

TEST(EvidenceArtifactFiles, GeneratedBenchmarkAndAllocationRejectSemanticMutations) {
    const auto directory=qEnvironmentVariable("LUMORA_EVIDENCE_DIRECTORY");if(directory.isEmpty()) GTEST_SKIP()<<"Run through Processing.EvidenceSmoke after CLI artifacts exist.";
    const auto base=std::filesystem::path(directory.toStdU16String());auto benchmark=strictObject(readFile(base/"benchmark.json"));auto allocation=strictObject(readFile(base/"allocation.json"));
    EXPECT_NO_THROW(validateArtifact(json(benchmark),base));
    EXPECT_NO_THROW(validateArtifact(json(allocation),base));
    auto rows=benchmark["rows"].toArray();ASSERT_EQ(rows.size(),22);
    auto invalid=benchmark;auto bad=rows;bad[1]=bad[0];invalid["rows"]=bad;
    EXPECT_THROW(validateArtifact(json(invalid),base),Error);
    bad=rows;auto row=bad[0].toObject();auto resource=row["resourcePlan"].toObject();resource["fixedBytes"]=0;row["resourcePlan"]=resource;bad[0]=row;invalid=benchmark;invalid["rows"]=bad;
    EXPECT_THROW(validateArtifact(json(invalid),base),Error);
    bad=rows;row=bad[0].toObject();auto timing=row["timing"].toObject();timing["fps"]=1;row["timing"]=timing;bad[0]=row;invalid=benchmark;invalid["rows"]=bad;
    EXPECT_THROW(validateArtifact(json(invalid),base),Error);
    auto allocationRows=allocation["rows"].toArray();ASSERT_EQ(allocationRows.size(),2);invalid=allocation;invalid["smoke"]=false;
    EXPECT_THROW(validateArtifact(json(invalid),base),Error);
    row=allocationRows[1].toObject();row["orientedDimensions"]=dimensions(64,48);allocationRows[1]=row;invalid=allocation;invalid["rows"]=allocationRows;
    EXPECT_THROW(validateArtifact(json(invalid),base),Error);
}
TEST(EvidencePrimitives, TraceRejectsUnpairedTransitionsAndTrailingContent) {
    for(const auto& text:std::vector<std::string>{"= Start\n@ c > 0x1 0x1\n= End\n","= Start\n@ c < 0x1\n= End\n","= Start\n@ c < 0x1\n@ c - 0x1\n= End\n","= Start\n@ c + (nil) 0x1\n= End","= Start\n@ c ? 0x1\n= End\n"}) EXPECT_THROW(parseTrace(text),Error);
}

TEST(EvidencePrimitives, CandidateDirectoryRefusesCommittedTreeAliasesAndExistingFiles) {
    QTemporaryDir temp;ASSERT_TRUE(temp.isValid());const auto base=std::filesystem::path(temp.path().toStdU16String());
    atomicWrite(base/"existing","do not replace");
    EXPECT_THROW(ensureCandidateDirectory(base),Error);
    EXPECT_THROW(ensureCandidateDirectory(base/"existing"),Error);
    EXPECT_THROW(ensureCandidateDirectory(std::filesystem::path(LUMORA_EXACT_ORACLE_ROOT)/"new-candidate"),Error);
    EXPECT_EQ(readFile(base/"existing"),"do not replace");
    EXPECT_NO_THROW(ensureCandidateDirectory(base/"new-output"));
}
TEST(EvidencePrimitives, AtomicReplacementRetainsAnExistingArtifactOnOpenFailure) {
    QTemporaryDir temp;ASSERT_TRUE(temp.isValid());const auto base=std::filesystem::path(temp.path().toStdU16String());
    atomicWrite(base/"record.json","original");
    EXPECT_THROW(atomicWrite(base/"missing-parent"/"record.json","new"),Error);
    EXPECT_EQ(readFile(base/"record.json"),"original");
    atomicWrite(base/"record.json","replacement");
    EXPECT_EQ(readFile(base/"record.json"),"replacement");
}

TEST(EvidenceValidation, AcceptedClaimsRequireProvenanceAndReferencedEvidenceEvenWithDesignation) {
    auto record=strictObject(readFile(std::filesystem::path(LUMORA_WORKSTATION_TEMPLATE)));
    record["status"]="accepted";record["designation"]=QJsonObject{{"machineId","test-only"},{"selectedBy","test reviewer"},{"selectedUtc","2026-09-08T00:00:00.000Z"}};
    EXPECT_THROW(validateArtifact(json(record)),Error); // Missing machine/provenance facts.
    const std::vector<std::pair<const char*,const char*>> fields={{"machine","manufacturer model firmware"},{"cpu","manufacturer model architecture physicalCores logicalCores"},{"gpu","manufacturer model dedicatedMemoryBytes"},{"ram","installedBytes speedMtPerSecond"},{"os","name edition version build"},{"compiler","id version"},{"dependencies","opencvVersion opencvVcpkgPortVersion vcpkgBaseline qtVersion"},{"drivers","gpu chipset"},{"power","plan acPower thermalCondition"},{"threading","threadModel opencvThreads logicalProcessorAffinity"},{"build","configuration compileOptions sourceRevision sourceDirty artifactSha256"}};
    for(const auto& [name,keys]:fields) {QJsonObject o;for(const auto& key:QString::fromLatin1(keys).split(' ')) o[key]="test-only";record[name]=o;}
    for(const auto& [name,key]:std::vector<std::pair<const char*,const char*>>{{"cpu","physicalCores"},{"cpu","logicalCores"},{"gpu","dedicatedMemoryBytes"},{"ram","installedBytes"},{"ram","speedMtPerSecond"},{"threading","opencvThreads"}}) {auto o=record[name].toObject();o[key]=1;record[name]=o;}
    auto os=record["os"].toObject();os["name"]="Windows";record["os"]=os;auto power=record["power"].toObject();power["acPower"]=true;record["power"]=power;
    auto build=record["build"].toObject();build["configuration"]="Release";build["sourceDirty"]=false;build["sourceRevision"]=QString(40,'0');build["artifactSha256"]=QString(64,'0');record["build"]=build;
    EXPECT_THROW(validateArtifact(json(record)),Error); // Missing acceptance evidence hashes.
    auto acceptance=record["acceptance"].toObject();for(const auto& key:{"benchmarkArtifactSha256","allocationArtifactSha256","referenceManifestSha256","freshnessArtifactSha256"}) acceptance[key]=QString(64,'0');acceptance["reviewedBy"]="test reviewer";acceptance["reviewedUtc"]="2026-09-08T00:00:00.000Z";
    QJsonArray thresholds,hashes;for(const auto& c:referenceCases(false)) if(!c.exact) {thresholds.append(QJsonObject{{"caseId",qs(c.id)},{"maxAbsoluteU16Error",0},{"changedPixelFraction",0},{"meanAbsoluteError",0}});hashes.append(QJsonObject{{"caseId",qs(c.id)},{"sourceSha256",QString(64,'0')},{"expectedSha256",QString(64,'0')}});}acceptance["approvedThresholds"]=thresholds;acceptance["approvedReferenceHashes"]=hashes;record["acceptance"]=acceptance;
    QTemporaryDir empty;ASSERT_TRUE(empty.isValid());
    EXPECT_THROW(validateArtifact(json(record),std::filesystem::path(empty.path().toStdU16String())),Error); // Hashes alone cannot replace referenced artifacts.
}

TEST(EvidenceValidation, FullReferenceContextStartsFromSensorNativeRawInput) {
    for(const auto& c:referenceCases(true)) if(c.operation.starts_with("full_standard_")) {
        const auto stage=caseMetadata(c)["stage"].toObject();
        EXPECT_EQ(stage["sourceDomain"],"sensor_native");
        EXPECT_EQ(stage["fullStandardExecuted"],true);
    }
}
namespace {
// Synthetic review data exercises validation only. Exact payloads are copied
// from independent fixtures; backend zeros and altered smoke timings are never
// reference or performance evidence and exist only in this temporary directory.
QJsonObject syntheticReviewedManifest(const std::filesystem::path& base) {
    const auto fixtures=std::filesystem::path(LUMORA_EXACT_ORACLE_ROOT)/"ordinary";
    auto manifest=strictObject(readFile(fixtures/"manifest.json"));
    auto rows=manifest["cases"].toArray();
    for(const auto& value:rows) {
        const auto row=value.toObject();
        for(const auto* key:{"source","expected"}) {
            const auto file=std::filesystem::path(row[key].toObject()["file"].toString().toStdU16String());
            atomicWrite(base/file,readFile(fixtures/file));
        }
    }
    for(const auto& c:referenceCases(false)) {
        if(c.exact) continue;
        auto row=caseMetadata(c);
        const auto input=makePattern(c.pattern);
        const auto dims=row["orientedDimensions"].toObject();
        Image output{static_cast<std::uint32_t>(dims["width"].toInt()),static_cast<std::uint32_t>(dims["height"].toInt()),{}};
        output.pixels.resize(static_cast<std::size_t>(output.width)*output.height);
        const auto sourceFile=c.id+"-synthetic-source.pgm",expectedFile=c.id+"-synthetic-expected.pgm";
        atomicWrite(base/sourceFile,encodePgm(input));
        atomicWrite(base/expectedFile,encodePgm(output));
        row["source"]=payloadJson(base,sourceFile,input);
        row["expected"]=payloadJson(base,expectedFile,output,c.gray8);
        row["acceptance"]=QJsonObject{{"status","reviewed"},
            {"thresholds",QJsonObject{{"maxAbsoluteU16Error",4},{"changedPixelFraction",0.25},{"meanAbsoluteError",1.5}}},
            {"review",QJsonObject{{"reviewedBy","synthetic test reviewer"},{"reviewedUtc","2026-09-08T00:00:00.000Z"},{"sourceArtifactSha256",QString(64,'1')}}}};
        rows.append(row);
    }
    manifest["cases"]=rows;
    manifest["status"]="reviewed";
    manifest["complete"]=true;
    return manifest;
}
QJsonObject approvalArrays(const QJsonObject& manifest) {
    QJsonArray thresholds,hashes;
    for(const auto& value:manifest["cases"].toArray()) {
        const auto row=value.toObject();
        if(row["classification"]!="provisional_backend") continue;
        auto metrics=row["acceptance"].toObject()["thresholds"].toObject();
        metrics["caseId"]=row["caseId"];
        thresholds.append(metrics);
        hashes.append(QJsonObject{{"caseId",row["caseId"]},{"sourceSha256",row["source"].toObject()["sha256"]},{"expectedSha256",row["expected"].toObject()["sha256"]}});
    }
    return {{"approvedThresholds",thresholds},{"approvedReferenceHashes",hashes}};
}
QJsonObject syntheticWorkstationRecord() {
    auto record=strictObject(readFile(std::filesystem::path(LUMORA_WORKSTATION_TEMPLATE)));
    record["status"]="accepted";
    record["designation"]=QJsonObject{{"machineId","synthetic-test-only"},{"selectedBy","synthetic test reviewer"},{"selectedUtc","2026-09-08T00:00:00.000Z"}};
    const std::vector<std::pair<const char*,const char*>> fields={{"machine","manufacturer model firmware"},{"cpu","manufacturer model architecture physicalCores logicalCores"},{"gpu","manufacturer model dedicatedMemoryBytes"},{"ram","installedBytes speedMtPerSecond"},{"os","name edition version build"},{"compiler","id version"},{"dependencies","opencvVersion opencvVcpkgPortVersion vcpkgBaseline qtVersion"},{"drivers","gpu chipset"},{"power","plan acPower thermalCondition"},{"threading","threadModel opencvThreads logicalProcessorAffinity"},{"build","configuration compileOptions sourceRevision sourceDirty artifactSha256"}};
    for(const auto& [name,keys]:fields) {
        QJsonObject facts;
        for(const auto& key:QString::fromLatin1(keys).split(' ')) facts[key]="synthetic-test-only";
        record[name]=facts;
    }
    for(const auto& [name,key]:std::vector<std::pair<const char*,const char*>>{{"cpu","physicalCores"},{"cpu","logicalCores"},{"gpu","dedicatedMemoryBytes"},{"ram","installedBytes"},{"ram","speedMtPerSecond"},{"threading","opencvThreads"}}) {
        auto facts=record[name].toObject();facts[key]=1;record[name]=facts;
    }
    auto os=record["os"].toObject();os["name"]="Windows";record["os"]=os;
    auto power=record["power"].toObject();power["acPower"]=true;record["power"]=power;
    auto build=record["build"].toObject();
    build["configuration"]="Release";build["sourceDirty"]=false;
    build["sourceRevision"]=QString(40,'0');build["artifactSha256"]=QString(64,'0');record["build"]=build;
    return record;
}
}

TEST(EvidenceValidation, ReviewedManifestRequiresEveryBackendReview) {
    QTemporaryDir temp;ASSERT_TRUE(temp.isValid());
    const auto base=std::filesystem::path(temp.path().toStdU16String());
    const auto reviewed=syntheticReviewedManifest(base);
    ASSERT_NO_THROW(validateArtifact(json(reviewed),base));
    auto pending=reviewed;
    auto rows=pending["cases"].toArray();
    for(qsizetype index=0;index<rows.size();++index) {
        auto row=rows[index].toObject();
        if(row["classification"]!="provisional_backend") continue;
        row["acceptance"]=QJsonObject{{"status","pending_review"},{"thresholds",QJsonValue()},{"review",QJsonValue()}};
        rows[index]=row;
    }
    pending["cases"]=rows;pending["status"]="pending";
    ASSERT_NO_THROW(validateArtifact(json(pending),base));
    pending["status"]="reviewed";
    EXPECT_THROW(validateArtifact(json(pending),base),Error);
    for(const auto* field:{"status","thresholds","review"}) {
        auto invalid=reviewed;
        auto invalidRows=invalid["cases"].toArray();
        auto row=invalidRows.last().toObject();
        auto acceptance=row["acceptance"].toObject();
        if(std::string_view(field)=="status") {
            acceptance=QJsonObject{{"status","pending_review"},{"thresholds",QJsonValue()},{"review",QJsonValue()}};
        } else acceptance[field]=QJsonValue();
        row["acceptance"]=acceptance;invalidRows[invalidRows.size()-1]=row;invalid["cases"]=invalidRows;
        EXPECT_THROW(validateArtifact(json(invalid),base),Error)<<field;
    }
}

TEST(EvidenceArtifactFiles, AcceptedWorkstationApprovalsMatchReviewedManifest) {
    const auto directory=qEnvironmentVariable("LUMORA_EVIDENCE_DIRECTORY");
    if(directory.isEmpty()) GTEST_SKIP()<<"Run through Processing.EvidenceSmoke after CLI artifacts exist.";
    const auto smokeBase=std::filesystem::path(directory.toStdU16String());
    QTemporaryDir temp;ASSERT_TRUE(temp.isValid());
    const auto base=std::filesystem::path(temp.path().toStdU16String());
    const auto manifest=syntheticReviewedManifest(base);
    auto benchmark=strictObject(readFile(smokeBase/"benchmark.json"));
    auto allocation=strictObject(readFile(smokeBase/"allocation.json"));
    const auto smokeRows=benchmark["rows"].toArray();ASSERT_EQ(smokeRows.size(),22);
    QJsonArray rows;
    for(const auto size:{512,1024,2048}) for(qsizetype index=0;index<11;++index) {
        auto row=smokeRows[index].toObject();
        row["size"]=size;row["smoke"]=false;row["warmUpFrames"]=100;row["measuredFrames"]=500;
        row["sourceDescriptor"]=descriptorJson(monoFormat(),layoutFor(static_cast<std::uint32_t>(size),static_cast<std::uint32_t>(size)));
        row["orientedDimensions"]=dimensions(static_cast<std::uint32_t>(size),static_cast<std::uint32_t>(size));
        row["timing"]=QJsonObject{{"unit","nanoseconds"},{"sampleCount",500},{"median",1000000},{"p95NearestRank",1000000},{"wallElapsed",500000000},{"fps",1000}};
        auto facts=row["provenance"].toObject();
        auto source=facts["source"].toObject();source["revision"]=QString(40,'0');source["dirty"]=false;facts["source"]=source;
        auto host=facts["host"].toObject();host["osName"]="Windows";facts["host"]=host;
        auto build=facts["build"].toObject();build["configuration"]="Release";facts["build"]=build;
        row["provenance"]=facts;rows.append(row);
    }
    benchmark["rows"]=rows;
    benchmark["workload"]=QJsonObject{{"kind","standard"},{"requestedSizes",QJsonArray{512,1024,2048}},{"warmUpFrames",100},{"measuredFrames",500}};
    rows=allocation["rows"].toArray();
    for(qsizetype index=0;index<rows.size();++index) {
        auto row=rows[index].toObject();row["warmUpFrames"]=100;row["measuredCycles"]=1000;
        auto region=row["measuredRegion"].toObject();region["cycles"]=1000;row["measuredRegion"]=region;rows[index]=row;
    }
    allocation["rows"]=rows;allocation["smoke"]=false;
    atomicWrite(base/"benchmark.json",json(benchmark));
    atomicWrite(base/"allocation.json",json(allocation));
    atomicWrite(base/"manifest.json",json(manifest));
    atomicWrite(base/"freshness.json","synthetic test-only attachment");
    auto record=syntheticWorkstationRecord();
    auto acceptance=approvalArrays(manifest);
    for(const auto& [key,file]:std::vector<std::pair<const char*,const char*>>{{"benchmarkArtifactSha256","benchmark.json"},{"allocationArtifactSha256","allocation.json"},{"referenceManifestSha256","manifest.json"},{"freshnessArtifactSha256","freshness.json"}}) acceptance[key]=qs(sha256(readFile(base/file)));
    acceptance["reviewedBy"]="synthetic test reviewer";acceptance["reviewedUtc"]="2026-09-08T00:00:00.000Z";
    record["acceptance"]=acceptance;
    ASSERT_NO_THROW(validateArtifact(json(record),base));
    for(const auto* field:{"maxAbsoluteU16Error","changedPixelFraction","meanAbsoluteError","sourceSha256","expectedSha256"}) {
        auto invalid=record;auto invalidAcceptance=acceptance;
        const auto* arrayKey=std::string_view(field).ends_with("Sha256")?"approvedReferenceHashes":"approvedThresholds";
        auto entries=acceptance[arrayKey].toArray();auto entry=entries[0].toObject();
        entry[field]=std::string_view(field).ends_with("Sha256")?QJsonValue(QString(64,'0')):QJsonValue(0);
        entries[0]=entry;invalidAcceptance[arrayKey]=entries;invalid["acceptance"]=invalidAcceptance;
        EXPECT_THROW(validateArtifact(json(invalid),base),Error)<<field;
    }
    // Approval ordering has no authority; case IDs bind each value to its case.
    auto reordered=acceptance;
    for(const auto* key:{"approvedThresholds","approvedReferenceHashes"}) {
        QJsonArray reversed;const auto entries=acceptance[key].toArray();
        for(qsizetype index=entries.size();index>0;--index) reversed.append(entries[index-1]);
        reordered[key]=reversed;
    }
    record["acceptance"]=reordered;
    EXPECT_NO_THROW(validateArtifact(json(record),base));
}

TEST(EvidenceArtifacts, ClaheProvenanceRetainsAdaptedSourceLicenseNotice) {
    const auto text=execution()["algorithmProvenance"].toString();
    EXPECT_TRUE(text.contains("three-clause BSD"));
    EXPECT_TRUE(text.contains("THIRD-PARTY-LICENSES/OpenCV-CLAHE.txt"));
    EXPECT_TRUE(text.contains("Lumora-owned code (Apache-2.0)"));
    EXPECT_FALSE(text.contains("CLAHE arithmetic/source (Apache-2.0)"));
    for(const auto& c:referenceCases(true)) if(!c.exact) {
        const auto caseText=caseMetadata(c)["stage"].toObject()["provenance"].toString();
        EXPECT_TRUE(caseText.contains("three-clause BSD"));
        EXPECT_TRUE(caseText.contains("THIRD-PARTY-LICENSES/OpenCV-CLAHE.txt"));
    }
}
