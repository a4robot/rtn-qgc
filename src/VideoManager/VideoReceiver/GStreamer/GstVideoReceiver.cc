//-----------------------------------------------------------------------------
// Our pipeline look like this:
//
//              +-->queue-->_decoderValve[-->_decoder-->_videoSink]
//              |
// _source-->_tee
//              |
//              +-->queue-->_recorderValve[-->_fileSink]
//-----------------------------------------------------------------------------

#include "GstVideoReceiver.h"

#if defined(QGC_HAS_GST_D3D11_GPU_PATH)
#include "HwBuffers/GstD3D11ContextBridge.h"
#endif
#if defined(QGC_HAS_GST_D3D12_GPU_PATH)
#include "HwBuffers/GstD3D12ContextBridge.h"
#endif
#include "GStreamerHelpers.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QDateTime>
#include <QtCore/QUrl>
#include <QtQuick/QQuickItem>
#include <thread>

#include <gst/gst.h>
#include <gst/video/video.h>

QGC_LOGGING_CATEGORY(GstVideoReceiverLog, "Video.GStreamer.GstVideoReceiver")

#if defined(QGC_HAS_GST_GLMEMORY_GPU_PATH) || defined(QGC_HAS_GST_D3D11_GPU_PATH) || defined(QGC_HAS_GST_D3D12_GPU_PATH)
#include "HwBuffers/GstContextBridgeRegistry.h"
namespace {
GstBusSyncReply _contextSyncDispatch(GstBus * /*bus*/, GstMessage *message, gpointer /*data*/)
{
    return GstContextBridgeRegistry::dispatchBridges(message);
}
} // namespace
#endif

GstVideoReceiver::GstVideoReceiver(QObject *parent)
    : VideoReceiver(parent)
    , _worker(new GstVideoWorker(this))
{
    qCDebug(GstVideoReceiverLog) << this;

    _worker->start();
    (void) connect(&_watchdogTimer, &QTimer::timeout, this, &GstVideoReceiver::_watchdog);
}

GstVideoReceiver::~GstVideoReceiver()
{
    stop();
    _worker->shutdown();

    qCDebug(GstVideoReceiverLog) << this;
}

void GstVideoReceiver::start(uint32_t timeout)
{
    if (_needDispatch()) {
        _worker->dispatch([this, timeout]() { start(timeout); });
        return;
    }

    if (_pipeline) {
        qCDebug(GstVideoReceiverLog) << "Already running!" << _uri;
        _dispatchSignal([this]() { emit onStartComplete(STATUS_INVALID_STATE); });
        return;
    }

    if (_uri.isEmpty()) {
        qCDebug(GstVideoReceiverLog) << "Failed because URI is not specified";
        _dispatchSignal([this]() { emit onStartComplete(STATUS_INVALID_URL); });
        return;
    }

    _timeout = timeout;
    _buffer = lowLatency() ? -1 : 0;

    qCDebug(GstVideoReceiverLog) << "Starting" << _uri << ", lowLatency" << lowLatency() << ", timeout" << _timeout;

    _endOfStream = false;

    bool running = false;
    bool pipelineUp = false;

    GstElement *decoderQueue = nullptr;
    GstElement *recorderQueue = nullptr;

    do {
        _tee = gst_element_factory_make("tee", nullptr);
        if (!_tee)  {
            qCCritical(GstVideoReceiverLog) << "gst_element_factory_make('tee') failed";
            break;
        }

        GstPad *pad = gst_element_get_static_pad(_tee, "sink");
        if (!pad) {
            qCCritical(GstVideoReceiverLog) << "gst_element_get_static_pad() failed";
            break;
        }

        _lastSourceFrameTime = 0;

        _teeProbeId = gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, _teeProbe, this, nullptr);
        gst_clear_object(&pad);
        if (_teeProbeId == 0) {
            // _teeProbe updates _lastSourceFrameTime; without it the watchdog timer fires spuriously instead of reporting a real failure.
            qCCritical(GstVideoReceiverLog) << "gst_pad_add_probe(_teeProbe) failed";
            break;
        }

        decoderQueue = gst_element_factory_make("queue", nullptr);
        if (!decoderQueue)  {
            qCCritical(GstVideoReceiverLog) << "gst_element_factory_make('queue') failed";
            break;
        }

        _decoderValve = gst_element_factory_make("valve", nullptr);
        if (!_decoderValve)  {
            qCCritical(GstVideoReceiverLog) << "gst_element_factory_make('valve') failed";
            break;
        }

        g_object_set(_decoderValve,
                     "drop", TRUE,
                     nullptr);

        recorderQueue = gst_element_factory_make("queue", nullptr);
        if (!recorderQueue)  {
            qCCritical(GstVideoReceiverLog) << "gst_element_factory_make('queue') failed";
            break;
        }

        // Make the recorder queue leaky. If the SD card is slow or the muxer errors out,
        // the queue will drop old buffers rather than blocking the tee (which would freeze
        // the live video display and crash the stream).
        g_object_set(recorderQueue,
                     "leaky", 2, // 2 = downstream (drop oldest buffers)
                     "max-size-time", (guint64)(3 * GST_SECOND),
                     "max-size-buffers", 0,
                     "max-size-bytes", 0,
                     nullptr);

        _recorderValve = gst_element_factory_make("valve", nullptr);
        if (!_recorderValve) {
            qCCritical(GstVideoReceiverLog) << "gst_element_factory_make('valve') failed";
            break;
        }

        g_object_set(_recorderValve,
                     "drop", TRUE,
                     nullptr);

        _pipeline = gst_pipeline_new("receiver");
        if (!_pipeline) {
            qCCritical(GstVideoReceiverLog) << "gst_pipeline_new() failed";
            break;
        }

        g_object_set(_pipeline,
                     "message-forward", TRUE,
                     nullptr);

        _source = _makeSource(_uri);
        if (!_source) {
            qCCritical(GstVideoReceiverLog) << "_makeSource() failed";
            break;
        }

        gst_bin_add_many(GST_BIN(_pipeline), _source, _tee, decoderQueue, _decoderValve, recorderQueue, _recorderValve, nullptr);

        pipelineUp = true;

        GstPad *srcPad = nullptr;
        GstIterator *it = gst_element_iterate_src_pads(_source);
        GValue vpad = G_VALUE_INIT;
        switch (gst_iterator_next(it, &vpad)) {
            case GST_ITERATOR_OK:
                srcPad = GST_PAD(g_value_get_object(&vpad));
                (void) gst_object_ref(srcPad);
                (void) g_value_reset(&vpad);
                break;
            case GST_ITERATOR_RESYNC:
                gst_iterator_resync(it);
                break;
            default:
                break;
        }
        g_value_unset(&vpad);
        gst_iterator_free(it);

        if (srcPad) {
            _onNewSourcePad(srcPad);
            gst_clear_object(&srcPad);
        } else {
            (void) g_signal_connect(_source, "pad-added", G_CALLBACK(_onNewPad), this);
        }

        if (!gst_element_link_many(_tee, decoderQueue, _decoderValve, nullptr)) {
            qCCritical(GstVideoReceiverLog) << "Unable to link decoder queue";
            break;
        }

        if (!gst_element_link_many(_tee, recorderQueue, _recorderValve, nullptr)) {
            qCCritical(GstVideoReceiverLog) << "Unable to link recorder queue";
            break;
        }

        // Block upstream RECONFIGURE events on both valve sink pads.
        //
        // Without this, caps renegotiation inside decodebin3/videoSink (decoder branch)
        // or mp4mux (recorder branch) sends GST_EVENT_RECONFIGURE backward through the tee
        // and into the source (parsebin/rtspsrc). The source then re-emits "pad-added",
        // which triggers another gst_element_link(_source, _tee) call. That second link
        // attempt fails ("gst_element_link_pads() failed"), corrupts the pipeline state,
        // and causes the entire stream + recording to stop.
        //
        // The valves are the natural chokepoints: all upstream events from decoder or
        // recorder branches pass through their respective valve sink pads before reaching
        // the shared tee/source. Dropping RECONFIGURE here is safe — it only affects
        // downstream caps re-negotiation, which should never need to ripple back into a
        // live source that is already streaming.
        static auto dropReconfigure = [](GstPad*, GstPadProbeInfo *info, gpointer) -> GstPadProbeReturn {
            if (GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info)) == GST_EVENT_RECONFIGURE) {
                return GST_PAD_PROBE_DROP;
            }
            return GST_PAD_PROBE_OK;
        };

        GstPad *decoderValveSink = gst_element_get_static_pad(_decoderValve, "sink");
        if (decoderValveSink) {
            gst_pad_add_probe(decoderValveSink, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
                              dropReconfigure, nullptr, nullptr);
            gst_object_unref(decoderValveSink);
        }

        GstPad *recorderValveSink = gst_element_get_static_pad(_recorderValve, "sink");
        if (recorderValveSink) {
            gst_pad_add_probe(recorderValveSink, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
                              dropReconfigure, nullptr, nullptr);
            gst_object_unref(recorderValveSink);
        }

        GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(_pipeline));
        if (bus) {
            gst_bus_enable_sync_message_emission(bus);
            (void) g_signal_connect(bus, "sync-message", G_CALLBACK(_onBusMessage), this);
#if defined(QGC_HAS_GST_GLMEMORY_GPU_PATH) || defined(QGC_HAS_GST_D3D11_GPU_PATH) || defined(QGC_HAS_GST_D3D12_GPU_PATH)
            // Single sync dispatcher chains every compiled context bridge so
            // they don't clobber each other via gst_bus_set_sync_handler. Must
            // run before GST_STATE_PLAYING — upstream queries context during
            // PAUSED→PLAYING. Each bridge cheap-rejects messages it doesn't
            // serve, so total cost on irrelevant messages is a strcmp.
            gst_bus_set_sync_handler(bus, _contextSyncDispatch, nullptr, nullptr);
#endif
            gst_clear_object(&bus);
        }

        GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-initial");
        running = (gst_element_set_state(_pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
    } while(0);

    if (!running) {
        qCCritical(GstVideoReceiverLog) << "Failed";

        if (_pipeline) {
            (void) gst_element_set_state(_pipeline, GST_STATE_NULL);
            (void) gst_element_get_state(_pipeline, nullptr, nullptr, GST_CLOCK_TIME_NONE);
            gst_clear_object(&_pipeline);
        }

        if (!pipelineUp) {
            gst_clear_object(&_recorderValve);
            gst_clear_object(&recorderQueue);
            gst_clear_object(&_decoderValve);
            gst_clear_object(&decoderQueue);
            gst_clear_object(&_tee);
            gst_clear_object(&_source);
        }

        // Rate limit restarts on failure. This sleep is OK because we're in the video worker thread.
        QThread::sleep(1);
        _dispatchSignal([this]() { emit onStartComplete(STATUS_FAIL); });
    } else {
        GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-started");
        qCDebug(GstVideoReceiverLog) << "Started" << _uri;

        // _watchdogTimer lives on `this` (GUI thread); _dispatchSignal runs synchronously on the
        // worker thread, so the timer start has to be queued separately or QObject warns.
        QMetaObject::invokeMethod(this, [this]() { _watchdogTimer.start(1000); }, Qt::QueuedConnection);
        _dispatchSignal([this]() { emit onStartComplete(STATUS_OK); });
    }
}

