# asmir

![The waveform asmir shows while it listens](docs/overlay.png)

Push-to-talk dictation for [Omarchy](https://omarchy.org). Hold Right Alt,
talk, let go, and what you said is pasted where you were typing. While it
listens, a small waveform sits at the bottom of the screen in your theme's
colors.

The name is a nod to ASMR: you whisper, it listens. (Fittingly, it runs on
whisper.cpp.)

Speech is recognized on your own computer by
[whisper.cpp](https://github.com/ggml-org/whisper.cpp), with a small quantized
Whisper model (`base.en-q5_1`, 60 MB). Nothing is sent anywhere.

## Install

asmir plugs into Omarchy's dictation the same way the other backends under
Setup > Defaults > Dictation do, so Omarchy's keys work with it: hold Right Alt
to talk, or press Alt + Space to start and stop.

```sh
git clone --recursive https://github.com/raffsmagic/asmir
cd asmir
bin/dev-install   # builds asmir and installs it in ~/.local/bin
asmir setup       # downloads the model and makes asmir the dictation backend
```

To use another backend again, pick it under Setup > Defaults > Dictation.

### Requirements

- Omarchy, or another Hyprland setup with PipeWire (see [Without Omarchy](#without-omarchy))
- An x86-64 processor with AVX2: Intel from 2013 on (Haswell and newer), or
  AMD from 2015 on, including every Ryzen
- To build: Qt 6, layer-shell-qt and a C++ compiler, which Omarchy includes.
  whisper.cpp is built with CMake; when CMake isn't installed, `bin/build-whisper`
  runs it through `uvx`.

## How it works

- **A small daemon** (`asmir serve`) keeps the model loaded, so recording
  starts as soon as you press the key. `asmir setup` has Omarchy start it at
  login.
- **Recording** goes through PipeWire's `pw-record`, so each dictation uses
  whatever microphone is the default at that moment.
- **The overlay** is a layer-shell surface: it shows on the screen you're
  using, never takes the keyboard or the mouse, follows your Omarchy theme,
  window rounding and language as they change, and uses your Omarchy font.
- **The text** is pasted through Omarchy's own paste command, which sends the
  right paste keys for the focused window (Ctrl+Shift+V in terminals).

## Commands

| Command | What it does |
| --- | --- |
| `asmir setup` | Make asmir Omarchy's dictation backend |
| `asmir start` / `stop` / `toggle` | Record, then paste what was said |
| `asmir status [--json]` | `idle`, `recording`, `transcribing`, or `stopped` |
| `asmir quit` | Stop the daemon |
| `asmir serve` | Run the daemon in the foreground |
| `asmir transcribe <file> [--json]` | Print what's said in an audio file (uses ffmpeg) |
| `asmir model` | Show the model in use and the ones available |
| `asmir model download [name]` | Fetch a model, checked against its SHA-256 |

## Settings

Everything is optional. Put any of these in `~/.config/asmir/config`:

```ini
model = base.en-q5_1      # small.en-q5_1 is more accurate and slower
language = en             # "auto" with a multilingual model such as small-q5_1
prompt = Omarchy, Hyprland, Neovim.   # words and spelling to expect
threads = 0               # 0 picks half the CPU threads, up to 8
trailing_space = true     # keeps back-to-back dictations apart
```

The daemon reads the settings when it starts: `asmir quit`, and the next
dictation starts it again.

## Without Omarchy

On another Wayland compositor with layer-shell support (Hyprland, Sway, KDE
Plasma, niri), bind a key to `asmir start` on press and `asmir stop` on
release. Without Omarchy's paste command, asmir copies the text to the
clipboard and says so in the overlay.

## Development

```sh
bin/build     # build/app/asmir
bin/test      # unit tests, plus a real transcription when the model is downloaded
```

- `ASMIR_DRY_RUN=1` logs the transcript instead of pasting it.
- `ASMIR_DEBUG=1` prints timings and whisper.cpp's own log.
- `ASMIR_NATIVE=1 bin/build-whisper` tunes whisper.cpp for the building CPU
  only (delete `build/whisper` first).

Messages are translated into Omarchy's languages in `src/i18n/`; fixes from
native speakers are very welcome. Issues and pull requests are welcome too.

## License

MIT. See [LICENSE](LICENSE). Built on [whisper.cpp](https://github.com/ggml-org/whisper.cpp)
(MIT) and the [Omarchy](https://github.com/omacom/omarchy) app templates.
