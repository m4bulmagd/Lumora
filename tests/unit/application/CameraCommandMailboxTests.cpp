#include <lumora/application/CameraCommandMailbox.hpp>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <latch>
#include <thread>
#include <vector>

namespace {
using namespace lumora::application;
using namespace std::chrono_literals;
void expectCancelled(const lumora::core::Result<void>& result);

ApplyConfiguration apply(std::uint64_t generation, std::uint64_t revision) {
    return {generation, {
        .pixelFormat = {"Mono8", 0x01080001U, 8U, 255U,
            lumora::core::SourcePacking::Unpacked, lumora::core::BitAlignment::LeastSignificant,
            lumora::core::StorageType::UInt8},
        .roi = {4U, 8U, 640U, 480U}, .requestedFps = 30.0,
        .exposure = {lumora::camera::ExposureMode::Manual, 1000.0},
        .gain = {lumora::camera::GainMode::Manual, 6.0},
        .acquisitionMode = lumora::camera::AcquisitionMode::Continuous}, revision};
}

TEST(CameraCommandMailbox, ApplyCoalescesOnlyWithinItsGenerationAndInvalidatesOldConfirmation) {
    CameraCommandMailbox mailbox;
    ASSERT_TRUE(mailbox.post({1U, apply(7U, 10U)}).hasValue());
    ASSERT_TRUE(mailbox.post({2U, ConfirmConfiguration{7U, 10U}}).hasValue());
    ASSERT_TRUE(mailbox.post({3U, apply(8U, 20U)}).hasValue());
    ASSERT_TRUE(mailbox.post({4U, ConfirmConfiguration{8U, 20U}}).hasValue());
    ASSERT_TRUE(mailbox.post({5U, apply(7U, 11U)}).hasValue());
    ASSERT_EQ(mailbox.size(), 3U);
    const auto latest = mailbox.tryPop();
    ASSERT_TRUE(latest);
    EXPECT_EQ(latest->requestId, 5U);
    const auto& payload = std::get<ApplyConfiguration>(latest->payload);
    EXPECT_EQ(payload.sessionGeneration, 7U);
    EXPECT_EQ(payload.requestRevision, 11U);
    EXPECT_EQ(payload.configuration.pixelFormat.canonicalName, "Mono8");
    EXPECT_EQ(payload.configuration.pixelFormat.canonicalEncoding, 0x01080001U);
    EXPECT_EQ(payload.configuration.pixelFormat.validBits, 8U);
    EXPECT_EQ(payload.configuration.pixelFormat.sampleMaximum, 255U);
    EXPECT_EQ(payload.configuration.pixelFormat.packing, lumora::core::SourcePacking::Unpacked);
    EXPECT_EQ(payload.configuration.pixelFormat.alignment, lumora::core::BitAlignment::LeastSignificant);
    EXPECT_EQ(payload.configuration.pixelFormat.applicationStorage, lumora::core::StorageType::UInt8);
    EXPECT_EQ(payload.configuration.roi.x, 4U);
    EXPECT_EQ(payload.configuration.roi.y, 8U);
    EXPECT_EQ(payload.configuration.roi.width, 640U);
    EXPECT_EQ(payload.configuration.roi.height, 480U);
    EXPECT_EQ(payload.configuration.requestedFps, 30.0);
    EXPECT_EQ(payload.configuration.exposure.mode, lumora::camera::ExposureMode::Manual);
    EXPECT_EQ(payload.configuration.exposure.requestedMicroseconds, 1000.0);
    EXPECT_EQ(payload.configuration.gain.mode, lumora::camera::GainMode::Manual);
    EXPECT_EQ(payload.configuration.gain.requestedDb, 6.0);
    EXPECT_EQ(payload.configuration.acquisitionMode, lumora::camera::AcquisitionMode::Continuous);
    const auto other = mailbox.tryPop();
    ASSERT_TRUE(other);
    EXPECT_EQ(other->requestId, 3U);
    const auto confirmation = mailbox.tryPop();
    ASSERT_TRUE(confirmation);
    EXPECT_EQ(confirmation->requestId, 4U);
    EXPECT_EQ(std::get<ConfirmConfiguration>(confirmation->payload).sessionGeneration, 8U);
    EXPECT_EQ(std::get<ConfirmConfiguration>(confirmation->payload).appliedRequestRevision, 20U);
    EXPECT_EQ(mailbox.stats().coalesced, 1U);
    EXPECT_EQ(mailbox.stats().cancelled, 1U);
}

TEST(CameraCommandMailbox, ConnectAndStartPreventApplyReplacementAcrossLifecycleSegments) {
    const CameraCommand barriers[]{{2U, Connect{{"camera-a"}}}, {2U, StartStream{7U, 10U}}};
    for (const auto& barrier : barriers) {
        CameraCommandMailbox mailbox;
        ASSERT_TRUE(mailbox.post({1U, apply(7U, 10U)}).hasValue());
        ASSERT_TRUE(mailbox.post(barrier).hasValue());
        ASSERT_TRUE(mailbox.post({3U, apply(7U, 11U)}).hasValue());
        ASSERT_TRUE(mailbox.post({4U, apply(7U, 12U)}).hasValue());
        ASSERT_EQ(mailbox.size(), 3U);
        for (const auto id : {1U, 2U, 4U}) {
            const auto command = mailbox.tryPop();
            ASSERT_TRUE(command);
            EXPECT_EQ(command->requestId, id);
            if (id == 2U && std::holds_alternative<StartStream>(command->payload)) {
                EXPECT_EQ(std::get<StartStream>(command->payload).sessionGeneration, 7U);
                EXPECT_EQ(std::get<StartStream>(command->payload).confirmedAppliedRequestRevision, 10U);
            }
        }
    }
}

TEST(CameraCommandMailbox, StopSegmentSurvivesPriorityDequeueAndAllowsStoppedApply) {
    CameraCommandMailbox mailbox;
    ASSERT_TRUE(mailbox.post({1U, apply(7U, 10U)}).hasValue());
    ASSERT_TRUE(mailbox.post({2U, StopStream{}}).hasValue());
    const auto stop = mailbox.tryPop();
    ASSERT_TRUE(stop);
    ASSERT_EQ(stop->requestId, 2U);
    ASSERT_TRUE(mailbox.post({3U, apply(7U, 11U)}).hasValue());
    mailbox.completeBarrier(2U);
    ASSERT_TRUE(mailbox.post({4U, apply(7U, 12U)}).hasValue());
    ASSERT_EQ(mailbox.size(), 2U);
    const auto before = mailbox.tryPop();
    ASSERT_TRUE(before);
    EXPECT_EQ(before->requestId, 1U);
    const auto after = mailbox.tryPop();
    ASSERT_TRUE(after);
    EXPECT_EQ(after->requestId, 4U);
    EXPECT_EQ(mailbox.stats().coalesced, 1U);
}

TEST(CameraCommandMailbox, CoalescedPendingStopDoesNotIntroduceAnotherLifecycleSegment) {
    CameraCommandMailbox mailbox;
    ASSERT_TRUE(mailbox.post({1U, StopStream{}}).hasValue());
    ASSERT_TRUE(mailbox.post({2U, apply(7U, 10U)}).hasValue());
    ASSERT_TRUE(mailbox.post({3U, StopStream{}}).hasValue());
    ASSERT_TRUE(mailbox.post({4U, apply(7U, 11U)}).hasValue());
    ASSERT_EQ(mailbox.size(), 2U);
    const auto stop = mailbox.tryPop();
    ASSERT_TRUE(stop);
    EXPECT_EQ(stop->requestId, 1U);
    mailbox.completeBarrier(1U);
    const auto latest = mailbox.tryPop();
    ASSERT_TRUE(latest);
    EXPECT_EQ(latest->requestId, 4U);
    EXPECT_EQ(mailbox.stats().coalesced, 2U);
}

TEST(CameraCommandMailbox, DisconnectCancelsApplyAndFencesAllSessionPayloads) {
    CameraCommandMailbox mailbox;
    ASSERT_TRUE(mailbox.post({1U, apply(7U, 10U)}).hasValue());
    ASSERT_TRUE(mailbox.post({2U, ConfirmConfiguration{7U, 10U}}).hasValue());
    ASSERT_TRUE(mailbox.post({3U, Disconnect{}}).hasValue());
    expectCancelled(mailbox.post({4U, apply(7U, 11U)}));
    expectCancelled(mailbox.post({5U, ConfirmConfiguration{7U, 10U}}));
    ASSERT_TRUE(mailbox.tryPop());
    expectCancelled(mailbox.post({6U, apply(8U, 1U)}));
    mailbox.completeBarrier(3U);
    ASSERT_TRUE(mailbox.post({7U, apply(8U, 1U)}).hasValue());
    ASSERT_EQ(mailbox.size(), 1U);
    EXPECT_EQ(mailbox.stats().cancelled, 2U);
}

TEST(CameraCommandMailbox, FullMailboxStillReplacesAnEligibleApplyWithoutEviction) {
    CameraCommandMailbox mailbox;
    ASSERT_TRUE(mailbox.post({1U, apply(7U, 10U)}).hasValue());
    for (std::uint64_t id = 2U; id <= 32U; ++id) {
        ASSERT_TRUE(mailbox.post({id, Discover{}}).hasValue());
    }
    ASSERT_EQ(mailbox.size(), 32U);
    ASSERT_TRUE(mailbox.post({33U, apply(7U, 11U)}).hasValue());
    ASSERT_EQ(mailbox.size(), 32U);
    EXPECT_EQ(mailbox.stats().coalesced, 1U);
    EXPECT_EQ(mailbox.stats().cancelled, 0U);
    const auto latest = mailbox.tryPop();
    ASSERT_TRUE(latest);
    EXPECT_EQ(latest->requestId, 33U);
}

void expectCancelled(const lumora::core::Result<void>& result) {
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().category, lumora::core::ErrorCategory::Cancelled);
    EXPECT_EQ(result.error().code, "cancelled");
}

