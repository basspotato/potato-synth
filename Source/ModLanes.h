#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>

namespace ps
{
// Mod step lanes: four step patterns that move the chain knobs, SUPER or the XY pad in time. Each lane has a
// target, a bipolar depth, a rate on the arp's step grid, a length and a smooth amount (the share of each step
// spent gliding into the next value, with an S-curve). A lane adds depth x value to its target's position; two
// lanes on one target add up. The knobs' own 30 ms glide follows, so a step never clicks, and the Harmony
// Engine keeps every position in harmony.
//
// Clock: the host's beat while its transport plays, otherwise an internal clock that runs while a key is
// held and restarts on the first key. With neither, the lanes rest at zero. Values are read once per block, at
// its centre, so a step lands within half a block of its beat (flagged: under 6 ms at 512 samples, 48 kHz).
class ModLanes
{
public:
    static constexpr int numLanes = 4, maxSteps = 32;
    // Targets: 0 = off, 1..8 the chain knobs, then SUPER, XY X, XY Y, CUTOFF
    static constexpr int numTargets = 12, superTarget = 8, padXTarget = 9, padYTarget = 10, cutoffTarget = 11;

    struct Settings
    {
        int target = 0;          // 0 = off, else 1 + index into the outputs
        float depth = 0.5f;      // -1..1
        double stepBeats = 0.25;
        int length = 16;
        float smooth = 0.0f;     // 0..1
    };

    ModLanes()
    {
        // Default shapes, so a lane moves as soon as it has a target: a ramp, a pulse, a triangle and a fixed
        // pseudo-random pattern
        for (int s = 0; s < maxSteps; ++s)
        {
            const float x = (float) (s % 16) / 15.0f;
            values[0][(size_t) s].store (x);
            values[1][(size_t) s].store ((s & 1) == 0 ? 1.0f : 0.0f);
            values[2][(size_t) s].store (1.0f - std::abs (2.0f * x - 1.0f));
            values[3][(size_t) s].store (std::fmod (0.618034f * (float) (s * 7 + 3), 1.0f));
        }
    }

    // ---- Message thread -----------------------------------------------------------------------------------
    void setValue (int lane, int step, float v) noexcept
    {
        values[(size_t) juce::jlimit (0, numLanes - 1, lane)][(size_t) juce::jlimit (0, maxSteps - 1, step)].store (juce::jlimit (0.0f, 1.0f, v));
    }

    float getValue (int lane, int step) const noexcept
    {
        return values[(size_t) juce::jlimit (0, numLanes - 1, lane)][(size_t) juce::jlimit (0, maxSteps - 1, step)].load();
    }

    // ---- Audio thread -------------------------------------------------------------------------------------
    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset() noexcept { keysDown = 0; internalBeat = 0.0; }

    // The offsets for this block (added to the targets' positions); `in` is the block's incoming MIDI
    std::array<float, numTargets> process (const juce::MidiBuffer& in, int numSamples, double bpm, double ppq,
                                           const std::array<Settings, numLanes>& lanes) noexcept
    {
        const double beatsPerSample = juce::jlimit (20.0, 999.0, bpm) / (60.0 * sr);
        const bool wasHeld = keysDown > 0;
        for (const auto meta : in)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())
                ++keysDown;
            else if (m.isNoteOff())
                keysDown = juce::jmax (0, keysDown - 1);
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                keysDown = 0;
        }

        std::array<float, numTargets> out {};
        double beat;
        if (ppq >= 0.0)
        {
            beat = ppq + 0.5 * numSamples * beatsPerSample;
        }
        else
        {
            if (! wasHeld && keysDown > 0)
                internalBeat = 0.0;
            if (! wasHeld && keysDown == 0)
                return out;
            beat = internalBeat + 0.5 * numSamples * beatsPerSample;
            internalBeat = keysDown > 0 ? internalBeat + numSamples * beatsPerSample : 0.0;
        }

        for (int l = 0; l < numLanes; ++l)
        {
            const auto& s = lanes[(size_t) l];
            if (s.target <= 0 || s.target > numTargets)
                continue;
            const int length = juce::jlimit (1, maxSteps, s.length);
            const double pos = beat / s.stepBeats;
            const auto step = (int64_t) std::floor (pos);
            const float frac = (float) (pos - (double) step);
            const auto index = (size_t) (((step % length) + length) % length);
            float v = values[(size_t) l][index].load();
            if (s.smooth > 1.0e-3f)
            {
                const float t = juce::jlimit (0.0f, 1.0f, (frac - (1.0f - s.smooth)) / s.smooth);
                const float next = values[(size_t) l][(index + 1) % (size_t) length].load();
                v += (next - v) * t * t * (3.0f - 2.0f * t);
            }
            out[(size_t) (s.target - 1)] += s.depth * v;
        }
        return out;
    }

private:
    double sr = 48000.0;
    std::array<std::array<std::atomic<float>, maxSteps>, numLanes> values;
    int keysDown = 0;
    double internalBeat = 0.0;
};
}