void GstVideoReceiver::stop()
{
    if (_needDispatch()) {
        _worker->dispatch([this]() { stop(); });
        return;
    }

    if (_uri.isEmpty()) {
        qCDebug(GstVideoReceiverLog) << "Stop called on empty URI (no-op)";
        return;
    }

    qCDebug(GstVideoReceiverLog) << "Stopping" << _uri;

    QMetaObject::invokeMethod(this, [this]() { _watchdogTimer.stop(); }, Qt::QueuedConnection);

    if (_teeProbeId != 0) {
        if (_tee) {
            GstPad *sinkpad = gst_element_get_static_pad(_tee, "sink");
            if (sinkpad) {
                gst_pad_remove_probe(sinkpad, _teeProbeId);
                gst_clear_object(&sinkpad);
            }
        }
        _teeProbeId = 0;
    }

    if (_pipeline) {
        GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(_pipeline));
        if (bus) {
            gst_bus_disable_sync_message_emission(bus);
            (void) g_signal_handlers_disconnect_by_data(bus, this);

            gboolean recordingValveClosed = TRUE;
            g_object_get(_recorderValve, "drop", &recordingValveClosed, nullptr);

            if (!recordingValveClosed) {
                (void) gst_element_send_event(_pipeline, gst_event_new_eos());

                GstMessage *msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, (GstMessageType)(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
                if (msg) {
                    switch (GST_MESSAGE_TYPE(msg)) {
                    case GST_MESSAGE_EOS:
                        qCDebug(GstVideoReceiverLog) << "End of stream received!";
                        break;
                    case GST_MESSAGE_ERROR:
                        qCCritical(GstVideoReceiverLog) << "Error stopping pipeline!";
                        break;
                    default:
                        break;
                    }

                    gst_clear_message(&msg);
                } else {
                    qCCritical(GstVideoReceiverLog) << "gst_bus_timed_pop_filtered() failed";
                }
            }

            gst_clear_object(&bus);
        } else {
            qCCritical(GstVideoReceiverLog) << "gst_pipeline_get_bus() failed";
        }

        (void) gst_element_set_state(_pipeline, GST_STATE_NULL);
        (void) gst_element_get_state(_pipeline, nullptr, nullptr, GST_CLOCK_TIME_NONE);

        // FIXME: check if branch is connected and remove all elements from branch
        if (_fileSink) {
           _shutdownRecordingBranch();
        }

        if (_videoSink) {
            _shutdownDecodingBranch();
        }

        GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-stopped");

        gst_clear_object(&_pipeline);
        _pipeline = nullptr;

        _recorderValve = nullptr;
        _decoderValve = nullptr;
        _tee = nullptr;
        _source = nullptr;

        _lastSourceFrameTime = 0;

        if (_streaming) {
            _streaming = false;
            qCDebug(GstVideoReceiverLog) << "Streaming stopped" << _uri;
            _dispatchSignal([this]() { emit streamingChanged(_streaming); });
        } else {
            qCDebug(GstVideoReceiverLog) << "Streaming did not start" << _uri;
        }
    }

    qCDebug(GstVideoReceiverLog) << "Stopped" << _uri;

    _dispatchSignal([this]() { emit onStopComplete(STATUS_OK); });
}

void GstVideoReceiver::startDecoding(void *sink)
{
    if (!sink) {
        qCCritical(GstVideoReceiverLog) << "VideoSink is NULL" << _uri;
        return;
    }

    if (_needDispatch()) {
        _worker->dispatch([this, sink]() mutable { startDecoding(sink); });
        return;
    }

    qCDebug(GstVideoReceiverLog) << "Starting decoding" << _uri;

    if (!_widget) {
        qCDebug(GstVideoReceiverLog) << "Video Widget is NULL" << _uri;
        _dispatchSignal([this]() { emit onStartDecodingComplete(STATUS_FAIL); });
        return;
    }

    if (!_pipeline) {
        gst_clear_object(&_videoSink);
    }

    if (_videoSink || _decoding) {
        qCDebug(GstVideoReceiverLog) << "Already decoding!" << _uri;
        _dispatchSignal([this]() { emit onStartDecodingComplete(STATUS_INVALID_STATE); });
        return;
    }

    GstElement *videoSink = GST_ELEMENT(sink);
    GstPad *pad = gst_element_get_static_pad(videoSink, "sink");
    if (!pad) {
        qCCritical(GstVideoReceiverLog) << "Unable to find sink pad of video sink" << _uri;
        _dispatchSignal([this]() { emit onStartDecodingComplete(STATUS_FAIL); });
        return;
    }

    _lastVideoFrameTime = 0;
    _resetVideoSink = true;

    _videoSinkProbeId = gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, _videoSinkProbe, this, nullptr);
    gst_clear_object(&pad);

    _videoSink = videoSink;
    gst_object_ref(_videoSink);

    _removingDecoder = false;

    if (!_streaming) {
        _dispatchSignal([this]() { emit onStartDecodingComplete(STATUS_OK); });
        return;
    }

    _ensureVideoSinkInPipeline();

    if (!_addDecoder(_decoderValve)) {
        qCCritical(GstVideoReceiverLog) << "_addDecoder() failed" << _uri;
        _shutdownDecodingBranch();
        _dispatchSignal([this]() { emit onStartDecodingComplete(STATUS_FAIL); });
        return;
    }

    g_object_set(_decoderValve,
                 "drop", FALSE,
                 nullptr);

    qCDebug(GstVideoReceiverLog) << "Decoding started" << _uri;

    _dispatchSignal([this]() { emit onStartDecodingComplete(STATUS_OK); });
}

void GstVideoReceiver::stopDecoding()
{
    if (_needDispatch()) {
        _worker->dispatch([this]() { stopDecoding(); });
        return;
    }

    qCDebug(GstVideoReceiverLog) << "Stopping decoding" << _uri;

    // Gate on _videoSink (set by startDecoding) instead of _decoding (which only flips on
    // first sink-buffer probe). Without this, stopDecoding() called between
    // onStartDecodingComplete(OK) and the first frame returns STATUS_INVALID_STATE and
    // leaves the decoder/sink branch live.
    if (!_pipeline || !_videoSink) {
        qCDebug(GstVideoReceiverLog) << "Not decoding!" << _uri;
        _dispatchSignal([this]() { emit onStopDecodingComplete(STATUS_INVALID_STATE); });
        return;
    }

    g_object_set(_decoderValve,
                 "drop", TRUE,
                 nullptr);

    _removingDecoder = true;

    const bool ret = _unlinkBranch(_decoderValve);

    // FIXME: it is much better to emit onStopDecodingComplete() after decoding is really stopped
    // (which happens later due to async design) but as for now it is also not so bad...
    _dispatchSignal([this, ret](){ emit onStopDecodingComplete(ret ? STATUS_OK : STATUS_FAIL); });
}

void GstVideoReceiver::startRecording(const QString &videoFile, FILE_FORMAT format)
{
    if (_needDispatch()) {
        const QString cachedVideoFile = videoFile;
        _worker->dispatch([this, cachedVideoFile, format]() { startRecording(cachedVideoFile, format); });
        return;
    }

    qCDebug(GstVideoReceiverLog) << "Starting recording" << _uri;

    if (!_pipeline) {
        qCDebug(GstVideoReceiverLog) << "Streaming is not active!" << _uri;
        _dispatchSignal([this](){ emit onStartRecordingComplete(STATUS_INVALID_STATE); });
        return;
    }

    if (_recording) {
        qCDebug(GstVideoReceiverLog) << "Already recording!" << _uri;
        _dispatchSignal([this]() { emit onStartRecordingComplete(STATUS_INVALID_STATE); });
        return;
    }

    qCDebug(GstVideoReceiverLog) << "New video file:" << videoFile << _uri;

    _fileSink = _makeFileSink(videoFile, format);
    if (!_fileSink) {
        qCCritical(GstVideoReceiverLog) << "_makeFileSink() failed" << _uri;
        _dispatchSignal([this]() { emit onStartRecordingComplete(STATUS_FAIL); });
        return;
    }

    _removingRecorder = false;

    (void) gst_object_ref(_fileSink);

    gst_bin_add(GST_BIN(_pipeline), _fileSink);

    if (!gst_element_link(_recorderValve, _fileSink)) {
        qCCritical(GstVideoReceiverLog) << "Failed to link valve and file sink" << _uri;
        _dispatchSignal([this]() { emit onStartRecordingComplete(STATUS_FAIL); });
        return;
    }

    (void) gst_element_sync_state_with_parent(_fileSink);

    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-with-filesink");

    // Install a probe on the recording branch to drop buffers until we hit our first keyframe
    // When we hit our first keyframe, we can offset the timestamps appropriately according to the first keyframe time
    // This will ensure the first frame is a keyframe at t=0, and decoding can begin immediately on playback
    GstPad *probepad = gst_element_get_static_pad(_recorderValve, "src");
    if (!probepad) {
        qCCritical(GstVideoReceiverLog) << "gst_element_get_static_pad() failed" << _uri;
        _dispatchSignal([this]() { emit onStartRecordingComplete(STATUS_FAIL); });
        return;
    }

    _keyframeWatchId = gst_pad_add_probe(probepad, GST_PAD_PROBE_TYPE_BUFFER, _keyframeWatch, this, nullptr);
    gst_clear_object(&probepad);

    g_object_set(_recorderValve,
                 "drop", FALSE,
                 nullptr);

    _recordingOutput = videoFile;
    _recording = true;
    qCDebug(GstVideoReceiverLog) << "Recording started" << _uri;
    _dispatchSignal([this]() {
        emit onStartRecordingComplete(STATUS_OK);
        emit recordingChanged(_recording);
    });
}

