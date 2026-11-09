#include "recorder.h"

#include "audio.h"

#include <QMutexLocker>
#include <algorithm>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/hook.h>

namespace {

// One waveform bar per this many samples: 33 ms, about 30 bars a second.
constexpr qsizetype LevelSlice = Audio::Rate / 30;

}

struct Recorder::Listeners {
    spa_hook core{};
    spa_hook stream{};
    pw_core_events coreEvents{};
    pw_stream_events streamEvents{};
};

Recorder::Recorder(QObject *parent) : QObject(parent), m_listeners(new Listeners) {
    pw_init(nullptr, nullptr);
    m_listeners->coreEvents.version = PW_VERSION_CORE_EVENTS;
    m_listeners->coreEvents.error = &Recorder::onCoreError;
    m_listeners->streamEvents.version = PW_VERSION_STREAM_EVENTS;
    m_listeners->streamEvents.process = &Recorder::onProcess;
    connectToPipeWire();
}

Recorder::~Recorder() {
    stop();
    disconnectFromPipeWire();
    delete m_listeners;
    pw_deinit();
}

bool Recorder::connectToPipeWire() {
    if (!m_loop) {
        m_loop = pw_thread_loop_new("asmir-audio", nullptr);
        if (!m_loop || pw_thread_loop_start(m_loop) != 0)
            return false;
    }
    pw_thread_loop_lock(m_loop);
    if (!m_context)
        m_context = pw_context_new(pw_thread_loop_get_loop(m_loop), nullptr, 0);
    if (m_context && !m_core) {
        m_core = pw_context_connect(m_context, nullptr, 0);
        if (m_core) {
            spa_zero(m_listeners->core);
            pw_core_add_listener(m_core, &m_listeners->core, &m_listeners->coreEvents, this);
        }
    }
    m_coreLost = false;
    const bool ok = m_core != nullptr;
    pw_thread_loop_unlock(m_loop);
    return ok;
}

void Recorder::disconnectFromPipeWire() {
    if (!m_loop)
        return;
    pw_thread_loop_lock(m_loop);
    if (m_core) {
        spa_hook_remove(&m_listeners->core);
        pw_core_disconnect(m_core);
        m_core = nullptr;
    }
    if (m_context) {
        pw_context_destroy(m_context);
        m_context = nullptr;
    }
    pw_thread_loop_unlock(m_loop);
    pw_thread_loop_stop(m_loop);
    pw_thread_loop_destroy(m_loop);
    m_loop = nullptr;
}

// PipeWire restarted or dropped the connection; reconnect on the next start.
void Recorder::onCoreError(void *data, uint32_t id, int, int res, const char *) {
    if (id == PW_ID_CORE && res == -EPIPE)
        static_cast<Recorder *>(data)->m_coreLost = true;
}

bool Recorder::start(QString *error) {
    if (m_stream)
        return true;
    if (m_coreLost)
        disconnectFromPipeWire();
    if (!m_core && !connectToPipeWire()) {
        if (error)
            *error = tr("Recording needs PipeWire.");
        return false;
    }

    {
        QMutexLocker lock(&m_mutex);
        m_incoming.clear();
    }
    m_remainder.clear();
    m_samples.clear();
    m_samples.reserve(Audio::Rate * 30);
    m_levelFrom = 0;
    m_heard = false;

    pw_thread_loop_lock(m_loop);
    // A short quantum keeps the waveform in step with the voice. The node
    // name is what volume mixers show while asmir listens.
    pw_properties *props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
                                             PW_KEY_NODE_NAME, "asmir", PW_KEY_MEDIA_NAME, "Dictation",
                                             PW_KEY_APP_NAME, "asmir", PW_KEY_NODE_LATENCY, "320/16000", nullptr);
    m_stream = pw_stream_new(m_core, "asmir", props);
    bool ok = m_stream != nullptr;
    if (ok) {
        spa_zero(m_listeners->stream);
        pw_stream_add_listener(m_stream, &m_listeners->stream, &m_listeners->streamEvents, this);

        // Ask for whisper's own format; PipeWire converts whatever the
        // microphone delivers.
        spa_audio_info_raw info{};
        info.format = SPA_AUDIO_FORMAT_F32;
        info.rate = Audio::Rate;
        info.channels = 1;
        info.position[0] = SPA_AUDIO_CHANNEL_MONO;
        uint8_t buffer[1024];
        spa_pod_builder builder;
        spa_pod_builder_init(&builder, buffer, sizeof(buffer));
        const spa_pod *params[1] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)};

        ok = pw_stream_connect(m_stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                               static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
                               params, 1) == 0;
        if (!ok) {
            spa_hook_remove(&m_listeners->stream);
            pw_stream_destroy(m_stream);
            m_stream = nullptr;
        }
    }
    pw_thread_loop_unlock(m_loop);

    if (!ok && error)
        *error = tr("Couldn't start recording.");
    return ok;
}

QVector<float> Recorder::stop() {
    if (!m_stream)
        return {};
    pw_thread_loop_lock(m_loop);
    spa_hook_remove(&m_listeners->stream);
    pw_stream_destroy(m_stream);
    m_stream = nullptr;
    pw_thread_loop_unlock(m_loop);

    read();
    QVector<float> samples;
    samples.swap(m_samples);
    return samples;
}

// On PipeWire's thread: copy the new audio and let the main thread take it.
void Recorder::onProcess(void *data) {
    auto *self = static_cast<Recorder *>(data);
    pw_buffer *buffer = pw_stream_dequeue_buffer(self->m_stream);
    if (!buffer)
        return;
    const spa_data &block = buffer->buffer->datas[0];
    if (block.data && block.chunk) {
        const uint32_t offset = std::min(block.chunk->offset, block.maxsize);
        const uint32_t size = std::min(block.chunk->size, block.maxsize - offset);
        {
            QMutexLocker lock(&self->m_mutex);
            self->m_incoming.append(static_cast<const char *>(block.data) + offset, size);
        }
        QMetaObject::invokeMethod(self, &Recorder::read, Qt::QueuedConnection);
    }
    pw_stream_queue_buffer(self->m_stream, buffer);
}

void Recorder::read() {
    QByteArray chunk;
    {
        QMutexLocker lock(&m_mutex);
        chunk.swap(m_incoming);
    }
    if (chunk.isEmpty())
        return;
    const qsizetype before = m_samples.size();
    Audio::appendFloats(chunk, m_remainder, m_samples);
    if (!m_heard) {
        for (qsizetype i = before; i < m_samples.size(); ++i) {
            if (m_samples.at(i) != 0.0f) {
                m_heard = true;
                emit heard();
                break;
            }
        }
    }
    if (!m_stream)
        return;
    while (m_samples.size() - m_levelFrom >= LevelSlice) {
        emit level(Audio::level(m_samples.constData() + m_levelFrom, LevelSlice));
        m_levelFrom += LevelSlice;
    }
}
