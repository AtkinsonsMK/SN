#pragma once

#include "PluginProcessor.h"

// One labelled knob attached to a parameter.
struct Knob
{
    juce::Slider slider;
    juce::Label  label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// A titled panel of knobs.
struct KnobGroup
{
    juce::String title;
    std::vector<Knob*> knobs;
    juce::Rectangle<int> bounds;
};

class SNSynthEditor : public juce::AudioProcessorEditor
{
public:
    explicit SNSynthEditor (SNSynthProcessor&);
    ~SNSynthEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

private:
    Knob& makeKnob (const char* paramId, const juce::String& label);
    void layoutGroup (KnobGroup&, juce::Rectangle<int> area);
    void shiftOctave (int delta);
    void updateOctaveLabel();

    SNSynthProcessor& processor;

    std::vector<std::unique_ptr<Knob>> knobs;
    KnobGroup oscGroup, filterEnvGroup, ampEnvGroup, movementGroup, outputGroup;

    juce::MidiKeyboardComponent keyboard;
    juce::Label octaveLabel;
    juce::Label hintLabel;
    int baseOctave = 5; // JUCE octave 5 = MIDI note 60 = middle C (C4)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SNSynthEditor)
};
