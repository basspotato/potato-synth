# Oscillator-level DSP for a high-quality EDM synth: supersaw/unison, anti-aliasing, wavetables, additive/spectral

Research notes. Primary sources: the Vital source code (cloned from github.com/mtytel/vital, branch `main`, commit `636ca0ef517a4db087a6a08a6a8a5e704e21f836`), Välimäki et al. (JASA 2012), Serra & Smith (CMJ 1990), the Image-Line and Native Instruments product docs, and KVR/HISE forum threads. CPU numbers marked "own benchmark" were measured during this research on a virtualised Intel Xeon @ 2.1 GHz with AVX2/FMA, using g++ 13.3 `-O3 -march=native -ffast-math`. The source is at `/tmp/claude-0/-home-claude/534cc1aa-2eb8-5ffb-9490-84c05b9af6ae/scratchpad/bench/b.cpp`, which is session scratch and not persistent. These are micro-benchmark numbers, not published figures.

**Licence warning:** Vital is GPLv3. The file headers read "vital is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License… version 3" ([synth_oscillator.cpp](https://github.com/mtytel/vital/blob/main/src/synthesis/producers/synth_oscillator.cpp)). Treat it as a reference design. Re-implement from the algorithm rather than copying code into a non-GPL product.

---

## 1. Supersaw theory (Szabo 2010, JP-8000) and modern unison design

### Takeaway
The JP-8000 supersaw is 7 naive, free-running (random-phase) saws: one centre saw plus 3 symmetric detuned pairs. Szabo found three nonlinear curves that shape it: an 11th-order polynomial maps the detune knob to frequency spread, and two separate curves set the centre and side gains from the mix knob. The summed signal is then high-passed near the fundamental. Modern unison engines such as Vital generalise this. They use up to 16 voices, a power-curve detune distribution, a blend law with constant-power normalisation, an equal-power stereo spread, and phase randomisation applied at note-on.

### Cited Findings
**Szabo / JP-8000**
- Szabo's thesis is "How to Emulate the Super Saw" (KTH, 2010). The original URL `nada.kth.se/.../szabo_adam_10131.pdf` is now dead because the NADA site has been discontinued. The MIT FPGA project report cites it as reference [1] — [MIT 6.111 report](https://fpga.mit.edu/videos/2018/team07/report.pdf).
- Structure: "seven detuned and phase-shifted sawtooth waves", with random intra-set phase shifts, a high-pass filter at the base frequency, and voices summed with scaling by 1/√(number of active waves) "since perceived volume of non-coherent sums doesn't increase linearly" — [MIT 6.111 report](https://fpga.mit.edu/videos/2018/team07/report.pdf).
- Szabo's detune curve, as transcribed in a widely copied SuperCollider port, with x = detune knob in 0..1:
  `d(x) = 10028.7312891634x^11 − 50818.8652045924x^10 + 111363.4808729368x^9 − 138150.6761080548x^8 + 106649.6679158292x^7 − 53046.9642751875x^6 + 17019.9518580080x^5 − 3425.0836591318x^4 + 404.2703938388x^3 − 24.1878824391x^2 + 0.6717417634x + 0.0030115596` — [colinsullivan/cs-supercollider-lib SuperSaw.scd](https://github.com/colinsullivan/cs-supercollider-lib/blob/master/Instr/cs/synths/SuperSaw.scd)
- Per-voice frequency offsets at full detune, multiplied by d(x): `−0.11002313, −0.06288439, −0.01952356, 0, +0.01991221, +0.06216538, +0.10745242`. Each voice is `f·(1 + offset·d(x))`. The port evaluates d(0.99·detune) — [SuperSaw.scd](https://github.com/colinsullivan/cs-supercollider-lib/blob/master/Instr/cs/synths/SuperSaw.scd). The same ratios appear as Szabo's column in a KVR comparison: 0.88997686, 0.93711560, 0.98047643, 1, 1.01991221, 1.06216538, 1.10745242 — [KVR "Roland Supersaw" thread](https://www.kvraudio.com/forum/viewtopic.php?p=8572590).
- Szabo's mix curves, with x = mix knob in 0..1: centre gain `= −0.55366x + 0.99785`; side gain (applied to each of the 6 side saws) `= −0.73764x² + 1.2841x + 0.044372` — [SuperSaw.scd](https://github.com/colinsullivan/cs-supercollider-lib/blob/master/Instr/cs/synths/SuperSaw.scd).
- Independent measurement (russel / "Ghostfact") gives ratios within about 0.2% of Szabo's values. Another poster (stepvhen) fits integer coefficients k/256 = 228, 240, 251, 256, 261, 272, 284, i.e. detune steps of 5, 16 and 28 out of 256. The gaps grow by one each step, so the pattern extrapolates to 41, 55, 70, 88 if more pairs are wanted. Seven waves was described as the "sweet spot": more sidebands start to clash — [KVR thread](https://www.kvraudio.com/forum/viewtopic.php?p=8572590).
- High-pass filter: the MIT report says the HPF sits "at the base frequency" ([MIT report](https://fpga.mit.edu/videos/2018/team07/report.pdf)). A KVR poster instead fitted a 2nd-order RBJ high-pass with Q = √2 at 2.5×f0 as a heuristic waveform match ([KVR thread](https://www.kvraudio.com/forum/viewtopic.php?p=8572590)). **These conflict.** The cutoff is key-tracked in both, but the exact multiple and order are not settled in the sources found.
- Aliasing and sample rate: according to a JP-8000 emulator developer, the original is "detuned naive saws at a high sample rate, followed by a high-pass" (reported second-hand in the [KVR thread](https://www.kvraudio.com/forum/viewtopic.php?p=8572590)). The SuperCollider port uses random start phases per saw (`LFSaw.ar(freq, Rand(0,2))`) and separate L/R centre saws with independent random phase ([SuperSaw.scd](https://github.com/colinsullivan/cs-supercollider-lib/blob/master/Instr/cs/synths/SuperSaw.scd)).

**Modern unison: Vital as a reference implementation**
- Up to 16 unison voices: `kMaxUnison = 16`. Voices are processed in SIMD `poly_float` lanes, with L/R handled as separate lanes — [synth_oscillator.h](https://github.com/mtytel/vital/blob/main/src/synthesis/producers/synth_oscillator.h).
- Detune distribution (`setPhaseIncMults`):
  - For voice pair i, `t = (2i + bump)/(unison−1)`, where bump = 1 for an even voice count.
  - `adjusted_t = powerScale(t, power)`, with `powerScale(x,p) = (e^{p·x} − 1)/(e^{p} − 1)`, which is linear when |p| < 0.01.
  - `cents = detune_range × unison_detune × adjusted_t`. The up ratio is `centsToRatio(cents)` and the down ratio is its reciprocal, so pairs are symmetric in log-frequency.
  - Sharp and flat alternate between L/R lanes via `sharp_flat_mask`.
  - Sources: [synth_oscillator.cpp](https://github.com/mtytel/vital/blob/main/src/synthesis/producers/synth_oscillator.cpp), [futils.h](https://github.com/mtytel/vital/blob/main/src/synthesis/framework/futils.h).
- Parameter ranges and defaults: `unison_voices` 1–16 (default 1); `unison_detune` 0–10 (default 4.472, quadratic display); `detune_power` −5..5 (default 1.5); `detune_range` 0–48 (default 2); `unison_blend` 0–1 (default 0.8); `stereo_spread` default 1.0; `random_phase` default 1.0, i.e. 100% — [synth_parameters.cpp](https://github.com/mtytel/vital/blob/main/src/common/synth_parameters.cpp).
- Blend law (`setAmplitude`), with constants kCenterLowAmplitude = 0.4 and kDetunedHighAmplitude = 0.6:
  - `centre = lerp(1.0, 0.4, blend)`
  - `detuned = lerp(0.6, 0, (1−blend)²)`
  - Both are then normalised by `1/sqrt(centre² + detuned²·(half_oscillators − 1))`, which is a constant-power law assuming uncorrelated voices.
  - With unison ≤ 2, the centre gain is 1 and the detuned gain is 0.
  - Source: [synth_oscillator.cpp](https://github.com/mtytel/vital/blob/main/src/synthesis/producers/synth_oscillator.cpp).
- Stereo spread (`stereoBlend`): each output is `val·equalPowerFade(s/2+0.5) + swappedChannel·equalPowerFadeInverse(s/2+0.5)`. The gain is ramped linearly across the block to avoid zipper noise — [synth_oscillator.cpp](https://github.com/mtytel/vital/blob/main/src/synthesis/producers/synth_oscillator.cpp).
- Phase randomisation happens on `reset` at note-on/retrigger. Each unison lane gets `random_generator_.next() × random_amount × INT_MAX` as a uint32 phase, so 0% random phase means a hard retrigger to phase 0. Phases are integer accumulators that wrap naturally — [synth_oscillator.cpp](https://github.com/mtytel/vital/blob/main/src/synthesis/producers/synth_oscillator.cpp).
- Unison "stack" styles multiply pair ratios by fixed intervals: Normal, centre drop octave, centre drop two octaves, Octave, 2×Octave, Power chord (fifth = 1.49830707688), 2×Power chord, Major chord (third = 1.25992104989), Minor chord (1.189207115), Harmonic series 1..8, and Odd harmonics 1,3,…,15. Note these are equal-tempered ratios — [synth_oscillator.cpp](https://github.com/mtytel/vital/blob/main/src/synthesis/producers/synth_oscillator.cpp).
- Per-voice spreads: wavetable frame spread, distortion (warp) spread and spectral-morph spread are distributed linearly over the unison voices (`t·spread`) — [synth_oscillator.cpp](https://github.com/mtytel/vital/blob/main/src/synthesis/producers/synth_oscillator.cpp).
- Serum: up to 16 unison voices per oscillator ([Xfer Serum page](https://www.xferrecords.com/serum)). Its unison controls are Detune, Blend (side vs centre level), Width (stereo field), Range (the maximum detune relative to the played note) and modes setting the detune spacing (linear, exponential and others), with voices auto-panned ([HdM Stuttgart thesis on Serum 2](https://curdt.home.hdm-stuttgart.de/PDF/Serum%202.pdf)). Serum 2 adds "Phase Memory" for phase and phase randomisation on new notes ([Serum 2 What's New](https://static.xferrecords.com/Serum%202%20What's%20New.pdf)).
- Harmor unison has per-note subvoices with pitch, pan and phase spread, and "Chorus" is the cheaper, post-output alternative ([Harmor manual](https://www.image-line.com/support/flstudio_online_manual/html/plugins/Harmor.htm)).

### Inferences
- **Recommended unison core:** symmetric log-frequency pairs; a Vital-style power curve, or Szabo's curve as a "JP" mode; randomised uint32 phases by default; and an optional retrigger mode for punchy plucks and basses. Retriggered identical phases give a phasey transient that "sweeps" as voices drift. Randomised phases give a consistent, wide onset but vary in level from note to note.
- **Loudness law:** with free-running phases the voices are uncorrelated, so constant power (1/√N scaling, or Vital's centre/side √-normalisation) keeps perceived loudness roughly constant as the voice count and blend change. At very small detune and with retriggered phases the voices are coherent and sum more like N, so a pure 1/√N law gets louder. Consider a detune-dependent correction or a gentle limiter.
- **Szabo's side-gain curve is not loudness-normalised.** It rises steeply, which is part of the JP character. An emulation mode should keep it as is, with a separate output trim.
- **Stereo:** Vital's approach puts alternating sharp/flat voices in alternating L/R lanes and then cross-blends L↔R with an equal-power law. That is cheap and stays mono-compatible-ish because the centre voice is duplicated in L and R. For EDM mono compatibility, consider an option that keeps the centre voice exactly mono.
- **Key-tracked HPF:** any supersaw emulation needs a key-tracked HPF after the sum. Start with a 1st- or 2nd-order HPF at 1–2.5×f0 and tune by ear and spectrum, because the sources conflict.

### Gaps
- Szabo's thesis PDF could not be retrieved: the KTH NADA host is discontinued and the egress proxy blocked it. The polynomials above come from a third-party transcription that many implementations agree on. His exact phase-behaviour findings and HPF order and cutoff are not verified first-hand.
- The JP-8000's internal sample rate was not found in a reliable source.

---

## 2. Anti-aliasing methods: PolyBLEP/PolyBLAMP, minBLEP, BLIT, mipmapped wavetables, oversampling

### Takeaway
For analog-shape oscillators such as saw/square/supersaw, PolyBLEP is the best quality-per-CPU choice. The 2nd-order version costs about 8 ops per discontinuity and is alias-free to about 2.1 kHz f0 at 44.1 kHz. The 4th-order B-spline version is alias-free to about 7.8 kHz at roughly 28 ops. PolyBLAMP handles slope discontinuities (triangle, clipping) the same way. For arbitrary wavetables, use band-limited tables built by FFT: one per octave as a minimum, or Vital's per-pitch FFT rebuild. Oversampling is mainly for nonlinear warps and FM, not for already band-limited tables.

### Cited Findings
- Välimäki, Pekonen & Nam, "Perceptually informed synthesis of bandlimited classical waveforms using integrated polynomial interpolation", JASA 2012 — [PDF](https://mac.kaist.ac.kr/pubs/ValimakiPeknenNam-jasa2012.pdf):
  - The 2nd-order PolyBLEP equals the integrated linear interpolation kernel: "a closed-form, second-order polynomial approximation of the bandlimited step function".
  - Highest perceptually alias-free sawtooth f0 at 44.1 kHz (Table VIII):

    | Method | Highest alias-free f0 |
    |---|---|
    | LUT-BLEP K=4 | 358 Hz |
    | LUT-BLEP K=32 | 2036 Hz |
    | PolyBLEP 2nd order | 2135 Hz |
    | Lagrange PolyBLEP NI=3 | 3236 Hz |
    | B-spline NI=3 | 4591 Hz |
    | LUT-BLEP K=64 | 4595 Hz |
    | Lagrange NI=4 | 5134 Hz |
    | B-spline NI=4 | 7845 Hz |

  - Cost: 2nd-order PolyBLEP is "8 operations" (4 mul + 4 add). LUT-BLEP with 32 samples is "almost 450 operations". 4th-order Lagrange is 14 mul + 15 add; 4th-order B-spline is 13 mul + 15 add. Add one multiply for the fractional delay, plus a division when f0 changes.
  - Compared with BLIT and DPW: the 3rd-order B-spline BLIT (4593 Hz) is essentially equal to the 3rd-order B-spline PolyBLEP. 2nd-order DPW crosses the 10 dB NMR threshold at about 900 Hz, versus over 2 kHz for 2nd-order PolyBLEP, and PolyBLEP is more than 30 dB better at 2 kHz. Even orders are recommended, and the 4th-order B-spline PolyBLEP is the overall recommendation. B-spline variants need a post-equaliser (Table IV gives b0/b1: B-spline NI=4 → 0.3564, 1.6292) to correct passband droop.
- Common 2-point PolyBLEP code: with `t` = phase in [0,1) and `dt` = phase increment, use `t<dt: x=t/dt; return 2x − x² − 1` and `t>1−dt: x=(t−1)/dt; return x² + 2x + 1`. Subtract it from the naive saw `2t−1`. For a square, add it at the rising edge and subtract at the falling edge, offset by 0.5 — [Metafunction: BLITs & BLEPs](https://metafunction.co.uk/post/all-about-digital-oscillators-part-2-blits-bleps).
- BLIT (Stilson & Smith, 1996): integrate a band-limited impulse train (a sinc train) to get a saw. A bipolar train gives a square, and integrating twice gives a triangle. It is expensive in real time, so it is often tabulated, and it has ringing near the band edge. BLEP / minBLEP (Brandt 2001) tabulate the residual of an integrated windowed-sinc step. minBLEP uses a minimum-phase lowpass, so it is causal with no lookahead — [Metafunction](https://metafunction.co.uk/post/all-about-digital-oscillators-part-2-blits-bleps). A MATLAB/Octave minBLEP table generator is on [musicdsp.org #211](https://musicdsp.readthedocs.io/en/latest/Synthesis/211-matlab-octave-code-for-minblep-table-generation.html). VCV Rack ships a minBLEP implementation in `include/dsp/minblep.hpp` ([VCV Rack source mirror](https://git.kx.studio/VCVRack/Rack/src/branch/v2/include/dsp/minblep.hpp); listed in search results, not read).
- PolyBLAMP: Esqueda, Välimäki & Bilbao, "Rounding Corners with BLAMP", DAFx-2016. It applies a polynomial band-limited ramp correction at first-derivative discontinuities (triangle waves, hard clipping, half- and full-wave rectification). It reduces aliasing by up to 50 dB, improves SNR by about 20 dB, and is "a more efficient tool for alias reduction than oversampling" — [DAFx archive](https://dafx.de/paper-archive/details/xRM0GiuF-JmeN0_aaP9VNg).
- Mipmapped wavetables: the usual approach "creates multiple copies of each cycle and bandlimits them in intervals of octaves", then picks the table for the current pitch. Common single-cycle export sizes are 2048 for Serum and 256 for WaveEdit. Nigel Redmon's EarLevel wavetable series is the standard tutorial reference — [IggyLabs wiki](https://github.com/isabelgk/IggyLabsModules/wiki/Oscillators).
- Christoph Hart (HISE) says mip-mapped tables are "already band-limited at creation so there are no frequencies above nyquist that can alias", so oversampling a plain table oscillator is mostly pointless. Cubic interpolation over linear mainly helps when modulating quickly between tables and is "rather cosmetic". A user reported residual aliasing on certain notes (A3–B3), which suggests leakage when the mip level choice is borderline — [HISE forum](https://forum.hise.audio/post/79823).
- Vital's table oscillator: see section 3 (FFT rebuild per pitch every 7 ms, Catmull-Rom read).
- Vital global oversampling: the parameter `oversampling` takes indexed values 0–3 (default 1) and is applied as `1 << value`, i.e. 1×/2×/4×/8×. It is automatically halved for each doubling of host sample rate above 44.1 kHz (`sample_rate_mult = sample_rate/44100`; while >1, `oversample >>= 1`). It applies to the voice handler, effect chain and output stage — [sound_engine.cpp](https://github.com/mtytel/vital/blob/main/src/synthesis/synth_engine/sound_engine.cpp), [synth_parameters.cpp](https://github.com/mtytel/vital/blob/main/src/common/synth_parameters.cpp).
- Own benchmark: a 47-tap half-band FIR decimator, direct form without exploiting zero taps, costs 2.5 ns per output sample (mono). That is about 0.012% of one core per channel at 48 kHz per 2:1 stage. A 2nd-order PolyBLEP saw costs 0.51 ns per voice-sample versus 0.35 ns for a naive saw (+47%), with 16 voices in a SIMD-friendly loop.

### Inferences
- **Trade-off summary:**

  | Method | Cost | Quality | Best for | Notes |
  |---|---|---|---|---|
  | Naive | cheapest | poor | — | — |
  | PolyBLEP 2nd | +~50% over naive | alias-free to ~2 kHz f0 | default VA saw/pulse/supersaw | works with hard sync (BLEP at the reset point with the step height) |
  | PolyBLEP 4th (B-spline) | ~3–4× PolyBLEP-2 ops | alias-free to ~7.8 kHz | "HQ" mode | needs a post-EQ |
  | minBLEP (table) | 16–64 taps per event | excellent | — | needs an accumulation buffer; cost scales with event density (high f0, many unison voices) |
  | BLIT | — | similar to BLEP | — | integrator drift/DC issues; not preferred today |
  | Mip wavetables | cheap per sample | — | arbitrary waves | memory: frames × levels × size |
  | Oversampling | 2–8× oscillator cost + filter | — | nonlinear warps, FM/PM, waveshaping, hard clipping | — |

- **Unison cost driver:** supersaw at 16 voices × polyphony 8 × stereo is about 256 saw lanes. Per-voice BLEP cost dominates, so keep the oscillator loop branch-light. Use a SIMD mask for "near discontinuity" rather than `if`.
- **Mip levels:** for wavetables, one table per octave (10–11 levels for 2048-sample frames, covering 1024 harmonics down to 1) is the minimum. Crossfade between adjacent levels by fractional octave to avoid timbre steps on glides. Alternatively, bias the level so the top octave keeps some headroom: the HISE "A3–B3" report is consistent with an octave boundary picking too-bright a table.
- **JUCE hints** (general knowledge, not verified in this session): `juce::dsp::Oversampling` offers polyphase IIR (low latency) and FIR equiripple (linear-phase) half-band stages. Use IIR for the oscillator/voice path where latency matters, and per-voice or post-voice-sum depending on where the nonlinearity sits.

### Gaps
- No published wall-clock CPU numbers for minBLEP vs PolyBLEP were found. The paper's op counts and the own benchmark are the only quantitative data here.
- The polynomial orders used in the BLAMP paper were not in the abstract retrieved.

---

## 3. How Serum and Vital handle aliasing; wavetable morphing and spectral warps

### Takeaway
Vital keeps every wavetable frame in the frequency domain. Every 7 ms per voice, it rebuilds a band-limited 2048-sample cycle by inverse FFT, zeroing every harmonic above Nyquist for the current pitch (including sync multiplication). Successive buffers are crossfaded, and the cycle is read with SIMD Catmull-Rom cubic interpolation. Spectral morphs are therefore free to be arbitrary spectral operations. Phase-domain warps (bend, squeeze, PWM, FM, quantize) are applied at read time and rely on global oversampling. Serum is closed source. Xfer only claims "ultra high-precision resampling" with about −150 dB SNR on a 1 kHz saw at 44.1 kHz, SSE2-optimised.

### Cited Findings
**Vital internals** ([wavetable.h/.cpp](https://github.com/mtytel/vital/blob/main/src/synthesis/lookups/wavetable.cpp), [wave_frame.h](https://github.com/mtytel/vital/blob/main/src/synthesis/lookups/wave_frame.h), [synth_oscillator.cpp](https://github.com/mtytel/vital/blob/main/src/synthesis/producers/synth_oscillator.cpp), [spectral_morph.h](https://github.com/mtytel/vital/blob/main/src/synthesis/producers/spectral_morph.h), [synth_constants.h](https://github.com/mtytel/vital/blob/main/src/common/synth_constants.h)):
- **Storage:**
  - Frame size: `kWaveformBits = 11` → `kWaveformSize = 2048`.
  - Up to `kNumOscillatorWaveFrames = 257` frames.
  - Per frame: time-domain data, `frequency_amplitudes`, `normalized_frequencies` (unit complex phase) and `phases` for `kNumHarmonics = 1025` bins.
  - `postProcess` normalises the table and fills in phase for near-silent harmonics (amplitude < 0.1) from neighbouring frames, so morphing between frames does not produce phase jumps.
- **Band-limiting:**
  - `last_harmonic` is computed from the voice's current phase increment: `bin = log2(1/phase_inc)`, then `last_harmonic = 2048·2^-(11+1−bin)`, clamped to 1024. This is effectively `0.5/phase_inc`, i.e. the harmonic count up to Nyquist.
  - All bins above `last_harmonic` are set to 0, then the cycle is inverse-FFT'd (`transformAndWrapBuffer`).
  - This is a continuous, per-pitch cutoff rather than octave mips.
- **Rebuild rate:**
  - `kWavetableFadeTime = 0.007 s`. Each voice recomputes its buffers every 7 ms.
  - It renders by crossfading between `last_buffers_` and `wave_buffers_` across that window (`interpolateMultipleBuffers`), which also gives smooth wavetable-position and morph modulation.
  - Buffers are also rebuilt on reset/retrigger.
- **Read interpolation:** Catmull-Rom cubic via a 4×4 matrix SIMD gather (`getCatmullInterpolationMatrix`). Phases are 32-bit integers, and the fractional part comes from the low bits (`kIntermediateBits`).
- **Sync and formant warps:** for these, the band-limit frequency is `phase_inc × detune × distortion × kMaxSync`, where `kMaxSync = 16`. The table is pre-band-limited for the sped-up read, so hard-sync-style warps stay alias-controlled.
- **Spectral unison:**
  - When "Spectral Unison" is on and there is frame spread, morph spread or Vocode, each unison pair gets its own IFFT buffer.
  - Otherwise, one buffer (band-limited for the centre voice) is shared by all unison voices. That is cheaper, but detuned-sharp voices can marginally exceed Nyquist.
- **Spectral morph modes** (`SpectralMorph` enum), operating on harmonic bins before the IFFT:

  | Mode | Notes |
  |---|---|
  | Vocode (even/odd) | `kMaxEvenOddFormantShift = 2` |
  | Formant Scale | `kMaxFormantShift = 1` |
  | Harmonic Scale (stretch) | ×up to `kMaxHarmonicScale = 4` |
  | Inharmonic Scale | ×up to 12 |
  | Smear | — |
  | Random Amplitudes | 16 stages |
  | Low Pass / High Pass | brickwall in the harmonic domain |
  | Phase Disperse | scale 0.05 |
  | Shepard Tone | — |
  | Skew (wavetable frame skew across harmonics) | scale 16 |

- **Phase-domain "distortion" (warp) modes** (`DistortionType` enum): None, Sync, Formant, Quantize, Bend, Squeeze, Pulse Width, FM ← Osc A/B/Sample, RM ← Osc A/B/Sample. Implementations:
  - Bend: cubic phase polynomial `p³ + (3d+o)(p²−p³) + (3d−o)(p − 2p² + p³)`, with `o = 2(d − d²)`.
  - Squeeze: a piecewise-linear phase pivot.
  - Sync: `int(phase·d)·16`.
  - Pulse Width: phase scaled by d and clamped, with a zero window after the end.
  - FM: phase offset = modulator × amount × kFmPhaseMult × 48.
  - RM: amplitude window `lerp(1, mod, amount)`.
  - These apply to the phase used to read the already band-limited buffer, so FM, bend and squeeze can create components above Nyquist.

**Serum**
- "Playback of wavetables requires digital resampling… In Serum, the native-mode (default) playback of oscillators operates with an ultra high-precision resampling", "yielding… −150 dB on a sawtooth played at 1 Khz at 44100". Other synths are said to produce "artifacts as high as −36 dB to −60 dB". Playback is "aggressively optimized using SSE2 instructions". There are up to 256 frames per oscillator and up to 16 unison voices. Warp includes "FM/AM/RM/Oscillator Sync" and Remap graph modes — [Xfer Serum page](https://www.xferrecords.com/serum).
- Serum warp modes described in a Serum 2 thesis: Bend (two halves bent symmetrically), Asym, PWM, Flip, Remap 1–4 (user-drawn phase→amplitude curves over 1–4 segments), plus distortion-in-oscillator modes (tube, soft/hard clip, linear/sine fold) and AM/FM/RM/PD/self-mod. The Global tab sets oversampling — [HdM Stuttgart thesis](https://curdt.home.hdm-stuttgart.de/PDF/Serum%202.pdf).
- Serum 2 has 5 oscillator modes: Wavetable, Multisample, Sample, Granular (up to 256 grains) and Spectral (analysis/resynthesis with independent time and pitch, "Timbre Shifting", scan rate). It adds Dual Warp, "true FM", phase distortion from filters, and smooth WT interpolation "without morph tables" — [Serum 2 What's New PDF](https://static.xferrecords.com/Serum%202%20What's%20New.pdf).

### Inferences
- **Vital's architecture is ideal for a "huge layered" EDM synth.** Spectral operations are O(harmonics) per 7 ms rather than per sample, aliasing is controlled by construction, and modulation is smoothed by the buffer crossfade. The cost is one 2048-point real IFFT per unison buffer per 7 ms per voice; see section 6 for numbers.
- **A 7 ms update means spectral parameters effectively update at about 143 Hz.** That is fine for LFO morphs, but not for audio-rate spectral modulation.
- **Phase-domain warps need oversampling.** For a premium design, offer per-oscillator 2×/4× oversampling only when phase warps or FM are active, rather than global oversampling.
- The "−150 dB" Serum claim is a marketing figure for one test case. It implies band-limited resampling far better than linear interpolation, but the method is not disclosed.

### Gaps
- Serum's actual algorithm (mip count, interpolation kernel, whether it rebuilds per pitch) is undisclosed. The KVR thread with posts that might cover it could not be fetched (site error).
- The exact names of Vital's 4 oversampling settings (`kOversamplingNames`) were not confirmed from source. The `1 << value` mapping is from code.

---

## 4. Additive / spectral synthesis efficiency (SIMD, recursive oscillators vs tables vs IFFT) and products

### Takeaway
With SIMD, a per-partial recursive (complex rotor) oscillator bank costs about 0.18 ns per partial-sample in the own benchmark. That puts 512 partials × 1 voice at about 0.44% of a core at 48 kHz, and 512 partials × 16 unison × 8 voices at about 56% of a core. So brute-force oscillator banks are feasible for a few hundred partials per voice, but unison multiplies the cost. IFFT overlap-add (Rodet & Depalle) makes cost nearly independent of partial count. Vital-style "spectral-to-wavetable" IFFT per block is the cheapest option for harmonic content. Commercial engines: Harmor (516 partials per voice per unison voice), Razor (320 partials), Pigments Harmonic (512 partials).

### Cited Findings
- **Harmor:**
  - "up to 516 sine wave (partials), per note, per unison voice". The partial count is adjustable from 12 to 516, and lowering it saves CPU.
  - Image resynthesis uses 255 (Generic) or 516 (High precision) partials.
  - There is no audio stream inside the engine. Filters, phaser, prism, harmonizer, blur and pluck operate on per-partial frequency/amplitude/phase tables.
  - Quality controls: Precision (Average/High/Perfect), Grain (envelope granularity), Safety (caps partials above the root), Threaded, and HQ-render-only.
  - CPU notes: harmonic (vertical) blur is among the biggest costs; low root notes generate more partials; unison and pitch bend add cost.
  - Timbre 2 reuses Timbre 1's phases to avoid cancellation.
  - Source: [Image-Line Harmor manual](https://www.image-line.com/support/flstudio_online_manual/html/plugins/Harmor.htm).
- **Razor** (Errorsmith/NI, built in Reaktor 5): "up to 320 partials". Filters, stereo imaging, reverbs and delays are created by manipulating individual partials, with continuously variable filter slopes from 0 to −36 dB/oct. Partials can be panned individually — [NI Razor](https://www.native-instruments.com/products/komplete/synths/razor/).
- **Pigments 3 Harmonic engine:** up to 512 partials. Controls include harmonic ratios, odd/even balance, gain decay, partial panning (manual/random/periodic), a Window that emphasises a frequency range, Cluster (groups of partials with ratios), dual formant filters and a Shepard mode — [Sound On Sound review](https://www.soundonsound.com/reviews/arturia-pigments-3?amp).
- **Vital's "spectral morph"** is not an oscillator bank. It is a harmonic-domain operation followed by a 2048-point IFFT every 7 ms (see section 3) — [spectral_morph.h](https://github.com/mtytel/vital/blob/main/src/synthesis/producers/spectral_morph.h).
- **IFFT synthesis (Rodet & Depalle 1992):**
  - An explicit oscillator bank is expensive with many partials, so IFFT synthesis writes each partial's window-transform main lobe into a spectral buffer.
  - They used a triangular window (squared-sinc main lobe), which gives a linear crossfade frame-to-frame. Side lobes are neglected.
  - Noise can be added directly in the frequency domain.
  - Limitation: frequencies are fixed per frame, so fast glissandi "stair-case". Goodwin & Kogon tabulated swept main lobes to fix this.
  - Source: [JOS, SASP: Inverse FFT Synthesis](https://ccrma.stanford.edu/%7ejos/sasp/Inverse_FFT_Synthesis.html); original paper [Rodet & Depalle 1992 PDF](https://www.ee.columbia.edu/~dpwe/papers/RodetD92-envs.pdf).
- **Freed, Rodet & Depalle:** "Synthesis and control of hundreds of sinusoidal partials on a desktop computer without custom hardware" (the FFT⁻¹ method), which also adds coloured noise efficiently — [Adrian Freed page](https://www.adrianfreed.com/synthesis-and-control-hundreds-sinusoidal-partials-desktop-computer-without-custom-hardware).
- **Own benchmark (AVX2, float, 256 partials, block 512):**
  - `sinf` per partial: 1.07 ns per partial-sample.
  - Complex-rotor recursion: 0.18 ns per partial-sample, about 6× faster. The rotor is `(re,im) ← (re·c − im·s, re·s + im·c)`, auto-vectorised with `#pragma omp simd` and renormalised once per block with `g = 1.5 − 0.5(re²+im²)`.
- **SMS deterministic synthesis:** linearly interpolated amplitude and frequency across each hop, with phase as the integral of frequency. The paper calls simulating noise with sinusoids "extremely expensive" and recommends an IFFT for the stochastic part — [Serra & Smith 1990, CMJ](https://audiolabs-erlangen.de/content/05_fau/professor/00_mueller/02_teaching/2024s_sarntal/02_group_SYNTH/1990_SerraS_SpectralModelingSynthesis_CMJ.pdf).

### Inferences
- **Choosing an engine:**
  - **Harmonic (integer-ratio) partials with block-rate spectral changes:** do what Vital does. Build the spectrum, IFFT one cycle, and play it as a wavetable. The cost is independent of partial count and Nyquist handling is trivial: zero bins ≥ Nyquist/f0.
  - **Inharmonic partials, per-partial pitch envelopes, or per-partial panning (Razor/Harmor-style):** use an oscillator bank in structure-of-arrays layout (`float re[N], im[N], cosw[N], sinw[N], amp[N], ampTarget[N]`). Process 8 partials per AVX register or 4 per NEON/SSE register.
- **Recursive oscillator choice:** prefer the complex rotor (two state variables, stable with periodic renormalisation) over the 2nd-order "magic circle" / Goertzel recursion `y[n] = 2cos(w)y[n−1] − y[n−2]`. The rotor is amplitude-stable and allows a frequency change by updating (c,s). The 2-pole form drifts in amplitude when w changes.
  - A practical frequency-change method: reset phase-coherently at block boundaries, or interpolate (c,s) by multiplying by a small "delta rotor" per sample (chirp).
- **Per-partial smoothing:** ramp amplitude linearly per block in the same loop (`amp += dAmp`). That costs one FMA per partial-sample.
- **Nyquist handling:** fade partials out with a smooth gain over roughly 0.45–0.5·fs, rather than a hard cut, so partials crossing Nyquist on pitch glides do not click. Skip partials above Nyquist entirely by sorting or loop-bounding by `kmax = floor(0.5·fs/f0)` to save CPU. Harmor's "Safety" and partial-count controls are this idea, and low notes generate more partials.
- **IFFT overlap-add for oscillator-bank-like flexibility:** with N = 1024 and hop = 256 (4× overlap), cost per voice is about 1 IFFT per 256 samples plus about 5–9 bins written per partial, which is cheaper than an oscillator bank above roughly 100 partials. Main caveats are the stair-cased glides, transient smearing within the frame, and latency of one frame.

### Gaps
- No published per-partial CPU figures from Harmor, Razor or Pigments were found. Their implementation (oscillator bank vs IFFT) is not documented publicly.
- No primary source was found for the NEON-specific throughput of rotor banks. The ratio should be similar, about 4 lanes per instruction versus AVX's 8.

---

## 5. Spectral analysis of samples into partials (STFT peak picking, SMS) and real-time feasibility

### Takeaway
SMS (Serra & Smith 1990) is the canonical pipeline:
1. A windowed STFT (Kaiser or Hamming, about 4 periods long, zero-padded to ≥2× window).
2. Local-maximum peak picking with parabolic interpolation.
3. Guide-based peak continuation with sleep/kill rules.
4. Optional harmonic pitch locking.
5. Deterministic resynthesis by interpolated sinusoids.
6. A residual modelled as a line-segment spectral envelope, resynthesised by random-phase IFFT and overlap-add.

Analysis is offline-class: it is complex and needs parameter tuning. Resynthesis is real-time.

### Cited Findings (all from [Serra & Smith 1990, CMJ](https://audiolabs-erlangen.de/content/05_fau/professor/00_mueller/02_teaching/2024s_sarntal/02_group_SYNTH/1990_SerraS_SpectralModelingSynthesis_CMJ.pdf) unless noted)
- **Window:** Kaiser is "a good general purpose choice". Rectangular has a 2-bin main lobe and −13 dB side lobes. Hamming has a 4-bin main lobe and −43 dB side lobes. Length is about 4 periods of the lowest f0.
- **FFT size and hop:** FFT size is the first power of two at least twice the window length, zero-padded. Hop = window / main-lobe width in bins, e.g. a quarter window for Hamming.
- **Peak picking:** a peak is a local maximum, with prominence measured against neighbouring valleys. Refine it by parabolic interpolation over 3 bins of the log-magnitude spectrum, which is exact for a Gaussian window. The paper does not print the formula.
- **Peak continuation:**
  - Each guide takes the nearest peak within a maximum deviation, and conflicts are resolved recursively in favour of the closer guide.
  - Guide update: `h = α(g − f) + f`.
  - An unmatched guide ramps to zero over one hop, "sleeps", then is killed.
  - New guides are born from the strongest unclaimed peaks with minimum spacing.
  - Tracking backward from the stable end of the sound helps through noisy attacks.
- **Harmonic mode:** use the 3 strongest peaks to find a common fundamental, then reset guides to its harmonic series.
- **Deterministic synthesis:** `d(m) = Σ A_r(m) cos θ_r(m)`, with A and ω linearly interpolated over the hop. Optional line-segment data reduction.
- **Residual and stochastic part:**
  - Residual magnitude `|E| = ||X| − |D||`, which requires identical STFT parameters for analysis and resynthesis.
  - The envelope is a line-segment approximation, chosen over LPC for flexibility.
  - Synthesis: interpolate the envelope to N/2 points, apply random phase each frame, IFFT, apply a Hann window of about 4×hop, and overlap-add.
- **Feasibility:** synthesis "can be performed in real-time with current technology" (1990), with analysis precomputed. Analysis is "a complex algorithm" needing manual parameters.
- **Products:** Harmor's resynthesis re-analyses the sample on each edit and recalculates harmonic phase, while image mode converts one-way to frequency/gain planes ([Harmor manual](https://www.image-line.com/support/flstudio_online_manual/html/plugins/Harmor.htm)). Serum 2's Spectral oscillator does analysis/resynthesis with independent time/pitch and timbre shifting ([Serum 2 What's New](https://static.xferrecords.com/Serum%202%20What's%20New.pdf)). Vital imports audio into wavetables with a pitch detector (`common/wavetable/pitch_detector.cpp`, `file_source.cpp`, with `detectPitch(max_period = 2048)`) — [Vital source tree](https://github.com/mtytel/vital/tree/main/src/common/wavetable).

### Inferences
- **Two-tier design for a synth.**
  - **Offline (on sample drop):** run harmonic-locked SMS-style analysis on a worker thread. Store per-frame harmonic amplitudes and phases, either as a 2048-sample wavetable with up to 256 frames (Vital/Serum style) or as partial tracks plus a residual envelope.
  - **Real time:** play back via the IFFT wavetable path, or via the oscillator bank for inharmonic sounds.
- **Analysis cost:** analysing a few seconds of audio with N = 4096 and hop = 512 is on the order of milliseconds to tens of ms with a decent FFT. Live real-time analysis of an input (vocoder-like) is feasible at block latency equal to the window length.
- **Parabolic interpolation formula** (standard, from JOS SASP; not quoted in the fetched text): with dB magnitudes α, β, γ at bins k−1, k, k+1, `p = 0.5(α − γ)/(α − 2β + γ)`, peak at bin k+p, and `mag = β − 0.25(α − γ)p`.

### Gaps
- Phase-vocoder and reassignment refinements (Laroche & Dolson, Auger-Flandrin) were not researched within the tool budget.
- No published real-time benchmarks for SMS analysis on modern CPUs were found.

---

## 6. Typical CPU numbers (per unison voice, per partial, per oversampling factor)

### Takeaway
Published CPU numbers are scarce. Op counts from Välimäki 2012 and the own micro-benchmark give a usable budget model. At 48 kHz on one 2.1 GHz AVX2 core:
- One band-limited saw lane costs about 0.0025% of a core, so a 16-voice stereo supersaw for one note is about 0.08%.
- A cubic-interpolated wavetable lane costs about 0.005%.
- One rotor partial costs about 0.0009%.
- A 2× oversampling stage adds the oscillator cost ×2 plus about 0.012% per channel for a half-band filter.
- Real synths land 10–50× higher because of modulation, filters, envelopes, cache misses and non-ideal loops.

### Cited Findings
- **Op counts:** 2nd-order PolyBLEP is 8 ops per corrected sample; 4th-order B-spline PolyBLEP is 28 ops; a 32-tap LUT-BLEP is about 450 ops — [Välimäki et al. 2012](https://mac.kaist.ac.kr/pubs/ValimakiPeknenNam-jasa2012.pdf).
- **Own benchmark** (Xeon @ 2.1 GHz virtualised, AVX2, `-O3 -ffast-math`, hot cache, 48 kHz; source `/tmp/claude-0/-home-claude/534cc1aa-2eb8-5ffb-9490-84c05b9af6ae/scratchpad/bench/b.cpp`; not a published source):

  | Operation | Cost per unit | Core % per unit at 48 kHz |
  |---|---|---|
  | Naive saw | 0.345 ns per voice-sample | 0.0017% |
  | PolyBLEP saw (2nd order) | 0.508 ns | 0.0024% |
  | Mip wavetable 2048, linear interpolation | 0.624 ns | 0.0030% |
  | Mip wavetable 2048, Catmull-Rom cubic | 1.118 ns | 0.0054% |
  | Additive with `sinf` | 1.068 ns per partial-sample | — |
  | Additive with complex rotor (vectorised) | 0.179 ns per partial-sample | 0.0009% |
  | Unoptimised radix-2 complex FFT, 2048 points | 37 µs per transform | — |
  | 47-tap half-band FIR decimator (no zero-tap skipping) | 2.5 ns per output sample | 0.012% |

  - For the FFT: an optimised real FFT (PFFFT/FFTW/vDSP/IPP-class) would plausibly be about 5–10× faster, but that was not measured. At a Vital-like rate of one transform per 7 ms (336 samples at 48 kHz) per buffer, 37 µs is about 0.53% of a core per buffer stream.
- **Vital per-buffer workload:**
  - With Spectral Unison active, Vital computes a buffer per unison pair: up to 8 pairs × 2 lanes per voice.
  - Otherwise it computes one buffer pair per voice and shares it.
  - Every 7 ms. Source: [synth_oscillator.cpp](https://github.com/mtytel/vital/blob/main/src/synthesis/producers/synth_oscillator.cpp).
- **Harmor:** CPU scales with partial count (12–516), low root notes (more partials below Nyquist), unison order and harmonic blur — [Harmor manual](https://www.image-line.com/support/flstudio_online_manual/html/plugins/Harmor.htm).
- **Real-world CPU usage:** a JUCE forum thread reports unison with spread hitting about 70% CPU in a naive implementation ([JUCE forum](https://forum.juce.com/t/cpu-usage-for-unison-with-spread-of-voices-is-too-high-70/28687)). That thread was not read in detail.

### Inferences (budget model)
- **Supersaw, 8-note chord × 16 unison × stereo** = 256 PolyBLEP lanes ≈ 256 × 0.0024% ≈ 0.6% of a core for raw oscillators. At 2× oversampling it is about 1.2%, plus the decimator.
  - With 3 oscillators and layers, oscillators stay under about 5%. Filters, per-voice FX and modulation dominate.
- **Vital-style IFFT wavetables with spectral unison:**
  - Worst case is 16 IFFTs per voice per 7 ms. With an optimised 2048-point real FFT at about 5 µs, that is about 80 µs per 7 ms ≈ 1.1% of a core per voice, times 8 voices ≈ 9%.
  - Without spectral unison it is about 1/8 of that.
  - The IFFT path therefore dominates over per-sample reading, which is about 16 × 2 × 0.0054% ≈ 0.17% per voice.
- **Additive oscillator bank:** 512 partials × 8 voices ≈ 3.5% of a core. Adding 16-voice unison per partial gives about 56%, which is why Harmor-style "unison per partial" is expensive.
  - Prefer IFFT-wavetable rendering for unison stacks, or apply unison after additive rendering of a single cycle (Vital's approach).
- **Oversampling factor cost** ≈ (oscillator + nonlinear-processing cost) × factor, plus filter cost per stage: about 0.012% per channel per 2:1 FIR stage, and roughly half that with polyphase/zero-tap skipping or IIR half-bands.
- **Caveat:** all own-benchmark figures are best-case micro-benchmarks. Budget 10–30× headroom for production code with per-sample modulation, denormal handling and cache pressure from 257-frame × 2048-sample tables (≈2 MB per oscillator in float).

### Gaps
- No vendor-published CPU per voice for Serum/Vital/Harmor was found. KVR benchmark threads could not be fetched (site errors).
- ARM/NEON (Apple Silicon) numbers were not measured.