void GstVideoReceiver::stopRecording()
{
    if (_needDispatch()) {
        _worker->dispatch([this]() { stopRecording(); });
        return;
    }

    qCDebug(GstVideoReceiverLog) << "Stopping recording" << _uri;

    if (!_pipeline || !_recording) {
        qCDebug(GstVideoReceiverLog) << "Not recording!" << _uri;
        _dispatchSignal([this]() { emit onStopRecordingComplete(STATUS_INVALID_STATE); });
        return;
    }

    // If we are still waiting for the first keyframe (we haven't written anything to the file),
    // we can immediately shut down the recording branch instead of sending an EOS that will
    // get stuck because caps were not negotiated yet.
    if (_keyframeWatchId != 0) {
        qCDebug(GstVideoReceiverLog) << "Stop recording requested before first keyframe. Shutting down immediately.";
        _recordingStopRequested = true;
        _shutdownRecordingBranch();
        return;
    }

    // Check if the recorder valve is already unlinked. If so, shut down immediately.
    GstPad *srcPad = gst_element_get_static_pad(_recorderValve, "src");
    if (srcPad) {
        GstPad *peerPad = gst_pad_get_peer(srcPad);
        if (!peerPad) {
            qCDebug(GstVideoReceiverLog) << "Recorder branch is already unlinked. Shutting down immediately.";
            gst_clear_object(&srcPad);
            _recordingStopRequested = true;
            _shutdownRecordingBranch();
            return;
        }
        gst_clear_object(&peerPad);
        gst_clear_object(&srcPad);
    }

    g_object_set(_recorderValve,
                 "drop", TRUE,
                 nullptr);

    _removingRecorder = true;

    if (!_unlinkBranch(_recorderValve)) {
        _removingRecorder = false;
        _dispatchSignal([this]() { emit onStopRecordingComplete(STATUS_FAIL); });
        return;
    }

    // EOS event propagates valve→mux→filesink; _shutdownRecordingBranch emits the
    // complete signal once the muxer index is written and the file is closed.
    _recordingStopRequested = true;

    // Fallback timer: force shutdown if EOS is not received within 3 s.
    // mp4mux/qtmux needs time to flush buffered data and write the moov atom (container index).
    // 200 ms was too short and caused the muxer to be killed before finalizing, producing corrupt files.
    QTimer::singleShot(3000, [this]() {
        _worker->dispatch([this]() {
            if (_removingRecorder) {
                qCWarning(GstVideoReceiverLog) << "Recording stop timed out (EOS got stuck). Forcing shutdown.";
                _shutdownRecordingBranch();
            }
        });
    });
}

void GstVideoReceiver::takeScreenshot(const QString &imageFile)
{
    if (_needDispatch()) {
        const QString cachedImageFile = imageFile;
        _worker->dispatch([this, cachedImageFile]() { takeScreenshot(cachedImageFile); });
        return;
    }

    qCDebug(GstVideoReceiverLog) << "taking screenshot" << _uri;

    // FIXME: record screenshot here
    _dispatchSignal([this]() { emit onTakeScreenshotComplete(STATUS_NOT_IMPLEMENTED); });
}

void GstVideoReceiver::_watchdog()
{
    _worker->dispatch([this]() {
        if (!_pipeline) {
            return;
        }

        const qint64 now = QDateTime::currentSecsSinceEpoch();
        if (_lastSourceFrameTime == 0) {
            _lastSourceFrameTime = now;
        }

        qint64 elapsed = now - _lastSourceFrameTime;
        if (elapsed > _timeout) {
            qCDebug(GstVideoReceiverLog) << "Stream timeout, no frames for" << elapsed << _uri;
            GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-watchdog-timeout");
            _dispatchSignal([this]() { emit timeout(); });
            if (!_recording) {
                stop();
            } else {
                qCDebug(GstVideoReceiverLog) << "Recording is active, keeping pipeline alive during timeout" << _uri;
            }
        }

        if (_decoding && !_removingDecoder) {
            if (_lastVideoFrameTime == 0) {
                _lastVideoFrameTime = now;
            }

            elapsed = now - _lastVideoFrameTime;
            if (elapsed > (_timeout * 2)) {
                qCDebug(GstVideoReceiverLog) << "Video decoder timeout, no frames for" << elapsed << _uri;
                GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-watchdog-timeout");
                _dispatchSignal([this]() { emit timeout(); });
                if (!_recording) {
                    stop();
                } else {
                    qCDebug(GstVideoReceiverLog) << "Recording is active, keeping decoder branch alive during timeout" << _uri;
                }
            }
        }
    });
}

void GstVideoReceiver::_handleEOS()
{
    if (!_pipeline) {
        return;
    }

    if (_endOfStream) {
        stop();
    } else if (_decoding && _removingDecoder) {
        _shutdownDecodingBranch();
    } else if (_recording && _removingRecorder) {
        _shutdownRecordingBranch();
    } /*else {
        qCWarning(GstVideoReceiverLog) << "Unexpected EOS!";
        stop();
    }*/
}

#if !defined(QGC_GST_BUILD_VERSION_MAJOR) || (QGC_GST_BUILD_VERSION_MAJOR == 1 && QGC_GST_BUILD_VERSION_MINOR < 28)
gboolean GstVideoReceiver::_filterParserCaps(GstElement *bin, GstPad *pad, GstElement *element, GstQuery *query, gpointer data)
{
    Q_UNUSED(bin); Q_UNUSED(pad); Q_UNUSED(element); Q_UNUSED(data)

    if (GST_QUERY_TYPE(query) != GST_QUERY_CAPS) {
        return FALSE;
    }

    GstCaps *srcCaps = nullptr;
    gst_query_parse_caps(query, &srcCaps);
    if (!srcCaps || gst_caps_is_any(srcCaps)) {
        return FALSE;
    }

    gchar *srcCapsStr = gst_caps_to_string(srcCaps);
    qCWarning(GstVideoReceiverLog) << "_filterParserCaps: srcCaps =" << srcCapsStr;
    g_free(srcCapsStr);

    GstCaps *sinkCaps = gst_caps_copy(srcCaps);
    sinkCaps = gst_caps_make_writable(sinkCaps);
    bool modified = false;

    for (guint i = 0; i < gst_caps_get_size(sinkCaps); ++i) {
        GstStructure *structure = gst_caps_get_structure(sinkCaps, i);
        if (gst_structure_has_name(structure, "video/x-h265")) {
            gst_structure_set(structure, "stream-format", G_TYPE_STRING, "hvc1", nullptr);
            modified = true;
        } else if (gst_structure_has_name(structure, "video/x-h264")) {
            gst_structure_set(structure, "stream-format", G_TYPE_STRING, "avc", nullptr);
            modified = true;
        }
    }

    if (modified) {
        gchar *sinkCapsStr = gst_caps_to_string(sinkCaps);
        qCWarning(GstVideoReceiverLog) << "_filterParserCaps: returning sinkCaps =" << sinkCapsStr;
        g_free(sinkCapsStr);

        gst_query_set_caps_result(query, sinkCaps);
        gst_clear_caps(&sinkCaps);
        return TRUE;
    }

    gst_clear_caps(&sinkCaps);
    return FALSE;
}
#endif

