# Potato-Synth — Chain Knobs and the Harmony Engine

This is the heart of the synth. In a traditional synth a knob moves one parameter. In Potato-Synth each **chain knob** moves a whole signal chain along a path we have tuned mathematically. That chain includes whole synth architectures and oscillator designs (Jupiter, JP-8080, Dreadbox, FM, additive, waveguide and more, see section 5) as well as layers, spectral shaping, EQ, OTT, glue, pump and limiter. Underneath, an always-on **Harmony Engine** keeps the layers in clear sonic harmony at every knob position. Turning a knob can never make the sound muddy, clipped, phasey or quieter by accident.

Research basis: `research/reports/Huge layered synth sound design.md`. Every number below is a starting value marked as inference. It gets tuned by ear and checked by the Potato Ready renderer.

## 1. The idea in one picture

```
 player turns 1 knob ──► trajectory p(u) through ~40 chain parameters ──► Harmony Engine keeps the invariants ──► sound
   (u = 0…1)            (layers, spectral, EQ, OTT, glue, pump, limiter)    (loudness, peak, mono lows, band ownership)
```

- **Trajectory, not a line.** Each knob position u maps to a point in parameter space through a smooth path. The path is a monotone cubic spline through 5 tuned waypoints (u = 0, .25, .5, .75, 1). Every waypoint is a state we have auditioned and measured, so the path between them stays "perfect" too.
- **Relative to the patch.** Knobs add offsets on top of each preset's own settings, in each parameter's natural unit: log2 for frequencies, dB for gains, cents for detune. That keeps presets distinct. Turning a knob makes *this* sound bigger rather than making every preset sound the same.
- **Invariants first.** After each knob's offsets are applied, the Harmony Engine (section 3) runs a constraint pass at control rate (every 32 samples) and fixes whatever would break the rules.
- **Glass box.** Every parameter a knob moves is visible. Its detail page shows a "ghost" ring on each moved control, and you can open the hood and edit anything.

## 2. The chain knobs

There are 8 chain knobs on the main page. Each has a name for what you *hear*, not for the device it drives. A SUPER knob sits above them, with BUILD and DROP impulse buttons.

### MASS: big and deep

You feel the low end more without it getting louder on the meter or muddier.

| Chain element | Path as u goes 0→1 |
|---|---|
| Sub translate (harmonics 2–5, LP at 4·f0) | 0 → +10 dB harmonic level (virtual-bass range +8 to +12 dB) |
| Sub energy focus | Shelf boost centred 40–80 Hz (the tactile band) from 0 to +3 dB; 20–30 Hz HPF stays fixed |
| Body drive (asymmetric, ADAA) | 0 → 35% parallel mix, so even harmonics thicken the 100 Hz–1 kHz band |
| Low-band OTT, upward only | 0 → 40% depth (sustain lifts, peaks stay) |
| −1 octave reinforcement on Body | 0 → −12 dB at u = .5, rising to −6 dB at u = 1 |
| Mono-bass crossover | 100 → 140 Hz (more mass means a wider mono zone) |
| Limiter | No added drive; MASS spends headroom through the sub's narrow-band energy, and the Harmony Engine trims it back |

**Math:** the goal is more energy in the 40–80 Hz ERBs plus more ERBs lit at 100–500 Hz (loudness summation), at constant integrated loudness. Because K-weighting de-weights the sub, the engine holds *true peak* constant rather than LUFS on this knob (see section 3).

### SCALE: wide and huge

The sound seems to fill the room without losing its centre.

| Chain element | Path |
|---|---|
| Unison voices on Mid/Air | 3 → 16 (count per voice, scaled down by the density governor in chords) |
| Detune | Register-scaled: d_cents(f) = d·ERB(f)/ERB(1 kHz), capped so beat rates stay under 25% of the critical band |
| Frequency-dependent decorrelation β | Below 250 Hz β = 0 always; above it 0 → 0.9 (output coherence ≈ cos²β) |
| Target correlation | Overall correlation +0.8 → +0.3 (the floor is +0.2, enforced) |
| Ensemble chorus on Air | 0 → 60% |
| Space early reflections | Pre-delay = one 64th note at the host tempo; early level 0 → −14 dB |

**Math:** width comes from low inter-ear correlation in the 500 Hz–2 kHz bands, while lows stay correlated. SCALE pushes the correlation of the 500 Hz–2 kHz bands toward 0.3 and leaves everything below 250 Hz mono.

### PRESSURE: loud and dense, mastered

The patch gets as dense and loud as a finished master, but stays clean and punchy.

This uses a **distributed gain reduction** principle: the total gain reduction G(u) is split across stages in fixed shares, so no single stage pumps or distorts.

```
G(u) = 12·u^1.3 dB        (0 → 12 dB total)
shares: saturation 25% · OTT 30% · glue 20% · clipper/limiter 25%
```

