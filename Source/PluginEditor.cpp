#include "PluginEditor.h"
#include "Parameters.h"
#include "Filters.h"

namespace
{
    const juce::Colour background = Theme::paper;
    const juce::Colour panel      = Theme::panel;
    const juce::Colour accent     = Theme::accent;
    const juce::Colour text       = Theme::ink;
    const juce::Colour dimText    = Theme::dim;
}

SNSynthEditor::SNSynthEditor (SNSynthProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      scope1 (p, 0), scope2 (p, 1), scopeMix (p, 2), filterEnvShape (p, true), ampEnvShape (p, false), lfo1Shape (p, 0), lfo2Shape (p, 1), eqCurve (p),
      outputMeter (p), modList (*this),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    using namespace ParamIDs;

    setLookAndFeel (&look);
    content.painter = [this] (juce::Graphics& g) { paintContent (g); };
    addAndMakeVisible (content);

    oscGroup.title = "Oscillator 1";
    oscGroup.knobs = { &makeKnob (sineLevel, "Sine"), &makeKnob (sawLevel, "Saw"), &makeKnob (pulseLevel, "Pulse"),
                       &makeKnob (pulseWidth, "Pulse Width"), &makeKnob (osc1Coarse, "Coarse"),
                       &makeKnob (osc1Fine, "Fine"), &makeKnob (osc1Level, "Level") };

    osc2Group.title = "Oscillator 2";
    osc2Group.knobs = { &makeKnob (osc2Sine, "Sine"), &makeKnob (osc2Saw, "Saw"), &makeKnob (osc2Pulse, "Pulse"),
                        &makeKnob (osc2Pw, "Pulse Width"), &makeKnob (osc2Coarse, "Coarse"),
                        &makeKnob (osc2Fine, "Fine"), &makeKnob (osc2Level, "Level") };

    mixGroup.title = "Mix and Voices";
    mixGroup.knobs = { &makeKnob (subLevel, "Sub"), &makeKnob (fm1to2, "FM 1 > 2"), &makeKnob (fm2to1, "FM 2 > 1"),
                       &makeKnob (ringMod, "Ring Mod"), &makeKnob (voices, "Voices"),
                       &makeKnob (drift, "Drift"), &makeKnob (glide, "Glide") };

    playGroup.title = "Playing";
    playGroup.knobs = { &makeKnob (velocityAmount, "Velocity"), &makeKnob (keyTracking, "Key Track"),
                        &makeKnob (bendRange, "Bend Range") };

    filterEnvGroup.title = "Filter Envelope";
    filterEnvGroup.knobs = { &makeKnob (fenvAttack, "Attack"), &makeKnob (fenvDecay, "Decay"),
                             &makeKnob (fenvSustain, "Sustain"), &makeKnob (fenvRelease, "Release") };

    outputGroup.title = "Output";
    outputGroup.knobs = { &makeKnob (volume, "Volume") };

    ampEnvGroup.title = "Amp Envelope";
    ampEnvGroup.knobs = { &makeKnob (aenvAttack, "Attack"), &makeKnob (aenvDecay, "Decay"),
                          &makeKnob (aenvSustain, "Sustain"), &makeKnob (aenvRelease, "Release") };

    lfo1Group.title = "LFO 1";
    lfo1Group.knobs = { &makeKnob (lfoRate, "Rate"), &makeKnob (ParamIDs::lfo1Shape, "Shape"), &makeKnob (lfo1Smooth, "Smooth") };
    lfo2Group.title = "LFO 2";
    lfo2Group.knobs = { &makeKnob (lfo2Rate, "Rate"), &makeKnob (ParamIDs::lfo2Shape, "Shape"), &makeKnob (lfo2Smooth, "Smooth") };

    eqGroup.title = "EQ";
    eqGroup.knobs = { &makeKnob (eqLow, "Low"), &makeKnob (eqMid, "Mid"), &makeKnob (eqHigh, "High") };

    for (int i = 0; i < numSlots; ++i)
        setupSlot (slots[(size_t) i], i);

    // Live displays
    for (int i = 0; i < numSlots; ++i)
    {
        filterCurves[(size_t) i] = std::make_unique<Vis::FilterCurve> (p, i);
        slots[(size_t) i].group.display = filterCurves[(size_t) i].get();
    }
    oscGroup.display       = &scope1;
    osc2Group.display      = &scope2;
    mixGroup.display       = &scopeMix;
    filterEnvGroup.display = &filterEnvShape;
    ampEnvGroup.display    = &ampEnvShape;
    lfo1Group.display      = &lfo1Shape;
    lfo2Group.display      = &lfo2Shape;
    eqGroup.display        = &eqCurve;
    for (auto* group : { &oscGroup, &osc2Group, &mixGroup, &filterEnvGroup, &ampEnvGroup, &lfo1Group, &lfo2Group, &eqGroup,
                         &slots[0].group, &slots[1].group, &slots[2].group })
    {
        content.addAndMakeVisible (group->display);
        displays.push_back (group->display);
    }

    numbersButton.setWantsKeyboardFocus (false);
    numbersButton.setColour (juce::TextButton::buttonColourId, panel);
    numbersButton.setTooltip ("Open the Numbers view: the actual sample values at each stage");
    numbersButton.onClick = [this]
    {
        if (numbersWindow == nullptr)
        {
            numbersWindow = std::make_unique<Vis::NumbersWindow> (processor);
            numbersWindow->setLookAndFeel (&look);
        }
        numbersWindow->addKeyListener (this); // keep playing while the Numbers window is in front
        numbersWindow->setVisible (true);
        numbersWindow->toFront (false);
    };
    content.addAndMakeVisible (numbersButton);

    // Computer keyboard: A W S E D F T G Y H U J K O L P ; plays notes.
    // The computer keyboard is handled by the editor itself (see handleKeys), so
    // notes keep playing while you click knobs and menus.
    keyboard.clearKeyMappings();
    keyboard.setOctaveForMiddleC (4);
    keyboard.setAvailableRange (24, 108);
    keyboard.setKeyWidth (24.0f);
    keyboard.setLowestVisibleKey (48);
    keyboard.setWantsKeyboardFocus (false);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, Theme::panel.brighter (0.4f));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, Theme::ink);
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, Theme::grid.darker (0.2f));
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, Theme::accent);
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Theme::accent.withAlpha (0.25f));
    keyboard.setColour (juce::MidiKeyboardComponent::textLabelColourId, Theme::dim);
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, Theme::panel);
    keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, Theme::ink);
    content.addAndMakeVisible (keyboard);

    for (auto* b : { &octaveDown, &octaveUp })
    {
        b->setWantsKeyboardFocus (false);
        b->setColour (juce::TextButton::buttonColourId, panel);
        content.addAndMakeVisible (*b);
    }
    octaveDown.setTooltip ("Computer keyboard: octave down");
    octaveUp.setTooltip ("Computer keyboard: octave up");
    octaveDown.onClick = [this] { shiftOctave (-1); };
    octaveUp.onClick   = [this] { shiftOctave (+1); };

    octaveLabel.setColour (juce::Label::textColourId, accent);
    octaveLabel.setJustificationType (juce::Justification::centredRight);
    content.addAndMakeVisible (octaveLabel);
    updateOctaveLabel();

    updateHint();
    hintLabel.setColour (juce::Label::textColourId, dimText);
    content.addAndMakeVisible (hintLabel);

    outputMeter.setTooltip ("Output level. Bar = average, line = peak. The light turns red if the sound goes over 0 dB; click to reset.");
    content.addAndMakeVisible (outputMeter);

    limiterButton.setTooltip ("Output Limiter: keeps the output just under 0 dB by turning loud peaks down smoothly. "
                              "Even when off, a safety ceiling stops anything above -0.3 dB (it clips instead).");
    voiceCompButton.setTooltip ("Voice Gain Compensation: turns the sum down as more notes play, so chords don't jump in level");
    for (auto* b : { &limiterButton, &voiceCompButton })
    {
        b->setWantsKeyboardFocus (false);
        b->setColour (juce::ToggleButton::textColourId, text);
        b->setColour (juce::ToggleButton::tickColourId, accent);
        content.addAndMakeVisible (*b);
    }
    limiterAttachment   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.parameters, limiter, limiterButton);
    voiceCompAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.parameters, voiceGainComp, voiceCompButton);

    oversamplingBox.addItemList ({ "1x", "2x", "4x" }, 1);
    oversamplingBox.setWantsKeyboardFocus (false);
    oversamplingBox.setTooltip ("Filter Oversampling: the filters run 1, 2 or 4 times faster inside. Higher is cleaner "
                                "with lots of resonance or drive on high notes, and uses more CPU.");
    oversamplingBox.setColour (juce::ComboBox::backgroundColourId, background);
    content.addAndMakeVisible (oversamplingBox);
    oversamplingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.parameters, oversampling, oversamplingBox);
    oversamplingLabel.setText ("Filter oversampling", juce::dontSendNotification);
    oversamplingLabel.setColour (juce::Label::textColourId, dimText);
    oversamplingLabel.setFont (Theme::label (11.5f));
    content.addAndMakeVisible (oversamplingLabel);

    // Modulation sources: pick one, then drag any knob to connect it.
    modulateLabel.setText ("Modulate with", juce::dontSendNotification);
    modulateLabel.setColour (juce::Label::textColourId, dimText);
    modulateLabel.setFont (Theme::font (12.0f, true));
    modulateLabel.setJustificationType (juce::Justification::centredRight);
    content.addAndMakeVisible (modulateLabel);
    for (int src = 0; src < Mod::numSources; ++src)
    {
        auto& b = sourceButtons[(size_t) src];
        b.setButtonText (Mod::sourceName (src));
        b.setWantsKeyboardFocus (false);
        b.setClickingTogglesState (false);
        b.setColour (juce::TextButton::buttonColourId, panel);
        b.setColour (juce::TextButton::buttonOnColourId, Mod::sourceColour (src));
        b.setColour (juce::TextButton::textColourOffId, Mod::sourceColour (src));
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        b.setTooltip ("Click, then drag any knob to set how much " + juce::String (Mod::sourceName (src)) + " moves it");
        b.onClick = [this, src] { selectSource (modSource == src ? -1 : src); };
        content.addAndMakeVisible (b);
    }
    content.addAndMakeVisible (modList);
    content.addAndMakeVisible (chainStrip);

    heldNotes.fill (-1);
    setWantsKeyboardFocus (true);

    // The whole panel scales. Open at a size that fits the screen, and let
    // the corner be dragged (the shape stays the same).
    float scale = 1.0f;
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto user = display->userArea;
        scale = juce::jmin (1.0f, (float) (user.getHeight() - 70) / (float) baseH,
                                  (float) (user.getWidth() - 40) / (float) baseW);
    }
    setResizable (true, true);
    setResizeLimits (baseW / 2, baseH / 2, baseW * 2, baseH * 2);
    getConstrainer()->setFixedAspectRatio ((double) baseW / (double) baseH);
    setSize (juce::roundToInt ((float) baseW * scale), juce::roundToInt ((float) baseH * scale));
    startTimerHz (30);
}

