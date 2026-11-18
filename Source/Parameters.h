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
    inline constexpr const char* sineLevel  = "osc_sine";
    inline constexpr const char* osc1Coarse = "osc1_coarse";
    inline constexpr const char* osc1Fine   = "osc1_fine";
    inline constexpr const char* osc1Level  = "osc1_level";

    // Oscillator 2
    inline constexpr const char* osc2Sine   = "osc2_sine";
    inline constexpr const char* osc2Saw    = "osc2_saw";
    inline constexpr const char* osc2Pulse  = "osc2_pulse";
    inline constexpr const char* osc2Pw     = "osc2_pw";
    inline constexpr const char* osc2Coarse = "osc2_coarse";
    inline constexpr const char* osc2Fine   = "osc2_fine";
    inline constexpr const char* osc2Level  = "osc2_level";

    // Oscillators together
    inline constexpr const char* fm1to2  = "fm_1to2";
    inline constexpr const char* fm2to1  = "fm_2to1";
    inline constexpr const char* ringMod = "ring";
    inline constexpr const char* voices  = "voices";

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

    // Filter rack: three slots, each with the same six controls.
    // IDs are "f1_type", "f1_cutoff" ... "f3_char".
    inline juce::String slot (int slotIndex, const char* control)
    {
        return "f" + juce::String (slotIndex + 1) + "_" + control;
    }
    inline constexpr const char* slotType      = "type";
    inline constexpr const char* slotCutoff    = "cutoff";
    inline constexpr const char* slotResonance = "res";
    inline constexpr const char* slotDrive     = "drive";
    inline constexpr const char* slotEnv       = "env";
    inline constexpr const char* slotCharacter = "char";
    inline constexpr int numSlots = 3;

    // EQ
    inline constexpr const char* eqLow  = "eq_low";
    inline constexpr const char* eqMid  = "eq_mid";
    inline constexpr const char* eqHigh = "eq_high";

    // Output
    inline constexpr const char* volume = "volume";
}

// Non-automatable settings stored with the project / preset.
namespace StateIDs
{
    inline constexpr const char* slotOrder = "slotOrder"; // e.g. "0,1,2"
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

    inline const StringArray& filterTypeNames()
    {
        static const StringArray names { "Off", "OTA", "Sallen-Key", "Lowpass Gate",
                                         "Steiner-Parker", "Polivoks", "Comb", "Formant" };
        return names;
    }

    inline std::unique_ptr<AudioParameterFloat> percent (const String& id, const String& name,
                                                         float min, float max, float def)
    {
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (min, max, 0.1f), def,
            AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percentText));
    }

    // Times use a skewed range so short times get most of the knob travel.
    inline std::unique_ptr<AudioParameterFloat> time (const String& id, const String& name,
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

        // Added in v1.1: Osc 1 extras, Osc 2, FM, Ring Mod, Voices.
        // Defaults keep older sounds unchanged (Osc 2 level 0).
        auto semis = [] (const String& id, const String& name)
        {
            return std::make_unique<AudioParameterInt> (
                ParameterID { id, 1 }, name, -24, 24, 0,
                AudioParameterIntAttributes().withLabel ("st").withStringFromValueFunction (
                    [] (int v, int) { return (v > 0 ? "+" : "") + String (v) + " st"; }));
        };
        auto cents = [] (const String& id, const String& name, float def)
        {
            return std::make_unique<AudioParameterFloat> (
                ParameterID { id, 1 }, name, NormalisableRange<float> (-100.0f, 100.0f, 0.1f), def,
                AudioParameterFloatAttributes().withLabel ("ct").withStringFromValueFunction (
                    [] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1) + " ct"; }));
        };
        layout.add (percent (ParamIDs::sineLevel,  "Osc 1 Sine Level", 0.0f, 100.0f, 0.0f));
        layout.add (semis   (ParamIDs::osc1Coarse, "Osc 1 Coarse"));
        layout.add (cents   (ParamIDs::osc1Fine,   "Osc 1 Fine", 0.0f));
        layout.add (percent (ParamIDs::osc1Level,  "Osc 1 Level", 0.0f, 100.0f, 100.0f));

        layout.add (percent (ParamIDs::osc2Sine,   "Osc 2 Sine Level",  0.0f, 100.0f, 0.0f));
        layout.add (percent (ParamIDs::osc2Saw,    "Osc 2 Saw Level",   0.0f, 100.0f, 100.0f));
        layout.add (percent (ParamIDs::osc2Pulse,  "Osc 2 Pulse Level", 0.0f, 100.0f, 0.0f));
        layout.add (percent (ParamIDs::osc2Pw,     "Osc 2 Pulse Width", 5.0f, 95.0f,  50.0f));
        layout.add (semis   (ParamIDs::osc2Coarse, "Osc 2 Coarse"));
        layout.add (cents   (ParamIDs::osc2Fine,   "Osc 2 Fine", 7.0f));
        layout.add (percent (ParamIDs::osc2Level,  "Osc 2 Level", 0.0f, 100.0f, 0.0f));

        layout.add (percent (ParamIDs::fm1to2,  "FM 1 to 2", 0.0f, 100.0f, 0.0f));
        layout.add (percent (ParamIDs::fm2to1,  "FM 2 to 1", 0.0f, 100.0f, 0.0f));
        layout.add (percent (ParamIDs::ringMod, "Ring Mod",  0.0f, 100.0f, 0.0f));
        layout.add (std::make_unique<AudioParameterInt> (
            ParameterID { ParamIDs::voices, 1 }, "Voices", 1, 16, 8,
            AudioParameterIntAttributes().withStringFromValueFunction (
                [] (int v, int) { return v == 1 ? String ("Mono") : String (v); })));

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

        // Filter rack (18): defaults are Slot 1 / Slot 2 / Slot 3.
        for (int i = 0; i < ParamIDs::numSlots; ++i)
        {
            const String n = "Filter " + String (i + 1) + " ";
            layout.add (std::make_unique<AudioParameterChoice> (
                ParameterID { ParamIDs::slot (i, ParamIDs::slotType), 1 }, n + "Type",
                filterTypeNames(), i == 0 ? 1 : 0));

            NormalisableRange<float> cutoff (20.0f, 20000.0f);
            cutoff.setSkewForCentre (1000.0f);
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { ParamIDs::slot (i, ParamIDs::slotCutoff), 1 }, n + "Cutoff",
                cutoff, i == 0 ? 5000.0f : 2000.0f,
                AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (
                    [] (float v, int) { return v < 1000.0f ? String (roundToInt (v)) + " Hz"
                                                           : String (v / 1000.0f, 2) + " kHz"; })));

            layout.add (percent (ParamIDs::slot (i, ParamIDs::slotResonance), n + "Resonance", 0.0f, 100.0f, 15.0f));
            layout.add (percent (ParamIDs::slot (i, ParamIDs::slotDrive),     n + "Drive",     0.0f, 100.0f, 0.0f));
            layout.add (percent (ParamIDs::slot (i, ParamIDs::slotEnv),       n + "Env Amount", -100.0f, 100.0f, i == 0 ? 40.0f : 0.0f));
            layout.add (percent (ParamIDs::slot (i, ParamIDs::slotCharacter), n + "Type Knob", 0.0f, 100.0f, 0.0f));
        }

        // EQ (3)
        for (auto [id, name] : { std::pair { ParamIDs::eqLow, "EQ Low" },
                                 std::pair { ParamIDs::eqMid, "EQ Mid" },
                                 std::pair { ParamIDs::eqHigh, "EQ High" } })
        {
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { id, 1 }, name, NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (
                    [] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1) + " dB"; })));
        }

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
