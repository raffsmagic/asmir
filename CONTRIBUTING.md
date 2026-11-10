# Contributing to asmir

Glad you're here. asmir does one thing: push-to-talk dictation for Omarchy,
fast, private and good-looking. Everything below protects that.

## What we're after

- **Bug fixes.** Something misheard, mis-pasted, crashed, or looked off.
  Include what you did, what happened, and your Omarchy version and hardware.
- **Speed.** Faster first words, faster text, less memory. Bring before and
  after numbers from your machine (`bin/bench` measures them).
- **Accuracy.** Better defaults, prompts, or model handling, with `bin/bench`
  numbers showing the word error rate doesn't get worse.
- **Omarchy integration.** Keeping up with Omarchy as it changes: themes,
  languages, dictation plumbing, new hardware.
- **Translations.** Messages live in `src/i18n/`, one file per Omarchy
  language. Native speakers fixing our wording are heroes.
- **Docs.** If the README confused you, it'll confuse the next person.

## What we'll say no to (nicely)

- **Anything that sends audio, text or usage data off the machine.** No cloud
  speech services, no analytics, no telemetry, no accounts. The only network
  request asmir makes is downloading a model you asked for.
- **Settings for the sake of settings.** Every option is something to explain,
  test and support forever. Better defaults beat more knobs.
- **Features beyond dictation.** Voice commands, assistants and chat are great
  ideas for their own projects. We'd happily make asmir easy to build on
  instead (see "Where it's headed" in the README).
- **Big dependencies.** asmir stays a single small binary. New libraries need
  a very good reason.
- **Changes to how other backends or other projects work.** asmir lives
  alongside them; it doesn't compete by poking at them.

Not sure which side your idea falls on? Open an issue and ask before you
build. It saves everyone a rewrite.

## How a pull request gets in

1. **One change per pull request**, with a short description of why.
2. **CI passes.** `bin/build` and `bin/test` run on every pull request.
3. **Logic gets a test**, one that fails without your change. A rename or a
   string doesn't need one.
4. **Visible changes get a screenshot** of the overlay, in a default theme.
5. **New messages are translated** into every file in `src/i18n/`; the tests
   check. Machine translation is fine as a start, so say so in the pull
   request and native speakers can polish it.
6. **Match the code around you.** Comments explain why, in full sentences.
   Commit messages are short and imperative ("Paste into XWayland apps").
7. **The maintainer has the final say,** and will always explain a no.

Using AI to help write your change is fine. You're still the author: read
every line, run it, and be ready to explain it.

## Be kind

Assume good intent, keep feedback about the code, and remember everyone here
is a volunteer. That includes the people building the other dictation tools.
