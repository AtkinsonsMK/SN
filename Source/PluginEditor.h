#pragma once

#include "PluginProcessor.h"

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
    void shiftOctave (int delta);
    void updateOctaveLabel();

    SNSynthProcessor& processor;

    juce::Slider volumeKnob;
    juce::Label  volumeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttachment;

    juce::MidiKeyboardComponent keyboard;
    juce::Label octaveLabel;
    juce::Label hintLabel;
    int baseOctave = 5; // JUCE octave 5 = MIDI note 60 = middle C (C4)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SNSynthEditor)
};
