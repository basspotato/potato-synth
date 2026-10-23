#pragma once
#include "Engine.h"
#include "Glide.h"
#include "Filter.h"
#include "Loudness.h"
#include "Simd.h"
#include "Unison.h"
#include "Wavetables.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <memory>
#include <vector>

namespace ps
{
// Spectral morph: the waveform is built from its harmonic spectrum, live. Two source spectra A and B are
// morphed in log amplitude and log frequency, then shaped by harmonic operations:
//
//   Morph    each source is a smooth envelope (over continuous harmonic number) plus a fine pattern (which
//            harmonics are missing). The envelopes are slid toward each other in log frequency by the gap
//            between their centroids while they crossfade in dB, so a peak travels instead of one fading out
//            and another fading in; the fine patterns crossfade in dB, phases along the shortest arc.
//   Tilt     dB per octave of harmonic number, +-6
//   Odd/Even toward odd only (even harmonics down 40 dB) or even only (odd harmonics above the fundamental)
//   Formant  the envelope is read at h / 2^(st/12), +-24 semitones: the resonances move, the pitch stays
//   Smear    the spectrum is averaged over up to +-0.5 octave (in power) and the phases blend toward fixed
//            random ones: hollow and buzzy shapes melt into smooth, softer-edged ones
//   Shepard  a cosine emphasis, one octave per period in log harmonic number, up to 30 dB deep; its
//            position moves at Shepard Rate, and since it repeats every octave the brightness climbs (or
//            falls) forever without ever arriving
//
// The spectrum is rebuilt at most every 512 samples while something moves, by inverse FFT into the band
// limits the sounding voices use (the Wavetables levels: the top harmonic stays under the alias floor, so
// it never aliases), and each rebuild crossfades from the previous one over those 512 samples, so moving a
// control never zippers. Every band limit is normalised to a sine's RMS and given the K-weighted loudness of
// a sine at the pitch it plays, as the Wavetable engine does, so the spectrum's shape never moves the
// loudness. Voices read the frames with cubic Hermite interpolation through JP or Modern unison.
class SpectralEngine final : public Engine
{
public:
    enum Slot { sourceA, sourceB, morph, tilt, oddEven, formant, smear, shepard, shepardRate,
                unison, detune, mix, spread, numUsedSlots };

    static constexpr int numSources = 9;
    static constexpr int numUnison = 5;

    const EngineInfo& info() const override
    {
        static const EngineInfo i { "spectral", "Spectral", 16, makeSlots(), true };
        return i;
    }

    void prepare (double sampleRate, int, int numVoices) override
    {
        sr = (float) sampleRate;
        voices.assign ((size_t) numVoices, {});
        for (auto& v : voices)
            v.rng = 0x9E3779B9u * (uint32_t) (&v - voices.data() + 1);
        fft = std::make_unique<juce::dsp::FFT> (11);
        fftBuffer.assign (2 * frameSize, 0.0f);
        for (auto& b : builds)
        {
            b.data.assign ((size_t) numLevels * stride, 0.0f);
            b.valid.fill (false);
            b.amp.assign ((size_t) maxHarmonic, 0.0f);
            b.phase.assign ((size_t) maxHarmonic, 0.0f);
            b.loudness.fill (1.0f);
            b.loudnessValid.fill (false);
        }
        work.assign ((size_t) maxHarmonic, 0.0f);
        prefix.assign ((size_t) maxHarmonic + 1, 0.0);
        smooth = target;
        rebuild (false);
        fadeLeft = 0;
        usedLevels = 0;
    }

    void reset() override
    {
        for (auto& v : voices)
            v.active = false;
    }

