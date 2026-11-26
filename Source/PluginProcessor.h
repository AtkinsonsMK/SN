#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "SynthEngine.h"
#include "OutputFollower.h"
#include "Limiter.h"

class SNSynthProcessor : public juce::AudioProcessor
{
public:
    static constexpr int maxVoices = SynthEngine::maxVoices;

    SNSynthProcessor();
    ~SNSynthProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;               // the DAW jumped or stopped: silence everything at once
    using juce::AudioProcessor::processBlockBypassed;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void markStateChanged();             // tell the DAW the project changed (drum grid, routings, chain)
    std::atomic<int> uiStateEpoch { 0 };   // goes up when the drum grid, routings or a whole save change (screen redraws)
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override; // the longest Amp Release, so renders keep the tails

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

    // Drum machine: the 16-step pattern (the screen edits it), the step
    // playing now (-1 when stopped), and a way to hear one drum straight away.
    Drums::Pattern drumPattern;
    std::atomic<int> drumStep { -1 };
    std::array<std::atomic<float>, Drums::numDrums> drumHitLevel {}; // set on each hit, faded by the screen
    void auditionDrum (int d) { auditionMask.fetch_or (1u << d); }

    // Just the filters, in the order they run.
    std::array<int, 3> getSlotOrder() const;
    void setSlotOrder (std::array<int, 3> order);
    void moveSlot (int position, int delta); // swap the slot at position with its neighbour

private:
    void updateVoiceSettings();
    void renderLfo (int numSamples);
    float globalVolumeDb (int pos);
    void applySlotOrderFromState();
    void renderDrums (int numSamples);
    void hitDrum (int d);

    struct SlotParams { std::atomic<float> *type, *cutoff, *res, *drive, *env, *character, *levelMatch; };
    std::array<SlotParams, 3> slotParams {};
    // The whole chain order packed into one value (3 bits per position), so
    // the sound can never read it half-changed while the screen rearranges it.
    std::atomic<uint32_t> packedChainOrder { packOrder (Chain::defaultOrder()) };
    static uint32_t packOrder (const std::array<int, Chain::numBlocks>& o)
    {
        uint32_t v = 0;
        for (size_t i = 0; i < o.size(); ++i) v |= (uint32_t) (o[i] & 7) << (3 * i);
        return v;
    }
    static std::array<int, Chain::numBlocks> unpackOrder (uint32_t v)
    {
        std::array<int, Chain::numBlocks> o {};
        for (size_t i = 0; i < o.size(); ++i) o[i] = (int) ((v >> (3 * i)) & 7);
        return o;
    }


    VoiceSettings voiceSettings;
    SynthEngine synth { voiceSettings };
    float meterMeanSquare = 0.0f;
    bool rampFromLastBlock = false;
    std::atomic<bool> jumpToKnobs { false }; // set when a project loads
    juce::uint64 gridClock = 0;          // samples since playback started (for the modulation grid)
    std::array<float, 2> lfoRateNow { 1.0f, 1.0f }, lfoCoefNow { 1.0f, 1.0f };
    bool lfoNeedsUpdate = true;
    float volModDbNow = 0.0f;
public:
    int loadedFormatVersion = StateIDs::currentFormatVersion; // format of the last loaded save
    bool drumsAreResting() const { return drumsResting; }      // for the checks
private:            // Volume modulation in dB, gliding between chunks // false until the first block after prepareToPlay
    Mod::Context modContext;
    std::array<std::atomic<float>*, Mod::numTargets> targetParams {};
    static constexpr int globalModInterval = 32;
    std::array<std::vector<float>, 2> lfoBuffers;
    std::array<double, 2> lfoPhases { 0.0, 0.0 };
    std::array<float, 2> lfoSmoothed { 0.0f, 0.0f }, lfoRandom { 0.0f, 0.0f };
    juce::Random lfoRng { 12345 };
    juce::SmoothedValue<float> voiceComp { 1.0f };
    TruePeakLimiter limiter;
    static constexpr float limiterCeiling = TruePeakLimiter::ceiling; // -0.3 dB
    std::array<std::vector<float>, 1 + Chain::numBlocks> tapBuffers; // oscillators, then after each chain position
    double currentSampleRate = 44100.0;

    // Drums
    struct DrumParams { std::atomic<float> *tune, *decay, *tone, *level, *route; };
    std::array<DrumParams, Drums::numDrums> drumParams {};
    std::array<Drums::DrumSettings, Drums::numDrums> drumSettings {};
    std::array<Drums::Voice, Drums::numDrums> drumVoices;
    Drums::Sequencer sequencer;
    std::array<FilterSlot, 3> drumFilters;   // the drums' own copy of each filter slot
    VoiceEq drumEq;
    VoiceSettings drumCur;                   // filter and EQ settings for the drums, with LFO modulation
    struct DrumSmooth { float oct = 10.0f, res = 0.0f, drive = 0.0f, character = 0.0f; };
    std::array<DrumSmooth, 3> drumSmooth {};
    std::array<float, Drums::numDrums> drumLevelSmooth {};
    float drumSmoothCoef = 0.005f;
    std::array<float, 3> drumEqDb {};
    std::array<std::vector<float>, Drums::numRoutes> drumBus; // dry, into Filter 1, 2, 3
    std::vector<float> drumOut;
    float drumDcX1 = 0.0f, drumDcY1 = 0.0f, drumDcCoef = 0.9993f;
    // The drums' filters rest once the drums have been silent for 100 ms.
    bool drumsResting = true;
    int drumQuietRun = 0;
    std::atomic<uint32_t> auditionMask { 0 };
    static constexpr float drumGain = 0.4f;
    std::unique_ptr<OutputFollower> outputFollower; // standalone app: follow the Mac's system output

    std::atomic<float>* volumeParam = nullptr;
    std::atomic<float>* ampReleaseParam = nullptr;
    void silenceAllVoices();

    // Every control's live value and its default, so a broken value ("not a
    // number" from a damaged preset or a glitchy host) can be put back.
    struct SafeParam { std::atomic<float>* value; float fallback; };
    std::vector<SafeParam> safeParams; // filled once in the constructor
    void repairBadParameterValues();
    std::vector<float> synthOutTap;    // the keyboard notes alone at the output, for the Numbers view
    std::atomic<float>* p (const char* id) { return parameters.getRawParameterValue (id); }
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain;

    static float volumeDbToGain (float db);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SNSynthProcessor)
};
