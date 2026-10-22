# Spectrasonics Omnisphere 3 (and 2.x lineage): architecture, depth and layering features

Research date: 2026-10-22. Version timeline below comes from cited sources. The Spectrasonics online manual pages fetched were mostly for v1 and v2, so v3 per-parameter detail is thin and is flagged where it matters.

**Version timeline (sourced):**
- v1.5: reviewed by SOS in May 2011. It had 2 layers, the Orb and the Omni TR iPad app ([SOS 1.5](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-15)).
- v2.0: reviewed by SOS in August 2015. It had 2 layers (A/B), 400+ wavetables and 58 FX ([SOS 2](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-2)).
- v2.5: August 2018, unveiled at Superbooth 2018. It brought 4 layers, Hardware Synth Integration, 8 LFOs and 12 envelopes ([Synth Anatomy](https://synthanatomy.com/2018/08/omnisphere-2-5-is-out-hardware-synth-integration-new-synthesis-features-more.html)).
- v2.6: announced at NAMM, 24 January 2019. It took support to 60+ hardware synths and enhanced the arpeggiator ([Synthtopia](https://www.synthtopia.com/content/2019/01/24/spectrasonics-announces-omnisphere-2-6/)).
- v2.8: around 19 October 2021. It added Sonic Extensions support and arp Strum/Humanity/Dynamics ([KVR](https://www.kvraudio.com/news-print.php?id=53093)).
- v3.0: 21 October 2025 ([Synthtopia](https://www.synthtopia.com/?p=150678)).

## What is new in Omnisphere 3 (release date, features) vs 2.8?

### Takeaway
Omnisphere 3 shipped on 21 October 2025 at $499, or $199 to upgrade. It keeps the 4-layer architecture and adds:
- Quadzone zone-morphing across layers
- 36 new filter types with circuit-modelled saturation
- Oscillator Drift
- Unison Phase Scatter and Analog for samples
- Granular, Unison and Harmonia usable at the same time
- A polyphonic dual frequency shifter
- MPE
- Adaptive Global Controls and Patch Mutations
- 35 new effects, also available as a standalone FX Rack plugin
- 300+ new hardware profiles

The GUI and the overall paradigm are largely unchanged.

### Cited Findings
- Released 21 October 2025. It costs $499 / €399 new and $199 to upgrade — [Synthtopia](https://www.synthtopia.com/?p=150678); MusicTech confirms the $199 upgrade also covers Omnisphere 1 licence holders — [MusicTech, 15 Dec 2025](https://musictech.com/reviews/plug-ins/spectrasonics-omnisphere-3-review/). SOS lists an Atmosphere upgrade at $249 — [SOS review, Mar 2026](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3). Dubspot quotes $479, which conflicts with the other sources and may reflect a later or sale price — [Dubspot](https://blog.dubspot.com/plugins/compare/omnisphere-3-vs-serum-2).
- Filters: 36 new filter types in seven "sonic colors": Classic, Jupiter, Beefy, OB, French, Subtle and Sauce. They come with circuit- or component-modelled filter saturation — [Synthtopia](https://www.synthtopia.com/?p=150678); [Spectrasonics](https://www.spectrasonics.net/products/omnisphere/). SOS counts 35 — [SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3).
- Oscillators:
  - New Oscillator Drift for analog instability.
  - Over 600 morphing wavetables; SOS says 638 DSP waveforms, each a modulatable morphing wavetable.
  - New "sweepable wavetables" — [Synthtopia](https://www.synthtopia.com/?p=150678); [SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3); [Spectrasonics](https://www.spectrasonics.net/products/omnisphere/).
  - MusicTech lists "New sound sources and EDM wavetables" as a key feature — [MusicTech](https://musictech.com/reviews/plug-ins/spectrasonics-omnisphere-3-review/).
- Unison gains Phase Scatter, Mix and Analog features for Sample Mode. Granular, Unison and Harmonia can now be blended at the same time — [Synthtopia](https://www.synthtopia.com/?p=150678). In v2.0, only one of the three could run per layer — [SOS 2015](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-2).
- The polyphonic dual frequency shifter tracks the keyboard. SOS says it lets chords "glide in two directions" — [Spectrasonics](https://www.spectrasonics.net/products/omnisphere/); [SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3).
- New glide modes: OBXA, Minimoog and Odyssey curves, Auto-Bend Up/Down, and CS-80-style Glissando — [Synthtopia](https://www.synthtopia.com/?p=150678); [Spectrasonics](https://www.spectrasonics.net/products/omnisphere/).
- Quadzone gives:
  - splits, crossfades and velocity switching
  - multiple zones per layer
  - a continuous MIDI fader to move between zones

  Zones are morphed by note position, velocity or the fader, and the fader can itself be a mod target — [SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3); [MusicTech](https://musictech.com/reviews/plug-ins/spectrasonics-omnisphere-3-review/).
- Adaptive Global Controls cover Tone, Ambience, Filter, Envelope, and Vibrato & Unison. They "analyze each patch automatically" to give musical results. Patch Mutations generate variations from Subtle to Extreme, and each is saved — [Spectrasonics](https://www.spectrasonics.net/products/omnisphere/); [Synthtopia](https://www.synthtopia.com/?p=150678).
- Expression: MPE mode, Adaptive MIDI Scaling, and smoothing with separate Rise and Fall times — [Synthtopia](https://www.synthtopia.com/?p=150678).
- Effects:
  - There are 35 new effects: 4 reverbs, 4 delays, 6 creative, 3 modulation, 6 EQ, 5 dynamics, 4 color and 3 distortion — [Synthtopia](https://www.synthtopia.com/?p=150678).
  - That makes 93 in total, or 103 with Sonic Extensions.
  - Each FX Rack holds up to 4 effects, and up to 53 racks can be open at once.
  - The Omni FX Rack runs as a VST/AU/AAX plugin in any DAW — [SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3).
- Content:
  - 18 new libraries with 26,421 patches, plus 3 legacy libraries with 9,242 patches.
  - 5,590 soundsources and 152 multis, in about 62 GB, the same size as v2 thanks to lossless optimisation — [MusicTech](https://musictech.com/reviews/plug-ins/spectrasonics-omnisphere-3-review/); [Spectrasonics](https://www.spectrasonics.net/products/omnisphere/).
  - Categories include "Club Land" and "Electronic Production" — [SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3).
- Hardware: 300+ new hardware profiles. 32 named synth models were added, including Moog One, Prophet 5/10, Jupiter-X, Novation Summit and MiniFreak — [Synthtopia](https://www.synthtopia.com/?p=150678). MusicTech also mentions Virus TI and Osmose support — [MusicTech](https://musictech.com/reviews/plug-ins/spectrasonics-omnisphere-3-review/).
- Fully backwards compatible with v1 and v2 projects — [Spectrasonics](https://www.spectrasonics.net/products/omnisphere/).
- The GUI "looks much the same as before" — [SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3).
- System requirements: macOS 13 or later or Windows 10 or later; 8 GB RAM minimum, 16 GB or more recommended; 64 GB of disk — [SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3).

### Inferences
- In v3, Spectrasonics spent its effort on "analog authenticity": filter colours named after classic synths, saturation, drift and modelled glide curves. It also added "macro intelligence": adaptive global controls and mutations. It did not add a new synthesis paradigm such as spectral or wavetable editing.
- Lifting the v2 limit of one of Unison/Harmonia/Granular per layer is a direct depth upgrade. A single layer can now be stacked, harmonised and granulated at once.

### Gaps
- No official v3 changelog listing every 2.8 to 3.0 delta was fetched. Whether layer, LFO or envelope counts changed in v3 is unconfirmed; the sources only confirm 4 layers.
- The internal mechanics of the Adaptive Global Controls ("analysis") are not documented in the sources found.

## Architecture: parts, multis, layers, oscillators, filters, modulation

### Takeaway
The structure is:
- A Multi holds 8 Parts.
- Each Part is a Patch with up to 4 layers. There were 2 layers (A/B) until v2.5 in 2018 raised it to 4.
- Each layer is either a sample-based soundsource or a DSP wavetable oscillator. It can be extended with Harmonia, Unison, Granular, FM, Ring Mod and Waveshaper, and runs into a Dual Filter.
- Modulation is a right-click-anywhere matrix ("Flex-Mod"). Since v2.5 it has 8 LFOs and 12 envelopes.

### Cited Findings
- Up to 4 soundsources are layered per patch, and a multi holds 8 patches. Each soundsource is one audio file of any length. Users can import WAV/AIFF up to 24-bit/192 kHz, including whole tracks or mixes, as oscillator material. Omnisphere describes itself as "a synthesizer first", not a multi-velocity key-mapped sampler — [Spectrasonics](https://www.spectrasonics.net/products/omnisphere/).
- The 4-layer patch, a doubled mod matrix, 8 LFOs, 12 envelopes, 100+ new wavetables, state-variable filters and "shared signal path routing" all arrived in v2.5 (August 2018) — [Synth Anatomy](https://synthanatomy.com/2018/08/omnisphere-2-5-is-out-hardware-synth-integration-new-synthesis-features-more.html).
- v2.0 (2015) layer details:
  - Two layers (A/B), each a synth or sample oscillator.
  - Each layer has a dual filter in serial or parallel.
  - 400+ DSP wavetables replaced a handful of waveforms.
  - FM and Ring Mod can use the wavetables as modulators.
  - 8 new filter types, including vowel, power and resonator.
  - A polyphonic LFO and a polyphonic mod envelope — [SOS 2015](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-2).
- Harmonia adds 4 oscillators per layer, each with transpose, detune, shape, sync, pan and mix. In v1.5 it was described as "multiplies an oscillator up to four times" with per-voice level and fine tune — [SOS 2015](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-2); [SOS 1.5](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-15).
- In v1.5, the Waveshaper gained Crusher and Reducer modules at oscillator level, which makes them polyphonic — [SOS 1.5](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-15).
- The v2 granular engine has smaller grains plus a legacy mode. Speed and Position allow time-stretching and reordering — [SOS 2015](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-2). v2.5 redesigned the granular layout with a waveform display — [Synth Anatomy](https://synthanatomy.com/2018/08/omnisphere-2-5-is-out-hardware-synth-integration-new-synthesis-features-more.html).
- Modulation: right-click any parameter to make it a mod destination. Sources include LFOs, envelopes and bias sources. The mod matrix can target the 4 layers individually or together — [MusicTech](https://musictech.com/reviews/plug-ins/spectrasonics-omnisphere-3-review/). The v1 manual describes right-click > Modulate > All Mod Sources routing, which is the Flex-Mod system — [Spectrasonics v1 manual](https://support.spectrasonics.net/manual/Omnisphere/main/all.htm).
- Part-level controls (v1 manual):
  - Voices 1–64 per Part, counting every sounding note including release tails.
  - Glide 0–2 s, polyphonic by default.
  - Bend up to ±24 semitones.
  - Attack and Release 0–100 s.
  - Layer and Part mix from -inf to +9.54 dB.
  - The Master LPF/HPF cannot self-oscillate, but the Edit-page Dual Filter can.
  - One scale per Part, up to 8 per Multi — [Spectrasonics v1 manual](https://support.spectrasonics.net/manual/Omnisphere/main/all.htm).
- In v2.8 the arpeggiator gained a Strum step modifier, Humanity (timing randomisation) and Dynamics (velocity randomisation) knobs, plus Arp Humanity and Arp Life mod targets — [KVR](https://www.kvraudio.com/news-print.php?id=53093). In v2 it supported up to 8 layered arps with per-step note offsets, giving basic step sequencing — [SOS 2015](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-2).
- The engine is "STEAM", shared with Trilian and Keyscape. v3 ships as standalone plus VST/AU/AAX — [Spectrasonics](https://www.spectrasonics.net/products/omnisphere/).

### Inferences
- The layer model is "4 full voices per patch, 8 patches per multi". That gives up to 32 layer-engines per instance, each with its own filter and modulation, plus a shared FX rack and aux sends.
- The shared signal-path routing added in v2.5 (exact topology not documented here) hints that layers can be fed into common processing rather than being purely parallel.

### Gaps
- No source fetched documents **Timbre Shift**, the **Layer Mixer** UI or **Filter Zone** in detail. From general knowledge, Timbre Shift is a sample-mode control that shifts formant/timbre independently of pitch, and Filter Zone is a filter-morph control. Both are unverified here.
- Envelope shapes and segment counts, LFO waveforms, and exact Flex-Mod depth or curve maths were not found. The Omnisphere 2 online reference guide (support.spectrasonics.net/manual/Omnisphere2/...) is the place to verify them.

## Depth/size features: Unison, Harmonia, Granular, FX, Innerspace, Drift, Orb, Sound Lock, Stack, Arp

### Takeaway
Omnisphere builds "size" at three levels:
- **Inside one voice:** Unison up to 8 voices with Drift/Analog; Harmonia with 4 harmonised copies; granular smear; and, in v3, all three combined plus Phase Scatter.
- **Across voices:** 4 layers per patch, Quadzone morphing, and Stack mode with up to 8 patches layered or split.
- **In space:** 58 effects in v2 and 93 in v3, including Innerspace convolution with arbitrary impulse files, aux racks, and Orb-driven macro motion.

### Cited Findings
- **Unison** in v2: up to 8 voices with Drift and Analog parameters. It was not available for samples, and 8-voice unison is CPU-heavy — [SOS 2015](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-2). v3 adds Phase Scatter, Mix and Analog for Sample Mode — [Synthtopia](https://www.synthtopia.com/?p=150678).
- **Harmonia**: 4 oscillators per layer with transpose, detune, shape, sync, pan and mix — [SOS 2015](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-2).
- **Exclusivity removed**: v2 allowed only one of Unison, Harmonia or Granular per layer ([SOS 2015](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-2)). v3 allows them to be blended simultaneously ([Synthtopia](https://www.synthtopia.com/?p=150678)).
- **Effects in v2**:
  - 58 effects, 25 of them new.
  - 4 aux effect slots alongside layer and common racks.
  - Effects are wired into the mod matrix.
  - New units included Innerspace (convolution-style "space imprinting"), Quad Resonator (4 parallel comb filters), Thriftshop Speaker, Analogue Phaser and Analog Flanger — [SOS 2015](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-2).
  - Eric Persing calls Innerspace "a sound designer's secret" — [MusicRadar, 10 Feb 2015](https://www.musicradar.com/news/tech/spectrasonics-eric-persing-talks-omnisphere-2-sound-design-and-more-616236).
- **Effects in v3**: 93 effects, or 103 with Sonic Extensions. Each rack holds 4 units, with up to 53 racks — [SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3).
- **Analog drift**: v3 adds Oscillator Drift and circuit-modelled filter saturation — [Spectrasonics](https://www.spectrasonics.net/products/omnisphere/).
- **Orb** (per the official v2 manual):
  - It is a circular controller that morphs between "automatically generated parameter scenes" which depend on the patch, so it works on every patch without setup.
  - **Angle** "defines which scenes or sets of parameters it manipulates". **Radius** "defines the intensity", with more dramatic changes toward the edge. The centre or CLEAR returns the original patch.
  - **Depth** scales overall intensity.
  - **Dice** "instantly creates a brand new group of intelligent sonic modifications".
  - **Inertia** leaves a "movement trail", like a ball bouncing inside a circular enclosure. **Attractor** makes the cursor "behave like a non-linear pendulum".
  - Motion can be recorded and synced to the host. The Orb can also be routed manually as a mod source in the matrix.
  - Turning it on activates extra processing modules and Aux FX — [Spectrasonics Orb manual](https://support.spectrasonics.net/manual/Omnisphere2/orb/index.html).
  - An Orb mod routing overrides DICE. Orb recordings can be MOTION, 1, 2 or 4 bars, triggered by LEGATO or SONG POSITION. Inertia is mouse/touch only and does nothing under MIDI — [v1 manual](https://support.spectrasonics.net/manual/Omnisphere/main/all.htm).
- **Sound Lock** carries favourite arp or effect settings into later browsing. **Sound Match** lists patches similar to the current one. **Stack/Multi mode** layers or splits up to 8 sounds and is more CPU-efficient than multiple instances — [SOS 2015](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-2).
- **Quadzone** (v3): splits, crossfades, velocity switching and multiple zones per layer, morphed by a fader or by note or velocity — [SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3); [MusicTech](https://musictech.com/reviews/plug-ins/spectrasonics-omnisphere-3-review/).

### Inferences
- **Orb maths.** Spectrasonics does not publish it. The documented behaviour (angle picks a scene, radius sets intensity, centre is the identity) suggests polar interpolation:
  - a set of N parameter-offset vectors ("scenes") placed around the circle
  - the angle blends between adjacent scenes
  - the radius scales the blended offset from the patch's base values
  - DICE regenerates the scene vectors
  - inertia and attractor add a 2-D physics simulation (momentum, wall bounce, pendulum pull) driving the cursor

  This is an inference, not a documented spec. A new synth could borrow it openly: a "scene ring" macro with randomisable scenes, a visible physics mode, and per-scene parameter lists the user can see and edit. Omnisphere's scenes are opaque.
- **The depth recipe implied by the features:**
  - stack within a voice (Unison/Harmonia)
  - texture it (granular, organic samples)
  - destabilise it (drift, analog)
  - place it in imprinted space (Innerspace convolution of unusual IRs, aux reverbs)
  - add slow macro motion (Orb, Quadzone faders, polyphonic LFOs)

### Gaps
- No source fetched gives Unison's maximum detune range, its spread curve, or Phase Scatter's exact behaviour.
- No source details Innerspace's IR library size or parameters, beyond its convolution-style "imprinting" and user audio import.

## How Omnisphere achieves deep/cinematic pads and huge leads; sound-design approaches

### Takeaway
The signature is source material plus space. Huge organic and psychoacoustic recordings (5,590 soundsources), or the user's own audio, are used as oscillators. They are then shaped with granular, waveshaping, FX and convolution. "Size" comes from layering contrasting sources (synth wave plus organic texture) across the 4 layers.

### Cited Findings
- Persing on sound design: "Omnisphere really is a creative tool, not just a library of sounds". "The creative part is being able to load your own audio in, and use it as a waveform", then shape it with granular, effects and waveshaping. v1 categorised sounds by origin: "hybrid-organic, psychoacoustic, synthesised" — [MusicRadar, 10 Feb 2015](https://www.musicradar.com/news/tech/spectrasonics-eric-persing-talks-omnisphere-2-sound-design-and-more-616236).
- v2 made each waveform a complete wavetable. Persing says "we can make each complete wavetables" — [MusicRadar](https://www.musicradar.com/news/tech/spectrasonics-eric-persing-talks-omnisphere-2-sound-design-and-more-616236).
- Reviewers and users single out pads and atmospheres. Dubspot calls it strongest for "cinematic pads, evolving textures" — [Dubspot](https://blog.dubspot.com/plugins/compare/omnisphere-3-vs-serum-2). VI-Control users prefer Omnisphere for keys, pads and vintage brass — [VI-Control, Nov 2025](https://vi-control.net/community/threads/does-serum-offer-something-omnisphere-doesnt.167997/).
- In v3, the classic patches were remastered with v3 features and effects — [Spectrasonics](https://www.spectrasonics.net/products/omnisphere/).

### Inferences
- The "Omnisphere pad" formula looks like this:
  - a sampled organic or psychoacoustic source on 1–2 layers, often granular-smeared
  - a wavetable or analog layer for tonal focus
  - Harmonia or Unison for width
  - slow polyphonic LFOs and drift for motion
  - Innerspace or long reverbs for depth

### Gaps
- No primary interview with specific step-by-step patch recipes from Persing or other Spectrasonics designers was retrieved. The designingsound.org (2010) and SOS "Eric Persing: creating Spectrasonics" articles exist but were not fetched.

## Hardware synth integration

### Takeaway
Hardware Synth Integration arrived in v2.5 in 2018. Supported hardware synths act as plug-and-play controllers: their knobs map to Omnisphere parameters with no mapping step, and matching patches are included. Coverage grew to 30+ synths at launch, 60+ in v2.6, and 300+ new profiles in v3, now including controllers. These are control profiles, not emulations. Emulation-style flavour comes separately from filter colours and glide models.

### Cited Findings
- v2.5: open a profile and the hardware's knobs control Omnisphere with "no mapping procedure". 30+ synths at launch, plus 1,000+ hardware-matched patches that reproduce those synths' waveforms and filters — [Synth Anatomy, Aug 2018](https://synthanatomy.com/2018/08/omnisphere-2-5-is-out-hardware-synth-integration-new-synthesis-features-more.html).
- v2.6 (Jan 2019): 60+ supported synths, with 30 new profiles including Juno-106, Andromeda A6, Minilogue XD and Moog One — [Synthtopia](https://www.synthtopia.com/content/2019/01/24/spectrasonics-announces-omnisphere-2-6/).
- v3: 300+ new profiles covering "every major brand" of synth and controller, with 32 named models added — [SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3); [Synthtopia](https://www.synthtopia.com/?p=150678).
- The v3 filter colours "Jupiter" and "OB", and the OBXA/Minimoog/Odyssey glide curves, are the closest things to vintage emulation — [Synthtopia](https://www.synthtopia.com/?p=150678).

### Inferences
- There is no evidence of dedicated "Jupiter-8 emulation" profiles. "Jupiter" is a filter colour in v3.

### Gaps
- A full list of profiles was not retrieved.

## CPU and voice management

### Takeaway
Polyphony is set per Part (1–64 voices, release tails included), and inactive layers use no CPU. Reviewers in 2025 call v3 "extremely CPU-friendly" on Apple Silicon, though 8-voice Unison and heavy FX patches are expensive.

### Cited Findings
- Voices 1–64 per Part, counting all sounding notes. Inactive layers consume no CPU. Turning the Orb on adds processing and FX — [v1 manual](https://support.spectrasonics.net/manual/Omnisphere/main/all.htm).
- In 2015, 8-voice Unison was CPU-heavy, and some factory patches crackled on a near-minimum Mac. Stack mode is more efficient than running multiple instances — [SOS 2015](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-2).
- In v2, progressive loading started playback sooner — [SOS 2015](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-2).
- MusicTech lists v3 as "Extremely CPU-friendly" and says it runs well on an M2 MacBook Air with the library on an external drive — [MusicTech](https://musictech.com/reviews/plug-ins/spectrasonics-omnisphere-3-review/). Dubspot instead warns of the "CPU and RAM it demands", without figures — [Dubspot](https://blog.dubspot.com/plugins/compare/omnisphere-3-vs-serum-2).
- The library drive must not be exFAT or FAT32 — [Spectrasonics](https://www.spectrasonics.net/products/omnisphere/).

### Gaps
- No benchmark figures were found. Voice-stealing algorithm details were not found.

## What users say it lacks for EDM

### Takeaway
Users and reviewers rate Omnisphere as top-tier for pads, keys and atmospheres. For modern EDM, they say:
- it sounds "laid back" next to Serum
- its modulation is less visual and slower to work with
- it lacks spectral wavetable warping and a wavetable editor
- its paradigm is preset browsing rather than bass design from scratch

### Cited Findings
- SOS notes that rivals may offer "more complex modulation routing, spectral wavetable warping, and animated graphical displays" — [SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3).
- VI-Control users (16 Nov 2025):
  - Serum's modulation is "easier and faster", and "way easier process wise and visual wise".
  - Serum 2 is "fantastic for… modern EDM-type sounds (bitey, crystal clear and punchy)".
  - Omnisphere sounds "a bit more laid back".
  - Browsing Omnisphere's presets is tiring.
  - Counterpoint: "omni can do everything" — [VI-Control](https://vi-control.net/community/threads/does-serum-offer-something-omnisphere-doesnt.167997/).
- Dubspot frames Serum 2 as the standard for EDM and bass, where you "sculpt aggressive, precise patches yourself", with a wavetable-first workflow and visual modulation. It frames Omnisphere around ready-made sounds — [Dubspot](https://blog.dubspot.com/plugins/compare/omnisphere-3-vs-serum-2).
- The GUI is largely unchanged in v3 — [SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3). Gearspace has a thread titled "Omnisphere user interface feels dated" (not fetched) — [Gearspace](https://gearspace.com/threads/omnisphere-user-interface-feels-dated.1408079/).
- Older criticisms: no analogue envelope modelling, and basic sample import (no loop control or tempo matching) — [SOS 2015](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-2).

### Inferences
**Ideas a new synth could borrow:**
- 4 independent layers with their own filters and mods, plus zone and velocity morphing across them (Quadzone-like).
- Per-layer Unison, Harmonia and Granular all at once.
- Analog drift and modelled filter saturation per layer.
- Convolution "imprint" with user IRs.
- An Orb-style polar macro with physics.
- Adaptive macros and patch mutation.
- Sound Lock: keep the FX/arp while browsing.
- A Stack mode for splits and layers.

**Ideas to improve on for EDM:**
- Make it forward and aggressive by default: punchy transients, an optional built-in OTT-style multiband compressor, and stronger distortion and waveshaping.
- Visual drag-and-drop modulation, with animated displays on every mod.
- A user wavetable editor with spectral warp modes.
- A transparent Orb whose scene contents are visible and editable.
- Tempo-synced sample import with loop points.
- Layer-level sidechain and ducking.
- Per-layer, phase-aligned unison for tight mono-compatible lows (Phase Scatter shows Spectrasonics is aware of this).
- Polyphonic per-layer FX inserts at no extra cost.

### Gaps
- No quantitative user surveys were found. The opinions come from forums and reviews.
