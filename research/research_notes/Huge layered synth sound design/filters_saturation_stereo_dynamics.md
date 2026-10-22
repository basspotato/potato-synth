# Post-oscillator DSP for "huge" synth sounds: filters, saturation, stereo, dynamics, loudness, reverb

Scope note: I made about 20 search/fetch calls. Several primary PDFs were only partly readable (Zavalishin's book came back with the table of contents and chapters 1-2 only; Cytomic's SVF paper came back with garbled equations). Items under "Inferences" that come from general DSP knowledge rather than a fetched source are marked **[domain knowledge, unverified here]**. The report writer should treat those as engineering defaults, not cited facts.

## Filters: ZDF/TPT SVF and ladder, nonlinear drive, self-oscillation, comb/formant

### Takeaway
Use topology-preserving-transform (TPT/ZDF) filters. A trapezoidal SVF (Simper/Cytomic) is the cheapest filter that stays stable and smooth under audio-rate modulation. A ZDF 4-pole ladder with an in-loop tanh is the classic "fat" resonant lowpass. JUCE already ships both, as `dsp::StateVariableTPTFilter` and `dsp::LadderFilter`.

### Cited Findings
- Zavalishin's *The Art of VA Filter Design* (Native Instruments, v2.1.0) covers "digital models of classical musical analog filter structures using the topology-preserving transform approach", which it calls a generalization of "bilinear transform, zero-delay feedback and trapezoidal integration". It says this approach leaves "plenty of options for nonlinearities". — [Zavalishin, VAFilterDesign 2.1.0](https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.1.0.pdf)
- The book's chapters relevant to a synth: Ch.3 Time-discretization (trapezoidal integration, cutoff prewarping, zero-delay feedback, "instantaneously unstable feedback"); Ch.4 State variable filter, digital model; Ch.5 Ladder filter (feedback and resonance, digital model, "feedback shaping"); Ch.6 Nonlinearities (nonlinear zero-delay feedback equation, iterative methods, approximate methods). — [Zavalishin](https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.1.0.pdf)
- Zavalishin, Sec. 2.7: put the cutoff gain *before* the integrator, so the integrator smooths cutoff-modulation artifacts. This matters for time-varying (modulated) behaviour, which Sec. 2.16 covers. The book credits Urs Heckmann (u-he) as an early proponent of ZDF. — [Zavalishin](https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.1.0.pdf)
- Cytomic (Andrew Simper) trapezoidal SVF, nodal solution with gc normalised to 1 and k = 1/Q:
  - v1 = (ic1eq − g·ic2eq + g·v0) / (1 + g·k + g²)
  - v2 = ic2eq + g·v1
  - state update: ic1eq = 2·v1 − ic1eq, ic2eq = 2·v2 − ic2eq
  - outputs: low = v2, band = v1, high = v0 − k·v1 − v2
  - The paper says that interpolating cutoff, Q and output mix morphs between responses stably and smoothly "even at audio-rate modulation". It also says a parallel (bounded) form can beat the sequential form even with more operations, so profile both.
  - Caveat: the fetched text was partly garbled, so this is the readable derivation, not the paper's final a1/a2/a3 form. — [Cytomic SvfLinearTrapOptimised2.pdf](https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf)