void fillWithDiscoveries(CameraCommandMailbox& mailbox) {
    for (std::uint64_t id = 1U; id <= 32U; ++id) {
        ASSERT_TRUE(mailbox.post({id, Discover{}}).hasValue());
    }
    ASSERT_EQ(mailbox.size(), 32U);
}

TEST(CameraCommandMailbox, FullMailboxRejectsOrdinaryWorkAndPreservesFifo) {
    CameraCommandMailbox mailbox;
    ASSERT_NO_FATAL_FAILURE(fillWithDiscoveries(mailbox));
    const auto result = mailbox.post({33U, Connect{{"camera-a"}}});
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().category, lumora::core::ErrorCategory::ResourceExhaustion);
    EXPECT_EQ(result.error().code, "camera_mailbox_full");
    for (std::uint64_t id = 1U; id <= 32U; ++id) {
        const auto command = mailbox.tryPop();
        ASSERT_TRUE(command);
        EXPECT_EQ(command->requestId, id);
    }
    EXPECT_FALSE(mailbox.tryPop());
    EXPECT_EQ(mailbox.stats().cancelled, 0U);
}

TEST(CameraCommandMailbox, FullMailboxAlwaysAdmitsEachPriorityAndCountsDiscardedWork) {
    const CameraCommand priorities[]{{100U, StopStream{}}, {100U, Disconnect{}}, {100U, Shutdown{}}};
    for (const auto& priority : priorities) {
        SCOPED_TRACE(priority.payload.index());
        CameraCommandMailbox mailbox;
        ASSERT_NO_FATAL_FAILURE(fillWithDiscoveries(mailbox));
        ASSERT_TRUE(mailbox.post(priority).hasValue());
        const bool shutdown = std::holds_alternative<Shutdown>(priority.payload);
        EXPECT_EQ(mailbox.size(), shutdown ? 1U : 32U);
        const auto command = mailbox.tryPop();
        ASSERT_TRUE(command);
        EXPECT_EQ(command->requestId, 100U);
        EXPECT_EQ(command->payload.index(), priority.payload.index());
        EXPECT_EQ(mailbox.stats().cancelled, shutdown ? 32U : 1U);
        mailbox.completeBarrier(100U);
        const auto next = mailbox.tryPop();
        if (shutdown) {
            EXPECT_FALSE(next);
        } else {
            ASSERT_TRUE(next);
            EXPECT_EQ(next->requestId, 2U);
        }
    }
}

