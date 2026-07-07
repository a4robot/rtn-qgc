#include "VideoStreamServer.h"

#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(VideoStreamServerLog, "WebBridge.VideoStreamServer")

#ifdef QGC_GST_STREAMING

#include <QtCore/QDateTime>
#include <QtCore/QtEndian>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <cstring>

//-----------------------------------------------------------------------------
// PROTOCOL.md §9.2 binary frame layout (all multi-byte fields little-endian):
//   offset 0  (2B) magic       u16 = 0x4656
//   offset 2  (1B) version     u8  = 1
//   offset 3  (1B) streamId    u8  = streamIndex from videoConfig
//   offset 4  (1B) flags       u8  = bit0 keyframe, bits1-7 reserved (0)
//   offset 5  (3B) reserved    zero-filled
//   offset 8  (8B) timestampUs u64
//   offset 16 (nB) payload     one complete Annex-B H.264 access unit
//-----------------------------------------------------------------------------
/// GStreamer-side state, kept out of VideoStreamServer.h so the header has zero GStreamer
/// dependency (see class docs). Owned exclusively by VideoStreamServer; constructed/destroyed
/// alongside it.
struct VideoStreamServer::Impl {
    GstElement *pipeline = nullptr; // borrowed — not owned, must outlive attach()/stop() bracket
    GstElement *tee = nullptr;      // borrowed — not owned
    GstPad *teeSrcPad = nullptr;    // owned: ref from gst_element_request_pad_simple()
    GstElement *bin = nullptr;      // owned: our tap branch (queue -> h264parse -> appsink)
    GstElement *appsink = nullptr;  // borrowed — owned by `bin`

    // Monotonic-PTS-to-epoch anchor (see docs on _timestampUsFor below).
    bool haveAnchor = false;
    quint64 anchorEpochUs = 0;
    quint64 anchorPtsNs = 0;

    // Last videoConfig sent, so we only re-emit configReady() when something actually changed.
    bool sentConfig = false;
    QByteArray lastSps;
    QByteArray lastPps;
    int lastWidth = 0;
    int lastHeight = 0;

    /// GstAppSinkCallbacks::new_sample. Static member (not a free function) so it can reach
    /// VideoStreamServer's private _impl/_handleAccessUnit — a nested class member has access
    /// to the enclosing class's privates — while still keeping every GStreamer type out of the
    /// header. Runs on a GStreamer streaming thread; see the emission-safety notes below.
    static GstFlowReturn newSample(GstAppSink *appsink, gpointer userData);
};

namespace
{
constexpr quint16 kMagic = 0x4656;
constexpr quint8 kVersion = 1;
constexpr quint8 kFlagKeyframe = 0x01;
constexpr int kHeaderSize = 16;

struct NalUnit {
    int offset; // offset of the NAL header byte (i.e. just past the start code)
    int length; // length of the NAL unit payload (header byte included), start code excluded
    int type;   // H.264 nal_unit_type (low 5 bits of the NAL header byte)
};

// Finds the next Annex-B start code (00 00 01 or 00 00 00 01) at or after `from`.
// Returns the offset of the first 0x00 of the start code, or -1 if none found.
int findStartCode(const QByteArray &buf, int from, int *scLen)
{
    const int n = buf.size();
    for (int i = from; i + 2 < n; ++i) {
        if (static_cast<quint8>(buf.at(i)) == 0 && static_cast<quint8>(buf.at(i + 1)) == 0) {
            if (static_cast<quint8>(buf.at(i + 2)) == 1) {
                *scLen = 3;
                return i;
            }
            if (i + 3 < n && static_cast<quint8>(buf.at(i + 2)) == 0 && static_cast<quint8>(buf.at(i + 3)) == 1) {
                *scLen = 4;
                return i;
            }
        }
    }
    return -1;
}

// Splits one Annex-B access unit into its constituent NAL units. Used only on keyframe access
// units (once per GOP, since h264parse's config-interval=-1 repeats SPS/PPS before every IDR) to
// pull out the SPS/PPS for the §9.1 videoConfig JSON — the wire payload itself is handed through
// byte-for-byte with start codes intact, this parsing is purely a side channel.
QList<NalUnit> splitAnnexB(const QByteArray &au)
{
    QList<NalUnit> nals;
    int scLen = 0;
    int pos = findStartCode(au, 0, &scLen);
    while (pos >= 0) {
        const int nalStart = pos + scLen;
        if (nalStart >= au.size()) {
            break;
        }
        int nextScLen = 0;
        const int nextPos = findStartCode(au, nalStart, &nextScLen);
        const int nalEnd = (nextPos >= 0) ? nextPos : au.size();
        if (nalEnd > nalStart) {
            const int type = static_cast<quint8>(au.at(nalStart)) & 0x1F;
            nals.append({nalStart, nalEnd - nalStart, type});
        }
        if (nextPos < 0) {
            break;
        }
        pos = nextPos;
        scLen = nextScLen;
    }
    return nals;
}

QByteArray buildFrame(quint8 streamId, bool keyframe, quint64 timestampUs, const QByteArray &accessUnit)
{
    QByteArray framed;
    framed.reserve(kHeaderSize + accessUnit.size());
    framed.resize(kHeaderSize);

    char *h = framed.data();
    const quint16 magicLE = qToLittleEndian<quint16>(kMagic);
    std::memcpy(h + 0, &magicLE, sizeof(magicLE));
    h[2] = static_cast<char>(kVersion);
    h[3] = static_cast<char>(streamId);
    h[4] = static_cast<char>(keyframe ? kFlagKeyframe : 0);
    h[5] = h[6] = h[7] = 0;
    const quint64 tsLE = qToLittleEndian<quint64>(timestampUs);
    std::memcpy(h + 8, &tsLE, sizeof(tsLE));

    framed.append(accessUnit);
    return framed;
}

} // namespace

