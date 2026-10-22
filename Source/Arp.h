#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>

namespace ps
{
// The note generator in front of the layer stack: an arpeggiator and a step sequencer on one clock and one
// step grid, so every layer plays the same notes.
//
// Clock: the host's beat position while its transport runs (steps land on the bar grid; a jump or loop sends
// every note off and picks the grid up again), otherwise an internal clock that starts on the first key.
// A key pressed up to a quarter step late (from silence) still plays the step it was meant for.
// Swing delays every second step by up to half a step; Gate sets each note's length (over 100 % they overlap
// the next note, legato). Each grid step reads one step of the pattern: on (or a rest), tie (hold the last
// note through it), velocity, probability, ratchet (2-4 even hits) and, for the sequencer, a note offset.
//
// Arp: the held keys (as played, or sorted) over 1-4 octaves; Chord plays them all on every step. Seq: the
// pattern's offsets play from the last held key (C3 with no key while the host plays, in Host trigger).
// Latch (and the sustain pedal, which the generator then takes) keeps released keys in the pattern until a new
// key is pressed with none down. Switching the generator off stops its notes and sounds the keys still held.
// No allocation: every list is fixed-size. Steps are edited on the message thread through atomics.
class NoteGenerator
{
public:
    enum class Mode { off, arp, seq };
    enum class Direction { up, down, upDown, random, asPlayed, chord };
    static constexpr int maxSteps = 32, maxHeld = 32, maxSounding = 64;

    struct Settings
    {
        Mode mode = Mode::off;
        Direction direction = Direction::up;
        double stepBeats = 0.25;
        int octaves = 1, length = 16;
        float gate = 0.6f, swing = 0.0f;   // gate: share of a step (0.05..2); swing 0..1 (1 = half a step late)
        bool latch = false, hostTrigger = false;
    };

    struct Step
    {
        bool on = true, tie = false;
        int offset = 0, ratchet = 1;        // semitones (-24..24), hits per step (1..4)
        float velocity = 1.0f, probability = 1.0f;
    };

    // ---- Message thread: the step pattern ----------------------------------------------------------------
    void setStep (int index, const Step& s) noexcept
    {
        auto& d = steps[(size_t) juce::jlimit (0, maxSteps - 1, index)];
        d.on.store (s.on);
        d.tie.store (s.tie);
        d.offset.store (juce::jlimit (-24, 24, s.offset));
        d.ratchet.store (juce::jlimit (1, 4, s.ratchet));
        d.velocity.store (juce::jlimit (0.0f, 1.0f, s.velocity));
        d.probability.store (juce::jlimit (0.0f, 1.0f, s.probability));
    }

    Step getStep (int index) const noexcept
    {
        const auto& d = steps[(size_t) juce::jlimit (0, maxSteps - 1, index)];
        return { d.on.load(), d.tie.load(), d.offset.load(), d.ratchet.load(), d.velocity.load(), d.probability.load() };
    }

    // ---- Audio thread -------------------------------------------------------------------------------------
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        numHeld = numDown = numSounding = 0;
        running = false;
        pedal = false;
        lastMode = Mode::off;
        arpIndex = 0;
        lastGridStep = std::numeric_limits<int64_t>::min();
        expectedBeat = -1.0;
        internalBeat = 0.0;
        sampleClock = 0;
    }

