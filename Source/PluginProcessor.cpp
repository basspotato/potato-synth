#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "EngineRegistry.h"

namespace
{
    const juce::Identifier layersNode { "Layers" };
    juce::Identifier engineProperty (int layerIndex) { return "L" + juce::String (layerIndex + 1) + "_engine"; }

    // Init patch (brief): Sub, a Dirt body, a JP mid and a Spectral air
    const char* const defaultEngines[ps::params::numLayers] = { "sub", "dirt", "jp", "spectral", "", "", "", "" };
}

PotatoSynthProcessor::PotatoSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PotatoSynth", ps::params::createLayout ([this] (int layer, int slot, float v) { return slotText (layer, slot, v); }))
{
    masterDb = apvts.getRawParameterValue (ps::params::master.getParamID());
    crossoverLowHz = apvts.getRawParameterValue (ps::params::crossoverLow.getParamID());
    monoBassOn = apvts.getRawParameterValue (ps::params::monoBass.getParamID());
    safeOutputOn = apvts.getRawParameterValue (ps::params::safeOutput.getParamID());
    phaseAlign = apvts.getRawParameterValue (ps::params::phaseAlign.getParamID());
    {
        namespace p = ps::params;
        auto raw = [this] (const juce::ParameterID& id) { return apvts.getRawParameterValue (id.getParamID()); };
        arpParams = { raw (p::arpMode), raw (p::arpRate), raw (p::arpDirection), raw (p::arpOctaves), raw (p::arpGate),
                      raw (p::arpSwing), raw (p::arpLatch), raw (p::arpLength), raw (p::seqTrigger) };
        for (int l = 0; l < ps::ModLanes::numLanes; ++l)
            laneParams[(size_t) l] = { raw (p::laneTarget (l)), raw (p::laneDepth (l)), raw (p::laneRate (l)), raw (p::laneLength (l)), raw (p::laneSmooth (l)) };
    }
    for (int k = 0; k < ps::params::numChainKnobs; ++k)
        chainKnobs[(size_t) k] = apvts.getRawParameterValue (ps::params::chainKnob (k).getParamID());
    superKnob = apvts.getRawParameterValue (ps::params::super.getParamID());
    buildButton = apvts.getRawParameterValue (ps::params::build.getParamID());
    dropButton = apvts.getRawParameterValue (ps::params::drop.getParamID());
    holdButton = apvts.getRawParameterValue (ps::params::hold.getParamID());
    cutoffKnob = apvts.getRawParameterValue (ps::params::cutoff.getParamID());
    resoKnob = apvts.getRawParameterValue (ps::params::resonance.getParamID());
    {
        namespace p = ps::params;
        auto raw = [this] (const juce::ParameterID& id) { return apvts.getRawParameterValue (id.getParamID()); };
        expressionParams = { raw (p::bendRange), raw (p::mpe), raw (p::mpeRange), raw (p::modWheelTarget), raw (p::modWheelDepth),
                             raw (p::aftertouchTarget), raw (p::aftertouchDepth) };
    }
    padX = apvts.getRawParameterValue (ps::params::padX.getParamID());
    {
        namespace p = ps::params;
        auto raw = [this] (const juce::ParameterID& id) { return apvts.getRawParameterValue (id.getParamID()); };
        sendParams = { raw (p::reverbAmount), raw (p::reverbSize), raw (p::reverbDamping), raw (p::reverbDuck),
                       raw (p::delayAmount), raw (p::delayTime), raw (p::delayFeedback), raw (p::delayTone),
                       raw (p::delayPingPong), raw (p::delayDuck) };
    }
    padY = apvts.getRawParameterValue (ps::params::padY.getParamID());
    {
        namespace p = ps::params;
        auto raw = [this] (const juce::ParameterID& id) { return apvts.getRawParameterValue (id.getParamID()); };
        masterParams = { raw (p::ottAmount), raw (p::ottTime), raw (p::ottUpward), raw (p::glueAmount), raw (p::glueRelease), raw (p::stackWidth), raw (p::subDuck) };
    }
    for (int i = 0; i < ps::params::numLayers; ++i)
        anchors[(size_t) i] = apvts.getRawParameterValue (ps::params::layerAnchor (i).getParamID());
    for (int i = 0; i < ps::params::numLayers; ++i)
    {
        namespace p = ps::params;
        auto raw = [this] (const juce::ParameterID& id) { return apvts.getRawParameterValue (id.getParamID()); };
        ps::LayerParams lp { raw (p::layerLevel (i)), raw (p::layerPan (i)), raw (p::layerOctave (i)),
                                         raw (p::layerSemi (i)), raw (p::layerFine (i)), raw (p::layerAttack (i)),
                                         raw (p::layerDecay (i)), raw (p::layerSustain (i)), raw (p::layerRelease (i)),
                                         raw (p::layerVoiceMode (i)), raw (p::layerGlide (i)), raw (p::layerRole (i)),
                                         raw (p::crossoverLow), raw (p::crossoverHigh), {},
                                         raw (p::layerFilterType (i)), raw (p::layerCutoff (i)), raw (p::layerResonance (i)),
                                         raw (p::layerFilterEnv (i)), raw (p::layerKeyTrack (i)), raw (p::layerFilterAttack (i)),
                                         raw (p::layerFilterDecay (i)), raw (p::layerFilterSustain (i)), raw (p::layerFilterRelease (i)),
                                         raw (p::layerDrive (i)), raw (p::layerChorus (i)), raw (p::layerChorusRate (i)),
                                         raw (p::layerChorusDepth (i)) };
        for (int k = 0; k < p::numInsertSlots; ++k)
        {
            auto& ip = lp.inserts[(size_t) k];
            ip.type = raw (p::layerInsertType (i, k));
            ip.amount = raw (p::layerInsertAmount (i, k));
            for (int c = 0; c < 3; ++c)
                ip.controls[(size_t) c] = raw (p::layerInsertControl (i, k, c));
        }
        lp.insertSwap = raw (p::layerInsertSwap (i));
        stack.layer (i).setParameters (lp);
        for (int k = 0; k < p::numEngineSlots; ++k)
            stack.layer (i).setSlotParameter (k, raw (p::layerSlot (i, k)));
    }

    auto layers = apvts.state.getOrCreateChildWithName (layersNode, nullptr);
    for (int i = 0; i < ps::params::numLayers; ++i)
        layers.setProperty (engineProperty (i), defaultEngines[i], nullptr);
    applyLayerEnginesFromState();
    // A fresh instance starts each default engine from its own slot defaults (the generic host default is 50 %)
    for (int i = 0; i < ps::params::numLayers; ++i)
        if (const auto* info = engineInfos[(size_t) i].load())
            for (int k = 0; k < (int) info->slots.size(); ++k)
                apvts.getParameter (ps::params::layerSlot (i, k).getParamID())->setValueNotifyingHost (info->slots[(size_t) k].defaultValue);

    startTimerHz (10);
}

void PotatoSynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    stack.prepare (sampleRate, samplesPerBlock);
    noteGenerator.prepare (sampleRate);
    modLanes.prepare (sampleRate);
    laneOffsets = {};
    generatedMidi.ensureSize (8192);
    heldMidi.ensureSize (8192);
    sweep.prepare (sampleRate);
    modWheel = 0.0f;
    channelPressure = {};
    notePressure = {};
    realPedal = pedalSent = false;
    bus.prepare (sampleRate, crossoverLowHz->load(), monoBassOn->load() > 0.5f);
    clipper.prepare (sampleRate, samplesPerBlock);
    limiter.prepare (sampleRate);
    mixHealth.prepare (sampleRate);
    lowFocus.prepare (sampleRate);
    bassHarmonics.prepare (sampleRate);
    pressure.prepare (sampleRate);
    masterDynamics.prepareMaster (sampleRate, samplesPerBlock);
    busWidth.prepare (sampleRate);
    tilt.prepare (sampleRate);
    deHarsh.prepare (sampleRate);
    space.prepare (sampleRate, samplesPerBlock);
    delay.prepare (sampleRate);
    for (auto* r : { &reverbReturn, &delayReturn })
        for (auto& ch : *r)
            ch.assign ((size_t) juce::jmax (samplesPerBlock, 4096), 0.0f);
    reverbSend = bloomSend = delaySend = 0.0f;
    reverbRunning = false;
    buildStage.prepare (sampleRate);
    performance = {};
    padXSmoothed = padX->load();
    padYSmoothed = padY->load();
    superSmoothed = superKnob->load();
    driftTime = 0.0;
    // Every layer and role starts at its own point of the cycle (golden-angle offsets)
    for (int l = 0; l < ps::params::numLayers; ++l)
        for (int r = 0; r < ps::numSlotRoles; ++r)
            motionPhase[(size_t) l][(size_t) r] = std::fmod (0.381966f * (float) (l * ps::numSlotRoles + r), 1.0f);
    pump.prepare (sampleRate);
    knobStagesIn.prepare (sampleRate);
    knobStagesOut.prepare (sampleRate);
    knobStagesActive = false;
    loudnessTrim.prepare (sampleRate, samplesPerBlock);
    trimLevel.reset();
    for (int k = 0; k < ps::params::numChainKnobs; ++k)
        knobPosition[(size_t) k] = chainKnobs[(size_t) k]->load();
    setLatencySamples (juce::roundToInt (stack.layer (0).driveLatencySamples())
                       + clipper.getLatencySamples() + limiter.getLatencySamples());   // constant for a given sample rate
    stackGain.reset (sampleRate, 0.05);
    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (masterDb->load()));
}

