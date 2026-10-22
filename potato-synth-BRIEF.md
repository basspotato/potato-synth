# Potato-Synth — Project Brief and Plan

Read `CLAUDE.md` first for build commands and rules. Update "Status" at the end of each session.
Research basis: `research/reports/Huge layered synth sound design.md` (findings marked "inference" are ours and need listening tests).

## Vision

A synth that just keeps belting out huge, BIG, layered sounds that make you shake your head. The layer stack is the base, and we only go up from there.

**The core idea from the research:** sounds feel huge when their energy is spread across many frequency bands, both ears and slow beating partials, while the low end stays mono, phase-locked and fast. Potato-Synth ships that producer layering discipline as its default state, so every patch starts huge instead of depending on the user's skill.

**Identity:** a transparent Jupiter-8-style base voice, with explicit "multipliers" on top (unison, dispersion, drift, width, space). A clean, wide JP-8080-style top sits against a driven, mono Dreadbox-style body.

## Architecture: the super stack

| Tier | Spec |
|---|---|
| **Stack** | 4 layers by default, expandable to 8. Each layer has its own voice and unison budget. Idle layers cost zero CPU. |
| **Layer** | Any engine, plus a role tag (Sub / Body / Mid / Air). Each layer has a filter, parallel drive, a role band-limit, amp, 2 inserts and EQ. Also key/velocity zones with crossfades, and trigger conditions (legato, key-off, cycle, random). |
| **Link** | Group/Link editing of several layers at once, absolute or relative (Nord Group and Multi-Focus). Copy layer. Layer fader bank. |
| **Morph** | Stack XY pad in two modes: Mix (constant-power layer crossfade) and Snapshot (parameter interpolation, AN1x/Nord style). Layers can be anchored so the sub never morphs away. Corners can be captured from the current sound, and Layer Scenes swap layers in one action. |
| **Macro** | SUPER knob → 8 global macros → destinations with min/max ranges. Assign by holding a macro and turning a knob, and show a visible macro map. BUILD and DROP impulse buttons. |
| **Bus** | 2 shared sends (space), the stack-glue master chain, optional per-layer outputs. |

### Engines

| Priority | Engine | Core design |
|---|---|---|
| P0 | **JP (Supersaw/VA)** | Two oscillators in Jupiter-8 topology: cross-mod, sync, VCO-2 as LFO, invertible ENV-1. JP-6-style waveform mixing. Unison has two modes. "JP" mode follows the JP-8080 reverse-engineering: integer detune table, linear side gain, key-tracked HPF, 2× oversampling. "Modern" mode has up to 16 voices, power-curve spread and constant-power blend, run on SIMD. |
| P0 | **Sub** | Mono sine/triangle, phase retrigger, 20–30 Hz HPF. "Translate" adds harmonics 2–5 so the sub reads on small speakers. |
| P0 | **Wavetable** | Re-band-limited per pitch by inverse FFT every few ms, with cubic reads, phase warps and frame/warp spread across unison. Vital is the reference design; it's GPLv3, so we re-implement rather than copy. |
| P1 | **Spectral morph** | Harmonic operations (tilt, odd/even, stretch, formant, smear, Shepard, log-domain A↔B morph), rendered to a wavetable by IFFT. A partial bank is used only for inharmonic sounds. |
| P1 | **Dirt (Dreadbox-style body)** | Shape-morph oscillator with a curved saw reset, −1/−2 octave subs, and OTA drive into a 2/3-pole filter whose resonance keeps the lows. Paraphonic mode and an "auto-tune re-roll". |
| P2 | Noise/air, FM/feedback, feedback-comb osc, resonator body (FDN/modal), granular | Mostly cheap add-ons. |

**Init patch:** Sub + Dirt body + JP mid + Spectral/Air top. The XY centre is the full stack, and each corner emphasises one role.

### Default signal chain

**Per layer:** engine → ZDF filter → parallel ADAA drive (2×) → role band-limit (LR4) → amp → inserts (chorus only on upper roles).

**Stack bus:**
1. XY √-weights
2. Mono-bass stage (LR4 at about 120 Hz)
3. Inter-layer ducking
4. OTT-style 3-band compression (88 Hz / 2.5 kHz, about 30% depth)
5. Glue compression (about 2:1, 1–4 dB of gain reduction)
6. Frequency-dependent width (narrow below 250 Hz)
7. Plate/FDN space with pre-delay and ducking
8. Auto-gain from an internal LUFS meter
9. Optional 4× true-peak soft clipper or limiter