TEST(CameraCommandMailbox, StopCancelsStartAndConfirmAndKeepsItsFenceUntilCompletion) {
    CameraCommandMailbox mailbox;
    ASSERT_TRUE(mailbox.post({1U, StartStream{7U, 9U}}).hasValue());
    ASSERT_TRUE(mailbox.post({2U, ConfirmConfiguration{7U, 9U}}).hasValue());
    ASSERT_TRUE(mailbox.post({3U, StopStream{}}).hasValue());
    EXPECT_EQ(mailbox.size(), 1U);
    EXPECT_EQ(mailbox.stats().cancelled, 2U);
    expectCancelled(mailbox.post({4U, StartStream{7U, 9U}}));
    expectCancelled(mailbox.post({5U, ConfirmConfiguration{7U, 9U}}));
    const auto stop = mailbox.tryPop();
    ASSERT_TRUE(stop);
    EXPECT_EQ(stop->requestId, 3U);
    expectCancelled(mailbox.post({6U, StartStream{7U, 9U}}));
    mailbox.completeBarrier(999U);
    expectCancelled(mailbox.post({7U, ConfirmConfiguration{7U, 9U}}));
    mailbox.completeBarrier(3U);
    ASSERT_TRUE(mailbox.post({8U, StartStream{7U, 9U}}).hasValue());
}

