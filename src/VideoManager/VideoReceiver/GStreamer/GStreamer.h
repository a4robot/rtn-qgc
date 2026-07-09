#pragma once

class QQuickItem;
#ifdef QGC_ENABLE_QML
class QVideoSink;
#endif
class VideoReceiver;

namespace GStreamer
{

enum VideoDecoderOptions {
    ForceVideoDecoderDefault = 0,
    ForceVideoDecoderSoftware,
    ForceVideoDecoderNVIDIA,
    ForceVideoDecoderVAAPI,
    ForceVideoDecoderDirectX3D,
    ForceVideoDecoderVideoToolbox,
    ForceVideoDecoderIntel,
    ForceVideoDecoderVulkan,
    ForceVideoDecoderHardware
};

void prepareEnvironment();
bool initialize();
bool completeInit();
void setDebugLevel(int level);
void *createVideoSink(QQuickItem *widget, QObject *parent = nullptr);
void releaseVideoSink(void *sink);
VideoReceiver *createVideoReceiver(QObject *parent = nullptr);

#ifdef QGC_ENABLE_QML
/// Connect the appsink inside @p sinkBin to @p videoSink. Returns true on success.
/// QML-only: bridges GStreamer frames to a QVideoSink for QQuickVideoOutput rendering.
bool setupAppSinkAdapter(void *sinkBin, QVideoSink *videoSink, QObject *adapterParent);

/// Toggle every appsink adapter parented under @p adapterParent. Used to drop frames at
/// the appsink while the host window is hidden/minimized — saves CPU vs. running the
/// full decode→render path against a non-visible sink. Safe to call repeatedly; no-op
/// when no adapters exist.
void setAppSinkAdaptersActive(QObject *adapterParent, bool active);
#endif // QGC_ENABLE_QML

}
