#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include "Envelope.h"
#include "Filters.h"
#include "LiveInfo.h"

// Values the processor hands to every voice once per audio block.
struct VoiceSettings
{
    float sawLevel = 1.0f, pulseLevel = 0.0f, pulseWidth = 0.5f, subLevel = 0.0f; // 0..1
    float fenvA = 0.001f, fenvD = 0.4f, fenvS = 0.3f, fenvR = 0.3f;
    float aenvA = 0.001f, aenvD = 0.3f, aenvS = 1.0f, aenvR = 0.2f;
    float lfoToCutoff = 0.0f; // 0..1
    float drift = 0.15f;      // 0..1
    float glideSeconds = 0.0f;

    FilterSlotSettings slots[3];
    int order[3] = { 0, 1, 2 };   // processing order of the slots

    // For the displays: per-sample stage outputs summed over all voices
    // (0 oscillator, 1-3 after each filter position), and live note info.
    float* taps[4] = { nullptr, nullptr, nullptr, nullptr };
    LiveInfo* live = nullptr;
    int noteCounter = 0;   // increases with every note played
    int newestNote = 0;    // the counter value of the most recent note

    const float* lfo = nullptr; // one LFO value (-1..1) per sample of the current block
    double lastNote = -1.0;     // most recent note played, for Glide
};

struct SynthSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

class SynthVoice : public juce::SynthesiserVoice
{
public:
    // Fixed for now (see "Fixed for now" in the sound concept doc).
    static constexpr double pitchBendRangeSemitones = 2.0;
    static constexpr float  velocityAmount          = 0.5f;
    static constexpr float  keyTracking             = 0.5f;
    static constexpr float  maxDriftCents           = 10.0f;

    static constexpr float envOctavesAtFull   = 6.0f;  // Env Amount 100% sweeps 6 octaves
    static constexpr float lfoOctavesAtFull   = 3.0f;  // LFO to Cutoff 100% = +/- 3 octaves

    SynthVoice (VoiceSettings& s, int voiceIndex) : settings (s), random ((juce::int64) (voiceIndex * 7919 + 17)) {}

    bool canPlaySound (juce::SynthesiserSound* s) override
    {
        return dynamic_cast<SynthSound*> (s) != nullptr;
    }

    void setCurrentPlaybackSampleRate (double newRate) override
    {
        juce::SynthesiserVoice::setCurrentPlaybackSampleRate (newRate);
        if (newRate <= 0.0)
            return;

        ampEnv.setSampleRate (newRate);
        filterEnv.setSampleRate (newRate);

        for (auto& f : filters)
            f.prepare ((float) newRate);

        smoothCoef = 1.0f - std::exp (-1.0f / (0.005f * (float) newRate)); // 5 ms
        driftCoef  = 1.0f - std::exp (-1.0f / (0.4f  * (float) newRate));  // 0.4 s
        driftValue = (float) (random.nextFloat() * 2.0 - 1.0);
    }

    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int pitchWheel) override
    {
        targetNote = midiNote;
        bendSemitones = wheelToSemitones (pitchWheel);

        const double sr = getSampleRate();
        if (settings.glideSeconds > 0.0005f && settings.lastNote >= 0.0)
        {
            currentNote = settings.lastNote;
            glideStep = (targetNote - currentNote) / (settings.glideSeconds * sr);
            gliding = std::abs (targetNote - currentNote) > 1.0e-9;
        }
        else
        {
            currentNote = targetNote;
            gliding = false;
        }
        settings.lastNote = targetNote;
        noteSerial = ++settings.noteCounter;
        settings.newestNote = noteSerial;

        // Velocity: 50% amount, so the softest note is half as loud as the hardest.
        noteGain = (1.0f - velocityAmount) + velocityAmount * velocity;

        if (! ampEnv.isActive())
        {
            phase = 0.0;
            subPhase = 0.0;
            for (auto& f : filters)
                f.reset();
            sSaw = settings.sawLevel; sPulse = settings.pulseLevel;
            sPw = settings.pulseWidth; sSub = settings.subLevel;
        }

        for (auto& f : filters)
            f.trigger ((float) sr);

        applyEnvelopeSettings();
        ampEnv.noteOn();
        filterEnv.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            ampEnv.noteOff();
            filterEnv.noteOff();
        }
        else
        {
            ampEnv.reset();
            filterEnv.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int newValue) override { bendSemitones = wheelToSemitones (newValue); }
    void controllerMoved (int, int) override {}

    using juce::SynthesiserVoice::renderNextBlock;

    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override
    {
        if (! isVoiceActive())
            return;

        applyEnvelopeSettings();

        const double sr = getSampleRate();
        const int numChannels = output.getNumChannels();
        const float keyTrackOct = keyTracking * (float) (targetNote - 60) / 12.0f;
        float baseOct[3];
        for (int s = 0; s < 3; ++s)
            baseOct[s] = std::log2 (settings.slots[s].cutoffHz) + keyTrackOct;

        for (int i = 0; i < numSamples; ++i)
        {
            // Parameter smoothing (avoids zipper noise when knobs move).
            sSaw   += (settings.sawLevel   - sSaw)   * smoothCoef;
            sPulse += (settings.pulseLevel - sPulse) * smoothCoef;
            sPw    += (settings.pulseWidth - sPw)    * smoothCoef;
            sSub   += (settings.subLevel   - sSub)   * smoothCoef;

            // Glide
            if (gliding)
            {
                currentNote += glideStep;
                if ((glideStep > 0.0 && currentNote >= targetNote) || (glideStep < 0.0 && currentNote <= targetNote))
                {
                    currentNote = targetNote;
                    gliding = false;
                }
            }

            // Drift: slow random wander per voice.
            if (--driftCounter <= 0)
            {
                driftTarget = random.nextFloat() * 2.0f - 1.0f;
                driftCounter = (int) (sr * (0.3 + 0.7 * random.nextFloat()));
            }
            driftValue += (driftTarget - driftValue) * driftCoef;
            const double driftSemis = settings.drift * maxDriftCents * driftValue / 100.0;

            const double pitch = currentNote + bendSemitones + driftSemis;
            const double freq = 440.0 * std::pow (2.0, (pitch - 69.0) / 12.0);
            const double inc = juce::jmin (freq / sr, 0.45);

            const float osc = 0.3f * (sSaw * saw (inc) + sPulse * pulse (inc, sPw) + sSub * sub (inc * 0.5));
            advance (inc);

            // Filter rack: slots in series, in the chosen order.
            const float fenv = filterEnv.next();
            const float lfo = settings.lfo != nullptr ? settings.lfo[startSample + i] : 0.0f;
            const float lfoOct = lfo * settings.lfoToCutoff * lfoOctavesAtFull;

            const float gain = ampEnv.next() * noteGain;
            const int pos = startSample + i;
            if (settings.taps[0] != nullptr) settings.taps[0][pos] += osc * gain;

            float filtered = osc;
            for (int n = 0; n < 3; ++n)
            {
                const int s = settings.order[n];
                const auto& slot = settings.slots[s];
                const float cutoffOct = baseOct[s] + fenv * slot.envAmount * envOctavesAtFull + lfoOct;
                lastCutoffHz[s] = std::exp2 (juce::jlimit (4.3f, 14.3f, cutoffOct));
                filtered = filters[s].process (filtered, slot, lastCutoffHz[s]);
                if (settings.taps[n + 1] != nullptr) settings.taps[n + 1][pos] += filtered * gain;
            }
            lastFreq = (float) freq;

            const float sample = filtered * gain;

            for (int ch = 0; ch < numChannels; ++ch)
                output.addSample (ch, startSample + i, sample);

            if (! ampEnv.isActive())
            {
                filterEnv.reset();
                clearCurrentNote();
                break;
            }
        }

        publishLiveInfo();
    }

