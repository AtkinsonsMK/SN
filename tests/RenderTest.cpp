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

static void setParam (SNSynthProcessor& p, const char* id, float value)
{
    auto* param = p.parameters.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

static float paramValue (SNSynthProcessor& p, const char* id)
{
    return p.parameters.getRawParameterValue (id)->load();
}

static double measureFreq (const juce::AudioBuffer<float>& b, int start, int len, double sr)
{
    int crossings = 0;
    auto* d = b.getReadPointer (0);
    for (int i = start + 1; i < start + len; ++i)
        if (d[i - 1] < 0.0f && d[i] >= 0.0f) ++crossings;
    return crossings / (len / sr);
}

static std::unique_ptr<SNSynthProcessor> makeSynth (double sr)
{
    auto p = std::make_unique<SNSynthProcessor>();
    p->setPlayConfigDetails (0, 2, sr, 512);
    p->prepareToPlay (sr, 512);
    return p;
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

    {
        // Every control exists with the default from the sound concept doc.
        auto p = makeSynth (sr);
        struct D { const char* id; float def; };
        const D defaults[] = {
            { ParamIDs::sawLevel, 100 }, { ParamIDs::pulseLevel, 0 }, { ParamIDs::pulseWidth, 50 }, { ParamIDs::subLevel, 0 },
            { ParamIDs::fenvAttack, 0.001f }, { ParamIDs::fenvDecay, 0.4f }, { ParamIDs::fenvSustain, 30 }, { ParamIDs::fenvRelease, 0.3f },
            { ParamIDs::aenvAttack, 0.001f }, { ParamIDs::aenvDecay, 0.3f }, { ParamIDs::aenvSustain, 100 }, { ParamIDs::aenvRelease, 0.2f },
            { ParamIDs::lfoRate, 4 }, { ParamIDs::lfoCutoff, 0 }, { ParamIDs::drift, 15 }, { ParamIDs::glide, 0 },
            { ParamIDs::volume, -6 } };
        bool allOk = true;
        for (auto& d : defaults)
        {
            const float v = paramValue (*p, d.id);
            if (std::abs (v - d.def) > std::max (0.01f, std::abs (d.def) * 0.02f))
            {
                std::cout << "      " << d.id << " = " << v << ", expected " << d.def << std::endl;
                allOk = false;
            }
        }
        check (allOk, "All 17 controls have the documented defaults");
    }

    {
        // Sub alone plays one octave down.
        auto p = makeSynth (sr);
        setParam (*p, ParamIDs::sawLevel, 0);
        setParam (*p, ParamIDs::subLevel, 100);
        setParam (*p, ParamIDs::drift, 0);
        auto out = render (*p, 69, 100, false);
        const double f = measureFreq (out, 9600, 24000, sr);
        check (std::abs (f - 220.0) < 3.0, "Sub plays one octave below (measured " + juce::String (f, 1) + " Hz)");
    }

    {
        // Pulse alone sounds, at pitch, at narrow and wide widths.
        for (float pw : { 10.0f, 50.0f, 90.0f })
        {
            auto p = makeSynth (sr);
            setParam (*p, ParamIDs::sawLevel, 0);
            setParam (*p, ParamIDs::pulseLevel, 100);
            setParam (*p, ParamIDs::pulseWidth, pw);
            setParam (*p, ParamIDs::drift, 0);
            auto out = render (*p, 57, 100, false); // A3 = 220 Hz
            const double f = measureFreq (out, 9600, 24000, sr);
            const float rms = out.getRMSLevel (0, 9600, 24000);
            check (rms > 0.01f && std::abs (f - 220.0) < 3.0,
                   "Pulse at " + juce::String (pw, 0) + "% width sounds at pitch (" + juce::String (f, 1) + " Hz)");
        }
    }

    {
        // Glide: second note starts near the first note's pitch and arrives at its own.
        auto p = makeSynth (sr);
        setParam (*p, ParamIDs::glide, 1.0f);
        setParam (*p, ParamIDs::drift, 0);
        render (*p, 57, 20, false);                     // A3 held
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOff (1, 57), 0);
        p->processBlock (block, m);
        auto out = render (*p, 69, 160, false);         // glide up to A4 over 1 s
        const double early = measureFreq (out, 0, 4800, sr);
        const double late  = measureFreq (out, 60000, 19000, sr);
        check (early < 260.0 && std::abs (late - 440.0) < 3.0,
               "Glide slides from 220 Hz (start " + juce::String (early, 0) + " Hz) to 440 Hz (end " + juce::String (late, 0) + " Hz)");
    }

    {
        // Slow amp attack: quiet at first, full later.
        auto p = makeSynth (sr);
        setParam (*p, ParamIDs::aenvAttack, 1.0f);
        auto out = render (*p, 69, 100, false);
        const float early = out.getRMSLevel (0, 0, 2400);
        const float late  = out.getRMSLevel (0, 48000, 2400);
        check (early < late * 0.2f, "Amp Attack 1 s fades in (" + juce::String (early, 4) + " then " + juce::String (late, 4) + ")");
    }

    {
        // Amp Sustain 0: note dies away while the key is still held.
        auto p = makeSynth (sr);
        setParam (*p, ParamIDs::aenvSustain, 0);
        setParam (*p, ParamIDs::aenvDecay, 0.2f);
        auto out = render (*p, 69, 100, false);
        check (out.getMagnitude (0, 40000, 4000) < 1.0e-3f, "Amp Sustain 0% decays to silence while held");
    }

    {
        // LFO to Cutoff makes the brightness move: compare high-frequency energy
        // (sample-to-sample change) across short windows.
        auto p = makeSynth (sr);
        setParam (*p, ParamIDs::lfoCutoff, 100);
        setParam (*p, ParamIDs::lfoRate, 2);
        setParam (*p, ParamIDs::fenvSustain, 0);
        auto out = render (*p, 45, 200, false); // 110 Hz, ~2 s
        float lo = 1e9f, hi = 0;
        for (int w = 0; w < 16; ++w)
        {
            double sum = 0;
            auto* d = out.getReadPointer (0);
            for (int i = 48000 + w * 2400 + 1; i < 48000 + (w + 1) * 2400; ++i)
                sum += (d[i] - d[i - 1]) * (d[i] - d[i - 1]);
            const float r = (float) std::sqrt (sum / 2400.0);
            lo = std::min (lo, r); hi = std::max (hi, r);
        }
        check (hi > lo * 1.5f, "LFO to Cutoff moves the brightness (" + juce::String (lo, 4) + " to " + juce::String (hi, 4) + ")");
    }

    {
        // Drift 100% wanders but stays within about 10 cents (440 Hz +/- 2.6 Hz).
        auto p = makeSynth (sr);
        setParam (*p, ParamIDs::drift, 100);
        auto out = render (*p, 69, 400, false); // ~4 s
        double minF = 1e9, maxF = 0;
        for (int w = 0; w < 7; ++w)
        {
            const double f = measureFreq (out, 9600 + w * 24000, 24000, sr);
            minF = std::min (minF, f); maxF = std::max (maxF, f);
        }
        check (minF > 437.0 && maxF < 443.0, "Drift stays within about 10 cents (" + juce::String (minF, 1) + " to " + juce::String (maxF, 1) + " Hz)");
    }

    std::cout << (failures == 0 ? "All checks passed" : juce::String (failures) + " check(s) failed") << std::endl;
    return failures == 0 ? 0 : 1;
}
