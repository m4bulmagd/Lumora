#include "EvidenceMeasurement.hpp"
#include "EvidenceHeap.hpp"
#include <QCoreApplication>
#include <cstdio>
int main(int argc,char** argv) {
    using namespace lumora::evidence;QCoreApplication app(argc,argv);QJsonObject root;std::filesystem::path output;
    try {
        std::vector<std::string> args;for(const auto& a:app.arguments().mid(1)) args.push_back(a.toStdString());const auto o=parseOptions(Tool::Allocation,args);
        if(o.help) {std::puts("lumora_processing_allocation_probe --output FILE [--source-format mono16|mono12] [--smoke] [--heap-trace DIRECTORY]; normal=100 warm/1000 cycles for each identity/nonidentity at64x48; smoke=2/20 nonproof");return 0;}
        const auto control=allocationControls();std::unique_ptr<HeapTrace> heap;if(!o.heapTrace.empty()) heap=std::make_unique<HeapTrace>(o.heapTrace);
        output=o.output;root={{"schemaVersion",o.sourceFormat==SourceFormat::Mono12?3:2},{"artifactType","lumora.processing.allocation-proof"},{"runStatus","incomplete"},{"complete",false},{"smoke",o.smoke},{"generatedUtc",utcNow()},{"rows",QJsonArray{}},{"failure",QJsonValue()}};if(o.sourceFormat==SourceFormat::Mono12) root["sourceFormat"]="mono12";writeProgress(output,root);
        for(bool oriented:{false,true}) {
            const auto id=oriented?"full_standard_nonidentity":"full_standard_identity";const lumora::core::Orientation orientation{oriented,false,oriented?lumora::core::Rotation::Degrees90:lumora::core::Rotation::Degrees0};
            const auto input=makeMeasurementInput(64,48,o.sourceFormat);Session session(input,orientation,o.sourceFormat);std::uint64_t fingerprint=1469598103934665603ULL;
            HelperAllocationControlContext helperContext;
            const std::string helperTraceName=std::string(id)+"-helpers";
            if(heap) heap->start(helperTraceName);
            lumora::test::beginAllocationTracking();
            const auto helperResult=session.helperAllocationControl(helperContext);
            const auto helperCounts=lumora::test::endAllocationMeasurement();
            if(heap) heap->stop();
            const auto helperTrace=heap?QJsonValue(heap->helperControlResult(helperResult.executionSlots)):QJsonValue();
            const auto helperControl=helperControlJson(helperResult,helperCounts,helperTrace);
            for(std::uint32_t i=0;i<o.warmUp;++i) if(!session.cycle(i,fingerprint)) throw Error(5,"Allocation warm-up processing failed");
            if(heap) heap->start(id);
            bool successful=true;fingerprint=1469598103934665603ULL;
            lumora::test::beginAllocationTracking();for(std::uint32_t i=0;i<o.measured;++i) {if(!session.cycle(i,fingerprint)) {successful=false;break;}}session.releaseMeasured();const auto counts=lumora::test::endAllocationMeasurement();if(heap) heap->stop();
            const auto trace=heap?QJsonValue(heap->result(o.measured,o.smoke)):QJsonValue();
            if(!successful || counts.allocations || counts.allocatedBytes || counts.deallocations) throw Error(5,"Prepared successful full Standard allocation proof failed");
            auto positive=allocationJson(control,"eight non-elidable replacement-new positive controls before warm-up; counters reset before each measured region");positive["cycles"]=1;
            auto measured=allocationJson(counts,"preexisting RawFrame; full Standard paired display mapping/orientation, pooled publication/consume, five-owner retention, every cycle eviction and final ring/sentinel release; acquisition/preparation/JSON/trace parsing excluded");measured["cycles"]=integer(o.measured);
            QJsonObject row{{"rowId",id},{"pipelineDefinition",pipelineJson(lumora::processing::standardPipeline())},{"input",inputJson(input,o.sourceFormat)},{"sourceDescriptor",descriptorJson(monoFormat(o.sourceFormat==SourceFormat::Mono12),layoutFor(64,48))},{"orientation",orientationJson(orientation)},{"orientedDimensions",dimensions(oriented?48:64,oriented?64:48)},{"provenance",provenance()},{"execution",execution()},{"resourcePlan",session.resources()},{"warmUpFrames",integer(o.warmUp)},{"measuredCycles",integer(o.measured)},{"positiveControl",positive},{"helperControl",helperControl},{"measuredRegion",measured},{"heapTrace",trace},{"processingErrors",0},{"drops",0},{"complete",true}};
            auto rows=root["rows"].toArray();rows.append(row);root["rows"]=rows;writeProgress(output,root);
            if(heap && trace.toObject()["status"]!="complete") throw Error(5,"Prepared region contained glibc allocator events");
        }
        writeProgress(output,root,true);return 0;
    } catch(const Error& e) {if(!root.isEmpty()) {root["failure"]=QJsonObject{{"code",QString::number(e.exitCode)},{"message",e.what()},{"completedRows",root["rows"].toArray().size()}};try {writeProgress(output,root);}catch(...) {}}std::fprintf(stderr,"%s\n",e.what());return e.exitCode;}catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 3;}
}