private:
    // The newest note tells the screen what to draw.
    void publishLiveInfo()
    {
        auto* live = settings.live;
        if (live == nullptr || noteSerial != settings.newestNote)
            return;

        live->noteActive.store (isVoiceActive());
        live->noteHz.store (lastFreq);
        for (int s = 0; s < 3; ++s)
        {
            live->liveCutoffHz[s].store (lastCutoffHz[s]);
            live->gateLevel[s].store (settings.slots[s].type == FilterType::lowpassGate ? filters[s].gate() : 1.0f);
        }
        live->filterEnvStage.store (filterEnv.stageIndex());
        live->filterEnvSeconds.store ((float) filterEnv.secondsInStage());
        live->filterEnvLevel.store (filterEnv.level());
        live->ampEnvStage.store (ampEnv.stageIndex());
        live->ampEnvSeconds.store ((float) ampEnv.secondsInStage());
        live->ampEnvLevel.store (ampEnv.level());
    }

    void applyEnvelopeSettings()
    {
        ampEnv.setParameters    (settings.aenvA, settings.aenvD, settings.aenvS, settings.aenvR);
        filterEnv.setParameters (settings.fenvA, settings.fenvD, settings.fenvS, settings.fenvR);
    }

    static double wheelToSemitones (int wheel)
    {
        return (wheel - 8192) / 8192.0 * pitchBendRangeSemitones;
    }

    // PolyBLEP: smooths each jump in the waveform so it doesn't alias (sound harsh).
    static double polyBlep (double t, double dt)
    {
        if (t < dt)       { t /= dt;            return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0.0;
    }

    float saw (double inc) const
    {
        return (float) (2.0 * phase - 1.0 - polyBlep (phase, inc));
    }

    float pulse (double inc, float width) const
    {
        double v = phase < width ? 1.0 : -1.0;
        v += polyBlep (phase, inc);
        double t = phase - width;
        if (t < 0.0) t += 1.0;
        v -= polyBlep (t, inc);
        return (float) (v - (2.0 * width - 1.0)); // remove DC so level stays even as width changes
    }

    float sub (double inc) const
    {
        double v = subPhase < 0.5 ? 1.0 : -1.0;
        v += polyBlep (subPhase, inc);
        double t = subPhase + 0.5;
        if (t >= 1.0) t -= 1.0;
        v -= polyBlep (t, inc);
        return (float) v;
    }

    void advance (double inc)
    {
        phase += inc;
        if (phase >= 1.0)
            phase -= 1.0;

        subPhase += inc * 0.5;
        if (subPhase >= 1.0)
            subPhase -= 1.0;
    }

    VoiceSettings& settings;
    juce::Random random;

    Envelope ampEnv, filterEnv;
    FilterSlot filters[3];

    int    targetNote    = 60;
    double currentNote   = 60.0;
    double glideStep     = 0.0;
    bool   gliding       = false;
    double bendSemitones = 0.0;
    double phase = 0.0, subPhase = 0.0;
    float  noteGain = 1.0f;
    int    noteSerial = 0;
    float  lastFreq = 0.0f;
    float  lastCutoffHz[3] = { 1000.0f, 1000.0f, 1000.0f };

    float smoothCoef = 0.01f;
    float sSaw = 1.0f, sPulse = 0.0f, sPw = 0.5f, sSub = 0.0f;

    float driftCoef = 0.0001f, driftValue = 0.0f, driftTarget = 0.0f;
    int   driftCounter = 0;
};
