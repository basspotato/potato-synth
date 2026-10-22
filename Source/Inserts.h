#pragma once
#include "Filter.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ps
{
// A layer's chorus insert: two modulated taps per channel (7 and 11 ms, swung by up to +-3 ms), their sine
// LFOs a quarter cycle apart between the channels, so the wet signal is wide while each tap only bends the
// pitch by a few cents. Cubic Hermite reads keep the highs from dulling as the taps move. The wet path is
// high-passed (LR4 at 250 Hz) before it joins the dry, so the chorus never puts stereo energy into the mono
// lows. Above 250 Hz the sum is scaled by 1 / sqrt(1 + a^2) (wet and dry are decorrelated, so their powers
// add) to keep the level; the lows pass at unity: out = dry - (1 - norm) HP(dry) + norm a HP(wet), which is
// exactly the dry signal at Amount 0. At Amount 0 it only keeps its delay lines filled.
class Chorus
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        int size = 1;
        while (size < (int) (0.03f * sr) + 8)
            size <<= 1;
        for (auto& l : lines)
            l.assign ((size_t) size, 0.0f);
        mask = size - 1;
        for (auto* bank : { &wetHigh, &dryHigh })
            for (auto& f : *bank)
                f.setCutoff (250.0, sampleRate);
        reset();
    }

    void reset()
    {
        for (auto& l : lines)
            std::fill (l.begin(), l.end(), 0.0f);
        for (auto* bank : { &wetHigh, &dryHigh })
            for (auto& f : *bank)
                f.reset();
        writePos = 0;
        amount = 0.0f;
        phase = 0.0f;
    }

    // a: 0..1 (ramped from the last block), rateHz: LFO rate, depth: 0..1
    void process (float* left, float* right, int numSamples, float a, float rateHz, float depth) noexcept
    {
        if (a <= 0.0f && amount <= 0.0f)
        {
            // Off: only keep the lines filled, so turning it on never reads a step from silence into the signal
            for (int n = 0; n < numSamples; ++n)
            {
                lines[0][(size_t) writePos] = left[n];
                lines[1][(size_t) writePos] = right[n];
                writePos = (writePos + 1) & mask;
            }
            return;
        }
        const float from = amount;
        amount = a;
        const float inc = juce::jlimit (0.01f, 10.0f, rateHz) / sr;
        const float swing = 0.003f * sr * juce::jlimit (0.0f, 1.0f, depth);
        const float base[2] = { 0.007f * sr, 0.011f * sr };
        const float twoPi = juce::MathConstants<float>::twoPi;
        float* io[2] = { left, right };
        for (int n = 0; n < numSamples; ++n)
        {
            const float t = (float) (n + 1) / (float) numSamples;
            const float wetGain = from + (a - from) * t;
            const float norm = 1.0f / std::sqrt (1.0f + wetGain * wetGain);
            lines[0][(size_t) writePos] = left[n];
            lines[1][(size_t) writePos] = right[n];
            for (int ch = 0; ch < 2; ++ch)
            {
                float wet = 0.0f;
                for (int v = 0; v < 2; ++v)
                {
                    const float p = phase + 0.25f * (float) ch + 0.5f * (float) v;
                    wet += tap (ch, base[v] + swing * (1.0f + std::sin (twoPi * p)));
                }
                const float high = (float) wetHigh[(size_t) ch].process (0.5f * wet);
                const float dryHi = (float) dryHigh[(size_t) ch].process (io[ch][n]);
                io[ch][n] += norm * wetGain * high - (1.0f - norm) * dryHi;
            }
            writePos = (writePos + 1) & mask;
            phase += inc;
            if (phase >= 1.0f)
                phase -= 1.0f;
        }
        if (amount <= 0.0f)
            for (auto* bank : { &wetHigh, &dryHigh })   // fully off: the next turn-on starts its filters clean
                for (auto& f : *bank)
                    f.reset();
    }

private:
    float tap (int ch, float delay) const noexcept
    {
        const float pos = (float) writePos - delay;
        const int i1 = (int) std::floor (pos);
        const float f = pos - (float) i1;
        const auto& l = lines[(size_t) ch];
        const float y0 = l[(size_t) ((i1 - 1) & mask)], y1 = l[(size_t) (i1 & mask)];
        const float y2 = l[(size_t) ((i1 + 1) & mask)], y3 = l[(size_t) ((i1 + 2) & mask)];
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    float sr = 48000.0f;
    std::array<std::vector<float>, 2> lines;
    int mask = 0, writePos = 0;
    float amount = 0.0f, phase = 0.0f;
    std::array<LinkwitzRiley4, 2> wetHigh { LinkwitzRiley4 { LinkwitzRiley4::Type::highPass }, LinkwitzRiley4 { LinkwitzRiley4::Type::highPass } };
    std::array<LinkwitzRiley4, 2> dryHigh { LinkwitzRiley4 { LinkwitzRiley4::Type::highPass }, LinkwitzRiley4 { LinkwitzRiley4::Type::highPass } };
};

