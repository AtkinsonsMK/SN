// Offline checks: the synth makes sound, at the right pitch and level,
// and its settings survive a save and reload (as FL Studio does with projects).
#include "PluginProcessor.h"
#include "Parameters.h"
#include "Filters.h"
#include "Drums.h"
#include "Envelope.h"
#include "PluginEditor.h"
#include <cmath>
#include <iostream>
#include <thread>
#include <functional>

// Counts memory allocations while switched on, to prove the audio thread never allocates.
static std::atomic<long> allocCount { 0 };
static thread_local bool countAllocs = false; // only the thread under test is counted
void* operator new (std::size_t n)
{
    if (countAllocs) allocCount.fetch_add (1, std::memory_order_relaxed);
    if (void* ptr = std::malloc (n > 0 ? n : 1)) return ptr;
    throw std::bad_alloc();
}
void operator delete (void* ptr) noexcept { std::free (ptr); }
void operator delete (void* ptr, std::size_t) noexcept { std::free (ptr); }

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
    p->modMatrix.set (Mod::lfo1, Mod::filterTarget (0, 0), 0.12f);
    p->modMatrix.set (Mod::lfo2, Mod::filterTarget (1, 0), -0.2f);
    p->modMatrix.set (Mod::velocity, Mod::osc2Level, 0.3f);
    p->modMatrix.set (Mod::filterEnv, Mod::fm12, 0.25f);
    setParam (*p, ParamIDs::lfo2Shape, 0);
    setParam (*p, ParamIDs::lfo2Smooth, 40);
    setParam (*p, ParamIDs::osc2Level, 70);
    setParam (*p, ParamIDs::osc2Coarse, 7);
    setParam (*p, ParamIDs::osc2Sine, 60);
    setParam (*p, ParamIDs::osc2Saw, 30);
    setParam (*p, ParamIDs::fm1to2, 25);
    setParam (*p, ParamIDs::drumPlay, 1);
    setParam (*p, ParamIDs::drum (Drums::hat, ParamIDs::drumRoute), 2);
    setParam (*p, ParamIDs::drumSwing, 20);

    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
    if (auto* ed = dynamic_cast<SNSynthEditor*> (editor.get()))
        ed->showSourceForSnapshot (-1);
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
    numbers.setSize (960, 500);
    numbers.capture();
    auto img2 = numbers.createComponentSnapshot (numbers.getLocalBounds(), true, 2.0f);
    juce::FileOutputStream out2 (file.getSiblingFile (file.getFileNameWithoutExtension() + "-numbers.png"));
    out2.setPosition (0); out2.truncate();
    juce::PNGImageFormat().writeImageToStream (img2, out2);
    return 0;
}

#include "StressChecks.inc"

// ---- Sound-quality checks (targets from the 26 Nov sound review) ----------
namespace SoundChecks
{
    struct Event { int sample; juce::MidiMessage msg; };