SNSynthEditor::~SNSynthEditor()
{
    setLookAndFeel (nullptr);
    if (numbersWindow != nullptr)
        numbersWindow->setLookAndFeel (nullptr);
    if (listeningTo != nullptr)
        listeningTo->removeKeyListener (this);
    if (numbersWindow != nullptr)
        numbersWindow->removeKeyListener (this);
    processor.keyboardState.allNotesOff (1);
}

void SNSynthEditor::setupSlot (SlotPanel& sp, int slotIndex)
{
    using namespace ParamIDs;
    sp.slotIndex = slotIndex;
    sp.group.title = "Filter " + juce::String (slotIndex + 1);

    const auto& names = Params::filterTypeNames();
    for (int t = 0; t < names.size(); ++t)
    {
        sp.type.addItem (names[t], t + 1);
        sp.type.setItemEnabled (t + 1, FilterType::isAvailable (t));
    }
    sp.type.setWantsKeyboardFocus (false);
    sp.type.setColour (juce::ComboBox::backgroundColourId, background);
    content.addAndMakeVisible (sp.type);
    sp.typeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.parameters, slot (slotIndex, slotType), sp.type);

    sp.group.knobs = { &makeKnob (slot (slotIndex, slotCutoff), "Cutoff"),
                       &makeKnob (slot (slotIndex, slotResonance), "Resonance"),
                       &makeKnob (slot (slotIndex, slotDrive), "Drive"),
                       &makeKnob (slot (slotIndex, slotEnv), "Env Amt") };
    sp.characterKnob = &makeKnob (slot (slotIndex, slotCharacter), "Type Knob");
    sp.group.knobs.push_back (sp.characterKnob);

    sp.levelMatch.setWantsKeyboardFocus (false);
    sp.levelMatch.setColour (juce::ToggleButton::textColourId, dimText);
    sp.levelMatch.setColour (juce::ToggleButton::tickColourId, accent);
    sp.levelMatch.setTooltip ("Level Match: evens out the loudness between filter types (some are naturally louder or quieter). "
                              "Turn off to hear each type's natural level.");
    content.addAndMakeVisible (sp.levelMatch);
    sp.levelMatchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.parameters, slot (slotIndex, slotLevelMatch), sp.levelMatch);

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
        case FilterType::steinerParker:
            label = "Mode";
            text = [] (double v) { const char* m[] = { "Low-pass", "Band-pass", "High-pass" }; return juce::String (m[FilterShape::steinerMode ((float) v / 100.0f)]); };
            break;
        case FilterType::polivoks:
            label = "Mode";
            text = [] (double v) { return juce::String (FilterShape::polivoksMode ((float) v / 100.0f) == 0 ? "Low-pass" : "Band-pass"); };
            break;
        case FilterType::comb:
            label = "Polarity";
            text = [] (double v) { return juce::String (v < 50.0 ? "+ bright" : "- hollow"); };
            break;
        case FilterType::formant:
            label = "Vowel";
            text = [] (double v) { const char* m[] = { "A", "E", "I", "O", "U" }; return juce::String (m[FilterShape::formantVowel ((float) v / 100.0f)]); };
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
    // Catch key releases that happened while a menu or another window had focus.
    updateHeldKeys();
    // Take the keys back only if our own window is already the active one,
    // so the synth never pulls focus away from other apps' windows.
    if (isShowing() && juce::Component::getCurrentlyFocusedComponent() == nullptr)
        if (auto* peer = getPeer(); peer != nullptr && peer->isFocused() && juce::Process::isForegroundProcess())
            grabKeyboardFocus();

    for (auto* d : displays)
        d->repaint();
    outputMeter.tick();
    content.repaint (0, 0, baseW, 60); // "n playing" in the header

    // Redraw rings and the list when routings change (also from loading a project).
    float sum = 0.0f;
    for (int src = 0; src < Mod::numSources; ++src)
        for (int t = 0; t < Mod::numTargets; ++t)
            sum += processor.modMatrix.get (src, t) * (float) (1 + src * Mod::numTargets + t);
    if (std::abs (sum - matrixChecksum) > 1.0e-6f)
    {
        matrixChecksum = sum;
        modulationChanged();
    }
    // Rings follow their knob's value; cheap to repaint the ones in use.
    for (auto* r : rings)
        r->repaint();

    for (auto& sp : slots)
        updateSlotTypeDisplay (sp);

    chainStrip.repaint();
}