// ---- The layer's two insert slots ----------------------------------------------------------------------
//
// Each slot holds one effect type with an Amount and three type-specific controls (p1..p3, 0..1). They run in
// the layer drive's 2x domain right after the drive (LayerDrive::process), so the nonlinear types are
// oversampled and the layer's latency never changes. Every type is exactly the dry signal at Amount 0. A type
// change (and a Drive mode change) fades the slot to dry over 5 ms, switches, and fades back; swapping the slot
// order fades both. Nothing is random: the LFOs (phaser, flanger) start from the same phase on every prepare.
// Aliasing: the bitcrusher's sample-and-hold aliases on purpose; Fold at high Amount still aliases a little
// (first-order ADAA at 2x).
namespace inserts
{
    // Order is permanent: it is saved as an index (names: params::insertTypeNames)
    enum class Type { off, drive, crush, phaser, flanger, comb, freqShift, formant, eq, transient, count };
    inline constexpr int numTypes = (int) Type::count;
    inline const juce::StringArray driveModeNames { "Tube", "Tape", "Diode", "Fold" };
    inline const char* const vowelNames[] = { "A", "E", "I", "O", "U" };

    struct ControlInfo { const char* name; float defaultValue; };
    // What p1..p3 mean for each type, and the values the GUI sets when the type is picked
    inline const std::array<std::array<ControlInfo, 3>, (size_t) numTypes> controls { {
        { { { "-", 0.5f }, { "-", 0.5f }, { "-", 0.5f } } },
        { { { "Mode", 0.0f }, { "Tone", 0.5f }, { "Bias", 0.0f } } },
        { { { "Bits", 0.5f }, { "Rate", 0.3f }, { "Tone", 1.0f } } },
        { { { "Rate", 0.5f }, { "Depth", 0.6f }, { "Feedback", 0.7f } } },
        { { { "Rate", 0.35f }, { "Depth", 0.5f }, { "Feedback", 0.7f } } },
        { { { "Freq", 0.5f }, { "Feedback", 0.85f }, { "Damp", 0.3f } } },
        { { { "Shift", 0.6f }, { "Feedback", 0.0f }, { "Spread", 0.0f } } },
        { { { "Vowel", 0.0f }, { "Shift", 0.5f }, { "Q", 0.5f } } },
        { { { "Low", 0.5f }, { "Mid", 0.5f }, { "High", 0.5f } } },
        { { { "Attack", 0.75f }, { "Sustain", 0.5f }, { "Speed", 0.5f } } },
    } };

    // ---- Value mappings, shared by the DSP and the display text ----
    inline float lfoRateHz (float v) noexcept       { return 0.02f * std::pow (400.0f, v); }          // 0.02..8 Hz
    inline float bipolar (float v) noexcept         { return 2.0f * v - 1.0f; }
    inline int driveMode (float v) noexcept         { return juce::jlimit (0, 3, (int) (v * 4.0f)); }
    inline float crushBits (float v) noexcept       { return 16.0f - 13.0f * v; }                    // 16..3
    inline float crushRateHz (float v) noexcept     { return 48000.0f * std::pow (0.02f, v); }       // 48 k..960 Hz
    inline float toneHz (float v) noexcept          { return 1000.0f * std::pow (20.0f, v); }        // 1..20 kHz
    inline float combHz (float v) noexcept          { return 50.0f * std::pow (100.0f, v); }         // 50..5000 Hz
    inline float shiftHz (float v) noexcept         { const float s = bipolar (v); return 2000.0f * s * s * std::abs (s); }
    inline float formantShift (float v) noexcept    { return std::exp2 (bipolar (v)); }              // +-1 octave
    inline float formantQ (float v) noexcept        { return 2.0f * std::pow (10.0f, v); }           // 2..20
    inline float eqDb (float v) noexcept            { return 24.0f * (v - 0.5f); }                   // +-12 dB
    inline float speedScale (float v) noexcept      { return std::pow (4.0f, bipolar (v)); }         // 0.25..4

