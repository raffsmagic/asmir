// asmir — push-to-talk dictation for Omarchy with whisper.cpp, and a small
// waveform at the bottom of the screen while it listens.

#include "client.h"
#include "config.h"
#include "dictation.h"
#include "models.h"
#include "setup.h"
#include "theme.h"
#include "transcriber.h"
#include "translator.h"

#include <LayerShellQt/Window>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStandardPaths>
#include <cstdio>
#include <cstring>

namespace {

const char *Version = "0.1.3";

const char *Usage = R"(asmir — push-to-talk dictation with whisper.cpp

Usage:
  asmir setup                    Make asmir Omarchy's dictation backend
  asmir start | stop | toggle    Record, then paste what was said (starts the daemon)
  asmir cancel                   Stop recording without pasting anything
  asmir status [--json]          Whether it's idle, recording, or transcribing
  asmir quit                     Stop the daemon
  asmir serve                    Run the daemon in the foreground
  asmir transcribe <file>        Print what's said in an audio file (needs ffmpeg)
  asmir model                    Show the model in use and the ones available
  asmir model download [name]    Fetch a model (default: tiny.en-q5_1)
  asmir --version

Settings live in ~/.config/asmir/config (model, language, prompt, threads,
trailing_space). Omarchy uses asmir through omarchy-dictation-asmir.
)";

// whisper.cpp is built for x86-64-v3 (see bin/build-whisper). On an older
// processor it would die on the first unknown instruction, so say why first.
bool processorSupported() {
#if defined(__x86_64__) && !defined(ASMIR_NATIVE)
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma") && __builtin_cpu_supports("f16c")
           && __builtin_cpu_supports("bmi2");
#else
    return true;
#endif
}

void print(const QString &text, FILE *stream = stdout) {
    std::fprintf(stream, "%s\n", qPrintable(text));
}

int serve(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("asmir");
    app.setApplicationVersion(Version);
    app.setDesktopFileName("asmir");
    app.setQuitOnLastWindowClosed(false);

    Translator translator;
    app.installTranslator(&translator);

    Dictation dictation;
    QString error;
    if (!dictation.listen(&error)) {
        print(error, stderr);
        return 1;
    }

    Theme theme;
    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("dictation", &dictation);
    engine.load(QUrl("qrc:/Main.qml"));
    auto *window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window)
        return 1;

    // A layer-shell surface, set up before the window first shows: it sits
    // above everything at the bottom of the screen that has focus, reserves no
    // space, and never takes the keyboard from the window being dictated into.
    if (auto *layer = LayerShellQt::Window::get(window)) {
        layer->setScope("asmir");
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setAnchors(LayerShellQt::Window::AnchorBottom);
        layer->setMargins({0, 0, 0, 56});
        layer->setExclusiveZone(-1);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layer->setWantsToBeOnActiveScreen(true);
    }

    return app.exec();
}

int control(const QString &command, bool json) {
    QString error;
    const QJsonObject reply = Client::send(command, &error);
    if (reply.isEmpty()) {
        print(error, stderr);
        return 1;
    }
    if (json)
        print(QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Compact)));
    else if (command == "status")
        print(reply.value("state").toString());
    if (!reply.value("ok").toBool()) {
        print(reply.value("error").toString(), stderr);
        return 1;
    }
    return 0;
}

int model(const QStringList &args) {
    const Config config = Config::load();
    if (args.value(0) == "download") {
        const QString name = args.value(1, config.model.isEmpty() ? Config::defaultModelName() : config.model);
        const Model *model = Models::find(name);
        if (!model) {
            print(QString("Unknown model: %1. Run `asmir model` to see the ones available.").arg(name), stderr);
            return 1;
        }
        QString error;
        if (!Models::download(*model, &error)) {
            print(error, stderr);
            return 1;
        }
        print(QString("Saved %1").arg(Config::modelsDirectory() + "/" + model->fileName()));
        return 0;
    }

    const QString path = config.modelPath();
    print(QString("In use: %1%2").arg(path, QFileInfo::exists(path) ? "" : "  (missing: run asmir model download)"));
    print("Available:");
    for (const auto &model : Models::known()) {
        const bool present = QFileInfo::exists(Config::modelsDirectory() + "/" + model.fileName());
        print(QString("  %1  %2 MB%3").arg(model.name, -14).arg(model.bytes / 1000000, 3).arg(present ? "  downloaded" : ""));
    }
    return 0;
}

int transcribe(const QString &file, bool json) {
    if (!QFileInfo::exists(file)) {
        print(QString("No such file: %1").arg(file), stderr);
        return 1;
    }
    // ffmpeg turns any audio into whisper's 16 kHz mono floats.
    QProcess ffmpeg;
    ffmpeg.start("ffmpeg", {"-nostdin", "-loglevel", "error", "-i", file, "-f", "f32le", "-ac", "1", "-ar", "16000", "-"});
    if (!ffmpeg.waitForFinished(120000) || ffmpeg.exitCode() != 0) {
        print(QString("ffmpeg couldn't read %1: %2").arg(file, QString::fromUtf8(ffmpeg.readAllStandardError()).trimmed()), stderr);
        return 1;
    }
    const QByteArray raw = ffmpeg.readAllStandardOutput();
    QVector<float> audio(raw.size() / int(sizeof(float)));
    std::memcpy(audio.data(), raw.constData(), size_t(audio.size()) * sizeof(float));

    const Config config = Config::load();
    Transcriber transcriber;
    QString error;
    QElapsedTimer timer;
    timer.start();
    if (!transcriber.loadNow(config.modelPath(), config.threadCount(), &error)) {
        print(error, stderr);
        return 1;
    }
    const qint64 loadMs = timer.restart();
    const QString text = transcriber.transcribeNow(audio, config.language, config.prompt, &error);
    const qint64 decodeMs = timer.elapsed();
    if (!error.isEmpty()) {
        print(error, stderr);
        return 1;
    }
    if (json) {
        const QJsonObject result{{"text", text},
                                 {"seconds", audio.size() / 16000.0},
                                 {"load_ms", loadMs},
                                 {"decode_ms", decodeMs},
                                 {"model", config.modelPath()},
                                 {"threads", config.threadCount()}};
        print(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
    } else {
        print(text);
    }
    return 0;
}

}

int main(int argc, char *argv[]) {
    const QString command = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    const bool needsWhisper = command == "serve" || command == "transcribe" || command == "start"
                              || command == "toggle" || command == "setup";
    if (needsWhisper && !processorSupported()) {
        std::fputs("asmir needs a processor with AVX2: an Intel CPU from 2013 on (Haswell or newer)\n"
                   "or an AMD CPU from 2015 on (including every Ryzen).\n", stderr);
        return 1;
    }
    if (command == "serve")
        return serve(argc, argv);

    QCoreApplication app(argc, argv);
    app.setApplicationName("asmir");
    app.setApplicationVersion(Version);
    QStringList args = app.arguments().mid(2);
    const bool json = args.removeAll("--json") > 0;

    if (command == "start" || command == "stop" || command == "toggle" || command == "cancel" || command == "status"
        || command == "quit")
        return control(command, json);
    if (command == "model")
        return model(args);
    if (command == "setup")
        return Setup::run();
    if (command == "transcribe" && !args.isEmpty())
        return transcribe(args.first(), json);
    if (command == "--version" || command == "version") {
        print(QString("asmir %1").arg(Version));
        return 0;
    }
    std::fputs(Usage, command.isEmpty() || command == "help" || command == "--help" ? stdout : stderr);
    return command.isEmpty() || command == "help" || command == "--help" ? 0 : 1;
}