Knob& SNSynthEditor::makeKnob (const juce::String& paramId, const juce::String& labelText)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 18);
    k->slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    k->slider.setColour (juce::Slider::thumbColourId, text);
    k->slider.setColour (juce::Slider::textBoxTextColourId, text);
    k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k->slider.setWantsKeyboardFocus (false);
    content.addAndMakeVisible (k->slider);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.parameters, paramId, k->slider);

    const int target = Mod::targetForParam (paramId);
    if (target >= 0)
    {
        k->ring = std::make_unique<ModRing> (*this, *k, target);
        content.addAndMakeVisible (*k->ring);
        rings.push_back (k->ring.get());
    }

    k->label.setText (labelText, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setColour (juce::Label::textColourId, dimText);
    k->label.setFont (Theme::label (11.5f));
    content.addAndMakeVisible (k->label);

    knobs.push_back (std::move (k));
    return *knobs.back();
}

void SNSynthEditor::layoutGroup (KnobGroup& group, juce::Rectangle<int> area, int topExtra)
{
    group.bounds = area;
    auto inner = area.reduced (6).withTrimmedTop (22 + topExtra);
    if (group.display != nullptr)
    {
        group.display->setBounds (inner.removeFromTop (54).reduced (4, 0));
        inner.removeFromTop (6);
    }
    const int w = inner.getWidth() / (int) group.knobs.size();
    for (auto* k : group.knobs)
    {
        auto cell = inner.removeFromLeft (w);
        k->label.setBounds (cell.removeFromTop (16));
        k->slider.setBounds (cell.reduced (4, 0));
        if (k->ring != nullptr)
            k->ring->setBounds (k->slider.getBounds());
    }
}

