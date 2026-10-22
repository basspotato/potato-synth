# Potato-Synth — Project Brief and Plan

Read `CLAUDE.md` first for build commands and rules. Update "Status" at the end of each session.

## Vision

A modern synth that breaks the usual osc → filter → amp layout and makes huge, deep sounds in very few clicks. The player shapes *ideas* (size, depth, grit, motion) instead of technical parameters.

## Concept: spectral morph engine (pending user confirmation)

- **Spectral core.** Each voice is a bank of up to 512 partials (additive resynthesis) rendered with SIMD oscillator banks. Nothing can alias: partials above Nyquist are simply dropped.
- **Four spectra on an XY pad.** Each corner holds a spectrum, which can be generated from a classic wave, drawn, or analysed from a dropped audio sample. The pad morphs between them per voice, and the morph position can be modulated.
- **Perceptual macros replace the classic chain.** 8 big knobs drive many spectral operations each:
  - **Size**: unison-style partial detune/spread and stereo decorrelation (supersaw width without stacking voices)
  - **Depth**: sub reinforcement and low-partial emphasis, mono-safe below about 120 Hz
  - **Grit**: spectral fold/smear harmonics (distortion without aliasing)
  - **Air**: high-partial tilt and noise-band shimmer
  - **Tone**: spectral filter (cutoff, resonance, formant shift) applied directly to partial amplitudes
  - **Motion**: internal LFO/phase drift for evolving sound
  - **Punch**: transient envelope on the spectrum
  - **Space**: built-in reverb/delay send
- **Minimal clicks.** The main page is the XY pad plus 8 macros and a preset browser. An "Under the hood" page exposes per-partial envelopes, macro ranges and a mod matrix for power users.
- **Post FX.** Small, fixed chain: glue compression, stereo width/bass mono, limiter. The "huge" character comes from the engine, not an FX rack.

## Decisions

| Area | Choice |
|---|---|
| Framework | JUCE 8.0.15, C++17, CMake + FetchContent |
| Formats / OS | VST3 on Windows 11 x64; VST3 + AU Universal on macOS 11+ |
| Builds | GitHub Actions, workflow based on Potato-Kicker's `build.yml` |
| Testing | pluginval strictness 10 + auval in CI; headless DSP tests |
| GUI | Potato house style, reusing `Look.h` / `Hud.h` / `Displays.h` |
| CPU budget | Under about 15% of one core for 8 voices × 256 partials at 48 kHz on a mid-range CPU |

## Roadmap (one feature per session)

**Phase 0: Scaffold**
1. CMakeLists, plugin identity, empty processor and editor, CI workflow copied from Potato-Kicker. Done when CI is green and pluginval passes.
2. Linux compile-check job, a cloud setup script, and a headless test target, so Claude sessions can build and test without a Windows or Mac machine.

**Phase 1: Spectral core**
3. `Spectrum` data type and generators (saw, square, sine stacks, and noise bands).
4. SIMD partial bank: render N partials per voice with phase/amp smoothing. Benchmark against the CPU budget before going further.
5. Voice manager: 16-voice poly, mono/legato, glide, voice stealing, amp envelope.

**Phase 2: Morph**
6. 4-corner XY morph with per-partial interpolation, plus a morph-position display.
7. Spectral resonance/formant "Tone" stage.

**Phase 3: Macros**
8. Macro system: each macro maps to many targets with curves. Wire Size, Depth, Grit and Air first.
9. Then Tone, Motion, Punch and Space. Tune them by ear against reference EDM sounds.

**Phase 4: Sample analysis**
10. Drop an audio file onto a corner to analyse it into a spectrum, on a background thread (never the audio thread).

**Phase 5: GUI** (separate sessions from DSP)
11. Main page: XY pad, 8 macros, a spectrum display.
12. "Under the hood" page with mod matrix and macro editing. Preset browser.

**Phase 6: Presets and polish**
13. Factory bank: ~100 presets (Supersaw, Lead, Pluck, Pad, Bass, Growl, FX). Each one must sound big with all macros at default.
14. CPU profiling, denormal checks, state round-trip tests, release build.

## Status

- Done: plan and CLAUDE.md drafted, then revised for the rule-breaking vision.
- Next: user picks the core concept and creates the repo, then Phase 0, step 1.
