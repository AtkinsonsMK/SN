#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "SynthEngine.h"
#include "OutputFollower.h"

class SNSynthProcessor : public juce::AudioProcessor
{
public:
    static constexpr int maxVoices = SynthEngine::maxVoices;

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
    LiveInfo live; // read by the on-screen displays
    int getActiveVoiceCount() const { return live.activeVoices.load(); }

    // Modulation routings (source x knob amounts). The screen edits these.
    Mod::Matrix modMatrix;
    static float lfoShapeValue (int shape, double phase, float randomValue);

    // Processing order of Filters 1-3, Amp and EQ. Saved with the project.
    std::array<int, Chain::numBlocks> getChainOrder() const;
    void setChainOrder (std::array<int, Chain::numBlocks> order);
    void moveBlock (int position, int newPosition); // take the block at position and put it at newPosition

    // Just the filters, in the order they run.
    std::array<int, 3> getSlotOrder() const;
    void setSlotOrder (std::array<int, 3> order);
    void moveSlot (int position, int delta); // swap the slot at position with its neighbour

private:
    void updateVoiceSettings();
    void renderLfo (int numSamples);
    float globalVolumeDb (int pos);
    void applySlotOrderFromState();

    struct SlotParams { std::atomic<float> *type, *cutoff, *res, *drive, *env, *character; };
    std::array<SlotParams, 3> slotParams {};
    std::array<std::atomic<int>, Chain::numBlocks> chainOrder { 0, 1, 2, 3, 4 };


    VoiceSettings voiceSettings;
    SynthEngine synth { voiceSettings };
    float meterMeanSquare = 0.0f;
    Mod::Context modContext;
    std::array<std::atomic<float>*, Mod::numTargets> targetParams {};
    static constexpr int globalModInterval = 32;
    std::array<std::vector<float>, 2> lfoBuffers;
    std::array<double, 2> lfoPhases { 0.0, 0.0 };
    std::array<float, 2> lfoSmoothed { 0.0f, 0.0f }, lfoRandom { 0.0f, 0.0f };
    juce::Random lfoRng { 12345 };
    juce::SmoothedValue<float> voiceComp { 1.0f };
    float limiterEnv = 0.0f, limiterRelease = 0.9999f;
    static constexpr float limiterCeiling = 0.966f; // -0.3 dB
    std::array<std::vector<float>, 1 + Chain::numBlocks> tapBuffers; // oscillators, then after each chain position
    double currentSampleRate = 44100.0;
    std::unique_ptr<OutputFollower> outputFollower; // standalone app: follow the Mac's system output

    std::atomic<float>* volumeParam = nullptr;
    std::atomic<float>* p (const char* id) { return parameters.getRawParameterValue (id); }
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain;

    static float volumeDbToGain (float db);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SNSynthProcessor)
};