    inline juce::String controlText (Type t, int k, float v)
    {
        auto pct = [] (float x) { return juce::String (juce::roundToInt (x * 100.0f)) + " %"; };
        auto hz = [] (float f) { return f < 1000.0f ? juce::String (f, f < 10.0f ? 2 : 0) + " Hz" : juce::String (f * 0.001f, 1) + " kHz"; };
        auto db = [] (float d) { return (d > 0.05f ? "+" : "") + juce::String (d, 1) + " dB"; };
        switch (t)
        {
            case Type::drive:     return k == 0 ? driveModeNames[driveMode (v)] : k == 1 ? db (18.0f * (v - 0.5f)) : pct (v);
            case Type::crush:     return k == 0 ? juce::String (crushBits (v), 1) + " bit" : hz (k == 1 ? crushRateHz (v) : toneHz (v));
            case Type::phaser:
            case Type::flanger:   return k == 0 ? hz (lfoRateHz (v)) : k == 1 ? pct (v) : pct (bipolar (v));
            case Type::comb:      return k == 0 ? hz (combHz (v)) : k == 1 ? pct (bipolar (v)) : pct (v);
            case Type::freqShift: return k == 0 ? juce::String (shiftHz (v), 1) + " Hz" : pct (v);
            case Type::formant:
            {
                if (k == 0)
                {
                    const int a = juce::jlimit (0, 4, juce::roundToInt (v * 4.0f));
                    return vowelNames[a];
                }
                return k == 1 ? juce::String (12.0f * bipolar (v), 1) + " st" : "Q " + juce::String (formantQ (v), 1);
            }
            case Type::eq:        return db (eqDb (v));
            case Type::transient: return k < 2 ? db (eqDb (v)) : "x" + juce::String (speedScale (v), 2);
            case Type::off:
            case Type::count:     break;
        }
        return pct (v);
    }

    struct Settings
    {
        int type = 0;
        float amount = 0.0f;                    // 0..1
        std::array<float, 3> p { 0.5f, 0.5f, 0.5f };
    };

    // First-order ADAA of the four drive curves (shifted by the bias so silence stays at zero)
    struct AdaaCurve
    {
        static double f (int mode, double x) noexcept
        {
            switch (mode)
            {
                case 0:  return std::tanh (x);
                case 1:  return x / (1.0 + std::abs (x));
                case 2:  return x >= 0.0 ? 1.0 - std::exp (-x) : 0.5 * (std::exp (2.0 * x) - 1.0);
                default: return std::sin (x);
            }
        }
        static double antiderivative (int mode, double x) noexcept
        {
            switch (mode)
            {
                case 0:  { const double a = std::abs (x); return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994531; }
                case 1:  { const double a = std::abs (x); return a - std::log1p (a); }
                case 2:  return x >= 0.0 ? x + std::exp (-x) - 1.0 : 0.25 * std::exp (2.0 * x) - 0.25 - 0.5 * x;
                default: return 1.0 - std::cos (x);
            }
        }
        double process (int mode, double x) noexcept
        {
            const double big = antiderivative (mode, x), dx = x - x1;
            const double y = std::abs (dx) > 1.0e-6 ? (big - big1) / dx : f (mode, 0.5 * (x + x1));
            x1 = x;
            big1 = big;
            return y;
        }
        void reset (int mode, double x0) noexcept { x1 = x0; big1 = antiderivative (mode, x0); }
        double x1 = 0.0, big1 = 0.0;
    };

    // One-pole TPT low-pass, used for tones, damping and the allpass stages
    struct OnePole
    {
        static float gain (float hz, float sr) noexcept { const float g = tptGain (hz, sr); return g / (1.0f + g); }
        float lowPass (float x, float G) noexcept { const float v = (x - s) * G, y = v + s; s = y + v; return y; }
        float allPass (float x, float G) noexcept { return 2.0f * lowPass (x, G) - x; }
        void reset() noexcept { s = 0.0f; }
        float s = 0.0f;
    };

    // Niemitalo's 90-degree IIR allpass pair (two chains of 4 second-order sections in z^-2). The first chain's
    // output is delayed a sample; then the two outputs are 90 degrees apart within a degree over most of the band.
    struct Hilbert
    {
        static constexpr float a2[4] = { 0.6923878f * 0.6923878f, 0.9360654322959f * 0.9360654322959f, 0.9882295226860f * 0.9882295226860f, 0.9987488452737f * 0.9987488452737f };
        static constexpr float b2[4] = { 0.4021921162426f * 0.4021921162426f, 0.8561710882420f * 0.8561710882420f, 0.9722909545651f * 0.9722909545651f, 0.9952884791278f * 0.9952884791278f };
        struct Stage { float x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
        std::array<Stage, 4> sa, sb;
        float delayed = 0.0f;