    // Renders mono (left) with MIDI events at exact sample positions.
    static std::vector<float> renderEvents (SNSynthProcessor& p, std::vector<Event> events, int numSamples, int blockSize = 512)
    {
        std::vector<float> out ((size_t) numSamples);
        juce::AudioBuffer<float> block (2, blockSize);
        for (int pos = 0; pos < numSamples; pos += blockSize)
        {
            const int len = juce::jmin (blockSize, numSamples - pos);
            juce::MidiBuffer midi;
            for (auto& e : events)
                if (e.sample >= pos && e.sample < pos + len)
                    midi.addEvent (e.msg, e.sample - pos);
            juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, len);
            p.processBlock (view, midi);
            std::copy (block.getReadPointer (0), block.getReadPointer (0) + len, out.begin() + pos);
        }
        return out;
    }

    static std::unique_ptr<SNSynthProcessor> neutral (double sr)
    {
        auto p = makeSynth (sr);
        setParam (*p, ParamIDs::drift, 0);
        setParam (*p, ParamIDs::limiter, 0);
        setParam (*p, ParamIDs::volume, -12);
        return p;
    }

    static double rms (const std::vector<float>& x, size_t a, size_t b)
    {
        double s = 0.0;
        for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i];
        return std::sqrt (s / (double) (b - a));
    }

    static void run (double sr)
    {
        using juce::MidiMessage;
        const int sec = (int) sr;

        // 1. A held note keeps its level while other notes come and go (target: within 0.2 dB).
        for (bool comp : { false, true })
        {
            std::vector<Event> held { { 0, MidiMessage::noteOn (1, 45, (juce::uint8) 100) } };
            std::vector<Event> others;
            for (int k = 0; k < 10; ++k)
            {
                const int at = sec / 2 + k * (sec / 5);
                others.push_back ({ at, MidiMessage::noteOn (1, 60 + (k % 5) * 2, (juce::uint8) 100) });
                others.push_back ({ at + sec / 20, MidiMessage::noteOff (1, 60 + (k % 5) * 2) });
            }
            auto all = held; all.insert (all.end(), others.begin(), others.end());
            auto setup = [&] { auto q = neutral (sr); setParam (*q, ParamIDs::voiceGainComp, comp ? 1.0f : 0.0f);
                               setParam (*q, ParamIDs::aenvRelease, 0.3f); return q; };
            auto a = renderEvents (*setup(), held, sec * 3);
            auto b = renderEvents (*setup(), all, sec * 3);
            auto c = renderEvents (*setup(), others, sec * 3);
            // The held note inside the mix = mix minus the other notes alone.
            double worst = 0.0;
            for (int w = sec / 2; w + sec / 10 <= sec * 3; w += sec / 10)
            {
                std::vector<float> d ((size_t) (sec / 10));
                for (size_t i = 0; i < d.size(); ++i) d[i] = b[(size_t) w + i] - c[(size_t) w + i];
                const double lvl = 20.0 * std::log10 (rms (d, 0, d.size()) / rms (a, (size_t) w, (size_t) (w + sec / 10)));
                worst = juce::jmax (worst, std::abs (lvl));
            }
            check (worst <= 0.2, juce::String ("Sound 1: held note level stays put as notes come and go, Voice Gain Comp ")
                                     + (comp ? "on" : "off") + " (worst " + juce::String (worst, 3) + " dB, target 0.2)");
        }

        // 3. DC offset on the worst cases from the review (target: -80 dBFS or lower).
        {
            struct Case { const char* name; std::function<void (SNSynthProcessor&)> set; };
            const Case cases[] = {
                { "OTA 24 drive 100", [] (SNSynthProcessor& q) { setParam (q, ParamIDs::slot (0, ParamIDs::slotDrive), 100); } },
                { "Sallen-Key drive 100 res 80", [] (SNSynthProcessor& q) {
                      setParam (q, ParamIDs::slot (0, ParamIDs::slotType), FilterType::sallenKey);
                      setParam (q, ParamIDs::slot (0, ParamIDs::slotDrive), 100); setParam (q, ParamIDs::slot (0, ParamIDs::slotResonance), 80); } },
                { "Steiner-Parker res 90 drive 50", [] (SNSynthProcessor& q) {
                      setParam (q, ParamIDs::slot (0, ParamIDs::slotType), FilterType::steinerParker);
                      setParam (q, ParamIDs::slot (0, ParamIDs::slotDrive), 50); setParam (q, ParamIDs::slot (0, ParamIDs::slotResonance), 90); } },
                { "ring mod pulse x pulse", [] (SNSynthProcessor& q) {
                      setParam (q, ParamIDs::slot (0, ParamIDs::slotType), FilterType::off);
                      setParam (q, ParamIDs::sawLevel, 0); setParam (q, ParamIDs::pulseLevel, 100); setParam (q, ParamIDs::osc2Saw, 0);
                      setParam (q, ParamIDs::osc2Pulse, 100); setParam (q, ParamIDs::osc2Fine, 0); setParam (q, ParamIDs::ringMod, 100);
                      setParam (q, ParamIDs::osc1Level, 0); setParam (q, ParamIDs::osc2Level, 0); } },
            };
            for (auto& cs : cases)
            {
                auto q = neutral (sr);
                setParam (*q, ParamIDs::volume, 0);
                cs.set (*q);
                auto x = renderEvents (*q, { { 0, MidiMessage::noteOn (1, 48, (juce::uint8) 127) } }, (int) (sr * 2.5));
                // Window-weighted mean (Blackman-Harris), so a part-cycle at
                // the window edges doesn't read as DC.
                double mean = 0.0, wsum = 0.0;
                const size_t n = x.size() - (size_t) sec;
                for (size_t i = 0; i < n; ++i)
                {
                    const double ph = 2.0 * juce::MathConstants<double>::pi * (double) i / (double) n;
                    const double w = 0.35875 - 0.48829 * std::cos (ph) + 0.14128 * std::cos (2 * ph) - 0.01168 * std::cos (3 * ph);
                    mean += w * x[(size_t) sec + i]; wsum += w;
                }
                mean /= wsum;
                const double dcDb = 20.0 * std::log10 (juce::jmax (1.0e-12, std::abs (mean)));
                check (dcDb <= -80.0, juce::String ("Sound 3: no DC offset, ") + cs.name + " (" + juce::String (dcDb, 1) + " dBFS, target -80)");
            }
        }

        // 4. Drive changes tone, not volume (target: within 1.5 dB across 0-100%).
        for (int type : { (int) FilterType::ota, (int) FilterType::sallenKey, (int) FilterType::lowpassGate, (int) FilterType::steinerParker,
                          (int) FilterType::polivoks, (int) FilterType::comb, (int) FilterType::formant })
        {
            juce::String curve;
            double ref = 0.0, worst = 0.0;
            for (int d = 0; d <= 100; d += 10)
            {
                auto q = neutral (sr);
                setParam (*q, ParamIDs::slot (0, ParamIDs::slotType), (float) type);
                setParam (*q, ParamIDs::slot (0, ParamIDs::slotCutoff), 2000.0f);
                setParam (*q, ParamIDs::slot (0, ParamIDs::slotDrive), (float) d);
                if (type == FilterType::lowpassGate) setParam (*q, ParamIDs::slot (0, ParamIDs::slotCharacter), 100);
                auto x = renderEvents (*q, { { 0, MidiMessage::noteOn (1, 48, (juce::uint8) 127) } }, sec);
                const double lvl = 20.0 * std::log10 (rms (x, (size_t) sec / 2, x.size()));
                if (d == 0) ref = lvl;
                worst = juce::jmax (worst, std::abs (lvl - ref));
                curve << juce::String (lvl - ref, 1) << " ";
            }
            check (worst <= 1.5, "Sound 4: Drive keeps the level, " + Params::filterTypeNames()[type] + " (worst " + juce::String (worst, 2)
                                     + " dB, target 1.5; 0-100%: " + curve.trim() + ")");
        }

        // 2. FM and ring mod don't alias (target: -60 dB). Osc 2 in unison, so every real
        //    tone is a harmonic of the note; anything between harmonics is aliasing.
        //    65536-point 7-term Blackman-Harris FFT, as in the review's rig.
        {
            auto aliasDb = [&] (const std::vector<float>& x, double f0, double& totalDb)
            {
                constexpr int order = 16, W = 1 << order;
                juce::dsp::FFT fft (order);
                const double a[7] = { 0.27105140069342, 0.43329793923448, 0.21812299954311, 0.06592544638803,
                                      0.01081174209837, 0.00077658482522, 0.00001388721735 };
                std::vector<float> buf (2 * W, 0.0f);
                const int st = (int) x.size() - W;
                for (int i = 0; i < W; ++i)
                {
                    double w = 0.0;
                    for (int k = 0; k < 7; ++k) w += ((k % 2) ? -1.0 : 1.0) * a[k] * std::cos (2.0 * juce::MathConstants<double>::pi * k * i / W);
                    buf[(size_t) i] = (float) (x[(size_t) (st + i)] * w);
                }
                fft.performFrequencyOnlyForwardTransform (buf.data());
                std::vector<bool> harm ((size_t) W / 2, false);
                for (int k = 1; k * f0 < sr / 2; ++k)
                {
                    const int c = (int) std::round (k * f0 * W / sr);
                    for (int j = juce::jmax (0, c - 9); j <= juce::jmin (W / 2 - 1, c + 9); ++j) harm[(size_t) j] = true;
                }
                const int c0 = (int) std::round (f0 * W / sr);
                double fund = 0.0, fundSum = 0.0, strongest = 0.0, total = 0.0;
                for (int j = c0 - 9; j <= c0 + 9; ++j) { const double pw = (double) buf[(size_t) j] * buf[(size_t) j]; fund = juce::jmax (fund, pw); fundSum += pw; }
                for (int j = 0; j < W / 2; ++j)
                {
                    const double f = j * sr / W;
                    if (f < 20.0 || f > 20000.0 || harm[(size_t) j]) continue;
                    const double pw = (double) buf[(size_t) j] * buf[(size_t) j];
                    strongest = juce::jmax (strongest, pw); total += pw;
                }
                totalDb = 10.0 * std::log10 (total / fundSum + 1.0e-30);
                return 10.0 * std::log10 (strongest / fund + 1.0e-30);
            };
            struct Case { const char* name; int note; std::function<void (SNSynthProcessor&)> set; };
            const Case cases[] = {
                { "FM 2>1 50%, sine into saw, A5", 81, [] (SNSynthProcessor& q) { setParam (q, ParamIDs::osc2Sine, 100); setParam (q, ParamIDs::osc2Saw, 0);
                                                                                setParam (q, ParamIDs::fm2to1, 50); } },
                { "FM 2>1 50%, saw into saw, A5", 81, [] (SNSynthProcessor& q) { setParam (q, ParamIDs::osc2Saw, 100); setParam (q, ParamIDs::fm2to1, 50); } },
                { "FM 1>2 100%, saw into saw, C7", 96, [] (SNSynthProcessor& q) { setParam (q, ParamIDs::osc2Saw, 100); setParam (q, ParamIDs::fm1to2, 100); } },
                { "Ring mod saw x saw, C7", 96, [] (SNSynthProcessor& q) { setParam (q, ParamIDs::osc2Saw, 100); setParam (q, ParamIDs::ringMod, 100);
                                                                         setParam (q, ParamIDs::osc1Level, 0); setParam (q, ParamIDs::osc2Level, 0); } },
            };
            for (auto& cs : cases)
            {
                auto q = neutral (sr);
                setParam (*q, ParamIDs::slot (0, ParamIDs::slotType), FilterType::off);
                setParam (*q, ParamIDs::osc2Fine, 0); setParam (*q, ParamIDs::osc2Coarse, 0);
                setParam (*q, ParamIDs::osc2Level, 100);
                cs.set (*q);
                auto x = renderEvents (*q, { { 0, MidiMessage::noteOn (1, cs.note, (juce::uint8) 127) } }, (int) (sr * 2.0));
                double total = 0.0;
                const double strongest = aliasDb (x, 440.0 * std::pow (2.0, (cs.note - 69) / 12.0), total);
                check (strongest <= -60.0, juce::String ("Sound 2: ") + cs.name + " alias strongest " + juce::String (strongest, 1)
                                               + " dB, total " + juce::String (total, 1) + " dB (target -60)");
            }
        }

        // 5. DAW automation glides instead of stepping, at any buffer size.
        //    Cutoff swept 200 Hz -> 5 kHz over 1 s, one update per block, on a 440 Hz sine through OTA 24 res 50.
        {
            auto sweep = [&] (int block)
            {
                auto q = neutral (sr);
                setParam (*q, ParamIDs::sawLevel, 0); setParam (*q, ParamIDs::sineLevel, 100);
                setParam (*q, ParamIDs::slot (0, ParamIDs::slotCutoff), 200);
                setParam (*q, ParamIDs::slot (0, ParamIDs::slotResonance), 50);
                const int total = (int) (sr * 1.5), start = (int) (sr * 0.25), len = sec;
                std::vector<float> out ((size_t) total);
                juce::AudioBuffer<float> buf (2, block);
                for (int pos = 0; pos < total; pos += block)
                {
                    if (pos >= start && pos < start + len)
                        setParam (*q, ParamIDs::slot (0, ParamIDs::slotCutoff), (float) (200.0 * std::pow (25.0, (double) (pos - start) / len)));
                    juce::MidiBuffer midi;
                    if (pos == 0) midi.addEvent (MidiMessage::noteOn (1, 69, (juce::uint8) 100), 0);
                    const int n = juce::jmin (block, total - pos);
                    juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
                    q->processBlock (view, midi);
                    std::copy (buf.getReadPointer (0), buf.getReadPointer (0) + n, out.begin() + pos);
                }
                if (auto* dir = std::getenv ("SN_DUMP"))
                    juce::File (juce::String (dir) + "/sweep_b" + juce::String (block) + ".f32").replaceWithData (out.data(), out.size() * sizeof (float));
                return out;
            };
            // Stepping shows up as sidebands 100-2000 Hz either side of the 440 Hz tone
            // (same method as the review's rig: 4096-point 7-term Blackman-Harris windows).
            auto sidebands = [&] (const std::vector<float>& x)
            {
                constexpr int order = 12, W = 1 << order;
                juce::dsp::FFT fft (order);
                const double a[7] = { 0.27105140069342, 0.43329793923448, 0.21812299954311, 0.06592544638803,
                                      0.01081174209837, 0.00077658482522, 0.00001388721735 };
                double worst = -400.0;
                for (int st = (int) (0.3 * sr); st + W <= (int) (1.2 * sr); st += W / 2)
                {
                    std::vector<float> buf (2 * W, 0.0f);
                    for (int i = 0; i < W; ++i)
                    {
                        double w = 0.0;
                        for (int k = 0; k < 7; ++k) w += ((k % 2) ? -1.0 : 1.0) * a[k] * std::cos (2.0 * juce::MathConstants<double>::pi * k * i / W);
                        buf[(size_t) i] = (float) (x[(size_t) (st + i)] * w);
                    }
                    fft.performFrequencyOnlyForwardTransform (buf.data());
                    const int c0 = (int) std::round (440.0 * W / sr);
                    double fund = 0.0, side = 0.0;
                    for (int k = c0 - 9; k <= c0 + 9; ++k) fund += (double) buf[(size_t) k] * buf[(size_t) k];
                    for (int k = 1; k < W / 2; ++k)
                    {
                        const double f = k * sr / W, df = std::abs (f - 440.0);
                        bool sel = df > 100.0 && df < 2000.0;
                        for (int h = 2; h < 6; ++h) sel = sel && std::abs (f - 440.0 * h) > 82.0;
                        if (sel) side += (double) buf[(size_t) k] * buf[(size_t) k];
                    }
                    worst = juce::jmax (worst, 10.0 * std::log10 (side / fund + 1.0e-30));
                }
                return worst;
            };
            for (int block : { 32, 512, 2048 })
            {
                const double sb = sidebands (sweep (block));
                check (sb <= -80.0, "Sound 5: cutoff automation at buffer " + juce::String (block) + " doesn't step (sidebands "
                                        + juce::String (sb, 1) + " dB, target -80)");
            }
        }

        // 6. Voice steal with very different velocities doesn't click (target: 1.2x a normal step).
        {
            auto q = neutral (sr);
            setParam (*q, ParamIDs::sawLevel, 0); setParam (*q, ParamIDs::sineLevel, 100);
            setParam (*q, ParamIDs::slot (0, ParamIDs::slotType), FilterType::off);
            setParam (*q, ParamIDs::voices, 2); setParam (*q, ParamIDs::velocityAmount, 50);
            setParam (*q, ParamIDs::aenvAttack, 0.0005f); setParam (*q, ParamIDs::aenvRelease, 0.001f);
            setParam (*q, ParamIDs::volume, 0);
            const int t3 = sec;
            auto x = renderEvents (*q, { { 0, MidiMessage::noteOn (1, 69, (juce::uint8) 127) },
                                         { sec / 2, MidiMessage::noteOn (1, 72, (juce::uint8) 20) },
                                         { t3, MidiMessage::noteOn (1, 76, (juce::uint8) 127) } }, sec * 2);
            double steady = 0.0, atSteal = 0.0;
            for (size_t i = (size_t) (sec * 3 / 2); i < (size_t) (sec * 2) - 1; ++i) steady = juce::jmax (steady, (double) std::abs (x[i + 1] - x[i]));
            for (size_t i = (size_t) t3; i < (size_t) t3 + 300; ++i) atSteal = juce::jmax (atSteal, (double) std::abs (x[i + 1] - x[i]));
            const double ratio = atSteal / steady;
            check (ratio <= 1.2, "Sound 6: stealing a voice doesn't click (step " + juce::String (ratio, 2) + "x normal, target 1.2x)");
        }

        // Same sound at any buffer size: a busy default patch (chord, notes close together,
        // LFO routing) rendered at buffers 7, 64 and 512 must match exactly.
        {
            std::vector<Event> ev;
            const int offs[] = { 100, 117, 133, 160, 999, 1013, 5000, 5001 };
            int k = 0;
            for (int o : offs) ev.push_back ({ o, MidiMessage::noteOn (1, 48 + 3 * k++, (juce::uint8) (60 + 7 * k)) });
            ev.push_back ({ 20000, MidiMessage::noteOff (1, 48) });
            ev.push_back ({ 20010, MidiMessage::pitchWheel (1, 12000) });
            ev.push_back ({ 20500, MidiMessage::controllerEvent (1, 1, 90) });
            auto setup = [&] { auto q = makeSynth (sr); q->modMatrix.set (Mod::lfo1, Mod::filterTarget (0, 0), 0.2f);
                               q->modMatrix.set (Mod::modWheel, Mod::filterTarget (0, 0), 0.3f); return q; };
            auto ref = renderEvents (*setup(), ev, sec, 512);
            for (int block : { 7, 64 })
            {
                auto y = renderEvents (*setup(), ev, sec, block);
                double worst = 0.0;
                for (size_t i = 0; i < y.size(); ++i) worst = juce::jmax (worst, (double) std::abs (y[i] - ref[i]));
                check (! (worst > 0.0), "Same sound at buffer " + juce::String (block) + " and 512 (largest difference "
                                         + juce::String (juce::Decibels::gainToDecibels ((float) worst, -400.0f), 1) + " dBFS)");
            }
        }

        // Modulation keeps its full depth: a 10 Hz LFO tremolo on Volume is as deep as a 1 Hz one
        // (it used to lose about 4 dB going through the knob smoother).
        {
            auto depthDb = [&] (float rate)
            {
                auto q = neutral (sr);
                setParam (*q, ParamIDs::sawLevel, 0); setParam (*q, ParamIDs::sineLevel, 100);
                setParam (*q, ParamIDs::slot (0, ParamIDs::slotType), FilterType::off);
                setParam (*q, ParamIDs::lfoRate, rate); setParam (*q, ParamIDs::lfo1Shape, 0);
                q->modMatrix.set (Mod::lfo1, Mod::volume, 0.3f);
                auto x = renderEvents (*q, { { 0, MidiMessage::noteOn (1, 81, (juce::uint8) 127) } }, sec * 3);
                const int win = (int) (sr / 880.0 * 4); // 4 cycles of the tone
                double lo = 1e9, hi = 0.0;
                for (int st = sec; st + win < sec * 3; st += win / 4)
                {
                    const double r = rms (x, (size_t) st, (size_t) (st + win));
                    lo = juce::jmin (lo, r); hi = juce::jmax (hi, r);
                }
                return 20.0 * std::log10 (hi / lo);
            };
            const double slow = depthDb (1.0f), fast = depthDb (10.0f);
            check (std::abs (slow - fast) < 1.0, "Modulation keeps its depth: Volume tremolo " + juce::String (slow, 1) + " dB at 1 Hz, "
                                                    + juce::String (fast, 1) + " dB at 10 Hz");
        }

        // 7. Rearranging the chain while the sound reads it never gives a half-changed order (target: 0 in 10 million reads).
        {
            auto q = makeSynth (sr);
            std::atomic<bool> stop { false };
            std::thread ui ([&] { int k = 0; while (! stop) { q->moveBlock (k % 5, (k * 3 + 1) % 5); ++k; } });
            long bad = 0;
            for (int i = 0; i < 10000000; ++i)
            {
                auto o = q->getChainOrder();
                std::sort (o.begin(), o.end());
                if (o != Chain::defaultOrder()) ++bad;
            }
            stop = true; ui.join();
            check (bad == 0, "Sound 7: chain order is never read half-changed (" + juce::String ((juce::int64) bad) + " bad reads in 10 million)");
        }
    }
}