| Stage | At u = 1 |
|---|---|
| Parallel saturation | about 3 dB of crest-factor reduction |
| OTT (3-band at 88 Hz / 2.5 kHz, up+down) | depth about 55% |
| Glue (2:1, slow attack, tempo-synced auto release) | about 2.4 dB of gain reduction |
| True-peak clipper then limiter | about 3 dB, ceiling −1 dBTP |
| Transient guard | A transient shaper restores the attack the dynamics removed, to keep the punch model (fast onset, <1 kHz) intact |

### GROOVE: the pump and the bounce

The sound breathes with the track.

| Chain element | Path |
|---|---|
| Tempo pump depth | 0 → 9 dB, shape morphing from smooth (sine-like) to hard (exponential release) |
| Pump curve | g(t) = 1 − D·exp(−t/τ), with τ = (60/BPM)·k and k from 0.12 to 0.35 |
| Inter-layer ducking | The transient layer ducks the pad onset by 10 → 50 ms (backward/forward masking window) |
| Glue and OTT release times | Locked to tempo subdivisions (1/16, 1/8), so all the dynamics breathe in time |
| **Tempo-locked beating** (new) | Unison detune is quantised so the main beat rate is a tempo subdivision: Δf_beat = n·BPM/60 for n = ¼, ½, 1, 2. Supersaws "swirl" in time with the track |

### CLARITY: harmony, not mud

Every layer becomes audible on its own while still fusing into one sound.

| Chain element | Path |
|---|---|
| Auto-carve depth | 0 → 6 dB of dynamic dip on the lower-priority layer in contested ERB bands |
| Band ownership sharpness | Softmax temperature from soft to sharp (see section 3) |
| Spectral interleave | Layer B's unison spread is offset by half of layer A's spacing, so their partials fall between each other instead of on top |
| Sub/body phase alignment | Auto polarity and sample-offset at the crossover, always on, with a strength from 0 to 1 |
| De-harsh | Dynamic EQ at 2–5 kHz, threshold falling as u rises |
| Detune roughness ceiling | Tightens from 25% to 15% of the critical band (more consonance) |

### SHINE: air and sparkle

| Chain element | Path |
|---|---|
| Air layer level | −18 → −6 dB |
| Global spectral tilt | −4.5 → −3 dB/octave around 1 kHz (brighter), with filter cutoffs following in log2 |
| Harmonic exciter above 6 kHz | 0 → 25% |
| Spectral engine odd/even and tilt | Toward odd and brighter |
| Transient shimmer | Noise burst on attack, 0 → −20 dB, 8 ms |
| Safety | De-harsh and true-peak guard follow automatically |

### MORPH: spectral mind-bend

This knob moves every layer's spectral identity together.

| Chain element | Path |
|---|---|
| Spectral engine A↔B morph | 0 → 1, interpolated in log-frequency and log-amplitude |
| Wavetable position, all wavetable layers | Follows along a per-patch path |
| Inharmonic stretch | B from 0 → 0.0004 (f_n = n·f0·√(1+B·n²)), for metallic bloom at high u |
| Formant shift | 0 → +5 semitones |
| Harmonic lattice lock (new) | Layer partials are pulled toward a shared harmonic lattice (root, fifth, octave) so morphing stays consonant |

### BLOOM: life and motion

| Chain element | Path |
|---|---|
| Drift (Ornstein–Uhlenbeck) | σ 0 → 8 cents, θ slow (about 0.3 Hz) |
| Kuramoto "Focus" | Coupling K from high (unison voices lock together) at u = 0 to low (voices drift free) at u = 1 |
| LFO and chaos on wavetable position, cutoff and spread | 0 → per-patch depth |
| Space size and late reverb level | Small → huge, with ducking rising alongside it |
| Golden-ratio spread | Modulation rates spaced by φ = 1.618 so movement never repeats in an obvious loop |

### SUPER, BUILD and DROP

- **SUPER** sweeps all 8 chain knobs along a stored path, verse to chorus to drop. Each preset ships with a tuned SUPER path.
- **BUILD** (momentary) raises SCALE and BLOOM, opens SHINE, and starts a Shepard riser and a filter rise quantised to bars.
- **DROP** (momentary, quantised to the next bar) kills the reverb tail, jumps PRESSURE and MASS, snaps GROOVE to full, and adds the −1 octave layer.

## 3. The Harmony Engine (always on)

This is a control-rate solver that runs after the knobs and before the audio. It reads a cheap analysis of every layer and nudges gains, EQ and dynamics to keep 6 invariants.

**Analysis:** each layer is measured with a 32-band ERB filterbank, decimated, at about 30 Hz, giving its energy E_l,b per band b. The bus gets short-term loudness (BS.1770-style), true peak and correlation in 3 bands.