    // Reads `in`, writes the notes to play into `out` (cleared first). ppq < 0: the host transport is stopped.
    void process (const juce::MidiBuffer& in, juce::MidiBuffer& out, int numSamples, double bpm, double ppq, const Settings& s)
    {
        out.clear();
        const double beatsPerSample = juce::jlimit (20.0, 999.0, bpm) / (60.0 * sr);
        const bool active = s.mode != Mode::off;
        hostClock = ppq >= 0.0;
        blockBeat0 = hostClock ? ppq : internalBeat;

        if (s.mode != lastMode)
        {
            // Off -> on: the keys sounding directly stop and the generator takes them. On -> off: its notes stop
            // and the keys still down sound directly. Arp <-> Seq: it restarts.
            stopAll (out, 0);
            if (lastMode == Mode::off)
                for (int i = 0; i < numHeld; ++i)
                    out.addEvent (juce::MidiMessage::noteOff (1, held[(size_t) i].note), 0);
            if (! active)
            {
                dropReleased();
                for (int i = 0; i < numHeld; ++i)
                    out.addEvent (juce::MidiMessage::noteOn (1, held[(size_t) i].note, held[(size_t) i].velocity), 0);
            }
            running = false;
            lastMode = s.mode;
            if (active && numHeld > 0)
                start (out, 0, beatsPerSample, s);
        }

        if (! active)
        {
            // Pass everything through; track the keys so switching on picks them up
            for (const auto meta : in)
            {
                const auto m = meta.getMessage();
                trackDirect (m);
                out.addEvent (m, meta.samplePosition);
            }
            expectedBeat = -1.0;
            sampleClock += numSamples;
            return;
        }

        // A transport jump, start or stop: every note off, and the pattern picks the grid up again
        if (hostClock != (expectedBeat >= 0.0) || (hostClock && std::abs (ppq - expectedBeat) > 1.0e-3))
        {
            stopAll (out, 0);
            running = false;
            if (! hostClock)
                blockBeat0 = internalBeat = 0.0;
            if (numHeld > 0)
                start (out, 0, beatsPerSample, s);
        }
        // The sequencer in Host trigger runs with the transport, keys or not
        if (s.mode == Mode::seq && s.hostTrigger && hostClock && ! running)
        {
            running = true;
            lastGridStep = std::numeric_limits<int64_t>::min();
        }

        int pos = 0;
        auto it = in.begin();
        while (pos < numSamples)
        {
            const int next = it != in.end() ? juce::jmin (numSamples, (*it).samplePosition) : numSamples;
            if (next > pos)
            {
                runSteps (out, pos, next, beatsPerSample, s);
                pos = next;
            }
            if (it == in.end())
                break;
            for (; it != in.end() && (*it).samplePosition <= pos; ++it)
                handleInput ((*it).getMessage(), out, pos, beatsPerSample, s);
        }
        flushOffs (out, numSamples);

        const double beatEnd = blockBeat0 + numSamples * beatsPerSample;
        expectedBeat = hostClock ? beatEnd : -1.0;
        if (! hostClock)
            internalBeat = running ? beatEnd : 0.0;
        sampleClock += numSamples;
    }

private:
    struct Key { int note = 0; float velocity = 0.0f; bool down = false; };
    struct Sounding { int note = 0; int64_t offAt = 0; };
    struct AtomicStep
    {
        std::atomic<bool> on { true }, tie { false };
        std::atomic<int> offset { 0 }, ratchet { 1 };
        std::atomic<float> velocity { 1.0f }, probability { 1.0f };
    };

