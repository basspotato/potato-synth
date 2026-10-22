# Potato-Synth

Huge-by-default layered synth (VST3) for EDM. A "super stack" of 4 layers (expandable to 8) plays every note. Each layer runs its own engine (JP supersaw/VA, Sub, Wavetable, Spectral morph, Dirt, ...) and has a role tag (Sub/Body/Mid/Air) that sets its band, phase and width. Stack glue (role band-limits, mono bass, loudness-compensated XY morph, inter-layer ducking, OTT/glue, auto-gain) makes the layers sound like one instrument. 8 chain knobs (MASS, SCALE, PRESSURE, GROOVE, CLARITY, SHINE, MORPH, BLOOM) each move a whole tuned signal chain, and an always-on Harmony Engine keeps every knob position in harmony (design: `potato-synth-CHAIN-KNOBS.md`). JUCE 8.0.15, C++17, CMake. Plan: `potato-synth-BRIEF.md`. Research: `research/reports/Huge layered synth sound design.md`.

Targets: **Windows 11 x64 VST3 is the priority**. The developer tests in Cubase 15 on Windows 11 (Ryzen 9 9950X). macOS VST3/AU is later and must never block a build or PR.

## Working style (token efficiency)

- Do the task. Don't narrate steps or recap finished work; end with 1-2 sentences on the outcome.
- Simple change = minimal action. Don't re-read files you just edited or add verification nobody asked for.
- Stay in scope: no refactors, renames, reformatting, extra features, options, dependencies or files that weren't asked for.
- Comments only where the logic is non-obvious.
- When the target file is known, go straight to it. Use the source layout below instead of searching the repo.
- Ask only when blocked on a decision that changes the outcome; otherwise take the obvious default and say which.
- One feature (or one engine) per session. Keep DSP and GUI changes in separate sessions.
- Show a short plan before any big change.
- When debugging a build error, quote only the relevant error lines.

## Git

- Never commit to `main` directly. Work on a branch and open a PR; CI must be green before merge.
- No force-push, history rewrites or branch deletion without asking.

## Build

```bash
# Windows (Visual Studio 2022) — the target that matters
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target PotatoSynth_VST3

# Linux (cloud sessions: compile check + headless tests; deps installed by .claude/hooks/session-start.sh)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target PotatoSynth_VST3
```

- Local JUCE checkout: `-DJUCE_DIR=path/to/JUCE` (otherwise FetchContent downloads 8.0.15).
- `COPY_PLUGIN_AFTER_BUILD` is off; output is in `build/PotatoSynth_artefacts/Release/`.
- Releases come from GitHub Actions (`.github/workflows/build.yml`): download from the run's Artifacts tab.

## Test

- The gate is the Windows job: VST3 build plus pluginval at strictness 10, on every push and PR. A Linux compile/test job runs headless DSP and preset tests. A macOS job, if added, is non-blocking.
- Local: `pluginval --strictness-level 10 --validate-in-process --validate build/PotatoSynth_artefacts/Release/VST3/Potato-Synth.vst3`
- DSP unit tests (once added): `cmake --build build --target PotatoSynthTests && ctest --test-dir build -C Release`

## Source layout

```
Source/
  Params.h              — every APVTS parameter ID, range and default (single source of truth)
  Engine.h              — abstract Engine interface every synth engine implements
  EngineRegistry.h      — engine type ID → factory; the only place a new engine is registered
  engines/              — one header per engine: JpEngine.h, SubEngine.h, WavetableEngine.h, SpectralEngine.h, DirtEngine.h, ...
  Unison.h              — "JP" (JP-8080 integer table) and "Modern" (SIMD, 16 voices) unison
  Layer.h               — one slot: engine + role tag, filter, drive, role band-limit, amp, inserts, zones
  LayerStack.h          — voice pools per engine, note routing, idle-layer bypass
  StackMorph.h          — XY pad: Mix (constant-power, loudness-compensated) and Snapshot modes, anchored layers
  StackBus.h            — mono-bass, inter-layer ducking, OTT, glue, freq-dependent width, auto-gain, true-peak out
  Filter.h / Drive.h    — ZDF filters (SVF, ladder/OTA), ADAA saturation
  Envelope.h / Lfo.h    — envelopes, tempo-synced LFOs
  Genome.h / ArchMorph.h — voice genome (node graph + routing matrix) and architecture morphing (interpolation, spectral bridge, stability caps)
  ChainKnobs.h          — 8 chain knobs: waypoint-spline trajectories, relative offsets; SUPER/BUILD/DROP
  HarmonyEngine.h       — ERB analysis + control-rate solver for the 6 invariants
  ModMatrix.h           — mod routing
  Space.h               — plate/FDN reverb and delay on shared sends
  Presets.h             — factory presets
  gui/                  — Potato-Synth's own early-design look (LookAndFeel, knobs, XY pad, meters); replaceable later
  PluginProcessor.*     — audio thread, parameter handling, state save/load
  PluginEditor.*        — GUI layout (main page + layer detail pages)
Resources/Fonts/        — Barlow Condensed, Share Tech Mono (juce_add_binary_data)
```

