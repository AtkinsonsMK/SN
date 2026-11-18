#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// Step 1 voice: one band-limited sawtooth with an amp envelope.
// Fixed values come from the sound concept doc ("Fixed for now" and the
// Amp envelope defaults); they become knobs in step 2.

struct SynthSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

class SynthVoice : public juce::SynthesiserVoice
{
public:
    static constexpr double pitchBendRangeSemitones = 2.0;
    static constexpr float  velocityAmount          = 0.5f;

    bool canPlaySound (juce::SynthesiserSound* s) override
    {
        return dynamic_cast<SynthSound*> (s) != nullptr;
    }

    void setCurrentPlaybackSampleRate (double newRate) override
    {
        juce::SynthesiserVoice::setCurrentPlaybackSampleRate (newRate);
        if (newRate > 0.0)
        {
            ampEnv.setSampleRate (newRate);
            ampEnv.setParameters ({ 0.001f, 0.3f, 1.0f, 0.2f });
        }
    }

    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int pitchWheel) override
    {
        noteNumber = midiNote;
        bendSemitones = wheelToSemitones (pitchWheel);
        updateIncrement();

        // Velocity: 50% amount, so the softest note is half as loud as the hardest.
        noteGain = (1.0f - velocityAmount) + velocityAmount * velocity;

        if (! ampEnv.isActive())
            phase = 0.0;

        ampEnv.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            ampEnv.noteOff();
        }
        else
        {
            ampEnv.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int newValue) override
    {
        bendSemitones = wheelToSemitones (newValue);
        updateIncrement();
    }

    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override
    {
        if (! isVoiceActive())
            return;

        const int numChannels = output.getNumChannels();

        for (int i = 0; i < numSamples; ++i)
        {
            const float env = ampEnv.getNextSample();
            const float sample = 0.25f * noteGain * env * nextSaw();

            for (int ch = 0; ch < numChannels; ++ch)
                output.addSample (ch, startSample + i, sample);

            if (! ampEnv.isActive())
            {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    static double wheelToSemitones (int wheel)
    {
        return (wheel - 8192) / 8192.0 * pitchBendRangeSemitones;
    }

    void updateIncrement()
    {
        const double sr = getSampleRate();
        if (sr <= 0.0)
            return;

        const double freq = 440.0 * std::pow (2.0, (noteNumber + bendSemitones - 69.0) / 12.0);
        increment = juce::jmin (freq / sr, 0.5);
    }

    // PolyBLEP: removes the aliasing a naive sawtooth would produce.
    static double polyBlep (double t, double dt)
    {
        if (t < dt)       { t /= dt;               return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt;    return t * t + t + t + 1.0; }
        return 0.0;
    }

    float nextSaw()
    {
        double value = 2.0 * phase - 1.0;
        value -= polyBlep (phase, increment);

        phase += increment;
        if (phase >= 1.0)
            phase -= 1.0;

        return (float) value;
    }

    juce::ADSR ampEnv;
    int    noteNumber    = 60;
    double bendSemitones = 0.0;
    double phase         = 0.0;
    double increment     = 0.0;
    float  noteGain      = 1.0f;
};
