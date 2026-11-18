#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

SNSynthProcessor::SNSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "SNSynthState", Params::createLayout())
{
    volumeParam = parameters.getRawParameterValue (ParamIDs::volume);

    for (int i = 0; i < 3; ++i)
    {
        auto get = [&] (const char* c) { return parameters.getRawParameterValue (ParamIDs::slot (i, c)); };
        slotParams[(size_t) i] = { get (ParamIDs::slotType), get (ParamIDs::slotCutoff), get (ParamIDs::slotResonance),
                                   get (ParamIDs::slotDrive), get (ParamIDs::slotEnv), get (ParamIDs::slotCharacter) };
    }
    parameters.state.setProperty (StateIDs::slotOrder, "0,1,2", nullptr);

    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new SynthVoice (voiceSettings, i));

    synth.addSound (new SynthSound());
}

float SNSynthProcessor::volumeDbToGain (float db)
{
    // Bottom of the range means silence; a tiny floor keeps the
    // multiplicative smoother valid.
    return db <= -60.0f ? 1.0e-5f : juce::Decibels::decibelsToGain (db);
}

void SNSynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    lfoBuffer.assign ((size_t) juce::jmax (samplesPerBlock, 512), 0.0f);
    voiceSettings.lastNote = -1.0;
    updateVoiceSettings();
    synth.setCurrentPlaybackSampleRate (sampleRate);
    keyboardState.reset();

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (samplesPerBlock, 1), 1 };
    for (auto* bank : { &eqLow, &eqMid, &eqHigh })
        for (auto& f : *bank) { f.prepare (spec); f.reset(); }
    lastEq[0] = lastEq[1] = lastEq[2] = 1000.0f; // force coefficient update
    updateEq();

    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (volumeDbToGain (volumeParam->load()));
}

bool SNSynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void SNSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Merges notes from the on-screen / computer keyboard into the incoming MIDI.
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    updateVoiceSettings();
    renderLfo (buffer.getNumSamples());
    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

    // 3-band EQ
    updateEq();
    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            d[i] = eqHigh[(size_t) ch].processSample (eqMid[(size_t) ch].processSample (eqLow[(size_t) ch].processSample (d[i])));
    }

    outputGain.setTargetValue (volumeDbToGain (volumeParam->load()));
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outputGain.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }
}

void SNSynthProcessor::updateVoiceSettings()
{
    auto& v = voiceSettings;
    v.sawLevel    = p (ParamIDs::sawLevel)->load()   * 0.01f;
    v.pulseLevel  = p (ParamIDs::pulseLevel)->load() * 0.01f;
    v.pulseWidth  = p (ParamIDs::pulseWidth)->load() * 0.01f;
    v.subLevel    = p (ParamIDs::subLevel)->load()   * 0.01f;

    v.fenvA = p (ParamIDs::fenvAttack)->load();
    v.fenvD = p (ParamIDs::fenvDecay)->load();
    v.fenvS = p (ParamIDs::fenvSustain)->load() * 0.01f;
    v.fenvR = p (ParamIDs::fenvRelease)->load();

    v.aenvA = p (ParamIDs::aenvAttack)->load();
    v.aenvD = p (ParamIDs::aenvDecay)->load();
    v.aenvS = p (ParamIDs::aenvSustain)->load() * 0.01f;
    v.aenvR = p (ParamIDs::aenvRelease)->load();

    v.lfoToCutoff  = p (ParamIDs::lfoCutoff)->load() * 0.01f;
    v.drift        = p (ParamIDs::drift)->load() * 0.01f;
    v.glideSeconds = p (ParamIDs::glide)->load();

    for (size_t i = 0; i < 3; ++i)
    {
        auto& sp = slotParams[i];
        auto& slot = v.slots[i];
        slot.type      = (int) sp.type->load();
        slot.cutoffHz  = sp.cutoff->load();
        slot.resonance = sp.res->load() * 0.01f;
        slot.drive     = sp.drive->load() * 0.01f;
        slot.envAmount = sp.env->load() * 0.01f;
        slot.character = sp.character->load() * 0.01f;
        v.order[i] = slotOrder[i].load();
    }
}

