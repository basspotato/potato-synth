# Yamaha Synthesizer Architecture as a Reference for a 4-8 Layer "Super Architecture" EDM Synth

## Montage M (2023+) and Montage (2016): Performance / Part / Element architecture and concrete limits

### Takeaway
Yamaha's flagship design is a three-level hierarchy: Performance (up to 16 Parts) > Part (one engine: AWM2 sample with 8 Elements on Montage, 128 on Montage M; FM-X with 8 operators; or AN-X VA) > Element/Operator/Oscillator. Every Part has its own two insert FX, eight Assign Knobs and 16 control destinations, and polyphony is budgeted per engine (Montage M: 128 + 128 + 128 + 16 = 400 notes).

### Cited Findings
- Montage M: 16 Parts per Performance; AWM2 up to 128 elements per part; AN-X has 3 oscillators (5 waveforms) plus noise and 2 filters with 10 filter types; FM-X has 8 operators. — [Yamaha Europe, Montage M product page](https://europe.yamaha.com/en/musical-instruments/keyboards/products/synthesizers/montagem/index.html)
- Montage M polyphony is 400 notes: AWM2 preset-wave Parts 128, AWM2 user-wave Parts 128, FM-X 128, AN-X 16. The original Montage had 256 notes. — [Yamaha Europe](https://europe.yamaha.com/en/musical-instruments/keyboards/products/synthesizers/montagem/index.html); confirmed by [Kirstein spec sheet](https://www.kirstein.de/en/Synthesizer/Yamaha-Montage-M8x-Synthesizer.html)
- AN-X waveforms: Saw 1, Saw 2, Triangle, Square, Sine, plus noise. It also has PWM and oscillator self-sync. — [Kirstein](https://www.kirstein.de/en/Synthesizer/Yamaha-Montage-M8x-Synthesizer.html)
- AN-X also has ring modulation, FM, a wave shaper, and "voltage drift and ageing" settings for vintage instability. — [Sound On Sound news, Montage M launch](https://www.soundonsound.com/node/4931104)
- Montage M has over 200 effects and an Envelope Follower that generates modulation from the A/D input or from another Part. — [Kirstein](https://www.kirstein.de/en/Synthesizer/Yamaha-Montage-M8x-Synthesizer.html); [Yamaha Europe](https://europe.yamaha.com/en/musical-instruments/keyboards/products/synthesizers/montagem/index.html)
- Montage M I/O and UI: USB audio interface with 32 outputs and 6 inputs, 7" touchscreen, 6 touchscreen display knobs, more than 2,700 preset Performances, poly aftertouch on the M8x only. — [Yamaha Europe](https://europe.yamaha.com/en/musical-instruments/keyboards/products/synthesizers/montagem/index.html)
- Original Montage (2016) specs:
  - AWM2: 128 notes, 8 elements per Part.
  - FM-X: 128 notes, 8 operators, 88 algorithms, 7 spectral forms.
  - 16 internal Parts, plus A/D and USB audio input Parts.
  - Effects: dual insert FX (A/B, 76 types each) on all 16 Parts; system Variation (76 types) and Reverb (12 types); Master Effect (15 types); 5-band master EQ; per-Part 3-band EQ plus 2-band EQ.
  - About 1,900 Performances.
  - — [Music Matter, Montage 8 specs](https://musicmatter.co.uk/yamaha-montage-8)
- MusicTech review: Performances are "the heart and soul of Montage"; 16 sounds per Performance; 128 notes per engine; 8 Scene buttons switch variations within a Performance (e.g. adding sounds, changing Part levels). — [MusicTech, Montage review](https://musictech.com/reviews/yamaha-montage-exclusive-review/)
- Layering and split inside a Performance: 16 Parts are available, of which 8 can be under real-time "KBD Control", meaning they are played together from the keyboard as one layered or split sound. — [yamahasynth.com, Mastering MONTAGE 1: The Super Knob](https://yamahasynth.com/?p=2020)
- Each Part and Element can be gated by Element Switches. In Bad Mister's Motion Sequence study, Part 1 has 4 Elements, all switched in by default. — [yamahasynth.com, Motion Sequence Study](https://yamahasynth.com/learn/montage-series-synthesizers/mastering-montage-motion-sequence-study/)
- XA (Expanded Articulation) element conditions, as introduced on the Motif XS and inherited by later AWM2 engines:
  - Normal; Legato (mono mode, plays only when played legato); Key Off (sounds on release).
  - Wave Cycle (elements rotate); Wave Random.
  - All AF Off, AF1 on, AF2 on (elements sound only according to the Assignable Function buttons).
  - — [Sound On Sound, Motif XS review](https://www.soundonsound.com/reviews/yamaha-motif-xs7)
- MODX (2018) uses the Montage engine at a lower price. AWM2 has 8 elements and 128-voice polyphony; FM-X has 8 operators, 88 algorithms and only 64-voice polyphony. It has a 2-in/10-out interface and the Super Knob/Motion Control. — [MusicRadar, MODX announcement](https://musicradar.com/news/modx-is-yamahas-new-mid-priced-synth-range)

### Inferences
- The Part is Yamaha's equivalent of a "layer" in a 4-8 layer synth. Each layer is a full synth with its own engine, filter, EGs, two insert FX, EQ, and 8 macro knobs. System FX (send) and Master FX sit after the layers. This maps directly onto a layer-stack VST: per-layer insert chain, shared send bus, master bus.
- Yamaha caps the "played together" set at 8 Parts (KBD Control) even though 16 exist. This is a sensible precedent for an 8-layer maximum for one playable sound.
- Montage M separates polyphony per engine (sample, FM, VA) rather than sharing one pool. AN-X at only 16 notes shows how expensive VA voices are compared with sample playback. A VST should budget voices per layer and offer per-layer voice limits and unison counts.

### Gaps
- The exact Montage M Motion Sequencer step and lane maximums, and any AN-X per-Part note limit separate from the 16-note total, were not found on the spec pages fetched.
- Whether Montage M AWM2's "128 elements per part" means all can sound at once (it appears to be mainly for key- and velocity-mapped multisample layering) was not confirmed.
- Exact Montage M effect-routing counts (inserts per Part, system and master types) were not found on the pages fetched. The original Montage figures above are the best available.

## Earlier history: DX7, CS-80, SY77/99, EX5, Motif, AN1x, Reface, MODX

### Takeaway
Yamaha has used "multiple engines or elements stacked into one voice" since the 1970s:
- CS-80: two complete synth layers per voice.
- SY77: 2 FM elements plus 2 AWM elements per voice.
- EX5: AWM2, AN, VL and FDSP elements mixed in one voice.
- Motif: 4 elements, later 8.
- Montage: 16 Parts of 8 elements or 8 operators.

The recurring constraints are polyphony that falls as you add layers, and interfaces that get harder to use.

### Cited Findings
- **CS-80 (1977–1980):**
  - 8-voice polyphony, with two independent synth layers per voice, each with its own front-panel controls.
  - Two oscillators per voice, two HPFs and two LPFs, ADSR envelopes.
  - Per-voice (polyphonic) aftertouch and a ribbon for polyphonic pitch bend and glissando.
  - 22 hardwired presets plus 4 user memories (potentiometer banks).
  - Vangelis's main synth (*Blade Runner*, *Chariots of Fire*); he called it "the only synthesizer I could describe as being a real instrument."
  - — [Wikipedia, Yamaha CS-80](https://en.wikipedia.org/wiki/Yamaha_CS-80)
- **DX7 (1983):**
  - 6 sine operators per voice, 32 algorithms, 16-voice polyphony.
  - About 200,000 units sold in three years (Wikipedia notes this figure may cover the whole DX series).
  - Many users found it hard to program because of LCD submenus and no knobs or sliders. MusicRadar called the interface "nearly impenetrable" with "tedious menus and a diminutive display"; most users relied on presets.
  - — [Wikipedia, Yamaha DX7](https://en.wikipedia.org/wiki/Yamaha_DX7)
- **SY77 (1989):**
  - AFM (a superset of DX7 6-op FM) with 45 algorithms and up to 3 feedback sources per algorithm.
  - AWM2 samples can be layered with FM elements, or used as FM modulators in place of sines.
  - Up to 2 AFM layers plus 2 AWM layers per voice.
  - 32-note polyphony (16 AFM plus 16 AWM); 16-part multitimbral.
  - Filters: 12/24 dB LP, HP, and resonant, self-oscillating.
  - — [Wikipedia, Yamaha SY77](https://en.wikipedia.org/wiki/Yamaha_SY77)
- **EX5 (1998):**
  - Voices combine elements from different engines: up to 4 AWM2 elements; fewer AWM2 elements plus 2 AN elements; one mono VL element; or one AN element plus 3 FDSP/AWM2 elements.
  - Polyphony per engine: 128 AWM2, 2 AN, 1 VL, 16 FDSP.
  - Only one non-AWM2 voice is allowed in a multitimbral Performance, and insert FX drop from four to one when a non-AWM2 voice is used.
  - — [Wikipedia, Yamaha EX5](https://en.wikipedia.org/wiki/Yamaha_EX5)
- **Motif XS (2007):**
  - Up to 8 elements per voice, up from 4 on earlier Motifs; 128-note polyphony.
  - Performances of up to 4 Parts that can be key-split, velocity-split or layered, each with its own arpeggio and insert FX.
  - 8 velocity layers in Mega Voices.
  - Insert FX: 8 per mixer, 53 types; 9 reverb, 22 chorus and 9 master FX types; VCM effects.
  - — [Sound On Sound, Motif XS review](https://www.soundonsound.com/reviews/yamaha-motif-xs7)
- **AN1x (1997):**
  - 10-note polyphony: two 5-note DSPs, one per Scene.
  - Modes: Single, Dual (5+5, two Scenes layered), Split, and Unison (five layered oscillators per Scene, each with its own tuning and stereo position).
  - 2 oscillators per voice with sync, 2-op FM (4 algorithms), ring mod and noise. An Edge waveshaper offers 128 intermediate waves.
  - Filters: non-resonant HPF in series with a resonant multimode filter (LP/HP/BP/BEF). VCA output can feed back to the mixer.
  - FX: Variation (14 types), Delay (5 types), Reverb (8 types), plus 3-band EQ.
  - 16-step sequencer.
  - — [Wikipedia, Yamaha AN1x](https://en.wikipedia.org/wiki/Yamaha_AN1x); [Sound On Sound, AN1x preview](https://www.soundonsound.com/reviews/yamaha-an1x)
- **MODX/MODX+:** a cheaper Montage-engine instrument; see the MODX finding in the first section. — [MusicRadar](https://musicradar.com/news/modx-is-yamahas-new-mid-priced-synth-range)

### Inferences
- Yamaha's history shows a trade-off at every step: more layers means fewer notes. The CS-80 spent 2 layers on 8 voices, the AN1x spent 2 Scenes on 5+5, and the EX5 had 2-note AN and 1-note VL. A CPU-bound VST with 8 layers × unison has the same problem and needs visible voice and CPU meters.
- The EX5's rule that exotic engines force simplification elsewhere (only one such voice, fewer inserts) is a warning about heterogeneous layer stacks. Keeping one engine type per layer, with a shared FX budget, is simpler.

### Gaps
- No sources were fetched for the SY99, Reface series, or MODX+ specifically (MODX+ raised FM-X polyphony and Part counts, but this was not verified). Treat any claims about them as unverified.
- The CS-80 ring modulator and sub-oscillator details were not in the source fetched.
- The DX7 feedback specification (one feedback loop per algorithm) was not confirmed in the fetched page.

## Morph and macro systems: AN1x Scenes, Montage Super Knob, Motion Control, Motion Sequencer, Scenes

### Takeaway
The AN1x interpolated parameters between two complete Scene snapshots (a true morph, not a crossfade). Montage generalized this into a macro hierarchy:
- One Super Knob drives 8 Common Assign Knobs.
- Each Part has 8 more Assign Knobs, and each Part can map up to 16 destinations with per-destination ranges and polarity.
- Motion Sequencer lanes (up to 8 active per Performance, 16 steps) can automate it all in tempo.

### Cited Findings
- **AN1x Scenes:**
  - Two Scene buttons each store a full set of synth parameters. With both selected, the mod wheel morphs between them. The earlier CS1x crossfaded instead. — [Sound On Sound, AN1x preview](https://www.soundonsound.com/reviews/yamaha-an1x)
  - Morphing interpolates individual parameters rather than crossfading audio, so different oscillator pitches produce a glissando during the morph. — [Wikipedia, AN1x](https://en.wikipedia.org/wiki/Yamaha_AN1x); [SOS](https://www.soundonsound.com/reviews/yamaha-an1x)
  - You can save a mid-morph snapshot as a new Scene, and Scene data can be copied between patches. — [Wikipedia, AN1x](https://en.wikipedia.org/wiki/Yamaha_AN1x)
  - SOS called the morph unexpected but useful, noted some extreme settings did not morph perfectly (preview unit), and said simple filter morphs give evolving sounds suited to arpeggios. — [SOS](https://www.soundonsound.com/reviews/yamaha-an1x)
- **AN1x Free EG:** 4 tracks, one parameter each. It records controller movements of up to 16 seconds per track, or 8 bars tempo-linked. Loop, key tracking and trigger are configurable, and the recording is stored as part of the voice. — [Wikipedia, AN1x](https://en.wikipedia.org/wiki/Yamaha_AN1x)
- **Montage Super Knob:**
  - At Common level there are 8 Assign Knobs, all linked to the Super Knob by default; each can be unlinked (Super Knob Link Off).
  - Each Part has its own 8 Assign Knobs, any of which can be linked to the Super Knob.
  - Each Part can have 16 Control Set destinations; the Motif had 6.
  - Each destination has its own range: VALUE 1 (min) and VALUE 2 (max), default 0–127. Setting VALUE 1 above VALUE 2 reverses the direction, and narrowing the span confines the effect to part of the sweep.
  - Common assignments reach shared items such as the A/D input and system FX sends. Part assignments reach Part parameters, including the Part's 2 insert FX.
  - Quick Edit offers 24 knob parameters per Part plus the 8 Assign Knobs, and applies offsets to stored values.
  - — [yamahasynth.com, Mastering MONTAGE 1](https://yamahasynth.com/?p=2020)
- Asked how many parameters the Super Knob can control at once, Bad Mister (Phil Clendeninn) said: "I don't know yet about 'how many' parameters, exactly." He confirmed 16 destinations per Part ("10 more than the XS/XF's Control Set") and 8 Assign Knobs per Part. — [yamahasynth.com forum](https://yamahasynth.com/community/montage-series-synthesizers/super-knob-1094)
- Yamaha markets the Montage M Super Knob as controlling "dozens" of modulations from one gesture, without an exact number. — [Yamaha Europe](https://europe.yamaha.com/en/musical-instruments/keyboards/products/synthesizers/montagem/index.html)
- MusicRadar described Motion Control and the Super Knob as a macro controller for complex automation and "morphing between layers." — [MusicRadar](https://musicradar.com/news/modx-is-yamahas-new-mid-priced-synth-range)
- **Motion Sequencer (Montage):**
  - Up to 8 lanes can be active at once ("a maximum of eight Lanes at once is the rule"), spread across Parts; the example uses 3 + 3 + 2.
  - 8 selectable sequences per lane; 16 steps, with per-step amplitude and Pulse A/B type.
  - Settings: tempo sync (Unit Multiply, e.g. 200% for half tempo, up to 6400%), Key On Reset (Off / 1st On / Each On), loop on/off, and velocity limit.
  - Each lane is a Source in Control Assign, routed to any destination (example: two lanes to cutoff, one to element pan).
  - Quick Edit motion-sequence parameters: Amplitude, Shape, Smooth, Random.
  - — [yamahasynth.com, Motion Sequence Study](https://yamahasynth.com/learn/montage-series-synthesizers/mastering-montage-motion-sequence-study/); [Mastering MONTAGE 1](https://yamahasynth.com/?p=2020)
- MusicTech wrote that the Motion Sequencer runs four lanes per Part, and that Scenes (8 buttons) switch Performance variations. — [MusicTech](https://musictech.com/reviews/yamaha-montage-exclusive-review/)
- The Super Knob itself can be automated by a Motion Sequence. — [yamahasynth.com SynthTips (title of article)](https://yamahasynth.com/community/montage-series-synthesizers/montagemodx-synthtips-how-to-automate-the-super-knob-with-a-motion-sequence)

### Inferences
- The macro hierarchy suggests a direct model for the VST: Global Macro ("Super Knob") > 8 global macros > per-layer macros (8 per layer) > up to 16 destinations per layer, each with min/max range (and polarity by inverting min/max).
  - The theoretical fan-out is 8 layers × 16 destinations = 128 per-layer destinations, plus global ones. This is our derived figure, not a Yamaha-published number.
- The AN1x's parameter interpolation is a morph in the true sense, not an audio crossfade. Combined with "save the mid-morph point as a new Scene," it is a strong pattern for a VST morph engine. It also has known artefacts: pitch glides, and stepped or discrete parameters that don't morph cleanly. Decide how discrete parameters (waveform select, filter type) behave mid-morph.
- Motion Sequencer lanes are just modulation sources inside the same assignment matrix as knobs. The VST should treat step lanes, envelope followers and macros uniformly as sources in one matrix.

### Gaps
- No authoritative published maximum total Super Knob destinations was found; Bad Mister himself said he did not know. The "128 destinations" figure in the brief is unverified.
- Montage M Motion Sequencer changes (e.g. more steps) were not verified.
- Whether Scenes store Motion Sequencer and Super Knob positions was not detailed in the sources fetched.

## FM-X and AN-X engine design notes for "huge" sounds

### Takeaway
FM-X adds two things to classic FM: 8 operators with 88 algorithms, and per-operator "spectral" waveforms with a Skirt (overtone low-pass, 0–7) and Resonance (emphasized harmonic). Rich, saw-like or formant spectra come from a single operator rather than a stack, which is cheap and useful for "huge" layers. AN-X stacks 3 oscillators, 2 filters (10 types), ring mod, FM, a wave shaper and analog drift/ageing.

### Cited Findings
- FM-X operator design:
  - 8 operators; earlier Yamaha FM mostly had 4 or 6, except the FS1r.
  - 7 spectral forms per operator: Sine, All1, All2 (all harmonics, saw-like), Odd1, Odd2 (odd harmonics, square-like), Res1, Res2 (one emphasized harmonic).
  - Skirt (0–7) acts like a low-pass cutoff on overtones: 0 gives very few, 7 gives all.
  - Resonance picks the emphasized overtone (e.g. 7 emphasizes the 8th overtone; values up to at least 14).
  - Feedback on a sine modulator at maximum, with ratio 1.00, gives the classic FM saw.
  - Final harmonics depend on ratio, waveform and resonance; brightness depends on modulator level, skirt and feedback.
  - A multimode filter and FX follow the FM-X engine.
  - — [yamahasynth.com, Manny's FM-Xplorations Article 1](https://yamahasynth.com/?p=1665)
- FM-X uses 88 algorithms; the SY77's AFM had 45 and the DX7 had 32. — [Music Matter](https://musicmatter.co.uk/yamaha-montage-8); [Wikipedia SY77](https://en.wikipedia.org/wiki/Yamaha_SY77); [Wikipedia DX7](https://en.wikipedia.org/wiki/Yamaha_DX7)
- AN-X engine: see the AN-X waveform, filter, ring mod, FM, wave shaper and drift/ageing findings in the first section. — [SOS news](https://www.soundonsound.com/node/4931104); [Kirstein](https://www.kirstein.de/en/Synthesizer/Yamaha-Montage-M8x-Synthesizer.html)
- Forum users report that Montage M FM-X Res2 at Skirt 7 shows signs of phase cancellation (forum thread title; user report, not verified). — [yamahasynth.com forum](https://yamahasynth.com/community/montage-series-synthesizers/montage-m-fm-x-res2-waveform-with-skirt-7-manifests-signs-of-phase-cancellation/)
- The AN1x's precedent for thick VA: Unison of 5 oscillators per Scene with per-oscillator detune and stereo spread, VCA-to-mixer feedback, and the Edge waveshaper (128 intermediate waves). — [Wikipedia, AN1x](https://en.wikipedia.org/wiki/Yamaha_AN1x)

### Inferences
- For an EDM layer synth, spectral-waveform operators (saw-like, square-like or resonant spectra with a "skirt" brightness control) are an efficient way to get dense layers without stacking many oscillators. Skirt is a natural macro target for a "brightness" macro.
- Analog drift and ageing in AN-X shows Yamaha considers per-voice instability essential for "big" VA. Per-voice random drift should be standard in each VA layer.

### Gaps
- No Yamaha source gave numeric ranges for FM-X feedback, or AN-X detune and unison counts.
- No source described AN-X unison specifically.

## Voice and polyphony management, gain staging, per-Part FX and master FX (VCM)

### Takeaway
Yamaha allocates polyphony per engine pool rather than one shared pool, and keeps the FX layout constant:
- Per Part: 2 inserts + EQ.
- Shared system sends: Variation + Reverb.
- Master: one Master FX + 5-band EQ.

Users mix layers with Part level and Scene recall. VCM (Virtual Circuit Modeling) analog-modelled effects are part of the insert library.

### Cited Findings
- Polyphony pools: see the per-engine polyphony findings in the first section (Montage M, original Montage, MODX). — [Yamaha Europe](https://europe.yamaha.com/en/musical-instruments/keyboards/products/synthesizers/montagem/index.html); [Music Matter](https://musicmatter.co.uk/yamaha-montage-8); [MusicRadar](https://musicradar.com/news/modx-is-yamahas-new-mid-priced-synth-range)
- Historical precedent for per-engine allocation: the SY77 had separate 16 AFM + 16 AWM pools, and the EX5 had 128 AWM2, 2 AN, 1 VL and 16 FDSP. — [Wikipedia SY77](https://en.wikipedia.org/wiki/Yamaha_SY77); [Wikipedia EX5](https://en.wikipedia.org/wiki/Yamaha_EX5)
- Montage FX layout: see the original Montage effects finding in the first section (dual inserts on all 16 Parts, A/D input Part inserts with 71 types, Variation, Reverb, Master Effect, EQ). — [Music Matter](https://musicmatter.co.uk/yamaha-montage-8)
- Scenes can change Part levels (and add sounds) for instant layer re-balancing. — [MusicTech](https://musictech.com/reviews/yamaha-montage-exclusive-review/)
- Motif XS used "Vintage Circuitry Modelled" (VCM) effects with stomp-box graphics, alongside 8 inserts per mixer and 9 master FX types. — [SOS, Motif XS](https://www.soundonsound.com/reviews/yamaha-motif-xs7)
- MODX insert FX include analog-style delay and reverb, amp sims, sidechain effects, vinyl brake, bitcrush and the Spiralizer pitch-shift. — [MusicRadar](https://musicradar.com/news/modx-is-yamahas-new-mid-priced-synth-range)
- EX5 precedent: the FX budget shrinks when heavier engines are used (four inserts drop to one). — [Wikipedia EX5](https://en.wikipedia.org/wiki/Yamaha_EX5)

### Inferences
- A VST should have two inserts per layer, shared send FX, and a master chain, matching Yamaha's long-stable layout. Per-layer EQ (3-band + 2-band on Montage) is worth copying for gain-staging and spectral carving between layers.
- Polyphony per layer type helps prevent a heavy layer (VA or unison) from stealing voices from lighter layers.

### Gaps
- No Yamaha source on internal gain staging, headroom or auto-gain when many Parts are layered was found.
- No source detailed voice-stealing priority across Parts.

## Design lessons and pitfalls (menu depth, complexity, editing many layers)

### Takeaway
Yamaha's recurring weakness is editing depth. The DX7's menus were "nearly impenetrable," and even Montage reviewers struggled with Motion Control at first. Yamaha's fixes were:
- a single macro (Super Knob) with sensible defaults (all Common knobs linked),
- Quick Edit offsets over deep edit pages,
- touchscreen navigation and Page Jump.

### Cited Findings
- DX7: complex LCD submenus and no knobs led most users to rely on presets. — [Wikipedia DX7](https://en.wikipedia.org/wiki/Yamaha_DX7)
- MusicTech's Montage reviewer "struggled here at first" with Motion Control options. The touchscreen made working through layers easier, and the reviewer recommends persevering. — [MusicTech](https://musictech.com/reviews/yamaha-montage-exclusive-review/)
- Montage M added Quick Edit encoders with LCD value readouts and a Page Jump button that opens a Quick Edit control's parameters on the touchscreen. — [SOS news](https://www.soundonsound.com/node/4931104)
- Quick Edit applies offsets to stored values per Part or at Common level, and all Common Assign Knobs are linked to the Super Knob by default. — [yamahasynth.com, Mastering MONTAGE 1](https://yamahasynth.com/?p=2020)
- Even Yamaha's own expert could not state the total parameter count the Super Knob can drive. — [yamahasynth.com forum](https://yamahasynth.com/community/montage-series-synthesizers/super-knob-1094)
- Motif XS limits: the AF element-switch buttons could not be assigned to a footswitch, limiting hands-free use of XA articulation switching. — [SOS Motif XS](https://www.soundonsound.com/reviews/yamaha-motif-xs7)
- EX5: mixing engine types forced constraints (only one non-AWM2 voice per Performance; inserts drop from 4 to 1). — [Wikipedia EX5](https://en.wikipedia.org/wiki/Yamaha_EX5)

### Inferences
- **Show the macro map.** A layer synth with 8 layers × 16 destinations needs a visible "who-controls-what" overview. The Super Knob forum question shows users lose track.
- **Default macro links on.** Global macros should drive per-layer macros by default (Yamaha's "Super Knob Link") so a new patch is immediately playable.
- **Edit relative to the base.** Provide Quick-Edit-style offset controls across all layers at once (e.g. global cutoff offset), so users needn't dive into each layer.
- **Morph parameters, not audio.** Use AN1x-style interpolation between whole-patch snapshots, plus "capture mid-morph as new snapshot." Define how discrete parameters behave mid-morph.
- **Keep layers homogeneous in their FX and voice rules.** This avoids EX5-style hidden constraints.
- **Expose per-layer voice and CPU budgets.**
- **Use a few clear trigger conditions.** Velocity and key ranges plus XA-like conditions (legato, key-off, cycle, random, button-gated) give expressive layer switching.

### Gaps
- No user studies or quantitative usability data on Montage editing were found. Review comments are anecdotal.
- Reviews of the Montage M editing workflow and Expanded Softsynth Plugin (ESP) editor were not fetched.