| Role | Band | Phase / stereo | Unison default |
|---|---|---|---|
| Sub | LP about 100–120 Hz | Mono, retrigger | Off |
| Body | HP about 100–130 Hz, LP about 3 kHz | Mono or narrow, retrigger | 2 voices at about ±25–30 cents |
| Mid | About 300 Hz–3 kHz, drive here | Random phase, spread | 7 voices at about 20%, plus −1 octave at 3 voices |
| Air | HP about 2–3 kHz | Widest, chorus and reverb | 7–16 voices, 10–15% noise |

### Stack glue (our differentiator: no surveyed product documents these)

- **Role tags:** they set band, phase, unison and width defaults. Two global crossover knobs, defaulting to 120 Hz and 2.5 kHz.
- **Loudness-compensated XY morph:** √-weights plus a slow LUFS-matched trim, so morphing never jumps level.
- **Sub/body phase alignment:** automatic polarity and sample-offset correction at the crossover.
- **Internal sidechains:** the sub ducks the body's lows, and the transient ducks the pad onset by 10–50 ms.
- **Shared drift seed and a Dispersion/Age control** across layers. Unison uses a 1/√N gain law.
- **Per-layer presets and dice/mutate, FX/arp lock while browsing, every parameter automatable,** and seamless patch switching that keeps tails.

### More edge (stack intelligence, all inference until tested)

- **Density governor.** When you play bigger chords, unison per note scales down automatically, so mud and CPU stay constant. A single note gets full width; an 8-note chord still sounds clean.
- **Register-aware everything.** Detune scales with critical-band width, so low notes don't turn rough and high notes don't sound thin. Width and drive also follow pitch: narrower and cleaner down low, wider and brighter up top.
- **Auto-carve.** Role-aware dynamic EQ between layers. When two layers pile up in the same band, the lower-priority layer dips there for that moment only.
- **Transient designer per patch.** PUNCH drives a separate transient path (pitch blip, click, noise burst) that cuts through the mix without making the sustain harsh.
- **Mix Health panel.** Live meters for true peak, loudness, stereo correlation, mono-bass check and a layer-masking warning. A "phone speaker" and a "mono club" check button let you hear the patch through a small-speaker or mono simulation instantly.
- **Loudness-matched browsing.** Every preset plays back at the same perceived loudness, so louder never fools you into thinking it's better.
- **Click-free everything.** Declicked note-on/off, smoothed parameter jumps, and seamless patch switching that keeps tails ringing.
- **Reference match (P2).** Drop in a reference track; TONE tilts the whole stack toward its spectral balance.

### Mix/master-ready standard ("Potato Ready")

Every factory preset, and the init patch, must pass this. It's checked automatically: CI renders each preset offline (held chord and single note, 2 s each) and fails the build if a preset is out of spec.

| Check | Target (inference, tune by ear) |
|---|---|
| Layers | At least 3 role-tagged layers (Sub/Body/Mid/Air), except deliberate solo sounds |
| True peak | ≤ −1 dBTP at velocity 127, full chord |
| Loudness | Within ±1 LU of the bank's category target (presets match each other) |
| Low end | Side signal below 120 Hz under −40 dB relative to mid; sub phase-aligned with body |
| Stereo | Overall correlation above about +0.2; nothing collapses badly in mono |
| Hygiene | No DC offset, no clicks at note-on/off, no denormal CPU spikes, no NaN |
| Macros | Every macro sweep stays within the true-peak limit and loudness window |

**Bank: 200 factory presets.** Categories (rough split): Supersaw/Lead 40, Bass/Reese/Growl 40, Pad/Atmos 30, Pluck/Stab 30, Chord/Keys 20, Arp/Sequence 20, FX/Riser/Impact 20. All built on the stack, all "Potato Ready".

### Built-in FX (tweakable, three tiers)

Every effect has one big Amount knob plus 2–3 key controls on the main view, full detail on its FX page, and every parameter is macro-mappable and automatable. FX lock keeps your FX while browsing presets.

- **Per layer, 2 reorderable insert slots:**
  - **Drive:** tube, tape, diode, foldback, bitcrush/downsample, all ADAA
  - **Chorus/Ensemble:** Juno I/II and Dimension-style modes
  - **Others:** phaser, flanger, comb/resonator, frequency shifter, formant filter, 3-band EQ, transient shaper