| # | Invariant | Rule |
|---|---|---|
| 1 | **Loudness constant** | Integrated loudness stays within ±0.5 LU of the patch's reference while any knob or the XY pad moves. A slow trim (about 300 ms) compensates. MASS holds true peak instead. |
| 2 | **Peak ceiling** | True peak ≤ −1 dBTP, by look-ahead clipper and limiter, always |
| 3 | **Mono lows** | Side energy below the crossover under −40 dB relative to mid |
| 4 | **Band ownership** | In each ERB band, layer shares follow w_l,b = softmax(π_l,b / T), where π is role priority × energy. Layers that don't own the band get a dynamic dip up to the CLARITY depth. One clear owner per band = no mud. |
| 5 | **Consonant beating** | For every pair of nearby partials (within or across layers), beat rate stays below c·CB(f), with c from 0.25 down to 0.15 (CLARITY). This is enforced by trimming detune, not by filtering. |
| 6 | **Spectral balance** | The bus's long-term ERB spectrum stays within ±3 dB of a target tilt curve (default −4.5 dB/oct, moved by SHINE and MASS). A gentle 6-band dynamic EQ corrects it. |

**Why it works:** the research says "huge" means spreading energy across many critical bands, many decorrelated ear signals and many slowly beating partials, with mono, phase-locked lows. Invariants 4–6 make the spreading orderly, and 1–3 keep it safe. The knobs only ever move the sound *within* that safe, harmonious region. That is the meaning of "clear sonic harmony" here.

**Cost:** the 32-band analysis is decimated and the solver runs at control rate, so it should come in under about 1% of a core for 4 layers. The dynamic EQ and dynamics are bus-level, not per voice. These are estimates until profiled.

## 4. How the layers sit over each other (the maths of presentation)

1. **Frequency:** roles own bands (Sub < 120 Hz < Body < 300 Hz–3 kHz Mid < 2.5 kHz Air), with LR4 crossovers. The softmax ownership resolves overlaps band by band.
2. **Time:** the transient layer leads, and the pad and body onset are delayed or ducked 10–50 ms (masking windows), so every attack reads.
3. **Space:** correlation is a function of frequency, from mono at the bottom to wide at the top (β(f)).
4. **Pitch:** layers lock to a harmonic lattice (octaves and fifths). Unison partials interleave instead of colliding. Beat rates are tempo-locked or golden-ratio spread, never random mush.
5. **Loudness:** sums use √-weights plus the loudness invariant, so adding or morphing layers never jumps the level.

## 5. Architecture space: whole synths inside the knobs

FX chains are only one dimension. A chain knob can also travel through **whole synth architectures and oscillator designs**. Turning it moves a layer from one synth's way of making sound to another's, through hybrids in between that no hardware has ever made.

### Every voice is a point in one shared space

Each layer's voice is described as a small network, the **voice genome**:

- **Nodes:** oscillators of any design (VCO, supersaw stack, FM operator, phase-distortion, wavetable, additive bank, noise), delay lines (for Karplus-Strong/waveguide), resonators (modal bank) and filters (SVF, ladder, OTA, comb, formant).
- **A routing matrix M:** every node-to-node connection as a continuous weight. That covers audio mix, FM/PM depth, ring/AM, sync strength, feedback and series/parallel filter routing.
- **Per-node shape parameters:** waveform continuum, nonlinearity (clean digital → OTA/tanh saturation), drift, unison.

Classic instruments are just **specific points** in this space:

| Architecture | As a genome point |
|---|---|
| Jupiter-8 voice | 2 VCOs, cross-mod 2→1, sync, HPF → IR3109-style 4-pole OTA LPF, clean and transparent |
| Jupiter-6 voice | 2 VCOs with mixable waveforms, multimode OTA filter, more resonant bite |
| JP-8080 | 7-node supersaw (integer detune table, linear side gain, key-tracked HPF), Feedback Osc as a self-resonating comb node, digital-clean (no drift) |
| Dreadbox body | Shape-morph VCO + −1/−2 subs, OTA drive into a 2/3-pole filter with low-preserving resonance, analog drift |
| DX7 / FM-X | 6 operators; M holds the algorithm, feedback on one operator; FM-X skirt and resonance per operator |
| Casio CZ | Phase-distortion node (a special case of phase modulation), windowed resonance waves |
| Additive / spectral | Partial bank or IFFT wavetable with spectral operators (tilt, odd/even, stretch, formant) |
| Karplus-Strong / waveguide | Exciter → delay node with lowpass feedback loop |
| Modal / FDN body | Resonator bank or Householder FDN fed by any exciter |
| Granular | Grain cloud over any node's output |

