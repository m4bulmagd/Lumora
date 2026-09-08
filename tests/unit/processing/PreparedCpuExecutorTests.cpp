#include "PreparedCpuExecutor.hpp"
#include <gtest/gtest.h>
#include <array>
#include <atomic>
#include <cfenv>
#include <thread>
#include <latch>
#include <limits>
#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#endif
namespace lumora::processing::detail {
namespace {
struct Coverage {
    std::array<unsigned,17> visits{};
    std::array<unsigned,4> calls{};
    std::array<std::thread::id,4> threads{};
    static void work(void* opaque,std::size_t slot,std::size_t begin,std::size_t end) noexcept {
        auto& c=*static_cast<Coverage*>(opaque); ++c.calls[slot]; c.threads[slot]=std::this_thread::get_id();
        for(auto i=begin;i<end;++i) ++c.visits[i];
    }
};
TEST(PreparedCpuExecutor, CoversDisjointRangesSynchronouslyAndReusesPersistentHelpers) {
    PreparedCpuExecutor executor(4); EXPECT_EQ(executor.slots(),4U);
    std::array<std::thread::id,4> first{};
    for(auto count:{17U,2U,0U,9U}) {
        Coverage c; executor.run(count,&c,Coverage::work);
        for(std::size_t i=0;i<c.visits.size();++i) EXPECT_EQ(c.visits[i],i<count?1U:0U);
        for(std::size_t s=0;s<4;++s) EXPECT_EQ(c.calls[s],s<count?1U:0U);
        if(count==17) first=c.threads;
        if(count==9) { EXPECT_EQ(c.threads,first); }
    }
    EXPECT_EQ(first[0],std::this_thread::get_id());
    for(std::size_t s=1;s<4;++s) { EXPECT_NE(first[s],first[0]); for(std::size_t t=1;t<s;++t) EXPECT_NE(first[s],first[t]); }
}
TEST(PreparedCpuExecutor, OneSlotRunsOnCallerAndRejectsInvalidCounts) {
    PreparedCpuExecutor executor(1); Coverage c; executor.run(17,&c,Coverage::work);
    EXPECT_EQ(c.calls[0],1U); EXPECT_EQ(c.threads[0],std::this_thread::get_id());
    EXPECT_THROW(PreparedCpuExecutor(0),std::invalid_argument);
    EXPECT_THROW(PreparedCpuExecutor(5),std::invalid_argument);
}
struct Faults {
    std::atomic<unsigned> live{},started{},stopped{},installed{},restored{};
    std::atomic<int> operation{-1};
    std::size_t startFailure{};
    static void start(void* p,std::size_t slot) { if(static_cast<Faults*>(p)->startFailure==slot) throw std::runtime_error("injected start failure"); }
    static void life(void* p,std::size_t,bool entering) noexcept {
        auto& f=*static_cast<Faults*>(p); if(entering) {++f.live;++f.started;} else {--f.live;++f.stopped;}
    }
    static bool environment(void* p,CpuEnvironmentOperation op,std::size_t slot) noexcept {
        auto& f=*static_cast<Faults*>(p);
        if(op==CpuEnvironmentOperation::InstallHelper) ++f.installed;
        if(op==CpuEnvironmentOperation::RestoreHelper) ++f.restored;
        auto expected=static_cast<int>(op);
        return (slot==1 || op==CpuEnvironmentOperation::CaptureCaller) && f.operation.compare_exchange_strong(expected,-1);
    }
    CpuExecutorTestHooks hooks() { return {this,start,life,environment}; }
};
TEST(PreparedCpuExecutor, StartupIsReadyDestructionJoinsAndPartialStartFailureCleansUp) {
    Faults f;
    { PreparedCpuExecutor executor(4,f.hooks()); EXPECT_EQ(f.live,3U); EXPECT_EQ(f.started,3U); }
    EXPECT_EQ(f.live,0U); EXPECT_EQ(f.stopped,3U);
    for(std::size_t failing=1;failing<=3;++failing) {
        Faults partial; partial.startFailure=failing;
        EXPECT_THROW(PreparedCpuExecutor(4,partial.hooks()),CpuExecutorStartupError);
        EXPECT_EQ(partial.live,0U); EXPECT_EQ(partial.started,failing-1); EXPECT_EQ(partial.stopped,failing-1);
    }
}
struct EnvironmentCoverage : Coverage {
    Faults* faults{}; bool serialExpected{};
    std::array<unsigned,4> preparedBefore{},restoredBefore{};
    static void work(void* p,std::size_t slot,std::size_t begin,std::size_t end) noexcept {
        auto& c=*static_cast<EnvironmentCoverage*>(p);
        c.preparedBefore[slot]=c.faults->installed.load(); c.restoredBefore[slot]=c.faults->restored.load();
        Coverage::work(static_cast<Coverage*>(&c),slot,begin,end);
    }
};
TEST(PreparedCpuExecutor, CaptureSaveAndInstallFailuresAbortBeforeAnyCallbackAndFallBackOnce) {
    for(auto op:{CpuEnvironmentOperation::CaptureCaller,CpuEnvironmentOperation::SaveHelper,CpuEnvironmentOperation::InstallHelper}) {
        Faults f; PreparedCpuExecutor executor(4,f.hooks()); f.operation=static_cast<int>(op);
        EnvironmentCoverage c; c.faults=&f; executor.run(17,&c,EnvironmentCoverage::work);
        EXPECT_EQ(c.calls,(std::array<unsigned,4>{1,0,0,0})); for(auto count:c.visits) EXPECT_EQ(count,1U);
        if(op!=CpuEnvironmentOperation::CaptureCaller) { EXPECT_EQ(c.restoredBefore[0],op==CpuEnvironmentOperation::SaveHelper?2U:3U); }
        Coverage next; executor.run(17,&next,Coverage::work); EXPECT_EQ(next.calls,(std::array<unsigned,4>{1,1,1,1}));
    }
}
TEST(PreparedCpuExecutor, CommitWaitsForAllSetupsAndRestoreFailureDoesNotReplayCompletedWork) {
    Faults f; PreparedCpuExecutor executor(4,f.hooks()); f.operation=static_cast<int>(CpuEnvironmentOperation::RestoreHelper);
    EnvironmentCoverage c; c.faults=&f; executor.run(17,&c,EnvironmentCoverage::work);
    EXPECT_EQ(c.calls,(std::array<unsigned,4>{1,1,1,1})); for(auto count:c.visits) EXPECT_EQ(count,1U);
    for(auto count:c.preparedBefore) EXPECT_EQ(count,3U);
    Coverage next; executor.run(17,&next,Coverage::work); EXPECT_EQ(next.calls,(std::array<unsigned,4>{1,0,0,0}));
}
TEST(PreparedCpuExecutor, EachDispatchPropagatesCallerRoundingAndPreservesCallerMode) {
    const int original=std::fegetround(); PreparedCpuExecutor executor(4);
    struct Modes { std::array<int,4> values{}; static void work(void* p,std::size_t s,std::size_t,std::size_t) noexcept { static_cast<Modes*>(p)->values[s]=std::fegetround(); } };
    for(auto mode:{FE_DOWNWARD,FE_UPWARD,FE_TONEAREST,FE_TOWARDZERO}) {
        ASSERT_EQ(std::fesetround(mode),0); Modes m; executor.run(4,&m,Modes::work);
        for(auto value:m.values) { EXPECT_EQ(value,mode); } EXPECT_EQ(std::fegetround(),mode);
    }
    EXPECT_EQ(std::fesetround(original),0);
}

TEST(PreparedCpuExecutor, ReturnWaitsForEveryBorrowedContextUser) {
    PreparedCpuExecutor executor(4);
    struct Context {
        std::latch entered{4},release{1};
        std::atomic<unsigned> finished{};
        static void work(void* p,std::size_t,std::size_t,std::size_t) noexcept {
            auto& c=*static_cast<Context*>(p);c.entered.count_down();c.release.wait();++c.finished;
        }
    } context;
    std::atomic<bool> returned{};
    std::thread caller([&] {executor.run(4,&context,Context::work);returned=true;});
    context.entered.wait();EXPECT_FALSE(returned);EXPECT_EQ(context.finished,0U);
    context.release.count_down();caller.join();EXPECT_TRUE(returned);EXPECT_EQ(context.finished,4U);
}
TEST(PreparedCpuExecutor, PartitionArithmeticHandlesMaximumItemCount) {
    PreparedCpuExecutor executor(4);
    struct Ranges {
        std::array<std::size_t,4> begin{},end{};
        static void work(void* p,std::size_t s,std::size_t b,std::size_t e) noexcept {auto& r=*static_cast<Ranges*>(p);r.begin[s]=b;r.end[s]=e;}
    } ranges;
    executor.run(std::numeric_limits<std::size_t>::max(),&ranges,Ranges::work);
    EXPECT_EQ(ranges.begin[0],0U);EXPECT_EQ(ranges.end[3],std::numeric_limits<std::size_t>::max());
    for(std::size_t s=1;s<4;++s) EXPECT_EQ(ranges.begin[s],ranges.end[s-1]);
}
#if defined(__SSE__) || defined(_M_X64)
TEST(PreparedCpuExecutor, PropagatesSimdControlModesAndRestoresHelpersBetweenJobs) {
    struct Context {
        std::array<unsigned,4> observed{};
        std::array<unsigned,4> prior{};
        static bool observeSave(void* p,CpuEnvironmentOperation op,std::size_t slot) noexcept {
            if(op==CpuEnvironmentOperation::SaveHelper) static_cast<Context*>(p)->prior[slot]=_mm_getcsr();
            return false;
        }
        static void work(void* p,std::size_t slot,std::size_t,std::size_t) noexcept {static_cast<Context*>(p)->observed[slot]=_mm_getcsr();}
    } context;
    const unsigned original=_mm_getcsr();
    PreparedCpuExecutor executor(4,{&context,nullptr,nullptr,Context::observeSave});
    // Flush-to-zero is a supported x86 SSE control independent of rounding.
    for(unsigned control:{original ^ 0x8000U,original}) {
        _mm_setcsr(control);executor.run(4,&context,Context::work);
        for(auto value:context.observed) EXPECT_EQ(value,control);
        for(std::size_t slot=1;slot<4;++slot) EXPECT_EQ(context.prior[slot],original);
        EXPECT_EQ(_mm_getcsr(),control);
    }
    _mm_setcsr(original);
}
#endif

}
}
