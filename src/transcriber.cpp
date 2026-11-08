#include "transcriber.h"

#include "audio.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <malloc.h>
#include <whisper.h>

namespace {

// whisper.cpp and ggml log every step of loading and decoding to stderr; keep
// the daemon's output to what matters.
void quiet(ggml_log_level, const char *, void *) {}

}

Transcriber::Transcriber(QObject *parent) : QObject(parent) {
    if (qEnvironmentVariableIsEmpty("ASMIR_DEBUG"))
        whisper_log_set(quiet, nullptr);
}

Transcriber::~Transcriber() {
    if (m_context)
        whisper_free(m_context);
}

bool Transcriber::loadNow(const QString &modelPath, int threads, QString *error) {
    m_threads = threads;
    if (m_context && modelPath == m_modelPath)
        return true;
    if (m_context) {
        whisper_free(m_context);
        m_context = nullptr;
    }
    if (!QFileInfo::exists(modelPath)) {
        if (error)
            *error = tr("No speech model yet. Run: asmir setup");
        return false;
    }
    // The model stays loaded (about 60 MB for base.en-q5_1); the working
    // memory whisper decodes in (about 100 MB more) only exists while it
    // transcribes, so the daemon stays small between dictations.
    whisper_context_params params = whisper_context_default_params();
    m_context = whisper_init_from_file_with_params_no_state(modelPath.toLocal8Bit().constData(), params);
    if (!m_context) {
        if (error)
            *error = tr("Couldn't load the speech model.");
        return false;
    }
    m_modelPath = modelPath;
    return true;
}

QString Transcriber::transcribeNow(const QVector<float> &audio, const QString &language, const QString &prompt,
                                   QString *error) {
    if (!m_context) {
        if (error)
            *error = tr("Couldn't load the speech model.");
        return {};
    }

    // whisper.cpp warns about, and guesses badly on, input under a second, so
    // pad a short recording with silence.
    QVector<float> samples = audio;
    const int minimum = Audio::Rate + Audio::Rate / 10;
    if (samples.size() < minimum)
        samples.resize(minimum, 0.0f);

    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.n_threads = m_threads;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_special = false;
    params.print_timestamps = false;
    params.no_timestamps = true;
    // Each dictation stands alone; carrying text over from the last one makes
    // whisper repeat it.
    params.no_context = true;
    params.suppress_blank = true;
    // No temperature fallback: the retries rarely fix dictation and can take
    // several times as long.
    params.temperature_inc = 0.0f;
    params.greedy.best_of = 1;

    const QByteArray languageBytes = language.toUtf8();
    params.language = languageBytes.constData();
    params.detect_language = false;
    const QByteArray promptBytes = prompt.toUtf8();
    params.initial_prompt = prompt.isEmpty() ? nullptr : promptBytes.constData();

    whisper_state *state = whisper_init_state(m_context);
    if (!state) {
        if (error)
            *error = tr("Speech recognition failed.");
        return {};
    }
    QString text;
    const bool ok = whisper_full_with_state(m_context, state, params, samples.constData(), int(samples.size())) == 0;
    if (ok) {
        const int segments = whisper_full_n_segments_from_state(state);
        for (int i = 0; i < segments; ++i)
            text += QString::fromUtf8(whisper_full_get_segment_text_from_state(state, i));
    }
    whisper_free_state(state);
    // Hand the freed buffers back to the system rather than keeping them for
    // the next dictation.
    malloc_trim(0);

    if (!ok) {
        if (error)
            *error = tr("Speech recognition failed.");
        return {};
    }
    return Audio::cleanTranscript(text);
}

void Transcriber::load(const QString &modelPath, int threads) {
    QString error;
    const bool ok = loadNow(modelPath, threads, &error);
    emit loaded(ok, error);
}

void Transcriber::transcribe(quint64 id, const QVector<float> &audio, const QString &language, const QString &prompt) {
    QElapsedTimer timer;
    timer.start();
    QString error;
    const QString text = transcribeNow(audio, language, prompt, &error);
    if (!error.isEmpty())
        emit failed(id, error);
    else
        emit finished(id, text, int(timer.elapsed()));
}
