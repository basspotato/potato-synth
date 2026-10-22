# Roland Jupiter-8 (1981) and Jupiter-6 (1983): Signal Path, Character, and How to Model and Multiply the Sound

## Jupiter-8 architecture (voices, VCOs, filter, VCA, envelopes, LFO, key modes, arpeggiator)

### Takeaway
The JP-8 is an 8-voice, bitimbral analog poly with 2 VCOs per voice. Each voice runs into a non-resonant HPF, then a resonant IR3109 OTA low-pass filter switchable between 12 and 24 dB/oct, then a BA662 VCA. It has two ADSR envelopes from IR3R01 chips (ENV-1 is invertible and can be routed to pitch, PWM and filter; ENV-2 is hardwired to the VCA), one LFO, and the Solo/Unison/Poly-1/Poly-2 assign modes. Split and dual layering are available, plus an arpeggiator. Its sound comes from simple ingredients: two detuned VCOs, cross-mod and sync, a clean OTA filter, fast envelopes, and the option to stack all 16 oscillators on one note.

### Cited Findings
- 8 voices, 2 timbres, 2 VCOs per voice, with PWM, cross-mod and oscillator sync. Non-resonant HPF; resonant LPF switchable 12/24 dB/oct, built on the Roland IR3109. VCA based on the BA662. Envelopes generated in hardware by the IR3R01 chip, 1 ms attack. Two ADSRs, one invertible — [Wikipedia: Roland Jupiter-8](https://en.wikipedia.org/wiki/Roland_Jupiter-8)
- Polyphonic unison: all 16 oscillators can stack on one note and "divide down" as more keys are pressed. Also split (Upper/Lower patches), a dual/"double four-voice" layer, adjustable polyphonic portamento, hold, and an assignable bender (pitch or filter) — [Wikipedia](https://en.wikipedia.org/wiki/Roland_Jupiter-8)
- Production 1981–1985, about 3,300 units. Units before serial 171700 had a 12-bit DAC "linked to tuning instability"; from 171700 a 14-bit DAC; from 282880 a standard DCB port (often called the JP-8A). 64 patch memories, 61 keys, balanced XLR plus 1/4" outputs — [Wikipedia](https://en.wikipedia.org/wiki/Roland_Jupiter-8)
- Official Roland spec sheet:
  - VCO-1: 16'/8'/4'/2' range, cross mod.
  - VCO-2: 16'–2', Normal/Low Frequency switch, fine tune up to ±50 cents, sync on/off.
  - Pitch mod: LFO/ENV to VCO-1, VCO-2 or both. PWM by LFO, manual or ENV-1.
  - Mixer: VCO-1/VCO-2 balance.
  - VCF: cutoff, resonance, slope −12/−24 dB/oct, env mod (ENV-1 or ENV-2), LFO mod, key follow 0–120%. Separate HPF.
  - VCA: level and LFO mod in 4 steps (0–3).
  - ENV-1: A 1 ms–5 s, D 1 ms–10 s, S 0–100%, R 1 ms–10 s, key follow, normal/inverse polarity. ENV-2: same ranges, no polarity switch.
  - LFO: 0.05–40 Hz, delay 0–4 s.
  - Arpeggiator: 1–20 Hz, 1–4 octaves, Up/Down/Up&Down/Random.
  - Assign modes: Solo, Unison, Poly-1, Poly-2.

  — [Roland Support: Jupiter-8 Technical Specifications](https://support.roland.com/hc/en-us/articles/201945109-Jupiter-8-Technical-Specifications)
- Waveforms: VCO-1 triangle, sawtooth, pulse, square. VCO-2 has noise in place of square. Envelope times are given as A 1.5 ms–6 s and D/R 1.5 ms–12 s (slightly different from Roland's sheet). ENV-2 is hardwired to the VCA. IR3109 filter, IR3R01 envelopes, BA662 VCA — [Polynominal: Roland Jupiter 8](https://www.polynominal.com/roland-jupiter8/). The Roland Cloud plug-in manual instead lists VCO-2 shapes as including sine. The common understanding is that VCO-2 = sine/saw/pulse/noise, but the sources conflict on whether VCO-2 has triangle or sine — [Roland Cloud JUPITER-8 manual](https://www.rolandcloud.com/getmedia/3d552dc5-46ca-415b-b121-a6c2474e28a4/JUPITER-8-Manual-E.pdf?ext=.pdf)
- Cross mod: VCO-2's waveform modulates VCO-1's frequency. Sync hard-syncs VCO-2 to VCO-1. LOW FREQ turns VCO-2 into an LFO, usable as an extra modulator through cross mod. ENV-1 key follow makes decay and release longer on low notes and shorter on high notes — [Roland Cloud JUPITER-8 manual](https://www.rolandcloud.com/getmedia/3d552dc5-46ca-415b-b121-a6c2474e28a4/JUPITER-8-Manual-E.pdf?ext=.pdf)
- Poly-1 vs Poly-2 (as implemented by Arturia, following the hardware): Poly-1 allocates voices one by one and envelopes "restart from their current value". In Poly-2 a new voice cuts off the previous voice's release — [Arturia Jup-8 V4 FAQ](https://support.arturia.com/hc/en-us/articles/4405740988690-Jup-8-V4-General-Questions)
- Sound On Sound retrospective: the JP-8 is "clear and transparent", doesn't impose its own character, and blends in a mix. Prophets and Oberheims are described as heavier and more dominant. JP-8 voices did not vary between instruments the way Oberheim and Prophet voices could. The JP-8A improved tuning stability — [Sound On Sound: Roland Jupiter retrospective](https://www.soundonsound.com/reviews/roland-jupiter)
- Stacked unison is described as "hyper fat" — [Polynominal](https://www.polynominal.com/roland-jupiter8/)

### Inferences
- The "big" JP-8 sound comes mainly from multiplication rather than from heavy per-voice colouring: 2 VCOs detuned by a few cents, times up to 8 voices in unison (16 oscillators), each voice with slightly different analog tuning. SOS's "clean/transparent" description suggests the base voice should be relatively clean (gentle OTA saturation, not aggressive drive). Width and weight then come from stacking.
- A modern "base engine" should copy the JP-8 modulation topology: ENV-1 bipolar to pitch, PWM and filter; ENV-2 to the VCA; VCO-2 able to act as an LFO; cross-mod from VCO-2 to VCO-1; sync of VCO-2 to VCO-1; a HPF before the LPF; key-follow on envelope times. That topology is the set of "Jupiter recipes".
- Pre-171700 tuning instability (12-bit DAC) suggests part of the "vintage drift" heard on early records is quantised/unstable control-voltage tuning, not only VCO thermal drift. That gives a possible "early unit" drift mode.

### Gaps
- No source found giving the JP-8 VCO circuit type (discrete vs chip) or measured drift figures. A service manual would be needed.
- No measured unison detune spread for the hardware. The hardware relies on natural tuning offsets; I found no documented dedicated unison detune knob.
- No measured envelope curve shapes (exponential time constants) from the hardware.

## Jupiter-6 differences

### Takeaway
The JP-6 is a cheaper 6-voice, 12-VCO bitimbral sibling. It uses CEM3340 VCOs, CEM3360 VCAs, and an IR3109-based filter that is a true multimode (LP/BP/HP). Its waveforms can be mixed within each oscillator, and it had early MIDI. Its high-resonance character is harsher and more "ringing" than the JP-8's, and it is generally heard as having a more fixed, recognisable sound.

### Cited Findings
- 6 voices, 2 VCOs per voice (12 total), bitimbral, 61 keys, no effects, no velocity/aftertouch, 1983–1985, US$2995. Waveforms: saw, triangle, variable pulse, square, noise. "Unusually, the JP-6 allows simultaneous selection of any or all of the waveforms in each of its two oscillator banks." Sync and cross-mod. One resonant multimode filter (LP/BP/HP). 2 ADSRs, 2 LFOs (one sine, one programmable tri/saw/square/random). Split 4/2 and 2/4, Whole, and Unison (all 12 oscillators on one key, dividing down polyphonically). MIDI, shown at Winter NAMM January 1983. 48 tones / 32 patches — [Wikipedia: Roland Jupiter-6](https://en.wikipedia.org/wiki/Roland_Jupiter-6)
- Chips: CEM3340 VCOs, CEM3360 VCAs, IR3109 filters — [SoundProgramming: Roland Jupiter-6](https://soundprogramming.net/synthesizers/roland/roland-jupiter-6)
- The IR3109 appears in the Jupiter-6, Jupiter-8, Juno-6/60, JX-3P, MC-202, SH-101, MKS-30 and MKS-80. It contains "four independent buffered OTAs which can be interconnected to provide a variety of filter responses", with a single exponential frequency-control input. Clone: Alfa AS3109 — [SDIY wiki: IR3109](https://sdiy.info/wiki/IR3109)
- The JP-6 filter is an IR3109 OTA multimode design, "distinctly different from other Roland designs of the era". Its high-resonance behaviour has "jarring, ringing harmonics" compared with a modern clone, the System80 Jove, which was "much sweeter". The Jove's 2-pole LP mode is "somewhat reminiscent of the Jupiter 8's". BP is 12 dB and HP "sizzles and slices" — [Sound On Sound: System80 Jove review](https://www.soundonsound.com/reviews/system-80-jove)
- SOS retrospective: the JP-6 adds HP and BP filter modes. It lacks chorus, velocity and aftertouch, has fewer programming options and a single output, and its arpeggiator lost Random. However it was programmed, it "always sounded like... a JP6" — [Sound On Sound](https://www.soundonsound.com/reviews/roland-jupiter)

### Inferences
- For a plugin: the JP-6's waveform-mixing oscillators (saw+pulse+tri summed per VCO) and its multimode IR3109 filter (BP and HP from the same core) are features worth borrowing for an EDM engine. Waveform summing gives a thicker single oscillator before any unison is applied.
- Because the JP-6 uses CEM3340 VCOs and the JP-8 does not (per the chip lists above), voice-to-voice oscillator behaviour probably differs between the two. Offering a "JP-6/JP-8 oscillator flavour" switch is reasonable, though no source measured the difference.

### Gaps
- No source found describing JP-6 envelope chip details or envelope time ranges.
- No authoritative measurement comparing JP-6 and JP-8 filter responses.

## Circuit-level character (drift, filter saturation, envelopes, tolerances, output stage/chorus)

### Takeaway
Documented character traits:
- A clean, transparent OTA (IR3109) low-pass with a 2-pole or 4-pole response.
- Fast hardware envelopes (about 1–1.5 ms minimum attack).
- Tuning instability on early units.
- No onboard chorus on either Jupiter. Modern Roland sound design often adds Juno/CE-1 chorus as an optional extra.

Component-tolerance statistics are not documented in the sources found. Emulators approximate them with per-voice "dispersion" parameters.

### Cited Findings
- Envelopes are made in hardware by the IR3R01, with a 1 ms attack — [Wikipedia](https://en.wikipedia.org/wiki/Roland_Jupiter-8). Roland's spec sheet gives A/D/R minimum 1 ms ([Roland Support](https://support.roland.com/hc/en-us/articles/201945109-Jupiter-8-Technical-Specifications)); Polynominal gives 1.5 ms ([Polynominal](https://www.polynominal.com/roland-jupiter8/)).
- TAL notes that the hardware's fastest attack on a "cold" unit is faster than its earlier emulation, and adjusted for it. TAL also notes slight attack lag with VCO modulation and adjusted the sawtooth for very low notes. It uses a small default VCO1/VCO2 detune so the oscillators never phase-lock statically — [TAL-J-8 product page](https://www.tal-software.com/products/tal-j-8)
- Early JP-8s (12-bit DAC, before serial 171700) had tuning instability — [Wikipedia](https://en.wikipedia.org/wiki/Roland_Jupiter-8). The JP-8A improved tuning stability — [Sound On Sound](https://www.soundonsound.com/reviews/roland-jupiter)
- Neither the JP-8 nor the JP-6 has onboard chorus or effects; the JP-6 has a single output — [Wikipedia JP-6](https://en.wikipedia.org/wiki/Roland_Jupiter-6); [Sound On Sound](https://www.soundonsound.com/reviews/roland-jupiter). Roland's own JP-8 model-expansion sound design adds JUNO-106 chorus or CE-1 chorus "for warmth and thickness" — [Roland Articles: JUPITER-8 Model Expansion Poly Presets](https://articles.roland.com/sound-design-jupiter-8-model-expansion-poly-presets/)
- Arturia's "Voice Dispersion" models voice-to-voice tolerance drift and aging. Custom trimmers cover Pitch, PW, ENV, Cutoff, Resonance and MOD — which suggests these are the parameters that vary per voice on real units — [Arturia Jup-8 V4 FAQ](https://support.arturia.com/hc/en-us/articles/4405740988690-Jup-8-V4-General-Questions)
- The Roland Cloud plug-in has an "AGING" knob to simulate age-related change — [Roland Cloud JUPITER-8 manual](https://www.rolandcloud.com/getmedia/3d552dc5-46ca-415b-b121-a6c2474e28a4/JUPITER-8-Manual-E.pdf?ext=.pdf). Roland's ACB JP-8 for FANTOM EX adds "Circuit Mod" and "Condition" functions that change the vintage character — [Roland: JUPITER-8 ACB Expansion](https://www.roland.com/de/products/rc_jupiter-8_acb_expansion/)

### Inferences
- The per-voice variation vector to randomise, following Arturia's list: VCO pitch offset, pulse width offset, envelope time scaling, cutoff offset, resonance offset, modulation depth scaling. Draw each from a fixed per-voice seed (static tolerance), then add a slow random walk on pitch (thermal drift). The static part makes voices distinct; the moving part makes the stack "breathe".
- Clean output plus no built-in chorus means width is not part of the original voice. A stereo chorus, unison pan spread and Haas widening are added multipliers, not part of the base engine.

### Gaps
- No primary measurements found for the IR3109's saturation curve, resonance-vs-cutoff compensation, or the real spread of VCO/filter tolerances across voices. A service manual or circuit analysis (e.g. Electric Druid, Polynominal IR3109 page, not fetched) would be needed.
- Envelope curve shapes (RC-exponential attack vs linear) are not documented in the fetched sources.

## Modern emulations and the "multipliers" they add

### Takeaway
All the main emulations add the same set of extras on top of a faithful JP-8 voice:
- per-voice dispersion/drift
- a unison detune amount
- unison pan spread / stereo path
- selectable unison voice counts
- filter overdrive / extreme resonance
- aging/condition controls
- built-in chorus, delay and reverb
- extra modulators, sequencers and MPE

These are the "multipliers" a new EDM synth should stack on a Jupiter-style base.

### Cited Findings
- **Arturia Jup-8 V4:**
  - Unison Classic splits the 8 voices across the held keys. Unison Mono assigns 2–8 voices per note.
  - Unison Detune knob. Pan Spread: first note centred, then voices alternate L/R.
  - Voice Dispersion presets (subtle/fair/strong) plus Custom trimmers for Pitch/PW/ENV/Cutoff/Res/MOD.
  - Selectable sync master/slave. Arpeggiator with 7 modes.
  - Advanced panel: LFO2/3 with cycle/single modes and a Warp saw shape, a Modulation Mixer (multiply/lag), and a 32-step note/mod sequencer.

  — [Arturia Jup-8 V4 FAQ](https://support.arturia.com/hc/en-us/articles/4405740988690-Jup-8-V4-General-Questions)
- **TAL-J-8:**
  - Calibrated against TAL's own hardware unit.
  - "Service Control" dialog with per-voice VCO1/VCO2 detune and per-voice panorama. "VCO instability" (added in v1.2.4).
  - X-Mod calibration, including a digital cross-mod that stays in tune.
  - Extreme resonance and filter overdrive in calibration. VCO-based unison with stereo spread.
  - Round-robin voice mode, hardware SOLO and POLY II modes, full stereo path with multi-out layers.
  - MPE, MTS microtuning, synced delay.

  — [TAL-J-8 product page](https://www.tal-software.com/products/tal-j-8)
- **Roland Cloud JUPITER-8 (plug-in):** polyphony 2–8 voices, a low-CPU option, an AGING knob, effects (effect type/tone/depth, delay, reverb), Mono/Uni/Poly-1/Poly-2 modes, tempo-synced LFO — [Roland Cloud JUPITER-8 manual](https://www.rolandcloud.com/getmedia/3d552dc5-46ca-415b-b121-a6c2474e28a4/JUPITER-8-Manual-E.pdf?ext=.pdf)
- **Roland ACB (Analog Circuit Behavior):** models "analog circuits and their interactions at the component level". It is computationally intensive. It is the same technology as in the TR-8S, SYSTEM-8 and the Legendary series. The JP-8 ACB for FANTOM EX adds Circuit Mod and Condition controls — [Roland: JUPITER-8 ACB Expansion](https://www.roland.com/de/products/rc_jupiter-8_acb_expansion/)

### Inferences
- A layered multiplier chain that matches what these products expose:
  1. A per-voice tolerance seed (dispersion).
  2. A slow drift random walk (instability).
  3. Unison count, from 2 up to N, with a detune amount and a detune distribution.
  4. Pan spread with alternating L/R.
  5. A dual/split layer of two complete patches (the original's Upper/Lower).
  6. Filter overdrive / pre-filter saturation.
  7. An optional chorus (Juno/CE-1 style).
  8. Delay and reverb.

  Going beyond 8 voices of unison per note goes past what the hardware could do. That suits EDM supersaws, but loses strict authenticity.
- TAL's "digital cross-mod that stays in tune" points to a known practical issue: analog-style exponential FM shifts the pitch centre. An in-tune option (linear/through-zero FM or DC-compensated FM) is a useful EDM-friendly variant.

### Gaps
- Not researched within budget: Roland JD-08 / Boutique JP-08, Jupiter-X/Xm ("JUPITER-8 model" in ZEN-Core), and Behringer UB-Xa. No concrete parameter lists for them are in these notes.
- Arturia's filter modelling method is not documented in the FAQ.

## Known patch recipes (brass, pads, leads, plucks)

### Takeaway
Roland's own JP-8 model-expansion recipes all start from two sawtooths (or pulses) with VCO-2 fine-tuned about 6–10 units off. They add an ENV-1 pitch blip for attack, ENV-2 shaping both filter and amp, and moderate cutoff, with chorus as an optional final stage.

### Cited Findings
All recipes below are from [Roland Articles: Sound Design — JUPITER-8 Model Expansion Poly Presets](https://articles.roland.com/sound-design-jupiter-8-model-expansion-poly-presets/). Values are on Roland's ZEN-Core 0–1023-style scale, approximate as published.
- **Smooth Pad:** both saws, mixer about 200 each, VCO-2 Fine ±7. ENV-2 attack about 400, sustain max, release 500–550. Cutoff 600–700, env mod about 70 from ENV-2, some resonance, HPF to thin the lows. Chorus optional (JUNO-106).
- **Brass Stab:** both saws, Fine ±7. ENV-1 instant attack, decay about 200, no sustain or release, inverted polarity, routed via the VCO Modulator at about 10 to VCO-1 (a pitch blip). Cutoff about 350 with ENV-2 filter mod at 700–750. ENV-2 attack about 180, decay 0, sustain max, release about 180. 2-tap pan delay at 100–150 ms.
- **Analog Strings:** VCO-1 at 4′, VCO-2 +12 semitones with Fine +6, VCO-2 pulse with manual PW about 110. ENV-2 attack about 450, release about 500. Cutoff 800–900, HPF about 300. CE-1 chorus.
- **Hollow Pad:** both pulse/square, Fine 10. ENV-1 pitch decay 230–250 to VCO-1 only at depth 20–25. ENV-2 attack about 400, release 500–550. Cutoff about 750. Optional LFO PWM at about 100–110. Hexa-Chorus.
- **Power Pluck:** both saws, Fine 8. ENV-2 attack 0, sustain 0, decay and release 500–600. Cutoff about 750 with ENV-1 env mod 230–250, ENV-1 decay and release about 470.

### Inferences
- An EDM "Jupiter unison lead" follows from these settings combined with the hardware Unison mode ([Wikipedia](https://en.wikipedia.org/wiki/Roland_Jupiter-8)): two saws with a small detune and unison of all voices on one note. Brass also uses the "inverted ENV-1 pitch blip into VCO-1" trick — characteristic Jupiter attack articulation worth keeping as a preset macro.

### Gaps
- No primary source found with documented settings for famous records (e.g. "Jump"-style brass is on an Oberheim, not a Jupiter; specific Duran Duran or Pet Shop Boys patches were not found). Any such recipes would be speculation.

## DSP modelling approaches (VA oscillators with drift, OTA filter models, per-voice randomisation)

### Takeaway
The standard toolkit:
- **Filter:** a cascade of four one-pole stages with tanh-type nonlinearities and global resonance feedback. Huovilainen's 2004 nonlinear ladder (Euler-discretised, tanh per stage, 2x oversampling, half-sample delay in the feedback) is the classic reference. Zavalishin's TPT/zero-delay-feedback framework gives better tuning and stability. An IR3109 has the same "4 cascaded one-pole stages plus feedback" topology but uses OTAs, and the 12/24 dB modes correspond to tapping stage 2 vs stage 4.
- **Per-voice drift:** a static random offset per voice plus a slow random walk.

### Cited Findings
- Huovilainen, "Non-Linear Digital Implementation of the Moog Ladder Filter," DAFx-04 (Naples, 2004):
  - Each stage is a first-order section derived from the differential-pair tanh relation, discretised with Euler's method. It is "equivalent to a cascade of first order IIR sections with embedded non-linearities". About 5 tanh evaluations per sample, with values reused across samples.
  - 2x oversampling is required for aliasing and the nonlinearity.
  - A half-sample delay (averaging two samples) in the resonance feedback keeps the phase near 180° at cutoff up to about Fs/4, with tuning error under 10%.
  - One tuning table can handle cutoff and resonance-gain compensation.
  - It models the transistor ladder specifically, not OTA circuits.

  — [DAFx-04 paper P_061](https://dafx.de/paper-archive/2004/P_061.PDF)
- Zavalishin, *The Art of VA Filter Design* (v2.1.0, Native Instruments): digital models of analog filters using the topology-preserving transform (a generalisation of bilinear transform, zero-delay feedback and trapezoidal integration). Chapters cover ladder filters (analog model, feedback/resonance, digital model, feedback shaping, multimode ladder, 8-pole, diode ladder) and nonlinear filters (saturators, feedback-loop saturation, nonlinear ZDF equation, multinonlinear feedback, antisaturators) — [Zavalishin, VAFilterDesign 2.1.0 PDF](https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.1.0.pdf). (Only the table of contents and front matter were reviewed; chapter contents were not read.)
- The IR3109 is four buffered OTAs with one exponential control input, interconnectable into different responses — [SDIY wiki](https://sdiy.info/wiki/IR3109). The JP-8 LPF is switchable 12/24 dB — [Wikipedia](https://en.wikipedia.org/wiki/Roland_Jupiter-8). The JP-6 adds BP/HP from the same chip — [Sound On Sound Jove review](https://www.soundonsound.com/reviews/system-80-jove)
- Commercial per-voice randomisation targets: Pitch, PW, ENV, Cutoff, Res, MOD ([Arturia](https://support.arturia.com/hc/en-us/articles/4405740988690-Jup-8-V4-General-Questions)). Per-voice VCO1/VCO2 detune, per-voice pan, and VCO instability ([TAL](https://www.tal-software.com/products/tal-j-8)).

### Inferences
- **Recommended base-engine DSP:**
  - **Oscillators:** PolyBLEP/BLEP saw, pulse and triangle oscillators. Allow JP-6-style summing of several waveforms per VCO.
  - **Filter:** a 4-stage OTA cascade in TPT/ZDF form with mild tanh per stage (OTA input is a differential pair, so tanh is a reasonable first approximation). Tap stage 2 for 12 dB and stage 4 for 24 dB. Compute BP/HP via the multimode mixing combinations (Zavalishin ch. 5 multimode ladder). Run at 2x or more oversampling.
  - **Envelopes:** RC-style exponential segments with a minimum of about 1–1.5 ms.
- **Suggested drift model (an engineering suggestion, not from a source):**
  - Per-voice static offsets drawn from a Gaussian, e.g. pitch σ of a few cents, cutoff σ of a few percent.
  - Plus an Ornstein-Uhlenbeck or filtered-noise slow walk on pitch, below 1 Hz.
  - Scale both with one "Dispersion/Age" macro, like Arturia's subtle/fair/strong.
  - Seed per voice index, so a given voice keeps its personality across notes, as hardware does.

### Gaps
- No DAFx paper found that is specific to the IR3109/OTA cascade, or to Jupiter oscillator drift statistics.
- Huovilainen's and Zavalishin's methods are ladder-generic. How well they fit the IR3109 is inferred, not demonstrated.
