# ============================================================================
# Actuators Subsystem
# Motor, servo, and actuator configuration and testing
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        ActuatorActions.cc
        ActuatorActions.h
        ActuatorOutputs.cc
        ActuatorOutputs.h
        Actuators.cc
        Actuators.h
        ActuatorTesting.cc
        ActuatorTesting.h
        Common.cc
        Common.h
        Mixer.cc
        Mixer.h
        MotorAssignment.cc
        MotorAssignment.h
)

# VehicleGeometryImageProvider is a QQuickImageProvider (renders the airframe
# geometry picture for ActuatorComponent.qml / APMMotorComponent.qml, both
# QML-only). Its headless callers — Actuators::imageClicked/updateGeometryImage/
# initMotorAssignment/highlightActuators and ActuatorComponent's constructor —
# are QML-invoked/UI-only entry points reachable only from those .qml files,
# so they're guarded with QGC_ENABLE_QML at the call site instead of dragging
# this class into headless builds.
if(QGC_ENABLE_QML)
    target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
            GeometryImage.cc
            GeometryImage.h
    )
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
