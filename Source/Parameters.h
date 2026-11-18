#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Parameter IDs are saved inside FL Studio projects and automation clips.
// Never rename or reuse an ID once it has shipped; add new ones instead.
// The plain-English reference for every control is the
// "Synth sound concept and parameters" doc.
namespace ParamIDs
{
    inline constexpr const char* volume = "volume";
}

namespace Params
{
    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        // Volume: -inf to +6 dB, default -6 dB. The bottom of the range is
        // treated as silence (-inf).
        NormalisableRange<float> volumeRange (-60.0f, 6.0f, 0.1f);
        volumeRange.setSkewForCentre (-12.0f);

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamIDs::volume, 1 },
            "Volume",
            volumeRange,
            -6.0f,
            AudioParameterFloatAttributes()
                .withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int)
                {
                    return v <= -60.0f ? String ("-inf dB")
                                       : String (v, 1) + " dB";
                })));

        return layout;
    }
}
