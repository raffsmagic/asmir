QT += core gui qml quick network

CONFIG += c++17 release link_pkgconfig
PKGCONFIG += libpipewire-0.3
TARGET = asmir
TEMPLATE = app

# whisper.cpp, built as static libraries by bin/build-whisper.
WHISPER = $$PWD/third_party/whisper.cpp
WHISPER_BUILD = $$PWD/build/whisper
INCLUDEPATH += $$WHISPER/include $$WHISPER/ggml/include
LIBS += -L$$WHISPER_BUILD/src -lwhisper \
        -L$$WHISPER_BUILD/ggml/src -lggml -lggml-cpu -lggml-base \
        -lLayerShellQtInterface -lpthread -ldl
PRE_TARGETDEPS += $$WHISPER_BUILD/src/libwhisper.a

# A build tuned for its own CPU (ASMIR_NATIVE=1 bin/build) skips the
# x86-64-v3 check, which wouldn't describe what that build needs.
ASMIR_ENV_NATIVE = $$(ASMIR_NATIVE)
equals(ASMIR_ENV_NATIVE, 1): DEFINES += ASMIR_NATIVE

HEADERS += \
    src/audio.h \
    src/client.h \
    src/config.h \
    src/dictation.h \
    src/models.h \
    src/recorder.h \
    src/setup.h \
    src/theme.h \
    src/transcriber.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/audio.cpp \
    src/client.cpp \
    src/config.cpp \
    src/dictation.cpp \
    src/models.cpp \
    src/recorder.cpp \
    src/setup.cpp \
    src/theme.cpp \
    src/transcriber.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
