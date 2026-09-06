#include <lumora/application/CameraSessionStateMachine.hpp>

#include <gtest/gtest.h>

#include <array>
#include <optional>

namespace {
using namespace lumora::application;
using S = CameraSessionState;
using E = CameraSessionEvent;

void reachState(CameraSessionStateMachine& machine, S target) {
    if (target == S::Disconnected) { return; }
    if (target == S::ShuttingDown) {
        ASSERT_TRUE(machine.apply(E::ShutdownRequested).hasValue());
        return;
    }
    if (target == S::Discovering || target == S::Error) {
        ASSERT_TRUE(machine.apply(E::DiscoverRequested).hasValue());
        if (target == S::Error) {
            ASSERT_TRUE(machine.apply(E::DiscoverFailed).hasValue());
        }
        return;
    }
    ASSERT_TRUE(machine.apply(E::ConnectRequested).hasValue());
    if (target == S::Connecting) { return; }
    ASSERT_TRUE(machine.apply(E::OpenSucceeded).hasValue());
    if (target == S::ConnectedIdle) { return; }
    ASSERT_TRUE(machine.apply(E::ApplyRequested).hasValue());
    ASSERT_TRUE(machine.apply(E::ApplySucceeded).hasValue());
    ASSERT_TRUE(machine.apply(E::ConfirmRequested).hasValue());
    ASSERT_TRUE(machine.apply(E::StartRequested).hasValue());
    ASSERT_TRUE(machine.apply(E::StartSucceeded).hasValue());
    if (target == S::Reconnecting) {
        ASSERT_TRUE(machine.apply(E::DeviceRemoved).hasValue());
    }
}

TEST(CameraSessionStateMachine, EveryStateEventPairFollowsThePreflightTable) {
    constexpr auto X = std::nullopt;
    constexpr auto D = S::Disconnected;
    constexpr auto V = S::Discovering;
    constexpr auto C = S::Connecting;
    constexpr auto I = S::ConnectedIdle;
    constexpr auto L = S::Streaming;
    constexpr auto R = S::Reconnecting;
    constexpr auto F = S::Error;
    constexpr auto Q = S::ShuttingDown;
    constexpr std::array states{D, V, C, I, L, R, F, Q};
    constexpr std::array events{
        E::DiscoverRequested, E::DiscoverSucceeded, E::DiscoverFailed,
        E::ConnectRequested, E::OpenSucceeded, E::OpenFailed, E::CapabilitiesFailed,
        E::ApplyRequested, E::ApplySucceeded, E::ApplyFailed, E::ConfirmRequested,
        E::StartRequested, E::StartSucceeded, E::StartFailed,
        E::StopRequested, E::StopSucceeded, E::StopFailed,
        E::DeviceRemoved, E::TimeoutThresholdReached, E::RetryRequested,
        E::DisconnectRequested, E::DisconnectSucceeded, E::DisconnectFailed, E::ShutdownRequested};
    // Literal expected rows from the preflight. Requested operations do not
    // report success early. Payload and outstanding-operation guards are worker-owned.
    constexpr std::array<std::array<std::optional<S>, 24U>, 8U> expected{{
        {{V,X,X,C,X,X,X,X,X,X,X,X,X,X,D,D,X,X,X,X,D,D,F,Q}},
        {{X,D,F,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,V,D,F,Q}},
        {{X,X,X,X,I,F,F,X,X,X,X,X,X,X,X,X,X,X,X,X,C,D,F,Q}},
        {{X,X,X,I,X,X,X,I,I,I,I,I,L,F,I,I,F,X,X,X,I,D,F,Q}},
        {{X,X,X,L,X,X,X,X,X,X,X,L,L,X,L,I,F,R,R,X,L,D,F,Q}},
        {{X,X,X,C,X,X,X,X,X,X,X,X,X,X,R,R,F,X,X,C,R,D,F,Q}},
        {{X,X,X,C,X,X,X,X,X,X,X,X,X,X,F,F,F,X,X,C,F,D,F,Q}},
        {{X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,Q}},
    }};
    for (std::size_t row = 0U; row < states.size(); ++row) {
        for (std::size_t column = 0U; column < events.size(); ++column) {
            SCOPED_TRACE(::testing::Message() << "state=" << row << " event=" << column);
            CameraSessionStateMachine machine;
            ASSERT_NO_FATAL_FAILURE(reachState(machine, states[row]));
            ASSERT_EQ(machine.state(), states[row]);
            const auto result = machine.apply(events[column]);
            if (expected[row][column]) {
                ASSERT_TRUE(result.hasValue());
                EXPECT_EQ(machine.state(), *expected[row][column]);
            } else {
                ASSERT_FALSE(result.hasValue());
                EXPECT_EQ(machine.state(), states[row]);
                const bool terminal = states[row] == Q;
                EXPECT_EQ(result.error().category, terminal
                    ? lumora::core::ErrorCategory::Cancelled
                    : lumora::core::ErrorCategory::CameraConfiguration);
                EXPECT_EQ(result.error().code, terminal ? "cancelled" : "invalid_camera_state");
            }
        }
    }
}

TEST(CameraSessionStateMachine, RemovalWhileStreamingRequestsReconnect) {
    CameraSessionStateMachine machine;
    ASSERT_NO_FATAL_FAILURE(reachState(machine, S::Streaming));
    ASSERT_TRUE(machine.apply(E::DeviceRemoved).hasValue());
    EXPECT_EQ(machine.state(), S::Reconnecting);
    ASSERT_TRUE(machine.apply(E::RetryRequested).hasValue());
    ASSERT_TRUE(machine.apply(E::OpenSucceeded).hasValue());
    EXPECT_EQ(machine.state(), S::ConnectedIdle);
}

TEST(CameraSessionStateMachine, RejectedIdleConfigurationDoesNotBecomeARecovery) {
    CameraSessionStateMachine machine;
    ASSERT_NO_FATAL_FAILURE(reachState(machine, S::ConnectedIdle));
    ASSERT_TRUE(machine.apply(E::ApplyRequested).hasValue());
    ASSERT_TRUE(machine.apply(E::ApplyFailed).hasValue());
    EXPECT_EQ(machine.state(), S::ConnectedIdle);
    ASSERT_TRUE(machine.apply(E::StartRequested).hasValue());
    EXPECT_EQ(machine.state(), S::ConnectedIdle);
    ASSERT_TRUE(machine.apply(E::StartFailed).hasValue());
    EXPECT_EQ(machine.state(), S::Error);
    ASSERT_TRUE(machine.apply(E::DisconnectRequested).hasValue());
    EXPECT_EQ(machine.state(), S::Error);
    ASSERT_TRUE(machine.apply(E::DisconnectSucceeded).hasValue());
    EXPECT_FALSE(machine.apply(E::RetryRequested).hasValue());
}

TEST(CameraSessionStateMachine, CannotStreamBeforeConnection) {
    CameraSessionStateMachine machine;
    const auto result = machine.apply(CameraSessionEvent::StartRequested);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().category, lumora::core::ErrorCategory::CameraConfiguration);
    EXPECT_EQ(result.error().code, "invalid_camera_state");
    EXPECT_EQ(machine.state(), CameraSessionState::Disconnected);
}
}  // namespace
