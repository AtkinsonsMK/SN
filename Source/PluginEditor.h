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

// The controls of one filter slot.
struct SlotPanel
{
    int slotIndex = 0;
    juce::ComboBox type;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAttachment;
    juce::TextButton moveLeft { "<" }, moveRight { ">" };
    KnobGroup group;
    Knob* characterKnob = nullptr;
    int shownType = -1;
};

class SNSynthEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit SNSynthEditor (SNSynthProcessor&);
    ~SNSynthEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

private:
    void timerCallback() override;
    Knob& makeKnob (const juce::String& paramId, const juce::String& label);
    void setupSlot (SlotPanel&, int slotIndex);
    void updateSlotTypeDisplay (SlotPanel&);
    void layoutGroup (KnobGroup&, juce::Rectangle<int> area, int topExtra = 0);
    void shiftOctave (int delta);
    void updateOctaveLabel();

    SNSynthProcessor& processor;

    std::vector<std::unique_ptr<Knob>> knobs;
    KnobGroup oscGroup, filterEnvGroup, ampEnvGroup, movementGroup, eqGroup, outputGroup;
    std::array<SlotPanel, 3> slots;
    std::array<int, 3> shownOrder { -1, -1, -1 };

    juce::MidiKeyboardComponent keyboard;
    juce::Label octaveLabel;
    juce::Label hintLabel;
    int baseOctave = 5; // JUCE octave 5 = MIDI note 60 = middle C (C4)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SNSynthEditor)
};
