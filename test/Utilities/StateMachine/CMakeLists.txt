# ============================================================================
# State Machine Unit Tests
# Tests for state machine states and transitions
# ============================================================================
#
# Q8b (STRANGLER_MILESTONES.md M8): these tests exercise the Qt-based
# QState/QStateMachine framework's classes directly (including ones
# deliberately not ported to src/Utilities/StateMachine/portable/, e.g.
# GuardedTransition, ParallelState, QGCHistoryState, TimeoutTransition, ...
# -- see tools/ghost/statemachine-port/AUDIT.md's "what can be dropped"
# list), so they only compile against the ON (QGC_ENABLE_QT_STATEMACHINE=ON,
# default) framework. Gating this whole file keeps a QGC_BUILD_TESTING=ON +
# QGC_ENABLE_QT_STATEMACHINE=OFF configuration buildable instead of failing
# to compile on now-excluded classes. Porting this coverage to the portable
# framework is out of Q8b's scope (a v1 drop-in per AUDIT.md's
# recommendation, not a full test-suite migration) -- flagged as follow-up
# work, not silently dropped.
# ============================================================================
if(QGC_ENABLE_QT_STATEMACHINE)

# ----------------------------------------------------------------------------
# Root-level tests
# ----------------------------------------------------------------------------
target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        QGCStateMachineTest.cc
        QGCStateMachineTest.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ----------------------------------------------------------------------------
# Helper Tests
# ----------------------------------------------------------------------------
target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        Helpers/ErrorRecoveryBuilderTest.cc
        Helpers/ErrorRecoveryBuilderTest.h
        Helpers/StateContextTest.cc
        Helpers/StateContextTest.h
        Helpers/StateHistoryRecorderTest.cc
        Helpers/StateHistoryRecorderTest.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Helpers)

# ----------------------------------------------------------------------------
# State Tests
# ----------------------------------------------------------------------------
target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        states/StateTestCommon.h
        states/FunctionStateTest.cc
        states/FunctionStateTest.h
        states/DelayStateTest.cc
        states/DelayStateTest.h
        states/AsyncFunctionStateTest.cc
        states/AsyncFunctionStateTest.h
        states/WaitForSignalStateTest.cc
        states/WaitForSignalStateTest.h
        states/ConditionalStateTest.cc
        states/ConditionalStateTest.h
        states/SkippableAsyncStateTest.cc
        states/SkippableAsyncStateTest.h
        states/ParallelStateTest.cc
        states/ParallelStateTest.h
        states/SubMachineStateTest.cc
        states/SubMachineStateTest.h
        states/QGCStateTest.cc
        states/QGCStateTest.h
        states/QGCFinalStateTest.cc
        states/QGCFinalStateTest.h
        states/QGCHistoryStateTest.cc
        states/QGCHistoryStateTest.h
        states/ProgressStateTest.cc
        states/ProgressStateTest.h
        states/RetryableRequestMessageStateTest.cc
        states/RetryableRequestMessageStateTest.h
        states/WaitStateBaseTest.cc
        states/WaitStateBaseTest.h
        states/QGCAbstractStateTest.cc
        states/QGCAbstractStateTest.h
        states/ShowAppMessageStateTest.cc
        states/ShowAppMessageStateTest.h
        states/RequestMessageStateTest.cc
        states/RequestMessageStateTest.h
        states/SendMavlinkCommandStateTest.cc
        states/SendMavlinkCommandStateTest.h
        states/SendMavlinkMessageStateTest.cc
        states/SendMavlinkMessageStateTest.h
        states/WaitForMavlinkMessageStateTest.cc
        states/WaitForMavlinkMessageStateTest.h
        states/AdditionalStatesCoverageTest.cc
        states/AdditionalStatesCoverageTest.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/states)

# ----------------------------------------------------------------------------
# Transition Tests
# ----------------------------------------------------------------------------
target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        transitions/TransitionTestCommon.cc
        transitions/TransitionTestCommon.h
        transitions/GuardedTransitionTest.cc
        transitions/GuardedTransitionTest.h
        transitions/InternalTransitionTest.cc
        transitions/InternalTransitionTest.h
        transitions/MachineEventTransitionTest.cc
        transitions/MachineEventTransitionTest.h
        transitions/NamedEventTransitionTest.cc
        transitions/NamedEventTransitionTest.h
        transitions/SignalDataTransitionTest.cc
        transitions/SignalDataTransitionTest.h
        transitions/QGCSignalTransitionTest.cc
        transitions/QGCSignalTransitionTest.h
        transitions/RetryTransitionTest.cc
        transitions/RetryTransitionTest.h
        transitions/TimeoutTransitionTest.cc
        transitions/TimeoutTransitionTest.h
        transitions/AdditionalTransitionsCoverageTest.cc
        transitions/AdditionalTransitionsCoverageTest.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/transitions)

endif() # QGC_ENABLE_QT_STATEMACHINE