        static float run (std::array<Stage, 4>& st, const float (&c2)[4], float x) noexcept
        {
            for (int i = 0; i < 4; ++i)
            {
                auto& s = st[(size_t) i];
                const float y = c2[i] * (x + s.y2) - s.x2;
                s.x2 = s.x1; s.x1 = x;
                s.y2 = s.y1; s.y1 = y;
                x = y;
            }
            return x;
        }
        // Returns {in-phase, quadrature}
        std::pair<float, float> process (float x) noexcept
        {
            const float i = delayed;
            delayed = run (sa, a2, x);
            return { i, run (sb, b2, x) };
        }
        void reset() noexcept { sa = {}; sb = {}; delayed = 0.0f; }
    };

    class Slot
    {
    public:
        void prepare (double sampleRate)
        {
            sr = (float) sampleRate;
            int size = 1;
            while (size < (int) (sr / 40.0f) + 8)   // the comb's 50 Hz and the flanger's 5.5 ms, with room
                size <<= 1;
            for (auto& l : lines)
                l.assign ((size_t) size, 0.0f);
            mask = size - 1;
            fadeStep = 1.0f / (0.005f * sr);
            smoothCoef = 1.0f - std::exp (-(float) chunk / (0.01f * sr));
            current = 0;
            fade = 0.0f;
            resetState();
        }

        bool isFadedOut() const noexcept { return fade <= 0.0f; }

        // hold: fade to dry and stay there (the chain is swapping the order)
        void process (float* left, float* right, int numSamples, const Settings& s, bool hold) noexcept
        {
            const int want = variantOf (s);
            if (current == 0 && want != 0 && ! hold)
            {
                switchTo (want, s);
            }
            if (current == 0)
                return;

            if (want == current && ! hold && fade >= 1.0f && s.amount <= 0.0f && amount <= 0.0f)
            {
                if (! resting)
                    resetState();
                resting = true;   // Amount 0 on every type is exactly dry: nothing to run
                return;
            }
            resting = false;

            for (int start = 0; start < numSamples; start += chunk)
            {
                const int n = std::min (chunk, numSamples - start);
                if (fade <= 0.0f && (want != current || hold))
                {
                    if (hold)
                        return;   // holding at dry
                    switchTo (want, s);
                    if (current == 0)
                        return;
                }
                smooth (s, want == current);
                updateControls (n);
                processChunk (left + start, right + start, n, want == current && ! hold ? 1.0f : 0.0f);
            }
        }

    private:
        static int variantOf (const Settings& s) noexcept
        {
            const int t = juce::jlimit (0, numTypes - 1, s.type);
            return t == (int) Type::drive ? t * 8 + driveMode (s.p[0]) : t * 8;
        }
        Type type() const noexcept { return (Type) (current / 8); }

        void switchTo (int variant, const Settings& s) noexcept
        {
            current = variant;
            fade = 0.0f;
            amount = amountFrom = 0.0f;   // Amount glides in from 0, so the new type starts from the dry signal
            p = s.p;
            resetState();
            updateControls (0);
        }

        // follow: false while fading out to another type, whose controls mean something else
        void smooth (const Settings& s, bool follow) noexcept
        {
            amountFrom = amount;
            amount += (s.amount - amount) * smoothCoef;
            if (std::abs (s.amount - amount) < 1.0e-4f)
                amount = s.amount;
            if (follow)
                for (size_t k = 0; k < 3; ++k)
                    p[k] += (s.p[k] - p[k]) * smoothCoef;
        }

        void resetState() noexcept
        {
            for (auto& l : lines)
                std::fill (l.begin(), l.end(), 0.0f);
            writePos = 0;
            for (auto& c : ch)
                c = {};
            for (int c = 0; c < 2; ++c)
                ch[(size_t) c].adaa.reset (0, 0.0);
            lfoPhase = 0.0;
            holdPhase = 0.0f;
            envFast = envSlow = envHold = envShort = 0.0f;
            settle = -1.0f;
            for (int c = 0; c < 2; ++c)
            {
                ch[(size_t) c].rotC = 1.0;
                ch[(size_t) c].rotS = 0.0;
            }
            resting = false;
        }