void PotatoSynthProcessor::setLayerEngine (int layerIndex, const juce::String& engineId)
{
    apvts.state.getOrCreateChildWithName (layersNode, nullptr).setProperty (engineProperty (layerIndex), engineId, nullptr);
    auto engine = ps::engines::create (engineId);

    // A newly picked engine starts from its own defaults (loading state keeps the saved slot values instead)
    const auto* info = engine != nullptr ? &engine->info() : nullptr;
    for (int k = 0; k < ps::params::numEngineSlots; ++k)
    {
        auto* p = apvts.getParameter (ps::params::layerSlot (layerIndex, k).getParamID());
        const float value = info != nullptr && k < (int) info->slots.size() ? info->slots[(size_t) k].defaultValue : 0.5f;
        p->beginChangeGesture();
        p->setValueNotifyingHost (value);
        p->endChangeGesture();
    }

    engineInfos[(size_t) layerIndex] = info;
    stack.layer (layerIndex).requestEngine (std::move (engine), engineId);
    updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));   // slot value text follows the engine
}

void PotatoSynthProcessor::loadPreset (const ps::presets::Preset& preset)
{
    auto setParam = [] (juce::RangedAudioParameter& p, float normalised)
    {
        p.beginChangeGesture();
        p.setValueNotifyingHost (normalised);
        p.endChangeGesture();
    };
    for (auto* param : getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (param))
            setParam (*p, p->getDefaultValue());
    for (int i = 0; i < ps::params::numLayers; ++i)
        setLayerEngine (i, preset.engines[(size_t) i]);
    for (const auto& [id, value] : preset.values)
        if (auto* p = apvts.getParameter (id))
            setParam (*p, p->convertTo0to1 (value));
        else
            jassertfalse;   // a preset names a parameter that doesn't exist

    const ps::NoteGenerator defaultGenerator;
    for (int i = 0; i < ps::NoteGenerator::maxSteps; ++i)
        noteGenerator.setStep (i, i < (int) preset.steps.size() ? preset.steps[(size_t) i] : defaultGenerator.getStep (i));
    const ps::ModLanes defaultLanes;
    for (int l = 0; l < ps::ModLanes::numLanes; ++l)
        for (int s = 0; s < ps::ModLanes::maxSteps; ++s)
            modLanes.setValue (l, s, defaultLanes.getValue (l, s));
    apvts.state.setProperty ("presetName", preset.name, nullptr);
}

juce::String PotatoSynthProcessor::slotText (int layer, int slot, float value) const
{
    const auto* info = engineInfos[(size_t) layer].load();
    if (info == nullptr || slot >= (int) info->slots.size())
        return "-";
    const auto& s = info->slots[(size_t) slot];
    return s.toText ? s.toText (value) : juce::String (juce::roundToInt (value * 100.0f)) + " %";
}

void PotatoSynthProcessor::applyLayerEnginesFromState()
{
    const auto layers = apvts.state.getChildWithName (layersNode);
    for (int i = 0; i < ps::params::numLayers; ++i)
    {
        const auto id = layers.getProperty (engineProperty (i)).toString();
        if (id != stack.layer (i).getEngineId())
        {
            auto engine = ps::engines::create (id);
            engineInfos[(size_t) i] = engine != nullptr ? &engine->info() : nullptr;
            stack.layer (i).requestEngine (std::move (engine), id);
        }
    }
    updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
}

void PotatoSynthProcessor::trackExpression (const juce::MidiBuffer& midi) noexcept
{
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isControllerOfType (1))
            modWheel = (float) m.getControllerValue() / 127.0f;
        else if (m.isChannelPressure())
            channelPressure[(size_t) juce::jlimit (1, 16, m.getChannel())] = (float) m.getChannelPressureValue() / 127.0f;
        else if (m.isAftertouch())
            notePressure[(size_t) m.getNoteNumber()] = (float) m.getAfterTouchValue() / 127.0f;
        else if (m.isNoteOff())
            notePressure[(size_t) m.getNoteNumber()] = 0.0f;
        else if (m.isResetAllControllers())
        {
            modWheel = 0.0f;
            channelPressure = {};
            notePressure = {};
        }
    }
}

const juce::MidiBuffer& PotatoSynthProcessor::applyHold (const juce::MidiBuffer& midi)
{
    if (holdButton->load() <= 0.5f && ! pedalSent)
    {
        // Nothing held: the MIDI passes untouched, and the real pedal is tracked for when HOLD goes on
        for (const auto meta : midi)
            if (const auto m = meta.getMessage(); m.isSustainPedalOn() || m.isSustainPedalOff())
                realPedal = m.isSustainPedalOn();
        pedalSent = realPedal;
        return midi;
    }

    heldMidi.clear();
    auto sendPedal = [this] (int at)
    {
        const bool down = realPedal || holdButton->load() > 0.5f;
        if (down != pedalSent)
            heldMidi.addEvent (juce::MidiMessage::controllerEvent (1, 64, down ? 127 : 0), at);
        pedalSent = down;
    };
    sendPedal (0);   // HOLD switched on or off since the last block
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isSustainPedalOn() || m.isSustainPedalOff())
        {
            realPedal = m.isSustainPedalOn();
            sendPedal (meta.samplePosition);
        }
        else
        {
            heldMidi.addEvent (m, meta.samplePosition);
        }
    }
    return heldMidi;
}

