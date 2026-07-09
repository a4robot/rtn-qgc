# ============================================================================
# QML Controls Module
# Reusable QML UI controls and controllers
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        FactValueGridModel.cc
        FactValueGridModel.h
        FlightPathSegment.cc
        FlightPathSegment.h
        InstrumentValueData.cc
        InstrumentValueData.h
        MavlinkAction.cc
        MavlinkAction.h
        MavlinkActionManager.cc
        MavlinkActionManager.h
        ObjectItemModelBase.cc
        ObjectItemModelBase.h
        ObjectListModelBase.cc
        ObjectListModelBase.h
        ParameterEditorController.cc
        ParameterEditorController.h
        QGCFenceCircle.cc
        QGCFenceCircle.h
        QGCFencePolygon.cc
        QGCFencePolygon.h
        QGCFileDialogController.cc
        QGCFileDialogController.h
        QGCGeoBoundingCube.cc
        QGCGeoBoundingCube.h
        QGCMapCircle.cc
        QGCMapCircle.h
        QGCMapPolygon.cc
        QGCMapPolygon.h
        QGCMapPolyline.cc
        QGCMapPolyline.h
        QGCQGeoCoordinate.cc
        QGCQGeoCoordinate.h
        QGroundControlQmlGlobal.cc
        QGroundControlQmlGlobal.h
        QmlObjectListModel.cc
        QmlObjectListModel.h
        QmlObjectTreeModel.cc
        QmlObjectTreeModel.h
        QmlUnitsConversion.h
        RCChannelMonitorController.cc
        RCChannelMonitorController.h
        RCToParamDialogController.cc
        RCToParamDialogController.h
        TransformPositionController.cc
        TransformPositionController.h
        ServoOutputMonitorController.cc
        ServoOutputMonitorController.h
)

# TerrainProfile is a QQuickItem (chart renderer for PlanView/TerrainStatus.qml,
# which is itself QML-only) — unlike the rest of this directory, it has no
# headless caller (MissionController::recalcTerrainProfile is just a signal
# name; it doesn't reference this class). It's also the only non-AnalyzeView
# user of QtGraphs, so gating it lets Qt6::Graphs itself be gated below.
#
# FactValueGrid/HorizontalFactValueGrid are the QQuickItem view classes for
# FactValueGridModel (unconditional, above): FactValueGrid just adds QML type
# registration on top of the model, and HorizontalFactValueGrid is the
# QML-only horizontal layout view used by HorizontalFactValueGrid.qml.
# Headless callers (SubtitleWriter, QGCCorePlugin) use FactValueGridModel
# directly so they don't need either of these.
#
# QGCImageProvider/ColoredSvgImageProvider inherit QQuickImageProvider and
# only exist to be registered with the QQmlApplicationEngine (see
# QGCApplication::_initForNormalAppBoot, itself QGC_ENABLE_QML-only). Their
# one headless caller, Vehicle::_createImageProtocolManager, guards its use
# of QGCApplication::qgcImageProvider() the same way.
#
# ScreenToolsController (Q7d): QML_SINGLETON registered with the QML engine only
# (QGCApplication::_initForNormalAppBoot); its one headless-looking reference
# (WebBridge/GuidedActionGate.cc) is a comment, not a call. It was previously
# unconditional here by oversight -- that's real (not just cosmetic): it calls
# qgcApp()->fakeMobile() via <QtGui/QCursor>'s transitive QGuiApplication
# machinery, which only resolves against a real QGuiApplication instance, so it
# fails to build once QGCApplication stops deriving from QGuiApplication in OFF
# builds (see Q7d probe, QGCApplication.h QGCApplicationBase alias).
#
# QGCPalette/QGCMapPalette (Q8g, wave 17): the ~40-entry QColor theme table is
# pure QML-rendering config (light/dark UI theming) -- QColor lives in
# Qt6::Gui, and QGCPalette's many Q_PROPERTY(QColor ...) force moc to emit
# code needing it regardless of whether _buildMap() ever runs. Their two
# headless-compiled callers were adjusted to not need the type:
# AppSettings::_indoorPaletteChanged/DECLARE_SETTINGGROUP(App) guard the
# QGCPalette::setGlobalTheme() call with #ifdef QGC_ENABLE_QML (that call
# doesn't touch QColor itself, but pulls this whole translation unit's
# QColor-heavy code in at .o granularity), QGCCorePlugin::paletteOverride is
# QGC_ENABLE_QML-gated to match, and QGroundControlQmlGlobal (a genuine
# headless dependency of MissionManager) only constructs its _globalPalette
# member under the same guard.
if(QGC_ENABLE_QML)
    target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
            ColoredSvgImageProvider.cc
            ColoredSvgImageProvider.h
            FactValueGrid.cc
            FactValueGrid.h
            HorizontalFactValueGrid.cc
            HorizontalFactValueGrid.h
            QGCImageProvider.cc
            QGCImageProvider.h
            QGCMapPalette.cc
            QGCMapPalette.h
            QGCPalette.cc
            QGCPalette.h
            ScreenToolsController.cc
            ScreenToolsController.h
            TerrainProfile.cc
            TerrainProfile.h
            ToolStripAction.cc
            ToolStripAction.h
            ToolStripActionList.cc
            ToolStripActionList.h
    )
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ----------------------------------------------------------------------------
# QML Controls Module
# ----------------------------------------------------------------------------
# QGroundControlControlsModule holds only the reusable QML widget/dialog
# library — no C++ sources of its own (the controller/model classes above,
# which core code needs, are unconditional target_sources on the exe). Skip
# it entirely when headless so it doesn't pull Qt6::Qml/Quick.
if(QGC_ENABLE_QML)
qt_add_library(QGroundControlControlsModule STATIC)