## Architecture rules

- **Adding an engine** = one new header in `engines/` implementing `Engine` + one line in `EngineRegistry.h`. If an engine needs changes to `Layer`/`LayerStack`, stop and flag it: the interface needs extending, not bypassing.
- **Parameters:** each layer has a fixed set of generic params (`L{n}_level`, `L{n}_pan`, ..., `L{n}_engine`, and engine slots `L{n}_e1`..`L{n}_e16`). Each engine declares what its slots mean (name, range, default) for the GUI. New engines therefore never add or rename host parameters. Parameter IDs are permanent once released.
- **State** carries a version number. Loading older state must still work; add a migration rather than breaking it.
- Engines are created when the user picks them (message thread), never on the audio thread. Swap them in lock-free and preallocate in `prepare`.
- SIMD: AVX2 baseline; anything wider must be runtime-dispatched with an AVX2 fallback.
- Cubase is the reference host: keep a stable bus layout (stereo out, MIDI in), never change latency on the fly, and keep parameter count fixed after load.
- Real-time safety: no allocation, locks, logging or file I/O on the audio thread.
- Read parameters through cached `std::atomic<float>*` pointers, never by string lookup on the audio thread. Smooth every continuous parameter.
- Oscillators must be band-limited (PolyBLEP for VA, per-pitch IFFT for wavetables, oversampling only where nonlinear). Flag any aliasing trade-off.
- Idle or near-zero-weight layers cost zero CPU. CPU target: up to 20% of one core for an 8-note chord on 4 layers at 48 kHz. Benchmark changes to `LayerStack`, `Unison` or any render loop. Spectral unison and per-voice width FX are known cost traps.
- Huge by default: role tags own the defaults (Sub mono and retriggered below about 120 Hz, width only above 250 Hz, random phase only on Mid/Air). Never add stereo or bass content that bypasses the mono-bass stage.
- Layer sums and XY morphs use √-weights plus auto-gain. Adding a layer or moving the pad must never jump the level.
- Architecture morphs: discrete switches become continuous weights; feedback keeps spectral radius < 0.98; FM depth stays under Carson's-rule bandwidth limits. A morph must never click, alias or run away.
- Chain knobs: never let a knob position break a Harmony Engine invariant (loudness ±0.5 LU, ≤ −1 dBTP, mono lows, band ownership, consonant beating, spectral balance). Waypoint tables are tuned by ear; don't change them without a listening check. CI sweeps every knob on every preset.
- Vital (GPLv3) is a reference design only: re-implement, never copy code.
- Every preset (200 in the factory bank, plus init) must pass the "Potato Ready" checks in the brief: at least 3 role layers, ≤ −1 dBTP, loudness within ±1 LU of its category, mono below 120 Hz, no clicks/DC. CI renders and checks them; never loosen a check to make a preset pass.
- Built-in FX: every effect has one Amount knob plus 2-3 key controls on the main view, full detail on its page, and is macro-mappable.
- Presets are not exposed as host programs; MIDI program changes must never replace the user's sound.

## GUI conventions

- Early design uses Potato-Synth's **own** look, not the Potato-Kicker / potato-bass house style. The final GUI comes later, so keep GUI code in its own files and easy to replace.
- Direction: deep, colourful, appealing. Dark deep background (indigo/violet) with saturated gradient accents. Each role has its own colour (Sub = deep red/magenta, Body = orange, Mid = cyan, Air = violet/white), and chain knobs glow brighter as they turn.
- Main page = layer strip + XY pad + SUPER/8 chain knobs/BUILD/DROP + presets + CPU meter. Engine-specific controls live on each layer's detail page.
- Repaint at a modest rate (about 30 Hz for meters) and keep CPU low.

## Plugin identity

Manufacturer `Basspotato` / code `Bpot`, plugin code `PtSy`, bundle ID `com.basspotato.potatosynth`. Never change these after the first release.
