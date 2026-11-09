#pragma once

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QVector>

struct pw_thread_loop;
struct pw_context;
struct pw_core;
struct pw_stream;

// Records the default microphone as 16 kHz mono floats for whisper, and
// reports how loud each slice is so the overlay can draw the waveform.
//
// It talks to PipeWire directly. The connection to PipeWire stays open for
// the daemon's life, which uses no microphone, and a capture stream exists
// only while recording, so a press starts recording without first starting a
// recorder program. Each new stream goes to whichever microphone is the
// default at that moment, so a headset plugged in later just works.
class Recorder : public QObject {
    Q_OBJECT

public:
    explicit Recorder(QObject *parent = nullptr);
    ~Recorder() override;

    // False, with a message in error, when recording can't start.
    bool start(QString *error = nullptr);
    QVector<float> stop();
    bool recording() const { return m_stream != nullptr; }

signals:
    // About 30 times a second while recording, from 0 to 1.
    void level(float value);
    // Once per recording, when the first real sound arrives. A microphone that
    // PipeWire suspended after a few idle seconds delivers silence, or nothing,
    // for a moment while it wakes; words spoken before this are lost.
    void heard();

private:
    static void onProcess(void *data);
    static void onCoreError(void *data, uint32_t id, int seq, int res, const char *message);
    bool connectToPipeWire();
    void disconnectFromPipeWire();
    void read();

    pw_thread_loop *m_loop = nullptr;
    pw_context *m_context = nullptr;
    pw_core *m_core = nullptr;
    pw_stream *m_stream = nullptr;
    bool m_coreLost = false;
    struct Listeners;
    Listeners *m_listeners = nullptr;

    // Filled on PipeWire's thread, emptied on the main thread.
    QMutex m_mutex;
    QByteArray m_incoming;

    QByteArray m_remainder;
    QVector<float> m_samples;
    qsizetype m_levelFrom = 0;
    bool m_heard = false;
};