void SNSynthEditor::visibilityChanged()
{
    if (isShowing())
        grabKeyboardFocus();
}

void SNSynthEditor::parentHierarchyChanged()
{
    // Also listen at the window level, so keys still play whatever was clicked last.
    auto* top = getTopLevelComponent();
    if (top != listeningTo)
    {
        if (listeningTo != nullptr) listeningTo->removeKeyListener (this);
        listeningTo = top != this ? top : nullptr;
        if (listeningTo != nullptr) listeningTo->addKeyListener (this);
    }
}

// FL Studio / tracker layout, two rows:
//   lower:  Z S X D C V G B H N J M , L . ; /   (Z = C3)
//   upper:  Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P [ = ]   (Q = C4)
const SNSynthEditor::NoteKey SNSynthEditor::noteKeys[] = {
    { 'z', 0 }, { 's', 1 }, { 'x', 2 }, { 'd', 3 }, { 'c', 4 }, { 'v', 5 }, { 'g', 6 }, { 'b', 7 },
    { 'h', 8 }, { 'n', 9 }, { 'j', 10 }, { 'm', 11 }, { ',', 12 }, { 'l', 13 }, { '.', 14 }, { ';', 15 }, { '/', 16 },
    { 'q', 12 }, { '2', 13 }, { 'w', 14 }, { '3', 15 }, { 'e', 16 }, { 'r', 17 }, { '5', 18 }, { 't', 19 },
    { '6', 20 }, { 'y', 21 }, { '7', 22 }, { 'u', 23 }, { 'i', 24 }, { '9', 25 }, { 'o', 26 }, { '0', 27 },
    { 'p', 28 }, { '[', 29 }, { '=', 30 }, { ']', 31 } };

bool SNSynthEditor::keyPressed (const juce::KeyPress& key)
{
    return handleKeyPress (key);
}

bool SNSynthEditor::keyStateChanged (bool)
{
    return updateHeldKeys();
}

bool SNSynthEditor::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    return handleKeyPress (key);
}

bool SNSynthEditor::keyStateChanged (bool, juce::Component*)
{
    return updateHeldKeys();
}

bool SNSynthEditor::handleKeyPress (const juce::KeyPress& key)
{
    // Leave shortcuts such as Cmd+Q to the app.
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;

    updateHeldKeys();

    // Swallow every other plain key, so macOS doesn't play its "bonk" sound.
    return true;
}

bool SNSynthEditor::updateHeldKeys()
{
    bool any = false;
    for (size_t i = 0; i < numNoteKeys; ++i)
    {
        const bool down = juce::KeyPress ((int) noteKeys[i].key).isCurrentlyDown();
        auto& held = heldNotes[i];

        if (down && held < 0)
        {
            held = 12 * (baseOctave + 1) + noteKeys[i].semitone; // octave 3: Z = C3 = MIDI 48
            if (held <= 127)
                processor.keyboardState.noteOn (1, held, (float) velocity / 127.0f);
            any = true;
        }
        else if (! down && held >= 0)
        {
            if (held <= 127)
                processor.keyboardState.noteOff (1, held, 0.0f);
            held = -1;
            any = true;
        }
    }
    return any;
}