TEST(CameraCommandMailbox, DisconnectSupersedesStopAndCameraSpecificWorkButKeepsDiscover) {
    CameraCommandMailbox mailbox;
    ASSERT_TRUE(mailbox.post({1U, Discover{}}).hasValue());
    ASSERT_TRUE(mailbox.post({2U, Connect{{"camera-a"}}}).hasValue());
    ASSERT_TRUE(mailbox.post({3U, Retry{}}).hasValue());
    ASSERT_TRUE(mailbox.post({4U, StartStream{1U, 1U}}).hasValue());
    ASSERT_TRUE(mailbox.post({5U, StopStream{}}).hasValue());
    ASSERT_TRUE(mailbox.post({6U, Disconnect{}}).hasValue());
    EXPECT_EQ(mailbox.size(), 2U);
    expectCancelled(mailbox.post({7U, Connect{{"camera-b"}}}));
    expectCancelled(mailbox.post({8U, Retry{}}));
    const auto disconnect = mailbox.tryPop();
    ASSERT_TRUE(disconnect);
    EXPECT_EQ(disconnect->requestId, 6U);
    expectCancelled(mailbox.post({9U, Connect{{"camera-b"}}}));
    expectCancelled(mailbox.post({10U, StartStream{1U, 1U}}));
    mailbox.completeBarrier(6U);
    ASSERT_TRUE(mailbox.post({11U, Connect{{"camera-b"}}}).hasValue());
    const auto discover = mailbox.tryPop();
    ASSERT_TRUE(discover);
    EXPECT_EQ(discover->requestId, 1U);
    EXPECT_EQ(mailbox.stats().cancelled, 4U);
}

TEST(CameraCommandMailbox, PendingDuplicateBarrierRetainsTheOriginalRequestId) {
    const CameraCommand priorities[]{{10U, StopStream{}}, {10U, Disconnect{}}};
    for (const auto& priority : priorities) {
        CameraCommandMailbox mailbox;
        ASSERT_TRUE(mailbox.post(priority).hasValue());
        const auto duplicateResult = std::holds_alternative<StopStream>(priority.payload)
            ? mailbox.post({20U, StopStream{}}) : mailbox.post({20U, Disconnect{}});
        ASSERT_TRUE(duplicateResult.hasValue());
        EXPECT_EQ(mailbox.size(), 1U);
        EXPECT_EQ(mailbox.stats().coalesced, 1U);
        const auto command = mailbox.tryPop();
        ASSERT_TRUE(command);
        EXPECT_EQ(command->requestId, 10U);
        mailbox.completeBarrier(20U);
        expectCancelled(mailbox.post({30U, StartStream{1U, 1U}}));
        mailbox.completeBarrier(10U);
        EXPECT_TRUE(mailbox.post({31U, StartStream{1U, 1U}}).hasValue());
    }
}

