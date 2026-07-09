# ============================================================================
# State Machine Subsystem
# ============================================================================
#
# QGCStateMachine (STRANGLER_MILESTONES.md M8 Q8b): Qt6::StateMachine
# INTERFACE-links Qt6::Gui (and transitively Qt6::DBus) at the Qt package
# level regardless of whether anything actually uses Gui symbols (wave-14
# Q7d finding) -- so as long as anything in the tree links the StateMachine
# Qt module, libQt6Gui/libQt6DBus stay in the ghost's link graph even in the
# headless/no-QML ("OFF") build. QGCStateMachine is the only consumer of
# Qt6::StateMachine in this repo (src/CMakeLists.txt's link line).
#
# Selected by QGC_ENABLE_QT_STATEMACHINE (cmake/CustomOptions.cmake):
#   ON  (default, matches upstream) -> this directory's original QState/
#                                       QStateMachine-based framework (90
#                                       files, full feature surface).
#   OFF (ghost-diet build)          -> portable/, a QObject-based
#                                       reimplementation of only the subset
#                                       actually used by the 5 real consumer
#                                       machines (InitialConnectStateMachine,
#                                       ComponentInformationManager,
#                                       RequestMetaDataTypeStateMachine,
#                                       ParameterManager's two ad-hoc
#                                       machines) -- see
#                                       tools/ghost/statemachine-port/
#                                       AUDIT.md for the full audit and
#                                       portable/QGCStateMachine.h's header
#                                       comment for the port's design.
#                                       Framework surface AUDIT.md found
#                                       unused by any consumer (parallel/
#                                       history/sub-machine states, event
#                                       posting, guarded/self-loop/internal
#                                       transitions, StateContext access,
#                                       property-restore, dot export,
#                                       dead-end analysis, ...) is dropped
#                                       entirely, not stubbed -- nothing in
#                                       the OFF build references it.
#
# Verified (Q8b): with QGC_ENABLE_QT_STATEMACHINE=OFF, `ldd` on the OFF
# ghost binary no longer lists libQt6StateMachine.so -- this dependency is
# fully eliminated. libQt6Gui.so/libQt6DBus.so are STILL present in that
# ldd output, but NOT because of this subsystem: src/Utilities/Geo/
# CMakeLists.txt's QGCGeoMath target links Qt6::Gui unconditionally (not
# gated by QGC_ENABLE_QML) because QGCGeo.cc/.h uses QVector3D (a Qt6::Gui
# type, despite being pure 3-float-vector math with no windowing/rendering
# dependency) for headless-reachable ENU/ECEF coordinate conversions. That
# is a separate, pre-existing dependency this port doesn't touch -- it was
# simply masked until now by Qt6::StateMachine's own Gui pull, so nobody
# had reason to notice or gate it. Eliminating it (e.g. swapping QVector3D
# for a small headless-safe Vector3 type in QGCGeo.h) is a distinct
# follow-up job, out of Q8b's scope.
#
# Selection is via a disjoint target_sources() list plus include-path
# shadowing (portable/ contains same-named headers, e.g. QGCStateMachine.h,
# WaitStateBase.h, ... so consumer `#include "QGCStateMachine.h"` resolves
# correctly either way with zero consumer-side #ifdef), not per-file
# #ifdef -- the 90-file ON tree is completely untouched, so upstream sync
# is unaffected, and the 34-file portable/ tree stays reviewable on its own.
# ============================================================================
if(QGC_ENABLE_QT_STATEMACHINE)
    target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
            # Core
            QGCStateMachine.cc
            QGCStateMachine.h
            QGCStateMachineEvent.cc
            QGCStateMachineEvent.h
            Helpers/StateContext.h
            Helpers/StateHistoryRecorder.cc
            Helpers/StateHistoryRecorder.h
            Helpers/StateMachineProfiler.cc
            Helpers/StateMachineProfiler.h
            Helpers/ErrorRecoveryBuilder.cc
            Helpers/ErrorRecoveryBuilder.h
            Helpers/ErrorHandlers.cc
            Helpers/ErrorHandlers.h
            Helpers/StateMachineLogger.cc
            Helpers/StateMachineLogger.h

            # States
            States/QGCAbstractState.cc
            States/QGCAbstractState.h
            States/QGCFinalState.cc
            States/QGCFinalState.h
            States/QGCHistoryState.cc
            States/QGCHistoryState.h
            States/ParallelState.cc
            States/ParallelState.h
            States/QGCState.cc
            States/QGCState.h
            States/AsyncFunctionState.cc
            States/AsyncFunctionState.h
            States/ConditionalState.cc
            States/ConditionalState.h
            States/DelayState.cc
            States/DelayState.h
            States/EventQueuedState.cc
            States/EventQueuedState.h
            States/FunctionState.cc
            States/FunctionState.h
            States/LoopState.h
            States/RetryState.cc
            States/RetryState.h
            States/FallbackChainState.cc
            States/FallbackChainState.h
            States/RollbackState.cc
            States/RollbackState.h
            States/CircuitBreakerState.cc
            States/CircuitBreakerState.h
            States/RequestMessageState.cc
            States/RequestMessageState.h
            States/SequenceState.cc
            States/SequenceState.h
            States/SendMavlinkCommandState.cc
            States/SendMavlinkCommandState.h
            States/SendMavlinkMessageState.cc
            States/SendMavlinkMessageState.h
            States/ShowAppMessageState.cc
            States/ShowAppMessageState.h
            States/SkippableAsyncState.cc
            States/SkippableAsyncState.h
            States/SubMachineState.cc
            States/SubMachineState.h
            States/WaitForMavlinkMessageState.cc
            States/WaitForMavlinkMessageState.h
            States/WaitForParamResponseState.cc
            States/WaitForParamResponseState.h
            States/WaitForSignalState.cc
            States/WaitForSignalState.h
            States/WaitStateBase.cc
            States/WaitStateBase.h
            States/ProgressState.cc
            States/ProgressState.h
            States/RetryableRequestMessageState.cc
            States/RetryableRequestMessageState.h

            # Transitions
            Transitions/QGCAbstractTransition.cc
            Transitions/QGCAbstractTransition.h
            Transitions/QGCSignalTransition.cc
            Transitions/QGCSignalTransition.h
            Transitions/GuardedTransition.cc
            Transitions/GuardedTransition.h
            Transitions/InternalTransition.cc
            Transitions/InternalTransition.h
            Transitions/MachineEventTransition.cc
            Transitions/MachineEventTransition.h
            Transitions/NamedEventTransition.cc
            Transitions/NamedEventTransition.h
            Transitions/QGCEventTransition.cc
            Transitions/QGCEventTransition.h
            Transitions/RetryTransition.cc
            Transitions/RetryTransition.h
            Transitions/SignalDataTransition.cc
            Transitions/SignalDataTransition.h
            Transitions/TimeoutTransition.cc
            Transitions/TimeoutTransition.h
    )

    target_include_directories(${CMAKE_PROJECT_NAME}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/Helpers
            ${CMAKE_CURRENT_SOURCE_DIR}/States
            ${CMAKE_CURRENT_SOURCE_DIR}/Transitions
    )
