import QtQuick
import QtMultimedia

import QGroundControl

VideoOutput {
    objectName: "videoContent"

    // Do NOT set `orientation` here — VideoOutput composes orientation on top of the
    // QVideoFrame's own rotation()/mirrored() metadata that qgcqvideosink forwards from
    // GstVideoOrientationMeta. Setting it would double-rotate any stream with orientation tags.

    // Set by a container that is already sized to the display aspect ratio of the stream (pixel aspect ratio
    // included, or the whole window when the video is stretched): the frame is then stretched into it.
    property bool stretchToContainer: false

    // videoFit enum: 0=Fit Width, 1=Fit Height, 2=Fill, 3=No Crop. The container
    // handles fit-width/fit-height sizing; only Fill needs the cropping fillMode.
    fillMode: stretchToContainer
              ? VideoOutput.Stretch
              : (QGroundControl.settingsManager.videoSettings.videoFit.rawValue === 2
                 ? VideoOutput.PreserveAspectCrop
                 : VideoOutput.PreserveAspectFit)

    Connections {
        target: QGroundControl.videoManager
        function onImageFileChanged(filename) {
            grabToImage(function(result) {
                if (!result.saveToFile(filename)) {
                    console.error('Error capturing video frame');
                }
            });
        }
    }
}