// GstAppSinkCallbacks::new_sample (declared inside Impl, see above). Runs on a GStreamer
// streaming thread, not the VideoStreamServer's own thread; all GStreamer-typed work (pulling
// the sample, mapping the buffer, scanning Annex-B/caps) happens here, and only plain-Qt-typed
// results are handed to _handleAccessUnit(), which does the actual (cross-thread, safe -- see
// the frameReady()/configReady() doc comments in VideoStreamServer.h) signal emission.
GstFlowReturn VideoStreamServer::Impl::newSample(GstAppSink *appsink, gpointer userData)
{
    auto *self = static_cast<VideoStreamServer *>(userData);
    if (!self) {
        return GST_FLOW_OK;
    }

    GstSample *sample = gst_app_sink_pull_sample(appsink);
    if (!sample) {
        // NULL is the documented return when the appsink is EOS/flushing.
        return GST_FLOW_EOS;
    }

    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstCaps *caps = gst_sample_get_caps(sample);
    if (!buffer || !caps) {
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    GstMapInfo map{};
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        qCWarning(VideoStreamServerLog) << self << "onNewSample: gst_buffer_map failed";
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    // Deep copy: the buffer is only valid until gst_sample_unref() below.
    const QByteArray accessUnit(reinterpret_cast<const char *>(map.data), static_cast<int>(map.size));
    const bool keyframe = !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);

    // timestampUs: GstVideoReceiver's PTS is pipeline running-time (monotonic from stream
    // start), not wall-clock epoch time. WebBridge::serverTimeUs() solves the equivalent problem
    // for its own clock by anchoring a wall-clock timestamp once and then advancing it by an
    // elapsed monotonic delta; we do the same here using the buffer's own PTS as the monotonic
    // source: capture wall time at the first buffer with a valid PTS, then for every later
    // buffer add (currentPtsNs - firstPtsNs) to that anchor. This keeps timestamps monotonic and
    // immune to wall-clock steps for the life of one attach(), at the cost of being anchored to
    // "roughly when streaming started" rather than true capture time (which GStreamer does not
    // give us without RTP sender-report / NTP correlation, out of scope here).
    quint64 timestampUs;
    if (GST_BUFFER_PTS_IS_VALID(buffer)) {
        const quint64 ptsNs = static_cast<quint64>(GST_BUFFER_PTS(buffer));
        if (!self->_impl->haveAnchor) {
            self->_impl->haveAnchor = true;
            self->_impl->anchorEpochUs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000;
            self->_impl->anchorPtsNs = ptsNs;
        }
        // PTS can regress slightly across a DISCONT (e.g. source restart); clamp rather than
        // underflow into a huge unsigned delta.
        const quint64 deltaNs = (ptsNs >= self->_impl->anchorPtsNs) ? (ptsNs - self->_impl->anchorPtsNs) : 0;
        timestampUs = self->_impl->anchorEpochUs + deltaNs / 1000;
    } else {
        // No PTS at all (shouldn't normally happen post-parse): best-effort wall clock.
        timestampUs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000;
    }

    int width = 0;
    int height = 0;
    QByteArray sps;
    QByteArray pps;
    if (keyframe) {
        // SPS/PPS + resolution are only (re)computed on keyframes: h264parse's config-interval=-1
        // guarantees they're present in-band before every IDR (PROTOCOL.md §9.2), and this is the
        // only point a §9.1 videoConfig update could legitimately be needed.
        if (GstStructure *s = gst_caps_get_structure(caps, 0)) {
            gst_structure_get_int(s, "width", &width);
            gst_structure_get_int(s, "height", &height);
        }
        const QList<NalUnit> nals = splitAnnexB(accessUnit);
        for (const NalUnit &nal : nals) {
            if (nal.type == 7 && sps.isEmpty()) { // SPS
                sps = accessUnit.mid(nal.offset, nal.length);
            } else if (nal.type == 8 && pps.isEmpty()) { // PPS
                pps = accessUnit.mid(nal.offset, nal.length);
            }
        }
    }

    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);

    // All GStreamer-typed work is done; hand plain Qt-typed data to the object for framing,
    // config diffing, and (cross-thread-safe) emission.
    self->_handleAccessUnit(keyframe, timestampUs, accessUnit, width, height, sps, pps);

    return GST_FLOW_OK;
}

