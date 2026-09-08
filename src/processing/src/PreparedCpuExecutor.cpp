#include "PreparedCpuExecutor.hpp"
#include <stdexcept>
#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#include <xmmintrin.h>
#define LUMORA_CPU_SIMD_ENV 1
#endif
namespace lumora::processing::detail {
namespace {
unsigned simdControl() noexcept {
#if defined(LUMORA_CPU_SIMD_ENV)
    return _mm_getcsr();
#else
    return 0;
#endif
}
void simdControl(unsigned value) noexcept {
#if defined(LUMORA_CPU_SIMD_ENV)
    _mm_setcsr(value);
#else
    (void)value;
#endif
}
}
PreparedCpuExecutor::PreparedCpuExecutor(std::size_t slots,CpuExecutorTestHooks hooks):slots_(slots),hooks_(hooks) {
    if(slots<1 || slots>4) throw std::invalid_argument("CPU execution slots must be in [1,4]");
    try {
        for(std::size_t slot=1;slot<slots_;++slot) {
            if(hooks_.beforeThreadStart) hooks_.beforeThreadStart(hooks_.context,slot);
            threads_[slot-1]=std::thread(&PreparedCpuExecutor::worker,this,slot);
        }
        std::unique_lock lock(mutex_);
        changed_.wait(lock,[&]{return ready_==slots_-1;});
    } catch(...) { stop(); throw CpuExecutorStartupError{}; }
}
PreparedCpuExecutor::~PreparedCpuExecutor() { stop(); }
void PreparedCpuExecutor::stop() noexcept {
    { std::lock_guard lock(mutex_); stopping_=true; changed_.notify_all(); }
    for(auto& thread:threads_) if(thread.joinable()) thread.join();
}
bool PreparedCpuExecutor::fail(CpuEnvironmentOperation op,std::size_t slot) noexcept {
    return hooks_.failEnvironment && hooks_.failEnvironment(hooks_.context,op,slot);
}
void PreparedCpuExecutor::invoke(std::size_t slot) noexcept {
    // Quotient/remainder avoids overflow even for SIZE_MAX items.
    const auto quotient=itemCount_/slots_,remainder=itemCount_%slots_;
    const auto begin=quotient*slot+(slot<remainder?slot:remainder);
    const auto end=begin+quotient+(slot<remainder?1U:0U);
    if(begin!=end) {
        if(observer_) observer_(observerContext_,jobKind_,slot,begin,end);
        work_(context_,slot,begin,end);
    }
}
void PreparedCpuExecutor::run(std::size_t n,void* context,Work work,CpuJobKind kind) noexcept {
    if(!n) return;
    const auto serial=[&] {
        if(observer_) observer_(observerContext_,kind,0,0,n);
        work(context,0,0,n);
    };
    if(slots_==1 || serialOnly_ || fail(CpuEnvironmentOperation::CaptureCaller,0) || std::fegetenv(&callerEnvironment_)!=0) {
        serial(); return;
    }
    callerSimdControl_=simdControl();
    std::unique_lock lock(mutex_);
    itemCount_=n;context_=context;work_=work;jobKind_=kind;prepared_=0;completed_=0;decided_=false;setupFailed_=false;
    ++generation_;changed_.notify_all();
    changed_.wait(lock,[&]{return prepared_==slots_-1;});
    commit_=!setupFailed_;decided_=true;changed_.notify_all();
    const bool committed=commit_;
    lock.unlock();
    if(committed) invoke(0);
    lock.lock();
    changed_.wait(lock,[&]{return completed_==slots_-1;});
    context_=nullptr;work_=nullptr;
    lock.unlock();
    // On abort every helper is restored/quiescent before the first mutation.
    if(!committed) serial();
}
void PreparedCpuExecutor::worker(std::size_t slot) noexcept {
    if(hooks_.workerLifetime) hooks_.workerLifetime(hooks_.context,slot,true);
    std::unique_lock lock(mutex_);
    ++ready_;changed_.notify_all();
    std::size_t observed=0;
    while(true) {
        changed_.wait(lock,[&]{return stopping_ || generation_!=observed;});
        if(stopping_) break;
        observed=generation_;
        lock.unlock();
        std::fenv_t previous{};
        const unsigned previousSimd=simdControl();
        const bool saved=!fail(CpuEnvironmentOperation::SaveHelper,slot) && std::fegetenv(&previous)==0;
        const bool installed=saved && !fail(CpuEnvironmentOperation::InstallHelper,slot) && std::fesetenv(&callerEnvironment_)==0;
        if(installed) simdControl(callerSimdControl_);
        lock.lock();
        setupFailed_=setupFailed_ || !installed;
        ++prepared_;changed_.notify_all();
        changed_.wait(lock,[&]{return decided_;});
        const bool committed=commit_;
        lock.unlock();
        if(committed) invoke(slot);
        bool restored=true;
        if(saved) {
            restored=!fail(CpuEnvironmentOperation::RestoreHelper,slot) && std::fesetenv(&previous)==0;
            simdControl(previousSimd);
        }
        lock.lock();
        // Already committed work is never replayed on failed restoration.
        if(!restored) serialOnly_=true;
        ++completed_;changed_.notify_all();
    }
    lock.unlock();
    if(hooks_.workerLifetime) hooks_.workerLifetime(hooks_.context,slot,false);
}
}
