#include "PluginEditor.h"
#include "Parameters.h"
#include "Filters.h"

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

    eqGroup.title = "EQ";
    eqGroup.knobs = { &makeKnob (eqLow, "Low"), &makeKnob (eqMid, "Mid"), &makeKnob (eqHigh, "High") };

    for (int i = 0; i < numSlots; ++i)
        setupSlot (slots[(size_t) i], i);

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
    setSize (1060, 690);
    startTimerHz (10);
}

void SNSynthEditor::setupSlot (SlotPanel& sp, int slotIndex)
{
    using namespace ParamIDs;
    sp.slotIndex = slotIndex;
    sp.group.title = "Filter " + juce::String (slotIndex + 1);

    const auto& names = Params::filterTypeNames();
    for (int t = 0; t < names.size(); ++t)
    {
        sp.type.addItem (FilterType::isAvailable (t) ? names[t] : names[t] + " (coming)", t + 1);
        sp.type.setItemEnabled (t + 1, FilterType::isAvailable (t));
    }
    sp.type.setWantsKeyboardFocus (false);
    sp.type.setColour (juce::ComboBox::backgroundColourId, background);
    sp.type.setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (sp.type);
    sp.typeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.parameters, slot (slotIndex, slotType), sp.type);

    sp.group.knobs = { &makeKnob (slot (slotIndex, slotCutoff), "Cutoff"),
                       &makeKnob (slot (slotIndex, slotResonance), "Resonance"),
                       &makeKnob (slot (slotIndex, slotDrive), "Drive"),
                       &makeKnob (slot (slotIndex, slotEnv), "Env Amt") };
    sp.characterKnob = &makeKnob (slot (slotIndex, slotCharacter), "Type Knob");
    sp.group.knobs.push_back (sp.characterKnob);

    for (auto* b : { &sp.moveLeft, &sp.moveRight })
    {
        b->setWantsKeyboardFocus (false);
        b->setColour (juce::TextButton::buttonColourId, background);
        addAndMakeVisible (*b);
    }
    sp.moveLeft.setTooltip ("Move this filter earlier in the chain");
    sp.moveRight.setTooltip ("Move this filter later in the chain");
    sp.moveLeft.onClick = [this, slotIndex]
    {
        auto order = processor.getSlotOrder();
        const int pos = (int) (std::find (order.begin(), order.end(), slotIndex) - order.begin());
        processor.moveSlot (pos, -1);
    };
    sp.moveRight.onClick = [this, slotIndex]
    {
        auto order = processor.getSlotOrder();
        const int pos = (int) (std::find (order.begin(), order.end(), slotIndex) - order.begin());
        processor.moveSlot (pos, +1);
    };

    updateSlotTypeDisplay (sp);
}

// The type knob does a different job for each filter type: relabel it.
void SNSynthEditor::updateSlotTypeDisplay (SlotPanel& sp)
{
    const int t = sp.type.getSelectedId() - 1;
    if (t == sp.shownType)
        return;
    sp.shownType = t;

    auto& k = *sp.characterKnob;
    juce::String label = "Type Knob";
    std::function<juce::String (double)> text;

    switch (t)
    {
        case FilterType::ota:
            label = "Slope";
            text = [] (double v) { return juce::String (v < 50.0 ? "24 dB" : "12 dB"); };
            break;
        case FilterType::sallenKey:
            label = "HP Cutoff";
            text = [] (double v)
            {
                const double hz = 20.0 * std::pow (100.0, v / 100.0);
                return hz < 1000.0 ? juce::String (juce::roundToInt (hz)) + " Hz" : juce::String (hz / 1000.0, 2) + " kHz";
            };
            break;
        case FilterType::lowpassGate:
            label = "Gate Decay";
            text = [] (double v)
            {
                const double sec = 0.12 * std::pow (25.0, v / 100.0);
                return sec < 1.0 ? juce::String (juce::roundToInt (sec * 1000.0)) + " ms" : juce::String (sec, 2) + " s";
            };
            break;
        default:
            text = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
            break;
    }

    k.label.setText (label, juce::dontSendNotification);
    k.slider.textFromValueFunction = text;
    k.slider.updateText();

    const bool active = t != FilterType::off && FilterType::isAvailable (t);
    for (auto* knob : sp.group.knobs)
    {
        knob->slider.setAlpha (active ? 1.0f : 0.35f);
        knob->label.setAlpha (active ? 1.0f : 0.35f);
    }
}