VideoStreamServer::VideoStreamServer(quint8 streamId, QObject *parent)
    : QObject(parent)
    , _impl(new Impl)
    , _streamId(streamId)
{
    qCDebug(VideoStreamServerLog) << this << "streamId" << streamId;
}

VideoStreamServer::~VideoStreamServer()
{
    stop();
    delete _impl;
}

bool VideoStreamServer::attach(void *pipelineVoid, void *teeVoid)
{
    if (_attached) {
        qCDebug(VideoStreamServerLog) << this << "attach: already attached";
        return true;
    }

    GstElement *pipeline = static_cast<GstElement *>(pipelineVoid);
    GstElement *tee = static_cast<GstElement *>(teeVoid);
    if (!pipeline || !tee) {
        qCWarning(VideoStreamServerLog) << this << "attach: null pipeline/tee";
        return false;
    }

    GstElement *queue = nullptr;
    GstElement *parse = nullptr;
    GstElement *appsink = nullptr;
    GstElement *bin = nullptr;
    GstPad *teeSrcPad = nullptr;
    bool ok = false;

    do {
        queue = gst_element_factory_make("queue", nullptr);
        parse = gst_element_factory_make("h264parse", nullptr);
        appsink = gst_element_factory_make("appsink", nullptr);
        if (!queue || !parse || !appsink) {
            qCCritical(VideoStreamServerLog) << this << "attach: gst_element_factory_make failed"
                                              << "queue" << !!queue << "h264parse" << !!parse << "appsink" << !!appsink;
            break;
        }

        // Leaky (drop-oldest) + unbounded-by-count/bytes, matching GstVideoReceiver's recorder
        // queue: this tap must never backpressure the shared tee (which would stall the decode
        // and/or recording branches too). Losing frames here is fine — the wire protocol is
        // explicitly lossy video state (PROTOCOL.md §9.2: "recovery is wait for next keyframe").
        g_object_set(queue,
                     "leaky", 2, // downstream: drop oldest buffered data
                     "max-size-time", static_cast<guint64>(1 * GST_SECOND),
                     "max-size-buffers", 0,
                     "max-size-bytes", 0,
                     nullptr);

        // config-interval=-1: repeat SPS/PPS in-band before every IDR, regardless of upstream
        // caps — required by PROTOCOL.md §9.2 ("keyframes must be preceded by in-band SPS/PPS").
        // h264parse also normalizes whatever the tee provides (byte-stream or avc) into the
        // format requested by downstream caps, so this works regardless of the video source type.
        g_object_set(parse, "config-interval", -1, nullptr);

        GstCaps *sinkCaps = gst_caps_new_simple("video/x-h264",
                                                 "stream-format", G_TYPE_STRING, "byte-stream",
                                                 "alignment", G_TYPE_STRING, "au",
                                                 nullptr);
        g_object_set(appsink,
                     "caps", sinkCaps,
                     "sync", FALSE,     // this is a network relay, not a display sink — no clock wait
                     "async", FALSE,
                     "max-buffers", 1,  // only ever hold the newest access unit
                     "drop", TRUE,      // ...and drop older ones rather than block upstream
                     "emit-signals", FALSE, // we use gst_app_sink_set_callbacks(), not the GObject signal
                     nullptr);
        gst_caps_unref(sinkCaps);

        bin = gst_bin_new(nullptr);
        if (!bin) {
            qCCritical(VideoStreamServerLog) << this << "attach: gst_bin_new failed";
            break;
        }

        // Add all elements to the bin before linking (GStreamer requires a shared parent bin
        // before pads can be linked) — same ordering GstVideoReceiver::_makeFileSink() uses.
        gst_bin_add_many(GST_BIN(bin), queue, parse, appsink, nullptr);

        if (!gst_element_link_many(queue, parse, appsink, nullptr)) {
            qCCritical(VideoStreamServerLog) << this << "attach: gst_element_link_many(queue, h264parse, appsink) failed";
            break;
        }

        // Ghost-pad the queue's sink so the outside world (the tee) links to `bin` as a single
        // opaque element, exactly like GstVideoReceiver::_makeFileSink()'s "sinkbin" wraps
        // parser->mux->filesink behind one ghost pad.
        GstPad *queueSinkPad = gst_element_get_static_pad(queue, "sink");
        if (!queueSinkPad) {
            qCCritical(VideoStreamServerLog) << this << "attach: gst_element_get_static_pad(queue, sink) failed";
            break;
        }
        GstPad *ghostPad = gst_ghost_pad_new("sink", queueSinkPad);
        gst_object_unref(queueSinkPad);
        if (!ghostPad) {
            qCCritical(VideoStreamServerLog) << this << "attach: gst_ghost_pad_new failed";
            break;
        }
        gst_element_add_pad(bin, ghostPad);

        // Block upstream RECONFIGURE events at our branch boundary. Without this, caps
        // renegotiation inside h264parse/appsink could propagate GST_EVENT_RECONFIGURE
        // backward through the tee into the shared source, which GstVideoReceiver.cc documents
        // (see the dropReconfigure lambda around its decoder/recorder valve sink pads, and the
        // matching probe on _makeFileSink()'s ghost sink pad) as corrupting the whole pipeline
        // (re-triggers "pad-added", double-links, etc). Same defensive probe here.
        gst_pad_add_probe(ghostPad, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
            [](GstPad *, GstPadProbeInfo *info, gpointer) -> GstPadProbeReturn {
                if (GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info)) == GST_EVENT_RECONFIGURE) {
                    return GST_PAD_PROBE_DROP;
                }
                return GST_PAD_PROBE_OK;
            },
            nullptr, nullptr);

        // Extra ref before gst_bin_add(): gst_bin_add() sinks bin's floating ref into a
        // bin-owned strong ref, so gst_bin_remove() in stop() would otherwise finalize it
        // immediately, leaving `bin` (this->_impl->bin) dangling before we're done with it.
        // Mirrors GstVideoReceiver::startRecording()'s `gst_object_ref(_fileSink)` before its
        // own gst_bin_add().
        gst_object_ref(bin);
        gst_bin_add(GST_BIN(pipeline), bin);

        teeSrcPad = gst_element_request_pad_simple(tee, "src_%u");
        if (!teeSrcPad) {
            qCCritical(VideoStreamServerLog) << this << "attach: gst_element_request_pad_simple(tee, src_%u) failed";
            break;
        }

        GstPad *binSinkPad = gst_element_get_static_pad(bin, "sink");
        if (!binSinkPad) {
            qCCritical(VideoStreamServerLog) << this << "attach: gst_element_get_static_pad(bin, sink) failed";
            break;
        }
        const GstPadLinkReturn linkRet = gst_pad_link(teeSrcPad, binSinkPad);
        gst_object_unref(binSinkPad);
        if (linkRet != GST_PAD_LINK_OK) {
            qCCritical(VideoStreamServerLog) << this << "attach: gst_pad_link(tee, bin) failed, error" << linkRet;
            break;
        }

        // Pipeline is already running (PLAYING/PAUSED) — a freshly-added element defaults to
        // NULL and won't receive data until its state is synced with its new parent. Mirrors
        // GstVideoReceiver::startRecording()'s gst_element_sync_state_with_parent(_fileSink).
        if (!gst_element_sync_state_with_parent(bin)) {
            qCCritical(VideoStreamServerLog) << this << "attach: gst_element_sync_state_with_parent(bin) failed";
            break;
        }

        GstAppSinkCallbacks callbacks{};
        callbacks.new_sample = &VideoStreamServer::Impl::newSample;
        gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &callbacks, this, nullptr);

        ok = true;
    } while (false);

    if (!ok) {
        // Best-effort teardown of whatever got built before the failure. Unlink before
        // releasing the request pad, same ordering as stop(), in case an earlier step (e.g.
        // gst_pad_link) already succeeded before a later one (e.g. sync_state_with_parent) failed.
        if (teeSrcPad) {
            GstPad *binSinkPad = bin ? gst_element_get_static_pad(bin, "sink") : nullptr;
            if (binSinkPad) {
                gst_pad_unlink(teeSrcPad, binSinkPad);
                gst_object_unref(binSinkPad);
            }
            gst_element_release_request_pad(tee, teeSrcPad);
            gst_object_unref(teeSrcPad);
        }
        if (bin) {
            GstObject *parent = gst_element_get_parent(bin);
            if (parent) {
                gst_bin_remove(GST_BIN(pipeline), bin);
                gst_object_unref(parent);
            }
            gst_element_set_state(bin, GST_STATE_NULL);
            gst_object_unref(bin); // drops our extra ref (and the factory-make elements it owns)
        } else {
            // Elements never made it into a bin — free them directly.
            if (appsink) gst_object_unref(appsink);
            if (parse) gst_object_unref(parse);
            if (queue) gst_object_unref(queue);
        }
        return false;
    }

    _impl->pipeline = pipeline;
    _impl->tee = tee;
    _impl->teeSrcPad = teeSrcPad;
    _impl->bin = bin;
    _impl->appsink = appsink;
    _attached = true;

    qCDebug(VideoStreamServerLog) << this << "attached, streamId" << _streamId;
    return true;
}