- The JUCE dsp module provides:
  - `StateVariableTPTFilter`: LP/BP/HP, 12 dB/oct, "built for fast modulation"
  - `LadderFilter`: Moog-style, 12 and 24 dB LP/HP/BP
  - `FirstOrderTPTFilter`
  - `LinkwitzRileyFilter`: TPT-based LP/HP/AP for multiband splits

  — [JUCE dsp namespace docs](https://docs.juce.com/master/namespacejuce_1_1dsp.html)
- Csound ships ZDF reference opcodes (`zdf_ladder`, `diode_ladder`, `k35_hpf`), useful as cross-check implementations. — [Csound zdf_ladder](https://csound.com/manual/zdf_ladder.html), [diode_ladder](https://csound.com/manual/diode_ladder.html)

### Inferences
- **[domain knowledge, unverified here]** TPT one-pole:
  - g = tan(π·fc/fs)
  - v = (x − s)·g/(1+g); y = v + s; s = y + v
  - Prewarping keeps the cutoff exact up to Nyquist.
- **[domain knowledge, unverified here]** A 4-pole ladder with feedback gain k self-oscillates around k ≈ 4. In ZDF form the feedback loop is solved for the instantaneous response, y = (G·x + S)/(1 + k·G), where G is the total instantaneous gain of the four stages (g/(1+g))^4.
- **[domain knowledge, unverified here]** Nonlinear options, from cheapest to most accurate:
  1. Put tanh only on the input/feedback, using the previous sample's state ("cheap" approximate method).
  2. Solve the ZDF equation once linearly, then apply tanh to the result.
  3. Run 1–4 Newton–Raphson iterations per sample (several times the cost; data-dependent iteration count is bad for real-time determinism, so cap the iterations).
- **[domain knowledge, unverified here]** Driving a nonlinear filter generates harmonics, so it needs 2–4x oversampling or ADAA, just like a waveshaper.
- **[domain knowledge, unverified here]** Linear SVF cost is about 10 multiply-adds per sample per channel (very cheap). The cost is dominated by recomputing tan() when cutoff is modulated. Use a per-block or per-N-sample update, or a polynomial tan approximation.
- **[domain knowledge, unverified here]** Comb filter: y[n] = x[n] + g·y[n−D], where D = fs/f0 sets the pitch. Use a fractional (allpass or Lagrange) delay so it can track pitch. A formant filter is 3–5 parallel bandpass SVFs at vowel formant frequencies (F1/F2/F3). Both are cheap.

### Gaps
- I could not extract the full ladder/nonlinear chapters of Zavalishin or the exact optimised a1–a3 coefficients from Cytomic (PDF text garbled). Formulas above marked unverified should be checked against the PDFs.
- I found no sourced CPU benchmarks comparing Newton–Raphson and approximate ZDF ladders.

## Saturation and waveshaping: ADAA, oversampling, curves, even/odd harmonics, parallel/multiband

### Takeaway
ADAA (Parker, Zavalishin and Le Bivic 2016; Bilbao, Esqueda, Parker and Välimäki 2017) replaces f(x) with a divided difference of its antiderivative. It suppresses aliasing at very low cost and adds a fractional delay. In practice it is combined with modest (2x) oversampling. JUCE's `dsp::WaveShaper` combined with `dsp::Oversampling` is the stock building block.

### Cited Findings
- First-order ADAA: y[n] = (F1(x[n]) − F1(x[n−1])) / (x[n] − x[n−1]), where F1 is the antiderivative of the nonlinearity. Second order uses F2 with nested divided differences. — [DAFx20in21 paper 42](https://dafx2020.mdw.ac.at/proceedings/papers/DAFx20in21_paper_42.pdf)
- In the linear (small-signal) limit, ADAA reduces to FIR kernels: H1(z) = (1 + z⁻¹)/2 for first order and H2(z) = (1 + z⁻¹ + z⁻²)/3 for second order. So first order adds a half-sample delay and a gentle HF rolloff. — [DAFx20in21 paper 42](https://dafx2020.mdw.ac.at/proceedings/papers/DAFx20in21_paper_42.pdf)
- Origins of ADAA:
  - Parker, Zavalishin and Le Bivic, "Reducing the aliasing of nonlinear waveshaping using continuous-time convolution", DAFx 2016.
  - Bilbao, Esqueda, Parker and Välimäki, "Antiderivative antialiasing for memoryless nonlinearities", IEEE Signal Processing Letters 2017.
  - Esqueda, Pöntynen, Parker and Bilbao applied ADAA to the Lockhart and Serge wavefolders.

  — [DAFx20in21 paper 42](https://dafx2020.mdw.ac.at/proceedings/papers/DAFx20in21_paper_42.pdf), [Aalto: VA models of Lockhart and Serge wavefolders](https://research.aalto.fi/en/publications/virtual-analog-models-of-the-lockhart-and-serge-wavefolders/)
- ADAA inside feedback/stateful systems: the added delay must be compensated. Holters and Albertini et al. use T̃ = (3/2)T for first order. The DAFx20in21 authors instead scale the reactive element (C̃ = 2/3·C for first order, C̃ = 1/2·C for second order). They also say second-order ADAA "probably requires more oversampling" than first order, and that oversampling is the simplest mitigation but expensive. — [DAFx20in21 paper 42](https://dafx2020.mdw.ac.at/proceedings/papers/DAFx20in21_paper_42.pdf)
- Ill-conditioning when x[n] ≈ x[n−1] is handled by special-case fallbacks found in the literature; the paper cites these without detailing them. — [DAFx20in21 paper 42](https://dafx2020.mdw.ac.at/proceedings/papers/DAFx20in21_paper_42.pdf)
- Ongoing research on ADAA interpolation filters and extensions continues (DAFx24/25). — [Interpolation Filters for ADAA (DAFx)](https://www.dafx.de/paper-archive/details/VEph91o4UTFBAN6Fha2z_A), [DAFx24 paper 33](https://dafx.de/paper-archive/2024/papers/DAFx24_paper_33.pdf)
- Esqueda's Aalto doctoral thesis, "Aliasing Reduction in Nonlinear Audio Signal Processing", is the consolidated reference. — [Aaltodoc](https://aaltodoc.aalto.fi/items/a46ae786-3014-4c49-a409-957b4c330bcd)
- JUCE: `WaveShaper` maps samples through a custom function, and `Oversampling` processes multiple channels at a higher internal rate. — [JUCE dsp docs](https://docs.juce.com/master/namespacejuce_1_1dsp.html)

### Inferences
- **[domain knowledge, unverified here]** Antiderivatives for common curves:
  - tanh(x): F1 = ln(cosh x). Compute it overflow-safe as |x| + log1p(exp(−2|x|)) − ln 2.
  - Hard clip: piecewise quadratic.
  - Fall back to f((x[n] + x[n−1])/2) when |Δx| < ~1e-5.
- **[domain knowledge, unverified here]** Harmonic content:
  - Symmetric (odd) curves such as tanh, hard clip and cubic generate odd harmonics.
  - Asymmetric curves, or a DC bias before the shaper, add even harmonics (a warmer, "tube" character). Remove the resulting DC afterwards with a ~5–20 Hz highpass.
  - Wavefolders produce dense, high harmonics. They alias the most and benefit most from ADAA plus oversampling.
- **[domain knowledge, unverified here]** Typical oversampling: 2x with ADAA, or 4–8x without, for heavy drive. JUCE `Oversampling` polyphase IIR mode has low latency. FIR equiripple mode is linear-phase but adds latency; report it via `setLatencySamples`.
- **[domain knowledge, unverified here]** Parallel distortion (dry/wet via `dsp::DryWetMixer`, which handles wet latency) keeps transients and low end while adding density. Multiband distortion splits bands with LR4 crossovers (`LinkwitzRileyFilter`) and distorts mids/highs more than lows, which avoids intermodulation mud in the bass.
- **[domain knowledge, unverified here]** Saturation raises perceived loudness by lowering the crest factor (peak-to-RMS) and adding upper harmonics in the ear's most sensitive 2–5 kHz region. Gain-compensate (auto-gain) so users don't judge "louder" as "better".

### Gaps
- I found no sourced quantitative alias-rejection figures (dB) per ADAA order versus oversampling factor.
- I found no sourced CPU cost comparison.

## Stereo decorrelation: Haas, all-pass, chorus/ensemble, M/S, mono lows, correlation targets

### Takeaway
Juno-style chorus (BBD delay, slow triangle LFO, anti-phase L/R) and Dimension-D-style multi-tap modulated delays are the classic "wide" tools. A modern synth adds M/S width with a mono-below-crossover bass (LR4 split, ~100–200 Hz). Static all-pass decorrelators and Haas delays are cheaper but risk comb filtering when summed to mono.

### Cited Findings
- Dimension D / CE-1 design (UA's description):
  - Built around bucket-brigade delay (BBD) lines that make several copies of the input, each delayed along its own trajectory and scaled according to the selected mode. The SDD-320 has four modes.
  - Output is dry plus delayed signal.
  - A compander (compressor before the BBDs, complementary expander after) keeps the BBDs at their best operating level.
  - Choruses typically use triangular delay trajectories (roughly constant delay rate, hence constant pitch shift).
  - CE-1 chorus mode sweeps the wet signal from about 1.5 ms to about 4 ms of delay. — [Universal Audio WebZine, Apr 2005](https://uaudio.com/webzine/2005/april/text/content2.html)
- Dattorro: two-voice chorus is the industry-standard form, and all-pass interpolation suits chorus. Linear interpolation causes time-varying low-pass artifacts. — [Dattorro, Effect Design Part 1 (JAES 1997)](https://ccrma.stanford.edu/%7Edattorro/EffectDesignPart1.pdf)
- JUCE provides:
  - `Chorus`: a modulated delay line.
  - `Phaser`: 6 first-order all-pass stages.
  - `DelayLine`: selectable interpolation (template types).
  - `Panner`: linear, balanced, sin3dB, sin4p5dB, sin6dB, squareRoot3dB and squareRoot4p5dB rules.

  — [JUCE dsp docs](https://docs.juce.com/master/namespacejuce_1_1dsp.html)

### Inferences
- **[domain knowledge, unverified here]** Juno-60 chorus values (commonly cited, not verified this session):
  - Mode I ≈ 0.5 Hz LFO, Mode II ≈ 0.8 Hz, Mode I+II ≈ 8–10 Hz with shallow depth.
  - Triangle LFO, with L and R wet signals modulated in anti-phase.
  - Delay roughly 1.5–5 ms.
  - Wet signal low-passed at ~10 kHz to emulate the BBD.
- **[domain knowledge, unverified here]** Haas: delay one channel by 1–30 ms. In mono this produces a comb filter with notches every 1/delay Hz, so restrict Haas to above ~200–300 Hz or keep it subtle.
- **[domain knowledge, unverified here]** All-pass decorrelation: use different cascades of all-pass filters per channel. Frequency-dependent decorrelation means applying it only above a crossover.
- **[domain knowledge, unverified here]** M/S:
  - M = (L+R)/2, S = (L−R)/2.
  - Width w scales S; L = M + w·S, R = M − w·S.
  - Mono bass: highpass S (or zero S) below ~120–150 Hz.
- **[domain knowledge, unverified here]** Phase-correlation meter: +1 = mono, 0 = uncorrelated, −1 = out of phase. A healthy wide mix stays mostly between 0 and +1. Sustained negative readings mean mono-cancellation risk.
- **[domain knowledge, unverified here]** CPU cost: chorus and Haas need a delay line plus an interpolator (cheap, a few ops per voice-tap). All-pass networks cost about 2 multiplies per stage. Running these per voice rather than on the summed bus multiplies cost by the polyphony.

### Gaps
- I found no primary schematic analysis of Juno-60 chorus LFO rates and delays, or of the four Dimension D mode settings.
- Correlation-meter target values could not be sourced to a standard.

## Dynamics: OTT/multiband up+down, glue, transient shaping, sidechain ducking, true-peak limiting

### Takeaway
OTT is Ableton Multiband Dynamics' "OTT" preset:
- 3 bands, crossovers at 88.3 Hz and 2.5 kHz.
- Extreme downward ratios with 4:1 upward compression below threshold.
- About +5.2 dB input and +5.7 to +10.3 dB output per band.
- Depth (dry/wet) and Time (attack/release scaling).

True-peak limiting per ITU-R BS.1770 detects inter-sample peaks via about 4x oversampling.

### Cited Findings
- OTT is a preset of Ableton Live's Multiband Dynamics; Xfer's free OTT emulates it. Settings:
  - 3 bands. The Low/Mid crossover is 88.3 Hz (the Ableton default is 250 Hz) and the Mid/High crossover is 2.5 kHz. Xfer's crossovers are fixed.
  - Preset: +5.2 dB input per band; output +10.3 dB on Low and High, +5.7 dB on Mid.
  - Above threshold: High ∞:1, Mid and Low 1:66.7 (as reported).
  - Below threshold: 4:1 upward on every band. Thresholds are very low.
  - Xfer controls: Depth (dry/wet), Time (scales attack and release together), in/out gain, per-band upward/downward amount. — [EDMProd, OTT plugin](https://www.edmprod.com/ott-plugin/)
- True peak / inter-sample peaks (FabFilter Pro-L 2 docs):
  - Fast limiting and hard clipping create inter-sample peaks where the DAC output exceeds the sample values. MP3 encoding also creates them.
  - EBU R128 and ATSC A/85 set maximum true-peak levels.
  - Pro-L 2's true-peak metering complies with ITU-R BS.1770 / EBU R128.
  - "Typically, 4x oversampling with a minimum lookahead of 0.1 ms keeps inter-sample peaks within about 0.1 dB."
  - True-peak limiting adds about 5 ms of latency. — [FabFilter Pro-L 2 help: true peak limiting](https://www.fabfilter.com/help/pro-l/using/truepeaklimiting)
- Open-source true-peak detectors (Essentia `TruePeakDetector`, ffmpeg ebur128) implement BS.1770 oversampled peak detection. — [Essentia TruePeakDetector](https://essentia.upf.edu/reference/std_TruePeakDetector.html), [ffmpeg trac #2545](https://ffmpeg.org/pipermail/ffmpeg-trac/2013-May/014855.html)
- JUCE:
  - `Compressor`: threshold, ratio, attack, release.
  - `Limiter`: two compressors followed by a 0 dB hard clipper. It has no look-ahead or true-peak detection, so a mastering-grade limiter must be written by hand.
  - `NoiseGate`.

  — [JUCE dsp docs](https://docs.juce.com/master/namespacejuce_1_1dsp.html)

### Inferences
- **[domain knowledge, unverified here]** BS.1770 true peak: oversample 4x at 48 kHz (a polyphase FIR, Annex 2 suggests 48 taps / 12 per phase), take the absolute maximum, and report in dBTP. Below 4x, sample-peak meters under-read by up to about 3 dB at high frequencies.
- **[domain knowledge, unverified here]** Look-ahead limiter design:
  - Delay the audio by L (1–5 ms).
  - Compute the required gain from the true-peak envelope.
  - Take a running minimum over the window, then smooth it (attack over L, release 50–200 ms).
  - Latency = L plus the oversampling filter delay. Report it to the host.
- **[domain knowledge, unverified here]** Sidechain/volume shaping (LFOTool/Kickstart style): a tempo-synced gain envelope, e.g. duck to −∞…−12 dB at the beat and recover over a 1/8–1/4 note. Implement it as a per-sample LUT envelope; nearly free in CPU.
- **[domain knowledge, unverified here]** Transient shaper: compare a fast and a slow envelope follower. The difference drives attack/sustain gain.
- **[domain knowledge, unverified here]** Glue compression: low ratio (2:1–4:1), slow attack (10–30 ms), auto/medium release, 1–3 dB of gain reduction.
- **[domain knowledge, unverified here]** Multiband up/down compression costs 3 crossover pairs (LR4 = 2 cascaded 2nd-order sections each) plus 3 envelope followers; cheap compared with oversampled distortion. Use LR4 crossovers (`LinkwitzRileyFilter`, summing allpass-flat) so the bands sum without magnitude ripple.

### Gaps
- The Ableton Multiband Dynamics manual was not fetched. OTT threshold dB values and attack/release times remain unsourced, and the "1:66.7" ratio should be checked against the Ableton preset.
- LFOTool and Kickstart documentation was not fetched.
- The ITU-R BS.1770-4/5 Annex 2 filter specification was not fetched directly.

## Loudness and headroom: gain staging in a synth, LUFS targets, layer summing

### Takeaway
Streaming platforms normalise to about −14 LUFS-I (Apple −16). Loud EDM/club masters commonly sit around −5 to −8 LUFS-I with true peaks near −0.1 to −1 dBTP. A synth should process in float with internal headroom and put an optional true-peak-safe output stage last.

### Cited Findings
- Streaming normalisation targets (as of January 2025, per EDMProd; not from official platform docs):
  - Spotify, YouTube and Tidal: −14 LUFS-I
  - Apple Music: −16 LUFS-I
  - SoundCloud: no LUFS spec

  The same article also says Spotify and Apple "typically ask for around −14", which conflicts with its −16 figure. — [EDMProd, LUFS](https://www.edmprod.com/lufs/)
- True-peak recommendations (same source): Spotify −1 dBTP, or −2 dBTP if the track is louder than −14 LUFS-I; SoundCloud −0.5 to −1 dBTP. "Almost all top-charting hits" exceed −1 dBTP. — [EDMProd, LUFS](https://www.edmprod.com/lufs/)
- Example loud masters: a Halogenix tune at −5.4 LUFS-I ("very compressed but suits the loud style"); −5 LUFS-I is called acceptable for massive techno; another example is −10.7 LUFS-I at −0.1 dBTP. — [EDMProd, LUFS](https://www.edmprod.com/lufs/)

### Inferences
- **[domain knowledge, unverified here]** Summing N uncorrelated layers (detuned unison voices, random phase) raises RMS by about 10·log10(N) dB. Summing correlated layers raises it by up to 20·log10(N). Scaling by 1/√N keeps uncorrelated unison at constant loudness; Serum, Vital and others use variants of this for unison.
- **[domain knowledge, unverified here]** With 32-bit float internal processing there is no internal clipping. Clipping happens only at nonlinear stages and the host/DAC. Keep about −12 to −18 dBFS RMS going into drive stages so the drive knob sets the distortion amount predictably.
- **[domain knowledge, unverified here]** Output: give presets a target of about −6 dBFS peak out of the synth, and offer an optional soft clipper or limiter at the end. Auto-gain is drive-compensation = −k·drive_dB, or an RMS-matched gain smoothed over 300 ms or more.
- **[domain knowledge, unverified here]** Synth presets measured in isolation are not masters. LUFS targets apply to the full mix. For the synth, the relevant metrics are crest factor and true-peak safety.

### Gaps
- Official Spotify, Apple and YouTube loudness documentation was not fetched. Use the platforms' own pages before quoting final numbers.
- I found no sourced survey of median LUFS-I for EDM releases.

## Reverb/delay for size: FDN, Dattorro plate, pre-delay, ducking, shimmer

### Takeaway
Dattorro's plate (a Griesinger-style figure-of-eight tank with four input diffusers and modulated all-passes) is a cheap, well-documented reverb. A 4–16-line FDN with a Householder/Hadamard matrix is the scalable alternative. Size comes from pre-delay (10–40 ms), long decay, wet ducking and modulation.

### Cited Findings
- Dattorro plate (JAES 1997), designed at fs = 29761 Hz:
  - Input diffusion 1 = 0.750, input diffusion 2 = 0.625 (optimum diffusion lies nearer 0.5).
  - Decay diffusion 1 = 0.70; decay diffusion 2 = decay + 0.15, clamped to 0.25–0.50.
  - Decay = 0.50; bandwidth (input LPF) = 0.9995; damping (tank LPF) = 0.0005.
  - Modulated delay taps in the tank (nodes 24 and 48) driven by a quadrature LFO, at about 1 Hz with an excursion of about 8 samples at 29.8 kHz (Table 1 lists EXCURSION = 16 as the maximum, which conflicts).
  - Stereo input is summed to mono. Output is 7 signed taps per channel, each scaled by 0.6 (e.g. YL taps node48_54[1266], [2974]...; YR taps node24_30[353], [3627]...).
  - Tank all-passes need magnitude truncation against limit cycles (12–24 dB lower noise floor). The bandwidth and damping filters are first-order lowpasses.
  - Lattice coefficients must stay below 1.0 for stability. — [Dattorro, Effect Design Part 1](https://ccrma.stanford.edu/%7Edattorro/EffectDesignPart1.pdf)
- Implementations: el-visio/dattorro-verb (C), CAPS Reverb.h (Debian), and MATLAB's `reverberator` object, which is based on the Dattorro plate. — [GitHub dattorro-verb](https://github.com/el-visio/dattorro-verb), [MathWorks reverberator](https://www.mathworks.com/help/audio/ref/reverberator.html), [CAPS Reverb.h](https://sources.debian.org/src/caps/0.9.24-4/Reverb.h/)
- JUCE: `dsp::Reverb` wraps `juce::Reverb` (a Freeverb-style design). `Convolution` does stereo partitioned FFT convolution. — [JUCE dsp docs](https://docs.juce.com/master/namespacejuce_1_1dsp.html)

### Inferences
- **[domain knowledge, unverified here]** Scale Dattorro's delay lengths by fs/29761 when running at 44.1 or 48 kHz. Cost is about a dozen delay lines and a few dozen multiply-adds per sample, which is very cheap. Linear interpolation is acceptable for the modulated taps, but all-pass or cubic interpolation avoids HF dulling.
- **[domain knowledge, unverified here]** FDN: N delay lines (mutually prime lengths), a unitary feedback matrix (Hadamard or Householder costs O(N log N) or O(N)), and per-line gain g_i = 10^(−3·d_i/(fs·T60)) for decay time T60, plus a shelving filter for frequency-dependent T60.
- **[domain knowledge, unverified here]** Ducking: a compressor or envelope follower on the wet return, keyed from the dry signal (e.g. 3–6 dB of reduction, 100–300 ms release). Shimmer: put a +12 st (and/or +7 st) pitch shifter, grain or delay-based, in the feedback loop. Pitch shifting raises cost substantially.
- **[domain knowledge, unverified here]** Real-time safety for every block:
  - Allocate all delay buffers in `prepareToPlay` and never in `processBlock`.
  - Smooth parameters (`SmoothedValue`).
  - Flush denormals (`juce::ScopedNoDenormals`), which matters especially in reverb tails and IIR filters.
  - Report the latency from oversampling and look-ahead.

### Gaps
- Dattorro's tank delay lengths (Fig. 1) were not extractable from the PDF text.
- I fetched no FDN primary paper (e.g. Jot, or Välimäki et al., "Fifty Years of Artificial Reverberation") in this session.
- Shimmer design references were not sourced.
