# Nord (Clavia) Synthesizer Architecture as a Reference for a 4-8 Layer "Super Architecture" EDM Synth

## Nord product lines and current models (as of 2026)

### Takeaway
Nord's layered synths have always topped out at **3-4 layers/slots** sharing a **fixed voice pool of 20-48 voices**. The Nord Wave 2 (4 layers, 48 voices, VA/wavetable/FM/sample) is the most relevant reference. The Stage 4 reuses the Wave 2 engine as 3 synth layers alongside piano and organ layers. The Modular G2 shows the patchable extreme: 4 slots, 8 morph groups, 8 variations.

### Cited Findings
- **Nord Lead (1), 1995:** 4 voices, 12 with an expansion; 2 oscillators per voice. Clavia coined the term "virtual analog" with it. — [Wikipedia: Nord Lead](https://en.wikipedia.org/wiki/Nord_Lead)
- **Nord Lead 2, 1997:** 16 voices; added ring mod, filter distortion and 4 individual outputs. — [Wikipedia](https://en.wikipedia.org/wiki/Nord_Lead)
- **Nord Lead 2X, 2003:** 20 voices, 4-part multitimbral, 4 program slots for layering, each slot on its own output. Oscillators: OSC1 sine/tri/saw/pulse; OSC2 tri/saw/pulse/noise with hard sync to OSC1; linear FM from OSC2 to OSC1. Play modes are Poly, Legato, Mono, Unison Mono and Unison Poly. 297 user and 693 factory programs. — [Nord Lead 2X spec](https://www.nordkeyboards.com/node/515)
- **Nord Lead 3, 2001-2007:** 24 voices; 4-operator FM ("sinus modulation"); endless rotary knobs with lit LED collars showing each value. — [Wikipedia](https://en.wikipedia.org/wiki/Nord_Lead)
- **Nord Lead 4, 2013:**
  - Nord's spec page gives 20 voices, 4 parts, 4 program slots for layering, a 2-slot keyboard split and 4 outputs (each slot assignable). — [Nord Lead 4 specs](https://www.nordkeyboards.com/legacy-products/nord-lead-4/specifications)
  - Sound On Sound calls it a "24-voice instrument". This conflicts with Nord's 20; Nord's own spec page is the more reliable figure. — [SOS Nord Lead 4 review](https://www.soundonsound.com/reviews/clavia-nord-lead-4)
  - SOS: 99 Performances with 4 slots (A-D) each, plus 4×99 Programs; AB and CD splits. — [SOS](https://www.soundonsound.com/reviews/clavia-nord-lead-4)
- **Nord Lead A1, NAMM 2014 (still on Nord's current product list):** 26 voices, 4-part multitimbral, 4 slots, each slot can have its own output. 400 Programs (8×50) and 200 Performances (4×50). — [Nord Lead A1 specs](https://www.nordkeyboards.com/products/nord-lead-a1/specifications/)
- **Nord Wave 2:**
  - Nord's page: 4 independent layers/parts, each with its own oscillator section, LFO and effect section; 48 voices; 61 keys with aftertouch. — [Nord Wave 2 product page](https://nordkeyboards.com/products/nord-wave-2)
  - SOS: 400 Programs (16 banks of 25). Every Program stores all 4 layers. 1GB of sample memory. — [SOS Wave 2 review p.1](https://www.soundonsound.com/node/4922243)
- **Nord Stage 4:**
  - Organ 2 layers, piano 2 layers, synth 3 layers. Synth 46 voices, piano 120 voices. Up to 3 split points (4 zones) with crossfade width Lrg, Sml or Off. 512 programs plus 8 Live Programs. 3 morph sources: mod wheel (also swell pedal), control pedal, aftertouch. — [Nord Stage 4 specs](https://www.nordkeyboards.com/products/nord-stage-4/specifications/)
  - The synth section is "based on the Nord Wave 2 Synth Engine". The panel has "dedicated LED faders for each Layer", and each layer has its own effect section. — [Nord Stage 4 product page](https://www.nordkeyboards.com/products/nord-stage-4)
- **Current Nord lineup** listed on the Stage 4 page: Stage 4, Electro 7, Piano 6, Organ 3, Grand 2, Wave 2, Lead A1, Drum 3P. — [Nord Stage 4 page](https://www.nordkeyboards.com/products/nord-stage-4)
- **Nord Modular G2:**
  - 4 slots (edit buffers A-D), each holding one Patch independently. Up to 32 voices "depending on Patch complexity". 8 encoders on 4 LCDs. 8 variations per Patch. Over 160 modules in 16 categories. Up to 4 split points; a Performance combines up to 4 Patches. — [Nord Modular G2 spec](https://www.nordkeyboards.com/node/511)
  - The G2X has twice the processing power of the G2 and G2 Engine. The patch editor splits each patch into a polyphonic "voice area" and a monophonic "FX area". — [NMG2 brochure PDF](https://c3.zzounds.com/media/NMG2-brochure-ae35adcccfe9a57feb7bb22957416ada.pdf)

### Inferences
- Across 30 years Nord settled on **4 parts** as the sweet spot, and never went past 4 layers per sound on synth-focused products. The Stage 4 caps the synth section at 3 layers. Going to 8 layers in software is beyond anything in Nord's lineup.
- Polyphony has always been a **shared pool**: about 20-48 voices across all 4 layers. Total cost rises linearly with the number of layers playing at once.
- The Modular G2's "depending on Patch complexity" wording is the closest analogue to a CPU-budgeted VST. Voice count is computed from DSP load, not fixed.

### Gaps
- I found no confirmed announcement of a Nord Lead 5 or a new synth in 2025-2026. A norduserforum thread asks "When Nord Lead 5?", which suggests none had been released. The Wave 2 and Lead A1 appear to be the current synths.
- Nord Modular G1 specs were not retrieved. From memory, and unverified: 4 slots and 4 morph groups.

## Layer and slot architecture: assignment, splits, stacking, mixing, per-layer FX, editing together, voice sharing

### Takeaway
Nord's layer model is fixed slots with light controls:
- per-layer enable and select buttons
- a fader bank for volume or pan of all layers at once
- splits at fixed key positions with crossfades
- a full per-layer FX chain
- a "Group"/"Multi-Focus" mode so one knob turn edits all selected layers

Voices come from one shared pool, and unison on the Wave 2 does not consume extra voices.

### Cited Findings
- **Wave 2 Layer section:** faders control volume or pan for all four parts at once. Layers have quick focus, pan, solo and multi-edit. — [Nord Wave 2 page](https://nordkeyboards.com/products/nord-wave-2)
- **Wave 2 Group mode:** 2, 3 or 4 layers can be linked to share Filter, Envelope, Arpeggiator, LFO and Effect settings. — [Nord Wave 2 page](https://nordkeyboards.com/products/nord-wave-2)
- **Wave 2 Hold:** "latches and holds slots independently" while you play on top. — [Nord Wave 2 page](https://nordkeyboards.com/products/nord-wave-2)
- **Wave 2 layer handling (SOS):** any number of layers can be grouped so a parameter change applies to all of them. A layer can be copied to others. Layers switch on and off glitch-free within a Program. Each layer can transmit on its own MIDI channel. — [SOS Wave 2 p.1](https://www.soundonsound.com/node/4922243)
- **Wave 2 splits:** 3 split points (4 zones). Split points sit only at E/F or B/C across the bottom four octaves. Crossfades span 6 or 12 semitones. — [SOS Wave 2 p.1](https://www.soundonsound.com/node/4922243)
- **Wave 2 output and program changes:** layers combine only at the single stereo output, and switching Programs silences whatever is sounding (no seamless transitions). — [SOS Wave 2 p.1](https://www.soundonsound.com/node/4922243)
- **Wave 2 per-layer FX:**
  - Each layer has a multi-effect (Tremolo, Autopan, Ring Mod, Phaser, Chorus, Ensemble, Vibe), EQ (2-band or a sweepable mid), Drive, Delay (digital/analogue, ping-pong, filters or modulation in the feedback path) and Reverb (5 algorithms).
  - SOS: "16 simultaneously active effects potentially." — [SOS Wave 2 p.2](https://www.soundonsound.com/node/4922243?page=2)
- **Wave 2 arpeggiators:** 4, one per layer, with Poly, Gate (rhythmic slicer) and Pattern modes. — [SOS Wave 2 p.2](https://www.soundonsound.com/node/4922243?page=2)
- **Lead A1 Multi-Focus Mode:** "editing and control of all four slots simultaneously", and the setting can be saved in a Performance. Split mode gives two slots per keyboard side. — [Nord Lead A1 page](https://www.nordkeyboards.com/products/nord-lead-a1/)
- **Lead 4 slot editing:**
  - Programs can be edited inside a Performance without changing the originals, and settings can be copied between slots and Performances. All 4 slots follow the master clock. — [SOS Lead 4](https://www.soundonsound.com/reviews/clavia-nord-lead-4)
  - The headphone output monitors only slots 1 and 2. — [SOS Lead 4](https://www.soundonsound.com/reviews/clavia-nord-lead-4)
- **Lead A1 voice sharing:** 26 voices "shared across four simultaneous synth parts". Nord does not publish the allocation or stealing algorithm. — [Nord Lead A1 page](https://www.nordkeyboards.com/products/nord-lead-a1/)
- **Wave 2 unison cost:** 3-way unison "doesn't steal polyphony". — [SOS Wave 2 p.1](https://www.soundonsound.com/node/4922243)
- **Modular G2 Parameter Pages:** up to 120 parameters, in Single Patch mode or Global mode (one page controlling all 4 slots). — [NMG2 brochure](https://c3.zzounds.com/media/NMG2-brochure-ae35adcccfe9a57feb7bb22957416ada.pdf)
- **Stage 4 Layer Scenes:** 2 per program, switched with one button press (for example, swapping which layers are active). — [Stage 4 page](https://www.nordkeyboards.com/products/nord-stage-4); [Stage 4 specs](https://www.nordkeyboards.com/products/nord-stage-4/specifications/)

### Inferences
- **Patterns worth copying for a 4-8 layer VST:**
  - per-layer focus, solo, mute and enable buttons
  - a layer mixer strip showing volume or pan for all layers at once
  - a Group/Link mode that applies an edit (absolute or relative) to every selected layer
  - copy and paste of whole layers
  - per-layer arp and FX
  - optional key-zone splits with crossfades
  - "scenes" that swap the active layer set
- A key Nord idea: with Group linking, layers can share sections (filter, envelope, LFO, FX). That cuts editing effort and, in software, could cut CPU too (a shared filter or FX bus instead of N copies).
- Nord's single stereo out (Wave 2) and the per-layer FX that each run separately at full quality show the trade-off. An 8-layer VST may want a "layer FX → shared bus FX" hierarchy to keep CPU and gain under control.
- Program changes that cut the sound are a known pain point, so seamless switching is a differentiator.

### Gaps
- Nord does not document its voice-stealing or priority rules between layers.
- I could not confirm whether Wave 2 Super-wave voices count against the 48-voice pool. The SOS statement covers only the 3-way unison.

## The Morph system (macro-style multi-parameter control)

### Takeaway
Nord Morph is "hold the source button, turn any knob to its target value". Each morphed parameter then sweeps from its base value to that target as the source moves. One source drives many parameters, each with its own range and polarity. Impulse Morph adds momentary button-triggered parameter jumps (including on/off switches). The Modular G2 extends this to 8 named morph groups per patch. This is essentially a macro system with range-by-demonstration.

### Cited Findings
- **Nord Lead 2X:** a knob can respond to velocity (Velocity Morph), or to the mod wheel or control pedal to "fade continuously between two sets of values". — [Lead 2X spec](https://www.nordkeyboards.com/node/515)
- **Nord Lead 4 Morph:**
  - Up to **7 parameters per patch**, each with its own amount. Sources are mod wheel, control pedal, MIDI CC01/CC11 and velocity. There are 36 continuous and 21 switch destinations. — [SOS Lead 4](https://www.soundonsound.com/reviews/clavia-nord-lead-4)
  - Nord lists 2 morph sources: velocity, and mod wheel/control pedal. — [Lead 4 specs](https://www.nordkeyboards.com/legacy-products/nord-lead-4/specifications)
- **Nord Lead 4 Impulse Morph:** **3 momentary buttons, giving 7 combinations**, with 55 destinations in total. — [SOS Lead 4](https://www.soundonsound.com/reviews/clavia-nord-lead-4); [Lead 4 specs](https://www.nordkeyboards.com/legacy-products/nord-lead-4/specifications)
- **Nord Lead A1:** Morph from mod wheel, velocity or pedal controls several parameters at once. To set it up, "just hold the Morph button and adjust any desired knob." — [Lead A1 page](https://www.nordkeyboards.com/products/nord-lead-a1/)
- **Nord Wave 2 Morph:**
  - 4 sources (velocity, aftertouch, wheel, control pedal), each with a dedicated Morph button. — [SOS Wave 2 p.2](https://www.soundonsound.com/node/4922243?page=2)
  - "Most continuous parameters on the front panel are fair game." Range is set by holding the source button and moving the knob, across any range and in either polarity. An LED lights beside each morphed parameter. — [SOS Wave 2 p.2](https://www.soundonsound.com/node/4922243?page=2)
  - Nord: one source controls multiple parameters, and Wheel Assign can route an LFO, Osc Control or Filter to the mod wheel. — [Nord Wave 2 page](https://nordkeyboards.com/products/nord-wave-2)
- **Wave 2 Impulse Morph:** gives momentary changes, toggles whole sections on or off (filter, arp, effects) and switches modes (octave shift, arp direction, delay/reverb type). — [SOS Wave 2 p.2](https://www.soundonsound.com/node/4922243?page=2)
- **Wave 2 limits:** there is no mod matrix and no keyboard scaling, and Morphs only partly make up for the limited modulation destinations. — [SOS Wave 2 p.2](https://www.soundonsound.com/node/4922243?page=2)
- **Stage 4:** 3 morph sources (wheel/swell, control pedal, aftertouch). — [Stage 4 specs](https://www.nordkeyboards.com/products/nord-stage-4/specifications/)
- **Modular G2:**
  - 8 Morph groups per patch, set up in the editor or on the panel. One source (mod wheel, aftertouch, etc.) controls multiple parameters, each with its own range. — [NMG2 brochure](https://c3.zzounds.com/media/NMG2-brochure-ae35adcccfe9a57feb7bb22957416ada.pdf)
  - 8 panel buttons support fast Morph Group programming. — [G2 spec](https://www.nordkeyboards.com/node/511)
  - 8 Variations per patch are recalled instantly with 8 buttons (snapshot-style). — [G2 spec](https://www.nordkeyboards.com/node/511)

### Inferences
- **For macros and XY morph:**
  - Copy "hold macro + turn knob = target value". Range-by-demonstration is far faster than drawing per-parameter min/max in a mod matrix.
  - Show a per-parameter indicator (Nord's LED) for parameters a macro affects.
  - Allow morph assignments that span layers, so one macro can push filter opens on 4-8 layers at once.
- Nord's G2 **Variations** (8 snapshots) plus **Morph** suggest an XY pad that interpolates between 4 corner snapshots, a natural software extension of the idea.
- **Impulse Morph** maps to "momentary performance buttons" in EDM: a drop or build button that kills reverb, opens the filter or doubles the octave.
- Nord's early caps (7 morph parameters on the Lead 4) were hardware limits. A VST can allow unlimited targets but should still show the targets clearly.

### Gaps
- Exact morph destination counts for the Wave 2 and A1 are not published in the sources found.
- I found no documented curve shaping for Nord morphs (beyond linear range and polarity).

## Oscillator design and the "Nord sound"

### Takeaway
Nord simplifies oscillators to **one primary waveform plus a single "Osc Control"/config knob** that changes meaning by mode (sync, FM, shape, detune, sub). The Wave 2 adds multi-osc modes in its Analog category ("Multi" double/triple saws, "Super" JP-8000-style supersaws). The goal is big sounds with very few controls.

### Cited Findings
- **Nord Lead 4:**
  - Nord's spec page: OSC1 offers sine, tri, saw, fixed and modulatable pulse, plus 128 "wavetables". OSC2 offers tri, saw, pulse or noise. OSC MOD offers hard and soft sync plus 3 FM types (OSC2 modulating OSC1). Voice modes include True Unison 1, 2 and 3. — [Lead 4 specs](https://www.nordkeyboards.com/legacy-products/nord-lead-4/specifications)
  - SOS counted 92 waves, not 128, and says they are single-cycle with no sweep. SOS also calls the Lead 4's "FM" much less flexible than the Lead 3's and notes ring mod was removed. — [SOS Lead 4](https://www.soundonsound.com/reviews/clavia-nord-lead-4)
  - Wikipedia: "True Voice Unison" supports up to 4 oscillators at once. — [Wikipedia](https://en.wikipedia.org/wiki/Nord_Lead)
- **Nord Lead A1:**
  - Waveforms include traditional analog, extended analog, pulse, and algorithmic additive, FM and formant waves. An OSC Config knob gives 11 control categories (Pitch, Shape, Osc Sync, Noise Mix, Freq Mod, etc.). Voice modes include True Unison 1, 2 and 3. — [Lead A1 specs](https://www.nordkeyboards.com/products/nord-lead-a1/specifications/)
  - The product page lists configs Pitch, Detune, Sub Mix, Shape, Sync, FM, AM and Noise, tuned with one Adjust knob, for "high speed programming without the need for the usual complexity of routing and patching". It also mentions an "Extra Fat Unison" mode. — [Lead A1 page](https://www.nordkeyboards.com/products/nord-lead-a1/)
- **Nord Wave 2 Analog categories:**
  - Basic: sine, tri, saw, square, pulse (33% or 10%).
  - Shape and Misc: noise colours, plus an inharmonic bell.
  - Sync: hard-sync sounds via the Osc Ctrl knob.
  - Multi: detunable double and triple saws at octave and fifth spacings.
  - Super: JP-8000-style supersaws, plus square and organ-spectra variants.
  - Each category has a three-encoder interface and a live waveform display. — [SOS Wave 2 p.1](https://www.soundonsound.com/node/4922243)
- **Nord Wave 2 other modes:** Wavetable mode has 46 static waves with no interpolation. FM mode has 10 algorithms with 2, 3 or 4 operators. Samples use 1GB of memory, mono or stereo, with simple loops and key-group multisampling; there is no velocity switching or round-robin. — [SOS Wave 2 p.1](https://www.soundonsound.com/node/4922243)
- **Nord on the Wave 2 engine:** the Analog mode has "up to 16 oscillators", and the unison is described as "true Superwaves… generating hundreds of waveforms simultaneously". This is manufacturer marketing. — [Nord Wave 2 page](https://nordkeyboards.com/products/nord-wave-2)
- **Wave 2 per-layer synth path:** a 6-mode filter (12/24 dB LP, LP M, HP, BP, LP+HP) with drive and ADSR; an Amp ADSR with "Transient Attack" mode; an Osc Mod AD/AR envelope; one mono LFO per layer; a vibrato LFO. — [Nord Wave 2 page](https://nordkeyboards.com/products/nord-wave-2); [SOS](https://www.soundonsound.com/node/4922243)
- **SOS criticisms of the Wave 2:**
  - Oscillators randomise their start phase on each note, causing phase cancellation on repeated notes; SOS asks for a key-reset option.
  - There is no stereo voice spread and no analog "slop" parameter.
  - Dual-osc combinations (e.g. detuned pair, square plus sine) are missing as categories. — [SOS Wave 2 p.1–2](https://www.soundonsound.com/node/4922243?page=2)
- **Sound reputation:**
  - SOS on the Lead 4: "sounds great", no audible aliasing, "a powerful synthesizer". Its filter overdrive "tends to choke sounds". — [SOS Lead 4](https://www.soundonsound.com/reviews/clavia-nord-lead-4)
  - SOS on the Wave 2: "a go-to, do-anything synth like few others". — [SOS Wave 2 p.2](https://www.soundonsound.com/node/4922243?page=2)

### Inferences
- The "one waveform plus one context-dependent control knob" model, combined with a "Super" multi-voice oscillator type, gets supersaw-class width cheaply per layer. A VST layer could expose: Type → Waveform → Ctrl (meaning depends on type) → Unison/Detune.
- **Phase handling matters for EDM punch.** Random phase gives width on pads, but transients on plucks and basses smear. Offer a per-layer phase reset/free-run switch.
- Stereo spread for unison voices is an obvious gap in Nord's design worth filling.

### Gaps
- I found no authoritative source on why Nord sounds "punchy, clear, aggressive" (e.g. DSP filter design or oscillator anti-aliasing details). Nord does not publish its DSP internals.
- Exact voice counts per Wave 2 "Super" oscillator, and per Lead "True Unison 1/2/3" level, are not published.

## UI philosophy: one knob per function, performance first

### Takeaway
Nord's design is a front panel of dedicated knobs and buttons with minimal display and no deep menus. Hidden complexity is pushed into "context" knobs (Osc Ctrl/Config) and hold-to-assign gestures (Morph). Reviewers praise the speed but note the "one knob per function" claim is not fully true: some functions sit behind Shift or repeated presses.

### Cited Findings
- **Lead 4 panel:**
  - Nord: 29 knobs and 20+ buttons. — [Lead 4 specs](https://www.nordkeyboards.com/legacy-products/nord-lead-4/specifications)
  - SOS: no screen beyond 3-character displays, and knobs jump to their value (no catch-up mode). Many parameters need Shift or multiple presses. The "dedicated knob for every parameter" claim is "untrue". Every panel control is mapped to a MIDI CC. — [SOS Lead 4](https://www.soundonsound.com/reviews/clavia-nord-lead-4)
- **Lead 3:** endless knobs with lit LED collars show each value. — [Wikipedia](https://en.wikipedia.org/wiki/Nord_Lead)
- **Lead A1:** Nord describes a "simple yet sophisticated front panel interface", designed at the request of professional players who need to make sounds quickly. — [Lead A1 page](https://www.nordkeyboards.com/products/nord-lead-a1/)
- **Wave 2:** two OLED screens (Program and Oscillator) replace the Lead series' 2-digit LEDs. Each oscillator category uses 3 encoders and a waveform display. — [SOS Wave 2](https://www.soundonsound.com/node/4922243); [Nord Wave 2 page](https://nordkeyboards.com/products/nord-wave-2)
- **Modular G2 editor:** drag-and-drop modules, colour coding, multi-level undo, "fast replace" that keeps connections, and Instant Hint views showing Morph and CC assignments. — [NMG2 brochure](https://c3.zzounds.com/media/NMG2-brochure-ae35adcccfe9a57feb7bb22957416ada.pdf)

### Inferences
- **For a VST:**
  - Keep one fixed per-layer "panel" layout identical across layers, so muscle memory transfers. Switch which layer the panel shows with layer-select tabs, as Nord does with one panel and slot buttons.
  - Avoid deep menus, and show morph and macro assignment status inline.
- The Lead 4 contrast (bare UI, SOS criticism) against the Wave 2 (OLED, praised) suggests minimal UI works when it is backed by good visual feedback (waveform displays, LED indicators).

### Gaps
- I did not retrieve Gearspace or Nord user-forum opinion on the UI. The views above come from SOS and Nord only.

## Lessons and pitfalls for a 4-8 layer architecture

### Takeaway
Nord's experience suggests:
- use a shared voice and CPU pool with cheap unison
- use Group/Link editing to avoid editing N layers one by one
- have per-layer FX, but be mindful of total FX count (16 on the Wave 2 at 4 layers)
- control the mix of all layers on one fader screen
- have morph sources that reach every layer

Known Nord weaknesses to improve on: no seamless program switching, no stereo spread, phase randomisation, single stereo bus, limited modulation.

### Cited Findings
- **Polyphony is shared:** Lead 4 has 20 voices for 4 slots; A1 26; Wave 2 48; Stage 4 synth 46 for 3 layers; G2 up to 32 "depending on Patch complexity". — [Lead 4 specs](https://www.nordkeyboards.com/legacy-products/nord-lead-4/specifications); [A1 specs](https://www.nordkeyboards.com/products/nord-lead-a1/specifications/); [Wave 2](https://nordkeyboards.com/products/nord-wave-2); [Stage 4 specs](https://www.nordkeyboards.com/products/nord-stage-4/specifications/); [G2](https://www.nordkeyboards.com/node/511)
- **Unison:** Wave 2 3-way unison does not reduce polyphony. — [SOS Wave 2](https://www.soundonsound.com/node/4922243)
- **Per-layer FX:** 4 layers × 4 effect blocks gives up to 16 active effects on the Wave 2. — [SOS Wave 2 p.2](https://www.soundonsound.com/node/4922243?page=2)
- **Group editing:** Wave 2 Group mode shares Filter, Env, Arp, LFO and FX across 2-4 layers. — [Nord Wave 2](https://nordkeyboards.com/products/nord-wave-2)
- **Mixing:** a Layer fader section controls volume or pan for all 4 parts at once. — [Nord Wave 2](https://nordkeyboards.com/products/nord-wave-2)
- **Pitfalls named by SOS:** layers mix only to one stereo out; program changes cut sound; one mono LFO per layer; no mod matrix; oscillator phase randomisation; no stereo spread. — [SOS Wave 2 p.1](https://www.soundonsound.com/node/4922243); [p.2](https://www.soundonsound.com/node/4922243?page=2)
- **Expansion:** the Modular G2X doubles processing power over the G2 via a voice expansion board. Nord ties voice count to processing headroom. — [NMG2 brochure](https://c3.zzounds.com/media/NMG2-brochure-ae35adcccfe9a57feb7bb22957416ada.pdf)

### Inferences
- **CPU:** at 8 layers × unison × per-layer FX, cost multiplies. Ways to manage it:
  - shared (grouped) sections and FX buses
  - per-layer quality or bypass
  - a visible voice/CPU meter, as G2-style patch-complexity polyphony implies
- **Gain staging:** Nord gives per-layer level faders but no published auto-gain. A VST stacking 4-8 layers should add per-layer trim, automatic headroom (e.g. -3 dB per doubling of active layers, or normalisation) and a master limiter. This is my own design inference; no Nord source documents gain compensation.
- **Editing many layers:** combine Nord's Group mode (shared sections), Multi-Focus (edit all selected layers at once), copy-layer, and morph sources that span layers. Add relative-offset editing so layers that differ keep their differences when edited together.
- **Arp per layer** (Wave 2) suits EDM: different rhythmic layers within one base sound.

### Gaps
- I found no Nord documentation on gain compensation, voice-stealing priority between layers, or how DSP is allocated between layers.
- Gearspace and Nord user-forum discussions of layer-stacking pitfalls were not retrieved within the research budget.
