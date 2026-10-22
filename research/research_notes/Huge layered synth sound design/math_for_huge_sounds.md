# Mathematical and Equation-Driven Synthesis for Huge EDM Sounds

Scope note: this file collects maths that act as "multipliers" for size and complexity in a multi-engine layered synth. Each section separates **Cited Findings** (verified from a fetched source this session) from **Inferences**. Inferences include standard textbook equations that I did not re-verify against a fetched page this session. They are marked "[standard, unverified this session]" so the report writer can tell them apart. Gaps lists items that need a primary source before they are stated as fact.

---

## 1. Detune and spread maths (supersaw, irrational/prime ratios, decorrelation, analog drift)

### Takeaway
A big supersaw comes from **non-linear, unevenly spaced detune ratios**, **free-running random phases**, a **center vs. side mix law**, and a **key-tracked high-pass**. Even (linear) spacing gives one dominant beat rate. Uneven spacing gives complex beating that does not audibly repeat. Slow random drift (mean-reverting noise) adds analog movement for almost no CPU.

### Cited Findings
- The Roland JP-8000 supersaw uses 7 sawtooths. Measured ratios at maximum detune are about .893, .939, .980, 1.0, 1.020, 1.064, 1.110. Common musical settings are much narrower, roughly 40–90 cents total. — [KVR: Roland Supersaw thread](https://www.kvraudio.com/forum/viewtopic.php?p=4644620)
- The detune spacing is **non-linear**, unlike the "commonly used" evenly spaced offsets (-0.3…+0.3). Valhalla's Sean Costello notes that linear spacing gives one strong beat rate instead of the complex beating of the real Supersaw. — [KVR thread](https://www.kvraudio.com/forum/viewtopic.php?p=4644620)
- "The oscillators need to be free running/random" (phase), described as perhaps the most important factor. A high-pass filter at the end of the chain key-follows near the fundamental. — [KVR thread](https://www.kvraudio.com/forum/viewtopic.php?p=4644620)
- Szabo's paper fits the detune knob-to-spread curve with an **11th-order polynomial**. A forum critic (Borogove) argues the underlying curve is really a two-segment piecewise-linear function, so the polynomial is a poor modelling choice. — [KVR thread](https://www.kvraudio.com/forum/viewtopic.php?p=4644620)
- The detune curve is non-linear. At the knob's halfway point the spread stays small (good for smooth pads), then it rises gradually and spreads rapidly toward maximum. The original aliased, which gave a bright, airy tone, and a HPF removed low-frequency mud. — [Synthtopia on Szabo's JP6K](https://www.synthtopia.com/?p=31937)
- An integer reverse-engineering of the detune: each side saw is f·(1 ± d/256) with d = 5, 16, 28, 41, 55, 70, 88… (start at 5, add 11, then raise the increment by 1 each step). The 7-voice ratios come out as 0.890625, 0.9375, 0.98046875, 1, 1.01953125, 1.0625, 1.109375, close to the Szabo measurements. Seven voices are a "sweet spot": more voices clash and lose level. A matching HPF was a 2nd-order RBJ filter at about 2.5×f0 with Q = √2 (a heuristic visual match). — [KVR thread p=8572590](https://www.kvraudio.com/forum/viewtopic.php?p=8572590)
- Analog drift practice: a randomly chosen target pitch with a slow glide toward it (DeepMind-style). Per-note detune is Σ of 3 uniform randoms × MaxDetune, which is approximately Gaussian. Box–Muller is x = sqrt(−2 ln u2)·cos(2π u1), and (2u−1)^3 is a cheap "Gaussian-ish" substitute. About ±5 cents gives pleasant beating, and ±20 cents is "huge". A Prophet-600 drifted about 10 cents over about 30 minutes. Semiconductor 1/f noise means real drift is non-stationary. — [KVR: Analog Drift](https://www.kvraudio.com/forum/viewtopic.php?p=7408514)

### Inferences
- **Mix law (Szabo, widely circulated, unverified this session):** with mix knob x ∈ [0,1], center gain ≈ −0.55366x + 0.99785 and side gain ≈ −0.73764x² + 1.2841x + 0.044372. Treat these as candidates and check them against the paper.
- **Golden-ratio/irrational spreading [standard, unverified this session]:** beats between voices i and j occur at |f_i − f_j|. If the offsets are integer multiples of one base offset, all beat rates are commensurate and the composite envelope is periodic. Offsets built from irrational steps (for example offset_k = Δ·frac(k·φ), with φ = (1+√5)/2, the low-discrepancy Weyl sequence) or from √prime ratios make beat frequencies pairwise incommensurate, so the composite never repeats exactly. That is the formal version of the Valhalla "complex beating" observation above.
- **Maximum-decorrelation stereo spread:** alternate detune sign across L/R (e.g. voices sorted by offset, assigned L,R,L,R…). Give each voice an independent random start phase (cited above as critical). Optionally pan with a constant-power law, gL = cos(θ), gR = sin(θ), θ ∈ [0, π/2].
- **Ornstein–Uhlenbeck drift [standard, unverified this session]:** dX = θ(μ − X)dt + σ dW. Discrete form per control tick: X[n+1] = X[n] + θ(μ − X[n])Δt + σ√Δt·N(0,1). This is a one-pole low-passed white noise, i.e. a leaky random walk. The stationary std is σ/√(2θ), so σ can be set to give, e.g., a 3–5 cent std (the KVR magnitudes). θ sets the correlation time 1/θ (seconds). Cost is one multiply-add plus one Gaussian random per voice per control block. It matches the "random target + slow glide" approach structurally.
- A 7–9 voice unison per oscillator per note is the main CPU multiplier, linear in voice count. Each saw voice should be band-limited (see Section 10).

### Gaps
- I could not retrieve Szabo's thesis PDF ("How to Emulate the Super Saw", 2010), so the 11th-order polynomial coefficients and the mix-curve formulas are not verified.
- I found no published paper that specifically evaluates golden-ratio detune against linear detune perceptually. It is a reasoned design principle, not a cited result.

---

## 2. FM / PM, feedback FM, phase distortion, through-zero FM, Chebyshev waveshaping

### Takeaway
FM/PM is the cheapest "spectrum multiplier": one extra sine per operator buys dozens of partials, controlled by the index I and the c:m ratio. Feedback FM turns a sine into a saw-like wave and then into noise. Phase distortion (CZ) is a special case of PM. Chebyshev waveshaping gives exact harmonic recipes, but only at a fixed input amplitude.

### Cited Findings
- **Chowning's FM equation:** e = A sin(αt + I sin βt), with α the carrier and β the modulator (rad/s), and I = d/m the index (peak deviation / modulator frequency). — [Chowning 1973, JAES 21(7):526–534](https://web.eecs.umich.edu/%7Efessler/course/100/misc/chowning-73-tso.pdf)
- **Bessel expansion:** e = A{J0(I) sin αt + J1(I)[sin(α+β)t − sin(α−β)t] + J2(I)[sin(α+2β)t + sin(α−2β)t] + J3(I)[sin(α+3β)t − sin(α−3β)t] + …}. Odd lower sidebands are sign-inverted. — [Chowning 1973](https://web.eecs.umich.edu/%7Efessler/course/100/misc/chowning-73-tso.pdf)
- **Bandwidth ≈ 2(d + m).** Sidebands below 0 Hz reflect with phase inversion and add to the positive components. — [Chowning 1973](https://web.eecs.umich.edu/%7Efessler/course/100/misc/chowning-73-tso.pdf)
- **Harmonicity rules:** if c/m = N1/N2 (reduced), the spectrum is harmonic with f0 = c/N1 = m/N2, and components sit at harmonics k = N1 ± nN2. N2 = 1 gives all harmonics. N2 even gives odd harmonics only. N2 = 3 omits every third harmonic. Irrational c/m (e.g. 1/√2) gives inharmonic (bell/metal) spectra. — [Chowning 1973](https://web.eecs.umich.edu/%7Efessler/course/100/misc/chowning-73-tso.pdf)
- **Feedback FM (SuperCollider SinOscFB):** the sine's output is fed back into its own phase input, with the feedback amount in radians. Raising it moves the timbre from sine toward sawtooth, and overmodulation becomes chaotic. That implementation updates frequency only per control block (64 samples), which causes artifacts under audio-rate FM. — [SuperCollider SinOscFB docs](https://docs.supercollider.online/Classes/SinOscFB.html)
- **Phase distortion (Casio CZ):** a DDS phase accumulator is passed through a non-linear transfer function (a "knee") before the sine table lookup. This is equivalent to adding a triangle wave to the phase, so PD is a special case of PM. Sine plus knee gives a sine-to-ramp wave. The same function on a square table gives variable pulse width, and on a triangle it gives a ramp. The CZ "resonance" waveforms are not PD but "windowed synchronisation" (a windowed, hard-synced sine). — [Electric Druid on PD](https://electricdruid.net/?p=226)
- **Chebyshev waveshaping:** shaping f(x) applied to x = cos ωn, where T_k(cos θ) = cos kθ. Example: T5(x) = 16x⁵ − 20x³ + 5x produces pure cos(5ωn). Instrument form: y[n] = f(a[n]·cos ωn), with a the index. The exact recipe holds **only at a = 1**. Other indices bring in the lower harmonics. — [Puckette, Theory and Technique of Electronic Music, §Chebyshev](https://msp.ucsd.edu/techniques/latest/book-html/node84.html)
- Csound implements this directly: GEN13/GEN14 build tables from Chebyshev polynomials, and an opcode applies a weighted Chebyshev sum. — [Csound GEN13](https://csound.com/manual/GEN13.html); [Csound chebyshevpoly](https://csound.com/manual/opcodes/chebyshevpoly/)

### Inferences
- **Practical index scaling [standard, unverified this session]:** roughly I+1 significant sideband pairs (Carson's rule is consistent with Chowning's 2(d+m)). To avoid aliasing, cap I so that (c + (I+1)m) < fs/2, i.e. key-track the index downward at high notes.
- **Feedback FM equation [standard, unverified this session]:** y[n] = sin(φ[n] + β·y[n−1]). Averaging the last two outputs (β·(y[n−1]+y[n−2])/2) is the classic Yamaha/Tomisawa "anti-hunting" fix that damps the Nyquist oscillation at high β. The search surfaced Tomisawa-related patents ([US 5834672](https://patents.justia.com/patent/5834672)), but I did not verify their content.
- **Through-zero FM [standard, unverified this session]:** the phase increment Δφ = 2π(fc + D·m(t))/fs is allowed to go negative (the phase runs backwards), so deep linear FM stays pitch-stable. Exponential FM shifts pitch with depth. TZFM is essential for keeping a constant perceived pitch in deep audio-rate FM of a layered patch.
- **For "size":** a stack of 2-op FM voices with slightly detuned modulators (Section 1 drift) gives evolving sidebands. Index envelopes give brightness. Chebyshev sums give exact, CPU-cheap harmonic "exciters" on a sine sub layer (e.g. add T2 and T3 for warmth), at the cost of a polynomial evaluation per sample.
- **Aliasing:** a polynomial of order k multiplies bandwidth by k, so oversample by about k or band-limit the input. FM bandwidth grows with I.

### Gaps
- I did not fetch a primary source for the Tomisawa feedback-FM averaging or for TZFM equations (e.g. Hutchins, or Eurorack TZFM docs).
- Did not fetch Casio's PD patent for the exact CZ phase-transfer breakpoints.

---

## 3. Chaotic and nonlinear oscillators (Lorenz, Rössler, Chua, logistic, Kuramoto)

### Takeaway
Chaotic systems give non-repeating but bounded modulation, or noisy/biting audio, from 3 state variables. Hardware (Joranalogue Orbit 3) covers both LFO and VCO ranges. Coupled-oscillator (Kuramoto) networks give a single "sync" macro that turns a cloud of detuned partials into a locked tone. It is a natural "size → focus" control.

### Cited Findings
- **Joranalogue Orbit 3 (Eurorack)** implements "a modified electronic analogue of the classic double-scroll attractor" (Chua-type), with bistable as well as chaotic regions. Audio range is 22 Hz–22 kHz, with waveforms from near-sine to white noise and calibrated pitch tracking. LFO range is 2.4 mHz (a 7-minute period) to 180 Hz. It has six chaos outputs (X/Y/Z plus inverses, stepped 60°), an equilibrium-point gate output (positive/negative scroll), CV over frequency, distribution and both equilibrium points, and a hard-sync-like reset. — [Orbit 3 manual](https://cdn.shopify.com/s/files/1/1594/2421/files/orbit-3-user-manual.pdf)
- **Kuramoto model:** φ̇_i = ω_i + (K/N)·Σ_j sin(φ_j − φ_i − α0). The order parameter is R e^{jψ} = (1/N)Σ e^{jφ_i}, with R = 1 for full sync and R ≈ 0 for incoherent. Mean-field form: φ̇_i = ω_i + K R sin(ψ − φ_i). Critical coupling is Kc = 2/(π g(0)). The fetched summary called g(0) the "mean", but in standard Kuramoto theory g(0) is the value of the natural-frequency density at its centre. — [Lem, "Sound in Multiples", SMC 2019](https://www.smc2019.uma.es/articles/P3/P3_09_SMC2019_paper.pdf)
- Musical uses (Lem): an audio-rate additive sine bank whose phases are the Kuramoto states, where oscillators near the centre of a Gaussian frequency distribution entrain first. Phase-vocoder resynthesis with coupled groups per band (a flute scale with 130 oscillators in 10 groups). Drifting filter banks. Control-rate triggers on phase wrap for converging/diverging polyrhythms. — [Lem SMC 2019](https://www.smc2019.uma.es/articles/P3/P3_09_SMC2019_paper.pdf)
- Other Kuramoto music work exists, e.g. NIME 2024 "Coupled Oscillator Networks: Perspectives on Synchronization and Nonlinear Musical Frameworks". — [NIME 2024](https://nime.org/proc/nime2024_60/)

### Inferences
- **Equations [standard, unverified this session]:**
  - Lorenz: ẋ = σ(y − x), ẏ = x(ρ − z) − y, ż = xy − βz (classic σ = 10, ρ = 28, β = 8/3).
  - Rössler: ẋ = −y − z, ẏ = x + a y, ż = b + z(x − c) (a = b = 0.2, c = 5.7).
  - Chua: ẋ = α(y − x − h(x)), ẏ = x − y + z, ż = −βy, with piecewise-linear h(x) = m1 x + ½(m0 − m1)(|x+1| − |x−1|).
  - Logistic map: x[n+1] = r x[n](1 − x[n]), chaotic for r above about 3.57.
- **Implementation:** integrate with RK4, or semi-implicit Euler at a fixed internal step. Rate is set by scaling dt (dt ∝ desired frequency), so the "speed" knob is a dt multiplier. Normalise the outputs by the attractor's known bounding box. Euler at large dt can diverge, so clamp dt and add soft-clipping on the state. At audio rate the output is broadband, so oversample 2–4× or low-pass. Cost is about 10–30 flops per step: negligible as LFOs, moderate at audio rate with oversampling.
- **EDM use:** route Lorenz x/y/z to wavetable position, detune spread and filter cutoff for non-repeating movement on long pads/drones. The equilibrium-point gate (Orbit 3 style, sign of x) gives an organic random gate for rhythmic layers. A Kuramoto "K" macro on a unison/additive engine: at K = 0 it is a wide chorus cloud, and above Kc it collapses into a phase-locked, punchy tone. Cost is O(N) with the mean-field form.
- Make Noise and VCV specifics (e.g. VCV chaos modules) were not verified. See Gaps.

### Gaps
- I did not verify Make Noise products' internal chaos maths, or specific VCV Rack chaos modules (names, equations).
- I found no primary source this session for the Lorenz/Rössler/Chua constants. They are standard dynamical-systems values.

---

## 4. Spectral maths (tilt, odd/even, inharmonic stretch, formants, morphing, smearing)

### Takeaway
Additive or resynthesis engines can treat the partial list as a vector and apply closed-form transforms: tilt (dB/oct), odd/even weighting, stiffness stretch f_n = n f0 √(1+Bn²), formant envelopes, and log-domain interpolation for morphs. Each transform is O(partials) per control update.

### Cited Findings
- **Stiff-string inharmonicity:** for a pinned (simply supported) string, f_n = n·f0·√(1 + B n²). For clamped ends, multiply by (1 + (2/π)√B + (4/π)B). B = π²·Y·S·κ²/(T·L²), where Y is Young's modulus, S the cross-section, κ the radius of gyration (a/2 for a solid cylinder), T the tension and L the length. Substituting gives B = π³ Y a⁴ /(4 T L²). — [D. Russell, Penn State, Stiffness & Inharmonicity](https://www.acs.psu.edu/drussell/Demos/Stiffness-Inharmonicity/Stiffness-B.html)
- Stiffness in waveguide models is done with allpass dispersion filters in the loop (a string stiffness allpass H_s(z) with several poles and zeros). — [JOS, Extended Karplus-Strong](https://ccrma.stanford.edu/%7Ejos/pasp/Extended_Karplus_Strong_Algorithm.html); see also [JOS, Modeling stiffness](https://ccrma.stanford.edu/~jos/stiffbowed/Modeling_stiffness_string.html)
- Puckette's spectral-envelope patch reads each partial's amplitude from a table with a nominal 50 dB range, converted to linear amplitude, which is a dB-domain spectral envelope. — [Puckette TTEM](https://msp.ucsd.edu/techniques/latest/book-html/node72.html)

### Inferences
- **Spectral tilt [standard, unverified this session]:** a_n = a_1·n^{−s}, i.e. −6.02·s dB/octave. s = 1 is a saw-like 1/n, and s = 2 is triangle-like 1/n² (odd only). A "tilt" macro maps directly to s.
- **Odd/even balance:** a_n ← a_n·(1 − w) for even n (w = 1 gives a square/clarinet-like hollow sound). Equivalently, mix saw (all harmonics) with square (odd only).
- **Inharmonic stretch as a "size" macro:** using f_n = n f0 √(1 + B n²) with B from about 1e-4 (piano-like) up to much larger (bell/metal). Generalised "stretch" f_n = f0·n^{γ}, with γ slightly above 1, is another option. Check that partials stay below Nyquist and drop those above.
- **Formant synthesis:** multiply the partial amplitudes by a sum of resonance curves, e.g. A(f) = Σ_k g_k / (1 + ((f − F_k)/(B_k/2))²) (Lorentzian), with vowel formants F1–F3. Or run a parallel bank of 2-pole band-pass filters on a rich source. FOF/VOSIM are time-domain alternatives.
- **Morphing:** interpolate per partial in **log-frequency** (log f_n = (1−t)·log f_n^A + t·log f_n^B) and **log-amplitude/dB**. This sounds more perceptually linear than linear-amplitude crossfading (a crossfade gives a "two sounds at once" effect, while log-domain interpolation gives a single sound that moves). Requires partial correspondence (same index n, or matched tracks).
- **Partial smearing:** for each partial, add k satellite sines at f_n·2^{±c/1200} with small random c, or apply per-partial OU drift (Section 1). This turns a line spectrum into narrow bands (a "chorus per harmonic", the Paulstretch-like blur).
- **Cost:** additive is O(P) sines per voice per sample (or IFFT-based synthesis at about O(log N) per partial per frame). Formant filter banks are about 5 multiplies per biquad per sample.

### Gaps
- I did not fetch a primary paper on log-domain spectral morphing (e.g. IRCAM/SMS, Serra). The perceptual claim above is an inference.
- I found no fetched source for typical piano B values (often quoted as about 1e-4 to 1e-3 for mid-range strings, but not verified here).

---

## 5. Psychoacoustic loudness maths (summing laws, energy across critical bands)

### Takeaway
Uncorrelated layers add in power (normalise by 1/√N). Correlated or identical layers add in amplitude (normalise by 1/N). At equal total energy, a sound spread across more than one critical band is perceived as louder. That is the psychoacoustic reason wide unison, layered and spread spectra sound "bigger" at the same peak meter level.

### Cited Findings
- Within a critical band, equal energy gives equal loudness. Once the bandwidth exceeds a critical band at constant energy, "the sound with the larger bandwidth sounds louder" (more neurons are recruited). The critical bandwidth is about 150 Hz at 1 kHz and about 800 Hz at 8 kHz (each about 1.3 mm of basilar membrane). — [NYU perception lecture notes (Heeger)](https://www.cns.nyu.edu/~david/courses/perception/lecturenotes/loudness/loudness.html)
- Loudness summation across bandwidth is a well-studied effect (e.g. a Launer 1995 dissertation chapter). — [Univ. Oldenburg](https://medi.uni-oldenburg.de/download/docs/diss/Launer_1995_LoudnessPerception/kap5.pdf)
- Pragmatic: a forum poster stopped at 7 unison voices partly because the level drop from normalisation "was hard to justify without a compressor". — [KVR p=8572590](https://www.kvraudio.com/forum/viewtopic.php?p=8572590)

### Inferences
- **Summing laws [standard, unverified this session]:** for N uncorrelated equal-RMS signals, the RMS of the sum = √N·RMS, so apply gain 1/√N (−3 dB per doubling) to keep loudness. For N coherent (in-phase) signals the peak and RMS scale by N, so use 1/N (−6 dB per doubling). Detuned free-phase unison sits between the two. Its RMS behaves like 1/√N, but peaks can approach N, so use 1/√N for gain plus a soft limiter for peaks.
- **ERB [standard, unverified this session]:** Glasberg & Moore: ERB(f) = 24.7·(4.37·f/1000 + 1) Hz. ERB-number scale: E = 21.4·log10(1 + 0.00437 f). "Loudness maximisation" means distributing layer energy so that each ERB band gets a share, rather than stacking layers in the same band (which just raises level in one band and gets compressed). Practical rule: give each layer a home register (sub, body, mid, air) and use detune spread on mid/upper layers to widen bands.
- Zwicker: specific loudness N' ∝ E^{0.23} per critical band, summed over bands [standard, unverified this session]. The compressive exponent is why spreading energy over k bands gives more total loudness than concentrating it in one.

### Gaps
- I did not fetch the Glasberg & Moore (1990) or Zwicker/ISO 532 primary sources this session. The formulas above need citation.

---

## 6. Fractal / self-similar sounds: Shepard–Risset tones and Risset rhythms

### Takeaway
Octave-spaced partials sweep in log frequency under a fixed bell-shaped spectral envelope, wrapping at the edges, which gives an endless riser. That is the perfect parameterised EDM build-up generator. The time-domain analogue (Risset rhythm) gives endless accelerando.

### Cited Findings
- **Shepard tone (Puckette's Pd example D09):** sinusoid frequencies sweep over a fixed range and then jump back to the start. A spectral envelope peaks mid-range and falls to inaudibility at the edges, so only the continuous sweep is heard. — [Puckette TTEM](https://msp.ucsd.edu/techniques/latest/book-html/node72.html)
- **Risset rhythm:** layered, looped rhythms at different speeds, with loudness adjusted across layers. Stowell attributes the illusion to attention gravitating to the mid-tempo range. Stowell's track doubles tempo about every 30 s (from 120 bpm it would nominally reach 15,360 bpm in 7 minutes). — [QMUL news 2012](https://www.qmul.ac.uk/media/news/2012/se/can-you-keep-up-with-the-dance-music-that-accelerates-forever.html)
- An implementation: 5 voices of the same drum loop, each speeding up, where each voice's end speed equals the next voice's start speed, with cosine gain segments so voices fade in and out across the chain. — [tmhglnd risset-rhythm-effect (Max)](https://github.com/tmhglnd/risset-rhythm-effect)

### Inferences
- **Shepard–Risset equations [standard, unverified this session]:** with K octave layers, global sweep phase u(t) ∈ [0,1) and u̇ = rate (octaves/s ÷ K):
  - f_k(t) = f_min·2^{(k + u(t)) mod K}, k = 0…K−1.
  - Amplitude a_k = exp(−½·((log2(f_k/f_c))/σ)²), a Gaussian in log-frequency centred at f_c, or a raised cosine a_k = ½(1 − cos(2π·((k+u) mod K)/K)).
  - Each "partial" can be a full saw/supersaw voice rather than a sine (a supersaw Shepard riser), using the same weight law.
  - A falling version uses negative rate. Rate under macro control can accelerate into a drop.
- **Risset rhythm [standard, unverified this session]:** layer k plays the loop at tempo T0·2^{(k+u)} with a log-tempo Gaussian/cosine gain, the same maths as Shepard with tempo in place of frequency. Playback speed changes pitch unless time-stretched.
- Cost: K voices (typically 6–10), and the band-limited oscillators must cope with changing frequency.

### Gaps
- Did not fetch Risset's original (1969) or Shepard's (1964) papers.

---

## 7. Physical models useful for size (Karplus–Strong, modal banks, FDNs)

### Takeaway
Delay-line loops are the cheapest way to get thousands of resonant modes. KS gives plucks and bodies, and an FDN with an orthogonal matrix (Hadamard/Householder) gives dense, huge reverbs or resonant "bodies". Losslessness and the T60 per-line gain formula make the decay time an exact, stable macro.

### Cited Findings
- **Extended Karplus–Strong components** (N = pitch period in samples, so f0 ≈ fs/N, my inference from the definition):
  - Pick-direction LPF H_p = (1−p)/(1−p z⁻¹).
  - Pick-position comb H_β = 1 − z^{−⌊βN+½⌋}.
  - Damping H_d with |H_d| ≤ 1 required for stability.
  - Stiffness allpass H_s.
  - Tuning allpass H_η = −(η − z⁻¹)/(1 − η z⁻¹), η ∈ [−1/11, 2/3] for 0.2–1.2-sample fractional delay.
  - Dynamic-level LPF H_L = (1−R_L)/(1−R_L z⁻¹), R_L = e^{−πLT}.

  — [JOS, PASP: Extended Karplus-Strong](https://ccrma.stanford.edu/%7Ejos/pasp/Extended_Karplus_Strong_Algorithm.html)
- **FDN:**
  - N delay lines of lengths M_i inside a feedback matrix. Order M = ΣM_i poles on the unit circle in the lossless prototype.
  - Lossless if and only if the matrix eigenvalues all have |λ| = 1 with linearly independent eigenvectors. Unitary/orthogonal matrices qualify.
  - Hadamard: H2 = (1/√2)[[1,1],[−1,1]], built recursively. For N a power of 4 it needs no multiplies in fixed-point.
  - Householder (Jot): A = I − (2/N) u uᵀ, u = ones. It needs 2N−1 adds, with no multiplies for N a power of 2. At N = 4 all entries are ±½, and as N grows it tends toward decoupled combs (Smith embeds four A4 blocks for N = 16).
  - Triangular/Jordan matrices are not necessarily lossless.

  — [JOS, PASP: FDN Reverberation](https://www.dsprelated.com/freebooks/pasp/FDN_Reverberation.html)
- **Delay lengths:** mutually prime, so the response takes as long as possible to repeat. Schroeder mode density M ≥ 0.15·t60·fs (e.g. 7500 samples at 50 kHz for 1 s). Prime-power lengths M̂_i = p_i^{round(log M_i / log p_i)} keep coprimality while allowing per-line tuning. — [JOS FDN](https://www.dsprelated.com/freebooks/pasp/FDN_Reverberation.html)
- **Decay:** each line's filter satisfies 20 log10|H_i| = −60·M_i·T / t60(ω). One-pole damping per line: H_i = g_i/(1 − p_i z⁻¹), with p_i = (R0^{M_i} − Rπ^{M_i})/(R0^{M_i} + Rπ^{M_i}) and g_i = 2R0^{M_i}Rπ^{M_i}/(R0^{M_i}+Rπ^{M_i}). A tonal-correction filter compensates the energy change when decay times shrink. — [JOS FDN](https://www.dsprelated.com/freebooks/pasp/FDN_Reverberation.html)
- History and Jot's FDN design: [JOS, History of FDNs](https://ccrma.stanford.edu/~jos/pasp/History_FDNs_Artificial_Reverberation.html); STK Jot reverb: [CCRMA RealSimple](https://ccrma.stanford.edu/realsimple/reverb/JotReverb_STK.html)

### Inferences
- **Basic KS [standard, unverified this session]:** y[n] = x[n] + ½(y[n−N] + y[n−N−1]), giving f0 ≈ fs/(N + ½).
- **Modal resonator banks [standard, unverified this session]:** each mode is a 2-pole resonator y[n] = 2r cos(ω_k) y[n−1] − r² y[n−2] + g_k x[n], with r = e^{−π B_k/fs} (or r = 10^{−3/(t60_k·fs)}). Mode frequencies can come from the stiff-string formula (Section 4), bar or plate equations, or measured data. Cost is about 3 multiplies per mode per sample, so 64–256 modes are feasible per voice with SIMD. It is stable if r < 1 (compute in double precision for long T60 at low ω).
- **"Resonant body" macro:** feed the synth output into an FDN tuned to a chord (set the delay lengths to fs/f_note multiples), with long T60. That gives a huge harmonic shimmer. Use a Householder N = 8 or 16 for low cost. Modulating delay lengths slightly (OU drift) reduces metallic ringing.
- **Stability:** keep the matrix orthogonal and per-line gains < 1. Any non-orthogonal parameterisation (e.g. a morph between matrices) can break losslessness. Interpolating rotation angles (Givens) keeps it orthogonal.

### Gaps
- I did not fetch modal synthesis primary sources (e.g. Adrien 1991; Mutable Instruments Rings/Elements docs).

---

## 8. Morph maths (XY pads, IDW, sound-space navigation, macro curves)

### Takeaway
XY morphing between 4 presets is bilinear interpolation. N arbitrary presets use inverse-distance weighting or barycentric (triangulated) weights. Macro-to-many-parameter mappings need per-parameter curves (power/exp/spline) and interpolation in perceptual domains (log for frequency/time, dB for gain).

### Cited Findings
- No sources fetched this session for this section. See Gaps.

### Inferences
- **Bilinear (4 corners A,B,C,D at (0,0),(1,0),(0,1),(1,1)) [standard, unverified this session]:** P(x,y) = (1−x)(1−y)A + x(1−y)B + (1−x)yC + xyD. The weights sum to 1 and are non-negative, so the result is always inside the parameter hull.
- **Inverse-distance weighting (Shepard 1968) [standard, unverified this session]:** w_i = 1/d_i^p, P = Σ w_i P_i / Σ w_i, with p ≈ 2. Snap to P_i when d_i < ε. Larger p gives "stickier" presets.
- **Barycentric:** triangulate preset points (Delaunay), then inside a triangle λ1,λ2,λ3 ≥ 0, Σλ = 1, from area ratios. Only 3 presets are active at a time and the result is continuous across edges.
- **Perceptual domains:** interpolate frequency/cutoff/delay time in log2 space, gain in dB, and unison detune in cents. Snap discrete parameters (waveform index, algorithm) at weight 0.5 or crossfade dual engines.
- **Macro curves:** p = p_min + (p_max − p_min)·m^γ (γ > 1 slow start, γ < 1 fast start). Or exponential, p = p_min·(p_max/p_min)^m. Or a monotone cubic (Fritsch–Carlson/Hermite) spline through user breakpoints, which avoids overshoot. Smooth all macro outputs with a one-pole (≈5–20 ms) to avoid zipper noise.
- **High-dimensional navigation:** preset vectors can be embedded into 2D (PCA/MDS/t-SNE) so an XY pad navigates many presets. That is the idea behind "sound-space" interfaces such as Hypercube-style and IRCAM corpus-based work.

### Gaps
- I did not research or verify the Hypercube synth (its interpolation scheme), IRCAM sound-space navigation papers (e.g. CataRT, "Mubu"), or Shepard (1968) IDW. Search budget ran out. This section needs sources.

---

## 9. Real-time cost, stability and aliasing summary

### Takeaway
The main CPU multipliers are voice count × unison count × (oversampling for nonlinear stages). Aliasing comes mainly from hard edges (saws), FM index and waveshaping order. Instability comes mainly from feedback loops (FDN/KS gains, feedback FM, chaotic integrators).

### Cited Findings
- Supersaw aliasing was part of the original's bright character, removed in part by a HPF. — [Synthtopia](https://www.synthtopia.com/?p=31937)
- Band-limited oscillator literature: Välimäki/Nam et al., "Alias-suppressed oscillators based on differentiated polynomial waveforms" (IEEE TASLP 18(4), May 2010). — [KAIST PDF](https://mac.kaist.ac.kr/pubs/jnam-taslp2010.pdf); [CCRMA listing](https://ccrma.stanford.edu/node/2646)
- FDN with power-of-2 Householder matrices costs 2N−1 adds and no multiplies. Losslessness requires a unitary matrix. KS damping needs |H_d| ≤ 1 for stability. — [JOS FDN](https://www.dsprelated.com/freebooks/pasp/FDN_Reverberation.html); [JOS EKS](https://ccrma.stanford.edu/%7Ejos/pasp/Extended_Karplus_Strong_Algorithm.html)
- Feedback FM becomes chaotic at high feedback. Block-rate frequency updates cause artifacts under audio-rate FM. — [SinOscFB](https://docs.supercollider.online/Classes/SinOscFB.html)

### Inferences (approximate per-voice cost table)

| Technique | Cost per sample | Aliasing | Stability |
|---|---|---|---|
| Unison saw ×7 (PolyBLEP) | ~7× (phase inc + 2-sample poly correction) | Low with BLEP; HPF/LPF still needed | Stable |
| 2-op FM / PM | 2 sin lookups | Grows with I; key-track I | Stable |
| Feedback FM | 1 sin + 1 mul (+avg) | High at large β | Chaotic at large β; use 2-sample avg |
| Phase distortion | 1 table + phase map | Moderate (knee sharpness) | Stable |
| Chebyshev order k | k mul-adds (Horner) | Bandwidth ×k: oversample k× | Stable; amplitude-dependent timbre |
| Lorenz/Rössler LFO | ~20 flops at control rate | n/a | RK4/clamped dt |
| Chaotic audio osc | ~20 flops × oversample | High (broadband) | dt limits, soft clip |
| Additive P partials | P sines (or IFFT) | None if partials < Nyquist culled | Stable |
| Modal bank K modes | ~3K mul | None | r<1, double precision |
| FDN N=8–16 | N delays + 2N−1 adds + N one-poles | None | Orthogonal matrix, g<1 |
| Kuramoto N (mean-field) | O(N) | as additive | Stable for reasonable dt |

- **PolyBLEP correction [standard, unverified this session]:** with t = phase ∈ [0,1) and dt = f/fs:
  - if t < dt: τ = t/dt, correction = 2τ − τ² − 1.
  - if t > 1 − dt: τ = (t − 1)/dt, correction = τ² + 2τ + 1.
  - Subtract the correction from the naive saw (2t − 1).
- Denormals in long decays (FDN, modal) need flush-to-zero or a DC offset.

### Gaps
- The KVR PolyBLEP thread failed to fetch, so PolyBLEP code and quality claims are unverified.
- No measured CPU benchmarks were found. The table above is an estimate.