set_source_files_properties(ScreenTools.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(QGroundControlControlsModule
    URI QGroundControl.Controls
    VERSION 1.0
    RESOURCE_PREFIX /qml
    QML_FILES
        AltFrameCombo.qml
        AltFrameDialog.qml
        AppSettings.qml
        APMSubMotorDisplay.qml
        AutotuneUI.qml
        AxisMonitor.qml
        ClickableColor.qml
        ConfigButton.qml
        ConfigSection.qml
        DeadMouseArea.qml
        DropButton.qml
        DropPanel.qml
        EditPositionDialog.qml
        ExclusiveGroupItem.qml
        FactSlider.qml
        FileButton.qml
        FlightModeMenu.qml
        HeightIndicator.qml
        HorizontalFactValueGrid.qml
        IndicatorButton.qml
        InstrumentValueEditDialog.qml
        InstrumentValueLabel.qml
        InstrumentValueValue.qml
        JoystickThumbPad.qml
        KMLOrSHPFileDialog.qml
        LabelledButton.qml
        LabelledComboBox.qml
        LabelledLabel.qml
        LogReplayStatusBar.qml
        MissionCommandDialog.qml
        MvPanelPage.qml
        OfflineMapButton.qml
        ParameterDiffDialog.qml
        ParameterEditor.qml
        ParameterEditorDialog.qml
        PIDTuning.qml
        PipState.qml
        PipView.qml
        PreFlightCheckButton.qml
        PreFlightCheckGroup.qml
        PreFlightCheckModel.qml
        QGCButton.qml
        QGCCheckBox.qml
        QGCCheckBoxSlider.qml
        QGCColoredImage.qml
        QGCColumnButton.qml
        QGCComboBox.qml
        QGCDelayButton.qml
        QGCDynamicObjectManager.qml
        QGCFileDialog.qml
        QGCFlickable.qml
        QGCFlickableScrollIndicator.qml
        QGCGroupBox.qml
        QGCLabel.qml
        QGCListView.qml
        QGCMapLabel.qml
        QGCMarqueeLabel.qml
        QGCMenu.qml
        QGCMenuItem.qml
        QGCMenuSeparator.qml
        QGCMouseArea.qml
        QGCMovableItem.qml
        QGCOptionsComboBox.qml
        QGCPageIndicator.qml
        QGCPopupDialog.qml
        QGCPopupDialogFactory.qml
        QGCRadioButton.qml
        QGCRoundButton.qml
        QGCSimpleMessageDialog.qml
        QGCSlider.qml
        QGCSwipeView.qml
        QGCTabBar.qml
        QGCTabButton.qml
        QGCTextField.qml
        QGCToolBarButton.qml
        QGCToolInsets.qml
        QGCVectorImage.qml
        RCChannelMonitor.qml
        RCToParamDialog.qml
        SectionHeader.qml
        SelectableControl.qml
        SettingsButton.qml
        SettingsGroupLayout.qml
        SetupPage.qml
        SliderSwitch.qml
        SubMenuButton.qml
        TimedProgressTracker.qml
        ToolIndicatorPage.qml
        ToolStrip.qml
        ToolStripDropPanel.qml
        ToolStripHoverButton.qml
        ValueSlider.qml
        VehicleRotationCal.qml
        VehicleSummaryRow.qml

# NOTE: ScreenTools.qml is declared here because CMake's
# set_source_files_properties() has directory scope. The QT_QML_SINGLETON_TYPE
# property must be visible in the same CMakeLists.txt that calls
# qt_add_qml_module(). On Windows this cross-directory property propagation
# fails, so this file is kept in this module until a better solution is available.

        ScreenTools.qml
    NO_PLUGIN
)
endif() # QGC_ENABLE_QML

# ----------------------------------------------------------------------------
# Control Configuration Resources
# ----------------------------------------------------------------------------
file(GLOB_RECURSE JSON_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*.json)
qt_add_resources(${CMAKE_PROJECT_NAME} json_controls
    PREFIX "/json"
    FILES ${JSON_FILES}
)

# Remaining resources are embedded into QGroundControlControlsModule itself
# (QML icons/images for the widget library above), so they only exist when
# that module does. Note: FactValueGridModel (core, unconditional) enumerates
# the :/InstrumentValueIcons/ prefix at runtime to build its icon picker — in a
# headless (QGC_ENABLE_QML=OFF) build that enumeration is simply empty, which
# is fine since there is no QML icon-picker UI to populate.
if(QGC_ENABLE_QML)
qt_add_resources(QGroundControlControlsModule qmlcontrols_resources
    PREFIX "/res"
    BASE "${CMAKE_SOURCE_DIR}/resources"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/ArrowRight.svg"
        "${CMAKE_SOURCE_DIR}/resources/buttonRight.svg"
        "${CMAKE_SOURCE_DIR}/resources/chevron-down.svg"
        "${CMAKE_SOURCE_DIR}/resources/chevron-up.svg"
        "${CMAKE_SOURCE_DIR}/resources/clockwise-arrow.svg"
        "${CMAKE_SOURCE_DIR}/resources/counter-clockwise-arrow.svg"
        "${CMAKE_SOURCE_DIR}/resources/JoystickBezelLight.png"
)

# subMenuButtonImage.png aliases the shared CogWheels.png at the repo root.
set_source_files_properties("${CMAKE_SOURCE_DIR}/resources/CogWheels.png"
    PROPERTIES QT_RESOURCE_ALIAS "subMenuButtonImage.png")

qt_add_resources(QGroundControlControlsModule qmlcontrols_qmlimages
    PREFIX "/qmlimages"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/arrow-down.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/checkbox-check.svg"
        "${CMAKE_SOURCE_DIR}/resources/CogWheels.png"
)

# Globbed because FactValueGridModel enumerates :/InstrumentValueIcons/ at
# runtime to build the icon picker — every SVG in the directory must be embedded.
file(GLOB INSTRUMENT_VALUE_ICONS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/resources/InstrumentValueIcons/*.svg"
)
qt_add_resources(QGroundControlControlsModule instrument_value_icons
    PREFIX "/InstrumentValueIcons"
    BASE "${CMAKE_SOURCE_DIR}/resources/InstrumentValueIcons"
    FILES ${INSTRUMENT_VALUE_ICONS}
)
endif() # QGC_ENABLE_QML