void SNSynthProcessor::updateEq()
{
    const float gains[3] = { p (ParamIDs::eqLow)->load(), p (ParamIDs::eqMid)->load(), p (ParamIDs::eqHigh)->load() };
    using C = juce::dsp::IIR::Coefficients<float>;
    const auto sr = currentSampleRate;

    if (std::abs (gains[0] - lastEq[0]) > 1.0e-4f)
    {
        auto c = C::makeLowShelf (sr, 100.0, 0.707f, juce::Decibels::decibelsToGain (gains[0]));
        for (auto& f : eqLow) *f.coefficients = *c;
    }
    if (std::abs (gains[1] - lastEq[1]) > 1.0e-4f)
    {
        auto c = C::makePeakFilter (sr, 1000.0, 0.7f, juce::Decibels::decibelsToGain (gains[1]));
        for (auto& f : eqMid) *f.coefficients = *c;
    }
    if (std::abs (gains[2] - lastEq[2]) > 1.0e-4f)
    {
        auto c = C::makeHighShelf (sr, 8000.0, 0.707f, juce::Decibels::decibelsToGain (gains[2]));
        for (auto& f : eqHigh) *f.coefficients = *c;
    }
    std::copy (gains, gains + 3, lastEq);
}

std::array<int, 3> SNSynthProcessor::getSlotOrder() const
{
    return { slotOrder[0].load(), slotOrder[1].load(), slotOrder[2].load() };
}

void SNSynthProcessor::setSlotOrder (std::array<int, 3> order)
{
    // Must be a permutation of 0, 1, 2; otherwise fall back to the default.
    auto sorted = order;
    std::sort (sorted.begin(), sorted.end());
    if (sorted != std::array<int, 3> { 0, 1, 2 })
        order = { 0, 1, 2 };

    for (size_t i = 0; i < 3; ++i)
        slotOrder[i].store (order[i]);

    parameters.state.setProperty (StateIDs::slotOrder,
                                  juce::String (order[0]) + "," + juce::String (order[1]) + "," + juce::String (order[2]),
                                  nullptr);
}

void SNSynthProcessor::moveSlot (int position, int delta)
{
    const int other = position + delta;
    if (position < 0 || position > 2 || other < 0 || other > 2)
        return;

    auto order = getSlotOrder();
    std::swap (order[(size_t) position], order[(size_t) other]);
    setSlotOrder (order);
}

void SNSynthProcessor::applySlotOrderFromState()
{
    juce::StringArray parts;
    parts.addTokens (parameters.state.getProperty (StateIDs::slotOrder, "0,1,2").toString(), ",", "");
    std::array<int, 3> order { 0, 1, 2 };
    if (parts.size() == 3)
        for (size_t i = 0; i < 3; ++i)
            order[i] = parts[(int) i].getIntValue();
    setSlotOrder (order);
}

void SNSynthProcessor::renderLfo (int numSamples)
{
    if ((int) lfoBuffer.size() < numSamples)
        lfoBuffer.resize ((size_t) numSamples); // only if the host exceeds its announced block size

    // Triangle LFO, free-running so it keeps its place between notes.
    const double inc = p (ParamIDs::lfoRate)->load() / currentSampleRate;
    for (int i = 0; i < numSamples; ++i)
    {
        lfoBuffer[(size_t) i] = (float) (lfoPhase < 0.5 ? 4.0 * lfoPhase - 1.0 : 3.0 - 4.0 * lfoPhase);
        lfoPhase += inc;
        if (lfoPhase >= 1.0)
            lfoPhase -= 1.0;
    }
    voiceSettings.lfo = lfoBuffer.data();
}

juce::AudioProcessorEditor* SNSynthProcessor::createEditor()
{
    return new SNSynthEditor (*this);
}

void SNSynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SNSynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
            applySlotOrderFromState();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SNSynthProcessor();
}
