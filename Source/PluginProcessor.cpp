#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "SynthVoice.h"

SNSynthProcessor::SNSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "SNSynthState", Params::createLayout())
{
    volumeParam = parameters.getRawParameterValue (ParamIDs::volume);

    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new SynthVoice());

    synth.addSound (new SynthSound());
}

float SNSynthProcessor::volumeDbToGain (float db)
{
    // Bottom of the range means silence; a tiny floor keeps the
    // multiplicative smoother valid.
    return db <= -60.0f ? 1.0e-5f : juce::Decibels::decibelsToGain (db);
}

void SNSynthProcessor::prepareToPlay (double sampleRate, int)
{
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
