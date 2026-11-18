#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include "Envelope.h"

// Values the processor hands to every voice once per audio block.
struct VoiceSettings
{
    float sawLevel = 1.0f, pulseLevel = 0.0f, pulseWidth = 0.5f, subLevel = 0.0f; // 0..1
    float fenvA = 0.001f, fenvD = 0.4f, fenvS = 0.3f, fenvR = 0.3f;
    float aenvA = 0.001f, aenvD = 0.3f, aenvS = 1.0f, aenvR = 0.2f;
    float lfoToCutoff = 0.0f; // 0..1
    float drift = 0.15f;      // 0..1
    float glideSeconds = 0.0f;

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

    // Temporary filter until the step 3 filter rack: Slot 1 defaults.
    static constexpr float tempCutoffHz       = 5000.0f;
    static constexpr float tempEnvAmount      = 0.4f;  // +40%
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

        filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        filter.prepare ({ newRate, 512, 1 });
        filter.setResonance (0.85f);

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

        // Velocity: 50% amount, so the softest note is half as loud as the hardest.
        noteGain = (1.0f - velocityAmount) + velocityAmount * velocity;

        if (! ampEnv.isActive())
        {
            phase = 0.0;
            subPhase = 0.0;
            filter.reset();
            sSaw = settings.sawLevel; sPulse = settings.pulseLevel;
            sPw = settings.pulseWidth; sSub = settings.subLevel;
        }

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
        const float baseOct = std::log2 (tempCutoffHz) + keyTrackOct;
        const float maxCutoff = (float) (sr * 0.45);

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

            // Temporary filter (replaced by the filter rack in step 3).
            const float fenv = filterEnv.next();
            const float lfo = settings.lfo != nullptr ? settings.lfo[startSample + i] : 0.0f;
            const float cutoffOct = baseOct
                                  + fenv * tempEnvAmount * envOctavesAtFull
                                  + lfo * settings.lfoToCutoff * lfoOctavesAtFull;
            filter.setCutoffFrequency (juce::jlimit (20.0f, maxCutoff, std::exp2 (cutoffOct)));
            const float filtered = filter.processSample (0, osc);

            const float sample = filtered * ampEnv.next() * noteGain;

            for (int ch = 0; ch < numChannels; ++ch)
                output.addSample (ch, startSample + i, sample);

            if (! ampEnv.isActive())
            {
                filterEnv.reset();
                clearCurrentNote();
                break;
            }
        }
    }

private:
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
    juce::dsp::StateVariableTPTFilter<float> filter;

    int    targetNote    = 60;
    double currentNote   = 60.0;
    double glideStep     = 0.0;
    bool   gliding       = false;
    double bendSemitones = 0.0;
    double phase = 0.0, subPhase = 0.0;
    float  noteGain = 1.0f;

    float smoothCoef = 0.01f;
    float sSaw = 1.0f, sPulse = 0.0f, sPw = 0.5f, sSub = 0.0f;

    float driftCoef = 0.0001f, driftValue = 0.0f, driftTarget = 0.0f;
    int   driftCounter = 0;
};
