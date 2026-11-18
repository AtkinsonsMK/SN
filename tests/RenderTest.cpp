// Offline checks: the synth makes sound, at the right pitch and level,
// and its settings survive a save and reload (as FL Studio does with projects).
#include "PluginProcessor.h"
#include "Parameters.h"
#include <cmath>
#include <iostream>

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
    if (! ok) ++failures;
}

static juce::AudioBuffer<float> render (SNSynthProcessor& p, int note, int numBlocks, bool releaseHalfway)
{
    const int blockSize = 512;
    juce::AudioBuffer<float> out (2, blockSize * numBlocks);
    juce::AudioBuffer<float> block (2, blockSize);

    for (int b = 0; b < numBlocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 127), 0);
        if (releaseHalfway && b == numBlocks / 2) midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
        p.processBlock (block, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, b * blockSize, block, ch, 0, blockSize);
    }
    return out;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const double sr = 48000.0;

    {
        SNSynthProcessor p;
        p.setPlayConfigDetails (0, 2, sr, 512);
        p.prepareToPlay (sr, 512);

        auto out = render (p, 69, 100, false); // A4 = 440 Hz, ~1 s
        const int start = 9600, len = 24000;   // skip attack/decay, use 0.5 s
        float rms = out.getRMSLevel (0, start, len);
        check (rms > 0.01f, "Note produces sound (RMS " + juce::String (rms, 4) + ")");
        check (out.getMagnitude (0, 0, out.getNumSamples()) < 1.0f, "Single note does not clip");

        int crossings = 0;
        auto* d = out.getReadPointer (0);
        for (int i = start + 1; i < start + len; ++i)
            if (d[i - 1] < 0.0f && d[i] >= 0.0f) ++crossings;
        const double freq = crossings / (len / sr);
        check (std::abs (freq - 440.0) < 3.0, "A4 plays at 440 Hz (measured " + juce::String (freq, 1) + " Hz)");

        SNSynthProcessor fresh;
        fresh.setPlayConfigDetails (0, 2, sr, 512);
        fresh.prepareToPlay (sr, 512);
        auto tail = render (fresh, 60, 100, true);
        check (tail.getMagnitude (0, tail.getNumSamples() - 4800, 4800) < 1.0e-4f, "Note fades out after key release");

        // 8 voices at once should all sound without crashing.
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer chord;
        for (int n = 0; n < 8; ++n) chord.addEvent (juce::MidiMessage::noteOn (1, 48 + n * 3, (juce::uint8) 100), 0);
        p.processBlock (block, chord);
        juce::MidiBuffer none;
        for (int i = 0; i < 20; ++i) p.processBlock (block, none);
        check (block.getRMSLevel (0, 0, 512) > 0.01f, "8-note chord plays");
    }

    {
        // Volume -inf silences; state round-trips.
        SNSynthProcessor p;
        p.setPlayConfigDetails (0, 2, sr, 512);
        p.prepareToPlay (sr, 512);
        auto* vol = p.parameters.getParameter (ParamIDs::volume);
        check (std::abs (p.parameters.getRawParameterValue (ParamIDs::volume)->load() + 6.0f) < 0.01f, "Volume defaults to -6 dB");

        vol->setValueNotifyingHost (0.0f);
        auto out = render (p, 69, 60, false);
        check (out.getMagnitude (0, 24000, 4000) < 1.0e-3f, "Volume at minimum is silent");

        vol->setValueNotifyingHost (vol->convertTo0to1 (-20.0f));
        juce::MemoryBlock state;
        p.getStateInformation (state);

        SNSynthProcessor q;
        q.setStateInformation (state.getData(), (int) state.getSize());
        const float restored = q.parameters.getRawParameterValue (ParamIDs::volume)->load();
        check (std::abs (restored + 20.0f) < 0.11f, "Saved settings reload (Volume " + juce::String (restored, 1) + " dB)");
    }

    std::cout << (failures == 0 ? "All checks passed" : juce::String (failures) + " check(s) failed") << std::endl;
    return failures == 0 ? 0 : 1;
}
