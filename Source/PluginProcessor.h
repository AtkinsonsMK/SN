#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "SynthVoice.h"

class SNSynthProcessor : public juce::AudioProcessor
{
public:
    static constexpr int numVoices = 8;

    SNSynthProcessor();
    ~SNSynthProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override    { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;
    juce::MidiKeyboardState keyboardState;

private:
    void updateVoiceSettings();
    void renderLfo (int numSamples);

    juce::Synthesiser synth;
    VoiceSettings voiceSettings;
    std::vector<float> lfoBuffer;
    double lfoPhase = 0.0;
    double currentSampleRate = 44100.0;

    std::atomic<float>* volumeParam = nullptr;
    std::atomic<float>* p (const char* id) { return parameters.getRawParameterValue (id); }
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain;

    static float volumeDbToGain (float db);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SNSynthProcessor)
};
