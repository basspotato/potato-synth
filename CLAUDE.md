# Potato-Synth

Polyphonic spectral morph synth (VST3 + AU) for EDM. There's no osc → filter → amp chain. Each voice is a bank of partials, morphed across 4 spectra on an XY pad and shaped by 8 perceptual macros (Size, Depth, Grit, Air, Tone, Motion, Punch, Space). Goal: huge, deep sounds in minimal clicks. JUCE 8.0.15, C++17, CMake.
Targets: Windows 11 x64 (VST3), macOS 11+ Universal (VST3 + AU). Plan and status: `potato-synth-BRIEF.md`.

## Working style (token efficiency)

- Do the task. Don't narrate steps or recap finished work; end with 1-2 sentences on the outcome.
- Simple change = minimal action. Don't re-read files you just edited or add verification nobody asked for.
- Stay in scope: no refactors, renames, reformatting, extra features, options, dependencies or files that weren't asked for.
- Comments only where the logic is non-obvious.
- When the target file is known, go straight to it. Use the source layout below instead of searching the repo.
- Ask only when blocked on a decision that changes the outcome; otherwise take the obvious default and say which.
- One feature per session. Keep DSP and GUI changes in separate sessions.
- Show a short plan before any big change.
- When debugging a build error, quote only the relevant error lines.

## Git

- Never commit to `main` directly. Work on a branch and open a PR; CI must be green before merge.
- No force-push, history rewrites or branch deletion without asking.

## Build

```bash
# macOS (Universal)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target PotatoSynth_VST3 PotatoSynth_AU

# Windows (Visual Studio 2022)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target PotatoSynth_VST3
```

- Local JUCE checkout: `-DJUCE_DIR=path/to/JUCE` (otherwise FetchContent downloads 8.0.15).
- `COPY_PLUGIN_AFTER_BUILD` is off; output is in `build/PotatoSynth_artefacts/Release/`.
- Releases come from GitHub Actions (`.github/workflows/build.yml`): download from the run's Artifacts tab.

## Test

- The gate is pluginval at strictness 10 (both OSes) plus auval on macOS, run in CI on every push and PR.
- Local: `pluginval --strictness-level 10 --validate-in-process --validate build/PotatoSynth_artefacts/Release/VST3/Potato-Synth.vst3`
- DSP unit tests (once added): `cmake --build build --target PotatoSynthTests && ctest --test-dir build -C Release`

## Source layout

```
Source/
  Params.h            — every APVTS parameter ID, range and default (single source of truth)
  SynthEngine.h       — voice allocation, poly/mono/legato, glide, voice stealing
  Voice.h             — one voice: morph → spectral ops → partial bank → amp
  Spectrum.h          — Spectrum type (per-partial amp/ratio/phase) + generators
  PartialBank.h       — SIMD additive renderer, smoothed per-partial amp/phase
  Morph.h             — 4-corner XY interpolation between spectra
  SpectralOps.h       — Size/Depth/Grit/Air/Tone/Punch operations on a Spectrum
  Macros.h            — macro → many targets with curves
  Envelope.h / Lfo.h  — envelopes, tempo-synced LFOs (Motion)
  ModMatrix.h         — mod sources → destinations (power-user page)
  Analysis.h          — audio file → Spectrum (background thread only)
  Fx.h                — fixed post chain: space send, glue, width/bass mono, limiter
  Presets.h           — factory presets
  Look.h, Hud.h, Displays.h — shared Potato GUI (ported from potato-bass / Potato-Kicker)
  PluginProcessor.*   — audio thread, parameter handling, state save/load
  PluginEditor.*      — GUI layout
Resources/Fonts/      — Barlow Condensed, Share Tech Mono (juce_add_binary_data)
```

## Architecture rules

- DSP lives in header-only engine classes; `PluginProcessor` owns one `SynthEngine` and calls it from `processBlock`.
- Real-time safety: no allocation, locks, logging or file I/O on the audio thread. Preallocate voices and buffers in `prepareToPlay`.
- Read parameters through cached `std::atomic<float>*` pointers from the APVTS, never by string lookup on the audio thread.
- Smooth every continuous parameter (no zipper noise). Drop partials above Nyquist, so nothing ever aliases.
- Spectral ops work on partial amplitudes and ratios, not on audio buffers. Spectra are computed at control rate and interpolated per sample in `PartialBank`.
- CPU budget: under about 15% of one core for 8 voices × 256 partials at 48 kHz. Benchmark any change to `PartialBank` or `SpectralOps`.
- Sample analysis and spectrum generation from files run off the audio thread. Results are swapped in lock-free.
- Macros are the main UI. A new feature should be reachable through a macro or the XY pad first, and the "Under the hood" page second.
- Keep CPU low. Flag anything that adds per-sample cost, allocations or high repaint rates. The GUI never touches DSP state directly.
- Presets are not exposed as host programs; MIDI program changes must never replace the user's sound.
- Parameter IDs are permanent once released. Renaming one breaks saved sessions, so add new IDs instead.

## GUI conventions

- Match the Potato house style (Potato-Kicker / potato-bass): black glossy knobs with orange-to-yellow light rings, all-caps labels, HUD tiles, Barlow Condensed + Share Tech Mono.
- ADSR knobs: Attack = dark purple, Decay = blue, Sustain = orange, Release = yellow; ring intensity follows the value. All other knobs: white rings.
- Once a GUI is approved, build on it; don't redesign.

## Plugin identity

Manufacturer `Basspotato` / code `Bpot`, plugin code `PtSy`, bundle ID `com.basspotato.potatosynth`. Never change these after the first release.
