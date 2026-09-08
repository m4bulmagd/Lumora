#include "EvidenceWorkload.hpp"
#include <QDateTime>
#include <QRegularExpression>
#include <cmath>
#include <algorithm>
#include <set>
namespace lumora::evidence {
namespace {
void check(bool ok,const char* why) {if(!ok) throw Error(3,why);}
QJsonObject object(const QJsonValue& v,const char* names) {
    check(v.isObject(),"Expected object");const auto o=v.toObject();const auto keys=QString::fromLatin1(names).split(' ',Qt::SkipEmptyParts);check(o.size()==keys.size(),"Missing or unknown object member");for(const auto& k:keys) check(o.contains(k),"Missing object member");return o;
}
QJsonArray array(const QJsonValue& v) {check(v.isArray(),"Expected array");return v.toArray();}
QString string(const QJsonValue& v,bool empty=false) {check(v.isString() && (empty || !v.toString().isEmpty()),"Expected string");return v.toString();}
bool boolean(const QJsonValue& v) {check(v.isBool(),"Expected boolean");return v.toBool();}
double number(const QJsonValue& v) {check(v.isDouble() && std::isfinite(v.toDouble()),"Expected finite number");return v.toDouble();}
std::uint64_t natural(const QJsonValue& v,bool positive=false) {const auto n=v.toInteger(-1);check(v.isDouble() && n>=0 && static_cast<double>(n)==v.toDouble() && (!positive || n>0),"Expected nonnegative/positive integer");return static_cast<std::uint64_t>(n);}
void oneOf(const QJsonValue& v,const char* values) {check(QString::fromLatin1(values).split(' ').contains(string(v)),"Unexpected enum value");}
void hash(const QJsonValue& v,int digits=64) {const auto s=string(v);check(s.size()==digits && QRegularExpression("^[0-9a-f]+$").match(s).hasMatch(),"Invalid lowercase hash/revision");}
void timestamp(const QJsonValue& v) {const auto s=string(v);check(s.endsWith('Z') && QDateTime::fromString(s,Qt::ISODateWithMs).isValid(),"Expected UTC timestamp");}
void strings(const QJsonValue& v) {for(const auto& s:array(v)) (void)string(s);}
void nullableString(const QJsonValue& v) {if(!v.isNull()) (void)string(v);}
void validateProvenance(const QJsonValue& value) {
    const auto p=object(value,"source build host dependencies"),s=object(p["source"],"revision dirty dirtyScope statusSha256 capturedUtc");
    if(!s["revision"].isNull()) hash(s["revision"],40);
    if(!s["dirty"].isNull()) (void)boolean(s["dirty"]);
    check(s["dirtyScope"]=="git-status-porcelain-v1-untracked-normal","Unknown dirty scope");if(!s["statusSha256"].isNull()) hash(s["statusSha256"]);timestamp(s["capturedUtc"]);
    const auto b=object(p["build"],"configuration compilerId compilerVersion cxxStandard compileOptions");nullableString(b["configuration"]);nullableString(b["compilerId"]);nullableString(b["compilerVersion"]);check(natural(b["cxxStandard"])==20,"C++20 required");
    const auto options=object(b["compileOptions"],"scope global configuration processing evidence sourceOptions");check(options["scope"]=="declared_cmake_configuration_and_target_options","Compiler options scope overclaim");for(const auto& key:{"global","configuration","processing","evidence","sourceOptions"}) (void)string(options[key],true);
    const auto h=object(p["host"],"osName osVersion architecture cpuModel logicalCores");for(const auto& key:{"osName","osVersion","architecture","cpuModel"}) nullableString(h[key]);if(!h["logicalCores"].isNull()) (void)natural(h["logicalCores"],true);
    const auto d=object(p["dependencies"],"opencvVersion opencvVcpkgPortVersion vcpkgBaseline qtVersion");for(const auto& v:d) nullableString(v);if(!d["vcpkgBaseline"].isNull()) hash(d["vcpkgBaseline"],40);
}
void validateExecution(const QJsonValue& value) {const auto e=object(value,"backend threadModel opencvThreads roundingMode algorithmImplementation algorithmProvenance");check(e["backend"]=="cpu" && e["roundingMode"]=="FE_TONEAREST","Unsupported execution");(void)natural(e["opencvThreads"]);for(const auto& key:{"threadModel","algorithmImplementation","algorithmProvenance"}) (void)string(e[key]);check(!e["algorithmImplementation"].toString().contains("cv::CLAHE::apply") && !e["algorithmImplementation"].toString().contains("cv::GaussianBlur"),"Prepared backend mislabeled");}
void validateResource(const QJsonValue& value,bool full,bool v2) {
    const auto r=object(value,full && v2?"scope limitScope limitBytes requiredBytes fixedBytes threeOwnerReserveBytes gammaCacheReserveBytes candidateRequiredBytes boundedStatistics exclusions cpuExecutionSlots cpuHelperThreads":"scope limitScope limitBytes requiredBytes fixedBytes threeOwnerReserveBytes gammaCacheReserveBytes candidateRequiredBytes boundedStatistics exclusions");check(r["scope"]==(full?"prepared_session":"standalone_stage"),"Resource scope mismatch");const auto limit=natural(r["limitBytes"],true),required=natural(r["requiredBytes"],true);strings(r["exclusions"]);
    if(full) {check(r["limitScope"]=="session_accounted_storage" && required<=limit,"Session exceeds budget");const auto fixed=natural(r["fixedBytes"]),reserve=natural(r["threeOwnerReserveBytes"]),gamma=natural(r["gammaCacheReserveBytes"]);(void)natural(r["candidateRequiredBytes"]);check(fixed<=required && reserve<=required-fixed && gamma==required-fixed-reserve,"Resource total inconsistency");const auto stats=object(r["boundedStatistics"],v2?"externalSessionBytes processingPoolBytes displayPoolBytes frameObjectBytes orientationBytes engineStateBytes activationEnvelopeBytes actualRetainedStageBytes cpuExecutorBytes":"externalSessionBytes processingPoolBytes displayPoolBytes frameObjectBytes orientationBytes engineStateBytes activationEnvelopeBytes actualRetainedStageBytes");for(const auto& v:stats) (void)natural(v);
        if(v2) {
            const auto executionSlots=natural(r["cpuExecutionSlots"],true);check(executionSlots<=4 && natural(r["cpuHelperThreads"])==executionSlots-1,"CPU slot/helper mismatch");
            check(natural(stats["cpuExecutorBytes"],true)>0,"Executor storage missing");
            std::uint64_t total=0;
            for(const auto* key:{"externalSessionBytes","processingPoolBytes","displayPoolBytes","frameObjectBytes","orientationBytes","engineStateBytes","cpuExecutorBytes"}) {
                const auto bytes=natural(stats[key]);check(bytes<=fixed-total,"Fixed component exceeds total");total+=bytes;
            }
            check(total==fixed,"Fixed executor accounting mismatch");
            const auto envelope=natural(stats["activationEnvelopeBytes"]);
            check(reserve%3==0 && envelope==reserve/3 && natural(r["candidateRequiredBytes"])<=envelope,"Activation reserve/envelope mismatch");
            check(array(r["exclusions"]).contains("thread_stacks_TLS_thread_library_and_OS_bookkeeping"),"Thread runtime storage exclusion missing");
        }}
    else {check(r["limitScope"]=="standalone_scratch_only","Standalone budget scope mismatch");for(const auto& key:{"fixedBytes","threeOwnerReserveBytes","gammaCacheReserveBytes","candidateRequiredBytes"}) check(r[key].isNull(),"Standalone engine-only field must be null");const auto stats=object(r["boundedStatistics"],"scratchBytes stageOwnerBytes separateHarnessImageBytes");const auto scratch=natural(stats["scratchBytes"]),fixed=natural(stats["stageOwnerBytes"]);(void)natural(stats["separateHarnessImageBytes"]);check(scratch<=limit && scratch<=required && fixed==required-scratch,"Standalone accounting mismatch");}
}
void validateAllocation(const QJsonValue& value,bool cycles=false,bool positive=false) {
    const auto a=object(value,cycles?"scope calls bytes deallocations armedRegion coveredRoutes unsupportedRoutes cycles":"scope calls bytes deallocations armedRegion coveredRoutes unsupportedRoutes");check(a["scope"]=="cxx_replacement_new","Wrong allocation scope");(void)string(a["armedRegion"]);
    check(array(a["coveredRoutes"])==QJsonArray({"ordinary","array","aligned","aligned_array","nothrow","nothrow_array","aligned_nothrow","aligned_nothrow_array"}),"Allocation routes mismatch");check(array(a["unsupportedRoutes"])==QJsonArray({"c_malloc_free","external_dll_private_heaps"}),"Allocation exclusions mismatch");
    const auto calls=natural(a["calls"]),bytes=natural(a["bytes"]),frees=natural(a["deallocations"]);if(positive) check(calls==8 && bytes==150 && frees==8,"Positive control mismatch");else check(calls==0 && bytes==0 && frees==0,"Prepared region contains C++ heap activity");if(cycles) (void)natural(a["cycles"],true);
}
void validateInput(const QJsonValue& value,SourceFormat sourceFormat) {const auto i=object(value,sourceFormat==SourceFormat::Mono12?"patternId version seed sha256 derivation":"patternId version seed sha256");check(i["patternId"]=="xorshift32_u16_v1" && natural(i["version"])==1 && natural(i["seed"])==0x6D2B79F5U,"Input pattern mismatch");if(sourceFormat==SourceFormat::Mono12) check(i["derivation"]=="uint16(state >> 16) >> 4","Mono12 input derivation mismatch");hash(i["sha256"]);}
void validateFailure(const QJsonValue& value,std::size_t rows,bool complete) {if(value.isNull()) return;check(!complete,"Complete run cannot have failure");const auto f=object(value,"code message completedRows");(void)string(f["code"]);(void)string(f["message"]);check(natural(f["completedRows"])==rows,"Failure progress mismatch");}
processing::PipelineDefinition diagnosticPipeline(const std::string& id) {auto p=processing::standardPipeline();for(auto& s:p.stages) s.enabled=s.id==processing::StageId::Normalize || stageId(s.id)==qs(id) || (s.id==processing::StageId::Denoise && id.starts_with("denoise_"));if(id=="denoise_median") std::get<processing::DenoiseParameters>(p.stages[5].parameters).mode=processing::DenoiseMode::Median;return p;}
void validateBenchmark(const QJsonObject& root,std::uint64_t version) {
    const bool mono12=version==3;const auto sourceFormat=mono12?SourceFormat::Mono12:SourceFormat::Mono16;
    (void)object(root,mono12?"schemaVersion artifactType runStatus complete workload generatedUtc rows failure sourceFormat":"schemaVersion artifactType runStatus complete workload generatedUtc rows failure");if(mono12) check(root["sourceFormat"]=="mono12","Mono12 source format discriminator mismatch");const bool complete=boolean(root["complete"]);check(root["runStatus"]==(complete?"complete":"incomplete"),"Run status mismatch");timestamp(root["generatedUtc"]);
    const auto w=object(root["workload"],"kind requestedSizes warmUpFrames measuredFrames");oneOf(w["kind"],"standard smoke custom");const auto sizes=array(w["requestedSizes"]);check(!sizes.empty(),"Workload sizes empty");std::uint64_t previous=0;for(const auto& size:sizes) {const auto n=natural(size,true);check(n>=8 && n>previous && n<=INT32_MAX,"Invalid requested sizes");previous=n;}
    const auto warm=natural(w["warmUpFrames"],true),measured=natural(w["measuredFrames"],true);const bool smoke=w["kind"]=="smoke";
    if(smoke) check(sizes==QJsonArray({64,128}) && warm==2 && measured==5,"Smoke workload changed");
    if(w["kind"]=="standard") check(sizes==QJsonArray({512,1024,2048}) && warm==100 && measured==500,"Standard workload changed");
    const auto& ids=measurementRowIds(sourceFormat);const auto rows=array(root["rows"]);check(rows.size()<=sizes.size()*static_cast<qsizetype>(ids.size()) && (!complete || rows.size()==sizes.size()*static_cast<qsizetype>(ids.size())),"Incomplete or excessive row product");std::size_t index=0;
    for(const auto& value:rows) {
        const auto r=object(value,"rowId scope size smoke pipelineDefinition input sourceDescriptor orientation orientedDimensions provenance execution resourcePlan warmUpFrames measuredFrames timing allocation workingSet checksum processingErrors drops complete");
        const auto id=ids[index%ids.size()];const auto size=static_cast<std::uint32_t>(natural(sizes[static_cast<qsizetype>(index/ids.size())]));++index;const bool full=id.starts_with("full_standard_");
        check(r["rowId"]==qs(id) && natural(r["size"])==size && boolean(r["smoke"])==smoke && r["scope"]==(full?"full_frame":"standalone_stage"),"Row ordering/scope mismatch");
        check(r["pipelineDefinition"]==pipelineJson(full?processing::standardPipeline():diagnosticPipeline(id)),"Row pipeline mismatch");validateInput(r["input"],sourceFormat);
        check(r["sourceDescriptor"]==descriptorJson(monoFormat(mono12),layoutFor(size,size)),"Source descriptor mismatch");const core::Orientation orientation{id=="full_standard_nonidentity",false,id=="full_standard_nonidentity"?core::Rotation::Degrees90:core::Rotation::Degrees0};check(r["orientation"]==orientationJson(orientation) && r["orientedDimensions"]==dimensions(size,size),"Orientation mismatch");
        validateProvenance(r["provenance"]);validateExecution(r["execution"]);validateResource(r["resourcePlan"],full,version>=2);check(natural(r["warmUpFrames"])==warm && natural(r["measuredFrames"])==measured,"Row count mismatch");
        const auto t=object(r["timing"],"unit sampleCount median p95NearestRank wallElapsed fps");check(t["unit"]=="nanoseconds" && natural(t["sampleCount"])==measured,"Timing count/unit mismatch");const auto median=number(t["median"]);const auto p95=natural(t["p95NearestRank"],true),wall=natural(t["wallElapsed"],true);const auto fps=number(t["fps"]);check(median>0 && std::floor(median*2)==median*2 && median<=static_cast<double>(p95) && static_cast<double>(p95)<=static_cast<double>(wall) && fps>0 && std::abs(fps-static_cast<double>(measured)*1e9/static_cast<double>(wall))<=fps*1e-12,"Timing statistic inconsistency");
        validateAllocation(r["allocation"]);const auto ws=object(r["workingSet"],"metric source beforeBytes afterBytes");if(ws["metric"]=="unavailable") check(ws["source"]=="unavailable" && ws["beforeBytes"].isNull() && ws["afterBytes"].isNull(),"Unavailable working-set fields mismatch");else {check((ws["metric"]=="linux_resident_bytes" && ws["source"]=="proc_self_statm") || (ws["metric"]=="windows_working_set_bytes" && ws["source"]=="GetProcessMemoryInfo"),"Working set metric/source mismatch");(void)natural(ws["beforeBytes"],true);(void)natural(ws["afterBytes"],true);}
        const auto c=object(r["checksum"],"algorithm value provenance verificationCycles measuredFingerprint fingerprintMethod");check(c["algorithm"]=="sha256" && natural(c["verificationCycles"])==(full?1U:0U),"Checksum verification scope mismatch");hash(c["value"]);(void)string(c["provenance"]);(void)string(c["fingerprintMethod"]);if(full) check(QRegularExpression("^[0-9a-f]{1,16}$").match(string(c["measuredFingerprint"])).hasMatch(),"Invalid measured fingerprint");else check(c["measuredFingerprint"].isNull(),"Standalone fingerprint must be null");
        check(natural(r["processingErrors"])==0 && natural(r["drops"])==0 && boolean(r["complete"]),"Failed row cannot be complete");
    }
    validateFailure(root["failure"],static_cast<std::size_t>(rows.size()),complete);
}
void validateTraceCounts(const QJsonValue& value) {const auto c=object(value,"events successfulAllocationResults nullAllocationResults releases reallocOldTransitions reallocNewResults reallocFailures traceReportedSuccessfulBytes");for(const auto& v:c) (void)natural(v);std::uint64_t total=0;for(const auto& key:{"successfulAllocationResults","nullAllocationResults","releases","reallocOldTransitions","reallocNewResults","reallocFailures"}) {const auto n=natural(c[key]);check(n<=UINT64_MAX-total,"Trace event overflow");total+=n;}check(natural(c["events"])==total && c["reallocOldTransitions"]==c["reallocNewResults"],"Trace counts inconsistent");}
void validateHeap(const QJsonValue& value,std::uint64_t cycles,bool smoke) {
    if(value.isNull()) return;
    const auto h=object(value,"status proof cycles tracePath traceSha256 capability events scope exclusions");oneOf(h["status"],"complete failed");check(natural(h["cycles"])==cycles,"Trace cycles mismatch");(void)string(h["tracePath"]);hash(h["traceSha256"]);(void)string(h["scope"]);strings(h["exclusions"]);validateTraceCounts(h["events"]);const bool zero=natural(h["events"].toObject()["events"])==0;check((h["status"]=="complete")==zero && boolean(h["proof"])==(!smoke && cycles>=1000 && zero),"Trace proof mismatch");
    const auto c=object(h["capability"],"status markerProvider endMarkerProvider mallocProvider glibcVersion architecture controlStatus emptyControlStatus controlTraceSha256 controlTracePath controlEvents emptyTraceSha256 emptyTracePath");check(c["status"]=="verified" && c["controlStatus"]=="passed" && c["emptyControlStatus"]=="passed","Heap controls unverified");for(const auto& key:{"markerProvider","endMarkerProvider","mallocProvider"}) {const auto p=object(c[key],"path sha256");(void)string(p["path"]);hash(p["sha256"]);}check(c["markerProvider"]==c["mallocProvider"] && c["endMarkerProvider"]==c["mallocProvider"],"Provider mismatch");for(const auto& key:{"glibcVersion","architecture","controlTracePath","emptyTracePath"}) (void)string(c[key]);hash(c["controlTraceSha256"]);hash(c["emptyTraceSha256"]);validateTraceCounts(c["controlEvents"]);check(natural(c["controlEvents"].toObject()["events"])>0,"Empty positive control");
}
void validateHelperControl(const QJsonValue& value,std::uint64_t executionSlots,bool traced) {
    const auto h=object(value,"scope expectedHelperMask observedHelperMask callbackInvocations cxxAllocation glibcTrace");
    check(h["scope"]=="prepared_cpu_executor_persistent_helpers","Wrong helper scope");
    const auto helpers=executionSlots-1,mask=(std::uint64_t{1}<<executionSlots)-2,bytes=helpers*256+helpers*(helpers+1)/2;
    check(natural(h["expectedHelperMask"])==mask && natural(h["observedHelperMask"])==mask && natural(h["callbackInvocations"])==helpers,"Helper slot participation mismatch");
    const auto a=object(h["cxxAllocation"],"scope calls bytes deallocations armedRegion coveredRoutes unsupportedRoutes");
    check(a["scope"]=="cxx_replacement_new" && natural(a["calls"])==helpers && natural(a["bytes"])==bytes && natural(a["deallocations"])==helpers,"Helper allocation control mismatch");
    check(a["armedRegion"]=="caller reset/armed before one synchronous helper control dispatch and ended after return","Helper tracking boundary mismatch");
    check(array(a["coveredRoutes"])==QJsonArray{"ordinary"} && array(a["unsupportedRoutes"])==QJsonArray({"c_malloc_free","external_dll_private_heaps"}),"Helper allocation routes mismatch");
    if(h["glibcTrace"].isNull()) {check(!traced,"Missing helper glibc control");return;}
    const auto trace=object(h["glibcTrace"],"status tracePath traceSha256 events");
    check(trace["status"]=="passed","Helper trace needs positive-control polarity");(void)string(trace["tracePath"]);hash(trace["traceSha256"]);validateTraceCounts(trace["events"]);
    const auto c=trace["events"].toObject();
    check(natural(c["events"])==2*helpers && natural(c["successfulAllocationResults"])==helpers && natural(c["releases"])==helpers && natural(c["traceReportedSuccessfulBytes"])==bytes,"Helper trace counts mismatch");
    for(const auto* key:{"nullAllocationResults","reallocOldTransitions","reallocNewResults","reallocFailures"}) check(natural(c[key])==0,"Helper trace contains failure/reallocation");
}
void validateAllocationRoot(const QJsonObject& root,std::uint64_t version) {
    const bool mono12=version==3;const auto sourceFormat=mono12?SourceFormat::Mono12:SourceFormat::Mono16;
    (void)object(root,mono12?"schemaVersion artifactType runStatus complete smoke generatedUtc rows failure sourceFormat":"schemaVersion artifactType runStatus complete smoke generatedUtc rows failure");if(mono12) check(root["sourceFormat"]=="mono12","Mono12 source format discriminator mismatch");const bool complete=boolean(root["complete"]),smoke=boolean(root["smoke"]);check(root["runStatus"]==(complete?"complete":"incomplete"),"Allocation run status mismatch");timestamp(root["generatedUtc"]);const auto rows=array(root["rows"]);check(rows.size()<=2 && (!complete || rows.size()==2),"Both allocation orientations required");int index=0;
    for(const auto& value:rows) {
        const auto r=object(value,version>=2?"rowId pipelineDefinition input sourceDescriptor orientation orientedDimensions provenance execution resourcePlan warmUpFrames measuredCycles positiveControl measuredRegion heapTrace processingErrors drops complete helperControl":"rowId pipelineDefinition input sourceDescriptor orientation orientedDimensions provenance execution resourcePlan warmUpFrames measuredCycles positiveControl measuredRegion heapTrace processingErrors drops complete");const bool oriented=index++!=0;check(r["rowId"]==(oriented?"full_standard_nonidentity":"full_standard_identity"),"Allocation row ordering mismatch");check(r["pipelineDefinition"]==pipelineJson(processing::standardPipeline()),"Allocation must execute shared Standard");validateInput(r["input"],sourceFormat);check(r["sourceDescriptor"]==descriptorJson(monoFormat(mono12),layoutFor(64,48)),"Allocation source must be64x48");check(r["orientation"]==orientationJson({oriented,false,oriented?core::Rotation::Degrees90:core::Rotation::Degrees0}) && r["orientedDimensions"]==dimensions(oriented?48:64,oriented?64:48),"Allocation orientation mismatch");validateProvenance(r["provenance"]);validateExecution(r["execution"]);validateResource(r["resourcePlan"],true,version>=2);if(version>=2) validateHelperControl(r["helperControl"],natural(r["resourcePlan"].toObject()["cpuExecutionSlots"]),!r["heapTrace"].isNull());const auto cycles=natural(r["measuredCycles"]);check(natural(r["warmUpFrames"])==(smoke?2U:100U) && cycles==(smoke?20U:1000U),"Allocation workload count mismatch");validateAllocation(r["positiveControl"],true,true);validateAllocation(r["measuredRegion"],true);check(natural(r["positiveControl"].toObject()["cycles"])==1 && natural(r["measuredRegion"].toObject()["cycles"])==cycles,"Allocation region cycles mismatch");validateHeap(r["heapTrace"],cycles,smoke);if(complete && !r["heapTrace"].isNull()) check(r["heapTrace"].toObject()["status"]=="complete","Complete allocation has failed trace");check(natural(r["processingErrors"])==0 && natural(r["drops"])==0 && boolean(r["complete"]),"Allocation processing failed");
    }
    validateFailure(root["failure"],static_cast<std::size_t>(rows.size()),complete);
}
void validateMetrics(const QJsonValue& value,bool nullable) {const auto m=object(value,"maxAbsoluteU16Error changedPixelFraction meanAbsoluteError");if(nullable && m["maxAbsoluteU16Error"].isNull()) {for(const auto& v:m) check(v.isNull(),"Partially null comparison");return;}check(natural(m["maxAbsoluteU16Error"])<=65535,"U16 error outside range");check(number(m["changedPixelFraction"])>=0 && number(m["changedPixelFraction"])<=1,"Changed fraction outside range");check(number(m["meanAbsoluteError"])>=0 && number(m["meanAbsoluteError"])<=65535,"Mean error outside range");}
void validatePayload(const QJsonValue& value,const std::filesystem::path& base,std::uint32_t width,std::uint32_t height,bool gray8) {
    const auto p=object(value,"file format width height maxValue byteOrder payloadEncoding sha256");const auto file=string(p["file"]);check(!file.contains('/') && !file.contains('\\') && file!="." && file!="..","Reference filename must be a local basename");check(p["format"]=="pgm_p5_u16" && p["byteOrder"]=="big_endian" && natural(p["maxValue"])==65535 && natural(p["width"])==width && natural(p["height"])==height,"PGM descriptor mismatch");check(p["payloadEncoding"]==(gray8?"gray8_zero_extended_to_u16_big_endian":"u16_big_endian"),"PGM encoding mismatch");hash(p["sha256"]);
    const auto path=base/std::filesystem::path(file.toStdU16String());check(!std::filesystem::is_symlink(path),"Reference payload symlinks are not accepted");const auto bytes=readFile(path);const auto im=decodePgm(bytes);check(im.width==width && im.height==height && qs(sha256(bytes))==p["sha256"],"Reference bytes/hash/dimensions mismatch");if(gray8) for(auto v:im.pixels) check(v<=255,"Gray8 payload must be zero-extended without scaling");
}
void validateReference(const QJsonObject& root,const std::filesystem::path& base,bool candidate) {
    (void)object(root,candidate
        ? "schemaVersion artifactType status workloadKind complete generatedUtc provenance pipelineDefinition cases failure"
        : "schemaVersion artifactType status sizeProfile complete updatedUtc pipelineDefinition cases");
    const bool complete=boolean(root["complete"]);
    bool smoke=false;
    if(candidate) {
        check(root["status"]=="candidate","Generator cannot approve candidates");
        oneOf(root["workloadKind"],"candidate smoke");
        smoke=root["workloadKind"]=="smoke";
        timestamp(root["generatedUtc"]);
        validateProvenance(root["provenance"]);
    } else {
        oneOf(root["status"],"pending reviewed");
        oneOf(root["sizeProfile"],"ordinary smoke");
        smoke=root["sizeProfile"]=="smoke";
        timestamp(root["updatedUtc"]);
    }
    check(root["pipelineDefinition"]==pipelineJson(processing::standardPipeline()),"Reference shared Standard mismatch");
    const auto rows=array(root["cases"]);
    const auto cases=referenceCases(smoke);
    std::set<QString> seen;
    check(rows.size()<=13 && (!candidate || !complete || rows.size()==13),"Reference case count mismatch");
    for(const auto& value:rows) {
        const auto row=object(value,candidate
            ? "caseId classification pattern stage sourceDescriptor orientation orientedDimensions source candidate comparison acceptance"
            : "caseId classification pattern stage sourceDescriptor orientation orientedDimensions source expected acceptance");
        const auto id=string(row["caseId"]);
        check(seen.insert(id).second,"Duplicate reference case");
        const auto found=std::find_if(cases.begin(),cases.end(),[&](const auto& c){return qs(c.id)==id;});
        check(found!=cases.end(),"Unknown reference case");
        const auto& c=*found;
        const auto metadata=caseMetadata(c);
        for(auto it=metadata.begin();it!=metadata.end();++it) {
            check(row[it.key()]==it.value(),"Reference operation/pattern/context mismatch");
        }
        validatePayload(row["source"],base,c.pattern.width,c.pattern.height,false);
        const auto dims=row["orientedDimensions"].toObject();
        validatePayload(row[candidate?"candidate":"expected"],base,
            static_cast<std::uint32_t>(natural(dims["width"])),
            static_cast<std::uint32_t>(natural(dims["height"])),c.gray8);
        const auto acceptance=object(row["acceptance"],"status thresholds review");
        if(candidate) {
            check(acceptance["status"]==(c.exact?"candidate":"pending_review")
                && acceptance["thresholds"].isNull() && acceptance["review"].isNull(),"Candidate approval forbidden");
            validateMetrics(row["comparison"],true);
        } else if(c.exact) {
            check(acceptance["status"]=="exact" && acceptance["review"].isNull(),"Independent exact acceptance mismatch");
            validateMetrics(acceptance["thresholds"],false);
            for(const auto& metric:acceptance["thresholds"].toObject()) check(number(metric)==0,"Exact threshold must be zero");
        } else if(acceptance["status"]=="pending_review") {
            check(root["status"]=="pending","Reviewed manifest cannot contain unreviewed backend cases");
            check(acceptance["thresholds"].isNull() && acceptance["review"].isNull(),"Unreviewed threshold must be null");
        } else {
            check(root["status"]=="reviewed" && acceptance["status"]=="reviewed","Backend acceptance requires review");
            validateMetrics(acceptance["thresholds"],false);
            const auto review=object(acceptance["review"],"reviewedBy reviewedUtc sourceArtifactSha256");
            (void)string(review["reviewedBy"]);
            timestamp(review["reviewedUtc"]);
            hash(review["sourceArtifactSha256"]);
        }
    }
    if(candidate) {
        validateFailure(root["failure"],static_cast<std::size_t>(rows.size()),complete);
    } else if(root["status"]=="reviewed") {
        check(complete && rows.size()==13 && !smoke,"Reviewed manifest needs all ordinary cases");
    }
}
void validateReferenceApprovals(const QJsonObject& approval,const QJsonObject& manifest) {
    // The manifest and approval shapes/case sets have already been validated.
    // Bind by case ID: array ordering must not substitute for case identity.
    for(const auto& value:manifest["cases"].toArray()) {
        const auto row=value.toObject();
        if(row["classification"]!="provisional_backend") continue;
        const auto acceptance=row["acceptance"].toObject();
        check(acceptance["status"]=="reviewed","Accepted backend reference needs review");
        for(const auto* key:{"approvedThresholds","approvedReferenceHashes"}) {
            const auto entries=approval[key].toArray();
            const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& entry) {
                return entry.toObject()["caseId"]==row["caseId"];
            });
            check(found!=entries.end(),"Accepted backend case missing from approvals");
            auto entry=found->toObject();
            if(std::string_view(key)=="approvedThresholds") {
                entry.remove("caseId");
                check(entry==acceptance["thresholds"].toObject(),"Approved thresholds differ from reviewed manifest");
            } else {
                check(entry["sourceSha256"]==row["source"].toObject()["sha256"]
                    && entry["expectedSha256"]==row["expected"].toObject()["sha256"],
                    "Approved reference hashes differ from reviewed manifest");
            }
        }
    }
}
void validateWorkstation(const QJsonObject& root,const std::filesystem::path& base) {
    (void)object(root,"schemaVersion artifactType status designation machine cpu gpu ram os compiler dependencies drivers power threading build requirements acceptance");oneOf(root["status"],"pending accepted");const bool accepted=root["status"]=="accepted";
    const auto designation=object(root["designation"],"machineId selectedBy selectedUtc");for(const auto& v:designation) {nullableString(v);if(accepted) check(!v.isNull(),"Accepted workstation requires designation");}if(!designation["selectedUtc"].isNull()) timestamp(designation["selectedUtc"]);
    const std::vector<std::pair<const char*,const char*>> facts={{"machine","manufacturer model firmware"},{"cpu","manufacturer model architecture physicalCores logicalCores"},{"gpu","manufacturer model dedicatedMemoryBytes"},{"ram","installedBytes speedMtPerSecond"},{"os","name edition version build"},{"compiler","id version"},{"dependencies","opencvVersion opencvVcpkgPortVersion vcpkgBaseline qtVersion"},{"drivers","gpu chipset"},{"power","plan acPower thermalCondition"},{"threading","threadModel opencvThreads logicalProcessorAffinity"},{"build","configuration compileOptions sourceRevision sourceDirty artifactSha256"}};
    const std::set<QString> integers={"physicalCores","logicalCores","dedicatedMemoryBytes","installedBytes","speedMtPerSecond","opencvThreads"};
    // Peripheral manufacturer/firmware, speed, memory, chipset and thermal details
    // remain nullable when unavailable; acceptance does not invent hardware facts.
    const std::set<QString> peripheral={"manufacturer","firmware","dedicatedMemoryBytes","speedMtPerSecond","chipset","thermalCondition","logicalProcessorAffinity"};
    for(const auto& [name,keys]:facts) {
        if(root[name].isNull()) {check(!accepted,"Accepted workstation missing facts");continue;}
        const auto o=object(root[name],keys);for(auto it=o.begin();it!=o.end();++it) {if(it.value().isNull()) {check(!accepted || peripheral.contains(it.key()),"Accepted workstation missing required fact");continue;}if(integers.contains(it.key())) (void)natural(it.value());else if(it.key()=="acPower" || it.key()=="sourceDirty") (void)boolean(it.value());else (void)string(it.value());}
    }
    const auto requirements=object(root["requirements"],"standard2048P95MaxMilliseconds sustainedFpsMinimum requiredEvidence");check(number(requirements["standard2048P95MaxMilliseconds"])==33.3 && number(requirements["sustainedFpsMinimum"])==30 && requirements["requiredEvidence"]==QJsonArray({"full_standard_benchmark","allocation_proof","reviewed_reference_manifest","freshness_60fps"}),"Acceptance requirements changed");
    const auto a=object(root["acceptance"],"benchmarkArtifactSha256 allocationArtifactSha256 referenceManifestSha256 freshnessArtifactSha256 approvedThresholds approvedReferenceHashes reviewedBy reviewedUtc");
    if(!accepted) {for(const auto& v:a) check(v.isNull(),"Pending record cannot carry acceptance claims");return;}
    check(root["os"].toObject()["name"].toString().compare("Windows",Qt::CaseInsensitive)==0,"Designated native Windows record required");check(root["build"].toObject()["configuration"]=="Release","Acceptance requires Release");hash(root["build"].toObject()["sourceRevision"],40);hash(root["build"].toObject()["artifactSha256"]);check(!boolean(root["build"].toObject()["sourceDirty"]),"Acceptance source must be clean");
    for(const auto& key:{"benchmarkArtifactSha256","allocationArtifactSha256","referenceManifestSha256","freshnessArtifactSha256"}) hash(a[key]);
    (void)string(a["reviewedBy"]);timestamp(a["reviewedUtc"]);
    const auto cases=referenceCases(false);std::set<QString> expected;for(const auto& c:cases) if(!c.exact) expected.insert(qs(c.id));
    for(bool thresholds:{false,true}) {
        std::set<QString> seen;
        for(const auto& value:array(a[thresholds?"approvedThresholds":"approvedReferenceHashes"])) {
            const auto entry=object(value,thresholds
                ? "caseId maxAbsoluteU16Error changedPixelFraction meanAbsoluteError"
                : "caseId sourceSha256 expectedSha256");
            check(seen.insert(string(entry["caseId"])).second,"Duplicate accepted case");
            if(thresholds) {
                auto metrics=entry;
                metrics.remove("caseId");
                validateMetrics(metrics,false);
            } else {
                hash(entry["sourceSha256"]);
                hash(entry["expectedSha256"]);
            }
        }
        check(seen==expected,"Accepted backend case set incomplete");
    }
    // Structural provenance validation only. Human designation/review is still
    // required; no executable writes accepted records. Attached artifacts have
    // fixed review-bundle filenames, documented beside the pending template.
    const std::vector<std::pair<const char*,const char*>> attachments={{"benchmarkArtifactSha256","benchmark.json"},{"allocationArtifactSha256","allocation.json"},{"referenceManifestSha256","manifest.json"},{"freshnessArtifactSha256","freshness.json"}};
    for(const auto& [key,file]:attachments) {
        const auto bytes=readFile(base/file);
        check(qs(sha256(bytes))==a[key],"Accepted attachment hash mismatch");
        if(std::string_view(file)!="freshness.json") validateArtifact(bytes,base);
    }
    const auto references=strictObject(readFile(base/"manifest.json"));
    check(references["status"]=="reviewed","Accepted references need review");
    validateReferenceApprovals(a,references);

    const auto benchmark=strictObject(readFile(base/"benchmark.json"));
    check(natural(benchmark["schemaVersion"])<=2,"Canonical Mono16 workstation review requires benchmark schemaVersion1 or2");
    check(benchmark["complete"]==true && benchmark["workload"].toObject()["kind"]=="standard", "Accepted benchmark must be complete Standard");
    bool gate=false;
    for(const auto& value:benchmark["rows"].toArray()) {
        const auto row=value.toObject();
        if(row["size"]!=2048 || row["rowId"]!="full_standard_identity") continue;
        const auto timing=row["timing"].toObject();
        gate=number(timing["p95NearestRank"])<=33300000 && number(timing["fps"])>=30;
        const auto rowProvenance=row["provenance"].toObject();
        for(const auto& section:{"source","host","dependencies"}) {
            for(const auto& fact:rowProvenance[section].toObject()) check(!fact.isNull(),"Accepted benchmark provenance is incomplete");
        }
        check(rowProvenance["host"].toObject()["osName"].toString().compare("windows",Qt::CaseInsensitive)==0,
            "Linux/hosted timing is not designated native Windows evidence");
        check(rowProvenance["build"].toObject()["configuration"]=="Release","Accepted row requires Release");
        const auto source=rowProvenance["source"].toObject();
        check(source["revision"]==root["build"].toObject()["sourceRevision"] && source["dirty"]==false,
            "Acceptance source provenance mismatch");
    }
    check(gate,"Accepted benchmark does not satisfy2048 gates");
    const auto allocation=strictObject(readFile(base/"allocation.json"));
    check(natural(allocation["schemaVersion"])<=2,"Canonical Mono16 workstation review requires allocation schemaVersion1 or2");
    check(allocation["complete"]==true && allocation["smoke"]==false,"Accepted allocation must be normal complete proof");
}
} // namespace
void validateArtifact(const std::string& bytes,const std::filesystem::path& base) {
    const auto root=strictObject(bytes);const auto version=natural(root["schemaVersion"]);const auto type=string(root["artifactType"]);
    const bool versioned=type=="lumora.processing.benchmark" || type=="lumora.processing.allocation-proof";
    check(version==1 || (versioned && (version==2 || version==3)),"Unsupported artifact schemaVersion");
    if(type=="lumora.processing.benchmark") validateBenchmark(root,version);
    else if(type=="lumora.processing.allocation-proof") validateAllocationRoot(root,version);
    else if(type=="lumora.processing.reference-candidate") validateReference(root,base,true);
    else if(type=="lumora.processing.reference-manifest") validateReference(root,base,false);
    else if(type=="lumora.processing.reference-workstation") validateWorkstation(root,base);
    else throw Error(3,"Unknown owned artifact type");
}
}