        // ---- Per-chunk coefficients ----
        // n: the samples this chunk covers (the LFOs advance by them)
        void updateControls (int n) noexcept
        {
            const float a = amount;
            switch (type())
            {
                case Type::drive:
                {
                    driveMode_ = current % 8;
                    pre = 1.0f + 24.0f * a * a;
                    post = std::pow (pre, driveMode_ == 3 ? -0.35f : -0.6f);
                    wetMix = std::min (1.0f, 20.0f * a);
                    bias = 0.5f * p[2];
                    biasOut = (float) AdaaCurve::f (driveMode_, bias);
                    const float tilt = 18.0f * (p[1] - 0.5f);
                    tiltHigh = juce::Decibels::decibelsToGain (0.5f * tilt);
                    tiltLow = 1.0f / tiltHigh;
                    tiltG = OnePole::gain (800.0f, sr);
                    dcG = OnePole::gain (10.0f, sr);
                    break;
                }
                case Type::crush:
                {
                    levels = std::exp2 (crushBits (p[0]) - 1.0f);
                    holdInc = crushRateHz (p[1]) / sr;
                    toneG = OnePole::gain (toneHz (p[2]), sr);
                    break;
                }
                case Type::phaser:
                case Type::flanger:
                {
                    lfoInc = (double) lfoRateHz (p[0]) / sr * n;
                    feedback = 0.95f * bipolar (p[2]);
                    if (type() == Type::phaser)
                        feedback *= 0.9f / 0.95f;
                    wetNorm = std::sqrt (1.0f - feedback * feedback);
                    for (int c = 0; c < 2; ++c)
                    {
                        const float lfo = std::sin (juce::MathConstants<float>::twoPi * (float) (lfoPhase + 0.25 * c));
                        if (type() == Type::phaser)
                            ch[(size_t) c].apG = OnePole::gain (700.0f * std::exp2 (4.0f * p[1] * lfo), sr);
                        else
                        {
                            auto& st = ch[(size_t) c];
                            st.delayFrom = st.delayTo;
                            st.delayTo = (0.0005f + 0.005f * p[1] * (0.5f + 0.5f * lfo)) * sr;
                            if (st.delayFrom <= 0.0f)
                                st.delayFrom = st.delayTo;
                        }
                    }
                    lfoPhase += lfoInc;
                    lfoPhase -= std::floor (lfoPhase);
                    break;
                }
                case Type::comb:
                {
                    combFrom = combDelay;
                    combDelay = juce::jlimit (3.0f, (float) mask - 4.0f, sr / combHz (p[0]));
                    if (n == 0)
                        combFrom = combDelay;
                    feedback = 0.95f * bipolar (p[1]);
                    wetNorm = std::sqrt (1.0f - feedback * feedback);
                    dampG = OnePole::gain (20000.0f * std::pow (0.05f, p[2]), sr);
                    break;
                }
                case Type::freqShift:
                {
                    const float hz = shiftHz (p[0]);
                    feedback = 0.9f * p[1];
                    wetNorm = std::sqrt (1.0f - feedback * feedback);
                    for (int c = 0; c < 2; ++c)
                    {
                        const double w = juce::MathConstants<double>::twoPi * (double) (c == 0 ? hz : hz * (1.0f - 2.0f * p[2])) / sr;
                        auto& st = ch[(size_t) c];
                        st.stepC = std::cos (w);
                        st.stepS = std::sin (w);
                        const double norm = 1.0 / std::sqrt (st.rotC * st.rotC + st.rotS * st.rotS);
                        st.rotC *= norm;
                        st.rotS *= norm;
                    }
                    break;
                }
                case Type::formant:
                {
                    static constexpr float vowels[5][3] = { { 800, 1150, 2900 }, { 400, 1700, 2600 }, { 300, 2250, 3000 },
                                                            { 450, 800, 2830 }, { 325, 700, 2530 } };
                    static constexpr float gains[3] = { 1.0f, 0.6f, 0.35f };
                    const float pos = juce::jlimit (0.0f, 4.0f, p[0] * 4.0f);
                    const int i0 = std::min (3, (int) pos);
                    const float t = pos - (float) i0, shift = formantShift (p[1]);
                    const float q = formantQ (p[2]);
                    formantK = 1.0f / q;
                    for (int f = 0; f < 3; ++f)
                    {
                        const float hz = std::exp (std::log (vowels[i0][f]) * (1.0f - t) + std::log (vowels[i0 + 1][f]) * t) * shift;
                        formantC[(size_t) f] = ZdfSvf::Coeffs::make (tptGain (hz, sr), formantK);
                        // k * band is a unity-peak band-pass; the make-up keeps a bright source near its own level
                        formantGain[(size_t) f] = gains[f] * formantK * 1.6f * std::sqrt (q);
                    }
                    break;
                }
                case Type::eq:
                {
                    const float dbs[3] = { eqDb (p[0]) * a, eqDb (p[1]) * a, eqDb (p[2]) * a };
                    static constexpr float freqs[3] = { 200.0f, 1000.0f, 5000.0f };
                    for (int b = 0; b < 3; ++b)
                    {
                        const float A = std::pow (10.0f, dbs[b] / 40.0f), g = tptGain (freqs[b], sr);
                        auto& c = eqBands[(size_t) b];
                        if (b == 0)        { c.k = juce::MathConstants<float>::sqrt2; c.g = g / std::sqrt (A); c.m0 = 1.0f; c.m1 = c.k * (A - 1.0f); c.m2 = A * A - 1.0f; }
                        else if (b == 1)   { c.k = 1.0f / (0.9f * A); c.g = g; c.m0 = 1.0f; c.m1 = c.k * (A * A - 1.0f); c.m2 = 0.0f; }
                        else               { c.k = juce::MathConstants<float>::sqrt2; c.g = g * std::sqrt (A); c.m0 = A * A; c.m1 = c.k * (1.0f - A) * A; c.m2 = 1.0f - A * A; }
                        c.c = ZdfSvf::Coeffs::make (c.g, c.k);
                        c.flat = std::abs (dbs[b]) < 1.0e-4f;
                    }
                    break;
                }
                case Type::transient:
                {
                    const float speed = speedScale (p[2]);
                    auto coef = [this] (float seconds) { return 1.0f - std::exp (-1.0f / (seconds * sr)); };
                    fastAtt = coef (0.0003f);
                    slowAtt = coef (0.015f * speed);
                    settleStep = 1.0f / (0.06f * speed * sr);
                    longRel = coef (0.3f * speed);
                    midRel = coef (0.12f * speed);
                    shortRel = coef (0.03f * speed);
                    attackDb = eqDb (p[0]) * a;
                    sustainDb = eqDb (p[1]) * a;
                    break;
                }
                case Type::off:
                case Type::count:
                    break;
            }
        }