- **2 shared sends:**
  - **Space A, reverb:** plate, hall, shimmer, and a "Huge" FDN with modulated tails. Pre-delay, low-cut, ducking.
  - **Space B, delay:** ping-pong, tape, dual-tap, filtered and ducked, tempo-synced, with a freeze button.
- **Stack/master chain:**
  - OTT-style multiband compression and glue compression
  - Tempo-synced pump (sidechain shapes, no kick needed)
  - Width with mono-bass
  - Master EQ and saturator
  - True-peak clipper/limiter

### Chain knobs and the Harmony Engine (the heart of the synth)

Full design: `potato-synth-CHAIN-KNOBS.md`. Each of the 8 chain knobs moves a whole signal chain, including whole synth architectures and oscillator designs (every voice is a "genome" in one shared architecture space, so a knob can travel Jupiter → JP-8080 → FM → additive → waveguide through never-heard hybrids) as well as layers, spectral, EQ, OTT, glue, pump and limiter, along a tuned mathematical path. An always-on Harmony Engine keeps every knob position loud-matched, peak-safe, mono in the lows, and free of mud and harsh beating.

| Knob | You hear |
|---|---|
| MASS | Big and deep: sub translate, body drive, low OTT, wider mono zone |
| SCALE | Wide and huge: unison, register-scaled detune, frequency-dependent width, early reflections |
| PRESSURE | Mastered density: distributed gain reduction across saturation, OTT, glue and limiter |
| GROOVE | Pump and bounce: tempo pump, inter-layer ducking, tempo-locked beating |
| CLARITY | Harmony: auto-carve, band ownership, spectral interleave, de-harsh |
| SHINE | Air and sparkle: air layer, tilt, exciter, transient shimmer |
| MORPH | Spectral mind-bend across all layers, locked to a harmonic lattice |
| BLOOM | Life and motion: drift, Kuramoto focus, chaos, space |

SUPER sweeps all 8 along a per-preset path; BUILD and DROP are momentary, bar-quantised impulses.

### Crazy features (candidates)

Layer bleed (cross-mod between layers), spectrum theft, gravity pad, resample-up, layer rotation, chaos knob, self-pump. BUILD/DROP and Kuramoto Focus from this list are already in the macro plan.

## Decisions

| Area | Choice |
|---|---|
| Framework | JUCE 8.0.15, C++17, CMake + FetchContent |
| Formats / OS | **Windows 11 x64 VST3 first**. User's test rig: Cubase 15, Windows 11, AMD Ryzen 9 9950X. macOS VST3/AU later, non-blocking |
| SIMD | AVX2 baseline (runs on any modern x64); optional AVX-512 path later (the 9950X supports it) |
| Builds | GitHub Actions, workflow based on Potato-Kicker's `build.yml` |
| Testing | Windows CI: VST3 build + pluginval strictness 10 (the gate). Linux CI: headless DSP, chain-knob sweeps, Potato Ready preset checks |
| GUI | Early design: its own look (deep, colourful, appealing, with role colours and glowing chain knobs). The final GUI comes later; the Potato house style is not used for now |
| CPU target | Up to 20% of one core for an 8-note chord on 4 layers at 48 kHz (estimate; replace with profiling). Global Draft/Normal/HQ quality setting. |

## Roadmap (one feature per session)

**Phase 0: Scaffold**
1. CMake, plugin identity, empty processor/editor, Windows CI from Potato-Kicker (VST3 + pluginval). Done when the Windows job is green and the VST3 loads in your DAW.
2. Linux compile-check job, cloud setup script, headless test and benchmark target.

**Phase 1: The super stack (the base)**
3. `Engine` interface, registry, `Layer`, `LayerStack`, per-engine voice pools, idle-layer bypass, test sine engine.
4. Per-layer controls, role tags, zones; parameter scheme and versioned state.
5. Stack bus core: role band-limits, mono-bass, auto-gain, true-peak output.

**Phase 2: P0 engines** (one per session)
6. JP engine: Jupiter topology plus "JP" unison.
7. JP "Modern" unison (SIMD, 16 voices).
8. Sub engine with translate.
9. Wavetable engine.

**Phase 3: Chain knobs and the Harmony Engine (the heart)**
10. Harmony Engine analysis (ERB filterbank) and invariants 1–3 (loudness, peak, mono lows).
11. Chain knob framework: waypoint splines, relative offsets, tuning tool, CI knob sweeps.
12. Harmony Engine invariants 4–6 (band ownership, consonant beating, spectral balance).
13. The 8 chain knobs, one or two per session, tuned by ear. Then SUPER, BUILD, DROP.
14. Voice genome and architecture morph: node graph, routing matrix, spectral bridge, stability caps; classic architectures as genome presets.
15. Stack XY pad (Mix mode, √-weights, anchoring, loudness-compensated).