void SNSynthEditor::shiftOctave (int delta)
{
    // Notes already held keep playing until their key is released.
    baseOctave = juce::jlimit (0, 6, baseOctave + delta);
    updateOctaveLabel();
}


void SNSynthEditor::updateOctaveLabel()
{
    // Shown with the common naming where middle C is C4.
    octaveLabel.setText ("Z = C" + juce::String (baseOctave) + ", Q = C" + juce::String (baseOctave + 1),
                         juce::dontSendNotification);
}

void SNSynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void SNSynthEditor::paintContent (juce::Graphics& g)
{
    g.fillAll (background);

    auto header = content.getLocalBounds().removeFromTop (60).reduced (20, 0);
    g.setColour (text);
    g.setFont (Theme::font (26.0f, true));
    g.drawText ("SN Synth", header, juce::Justification::centredLeft);

    g.setColour (dimText);
    g.setFont (Theme::value (12.0f));
    g.drawText (juce::String (processor.getActiveVoiceCount()) + " playing",
                header.withTrimmedRight (104), juce::Justification::centredRight);

    // Strict grid: each panel is a short ink bar and a flush-left title.
    KnobGroup modGroup;
    modGroup.title = "Modulation";
    modGroup.bounds = modListBounds;
    for (auto* group : { &oscGroup, &osc2Group, &playGroup, &mixGroup, &filterEnvGroup, &ampEnvGroup,
                         &slots[0].group, &slots[1].group, &slots[2].group,
                         &lfo1Group, &lfo2Group, &eqGroup, &outputGroup, &modGroup })
    {
        auto r = group->bounds;
        g.setColour (text);
        g.fillRect (r.getX() + 10, r.getY(), 24, 3);
        g.setFont (Theme::title (13.0f));
        g.drawText (group->title, r.reduced (10, 0).withTrimmedTop (6).removeFromTop (16), juce::Justification::centredLeft);
    }
}

void SNSynthEditor::resized()
{
    const float scale = (float) getWidth() / (float) baseW;
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (0, 0, baseW, baseH);
    layoutContent();
}

void SNSynthEditor::layoutContent()
{
    auto area = content.getLocalBounds().reduced (20);
    area.removeFromTop (44);

    numbersButton.setBounds (baseW - 20 - 90, 26, 90, 24);

    // Header: modulation sources
    {
        juce::Rectangle<int> strip (180, 26, 720, 24);
        modulateLabel.setBounds (strip.removeFromLeft (108));
        strip.removeFromLeft (6);
        const int w = strip.getWidth() / Mod::numSources;
        for (auto& b : sourceButtons)
            b.setBounds (strip.removeFromLeft (w).reduced (2, 0));
    }

    const int rowH = 180, gap = 8;

    // Row 1: the two oscillators and playing settings
    auto row1 = area.removeFromTop (rowH);
    const int unit1 = (row1.getWidth() - 2 * gap) / 17;
    layoutGroup (oscGroup,  row1.removeFromLeft (unit1 * 7)); row1.removeFromLeft (gap);
    layoutGroup (osc2Group, row1.removeFromLeft (unit1 * 7)); row1.removeFromLeft (gap);
    layoutGroup (playGroup, row1);

    // Row 2: mix and voices, envelopes
    area.removeFromTop (gap);
    auto row2a = area.removeFromTop (rowH);
    const int unit = (row2a.getWidth() - 2 * gap) / 15;
    layoutGroup (mixGroup,       row2a.removeFromLeft (unit * 7)); row2a.removeFromLeft (gap);
    layoutGroup (filterEnvGroup, row2a.removeFromLeft (unit * 4)); row2a.removeFromLeft (gap);
    layoutGroup (ampEnvGroup,    row2a);

    // The chain: drag blocks to change the processing order.
    area.removeFromTop (gap);
    chainStrip.setBounds (area.removeFromTop (34));

    // Row 3: the three filter slots
    area.removeFromTop (gap);
    auto row2 = area.removeFromTop (rowH + 26);
    const int slotW = (row2.getWidth() - 2 * gap) / 3;
    for (int i = 0; i < 3; ++i)
    {
        auto& sp = slots[(size_t) i];
        auto b = row2.removeFromLeft (slotW);
        if (i < 2) row2.removeFromLeft (gap);

        layoutGroup (sp.group, b, 26);
        auto header = b.reduced (8, 6).removeFromTop (20);
        sp.levelMatch.setBounds (header.removeFromRight (70));
        sp.type.setBounds (header.withY (header.getY() + 22).withHeight (22).withX (b.getX() + 8).withWidth (b.getWidth() - 16));
    }

    // Row 4: LFOs, EQ, output (Volume, meter, switches), modulation list
    area.removeFromTop (gap);
    auto row3 = area.removeFromTop (rowH);
    const int knobW = 64;
    layoutGroup (lfo1Group, row3.removeFromLeft (knobW * 3)); row3.removeFromLeft (gap);
    layoutGroup (lfo2Group, row3.removeFromLeft (knobW * 3)); row3.removeFromLeft (gap);
    layoutGroup (eqGroup,   row3.removeFromLeft (knobW * 3)); row3.removeFromLeft (gap);
    auto out = row3.removeFromLeft (250);
    row3.removeFromLeft (gap);
    outputGroup.bounds = out;
    {
        auto inner = out.reduced (6).withTrimmedTop (22);
        auto knobArea = inner.removeFromLeft (76);
        auto* vol = outputGroup.knobs[0];
        vol->label.setBounds (knobArea.removeFromTop (16));
        vol->slider.setBounds (knobArea.removeFromTop (100).reduced (2, 0));
        if (vol->ring != nullptr) vol->ring->setBounds (vol->slider.getBounds());
        outputMeter.setBounds (inner.removeFromLeft (64).reduced (4, 0));
        inner.removeFromLeft (6);
        limiterButton.setBounds (inner.removeFromTop (24));
        voiceCompButton.setBounds (inner.removeFromTop (24));
        inner.removeFromTop (10);
        oversamplingLabel.setBounds (inner.removeFromTop (18));
        oversamplingBox.setBounds (inner.removeFromTop (24).withWidth (70));
    }
    modListBounds = row3;
    modList.setBounds (row3.reduced (6).withTrimmedTop (22));

    area.removeFromTop (gap);
    auto bottom = area;
    auto hintRow = bottom.removeFromTop (24);
    octaveUp.setBounds (hintRow.removeFromRight (26).reduced (0, 1));
    hintRow.removeFromRight (4);
    octaveDown.setBounds (hintRow.removeFromRight (26).reduced (0, 1));
    hintRow.removeFromRight (8);
    octaveLabel.setBounds (hintRow.removeFromRight (150));
    hintLabel.setBounds (hintRow);
    keyboard.setBounds (bottom.withTrimmedTop (4));
}

