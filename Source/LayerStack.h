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
    static constexpr int numLayers = params::numLayers;
    static constexpr int numNoteSlots = 16;

    Layer& layer (int index) { return layers[(size_t) index]; }
    const Layer& layer (int index) const { return layers[(size_t) index]; }

    // ---- Called while audio is stopped -----------------------------------------------------------
    void prepare (double sampleRate, int maxBlockSize)
    {
        maxBlock = juce::jmax (1, maxBlockSize);
        rate = sampleRate;
        for (auto& l : layers)
            l.prepare (sampleRate, maxBlock, numNoteSlots);
        subEnvelope.assign ((size_t) maxBlock, 0.0f);
        clock = 0;
        signature = 0.0;
        reset();
    }

    void reset()
    {
        slots = {};
        sustainPedal = false;
        channelBend = {};
        channelPressure = {};
        channelTimbre.fill (0.5f);
        bendNow = {};
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
        updateAlignment();
    }

    // How far the sub ducks the body's lows, 0..1 (Layer::setLowDuck)
    void setLowDuck (float depth) noexcept { lowDuckDepth = depth; }

    // Sub/body phase alignment strength, 0..1 (PhaseAlign.h)
    void setAlignStrength (float s) noexcept
    {
        for (auto& l : layers)
            l.setAlignStrength (s);
    }

    // Pitch bend: range in semitones for the whole keyboard; with MPE on, channels 2-16 are per-note channels
    // (the lower zone) with their own range, and channel 1 bends every note
    void setBendSettings (float rangeSemitones, bool mpeOn, float mpeRangeSemitones) noexcept
    {
        bendRange = rangeSemitones;
        mpe = mpeOn;
        mpeRange = mpeRangeSemitones;
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
        int channel = 1;          // 1..16
        float pressure = 0.0f;    // poly aftertouch, 0..1
        bool held = false;        // key is down
        bool sustained = false;   // key released while the sustain pedal was down
        uint32_t order = 0;       // when it started, for stealing
    };

    void handleMidi (const juce::MidiMessage& m)
    {
        const int ch = juce::jlimit (1, 16, m.getChannel());
        if (m.isNoteOn())
            noteOn (m.getNoteNumber(), m.getFloatVelocity(), ch);
        else if (m.isNoteOff())
            noteOff (m.getNoteNumber(), ch);
        else if (m.isPitchWheel())
            channelBend[(size_t) ch] = juce::jlimit (-1.0f, 1.0f, (float) (m.getPitchWheelValue() - 8192) / 8191.0f);
        else if (m.isChannelPressure())
            channelPressure[(size_t) ch] = (float) m.getChannelPressureValue() / 127.0f;
        else if (m.isAftertouch())
        {
            for (auto& s : slots)
                if (s.note == m.getNoteNumber() && s.channel == ch)
                    s.pressure = (float) m.getAfterTouchValue() / 127.0f;
        }
        else if (m.isController() && m.getControllerNumber() == 74)
            channelTimbre[(size_t) ch] = (float) m.getControllerValue() / 127.0f;
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

    void noteOn (int note, float velocity, int channel)
    {
        const int i = findSlotFor (note, channel);
        auto& s = slots[(size_t) i];
        s = { note, channel, 0.0f, true, false, ++orderCounter };
        // A new note starts at its channel's bend at once (no glide from the slot's last note)
        bendNow[(size_t) i] = targetBend (s);
        for (auto& l : layers)
        {
            l.setSlotExpression (i, bendNow[(size_t) i], pressureOf (s), channelTimbre[(size_t) channel]);
            l.startVoice (i, note, velocity, s.order);
        }
    }

    void noteOff (int note, int channel)
    {
        for (int i = 0; i < numNoteSlots; ++i)
        {
            auto& s = slots[(size_t) i];
            if (s.note != note || ! s.held || (mpe && s.channel != channel))
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
    int findSlotFor (int note, int channel) const
    {
        for (int i = 0; i < numNoteSlots; ++i)
            if (slots[(size_t) i].note == note && (! mpe || slots[(size_t) i].channel == channel))
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

    // The bend a slot is heading to, in semitones
    float targetBend (const NoteSlot& s) const noexcept
    {
        if (! mpe)
            return channelBend[(size_t) s.channel] * bendRange;
        return channelBend[1] * bendRange + (s.channel != 1 ? channelBend[(size_t) s.channel] * mpeRange : 0.0f);
    }

    float pressureOf (const NoteSlot& s) const noexcept
    {
        return juce::jmax (s.pressure, channelPressure[(size_t) s.channel], mpe ? 0.0f : channelPressure[1]);
    }

    // Smooths the bends (5 ms) and hands every slot's expression to the layers; returns whether a bend is
    // still moving, in which case the stack renders in 32-sample steps so the pitch never zippers
    bool updateExpression (int numSamples) noexcept
    {
        const float coef = 1.0f - std::exp (-(float) numSamples / (0.005f * (float) rate));
        bool moving = false;
        for (int i = 0; i < numNoteSlots; ++i)
        {
            const auto& s = slots[(size_t) i];
            if (s.note < 0)
                continue;
            auto& now = bendNow[(size_t) i];
            const float target = targetBend (s);
            now += (target - now) * coef;
            if (std::abs (target - now) < 1.0e-4f)
                now = target;
            else
                moving = true;
            for (auto& l : layers)
                l.setSlotExpression (i, now, pressureOf (s), channelTimbre[(size_t) s.channel]);
        }
        return moving;
    }

    bool bendMoving() const noexcept
    {
        for (int i = 0; i < numNoteSlots; ++i)
            if (slots[(size_t) i].note >= 0 && std::abs (targetBend (slots[(size_t) i]) - bendNow[(size_t) i]) >= 1.0e-4f)
                return true;
        return false;
    }

    void renderRange (juce::AudioBuffer<float>& buffer, int start, int length)
    {
        while (length > 0)
        {
            const int n = juce::jmin (length, bendMoving() ? 32 : maxBlock);
            updateExpression (n);
            float* l = buffer.getWritePointer (0, start);
            float* r = buffer.getWritePointer (1, start);
            // The first playing Sub layer renders first, so the Body layers can duck under it in the same block
            int sub = -1;
            if (lowDuckDepth > 0.0f)
                for (int i = 0; i < numLayers && sub < 0; ++i)
                    if (layers[(size_t) i].role() == params::Role::sub && layers[(size_t) i].canPlay())
                        sub = i;
            if (sub >= 0)
            {
                auto& s = layers[(size_t) sub];
                s.setLowDuck (subEnvelope.data(), nullptr, 0.0f);
                s.setClock (clock);
                s.render (l, r, n);
            }
            const float* envelope = sub >= 0 && ! layers[(size_t) sub].isIdle() ? subEnvelope.data() : nullptr;
            for (int i = 0; i < numLayers; ++i)
            {
                if (i == sub)
                    continue;
                auto& layer = layers[(size_t) i];
                const bool body = layer.role() == params::Role::body;
                layer.setLowDuck (nullptr, body ? envelope : nullptr, body ? lowDuckDepth : 0.0f);
                layer.setClock (clock);
                layer.render (l, r, n);
            }
            clock += n;
            start += n;
            length -= n;
        }
    }

    // The first playing Sub-role and Body-role layers are probed; once a sub note that started from silence
    // has sounded for the settle time plus the window, it is measured and the result stored on the Sub layer.
    void updateAlignment()
    {
        int sub = -1, body = -1;
        for (int i = 0; i < numLayers; ++i)
        {
            const auto r = layers[(size_t) i].role();
            if (! layers[(size_t) i].canPlay())
                continue;
            if (r == params::Role::sub && sub < 0) sub = i;
            if (r == params::Role::body && body < 0) body = i;
        }
        const double crossover = layers[0].crossoverLow();
        for (int i = 0; i < numLayers; ++i)
            layers[(size_t) i].setProbing (i == sub || i == body, crossover);
        if (sub < 0 || body < 0)
            return;

        auto& s = layers[(size_t) sub];
        const double sig = s.soundSignature() + 3.1 * layers[(size_t) body].soundSignature();
        if (sig < signature || sig > signature)
        {
            signature = sig;
            s.clearAlignment();
            return;
        }
        int note = -1;
        int64_t started = 0;
        const int decimation = s.getProbe().getDecimation();
        const int64_t settle = (int64_t) (0.04 * rate), span = (int64_t) alignWindow * decimation;
        if (s.pendingMeasurement (note, started) && clock - started >= settle + span)
            s.storeAlignment (note, align::solve (s.getProbe(), layers[(size_t) body].getProbe(), clock / decimation,
                                                  alignWindow, crossover, 440.0 * std::exp2 ((note + s.tuneSemitones() - 69.0) / 12.0), rate));
    }

    static constexpr int alignWindow = 512;   // decimated samples, about 85 ms
    double rate = 48000.0, signature = 0.0;
    int64_t clock = 0;

    std::array<Layer, numLayers> layers;
    std::array<NoteSlot, numNoteSlots> slots {};
    bool sustainPedal = false;
    uint32_t orderCounter = 0;
    // Expression: per MIDI channel (index 1..16) and the slots' smoothed bends
    std::array<float, 17> channelBend {}, channelPressure {}, channelTimbre {};
    std::array<float, numNoteSlots> bendNow {};
    float bendRange = 2.0f, mpeRange = 48.0f;
    bool mpe = false;
    int maxBlock = 512;
    std::vector<float> subEnvelope;
    float lowDuckDepth = 0.0f;
};
}