    // Once per rendered chunk: smooth the controls, move the Shepard position and rebuild if anything moved
    void beginBlock (int numSamples) override
    {
        const float coef = 1.0f - std::exp (-(float) numSamples / (0.02f * sr));
        bool moving = false;
        for (int i = 0; i < numUsedSlots; ++i)
        {
            if (isChoice (i))
                smooth[(size_t) i] = target[(size_t) i];
            else
                smooth[(size_t) i] += (target[(size_t) i] - smooth[(size_t) i]) * coef;
            moving = moving || std::abs (smooth[(size_t) i] - built[(size_t) i]) > 1.0e-4f;
        }
        const float rate = shepardRateHz (smooth[shepardRate]);
        if (shepardDepth (smooth[shepard]) > 0.0f && std::abs (rate) > 1.0e-4f)
        {
            shepardPos += rate * (float) numSamples / sr;
            shepardPos -= std::floor (shepardPos);
            moving = true;
        }

        fadeFrom = fadeLeft > 0 ? 1.0f - (float) fadeLeft / (float) rebuildInterval : 1.0f;
        fadeLeft = std::max (0, fadeLeft - numSamples);
        fadeTo = fadeLeft > 0 ? 1.0f - (float) fadeLeft / (float) rebuildInterval : 1.0f;
        sinceRebuild += numSamples;
        if (moving && fadeLeft == 0 && sinceRebuild >= rebuildInterval)
        {
            rebuild (true);
            fadeFrom = 0.0f;
            fadeTo = std::min (1.0f, (float) numSamples / (float) rebuildInterval);
            fadeLeft = std::max (0, rebuildInterval - numSamples);
        }
    }

    void noteOn (int index, const NoteEvent& e) override
    {
        auto& v = voices[(size_t) index];
        if (! v.active)
        {
            v.pitch.jumpTo (e.glideSeconds > 0.0 && e.glideFrom >= 0.0f ? e.glideFrom : (float) e.note);
            for (size_t k = 0; k < v.phase.size(); ++k)
                v.phase[k] = e.fixedPhase ? std::fmod (0.618034f * (float) k, 1.0f) : random (v);
            v.unisonMode = choice (target[unison], numUnison);
            v.layoutDetune = -1.0f;
            updateLayout (v);
            v.gain = v.layout.gain;
            v.switchGain = 1.0f;
            v.switching = false;
            v.level = -1;
            v.loudGain = -1.0f;
            v.active = true;
        }
        v.pitch.glideTo ((float) e.note, e.glideSeconds, sr);
    }

    void noteOff (int) override {}
    void endVoice (int index) override { voices[(size_t) index].active = false; }
    void setTune (float semitones) override { tune = semitones; }

    float detuneSpacingCents (float detuneValue) const override
    {
        UnisonLayout layout;
        layout.set ((UnisonLayout::Mode) choice (target[unison], numUnison), detuneValue, 1.0f, 0.0f);
        return layout.widestGapCents();
    }

    void setSlot (int slot, float value) override
    {
        if (slot >= 0 && slot < numUsedSlots)
            target[(size_t) slot] = juce::jlimit (0.0f, 1.0f, value);
    }

    void render (int index, float* left, float* right, int numSamples) override
    {
        auto& v = voices[(size_t) index];
        if (! v.active)
            return;
        v.pitchBend = voiceBend (index);
        const float invTotal = 1.0f / (float) numSamples;
        for (int pos = 0; pos < numSamples; pos += subBlock)
        {
            const int n = juce::jmin (subBlock, numSamples - pos);
            const float a = fadeFrom + (fadeTo - fadeFrom) * (float) pos * invTotal;
            const float b = fadeFrom + (fadeTo - fadeFrom) * (float) (pos + n) * invTotal;
            renderSubBlock (v, left + pos, right + pos, n, a, b);
        }
    }

    // ---- Slot mappings -----------------------------------------------------------------------------------
    static int choice (float v, int n) noexcept      { return juce::jlimit (0, n - 1, (int) (v * (float) n)); }
    static float choiceValue (int i, int n) noexcept { return ((float) i + 0.5f) / (float) n; }
    static float tiltDb (float v) noexcept           { return (v * 2.0f - 1.0f) * 6.0f; }
    static float formantSemis (float v) noexcept     { return (v * 2.0f - 1.0f) * 24.0f; }
    static float shepardDepth (float v) noexcept     { return v * 30.0f; }
    static float shepardRateHz (float v) noexcept    { const float x = v * 2.0f - 1.0f; return std::copysign (0.5f * x * x, x); }

    static const char* sourceName (int i) noexcept
    {
        static const char* const n[numSources] = { "Saw", "Square", "Triangle", "Pulse 25", "Sine", "Organ", "Vowel A", "Vowel O", "Glass" };
        return n[juce::jlimit (0, numSources - 1, i)];
    }

private:
    static constexpr int frameSize = Wavetables::frameSize, maxHarmonic = Wavetables::maxHarmonic, numLevels = Wavetables::numLevels;
    static constexpr int stride = Wavetables::stride, numPitchPoints = Wavetables::numPitchPoints;
    static constexpr int subBlock = 16, rebuildInterval = 512;
    static constexpr float voiceLevel = 0.25f;   // sine-RMS frames at -15 dBFS RMS, like the other engines
    static constexpr float floorDb = -140.0f;
    static constexpr float maxCrest = 4.0f;   // 12 dB