**Waypoints can be architectures.** A chain knob's waypoints can hold different genomes, for example u = 0 Jupiter-8, u = .5 JP-8080 supersaw, u = 1 additive spectral. Everything in between is a real, playable hybrid. A Jupiter voice whose oscillators slowly *become* a supersaw stack, then dissolve into partials, is a sound nobody has heard before.

### Making architecture morphs smooth (the maths)

1. **Discrete becomes continuous.** Switches are replaced by weights: waveform type → shape continuum; FM algorithm → matrix weights M_ij; filter series ↔ parallel → a routing crossfade; supersaw voice count → fractional voices (the newest voice fades in by amplitude, so 6.4 voices is legal).
2. **Genome interpolation.** Continuous parameters interpolate in natural units (log2 frequency, dB gain, cents). The matrix M interpolates element-wise. Nodes that exist in only one waypoint fade in or out by amplitude.
3. **Spectral bridge for the hard jumps.** When two architectures are too different to interpolate (e.g. waveguide → FM), each one's steady-state harmonic spectrum is cached per pitch and morphed in log-frequency / log-amplitude, then rebuilt by IFFT into a wavetable. The two live renders crossfade only for their *inharmonic and transient* parts. Result: no phasey double-sound mid-morph.
4. **Phase-aligned crossfades.** Where two renders do overlap, they're aligned by cross-correlation at control rate and summed with constant-power weights, so they never comb-filter.
5. **Stability guarantees.** Feedback paths keep the matrix's spectral radius ρ(M_fb) < 0.98, so no runaway at any position. FM depth is capped by Carson's rule (bandwidth 2(Δf + f_m) below Nyquist / oversampled limit), so no aliasing. Waveguide loop gain stays < 1.
6. **Harmony Engine on top.** All 6 invariants still apply, so a morph through architectures stays level-matched, peak-safe, mono in the lows and consonant.

### Which knobs travel which architecture paths (defaults)

| Knob | Architecture path (as u rises) |
|---|---|
| MASS | Body layer: clean Jupiter VCO → Dreadbox OTA voice (drift, subs, low-preserving resonance) |
| SCALE | Mid layer: Jupiter 2-VCO unison → JP-8080 7-saw → Modern 16-voice SIMD stack |
| PRESSURE | Oscillator nonlinearity: digital-clean → OTA/tanh saturation inside the voice (pre-filter), feeding the distributed gain reduction |
| GROOVE | FM ratio and index lock to tempo-related beat rates; sync strength pulses with the pump |
| CLARITY | Toward cleaner topologies: fewer feedback paths, consonant FM ratios (integer c:m) |
| SHINE | Air layer: noise → FM-X operators with skirt and resonance → additive top partials |
| MORPH | The full journey: subtractive (Jupiter) → FM (DX/FM-X) → phase distortion (CZ) → additive spectral → waveguide/modal body |
| BLOOM | Coupled oscillators (Kuramoto) and chaotic modulators; the genome itself drifts slowly around its point |

Presets can redefine any knob's path, so each of the 200 presets can have its own journey. A **dice** button proposes new paths, which the Harmony Engine keeps safe.

### Cost

During a morph between two non-interpolable architectures, both render for that layer, so its cost roughly doubles until the knob settles. The spectral bridge is cheap (cached spectra plus one IFFT every few ms). The density governor and idle-node bypass keep the total inside the CPU target (estimate, to profile).

## 6. Building it

- **Engine side:** `ChainKnobs.h` (trajectories, waypoint splines, relative offsets), `HarmonyEngine.h` (analysis + solver), with `StackBus.h` hosting the dynamics, EQ and limiter stages the knobs drive.
- **Authoring:** waypoints live in a JSON table per knob. A small tuning tool renders a test chord at each waypoint and reports the Potato Ready metrics. We tune waypoints by ear plus meters, then freeze them.
- **Voice genome:** `Genome.h` (node graph + routing matrix M), node types in `engines/`, `ArchMorph.h` (genome interpolation, spectral bridge, phase-aligned crossfade, stability caps).
- **Tests:** CI sweeps every chain knob from 0 to 1 on every preset and checks invariants 1–3 at each step. A knob that breaks an invariant fails the build.
- **Roadmap placement:** this replaces the old "8 macros" step. Harmony Engine invariants 1–3 come with the stack bus (Phase 1). The chain knobs and invariants 4–6 get their own phase right after the P0 engines (see the brief).

## 7. Open questions

- Waypoint values are all first guesses and must be tuned by ear on real material.
- Tempo-locked beating and the harmonic lattice lock are new ideas with no prior art found. They need listening tests to confirm they sound better than free beating.
- Architecture morphs need listening tests per pair. Some pairs may sound best through the spectral bridge, others through direct genome interpolation.
- Should chain knobs be global only, or also available per layer (e.g. SCALE on Air only)? Proposal: global by default, with a per-layer "follow" amount.
