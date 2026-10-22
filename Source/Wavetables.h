#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Loudness.h"
#include <array>
#include <complex>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace ps
{
// Factory wavetables, built once from harmonic spectra and shared by every Wavetable engine instance.
//
// Each table has `numFrames` frames (the Position axis). Each frame is stored at `numLevels` band limits, made
// by inverse FFT with every harmonic above the limit removed: level l keeps harmonics 1 .. 1024 / 2^(l/2). A
// voice reads the level whose top harmonic stays under the alias floor (below), so the wavetable never aliases
// into the audible band. Half-octave steps keep the top harmonic within a factor of 1.41 of the floor, so the
// band limit never dulls the audible top end (whole octaves would cut 14-20 kHz at some pitches).
//
// Every frame and band limit is normalised to a sine's RMS, and Table::loudnessGain adds the BS.1770
// K-weighted correction for the pitch it plays at, so neither Position nor pitch moves the loudness: a brighter
// frame isn't louder on the meter.
class Wavetables
{
public:
    static constexpr int frameSize = 2048, numFrames = 32, numLevels = 20, maxHarmonic = frameSize / 2, stride = frameSize + 3;
    static constexpr int numWarpPoints = 17;
    static constexpr int numPitchPoints = 31;          // third octaves from lowestPitch: 16 Hz .. 16 kHz
    static constexpr float lowestPitch = 16.0f;
    static constexpr float maxWarp = 0.95f;

    // Harmonics kept at a level: 1023, 724, 512, 362, ... 1
    static int harmonicsAt (int level) noexcept
    {
        return juce::jmax (1, juce::jmin (maxHarmonic - 1, (int) std::floor ((float) maxHarmonic * std::pow (2.0f, -0.5f * (float) level))));
    }

    struct Table
    {
        juce::String name;
        // [frame][level][stride]: x[-1], x[0] .. x[frameSize - 1], x[0], x[1], so a 4-point interpolator at any
        // index reads straight through without wrapping
        std::vector<float> data;

        // Gain that loudness-matches a frame under Warp depth b (see warpGain below), at numWarpPoints depths
        std::vector<float> warpGains;   // [frame][numWarpPoints]
        // Gain that loudness-matches a frame to a sine of the same pitch, on a third-octave pitch grid
        std::vector<float> loudnessGains;   // [frame][numPitchPoints]

        // Points at x[0] of a frame
        const float* frame (int f, int level) const noexcept
        {
            return data.data() + ((size_t) f * numLevels + (size_t) level) * stride + 1;
        }

        // The loudness match at a (fractional) frame position and fundamental (Hz, at 48 kHz)
        float loudnessGain (float framePos, float f0) const noexcept
        {
            const float p = juce::jlimit (0.0f, (float) (numPitchPoints - 1), 3.0f * std::log2 (juce::jmax (1.0f, f0) / lowestPitch));
            const int f = juce::jlimit (0, numFrames - 2, (int) framePos), p0 = juce::jmin (numPitchPoints - 2, (int) p);
            const float ff = juce::jlimit (0.0f, 1.0f, framePos - (float) f), pf = p - (float) p0;
            auto at = [this] (int fr, int k) { return loudnessGains[(size_t) (fr * numPitchPoints + k)]; };
            const float g0 = at (f, p0) + (at (f, p0 + 1) - at (f, p0)) * pf;
            const float g1 = at (f + 1, p0) + (at (f + 1, p0 + 1) - at (f + 1, p0)) * pf;
            return g0 + (g1 - g0) * ff;
        }

        // The Warp loudness match at a (fractional) frame position and depth b in [-maxWarp, maxWarp]
        float warpGain (float framePos, float b) const noexcept
        {
            const float w = juce::jlimit (0.0f, (float) (numWarpPoints - 1), (b / maxWarp + 1.0f) * 0.5f * (float) (numWarpPoints - 1));
            const int f0 = juce::jlimit (0, numFrames - 2, (int) framePos), w0 = juce::jmin (numWarpPoints - 2, (int) w);
            const float ff = juce::jlimit (0.0f, 1.0f, framePos - (float) f0), wf = w - (float) w0;
            auto at = [this] (int f, int k) { return warpGains[(size_t) (f * numWarpPoints + k)]; };
            const float g0 = at (f0, w0) + (at (f0, w0 + 1) - at (f0, w0)) * wf;
            const float g1 = at (f0 + 1, w0) + (at (f0 + 1, w0 + 1) - at (f0 + 1, w0)) * wf;
            return g0 + (g1 - g0) * ff;
        }
    };

    static const Wavetables& get()
    {
        static const Wavetables instance;
        return instance;
    }

    int size() const noexcept                   { return (int) tables.size(); }
    const Table& table (int i) const noexcept   { return tables[(size_t) juce::jlimit (0, size() - 1, i)]; }

    juce::StringArray names() const
    {
        juce::StringArray n;
        for (auto& t : tables)
            n.add (t.name);
        return n;
    }

    // The band-limit level for a fundamental: its highest harmonic stays at or below sampleRate - 20 kHz, so
    // anything past Nyquist folds back above 20 kHz and stays inaudible.
    //
    // Under Warp depth b, harmonic h is phase-modulated with index beta = h |b|, and its sidebands J_n(beta)
    // spread n harmonics either side. They fall below -60 dB (J_n < 1e-3) once n > beta + 3 beta^(1/3) + 2
    // (checked against the Bessel functions for beta up to 1000), so the top harmonic plus that spread must fit.
    static int levelFor (float f0, float sampleRate, float warp = 0.0f) noexcept
    {
        const float highest = juce::jmax (1.0f, (sampleRate - 20000.0f) / juce::jmax (1.0f, f0));
        const float b = std::abs (warp);
        auto reach = [b] (float h)
        {
            const float beta = h * b;
            return beta > 0.0f ? h + beta + 3.0f * std::cbrt (beta) + 2.0f : h;
        };
        int level = 0;
        while (level < numLevels - 1 && reach ((float) harmonicsAt (level)) > highest)
            ++level;
        return level;
    }

private:
    // amplitude[h] and phase[h] for harmonics h = 1 .. maxHarmonic (index 0 unused); x = position 0..1
    using Spectrum = std::function<void (float x, std::vector<float>& amplitude, std::vector<float>& phase)>;

    Wavetables()
    {
        add ("Basic", basic);
        add ("PWM", pwm);
        add ("Vowels", vowels);
        add ("Reso Sweep", resoSweep);
        add ("Drawbars", drawbars);
        add ("Digital", digital);
    }

    void add (const char* name, const Spectrum& spectrum)
    {
        Table t;
        t.name = name;
        t.data.assign ((size_t) numFrames * numLevels * stride, 0.0f);
        juce::dsp::FFT fft (11);
        std::vector<float> amp ((size_t) maxHarmonic + 1), phase ((size_t) maxHarmonic + 1), buffer (2 * frameSize);

        for (int f = 0; f < numFrames; ++f)
        {
            std::fill (amp.begin(), amp.end(), 0.0f);
            std::fill (phase.begin(), phase.end(), 0.0f);
            spectrum ((float) f / (float) (numFrames - 1), amp, phase);
            amp[0] = 0.0f;   // no DC

            for (int level = 0; level < numLevels; ++level)
            {
                const int top = harmonicsAt (level);
                std::fill (buffer.begin(), buffer.end(), 0.0f);
                for (int h = 1; h <= top; ++h)
                {
                    // a sin(2 pi h n / N + phi) as a conjugate-symmetric spectrum
                    const float a = amp[(size_t) h];
                    buffer[(size_t) (2 * h)] = a * std::sin (phase[(size_t) h]);
                    buffer[(size_t) (2 * h + 1)] = -a * std::cos (phase[(size_t) h]);
                }
                fft.performRealOnlyInverseTransform (buffer.data());
                // Each band limit is normalised on its own: what a voice plays is exactly the copy it reads, so
                // a bright table (Digital, narrow PWM) doesn't lose level when its ultrasonic harmonics go
                double power = 0.0;
                for (int i = 0; i < frameSize; ++i)
                    power += (double) buffer[(size_t) i] * buffer[(size_t) i];
                const double rms = std::sqrt (power / frameSize);
                const float gain = rms > 1.0e-12 ? (float) (0.70710678 / rms) : 0.0f;
                float* out = t.data.data() + ((size_t) f * numLevels + (size_t) level) * stride + 1;
                for (int i = 0; i < frameSize; ++i)
                    out[i] = buffer[(size_t) i] * gain;
                out[-1] = out[frameSize - 1];
                out[frameSize] = out[0];
                out[frameSize + 1] = out[1];
            }
        }
        measureWarpGains (t);
        measureLoudnessGains (t, spectrum);
        tables.push_back (std::move (t));
    }


    // |K(f)|^2 of the BS.1770 K-weighting at 48 kHz
    static double kWeight2 (double f)
    {
        static const auto k = loudness::kWeighting (48000.0);
        const std::complex<double> zi = std::polar (1.0, -juce::MathConstants<double>::twoPi * f / 48000.0);
        double g = 1.0;
        for (auto& b : k)
            g *= std::norm ((b.b0 + b.b1 * zi + b.b2 * zi * zi) / (1.0 + b.a1 * zi + b.a2 * zi * zi));
        return g;
    }

    // Gain that gives a spectrum (amplitudes 1..top, RMS-normalised) the K-weighted loudness of a sine at f0.
    // Harmonics between Nyquist and the 28 kHz floor fold back to 20-24 kHz, and are weighted there.
    static double loudnessMatch (const std::vector<float>& amp, int top, double f0)
    {
        double plain = 0.0, weighted = 0.0;
        for (int h = 1; h <= top; ++h)
        {
            const double p = (double) amp[(size_t) h] * amp[(size_t) h], f = h * f0;
            plain += p;
            weighted += p * kWeight2 (f < 24000.0 ? f : 48000.0 - f);
        }
        return plain > 1.0e-20 && weighted > 1.0e-20 ? std::sqrt (kWeight2 (f0) * plain / weighted) : 1.0;
    }

    // For each frame and grid pitch: the band limit that pitch plays (at 48 kHz) and the gain that brings its
    // RMS-normalised spectrum to the K-weighted loudness of a sine at that pitch
    static void measureLoudnessGains (Table& t, const Spectrum& spectrum)
    {
        t.loudnessGains.assign ((size_t) numFrames * numPitchPoints, 1.0f);
        std::vector<float> amp ((size_t) maxHarmonic + 1), phase ((size_t) maxHarmonic + 1);
        for (int f = 0; f < numFrames; ++f)
        {
            std::fill (amp.begin(), amp.end(), 0.0f);
            spectrum ((float) f / (float) (numFrames - 1), amp, phase);
            for (int k = 0; k < numPitchPoints; ++k)
            {
                const double f0 = lowestPitch * std::exp2 (k / 3.0);
                const int top = harmonicsAt (levelFor ((float) f0, 48000.0f));
                t.loudnessGains[(size_t) (f * numPitchPoints + k)] = (float) loudnessMatch (amp, top, f0);
            }
        }
    }

    // Warp reads a frame at phi(t) = t + b/(2 pi) sin(2 pi t), which moves its loudness by an amount that depends
    // on where the frame's energy sits in its cycle (a sine loses about 1.7 dB at full depth) and on the
    // harmonics it adds. Measure it once per frame and depth on a 256-harmonic copy, K-weighted at the pitch
    // that copy plays at, so the engine can play every warp at the frame's own loudness.
    static void measureWarpGains (Table& t)
    {
        t.warpGains.assign ((size_t) numFrames * numWarpPoints, 1.0f);
        constexpr int order = 12, points = 1 << order, level = 4;
        const double f0 = 28000.0 / harmonicsAt (level);   // the pitch this copy plays at (48 kHz)
        juce::dsp::FFT fft (order);
        std::vector<float> buffer (2 * points);
        // K-weighted power of one cycle read through the warp, from its harmonic spectrum
        auto weightedPower = [&] (const float* x, double b)
        {
            for (int i = 0; i < points; ++i)
            {
                const double ph = (double) i / points;
                double phi = ph + b / juce::MathConstants<double>::twoPi * std::sin (juce::MathConstants<double>::twoPi * ph);
                phi -= std::floor (phi);
                const double idx = phi * frameSize;
                const int i0 = juce::jmin (frameSize - 1, (int) idx);
                buffer[(size_t) i] = (float) (x[i0] + (x[i0 + 1] - x[i0]) * (idx - i0));
            }
            std::fill (buffer.begin() + points, buffer.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (buffer.data());
            double p = 0.0;
            for (int h = 1; h < points / 2 && h * f0 < 28000.0; ++h)
                p += (double) buffer[(size_t) h] * buffer[(size_t) h] * kWeight2 (h * f0 < 24000.0 ? h * f0 : 48000.0 - h * f0);
            return p;
        };
        for (int f = 0; f < numFrames; ++f)
        {
            const float* x = t.frame (f, level);
            const double plain = weightedPower (x, 0.0);
            for (int k = 0; k < numWarpPoints; ++k)
            {
                const double b = maxWarp * (2.0 * k / (numWarpPoints - 1) - 1.0);
                const double warped = weightedPower (x, b);
                t.warpGains[(size_t) (f * numWarpPoints + k)] = warped > 1.0e-20 ? (float) std::sqrt (plain / warped) : 1.0f;
            }
        }
    }

    // ---- Factory spectra ---------------------------------------------------------------------------------
    static float sawAmp (int h) { return 1.0f / (float) h; }

    // Sine -> triangle -> saw -> square, crossfading spectra between the four shapes
    static void basic (float x, std::vector<float>& a, std::vector<float>& p)
    {
        auto shapeAmp = [] (int shape, int h)
        {
            switch (shape)
            {
                case 0:  return h == 1 ? 1.0f : 0.0f;
                case 1:  return h % 2 == 1 ? 1.0f / (float) (h * h) : 0.0f;
                case 2:  return 1.0f / (float) h;
                default: return h % 2 == 1 ? 1.0f / (float) h : 0.0f;
            }
        };
        const float pos = x * 3.0f;
        const int s0 = juce::jmin (2, (int) pos);
        const float m = pos - (float) s0;
        for (int h = 1; h < maxHarmonic; ++h)
        {
            // Triangle harmonics alternate in sign; give them a phase of pi instead
            auto signedAmp = [h, &shapeAmp] (int shape) { return shapeAmp (shape, h) * (shape == 1 && (h / 2) % 2 == 1 ? -1.0f : 1.0f); };
            const float v = signedAmp (s0) * (1.0f - m) + signedAmp (s0 + 1) * m;
            a[(size_t) h] = std::abs (v);
            p[(size_t) h] = v < 0.0f ? juce::MathConstants<float>::pi : 0.0f;
        }
    }

    // Pulse width 50 % -> 95 %
    static void pwm (float x, std::vector<float>& a, std::vector<float>& p)
    {
        const float w = 0.5f + 0.45f * x;
        for (int h = 1; h < maxHarmonic; ++h)
        {
            // Fourier series of a pulse of width w (centred), as a phase-shifted sine
            const float v = std::sin (juce::MathConstants<float>::pi * (float) h * w) / (float) h;
            a[(size_t) h] = std::abs (v);
            p[(size_t) h] = juce::MathConstants<float>::halfPi + (v < 0.0f ? juce::MathConstants<float>::pi : 0.0f);
        }
    }

    // A saw through vowel formants A -> E -> I -> O -> U (formant frequencies for a 110 Hz voice)
    static void vowels (float x, std::vector<float>& a, std::vector<float>& p)
    {
        static constexpr float formants[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 270, 2140, 2950 },
                                                  { 450, 800, 2830 },  { 325, 700, 2530 } };
        const float pos = x * 4.0f;
        const int v0 = juce::jmin (3, (int) pos);
        const float m = pos - (float) v0;
        for (int h = 1; h < maxHarmonic; ++h)
        {
            const float f = 110.0f * (float) h;
            float env = 0.02f;
            for (int k = 0; k < 3; ++k)
            {
                const float centre = formants[v0][k] * (1.0f - m) + formants[v0 + 1][k] * m;
                const float width = 0.12f * centre;
                env += (k == 0 ? 1.0f : k == 1 ? 0.6f : 0.3f) * std::exp (-0.5f * std::pow ((f - centre) / width, 2.0f));
            }
            a[(size_t) h] = sawAmp (h) * env;
            p[(size_t) h] = 0.0f;
        }
    }

    // A saw through a resonant low-pass whose cutoff sweeps from harmonic 2 to harmonic 200
    static void resoSweep (float x, std::vector<float>& a, std::vector<float>& p)
    {
        const float cutoff = 2.0f * std::pow (100.0f, x);
        for (int h = 1; h < maxHarmonic; ++h)
        {
            const float r = (float) h / cutoff;
            const float lowPass = 1.0f / std::sqrt (1.0f + std::pow (r, 8.0f));
            const float peak = 3.0f * std::exp (-0.5f * std::pow ((r - 1.0f) / 0.12f, 2.0f));
            a[(size_t) h] = sawAmp (h) * (lowPass + peak);
            p[(size_t) h] = 0.0f;
        }
    }

    // Organ drawbar registrations (16', 8', 5 1/3', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1') morphing in turn
    static void drawbars (float x, std::vector<float>& a, std::vector<float>& p)
    {
        static constexpr int harmonics[9] = { 1, 2, 3, 4, 6, 8, 10, 12, 16 };   // relative to the 16' drawbar
        static constexpr float registrations[4][9] = { { 8, 8, 8, 0, 0, 0, 0, 0, 0 }, { 8, 8, 6, 8, 0, 0, 0, 0, 8 },
                                                       { 8, 0, 8, 0, 8, 0, 8, 0, 8 }, { 8, 8, 8, 8, 8, 8, 8, 8, 8 } };
        const float pos = x * 3.0f;
        const int r0 = juce::jmin (2, (int) pos);
        const float m = pos - (float) r0;
        for (int d = 0; d < 9; ++d)
        {
            const float level = registrations[r0][d] * (1.0f - m) + registrations[r0 + 1][d] * m;
            a[(size_t) harmonics[d]] += std::pow (10.0f, (level - 8.0f) * 3.0f / 20.0f) * (level > 0.0f ? 1.0f : 0.0f);
            p[(size_t) harmonics[d]] = 0.0f;
        }
    }

    // Bright digital spectra: harmonic groups switch on in a fixed pseudo-random order as Position rises
    static void digital (float x, std::vector<float>& a, std::vector<float>& p)
    {
        juce::Random random (0x5eed);
        for (int h = 1; h < maxHarmonic; ++h)
        {
            const float threshold = random.nextFloat();
            const float phase = random.nextFloat() * juce::MathConstants<float>::twoPi;
            const float on = juce::jlimit (0.0f, 1.0f, (x * 1.2f - threshold) * 10.0f);
            a[(size_t) h] = (h == 1 ? 1.0f : on) / std::sqrt ((float) h);
            p[(size_t) h] = phase;
        }
    }

    std::vector<Table> tables;
};
}