    static bool isChoice (int slot) noexcept { return slot == sourceA || slot == sourceB || slot == unison; }

    // ---- Sources: an envelope over continuous harmonic number x (dB) and a fine pattern per harmonic ------
    static float envelopeDb (int source, float x) noexcept
    {
        x = juce::jmax (1.0e-3f, x);
        const float l = std::log10 (x);
        switch (source)
        {
            case 0: case 1: case 3: return juce::jmin (24.0f, -20.0f * l);           // saw, square, pulse: 1/h
            case 2:                 return juce::jmin (24.0f, -40.0f * l);           // triangle: 1/h^2
            case 4:                 return x <= 1.0f ? 0.0f : -120.0f * juce::jmin (1.0f, x - 1.0f);   // sine
            case 5:                 return -9.0f * std::log2 (juce::jmax (1.0f, x)) * 0.33f;           // organ: nearly flat
            case 6: case 7:
            {
                // A saw through vowel formants (for a 110 Hz voice)
                static constexpr float formants[2][3] = { { 800, 1150, 2900 }, { 450, 800, 2830 } };
                const auto& fm = formants[source - 6];
                const float f = 110.0f * x;
                float env = 0.02f;
                for (int k = 0; k < 3; ++k)
                {
                    const float width = 0.12f * fm[k], d = (f - fm[k]) / width;
                    env += (k == 0 ? 1.0f : k == 1 ? 0.6f : 0.3f) * std::exp (-0.5f * d * d);
                }
                return juce::jmin (24.0f, -20.0f * l) + 20.0f * std::log10 (env);
            }
            default:                return juce::jmin (24.0f, -10.0f * l);           // glass: 1/sqrt(h)
        }
    }

    static float fineDb (int source, int h) noexcept
    {
        switch (source)
        {
            case 1: case 2: return h % 2 == 0 ? floorDb : 0.0f;
            case 3:
            {
                const float s = std::abs (std::sin (juce::MathConstants<float>::pi * (float) h * 0.25f)) * juce::MathConstants<float>::sqrt2;
                return s > 1.0e-6f ? 20.0f * std::log10 (s) : floorDb;
            }
            case 5:
            {
                static constexpr int bars[] = { 1, 2, 3, 4, 6, 8 };
                return std::find (std::begin (bars), std::end (bars), h) != std::end (bars) ? 0.0f : floorDb;
            }
            default: return 0.0f;
        }
    }

    static float sourcePhase (int source, int h) noexcept
    {
        const float pi = juce::MathConstants<float>::pi;
        switch (source)
        {
            case 2: return (h / 2) % 2 == 1 ? pi : 0.0f;                            // triangle: alternating signs
            case 3: return 0.5f * pi + (std::sin (pi * (float) h * 0.25f) < 0.0f ? pi : 0.0f);
            case 8: return glassPhases()[(size_t) h];
            default: return 0.0f;
        }
    }

    static const std::array<float, maxHarmonic>& glassPhases()
    {
        static const auto p = [] { std::array<float, maxHarmonic> a {}; juce::Random r (0x61a55); for (auto& x : a) x = r.nextFloat() * juce::MathConstants<float>::twoPi; return a; }();
        return p;
    }

    static const std::array<float, maxHarmonic>& smearPhases()
    {
        static const auto p = [] { std::array<float, maxHarmonic> a {}; juce::Random r (0x5ea7); for (auto& x : a) x = r.nextFloat() * juce::MathConstants<float>::twoPi; return a; }();
        return p;
    }

    // Power-weighted mean of log2 h over the first 64 harmonics: where a source's energy sits
    static float centroid (int source)
    {
        static const auto c = []
        {
            std::array<float, numSources> out {};
            for (int s = 0; s < numSources; ++s)
            {
                double w = 0.0, sum = 0.0;
                for (int h = 1; h <= 64; ++h)
                {
                    const double p = std::pow (10.0, (envelopeDb (s, (float) h) + fineDb (s, h)) / 10.0);
                    w += p;
                    sum += p * std::log2 ((double) h);
                }
                out[(size_t) s] = (float) (sum / w);
            }
            return out;
        }();
        return c[(size_t) juce::jlimit (0, numSources - 1, source)];
    }

