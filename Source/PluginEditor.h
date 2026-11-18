#pragma once

#include "PluginProcessor.h"
#include "Visualisers.h"

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
    juce::Component* display = nullptr; // optional live display above the knobs
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
                      private juce::Timer,
                      private juce::KeyListener
{
public:
    explicit SNSynthEditor (SNSynthProcessor&);
    ~SNSynthEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

private:
    void timerCallback() override;
    Knob& makeKnob (const juce::String& paramId, const juce::String& label);
    void setupSlot (SlotPanel&, int slotIndex);
    void updateSlotTypeDisplay (SlotPanel&);
    void layoutGroup (KnobGroup&, juce::Rectangle<int> area, int topExtra = 0);
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    bool keyStateChanged (bool isKeyDown, juce::Component*) override;
    bool handleKeyPress (const juce::KeyPress&);
    bool updateHeldKeys();
    void changeVelocity (int delta);
    void shiftOctave (int delta);
    void updateOctaveLabel();

    SNSynthProcessor& processor;

    std::vector<std::unique_ptr<Knob>> knobs;
    KnobGroup oscGroup, filterEnvGroup, ampEnvGroup, movementGroup, eqGroup, outputGroup;
    std::array<SlotPanel, 3> slots;
    std::array<int, 3> shownOrder { -1, -1, -1 };

    Vis::Scope scope;
    Vis::EnvelopeShape filterEnvShape, ampEnvShape;
    Vis::LfoShape lfoShape;
    Vis::EqCurve eqCurve;
    std::array<std::unique_ptr<Vis::FilterCurve>, 3> filterCurves;
    std::vector<juce::Component*> displays;

    juce::TextButton numbersButton { "Numbers" };
    std::unique_ptr<Vis::NumbersWindow> numbersWindow;

    juce::MidiKeyboardComponent keyboard;
    juce::Label octaveLabel;
    juce::Label hintLabel;
    int baseOctave = 5; // 5 = MIDI note 60 = middle C (C4)
    int velocity = 100;
    static const char* noteKeys;
    std::array<int, 18> heldNotes {}; // note held by each computer key, or -1
    juce::Component* listeningTo = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SNSynthEditor)
};
