#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

// Small pure helpers for what the microphone hands over and what whisper
// returns, kept apart from the recorder and whisper.cpp so they can be tested.
namespace Audio {

// whisper.cpp only takes 16 kHz mono float samples, which is what asmir asks
// PipeWire (and ffmpeg) for, so nothing is ever resampled.
constexpr int Rate = 16000;

// Appends the whole little-endian 32-bit float samples in chunk to out. A
// sample split across two reads is held in remainder until the rest arrives.
void appendFloats(const QByteArray &chunk, QByteArray &remainder, QVector<float> &out);

// How loud a run of samples is, from 0 (silence) to 1 (loud speech), on a
// decibel scale so quiet speech still moves the waveform.
float level(const float *samples, qsizetype count);

// The loudest sample, from 0 to 1.
float peak(const QVector<float> &samples);

// Where the speech in a recording starts and ends, as sample positions, with
// a little margin either side. Empty when nothing louder than the room lasts
// long enough to be a word: a muted mic, a cough, a key click.
struct Span {
    qsizetype from = 0;
    qsizetype to = 0;
    bool isEmpty() const { return to <= from; }
};
Span speech(const QVector<float> &samples);

// True when a transcript has far more words than seconds of speech allow,
// the sign of whisper repeating itself.
bool looksRunaway(const QString &text, int seconds);

// What whisper heard, without the tags it writes for sounds that aren't
// words, such as [BLANK_AUDIO] or (typing), and with its spacing tidied.
QString cleanTranscript(const QString &text);

}