GstElement *GstVideoReceiver::_makeSource(const QString &input)
{
    if (input.isEmpty()) {
        qCCritical(GstVideoReceiverLog) << "Failed because URI is not specified";
        return nullptr;
    }

    const QUrl sourceUrl(input);

    const bool isRtsp = sourceUrl.scheme().startsWith("rtsp", Qt::CaseInsensitive);
    const bool isUdp264 = input.contains("udp://", Qt::CaseInsensitive);
    const bool isUdp265 = input.contains("udp265://", Qt::CaseInsensitive);
    const bool isUdpMPEGTS = input.contains("mpegts://", Qt::CaseInsensitive);
    const bool isTcpMPEGTS = input.contains("tcp://", Qt::CaseInsensitive);

    GstElement *source = nullptr;
    GstElement *buffer = nullptr;
    GstElement *tsdemux = nullptr;
    GstElement *parser = nullptr;
    GstElement *bin = nullptr;
    GstElement *srcbin = nullptr;

    do {
        if (isRtsp) {
            if (!GStreamer::isValidRtspUri(input.toUtf8().constData())) {
                qCCritical(GstVideoReceiverLog) << "Invalid RTSP URI:" << input;
                break;
            }

            source = gst_element_factory_make("rtspsrc", "source");
            if (!source) {
                qCCritical(GstVideoReceiverLog) << "gst_element_factory_make('rtspsrc') failed";
                break;
            }

            const QString rtspUserInfo = sourceUrl.userInfo();
            QString rtspUser, rtspPassword;
            if (!rtspUserInfo.isEmpty()) {
                const int colonIdx = rtspUserInfo.indexOf(QLatin1Char(':'));
                if (colonIdx >= 0) {
                    rtspUser = rtspUserInfo.left(colonIdx);
                    rtspPassword = rtspUserInfo.mid(colonIdx + 1);
                } else {
                    rtspUser = rtspUserInfo;
                }
            }
            QUrl cleanUrl(sourceUrl);
            cleanUrl.setUserInfo(QString());
            const QByteArray cleanLocation = cleanUrl.toString().toUtf8();

            g_object_set(source,
                         "location", cleanLocation.constData(),
                         "latency", 25,
                         "do-rtcp", TRUE,
                         "tcp-timeout", G_GUINT64_CONSTANT(5000000),
                         "udp-reconnect", TRUE,
                         "drop-on-latency", TRUE,
                         "retry", 3,
                         nullptr);

            if (!rtspUser.isEmpty()) {
                g_object_set(source,
                             "user-id", rtspUser.toUtf8().constData(),
                             "user-pw", rtspPassword.toUtf8().constData(),
                             nullptr);
            }
        } else if (isTcpMPEGTS) {
            source = gst_element_factory_make("tcpclientsrc", "source");
            if (!source) {
                qCCritical(GstVideoReceiverLog) << "gst_element_factory_make('tcpclientsrc') failed";
                break;
            }

            const QString host = sourceUrl.host();
            const quint16 port = sourceUrl.port();
            g_object_set(source,
                         "host", host.toUtf8().constData(),
                         "port", port,
                         nullptr);
        } else if (isUdp264 || isUdp265 || isUdpMPEGTS) {
            source = gst_element_factory_make("udpsrc", "source");
            if (!source) {
                qCCritical(GstVideoReceiverLog) << "gst_element_factory_make('udpsrc') failed";
                break;
            }

            const QString uri = QStringLiteral("udp://%1:%2").arg(sourceUrl.host(), QString::number(sourceUrl.port()));
            g_object_set(source,
                         "uri", uri.toUtf8().constData(),
                         "buffer-size", 8 * 1024 * 1024,
                         nullptr);

            GstCaps *caps = nullptr;
            if (isUdp264) {
                caps = gst_caps_from_string("application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264");
                if (!caps) {
                    qCCritical(GstVideoReceiverLog) << "gst_caps_from_string() failed";
                    break;
                }
            } else if (isUdp265) {
                caps = gst_caps_from_string("application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H265");
                if (!caps) {
                    qCCritical(GstVideoReceiverLog) << "gst_caps_from_string() failed";
                    break;
                }
            }

            if (caps) {
                g_object_set(source,
                             "caps", caps,
                             nullptr);
                gst_clear_caps(&caps);
            }
        } else {
            qCDebug(GstVideoReceiverLog) << "URI is not recognized";
        }

        if (!source) {
            qCCritical(GstVideoReceiverLog) << "gst_element_factory_make() for data source failed";
            break;
        }

        bin = gst_bin_new("sourcebin");
        if (!bin) {
            qCCritical(GstVideoReceiverLog) << "gst_bin_new('sourcebin') failed";
            break;
        }

        parser = gst_element_factory_make("parsebin", "parser");
        if (!parser) {
            qCCritical(GstVideoReceiverLog) << "gst_element_factory_make('parsebin') failed";
            break;
        }

        // GStreamer < 1.28: decodebin3 didn't negotiate stream-format caps properly
        // between parser and decoder, so we forced hvc1/avc. GStreamer 1.28+ fixes
        // this natively, and the forced caps break hardware decoders that need
        // byte-stream format (e.g. Qualcomm AMC on Android, D3D12 on Windows).
#if !defined(QGC_GST_BUILD_VERSION_MAJOR) || (QGC_GST_BUILD_VERSION_MAJOR == 1 && QGC_GST_BUILD_VERSION_MINOR < 28)
        (void) g_signal_connect(parser, "autoplug-query", G_CALLBACK(_filterParserCaps), nullptr);
#endif

        gst_bin_add_many(GST_BIN(bin), source, parser, nullptr);

        // FIXME: AV: Android does not determine MPEG2-TS via parsebin - have to explicitly state which demux to use
        // FIXME: AV: tsdemux handling is a bit ugly - let's try to find elegant solution for that later
        if (isTcpMPEGTS || isUdpMPEGTS) {
            tsdemux = gst_element_factory_make("tsdemux", nullptr);
            if (!tsdemux) {
                qCCritical(GstVideoReceiverLog) << "gst_element_factory_make('tsdemux') failed";
                break;
            }

            (void) gst_bin_add(GST_BIN(bin), tsdemux);

            if (!gst_element_link(source, tsdemux)) {
                qCCritical(GstVideoReceiverLog) << "gst_element_link() failed";
                break;
            }

            source = tsdemux;
            tsdemux = nullptr;
        }

        int probeRes = 0;
        (void) gst_element_foreach_src_pad(source, _padProbe, &probeRes);

        if (probeRes & 1) {
            if ((probeRes & 2) && (_buffer >= 0)) {
                buffer = gst_element_factory_make("rtpjitterbuffer", nullptr);
                if (!buffer) {
                    qCCritical(GstVideoReceiverLog) << "gst_element_factory_make('rtpjitterbuffer') failed";
                    break;
                }

                g_object_set(buffer,
                             "do-lost", TRUE,
                             "drop-on-latency", _buffer == 0 ? TRUE : FALSE,
                             nullptr);

                (void) gst_bin_add(GST_BIN(bin), buffer);

                if (!gst_element_link_many(source, buffer, parser, nullptr)) {
                    qCCritical(GstVideoReceiverLog) << "gst_element_link() failed";
                    break;
                }
            } else {
                if (!gst_element_link(source, parser)) {
                    qCCritical(GstVideoReceiverLog) << "gst_element_link() failed";
                    break;
                }
            }
        } else {
            (void) g_signal_connect(source, "pad-added", G_CALLBACK(_linkPad), parser);
        }

        (void) g_signal_connect(parser, "pad-added", G_CALLBACK(_wrapWithGhostPad), nullptr);

        source = tsdemux = buffer = parser = nullptr;

        srcbin = bin;
        bin = nullptr;
    } while(0);

    gst_clear_object(&bin);
    gst_clear_object(&parser);
    gst_clear_object(&tsdemux);
    gst_clear_object(&buffer);
    gst_clear_object(&source);

    return srcbin;
}

GstElement *GstVideoReceiver::_makeDecoder()
{
    GstElement *decoder = gst_element_factory_make("decodebin3", nullptr);
    if (!decoder) {
        qCCritical(GstVideoReceiverLog) << "gst_element_factory_make('decodebin3') failed";
    }
    return decoder;
}

// Parser properties are left at defaults to allow caps-based passthrough of H264/H265 headers.

