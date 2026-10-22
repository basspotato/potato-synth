#pragma once
#include "Layer.h"
#include <array>

namespace ps
{
// The super stack: note slots shared by every layer, so one played note sounds on all layers at once.
// Note slot i drives voice i of each layer's engine.
class LayerStack
{
public:
    static constexpr int numLayers = 4;
    static constexpr int numNoteSlots = 16;

    Layer& layer (int index) { return layers[(size_t) index]; }
    const Layer& layer (int index) const { return layers[(size_t) index]; }

    // ---- Called while audio is stopped -----------------------------------------------------------
    void prepare (double sampleRate, int maxBlockSize)
    {
        maxBlock = juce::jmax (1, maxBlockSize);
        for (auto& l : layers)
            l.prepare (sampleRate, maxBlock, numNoteSlots);
        reset();
    }

    void reset()
    {
        slots = {};
        sustainPedal = false;
        for (auto& l : layers)
            l.reset();
    }

    // ---- Message thread ----------------------------------------------------------------------------
    void collectGarbage()
    {
        for (auto& l : layers)
            l.collectGarbage();
    }

    // ---- Audio thread ----------------------------------------------------------------------------
    // Renders into a stereo buffer, splitting at MIDI events so every note starts on its exact sample.
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi)
    {
        for (auto& l : layers)
            l.beginBlock();

        const int numSamples = buffer.getNumSamples();
        int pos = 0;
        for (const auto metadata : midi)
        {
            const int eventPos = juce::jlimit (0, numSamples, metadata.samplePosition);
            renderRange (buffer, pos, eventPos - pos);
            pos = eventPos;
            handleMidi (metadata.getMessage());
        }
        renderRange (buffer, pos, numSamples - pos);
        freeFinishedSlots();
    }

    int activeNoteCount() const
    {
        int n = 0;
        for (auto& s : slots)
            n += s.note >= 0 ? 1 : 0;
        return n;
    }

private:
    struct NoteSlot
    {
        int note = -1;            // -1: free
        bool held = false;        // key is down
        bool sustained = false;   // key released while the sustain pedal was down
        uint32_t order = 0;       // when it started, for stealing
    };

    void handleMidi (const juce::MidiMessage& m)
    {
        if (m.isNoteOn())
            noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff())
            noteOff (m.getNoteNumber());
        else if (m.isSustainPedalOn())
            sustainPedal = true;
        else if (m.isSustainPedalOff())
        {
            sustainPedal = false;
            for (int i = 0; i < numNoteSlots; ++i)
                if (slots[(size_t) i].sustained)
                    releaseSlot (i);
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            for (int i = 0; i < numNoteSlots; ++i)
                if (slots[(size_t) i].note >= 0)
                    releaseSlot (i);
        }
    }

    void noteOn (int note, float velocity)
    {
        const int i = findSlotFor (note);
        auto& s = slots[(size_t) i];
        s = { note, true, false, ++orderCounter };
        for (auto& l : layers)
            l.startVoice (i, note, velocity, s.order);
    }

    void noteOff (int note)
    {
        for (int i = 0; i < numNoteSlots; ++i)
        {
            auto& s = slots[(size_t) i];
            if (s.note != note || ! s.held)
                continue;
            if (sustainPedal)
            {
                s.held = false;
                s.sustained = true;
            }
            else
                releaseSlot (i);
        }
    }

    void releaseSlot (int i)
    {
        auto& s = slots[(size_t) i];
        s.held = s.sustained = false;
        for (auto& l : layers)
            l.releaseVoice (i);
    }

    // Same note again reuses its slot (a click-free retrigger). Otherwise: a free slot, then the oldest
    // released slot, then the oldest held one.
    int findSlotFor (int note) const
    {
        for (int i = 0; i < numNoteSlots; ++i)
            if (slots[(size_t) i].note == note)
                return i;

        int bestReleased = -1, bestHeld = -1;
        for (int i = 0; i < numNoteSlots; ++i)
        {
            const auto& s = slots[(size_t) i];
            if (s.note < 0)
                return i;
            int& best = (s.held || s.sustained) ? bestHeld : bestReleased;
            if (best < 0 || s.order < slots[(size_t) best].order)
                best = i;
        }
        return bestReleased >= 0 ? bestReleased : bestHeld;
    }

    // A released slot becomes free once no layer is still sounding it.
    void freeFinishedSlots()
    {
        for (int i = 0; i < numNoteSlots; ++i)
        {
            auto& s = slots[(size_t) i];
            if (s.note < 0 || s.held || s.sustained)
                continue;
            bool sounding = false;
            for (auto& l : layers)
                sounding = sounding || l.isVoiceActive (i);
            if (! sounding)
                s = {};
        }
    }

    void renderRange (juce::AudioBuffer<float>& buffer, int start, int length)
    {
        while (length > 0)
        {
            const int n = juce::jmin (length, maxBlock);
            float* l = buffer.getWritePointer (0, start);
            float* r = buffer.getWritePointer (1, start);
            for (auto& layer : layers)
                layer.render (l, r, n);
            start += n;
            length -= n;
        }
    }

    std::array<Layer, numLayers> layers;
    std::array<NoteSlot, numNoteSlots> slots {};
    bool sustainPedal = false;
    uint32_t orderCounter = 0;
    int maxBlock = 512;
};
}