else()
    target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
            portable/AsyncFunctionState.cc
            portable/AsyncFunctionState.h
            portable/ErrorRecoveryBuilder.cc
            portable/ErrorRecoveryBuilder.h
            portable/FunctionState.cc
            portable/FunctionState.h
            portable/QGCAbstractState.cc
            portable/QGCAbstractState.h
            portable/QGCFinalState.cc
            portable/QGCFinalState.h
            portable/QGCState.cc
            portable/QGCState.h
            portable/QGCStateMachine.cc
            portable/QGCStateMachine.h
            portable/RetryableRequestMessageState.cc
            portable/RetryableRequestMessageState.h
            portable/RetryState.cc
            portable/RetryState.h
            portable/RetryTransition.cc
            portable/RetryTransition.h
            portable/SendMavlinkMessageState.cc
            portable/SendMavlinkMessageState.h
            portable/SkippableAsyncState.cc
            portable/SkippableAsyncState.h
            portable/StateHistoryRecorder.cc
            portable/StateHistoryRecorder.h
            portable/StateMachineLogger.cc
            portable/StateMachineLogger.h
            portable/StateMachineProfiler.cc
            portable/StateMachineProfiler.h
            portable/WaitForParamResponseState.cc
            portable/WaitForParamResponseState.h
            portable/WaitStateBase.cc
            portable/WaitStateBase.h
    )

    # portable/ is listed first so its same-named headers (QGCStateMachine.h,
    # WaitStateBase.h, ...) shadow ../Helpers, ../States, ../Transitions for
    # consumers' unqualified `#include "QGCStateMachine.h"` etc. Helpers/ is
    # still needed for StateContext.h, which portable/QGCStateMachine.h
    # includes unchanged (it never depended on QState/QStateMachine -- see
    # AUDIT.md's minimal-replacement design spec point 7).
    target_include_directories(${CMAKE_PROJECT_NAME}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/portable
            ${CMAKE_CURRENT_SOURCE_DIR}/Helpers
    )
endif()