GstElement *GstVideoReceiver::_makeFileSink(const QString &videoFile, FILE_FORMAT format)
{
    GstElement *fileSink = nullptr;
    GstElement *parser = nullptr;
    GstElement *mux = nullptr;
    GstPad *muxPad = nullptr;
    GstElement *sink = nullptr;
    GstElement *bin = nullptr;
    bool releaseElements = true;

    do {
        if (!isValidFileFormat(format)) {
            qCCritical(GstVideoReceiverLog) << "Unsupported file format";
            break;
        }

        bool isH265 = false;
        bool needsParser = true; // assume byte-stream until we inspect caps

        // Inspect the current caps at _recorderValve to decide whether we need
        // a parser. RTSP streams come through parsebin already converted to
        // stream-format=avc/hvc1. Adding h264parse on top of avc data causes
        // mp4mux to reject it ("Could not multiplex stream").
        if (_recorderValve) {
            GstPad *valvePad = gst_element_get_static_pad(_recorderValve, "sink");
            if (valvePad) {
                GstCaps *caps = gst_pad_get_current_caps(valvePad);
                if (caps) {
                    GstStructure *s = gst_caps_get_structure(caps, 0);
                    if (s) {
                        const gchar *name = gst_structure_get_name(s);
                        if (g_strcmp0(name, "video/x-h265") == 0) {
                            isH265 = true;
                        }
                        const gchar *sf = gst_structure_get_string(s, "stream-format");
                        // avc/hvc1/avc3: already packaged for MP4/TS — no parser needed.
                        if (sf && (g_strcmp0(sf, "avc")  == 0 ||
                                   g_strcmp0(sf, "hvc1") == 0 ||
                                   g_strcmp0(sf, "avc3") == 0)) {
                            needsParser = false;
                        }
                    }
                    gchar *capsStr = gst_caps_to_string(caps);
                    qCDebug(GstVideoReceiverLog) << "_makeFileSink: valve caps =" << capsStr
                                                  << "needsParser =" << needsParser;
                    g_free(capsStr);
                    gst_caps_unref(caps);
                }
                gst_object_unref(valvePad);
            }
        }

        // Fallback codec detection from decoder name
        if (!isH265 && (_decoderName.contains("265", Qt::CaseInsensitive) ||
                        _decoderName.contains("hevc", Qt::CaseInsensitive))) {
            isH265 = true;
        }

        qCDebug(GstVideoReceiverLog) << "_makeFileSink: isH265 =" << isH265
                                      << "needsParser =" << needsParser
                                      << "format =" << format;

        if (needsParser) {
            // Byte-stream UDP/TCP sources need h264parse/h265parse to convert to
            // avc/hvc1 and inject codec_data into caps for the muxer.
            parser = gst_element_factory_make(isH265 ? "h265parse" : "h264parse", nullptr);
            if (!parser) {
                qCCritical(GstVideoReceiverLog) << "Failed to create parser for file sink";
                break;
            }
        }

        // Name the mux "sinkbin-mux" so _unlinkBranch can locate it by name
        // to send EOS directly to its video sink pad when stopping recording.
        mux = gst_element_factory_make(_kFileMux[format], "sinkbin-mux");
        if (!mux) {
            qCCritical(GstVideoReceiverLog) << "gst_element_factory_make('" << _kFileMux[format] << "') failed";
            break;
        }

        sink = gst_element_factory_make("filesink", nullptr);
        if (!sink) {
            qCCritical(GstVideoReceiverLog) << "gst_element_factory_make('filesink') failed";
            break;
        }

        g_object_set(sink,
                     "location", qPrintable(videoFile),
                     nullptr);

        bin = gst_bin_new("sinkbin");
        if (!bin) {
            qCCritical(GstVideoReceiverLog) << "gst_bin_new('sinkbin') failed";
            break;
        }

        // NOTE: Do NOT set message-forward=TRUE on the sinkbin. The parent pipeline
        // already has message-forward=TRUE which forwards the sinkbin's EOS as a
        // GstBinForwarded element message. Setting it on both causes double-forwarding.

        // Add ALL elements to the bin BEFORE linking — GStreamer requires elements
        // to share the same parent bin before pad links can be established.
        if (parser) {
            gst_bin_add_many(GST_BIN(bin), parser, mux, sink, nullptr);
        } else {
            gst_bin_add_many(GST_BIN(bin), mux, sink, nullptr);
        }
        releaseElements = false;

        // Request a video sink pad on the mux
        GstPadTemplate *padTemplate = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(mux), "video_%u");
        if (!padTemplate) {
            qCCritical(GstVideoReceiverLog) << "gst_element_class_get_pad_template(mux) failed";
            break;
        }

        muxPad = gst_element_request_pad(mux, padTemplate, nullptr, nullptr);
        if (!muxPad) {
            qCCritical(GstVideoReceiverLog) << "gst_element_request_pad(mux) failed";
            break;
        }



        if (parser) {
            // Link parser src → mux video pad
            GstPad *parserSrcPad = gst_element_get_static_pad(parser, "src");
            if (!parserSrcPad) {
                qCCritical(GstVideoReceiverLog) << "gst_element_get_static_pad(parser,'src') failed";
                break;
            }
            GstPadLinkReturn linkRet = gst_pad_link(parserSrcPad, muxPad);
            gst_object_unref(parserSrcPad);
            if (linkRet != GST_PAD_LINK_OK) {
                qCCritical(GstVideoReceiverLog) << "Failed to link parser src → mux pad, error:" << linkRet;
                break;
            }
        }

        // Link mux → filesink
        if (!gst_element_link(mux, sink)) {
            qCCritical(GstVideoReceiverLog) << "gst_element_link(mux, filesink) failed";
            break;
        }

        // Build the ghost pad: expose parser.sink (if present) or mux.video_0 to the outside.
        GstPad *innerSinkPad = nullptr;
        if (parser) {
            innerSinkPad = gst_element_get_static_pad(parser, "sink");
        } else {
            // No parser: ghost pad wraps the mux's video request pad directly.
            // EOS sent to this ghost pad goes straight to the mux → moov is written → file finalized.
            innerSinkPad = GST_PAD(gst_object_ref(muxPad));
        }

        if (!innerSinkPad) {
            qCCritical(GstVideoReceiverLog) << "Failed to get inner sink pad for ghostpad";
            break;
        }

        GstPad *ghostpad = gst_ghost_pad_new("sink", innerSinkPad);
        gst_object_unref(innerSinkPad);

        if (!ghostpad) {
            qCCritical(GstVideoReceiverLog) << "gst_ghost_pad_new() failed";
            break;
        }

        (void) gst_element_add_pad(bin, ghostpad);

        // Block upstream RECONFIGURE events at the sinkbin boundary.
        // Without this, h264parse (or mp4mux) caps negotiation sends RECONFIGURE upstream:
        // recorderValve → recorderQueue → tee → decodebin3, causing decodebin3 to
        // re-emit pad-added and set up a second decoder/video display ("two video" bug).
        GstPad *ghostSinkForProbe = gst_element_get_static_pad(bin, "sink");
        if (ghostSinkForProbe) {
            gst_pad_add_probe(ghostSinkForProbe,
                GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
                [](GstPad*, GstPadProbeInfo *info, gpointer) -> GstPadProbeReturn {
                    if (GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info)) == GST_EVENT_RECONFIGURE) {
                        return GST_PAD_PROBE_DROP;
                    }
                    return GST_PAD_PROBE_OK;
                },
                nullptr, nullptr);

            // Unified Data Probe:
            // 1. Timestamps: Cleanly start timestamps at 0 and mathematically enforce monotonic DTS
            //    (advancing by 33.3ms for missing/broken timestamps) to prevent 3s playback bugs.
            // 2. Caps freeze: We freeze the caps format AFTER the first buffer arrives.
            //    This mathematically guarantees we capture any late-arriving codec_data (like SPS/PPS headers
            //    needed for CAM1), but strictly prevents any mid-stream caps changes from reaching mp4mux
            //    after data flow has started, preventing the "Could not multiplex stream" pipeline crash.
            struct MuxState {
                GstClockTime lastDts = GST_CLOCK_TIME_NONE;
                GstClockTime offset = GST_CLOCK_TIME_NONE;
                int bufferCount = 0;
                bool capsLocked = false;
            };
            auto *muxState = new MuxState();
            gst_pad_add_probe(ghostSinkForProbe, GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM,
                [](GstPad*, GstPadProbeInfo *info, gpointer user_data) -> GstPadProbeReturn {
                    auto *state = static_cast<MuxState*>(user_data);

                    if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
                        state->bufferCount++;
                        // Fallback lock: unconditionally lock the caps format after 10 frames (approx 330ms).
                        // This protects streams like CAM2 (which never send codec_data) from crashing
                        // when mid-stream caps changes arrive on subsequent IDR frames.
                        if (state->bufferCount > 10) {
                            state->capsLocked = true;
                        }
                        
                        GstBuffer *buf = gst_pad_probe_info_get_buffer(info);

                        if (!GST_CLOCK_TIME_IS_VALID(state->offset)) {
                            if (GST_CLOCK_TIME_IS_VALID(buf->pts)) {
                                state->offset = buf->pts;
                                if (GST_CLOCK_TIME_IS_VALID(buf->dts) && buf->dts < state->offset) {
                                    state->offset = buf->dts;
                                }
                            }
                        }

                        if (GST_CLOCK_TIME_IS_VALID(state->offset)) {
                            buf = gst_buffer_make_writable(buf);
                            GST_PAD_PROBE_INFO_DATA(info) = buf;

                            if (GST_CLOCK_TIME_IS_VALID(buf->pts) && buf->pts >= state->offset) {
                                buf->pts -= state->offset;
                            } else {
                                buf->pts = 0;
                            }

                            if (GST_CLOCK_TIME_IS_VALID(buf->dts) && buf->dts >= state->offset) {
                                buf->dts -= state->offset;
                            } else {
                                buf->dts = 0;
                            }

                            if (!GST_CLOCK_TIME_IS_VALID(buf->dts) && GST_CLOCK_TIME_IS_VALID(buf->pts)) {
                                buf->dts = buf->pts;
                            }

                            if (GST_CLOCK_TIME_IS_VALID(state->lastDts)) {
                                if (!GST_CLOCK_TIME_IS_VALID(buf->dts) || buf->dts <= state->lastDts) {
                                    buf->dts = state->lastDts + 33333333; // 30fps fallback
                                }
                            }
                            state->lastDts = buf->dts;

                            if (GST_CLOCK_TIME_IS_VALID(buf->pts) && buf->pts < buf->dts) {
                                buf->pts = buf->dts;
                            }
                        }
                        return GST_PAD_PROBE_OK;
                    }

                    if (info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
                        GstEvent *event = gst_pad_probe_info_get_event(info);
                        if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {
                            if (state->capsLocked) {
                                qCDebug(GstVideoReceiverLog) << "Dropping mid-stream caps change to prevent crash";
                                return GST_PAD_PROBE_DROP;
                            }
                            
                            // If we see codec_data, we can confidently lock the format immediately!
                            // This perfectly protects CAM1 from subsequent IDR frames crashing the pipeline.
                            GstCaps *caps = nullptr;
                            gst_event_parse_caps(event, &caps);
                            if (caps) {
                                GstStructure *s = gst_caps_get_structure(caps, 0);
                                if (s && gst_structure_has_field(s, "codec_data")) {
                                    qCDebug(GstVideoReceiverLog) << "Freezing caps! Valid codec_data found.";
                                    state->capsLocked = true;
                                }
                            }
                        }
                    }

                    return GST_PAD_PROBE_OK;
                },
                muxState,
                [](gpointer data) { delete static_cast<MuxState*>(data); });

            gst_object_unref(ghostSinkForProbe);
        }

        fileSink = bin;
        bin = nullptr;
    } while(0);

    if (releaseElements) {
        gst_clear_object(&sink);
        if (mux) {
            if (muxPad) {
                gst_element_release_request_pad(mux, muxPad);
                gst_clear_object(&muxPad);
            }
            gst_clear_object(&mux);
        }
        if (parser) {
            gst_clear_object(&parser);
        }
    } else {
        // Unref the muxPad ref we held — the bin/mux now own it.
        gst_clear_object(&muxPad);
    }

    gst_clear_object(&bin);
    return fileSink;
}

void GstVideoReceiver::_onNewSourcePad(GstPad *pad)
{
    // FIXME: check for caps - if this is not video stream (and preferably - one of these which we have to support) then simply skip it
    if (!gst_element_link(_source, _tee)) {
        qCCritical(GstVideoReceiverLog) << "Unable to link source";
        return;
    }

    if (!_streaming) {
        _streaming = true;
        qCDebug(GstVideoReceiverLog) << "Streaming started" << _uri;
        _dispatchSignal([this]() { emit streamingChanged(_streaming); });
    }

    _eosProbeId = gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, _eosProbe, this, nullptr);
    if (_eosProbeId != 0) {
        // Hold a ref so _shutdownDecodingBranch can remove the probe even after _decoder is gone.
        _eosProbePad = GST_PAD_CAST(gst_object_ref(pad));
    }
    if (!_videoSink) {
        return;
    }

    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-with-new-source-pad");

    _ensureVideoSinkInPipeline();

    if (!_addDecoder(_decoderValve)) {
        qCCritical(GstVideoReceiverLog) << "_addDecoder() failed";
        _shutdownDecodingBranch();
        return;
    }

    g_object_set(_decoderValve,
                 "drop", FALSE,
                 nullptr);

    qCDebug(GstVideoReceiverLog) << "Decoding started" << _uri;
}

void GstVideoReceiver::_logDecodebin3SelectedCodec(GstElement *decodebin3)
{
    GValue value = G_VALUE_INIT;
    GstIterator *iter = gst_bin_iterate_elements(GST_BIN(decodebin3));
    GstElement *child;

    while (gst_iterator_next(iter, &value) == GST_ITERATOR_OK) {
        child = GST_ELEMENT(g_value_get_object(&value));
        GstElementFactory *factory = gst_element_get_factory(child);

        if (factory) {
            gboolean is_decoder = gst_element_factory_list_is_type(factory, GST_ELEMENT_FACTORY_TYPE_DECODER);
            if (is_decoder) {
                const gchar *decoderKlass = gst_element_factory_get_klass(factory);
                GstPluginFeature *feature = GST_PLUGIN_FEATURE(factory);
                const gchar *featureName = gst_plugin_feature_get_name(feature);
                const guint rank = gst_plugin_feature_get_rank(feature);
                bool isHardwareDecoder = GStreamer::isHardwareDecoderFactory(factory);

                QString pluginName = featureName;
                GstPlugin *plugin = gst_plugin_feature_get_plugin(feature);
                if (plugin) {
                    pluginName = gst_plugin_get_name(plugin);
                    gst_object_unref(plugin);
                }
                qCDebug(GstVideoReceiverLog) << "Decodebin3 selected codec:rank -" << pluginName << "/" << featureName << "-" << decoderKlass << (isHardwareDecoder ? "(HW)" : "(SW)") << ":" << rank;

                const QString newName = QString::fromUtf8(featureName);
                if (newName != _decoderName) {
                    _decoderName = newName;
                    _dispatchSignal([this]() { emit decoderStatsChanged(); });
                }

                // Disable QoS on the internal decoder to prevent cascading
                // frame drops on live streams.  The videodecoder base class
                // aggressively advances earliest_time after the first late
                // frame, causing all subsequent frames to be dropped.
                g_object_set(child, "qos", FALSE, nullptr);
                qCDebug(GstVideoReceiverLog) << "Disabled QoS on internal decoder" << featureName;
            }
        }
        g_value_reset(&value);
    }
    g_value_unset(&value);
    gst_iterator_free(iter);
}


void GstVideoReceiver::_onNewDecoderPad(GstPad *pad)
{
    qCDebug(GstVideoReceiverLog) << "_onNewDecoderPad" << _uri;

    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-with-new-decoder-pad");

    // We should now know what codec decodebin3 selected.
    _logDecodebin3SelectedCodec(_decoder);

    if (!_addVideoSink(pad)) {
        qCCritical(GstVideoReceiverLog) << "_addVideoSink() failed";
    }
}