bool PotatoSynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PotatoSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int n = buffer.getNumSamples();
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    // The host's tempo, beat and bar position (120 BPM 4/4, free-running, without a host transport)
    double bpm = 120.0, ppq = -1.0, barStart = 0.0, barLength = 4.0;
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
        {
            if (const auto b = position->getBpm())
                bpm = *b;
            if (const auto p = position->getPpqPosition(); p && position->getIsPlaying())
                ppq = *p;
            if (const auto bar = position->getPpqPositionOfLastBarStart())
                barStart = *bar;
            if (const auto sig = position->getTimeSignature(); sig && sig->denominator > 0)
                barLength = 4.0 * sig->numerator / sig->denominator;
        }

    const auto& played = applyHold (midi);
    updatePerformance (n, bpm, ppq, barStart, barLength);
    {
        std::array<ps::ModLanes::Settings, ps::ModLanes::numLanes> lanes {};
        for (int l = 0; l < ps::ModLanes::numLanes; ++l)
        {
            const auto& lp = laneParams[(size_t) l];
            lanes[(size_t) l] = { juce::roundToInt (lp.target->load()), lp.depth->load() * 0.01f,
                                  ps::params::delayDivisionBeats[juce::jlimit (0, ps::params::delayDivisionNames.size() - 1, juce::roundToInt (lp.rate->load()))],
                                  juce::roundToInt (lp.length->load()), lp.smooth->load() * 0.01f };
        }
        laneOffsets = modLanes.process (played, n, bpm, ppq, lanes);
    }
    {
        // The mod wheel and aftertouch (the strongest of channel and poly pressure) add to their targets, as a lane does
        trackExpression (played);
        float touch = 0.0f;
        for (auto v : channelPressure) touch = juce::jmax (touch, v);
        for (auto v : notePressure) touch = juce::jmax (touch, v);
        const auto& e = expressionParams;
        auto add = [this] (float target, float depthPercent, float value)
        {
            const int t = juce::roundToInt (target);
            if (t > 0 && t <= ps::ModLanes::numTargets)
                laneOffsets[(size_t) (t - 1)] += depthPercent * 0.01f * value;
        };
        add (e.wheelTarget->load(), e.wheelDepth->load(), modWheel);
        add (e.touchTarget->load(), e.touchDepth->load(), touch);
        stack.setBendSettings (e.bendRange->load(), e.mpe->load() > 0.5f, e.mpeRange->load());
    }
    const auto offsets = updateChainKnobs (n);
    stack.setAlignStrength (juce::jlimit (0.0f, 1.0f, phaseAlign->load() * 0.01f));
    {
        ps::NoteGenerator::Settings s;
        s.mode = (ps::NoteGenerator::Mode) juce::jlimit (0, 2, juce::roundToInt (arpParams.mode->load()));
        s.direction = (ps::NoteGenerator::Direction) juce::jlimit (0, 5, juce::roundToInt (arpParams.direction->load()));
        s.stepBeats = ps::params::delayDivisionBeats[juce::jlimit (0, ps::params::delayDivisionNames.size() - 1, juce::roundToInt (arpParams.rate->load()))];
        s.octaves = juce::roundToInt (arpParams.octaves->load());
        s.length = juce::roundToInt (arpParams.length->load());
        s.gate = arpParams.gate->load() * 0.01f;
        s.swing = arpParams.swing->load() * 0.01f;
        s.latch = arpParams.latch->load() > 0.5f;
        s.hostTrigger = arpParams.trigger->load() > 0.5f;
        noteGenerator.process (played, generatedMidi, n, bpm, ppq, s);
    }
    stack.setLowDuck (masterParams.subDuck->load() * 0.01f);
    stack.process (buffer, generatedMidi);

    // Stack sum with sqrt-weights: the layers are scaled by 1 / sqrt(n), n the number of layers counted by
    // their level (a layer at 0 dB counts 1, a fading one less), so adding or fading in a layer keeps the
    // stack's power. A single quieter layer is never boosted.
    float layerCount = 0.0f;
    for (int i = 0; i < ps::params::numLayers; ++i)
        if (stack.layer (i).engine() != nullptr)
            layerCount += juce::square (juce::jmin (1.0f, stack.layer (i).patchGain()));
    stackGain.setTargetValue (1.0f / std::sqrt (juce::jmax (1.0f, layerCount)));
    stackGain.applyGain (buffer, n);
    bus.process (left, right, n, crossoverLowHz->load() * std::exp2 (offsets.monoBassOctaves), monoBassOn->load() > 0.5f);
    // CUTOFF and RESO: before the sends, so the reverb and delay hear the filtered stack, as on a hardware synth
    sweep.setPosition (cutoffKnob->load() * 0.01f + laneOffsets[ps::ModLanes::cutoffTarget], resoKnob->load() * 0.01f);
    sweep.process (left, right, n);

    // The patch's master chain: OTT, glue, width. Part of the patch's sound, so it sits before the sends and
    // outside the knob stages the loudness trim measures.
    {
        const auto& m = masterParams;
        masterDynamics.processMaster (left, right, n, m.ott->load() * 0.01f, m.ottTime->load() * 0.01f, m.ottUpward->load() * 0.01f,
                                      m.glue->load(), m.glueRelease->load() * 0.001f);
        busWidth.process (left, right, n, m.width->load() * 0.01f);
    }

    // The shared sends, fed from the dry stack; the patch's own send levels go in here, before the knob stages
    // are measured, since they are part of the patch's loudness
    const bool bloomAfterKnobs = n <= (int) reverbReturn[0].size();
    renderSends (left, right, n, offsets, bpm);

    // The bus stages the knobs drive, measured in and out for the loudness trim (only while one is working)
    knobStagesIn.process (left, right, n);
    bool active = lowFocus.process (left, right, n, offsets.lowFocusDb);
    active = bassHarmonics.process (left, right, n, offsets.bassHarmonics) || active;
    active = buildStage.process (left, right, n, performance.build, 0.5f * knobStagesIn.getPower(), bpm) || active;
    active = tilt.process (left, right, n, offsets.tiltDbPerOct) || active;
    // BLOOM's extra reverb send is one of the knob stages, so the loudness trim compensates it
    if (bloomAfterKnobs && reverbRunning)
    {
        const float to = juce::jmax (0.0f, offsets.spaceWet);
        addRamped (left, reverbReturn[0].data(), n, bloomSend, to);
        addRamped (right, reverbReturn[1].data(), n, bloomSend, to);
        active = active || bloomSend > 0.0f || to > 0.0f;
        bloomSend = to;
    }
    active = deHarsh.process (left, right, n, offsets.deHarsh) || active;
    pressure.setTempoLock (offsets.tempoLock, 60.0 / juce::jlimit (20.0, 400.0, bpm));
    active = pressure.process (left, right, n, offsets.pressure) || active;
    knobStagesOut.process (left, right, n);
    knobStagesIn.update();
    knobStagesOut.update();
    if (! active && knobStagesActive)
    {
        knobStagesIn.reset();
        knobStagesOut.reset();
    }
    knobStagesActive = active;
    // The pump's power loss is known exactly, so it sits outside the measured stages
    pump.process (left, right, n, offsets.pumpDb, offsets.pumpHardness, offsets.pumpTau, bpm, ppq);

    // Harmony Engine, invariant 1: hold the loudness the patch has without the knobs
    std::array<float, ps::params::numLayers> power {}, patchGain {}, knobGain {};
    for (int i = 0; i < ps::params::numLayers; ++i)
    {
        const auto& layer = stack.layer (i);
        power[(size_t) i] = layer.measuredPower();
        patchGain[(size_t) i] = layer.patchGain();
        knobGain[(size_t) i] = layer.knobGain() * std::sqrt (layer.carvePowerRatio() * layer.drivePowerRatio());
    }
    // Playing: this block within 10 dB of the held level (the loudness meter's relative gate); an onset: 6 dB
    // over the 400 ms average
    const float blockPower = knobStagesIn.getBlockPower();
    trimLevel.update (blockPower, 1.0f - std::exp (-(float) n / (0.4f * (float) getSampleRate())),
                      1.0f - std::exp (-(float) n / (0.1f * (float) getSampleRate())));
    const bool playing = blockPower > 0.1f * trimLevel.power && blockPower > 1.0e-10f;
    const bool onset = blockPower > 4.0f * knobStagesIn.getPower();
    loudnessTrim.update (power.data(), patchGain.data(), knobGain.data(), ps::params::numLayers,
                         (active ? ps::harmony::stageRatio (knobStagesIn, knobStagesOut) : 1.0) * clipper.knobStageRatio(), n,
                         playing, onset);
    // The pump's makeup is exact and immediate (no meter to wait for), so it rides on top of the smoothed trim.
    // Both go in inside the clipper, after its knob stages.
    const float trim = juce::jlimit (0.25f, 4.0f, loudnessTrim.getGain() / std::sqrt (pump.powerRatio())
                                                      * juce::Decibels::decibelsToGain (offsets.loudnessLiftDb));

    masterGain.setTargetValue (juce::Decibels::decibelsToGain (masterDb->load()));
    masterGain.applyGain (buffer, buffer.getNumSamples());

    // Safe output: a 4x oversampled soft clipper shapes overs musically, then a look-ahead limiter
    // guarantees the -1 dBTP ceiling the clipper's decimation filter can overshoot. The loudness trim goes in
    // just before the clipper's curve.
    const bool safe = safeOutputOn->load() > 0.5f;
    clipper.process (buffer, safe, pressure.getAmount(), offsets.exciter, trim);
    limiter.process (buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples(), safe);
    mixHealth.setCrossover (crossoverLowHz->load());
    mixHealth.process (buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples());
}

