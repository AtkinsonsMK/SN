// Offline checks: the synth makes sound, at the right pitch and level,
// and its settings survive a save and reload (as FL Studio does with projects).
#include "PluginProcessor.h"
#include "Parameters.h"
#include "Filters.h"
#include "PluginEditor.h"
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

static void setParam (SNSynthProcessor& p, const juce::String& id, float value)
{
    auto* param = p.parameters.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

static float paramValue (SNSynthProcessor& p, const juce::String& id)
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

// Renders the panel with a chord playing, to check the live displays.
static int saveSnapshot (const juce::File& file)
{
    const double sr = 48000.0;
    auto p = makeSynth (sr);
    setParam (*p, ParamIDs::pulseLevel, 60);
    setParam (*p, ParamIDs::subLevel, 40);
    setParam (*p, ParamIDs::slot (0, ParamIDs::slotResonance), 55);
    setParam (*p, ParamIDs::slot (1, ParamIDs::slotType), FilterType::steinerParker);
    setParam (*p, ParamIDs::slot (1, ParamIDs::slotCharacter), 50);
    setParam (*p, ParamIDs::slot (1, ParamIDs::slotResonance), 60);
    setParam (*p, ParamIDs::slot (1, ParamIDs::slotCutoff), 1500);
    setParam (*p, ParamIDs::slot (2, ParamIDs::slotType), FilterType::formant);
    setParam (*p, ParamIDs::slot (2, ParamIDs::slotCutoff), 1000);
    setParam (*p, ParamIDs::slot (2, ParamIDs::slotResonance), 40);
    setParam (*p, ParamIDs::eqLow, 4);
    setParam (*p, ParamIDs::eqHigh, -6);
    setParam (*p, ParamIDs::lfoCutoff, 20);
    setParam (*p, ParamIDs::osc2Level, 70);
    setParam (*p, ParamIDs::osc2Coarse, 7);
    setParam (*p, ParamIDs::osc2Sine, 60);
    setParam (*p, ParamIDs::osc2Saw, 30);
    setParam (*p, ParamIDs::fm1to2, 25);

    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer m;
    for (int n : { 45, 52, 57 }) m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 110), 0);
    p->processBlock (block, m);
    juce::MidiBuffer none;
    for (int i = 0; i < 20; ++i) p->processBlock (block, none);

    auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
    juce::FileOutputStream out (file);
    out.setPosition (0); out.truncate();
    juce::PNGImageFormat().writeImageToStream (image, out);

    // And the Numbers view.
    Vis::Numbers numbers (*p);
    numbers.setSize (820, 500);
    numbers.capture();
    auto img2 = numbers.createComponentSnapshot (numbers.getLocalBounds(), true, 2.0f);
    juce::FileOutputStream out2 (file.getSiblingFile (file.getFileNameWithoutExtension() + "-numbers.png"));
    out2.setPosition (0); out2.truncate();
    juce::PNGImageFormat().writeImageToStream (img2, out2);
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const double sr = 48000.0;

    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
        return saveSnapshot (juce::File (juce::String (argv[2])));

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
            { ParamIDs::volume, -6 },
            { ParamIDs::sineLevel, 0 }, { ParamIDs::osc1Coarse, 0 }, { ParamIDs::osc1Fine, 0 }, { ParamIDs::osc1Level, 100 },
            { ParamIDs::osc2Sine, 0 }, { ParamIDs::osc2Saw, 100 }, { ParamIDs::osc2Pulse, 0 }, { ParamIDs::osc2Pw, 50 },
            { ParamIDs::osc2Coarse, 0 }, { ParamIDs::osc2Fine, 7 }, { ParamIDs::osc2Level, 0 },
            { ParamIDs::fm1to2, 0 }, { ParamIDs::fm2to1, 0 }, { ParamIDs::ringMod, 0 }, { ParamIDs::voices, 8 } };
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
        check (allOk, "All 32 non-filter controls have the documented defaults");
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

    // ---------------- Step 3: filter rack and EQ ----------------
    auto sid = [] (int slot, const char* c) { return ParamIDs::slot (slot, c); };
    auto setType = [&] (SNSynthProcessor& p, int slot, int type)
    {
        auto* param = p.parameters.getParameter (sid (slot, ParamIDs::slotType));
        param->setValueNotifyingHost (param->convertTo0to1 ((float) type));
    };
    auto brightness = [] (const juce::AudioBuffer<float>& b, int start, int len)
    {
        double sum = 0; auto* d = b.getReadPointer (0);
        for (int i = start + 1; i < start + len; ++i) sum += (d[i] - d[i - 1]) * (d[i] - d[i - 1]);
        return (float) std::sqrt (sum / len);
    };

    {
        auto p = makeSynth (sr);
        const int total = (int) p->getParameters().size();
        check (total == 53, "Synth has 53 controls (found " + juce::String (total) + ")");

        bool ok = std::abs (paramValue (*p, sid (0, ParamIDs::slotType)) - FilterType::ota) < 0.01f
               && std::abs (paramValue (*p, sid (1, ParamIDs::slotType)) - FilterType::off) < 0.01f
               && std::abs (paramValue (*p, sid (2, ParamIDs::slotType)) - FilterType::off) < 0.01f
               && std::abs (paramValue (*p, sid (0, ParamIDs::slotCutoff)) - 5000.0f) < 1.0f
               && std::abs (paramValue (*p, sid (1, ParamIDs::slotCutoff)) - 2000.0f) < 1.0f
               && std::abs (paramValue (*p, sid (0, ParamIDs::slotEnv)) - 40.0f) < 0.1f
               && std::abs (paramValue (*p, sid (2, ParamIDs::slotResonance)) - 15.0f) < 0.1f
               && std::abs (paramValue (*p, ParamIDs::eqMid)) < 0.01f;
        check (ok, "Filter rack and EQ have the documented defaults (OTA / Off / Off)");
    }

    for (int type = 1; type < FilterType::count; ++type)
    {
        const auto name = Params::filterTypeNames()[type];
        const bool lowpassLike = type <= FilterType::polivoks;

        // Low cutoff makes it darker than high cutoff.
        float bright[2];
        for (int k = 0; k < 2; ++k)
        {
            auto p = makeSynth (sr);
            setType (*p, 0, type);
            setParam (*p, sid (0, ParamIDs::slotEnv), 0);
            setParam (*p, sid (0, ParamIDs::slotCutoff), k == 0 ? 300.0f : 8000.0f);
            setParam (*p, sid (0, ParamIDs::slotCharacter), type == FilterType::lowpassGate ? 100.0f : 0.0f);
            auto out = render (*p, 45, 40, false);
            bright[k] = brightness (out, 2400, 4800);
        }
        if (lowpassLike)
            check (bright[0] < bright[1] * 0.5f, name + ": Cutoff darkens the sound (" + juce::String (bright[0], 4) + " vs " + juce::String (bright[1], 4) + ")");

        // Full resonance and drive, sweeping: stays stable (no blow-ups, no silence-killing NaN).
        auto p = makeSynth (sr);
        setType (*p, 0, type);
        setParam (*p, sid (0, ParamIDs::slotResonance), 100);
        setParam (*p, sid (0, ParamIDs::slotDrive), 100);
        setParam (*p, sid (0, ParamIDs::slotEnv), 100);
        setParam (*p, ParamIDs::lfoCutoff, 100);
        setParam (*p, ParamIDs::lfoRate, 7);
        auto out = render (*p, 33, 150, false);
        bool finite = true;
        for (int i = 0; i < out.getNumSamples(); ++i)
            if (! std::isfinite (out.getSample (0, i))) { finite = false; break; }
        const float peak = out.getMagnitude (0, 0, out.getNumSamples());
        check (finite && peak < 1.5f && peak > 0.001f, name + ": stable at full Resonance and Drive (peak " + juce::String (peak, 3) + ")");
    }

    {
        // Switching types keeps a similar loudness (within about 8 dB of OTA).
        float levels[FilterType::count];
        int idx = 0;
        juce::String detail;
        for (int type = 1; type < FilterType::count; ++type)
        {
            auto p = makeSynth (sr);
            setType (*p, 0, type);
            setParam (*p, sid (0, ParamIDs::slotCharacter), type == FilterType::lowpassGate ? 100.0f : 0.0f);
            auto out = render (*p, 57, 40, false);
            levels[idx] = out.getRMSLevel (0, 2400, 4800);
            detail << Params::filterTypeNames()[type] << " " << juce::String (juce::Decibels::gainToDecibels (levels[idx] / levels[0]), 1) << " dB, ";
            ++idx;
        }
        bool ok = true;
        for (int i = 0; i < idx; ++i) ok = ok && std::abs (juce::Decibels::gainToDecibels (levels[i] / levels[0])) < 8.0f;
        check (ok, "Switching filter type keeps similar loudness (" + detail.dropLastCharacters (2) + ")");
    }

    {
        // Formant: the vowel changes the tone (I is brighter than U).
        float b[2]; int k = 0;
        for (float vowel : { 50.0f, 90.0f })
        {
            auto p = makeSynth (sr);
            setType (*p, 0, FilterType::formant);
            setParam (*p, sid (0, ParamIDs::slotCutoff), 1000.0f);
            setParam (*p, sid (0, ParamIDs::slotEnv), 0);
            setParam (*p, sid (0, ParamIDs::slotCharacter), vowel);
            auto out = render (*p, 45, 40, false);
            b[k++] = brightness (out, 2400, 9600) / std::max (out.getRMSLevel (0, 2400, 9600), 1.0e-6f);
        }
        check (b[0] > b[1] * 1.3f, "Formant: vowel I is brighter than U (" + juce::String (b[0], 3) + " vs " + juce::String (b[1], 3) + ")");
    }

    {
        // Comb: high Resonance makes it ring, so the sound changes a lot.
        float lv[2]; int k = 0;
        for (float res : { 0.0f, 95.0f })
        {
            auto p = makeSynth (sr);
            setType (*p, 0, FilterType::comb);
            setParam (*p, sid (0, ParamIDs::slotCutoff), 330.0f);
            setParam (*p, sid (0, ParamIDs::slotEnv), 0);
            setParam (*p, sid (0, ParamIDs::slotResonance), res);
            auto out = render (*p, 45, 40, false);
            lv[k++] = brightness (out, 2400, 9600) / std::max (out.getRMSLevel (0, 2400, 9600), 1.0e-6f);
        }
        check (std::abs (lv[1] - lv[0]) > 0.1f * lv[0], "Comb: Resonance changes the tone (" + juce::String (lv[0], 3) + " vs " + juce::String (lv[1], 3) + ")");
    }

    {
        // Lowpass Gate plucks: it dies away while the key is held, faster with short Gate Decay.
        float tails[2];
        for (int k = 0; k < 2; ++k)
        {
            auto p = makeSynth (sr);
            setType (*p, 0, FilterType::lowpassGate);
            setParam (*p, sid (0, ParamIDs::slotCharacter), k == 0 ? 0.0f : 60.0f);
            auto out = render (*p, 57, 60, false);
            tails[k] = out.getRMSLevel (0, 24000, 4800); // 0.5 s in, key still held
        }
        check (tails[0] < 0.002f && tails[1] > tails[0] * 5.0f, "Lowpass Gate plucks; Gate Decay lengthens it (" + juce::String (tails[0], 4) + " vs " + juce::String (tails[1], 4) + ")");
    }

    {
        // Sallen-Key HP Cutoff thins out a low note.
        float lv[2];
        for (int k = 0; k < 2; ++k)
        {
            auto p = makeSynth (sr);
            setType (*p, 0, FilterType::sallenKey);
            setParam (*p, sid (0, ParamIDs::slotEnv), 0);
            setParam (*p, sid (0, ParamIDs::slotCharacter), k == 0 ? 0.0f : 80.0f);
            auto out = render (*p, 36, 40, false);
            lv[k] = out.getRMSLevel (0, 2400, 9600);
        }
        check (lv[1] < lv[0] * 0.6f, "Sallen-Key HP Cutoff thins the low end (" + juce::String (lv[0], 3) + " to " + juce::String (lv[1], 3) + ")");
    }

    {
        // Three slots in series: adding a dark second slot darkens the sound.
        float b[2];
        for (int k = 0; k < 2; ++k)
        {
            auto p = makeSynth (sr);
            setParam (*p, sid (0, ParamIDs::slotEnv), 0);
            if (k == 1) { setType (*p, 1, FilterType::sallenKey); setParam (*p, sid (1, ParamIDs::slotCutoff), 400.0f); }
            auto out = render (*p, 45, 40, false);
            b[k] = brightness (out, 2400, 4800);
        }
        check (b[1] < b[0] * 0.5f, "Slots run in series (second slot darkens: " + juce::String (b[0], 4) + " to " + juce::String (b[1], 4) + ")");
    }

    {
        // Slot order is remembered when the project is saved and reloaded.
        auto p = makeSynth (sr);
        p->moveSlot (0, 1); // order becomes 1,0,2
        p->moveSlot (1, 1); // order becomes 1,2,0
        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = makeSynth (sr);
        q->setStateInformation (state.getData(), (int) state.getSize());
        auto o = q->getSlotOrder();
        check (o[0] == 1 && o[1] == 2 && o[2] == 0, "Filter slot order is saved and reloaded (" + juce::String (o[0] + 1) + ", " + juce::String (o[1] + 1) + ", " + juce::String (o[2] + 1) + ")");
    }

    {
        // EQ: Low +12 dB makes a bass note louder; High -12 dB makes it darker.
        auto ref = makeSynth (sr);
        auto refOut = render (*ref, 33, 40, false);
        auto p = makeSynth (sr);
        setParam (*p, ParamIDs::eqLow, 12);
        auto out = render (*p, 33, 40, false);
        const float gain = out.getRMSLevel (0, 4800, 9600) / refOut.getRMSLevel (0, 4800, 9600);
        check (gain > 1.5f, "EQ Low boosts the bass (x" + juce::String (gain, 2) + ")");

        auto h = makeSynth (sr);
        setParam (*h, ParamIDs::eqHigh, -12);
        setParam (*h, sid (0, ParamIDs::slotCutoff), 20000.0f);
        setParam (*ref, sid (0, ParamIDs::slotCutoff), 20000.0f);
        auto hOut = render (*h, 57, 40, false);
        auto r2 = makeSynth (sr);
        setParam (*r2, sid (0, ParamIDs::slotCutoff), 20000.0f);
        auto rOut = render (*r2, 57, 40, false);
        const float bh = brightness (hOut, 4800, 9600), br = brightness (rOut, 4800, 9600);
        check (bh < br * 0.8f, "EQ High cuts the treble (" + juce::String (br, 4) + " to " + juce::String (bh, 4) + ")");
    }


    //==========================================================================
    // Stage A: second oscillator, sine, tuning, FM, ring mod, voices, meter.
    auto oneOsc = [&] (int which)
    {
        auto p = makeSynth (sr);
        setParam (*p, ParamIDs::drift, 0);
        setParam (*p, ParamIDs::osc1Level, which == 0 ? 100.0f : 0.0f);
        setParam (*p, ParamIDs::osc2Level, which == 1 ? 100.0f : 0.0f);
        setParam (*p, ParamIDs::osc2Fine, 0);
        return p;
    };
    auto heldBlocks = [] (SNSynthProcessor& p, int blocks)
    {
        juce::AudioBuffer<float> out (2, 512 * blocks), block (2, 512);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer none;
            p.processBlock (block, none);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, b * 512, block, ch, 0, 512);
        }
        return out;
    };
    auto send = [] (SNSynthProcessor& p, std::initializer_list<juce::MidiMessage> msgs)
    {
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer m;
        for (auto& msg : msgs) m.addEvent (msg, 0);
        p.processBlock (block, m);
    };

    {
        auto p = oneOsc (1);
        setParam (*p, ParamIDs::osc2Coarse, 12);
        auto out = render (*p, 69, 100, false);
        const double f = measureFreq (out, 9600, 24000, sr);
        check (std::abs (f - 880.0) < 4.0, "Osc 2 alone, Coarse +12, plays an octave up (" + juce::String (f, 1) + " Hz)");
    }
    {
        auto p = oneOsc (0);
        setParam (*p, ParamIDs::osc1Coarse, -12);
        auto out = render (*p, 69, 100, false);
        const double f = measureFreq (out, 9600, 24000, sr);
        check (std::abs (f - 220.0) < 3.0, "Osc 1 Coarse -12 plays an octave down (" + juce::String (f, 1) + " Hz)");
    }
    {
        auto p = oneOsc (0);
        setParam (*p, ParamIDs::osc1Fine, 100);
        auto out = render (*p, 57, 100, false);
        const double f = measureFreq (out, 9600, 24000, sr);
        const double expected = 220.0 * std::pow (2.0, 1.0 / 12.0);
        check (std::abs (f - expected) < 3.0, "Osc 1 Fine +100 cents is one semitone up (" + juce::String (f, 1) + " Hz)");
    }
    {
        // Sine alone: at pitch and much less bright than the saw.
        auto saw = oneOsc (0);
        auto sawOut = render (*saw, 57, 100, false);
        auto sine = oneOsc (0);
        setParam (*sine, ParamIDs::sawLevel, 0);
        setParam (*sine, ParamIDs::sineLevel, 100);
        setParam (*sine, ParamIDs::slot (0, ParamIDs::slotType), FilterType::off);
        setParam (*saw, ParamIDs::slot (0, ParamIDs::slotType), FilterType::off);
        auto out = render (*sine, 57, 100, false);
        sawOut = render (*saw, 57, 100, false);
        const double f = measureFreq (out, 9600, 24000, sr);
        const float rs = brightness (out, 9600, 24000) / juce::jmax (1.0e-6f, out.getRMSLevel (0, 9600, 24000));
        const float rw = brightness (sawOut, 9600, 24000) / juce::jmax (1.0e-6f, sawOut.getRMSLevel (0, 9600, 24000));
        check (std::abs (f - 220.0) < 3.0 && rs < rw * 0.5f,
               "Sine sounds at pitch and is smoother than Saw (" + juce::String (f, 1) + " Hz)");
    }
    {
        // FM 1 > 2 adds brightness to Osc 2's sine.
        auto plain = oneOsc (1);
        setParam (*plain, ParamIDs::osc2Saw, 0);
        setParam (*plain, ParamIDs::osc2Sine, 100);
        setParam (*plain, ParamIDs::slot (0, ParamIDs::slotType), FilterType::off);
        auto a = render (*plain, 57, 60, false);
        auto fm = oneOsc (1);
        setParam (*fm, ParamIDs::osc2Saw, 0);
        setParam (*fm, ParamIDs::osc2Sine, 100);
        setParam (*fm, ParamIDs::slot (0, ParamIDs::slotType), FilterType::off);
        setParam (*fm, ParamIDs::fm1to2, 80);
        auto b = render (*fm, 57, 60, false);
        const float ba = brightness (a, 9600, 12000), bb = brightness (b, 9600, 12000);
        const double f = measureFreq (b, 9600, 24000, sr);
        check (bb > ba * 1.5f && b.getMagnitude (0, 0, b.getNumSamples()) < 1.0f,
               "FM 1 > 2 makes Osc 2 brighter without clipping (x" + juce::String (bb / ba, 1) + ")");
    }
    {
        auto p = oneOsc (0);
        setParam (*p, ParamIDs::osc1Level, 0);
        setParam (*p, ParamIDs::ringMod, 100);
        auto out = render (*p, 57, 60, false);
        check (out.getRMSLevel (0, 9600, 12000) > 0.005f, "Ring Mod alone makes sound");
    }
    {
        // Voices limit.
        auto p = makeSynth (sr);
        setParam (*p, ParamIDs::voices, 4);
        send (*p, { juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), juce::MidiMessage::noteOn (1, 52, (juce::uint8) 100),
                    juce::MidiMessage::noteOn (1, 55, (juce::uint8) 100), juce::MidiMessage::noteOn (1, 59, (juce::uint8) 100),
                    juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100), juce::MidiMessage::noteOn (1, 65, (juce::uint8) 100) });
        heldBlocks (*p, 4);
        check (p->getActiveVoiceCount() == 4, "Voices = 4: a 6-note chord plays 4 notes (" + juce::String (p->getActiveVoiceCount()) + ")");

        setParam (*p, ParamIDs::voices, 16);
        juce::MidiBuffer m;
        for (int n = 0; n < 16; ++n) m.addEvent (juce::MidiMessage::noteOn (1, 36 + n * 2, (juce::uint8) 100), 0);
        juce::AudioBuffer<float> block (2, 512);
        p->processBlock (block, m);
        heldBlocks (*p, 2);
        check (p->getActiveVoiceCount() == 16, "Voices = 16: 16 notes at once (" + juce::String (p->getActiveVoiceCount()) + ")");
    }
    {
        // Mono, legato, last-note priority.
        auto p = oneOsc (0);
        setParam (*p, ParamIDs::voices, 1);
        setParam (*p, ParamIDs::slot (0, ParamIDs::slotType), FilterType::off);
        send (*p, { juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100) });
        heldBlocks (*p, 20);
        send (*p, { juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100) });
        auto upper = heldBlocks (*p, 60);
        const double f1 = measureFreq (upper, 4800, 24000, sr);
        const int voicesNow = p->getActiveVoiceCount();
        send (*p, { juce::MidiMessage::noteOff (1, 69) });
        auto back = heldBlocks (*p, 60);
        const double f2 = measureFreq (back, 4800, 24000, sr);
        check (voicesNow == 1 && std::abs (f1 - 440.0) < 3.0 && std::abs (f2 - 220.0) < 3.0,
               "Mono: newest key wins, releasing it returns to the held key ("
                   + juce::String (f1, 0) + " then " + juce::String (f2, 0) + " Hz, " + juce::String (voicesNow) + " voice)");

        // Legato: the amp envelope isn't restarted, so with a slow attack the
        // level keeps climbing smoothly instead of dropping back.
        auto q = oneOsc (0);
        setParam (*q, ParamIDs::voices, 1);
        setParam (*q, ParamIDs::aenvAttack, 2.0f);
        send (*q, { juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100) });
        auto before = heldBlocks (*q, 40);
        send (*q, { juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) });
        auto after = heldBlocks (*q, 10);
        const float lb = before.getRMSLevel (0, before.getNumSamples() - 2400, 2400);
        const float la = after.getRMSLevel (0, 0, 2400);
        check (la > lb * 0.8f, "Mono legato keeps the envelope going (" + juce::String (lb, 3) + " -> " + juce::String (la, 3) + ")");

        send (*q, { juce::MidiMessage::noteOff (1, 60), juce::MidiMessage::noteOff (1, 57) });
        heldBlocks (*q, 200);
        check (q->getActiveVoiceCount() == 0, "Mono: releasing all keys ends the note");
    }
    {
        // Output meter sees the sound, and the clip light catches overs.
        auto p = makeSynth (sr);
        render (*p, 57, 30, false);
        const float peak = p->live.outPeak.load(), rms = p->live.outRms.load();
        check (peak > 0.05f && rms > 0.01f && peak >= rms && ! p->live.outClipped.load(),
               "Output meter reads the level (peak " + juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dB)");
        setParam (*p, ParamIDs::volume, 6);
        setParam (*p, ParamIDs::osc2Level, 100);
        setParam (*p, ParamIDs::subLevel, 100);
        juce::MidiBuffer m;
        for (int n : { 45, 48, 52, 55, 57, 60 }) m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 127), 0);
        juce::AudioBuffer<float> block (2, 512);
        p->processBlock (block, m);
        heldBlocks (*p, 20);
        check (p->live.outClipped.load(), "Clip light comes on when the output goes over 0 dB");
    }

    std::cout << (failures == 0 ? "All checks passed" : juce::String (failures) + " check(s) failed") << std::endl;
    return failures == 0 ? 0 : 1;
}