    // ---- Keys -----------------------------------------------------------------------------------------------
    void trackDirect (const juce::MidiMessage& m) noexcept
    {
        if (m.isNoteOn())
            addKey (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff())
            removeKey (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            numHeld = numDown = 0;
    }

    void addKey (int note, float velocity) noexcept
    {
        for (int i = 0; i < numHeld; ++i)
            if (held[(size_t) i].note == note)
            {
                if (! held[(size_t) i].down)
                    ++numDown;
                held[(size_t) i] = { note, velocity, true };
                return;
            }
        if (numHeld == maxHeld)
            return;
        held[(size_t) numHeld++] = { note, velocity, true };
        ++numDown;
    }

    // Released while latched or on the pedal: kept, marked up
    void releaseKey (int note, bool keep) noexcept
    {
        for (int i = 0; i < numHeld; ++i)
            if (held[(size_t) i].note == note && held[(size_t) i].down)
            {
                --numDown;
                if (keep)
                    held[(size_t) i].down = false;
                else
                    removeAt (i);
                return;
            }
    }

    void removeKey (int note) noexcept
    {
        for (int i = 0; i < numHeld; ++i)
            if (held[(size_t) i].note == note)
            {
                if (held[(size_t) i].down)
                    --numDown;
                removeAt (i);
                return;
            }
    }

    void removeAt (int i) noexcept
    {
        for (int j = i; j + 1 < numHeld; ++j)
            held[(size_t) j] = held[(size_t) (j + 1)];
        --numHeld;
    }

    void dropReleased() noexcept
    {
        for (int i = numHeld - 1; i >= 0; --i)
            if (! held[(size_t) i].down)
                removeAt (i);
    }

    void handleInput (const juce::MidiMessage& m, juce::MidiBuffer& out, int pos, double beatsPerSample, const Settings& s)
    {
        if (m.isNoteOn())
        {
            // A new key with none down replaces a latched (or pedal-held) pattern
            if (numDown == 0)
                numHeld = 0;
            const bool wasEmpty = numHeld == 0 && ! running;
            addKey (m.getNoteNumber(), m.getFloatVelocity());
            if (wasEmpty)
                start (out, pos, beatsPerSample, s);
        }
        else if (m.isNoteOff())
        {
            releaseKey (m.getNoteNumber(), s.latch || pedal);
        }
        else if (m.isSustainPedalOn())
        {
            pedal = true;
        }
        else if (m.isSustainPedalOff())
        {
            pedal = false;
            if (! s.latch)
                dropReleased();
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            numHeld = numDown = 0;
            stopAll (out, pos);
            running = false;
            out.addEvent (m, pos);
        }
        else
        {
            out.addEvent (m, pos);
        }
    }

    // The first key from silence: the internal clock starts at this sample, or the pattern joins the host's
    // grid, playing the current step at once when the key came less than a quarter step late
    void start (juce::MidiBuffer& out, int pos, double beatsPerSample, const Settings& s)
    {
        running = true;
        arpIndex = 0;
        lastGridStep = std::numeric_limits<int64_t>::min();
        if (! hostClock)
        {
            blockBeat0 = -pos * beatsPerSample;   // beat 0 here: runSteps plays step 0 at this sample
            return;
        }
        const double beat = blockBeat0 + pos * beatsPerSample;
        const auto k = (int64_t) std::floor (beat / s.stepBeats);
        const double into = beat - stepStart (k, s);
        if (into < 0.0)
        {
            lastGridStep = k - 1;   // a swung step that hasn't started yet
        }
        else
        {
            if (into < 0.25 * s.stepBeats)
                playStep (out, pos, k, beatsPerSample, s);
            lastGridStep = k;
        }
    }

    // ---- Steps ----------------------------------------------------------------------------------------------
    double stepStart (int64_t k, const Settings& s) const noexcept
    {
        return ((double) k + ((k & 1) != 0 ? 0.5 * s.swing : 0.0)) * s.stepBeats;
    }

    void runSteps (juce::MidiBuffer& out, int from, int to, double beatsPerSample, const Settings& s)
    {
        flushOffs (out, from);
        if (! running)
        {
            flushOffs (out, to);
            return;
        }
        if (numHeld == 0 && ! (s.mode == Mode::seq && s.hostTrigger && hostClock))
        {
            // Every key gone: the notes finish their gates, the clock stops
            running = false;
            flushOffs (out, to);
            return;
        }
        const double beat0 = blockBeat0, b0 = beat0 + from * beatsPerSample;
        int64_t k = lastGridStep == std::numeric_limits<int64_t>::min() ? (int64_t) std::floor (b0 / s.stepBeats) - 1 : lastGridStep;
        for (;;)
        {
            const int64_t next = k + 1;
            // The step's nearest sample (rounding, so clock drift in the last bits can't pull it a sample early)
            const auto at = (int64_t) std::llround ((stepStart (next, s) - beat0) / beatsPerSample);
            if (at >= to)
                break;
            if (at >= from)
            {
                flushOffs (out, (int) at);
                playStep (out, (int) at, next, beatsPerSample, s);
            }
            lastGridStep = k = next;
        }
        flushOffs (out, to);
    }

    void playStep (juce::MidiBuffer& out, int sample, int64_t k, double beatsPerSample, const Settings& s)
    {
        const int length = juce::jlimit (1, maxSteps, s.length);
        const auto& st = steps[(size_t) (((k % length) + length) % length)];
        const double stepSamples = s.stepBeats / beatsPerSample;

        if (st.tie.load())
            return;   // held through by the step before (below)
        if (! st.on.load() || chance (k) >= st.probability.load())
            return;

        std::array<int, maxHeld> notes {};
        std::array<float, maxHeld> velocities {};
        const int count = s.mode == Mode::arp ? nextArpNotes (notes, velocities, s) : seqNote (notes, velocities, st.offset.load());
        if (count == 0)
            return;
        const float stepVelocity = st.velocity.load();
        const int hits = juce::jlimit (1, 4, st.ratchet.load());
        const double hitSamples = stepSamples / hits;
        // Tied steps after this one: the (last) note is held through them
        int tied = 0;
        while (tied < length - 1 && steps[(size_t) ((((k + 1 + tied) % length) + length) % length)].tie.load())
            ++tied;
        for (int h = 0; h < hits; ++h)
        {
            const int at = sample + (int) std::lround (h * hitSamples);
            // Ratchet hits never overlap each other
            const float gate = hits > 1 ? juce::jmin (0.9f, s.gate) : s.gate;
            const auto gateSamples = (int64_t) juce::jmax (1.0, std::round (hitSamples * gate + (h == hits - 1 ? tied * stepSamples : 0.0)));
            for (int i = 0; i < count; ++i)
            {
                noteOn (out, at, notes[(size_t) i], juce::jlimit (0.01f, 1.0f, velocities[(size_t) i] * stepVelocity),
                        sampleClock + at + gateSamples);
            }
        }
    }

    int nextArpNotes (std::array<int, maxHeld>& notes, std::array<float, maxHeld>& velocities, const Settings& s) noexcept
    {
        if (numHeld == 0)
            return 0;
        // The held keys, sorted by pitch (or as played)
        std::array<int, maxHeld> order {};
        for (int i = 0; i < numHeld; ++i)
            order[(size_t) i] = i;
        if (s.direction != Direction::asPlayed)
            std::sort (order.begin(), order.begin() + numHeld, [this] (int a, int b) { return held[(size_t) a].note < held[(size_t) b].note; });
        const int octaves = juce::jlimit (1, 4, s.octaves);

        if (s.direction == Direction::chord)
        {
            const int octave = (arpIndex++) % octaves;
            for (int i = 0; i < numHeld; ++i)
            {
                notes[(size_t) i] = juce::jlimit (0, 127, held[(size_t) order[(size_t) i]].note + 12 * octave);
                velocities[(size_t) i] = held[(size_t) order[(size_t) i]].velocity;
            }
            return numHeld;
        }

        const int span = numHeld * octaves;
        int index = 0;
        switch (s.direction)
        {
            case Direction::down:     index = span - 1 - (arpIndex % span); break;
            case Direction::upDown:
            {
                const int cycle = span > 1 ? 2 * (span - 1) : 1;
                const int p = arpIndex % cycle;
                index = p < span ? p : cycle - p;
                break;
            }
            case Direction::random:   index = (int) (chance (arpIndex + 0x5bd1e995LL) * (float) span) % span; break;
            default:                  index = arpIndex % span; break;
        }
        ++arpIndex;
        const auto& key = held[(size_t) order[(size_t) (index % numHeld)]];
        notes[0] = juce::jlimit (0, 127, key.note + 12 * (index / numHeld));
        velocities[0] = key.velocity;
        return 1;
    }

    int seqNote (std::array<int, maxHeld>& notes, std::array<float, maxHeld>& velocities, int offset) const noexcept
    {
        const int root = numHeld > 0 ? held[(size_t) (numHeld - 1)].note : 60;
        notes[0] = juce::jlimit (0, 127, root + offset);
        velocities[0] = numHeld > 0 ? held[(size_t) (numHeld - 1)].velocity : 0.8f;
        return 1;
    }

    // Deterministic 0..1 per step (probability and random order repeat with the grid)
    static float chance (int64_t k) noexcept
    {
        auto h = (uint64_t) k * 0x9E3779B97F4A7C15ull;
        h ^= h >> 31;
        h *= 0xBF58476D1CE4E5B9ull;
        h ^= h >> 29;
        return (float) (h >> 40) * (1.0f / 16777216.0f);
    }

    // ---- Sounding notes -------------------------------------------------------------------------------------
    void noteOn (juce::MidiBuffer& out, int at, int note, float velocity, int64_t offAt)
    {
        // Retriggering a sounding pitch: its note off goes first
        for (int i = 0; i < numSounding; ++i)
            if (sounding[(size_t) i].note == note)
            {
                out.addEvent (juce::MidiMessage::noteOff (1, note), at);
                sounding[(size_t) i] = sounding[(size_t) --numSounding];
                break;
            }
        if (numSounding == maxSounding)
            return;
        out.addEvent (juce::MidiMessage::noteOn (1, note, velocity), at);
        sounding[(size_t) numSounding++] = { note, offAt };
    }

    // Note offs due before `until` (a block-relative sample)
    void flushOffs (juce::MidiBuffer& out, int until)
    {
        for (int i = numSounding - 1; i >= 0; --i)
        {
            const int64_t rel = sounding[(size_t) i].offAt - sampleClock;
            if (rel < until)
            {
                out.addEvent (juce::MidiMessage::noteOff (1, sounding[(size_t) i].note), (int) juce::jmax ((int64_t) 0, rel));
                sounding[(size_t) i] = sounding[(size_t) --numSounding];
            }
        }
    }

    void stopAll (juce::MidiBuffer& out, int at)
    {
        for (int i = 0; i < numSounding; ++i)
            out.addEvent (juce::MidiMessage::noteOff (1, sounding[(size_t) i].note), at);
        numSounding = 0;
    }

    double sr = 48000.0;
    std::array<AtomicStep, maxSteps> steps;
    std::array<Key, maxHeld> held {};
    std::array<Sounding, maxSounding> sounding {};
    int numHeld = 0, numDown = 0, numSounding = 0;
    bool running = false, pedal = false, hostClock = false;
    Mode lastMode = Mode::off;
    int arpIndex = 0;
    int64_t lastGridStep = std::numeric_limits<int64_t>::min();
    double expectedBeat = -1.0, internalBeat = 0.0, blockBeat0 = 0.0;
    int64_t sampleClock = 0;
};
}
