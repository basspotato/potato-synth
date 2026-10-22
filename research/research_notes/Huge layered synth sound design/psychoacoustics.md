# Psychoacoustics of "Huge" Synth Sounds (loudness, size, width, depth, power)

Scope: the perceptual mechanisms behind sounds heard as loud, big, wide, deep and powerful, with numbers a DSP designer can use for an EDM synth that is "huge by default". Each finding carries an inline source. Values I knew from training but could not confirm in a fetched source are under Gaps, marked "unverified".

## Loudness: equal-loudness contours, critical bands/ERB, loudness summation, LUFS/K-weighting

### Takeaway
At the same energy, a sound gets louder once its energy spreads past one critical band. Loudness also climbs steeply with level in the low bass, where hearing thresholds are high. So the quickest way to make a synth sound "bigger" without raising peak or RMS is to spread energy across many critical bands (or ERBs): unison, harmonics, saturation, and noise or air layers. ISO 226:2023 is practically the same as the 2003 contours, so either works as a design reference.

### Cited Findings
- **ISO 226:2023 vs 2003.** The two editions differ by 0.6 dB at most and are "practically equivalent". The 20 Hz threshold was lowered by 0.4 dB to match ISO 389-7:2019, the loudness power exponent was updated, and the formulas were rewritten with appropriate significant digits. — [Acoustical Science and Technology 45(1) e23.66](https://www.jstage.jst.go.jp/article/ast/45/1/45_e23.66/_article/-char/en)
- **Critical bandwidth.** About 150 Hz at a 1 kHz centre and about 800 Hz at 8 kHz. Each critical band covers about 1.3 mm of basilar membrane. Within one critical band, sounds of equal total energy are equally loud. — [NYU Perception lecture notes (Heeger)](https://www.cns.nyu.edu/~david/courses/perception/lecturenotes/loudness/loudness.html)
- **Loudness summation (Zwicker).** Band-limited noise centred at 1 kHz was widened from 20 Hz bandwidth with total energy held constant. Loudness stayed the same up to about the critical bandwidth, then grew as the bandwidth went beyond it. — [NYU lecture notes](https://www.cns.nyu.edu/~david/courses/perception/lecturenotes/loudness/loudness.html)
- **Loudness scale.** A 100 dB level difference (rustling leaves at 20 dB vs a rock band at 120 dB) is about 100,000x in amplitude but only about 1,000x in perceived loudness (sones). — [NYU lecture notes](https://www.cns.nyu.edu/~david/courses/perception/lecturenotes/loudness/loudness.html)
- **ERB-number (Cam) scale, Glasberg & Moore (1990).** ERBS = A·log10(1 + 0.00437·f), with A = 1000·ln(10)/(24.7·4.37) ≈ 21.33. — [MathWorks hz2erb](https://it.mathworks.com/help/audio/ref/hz2erb.html). The approximation holds at moderate levels for 100 Hz to 10 kHz. ERBS(f) counts the ERBs below f, in units called "Cams". — [Wikipedia: Equivalent rectangular bandwidth](https://en.wikipedia.org/wiki/Equivalent_rectangular_bandwidth)
- **Low-frequency hearing.** At low frequencies, loudness grows much faster above threshold than at mid frequencies: "a slight increase in level can change the perceived loudness from barely audible to loud". Individual thresholds vary with a standard deviation of about 5 dB, possibly a little more at 20–50 Hz. Standards disagree by at most 3.9 dB, at 20 Hz. The threshold slope in the infrasound region is about 12 dB/octave, matching G-weighting. — [Møller & Pedersen 2004, "Hearing at low and infrasonic frequencies"](https://vbn.aau.dk/ws/portalfiles/portal/89856984/Moeller_and_Pedersen_2004.pdf)
- **K-weighting (BS.1770).** It is broadly similar to A-weighting but "adds a shelf boost above 2kHz" and is integral to BS.1770 loudness measurement. — [Sound On Sound glossary](https://www.soundonsound.com/node/4921525). Stage 1 is a high shelf that approximates the acoustic effect of the head; stage 2 is a high-pass that de-weights the lowest frequencies. Common targets are about −14 LUFS (streaming), −23 LUFS (EBU R128) and −24 (ATSC). — [Fora Soft glossary](https://www.forasoft.com/learn/audio-for-video/glossary/terms-audio/k-weighting)

### Inferences
- From the ERB-number constants (24.7, 4.37), the bandwidth form is ERB(f) ≈ 24.7·(4.37·f/1000 + 1) Hz. That gives about 35 Hz at 100 Hz, about 133 Hz at 1 kHz and about 456 Hz at 4 kHz. Use it to size unison spreads and layer spacing.
- The summation result means a supersaw with N detuned voices, or a saw with saturation harmonics, sounds louder than a sine or single saw at the same RMS, because its energy covers more ERBs. "Huge by default" presets can therefore be loud without clipping.
- LUFS meters use K-weighting, which de-weights the lowest bass and adds a shelf above 2 kHz. Heavy sub energy uses up headroom while adding comparatively little LUFS. Upper-mid and high harmonic energy adds LUFS efficiently. An "output loudness-match" or "auto-gain" control can use a BS.1770 meter internally.
- Below about 100 Hz the loudness contours are compressed (loudness grows fast per dB). Small level changes in the sub therefore produce big perceived changes, so sub-level and drive controls want fine resolution in that region.

### Gaps
- Unverified (training knowledge, not confirmed in a fetched source): the BS.1770 K-filter coefficients (stage 1 high shelf of about +4 dB around 1.5 kHz; stage 2 RLB high-pass at about 38 Hz), 400 ms momentary and 3 s short-term windows, gating at −70 LUFS absolute and −10 LU relative, and the 4x oversampled true-peak measure. Check against the ITU-R BS.1770-5 PDF.
- ISO 226 threshold values at specific frequencies (for example about 78 dB SPL at 20 Hz and about 22 dB at 125 Hz) were not available in the sources fetched. The standard is paywalled.
- I found no exact Bark formula or Zwicker critical-band table in the fetched sources. Below 500 Hz, critical bandwidth is about 100 Hz (Zwicker), but that figure is unverified here.

## Masking: simultaneous, temporal, upward spread; implications for layering

### Takeaway
Masking spreads upward in frequency, more so at high levels, and lasts in time for up to about 200 ms after a masker (forward) and about 25 ms before it (backward). A loud sub or bass layer therefore buries upper layers far more than the reverse. To be heard separately, layers need either more than about one critical band of separation or different temporal envelopes.

### Cited Findings
- **Forward masking.** Masking grows as masker duration rises from 20 to 200 ms; longer maskers add nothing. The effect is strongest for gaps up to about 50 ms and fades by about 200 ms. A 10 dB increase in masker level raises masking by only about 3 dB (compressive). — [Hearing Health Matters, "Unmasking auditory temporal masking"](https://hearinghealthmatters.org/pathways-society/2015/unmasking-auditory-temporal-masking/)
- **Backward masking.** It falls sharply as the gap grows to about 25 ms and is gone by about 100 ms. Masker duration has no effect. — [Hearing Health Matters](https://hearinghealthmatters.org/pathways-society/2015/unmasking-auditory-temporal-masking/)
- Low-frequency binaural signals are more correlated and harder to localise, and low-frequency energy seems to come from all around. — [Skålevik, IACC(t) paper](https://www.akutek.info/Papers/MS_iacc-t_London-2017.pdf). This is relevant to how sub layers merge.

### Inferences
- Layering rule: two layers whose dominant partials fall in the same ERB fuse and partly mask each other. Separating them by at least one ERB (about 35 Hz at 100 Hz, about 130 Hz at 1 kHz) makes them additive in loudness, per the summation finding above, rather than competing.
- Because of forward masking, a loud transient or pluck layer can mask the first tens of ms of a pad's onset. Offset or envelope-duck layers by roughly 10–50 ms to let the attack read.
- Because spread is upward, high-passing non-bass layers (to keep their energy out of the sub's band) gives the bass layer room; it does not improve the audibility of the highs.

### Gaps
- Unverified: masking spreading slopes. Commonly quoted values are about +25–27 dB/Bark on the low side and about −5 to −24 dB/Bark on the high side, level-dependent (Terhardt and Zwicker models). The auditory.org page I fetched had no content, and I found no primary source for these slopes.
- I found no study giving a quantified minimum layer separation for synth layers. The ERB-based rule above is an inference.

## Width and spaciousness: IACC, ASW, LEV, precedence/Haas, decorrelation, mono compatibility

### Takeaway
Width is driven by low interaural (or inter-channel) correlation, especially in early energy (ASW) and late, sustained energy (envelopment). Inter-channel delays under about 1 ms shift or blur the image. About 1–10 ms gives a fused but widened image, with the lead channel dominating localisation. Echoes split off from about 5 ms for clicks up to 15–30 ms or more for sustained or repeated tones. Low frequencies should stay coherent: decorrelate only above about 100–250 Hz.

### Cited Findings
- **IACC.** Ranges from −1 to +1. Lower IACC means a broader image. ASW is tied to early lateral energy (1−IACC_E, first 80 ms) and listener envelopment (LEV) to late energy (1−IACC_L, after 80 ms). IACC3 is the mean of the 500 Hz, 1 kHz and 2 kHz octave bands. Typical hall averages are IACC_E3 = 0.39 and IACC_L3 = 0.15. — [Skålevik, IACC(t), London 2017](https://www.akutek.info/Papers/MS_iacc-t_London-2017.pdf)
- **Frequency dependence of IACC.** For a pure tone from the side (anechoic), IACC is −0.92 at 1 kHz, −0.04 at 500 Hz, 0.37 at 250 Hz and 0.71 at 125 Hz. Low frequencies stay correlated by nature, and "low frequency reflections contribute to ASW and LEV even if non-lateral". — [Skålevik 2017](https://www.akutek.info/Papers/MS_iacc-t_London-2017.pdf)
- **Precedence (summing localisation).** Lead and lag fuse into an image between them below about 1 ms, especially below 0.5 ms. — [Brown, Stecker & Tollin 2015, JARO review (PMC4310855)](https://pmc.ncbi.nlm.nih.gov/articles/PMC4310855)
- **Localisation dominance.** For broadband stimuli the lead dominates localisation up to about 10 ms. Dominance is weak or absent for narrowband stimuli. — [PMC4310855](https://pmc.ncbi.nlm.nih.gov/articles/PMC4310855)
- **Echo threshold.** Across studies it ranges from 2 to over 100 ms. For clicks it is about 5 ms (4–7 ms for 1 ms noise bursts). For 500 ms piano tones it is 7–8 ms with equal-level lag and 15–26 ms with realistically attenuated lag. With repeated stimuli it builds up from 5–10 ms to 15–30 ms or more. — [PMC4310855](https://pmc.ncbi.nlm.nih.gov/articles/PMC4310855)
- **Decorrelator designs (DAFx24).** Velvet-noise sequences of 15–30 ms. Allpass cascades of about 200–250 random-phase biquads with up to 30 ms group delay. Allpass decorrelates best below 1 kHz but has coherence peaks at 2–8 kHz. Velvet noise is better at 2–8 kHz but poor at low frequencies. Output inter-channel coherence ≈ cos²(β)·input coherence when mixing dry with decorrelated signal at angle β (0 to π/2). Allpass at full width caused audible "chirping" (low frequencies delayed more) on drum-and-bass transients, and transient handling reduced it significantly. Short velvet-noise sequences cause comb filtering. The paper recommends βlow < βhigh, using a 250 Hz Linkwitz-Riley crossover (plugin range 100 Hz–4 kHz). — [DAFx24 paper 92, frequency-dependent decorrelating widener](https://dafx.de/paper-archive/2024/papers/DAFx24_paper_92.pdf)
- A standard stereo setup with speakers at ±30° gives a perceived width of 60°. — [DAFx24 paper 92](https://dafx.de/paper-archive/2024/papers/DAFx24_paper_92.pdf)

### Inferences
- Haas-style widening (L/R delay): 1–10 ms gives a fused, widened image. Under about 1 ms it mostly shifts the image. Above about 15–30 ms on sustained synths (5 ms on clicks or plucks) it risks an audible echo. In mono, a delay of d ms produces comb notches at odd multiples of 1/(2d) kHz (for example a first notch at 50 Hz for 10 ms), so Haas widening is poorly mono-compatible.
- "Huge by default" width: keep content below about 120–250 Hz mono or highly correlated, and decorrelate the mids and highs (where IACC actually affects width, 500 Hz–2 kHz and up). Unison voices with independent detune and phase per channel are a natural decorrelator that sums to mono without static comb notches, because the beating moves.
- A sensible design target is an inter-channel correlation of roughly 0.2–0.6 for wide pads, in line with concert-hall IACC_E values around 0.4. This is an extrapolation from hall acoustics to stereo mixes, not a mix-specific standard.

### Gaps
- No primary source fetched for the JND of IACC (commonly quoted as about 0.075, unverified) or for quantitative mono-compatibility metrics (for example, correlation meters staying above 0).
- The level tolerance of the Haas effect (that the lag can be about 10 dB louder before it is heard as the source) was not supported by the fetched review. Unverified.

## Depth and low end: missing fundamental, tactile sub, sensitivity at 30–120 Hz

### Takeaway
On small speakers, harmonics of a bass note recreate its pitch through virtual pitch. Small speakers typically cut off at about 100–200 Hz, so virtual-bass generators synthesize harmonics 2 to K+1 in the band fc to 4fc at about +8 to +12 dB. On large systems, the body feels sub energy about 20–25 dB above the hearing threshold, and 40–80 Hz vibration measurably improves perceived music quality.

### Cited Findings
- **Virtual bass.** Typical small-loudspeaker cutoff fc is about 100–200 Hz. Harmonics are generated starting from the order that lands above fc (2..K+1 if f0 is in [fc/2, fc]; 3..K+2 if in [fc/3, fc/2]; and so on), band-passed fc–4fc, with gain of typically 8–12 dB. Harmonic weights are bounded between −10 dB/oct and −3 dB/oct envelopes. — [Moliner et al., DAFx2020 virtual bass](https://acris.aalto.fi/ws/portalfiles/portal/51973173/Eloi2020_Virtual_bass_DAFx2020.pdf)
- **Half-wave rectification** produces only even harmonics, so the virtual pitch can jump an octave (heard at 2f0). An odd-harmonic generator (FEXP1) is added to complete the series. — [DAFx2020](https://acris.aalto.fi/ws/portalfiles/portal/51973173/Eloi2020_Virtual_bass_DAFx2020.pdf)
- **NLD vs phase vocoder.** Nonlinear devices (NLDs) suffer intermodulation distortion on tonal material; phase vocoders smear transients. The hybrid (NLD for transients, PV for tonal) scored 47 vs 29.4 (NLD only) in MUSHRA (reference 98.6). — [DAFx2020](https://acris.aalto.fi/ws/portalfiles/portal/51973173/Eloi2020_Virtual_bass_DAFx2020.pdf)
- **Tactile perception.** Whole-body vibration is felt about 20–25 dB above the hearing threshold, mainly in the lumbar, buttock, thigh and calf regions (Landström et al. 1983). Below about 10 Hz, pressure is felt at the eardrums. — [Møller & Pedersen 2004](https://vbn.aau.dk/ws/portalfiles/portal/89856984/Moeller_and_Pedersen_2004.pdf)
- **Vibration and perceived quality.** Adding substitute vibration to music raised quality ratings by +29 points (out of 100) at 40 Hz and +18 for 100 Hz-lowpassed noise and for 160 Hz. 20 Hz gave no significant improvement. 40 Hz and 80 Hz amplitude-modulated sines performed best. — [Merchel & Altinsoy, "Tactile perception of music" (ICMEM 2015)](https://dhi.ac.uk/books/icmem2015/tactile-perception-of-music)
- **Low-frequency loudness growth.** Loudness grows faster above threshold at low frequencies. Below 20 Hz, a sound only a few dB above threshold can seem intense. — [Møller & Pedersen 2004](https://vbn.aau.dk/ws/portalfiles/portal/89856984/Moeller_and_Pedersen_2004.pdf)

### Inferences
- A "sub harmonics" or "translate" control could blend 2nd–5th harmonics of the sub oscillator, generated with an odd+even NLD such as asymmetric soft-clipping, at roughly +8 to +12 dB relative to the band above 100–150 Hz. Low-passing the result at about 4x the note's fundamental keeps it from sounding like fizz. This is how a sub "translates" to phones and laptops.
- The most useful "felt" region for a club sub appears to be about 40–80 Hz. 20 Hz content added little in the vibration study and costs a lot of headroom, so a default high-pass around 20–30 Hz on sub layers is reasonable.
- Loudness grows steeply in the bass, so sub level differences of 1–2 dB are perceptually large. Use fine-grained sub level and drive parameters.

### Gaps
- Exact ISO 226 sensitivity figures for 30–120 Hz were not obtainable (paywalled and only in figures). Unverified: the threshold is about 60 dB SPL at 30 Hz and about 45 dB at 60 Hz falling to about 25 dB at 125 Hz, and the 40-phon contour needs about 30 dB more SPL at 30 Hz than at 1 kHz.
- I found no source giving how many harmonics are needed for a robust virtual pitch (the classical literature indicates resolved harmonics 2–5 dominate, but this is unverified here).

## Beating and chorus: detuned unison, roughness (Plomp & Levelt), richness vs dissonance

### Takeaway
Beating slower than about 15 Hz reads as a pleasant loudness fluctuation ("movement" or "richness"). Faster beating becomes roughness, which peaks when two partials are about 25% of a critical band apart and disappears at about one full critical band (or about 75–150 Hz fluctuation rate). Detune amounts in unison should therefore be set relative to the ERB at each partial's frequency.

### Cited Findings
- **Maximal dissonance.** Plomp & Levelt (1965) found that "maximal tonal dissonance is produced by intervals subtending 25% of the critical bandwidth". Maximal consonance is reached at about 100% of the critical bandwidth. The Helmholtz constant of 30–40 Hz for maximal roughness only holds between about 500 and 1000 Hz. — [Plomp & Levelt 1965, JASA](https://www.math.miami.edu/~armstrong/592sp15/Plomp_Levelt_1965.pdf)
- **Low-register dissonance.** For complex tones, intervals become more dissonant below a critical frequency. Octaves have zero dissonance above about 100 Hz. Strong upper partials increase sharpness. — [Plomp & Levelt 1965](https://www.math.miami.edu/~armstrong/592sp15/Plomp_Levelt_1965.pdf)
- **Most dissonant interval by register.** It is somewhat less than a semitone in the treble, about 1.5 semitones at middle C, and roughly doubles per octave downward. — [Moore, "Modern psycho-acoustics" summary](https://www.mooremusic.org.uk/schenk/aco3.htm)
- **Beating, roughness and separate tones.** Beating is at or below about 15 fluctuations/s and is heard as loudness fluctuation. Roughness lies above that and vanishes at about 75–150 fluctuations/s depending on frequency. Beyond the critical bandwidth, partials are heard as smooth separate tones. Equal-amplitude components give maximal fluctuation and roughness. — [Vassilakis, acousticslab.org](https://acousticslab.org/learnmoresra/moreroughness.html)

### Inferences
- For a supersaw at fundamental f with partial n at n·f, a detune of c cents gives a beat rate of about n·f·(2^(c/1200)−1). For example, A2 = 110 Hz detuned 10 cents beats at about 0.64 Hz on the fundamental, about 6.4 Hz on harmonic 10 and about 32 Hz on harmonic 50 (5.5 kHz, ERB about 625 Hz, so well under 25% of the ERB and not rough). The upper partials beat fast, but relative to their (wide) ERBs they stay in the "shimmer" region, which explains why moderate detune sounds rich rather than dissonant.
- Danger zone: in the bass (fundamental below about 200 Hz), ERBs are only about 35–50 Hz. Wide detune there pushes fundamental beats toward roughness and also cancels or phase-modulates the sub. Bass-range unison should use smaller detune, or keep the fundamental or sub voice un-detuned (the "center voice" or sub-mono approach).
- Unequal voice amplitudes (side voices quieter than the centre) reduce fluctuation depth and therefore roughness. This is one reason supersaw-style designs use a "mix" control.

### Gaps
- I found no quantitative perceptual study specifically on detuned-unison "richness" ratings versus voice count or detune cents. The richness/roughness mapping above is inferred from general roughness data.
- Unverified: roughness peaks at about 70 Hz modulation rate for 1 kHz carriers (Zwicker & Fastl). Not confirmed in a fetched source.

## Transients and perceived punch: attack time, crest factor, transient masking

### Takeaway
Perceived punch tracks the loudness of the transient portion of a sound, measured over a window shorter than standard loudness meters (280 ms vs 400 ms). It is strongest for fast onsets and lower frequency bands (64 Hz–1 kHz), and drops as onset time grows from 0 to 60 ms.

### Cited Findings
- **Fenton et al. punch model.** 80 ms bursts with onset times of 0, 5, 10, 20 and 60 ms were rated by 11 expert listeners. Punch fell with both rising band centre frequency and longer onset: EstPunch = 0.954 − 0.171·band − 0.088·onset, R² = 0.906. Regression covered 64 Hz–1 kHz octave bands, with a "pivot point around the 1kHz reference band". The model uses a 280 ms window, separates the transient component (QMF, STFT and median filtering) and defines punch as "a sense of dynamic power or weight". — [Fenton, AES 139 "Perceptual Punch Model"](https://eprints.hud.ac.uk/id/eprint/26288/9/AES%20139_Perceptual_Punch_Model_Fenton.pdf)
- **Fast attack and brightness.** Among sounds with close spectral centroids, faster attacks are heard as brighter. Centroid fluctuation during the onset contributes to brightness. — [Univ. Vienna "Revisiting timbral brightness perception"](https://muwidb.univie.ac.at/brightness-studies/index.htm)
- Backward masking (up to about 25 ms, see the masking section) and forward masking (up to about 200 ms) define the time windows in which a transient can hide or be hidden. — [Hearing Health Matters](https://hearinghealthmatters.org/pathways-society/2015/unmasking-auditory-temporal-masking/)

### Inferences
- Default amp-envelope attacks of about 0–5 ms on bass and lead presets maximize punch. The model penalizes onsets of 10 ms or more, and 60 ms reads as soft.
- Punch is concentrated below about 1 kHz, so a transient layer (click or pitch-envelope "thump") should place energy around 64–500 Hz, not only high clicks.
- Keeping crest factor (transient peak above the sustain) while raising sustained loudness through the summation strategy gives "loud and punchy". Heavy limiting inside the synth flattens the transient-loudness contrast the punch model measures.

### Gaps
- I found no source with a quantified crest-factor-to-punch relationship. The 2–8 kHz bands' effect on punch was "still under study" in Fenton's work.

## Brightness and air: spectral centroid, 8–16 kHz content

### Takeaway
Brightness is mainly predicted by spectral centroid and interacts with attack (faster attack sounds brighter at equal centroid). I could not find a quantified link between high-frequency "air" (8–16 kHz) and perceived size in the sources available.

### Cited Findings
- Brightness is the timbre dimension linked to spectral centroid. Its link to log attack time is likely an interaction with centroid, and centroid movement at onset matters. — [Univ. Vienna brightness studies](https://muwidb.univie.ac.at/brightness-studies/index.htm)
- The critical band at 8 kHz is about 800 Hz wide, so high frequencies are integrated over wide bands. — [NYU lecture notes](https://www.cns.nyu.edu/~david/courses/perception/lecturenotes/loudness/loudness.html)
- K-weighting adds a shelf boost above about 2 kHz, so high-frequency energy contributes disproportionately to measured LUFS. — [Sound On Sound](https://www.soundonsound.com/node/4921525)

### Inferences
- High-frequency "air" (noise or saturated harmonics at 8–16 kHz) cheaply spreads energy across several wide critical bands. It increases loudness through summation and brightness (higher centroid), but also LUFS through K-weighting, so in a loudness-normalised context it can make a sound quieter overall. Keep air as a modest, decorrelated layer for width rather than a level boost.
- Animating the spectral centroid at onset (filter-envelope "pluck") reinforces perceived brightness and attack without adding sustained high-frequency level.

### Gaps
- I found no quantified study on 8–16 kHz content and perceived size, energy or "bigness". Also missing: data on the relationship between spectral centroid and perceived source size (the classical "bigger source = lower centroid" association is unverified here). Fetched sources gave no numeric centroid values.