    // |K(f)|^2 of the BS.1770 K-weighting at 48 kHz, on a 1 Hz grid up to 24 kHz
    static const std::vector<float>& kWeightTable()
    {
        static const auto t = []
        {
            const auto k = loudness::kWeighting (48000.0);
            std::vector<float> out (24001);
            for (size_t i = 0; i < out.size(); ++i)
            {
                const std::complex<double> zi = std::polar (1.0, -juce::MathConstants<double>::twoPi * (double) i / 48000.0);
                double g = 1.0;
                for (auto& b : k)
                    g *= std::norm ((b.b0 + b.b1 * zi + b.b2 * zi * zi) / (1.0 + b.a1 * zi + b.a2 * zi * zi));
                out[i] = (float) g;
            }
            return out;
        }();
        return t;
    }

    static float kWeight2 (double f) noexcept
    {
        const auto& t = kWeightTable();
        f = f < 24000.0 ? f : 48000.0 - f;   // harmonics between Nyquist and the floor fold back to 20-24 kHz
        return t[(size_t) juce::jlimit (0, 24000, (int) f)];
    }

    // ---- Building the spectrum -----------------------------------------------------------------------------
    struct Build
    {
        std::vector<float> data;                 // [level][stride], laid out like Wavetables::Table
        std::array<bool, numLevels> valid {};
        std::vector<float> amp, phase;           // [h], h = 1 .. maxHarmonic - 1
        std::array<float, numPitchPoints> loudness {};
        std::array<bool, numPitchPoints> loudnessValid {};
        const float* frame (int level) const noexcept { return data.data() + (size_t) level * stride + 1; }
    };

    // Recomputes the spectrum into the current build. With `crossfade` the previous build is kept to fade
    // from, and the band limits the voices used since the last rebuild are made at once.
    void rebuild (bool crossfade)
    {
        if (crossfade)
            std::swap (current, previous);
        auto& b = builds[(size_t) current];
        const int a = choice (smooth[sourceA], numSources), bs = choice (smooth[sourceB], numSources);
        const float m = smooth[morph];
        const float delta = centroid (bs) - centroid (a);
        const float shift = std::exp2 (formantSemis (smooth[formant]) / 12.0f);
        const float t = tiltDb (smooth[tilt]), oe = smooth[oddEven] * 2.0f - 1.0f;
        const float depth = shepardDepth (smooth[shepard]), s = smooth[smear];
        const float scaleA = std::exp2 (-m * delta), scaleB = std::exp2 ((1.0f - m) * delta);

        for (int h = 1; h < maxHarmonic; ++h)
        {
            const float x = (float) h / shift, lh = std::log2 ((float) h);
            float l = (1.0f - m) * (envelopeDb (a, x * scaleA) + fineDb (a, h)) + m * (envelopeDb (bs, x * scaleB) + fineDb (bs, h));
            l += t * lh;
            if (oe > 0.0f && h % 2 == 0)
                l -= 40.0f * oe;
            else if (oe < 0.0f && h % 2 == 1 && h > 1)
                l += 40.0f * oe;
            if (depth > 0.0f)
                l += depth * (0.5f + 0.5f * std::cos (juce::MathConstants<float>::twoPi * (lh - shepardPos)) - 1.0f);
            work[(size_t) h] = juce::jmax (floorDb, l);
        }

        // Smear: blend with the power average over +-0.5 s octaves
        if (s > 1.0e-4f)
        {
            prefix[0] = 0.0;
            for (int h = 1; h < maxHarmonic; ++h)
                prefix[(size_t) h] = prefix[(size_t) h - 1] + std::pow (10.0, (double) work[(size_t) h] / 10.0);
            const float span = std::exp2 (0.5f * s);
            for (int h = 1; h < maxHarmonic; ++h)
            {
                const int lo = juce::jlimit (1, maxHarmonic - 1, (int) std::floor ((float) h / span));
                const int hi = juce::jlimit (1, maxHarmonic - 1, (int) std::ceil ((float) h * span));
                const double mean = (prefix[(size_t) hi] - prefix[(size_t) lo - 1]) / (double) (hi - lo + 1);
                const float blurred = mean > 1.0e-30 ? (float) (10.0 * std::log10 (mean)) : floorDb;
                b.amp[(size_t) h] = work[(size_t) h] + s * (blurred - work[(size_t) h]);
            }
        }
        else
            std::copy (work.begin(), work.end(), b.amp.begin());

        const auto& smearP = smearPhases();
        for (int h = 1; h < maxHarmonic; ++h)
        {
            b.amp[(size_t) h] = b.amp[(size_t) h] <= floorDb + 1.0f ? 0.0f : std::pow (10.0f, b.amp[(size_t) h] / 20.0f);
            float p = arcLerp (sourcePhase (a, h), sourcePhase (bs, h), m);
            b.phase[(size_t) h] = s > 1.0e-4f ? arcLerp (p, smearP[(size_t) h], s) : p;
        }
        b.amp[0] = 0.0f;

        b.loudnessValid.fill (false);
        b.valid.fill (false);
        const uint32_t levels = usedLevels;
        usedLevels = 0;
        for (int level = 0; level < numLevels; ++level)
            if ((levels >> level) & 1u)
                makeLevel (b, level);
        built = smooth;
        sinceRebuild = 0;
    }

