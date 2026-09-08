#include "EvidenceHeap.hpp"
#include "EvidenceMeasurement.hpp"
#include <cstdlib>
#include <cstring>
#if defined(LUMORA_GLIBC_MTRACE)
#include <mcheck.h>
#include <dlfcn.h>
#include <gnu/libc-version.h>
#include <QSysInfo>
extern "C" bool lumora_heap_shared_control();
#endif
namespace lumora::evidence {
namespace {
QJsonObject traceCounts(const TraceCounts& c) {return {{"events",integer(c.events)},{"successfulAllocationResults",integer(c.allocations)},{"nullAllocationResults",integer(c.allocationFailures)},{"releases",integer(c.releases)},{"reallocOldTransitions",integer(c.reallocOld)},{"reallocNewResults",integer(c.reallocNew)},{"reallocFailures",integer(c.reallocFailures)},{"traceReportedSuccessfulBytes",integer(c.reportedBytes)}};}
#if defined(LUMORA_GLIBC_MTRACE)
QJsonObject provider(void* address) {Dl_info info{};if(!dladdr(address,&info) || !info.dli_fname) throw Error(5,"Cannot resolve allocator provider");const auto path=std::filesystem::canonical(info.dli_fname);return {{"path",qs(path.string())},{"sha256",qs(sha256(readFile(path)))}};}
bool cControls() {
    void* (*volatile allocate)(std::size_t)=std::malloc;void* (*volatile zero)(std::size_t,std::size_t)=std::calloc;void* (*volatile resize)(void*,std::size_t)=std::realloc;void (*volatile release)(void*)=std::free;
    auto* p=static_cast<unsigned char*>(allocate(11));if(!p) return false;std::memset(p,0x5a,11);auto* q=static_cast<unsigned char*>(resize(p,29));if(!q) {release(p);return false;}bool valid=true;for(int i=0;i<11;++i) valid=valid && q[i]==0x5a;release(q);
    auto* z=static_cast<unsigned char*>(zero(3,7));if(!z) return false;for(int i=0;i<21;++i) valid=valid && z[i]==0;release(z);
    void* aligned=nullptr;const int result=posix_memalign(&aligned,64,19);valid=valid && result==0 && reinterpret_cast<std::uintptr_t>(aligned)%64==0;release(aligned);
    return valid && lumora_heap_shared_control();
}
#endif
}
HeapTrace::HeapTrace(const std::filesystem::path& directory):directory_(std::filesystem::absolute(directory)) {
#if defined(LUMORA_GLIBC_MTRACE)
    if(std::filesystem::exists(directory_) && (!std::filesystem::is_directory(directory_) || !std::filesystem::is_empty(directory_))) throw Error(5,"Heap trace output must be a new or empty directory");
    std::filesystem::create_directories(directory_);
    const auto marker=provider(reinterpret_cast<void*>(&mtrace)),endMarker=provider(reinterpret_cast<void*>(&muntrace)),allocator=provider(reinterpret_cast<void*>(&malloc));
    if(marker["path"]!=allocator["path"] || marker["path"]!=endMarker["path"] || !marker["path"].toString().contains("libc_malloc_debug")) throw Error(5,"Requested glibc tracing unavailable: markers/malloc must bind to preloaded libc_malloc_debug");
    capability_={{"status","verified"},{"markerProvider",marker},{"endMarkerProvider",endMarker},{"mallocProvider",allocator},{"glibcVersion",gnu_get_libc_version()},{"architecture",QSysInfo::currentCpuArchitecture()},{"controlStatus","pending"},{"emptyControlStatus","pending"}};
    start("positive-control");bool valid=false;try {valid=cControls();(void)allocationControls();stop();}catch(...) {stop();throw;}
    const auto control=parseTrace(readFile(active_));if(!valid || control.allocations<12 || control.releases<12 || !control.reallocOld || !control.reallocNew || !control.reportedBytes) throw Error(5,"C/C++/shared-library heap positive controls failed");
    capability_["controlStatus"]="passed";capability_["controlTraceSha256"]=qs(sha256(readFile(active_)));capability_["controlTracePath"]=qs(active_.string());capability_["controlEvents"]=traceCounts(control);
    start("empty-control");stop();const auto empty=parseTrace(readFile(active_));if(empty.events) throw Error(5,"Empty glibc trace control had events");capability_["emptyControlStatus"]="passed";capability_["emptyTraceSha256"]=qs(sha256(readFile(active_)));capability_["emptyTracePath"]=qs(active_.string());
#else
    throw Error(5,"Requested glibc heap tracing is unavailable on this build/platform");
#endif
}
HeapTrace::~HeapTrace() {stop();}
void HeapTrace::start(std::string_view row) {
#if defined(LUMORA_GLIBC_MTRACE)
    if(armed_) throw Error(5,"Heap trace already armed");
    active_=directory_/(std::string(row)+".trace");if(std::filesystem::exists(active_)) throw Error(5,"Refusing to overwrite heap trace");
    if(setenv("MALLOC_TRACE",active_.c_str(),1)!=0) throw Error(5,"Cannot select heap trace path");
    mtrace();armed_=true;
#else
    (void)row;throw Error(5,"Heap tracing unavailable");
#endif
}
void HeapTrace::stop() noexcept {
#if defined(LUMORA_GLIBC_MTRACE)
    if(armed_) {muntrace();armed_=false;}
#endif
}
QJsonObject HeapTrace::helperControlResult(std::size_t executionSlots) {
    if(armed_ || executionSlots<1 || executionSlots>4) throw Error(5,"Cannot parse an armed/invalid helper control trace");
    const auto bytes=readFile(active_);const auto c=parseTrace(bytes);const auto helpers=executionSlots-1;
    const bool passed=c.events==helpers*2 && c.allocations==helpers && c.releases==helpers && c.reportedBytes==helpers*256+helpers*(helpers+1)/2 && !c.allocationFailures && !c.reallocOld && !c.reallocNew && !c.reallocFailures;
    if(!passed) throw Error(5,"Persistent helper glibc positive control failed");
    // The callback mask supplies slot attribution; mtrace itself has no thread IDs.
    return {{"status","passed"},{"tracePath",qs(active_.string())},{"traceSha256",qs(sha256(bytes))},{"events",traceCounts(c)}};
}
QJsonObject HeapTrace::result(std::uint32_t cycles,bool smoke) {
    if(armed_) throw Error(5,"Cannot parse an armed trace");
    const auto bytes=readFile(active_);const auto counts=parseTrace(bytes);
    QJsonObject result{{"status",counts.events?"failed":"complete"},{"proof",!smoke && cycles>=1000 && counts.events==0},{"cycles",integer(cycles)},{"tracePath",qs(active_.string())},{"traceSha256",qs(sha256(bytes))},{"capability",capability_},{"events",traceCounts(counts)},
        {"scope","Linux glibc allocator-event evidence; verified C/C++ and linked shared-library controls"},{"exclusions",QJsonArray{"Windows_CRT_DLL_private_heaps","static_privately_bound_custom_allocators","direct_mmap_driver_GPU_allocations","other_loader_namespaces_deep_binding","unexercised_libc_private_paths","every_failed_API_attempt","work_outside_region","one_shared_control_does_not_prove_every_dependency_binding"}}};
    return result;
}
}