//==============================================================================
// Modulation
void SNSynthEditor::selectSource (int source)
{
    modSource = source;
    for (int s = 0; s < Mod::numSources; ++s)
        sourceButtons[(size_t) s].setToggleState (s == modSource, juce::dontSendNotification);
    for (auto* r : rings)
    {
        r->setInterceptsMouseClicks (modSource >= 0, false);
        r->setMouseCursor (modSource >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
        r->repaint();
    }
    updateHint();
}

void SNSynthEditor::updateHint()
{
    if (modSource >= 0)
    {
        const juce::String name (Mod::sourceName (modSource));
        hintLabel.setText ("Assigning " + name + ": drag any knob up or down to set how much " + name
                               + " moves it. Double-click a knob to disconnect. Click " + name + " again when done.",
                           juce::dontSendNotification);
        hintLabel.setColour (juce::Label::textColourId, Mod::sourceColour (modSource));
    }
    else
    {
        hintLabel.setText ("Computer keyboard (FL Studio layout): Z S X D C V ... lower octave, Q 2 W 3 E R ... upper octave.",
                           juce::dontSendNotification);
        hintLabel.setColour (juce::Label::textColourId, dimText);
    }
}

juce::String SNSynthEditor::targetName (int target) const
{
    if (auto* param = processor.parameters.getParameter (Mod::targetIds()[(size_t) target]))
        return param->getName (60);
    return {};
}

void SNSynthEditor::modulationChanged()
{
    for (auto* r : rings)
        r->repaint();
    modList.refresh();
}

//==============================================================================
namespace
{
    struct RingGeometry { juce::Point<float> centre; float radius, start, end; };

    // Matches where JUCE's rotary knob draws its arc.
    RingGeometry ringGeometry (const juce::Slider& slider, juce::Rectangle<int> local)
    {
        auto knobArea = local.withTrimmedBottom (slider.getTextBoxHeight()).toFloat().reduced (10.0f);
        const float radius = juce::jmin (knobArea.getWidth(), knobArea.getHeight()) * 0.5f;
        const auto p = slider.getRotaryParameters();
        return { knobArea.getCentre(), radius, p.startAngleRadians, p.endAngleRadians };
    }
}

void ModRing::paint (juce::Graphics& g)
{
    auto& matrix = editor.getProcessor().modMatrix;
    const auto geo = ringGeometry (knob.slider, getLocalBounds());
    const float value = (float) knob.slider.valueToProportionOfLength (knob.slider.getValue());
    const int selected = editor.getSelectedSource();

    // While assigning, show every knob that can take the source.
    if (selected >= 0)
    {
        g.setColour (Mod::sourceColour (selected).withAlpha (0.6f));
        g.drawEllipse (juce::Rectangle<float> (geo.radius * 2.0f + 6.0f, geo.radius * 2.0f + 6.0f).withCentre (geo.centre), 1.0f);
    }

    int ringIndex = 0;
    for (int s = 0; s < Mod::numSources; ++s)
    {
        const float amount = matrix.get (s, target);
        if (std::abs (amount) < 1.0e-5f)
            continue;

        // The arc runs from the knob's position to where the source can push it.
        // Cutoff is moved in octaves, so convert to knob travel for the picture.
        float reach = amount;
        if (Mod::isCutoff (target))
            reach = amount * Mod::cutoffOctavesAtFull / std::log2 (20000.0f / 20.0f);
        const float from = juce::jlimit (0.0f, 1.0f, value);
        const float to   = juce::jlimit (0.0f, 1.0f, value + reach);
        const float lo   = Mod::isBipolar (s) ? juce::jlimit (0.0f, 1.0f, value - reach) : from;

        const float r = geo.radius + 4.0f + 4.5f * (float) ringIndex++;
        juce::Path arc;
        const float a0 = geo.start + juce::jmin (lo, to) * (geo.end - geo.start);
        const float a1 = geo.start + juce::jmax (lo, to) * (geo.end - geo.start);
        arc.addCentredArc (geo.centre.x, geo.centre.y, r, r, 0.0f, a0, juce::jmax (a1, a0 + 0.02f), true);
        g.setColour (Mod::sourceColour (s).withAlpha (s == selected || selected < 0 ? 1.0f : 0.45f));
        g.strokePath (arc, juce::PathStrokeType (s == selected ? 4.0f : 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (dragging && selected >= 0)
    {
        const float amount = matrix.get (selected, target);
        g.setColour (Theme::ink);
        auto box = juce::Rectangle<float> (54.0f, 18.0f).withCentre (geo.centre);
        g.fillRect (box);
        g.setColour (Mod::sourceColour (selected));
        g.setFont (12.0f);
        g.drawText ((amount > 0.0f ? "+" : "") + juce::String (juce::roundToInt (amount * 100.0f)) + "%", box, juce::Justification::centred);
    }
}

void ModRing::mouseDown (const juce::MouseEvent&)
{
    const int s = editor.getSelectedSource();
    if (s < 0) return;
    dragStartAmount = editor.getProcessor().modMatrix.get (s, target);
    dragging = true;
    repaint();
}

void ModRing::mouseDrag (const juce::MouseEvent& e)
{
    const int s = editor.getSelectedSource();
    if (s < 0) return;
    const float speed = e.mods.isShiftDown() ? 600.0f : 150.0f; // Shift = fine
    editor.getProcessor().modMatrix.set (s, target, dragStartAmount - (float) e.getDistanceFromDragStartY() / speed);
    editor.modulationChanged();
}

void ModRing::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    repaint();
}

void ModRing::mouseDoubleClick (const juce::MouseEvent&)
{
    const int s = editor.getSelectedSource();
    if (s < 0) return;
    editor.getProcessor().modMatrix.set (s, target, 0.0f);
    editor.modulationChanged();
}

//==============================================================================
void ModList::refresh()
{
    rows.clear();
    auto& m = editor.getProcessor().modMatrix;
    for (int s = 0; s < Mod::numSources; ++s)
        for (int t = 0; t < Mod::numTargets; ++t)
        {
            const float a = m.get (s, t);
            if (std::abs (a) > 1.0e-5f)
                rows.push_back ({ s, t, a });
        }
    scroll = juce::jlimit (0, juce::jmax (0, (int) rows.size() - getHeight() / rowH), scroll);
    repaint();
}

void ModList::paint (juce::Graphics& g)
{
    g.setFont (Theme::label (12.0f));
    if (rows.empty())
    {
        g.setColour (dimText);
        g.drawFittedText ("Nothing connected yet.\nPick a source at the top (LFO 1, LFO 2, ...) and drag any knob.",
                          getLocalBounds().reduced (4), juce::Justification::centredLeft, 4);
        return;
    }

    const int visible = getHeight() / rowH;
    for (int i = 0; i < visible && i + scroll < (int) rows.size(); ++i)
    {
        const auto& row = rows[(size_t) (i + scroll)];
        auto r = juce::Rectangle<int> (0, i * rowH, getWidth(), rowH);
        if (i % 2 == 0)
        {
            g.setColour (background.withAlpha (0.5f));
            g.fillRect (r);
        }
        auto chip = r.removeFromLeft (14).toFloat().withSizeKeepingCentre (8.0f, 8.0f);
        g.setColour (Mod::sourceColour (row.source));
        g.fillEllipse (chip);

        auto remove = r.removeFromRight (20);
        g.setColour (dimText);
        g.drawText (juce::String::fromUTF8 ("\xc3\x97"), remove, juce::Justification::centred);

        auto amountArea = r.removeFromRight (48);
        g.setColour (text);
        g.drawText ((row.amount > 0.0f ? "+" : "") + juce::String (juce::roundToInt (row.amount * 100.0f)) + "%",
                    amountArea, juce::Justification::centredRight);

        g.setColour (text);
        g.drawText (juce::String (Mod::sourceName (row.source)) + juce::String::fromUTF8 ("  \xe2\x86\x92  ") + editor.targetName (row.target),
                    r.withTrimmedLeft (4), juce::Justification::centredLeft, true);
    }

    if ((int) rows.size() > visible)
    {
        g.setColour (dimText);
        g.drawText (juce::String (rows.size()) + " routings, scroll for more", getLocalBounds().removeFromBottom (14),
                    juce::Justification::centredRight);
    }
}

void ModList::mouseUp (const juce::MouseEvent& e)
{
    const int i = e.y / rowH + scroll;
    if (i < 0 || i >= (int) rows.size())
        return;
    const auto row = rows[(size_t) i];
    if (e.x > getWidth() - 20)
    {
        editor.getProcessor().modMatrix.set (row.source, row.target, 0.0f); // remove
        editor.modulationChanged();
    }
}

void ModList::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    scroll = juce::jlimit (0, juce::jmax (0, (int) rows.size() - getHeight() / rowH), scroll + (w.deltaY < 0 ? 1 : -1));
    repaint();
}

//==============================================================================
// Chain strip
namespace
{
    constexpr float chainEndW = 110.0f, chainArrowW = 26.0f;
}

juce::Rectangle<float> ChainStrip::blockBounds (int position) const
{
    auto r = getLocalBounds().toFloat();
    const float inner = r.getWidth() - 2.0f * chainEndW - 6.0f * chainArrowW;
    const float w = inner / (float) Chain::numBlocks;
    return { r.getX() + chainEndW + chainArrowW + (float) position * (w + chainArrowW), r.getY() + 3.0f, w, r.getHeight() - 6.0f };
}

int ChainStrip::positionAt (float x) const
{
    int best = 0;
    float bestDist = 1.0e9f;
    for (int p = 0; p < Chain::numBlocks; ++p)
    {
        const float d = std::abs (blockBounds (p).getCentreX() - x);
        if (d < bestDist) { bestDist = d; best = p; }
    }
    return best;
}

void ChainStrip::paint (juce::Graphics& g)
{
    const auto order = processor.getChainOrder();
    auto r = getLocalBounds().toFloat();
    const auto& typeNames = Params::filterTypeNames();

    auto endBox = [&] (juce::Rectangle<float> b, const juce::String& t)
    {
        g.setColour (dimText);
        g.setFont (Theme::font (12.0f, true));
        g.drawText (t, b, juce::Justification::centred);
    };
    endBox (r.withWidth (chainEndW), "Oscillators");
    endBox (r.withLeft (r.getRight() - chainEndW), "Volume");

    for (int pos = 0; pos <= Chain::numBlocks; ++pos)
    {
        const float x1 = pos < Chain::numBlocks ? blockBounds (pos).getX() : r.getRight() - chainEndW;
        juce::Path a;
        a.addArrow ({ x1 - chainArrowW + 5.0f, r.getCentreY(), x1 - 5.0f, r.getCentreY() }, 1.0f, 6.0f, 5.0f);
        g.setColour (text);
        g.fillPath (a);
    }

    for (int pos = 0; pos < Chain::numBlocks; ++pos)
    {
        const int b = order[(size_t) pos];
        auto box = blockBounds (pos);
        if (pos == dragFrom)
            box = box.withX (juce::jlimit (blockBounds (0).getX(), blockBounds (Chain::numBlocks - 1).getX(), dragX - box.getWidth() * 0.5f));

        juce::String label = Chain::blockName (b);
        bool off = false;
        if (Chain::isFilter (b))
        {
            const int type = (int) processor.parameters.getRawParameterValue (ParamIDs::slot (b, ParamIDs::slotType))->load();
            label << "   " << typeNames[type];
            off = type == FilterType::off;
        }

        g.setColour (pos == dragFrom ? accent : Theme::screen);
        g.fillRect (box);
        g.setColour (pos == dragFrom ? juce::Colours::white : (off ? dimText : text));
        g.setFont (Theme::font (12.0f, ! off));
        g.drawText (label, box.reduced (10.0f, 0.0f), juce::Justification::centredLeft, true);
    }

    if (dragFrom >= 0 && dropAt >= 0 && dropAt != dragFrom)
    {
        g.setColour (accent);
        g.drawRect (blockBounds (dropAt).reduced (1.0f), 2.0f);
    }
}

void ChainStrip::mouseMove (const juce::MouseEvent& e)
{
    bool onBlock = false;
    for (int p = 0; p < Chain::numBlocks; ++p)
        onBlock = onBlock || blockBounds (p).contains (e.position);
    setMouseCursor (onBlock ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void ChainStrip::mouseDown (const juce::MouseEvent& e)
{
    dragFrom = -1;
    for (int p = 0; p < Chain::numBlocks; ++p)
        if (blockBounds (p).contains (e.position))
            dragFrom = p;
    dragX = e.position.x;
    dropAt = dragFrom;
    repaint();
}

void ChainStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0) return;
    dragX = e.position.x;
    dropAt = positionAt (dragX);
    repaint();
}

void ChainStrip::mouseUp (const juce::MouseEvent&)
{
    if (dragFrom >= 0 && dropAt >= 0 && dropAt != dragFrom)
        processor.moveBlock (dragFrom, dropAt);
    dragFrom = dropAt = -1;
    repaint();
}
