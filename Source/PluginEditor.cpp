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
    using namespace ParamIDs;

    oscGroup.title = "Oscillator";
    oscGroup.knobs = { &makeKnob (sawLevel, "Saw"), &makeKnob (pulseLevel, "Pulse"),
                       &makeKnob (pulseWidth, "Pulse Width"), &makeKnob (subLevel, "Sub") };

    filterEnvGroup.title = "Filter Envelope";
    filterEnvGroup.knobs = { &makeKnob (fenvAttack, "Attack"), &makeKnob (fenvDecay, "Decay"),
                             &makeKnob (fenvSustain, "Sustain"), &makeKnob (fenvRelease, "Release") };

    outputGroup.title = "Output";
    outputGroup.knobs = { &makeKnob (volume, "Volume") };

    ampEnvGroup.title = "Amp Envelope";
    ampEnvGroup.knobs = { &makeKnob (aenvAttack, "Attack"), &makeKnob (aenvDecay, "Decay"),
                          &makeKnob (aenvSustain, "Sustain"), &makeKnob (aenvRelease, "Release") };

    movementGroup.title = "Movement";
    movementGroup.knobs = { &makeKnob (lfoRate, "LFO Rate"), &makeKnob (lfoCutoff, "LFO > Cutoff"),
                            &makeKnob (drift, "Drift"), &makeKnob (glide, "Glide") };

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
    setSize (860, 530);
}

Knob& SNSynthEditor::makeKnob (const char* paramId, const juce::String& labelText)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 18);
    k->slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    k->slider.setColour (juce::Slider::thumbColourId, text);
    k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k->slider.setWantsKeyboardFocus (false);
    addAndMakeVisible (k->slider);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.parameters, paramId, k->slider);

    k->label.setText (labelText, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setColour (juce::Label::textColourId, text);
    k->label.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (k->label);

    knobs.push_back (std::move (k));
    return *knobs.back();
}

void SNSynthEditor::layoutGroup (KnobGroup& group, juce::Rectangle<int> area)
{
    group.bounds = area;
    auto inner = area.reduced (6).withTrimmedTop (22);
    const int w = inner.getWidth() / (int) group.knobs.size();
    for (auto* k : group.knobs)
    {
        auto cell = inner.removeFromLeft (w);
        k->label.setBounds (cell.removeFromTop (18));
        k->slider.setBounds (cell.reduced (4, 0));
    }
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
    g.drawText ("v" + juce::String (JucePlugin_VersionString) + "  ·  step 2",
                header, juce::Justification::centredRight);

    for (auto* group : { &oscGroup, &filterEnvGroup, &outputGroup, &ampEnvGroup, &movementGroup })
    {
        g.setColour (panel);
        g.fillRoundedRectangle (group->bounds.toFloat(), 8.0f);
        g.setColour (dimText);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (group->title.toUpperCase(), group->bounds.reduced (10, 6).removeFromTop (16),
                    juce::Justification::centredLeft);
    }
}

void SNSynthEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (44);

    const int knobW = 84, rowH = 150, gap = 10;

    auto row1 = area.removeFromTop (rowH);
    layoutGroup (oscGroup,       row1.removeFromLeft (knobW * 4)); row1.removeFromLeft (gap);
    layoutGroup (filterEnvGroup, row1.removeFromLeft (knobW * 4)); row1.removeFromLeft (gap);
    layoutGroup (outputGroup,    row1);

    area.removeFromTop (gap);
    auto row2 = area.removeFromTop (rowH);
    layoutGroup (ampEnvGroup,   row2.removeFromLeft (knobW * 4)); row2.removeFromLeft (gap);
    layoutGroup (movementGroup, row2.removeFromLeft (knobW * 4));

    auto bottom = area.removeFromBottom (110);
    auto hintRow = bottom.removeFromTop (24);
    octaveLabel.setBounds (hintRow.removeFromRight (180));
    hintLabel.setBounds (hintRow);
    keyboard.setBounds (bottom.withTrimmedTop (4));
}
