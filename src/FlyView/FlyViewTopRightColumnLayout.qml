import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView
import QGroundControl.FlightMap

ColumnLayout {
    spacing: ScreenTools.defaultFontPixelHeight / 2

    TerrainProgress {
        Layout.fillWidth: true
    }

    RowLayout {
        Layout.alignment: Qt.AlignRight
        spacing: ScreenTools.defaultFontPixelWidth * 0.75

        // We use a Loader to load the photoVideoControlComponent only when we have an active vehicle and a camera manager.
        // This make it easier to implement PhotoVideoControl without having to check for the mavlink camera
        // to be null all over the place
        Loader {
            id:                 photoVideoControlLoader
            sourceComponent:    globals.activeVehicle && globals.activeVehicle.cameraManager && globals.activeVehicle.cameraManager.cameras.count > 0 ? photoVideoControlComponent : undefined

            property real rightEdgeCenterInset: visible ? parent.width - x : 0

            Component {
                id: photoVideoControlComponent

                PhotoVideoControl {
                    camera: globals.activeVehicle.cameraManager.cameras.get(0)
                }
            }
        }

        Loader {
            id:                 photoVideoControlLoader2
            sourceComponent:    globals.activeVehicle && globals.activeVehicle.cameraManager && globals.activeVehicle.cameraManager.cameras.count > 1 ? photoVideoControlComponent2 : undefined

            property real rightEdgeCenterInset: visible ? parent.width - x : 0

            Component {
                id: photoVideoControlComponent2

                PhotoVideoControl {
                    camera: globals.activeVehicle.cameraManager.cameras.get(1)
                }
            }
        }

        Loader {
            id: telemetryPanelLoader
            sourceComponent: globals.activeVehicle && globals.activeVehicle.rover ? telemetryPanelComponent : undefined
            visible: status === Loader.Ready

            Component {
                id: telemetryPanelComponent
                CustomBoatTelemetryPanel { }
            }
        }
    }
}