TEST(CameraCommandMailbox, NewBarrierDuringExecutionCannotBeClearedByOldCompletion) {
    CameraCommandMailbox mailbox;
    ASSERT_TRUE(mailbox.post({1U, StopStream{}}).hasValue());
    ASSERT_TRUE(mailbox.tryPop());
    ASSERT_TRUE(mailbox.post({2U, Disconnect{}}).hasValue());
    mailbox.completeBarrier(2U); // A pending request has not completed execution.
    expectCancelled(mailbox.post({3U, StartStream{1U, 1U}}));
    mailbox.completeBarrier(1U);
    expectCancelled(mailbox.post({4U, Connect{{"camera-a"}}}));
    const auto disconnect = mailbox.tryPop();
    ASSERT_TRUE(disconnect);
    EXPECT_EQ(disconnect->requestId, 2U);
    mailbox.completeBarrier(1U); // A stale completion cannot release request 2.
    expectCancelled(mailbox.post({5U, Connect{{"camera-a"}}}));
    mailbox.completeBarrier(2U);
    EXPECT_TRUE(mailbox.post({6U, Connect{{"camera-a"}}}).hasValue());
}

TEST(CameraCommandMailbox, RepeatedStopDuringExecutionNeedsItsOwnCompletion) {
    CameraCommandMailbox mailbox;
    ASSERT_TRUE(mailbox.post({1U, StopStream{}}).hasValue());
    ASSERT_TRUE(mailbox.tryPop());
    ASSERT_TRUE(mailbox.post({2U, StopStream{}}).hasValue());
    mailbox.completeBarrier(1U);
    expectCancelled(mailbox.post({3U, StartStream{1U, 1U}}));
    const auto second = mailbox.tryPop();
    ASSERT_TRUE(second);
    EXPECT_EQ(second->requestId, 2U);
    mailbox.completeBarrier(2U);
    EXPECT_TRUE(mailbox.post({4U, StartStream{1U, 1U}}).hasValue());
}

TEST(CameraCommandMailbox, ShutdownSealsAdmissionAndRepeatedShutdownIsIdempotent) {
    CameraCommandMailbox mailbox;
    ASSERT_TRUE(mailbox.post({1U, Disconnect{}}).hasValue());
    ASSERT_TRUE(mailbox.tryPop());
    ASSERT_TRUE(mailbox.post({2U, StopStream{}}).hasValue());
    ASSERT_TRUE(mailbox.post({3U, Shutdown{}}).hasValue());
    ASSERT_TRUE(mailbox.post({4U, Shutdown{}}).hasValue());
    expectCancelled(mailbox.post({5U, Discover{}}));
    expectCancelled(mailbox.post({6U, Disconnect{}}));
    mailbox.completeBarrier(1U);
    const auto shutdown = mailbox.tryPop();
    ASSERT_TRUE(shutdown);
    EXPECT_EQ(shutdown->requestId, 3U);
    EXPECT_TRUE(std::holds_alternative<Shutdown>(shutdown->payload));
    ASSERT_TRUE(mailbox.post({7U, Shutdown{}}).hasValue());
    EXPECT_FALSE(mailbox.tryPop());
    EXPECT_FALSE(mailbox.waitPop({}));
    EXPECT_EQ(mailbox.stats().coalesced, 2U);
    EXPECT_EQ(mailbox.stats().cancelled, 1U);
}

TEST(CameraCommandMailbox, StopPrecedesOrdinaryWork) {
    CameraCommandMailbox mailbox;
    ASSERT_TRUE(mailbox.post({1U, Discover{}}).hasValue());
    ASSERT_TRUE(mailbox.post({2U, StopStream{}}).hasValue());
    const auto first = mailbox.tryPop();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->requestId, 2U);
    EXPECT_TRUE(std::holds_alternative<StopStream>(first->payload));
}