**Phase 4: Glue and FX**
16. Per-layer filter and ADAA drive.
17. OTT, glue, frequency-dependent width, inter-layer ducking, sub/body phase alignment, auto-carve, density governor.
18. FX: shared sends (reverb, delay), per-layer insert slots and the effect set, pump, Mix Health meters.

**Phase 5: P1 engines**
19. Spectral morph engine.
20. Dirt engine.
20b. Arpeggiator (user request 2026-10-22): MIDI-side, before the layer stack, so every layer follows it. Tempo-synced rate, up/down/up-down/random/as-played/chord modes, octave range, gate, swing, latch, and a step pattern with per-step velocity and ties. Arp on/off and its settings are host parameters; it never changes latency. Built before the presets, so factory presets can use it.
20c. Step sequencer (user request 2026-10-22), sharing the arp's clock and step grid: up to 32 steps, each with a note offset (or rest), velocity, gate, tie, probability and ratchet. A held key transposes the pattern, or it plays its own notes. Plus mod step lanes that move the chain knobs and XY pad in time.

**Phase 6: GUI** (separate sessions from DSP)
21. Main page: layer strip and fader bank, XY pad, 8 chain knobs, SUPER/BUILD/DROP, Harmony Engine display, presets, CPU meter.
22. Layer detail pages, Group/Link editing, chain-knob map view (ghost rings on every moved control).

**Phase 7: Presets and polish**
23. Preset QA renderer (offline render + Potato Ready checks in CI).
24. Factory bank: 200 stacked presets, each one huge at default chain knobs, Potato Ready at every knob position, with a tuned SUPER path.
25. CPU profiling, denormals, state round-trip tests, release build.

**Then up from there:** 8 layers, XY Snapshot mode, step lanes, chaos sources, P2 engines, crazy features.

## Before coding: readiness

**In place:**
- Repo `basspotato/potato-synth`, saved to the project.
- Toolchain decisions, plan, research report, CLAUDE.md, project memory.
- Cloud sessions can compile JUCE for Linux: cmake 3.28, g++ 13, clang and ninja are present, and the 5 missing dev packages install fine (checked 2026-10-22).

**Still needed:**
1. **User says go.** Then update PR #1 with the current docs and merge it.
2. **Cloud build setup.** The missing packages must install in every new session. Either add a setup script to a cloud environment (Project settings → Cloud environment), or commit a SessionStart hook to the repo in Phase 0. The hook is preferred: it lives in the repo and needs no settings.
3. ~~Test DAW~~ Answered: Cubase 15 on Windows 11, Ryzen 9 9950X. Each feature gets a Cubase check after CI is green.
4. **Reference sounds.** A few reference tracks or patches you love, for the listening tests in "Open questions" and to calibrate the Potato Ready loudness targets.
5. **Identity check.** Product name "Potato-Synth" and plugin code `PtSy` are permanent after the first release.
6. **Licences.** JUCE 8 is dual-licensed (AGPLv3 or a commercial tier); your other plugins already use it, so just confirm the tier covers a sold product if you plan to sell. Vital is GPLv3: reference only, no copied code.

## Open questions before locking

These need listening tests or primary sources: supersaw HPF order and cutoff, sub/body phase-alignment thresholds, Dreadbox circuit behaviour, irrational-detune perception, and real CPU figures.

## Status

- Done: research report; plan and CLAUDE.md revised to the super stack; chain knobs + Harmony Engine designed (potato-synth-CHAIN-KNOBS.md).
- Phase 0 merged 2026-10-22.
- Phase 1 (2026-10-22): layer stack with lock-free engine swaps (#3); per-layer amp ADSR, pan, tune (#4, GUI #5); Poly/Mono/Legato + glide (#6, GUI #7); role tags, LR4 band limits, crossovers, mono bass (#8, GUI #9); safe output: 4x soft clipper + look-ahead true-peak limiter, BS.1770 meters (#10).
- Deferred: stack auto-gain moves to the Harmony Engine (Phase 3), where XY/macro loudness matching gives it a job; zones come with the preset work.
- Next: Phase 2, the JP engine (Jupiter topology + JP-8080 supersaw unison).
