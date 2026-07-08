target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        FactPanelController.cc
        FactPanelController.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# FactControlsModule holds only the Fact-bound editor widget QML — no C++
# sources of its own (FactPanelController above, the base class core code
# needs, is unconditional). Skip it entirely when headless so it doesn't
# pull Qt6::Qml/Quick.
if(QGC_ENABLE_QML)
    qt_add_library(FactControlsModule STATIC)

    qt_add_qml_module(FactControlsModule
        URI QGroundControl.FactControls
        VERSION 1.0
        RESOURCE_PREFIX /qml
        QML_FILES
            AltitudeFactTextField.qml
            FactBitMaskCheckBoxSlider.qml
            FactBitmask.qml
            FactCheckBox.qml
            FactCheckBoxSlider.qml
            FactComboBox.qml
            FactLabel.qml
            FactTextField.qml
            FactTextFieldGrid.qml
            FactTextFieldSlider.qml
            FactTextFieldSlider2.qml
            FactValueSlider.qml
            LabelledFactBrowse.qml
            LabelledFactComboBox.qml
            LabelledFactLabel.qml
            LabelledFactIncrementer.qml
            LabelledFactTextField.qml
        NO_PLUGIN
    )
endif()