    // One band limit by inverse FFT, normalised to a sine's RMS. The top 40 % of its harmonics taper off along
    // a raised cosine (about -1.7 dB at 20 kHz, -12 dB at Nyquist), so the copies that fold back above 20 kHz
    // and the interpolator's images stay small even when a tilt makes the spectrum rise.
    //
    // Crest guard: a flat or rising spectrum with aligned phases is a pulse train whose peaks sit 20-30 dB over
    // its RMS, and the true-peak limiter would flatten it. When a frame's crest factor goes past 12 dB, its
    // phases are blended toward Schroeder's low-crest set (phi_h = pi h^2 / top), as far as the excess asks,
    // and the frame is made again: the same amplitudes, so the same timbre, with peaks the stack can carry.
    void makeLevel (Build& b, int level) noexcept
    {
        const int top = Wavetables::harmonicsAt (level);
        const float crest = renderLevel (b, level, top, 0.0f);
        if (crest > maxCrest)
            renderLevel (b, level, top, juce::jlimit (0.0f, 1.0f, (crest - maxCrest) / maxCrest + 0.25f));
        b.valid[(size_t) level] = true;
    }

    // Returns the frame's crest factor (peak / RMS)
    float renderLevel (Build& b, int level, int top, float schroeder) noexcept
    {
        std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);
        const float taperStart = 0.6f * (float) top, taperWidth = 0.4f * (float) top;
        for (int h = 1; h <= top; ++h)
        {
            float amp = b.amp[(size_t) h];
            if ((float) h > taperStart)
                amp *= juce::square (std::cos (juce::MathConstants<float>::halfPi * ((float) h - taperStart) / taperWidth));
            float ph = b.phase[(size_t) h];
            if (schroeder > 0.0f)
                ph = arcLerp (ph, std::fmod (juce::MathConstants<float>::pi * (float) h * (float) h / (float) top, juce::MathConstants<float>::twoPi), schroeder);
            fftBuffer[(size_t) (2 * h)] = amp * std::sin (ph);
            fftBuffer[(size_t) (2 * h + 1)] = -amp * std::cos (ph);
        }
        fft->performRealOnlyInverseTransform (fftBuffer.data());
        // Normalised on the result rather than on the transform's nominal scale, so it holds for any FFT backend
        double power = 0.0;
        float peakValue = 0.0f;
        for (int i = 0; i < frameSize; ++i)
        {
            power += (double) fftBuffer[(size_t) i] * fftBuffer[(size_t) i];
            peakValue = std::max (peakValue, std::abs (fftBuffer[(size_t) i]));
        }
        const double rms = std::sqrt (power / frameSize);
        const float gain = rms > 1.0e-20 ? (float) (0.70710678 / rms) : 0.0f;
        float* x = b.data.data() + (size_t) level * stride + 1;
        for (int i = 0; i < frameSize; ++i)
            x[i] = fftBuffer[(size_t) i] * gain;
        x[-1] = x[frameSize - 1];
        x[frameSize] = x[0];
        x[frameSize + 1] = x[1];
        return rms > 1.0e-20 ? (float) (peakValue / rms) : 0.0f;
    }

