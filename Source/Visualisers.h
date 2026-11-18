#pragma once

#include "PluginProcessor.h"
#include "Theme.h"

// The live displays. Each one reads the synth's settings and the LiveInfo
// written by the audio, and is repainted by the editor about 30 times a second.
namespace Vis
{
    struct Colours
    {
        static inline const juce::Colour screen  = Theme::screen;
        static inline const juce::Colour grid    = Theme::grid;
        static inline const juce::Colour trace   = Theme::ink;
        static inline const juce::Colour trace2  = Theme::trace2;
        static inline const juce::Colour text    = Theme::dim;
    };

    class Display : public juce::Component
    {
    public:
        explicit Display (SNSynthProcessor& p) : processor (p) { setInterceptsMouseClicks (false, false); }

    protected:
        float param (const juce::String& id) const { return processor.parameters.getRawParameterValue (id)->load(); }
        void paintScreen (juce::Graphics& g) const
        {
            g.setColour (Colours::screen);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        }

        SNSynthProcessor& processor;
    };

    // Oscillator shape: a still picture of the wave the oscillator settings
    // make. which: 0 = Osc 1, 1 = Osc 2, 2 = the mix of both (with Sub and Ring).
    class Scope : public Display
    {
    public:
        Scope (SNSynthProcessor& p, int whichOsc) : Display (p), which (whichOsc) {}
        void paint (juce::Graphics&) override;

    private:
        int which;
    };

    // Output level in dB: a bar for the average and a line for the peak,
    // with a red light that stays on after the sound goes over 0 dB
    // (click it to reset).
    class OutputMeter : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit OutputMeter (SNSynthProcessor& p) : processor (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void tick(); // called by the editor about 30 times a second

    private:
        SNSynthProcessor& processor;
        float rmsDb = -100.0f, peakDb = -100.0f, holdDb = -100.0f;
        int holdFrames = 0;
        bool clip = false;
    };

    // Filter response curve for one slot, following the live cutoff.
    class FilterCurve : public Display
    {
    public:
        FilterCurve (SNSynthProcessor& p, int slot) : Display (p), slotIndex (slot) {}
        void paint (juce::Graphics&) override;

    private:
        int slotIndex;
    };

    // Envelope shape, with a dot showing where the newest note is.
    class EnvelopeShape : public Display
    {
    public:
        EnvelopeShape (SNSynthProcessor& p, bool isFilterEnvelope) : Display (p), filterEnv (isFilterEnvelope) {}
        void paint (juce::Graphics&) override;

    private:
        bool filterEnv;
    };

    // One LFO's shape (one cycle), with its current position.
    class LfoShape : public Display
    {
    public:
        LfoShape (SNSynthProcessor& p, int lfoIndex) : Display (p), index (lfoIndex) {}
        void paint (juce::Graphics&) override;

    private:
        int index;
    };

    // EQ curve: the combined effect of Low, Mid and High.
    class EqCurve : public Display
    {
    public:
        using Display::Display;
        void paint (juce::Graphics&) override;
    };

    // "Numbers": a frozen snapshot of 16 consecutive samples at each stage.
    class Numbers : public juce::Component, private juce::Timer
    {
    public:
        explicit Numbers (SNSynthProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;
        void capture(); // take a new snapshot (keeps the old one if there's no sound)

    private:
        void timerCallback() override;

        SNSynthProcessor& processor;
        juce::ToggleButton freeze { "Freeze" };
        juce::TextButton grab { "Grab now" };
        static constexpr int numRows = 16;
        LiveInfo::Frame frames[numRows] {};
        juce::String stageNames[LiveInfo::numStages];
    };

    class NumbersWindow : public juce::DocumentWindow
    {
    public:
        explicit NumbersWindow (SNSynthProcessor&);
        void closeButtonPressed() override { setVisible (false); }
    };
}