void VideoStreamServer::stop()
{
    if (!_attached) {
        return;
    }

    // Stop callbacks first so a racing streaming-thread invocation can't touch state we're
    // about to tear down (mirrors GstAppSinkAdapter::teardown()).
    if (_impl->appsink) {
        GstAppSinkCallbacks empty{};
        gst_app_sink_set_callbacks(GST_APP_SINK(_impl->appsink), &empty, nullptr, nullptr);
    }

    // Unlink from the tee and hand the request pad back before tearing the branch down —
    // same ordering as GstVideoReceiver::_unlinkBranch() (unlink first to stop data flow).
    if (_impl->teeSrcPad) {
        GstPad *binSinkPad = _impl->bin ? gst_element_get_static_pad(_impl->bin, "sink") : nullptr;
        if (binSinkPad) {
            gst_pad_unlink(_impl->teeSrcPad, binSinkPad);
            gst_object_unref(binSinkPad);
        }
        if (_impl->tee) {
            gst_element_release_request_pad(_impl->tee, _impl->teeSrcPad);
        }
        gst_object_unref(_impl->teeSrcPad);
        _impl->teeSrcPad = nullptr;
    }

    if (_impl->bin) {
        // No muxer/EOS choreography needed here (unlike the recording branch's mp4mux, which
        // must flush a moov atom) — this tap has no stateful container to finalize, so we can
        // drive it straight to NULL. remove-then-null-then-unref matches
        // GstVideoReceiver::_shutdownDecodingBranch()'s ordering for _decoder/_videoSink.
        if (_impl->pipeline) {
            gst_bin_remove(GST_BIN(_impl->pipeline), _impl->bin);
        }
        gst_element_set_state(_impl->bin, GST_STATE_NULL);
        gst_element_get_state(_impl->bin, nullptr, nullptr, GST_CLOCK_TIME_NONE);
        gst_object_unref(_impl->bin); // final unref of the extra ref taken in attach()
        _impl->bin = nullptr;
    }

    _impl->appsink = nullptr; // owned by _impl->bin, already freed above
    _impl->pipeline = nullptr;
    _impl->tee = nullptr;
    _impl->haveAnchor = false;
    _impl->anchorEpochUs = 0;
    _impl->anchorPtsNs = 0;
    _impl->sentConfig = false;
    _impl->lastSps.clear();
    _impl->lastPps.clear();
    _impl->lastWidth = 0;
    _impl->lastHeight = 0;

    _attached = false;
    qCDebug(VideoStreamServerLog) << this << "stopped, streamId" << _streamId;
}

