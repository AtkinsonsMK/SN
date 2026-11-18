#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

SNSynthProcessor::SNSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "SNSynthState", Params::createLayout())
{
    volumeParam = parameters.getRawParameterValue (ParamIDs::volume);

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
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SNSynthProcessor();
}
