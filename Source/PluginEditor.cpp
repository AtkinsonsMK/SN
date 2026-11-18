#include "PluginEditor.h"
#include "Parameters.h"

namespace
{
    const juce::Colour background { 0xff1d1f24 };
    const juce::Colour panel      { 0xff2a2d34 };
    const juce::Colour accent     { 0xffe8a33d };
    const juce::Colour text       { 0xffe6e6e6 };
    const juce::Colour dimText    { 0xff9aa0a8 };
}

SNSynthEditor::SNSynthEditor (SNSynthProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    volumeKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    volumeKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    volumeKnob.setColour (juce::Slider::rotarySliderFillColourId, accent);
    volumeKnob.setColour (juce::Slider::thumbColourId, text);
    volumeKnob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    volumeKnob.setWantsKeyboardFocus (false);
    addAndMakeVisible (volumeKnob);
    volumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        p.parameters, ParamIDs::volume, volumeKnob);

    volumeLabel.setText ("Volume", juce::dontSendNotification);
    volumeLabel.setJustificationType (juce::Justification::centred);
    volumeLabel.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (volumeLabel);

    // Computer keyboard: A W S E D F T G Y H U J K O L P ; plays notes.
    keyboard.setKeyPressBaseOctave (baseOctave);
    keyboard.setOctaveForMiddleC (4);
    keyboard.setAvailableRange (24, 108);
    keyboard.setKeyWidth (24.0f);
    keyboard.setLowestVisibleKey (48);
    keyboard.setWantsKeyboardFocus (true);
    addAndMakeVisible (keyboard);

    octaveLabel.setColour (juce::Label::textColourId, accent);
    octaveLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (octaveLabel);
    updateOctaveLabel();

    hintLabel.setText ("Play with A to ; on your computer keyboard.  Z / X: octave down / up.",
                       juce::dontSendNotification);
    hintLabel.setColour (juce::Label::textColourId, dimText);
    addAndMakeVisible (hintLabel);

    setWantsKeyboardFocus (true);
    setSize (760, 360);
}

void SNSynthEditor::visibilityChanged()
{
    if (isShowing())
        keyboard.grabKeyboardFocus();
}

bool SNSynthEditor::keyPressed (const juce::KeyPress& key)
{
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    if (c == 'z') { shiftOctave (-1); return true; }
    if (c == 'x') { shiftOctave (+1); return true; }
    return false;
}

void SNSynthEditor::shiftOctave (int delta)
{
    const int newOctave = juce::jlimit (2, 8, baseOctave + delta);
    if (newOctave == baseOctave)
        return;

    // Release held notes so nothing hangs when the mapping moves.
    processor.keyboardState.allNotesOff (1);
    baseOctave = newOctave;
    keyboard.setKeyPressBaseOctave (baseOctave);

    // Bouncing focus clears the keyboard's record of held keys.
    grabKeyboardFocus();
    keyboard.grabKeyboardFocus();
    updateOctaveLabel();
}

void SNSynthEditor::updateOctaveLabel()
{
    // Shown with the common naming where middle C is C4.
    octaveLabel.setText ("Keys start at C" + juce::String (baseOctave - 1), juce::dontSendNotification);
}

void SNSynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    auto header = getLocalBounds().removeFromTop (56).reduced (20, 0);
    g.setColour (text);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("SN Synth", header, juce::Justification::centredLeft);

    g.setColour (dimText);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("v" + juce::String (JucePlugin_VersionString) + "  ·  step 1",
                header, juce::Justification::centredRight);

    g.setColour (panel);
    g.fillRoundedRectangle (juce::Rectangle<float> (20.0f, 64.0f, 140.0f, 150.0f), 8.0f);
}

void SNSynthEditor::resized()
{
    volumeLabel.setBounds (20, 70, 140, 20);
    volumeKnob.setBounds (35, 92, 110, 116);

    auto area = getLocalBounds().reduced (20);
    auto bottom = area.removeFromBottom (110);
    auto hintRow = bottom.removeFromTop (24);
    octaveLabel.setBounds (hintRow.removeFromRight (180));
    hintLabel.setBounds (hintRow);
    keyboard.setBounds (bottom.withTrimmedTop (4));
}