void PotatoSynthProcessor::addRamped (float* out, const float* in, int numSamples, float from, float to) noexcept
{
    if (from <= 0.0f && to <= 0.0f)
        return;
    const float step = (to - from) / (float) numSamples;
    for (int i = 0; i < numSamples; ++i)
        out[i] += (from + step * (float) (i + 1)) * in[i];
}

void PotatoSynthProcessor::renderSends (float* left, float* right, int n, const ps::chain::Offsets& offsets, double bpm)
{
    const auto& sp = sendParams;
    const float reverbTo = juce::square (sp.reverbAmount->load() * 0.01f);   // 100 % returns at unity
    const float bloomTo = juce::jmax (0.0f, offsets.spaceWet);
    const float delayTo = 0.5f * juce::square (sp.delayAmount->load() * 0.01f);
    // BLOOM lengthens the patch's space (never shortens it) and adds to its ducking
    const float rt60 = juce::jmax (sp.reverbSize->load(), offsets.spaceRt60);
    const float damping = juce::jlimit (0.05f, 1.0f, 1.0f - sp.reverbDamping->load() * 0.01f);
    const float reverbDuck = juce::jlimit (0.0f, 1.0f, sp.reverbDuck->load() * 0.01f + offsets.spaceDuck);
    const int division = juce::jlimit (0, (int) std::size (ps::params::delayDivisionBeats) - 1, juce::roundToInt (sp.delayTime->load()));
    const float seconds = (float) (ps::params::delayDivisionBeats[division] * 60.0 / juce::jlimit (20.0, 400.0, bpm));
    const bool reverbOn = reverbTo > 0.0f || reverbSend > 0.0f || bloomTo > 0.0f || bloomSend > 0.0f;
    const bool delayOn = delayTo > 0.0f || delaySend > 0.0f;

    // Blocks longer than prepared are split; then BLOOM's send has no whole-block return to read later and
    // goes in here with the patch's
    const int capacity = (int) reverbReturn[0].size();
    const bool bloomHere = n > capacity;
    for (int start = 0; start < n; start += capacity)
    {
        const int len = std::min (capacity, n - start);
        const float t0 = (float) start / (float) n, t1 = (float) (start + len) / (float) n;
        auto lerp = [] (float a, float b, float t) { return a + (b - a) * t; };
        float* l = left + start;
        float* r = right + start;
        // Both sends read the dry signal: render both returns before adding either
        reverbRunning = space.render (l, r, reverbReturn[0].data(), reverbReturn[1].data(), len, reverbOn, rt60, damping, reverbDuck);
        const bool delayRunning = delay.render (l, r, delayReturn[0].data(), delayReturn[1].data(), len, delayOn, seconds,
                                                sp.delayFeedback->load() * 0.01f, sp.delayTone->load(), sp.delayPingPong->load() > 0.5f,
                                                sp.delayDuck->load() * 0.01f);
        if (reverbRunning)
            for (int ch = 0; ch < 2; ++ch)
            {
                float* out = ch == 0 ? l : r;
                addRamped (out, reverbReturn[(size_t) ch].data(), len, lerp (reverbSend, reverbTo, t0), lerp (reverbSend, reverbTo, t1));
                if (bloomHere)
                    addRamped (out, reverbReturn[(size_t) ch].data(), len, lerp (bloomSend, bloomTo, t0), lerp (bloomSend, bloomTo, t1));
            }
        if (delayRunning)
            for (int ch = 0; ch < 2; ++ch)
                addRamped (ch == 0 ? l : r, delayReturn[(size_t) ch].data(), len, lerp (delaySend, delayTo, t0), lerp (delaySend, delayTo, t1));
    }
    reverbSend = reverbTo;
    delaySend = delayTo;
    if (bloomHere)
        bloomSend = bloomTo;
}