bool GstVideoReceiver::_addDecoder(GstElement *src)
{
    _decoder = _makeDecoder();
    if (!_decoder) {
        qCCritical(GstVideoReceiverLog) << "_makeDecoder() failed";
        return false;
    }

    (void) gst_object_ref(_decoder);

    (void) gst_bin_add(GST_BIN(_pipeline), _decoder);
    (void) gst_element_sync_state_with_parent(_decoder);

    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-with-decoder");

    if (!gst_element_link(src, _decoder)) {
        qCCritical(GstVideoReceiverLog) << "Unable to link decoder";
        gst_element_set_state(_decoder, GST_STATE_NULL);
        (void) gst_element_get_state(_decoder, nullptr, nullptr, GST_CLOCK_TIME_NONE);
        (void) gst_bin_remove(GST_BIN(_pipeline), _decoder);
        gst_clear_object(&_decoder);
        return false;
    }

    GstPad *srcPad = nullptr;
    GstIterator *it = gst_element_iterate_src_pads(_decoder);
    GValue vpad = G_VALUE_INIT;
    switch (gst_iterator_next(it, &vpad)) {
        case GST_ITERATOR_OK:
            srcPad = GST_PAD(g_value_get_object(&vpad));
            (void) gst_object_ref(srcPad);
            (void) g_value_reset(&vpad);
            break;
        case GST_ITERATOR_RESYNC:
            gst_iterator_resync(it);
            break;
        default:
            break;
    }
    g_value_unset(&vpad);
    gst_iterator_free(it);

    if (srcPad) {
        _onNewDecoderPad(srcPad);
    } else {
        (void) g_signal_connect(_decoder, "pad-added", G_CALLBACK(_onNewPad), this);
    }

    gst_clear_object(&srcPad);
    return true;
}

void GstVideoReceiver::_ensureVideoSinkInPipeline()
{
    if (!_videoSink || !_pipeline) {
        return;
    }

    GstObject *parent = gst_element_get_parent(_videoSink);
    if (parent) {
        gst_object_unref(parent);
        return;
    }

    g_object_set(_videoSink,
                 "sync", (_buffer >= 0),
                 NULL);

    (void) gst_object_ref(_videoSink);
    (void) gst_bin_add(GST_BIN(_pipeline), _videoSink);

    // PAUSED (not READY) triggers downstream caps negotiation before source data arrives.
    (void) gst_element_set_state(_videoSink, GST_STATE_PAUSED);
}

bool GstVideoReceiver::_addVideoSink(GstPad *pad)
{
    GstCaps *caps = gst_pad_query_caps(pad, nullptr);

    _ensureVideoSinkInPipeline();

    GstPad *sinkPad = gst_element_get_static_pad(_videoSink, "sink");
    GstPadLinkReturn linkRet = sinkPad ? gst_pad_link(pad, sinkPad) : GST_PAD_LINK_WRONG_HIERARCHY;
    if (linkRet != GST_PAD_LINK_OK) {
        qCCritical(GstVideoReceiverLog) << "Unable to link decoder pad to video sink, result:" << linkRet;

        // _ensureVideoSinkInPipeline() added it before linking; detach for the next retry.
        GstObject *parent = gst_element_get_parent(_videoSink);
        if (parent) {
            (void) gst_element_set_state(_videoSink, GST_STATE_NULL);
            (void) gst_element_get_state(_videoSink, nullptr, nullptr, GST_CLOCK_TIME_NONE);
            (void) gst_bin_remove(GST_BIN(_pipeline), _videoSink);
            gst_clear_object(&parent);
        }

        gst_clear_object(&sinkPad);
        gst_clear_caps(&caps);
        return false;
    }
    gst_clear_object(&sinkPad);

    (void) gst_element_sync_state_with_parent(_videoSink);

    // sync=FALSE + max-lateness=-1: HW decoders with startup latency must not drop early frames.
    g_object_set(_videoSink, "sync", FALSE, "max-lateness", G_GINT64_CONSTANT(-1), nullptr);

    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-with-videosink");

    // Determine video size. Errors here are non-fatal.
    QSize videoSize;
    do {
        if (!_decoderValve) {
            qCCritical(GstVideoReceiverLog) << "Unable to determine video size - _decoderValve is NULL" << _uri;
            break;
        }

        GstPad *valveSrcPad = gst_element_get_static_pad(_decoderValve, "src");
        if (!valveSrcPad) {
            qCCritical(GstVideoReceiverLog) << "gst_element_get_static_pad() failed";
            break;
        }

        GstCaps *valveSrcPadCaps = gst_pad_query_caps(valveSrcPad, nullptr);
        if (!valveSrcPadCaps) {
            qCCritical(GstVideoReceiverLog) << "gst_pad_query_caps() failed";
            gst_clear_object(&valveSrcPad);
            break;
        }

        const GstStructure *structure = gst_caps_get_structure(valveSrcPadCaps, 0);
        if (!structure) {
            qCCritical(GstVideoReceiverLog) << "Unable to determine video size - structure is NULL" << _uri;
            gst_clear_object(&valveSrcPad);
            break;
        }

        gint width = 0;
        gint height = 0;
        (void) gst_structure_get_int(structure, "width", &width);
        (void) gst_structure_get_int(structure, "height", &height);

        // Swap W×H for 90°/270° streams so QML AR is computed on display dimensions.
        gint orientation = 0;
        if (gst_structure_get_int(structure, "video-orientation", &orientation)
            && (orientation == GST_VIDEO_ORIENTATION_90R
                || orientation == GST_VIDEO_ORIENTATION_90L
                || orientation == GST_VIDEO_ORIENTATION_UL_LR
                || orientation == GST_VIDEO_ORIENTATION_UR_LL)) {
            videoSize.setWidth(height);
            videoSize.setHeight(width);
        } else {
            videoSize.setWidth(width);
            videoSize.setHeight(height);
        }

        gst_clear_caps(&valveSrcPadCaps);
        gst_clear_object(&valveSrcPad);
    } while (false);
    _dispatchSignal([this, videoSize]() { emit videoSizeChanged(videoSize); });

    gst_clear_caps(&caps);
    return true;
}

void GstVideoReceiver::_noteTeeFrame()
{
    _lastSourceFrameTime = QDateTime::currentSecsSinceEpoch();
}

void GstVideoReceiver::_noteVideoSinkFrame()
{
    _lastVideoFrameTime = QDateTime::currentSecsSinceEpoch();
    if (!_decoding) {
        _decoding = true;
        qCDebug(GstVideoReceiverLog) << "Decoding started";
        _dispatchSignal([this]() { emit decodingChanged(_decoding); });
    }
}

void GstVideoReceiver::_noteEndOfStream()
{
    _endOfStream = true;
}

bool GstVideoReceiver::_unlinkBranch(GstElement *from)
{
    GstPad *src = gst_element_get_static_pad(from, "src");
    if (!src) {
        qCCritical(GstVideoReceiverLog) << "gst_element_get_static_pad() failed";
        return false;
    }

    GstPad *sink = gst_pad_get_peer(src);
    if (!sink) {
        gst_clear_object(&src);
        qCCritical(GstVideoReceiverLog) << "gst_pad_get_peer() failed";
        return false;
    }

    // Unlink the pads first to prevent further data flow
    if (!gst_pad_unlink(src, sink)) {
        qCWarning(GstVideoReceiverLog) << "gst_pad_unlink() failed";
    }

    gst_clear_object(&src);
    gst_clear_object(&sink);

    // Send EOS directly to the mux's video sink pad inside the sinkbin.
    //
    // Why NOT gst_pad_send_event(ghostSink, EOS):
    //   After gst_pad_unlink(), the ghost pad has no upstream peer. In push mode,
    //   GhostPad event routing depends on the pad being in an active push stream;
    //   after unlinking, the internal proxy routing may not fire correctly.
    //
    // Why NOT gst_element_send_event(_fileSink, EOS):
    //   GstBin's send_event iterates sink-leaf children (only filesink), completely
    //   bypassing the mux → moov atom is never written → corrupt MP4.
    //
    // Direct pad approach: find the mux inside the sinkbin, get its first video
    // sink pad, and push EOS there. This routes EOS: mux → filesink, writing moov.
    // h264parse EOS is handled implicitly — since data flow stopped (valve dropped),
    // h264parse has no pending buffers, and sending EOS to the mux directly is safe.
    if (!_fileSink) {
        qCCritical(GstVideoReceiverLog) << "_fileSink is NULL, cannot send EOS";
        return false;
    }

    // Find the mux by its fixed name "sinkbin-mux" and get its first video sink pad.
    // We send EOS directly to the mux's pad, bypassing h264parse, because:
    // - data flow has stopped (valve is in DROP mode, no buffers in-flight)
    // - the ghost pad may not route events after unlinking
    // - gst_element_send_event(bin) only sends to leaf sinks (filesink), skipping the mux
    GstElement *mux = gst_bin_get_by_name(GST_BIN(_fileSink), "sinkbin-mux");
    if (!mux) {
        qCCritical(GstVideoReceiverLog) << "Could not find 'sinkbin-mux' element";
        return false;
    }

    GstPad *muxSinkPad = nullptr;
    {
        GstIterator *it = gst_element_iterate_sink_pads(mux);
        GValue val = G_VALUE_INIT;
        if (gst_iterator_next(it, &val) == GST_ITERATOR_OK) {
            muxSinkPad = GST_PAD(gst_object_ref(g_value_get_object(&val)));
            g_value_unset(&val);
        }
        gst_iterator_free(it);
    }
    gst_object_unref(mux);

    if (!muxSinkPad) {
        qCCritical(GstVideoReceiverLog) << "Could not get mux video sink pad";
        return false;
    }

    qCDebug(GstVideoReceiverLog) << "Sending EOS directly to mux sink pad (asynchronously)";
    
    std::thread([muxSinkPad]() {
        const gboolean ret = gst_pad_send_event(muxSinkPad, gst_event_new_eos());
        if (!ret) {
            qCCritical(GstVideoReceiverLog) << "Branch EOS was NOT sent";
        } else {
            qCDebug(GstVideoReceiverLog) << "Branch EOS was sent successfully.";
        }
        gst_object_unref(muxSinkPad);
    }).detach();

    return true;
}