        // ---- Per-sample processing ----
        void processChunk (float* left, float* right, int n, float fadeTarget) noexcept
        {
            float* io[2] = { left, right };
            float f = fade;
            // The filter and delay types are fed the faded input u = f x, so their state
            // starts from silence smoothly instead of from a step (out = x + f (wet(u) - u), which also fades out the
            // tail they still hold); the nonlinear ones crossfade (out = x + f (wet(x) - x))
            const Type t = type();
            const bool linear = t != Type::drive && t != Type::crush && t != Type::transient;
            for (int i = 0; i < n; ++i)
            {
                f = fadeTarget > f ? std::min (fadeTarget, f + fadeStep) : std::max (fadeTarget, f - fadeStep);
                const float a = amountFrom + (amount - amountFrom) * (float) (i + 1) / (float) n;
                const float g = f * f * (3.0f - 2.0f * f);   // smoothstep: no corners where the fade starts and ends
                float wet[2];
                if (linear)
                {
                    const float u[2] = { g * io[0][i], g * io[1][i] };
                    computeWet (u[0], u[1], wet, a, i, n);
                    for (int c = 0; c < 2; ++c)
                        io[c][i] += g * (wet[c] - u[c]);
                }
                else
                {
                    computeWet (io[0][i], io[1][i], wet, a, i, n);
                    for (int c = 0; c < 2; ++c)
                        io[c][i] += g * (wet[c] - io[c][i]);
                }
            }
            fade = f;
        }