    static float arcLerp (float a, float b, float t) noexcept
    {
        const float twoPi = juce::MathConstants<float>::twoPi;
        float d = std::fmod (b - a, twoPi);
        if (d > juce::MathConstants<float>::pi) d -= twoPi;
        if (d < -juce::MathConstants<float>::pi) d += twoPi;
        return a + t * d;
    }

    // K-weighted match of the spectrum to a sine at grid pitch k, for the band limit that pitch plays (with
    // the same taper as the frames); made when a voice first asks for it after a rebuild
    static float gridLoudness (Build& b, int k) noexcept
    {
        if (! b.loudnessValid[(size_t) k])
        {
            const double f0 = Wavetables::lowestPitch * std::exp2 (k / 3.0);
            const int top = Wavetables::harmonicsAt (Wavetables::levelFor ((float) f0, 48000.0f));
            const double taperStart = 0.6 * top, taperWidth = 0.4 * top;
            double plain = 0.0, weighted = 0.0;
            for (int h = 1; h <= top; ++h)
            {
                double p = (double) b.amp[(size_t) h] * b.amp[(size_t) h];
                if (h > taperStart)
                    p *= std::pow (std::cos (juce::MathConstants<double>::halfPi * (h - taperStart) / taperWidth), 4.0);
                plain += p;
                weighted += p * kWeight2 (h * f0);
            }
            b.loudness[(size_t) k] = plain > 1.0e-20 && weighted > 1.0e-20 ? (float) std::sqrt (kWeight2 (f0) * plain / weighted) : 1.0f;
            b.loudnessValid[(size_t) k] = true;
        }
        return b.loudness[(size_t) k];
    }

    static float loudnessGain (Build& b, float f0) noexcept
    {
        const float p = juce::jlimit (0.0f, (float) (numPitchPoints - 1), 3.0f * std::log2 (juce::jmax (1.0f, f0) / Wavetables::lowestPitch));
        const int p0 = juce::jmin (numPitchPoints - 2, (int) p);
        const float pf = p - (float) p0;
        const float g0 = gridLoudness (b, p0), g1 = gridLoudness (b, p0 + 1);
        return g0 + (g1 - g0) * pf;
    }

    // ---- Voices -------------------------------------------------------------------------------------------
    struct Voice
    {
        float pitchBend = 0.0f;   // semitones, from the layer (pitch bend, MPE)
        bool active = false;
        PitchGlide pitch;
        uint32_t rng = 1;
        std::array<float, UnisonLayout::maxVoices> phase {}, gain {}, panL {}, panR {};
        UnisonLayout layout;
        float layoutDetune = -1.0f, layoutMix = -1.0f, layoutSpread = -1.0f;
        float loudGain = 1.0f;
        int unisonMode = 0, level = -1;
        float switchGain = 1.0f;
        bool switching = false;
    };

    static std::vector<SlotInfo> makeSlots()
    {
        auto percent = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
        auto source = [] (float v) { return juce::String (sourceName (choice (v, numSources))); };
        std::vector<SlotInfo> s ((size_t) numUsedSlots);
        s[sourceA]     = { "Source A", choiceValue (0, numSources), source };
        s[sourceB]     = { "Source B", choiceValue (6, numSources), source };
        s[morph]       = { "Morph", 0.0f, percent };
        s[tilt]        = { "Tilt", 0.5f, [] (float v) { return juce::String (tiltDb (v), 1) + " dB/oct"; } };
        s[oddEven]     = { "Odd/Even", 0.5f, [] (float v)
                           { const int p = juce::roundToInt ((v * 2.0f - 1.0f) * 100.0f); return p == 0 ? juce::String ("Both") : p > 0 ? "Odd " + juce::String (p) + " %" : "Even " + juce::String (-p) + " %"; } };
        s[formant]     = { "Formant", 0.5f, [] (float v) { const float st = formantSemis (v); return (st > 0.05f ? "+" : "") + juce::String (st, 1) + " st"; } };
        s[smear]       = { "Smear", 0.0f, percent };
        s[shepard]     = { "Shepard", 0.0f, [] (float v) { return juce::String (shepardDepth (v), 1) + " dB"; } };
        s[shepardRate] = { "Shepard Rate", 0.5f, [] (float v) { const float r = shepardRateHz (v); return (r > 0.0f ? "+" : "") + juce::String (r, 3) + " oct/s"; } };
        s[unison]      = { "Unison", choiceValue (1, numUnison), [] (float v)
                           { static const char* n[] = { "Off", "JP 7", "Modern 8", "Modern 12", "Modern 16" }; return juce::String (n[choice (v, numUnison)]); } };
        s[detune]      = { "Detune", 0.35f, percent };
        s[mix]         = { "Mix", 0.6f, percent };
        s[spread]      = { "Spread", 0.7f, percent };
        s[morph].role = SlotRole::position;
        s[tilt].role = SlotRole::brightness;
        s[formant].role = SlotRole::formant;
        s[detune].role = SlotRole::detune;
        s[mix].role = SlotRole::unisonMix;
        s[spread].role = SlotRole::stereoSpread;
        return s;
    }