void GstVideoReceiver::_shutdownDecodingBranch()
{
    if (_decoder) {
        GstObject *parent = gst_element_get_parent(_decoder);
        if (parent) {
            (void) gst_bin_remove(GST_BIN(_pipeline), _decoder);
            (void) gst_element_set_state(_decoder, GST_STATE_NULL);
            (void) gst_element_get_state(_decoder, nullptr, nullptr, GST_CLOCK_TIME_NONE);
            gst_clear_object(&parent);
        }

        gst_clear_object(&_decoder);
    }

    if (_videoSinkProbeId != 0 && _videoSink) {
        GstPad *sinkpad = gst_element_get_static_pad(_videoSink, "sink");
        if (sinkpad) {
            gst_pad_remove_probe(sinkpad, _videoSinkProbeId);
            gst_clear_object(&sinkpad);
        }
    }
    _videoSinkProbeId = 0;

    if (_eosProbeId != 0 && _eosProbePad) {
        // Probe was installed on the source pad in _onNewSourcePad; remove from that exact pad — not from _decoder, which may already be cleared above.
        gst_pad_remove_probe(_eosProbePad, _eosProbeId);
    }
    _eosProbeId = 0;
    gst_clear_object(&_eosProbePad);

    _lastVideoFrameTime = 0;

    if (_videoSink) {
        GstObject *parent = gst_element_get_parent(_videoSink);
        if (parent) {
            (void) gst_bin_remove(GST_BIN(_pipeline), _videoSink);
            (void) gst_element_set_state(_videoSink, GST_STATE_NULL);
            (void) gst_element_get_state(_videoSink, nullptr, nullptr, GST_CLOCK_TIME_NONE);
            gst_clear_object(&parent);
        }
        gst_clear_object(&_videoSink);
    }

    _removingDecoder = false;

    if (_decoding) {
        _decoding = false;
        qCDebug(GstVideoReceiverLog) << "Decoding stopped";
        _dispatchSignal([this]() { emit decodingChanged(_decoding); });
    }

    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-decoding-stopped");
}

void GstVideoReceiver::_shutdownRecordingBranch()
{
    // Guard against double invocation: may be called by either the EOS path or the
    // fallback timer. If _fileSink is already NULL it was already cleaned up.
    if (!_fileSink) {
        qCDebug(GstVideoReceiverLog) << "_shutdownRecordingBranch called but _fileSink is already NULL, skipping";
        return;
    }

    if (_keyframeWatchId != 0 && _recorderValve) {
        GstPad *probepad = gst_element_get_static_pad(_recorderValve, "src");
        if (probepad) {
            gst_pad_remove_probe(probepad, _keyframeWatchId);
            gst_clear_object(&probepad);
        }
        _keyframeWatchId = 0;
    }

    (void) gst_element_set_state(_fileSink, GST_STATE_NULL);
    (void) gst_element_get_state(_fileSink, nullptr, nullptr, GST_CLOCK_TIME_NONE);
    gst_bin_remove(GST_BIN(_pipeline), _fileSink);
    gst_clear_object(&_fileSink);

    _removingRecorder = false;

    if (_recording) {
        _recording = false;
        qCDebug(GstVideoReceiverLog) << "Recording stopped";
        _dispatchSignal([this]() { emit recordingChanged(_recording); });
    }

    if (_recordingStopRequested) {
        _recordingStopRequested = false;
        _dispatchSignal([this]() { emit onStopRecordingComplete(STATUS_OK); });
    }

    GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-recording-stopped");
}

bool GstVideoReceiver::_needDispatch()
{
    return _worker->needDispatch();
}

void GstVideoReceiver::_dispatchSignal(Task emitter)
{
    _signalDepth += 1;
    emitter();
    _signalDepth -= 1;
}

gboolean GstVideoReceiver::_onBusMessage(GstBus * /* bus */, GstMessage *msg, gpointer data)
{
    if (!msg || !data) {
        qCCritical(GstVideoReceiverLog) << "Invalid parameters in _onBusMessage: msg=" << msg << "data=" << data;
        return TRUE;
    }

    GstVideoReceiver *pThis = static_cast<GstVideoReceiver*>(data);

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
        gchar *debug = nullptr;
        GError *error = nullptr;
        gst_message_parse_error(msg, &error, &debug);

        if (debug) {
            qCDebug(GstVideoReceiverLog) << "GStreamer debug:" << debug;
            g_clear_pointer(&debug, g_free);
        }

        if (error) {
            qCCritical(GstVideoReceiverLog) << "GStreamer error:" << error->message;
            g_clear_error(&error);
        }

        if (pThis->_pipeline) {
            GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(pThis->_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-error");
        }

        // Determine whether the error originated from the recording branch (sinkbin)
        // by walking the GObject ancestry of the error source. If yes, we clean up
        // only the recording and keep the main stream alive. A mp4mux failure (e.g.
        // "Could not multiplex stream" from a mid-stream caps change) must NOT kill
        // the video display — those are completely independent pipeline branches.
        bool isRecordingBranchError = false;
        {
            GstObject *obj = GST_MESSAGE_SRC(msg) ? GST_OBJECT_CAST(gst_object_ref(GST_MESSAGE_SRC(msg))) : nullptr;
            while (obj && !isRecordingBranchError) {
                gchar *name = gst_object_get_name(obj);
                isRecordingBranchError = (name && g_str_has_prefix(name, "sinkbin"));
                g_free(name);
                GstObject *parent = gst_object_get_parent(obj);
                gst_object_unref(obj);
                obj = parent;
            }
            if (obj) {
                gst_object_unref(obj);
            }
        }

        if (isRecordingBranchError) {
            qCWarning(GstVideoReceiverLog) << "Recording branch error — cleaning up recording, stream stays alive";
            pThis->_worker->dispatch([pThis]() {
                // Close the recorder valve first so no further data flows into
                // the (now-dead) sinkbin while we tear it down.
                if (pThis->_recorderValve) {
                    g_object_set(pThis->_recorderValve, "drop", TRUE, nullptr);
                }
                
                // If mp4mux failed, it returned GST_FLOW_ERROR upstream, which killed
                // the recorderQueue's source task. We must resurrect the queue task
                // by cycling it through READY, otherwise the next recording will silently fail.
                GstElement *recQueue = nullptr;
                if (pThis->_recorderValve) {
                    GstPad *valveSink = gst_element_get_static_pad(pThis->_recorderValve, "sink");
                    if (valveSink) {
                        GstPad *queueSrc = gst_pad_get_peer(valveSink);
                        if (queueSrc) {
                            recQueue = GST_ELEMENT(gst_pad_get_parent(queueSrc));
                            gst_object_unref(queueSrc);
                        }
                        gst_object_unref(valveSink);
                    }
                }

                // Detach the valve from the sinkbin without sending EOS
                // (mp4mux is already in error state; EOS would be ignored).
                if (pThis->_recorderValve) {
                    GstPad *valveSrc = gst_element_get_static_pad(pThis->_recorderValve, "src");
                    if (valveSrc) {
                        GstPad *peer = gst_pad_get_peer(valveSrc);
                        if (peer) {
                            gst_pad_unlink(valveSrc, peer);
                            gst_object_unref(peer);
                        }
                        gst_object_unref(valveSrc);
                    }
                }
                
                // _shutdownRecordingBranch guards against double-call via _fileSink NULL check.
                pThis->_removingRecorder = false;
                pThis->_recordingStopRequested = false;
                pThis->_shutdownRecordingBranch();

                // Reset the queue now that the downstream is detached
                if (recQueue) {
                    gst_element_set_state(recQueue, GST_STATE_READY);
                    gst_element_set_state(recQueue, GST_STATE_PLAYING);
                    gst_object_unref(recQueue);
                }

                pThis->_dispatchSignal([pThis]() {
                    emit pThis->onStopRecordingComplete(VideoReceiver::STATUS_FAIL);
                });
            });
        } else {
#if defined(QGC_HAS_GST_GLMEMORY_GPU_PATH) || defined(QGC_HAS_GST_D3D11_GPU_PATH) || defined(QGC_HAS_GST_D3D12_GPU_PATH)
            // Drop bridge-cached devices defensively. D3D11/D3D12 errors are most often device-loss
            // (DXGI_ERROR_DEVICE_REMOVED on driver reset / TDR / GPU detach); GST_MESSAGE_ERROR
            // doesn't carry a structured device-lost code, so reset on any error rather than
            // letting the next pipeline restart re-use a potentially-dead cached device. Cost on
            // false positives is one device re-discovery on next prime — already paid on cold start.
            // Render-thread mapTextures calls currentDevice() with transfer-full ownership now,
            // so an in-flight mapTextures keeps its own ref alive across this reset.
            GstContextBridgeRegistry::resetAllBridges();
#endif
            pThis->_worker->dispatch([pThis]() {
                qCDebug(GstVideoReceiverLog) << "Stopping because of error";
                pThis->stop();
            });
        }
        break;
    }
    case GST_MESSAGE_WARNING: {
        // GStreamer posts WARNING for caps mismatches, decoder fallbacks, clock drift —
        // surfacing keeps these visible without escalating to STATUS_FAIL.
        gchar *debug = nullptr;
        GError *error = nullptr;
        gst_message_parse_warning(msg, &error, &debug);
        qCWarning(GstVideoReceiverLog) << "GStreamer warning:"
                                       << (error ? error->message : "(no message)")
                                       << "debug:" << (debug ? debug : "(none)");
        g_clear_error(&error);
        g_clear_pointer(&debug, g_free);
        break;
    }
    case GST_MESSAGE_EOS:
        pThis->_worker->dispatch([pThis]() {
            qCDebug(GstVideoReceiverLog) << "Received EOS";
            pThis->_handleEOS();
        });
        break;
    case GST_MESSAGE_STREAM_COLLECTION: {
        GstStreamCollection *collection = nullptr;
        gst_message_parse_stream_collection(msg, &collection);
        if (!collection) {
            break;
        }
        // SELECT_STREAMS keeps decodebin3 from instantiating audio decoder branches.
        GList *selectedIds = nullptr;
        const guint nStreams = gst_stream_collection_get_size(collection);
        for (guint i = 0; i < nStreams; ++i) {
            GstStream *stream = gst_stream_collection_get_stream(collection, i);
            const GstStreamType type = gst_stream_get_stream_type(stream);
            if (type & GST_STREAM_TYPE_VIDEO) {
                selectedIds = g_list_append(selectedIds,
                    g_strdup(gst_stream_get_stream_id(stream)));
            }
        }
        if (selectedIds) {
            GstEvent *event = gst_event_new_select_streams(selectedIds);
            gst_element_send_event(GST_ELEMENT(GST_MESSAGE_SRC(msg)), event);
            g_list_free_full(selectedIds, g_free);
        }
        gst_object_unref(collection);
        break;
    }
    case GST_MESSAGE_QOS: {
        guint64 processed = 0, dropped = 0;
        gst_message_parse_qos_stats(msg, nullptr, &processed, &dropped);

        gint64 jitter = 0;
        gdouble proportion = 0;
        gint quality = 0;
        gst_message_parse_qos_values(msg, &jitter, &proportion, &quality);

        pThis->_processedFrames = processed;
        pThis->_droppedFrames = dropped;
        pThis->_currentJitterNs = jitter;
        pThis->_qosProportion = proportion;
        pThis->_qosQuality = quality;
        pThis->_dispatchSignal([pThis]() { emit pThis->decoderStatsChanged(); });
        break;
    }
    case GST_MESSAGE_ELEMENT: {
        const GstStructure *structure = gst_message_get_structure(msg);
        if (!gst_structure_has_name(structure, "GstBinForwarded")) {
            break;
        }

        GstMessage *forward_msg = nullptr;
        gst_structure_get(structure, "message", GST_TYPE_MESSAGE, &forward_msg, NULL);
        if (!forward_msg) {
            break;
        }

        if (GST_MESSAGE_TYPE(forward_msg) == GST_MESSAGE_EOS) {
            // Only respond to EOS from the sinkbin (recording branch). The message source
            // of the forwarded inner message is the element that originally posted the EOS.
            // _handleEOS() guards against spurious calls via _removingRecorder and _fileSink checks.
            GstObject *msgSrc = GST_MESSAGE_SRC(forward_msg);
            gchar *srcName = msgSrc ? gst_object_get_name(msgSrc) : nullptr;
            qCDebug(GstVideoReceiverLog) << "GstBinForwarded EOS from:" << (srcName ? srcName : "(unknown)");
            g_free(srcName);

            if (pThis->_removingRecorder) {
                pThis->_worker->dispatch([pThis]() {
                    qCDebug(GstVideoReceiverLog) << "Received branch EOS from sinkbin";
                    pThis->_handleEOS();
                });
            }
        }

        gst_clear_message(&forward_msg);
        break;
    }
    case GST_MESSAGE_STATE_CHANGED: {
        if (GST_MESSAGE_SRC(msg) != GST_OBJECT(pThis->_pipeline)) {
            break;
        }
        GstState oldState = GST_STATE_NULL, newState = GST_STATE_NULL;
        gst_message_parse_state_changed(msg, &oldState, &newState, nullptr);
        if (newState == GST_STATE_PLAYING && oldState != GST_STATE_PLAYING) {
            GstClockTime min = 0, max = 0;
            GstQuery *q = gst_query_new_latency();
            if (gst_element_query(pThis->_pipeline, q)) {
                gboolean live = FALSE;
                gst_query_parse_latency(q, &live, &min, &max);
            }
            gst_query_unref(q);
            qCDebug(GstVideoReceiverLog).noquote()
                << "Pipeline PLAYING:" << pThis->_uri
                << "decoder:" << (pThis->_decoderName.isEmpty() ? QStringLiteral("(pending)") : pThis->_decoderName)
                << "min-latency:" << (min / 1000000) << "ms"
                << "max-latency:" << (max / 1000000) << "ms";
        }
        break;
    }
    case GST_MESSAGE_LATENCY:
        pThis->_worker->dispatch([pThis]() {
            if (pThis->_pipeline) {
                (void) gst_bin_recalculate_latency(GST_BIN(pThis->_pipeline));
            }
        });
        pThis->_dispatchSignal([pThis]() { emit pThis->latencyChanged(); });
        break;
    default:
        break;
    }

    return TRUE;
}