void SNSynthEditor::timerCallback()
{
    for (auto& sp : slots)
        updateSlotTypeDisplay (sp);

    if (processor.getSlotOrder() != shownOrder)
    {
        resized();
        repaint();
    }
}

Knob& SNSynthEditor::makeKnob (const juce::String& paramId, const juce::String& labelText)
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

void SNSynthEditor::layoutGroup (KnobGroup& group, juce::Rectangle<int> area, int topExtra)
{
    group.bounds = area;
    auto inner = area.reduced (6).withTrimmedTop (22 + topExtra);
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
    g.drawText ("v" + juce::String (JucePlugin_VersionString) + "  ·  step 3",
                header, juce::Justification::centredRight);

    for (auto* group : { &oscGroup, &filterEnvGroup, &outputGroup, &ampEnvGroup, &movementGroup, &eqGroup,
                         &slots[0].group, &slots[1].group, &slots[2].group })
    {
        g.setColour (panel);
        g.fillRoundedRectangle (group->bounds.toFloat(), 8.0f);
        g.setColour (dimText);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (group->title.toUpperCase(), group->bounds.reduced (10, 6).removeFromTop (16),
                    juce::Justification::centredLeft);
    }

    // Arrows between the filter slots show the order sound flows through them.
    g.setColour (accent);
    for (int pos = 0; pos < 2; ++pos)
    {
        const auto a = slots[(size_t) shownOrder[(size_t) pos]].group.bounds;
        const auto b = slots[(size_t) shownOrder[(size_t) pos + 1]].group.bounds;
        const float y = (float) a.getCentreY();
        juce::Path arrow;
        arrow.addArrow ({ (float) a.getRight() + 1.0f, y, (float) b.getX() - 1.0f, y }, 2.0f, 7.0f, 5.0f);
        g.fillPath (arrow);
    }
}

void SNSynthEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (44);

    const int knobW = 80, rowH = 150, gap = 10;

    // Row 1: oscillator and envelopes
    auto row1 = area.removeFromTop (rowH);
    layoutGroup (oscGroup,       row1.removeFromLeft (knobW * 4)); row1.removeFromLeft (gap);
    layoutGroup (filterEnvGroup, row1.removeFromLeft (knobW * 4)); row1.removeFromLeft (gap);
    layoutGroup (ampEnvGroup,    row1);

    // Row 2: the filter rack, slots drawn in processing order
    area.removeFromTop (gap);
    auto row2 = area.removeFromTop (rowH + 10);
    const int slotW = (row2.getWidth() - 2 * 14) / 3;
    shownOrder = processor.getSlotOrder();
    for (int pos = 0; pos < 3; ++pos)
    {
        auto& sp = slots[(size_t) shownOrder[(size_t) pos]];
        auto b = row2.removeFromLeft (slotW);
        if (pos < 2) row2.removeFromLeft (14);

        layoutGroup (sp.group, b, 26);
        auto header = b.reduced (8, 6).removeFromTop (20);
        header.removeFromLeft (70);
        sp.moveRight.setBounds (header.removeFromRight (22));
        header.removeFromRight (4);
        sp.moveLeft.setBounds (header.removeFromRight (22));
        header.removeFromRight (8);
        sp.type.setBounds (header.withY (header.getY() + 22).withHeight (22).withX (b.getX() + 8).withWidth (b.getWidth() - 16));

        sp.moveLeft.setEnabled (pos > 0);
        sp.moveRight.setEnabled (pos < 2);
    }

    // Row 3: movement, EQ, output
    area.removeFromTop (gap);
    auto row3 = area.removeFromTop (rowH);
    layoutGroup (movementGroup, row3.removeFromLeft (knobW * 4)); row3.removeFromLeft (gap);
    layoutGroup (eqGroup,       row3.removeFromLeft (knobW * 3)); row3.removeFromLeft (gap);
    layoutGroup (outputGroup,   row3.removeFromLeft (knobW + 20));

    auto bottom = area.removeFromBottom (110);
    auto hintRow = bottom.removeFromTop (24);
    octaveLabel.setBounds (hintRow.removeFromRight (180));
    hintLabel.setBounds (hintRow);
    keyboard.setBounds (bottom.withTrimmedTop (4));
}
