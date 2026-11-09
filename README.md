# asmir

![The waveform asmir shows while it listens](docs/overlay.png)

**Whisper to your Omarchy.**

Hold Right Alt. Say the thing. Let go. Your words land where your cursor was.
That's it. That's the app.

asmir is push-to-talk dictation built for [Omarchy](https://omarchy.org) and
nothing else on purpose. It listens through a tiny waveform at the bottom of
the screen, dressed in your theme, and turns speech into text right on your
machine with [whisper.cpp](https://github.com/ggml-org/whisper.cpp). No cloud,
no account, no subscription. Your rambling stays yours.

*Why "asmir"? It's ASMR for your keyboard: you whisper, it listens. And yes,
it runs on whisper.cpp. We couldn't help ourselves.*

## Get it

```sh
git clone https://github.com/raffsmagic/asmir
cd asmir/pkgbuild && makepkg -si   # builds the 0.1.0 release into a real package
asmir setup                        # fetch the model and plug asmir into Omarchy's dictation
```

The AUR package is on its way: the AUR has paused new account sign-ups for
now. Once it's up, this becomes `yay -S asmir`.

`asmir setup` does the whole dance: downloads the speech model (60 MB, checked
against its SHA-256), makes asmir your backend under
Setup > Defaults > Dictation, has it start at login, and reloads Hyprland. Then:

| Key | Does |
| --- | --- |
| Hold **Right Alt** | Talk while held, paste when you let go |
| **Alt + Space** | Start, then press again to stop and paste |

Changed your mind? Pick another backend under Setup > Defaults > Dictation.
No hard feelings.

### Building it yourself

```sh
git clone --recursive https://github.com/raffsmagic/asmir
cd asmir
bin/dev-install   # builds and installs into ~/.local/bin
asmir setup
```

## What you need

- **Omarchy.** Other Hyprland or layer-shell setups work too, minus the
  conveniences (see [Off the beaten path](#off-the-beaten-path)).
- **A processor from this decade, roughly:** x86-64 with AVX2, meaning Intel
  from 2013 on (Haswell and newer) or AMD from 2015 on, every Ryzen included.
  That's what keeps whisper quick. ARM isn't supported yet.
- **A microphone.** asmir uses whichever one is your default right now, so
  plugging in a headset mid-session just works.

## Omarchy-native, all the way down

- **Your theme.** Accent, background, window rounding and font come from
  Omarchy, and the overlay follows a theme switch live.
- **Your language.** Messages come in all 50 of Omarchy's languages and follow
  Setup > Language as you change it.
- **Your keys.** asmir plugs into `omarchy-dictation`, so Omarchy's Right Alt
  and Alt + Space work out of the box, and so does anything else that calls it.
- **Your paste.** Text goes in through Omarchy's own paste, which knows a
  terminal wants Ctrl+Shift+V.
- **Out of your way.** The overlay is a layer-shell surface: it shows on the
  screen you're using and never steals focus or clicks.
- **Honest about the mic.** A microphone that's been idle needs a moment to
  wake up. Until the first real sound arrives, a faint glint runs across flat
  bars. When they come alive, talk.

## Tune it (or don't)

The defaults are the point. If you insist, `~/.config/asmir/config` takes:

```ini
model = base.en-q5_1      # small.en-q5_1: more accurate, takes its time
language = en             # "auto" with a multilingual model like small-q5_1
prompt = Omarchy, Hyprland, Neovim.   # words and spellings to expect
threads = 0               # 0 = half your CPU threads, up to 8
trailing_space = true     # back-to-back dictations don't run together
```

Run `asmir quit` after editing; the next dictation starts it fresh.

## Commands

| Command | What it does |
| --- | --- |
| `asmir setup` | Make asmir Omarchy's dictation backend |
| `asmir start` / `stop` / `toggle` | Record, then paste what was said |
| `asmir status [--json]` | `idle`, `recording`, `transcribing`, or `stopped` |
| `asmir quit` | Stop the daemon |
| `asmir serve` | Run the daemon in the foreground |
| `asmir transcribe <file> [--json]` | Print what's said in an audio file (needs ffmpeg) |
| `asmir model` | The model in use and the ones on offer |
| `asmir model download [name]` | Fetch a model, checksum and all |

## Off the beaten path

On another Wayland compositor with layer-shell (Hyprland, Sway, KDE Plasma,
niri), bind `asmir start` to a key press and `asmir stop` to its release.
Without Omarchy's paste command, asmir leaves the text on the clipboard and
tells you so. GNOME doesn't do layer-shell, so the overlay won't show there.

## Under the hood

- C++ and Qt 6, built like Omarchy's own apps, with the overlay in QML.
- whisper.cpp 1.9.5, pinned as a submodule and compiled into the binary.
- A quantized Whisper model (`base.en-q5_1`): a third of the full size.
- A small daemon keeps the model loaded between dictations, and only borrows
  whisper's working memory while it transcribes.
- Recording through PipeWire's `pw-record`, which follows your default mic.

## Where it's headed

- Faster first words: record through PipeWire directly instead of starting a
  recorder for every dictation.
- GPU acceleration through Vulkan, for the bigger and more accurate models.
- ARM, for Omarchy on Apple Silicon and Snapdragon.
- A way for voice assistants and other tools to use asmir as their local ears.

Want one of these, or something else? See [CONTRIBUTING.md](CONTRIBUTING.md).

## Hacking on it

```sh
bin/build     # build/app/asmir
bin/test      # unit tests, plus a real transcription when the model is downloaded
```

- `ASMIR_DRY_RUN=1` logs the transcript instead of pasting it.
- `ASMIR_DEBUG=1` prints timings and whisper.cpp's own log.
- `ASMIR_NATIVE=1 bin/build-whisper` tunes whisper.cpp for the building CPU
  only (delete `build/whisper` first).

## License

MIT. See [LICENSE](LICENSE). Standing on the shoulders of
[whisper.cpp](https://github.com/ggml-org/whisper.cpp) and the
[Omarchy](https://github.com/omacom/omarchy) app templates.
