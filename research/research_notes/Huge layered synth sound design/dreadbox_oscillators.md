# Dreadbox Oscillators: Character and DSP Modelling for a "Dirty/Analog" Oscillator Layer

Scope: Dreadbox (Greek analog synth maker) only. JP-8080 clean oscillators and Jupiter-6/8 are covered in sibling notes files.

## 1. Product line and oscillator specs

### Takeaway
Dreadbox designs are mostly simple: one or two (sometimes three) VCOs, a small set of waveforms, often morphable, plus sub-octaves. Most also have hard sync, some linear/exponential FM, and auto-tune on recent models. The Erebus V3 has a "Triple Ring" switching modulator, Medusa runs three analog and three digital oscillators side by side, and the Murmux v2 has a 6-saw "hyper VCO". I found no public source naming the VCO core topology or chip (AS3340, discrete saw core or triangle core) for any model.

### Cited Findings
**Erebus (v1, 2022 reissue)**
- 2 VCOs, two waveforms each. Optional paraphonic mode, hard sync, independent glide per VCO. 2-pole low-pass filter whose character is attributed partly to the OTA VCA. Built-in lo-fi delay. 18 patch points, rackable at 42hp — [Perfect Circuit, Erebus 2022 reissue](https://www.perfectcircuit.com/dreadbox-erebus-2022-reissue.html)
- Commenters called it "paraphonic" and "duophonic". User comments describe "analog gooeyness" and say "it just sounds huge" — [Synthtopia Erebus review (2014)](https://synthtopia.com/content/2014/12/27/dreadbox-erebus-synth-module-review)

**Erebus V3**
- 3 VCOs. OSC1 has square and saw, OSC2 has saw and triangle, and both tune ±11 semitones. OSC3 is multiwave (saw up/down, square, triangle, sine, noise) and can drop to LFO range. The synth is duophonic, with first-note priority applied to OSC3 — [Sonic State Sonic LAB Erebus V3](https://sonicstate.com/news/2019/01/11/sonic-lab-dreadbox-erebus-v3-review/)
- "Triple Ring" is not a true ring modulator. It is a clocked switch that alternates OSC1 triangle and OSC3 sine, clocked by OSC1's pulse. The reviewer says it "sorta sounds like a Ring mod" — [Sonic State](https://sonicstate.com/news/2019/01/11/sonic-lab-dreadbox-erebus-v3-review/). MusicRadar describes it as switching between "Osc 2's triangle and a sine from the Osc 3 section", giving audio-rate AM. The two reviews disagree on which oscillator's triangle is switched — [MusicRadar Erebus V3 review](https://www.musicradar.com/reviews/dreadbox-erebus-v3)
- MusicRadar says Osc 3 has six waveforms, including a "supersaw wave with pulse-width control", and tunes down to a cycle of nearly two minutes. Its FM knob sends Osc 3 to Osc 1 and 2. The specs list auto-tuning. Duophonic mode is set by a rear DIP switch: Osc 1 and 3 play the lowest note, Osc 2 the highest. There is one VCF and one VCA — [MusicRadar](https://www.musicradar.com/reviews/dreadbox-erebus-v3). Sonic State does not mention a supersaw, which may be a conflict or an omission.
- Filter: 2-pole (12 dB/oct) multimode that sweeps continuously from LP through a notch to HP, with no BP. It self-oscillates. A "resonant bump is always present, even when the Res knob is set to zero" — [MusicRadar](https://www.musicradar.com/reviews/dreadbox-erebus-v3). Its resonance "seems to enhance the low end rather than remove it" — [Sonic State](https://sonicstate.com/news/2019/01/11/sonic-lab-dreadbox-erebus-v3-review/)
- The V3 echo "sounds a little less grungey than the original Erebus" — [Sonic State](https://sonicstate.com/news/2019/01/11/sonic-lab-dreadbox-erebus-v3-review/)

**Nyx v2**
- 2 syncable VCOs, switchable between saw+pulse (position 1) and saw+triangle (position 2). Tuning ±12 semitones, octave switch covering three octaves, white noise. Auto-tune uses the octave switch at its midpoint — [MusicTech Nyx 2 review](https://www.musictech.net/reviews/hardware-instruments/dreadbox-nyx-2/)
- Synth Anatomy gives OSC1 as saw/pulse and OSC2 as saw/triangle, with sync, individual glide and a mixer. DIP switch 2 enables paraphonic mode and DIP switch 1 enables auto-tune — [Synth Anatomy Nyx 2 review](https://synthanatomy.com/2019/06/dreadbox-nyx-2-review-analog-synth.html)
- Dual filter, each 12 dB, 24 dB combined. Modes LP, BP, HP and parallel (both LP), with an Offset knob setting the cutoff spread. Routings Norm, Half, Split and VCA. Split sends one VCO to each filter — [MusicTech](https://www.musictech.net/reviews/hardware-instruments/dreadbox-nyx-2/); [Synth Anatomy](https://synthanatomy.com/2019/06/dreadbox-nyx-2-review-analog-synth.html)
- Neither review mentions FM, ring mod or a "Vortex" feature, so I could not verify those for Nyx.

**Typhon (2021)**
- 2 analog VCOs with analog VCAs. The analog filter is a 24 dB/oct resonant low-pass. Two envelopes. Three modulators route to 13 destinations, including waveform. 32-step sequencer, 254 presets — [Kraft Music Typhon](https://kraftmusic.com/products/dreadbox-typhon-desktop-monophonic-analog-synthesizer)
- The main wave control "sweeps through combinations of wave shapes delivered by the oscillators", and the change is "continuous and satisfying". FM and pulse width are reached through the wave control knob — [MusicTech Big Review: Typhon](https://musictech.com/reviews/hardware-instruments/the-big-review-dreadbox-typhon)
- Sinevibes DSP runs at 32-bit/96 kHz stereo with three FX slots. FX1 has six distortion, bitcrusher and FM-style algorithms. FX2 is chorus, ensemble and flanger. FX3 is reverb and two delays — [Kraft Music](https://kraftmusic.com/products/dreadbox-typhon-desktop-monophonic-analog-synthesizer). The FX run in parallel, so there is "no 'digital taint'" unless you add it on purpose — [MusicTech](https://musictech.com/reviews/hardware-instruments/the-big-review-dreadbox-typhon)
- I found no source describing a wavefolder on Typhon. Its digital colouring is distortion, bitcrush and "FM-style" algorithms.

**Hades (2015, 2022 reissue)**
- Marketed as a "Super fat oscillator": 1 VCO with 2 sub-octaves, 2 waveforms and pulse-width control. Warm 3-pole resonant LPF, internal bass boost circuit, OTA distortion plus drive "for aggressive sounds" — [Kraft Music Hades reissue](https://kraftmusic.com/products/dreadbox-hades-reissue-monophonic-analog-bass-synthesizer)
- Reissue: OTA distortion ("lovely distortion"), OTA VCA, AD/RS and AR envelopes, triangle LFO, 11 patch points. Aimed at bass with "rumble and girth" — [Perfect Circuit Hades reissue](https://perfectcircuit.com/dreadbox-hades-2022-reissue.html)

**Abyss (4-voice, 2017)**
- 1 VCO per voice plus a square sub-oscillator. A continuous waveshape knob morphs square, square-saw, pulse, saw, then noise, and a dedicated LFO modulates the waveshaper. The detune control spreads the four voices and can be LFO-modulated. Auto-tune. Modes Poly, Unison, Chord and Multi-channel — [MusicRadar Abyss review](https://musicradar.com/reviews/dreadbox-abyss)
- 24 dB LPF per voice, self-oscillating. The Drive circuit in the amp section adds "body and mid-focused saturation", short of full distortion. FX are a BBD "Reflector" (≤50 ms), a 40–200 ms delay and a 4-stage OTA phaser. Pros list: "Excellent-sounding oscillators with powerful waveshaping tools", "The four-pole filter sounds stellar too" — [MusicRadar](https://musicradar.com/reviews/dreadbox-abyss)

**Nymphes (6-voice, 2021)**
- 1 VCO per voice, wave-variable across saw, square and triangle. Sub-oscillator, noise, detune via Shift. Self-tuning, with no manual tune parameter. 24 dB/oct LPF plus 6 dB/oct HPF — [Sound On Sound Nymphes review](https://www.soundonsound.com/reviews/dreadbox-nymphes)
- Shift turns the wave dial into a pulse-width control. The sub "introduces a lower octave to thicken and fatten sounds". The HPF is "Roland Juno-style" — [MusicTech Nymphes review](https://musictech.com/reviews/hardware-instruments/dreadbox-nymphes-review/)
- Voice modes include 6-osc unison, 4-osc unison, duophonic (3 oscillators per voice) and triphonic (2 per voice). Unison detune runs from "gooiness to cacophony" — [Gearnews Nymphes review](https://www.gearnews.com/dreadbox-nymphes-polyphonic-analogue-synthesizer-review); [Sonic State Nymphes](https://www.sonicstate.com/news/2022/01/06/sonic-lab-dreadbox-nymphes-6-analog-poly/)

**Murmux v2 (limited edition, 2016)**
- 3 standard VCOs plus 1 sub-oscillator, and a "hyper VCO" built from 6 sawtooth VCOs. White noise. Classic 4-pole Murmux VCF (HP, LP, notch) with 1V/oct tracking. OTA drive circuit, hybrid VC delay, S&H. "Designed for aggressive sounds but not limited to these." 50 units, hand-soldered through-hole — [Synth Anatomy Murmux v2](https://synthanatomy.com/2016/08/dreadbox-murmux-v2-full-analog.html)

**Medusa (with Polyend, 2018)**
- 6 oscillators: 3 analog (saw, square, triangle, sine, with a switchable 3-octave range and one shared pulse width) and 3 digital (the same four waves plus wavetable mode, sharing one of 20 wavetables, with per-oscillator position modulation). Exponential FM goes from osc 3 to oscs 1 and 2. Sync runs 1→2 and 2→3. Modes are mono (6 oscillators), 3-voice paraphonic (analog/digital pairs) and 6-voice. Filter options are −12 and −24 dB LP and −6 dB HP; it self-oscillates and accepts audio-rate FM from analog osc 3 — [MusicRadar Medusa review](https://www.musicradar.com/reviews/dreadbox-and-polyend-medusa)

### Inferences
- Across the line the oscillators are deliberately simple: saw, pulse, triangle, plus sub and noise. The character comes from the analog signal chain (OTA VCAs and drive, 2-, 3- and 4-pole filters with low-end-friendly resonance) more than from exotic oscillator features.
- The Abyss, Nymphes and Typhon morphing wave controls point to a single continuous "shape" parameter as the native Dreadbox control idiom, unlike the JP-8080's discrete waveform selection.
- Auto-tune on the V3, Nyx v2, Abyss and Nymphes implies the newer VCOs drift enough to need calibration. Expect pitch spread from voice to voice, but no gross detuning after calibration.

### Gaps
- VCO core topology (saw core or triangle core) and chips (AS3340/CEM or discrete) are not documented in any source I found. ModWiggler and teardown searches returned nothing Dreadbox-specific.
- Lil' Erebus (the Erica Synths collaboration) specs were not retrieved.
- I could not verify a Nyx "Vortex" feature or ring mod on Nyx.
- Erebus V3 Osc 3 supersaw (MusicRadar) is not mentioned by Sonic State, and the two reviews disagree on the Triple Ring source.

## 2. Sonic character: why "dark, gritty, warm, raw"

### Takeaway
Reviewers credit the Dreadbox sound to direct, unprocessed analog paths rather than clever oscillators. They point to resonance that keeps or boosts lows (Erebus), an always-present resonant bump, OTA VCAs and OTA drive stages (Hades, Murmux, Erebus), and mid-focused drive (Abyss). Digital FX on Typhon sit in parallel, so the analog core stays "clean-raw".

### Cited Findings
- Erebus V3 resonance "seems to enhance the low end rather than remove it" — [Sonic State](https://sonicstate.com/news/2019/01/11/sonic-lab-dreadbox-erebus-v3-review/)
- "a resonant bump is always present, even when the Res knob is set to zero" (Erebus V3) — [MusicRadar](https://www.musicradar.com/reviews/dreadbox-erebus-v3)
- The Erebus 2-pole LP gives the sound a "unique characteristic", with the OTA VCA contributing — [Perfect Circuit](https://www.perfectcircuit.com/dreadbox-erebus-2022-reissue.html)
- Nyx 2: "powerful raw sound with a lot of character"; filters "can sound creamy, gentle to distorted/brutal"; "It sounds for me like Moog Synthesizer but with a bit more bite in the sound" — [Synth Anatomy](https://synthanatomy.com/2019/06/dreadbox-nyx-2-review-analog-synth.html)
- Nyx 2: "a beast of a synth. A god, even." — [MusicTech](https://www.musictech.net/reviews/hardware-instruments/dreadbox-nyx-2/)
- Nymphes: "unashamedly naked", "classically smooth and brassy", "rawness, life and warmth", and a "monosynth" vibe — [Gearnews](https://www.gearnews.com/dreadbox-nymphes-polyphonic-analogue-synthesizer-review). Also "phat", "full of movement and dynamism", "some deliciously abrasive and ugly ones" — [Sound On Sound](https://www.soundonsound.com/reviews/dreadbox-nymphes). And "deep, broad and versatile", with "rasping basses" — [MusicTech](https://musictech.com/reviews/hardware-instruments/dreadbox-nymphes-review/)
- Nymphes filter is "very Roland like", in the Juno-106 ballpark but not a clone; there is a "uniquely Dreadbox oscillator thing" — [Sonic State](https://www.sonicstate.com/news/2022/01/06/sonic-lab-dreadbox-nymphes-6-analog-poly/)
- Medusa: "The analogue oscillators alone have the warm, characterful quality Dreadbox has become known for", while the wavetables "have plenty of character but lack weight and warmth" — [MusicRadar](https://www.musicradar.com/reviews/dreadbox-and-polyend-medusa)
- Hades: OTA distortion plus drive for aggressive sounds, an internal bass boost and a 3-pole LPF — [Kraft Music](https://kraftmusic.com/products/dreadbox-hades-reissue-monophonic-analog-bass-synthesizer)
- Abyss Drive: "body and mid-focused saturation" — [MusicRadar](https://musicradar.com/reviews/dreadbox-abyss)
- Typhon: "a solid dual-oscillator synth engine capable of beefy tones"; parallel FX mean "no 'digital taint'" — [MusicTech](https://musictech.com/reviews/hardware-instruments/the-big-review-dreadbox-typhon)

### Inferences
- The Medusa quote compares Dreadbox analog oscillators with digital oscillators in the same box. It is the most direct evidence that "weight and warmth" is what digital versions lose, which is the gap a "dirty" plugin layer should fill.
- Odd-pole and 2-pole filters (Hades 3-pole, Erebus 2-pole) roll off more gently than 24 dB ladders, so more upper harmonics survive. Combined with OTA soft-clipping, that likely explains "gritty but warm".
- Low-end-preserving resonance and bass boost are filter and VCA traits rather than oscillator traits. Modelling the oscillator alone will not deliver the Dreadbox sound.

### Gaps
- No source gave filter circuit topologies (ladder, OTA cascade, SVF) for any model, beyond the OTA VCA, drive and phaser notes.
- No measured spectra or scope captures of Dreadbox waveforms were found.

## 3. Reviews on oscillator behaviour (drift, shapes, sub weight)

### Takeaway
Reviews rarely address oscillator micro-behaviour directly. They praise the oscillators broadly ("excellent-sounding", "super fat", "high-quality") and the subs for "thickening". Drift is implied by the auto-tune routines rather than described.

### Cited Findings
- Abyss: "Excellent-sounding oscillators with powerful waveshaping tools" — [MusicRadar](https://musicradar.com/reviews/dreadbox-abyss)
- Nymphes: "high-quality oscillator"; self-tuning, no manual tune — [Sound On Sound](https://www.soundonsound.com/reviews/dreadbox-nymphes). 6-voice unison with detune is "massive!" — [Sonic State](https://www.sonicstate.com/news/2022/01/06/sonic-lab-dreadbox-nymphes-6-analog-poly/)
- Nymphes sub "introduces a lower octave to thicken and fatten sounds" — [MusicTech](https://musictech.com/reviews/hardware-instruments/dreadbox-nymphes-review/)
- Hades marketed as a "Super fat oscillator" with 2 sub-octaves — [Kraft Music](https://kraftmusic.com/products/dreadbox-hades-reissue-monophonic-analog-bass-synthesizer)
- Typhon wave morph is "continuous and satisfying" — [MusicTech](https://musictech.com/reviews/hardware-instruments/the-big-review-dreadbox-typhon)
- Nyx 2 has "something of that paraphonic nature of the sound coming through" — [MusicTech](https://www.musictech.net/reviews/hardware-instruments/dreadbox-nyx-2/)

### Inferences
- Paraphony (Erebus, Nyx, Murmux, Medusa) runs several oscillators through one filter and VCA, with envelopes retriggering on every note (MusicRadar on Erebus V3). That shared-filter intermodulation is part of the "raw" sound and could be offered as a paraphonic mode in the plugin.

### Gaps
- No reviewer quantified drift (cents or time scale), waveform asymmetry or sub level.

## 4. Dreadbox in bass-heavy, techno and EDM contexts

### Takeaway
Marketing and reviews position Hades, Murmux and Erebus for aggressive and bass sounds. Nymphes is noted for "rasping basses" and percussion. I found no direct techno-genre attribution.

### Cited Findings
- Hades aimed at bass with "rumble and girth" — [Perfect Circuit](https://perfectcircuit.com/dreadbox-hades-2022-reissue.html). Its OTA distortion and drive target "aggressive sounds" — [Kraft Music](https://kraftmusic.com/products/dreadbox-hades-reissue-monophonic-analog-bass-synthesizer)
- Murmux v2: "Designed for aggressive sounds" — [Synth Anatomy](https://synthanatomy.com/2016/08/dreadbox-murmux-v2-full-analog.html)
- Nymphes: "rasping basses, ethereal pads, spiky, wonky melodies" and "a host of options for electronic percussion" — [MusicTech](https://musictech.com/reviews/hardware-instruments/dreadbox-nymphes-review/)
- Erebus V3 resonance boosts low end, which helps resonant acid and techno bass — [Sonic State](https://sonicstate.com/news/2019/01/11/sonic-lab-dreadbox-erebus-v3-review/)

### Inferences
- For a layered EDM synth, the Dreadbox-style layer fits the sub and low-mid "body" role: a one-oscillator saw or pulse plus sub-octaves, into an OTA drive and a gentle filter. The clean JP-8080 layer then carries the supersaw width and top end.

### Gaps
- No artist or track citations tying Dreadbox to specific techno or EDM productions were found within budget.

## 5. DSP modelling ideas and existing emulations

### Takeaway
I found no official Dreadbox plugin or third-party emulation. KVR lists only a patch-mapper utility, and Dreadbox ships a hardware editor. Published virtual analog (VA) literature gives the building blocks: anti-aliased oscillators (DPW, PTR, polyBLEP), tanh-smoothed transitions, OTA nonlinear models and wavefolder models. A "Dreadbox-style" layer would combine these with drift and waveshape-imperfection models, shared paraphonic filtering and OTA drive.

### Cited Findings
- No emulation found. KVR lists a "Dreadbox Erebus V3 Patch Mapper" by Sunny Synths, a utility rather than a synth — [KVR](https://www.kvraudio.com/product/dreadbox-erebus-v3-patch-mapper-by-sunny-synths). Typhon presets are managed with the free "Dreadbox Software Editor" — [Kraft Music](https://kraftmusic.com/products/dreadbox-typhon-desktop-monophonic-analog-synthesizer)
- DPW sawtooth: square a modulo counter, then differentiate with H(z)=c(1−z⁻¹), c=fs/4f. Higher orders and the PTR method are more efficient. Pulses come from subtracting two saws. BLEP adds tabulated bandlimited-step residuals at each discontinuity, and polyBLEP replaces the table with integrated Lagrange polynomials — [Välimäki, Virtual Analog Modeling: Research at Aalto (2013 slides)](https://www.musica.ed.ac.uk/wp-content/uploads/Manual_Upload/Valimaki-Virtual-analog.pdf)
- Lazzarini & Timoney (2010, CMJ 34(1)) use tanh waveshaping to smooth square transitions, keeping aliasing around −60 dB. A ModFM-style parameter *m* blends even and odd harmonics to mimic analog shape controls, and DC blocking is needed after integration — [Lazzarini & Timoney, New Perspectives on Distortion Synthesis for VA Oscillators](https://mural.maynoothuniversity.ie/4104/1/VL_New_perspectives.pdf)
- OTA nonlinear modelling: Köper, Holters, Esqueda & Parker (DAFx20in22) model OTAs, including behaviour near the supply rails, and CMOS inverters in the EDP Wasp filter. Parameters were fitted to measurements by differentiable white-box modelling and simulated in ACME. This is a template for modelling Dreadbox OTA VCAs, drive and filters — [DAFx20in22 paper 34](https://dafx2020.mdw.ac.at/proceedings/papers/DAFx20in22_paper_34.pdf)
- Wavefolder modelling: "Virtual Analog Models of the Lockhart and Serge Wavefolders" (Applied Sciences 7(12):1328) — [DOI 10.3390/app7121328](https://0-doi-org.brum.beds.ac.uk/10.3390/app7121328)
- Feedback-delay-loop alias-free VA oscillators and hard-sync Fourier-series methods appear in the DAFx archive — [DAFx: Alias-free VA oscillators using a feedback delay loop](https://dafx.de/paper-archive/details/C4iObNyhou3Sz9mbNa3AkQ); [DAFx: VA oscillator hard sync](https://dafx.de/paper-archive/details/C3p4ljTQMk9GMtSpRIU8-A)

### Inferences (design proposals, not sourced facts)
- **Saw reset curvature:** a real saw core resets through a finite discharge, not an instant edge. Model it as a polyBLEP saw whose reset is replaced by a short ramp of about 1–5 µs, which can be implemented as a band-limited ramp (BLAMP) segment. Add a slight exponential bow to the ramp, for example x → (1−e^(−kx))/(1−e^(−k)) with small k. That bow imitates a capacitor charged by an imperfect current source and adds even harmonics.
- **Triangle-core asymmetry:** use slightly unequal rise and fall slopes (duty 0.48–0.52) and soft rounding at the peaks. A tanh or sin shaper on the derived "sine" gives Dreadbox-style multiwave sines that are not pure.
- **Pulse:** derive it from the saw with a comparator, then add a small DC offset and rise/fall smoothing. Map PWM through the Dreadbox-style single "shape" knob: square → square-saw → pulse → saw → noise, as on the Abyss, and saw ↔ square ↔ triangle, as on the Nymphes.
- **Sub-octaves:** use flip-flop-divided squares at −1 and −2 octaves, as on the Hades, phase-locked to the main oscillator's reset. Add a mild one-pole low-pass and level mismatch so they don't sound perfectly clinical.
- **Drift:** use per-voice static offsets of ±2–5 cents, reflecting a post-autotune residual, and slow 1/f or random-walk drift of about 0.1–1 Hz bandwidth and a few cents. Add tiny per-cycle period jitter. Provide an "auto-tune" button that re-randomises the offsets, mirroring the hardware calibration on the V3, Nyx v2, Abyss and Nymphes.
- **Triple Ring:** at audio rate, switch between two waves (for example a triangle and a sine), clocked by another oscillator's pulse. This is cheap and distinctive, and needs polyBLEP at the switch points.
- **FM:** Medusa offers exponential FM (osc 3 → 1 and 2). Offer exponential FM with through-zero off, so the pitch shift that comes with depth gives an "analog" FM feel.
- **Shared paraphonic path:** an option to mix all oscillators into one non-linear filter and VCA, with envelopes retriggering on every note, for the "paraphonic nature" sound.
- **Drive and filter:** an OTA-style tanh drive before the filter, plus a 2-pole OTA/SVF (Erebus) or 3-pole (Hades) low-pass. Include resonance compensation that adds low-end, giving a "resonant bump at zero" and a low-shelf boost like the Hades "bass boost". Run nonlinear stages with 2–4× oversampling.
- **Contrast with the JP-8080 layer:** keep the clean layer as the wide supersaw. The Dreadbox layer should usually be mono or narrow, saw or pulse plus sub, and driven. Medusa's verdict that wavetables "lack weight and warmth" next to analog oscillators suggests the dirty layer should own the low-mids.

### Gaps
- No published measurement or model of any Dreadbox circuit was found.
- No source documents Dreadbox drift magnitudes, so the drift parameter ranges above are engineering estimates.
- I did not find a DAFx paper specifically on saw-core reset curvature within budget. That model is proposed from general circuit principles.