// Plain-Qt-typed: builds the §9.2 wire frame and emits frameReady(), and (on keyframes) diffs
// the extracted config against the last one sent and emits configReady() if it changed. All
// GStreamer-typed extraction happened in onNewSample() above; this method deliberately touches
// no GStreamer type, matching the header's declared intent to stay GStreamer-free.
void VideoStreamServer::_handleAccessUnit(bool keyframe, quint64 timestampUs, const QByteArray &accessUnit,
                                           int width, int height, const QByteArray &sps, const QByteArray &pps)
{
    if (keyframe) {
        const bool changed = !_impl->sentConfig
                              || width != _impl->lastWidth
                              || height != _impl->lastHeight
                              || (!sps.isEmpty() && sps != _impl->lastSps)
                              || (!pps.isEmpty() && pps != _impl->lastPps);
        if (changed) {
            _impl->sentConfig = true;
            _impl->lastWidth = width;
            _impl->lastHeight = height;
            if (!sps.isEmpty()) {
                _impl->lastSps = sps;
            }
            if (!pps.isEmpty()) {
                _impl->lastPps = pps;
            }

            QJsonObject cfg;
            cfg[QStringLiteral("codec")] = QStringLiteral("h264");
            cfg[QStringLiteral("streamIndex")] = static_cast<int>(_streamId);
            if (width > 0) {
                cfg[QStringLiteral("width")] = width;
            }
            if (height > 0) {
                cfg[QStringLiteral("height")] = height;
            }
            if (!_impl->lastSps.isEmpty()) {
                cfg[QStringLiteral("sps")] = QString::fromLatin1(_impl->lastSps.toBase64());
            }
            if (!_impl->lastPps.isEmpty()) {
                cfg[QStringLiteral("pps")] = QString::fromLatin1(_impl->lastPps.toBase64());
            }

            // Cross-thread emit — see VideoStreamServer.h doc comment on configReady().
            emit configReady(_streamId, cfg);
        }
    }

    const QByteArray framed = buildFrame(_streamId, keyframe, timestampUs, accessUnit);

    // Cross-thread emit — see the frameReady()/configReady() doc comments in VideoStreamServer.h
    // for why this is safe without explicit QMetaObject::invokeMethod marshaling.
    emit frameReady(_streamId, framed);
}

#else // !QGC_GST_STREAMING

// Inert stub: no GstElement, no GStreamer includes at all. VideoStreamServer::Impl and the
// _impl/_handleAccessUnit members are themselves declared under the matching #ifdef in
// VideoStreamServer.h, so in this branch the class simply has no such members to define.

VideoStreamServer::VideoStreamServer(quint8 streamId, QObject *parent)
    : QObject(parent)
    , _streamId(streamId)
{
}

VideoStreamServer::~VideoStreamServer() = default;

bool VideoStreamServer::attach(void *pipeline, void *tee)
{
    Q_UNUSED(pipeline);
    Q_UNUSED(tee);
    qCWarning(VideoStreamServerLog) << this << "attach: GStreamer streaming support not compiled in (QGC_GST_STREAMING undefined)";
    return false;
}

void VideoStreamServer::stop()
{
}

#endif // QGC_GST_STREAMING