TEST(CameraCommandMailbox, CloseDiscardsPendingWorkAndKeepsAggregateSnapshotsIndependent) {
    CameraCommandMailbox mailbox;
    ASSERT_TRUE(mailbox.post({1U, Discover{}}).hasValue());
    for (std::uint64_t id = 2U; id <= 10002U; ++id) {
        ASSERT_TRUE(mailbox.post({id, StopStream{}}).hasValue());
    }
    const auto before = mailbox.stats();
    EXPECT_EQ(before.coalesced, 10000U);
    EXPECT_EQ(before.cancelled, 0U);
    mailbox.close();
    mailbox.close();
    EXPECT_EQ(mailbox.size(), 0U);
    EXPECT_EQ(mailbox.stats().cancelled, 2U);
    EXPECT_EQ(before.cancelled, 0U);
    EXPECT_FALSE(mailbox.tryPop());
    EXPECT_FALSE(mailbox.waitPop({}));
    expectCancelled(mailbox.post({10003U, Discover{}}));
    expectCancelled(mailbox.post({10004U, Shutdown{}}));
}

TEST(CameraCommandMailbox, WaitPopWakesForPostedWorkCloseAndStopToken) {
    enum class Wake { Post, Close, Cancel, Shutdown };
    for (const auto wake : {Wake::Post, Wake::Close, Wake::Cancel, Wake::Shutdown}) {
        SCOPED_TRACE(static_cast<int>(wake));
        CameraCommandMailbox mailbox;
        std::promise<void> enteredPromise;
        auto entered = enteredPromise.get_future();
        std::promise<std::optional<CameraCommand>> resultPromise;
        auto result = resultPromise.get_future();
        std::jthread worker([&](std::stop_token token) {
            enteredPromise.set_value();
            resultPromise.set_value(mailbox.waitPop(token));
        });
        ASSERT_EQ(entered.wait_for(1s), std::future_status::ready);
        if (wake == Wake::Post) {
            ASSERT_TRUE(mailbox.post({1U, Discover{}}).hasValue());
        } else if (wake == Wake::Shutdown) {
            ASSERT_TRUE(mailbox.post({1U, Shutdown{}}).hasValue());
        } else if (wake == Wake::Close) {
            mailbox.close();
        } else {
            worker.request_stop();
        }
        ASSERT_EQ(result.wait_for(1s), std::future_status::ready);
        const auto command = result.get();
        if (wake == Wake::Post || wake == Wake::Shutdown) {
            ASSERT_TRUE(command);
            EXPECT_EQ(command->requestId, 1U);
            EXPECT_EQ(std::holds_alternative<Shutdown>(command->payload), wake == Wake::Shutdown);
        } else {
            EXPECT_FALSE(command);
        }
    }
}

TEST(CameraCommandMailbox, AlreadyCancelledWaitDoesNotConsumePendingWork) {
    CameraCommandMailbox mailbox;
    ASSERT_TRUE(mailbox.post({1U, Discover{}}).hasValue());
    std::stop_source stop;
    stop.request_stop();
    EXPECT_FALSE(mailbox.waitPop(stop.get_token()));
    const auto retained = mailbox.tryPop();
    ASSERT_TRUE(retained);
    EXPECT_EQ(retained->requestId, 1U);
}

TEST(CameraCommandMailbox, WaitPopKeepsBarrierUntilThePoppedRequestCompletes) {
    CameraCommandMailbox mailbox;
    ASSERT_TRUE(mailbox.post({1U, StopStream{}}).hasValue());
    ASSERT_TRUE(mailbox.tryPop());
    ASSERT_TRUE(mailbox.post({2U, Discover{}}).hasValue());
    EXPECT_FALSE(mailbox.tryPop());
    std::promise<std::optional<CameraCommand>> resultPromise;
    auto result = resultPromise.get_future();
    std::jthread worker([&](std::stop_token token) {
        resultPromise.set_value(mailbox.waitPop(token));
    });
    mailbox.completeBarrier(1U);
    ASSERT_EQ(result.wait_for(1s), std::future_status::ready);
    const auto command = result.get();
    ASSERT_TRUE(command);
    EXPECT_EQ(command->requestId, 2U);
}

