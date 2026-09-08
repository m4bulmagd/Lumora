#include "EvidenceMeasurement.hpp"
#include <chrono>
#include <fstream>
#include <new>
#if defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif
namespace lumora::evidence {
namespace {
using Clock=std::chrono::steady_clock;
std::uint64_t elapsed(Clock::time_point start,Clock::time_point end) {return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count());}
QJsonValue workingBytes() {
#if defined(__linux__)
    std::ifstream stream("/proc/self/statm");std::uint64_t virtualPages=0,resident=0;const auto page=sysconf(_SC_PAGESIZE);if(stream>>virtualPages>>resident && page>0 && resident<=static_cast<std::uint64_t>(INT64_MAX)/static_cast<std::uint64_t>(page)) return integer(resident*static_cast<std::uint64_t>(page));
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};if(GetProcessMemoryInfo(GetCurrentProcess(),&counters,static_cast<DWORD>(sizeof(counters)))) return integer(counters.WorkingSetSize);
#endif
    return {};
}
QJsonObject workingSet(QJsonValue before,QJsonValue after) {
    if(before.isNull() || after.isNull()) return {{"metric","unavailable"},{"source","unavailable"},{"beforeBytes",QJsonValue()},{"afterBytes",QJsonValue()}};
#if defined(__linux__)
    return {{"metric","linux_resident_bytes"},{"source","proc_self_statm"},{"beforeBytes",before},{"afterBytes",after}};
#else
    return {{"metric","windows_working_set_bytes"},{"source","GetProcessMemoryInfo"},{"beforeBytes",before},{"afterBytes",after}};
#endif
}
}
test::AllocationCounts allocationControls() {
    // Volatile function pointers make the eight ABI calls non-elidable.
    void* (*volatile ordinary)(std::size_t)=::operator new;void* (*volatile array)(std::size_t)=::operator new[];
    void* (*volatile aligned)(std::size_t,std::align_val_t)=::operator new;void* (*volatile alignedArray)(std::size_t,std::align_val_t)=::operator new[];
    void* (*volatile noThrow)(std::size_t,const std::nothrow_t&) noexcept=::operator new;void* (*volatile noThrowArray)(std::size_t,const std::nothrow_t&) noexcept=::operator new[];
    void* (*volatile alignedNoThrow)(std::size_t,std::align_val_t,const std::nothrow_t&) noexcept=::operator new;void* (*volatile alignedNoThrowArray)(std::size_t,std::align_val_t,const std::nothrow_t&) noexcept=::operator new[];
    test::beginAllocationTracking();
    auto* a=ordinary(7);auto* b=array(11);auto* c=aligned(13,std::align_val_t{64});auto* d=alignedArray(17,std::align_val_t{64});auto* e=noThrow(19,std::nothrow);auto* f=noThrowArray(23,std::nothrow);auto* g=alignedNoThrow(29,std::align_val_t{64},std::nothrow);auto* h=alignedNoThrowArray(31,std::align_val_t{64},std::nothrow);
    const bool valid=a && b && c && d && e && f && g && h;
    ::operator delete(a);::operator delete[](b);::operator delete(c,std::align_val_t{64});::operator delete[](d,std::align_val_t{64});::operator delete(e,std::nothrow);::operator delete[](f,std::nothrow);::operator delete(g,std::align_val_t{64},std::nothrow);::operator delete[](h,std::align_val_t{64},std::nothrow);
    const auto counts=test::endAllocationMeasurement();if(!valid || counts.allocations!=8 || counts.allocatedBytes!=150 || counts.deallocations!=8) throw Error(5,"Replacement-new positive controls failed");return counts;
}
QJsonObject allocationJson(test::AllocationCounts c,const QString& region) {return {{"scope","cxx_replacement_new"},{"calls",integer(c.allocations)},{"bytes",integer(c.allocatedBytes)},{"deallocations",integer(c.deallocations)},{"armedRegion",region},{"coveredRoutes",QJsonArray{"ordinary","array","aligned","aligned_array","nothrow","nothrow_array","aligned_nothrow","aligned_nothrow_array"}},{"unsupportedRoutes",QJsonArray{"c_malloc_free","external_dll_private_heaps"}}};}
QJsonObject helperControlJson(HelperAllocationControlResult control,test::AllocationCounts counts,QJsonValue trace) {
    const auto helpers=control.executionSlots-1;const auto mask=(1U<<control.executionSlots)-2U;
    const auto bytes=helpers*256+helpers*(helpers+1)/2;
    if(!control.successful || control.observedHelperMask!=mask || control.callbackInvocations!=helpers || counts.allocations!=helpers || counts.allocatedBytes!=bytes || counts.deallocations!=helpers)
        throw Error(5,"Persistent helper allocation positive control failed");
    auto allocation=allocationJson(counts,"caller reset/armed before one synchronous helper control dispatch and ended after return");
    allocation["coveredRoutes"]=QJsonArray{"ordinary"};
    return {{"scope","prepared_cpu_executor_persistent_helpers"},{"expectedHelperMask",integer(mask)},{"observedHelperMask",integer(control.observedHelperMask)},{"callbackInvocations",integer(control.callbackInvocations)},{"cxxAllocation",allocation},{"glibcTrace",trace}};
}
QJsonObject measureBenchmarkRow(const std::string& id,std::uint32_t size,const Options& o) {
    const bool full=id.starts_with("full_standard_");const core::Orientation orientation{id=="full_standard_nonidentity",false,id=="full_standard_nonidentity"?core::Rotation::Degrees90:core::Rotation::Degrees0};
    if(o.sourceFormat==SourceFormat::Mono12 && !full) throw Error(2,"Mono12 benchmark supports full Standard rows only");
    auto source=makeMeasurementInput(size,size,o.sourceFormat);
    std::unique_ptr<Standalone> stage;std::unique_ptr<Session> session;
    if(full) session=std::make_unique<Session>(source,orientation,o.sourceFormat);else stage=std::make_unique<Standalone>(id,source);
    auto definition=full?processing::standardPipeline():stage->definition;auto resources=full?session->resources():stage->resources();
    std::vector<std::uint64_t> samples(o.measured);std::uint64_t fingerprint=1469598103934665603ULL;
    for(std::size_t i=0;i<o.warmUp;++i) if(!(full?session->cycle(i,fingerprint):stage->cycle())) throw Error(4,"Warm-up processing failed");
    const auto before=workingBytes();bool successful=true;fingerprint=1469598103934665603ULL;
    test::beginAllocationTracking();const auto wallStart=Clock::now();
    for(std::size_t i=0;i<o.measured;++i) {const auto start=Clock::now();const bool ok=full?session->cycle(i,fingerprint):stage->cycle();samples[i]=elapsed(start,Clock::now());if(!ok) {successful=false;break;}}
    if(full) session->releaseMeasured();
    const auto wall=elapsed(wallStart,Clock::now());const auto counts=test::endAllocationMeasurement();const auto after=workingBytes();
    if(!successful) throw Error(4,"Measured processing failed");
    if(counts.allocations || counts.allocatedBytes || counts.deallocations) throw Error(5,"Prepared successful region performed C++ allocation/release");
    const auto stats=statistics(samples,wall);const auto checksum=full?fullChecksum(*session->verificationOutput()):sha256(encodePgm(stage->output()));
    return {{"rowId",qs(id)},{"scope",full?"full_frame":"standalone_stage"},{"size",integer(size)},{"smoke",o.smoke},{"pipelineDefinition",pipelineJson(definition)},{"input",inputJson(source,o.sourceFormat)},{"sourceDescriptor",descriptorJson(monoFormat(o.sourceFormat==SourceFormat::Mono12),layoutFor(size,size))},{"orientation",orientationJson(orientation)},{"orientedDimensions",dimensions(size,size)},{"provenance",provenance()},{"execution",execution()},{"resourcePlan",resources},{"warmUpFrames",integer(o.warmUp)},{"measuredFrames",integer(o.measured)},
        {"timing",QJsonObject{{"unit","nanoseconds"},{"sampleCount",integer(o.measured)},{"median",stats.median},{"p95NearestRank",integer(stats.p95)},{"wallElapsed",integer(stats.wall)},{"fps",stats.fps}}},
        {"allocation",allocationJson(counts,full?"preexisting RawFrame; full Standard paired displays, orientation, pooled publication/consume, five-owner retention, cycle eviction and final ring/sentinel release":"named prepared stage only; preexisting input/output remain alive through post-region hashing")},{"workingSet",workingSet(before,after)},
        {"checksum",QJsonObject{{"algorithm","sha256"},{"value",qs(checksum)},{"provenance",full?"one extra unmeasured verification cycle; concatenated canonical P5 EnhancedU16, OriginalGray8 and EnhancedGray8":"complete P5 U16 output after measurement; standalone output buffer retained"},{"verificationCycles",full?1:0},{"measuredFingerprint",full?QJsonValue(QString::number(fingerprint,16)):QJsonValue()},{"fingerprintMethod",full?"FNV1a uint64;16 evenly spaced bytes per each of3 payloads each measured cycle; included in timing":"not_applicable"}}},{"processingErrors",0},{"drops",0},{"complete",true}};
}
void writeProgress(const std::filesystem::path& path,QJsonObject& root,bool complete) {
    root["complete"]=complete;if(root.contains("runStatus")) root["runStatus"]=complete?"complete":"incomplete";
    validateArtifact(json(root),path.parent_path());atomicWrite(path,json(root));
}
}