    static std::array<float, numUsedSlots> defaults()
    {
        std::array<float, numUsedSlots> d {};
        const auto slots = makeSlots();
        for (size_t i = 0; i < d.size(); ++i)
            d[i] = slots[i].defaultValue;
        return d;
    }

    static float random (Voice& v) noexcept
    {
        v.rng ^= v.rng << 13;
        v.rng ^= v.rng >> 17;
        v.rng ^= v.rng << 5;
        return (float) (v.rng >> 8) * (1.0f / 16777216.0f);
    }

    void updateLayout (Voice& v) noexcept
    {
        const float d = smooth[detune], m = smooth[mix], s = smooth[spread];
        if (std::abs (d - v.layoutDetune) < 1.0e-6f && std::abs (m - v.layoutMix) < 1.0e-6f && std::abs (s - v.layoutSpread) < 1.0e-6f)
            return;
        v.layoutDetune = d;
        v.layoutMix = m;
        v.layoutSpread = s;
        v.layout.set ((UnisonLayout::Mode) v.unisonMode, d, m, s);
        for (int k = 0; k < UnisonLayout::maxVoices; ++k)
        {
            const float angle = (v.layout.pan[(size_t) k] + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            v.panL[(size_t) k] = std::cos (angle) * juce::MathConstants<float>::sqrt2;
            v.panR[(size_t) k] = std::sin (angle) * juce::MathConstants<float>::sqrt2;
        }
    }

    // A band limit of a build, made on the spot if no voice asked for it before the build was made
    const float* levelOf (Build& b, int level) noexcept
    {
        if (! b.valid[(size_t) level])
            makeLevel (b, level);
        return b.frame (level);
    }

    // fadeA/fadeB: the weight of the current build at the sub-block's start and end (1 = no crossfade)
    void renderSubBlock (Voice& v, float* outL, float* outR, int n, float fadeA, float fadeB) noexcept
    {
        updateLayout (v);
        if (! v.switching && v.unisonMode != choice (target[unison], numUnison))
            v.switching = true;

        std::array<float, subBlock> inc {};
        auto incrementFor = [this] (float pitch) { return 440.0f * fastExp2 ((pitch + tune - 69.0f) / 12.0f) / sr; };
        if (v.pitch.isGliding())
            for (int j = 0; j < n; ++j)
                inc[(size_t) j] = incrementFor (v.pitch.next() + v.pitchBend);
        else
            std::fill (inc.begin(), inc.begin() + n, incrementFor (v.pitch.getCurrent() + v.pitchBend));

        float maxRatio = 1.0f;
        for (int k = 0; k < v.layout.count; ++k)
            maxRatio = juce::jmax (maxRatio, v.layout.ratio[(size_t) k]);
        const float topInc = *std::max_element (inc.begin(), inc.begin() + n);
        const int level = Wavetables::levelFor (topInc * maxRatio * sr, sr);
        const int fromLevel = v.level < 0 ? level : v.level;
        v.level = level;
        usedLevels |= (1u << level) | (1u << fromLevel);

        auto& cur = builds[(size_t) current];
        auto& prev = builds[(size_t) previous];
        const bool fading = fadeA < 1.0f && prev.valid[(size_t) level] && prev.valid[(size_t) fromLevel];
        const float* frames[4] = { levelOf (cur, level), levelOf (cur, fromLevel),
                                   fading ? prev.frame (level) : nullptr, fading ? prev.frame (fromLevel) : nullptr };
        const bool crossfadeLevels = fromLevel != level;

        using simd::F;
        std::array<float, subBlock> sumL {}, sumR {};
        const float invN = 1.0f / (float) n;
        for (int k0 = 0; k0 < v.layout.count; k0 += F::width)
        {
            const auto ku = (size_t) k0;
            F t = F::load (&v.phase[ku]), ratio = F::load (&v.layout.ratio[ku]), g = F::load (&v.gain[ku]);
            const F gStep = (F::load (&v.layout.gain[ku]) - g) * F (invN);
            const F pl = F::load (&v.panL[ku]), pr = F::load (&v.panR[ku]);
            std::array<F, subBlock> accL, accR;
            accL.fill (F (0.0f));
            accR.fill (F (0.0f));
            for (int j = 0; j < n; ++j)
            {
                const float ramp = (float) (j + 1) * invN;
                t = t + simd::min (F (0.45f), F (inc[(size_t) j]) * ratio);
                t = t - simd::select (t >= F (1.0f), F (1.0f), F (0.0f));
                const F idx = t * F ((float) frameSize);
                const simd::I i0 = simd::truncate (idx);
                const F frac = idx - simd::toFloat (i0);
                auto read = [&] (const float* now, const float* before)
                {
                    F x = hermite (now, i0, frac);
                    if (crossfadeLevels)
                    {
                        const F old = hermite (before, i0, frac);
                        x = old + (x - old) * F (ramp);
                    }
                    return x;
                };
                F x = read (frames[0], frames[1]);
                if (fading)
                {
                    const F old = read (frames[2], frames[3]);
                    x = old + (x - old) * F (fadeA + (fadeB - fadeA) * ramp);
                }
                g = g + gStep;
                x = x * g;
                accL[(size_t) j] = x * pl;
                accR[(size_t) j] = x * pr;
            }
            for (int j = 0; j < subBlock; j += 8)
            {
                simd::addSums8 (&accL[(size_t) j], &sumL[(size_t) j]);
                simd::addSums8 (&accR[(size_t) j], &sumR[(size_t) j]);
            }
            t.store (&v.phase[ku]);
        }
        v.gain = v.layout.gain;

        // Loudness match, crossfaded between the builds like the frames
        const float f0 = inc[0] * sr;
        const float gainNow = fading ? loudnessGain (prev, f0) + (loudnessGain (cur, f0) - loudnessGain (prev, f0)) * fadeB
                                     : loudnessGain (cur, f0);
        const float gainFrom = v.loudGain < 0.0f ? gainNow : v.loudGain;
        v.loudGain = gainNow;

        const float switchStep = 1.0f / (0.003f * sr);
        bool switchedHere = false;
        for (int j = 0; j < n; ++j)
        {
            if (switchedHere)
            {
            }
            else if (v.switching)
            {
                v.switchGain -= switchStep;
                if (v.switchGain <= 0.0f)
                {
                    v.switchGain = 0.0f;
                    v.switching = false;
                    v.unisonMode = choice (target[unison], numUnison);
                    v.layoutDetune = -1.0f;
                    updateLayout (v);
                    v.gain = v.layout.gain;
                    switchedHere = true;
                }
            }
            else if (v.switchGain < 1.0f)
                v.switchGain = juce::jmin (1.0f, v.switchGain + switchStep);
            const float gain = voiceLevel * v.switchGain * (gainFrom + (gainNow - gainFrom) * (float) (j + 1) * invN);
            outL[j] += sumL[(size_t) j] * gain;
            outR[j] += sumR[(size_t) j] * gain;
        }
    }

    static simd::F hermite (const float* x, simd::I i, simd::F t) noexcept
    {
        using simd::F;
        F xm, x0, x1, x2;
        simd::load4 (x, i, xm, x0, x1, x2);
        const F c1 = F (0.5f) * (x1 - xm), c3 = F (1.5f) * (x0 - x1) + F (0.5f) * (x2 - xm), c2 = xm - x0 + c1 - c3;
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    float sr = 48000.0f, tune = 0.0f;
    std::array<float, numUsedSlots> target = defaults(), smooth = defaults(), built {};
    std::vector<Voice> voices;
    std::array<Build, 2> builds;
    int current = 0, previous = 1;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> fftBuffer, work;
    std::vector<double> prefix;
    uint32_t usedLevels = 0;
    int sinceRebuild = 0, fadeLeft = 0;
    float fadeFrom = 1.0f, fadeTo = 1.0f, shepardPos = 0.0f;
};
}