TEST(CameraCommandMailbox, ConcurrentProducersStayBoundedAndPreserveEachProducerOrder) {
    CameraCommandMailbox mailbox;
    std::latch begin(1);
    std::atomic<unsigned> admitted{0U};
    std::atomic<unsigned> exhausted{0U};
    std::vector<std::jthread> producers;
    for (std::uint64_t producer = 0U; producer < 4U; ++producer) {
        producers.emplace_back([&, producer] {
            begin.wait();
            for (std::uint64_t sequence = 1U; sequence <= 16U; ++sequence) {
                const auto result = mailbox.post({producer * 100U + sequence, Discover{}});
                if (result.hasValue()) {
                    ++admitted;
                } else {
                    EXPECT_EQ(result.error().category, lumora::core::ErrorCategory::ResourceExhaustion);
                    EXPECT_EQ(result.error().code, "camera_mailbox_full");
                    ++exhausted;
                }
                EXPECT_LE(mailbox.size(), 32U);
            }
        });
    }
    begin.count_down();
    producers.clear();
    EXPECT_EQ(admitted.load(), 32U);
    EXPECT_EQ(exhausted.load(), 32U);
    std::array<std::uint64_t, 4U> previous{};
    unsigned consumed = 0U;
    while (const auto command = mailbox.tryPop()) {
        const auto producer = static_cast<std::size_t>(command->requestId / 100U);
        ASSERT_LT(producer, previous.size());
        EXPECT_GT(command->requestId, previous[producer]);
        previous[producer] = command->requestId;
        ++consumed;
    }
    EXPECT_EQ(consumed, 32U);
}

TEST(CameraCommandMailbox, ConcurrentApplyProducersCoalesceIndependentlyByGeneration) {
    CameraCommandMailbox mailbox;
    std::latch begin(1);
    std::vector<std::jthread> producers;
    for (std::uint64_t generation = 1U; generation <= 4U; ++generation) {
        producers.emplace_back([&, generation] {
            begin.wait();
            for (std::uint64_t revision = 1U; revision <= 128U; ++revision) {
                EXPECT_TRUE(mailbox.post({generation * 1000U + revision,
                    apply(generation, revision)}).hasValue());
            }
        });
    }
    begin.count_down();
    producers.clear();
    EXPECT_EQ(mailbox.size(), 4U);
    EXPECT_EQ(mailbox.stats().coalesced, 508U);
    std::array<bool, 4U> seen{};
    while (const auto command = mailbox.tryPop()) {
        const auto& payload = std::get<ApplyConfiguration>(command->payload);
        ASSERT_GE(payload.sessionGeneration, 1U);
        ASSERT_LE(payload.sessionGeneration, 4U);
        const auto index = static_cast<std::size_t>(payload.sessionGeneration - 1U);
        EXPECT_FALSE(seen[index]);
        seen[index] = true;
        EXPECT_EQ(payload.requestRevision, 128U);
        EXPECT_EQ(command->requestId, payload.sessionGeneration * 1000U + 128U);
    }
    EXPECT_EQ(seen, (std::array{true, true, true, true}));
}

TEST(CameraCommandMailbox, ConcurrentLateCommandsStayCancelledWhileBarrierExecutes) {
    const CameraCommand barriers[]{{1U, StopStream{}}, {1U, Disconnect{}}};
    for (const auto& barrier : barriers) {
        CameraCommandMailbox mailbox;
        ASSERT_TRUE(mailbox.post(barrier).hasValue());
        ASSERT_TRUE(mailbox.tryPop());
        std::latch begin(1);
        std::vector<std::jthread> producers;
        for (std::uint64_t producer = 0U; producer < 4U; ++producer) {
            producers.emplace_back([&, producer] {
                begin.wait();
                for (std::uint64_t sequence = 1U; sequence <= 64U; ++sequence) {
                    const auto id = 10U + producer * 1000U + sequence;
                    expectCancelled(mailbox.post({id, StartStream{7U, 11U}}));
                    expectCancelled(mailbox.post({id, ConfirmConfiguration{7U, 11U}}));
                    if (std::holds_alternative<Disconnect>(barrier.payload)) {
                        expectCancelled(mailbox.post({id, Connect{{"camera-a"}}}));
                        expectCancelled(mailbox.post({id, apply(7U, 12U)}));
                        expectCancelled(mailbox.post({id, Retry{}}));
                    }
                }
            });
        }
        begin.count_down();
        producers.clear();
        EXPECT_EQ(mailbox.size(), 0U);
        mailbox.completeBarrier(1U);
        EXPECT_TRUE(mailbox.post({5000U, StartStream{7U, 11U}}).hasValue());
    }
}
}  // namespace
