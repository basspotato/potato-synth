# Commercial and Open-Source Synth Architecture Survey for Huge EDM Sounds (layering, morphing, macros)

Research date: 2026-10-22. Version dates are noted where the sources give them. About 25 tool calls were used, so coverage is uneven: the flagship synths are well sourced, and several niche products (Rob Papen, Heavyocity, PolyBrute Morphée, Kaossilator, NKS macro standard) are listed under Gaps.

## 1. Per-synth architecture: oscillators, unison, layers/parts, morph/macro systems, FX, reputation

### Takeaway
The current flagships converge on 3-5 oscillator or generator slots per patch, 8 macros, and a two-bus or multi-lane FX chain. Only Omnisphere 3, with four layers and its new Quadzone morphing, and Korg Wavestate, with four layers mixed by a vector joystick, ship a built-in "4 layers + morph control" design close to the proposed synth. Nobody found here combines that four-layer XY morph with a modern supersaw, wavetable and spectral engine set in one patch.

### Cited Findings

**Xfer Serum 2 (v2.0, manual v1.0.0 dated 17 Mar 2025)**
- 3 main oscillators, each switchable among 5 types (Wavetable, Multisample, Sample, Granular, Spectral), plus a dedicated Sub and a dedicated Noise oscillator — [Xfer Serum 2 What's New PDF](https://static.xferrecords.com/Serum%202%20What's%20New.pdf)
- Tuning modes: semitone, harmonics, ratio and step. Unison is described as an "enhanced unison configuration", with no maximum stated in the PDF — [Xfer PDF](https://static.xferrecords.com/Serum%202%20What's%20New.pdf)
- 8 macros, and each macro can itself be a modulation destination. Up to 10 LFOs, 4 envelopes, dual filters, a clip sequencer and arp (12 slots per bank), and an improved mod matrix with bypass and reorder — [Xfer PDF](https://static.xferrecords.com/Serum%202%20What's%20New.pdf)
- FX: 13 effects and 3 splitter modules (Lows/Highs, Lows/Mids/Highs, Mid/Side) across two FX busses. The PDF names Bode, Convolve, Delay, Distortion, Reverb and Utility among them — [Xfer PDF](https://static.xferrecords.com/Serum%202%20What's%20New.pdf)
- Xfer's own CPU guidance says more than 3-7 unison voices per oscillator is "often unnecessary". Higher counts raise CPU significantly and cause phasing. Xfer recommends a chorus on an FX bus instead, because it "processes the effect once, not once per voice" — [Xfer support: Serum 2 CPU guidelines](https://support.xferrecords.com/article/51-serum2-sound-design-guidelines-for-optimizing-cpu-usage)

**Vital (open source, mtytel/vital on GitHub)**
- Source constants: `kNumOscillators = 3`, `kMaxUnison = 16` per oscillator, `kNumMacros = 4`, `kNumLfos = 8`, `kNumEnvelopes = 6`, `kNumRandomLfos = 4`, `kNumFilters = 2`, `kMaxPolyphony = 33` (32 active), `kMaxModulationConnections = 64`, and 257 wavetable frames — [vital synth_constants.h](https://raw.githubusercontent.com/mtytel/vital/main/src/common/synth_constants.h); [vital synth_oscillator.h](https://raw.githubusercontent.com/mtytel/vital/main/src/synthesis/producers/synth_oscillator.h)
- Unison voices are packed into SIMD `poly_float` lanes (`kPolyPhasePerVoice = kMaxUnison / poly_float::kSize`). This is how the "efficient unison" claim is implemented — [vital synth_oscillator.h](https://raw.githubusercontent.com/mtytel/vital/main/src/synthesis/producers/synth_oscillator.h)
- Unison **stack types**: Normal, Center Drop Octave (1 and 2), Octave (1 and 2), Power Chord (1 and 2), Major Chord, Minor Chord, Harmonic Series, Odd Harmonic Series. There are also per-unison-voice spreads for wavetable frame, distortion and spectral morph (`kUnisonFrameSpread`, `kUnisonDistortionSpread`, `kUnisonSpectralMorphSpread`) — [vital synth_oscillator.h](https://raw.githubusercontent.com/mtytel/vital/main/src/synthesis/producers/synth_oscillator.h)
- Launch (Nov 2020): spectral warping (stretch, shift, smear, skew harmonics), "efficient unison", 8 routable FX (including a multiband compressor and a 16-voice chorus), 2 filters with continuous blending modes, audio-rate modulation, MPE, and Perlin/Lorenz random sources. Tiers were Basic (free), Plus $25, Pro $80, or $5/month — [Sonic State, 10 Nov 2020](https://sonicstate.com/news/2020/11/10/vital-a-spectral-warping-wavetable-synth/)

**Kilohearts Phase Plant**
- Generators: analog oscillator, noise, granular, sample player and wavetable. The generator area holds up to 32 modules, organised in **groups**. Audio does not flow between groups automatically, but can be routed through audio-rate modulation — [Kilohearts docs](https://kilohearts.com/docs/phase_plant)
- Unison modes: Hard, Smooth and Synthetic, plus the creative modes Frequency Stack, Pitch Stack and Shepard, and Chords. The docs give no maximum voice count — [Kilohearts docs](https://kilohearts.com/docs/phase_plant)
- 8 macros. 3 effect lanes, each routable to a later lane or the master. Polyphonic lanes must be enabled left to right (lane 2 can be polyphonic only if lane 1 is). The docs warn that too many ringing voices hurt CPU — [Kilohearts docs](https://kilohearts.com/docs/phase_plant)

**Arturia Pigments (v6 June 2025; v7 Dec 2025)**
- Engine families: virtual analog, wavetable, sample/granular, and physical modelling (Modal, added in v5). v7 added no new engine — [Attack Magazine, Pigments 7 review, 16 Dec 2025](https://www.attackmagazine.com/reviews/gear-software/arturia-pigments-7-the-best-gets-even-better/)
- Modulation is drag-and-drop with unlimited modulators. In v6, a modulator can modulate another modulator's amount, and a new Voice Modulator sends different values to up to 8 voices. The SOS criticism is that engines lack engine-level preset save — [Sound On Sound, Pigments 6, June 2025](https://www.soundonsound.com/reviews/arturia-pigments-6)
- v7 added the Rage, Ripple and Reverb filters, FM inside the Classic filter, the Corroder distortion, snappier envelopes and **improved CPU usage**. It was free for existing users, with a full price of $99 rising to $199 after 7 Jan 2026 — [Attack Magazine](https://www.attackmagazine.com/reviews/gear-software/arturia-pigments-7-the-best-gets-even-better/)
- One user anecdote says Pigments is heavier than Surge XT on their laptop — [Vital forum CPU thread](https://forum.vital.audio/t/cpu-optimization-for-vital/4810?page=4)

**Spectrasonics Omnisphere 3 (announced autumn 2025; MusicTech review Dec 2025; SOS review Mar 2026)**
- **4 synth layers per patch**, and the mod matrix can target one layer or all four — [MusicTech review](https://musictech.com/reviews/plug-ins/spectrasonics-omnisphere-3-review/)
- **Quadzone** is new. It creates splits, crossfades and velocity switches between the four layers, supports multiple zones within each layer, and morphs between zones globally with one continuous MIDI fader — [Sound On Sound, Omnisphere 3](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3)
- 638 DSP waveforms (each a morphing wavetable), 35 new filters, circuit-modelled filter saturation, a dual frequency shifter, unison phase scatter, MPE, and 18 new libraries (26,421 patches) — [SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3); [MusicTech](https://musictech.com/reviews/plug-ins/spectrasonics-omnisphere-3-review/)
- FX: 93 total (35 new and 58 carried over). Omni FX runs as a standalone DAW rack with 4 effects per rack — [SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3)
- Price is $499, or a $199 upgrade. MusicTech rated it 10/10, said it runs "remarkably well" on an M2 MacBook Air, and lists "Extremely CPU-friendly" as a pro — [MusicTech](https://musictech.com/reviews/plug-ins/spectrasonics-omnisphere-3-review/)
- Requirements: 8 GB RAM (16 GB recommended) and 64 GB disk. SOS notes that other synths offer more complex mod routing, spectral wavetable warping and animated displays — [SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3)

**Reveal Sound Spire (v1.5.19)**
- 4 polymorphing oscillators (Classic, Noise, FM, AMSync, SawPWM, HardFM, Vowel) with up to 9 unison voices per oscillator, described as "supersaw/hypersaw capable". 4 macros, 4 envelopes, 4 LFOs, 15 matrix slots and 2 steppers. FX include X-Comp multiband, shaper and decimator — [Dubspot Spire vs Sylenth1](https://blog.dubspot.com/plugins/compare/spire-vs-sylenth1)

**LennarDigital Sylenth1**
- 4 alias-free oscillators with up to 8 unison voices each (32 voices per note), 16-note polyphony (up to 512 voices), and 2 filter sections. Reputation: "warm", "ready-to-mix", "minimal CPU cost" — [Dubspot](https://blog.dubspot.com/plugins/compare/spire-vs-sylenth1)

**u-he Hive 2**
- 2 pairs of stereo oscillators (9 waveforms plus 140 wavetables), up to 16x unison per oscillator, and 16-voice polyphony. Modulation: a 12x2 mod matrix and 7 FX. Described as "Extremely CPU-efficient", running "dozens of instances" — [Dubspot Hive 2](https://blog.dubspot.com/plugins/hive-2)

**Native Instruments Massive X**
- Wavetable oscillators with spectral manipulation. 16 macros, double the original Massive's 8. A macro can be a depth-and-direction modifier or a bidirectional "mirror" of a single control. Macros map to Komplete Kontrol hardware — [SOS Massive X review p.2](https://www.soundonsound.com/reviews/native-instruments-massive-x?page=2)
- 3 Performer mod sources, each storing up to 12 patterns that switch from one keyboard octave. Unison has a multi-mode Spread that detunes up to ±1 octave ("Deep Note"), and a chord mode that morphs between chord and spread — [SOS](https://www.soundonsound.com/reviews/native-instruments-massive-x?page=2)
- Launch criticisms: only macros were automatable, there was no MIDI CC, the Performers were "daunting", and the manuals were incomplete — [SOS](https://www.soundonsound.com/reviews/native-instruments-massive-x?page=2)

**UVI Falcon 3 (review Jul 2025; €/$299)**
- Hierarchy: Multi > Part > Program > Layer > Keygroup > Oscillator. Effects, events and modulation can sit at the Program, Layer or Keygroup level. Modulators target only levels below where they are inserted — [Rekkerd Falcon 3 review](https://rekkerd.org/review-falcon-3-creative-hybrid-instrument-by-uvi)
- 20 oscillators (additive, subtractive, FM, wavetable, physical modelling, IRCAM granular and stretch), 100+ FX, 60+ event processors and 12 modulators. **No spectral synthesis.** CPU is "well optimised", but stacking many Parts strains machines — [Rekkerd](https://rekkerd.org/review-falcon-3-creative-hybrid-instrument-by-uvi)
- Criticisms: deep levels are hard to tell apart in the UI, the platform is not fully modular, and it takes a steep time investment to learn — [Rekkerd](https://rekkerd.org/review-falcon-3-creative-hybrid-instrument-by-uvi)

**Korg Wavestate (hardware, Wave Sequencing 2.0)**
- Up to 4 Layers per Performance, each with its own FX and arp. A **Vector Envelope and Joystick mix the four layers** and can also modulate other parameters — [Korg Wavestate page](https://www.korg.com/us/products/synthesizers/wavestate/)
- Sequences split into independent Lanes (timing, sample, pitch, shape, gate, step value), each with its own length and loop points, so each note of a chord can play something different. The per-sound settings for the 8 Mod Knobs are saved with the sound. Polyphony is 64 stereo voices. A dice button randomises intelligently, either per section or for the whole sound — [Korg](https://www.korg.com/us/products/synthesizers/wavestate/)

**Output Arcade 2 (Oct 2021)**
- A loop and sample "kit" instrument. v2 added chromatic, multi-layered Note Kits (with velocity layers and round robins) and an AI tool that turns dragged-in audio into a kit. Subscription at $10/month — [MusicRadar](https://www.musicradar.com/news/output-arcade-2-plugin-launched)

### Inferences
- The 8-macro count is the de facto standard: Serum 2, Phase Plant, the Wavestate mod knobs, and the original Massive. Massive X's 16 is the outlier, and Vital's 4 and Spire's 4 are the low end. Eight macros is the right default for the proposed synth.
- Omnisphere 3's Quadzone (one fader morphing four layers) is the closest commercial precedent to the proposed XY-over-4-layers design. Wavestate's joystick is the hardware precedent. The new synth needs to beat these on two axes: a 2D XY pad instead of a 1D fader, and EDM-native engines.

### Gaps
- No primary source fetched for: Serum 2 unison maximum (Serum 1 is widely reported at 16, but not verified here); Omnisphere's 8-part multi and the Orb (search results pointed to the Spectrasonics manual hierarchy pages, but these were not fetched); Diva; Rob Papen (Blue III, Predator 3); Heavyocity and NI Play Series; Output Portal; Arturia PolyBrute Morphée. These remain unverified.

## 2. Which synths are known for the biggest supersaws, and why

### Takeaway
Spire is the most often cited "best-in-class supersaw", and Sylenth1 the warm, mix-ready standard. Both rest on 4 oscillators of 8-9 unison voices, not extreme per-oscillator counts. Vital, Phase Plant and Massive X differentiate through unison *stacking modes* (chord, octave and harmonic stacks, and spectral or frame spread) rather than raw voice count.

### Cited Findings
- Spire is described as having a "best-in-class supersaw and unison engine" with a wide, glassy sound, at 9 voices per oscillator × 4 oscillators — [Dubspot](https://blog.dubspot.com/plugins/compare/spire-vs-sylenth1)
- Sylenth1's supersaws are called "fatter and warmer", at 8 voices × 4 oscillators = 32 per note — [Dubspot](https://blog.dubspot.com/plugins/compare/spire-vs-sylenth1)
- Vital's unison spreads the wavetable frame, distortion and *spectral morph* per unison voice, and offers 11 stack types including power chords and harmonic series — [vital source](https://raw.githubusercontent.com/mtytel/vital/main/src/synthesis/producers/synth_oscillator.h)
- Phase Plant's unison includes Frequency Stack, Pitch Stack and Shepard modes — [Kilohearts docs](https://kilohearts.com/docs/phase_plant)
- Massive X's Spread can detune unison voices up to ±1 octave and morph to chord mode — [SOS](https://www.soundonsound.com/reviews/native-instruments-massive-x?page=2)
- Omnisphere 3 added "unison phase scatter" — [MusicTech](https://musictech.com/reviews/plug-ins/spectrasonics-omnisphere-3-review/)
- Xfer says more than 3-7 voices per oscillator often just adds phasing and CPU cost, and recommends bus chorus instead — [Xfer support](https://support.xferrecords.com/article/51-serum2-sound-design-guidelines-for-optimizing-cpu-usage)

### Inferences
- "Bigness" comes from the stack of several oscillators with moderate unison, combined with per-voice decorrelation (phase scatter, frame and spectral spread) and post-voice width FX. Unison counts above 16 per oscillator show diminishing returns according to the vendor (Xfer). A design could offer many "virtual" voices cheaply through SIMD packing, as Vital does, plus phase randomisation, plus post-sum chorus or dimension processing.

### Gaps
- No objective (measured) comparison of supersaw width or detune curves between synths was found. The JP-8000-style detune-curve analyses (Adam Szabo's thesis) were not fetched. Serum 2 "hyper/dimension" unison details are not covered in the What's New PDF.

## 3. Macro and morph UX: what works for minimal clicks

### Takeaway
The patterns that work are: a fixed 8-knob macro bank saved with the patch and mapped to hardware; macros that are themselves modulatable (Serum 2); a single continuous morph control (Omnisphere Quadzone fader, Wavestate joystick); and per-section intelligent randomise (Wavestate dice). Massive X shows the failure mode: if only macros are automatable, and the performance tools are complex, users find it frustrating.

### Cited Findings
- Serum 2: 8 macros, each can be a modulation destination — [Xfer PDF](https://static.xferrecords.com/Serum%202%20What's%20New.pdf)
- Pigments: drag-and-drop modulation, unlimited modulators, and modulation of modulation amounts (v6) — [SOS](https://www.soundonsound.com/reviews/arturia-pigments-6)
- Massive X: 16 macros with "modifier" vs "mirror" mapping, auto-mapped to Komplete Kontrol hardware. Negatives: macro-only automation made DAW snapshot automation unreliable, there was no MIDI CC, and the Performer setup was "daunting" — [SOS](https://www.soundonsound.com/reviews/native-instruments-massive-x?page=2)
- Omnisphere 3 Quadzone: one continuous MIDI fader moves across splits and crossfades between the 4 layers — [SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3)
- Wavestate: the joystick and vector envelope mix 4 layers; 8 Mod Knobs per sound; and randomisation for the whole sound or a single section — [Korg](https://www.korg.com/us/products/synthesizers/wavestate/)
- Pigments 7's animated Play Mode was found distracting by one reviewer — [Attack](https://www.attackmagazine.com/reviews/gear-software/arturia-pigments-7-the-best-gets-even-better/)

### Inferences
- For "minimal clicks", the proposed synth should: expose the XY pad and 8 macros on one always-visible performance page; make every parameter host-automatable (avoid the Massive X pitfall); allow the XY position to be recorded or animated by a vector envelope (the Wavestate precedent); and offer per-layer randomise.

### Gaps
- Not researched with sources: the Omnisphere Orb's specific behaviour, the NI NKS/Komplete Kontrol macro page conventions (8 knobs per page is the commonly stated standard, unverified here), Korg Kaossilator XY design, and the Arturia PolyBrute Morphée/morph knob.

## 4. Layer-stack instruments: gain staging, voice allocation, CPU and mixing

### Takeaway
The layer-capable designs differ mainly in where FX and modulation live. Falcon is strictly hierarchical, Phase Plant isolates audio per group with polyphonic-to-mono lanes, Omnisphere has 4 layers per patch plus a global FX rack, and Wavestate gives each layer its own FX and arp. None of the sources found document automatic gain compensation across layers, which is a likely opportunity.

### Cited Findings
- Falcon: modulators affect only levels below their insertion point. Effects can live at the Program, Layer or Keygroup level. Stacking Parts strains CPU — [Rekkerd](https://rekkerd.org/review-falcon-3-creative-hybrid-instrument-by-uvi)
- Phase Plant: groups do not pass audio automatically. There are 3 lanes, polyphonic lanes must be enabled in order, and a polyphony setting caps voices (1 = mono, with retrig or legato) — [Kilohearts docs](https://kilohearts.com/docs/phase_plant)
- Serum 2: two FX busses, with M/S and multiband splitter modules — [Xfer PDF](https://static.xferrecords.com/Serum%202%20What's%20New.pdf). Xfer's recommendation is to do width on the bus rather than per voice — [Xfer support](https://support.xferrecords.com/article/51-serum2-sound-design-guidelines-for-optimizing-cpu-usage)
- Wavestate: each of the 4 layers has its own FX and arp, with a 64-stereo-voice pool — [Korg](https://www.korg.com/us/products/synthesizers/wavestate/)
- Vital: a 32-active-voice cap — [vital constants](https://raw.githubusercontent.com/mtytel/vital/main/src/common/synth_constants.h)
- Sylenth1: 16 notes, up to 512 voices — [Dubspot](https://blog.dubspot.com/plugins/compare/spire-vs-sylenth1)

### Inferences
- A layered EDM synth should have: per-layer polyphonic processing, then a mono or summed bus per layer; a global master bus with M/S and multiband splitters (the Serum 2 pattern), so the sub layer can be kept mono below about 120 Hz; and equal-power crossfade laws on the XY pad, so loudness stays constant while morphing.

### Gaps
- No sourced documentation was found for the Omnisphere 8-part multi gain staging, the Kontakt multi voice allocation, or any product's automatic loudness compensation on layer crossfades.

## 5. User complaints and gaps a new design could fix

### Takeaway
The recurring complaints are: CPU load from unison and oversampling (Vital, Serum, Pigments); depth that is hard to navigate (Falcon, Massive X); weak engine-level preset management (Pigments); and preset overload (Omnisphere's 26k patches). Spectral synthesis is missing from Falcon and is seen as weaker in Omnisphere.

### Cited Findings
- Vital users report dropouts above 16 voices with unison on a Ryzen 4500U laptop, and ask for global (not per-patch) oversampling settings. One user says the nature of Vital "is legitimately CPU-hungry" — [Vital forum](https://forum.vital.audio/t/cpu-optimization-for-vital/4810?page=4)
- Xfer acknowledges the CPU and phasing cost of high unison — [Xfer support](https://support.xferrecords.com/article/51-serum2-sound-design-guidelines-for-optimizing-cpu-usage)
- Pigments: no engine-level preset save — [SOS](https://www.soundonsound.com/reviews/arturia-pigments-6)
- Falcon: no spectral synthesis, a confusing hierarchy UI, and a steep learning curve — [Rekkerd](https://rekkerd.org/review-falcon-3-creative-hybrid-instrument-by-uvi)
- Omnisphere 3: SOS says competitors beat it on spectral warping and complex mod routing. Its only listed con is that "26,000 new patches will take an eternity" to explore — [SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3)
- Massive X: launch automation and MIDI CC limits — [SOS](https://www.soundonsound.com/reviews/native-instruments-massive-x?page=2)

### Inferences
- Openings for the new design:
  - Per-layer (engine-level) presets, so users can swap one layer without losing the rest of the patch.
  - Global quality and oversampling settings with a draft mode.
  - SIMD-packed unison.
  - Spectral-morph as a first-class engine, which is missing in Falcon and weak in Omnisphere.
  - A 2D XY pad rather than Quadzone's 1D fader.
  - Loudness-compensated morphing.
  - A flat UI with one performance page rather than deep hierarchy.

### Gaps
- Reddit and Gearspace sentiment on "muddy layering" and "preset dependence" was not directly sourced in this pass.

## 6. Approximate CPU reputations

### Takeaway
Light: Hive 2, Sylenth1 and (per MusicTech) Omnisphere 3. Moderate to heavy: Vital, Serum at high unison, Pigments (improved in v7), and many stacked Parts in Falcon. Spire is moderate and scales with stacking.

### Cited Findings
- Hive 2 is "Extremely CPU-efficient", running dozens of instances — [Dubspot](https://blog.dubspot.com/plugins/hive-2)
- Sylenth1 has "minimal CPU cost", while Spire's stacked patches cost more — [Dubspot](https://blog.dubspot.com/plugins/compare/spire-vs-sylenth1)
- Omnisphere 3 is "Extremely CPU-friendly" on an M2 Air — [MusicTech](https://musictech.com/reviews/plug-ins/spectrasonics-omnisphere-3-review/)
- Pigments 7 improved CPU usage — [Attack](https://www.attackmagazine.com/reviews/gear-software/arturia-pigments-7-the-best-gets-even-better/). A user found Pigments heavier than Surge XT — [Vital forum](https://forum.vital.audio/t/cpu-optimization-for-vital/4810?page=4)
- Vital: mixed reports (laptop dropouts vs under 10% CPU on a desktop) — [Vital forum](https://forum.vital.audio/t/cpu-optimization-for-vital/4810?page=4)
- Falcon: well optimised, but many Parts strain the system — [Rekkerd](https://rekkerd.org/review-falcon-3-creative-hybrid-instrument-by-uvi)

### Inferences
- A four-layer synth that runs four full engines per voice could easily be several times the cost of a single-engine synth. Per-layer voice caps, an idle-layer bypass when the XY weight is near 0, and bus-level width FX are the obvious mitigations.

### Gaps
- No standardised benchmark (for example, % CPU per instance on the same machine) was found across these synths. All the figures above are reputation or anecdote.
