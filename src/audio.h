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

// What whisper heard, without the tags it writes for sounds that aren't
// words, such as [BLANK_AUDIO] or (typing), and with its spacing tidied.
QString cleanTranscript(const QString &text);

}
