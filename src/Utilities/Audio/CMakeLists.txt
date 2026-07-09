qt_add_library(QGCAudio STATIC
    AudioOutput.cc
    AudioOutput.h
)

target_link_libraries(QGCAudio
    PUBLIC
        Qt6::Core
    PRIVATE
        QGCLogging
)

# Wave-12: opt-out via QGC_ENABLE_TEXTTOSPEECH (default ON — see
# cmake/CustomOptions.cmake). AudioOutput.cc guards its QTextToSpeech usage on
# the same QGC_ENABLE_TEXTTOSPEECH compile definition (set globally in root
# CMakeLists.txt) and degrades to a no-op when it's undefined, so this target
# and its public API surface (callers: QGCApplication, Vehicle,
# VehicleLinkManager, QGroundControlQmlGlobal) stay unchanged either way.
if(QGC_ENABLE_TEXTTOSPEECH)
    target_link_libraries(QGCAudio PRIVATE Qt6::TextToSpeech)
endif()

target_include_directories(QGCAudio
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/FactSystem
        ${CMAKE_SOURCE_DIR}/src/Utilities
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE QGCAudio)

# AudioOutput.cc was 3.4s frontend in build profile; absorbing the QtTextToSpeech
# + QObject (qfloat16->std::format chain) headers cuts most of it.
if(QGC_ENABLE_TEXTTOSPEECH)
    target_precompile_headers(QGCAudio PRIVATE
        <QtCore/QObject>
        <QtCore/QString>
        <QtTextToSpeech/QTextToSpeech>
    )
else()
    target_precompile_headers(QGCAudio PRIVATE
        <QtCore/QObject>
        <QtCore/QString>
    )
endif()