void PotatoSynthProcessor::updatePerformance (int numSamples, double bpm, double ppq, double barStart, double barLength)
{
    auto& p = performance;
    const double beats = numSamples * juce::jlimit (20.0, 400.0, bpm) / 60.0 / getSampleRate();

    // BUILD rises over 8 beats while held and falls over one when let go
    const bool building = buildButton->load() > 0.5f;
    p.build = juce::jlimit (0.0f, 1.0f, p.build + (float) (building ? beats / 8.0 : -beats));

    // DROP waits for the next bar line (the block holding it) while the transport runs
    const bool held = dropButton->load() > 0.5f;
    if (! held)
        p.dropArmed = p.dropOn = false;
    else if (! p.dropOn)
    {
        p.dropArmed = true;
        bool onBar = ppq < 0.0;
        if (! onBar && barLength > 0.0)
        {
            const double sinceBar = std::fmod (ppq - barStart, barLength);
            const double toNext = sinceBar < 1.0e-9 ? 0.0 : barLength - sinceBar;
            onBar = toNext < beats;
        }
        if (onBar)
        {
            p.dropOn = true;
            p.dropArmed = false;
            space.killTail();
        }
    }
    p.drop = p.dropOn ? 1.0f : 0.0f;
}

// Chain knob positions are smoothed (30 ms) at block rate, SUPER is folded in, and the offsets go to the layers
ps::chain::Offsets PotatoSynthProcessor::updateChainKnobs (int numSamples)
{
    const float coef = 1.0f - std::exp (-(float) numSamples / (0.03f * (float) getSampleRate()));
    // The mod lanes move the knobs, SUPER and the pad through the same glide as a hand on the knob
    superSmoothed += (juce::jlimit (0.0f, 1.0f, superKnob->load() + laneOffsets[ps::ModLanes::superTarget]) - superSmoothed) * coef;
    const float super = superSmoothed;
    std::array<float, ps::chain::numKnobs> u {};
    for (int k = 0; k < ps::chain::numKnobs; ++k)
    {
        auto& p = knobPosition[(size_t) k];
        p += (juce::jlimit (0.0f, 1.0f, chainKnobs[(size_t) k]->load() + laneOffsets[(size_t) k]) - p) * coef;
        u[(size_t) k] = p + ps::chain::superContribution ((ps::chain::Knob) k, super);
    }
    // BUILD widens, blooms and opens the top; DROP hits with mass and pressure and snaps GROOVE to full.
    // The drop is smoothed like the knobs, so it lands within 30 ms and never clicks.
    dropSmoothed += (performance.drop - dropSmoothed) * coef;
    const float b = performance.build, d = dropSmoothed;
    u[ps::chain::scale] += 0.5f * b;
    u[ps::chain::bloom] += 0.5f * b;
    u[ps::chain::shine] += 0.6f * b;
    u[ps::chain::mass] += 0.4f * d;
    u[ps::chain::pressure] += 0.4f * d;
    u[ps::chain::groove] += d * (1.0f - u[ps::chain::groove]);
    for (size_t k = 0; k < u.size(); ++k)
    {
        u[k] = juce::jlimit (0.0f, 1.0f, u[k]);
        effectiveKnob[k].store (u[k]);
    }
    std::array<ps::params::Role, ps::params::numLayers> roles {};
    for (int i = 0; i < ps::params::numLayers; ++i)
        roles[(size_t) i] = stack.layer (i).role();
    auto offsets = ps::chain::compute (u, roles);
    // The XY pad: layer emphasis at constant power (StackMorph.h), glided like the knobs
    padXSmoothed += (juce::jlimit (0.0f, 1.0f, padX->load() + laneOffsets[ps::ModLanes::padXTarget]) - padXSmoothed) * coef;
    padYSmoothed += (juce::jlimit (0.0f, 1.0f, padY->load() + laneOffsets[ps::ModLanes::padYTarget]) - padYSmoothed) * coef;
    effectivePad[0].store (padXSmoothed);
    effectivePad[1].store (padYSmoothed);
    {
        std::array<bool, ps::params::numLayers> free {};
        for (int i = 0; i < ps::params::numLayers; ++i)
            free[(size_t) i] = stack.layer (i).engine() != nullptr && anchors[(size_t) i]->load() < 0.5f;
        const auto padDb = ps::morph::mixGainsDb (padXSmoothed, padYSmoothed, free);
        for (int i = 0; i < ps::params::numLayers; ++i)
            offsets.levelDb[(size_t) i] += padDb[(size_t) i];
    }

    // DROP's -1 octave: the Sub and Body layers bring in their sub-octave
    for (int i = 0; i < ps::params::numLayers; ++i)
        if (roles[(size_t) i] == ps::params::Role::sub || roles[(size_t) i] == ps::params::Role::body)
            offsets.slot[(size_t) i][(size_t) ps::SlotRole::subOctave] += 0.5f * d;

    // BLOOM. Drift: one slow wobble for the whole stack, never random (user, 2026-10-22): two sines at 0.17 Hz and
    // 0.17 x golden ratio (unit RMS, so the knob's cents are its standard deviation). It restarts from zero
    // whenever the stack falls silent, so every phrase moves the same way.
    // Motion: each slot role swings on a slow sine, the rates spaced by powers of the golden ratio so the
    // layers' movements never line up into an obvious loop.
    const float dt = (float) numSamples / (float) getSampleRate();
    if (stack.activeNoteCount() == 0)
        driftTime = 0.0;
    else
        driftTime += dt;
    const float driftWobble = (float) (std::sin (juce::MathConstants<double>::twoPi * 0.17 * driftTime)
                                       + std::sin (juce::MathConstants<double>::twoPi * 0.17 * 1.618034 * driftTime));
    for (int l = 0; l < ps::params::numLayers; ++l)
    {
        for (int r = 0; r < ps::numSlotRoles; ++r)
        {
            const float depth = offsets.slotMotion[(size_t) l][(size_t) r];
            auto& ph = motionPhase[(size_t) l][(size_t) r];
            ph = std::fmod (ph + dt * 0.07f * std::pow (1.618034f, (float) ((l + r) % 5)), 1.0f);
            if (depth != 0.0f)
                offsets.slot[(size_t) l][(size_t) r] += depth * std::sin (juce::MathConstants<float>::twoPi * ph);
        }
    }

    // Band ownership, from the energies the layers measured last block
    std::array<std::array<float, ps::harmony::CarveBank::numBands>, ps::params::numLayers> energy {};
    std::array<bool, ps::params::numLayers> sounding {};
    for (int i = 0; i < ps::params::numLayers; ++i)
    {
        energy[(size_t) i] = stack.layer (i).carveEnergy();
        sounding[(size_t) i] = stack.layer (i).engine() != nullptr;
    }
    const bool carving = offsets.carveDepthDb > 1.0e-3f;
    const auto carve = ps::harmony::solveOwnership (energy, roles, sounding, offsets.carveTemperature, offsets.carveDepthDb);
    mixHealth.updateMasking (energy, sounding, ps::params::numLayers);
    const bool analyse = carving || mixHealth.isActive();   // Mix Health's masking check reads the same band energies
    for (int i = 0; i < ps::params::numLayers; ++i)
    {
        stack.layer (i).setChainOffsets (offsets.levelDb[(size_t) i], offsets.slot[(size_t) i]);
        stack.layer (i).setBeatCeiling (offsets.beatCeiling);
        stack.layer (i).setOnsetDuck (offsets.onsetDuckDb[(size_t) i], offsets.onsetDuckMs);
        stack.layer (i).setCarveGains (carve[(size_t) i], analyse);
        stack.layer (i).setChainDrift (offsets.driftCents[(size_t) i] * driftWobble);
        stack.layer (i).setChainInserts (offsets.layerDrive[(size_t) i], offsets.layerChorus[(size_t) i]);
    }
    return offsets;
}