#include "Phase4Checks.inc"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const double sr = 48000.0;

    if (argc > 1 && juce::String (argv[1]) == "--write-references")
    {
        StressChecks::writeReferences();
        return 0;
    }
    if (argc > 1 && (juce::String (argv[1]) == "--stress" || juce::String (argv[1]) == "--bench"))
    {
        if (juce::String (argv[1]) == "--stress") StressChecks::run(); else StressChecks::bench();
        std::cout << (failures == 0 ? "All checks passed" : juce::String (failures) + " check(s) failed") << std::endl;
        return failures == 0 ? 0 : 1;
    }
    if (argc > 1 && juce::String (argv[1]) == "--sound")
    {
        SoundChecks::run (sr);
        Phase4Checks::run (sr);
        std::cout << (failures == 0 ? "All checks passed" : juce::String (failures) + " check(s) failed") << std::endl;
        return failures == 0 ? 0 : 1;
    }

    // A short audio demo of the drum machine: 2 bars straight, 2 bars with swing.
    if (argc > 2 && juce::String (argv[1]) == "--drumdemo")
    {
        auto p = makeSynth (sr);
        setParam (*p, ParamIDs::drumPlay, 1.0f);
        const int bar = (int) (sr * 2.0), block = 512;
        juce::AudioBuffer<float> out (2, bar * 4), buf (2, block);
        for (int pos = 0; pos < out.getNumSamples(); pos += block)
        {
            if (pos >= bar * 2) setParam (*p, ParamIDs::drumSwing, 45.0f);
            juce::MidiBuffer midi;
            p->processBlock (buf, midi);
            const int len = juce::jmin (block, out.getNumSamples() - pos);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, len);
        }
        const juce::File f { juce::String (argv[2]) };
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 16, {}, 0));
        w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
        return 0;
    }

    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
        return saveSnapshot (juce::File (juce::String (argv[2])));

    // Envelope display: the curve it draws (by time) and the dot (by level)
    // must match the real envelope, including a release that starts mid-attack
    // and an attack retriggered from a non-zero level.
    {
        Envelope e;
        e.setSampleRate (sr);
        const float A = 0.2f, D = 0.5f, S = 0.4f, R = 0.8f;
        e.setParameters (A, D, S, R);
        float worstTime = 0.0f, worstLevel = 0.0f;
        auto run = [&] (int samples)
        {
            for (int i = 0; i < samples; ++i)
            {
                e.next();
                const int st = e.stageIndex();
                if (st != 1 && st != 2 && st != 4)
                    continue;
                const float knob = st == 1 ? A : st == 2 ? D : R;
                const float start = st == 2 ? 1.0f : e.stageStartLevel();
                const float total = Envelope::stageSeconds (st, knob, start, S);
                const float fTime = total > 0.0f ? (float) e.secondsInStage() / total : 1.0f;
                worstTime = juce::jmax (worstTime, std::abs (Envelope::curveLevel (st, fTime, start, S) - e.level()));
                const float fLevel = Envelope::curveFraction (st, e.level(), start, S);
                worstLevel = juce::jmax (worstLevel, std::abs (Envelope::curveLevel (st, fLevel, start, S) - e.level()));
            }
        };
        e.noteOn();  run ((int) (sr * 1.2));   // attack, decay, sustain
        e.noteOff(); run ((int) (sr * 0.3));   // part of the release
        e.noteOn();  run ((int) (sr * 0.05));  // retrigger from mid-release, part of the attack
        e.noteOff(); run ((int) (sr * 1.0));   // release from mid-attack
        check (worstTime < 0.005f, "Envelope display curve matches the real envelope over time (worst " + juce::String (worstTime, 5) + ")");
        check (worstLevel < 1.0e-4f, "Envelope dot sits on the curve at the real level (worst " + juce::String (worstLevel, 6) + ")");
    }

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
        check (total == 95, "Synth has 95 controls (found " + juce::String (total) + ")");

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
        check (finite && peak <= 1.0f && peak > 0.001f, name + ": stable at full Resonance and Drive (peak " + juce::String (peak, 3) + ")");
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
        setParam (*p, ParamIDs::voiceGainComp, 0);
        setParam (*p, ParamIDs::limiter, 0);
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


    //==========================================================================
    // Output safety
    auto peakOf = [] (const juce::AudioBuffer<float>& b)
    {
        float peak = 0.0f; bool finite = true;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float x = b.getSample (ch, i);
                if (! std::isfinite (x)) finite = false;
                peak = juce::jmax (peak, std::abs (x));
            }
        return finite ? peak : 99.0f;
    };
    auto chordRender = [&] (SNSynthProcessor& p, std::initializer_list<int> notes, int blocks)
    {
        juce::MidiBuffer m;
        for (int n : notes) m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 127), 0);
        juce::AudioBuffer<float> block (2, 512);
        p.processBlock (block, m);
        return heldBlocks (p, blocks);
    };
    {
        auto p = makeSynth (sr);
        auto out = chordRender (*p, { 48, 52, 55, 59, 60, 64, 67, 71 }, 60);
        const float peak = peakOf (out);
        check (peak < 0.6f, "Default 8-note chord leaves headroom (peak " + juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dB)");
    }
    for (bool limiterOn : { true, false })
    {
        // Worst case: 3 resonant Formant slots, all EQ +12, Volume +6, 16 notes.
        auto p = makeSynth (sr);
        setParam (*p, ParamIDs::limiter, limiterOn ? 1.0f : 0.0f);
        setParam (*p, ParamIDs::voiceGainComp, 0);
        setParam (*p, ParamIDs::voices, 16);
        for (int sl = 0; sl < 3; ++sl)
        {
            setParam (*p, ParamIDs::slot (sl, ParamIDs::slotType), FilterType::formant);
            setParam (*p, ParamIDs::slot (sl, ParamIDs::slotResonance), 100);
            setParam (*p, ParamIDs::slot (sl, ParamIDs::slotDrive), 100);
            setParam (*p, ParamIDs::slot (sl, ParamIDs::slotCutoff), 1000);
        }
        for (auto* id : { ParamIDs::eqLow, ParamIDs::eqMid, ParamIDs::eqHigh }) setParam (*p, id, 12);
        setParam (*p, ParamIDs::volume, 6);
        setParam (*p, ParamIDs::osc2Level, 100);
        setParam (*p, ParamIDs::subLevel, 100);
        auto out = chordRender (*p, { 36, 40, 43, 47, 48, 52, 55, 59, 60, 64, 67, 71, 72, 76, 79, 83 }, 60);
        const float peak = peakOf (out);
        check (peak <= 0.967f, juce::String ("Worst-case chord never goes over -0.3 dB, Limiter ") + (limiterOn ? "on" : "off")
                                   + " (peak " + juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dB)");
        if (limiterOn)
            check (p->live.limiting.load(), "Limiter reports that it is working");
    }

    //==========================================================================
    // Modulation
    {
        // An oscillator at Level 0 still works as an FM source (M's question).
        auto plain = oneOsc (0);
        setParam (*plain, ParamIDs::slot (0, ParamIDs::slotType), FilterType::off);
        setParam (*plain, ParamIDs::sawLevel, 0);
        setParam (*plain, ParamIDs::sineLevel, 100);
        auto a = render (*plain, 57, 60, false);
        auto fm = oneOsc (0);
        setParam (*fm, ParamIDs::slot (0, ParamIDs::slotType), FilterType::off);
        setParam (*fm, ParamIDs::sawLevel, 0);
        setParam (*fm, ParamIDs::sineLevel, 100);
        setParam (*fm, ParamIDs::fm2to1, 80); // Osc 2 Level stays at 0
        auto b = render (*fm, 57, 60, false);
        const float ba = brightness (a, 9600, 12000), bb = brightness (b, 9600, 12000);
        check (paramValue (*fm, ParamIDs::osc2Level) < 0.01f && bb > ba * 1.5f,
               "FM 2 > 1 works with Osc 2 Level at 0 (x" + juce::String (bb / ba, 1) + " brighter)");
    }
    {
        // LFO 1 routed to Filter 1 Cutoff moves the brightness.
        auto still = makeSynth (sr);
        setParam (*still, ParamIDs::drift, 0);
        setParam (*still, ParamIDs::slot (0, ParamIDs::slotCutoff), 500);
        setParam (*still, ParamIDs::slot (0, ParamIDs::slotEnv), 0);
        auto a = render (*still, 45, 120, false);
        auto moving = makeSynth (sr);
        setParam (*moving, ParamIDs::drift, 0);
        setParam (*moving, ParamIDs::slot (0, ParamIDs::slotCutoff), 500);
        setParam (*moving, ParamIDs::slot (0, ParamIDs::slotEnv), 0);
        setParam (*moving, ParamIDs::lfoRate, 2);
        moving->modMatrix.set (Mod::lfo1, Mod::filterTarget (0, 0), 0.3f);
        auto b = render (*moving, 45, 120, false);
        auto spread = [&] (const juce::AudioBuffer<float>& buf)
        {
            float lo = 1e9f, hi = 0.0f;
            for (int k = 0; k < 20; ++k) { const float v = brightness (buf, 9600 + k * 2400, 2400); lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); }
            return hi / juce::jmax (lo, 1e-9f);
        };
        check (spread (b) > spread (a) * 2.0f, "LFO 1 > Filter 1 Cutoff routing moves the brightness (x" + juce::String (spread (b), 1) + " vs x" + juce::String (spread (a), 1) + ")");
    }
    {
        // Velocity routed to Osc 1 Level: soft notes quieter than hard ones beyond the fixed amount.
        auto level = [&] (int vel, bool routed)
        {
            auto p = makeSynth (sr);
            setParam (*p, ParamIDs::velocityAmount, 0);
            setParam (*p, ParamIDs::osc1Level, 0);
            if (routed) p->modMatrix.set (Mod::velocity, Mod::osc1Level, 1.0f);
            juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) vel), 0);
            juce::AudioBuffer<float> block (2, 512);
            p->processBlock (block, m);
            auto out = heldBlocks (*p, 30);
            return out.getRMSLevel (0, 4800, 9600);
        };
        const float soft = level (30, true), hard = level (127, true), none = level (127, false);
        check (hard > soft * 2.0f && none < 1.0e-4f, "Velocity > Osc 1 Level: harder notes are louder (" + juce::String (soft, 3) + " vs " + juce::String (hard, 3) + ")");
    }
    {
        // Mod wheel routed to Osc 1 Fine bends the pitch up.
        auto p = oneOsc (0);
        setParam (*p, ParamIDs::slot (0, ParamIDs::slotType), FilterType::off);
        p->modMatrix.set (Mod::modWheel, Mod::osc1Fine, 0.5f); // half of the -100..+100 range = +100 cents at full wheel
        send (*p, { juce::MidiMessage::controllerEvent (1, 1, 127), juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100) });
        auto out = heldBlocks (*p, 60);
        const double f = measureFreq (out, 4800, 24000, sr);
        const double expected = 220.0 * std::pow (2.0, 1.0 / 12.0);
        check (std::abs (f - expected) < 3.0, "Mod Wheel > Osc 1 Fine raises the pitch (" + juce::String (f, 1) + " Hz)");
    }
    {
        // LFO 2 > Volume makes the level move (a global target).
        auto p = makeSynth (sr);
        setParam (*p, ParamIDs::lfo2Rate, 4);
        setParam (*p, ParamIDs::lfo2Shape, 4); // Square
        p->modMatrix.set (Mod::lfo2, Mod::volume, -0.5f);
        auto out = render (*p, 57, 100, false);
        float lo = 1e9f, hi = 0.0f;
        for (int k = 0; k < 30; ++k) { const float v = out.getRMSLevel (0, 9600 + k * 1200, 1200); lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); }
        check (hi > lo * 3.0f, "LFO 2 (Square) > Volume makes the level pulse (x" + juce::String (hi / lo, 1) + ")");
    }
    {
        // LFO shapes
        bool ok = std::abs (SNSynthProcessor::lfoShapeValue (0, 0.25, 0) - 1.0f) < 1e-4f
               && std::abs (SNSynthProcessor::lfoShapeValue (1, 0.5, 0) - 1.0f) < 1e-4f
               && std::abs (SNSynthProcessor::lfoShapeValue (2, 0.0, 0) + 1.0f) < 1e-4f
               && std::abs (SNSynthProcessor::lfoShapeValue (3, 0.0, 0) - 1.0f) < 1e-4f
               && SNSynthProcessor::lfoShapeValue (4, 0.7, 0) < 0.0f
               && std::abs (SNSynthProcessor::lfoShapeValue (5, 0.3, 0.42f) - 0.42f) < 1e-6f;
        check (ok, "LFO shapes: Sine, Triangle, Saw Up, Saw Down, Square, Random");
    }
    {
        // Routings are saved and reloaded; old "LFO to Cutoff" becomes routings.
        auto p = makeSynth (sr);
        p->modMatrix.set (Mod::ampEnv, Mod::filterTarget (2, 1), -0.25f);
        p->modMatrix.set (Mod::key, Mod::eqHigh, 0.4f);
        juce::MemoryBlock state;
        p->getStateInformation (state);
        SNSynthProcessor q;
        q.setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (q.modMatrix.get (Mod::ampEnv, Mod::filterTarget (2, 1)) + 0.25f) < 1e-4f
                   && std::abs (q.modMatrix.get (Mod::key, Mod::eqHigh) - 0.4f) < 1e-4f
                   && std::abs (q.modMatrix.get (Mod::lfo1, Mod::osc1Saw)) < 1e-6f,
               "Modulation routings are saved and reloaded");

        auto old = makeSynth (sr);
        setParam (*old, ParamIDs::lfoCutoff, 50);
        juce::MemoryBlock s2;
        old->getStateInformation (s2);
        SNSynthProcessor r;
        r.setStateInformation (s2.getData(), (int) s2.getSize());
        const float a = r.modMatrix.get (Mod::lfo1, Mod::filterTarget (1, 0));
        check (std::abs (a - 0.15f) < 1e-3f && paramValue (r, ParamIDs::lfoCutoff) < 0.01f,
               "Older projects' LFO to Cutoff becomes LFO 1 routings (" + juce::String (a, 3) + ")");
    }
    {
        // Oversampling and Level Match switches change the sound, not the pitch.
        auto p = makeSynth (sr);
        setParam (*p, ParamIDs::oversampling, 0);
        auto a = render (*p, 69, 60, false);
        auto q = makeSynth (sr);
        setParam (*q, ParamIDs::oversampling, 2);
        auto b = render (*q, 69, 60, false);
        check (std::abs (measureFreq (a, 9600, 12000, sr) - 440.0) < 4.0 && std::abs (measureFreq (b, 9600, 12000, sr) - 440.0) < 4.0,
               "Oversampling 1x and 4x both play in tune");
    }


    //==========================================================================
    // Reorderable chain
    {
        auto p = makeSynth (sr);
        const auto d = p->getChainOrder();
        check (d == Chain::defaultOrder(), "Chain starts as Filter 1, 2, 3, Amp, EQ");

        p->moveBlock (3, 0); // Amp to the front
        p->moveBlock (4, 1); // EQ second
        juce::MemoryBlock state;
        p->getStateInformation (state);
        SNSynthProcessor q;
        q.setStateInformation (state.getData(), (int) state.getSize());
        const auto o = q.getChainOrder();
        check (o == std::array<int, 5> { 3, 4, 0, 1, 2 }, "Chain order is saved and reloaded (Amp, EQ, Filter 1, 2, 3)");

        // A project from before the chain: only the filter order was saved.
        auto tree = p->parameters.copyState();
        tree.removeProperty (StateIDs::chainOrder, nullptr);
        tree.setProperty (StateIDs::slotOrder, "2,0,1", nullptr);
        juce::MemoryBlock old;
        if (auto xml = tree.createXml()) juce::AudioProcessor::copyXmlToBinary (*xml, old);
        SNSynthProcessor r;
        r.setStateInformation (old.getData(), (int) old.getSize());
        check (r.getChainOrder() == std::array<int, 5> { 2, 0, 1, 3, 4 }, "Older projects keep their filter order, with Amp and EQ after");
    }
    {
        // Amp before a driven filter: quieter parts of the envelope drive the
        // filter less, so the tone changes, not just the level.
        auto measure = [&] (bool ampFirst)
        {
            auto p = makeSynth (sr);
            setParam (*p, ParamIDs::drift, 0);
            setParam (*p, ParamIDs::slot (0, ParamIDs::slotDrive), 100);
            setParam (*p, ParamIDs::slot (0, ParamIDs::slotEnv), 0);
            setParam (*p, ParamIDs::aenvSustain, 15);
            if (ampFirst) p->moveBlock (3, 0);
            auto out = render (*p, 45, 60, false);
            return brightness (out, 14400, 9600) / juce::jmax (1.0e-6f, out.getRMSLevel (0, 14400, 9600));
        };
        const float last = measure (false), first = measure (true);
        check (std::abs (first - last) / last > 0.1f,
               "Moving Amp before a driven filter changes the tone (" + juce::String (last, 3) + " vs " + juce::String (first, 3) + ")");
    }
    {
        // EQ placed first still works.
        auto ref = makeSynth (sr);
        auto a = render (*ref, 33, 40, false);
        auto p = makeSynth (sr);
        p->moveBlock (4, 0);
        setParam (*p, ParamIDs::eqLow, 12);
        auto b = render (*p, 33, 40, false);
        const float gain = b.getRMSLevel (0, 4800, 9600) / a.getRMSLevel (0, 4800, 9600);
        check (gain > 1.3f, "EQ moved to the front still boosts the bass (x" + juce::String (gain, 2) + ")");
    }


    //==========================================================================
    // LFOs modulating LFO speed
    {
        auto pulses = [&] (bool modulated, bool selfMod)
        {
            auto p = makeSynth (sr);
            setParam (*p, ParamIDs::lfo2Rate, 2.0f);
            setParam (*p, ParamIDs::lfo2Shape, 4); // Square
            setParam (*p, ParamIDs::lfoRate, 0.5f);
            setParam (*p, ParamIDs::lfo1Shape, 4);
            p->modMatrix.set (Mod::lfo2, Mod::volume, -0.5f);
            if (modulated) p->modMatrix.set (Mod::lfo1, Mod::lfo2Rate, 0.4f);
            if (selfMod)   p->modMatrix.set (Mod::lfo2, Mod::lfo2Rate, 0.5f);
            auto out = render (*p, 57, 400, false); // about 4.3 s
            // Count level jumps (square LFO edges) in 10 ms windows.
            int edges = 0; bool loud = false; bool first = true; bool finite = true;
            for (int k = 2; k < out.getNumSamples() / 480; ++k)
            {
                const float r = out.getRMSLevel (0, k * 480, 480);
                if (! std::isfinite (r)) finite = false;
                const bool l = r > 0.02f;
                if (! first && l != loud) ++edges;
                loud = l; first = false;
            }
            return finite ? edges : -1;
        };
        const int plain = pulses (false, false), crossed = pulses (true, false), self = pulses (false, true);
        check (crossed > plain + 4, "LFO 1 > LFO 2 Rate speeds LFO 2 up and down (" + juce::String (plain) + " vs " + juce::String (crossed) + " edges)");
        check (self >= 0 && self != plain, "LFO 2 can modulate its own Rate and stays stable (" + juce::String (self) + " edges)");
    }

    // Drum machine
    {
        auto renderSilent = [] (SNSynthProcessor& p, int numSamples)
        {
            const int blockSize = 512;
            juce::AudioBuffer<float> out (2, numSamples);
            juce::AudioBuffer<float> block (2, blockSize);
            for (int pos = 0; pos < numSamples; pos += blockSize)
            {
                juce::MidiBuffer midi;
                p.processBlock (block, midi);
                out.copyFrom (0, pos, block, 0, 0, juce::jmin (blockSize, numSamples - pos));
                out.copyFrom (1, pos, block, 1, 0, juce::jmin (blockSize, numSamples - pos));
            }
            return out;
        };
        auto onlyStep = [] (SNSynthProcessor& p, int drum, std::initializer_list<int> steps)
        {
            for (int d = 0; d < Drums::numDrums; ++d)
                for (int s = 0; s < Drums::numSteps; ++s)
                    p.drumPattern.set (d, s, false);
            for (int s : steps)
                p.drumPattern.set (drum, s, true);
        };
        auto firstAbove = [] (const juce::AudioBuffer<float>& b, int from, float threshold)
        {
            for (int i = from; i < b.getNumSamples(); ++i)
                if (std::abs (b.getSample (0, i)) > threshold)
                    return i;
            return -1;
        };
        const int stepLen = (int) (sr * 60.0 / 120.0 / 4.0); // one 16th at 120 BPM

        // Every drum makes a sound, and stays well-behaved.
        {
            juce::StringArray quiet;
            bool allFinite = true;
            float loudest = 0.0f;
            for (int d = 0; d < Drums::numDrums; ++d)
            {
                auto p = makeSynth (sr);
                p->auditionDrum (d);
                auto out = renderSilent (*p, (int) sr);
                const float peak = out.getMagnitude (0, 0, out.getNumSamples());
                loudest = juce::jmax (loudest, peak);
                std::cout << "      " << Drums::drumName (d) << " peak " << juce::Decibels::gainToDecibels (peak) << " dB" << std::endl;
                if (peak < 0.02f) quiet.add (Drums::drumName (d));
                for (int i = 0; i < out.getNumSamples(); ++i)
                    allFinite = allFinite && std::isfinite (out.getSample (0, i));
                check (out.getMagnitude (0, (int) sr - 4800, 4800) < 1.0e-3f,
                       juce::String (Drums::drumName (d)) + " dies away within a second at default Decay");
            }
            check (quiet.isEmpty() && allFinite, "All five drums make a sound (quiet: " + quiet.joinIntoString (", ") + ")");
            check (loudest < 0.6f, "A single drum hit leaves headroom at default settings (peak " + juce::String (juce::Decibels::gainToDecibels (loudest), 1) + " dB)");
        }

        // Stopped: silence.
        {
            auto p = makeSynth (sr);
            auto out = renderSilent (*p, (int) sr);
            check (out.getMagnitude (0, 0, out.getNumSamples()) < 1.0e-6f, "Drums are silent until Play is pressed");
        }

        // Timing at 120 BPM: kick on steps 0 and 8 lands on 0 and exactly 8 sixteenths later.
        {
            auto p = makeSynth (sr);
            onlyStep (*p, Drums::kick, { 0, 8 });
            setParam (*p, ParamIDs::drumPlay, 1.0f);
            auto out = renderSilent (*p, (int) (sr * 1.5));
            // Timed as a DAW hears it: minus the limiter's reported look-ahead.
            const int lat = p->getLatencySamples();
            const int first = firstAbove (out, 0, 0.01f) - lat;
            const int second = firstAbove (out, 8 * stepLen - 100, 0.01f) - lat;
            check (first >= 0 && first < 64 && std::abs (second - 8 * stepLen) < 64,
                   "Kick on steps 1 and 9 plays on time at 120 BPM (at " + juce::String (first) + " and " + juce::String (second) + " samples)");
        }

        // Swing pushes the off-beat 16ths later (100% = half a step).
        {
            auto p = makeSynth (sr);
            onlyStep (*p, Drums::rim, { 1 });
            setParam (*p, ParamIDs::drumSwing, 100.0f);
            setParam (*p, ParamIDs::drumPlay, 1.0f);
            auto out = renderSilent (*p, (int) sr);
            const int at = firstAbove (out, 0, 0.01f) - p->getLatencySamples();
            const int expected = stepLen + stepLen / 2;
            check (std::abs (at - expected) < 64, "Swing 100% moves step 2 half a step later (at " + juce::String (at) + ", expected " + juce::String (expected) + ")");
        }

        // Routing: a hat sent into Filter 1 set dark is much quieter than dry.
        {
            auto hatLevel = [&] (int route)
            {
                auto p = makeSynth (sr);
                setParam (*p, ParamIDs::slot (0, ParamIDs::slotCutoff), 150.0f);
                setParam (*p, ParamIDs::drum (Drums::hat, ParamIDs::drumRoute), (float) route);
                p->auditionDrum (Drums::hat);
                auto out = renderSilent (*p, (int) (sr * 0.3));
                return out.getRMSLevel (0, 0, out.getNumSamples());
            };
            const float dry = hatLevel (Drums::dry), filtered = hatLevel (Drums::toFilter1);
            check (dry > 0.005f && filtered < dry * 0.2f, "Hat routed into Filter 1 goes through it (dry " + juce::String (dry, 4) + ", filtered " + juce::String (filtered, 4) + ")");
        }

        // The pattern is saved with the project.
        {
            auto p = makeSynth (sr);
            onlyStep (*p, Drums::clap, { 3, 11 });
            juce::MemoryBlock state;
            p->getStateInformation (state);
            auto q = makeSynth (sr);
            q->setStateInformation (state.getData(), (int) state.getSize());
            bool same = true;
            for (int d = 0; d < Drums::numDrums; ++d)
                for (int st = 0; st < Drums::numSteps; ++st)
                    same = same && p->drumPattern.get (d, st) == q->drumPattern.get (d, st);
            check (same && q->drumPattern.get (Drums::clap, 11), "Drum pattern saves and reloads");
        }

        // Everything at once (all drums, every step, full level, chord) stays under the ceiling.
        {
            auto p = makeSynth (sr);
            for (int d = 0; d < Drums::numDrums; ++d)
            {
                for (int st = 0; st < Drums::numSteps; ++st)
                    p->drumPattern.set (d, st, true);
                setParam (*p, ParamIDs::drum (d, ParamIDs::drumLevel), 100.0f);
            }
            setParam (*p, ParamIDs::volume, 6.0f);
            setParam (*p, ParamIDs::limiter, 0.0f);
            setParam (*p, ParamIDs::drumPlay, 1.0f);
            auto out = render (*p, 60, 200, false);
            const float peak = out.getMagnitude (0, 0, out.getNumSamples());
            check (peak <= 0.9661f, "Drums flat out with the Limiter off stay under -0.3 dB (peak " + juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dB)");
        }
    }

    // DSP quality (from the DSP review): oversampling treble, OTA 24 dB, smoothing.
    {
        const double fs = 44100.0;
        auto sineGainDb = [&] (FilterSlot& f, const FilterSlotSettings& st, double hz)
        {
            f.prepare ((float) fs);
            double inSq = 0.0, outSq = 0.0;
            const int n = 16384;
            for (int i = 0; i < n; ++i)
            {
                const float x = 0.05f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / fs);
                const float y = f.process (x, st, st.cutoffHz);
                if (i >= n / 2) { inSq += x * x; outSq += y * y; }
            }
            return 10.0 * std::log10 (outSq / inSq);
        };

        // The rate-doubling filters on their own: up then down, nothing in between.
        {
            juce::String report;
            double worst = 0.0;
            for (double hz : { 1000.0, 10000.0, 15000.0, 19000.0 })
            {
                Halfband h;
                h.design (20000.0 / (2.0 * fs));
                double inSq = 0.0, outSq = 0.0;
                for (int i = 0; i < 16384; ++i)
                {
                    const float x = (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / fs);
                    float a, b;
                    h.up (x, a, b);
                    const float y = h.down (a, b);
                    if (i >= 8192) { inSq += x * x; outSq += y * y; }
                }
                const double db = 10.0 * std::log10 (outSq / inSq);
                worst = juce::jmax (worst, std::abs (db));
                report << juce::String (hz / 1000.0, 0) << " kHz " << juce::String (db, 2) << " dB; ";
            }
            check (worst < 0.1, "Oversampling filters are flat to 19 kHz (" + report + ")");
        }

        // 1. Treble through one slot (OTA 12 dB, cutoff 20 kHz, no resonance):
        // at 2x and 4x it should follow the filter's own (analogue) curve,
        // 1 / (1 + (f/fc)^2), with no extra loss from the oversampling.
        // (Before this fix: 2x was -3.2 dB at 10 kHz and -7.1 dB at 15 kHz.)
        {
            FilterSlotSettings st;
            st.type = FilterType::ota; st.character = 0.75f; st.cutoffHz = 20000.0f; st.resonance = 0.0f; st.levelMatch = false;
            juce::String report;
            double worst = 0.0;
            for (double hz : { 10000.0, 15000.0 })
            {
                const double r = hz / 20000.0;
                const double ideal = 20.0 * std::log10 (1.0 / (1.0 + r * r));
                FilterSlot b, c;
                st.oversample = 2; const double g2 = sineGainDb (b, st, hz);
                st.oversample = 4; const double g4 = sineGainDb (c, st, hz);
                report << juce::String (hz / 1000.0, 0) << " kHz: ideal " << juce::String (ideal, 1) << ", 2x " << juce::String (g2, 1) << ", 4x " << juce::String (g4, 1) << " dB; ";
                worst = juce::jmax (worst, std::abs (g2 - ideal), std::abs (g4 - ideal));
            }
            check (worst < 0.7, "Oversampling keeps the treble (" + report + ")");
        }

        // 2. OTA 24 dB: resonance peak is the same height at low and high cutoffs,
        // and full resonance rings at the cutoff frequency.
        {
            FilterSlotSettings st;
            st.type = FilterType::ota; st.character = 0.0f; st.resonance = 0.7f; st.levelMatch = false; st.oversample = 2;
            auto peakDb = [&] (float fc)
            {
                st.cutoffHz = fc;
                FilterSlot a, b;
                return sineGainDb (a, st, fc) - sineGainDb (b, st, fc / 16.0);
            };
            const double lo = peakDb (300.0f), hi = peakDb (6000.0f);
            check (std::abs (lo - hi) < 1.5, "OTA 24 dB resonance is the same at 300 Hz and 6 kHz (" + juce::String (lo, 1) + " vs " + juce::String (hi, 1) + " dB)");

            juce::String report;
            double worstCents = 0.0;
            for (float fc : { 500.0f, 2000.0f, 6000.0f })
            {
                FilterSlot f;
                f.prepare ((float) fs);
                st.cutoffHz = fc; st.resonance = 1.0f;
                std::vector<float> y (44100);
                for (int i = 0; i < (int) y.size(); ++i)
                    y[(size_t) i] = f.process (i < 4 ? 0.2f : 0.0f, st, fc);
                int crossings = 0, firstC = -1, lastC = -1;
                for (int i = 22050; i < (int) y.size(); ++i)
                    if (y[(size_t) i - 1] < 0.0f && y[(size_t) i] >= 0.0f) { ++crossings; if (firstC < 0) firstC = i; lastC = i; }
                const double hz = crossings > 1 ? (crossings - 1) * fs / (lastC - firstC) : 0.0;
                const double cents = hz > 0.0 ? 1200.0 * std::log2 (hz / fc) : 9999.0;
                worstCents = juce::jmax (worstCents, std::abs (cents));
                report << juce::String (fc, 0) << " Hz rings at " << juce::String (hz, 0) << "; ";
            }
            check (worstCents < 35.0, "OTA 24 dB at full resonance rings at the cutoff (" + report + "worst " + juce::String (worstCents, 0) + " cents)");
        }

        // 3. Zipper noise: a host jumps Cutoff once per 512-sample block on a
        // resonant filter. Measure the clicks (high-frequency energy) just after
        // each jump against the level between jumps.
        {
            auto p = makeSynth (sr);
            setParam (*p, ParamIDs::sawLevel, 0.0f);
            setParam (*p, ParamIDs::sineLevel, 100.0f);
            setParam (*p, ParamIDs::slot (0, ParamIDs::slotResonance), 85.0f);
            setParam (*p, ParamIDs::slot (0, ParamIDs::slotEnv), 0.0f);
            setParam (*p, ParamIDs::slot (0, ParamIDs::slotCharacter), 75.0f); // 12 dB
            setParam (*p, ParamIDs::drift, 0.0f);
            juce::AudioBuffer<float> block (2, 512);
            std::vector<float> out;
            for (int b = 0; b < 120; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 45, (juce::uint8) 100), 0);
                // Cutoff steps between 600 Hz and 1.2 kHz every block, like coarse automation.
                setParam (*p, ParamIDs::slot (0, ParamIDs::slotCutoff), (b % 2) ? 1200.0f : 600.0f);
                p->processBlock (block, midi);
                out.insert (out.end(), block.getReadPointer (0), block.getReadPointer (0) + 512);
            }
            // Second difference = high-frequency content.
            double atJump = 0.0, between = 0.0;
            for (int b = 40; b < 120; ++b)
                for (int i = 2; i < 512; ++i)
                {
                    const size_t k = (size_t) (b * 512 + i);
                    const double d = out[k] - 2.0 * out[k - 1] + out[k - 2];
                    if (i < 48) atJump = juce::jmax (atJump, std::abs (d));
                    else if (i > 300) between = juce::jmax (between, std::abs (d));
                }
            const double ratio = atJump / juce::jmax (1.0e-9, between);
            check (ratio < 2.0, "No zipper clicks when Cutoff jumps (click / steady = " + juce::String (ratio, 2) + ")");
        }
    }

    // Robustness: no memory allocated per block, broken numbers recover,
    // and the Numbers view's Amp column shows the Amp's effect.
    {
        const double robustSr = 48000.0;
        auto p = makeSynth (robustSr);
        setParam (*p, ParamIDs::drumPlay, 1.0f);
        juce::AudioBuffer<float> block (2, 512);
        auto play = [&] (int blocks, bool count)
        {
            float peak = 0.0f;
            for (int b = 0; b < blocks; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                if (b == blocks - 1) midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
                countAllocs = count;
                p->processBlock (block, midi);
                countAllocs = false;
                if (b > blocks / 2) peak = juce::jmax (peak, block.getMagnitude (0, 0, 512));
            }
            return peak;
        };
        play (20, false); // warm up
        allocCount = 0;
        play (200, true);
        check (allocCount.load() == 0, "No memory allocated while playing (" + juce::String ((int) allocCount.load()) + " in 200 blocks)");

        {
            juce::MemoryBlock st;
            p->getStateInformation (st);
            auto x = juce::AudioProcessor::getXmlFromBinary (st.getData(), (int) st.getSize());
            check (x != nullptr && x->getIntAttribute (StateIDs::formatVersion) == StateIDs::currentFormatVersion,
                   "Saved state carries its format version (" + juce::String (x != nullptr ? x->getIntAttribute (StateIDs::formatVersion) : 0) + ")");
        }

        {
            // A key let go while bypassed doesn't stick on afterwards; reset silences at once.
            auto b = makeSynth (robustSr);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer on; on.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            b->processBlock (buf, on);
            juce::MidiBuffer off; off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            b->processBlockBypassed (buf, off);
            for (int k = 0; k < 200; ++k) { juce::MidiBuffer none; b->processBlock (buf, none); }
            check (buf.getMagnitude (0, 0, 512) < 1.0e-4f && b->getTailLengthSeconds() > 0.2,
                   "Bypass lets go of released keys, and the release tail is reported to the DAW ("
                       + juce::String (b->getTailLengthSeconds(), 2) + " s)");
            juce::MidiBuffer on2; on2.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
            for (int k = 0; k < 10; ++k) { b->processBlock (buf, on2); on2.clear(); }
            b->reset();
            juce::MidiBuffer none; b->processBlock (buf, none);
            check (buf.getMagnitude (0, 256, 256) < 1.0e-4f, "Reset silences a playing note at once");
        }

        // A damaged preset holding "nan" for Filter 1 Cutoff.
        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        for (auto* child : xml->getChildWithTagNameIterator ("PARAM"))
            if (child->getStringAttribute ("id") == ParamIDs::slot (0, ParamIDs::slotCutoff))
                child->setAttribute ("value", "nan");
        juce::MemoryBlock bad;
        juce::AudioProcessor::copyXmlToBinary (*xml, bad);
        p->setStateInformation (bad.getData(), (int) bad.getSize());
        const float afterPreset = play (40, false);
        check (afterPreset > 0.01f && std::isfinite (paramValue (*p, ParamIDs::slot (0, ParamIDs::slotCutoff))),
               "A preset with a broken number still plays (peak " + juce::String (afterPreset, 3) + ")");

        // A broken number arriving while a note plays (glitchy host automation).
        p->parameters.getRawParameterValue (ParamIDs::slot (0, ParamIDs::slotResonance))->store (std::numeric_limits<float>::quiet_NaN());
        const float afterGlitch = play (40, false);
        check (afterGlitch > 0.01f, "A broken value during play doesn't silence the synth (peak " + juce::String (afterGlitch, 3) + ")");

        // Numbers view: with Amp Sustain at 50%, the Amp column is about half the one before it.
        setParam (*p, ParamIDs::drumPlay, 0.0f);
        setParam (*p, ParamIDs::aenvSustain, 50.0f);
        for (int b = 0; b < 80; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 127), 0);
            p->processBlock (block, midi);
        }
        std::vector<LiveInfo::Frame> frames (4096);
        p->live.copyLatest (frames.data(), 4096);
        double before = 0.0, after = 0.0;
        for (auto& f : frames) { before += f.v[3] * f.v[3]; after += f.v[4] * f.v[4]; }
        const double ampDb = 10.0 * std::log10 (after / juce::jmax (1.0e-12, before));
        check (std::abs (ampDb + 6.0) < 1.5, "Numbers view: the Amp column shows the Amp's change (" + juce::String (ampDb, 1) + " dB at 50% sustain)");
    }

    SoundChecks::run (sr);
    Phase4Checks::run (sr);

    std::cout << (failures == 0 ? "All checks passed" : juce::String (failures) + " check(s) failed") << std::endl;
    return failures == 0 ? 0 : 1;
}