        void computeWet (float l, float r, float (&wet)[2], float a, int i, int n) noexcept
        {
            const float in[2] = { l, r };
            switch (type())
            {
                case Type::drive:
                    if (amountFrom != amount)
                    {
                        // Amount moving: the curve's gains follow it per sample (a step in pre would click)
                        pre = 1.0f + 24.0f * a * a;
                        post = std::pow (pre, driveMode_ == 3 ? -0.35f : -0.6f);
                        wetMix = std::min (1.0f, 20.0f * a);
                    }
                    for (int c = 0; c < 2; ++c)
                    {
                        auto& st = ch[(size_t) c];
                        float y = post * (float) (st.adaa.process (driveMode_, (double) (pre * in[c] + bias)) - biasOut);
                        // DC blocker (the bias and the diode make some), then the tone tilt around 800 Hz
                        y -= st.dc.lowPass (y, dcG);
                        const float low = st.tone.lowPass (y, tiltG);
                        y = tiltLow * low + tiltHigh * (y - low);
                        wet[c] = in[c] + wetMix * (y - in[c]);
                    }
                    return;
                case Type::crush:
                {
                    holdPhase += holdInc;
                    const bool sample = holdPhase >= 1.0f;
                    if (sample)
                        holdPhase -= std::floor (holdPhase);
                    for (int c = 0; c < 2; ++c)
                    {
                        auto& st = ch[(size_t) c];
                        if (sample)
                            st.held = std::round (in[c] * levels) / levels;
                        const float y = st.tone.lowPass (st.held, toneG);
                        wet[c] = in[c] + a * (y - in[c]);
                    }
                    return;
                }
                case Type::phaser:
                    for (int c = 0; c < 2; ++c)
                    {
                        auto& st = ch[(size_t) c];
                        float x = in[c] + feedback * st.last;
                        for (auto& stage : st.stages)
                            x = stage.allPass (x, st.apG);
                        st.last = x;
                        wet[c] = in[c] + a * (0.5f * (in[c] + wetNorm * x) - in[c]);
                    }
                    return;
                case Type::flanger:
                {
                    const float t = (float) (i + 1) / (float) n;
                    for (int c = 0; c < 2; ++c)
                    {
                        auto& st = ch[(size_t) c];
                        const float d = st.delayFrom + (st.delayTo - st.delayFrom) * t;
                        const float tapped = tap (c, d);
                        lines[(size_t) c][(size_t) writePos] = in[c] + feedback * tapped;
                        wet[c] = in[c] + a * (0.70710678f * (in[c] + wetNorm * tapped) - in[c]);
                    }
                    writePos = (writePos + 1) & mask;
                    return;
                }
                case Type::comb:
                {
                    const float d = combFrom + (combDelay - combFrom) * (float) (i + 1) / (float) n;
                    for (int c = 0; c < 2; ++c)
                    {
                        auto& st = ch[(size_t) c];
                        const float y = in[c] + feedback * st.damp.lowPass (tap (c, d), dampG);
                        lines[(size_t) c][(size_t) writePos] = y;
                        wet[c] = in[c] + a * (wetNorm * y - in[c]);
                    }
                    writePos = (writePos + 1) & mask;
                    return;
                }
                case Type::freqShift:
                    for (int c = 0; c < 2; ++c)
                    {
                        auto& st = ch[(size_t) c];
                        const auto iq = st.hilbert.process (in[c] + feedback * st.last);
                        const float y = iq.first * (float) st.rotC + iq.second * (float) st.rotS;
                        const double rc = st.rotC * st.stepC - st.rotS * st.stepS;
                        st.rotS = st.rotS * st.stepC + st.rotC * st.stepS;
                        st.rotC = rc;
                        st.last = y;
                        wet[c] = in[c] + a * (wetNorm * y - in[c]);
                    }
                    return;
                case Type::formant:
                    for (int c = 0; c < 2; ++c)
                    {
                        auto& st = ch[(size_t) c];
                        float y = 0.0f;
                        for (size_t f = 0; f < 3; ++f)
                            y += formantGain[f] * st.svf[f].process (in[c], formantC[f]).band;
                        wet[c] = in[c] + a * (y - in[c]);
                    }
                    return;
                case Type::eq:
                    for (int c = 0; c < 2; ++c)
                    {
                        auto& st = ch[(size_t) c];
                        float x = in[c];
                        for (size_t b = 0; b < 3; ++b)
                        {
                            const auto& k = eqBands[b];
                            if (k.flat)
                            {
                                st.svf[b].reset();
                                continue;
                            }
                            const auto o = st.svf[b].process (x, k.c);
                            x = k.m0 * x + k.m1 * o.band + k.m2 * o.low;
                        }
                        wet[c] = x;
                    }
                    return;
                case Type::transient:
                {
                    const float level = std::max (std::abs (in[0]), std::abs (in[1])) + 1.0e-9f;
                    auto follow = [] (float& e, float x, float att, float rel) { e += (x - e) * (x > e ? att : rel); };
                    follow (envFast, level, fastAtt, midRel);
                    follow (envSlow, level, slowAtt, midRel);
                    follow (envHold, level, fastAtt, longRel);
                    follow (envShort, level, fastAtt, shortRel);
                    // How far the fast follower leads the slow one (the attack), and the long release trails the
                    // short one (the tail), each saturating at 12 dB
                    const float lead = juce::jlimit (0.0f, 1.0f, 0.5f * std::log2 ((envFast + 1.0e-9f) / (envSlow + 1.0e-9f)));
                    const float trail = juce::jlimit (0.0f, 1.0f, 0.5f * std::log2 ((envHold + 1.0e-9f) / (envShort + 1.0e-9f)));
                    // Switched in mid-note, the followers start from silence and would hear an onset: the gain
                    // comes in over four of the slow follower's attack times. Switched in during silence they are right.
                    if (settle < 0.0f)
                        settle = level < 1.0e-6f ? 1.0f : 0.0f;
                    settle = std::min (1.0f, settle + settleStep);
                    const float g = fastExp2 ((attackDb * lead + sustainDb * trail) * settle * settle * (3.0f - 2.0f * settle) * 0.16609640f);
                    wet[0] = in[0] * g;
                    wet[1] = in[1] * g;
                    return;
                }
                case Type::off:
                case Type::count:
                    break;
            }
            wet[0] = l;
            wet[1] = r;
        }

