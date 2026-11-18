#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Parameter IDs are saved inside FL Studio projects and automation clips.
// Never rename or reuse an ID once it has shipped; add new ones instead.
// The plain-English reference for every control is the
// "Synth sound concept and parameters" doc.
namespace ParamIDs
{
    // Oscillator
    inline constexpr const char* sawLevel   = "osc_saw";
    inline constexpr const char* pulseLevel = "osc_pulse";
    inline constexpr const char* pulseWidth = "osc_pw";
    inline constexpr const char* subLevel   = "osc_sub";

    // Filter envelope
    inline constexpr const char* fenvAttack  = "fenv_attack";
    inline constexpr const char* fenvDecay   = "fenv_decay";
    inline constexpr const char* fenvSustain = "fenv_sustain";
    inline constexpr const char* fenvRelease = "fenv_release";

    // Amp envelope
    inline constexpr const char* aenvAttack  = "aenv_attack";
    inline constexpr const char* aenvDecay   = "aenv_decay";
    inline constexpr const char* aenvSustain = "aenv_sustain";
    inline constexpr const char* aenvRelease = "aenv_release";

    // Movement and play
    inline constexpr const char* lfoRate   = "lfo_rate";
    inline constexpr const char* lfoCutoff = "lfo_cutoff";
    inline constexpr const char* drift     = "drift";
    inline constexpr const char* glide     = "glide";

    // Output
    inline constexpr const char* volume = "volume";
}

namespace Params
{
    using namespace juce;

    inline String percentText (float v, int)   { return String (roundToInt (v)) + "%"; }

    inline String timeText (float seconds, int)
    {
        if (seconds <= 0.0f)  return "0 ms";
        if (seconds < 0.001f) return String (seconds * 1000.0f, 2) + " ms";
        if (seconds < 0.01f)  return String (seconds * 1000.0f, 1) + " ms";
        if (seconds < 1.0f)   return String (roundToInt (seconds * 1000.0f)) + " ms";
        return String (seconds, 2) + " s";
    }

    inline std::unique_ptr<AudioParameterFloat> percent (const char* id, const String& name,
                                                         float min, float max, float def)
    {
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (min, max, 0.1f), def,
            AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percentText));
    }

    // Times use a skewed range so short times get most of the knob travel.
    inline std::unique_ptr<AudioParameterFloat> time (const char* id, const String& name,
                                                      float min, float max, float def, float centre)
    {
        NormalisableRange<float> range (min, max);
        range.setSkewForCentre (centre);
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, range, def,
            AudioParameterFloatAttributes().withLabel ("s").withStringFromValueFunction (timeText));
    }

    inline AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        AudioProcessorValueTreeState::ParameterLayout layout;

        // Oscillator (4)
        layout.add (percent (ParamIDs::sawLevel,   "Saw Level",   0.0f, 100.0f, 100.0f));
        layout.add (percent (ParamIDs::pulseLevel, "Pulse Level", 0.0f, 100.0f, 0.0f));
        layout.add (percent (ParamIDs::pulseWidth, "Pulse Width", 5.0f, 95.0f,  50.0f));
        layout.add (percent (ParamIDs::subLevel,   "Sub Level",   0.0f, 100.0f, 0.0f));

        // Filter envelope (4)
        layout.add (time    (ParamIDs::fenvAttack,  "Filter Attack",  0.0005f, 10.0f, 0.001f, 0.3f));
        layout.add (time    (ParamIDs::fenvDecay,   "Filter Decay",   0.001f,  10.0f, 0.4f,   0.5f));
        layout.add (percent (ParamIDs::fenvSustain, "Filter Sustain", 0.0f, 100.0f, 30.0f));
        layout.add (time    (ParamIDs::fenvRelease, "Filter Release", 0.001f,  15.0f, 0.3f,   0.6f));

        // Amp envelope (4)
        layout.add (time    (ParamIDs::aenvAttack,  "Amp Attack",  0.0005f, 10.0f, 0.001f, 0.3f));
        layout.add (time    (ParamIDs::aenvDecay,   "Amp Decay",   0.001f,  10.0f, 0.3f,   0.5f));
        layout.add (percent (ParamIDs::aenvSustain, "Amp Sustain", 0.0f, 100.0f, 100.0f));
        layout.add (time    (ParamIDs::aenvRelease, "Amp Release", 0.001f,  15.0f, 0.2f,   0.6f));

        // Movement and play (4)
        {
            NormalisableRange<float> range (0.05f, 20.0f);
            range.setSkewForCentre (2.0f);
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { ParamIDs::lfoRate, 1 }, "LFO Rate", range, 4.0f,
                AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (
                    [] (float v, int) { return String (v, v < 1.0f ? 2 : 1) + " Hz"; })));
        }
        layout.add (percent (ParamIDs::lfoCutoff, "LFO to Cutoff", 0.0f, 100.0f, 0.0f));
        layout.add (percent (ParamIDs::drift,     "Drift",         0.0f, 100.0f, 15.0f));
        layout.add (time    (ParamIDs::glide,     "Glide",         0.0f, 2.0f,   0.0f, 0.25f));

        // Output: Volume -inf to +6 dB, default -6 dB. The bottom of the
        // range is treated as silence (-inf).
        {
            NormalisableRange<float> range (-60.0f, 6.0f, 0.1f);
            range.setSkewForCentre (-12.0f);
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { ParamIDs::volume, 1 }, "Volume", range, -6.0f,
                AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (
                    [] (float v, int) { return v <= -60.0f ? String ("-inf dB") : String (v, 1) + " dB"; })));
        }

        return layout;
    }
}
