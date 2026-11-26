#pragma once

#include "PluginProcessor.h"
#include "Visualisers.h"
#include "Theme.h"

class SNSynthEditor;
struct Knob;

// Drawn over a knob: a coloured arc for each modulation source connected to
// it, showing how far that source can move the knob. While a source is
// selected in the header, dragging here sets that source's amount.
class ModRing : public juce::Component
{
public:
    ModRing (SNSynthEditor& e, Knob& k, int targetIndex) : target (targetIndex), editor (e), knob (k)
    {
        setWantsKeyboardFocus (false);
        setInterceptsMouseClicks (false, false);
    }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    const int target;

private:
    SNSynthEditor& editor;
    Knob& knob;
    float dragStartAmount = 0.0f;
    bool dragging = false;
};

// One labelled knob attached to a parameter.
struct Knob
{
    juce::Slider slider;
    juce::Label  label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    std::unique_ptr<ModRing> ring; // only for knobs that can be modulated
};

// The processing order: Oscillators -> five movable blocks -> Volume.
// Drag a block left or right to change where it runs.
class ChainStrip : public juce::Component
{
public:
    explicit ChainStrip (SNSynthProcessor& p) : processor (p) { setWantsKeyboardFocus (false); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> blockBounds (int position) const;
    int positionAt (float x) const;
    SNSynthProcessor& processor;
    int dragFrom = -1, dropAt = -1;
    float dragX = 0.0f;
};

// The list of every active routing, with a remove button on each.
class ModList : public juce::Component
{
public:
    explicit ModList (SNSynthEditor& e) : editor (e) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void refresh(); // rebuild the rows if any amount changed

private:
    struct Row { int source, target; float amount; };
    SNSynthEditor& editor;
    std::vector<Row> rows;
    int scroll = 0;
    static constexpr int rowH = 20;
};

// The drum machine's 16-step grid: one row per drum. Click a step to turn
// it on or off; drag across to paint several. The step playing now is lit.
class DrumGrid : public juce::Component
{
public:
    explicit DrumGrid (SNSynthProcessor& p) : processor (p) { setWantsKeyboardFocus (false); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    juce::Rectangle<float> cellBounds (int drum, int step) const;

private:
    bool cellAt (juce::Point<float> pos, int& drum, int& step) const;
    SNSynthProcessor& processor;
    bool paintOn = true;
    int lastDrum = -1, lastStep = -1;
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
    juce::ToggleButton levelMatch { "Match" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> levelMatchAttachment;
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

    // The panel is laid out at this size and scaled to the window.
    static constexpr int baseW = 1640, baseH = 1130;
    static constexpr int colAWidth = 940, colBWidth = 300;

    SNSynthProcessor& getProcessor() { return processor; }
    int getSelectedSource() const { return modSource; }
    juce::String targetName (int target) const;
    void modulationChanged(); // repaint rings and the list
    void showSourceForSnapshot (int source) { selectSource (source); modulationChanged(); }
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

private:
    void timerCallback() override;
    void paintContent (juce::Graphics&);
    void layoutContent();
    Knob& makeKnob (const juce::String& paramId, const juce::String& label);
    void setupSlot (SlotPanel&, int slotIndex);
    void updateSlotTypeDisplay (SlotPanel&);
    void layoutGroup (KnobGroup&, juce::Rectangle<int> area, int topExtra = 0);
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    bool keyStateChanged (bool isKeyDown, juce::Component*) override;
    static bool isNoteKey (const juce::KeyPress&);
    bool canStartKeyNotes() const;
    bool handleKeyPress (const juce::KeyPress&);
    bool updateHeldKeys();
    void shiftOctave (int delta);
    void updateOctaveLabel();
    void selectSource (int source);
    void updateHint();

    SNSynthProcessor& processor;
    Theme::LookAndFeel look;

    // Everything sits on this, so it can be scaled as one.
    struct Content : juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override { if (painter) painter (g); }
    } content;

    std::vector<std::unique_ptr<Knob>> knobs;
    KnobGroup oscGroup, osc2Group, mixGroup, playGroup, filterEnvGroup, ampEnvGroup, lfo1Group, lfo2Group, eqGroup, outputGroup;
    std::array<SlotPanel, 3> slots;
    ChainStrip chainStrip { processor };

    Vis::Scope scope1, scope2, scopeMix;
    Vis::EnvelopeShape filterEnvShape, ampEnvShape;
    Vis::LfoShape lfo1Shape, lfo2Shape;
    Vis::EqCurve eqCurve;
    std::array<std::unique_ptr<Vis::FilterCurve>, 3> filterCurves;
    std::vector<juce::Component*> displays;

    juce::TextButton numbersButton { "Numbers" };
    std::unique_ptr<Vis::NumbersWindow> numbersWindow;

    Vis::OutputMeter outputMeter;
    juce::ToggleButton limiterButton { "Limiter" }, voiceCompButton { "Voice Gain Comp" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> limiterAttachment, voiceCompAttachment;
    juce::ComboBox oversamplingBox;
    juce::Label oversamplingLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> oversamplingAttachment;

    // Modulation
    std::array<juce::TextButton, Mod::numSources> sourceButtons;
    juce::Label modulateLabel;
    int modSource = -1; // the source being assigned, or -1
    ModList modList;
    juce::Rectangle<int> modListBounds, chainBounds, drumBounds, drumLabelColumn, drumControlRows;

    // Drum machine
    DrumGrid drumGrid { processor };
    juce::TextButton drumPlayButton { "Play" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> drumPlayAttachment;
    Knob* drumTempoKnob = nullptr;
    Knob* drumSwingKnob = nullptr;
    std::array<juce::TextButton, Drums::numDrums> drumNames;
    std::array<std::array<Knob*, 4>, Drums::numDrums> drumKnobs {};
    std::array<juce::ComboBox, Drums::numDrums> drumRoutes;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>, Drums::numDrums> drumRouteAttachments;
    std::array<float, Drums::numDrums> drumFlash {};
    bool spaceHeld = false;
    void setupDrums();
    void layoutDrums (juce::Rectangle<int> area);
    std::vector<juce::Rectangle<int>> dividers;
    std::vector<ModRing*> rings;
    float matrixChecksum = 0.0f;
    juce::MidiKeyboardComponent keyboard;
    juce::Label octaveLabel;
    juce::Label hintLabel;
    int baseOctave = 3;  // octave of the Z key (Z = C3, Q = C4 = middle C)
    int velocity = 100;
    struct NoteKey { char key; int semitone; };
    static constexpr size_t numNoteKeys = 37;
    static const NoteKey noteKeys[numNoteKeys];
    std::array<int, numNoteKeys> heldNotes {}; // note held by each computer key, or -1
    juce::TextButton octaveDown { "-" }, octaveUp { "+" };
    juce::Component* listeningTo = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SNSynthEditor)
};
