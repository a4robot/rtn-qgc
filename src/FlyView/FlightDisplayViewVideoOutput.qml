import QtQuick
import QtMultimedia

import QGroundControl

VideoOutput {
    property string videoReceiverName: "videoContent"
    property var videoManager: QGroundControl.videoManager
    property var videoSettings: QGroundControl.settingsManager.videoSettings
    objectName: videoReceiverName

    // Do NOT set `orientation` here — VideoOutput composes orientation on top of the
    // QVideoFrame's own rotation()/mirrored() metadata that GstAppSinkAdapter forwards from
    // GstVideoOrientationMeta. Setting it would double-rotate any stream with orientation tags.

    // videoFit enum: 0=Fit Width, 1=Fit Height, 2=Fill, 3=No Crop. The container
    // handles fit-width/fit-height sizing; only Fill needs the cropping fillMode.
    fillMode: videoSettings.videoFit.rawValue === 2
              ? VideoOutput.PreserveAspectCrop
              : VideoOutput.PreserveAspectFit

    Connections {
        target: videoManager
        function onImageFileChanged(filename) {
            var targetSize = Qt.size(sourceRect.width, sourceRect.height);
            if (targetSize.width === 0 || targetSize.height === 0) {
                targetSize = Qt.size(1920, 1080);
            }
            grabToImage(function(result) {
                if (!result.saveToFile(filename)) {
                    console.error('Error capturing video frame');
                }
            }, targetSize);
        }
    }
}
