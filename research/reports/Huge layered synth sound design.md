# Spread the energy, lock the core

A synth sounds "huge" when its energy is spread across many critical bands, many decorrelated ear signals and many slowly beating partials, while the low end stays mono, phase-locked and punchy. Loudness at the same energy rises once a sound spreads past one critical band ([NYU](https://www.cns.nyu.edu/~david/courses/perception/lecturenotes/loudness/loudness.html)). Width comes from low inter-channel correlation in the 500 Hz–2 kHz bands, while lows stay correlated by nature ([Skålevik](https://www.akutek.info/Papers/MS_iacc-t_London-2017.pdf)). Punch falls as onset time grows past a few milliseconds ([Fenton](https://eprints.hud.ac.uk/id/eprint/26288/9/AES%20139_Perceptual_Punch_Model_Fenton.pdf)). Producers already build big sounds this way: a mono sub, a driven body, a supersaw mid and a wide air layer, split around 88–130 Hz and 2–3 kHz and glued with OTT-style compression. Potato-Synth should make that recipe the default. That means four engine slots that come up already pre-split by role, a clean JP-8080-style supersaw and a dirty Dreadbox-style body, a Jupiter-style two-oscillator voice as the base with "multipliers" stacked on top (dispersion, drift, unison, chorus, space), and a stack bus that handles mono-bass, gain compensation, glue and true-peak safety without the user asking. The market gap is real. Only Omnisphere 3 (Quadzone) and Korg Wavestate (vector joystick) morph four layers with one control, and neither pairs that with EDM-native supersaw, wavetable and spectral engines ([SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3); [Korg](https://www.korg.com/us/products/synthesizers/wavestate/)). Nord and Yamaha show how to scale the stack to 8 layers. Four layers is the proven default, and Yamaha caps one playable sound at 8 Parts under keyboard control ([yamahasynth.com](https://yamahasynth.com/?p=2020)). Their other lessons are effects in three tiers (insert, shared send, master), per-engine voice budgets, Group/Link editing, parameter-snapshot morphing alongside layer crossfading, and a Super Knob macro hierarchy with explicit per-destination ranges. Throughout this report, claims with a link are sourced. Claims marked **(inference)** are engineering judgement drawn from those sources and should be tested by ear and by measurement.

## Hugeness is energy spread across bands, ears and time

### Loudness grows with bandwidth, not level

The core perceptual fact is Zwicker's loudness summation. Hold total energy constant and widen a noise band centred at 1 kHz: loudness stays flat until the bandwidth passes one critical band, then rises ([NYU](https://www.cns.nyu.edu/~david/courses/perception/lecturenotes/loudness/loudness.html)). **Critical bandwidth is about 150 Hz at 1 kHz and about 800 Hz at 8 kHz**, and the Glasberg–Moore ERB-number scale, ERBS = 21.33·log10(1 + 0.00437·f), counts how many such bands lie below a frequency ([MathWorks](https://it.mathworks.com/help/audio/ref/hz2erb.html); [Wikipedia](https://en.wikipedia.org/wiki/Equivalent_rectangular_bandwidth)). The matching bandwidth form, ERB(f) ≈ 24.7·(4.37·f/1000 + 1), gives about 35 Hz at 100 Hz, 133 Hz at 1 kHz and 456 Hz at 4 kHz **(inference)**. That is the ruler Potato-Synth should use to size unison spreads and layer spacing. A seven-voice supersaw, a saturated saw or an air layer covers more ERBs than a sine at the same RMS, so it sounds louder without clipping. That is the main reason the "huge by default" brief can be met without pushing peaks **(inference)**.

LUFS meters complicate this. BS.1770's K-weighting adds a shelf above about 2 kHz and de-weights the lowest bass ([Sound On Sound](https://www.soundonsound.com/node/4921525); [Fora Soft](https://www.forasoft.com/learn/audio-for-video/glossary/terms-audio/k-weighting)). Heavy sub energy therefore uses up headroom while adding little measured loudness, and upper-mid harmonics add LUFS efficiently. Below about 100 Hz, loudness grows very fast per decibel: "a slight increase in level can change the perceived loudness from barely audible to loud" ([Møller & Pedersen](https://vbn.aau.dk/ws/portalfiles/portal/89856984/Moeller_and_Pedersen_2004.pdf)). Sub level and sub drive controls therefore need fine resolution, because a 1–2 dB change there is perceptually large **(inference)**.

### Masking spreads upward, so the sub must be fenced in

Masking spreads upward in frequency and lasts in time. **Forward masking builds over 20–200 ms of masker and fades by about 200 ms; backward masking is gone by about 100 ms and mostly within 25 ms** ([Hearing Health Matters](https://hearinghealthmatters.org/pathways-society/2015/unmasking-auditory-temporal-masking/)). A loud bass layer buries upper layers far more than the reverse. Two layers whose dominant partials fall in the same ERB fuse and partly mask each other. Separating them by about one ERB makes their loudness add instead of compete **(inference)**. Masking also lasts in time, so a transient or pluck layer hides the first tens of milliseconds of a pad. Offsetting or ducking the pad by roughly 10–50 ms lets both attacks read **(inference)**.

### Width lives above 250 Hz, and the lows should stay coherent

Width tracks low interaural cross-correlation (IACC). Apparent source width follows early energy (the first 80 ms), envelopment follows late energy, and the standard IACC3 measure averages the 500 Hz, 1 kHz and 2 kHz octaves. Typical concert halls sit near **IACC_E3 = 0.39** ([Skålevik](https://www.akutek.info/Papers/MS_iacc-t_London-2017.pdf)). A pure tone arriving from the side gives an IACC of −0.92 at 1 kHz but 0.71 at 125 Hz, so lows stay correlated however you pan them ([Skålevik](https://www.akutek.info/Papers/MS_iacc-t_London-2017.pdf)). Inter-channel delays behave in three regimes:

| Inter-channel delay | What the listener hears |
|---|---|
| Under about 1 ms | One image placed between the speakers |
| Up to about 10 ms | The lead channel dominates localisation for broadband sound |
| From about 5 ms (clicks) or 15–30 ms or more (sustained or repeated tones) | A separate echo |

Source: [Brown, Stecker & Tollin](https://pmc.ncbi.nlm.nih.gov/articles/PMC4310855).

The DAFx 2024 frequency-dependent decorrelator gives the engineering rule. Its output coherence is about cos²(β) times the input coherence, where β is the mixing angle. The authors recommend a lower β below a 250 Hz Linkwitz-Riley crossover than above it, and they warn that full-width allpass decorrelation "chirps" on drum-and-bass transients ([DAFx24 paper 92](https://dafx.de/paper-archive/2024/papers/DAFx24_paper_92.pdf)). Detuned unison voices with independent phase per channel are a natural decorrelator. They avoid the static comb notches of a Haas delay because the beating moves **(inference)**. A target inter-channel correlation of about 0.2–0.6 for wide pads extrapolates hall data to mixes. It is a design target, not a mix standard **(inference)**.

### Depth is felt at 40–80 Hz and heard through harmonics

On large systems, depth is tactile. Whole-body vibration is felt about **20–25 dB above the hearing threshold** ([Møller & Pedersen](https://vbn.aau.dk/ws/portalfiles/portal/89856984/Moeller_and_Pedersen_2004.pdf)). In one study, adding a 40 Hz vibration raised music-quality ratings by **29 points out of 100**, while 20 Hz gave no significant improvement ([Merchel & Altinsoy](https://dhi.ac.uk/books/icmem2015/tactile-perception-of-music)). Small speakers cut off around 100–200 Hz, and virtual-bass systems restore the missing fundamental by synthesising harmonics in the fc–4fc band at about **+8 to +12 dB**. Half-wave rectification alone gives only even harmonics and can make the pitch jump an octave, so an odd-harmonic generator must complete the series ([Moliner et al., DAFx2020](https://acris.aalto.fi/ws/portalfiles/portal/51973173/Eloi2020_Virtual_bass_DAFx2020.pdf)). For Potato-Synth this suggests a sub engine with a 20–30 Hz high-pass by default, energy aimed at 40–80 Hz, and a "translate" control. That control would add odd and even harmonics 2–5 through asymmetric soft-clipping, low-passed at about 4×f0, so the sub carries on phones **(inference)**.

### Beating is richness until it becomes roughness

Plomp and Levelt found that **maximal dissonance occurs at about 25% of a critical bandwidth**, and that consonance returns at about one full critical band ([Plomp & Levelt 1965](https://www.math.miami.edu/~armstrong/592sp15/Plomp_Levelt_1965.pdf)). Beating below about 15 Hz is heard as a pleasant fluctuation in loudness. Faster beating becomes roughness, which disappears at about 75–150 Hz ([Vassilakis](https://acousticslab.org/learnmoresra/moreroughness.html)). A worked example shows why this matters. A 10-cent detune on A2 (110 Hz) beats at about 0.64 Hz on the fundamental, 6.4 Hz on harmonic 10 and 32 Hz on harmonic 50. The upper partials beat fast, but relative to their wide ERBs they stay in "shimmer" territory. In the bass, ERBs are only 35–50 Hz wide, so wide detune there pushes the fundamental toward roughness and phase cancellation **(inference)**. **Detune should therefore scale with register.** Narrow it, or keep a centre voice un-detuned, below about 200 Hz.

### Punch is transient loudness below 1 kHz

Fenton's perceptual punch model measures transient loudness over a 280 ms window. It fits **EstPunch = 0.954 − 0.171·band − 0.088·onset (R² = 0.906)** over octave bands from 64 Hz to 1 kHz and onset times from 0 to 60 ms ([Fenton, AES 139](https://eprints.hud.ac.uk/id/eprint/26288/9/AES%20139_Perceptual_Punch_Model_Fenton.pdf)). Punch falls with both higher band and slower onset. Faster attacks are also heard as brighter at equal spectral centroid ([Univ. Vienna](https://muwidb.univie.ac.at/brightness-studies/index.htm)). Three design rules follow, all **(inference)**:

- Default bass and lead attacks should be 0–5 ms.
- Transient "thump" energy belongs in the 64–500 Hz region.
- Heavy limiting inside the synth flattens the transient-to-sustain contrast that punch depends on.

## Four layers, two crossovers: how producers already build big

### The band map clusters at 88–130 Hz and 2–3 kHz

Tutorials agree on three or four layers, though their numbers differ:

| Source | Split |
|---|---|
| iZotope | Sub low-passed at 100 Hz, mids 300–3000 Hz, top layer cut below 3 kHz |
| Attack Magazine | Sine sub rolled off above 190 Hz with a 30 Hz low cut; body cut below about 130 Hz and above 3 kHz; chord layer low-cut at 250 Hz, with width added only above 2 kHz |
| Soundbridge | Split at about 300 Hz |
| MODE Audio | Four bands: 150–300 Hz, 300–550 Hz, 550 Hz–2 kHz, 2–15 kHz |

Sources: [iZotope](https://www.izotope.com/en/learn/how-to-program-and-layer-electronic-basslines-in-your-daw?page=6); [Attack Magazine](https://www.attackmagazine.com/technique/synth-secrets/layering-synths/); [Soundbridge](https://soundbridge.io/layered-synth-sounds); [MODE Audio](https://modeaudio.com/magazine/layering-synths).

Xfer OTT's fixed crossovers, **88.3 Hz and 2.5 kHz**, are the de facto EDM three-band split ([EDMProd](https://edmprod.com/ott-plugin)). The numbers fall into two zones, sub-versus-body at 88–130 Hz and mid-versus-air at 2–3 kHz. Two crossover controls with those defaults therefore cover most of the practice **(inference)**. No source specified filter slopes. Linkwitz-Riley 24 dB/oct complementary pairs, which sum flat, are standard crossover practice and JUCE ships them as `LinkwitzRileyFilter` ([JUCE](https://docs.juce.com/master/namespacejuce_1_1dsp.html)). The 100–300 Hz body zone has no consensus, so it should stay user-adjustable.

### Phase-lock the low end and decorrelate the rest

For reese basses, producers set voicing to mono, unison spread to mono, and "turn the phase randomization down so they start at the exact same point" ([Future Proof](https://futureproofmusicschool.com/blog/reese-bass-sound-design-everything-you-need-to-know)). Even Syntorial's Martin Garrix "Animal" lead resets all oscillator phases on note start ([Syntorial](https://www.syntorial.com/preset-recipe/martin-garrix-animal-lead/)). Ableton Utility's Bass Mono threshold ranges from 50 to 500 Hz ([Cycling '74](https://docs.cycling74.com/reference/abl.device.utility~)). Haas widening uses 1–30 ms with zero feedback, and the delayed copy on bass has lows removed up to about 200 Hz ([eMastered](https://emastered.com/blog/haas-effect)). Potato-Synth's role defaults should follow, all **(inference)**:

- Sub and body layers: retriggered phase and mono unison.
- Upper layers: 100% random phase.
- Stack output: a mono-below-crossover stage defaulting to about 120 Hz.

### Genre recipes give concrete starting numbers

| Patch | Settings | Source |
|---|---|---|
| Vital supersaw | Seven unison voices near the 20% default detune ("above about 50% the sound becomes dissonant"); second saw an octave down with three voices at about 7%; reverb about 500 ms at 30–40% | [FaderPro](https://blog.faderpro.com/techniques/supersaw-how-make-iconic-sound/) |
| Future-bass chords | Two 16-voice unison saws an octave apart, with a quick upward pitch envelope, reverb then gate, and kick/snare ducking | [Loopmasters](https://loopmasters.com/articles/4372-How-To-Make-A-Future-Bass-Drop-A-Future-Bass-Production-Tutorial) |
| Big-room lead ("Animal") | Four voices at 19 cents with 100% spread; 15% noise; pitch envelope of +3 octaves with 0 ms attack and 10 ms decay; chorus 100% wet; reverb 40% | [Syntorial](https://www.syntorial.com/preset-recipe/martin-garrix-animal-lead/) |
| Reese | Two sines at ±0.27 semitones, mono, 24 ms release | [Attack Magazine](https://www.attackmagazine.com/technique/tutorials/reese-bass-redux/) |

The recurring toolkit adds three habits:

- Distortion on the mid layers only, often "high amount, low mix" ([Attack](https://www.attackmagazine.com/technique/synth-secrets/layering-synths/)).
- OTT Depth around 30–40% on instruments ([EDMProd](https://edmprod.com/ott-plugin)).
- Glue compression on the layer bus at about 2:1 and 4 dB of gain reduction ([Attack](https://www.attackmagazine.com/technique/synth-secrets/layering-synths/)).

No text source gave settings for Seamless R, Mr. Bill, Au5 or Zedd, or for hoover patches. Those exist only in videos. The notes also found no per-layer headroom targets and no inter-layer dynamic-EQ settings, so the stack-glue proposals below that use them are inference.

## Oscillators are the first multiplier, and the supersaw is now fully known

### The supersaw is simpler than Szabo's curves suggest

Adam Szabo's 2010 thesis analysed JP-8000 and JP-8080 recordings with a 131072-point FFT. He modelled the supersaw as seven naive saws with random phase on each note, an 11th-order polynomial detune curve, a linear-down centre gain (−0.55366x + 0.99785) and a parabolic-up side gain (−0.73764x² + 1.2841x + 0.044372). He said the aliasing "adds airiness" and should be kept, with a pitch-tracked HPF removing what falls below the fundamental ([Szabo](https://www.adamszabo.com/internet/adam_szabo_how_to_emulate_the_super_saw.pdf)).

The December 2025 39C3 reverse-engineering of the actual DSP code, as summarised by the ÜBERSAW project, simplifies the picture ([media.ccc.de](https://media.ccc.de/v/39c3-from-silicon-to-darude-sand-storm-breaking-famous-synthesizer-dsps/audio); [ÜBERSAW](https://github.com/house-of-houses/ubersaw/blob/master/docs/algorithm.md)):

- **24-bit integer phase accumulators at 88.2 kHz.**
- A fixed asymmetric detune table of **0, ±128, +816/−824, +1408/−1440**, scaled by pitch × detune.
- Oscillator 0 always at full level, with the six side saws scaled linearly by the Mix control.
- A pitch-tracked HPF after the sum.

ÜBERSAW's claims were not independently verified. Synth Anatomy adds that the 88.2 kHz rate is what keeps the aliasing inaudible, so copying the design at 44.1 kHz will alias ([Synth Anatomy](https://synthanatomy.com/2026/01/je-8086-roland-dsp-emulator-insights-and-the-supersaw-is-no-longer-a-mystery.html)).

The HPF is still unsettled. The MIT 6.111 report places it at the fundamental ([MIT](https://fpga.mit.edu/videos/2018/team07/report.pdf)). A KVR fit used a second-order filter at 2.5×f0 with Q = √2 ([KVR](https://www.kvraudio.com/forum/viewtopic.php?p=8572590)), and ÜBERSAW uses a one-pole. Valhalla's Sean Costello noted on KVR that the uneven, non-linear detune spacing is what produces complex beating, where even spacing gives one dominant beat rate ([KVR](https://www.kvraudio.com/forum/viewtopic.php?p=4644620)).

**Design consequence (inference):** ship a "JP" unison mode built from the integer table, linear side gain, random static start phase, a key-tracked HPF inside the oscillator and 2× internal oversampling. Ship a "Modern" mode beside it with band-limited saws and a power-curve spread.

### Vital is the reference for modern unison

Vital's GPLv3 source is a reference design to re-implement, not to copy into a closed product. It packs up to 16 unison voices into SIMD lanes and spreads detune in symmetric log-frequency pairs, using a power curve (e^{p·x} − 1)/(e^{p} − 1) with a default power of 1.5. Its blend law is normalised by 1/√(centre² + detuned²·(n−1)), which is constant power for uncorrelated voices. Stereo spread is equal-power, and phases are randomised at note-on with 100% random phase as the default ([synth_oscillator.cpp](https://github.com/mtytel/vital/blob/main/src/synthesis/producers/synth_oscillator.cpp); [synth_parameters.cpp](https://github.com/mtytel/vital/blob/main/src/common/synth_parameters.cpp)). Its unison "stack" modes multiply pair ratios by octaves, fifths, chords or harmonic series. Wavetable frame, warp and spectral-morph settings can also be spread across unison voices ([synth_oscillator.h](https://github.com/mtytel/vital/blob/main/src/synthesis/producers/synth_oscillator.h)).

Vendor guidance limits voice counts. Xfer says more than 3–7 unison voices per oscillator is "often unnecessary", raises CPU and causes phasing, and recommends a bus chorus because it "processes the effect once, not once per voice" ([Xfer support](https://support.xferrecords.com/article/51-serum2-sound-design-guidelines-for-optimizing-cpu-usage)). Spire and Sylenth1, the most-cited supersaw synths, rely on four oscillators with 8–9 voices each rather than extreme per-oscillator counts ([Dubspot](https://blog.dubspot.com/plugins/compare/spire-vs-sylenth1)). Bigness comes from moderate unison across several oscillators, per-voice decorrelation and post-sum width effects **(inference)**.

One caveat applies to the loudness law. With free-running phases, 1/√N scaling holds loudness steady. With retriggered phases and small detune, the voices are coherent and sum closer to N, so the patch gets louder. A detune-dependent correction or a gentle peak limiter is needed **(inference)**.

### Anti-aliasing: PolyBLEP for VA, per-pitch IFFT for tables

Välimäki, Pekonen and Nam measured the highest perceptually alias-free sawtooth f0 at 44.1 kHz ([JASA 2012](https://mac.kaist.ac.kr/pubs/ValimakiPeknenNam-jasa2012.pdf)):

| Method | Highest alias-free f0 | Cost per corrected sample |
|---|---|---|
| Second-order PolyBLEP | **2,135 Hz** | **8 operations** |
| Fourth-order B-spline PolyBLEP | **7,845 Hz** | About 28 operations, plus a post-equaliser |
| 32-sample LUT-BLEP | 2,036 Hz | Almost 450 operations |

PolyBLAMP fixes slope discontinuities such as triangles, clipping and rectification. It cuts aliasing by up to 50 dB and is "a more efficient tool for alias reduction than oversampling" ([DAFx 2016](https://dafx.de/paper-archive/details/xRM0GiuF-JmeN0_aaP9VNg)).

Vital stores each wavetable frame as 1025 harmonics. Every 7 ms per voice, it zeroes the bins above Nyquist for the current pitch, inverse-FFTs a 2048-sample cycle, crossfades from the previous buffer and reads it with Catmull-Rom interpolation ([wavetable.cpp](https://github.com/mtytel/vital/blob/main/src/synthesis/lookups/wavetable.cpp)). Aliasing is therefore controlled by construction, and spectral morphs are arbitrary bin operations. Phase-domain warps such as FM, bend and squeeze still create components above Nyquist and rely on oversampling. HISE's Christoph Hart notes that oversampling a properly mip-mapped table oscillator is mostly pointless ([HISE forum](https://forum.hise.audio/post/79823)).

**Design consequence (inference):** use second-order PolyBLEP saws with a fourth-order HQ option, per-pitch IFFT tables, and oversampling per oscillator only while warps, FM or hard drive are active.

### Spectral engines: IFFT for harmonic content, rotor banks for inharmonic

Commercial additive engines run at hundreds of partials per voice:

- Harmor runs up to **516 partials per note per unison voice**, and its CPU rises with partial count, low root notes, unison and harmonic blur ([Image-Line](https://www.image-line.com/support/flstudio_online_manual/html/plugins/Harmor.htm)).
- Razor uses up to 320 partials with per-partial panning and filtering ([NI](https://www.native-instruments.com/products/komplete/synths/razor/)).
- Pigments' Harmonic engine uses 512 ([SOS](https://www.soundonsound.com/reviews/arturia-pigments-3?amp)).

Rodet and Depalle's IFFT synthesis makes cost nearly independent of partial count, at the price of stair-cased glides and one frame of latency ([JOS SASP](https://ccrma.stanford.edu/%7ejos/sasp/Inverse_FFT_Synthesis.html)). The research team also ran an unpublished in-session micro-benchmark (AVX2 Xeon at 2.1 GHz, so only indicative). A vectorised complex-rotor oscillator cost about 0.18 ns per partial-sample, six times cheaper than `sinf`. On that basis, 512 partials × 8 voices costs about 3.5% of a core, but adding 16-voice unison per partial costs about 56%.

**Design consequence (inference):** the spectral-morph engine should build harmonic spectra Vital-style (bins, then IFFT, then a table) and apply unison after rendering. A rotor bank should be kept only for inharmonic or per-partial-panned modes.

## The post-oscillator chain turns a big oscillator into a finished sound

### Filters and saturation

Topology-preserving-transform (zero-delay-feedback) filters are the modern standard. Zavalishin's *Art of VA Filter Design* covers SVF and ladder models and their nonlinear zero-delay-feedback equations ([Zavalishin](https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.1.0.pdf)). Simper's trapezoidal SVF morphs smoothly "even at audio-rate modulation" ([Cytomic](https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf)). JUCE ships `StateVariableTPTFilter` and `LadderFilter` ([JUCE](https://docs.juce.com/master/namespacejuce_1_1dsp.html)). Huovilainen's nonlinear ladder uses about five tanh evaluations per sample, 2× oversampling and a half-sample feedback delay ([DAFx-04](https://dafx.de/paper-archive/2004/P_061.PDF)).

For waveshaping, first-order antiderivative antialiasing (ADAA), y = (F1(x[n]) − F1(x[n−1]))/(x[n] − x[n−1]), suppresses aliasing cheaply but adds a half-sample delay. That delay must be compensated inside feedback loops ([DAFx20in21 paper 42](https://dafx2020.mdw.ac.at/proceedings/papers/DAFx20in21_paper_42.pdf)).

**Practical defaults (inference):**

- Use tanh-family ADAA with 2× oversampling.
- Use asymmetric curves or a DC bias for even-harmonic warmth, followed by a DC blocker.
- Run drive in parallel, with a dry/wet mix.
- Split drive into bands so the lows are distorted least, which avoids intermodulation mud.

### Stereo, dynamics and loudness

The Roland CE-1 chorus sweeps its wet signal between about 1.5 and 4 ms of delay. The Dimension D uses several bucket-brigade taps, each on its own delay trajectory ([UA WebZine](https://uaudio.com/webzine/2005/april/text/content2.html)). Dattorro calls two-voice chorus the industry standard and recommends allpass interpolation ([Dattorro](https://ccrma.stanford.edu/%7Edattorro/EffectDesignPart1.pdf)). The Juno-60 chorus rates often quoted (about 0.5 and 0.8 Hz) were not verified in this research.

OTT is Ableton Multiband Dynamics' "OTT" preset:

- **Bands:** three, split at 88.3 Hz and 2.5 kHz.
- **Ratios:** extreme downward ratios above threshold, plus 4:1 upward compression below it.
- **Gain:** +5.2 dB in per band, and +5.7 to +10.3 dB out.
- **Controls:** Depth is the dry/wet mix and Time scales attack and release together.

EDMProd suggests 30–40% depth on instruments and advises turning the output down to avoid "louder is better" bias ([EDMProd](https://edmprod.com/ott-plugin)).

True-peak limiting with "4x oversampling with a minimum lookahead of 0.1 ms keeps inter-sample peaks within about 0.1 dB", at about 5 ms of latency ([FabFilter](https://www.fabfilter.com/help/pro-l/using/truepeaklimiting)). JUCE's stock `Limiter` has no lookahead or true-peak detection ([JUCE](https://docs.juce.com/master/namespacejuce_1_1dsp.html)). Streaming platforms normalise to about −14 LUFS-I, or −16 LUFS-I for Apple. Loud club masters sit near −5 to −8 LUFS-I, though these figures come from EDMProd rather than platform documentation ([EDMProd LUFS](https://www.edmprod.com/lufs/)).

LUFS targets apply to the full mix, not the synth. Inside the synth, what matters is crest factor, a predictable drive level and true-peak safety. Potato-Synth should process in float, aim presets at about −6 dBFS peak, and make the final soft clipper or limiter optional rather than always on, to preserve punch **(inference)**.

### Space

Dattorro's plate is cheap and fully specified. Its published values are input diffusion 0.750 and 0.625, decay diffusion 0.70, and tank modulation at about 1 Hz ([Dattorro](https://ccrma.stanford.edu/%7Edattorro/EffectDesignPart1.pdf)). Feedback delay networks (FDNs) scale further. Householder matrices need only 2N−1 additions, losslessness requires a unitary matrix, and delay lengths should be mutually prime with a mode density of at least 0.15·T60·fs ([JOS FDN](https://www.dsprelated.com/freebooks/pasp/FDN_Reverberation.html)).

The size tools from tutorials, all [Attack Magazine](https://www.attackmagazine.com/technique/synth-secrets/layering-synths/) unless noted:

- Reverb low-cut and darkened tails.
- A delay ducked by the kick.
- An envelope on effects mix that shuts the tail off after the note ([Production Music Live](https://www.productionmusiclive.com/blogs/news/10-supersaw-tips-for-xfer-serum-get-a-big-lush-sound-without-layering)).

Pre-delay of 10–40 ms and 3–6 dB of wet ducking are domain defaults, not sourced values **(inference)**.

## Omnisphere 3 shows the layered recipe, and the EDM gap

### What the competition does

The flagships converge on three to five generator slots, eight macros and multi-bus effects:

- **Serum 2:** three oscillators, each switchable among five types, plus dedicated sub and noise oscillators; eight modulatable macros; two effect busses with Lows/Highs, three-band and Mid/Side splitters ([Xfer PDF](https://static.xferrecords.com/Serum%202%20What's%20New.pdf)).
- **Phase Plant:** eight macros, generator groups and three effect lanes ([Kilohearts](https://kilohearts.com/docs/phase_plant)).
- **Vital:** three oscillators, 16 unison voices, four macros and a 32-voice cap ([synth_constants.h](https://raw.githubusercontent.com/mtytel/vital/main/src/common/synth_constants.h)).
- **Massive X:** 16 macros. At launch, only macros were automatable, which made DAW automation unreliable, and the Performer setup was "daunting" ([SOS](https://www.soundonsound.com/reviews/native-instruments-massive-x?page=2)).
- **Korg Wavestate:** four layers mixed by a vector joystick and envelope, eight mod knobs saved per sound, and intelligent per-section randomisation ([Korg](https://www.korg.com/us/products/synthesizers/wavestate/)).

The recurring complaints point to openings:

- Unison and oversampling CPU in Vital and Serum ([Vital forum](https://forum.vital.audio/t/cpu-optimization-for-vital/4810?page=4)).
- No engine-level preset save in Pigments ([SOS](https://www.soundonsound.com/reviews/arturia-pigments-6)).
- Deep, confusing hierarchy and no spectral synthesis in Falcon ([Rekkerd](https://rekkerd.org/review-falcon-3-creative-hybrid-instrument-by-uvi)).
- No product in the survey documents automatic gain compensation when crossfading layers. That gap is an opening **(inference)**.

### Omnisphere 3 in depth

Omnisphere 3 shipped on **21 October 2025 at $499, or a $199 upgrade** ([Synthtopia](https://www.synthtopia.com/?p=150678)). It keeps four layers per patch and eight patches per multi ([Spectrasonics](https://www.spectrasonics.net/products/omnisphere/)). New in v3:

- **Quadzone:** splits, crossfades, velocity switching and multiple zones per layer, morphed by note, velocity or one continuous MIDI fader ([SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3)).
- **Filters:** 36 new types in seven "sonic colors" (Classic, Jupiter, Beefy, OB, French, Subtle, Sauce), with circuit-modelled saturation. SOS counts 35.
- **Analog behaviour:** Oscillator Drift, plus Unison Phase Scatter.
- **Stacking within a layer:** Granular, Unison and Harmonia can now run at the same time. In v2, only one of the three could run per layer ([SOS 2015](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-2)).
- **Other additions:** a polyphonic dual frequency shifter, MPE, Adaptive Global Controls that "analyze each patch automatically", and saved Patch Mutations ([Synthtopia](https://www.synthtopia.com/?p=150678)).
- **Effects:** 93 in total, in four-slot racks, also available as a standalone plugin ([SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3)).

Harmonia adds four oscillators per layer, each with its own transpose, detune, shape, sync, pan and mix ([SOS 2015](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-2)).

The Orb morphs between "automatically generated parameter scenes" that depend on the patch:

- **Angle** chooses which scenes it blends.
- **Radius** sets the intensity, and the centre returns the original patch.
- **Dice** regenerates the scenes.
- **Inertia** and **Attractor** give the cursor ball-bouncing and pendulum physics.

Source: [Orb manual](https://support.spectrasonics.net/manual/Omnisphere2/orb/index.html). Inactive layers use no CPU ([v1 manual](https://support.spectrasonics.net/manual/Omnisphere/main/all.htm)). MusicTech calls v3 "extremely CPU-friendly" on an M2 MacBook Air ([MusicTech](https://musictech.com/reviews/plug-ins/spectrasonics-omnisphere-3-review/)).

Omnisphere builds size at three levels: stacking within a voice, layering across voices, and imprinting space with Innerspace convolution of arbitrary audio ([MusicRadar](https://www.musicradar.com/news/tech/spectrasonics-eric-persing-talks-omnisphere-2-sound-design-and-more-616236)). Its pads come from organic sample sources, granular smearing, drift and convolution space.

Its EDM weaknesses are documented. SOS says rivals offer "more complex modulation routing, spectral wavetable warping, and animated graphical displays" ([SOS](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-3)). VI-Control users call it "a bit more laid back" than Serum 2, which they describe as "bitey, crystal clear and punchy" ([VI-Control](https://vi-control.net/community/threads/does-serum-offer-something-omnisphere-doesnt.167997/)). Its 26,421 new patches are a browsing burden.

Potato-Synth can take Omnisphere's structure and correct its weaknesses **(inference)**:

- **Quadzone becomes a 2D XY pad.** The Orb's opaque scenes become visible, editable ones.
- **Within-layer stacking is kept,** with unison, Harmonia-style and granular running at once.
- **The default voicing is forward and aggressive,** with fast transients, OTT and stronger drive, where Omnisphere is laid back.

The Orb's internal maths is unpublished. Polar interpolation of scene offset vectors (angle picks the blend, radius scales it) fits its documented behaviour but is an inference.

## Jupiter base, JP-8080 clarity, Dreadbox dirt

### The Jupiter-8 as the base voice

The Jupiter-8 voice is two VCOs into a non-resonant HPF, then a resonant IR3109 OTA low-pass switchable between 12 and 24 dB/oct, then a BA662 VCA. It has two ADSRs with attacks of about 1 ms ([Wikipedia](https://en.wikipedia.org/wiki/Roland_Jupiter-8)). VCO-2 can cross-modulate VCO-1's frequency, sync to it, or drop to LFO range. ENV-1 is invertible and routes to pitch, PWM and filter ([Roland spec](https://support.roland.com/hc/en-us/articles/201945109-Jupiter-8-Technical-Specifications)). Unison stacks all 16 oscillators on one note ([Wikipedia](https://en.wikipedia.org/wiki/Roland_Jupiter-8)). Sound On Sound calls it "clear and transparent", a synth that blends into a mix rather than dominating it like Prophets and Oberheims ([SOS](https://www.soundonsound.com/reviews/roland-jupiter)).

The Jupiter-6 contributes two features:

- **Waveform summing.** Any or all waveforms can be selected at once in each oscillator ([Wikipedia](https://en.wikipedia.org/wiki/Roland_Jupiter-6)).
- **A multimode IR3109 filter** (LP, BP, HP) whose high resonance rings harshly ([SOS Jove](https://www.soundonsound.com/reviews/system-80-jove)).

Roland's own Jupiter-8 recipes start from two saws or pulses with VCO-2 fine-tuned about 6–10 units off. Brass gets an inverted ENV-1 pitch blip into VCO-1, and chorus is optional ([Roland Articles](https://articles.roland.com/sound-design-jupiter-8-model-expansion-poly-presets/)).

Every modern emulation adds the same "multipliers":

- **Arturia** has Voice Dispersion over pitch, PW, envelope, cutoff, resonance and mod depth, plus unison detune and alternating L/R pan spread ([Arturia FAQ](https://support.arturia.com/hc/en-us/articles/4405740988690-Jup-8-V4-General-Questions)).
- **TAL** has per-voice VCO detune and pan, VCO instability, a digital cross-mod that stays in tune, and filter overdrive ([TAL-J-8](https://www.tal-software.com/products/tal-j-8)).
- **Roland** adds Juno-106 or CE-1 chorus "for warmth and thickness" ([Roland Articles](https://articles.roland.com/sound-design-jupiter-8-model-expansion-poly-presets/)).

**Design consequence (inference):** the Jupiter voice should be the default topology of the VA engine. The multipliers then stack in a fixed order: per-voice tolerance seed, slow drift, unison count and spread, alternating pan, pre-filter drive, chorus, and space. Taking unison beyond 8 voices per note suits EDM but leaves strict authenticity behind.

### The JP-8080 as the clean reference

The JP-8080's OSC1 offers seven algorithms (Super Saw, Triangle Mod, Noise, Feedback, Square, Saw, Triangle), each with two wave-specific controls such as Detune/Mix or Feedback/Harmonics. OSC2 adds sync, ring and cross-mod ([Roland PDF](https://static.roland.com/assets/media/pdf/JP-8000.pdf)). The 8080 adds 10 voices, Unison and noise on OSC2. In A/B tests, SOS heard no sonic difference from the JP-8000 ([SOS](https://www.soundonsound.com/reviews/roland-jp8080)).

Its cleanness is structural **(inference)**:

- naive saws run at 88.2 kHz
- a key-tracked HPF inside the oscillator
- fixed-point tuning with zero drift
- no phase tricks or chorus inside the oscillator

Reviewers in 1998 still found its DSP unison "thinner" than analog, "though still pleasing" ([SOS](https://www.soundonsound.com/reviews/roland-jp8080)).

Potato-Synth's clean engine should copy that model of "one algorithm, two macro knobs". The supersaw HPF should stay inside the oscillator so the user's filter cannot undo the de-mudding, and drift should be opt-in **(inference)**. The Feedback oscillator is described by third parties as a saw with a self-resonating, key-tracked comb ([Vital forum](https://forum.vital.audio/t/roland-style-feedback-oscillator/12153)). That is a cheap, distinctive extra, but it is unverified.

### Dreadbox as the dirty body

Dreadbox oscillators are deliberately simple: saw, pulse and triangle, plus subs and noise, often under one continuous shape control. The Abyss morphs square, square-saw, pulse, saw and noise ([MusicRadar](https://musicradar.com/reviews/dreadbox-abyss)). Reviewers credit the character to the analog path rather than the oscillators:

- **Erebus V3:** resonance "seems to enhance the low end rather than remove it" ([Sonic State](https://sonicstate.com/news/2019/01/11/sonic-lab-dreadbox-erebus-v3-review/)), and "a resonant bump is always present, even when the Res knob is set to zero" ([MusicRadar](https://www.musicradar.com/reviews/dreadbox-erebus-v3)).
- **Hades:** a "Super fat oscillator" with two sub-octaves, a bass-boost circuit and OTA distortion ([Kraft Music](https://kraftmusic.com/products/dreadbox-hades-reissue-monophonic-analog-bass-synthesizer)).
- **Abyss:** a Drive control that adds "body and mid-focused saturation" ([MusicRadar](https://musicradar.com/reviews/dreadbox-abyss)).

The Medusa review gives the most direct evidence. Its analog oscillators have Dreadbox's "warm, characterful quality", while its wavetables "lack weight and warmth" ([MusicRadar](https://www.musicradar.com/reviews/dreadbox-and-polyend-medusa)).

No Dreadbox circuit has been published or emulated, so the "dirt" engine is a proposal **(inference)**:

- **Oscillator:** a PolyBLEP saw with a curved, finite-time reset and a slightly bowed ramp; flip-flop sub-octaves at −1 and −2.
- **Drive and filter:** OTA tanh drive into a 2- or 3-pole filter whose resonance adds low end.
- **Paraphonic option:** one shared filter and VCA for all notes.
- **Drift:** about ±2–5 cents per voice, with an "auto-tune" button that re-rolls the offsets.

It belongs in the sub-adjacent body role: mono or narrow, driven, owning the low mids. The clean supersaw then owns the width.

## Creative maths multiplies size cheaply

### Detune, drift and FM

Making detune offsets incommensurate, for example spaced by a golden-ratio Weyl sequence or √prime ratios, means the beat pattern never repeats exactly. That formalises Costello's "complex beating" point. No perceptual study tests this against linear detune, so it is **(inference)**.

Analog drift is commonly modelled as a random target pitch with a slow glide. A sum of three uniform randoms approximates a Gaussian per-note detune. About ±5 cents gives pleasant beating, ±20 cents sounds "huge", and a Prophet-600 drifted about 10 cents over 30 minutes ([KVR](https://www.kvraudio.com/forum/viewtopic.php?p=7408514)). An Ornstein–Uhlenbeck process, X += θ(μ−X)Δt + σ√Δt·N(0,1), with stationary standard deviation σ/√(2θ), is the cleanest parametrisation. Its σ maps to cents and θ to drift speed, at one multiply-add per voice per block (standard maths, **inference**).

Chowning's FM gives a bandwidth of about 2(d + m). Reduced c:m ratios set harmonicity: N2 = 1 gives all harmonics, an even N2 gives odd harmonics only, and an irrational ratio gives bells ([Chowning 1973](https://web.eecs.umich.edu/%7Efessler/course/100/misc/chowning-73-tso.pdf)). Feedback FM moves from sine toward saw and then into chaos ([SuperCollider](https://docs.supercollider.online/Classes/SinOscFB.html)). Casio phase distortion is a special case of phase modulation ([Electric Druid](https://electricdruid.net/?p=226)). Chebyshev polynomials give exact harmonic recipes, such as T5 producing pure cos 5ωn, but only at unity input amplitude ([Puckette](https://msp.ucsd.edu/techniques/latest/book-html/node84.html)). That makes them a cheap, exact "warmth" exciter on a sine sub **(inference)**.

### Coupling, chaos, Shepard and resonant bodies

The Kuramoto model, φ̇_i = ω_i + (K/N)Σ sin(φ_j − φ_i), has a critical coupling Kc = 2/(π·g(0)), where g(0) is the density of natural frequencies at their centre. It has been used musically on audio-rate sine banks ([Lem, SMC 2019](https://www.smc2019.uma.es/articles/P3/P3_09_SMC2019_paper.pdf)). A single "Focus" macro on unison or additive voices could run from a wide incoherent cloud at K = 0 to a phase-locked, punchy tone above Kc **(inference)**. Joranalogue's Orbit 3 shows that one Chua-type attractor covers both a 7-minute LFO and 22 Hz–22 kHz audio ([Orbit 3 manual](https://cdn.shopify.com/s/files/1/1594/2421/files/orbit-3-user-manual.pdf)). Lorenz or Rössler sources routed to wavetable position, spread and cutoff give non-repeating motion for almost no CPU **(inference)**.

Shepard tones sweep octave-spaced partials under a fixed bell-shaped spectral envelope ([Puckette](https://msp.ucsd.edu/techniques/latest/book-html/node72.html)). Risset rhythms do the same with tempo ([QMUL](https://www.qmul.ac.uk/media/news/2012/se/can-you-keep-up-with-the-dance-music-that-accelerates-forever.html)). A supersaw Shepard riser under macro control is a natural build-up generator **(inference)**.

The stiff-string formula f_n = n·f0·√(1 + B·n²) gives a physically grounded "stretch" control ([Russell](https://www.acs.psu.edu/drussell/Demos/Stiffness-Inharmonicity/Stiffness-B.html)). Lossless FDNs with Householder matrices and per-line gain set from T60 make decay an exact, stable macro ([JOS](https://www.dsprelated.com/freebooks/pasp/FDN_Reverberation.html)). Tuning those delays to the played chord gives a resonant "body" shimmer **(inference)**.

### Morph maths for the XY pad

No source was fetched for morph maths, so this whole subsection is **(inference)** from standard results:

- **Bilinear weights.** With the four layers at the corners of the pad, the weights are w = [(1−x)(1−y), x(1−y), (1−x)y, xy]. They sum to 1.
- **Constant-power gains.** Layers in different bands are effectively uncorrelated, so applying √w as layer gain keeps the summed power constant: Σ(√w)² = 1.
- **Perceptual interpolation.** Parameters should interpolate in perceptual domains: log2 for frequency and time, dB for gain, cents for detune.
- **Macro curves.** Macros need per-target curves (power, exponential or monotone cubic) and 5–20 ms smoothing.

## Nord and Yamaha show how a super architecture scales to 8 layers

### Thirty years of hardware settle on four layers, with eight as the ceiling

Nord's synths have always topped out at three or four layers drawing on one shared voice pool:

| Model | Layers or slots | Shared voices |
|---|---|---|
| Lead 4 | 4 slots | 20 |
| Lead A1 | 4 slots | 26 |
| Wave 2 | 4 layers, each with its own oscillators, LFO and effects | 48 |
| Stage 4 (synth section) | 3 layers | 46 |
| Modular G2 | 4 slots | Up to 32 "depending on Patch complexity" |

Sources: [Lead 4 specs](https://www.nordkeyboards.com/legacy-products/nord-lead-4/specifications); [Lead A1 specs](https://www.nordkeyboards.com/products/nord-lead-a1/specifications/); [Wave 2](https://nordkeyboards.com/products/nord-wave-2); [Stage 4 specs](https://www.nordkeyboards.com/products/nord-stage-4/specifications/); [G2](https://www.nordkeyboards.com/node/511).

Yamaha has stacked engines inside a single voice since the 1970s:

- **CS-80:** two complete synth layers per voice ([Wikipedia](https://en.wikipedia.org/wiki/Yamaha_CS-80)).
- **SY77:** two FM elements plus two sample elements per voice, from separate 16-note pools ([Wikipedia](https://en.wikipedia.org/wiki/Yamaha_SY77)).
- **EX5:** sample, analog, physical-model (VL) and FDSP elements mixed in one voice ([Wikipedia](https://en.wikipedia.org/wiki/Yamaha_EX5)).
- **Montage:** 16 Parts per Performance, of which **8 can be under keyboard control at once** as one layered or split sound ([yamahasynth.com](https://yamahasynth.com/?p=2020)).

The Montage M budgets polyphony per engine: 128 notes each for sample (two pools) and FM, but **only 16 for the AN-X virtual-analog engine** ([Yamaha Europe](https://europe.yamaha.com/en/musical-instruments/keyboards/products/synthesizers/montagem/index.html)). In every generation, adding layers cost notes. The CS-80 spent its two layers on 8 voices, the AN1x split its 10 notes into 5 + 5 across two Scenes, and the EX5's analog engine ran only two notes ([Wikipedia AN1x](https://en.wikipedia.org/wiki/Yamaha_AN1x); [Wikipedia EX5](https://en.wikipedia.org/wiki/Yamaha_EX5)).

**Design consequence (inference):** four layers is the proven playable default. Eight is a defensible maximum for one playable sound, following Yamaha's keyboard-control cap. VA and unison layers are the expensive ones, so voices and CPU should be budgeted per layer rather than from one undifferentiated pool.

### Effects follow a three-tier hierarchy: insert, send, master

Yamaha's FX layout has been stable for decades ([Music Matter](https://musicmatter.co.uk/yamaha-montage-8)):

- **Per Part:** two insert effects plus 3-band and 2-band EQ.
- **Shared system sends:** a Variation effect and a Reverb.
- **Master:** a Master Effect plus 5-band EQ.

Nord's Wave 2 gives every layer a full chain of multi-effect, EQ, drive, delay and reverb. That means "16 simultaneously active effects potentially" at four layers, all summed to a single stereo output ([SOS Wave 2](https://www.soundonsound.com/node/4922243?page=2)). The EX5 shows the cost of heterogeneity: using a non-sample engine cut the insert effects from four to one ([Wikipedia](https://en.wikipedia.org/wiki/Yamaha_EX5)).

At eight layers, per-layer reverbs and delays would multiply CPU and wash out the mix. Potato-Synth should therefore keep two inserts plus EQ per layer, put space effects on two shared sends with per-layer send levels, and run the stack-glue master chain once **(inference)**. Every layer type should follow the same FX and voice rules, so that no engine silently disables features elsewhere **(inference)**.

### Many layers must be editable as one

Nord's answer to editing N layers is a set of grouping and overview tools:

- **Group mode** links 2–4 layers so they share filter, envelope, arpeggiator, LFO and effect settings ([Nord Wave 2](https://nordkeyboards.com/products/nord-wave-2)).
- **Layer handling** includes a fader bank for volume or pan of all layers, layer copy, glitch-free layer enable, and one MIDI channel per layer ([SOS Wave 2](https://www.soundonsound.com/node/4922243)).
- **Multi-Focus** on the Lead A1 edits all four slots at once ([Lead A1](https://www.nordkeyboards.com/products/nord-lead-a1/)).
- **Layer Scenes** on the Stage 4 swap the active layer set with one button ([Stage 4](https://www.nordkeyboards.com/products/nord-stage-4)).

Yamaha's equivalents are Quick Edit, which applies offsets to stored values per Part or at Common level ([yamahasynth.com](https://yamahasynth.com/?p=2020)), and eight Scene buttons that change Part levels and add sounds ([MusicTech](https://musictech.com/reviews/yamaha-montage-exclusive-review/)).

**Design consequence (inference):** linked sections in Potato-Synth should do two jobs. In the UI, a linked edit applies to all selected layers, either as an absolute value or as a relative offset that preserves the differences between layers. In the DSP, linked layers can share one filter or FX instance on a bus, which saves CPU. A fixed per-layer panel layout, identical across engines and switched by layer tabs, carries muscle memory from layer to layer.

### Two kinds of morph, and a macro hierarchy above them

Nord and Yamaha morph parameters, not audio.

**Nord Morph** works by demonstration: "hold the Morph button and adjust any desired knob" ([Lead A1](https://www.nordkeyboards.com/products/nord-lead-a1/)).

- On the Wave 2, four sources (velocity, aftertouch, wheel, pedal) can each push most panel parameters across any range in either polarity, and an LED marks each morphed parameter ([SOS Wave 2](https://www.soundonsound.com/node/4922243?page=2)).
- **Impulse Morph** uses three momentary buttons, giving seven combinations, to jump parameters or toggle whole sections ([SOS Lead 4](https://www.soundonsound.com/reviews/clavia-nord-lead-4)).
- The Modular G2 has 8 morph groups and 8 instantly recalled Variations per patch ([G2](https://www.nordkeyboards.com/node/511)).

**The Yamaha AN1x** interpolated between two complete Scene snapshots from the mod wheel. It moved individual parameters rather than crossfading audio, so different oscillator pitches produced a glissando mid-morph, and a mid-morph state could be saved as a new Scene ([SOS](https://www.soundonsound.com/reviews/yamaha-an1x); [Wikipedia](https://en.wikipedia.org/wiki/Yamaha_AN1x)).

**The Montage** generalised this into a macro hierarchy:

1. One **Super Knob** drives 8 Common Assign Knobs, all linked by default.
2. Each Part adds 8 more Assign Knobs.
3. Each Part can reach **16 destinations, each with its own VALUE 1/VALUE 2 range.** Setting VALUE 1 above VALUE 2 inverts the direction ([yamahasynth.com](https://yamahasynth.com/?p=2020)).
4. Up to 8 Motion Sequencer lanes of 16 steps act as ordinary modulation sources and can automate the Super Knob itself ([Motion Sequence Study](https://yamahasynth.com/learn/montage-series-synthesizers/mastering-montage-motion-sequence-study/)).

The pitfall is opacity. Asked how many parameters the Super Knob can control, Yamaha's own Phil Clendeninn answered, "I don't know yet about 'how many' parameters, exactly" ([yamahasynth.com forum](https://yamahasynth.com/community/montage-series-synthesizers/super-knob-1094)).

That gives two distinct morph models. Layer-mix morphing crossfades the audio of parallel layers (Omnisphere Quadzone, Wavestate's joystick). Parameter morphing interpolates snapshots (AN1x, Nord Morph, G2 Variations). **Potato-Synth should offer both (inference):**

- **XY Mix (default):** the √-weighted layer crossfade from the section above. It is cheap, glitch-free and always in tune.
- **XY Snapshot:** each corner stores a whole-stack snapshot, and parameters interpolate in perceptual domains. Discrete parameters such as waveform or filter type snap at the midpoint or crossfade dual instances, and pitch glides are optional. "Capture this point as a new corner" works the way AN1x mid-morph capture did.

Above the 8 global macros sits a **Super** control that moves all eight along a stored path and is linked by default. Two momentary **Impulse** buttons (for example BUILD and DROP) follow Nord's model. Assignment works by range-by-demonstration: hold a macro and turn any knob. A visible macro map shows every target and range, which avoids the Super Knob's "how many?" problem.

### Oscillator and voice lessons

Nord reduces each oscillator to a waveform plus one context-dependent control. The Lead A1's Osc Config knob has 11 categories, including Detune, Sub Mix, Shape, Sync and FM, all set with one Adjust knob ([Lead A1 specs](https://www.nordkeyboards.com/products/nord-lead-a1/specifications/)). The Wave 2's "Super" category offers JP-8000-style supersaws, and its 3-way unison "doesn't steal polyphony" ([SOS Wave 2](https://www.soundonsound.com/node/4922243)).

SOS's Wave 2 criticisms are a checklist for Potato-Synth:

- Random start phase causes phase cancellation on repeated notes, and there is no key-reset option.
- There is no stereo voice spread and no analog slop.
- Program changes cut the sound.
- All layers sum to a single stereo output.

Source: [SOS Wave 2](https://www.soundonsound.com/node/4922243?page=2). The role-based phase defaults, unison spread, drift and stack bus above already answer the first two. Seamless patch switching, with tails carried over, and per-layer outputs would answer the last two **(inference)**.

Yamaha's FM-X gives each operator a "spectral" waveform (All, Odd or Res) with a Skirt (0–7) that low-passes the overtones ([Manny's FM-Xplorations](https://yamahasynth.com/?p=1665)). That is cheap density from a single operator, and Skirt is a natural TONE macro target **(inference)**. The AN-X engine includes "voltage drift and ageing" ([SOS](https://www.soundonsound.com/node/4931104)). Like Arturia and TAL, Yamaha treats per-voice instability as core to big VA.

Neither Nord nor Yamaha documents automatic gain compensation when layers are stacked, so loudness-managed stacking remains open ground **(inference from absence)**.

### The super architecture, summarised

| Tier | Potato-Synth spec | Precedent |
|---|---|---|
| Stack | 4 layers default, expandable to 8; voices and unison budgeted per layer; idle-layer bypass | Nord 4 slots; Montage 8 under keyboard control; Montage M per-engine pools |
| Layer | Any engine; fixed panel; Type → Wave → Ctrl → Unison; 2 inserts + EQ; per-layer arp; key/velocity zones with crossfades; trigger conditions (legato, key-off, cycle, random) | Nord Osc Config, Wave 2 per-layer arp and splits; Motif XS XA conditions ([SOS](https://www.soundonsound.com/reviews/yamaha-motif-xs7)) |
| Link | Group/Link sections (absolute or relative), shared bus instances, layer copy, Multi-Focus | Nord Group mode and Multi-Focus; Yamaha Quick Edit |
| Morph | XY Mix (layer crossfade) and XY Snapshot (parameter interpolation), capture point as corner, Scenes | Quadzone/Wavestate; AN1x Scenes; G2 Variations; Stage 4 Layer Scenes |
| Macro | Super control → 8 global macros → optional per-layer macros → destinations with min/max ranges; hold-to-assign; Impulse buttons; step lanes as sources; visible map | Montage Super Knob and Motion Sequencer; Nord Morph and Impulse Morph |
| Bus | 2 shared sends (space), stack-glue master chain, optional per-layer outputs | Yamaha system and master FX; Nord's single-output weakness |

The precedents are sourced. The combination is **(inference)**.

## Design recommendations for a huge-by-default Potato-Synth

### Priority 0: engines

| Priority | Engine | Core design | Basis |
|---|---|---|---|
| P0 | **Supersaw/VA ("JP")** | Two oscillators in Jupiter topology (cross-mod 2→1, sync, VCO-2 as LFO, invertible ENV-1 to pitch, PWM and filter). JP-6 waveform summing. JP-8080-style algorithm menu with two controls each. Unison modes "JP" (integer table, linear side gain, internal key-tracked HPF, 2× oversampling) and "Modern" (up to 16 voices, power-curve spread, constant-power blend, SIMD lanes) | Topology sourced ([Roland](https://support.roland.com/hc/en-us/articles/201945109-Jupiter-8-Technical-Specifications); [ÜBERSAW](https://github.com/house-of-houses/ubersaw/blob/master/docs/algorithm.md); [Vital](https://github.com/mtytel/vital/blob/main/src/synthesis/producers/synth_oscillator.cpp)); combination is inference |
| P0 | **Sub** | Sine or triangle, mono, phase retrigger, 20–30 Hz HPF, "translate" harmonics 2–5 via asymmetric shaper or Chebyshev, low-passed at about 4×f0 | Virtual-bass parameters sourced ([DAFx2020](https://acris.aalto.fi/ws/portalfiles/portal/51973173/Eloi2020_Virtual_bass_DAFx2020.pdf)); engine is inference |
| P0 | **Wavetable** | Per-pitch IFFT band-limiting every few ms with buffer crossfade, Catmull-Rom read, phase warps with on-demand oversampling, frame and warp spread across unison | Vital architecture sourced ([wavetable.cpp](https://github.com/mtytel/vital/blob/main/src/synthesis/lookups/wavetable.cpp)) |
| P1 | **Spectral morph** | Harmonic-bin operations (tilt n^−s, odd/even, stiff stretch, formant envelope, smear, Shepard, log-domain morph A↔B), rendered by IFFT to a table; spectral unison off by default; rotor bank only for inharmonic or per-partial pan | Bin-op list and IFFT cost sourced ([spectral_morph.h](https://github.com/mtytel/vital/blob/main/src/synthesis/producers/spectral_morph.h); [JOS](https://ccrma.stanford.edu/%7ejos/sasp/Inverse_FFT_Synthesis.html)); maths is inference |
| P1 | **Dirt (analog body)** | Morphing shape knob, curved saw reset, −1/−2 octave subs, OTA drive into a 2/3-pole filter with low-end-preserving resonance, paraphonic mode, auto-tune re-roll | Character sourced from reviews; DSP is inference |
| P2 | Noise/air, FM/feedback, Feedback-comb oscillator, resonator body (FDN/modal), granular | Each is cheap; granular follows the Omnisphere precedent | Inference |

The init patch should load all four core roles: Sub, Dirt body, Supersaw mid and Spectral/air top. The XY centre would then be the full stack, and each corner would solo-emphasise one role **(inference)**.

### Priority 0: default signal chain

Each layer runs: engine (with any internal key-tracked HPF), then a zero-delay-feedback filter (Simper SVF, or an OTA/ladder model at 2×), then parallel ADAA drive at 2×, then a role band-limit using LR4 high- and low-pass, then amp, then an insert (chorus only on upper roles). The stack bus then applies, in order:

1. XY √-weights.
2. A mono-bass stage (LR4 at about 120 Hz, side signal zeroed below it).
3. Inter-layer ducking.
4. OTT-style three-band up/down compression at 88/2500 Hz, depth about 30%.
5. Glue compression at about 2:1 for 1–4 dB of gain reduction.
6. Frequency-dependent width (β low below 250 Hz, β high above).
7. Dattorro plate or FDN space with pre-delay, low-cut, dry-keyed ducking and envelope-gated mix.
8. Auto-gain from an internal BS.1770 meter.
9. An optional 4× true-peak-safe soft clipper or limiter.

The building blocks are sourced (see sections above). The order and defaults are **(inference)**. The JUCE stock `Limiter` should not be used for the last stage, because it lacks lookahead and true-peak detection ([JUCE](https://docs.juce.com/master/namespacejuce_1_1dsp.html)).

| Role default | Band | Phase/stereo | Unison | Basis |
|---|---|---|---|---|
| Sub | LP about 100–120 Hz, HP 20–30 Hz | Mono, retrigger | Off | [iZotope](https://www.izotope.com/en/learn/how-to-program-and-layer-electronic-basslines-in-your-daw?page=6); [Attack](https://www.attackmagazine.com/technique/synth-secrets/layering-synths/) |
| Body | HP about 100–130 Hz, LP about 3 kHz | Mono or narrow, retrigger | 2 voices about ±25–30 cents (reese) | [Attack reese](https://www.attackmagazine.com/technique/tutorials/reese-bass-redux/); [Future Proof](https://futureproofmusicschool.com/blog/reese-bass-sound-design-everything-you-need-to-know) |
| Mid | About 300 Hz–3 kHz, drive here | Random phase, spread | 7 voices about 20% plus −1 octave at 3 voices | [FaderPro](https://blog.faderpro.com/techniques/supersaw-how-make-iconic-sound/) |
| Air | HP about 2–3 kHz | Widest; chorus and reverb here | 7–16 voices, 10–15% noise | [Attack](https://www.attackmagazine.com/technique/synth-secrets/layering-synths/); [Syntorial](https://www.syntorial.com/preset-recipe/martin-garrix-animal-lead/) |

### Priority 1: stack-glue features

These features are what make four engines sound like one instrument. All are **(inference)** unless cited.

- **Role tags** set band, phase, unison and width defaults.
- **Two global crossover knobs** default to 120 Hz and 2.5 kHz.
- **Anchored layers.** A layer such as the sub can be excluded from XY morphing, so moving the pad never removes the foundation.
- **Loudness-compensated XY morphing** combines √-weights with a slow internal LUFS-matched trim, which no surveyed product documents.
- **Sub/body phase alignment** offers automatic polarity and sample-offset alignment at the crossover.
- **Internal sidechains:** the sub ducks the body's lows, and the transient layer ducks the pad onset by 10–50 ms, following the masking data above.
- **A shared drift seed and Dispersion/Age control** span all layers, with per-voice seeds that keep each voice's personality, as hardware does ([Arturia](https://support.arturia.com/hc/en-us/articles/4405740988690-Jup-8-V4-General-Questions)).
- **Unison gain law:** the 1/√N law, with a coherence correction when phases are retriggered.
- **Per-layer presets and per-layer dice/mutate** answer the Pigments complaint and follow the Wavestate and Omnisphere precedents.
- **An FX/arp "lock"** while browsing follows Omnisphere's Sound Lock ([SOS 2015](https://www.soundonsound.com/reviews/spectrasonics-omnisphere-2)).
- **Full automation of every parameter** avoids the Massive X pitfall.
- **A vector envelope or recorder** animates the XY position (Wavestate, and Orb motion recording).
- **Group/Link sections** edit all selected layers absolutely or relatively and can share one DSP instance (Nord Group mode).
- **A layer fader bank and Layer Scenes** rebalance or swap the active layers in one action (Nord Wave 2 and Stage 4, Montage Scenes).
- **Seamless patch and scene switching** carries tails over, fixing a named Nord Wave 2 weakness ([SOS](https://www.soundonsound.com/node/4922243)).

### Priority 1: macro mappings

| Macro | Primary targets (curve) | Why |
|---|---|---|
| 1 SIZE | Unison voices and detune on Mid/Air (power curve, register-scaled cents), stack interval, chorus depth, reverb size | Spreads energy across ERBs (loudness summation) |
| 2 WIDTH | High-band β, side gain above crossover, unison stereo spread; never below 120 Hz | IACC in 500 Hz–2 kHz drives width |
| 3 DIRT | Parallel drive mix per layer (strongest on Body/Mid), OTA drive on Dirt engine, even/odd asymmetry | Density and crest-factor reduction |
| 4 TONE | Global tilt (filter cutoffs in log2, spectral tilt s), Air layer level | Centroid equals brightness |
| 5 PUNCH | Pitch-env transient depth (up to +3 oct, 10 ms), amp attack toward 0 ms, transient shaper, OTT Time | Fenton onset term |
| 6 MOTION | Drift σ, LFO and chaos depths on WT position, spread and cutoff; Kuramoto K inverted ("Focus") | Slow beating equals richness |
| 7 SPACE | Reverb/delay mix with pre-delay, ducking amount rising with mix | Late decorrelated energy equals envelopment |
| 8 SQUASH | OTT depth (about 20–60%), glue gain reduction, auto-gain held | OTT practice ([EDMProd](https://edmprod.com/ott-plugin)) |

Eight macros matches Serum 2, Phase Plant, Wavestate and the Montage's 8 Common Assign Knobs ([Xfer](https://static.xferrecords.com/Serum%202%20What's%20New.pdf); [Kilohearts](https://kilohearts.com/docs/phase_plant); [Korg](https://www.korg.com/us/products/synthesizers/wavestate/); [yamahasynth.com](https://yamahasynth.com/?p=2020)). The mappings themselves are **(inference)**. Every macro output should be smoothed and itself modulatable, as in Serum 2.

Three controls sit above the eight macros:

- **SUPER**, linked by default. It sweeps all eight macros along a stored path, for example from verse to drop, and step lanes can automate it.
- **BUILD**, a momentary Impulse button that raises SIZE and SPACE and starts a Shepard riser.
- **DROP**, a momentary Impulse button that kills reverb, opens PUNCH and adds a −1 octave layer.

Macros are assigned Nord-style by holding a macro and turning a knob, with an LED-style indicator on every target **(inference from the Montage and Nord precedents)**.

### Priority 1: CPU budget

The only kernel costs available are the Välimäki op counts ([JASA 2012](https://mac.kaist.ac.kr/pubs/ValimakiPeknenNam-jasa2012.pdf)) and the research team's unpublished in-session micro-benchmark (AVX2 Xeon at 2.1 GHz). That benchmark measured:

- **0.0024% of a core** per PolyBLEP saw lane
- **0.0054%** per cubic wavetable lane
- **0.012% per channel** per 2:1 half-band decimation stage

Production code runs 10–30× above such micro-benchmarks once modulation, filters and cache pressure are added.

| Block (8-note chord, 48 kHz) | Kernel estimate | Planning allocation of a ≤20% single-core target | Lever |
|---|---|---|---|
| Supersaw layer, 2 osc × 16 lanes × 8 notes (256 lanes), 2× | About 1.2% | 4% | Bus chorus instead of more voices ([Xfer](https://support.xferrecords.com/article/51-serum2-sound-design-guidelines-for-optimizing-cpu-usage)) |
| Wavetable layer, 16 unison, shared IFFT buffer | About 2.5% | 4% | Shared buffer per voice |
| Spectral morph, spectral unison off / on | About 2.5% / about 10% | 5% | Spectral unison opt-in only |
| Sub plus Dirt | Under 0.5% plus drive | 2% | Mono voices, 2× only on drive |
| Per-voice filters and drive | Not measured | 3% | Per-block coefficient updates, capped NR iterations |
| Stack bus (OTT, glue, width, plate) | Small | 2% | Bus, not per voice |

The allocations and the 20% target are **(inference)** and must be replaced by profiling. The structural levers are better supported:

- **Idle-layer bypass** when a layer's XY weight is near zero. Omnisphere's inactive layers use no CPU ([v1 manual](https://support.spectrasonics.net/manual/Omnisphere/main/all.htm)).
- **Per-layer voice caps.**
- **On-demand oversampling** per oscillator.
- **A global Draft/Normal/HQ quality setting.** Vital users specifically ask for a global rather than per-patch setting ([Vital forum](https://forum.vital.audio/t/cpu-optimization-for-vital/4810?page=4)).

Expanding to 8 layers roughly doubles the layer rows. The 20% target holds only if layers 5–8 are mostly lighter roles (sub, noise, FM-style spectral operators) and space effects stay on shared sends. The plugin should show a live per-layer voice and CPU meter, echoing the G2's "depending on Patch complexity" polyphony ([G2](https://www.nordkeyboards.com/node/511)), and per-engine voice pools in the Montage M style should stop a heavy VA layer from stealing voices from light ones **(inference)**.

### Build order

1. **P0:** the Supersaw/VA, Sub and Wavetable engines; the role-tagged band-limit, mono-bass and auto-gain chain; the XY pad with √-weights and anchoring; and the eight macros.
2. **P1:** the Spectral and Dirt engines, OTT and glue, frequency-dependent width, the plate reverb, true-peak output, per-layer presets and dice, Group/Link, the SUPER control and Impulse buttons, and two inserts plus two shared sends.
3. **P2:** expansion to 8 layers with per-layer voice pools and a CPU meter, XY Snapshot morphing, step lanes, Kuramoto Focus, chaos sources, the Shepard riser, the FDN resonant body, granular, and a transparent Orb-style scene ring.

The evidence base is thinnest in a few places, and these need listening tests or primary sources before they are locked in:

- the supersaw HPF order and cutoff
- the inter-layer phase-alignment thresholds
- the Dreadbox circuit behaviour
- perceptual validation of irrational detune
- real production CPU figures

## Conclusion

The research changes the design question. "How do we make each engine bigger?" becomes "how do we make four engines share the ear's resources without fighting?" Loudness summation, upward masking, frequency-dependent IACC and the punch model all favour the same architecture. Each layer owns a band, the lows are coherent and fast, and the mids and highs are decorrelated and slowly beating. That architecture can be enforced by defaults rather than left to user skill. That is the real meaning of "huge by default": the synth ships the producer's layering discipline as its starting state. The supersaw's internals are now public through the 39C3 work, so the "JP" sound is a solved component rather than a differentiator. The opportunity lies in the stack-glue layer that no surveyed competitor documents: loudness-compensated 2D morphing, anchored layers, role-aware width and mono-bass, and inter-layer ducking.

Potato-Synth's identity comes from combining two characters. A clean, wide JP-style top is set against a driven, mono Dreadbox-style body, on a transparent Jupiter-topology base with explicit multipliers. That pairing is something neither Omnisphere's laid-back, preset-centred approach nor Serum's single-voice wavetable focus offers. Nord and Yamaha add a warning from forty years of hardware: every extra layer costs polyphony, editing clarity or both. An 8-layer "super architecture" works only with shared sends, linked editing, per-layer budgets and a macro map the user can actually see. The biggest risk is not sound quality but CPU and coherence. Spectral unison, per-partial unison and per-voice width effects are the cost traps, and phase interaction at the sub/body crossover is the coherence trap. Both should be designed out at the architecture stage, not patched later.