// Parameters missing from older state (added in a later version) load at their defaults, not at whatever
// value this instance happened to hold.
void PotatoSynthProcessor::migrateState (juce::ValueTree& state) const
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! state.getChildWithProperty ("id", rp->getParameterID()).isValid())
                state.appendChild (juce::ValueTree ("PARAM", { { "id", rp->getParameterID() },
                                                               { "value", rp->convertFrom0to1 (rp->getDefaultValue()) } }),
                                   nullptr);
    state.setProperty ("stateVersion", ps::params::stateVersion, nullptr);
}

juce::AudioProcessorEditor* PotatoSynthProcessor::createEditor()
{
    return new PotatoSynthEditor (*this);
}

void PotatoSynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", ps::params::stateVersion, nullptr);
    juce::ValueTree steps ("Steps");
    for (int i = 0; i < ps::NoteGenerator::maxSteps; ++i)
    {
        const auto s = noteGenerator.getStep (i);
        steps.appendChild (juce::ValueTree ("STEP", { { "on", s.on }, { "tie", s.tie }, { "offset", s.offset }, { "ratchet", s.ratchet },
                                                      { "velocity", s.velocity }, { "probability", s.probability } }), nullptr);
    }
    state.removeChild (state.getChildWithName ("Steps"), nullptr);
    state.appendChild (steps, nullptr);
    juce::ValueTree lanes ("Lanes");
    for (int l = 0; l < ps::ModLanes::numLanes; ++l)
    {
        juce::StringArray values;
        for (int s = 0; s < ps::ModLanes::maxSteps; ++s)
            values.add (juce::String (modLanes.getValue (l, s), 4));
        lanes.appendChild (juce::ValueTree ("LANE", { { "values", values.joinIntoString (",") } }), nullptr);
    }
    state.removeChild (state.getChildWithName ("Lanes"), nullptr);
    state.appendChild (lanes, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PotatoSynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    migrateState (state);
    // The step pattern (state version 9; older saves get the default pattern)
    const auto steps = state.getChildWithName ("Steps");
    for (int i = 0; i < ps::NoteGenerator::maxSteps; ++i)
    {
        const auto st = steps.getChild (i);
        ps::NoteGenerator::Step s;
        if (st.isValid())
            s = { (bool) st.getProperty ("on", true), (bool) st.getProperty ("tie", false), (int) st.getProperty ("offset", 0),
                  (int) st.getProperty ("ratchet", 1), (float) st.getProperty ("velocity", 1.0f), (float) st.getProperty ("probability", 1.0f) };
        noteGenerator.setStep (i, s);
    }
    state.removeChild (steps, nullptr);
    // The mod lanes' steps (older saves keep the default shapes)
    const auto lanes = state.getChildWithName ("Lanes");
    const ps::ModLanes defaultLanes;
    for (int l = 0; l < ps::ModLanes::numLanes; ++l)
    {
        const auto values = juce::StringArray::fromTokens (lanes.getChild (l).getProperty ("values").toString(), ",", {});
        for (int s = 0; s < ps::ModLanes::maxSteps; ++s)
            modLanes.setValue (l, s, s < values.size() ? values[s].getFloatValue() : defaultLanes.getValue (l, s));
    }
    state.removeChild (lanes, nullptr);
    apvts.replaceState (state);
    applyLayerEnginesFromState();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PotatoSynthProcessor();
}