        float tap (int c, float delay) const noexcept
        {
            const float pos = (float) writePos - delay;
            const int i1 = (int) std::floor (pos);
            const float f = pos - (float) i1;
            const auto& l = lines[(size_t) c];
            const float y0 = l[(size_t) ((i1 - 1) & mask)], y1 = l[(size_t) (i1 & mask)];
            const float y2 = l[(size_t) ((i1 + 1) & mask)], y3 = l[(size_t) ((i1 + 2) & mask)];
            const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * f + c2) * f + c1) * f + y1;
        }

        static constexpr int chunk = 16;
        float sr = 96000.0f, fadeStep = 0.002f, smoothCoef = 0.1f;
        int current = 0;                    // type * 8 + drive mode, 0 = off
        float fade = 0.0f, amount = 0.0f, amountFrom = 0.0f;   // Amount at the end of this chunk and the last
        bool resting = false;
        std::array<float, 3> p { 0.5f, 0.5f, 0.5f };

        struct Channel
        {
            AdaaCurve adaa;
            OnePole dc, tone, damp;
            std::array<OnePole, 6> stages;
            std::array<ZdfSvf, 3> svf;
            Hilbert hilbert;
            float last = 0.0f, held = 0.0f, apG = 0.0f, delayFrom = 0.0f, delayTo = 0.0f;
            double rotC = 1.0, rotS = 0.0, stepC = 1.0, stepS = 0.0;
        };
        std::array<Channel, 2> ch;
        std::array<std::vector<float>, 2> lines;
        int mask = 0, writePos = 0;

        // drive
        int driveMode_ = 0;
        float pre = 1.0f, post = 1.0f, wetMix = 0.0f, bias = 0.0f, biasOut = 0.0f, tiltHigh = 1.0f, tiltLow = 1.0f, tiltG = 0.0f, dcG = 0.0f;
        // crush
        float levels = 32768.0f, holdInc = 1.0f, holdPhase = 0.0f, toneG = 0.0f;
        // modulated delays and feedback types
        double lfoPhase = 0.0, lfoInc = 0.0;
        float feedback = 0.0f, wetNorm = 1.0f, combDelay = 3.0f, combFrom = 3.0f, dampG = 0.0f;
        // formant
        float formantK = 0.5f;
        std::array<ZdfSvf::Coeffs, 3> formantC {};
        std::array<float, 3> formantGain {};
        // eq: Simper's SVF shelf/bell, out = m0 x + m1 band + m2 low
        struct EqBand { float g = 0.0f, k = 1.0f, m0 = 1.0f, m1 = 0.0f, m2 = 0.0f; ZdfSvf::Coeffs c {}; bool flat = true; };
        std::array<EqBand, 3> eqBands;
        // transient
        float settle = 0.0f, settleStep = 0.0f;
        float envFast = 0.0f, envSlow = 0.0f, envHold = 0.0f, envShort = 0.0f;
        float fastAtt = 0.0f, slowAtt = 0.0f, longRel = 0.0f, midRel = 0.0f, shortRel = 0.0f, attackDb = 0.0f, sustainDb = 0.0f;
    };

    // The two slots in series, in either order
    class Chain
    {
    public:
        void prepare (double sampleRate)
        {
            for (auto& s : slots)
                s.prepare (sampleRate);
            swapped = false;
        }

        void process (float* left, float* right, int numSamples, const std::array<Settings, 2>& settings, bool swap) noexcept
        {
            const bool hold = swap != swapped;
            if (hold && slots[0].isFadedOut() && slots[1].isFadedOut())
                swapped = swap;
            const bool holding = swap != swapped;
            const int first = swapped ? 1 : 0;
            slots[(size_t) first].process (left, right, numSamples, settings[(size_t) first], holding);
            slots[(size_t) (1 - first)].process (left, right, numSamples, settings[(size_t) (1 - first)], holding);
        }

    private:
        std::array<Slot, 2> slots;
        bool swapped = false;
    };
}
}