void GstVideoReceiver::_onNewPad(GstElement *element, GstPad *pad, gpointer data)
{
    GstVideoReceiver *self = static_cast<GstVideoReceiver*>(data);

    if (element == self->_source) {
        self->_onNewSourcePad(pad);
    } else if (element == self->_decoder) {
        self->_onNewDecoderPad(pad);
    } else {
        qCDebug(GstVideoReceiverLog) << "Unexpected call!";
    }
}

void GstVideoReceiver::_wrapWithGhostPad(GstElement *element, GstPad *pad, gpointer data)
{
    Q_UNUSED(data)

    gchar *name = gst_pad_get_name(pad);
    if (!name) {
        qCCritical(GstVideoReceiverLog) << "gst_pad_get_name() failed";
        return;
    }

    GstPad *ghostpad = gst_ghost_pad_new(name, pad);
    if (!ghostpad) {
        qCCritical(GstVideoReceiverLog) << "gst_ghost_pad_new() failed";
        g_clear_pointer(&name, g_free);
        return;
    }

    g_clear_pointer(&name, g_free);

    (void) gst_pad_set_active(ghostpad, TRUE);

    if (!gst_element_add_pad(GST_ELEMENT_PARENT(element), ghostpad)) {
        qCCritical(GstVideoReceiverLog) << "gst_element_add_pad() failed";
    }
}

void GstVideoReceiver::_linkPad(GstElement *element, GstPad *pad, gpointer data)
{
    gchar *name = gst_pad_get_name(pad);
    if (!name) {
        qCCritical(GstVideoReceiverLog) << "gst_pad_get_name() failed";
        return;
    }

    GstPad *sinkPad = gst_element_get_static_pad(GST_ELEMENT(data), "sink");
    if (sinkPad) {
        if (gst_pad_is_linked(sinkPad)) {
            // parsebin can emit multiple pad-added events (e.g. video, audio, rtcp).
            // Since our parser only has one sink pad, subsequent links will fail.
            // Silently ignore them to avoid spamming the log.
            gst_object_unref(sinkPad);
            g_clear_pointer(&name, g_free);
            return;
        }
        gst_object_unref(sinkPad);
    }

    if (!gst_element_link_pads(element, name, GST_ELEMENT(data), "sink")) {
        qCCritical(GstVideoReceiverLog) << "gst_element_link_pads() failed for" << name;
    }

    g_clear_pointer(&name, g_free);
}

gboolean GstVideoReceiver::_padProbe(GstElement *element, GstPad *pad, gpointer user_data)
{
    Q_UNUSED(element)

    int *probeRes = static_cast<int*>(user_data);
    *probeRes |= 1;

    GstCaps *filter = gst_caps_from_string("application/x-rtp");
    if (filter) {
        GstCaps *caps = gst_pad_query_caps(pad, nullptr);
        if (caps) {
            if (!gst_caps_is_any(caps) && gst_caps_can_intersect(caps, filter)) {
                *probeRes |= 2;
            }

            gst_clear_caps(&caps);
        }

        gst_clear_caps(&filter);
    }

    return TRUE;
}

GstPadProbeReturn GstVideoReceiver::_teeProbe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    Q_UNUSED(pad); Q_UNUSED(info)

    if (user_data) {
        GstVideoReceiver *pThis = static_cast<GstVideoReceiver*>(user_data);
        pThis->_noteTeeFrame();
    }

    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn GstVideoReceiver::_videoSinkProbe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    Q_UNUSED(pad); Q_UNUSED(info)

    if (user_data) {
        GstVideoReceiver *pThis = static_cast<GstVideoReceiver*>(user_data);

        if (pThis->_resetVideoSink) {
            pThis->_resetVideoSink = false;

#if 0 // FIXME: this makes MPEG2-TS playing smooth but breaks RTSP
           gst_pad_send_event(pad, gst_event_new_flush_start());
           gst_pad_send_event(pad, gst_event_new_flush_stop(TRUE));

           GstBuffer* buf;

           if ((buf = gst_pad_probe_info_get_buffer(info)) != nullptr) {
               GstSegment* seg;

               if ((seg = gst_segment_new()) != nullptr) {
                   gst_segment_init(seg, GST_FORMAT_TIME);

                   seg->start = buf->pts;

                   gst_pad_send_event(pad, gst_event_new_segment(seg));

                   gst_segment_free(seg);
                   seg = nullptr;
               }

               gst_pad_set_offset(pad, -static_cast<gint64>(buf->pts));
           }
#endif
        }

        pThis->_noteVideoSinkFrame();
    }

    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn GstVideoReceiver::_eosProbe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    Q_UNUSED(pad);
    Q_ASSERT(user_data);

    if (info) {
        const GstEvent *event = gst_pad_probe_info_get_event(info);
        if (GST_EVENT_TYPE(event) == GST_EVENT_EOS) {
            GstVideoReceiver *pThis = static_cast<GstVideoReceiver*>(user_data);
            pThis->_noteEndOfStream();
        }
    }

    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn GstVideoReceiver::_keyframeWatch(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    Q_UNUSED(pad)

    if (!info || !user_data) {
        qCCritical(GstVideoReceiverLog) << "Invalid arguments";
        return GST_PAD_PROBE_DROP;
    }

    GstBuffer *buf = gst_pad_probe_info_get_buffer(info);
    const gboolean isDelta = GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_DELTA_UNIT);
    qCDebug(GstVideoReceiverLog) << "_keyframeWatch: Received buffer PTS =" << (buf ? buf->pts : 0) << "DTS =" << (buf ? buf->dts : 0) << "isDelta =" << isDelta;

    guint dropCount = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(pad), "drop-count"));
    if (isDelta && dropCount < 60) {
        g_object_set_data(G_OBJECT(pad), "drop-count", GUINT_TO_POINTER(dropCount + 1));
        // wait for a keyframe
        return GST_PAD_PROBE_DROP;
    }

    qCDebug(GstVideoReceiverLog) << "Got keyframe (or timed out), stop dropping buffers";

    GstVideoReceiver *pThis = static_cast<GstVideoReceiver*>(user_data);
    pThis->_keyframeWatchId = 0;
    pThis->_dispatchSignal([pThis]() { emit pThis->recordingStarted(pThis->recordingOutput()); });

    return GST_PAD_PROBE_REMOVE;
}

GstVideoWorker::GstVideoWorker(QObject *parent)
    : QThread(parent)
{
    qCDebug(GstVideoReceiverLog) << this;
}

GstVideoWorker::~GstVideoWorker()
{
    qCDebug(GstVideoReceiverLog) << this;
}

bool GstVideoWorker::needDispatch() const
{
    return (QThread::currentThread() != this);
}

void GstVideoWorker::dispatch(Task task)
{
    QMutexLocker lock(&_taskQueueSync);
    _taskQueue.enqueue(task);
    _taskQueueUpdate.wakeOne();
}

void GstVideoWorker::shutdown()
{
    if (needDispatch()) {
        dispatch([this]() { _shutdown = true; });
        (void) QThread::wait(2000);
    } else {
        QThread::quit();
    }
}

void GstVideoWorker::run()
{
    while (!_shutdown) {
        _taskQueueSync.lock();

        while (_taskQueue.isEmpty()) {
            _taskQueueUpdate.wait(&_taskQueueSync);
        }

        const Task task = _taskQueue.dequeue();

        _taskQueueSync.unlock();

        task();
    }
}
