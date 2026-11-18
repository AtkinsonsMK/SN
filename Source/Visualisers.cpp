#include "Visualisers.h"
#include "Parameters.h"
#include "Filters.h"
#include <complex>

namespace Vis
{
namespace
{
    constexpr float minHz = 20.0f, maxHz = 20000.0f;

    float xForHz (float hz, float width)
    {
        return width * std::log (hz / minHz) / std::log (maxHz / minHz);
    }

    float hzForX (float x, float width)
    {
        return minHz * std::pow (maxHz / minHz, x / width);
    }

    void drawFrequencyGrid (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (Colours::grid);
        for (float hz : { 100.0f, 1000.0f, 10000.0f })
        {
            const float x = r.getX() + xForHz (hz, r.getWidth());
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        }
    }

    // Magnitude (linear) of one filter slot's shape at frequency hz.
    float slotMagnitude (int type, float hz, float cutoff, float res, float character)
    {
        using C = std::complex<float>;
        const C s (0.0f, hz / cutoff);

        auto twoPole = [&] (float damping) { return 1.0f / (s * s + std::max (damping, 0.02f) * s + 1.0f); };

        switch (type)
        {
            case FilterType::ota:
                if (FilterShape::otaIs24dB (character))
                {
                    const float k = FilterShape::otaFeedback (res);
                    const C g = 1.0f / (1.0f + s);
                    const C g4 = g * g * g * g;
                    return std::abs (g4 / (1.0f + k * g4)) * (1.0f + k * 0.45f);
                }
                return std::abs (twoPole (FilterShape::otaDamping (res)));

            case FilterType::sallenKey:
            {
                const C sh (0.0f, hz / FilterShape::sallenKeyHpHz (character));
                const C hp = sh / (1.0f + sh);
                return std::abs (twoPole (FilterShape::sallenKeyDamping (res)) * hp * hp);
            }

            case FilterType::lowpassGate:
                return std::abs (twoPole (FilterShape::gateDamping (res)));

            case FilterType::steinerParker:
            case FilterType::polivoks:
            {
                const bool steiner = type == FilterType::steinerParker;
                const float d = std::max (steiner ? FilterShape::steinerDamping (res) : FilterShape::polivoksDamping (res), 0.02f);
                const C den = s * s + d * s + 1.0f;
                const int mode = steiner ? FilterShape::steinerMode (character) : FilterShape::polivoksMode (character);
                if (mode == 0) return std::abs (1.0f / den);
                if (mode == 1) return std::abs (s / den) * 1.5f;
                return std::abs (s * s / den);
            }

            case FilterType::comb:
            {
                // Feedback comb: peaks at multiples of the cutoff frequency.
                const float fb = FilterShape::combFeedback (res, character);
                const C z = std::exp (C (0.0f, -2.0f * FilterMath::pi * hz / cutoff));
                return std::abs (1.0f / (1.0f - fb * z)) * (1.0f - 0.5f * std::abs (fb));
            }

            case FilterType::formant:
            {
                const auto& vowel = FilterShape::formantTable()[FilterShape::formantVowel (character)];
                const float scale = cutoff / FilterShape::formantReferenceHz;
                const float d = 1.0f / FilterShape::formantQ (res);
                C sum (0.0f, 0.0f);
                for (int b = 0; b < 3; ++b)
                {
                    const C sb (0.0f, hz / (vowel[b][0] * scale));
                    sum += (sb / (sb * sb + d * sb + 1.0f)) * d * 2.0f * vowel[b][1];
                }
                return std::abs (sum) * 2.5f;
            }

            default:
                return 1.0f;
        }
    }

    juce::Path curvePath (juce::Rectangle<float> r, float minDb, float maxDb, const std::function<float (float)>& dbAtHz)
    {
        juce::Path p;
        const int steps = juce::jmax (2, (int) r.getWidth());
        for (int i = 0; i <= steps; ++i)
        {
            const float x = r.getWidth() * (float) i / (float) steps;
            const float db = juce::jlimit (minDb, maxDb, dbAtHz (hzForX (x, r.getWidth())));
            const float y = juce::jmap (db, minDb, maxDb, r.getBottom(), r.getY());
            if (i == 0) p.startNewSubPath (r.getX() + x, y);
            else        p.lineTo (r.getX() + x, y);
        }
        return p;
    }
}

//==============================================================================
void Scope::paint (juce::Graphics& g)
{
    // A still picture of the wave: two cycles of Osc 1's pitch, made by
    // running the same oscillator code the sound uses. It only changes when
    // you change a setting. The live, moving signal is in the Numbers window.
    paintScreen (g);
    auto r = getLocalBounds().toFloat().reduced (6.0f, 5.0f);
    g.setColour (Colours::grid);
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

    OscSettings s[2];
    s[0].sine  = param (ParamIDs::sineLevel)  * 0.01f;
    s[0].saw   = param (ParamIDs::sawLevel)   * 0.01f;
    s[0].pulse = param (ParamIDs::pulseLevel) * 0.01f;
    s[0].pulseWidth = param (ParamIDs::pulseWidth) * 0.01f;
    s[0].level = param (ParamIDs::osc1Level)  * 0.01f;
    s[0].coarse = param (ParamIDs::osc1Coarse); s[0].fine = param (ParamIDs::osc1Fine);
    s[1].sine  = param (ParamIDs::osc2Sine)   * 0.01f;
    s[1].saw   = param (ParamIDs::osc2Saw)    * 0.01f;
    s[1].pulse = param (ParamIDs::osc2Pulse)  * 0.01f;
    s[1].pulseWidth = param (ParamIDs::osc2Pw) * 0.01f;
    s[1].level = param (ParamIDs::osc2Level)  * 0.01f;
    s[1].coarse = param (ParamIDs::osc2Coarse); s[1].fine = param (ParamIDs::osc2Fine);
    const float sub  = param (ParamIDs::subLevel) * 0.01f;
    const float fm12 = param (ParamIDs::fm1to2)  * 0.01f;
    const float fm21 = param (ParamIDs::fm2to1)  * 0.01f;
    const float ring = param (ParamIDs::ringMod) * 0.01f;

    const int n = juce::jmax (2, (int) r.getWidth());
    std::vector<float> wave ((size_t) n + 1);

    // The picture shows Osc 1's pitch with tuning relative to it, so a
    // shift on Osc 1 alone doesn't squash the drawing.
    const double base = 2.0 / (double) n;  // two cycles across the width
    const double inc1 = base;
    const double inc2 = base * std::exp2 ((s[1].tuneSemitones() - s[0].tuneSemitones()) / 12.0);
    Oscillator o1, o2;
    o1.reset (s[0]);
    o2.reset (s[1]);
    double subPh = 0.0;
    float peak = 0.0f;
    for (int i = 0; i <= n; ++i)
    {
        const float a = o1.next (inc1, fm21 * SynthVoice::fmDepthAtFull * o2.getLastOut(), s[0], 1.0f);
        const float b = o2.next (inc2, fm12 * SynthVoice::fmDepthAtFull * a, s[1], 1.0f);
        float v = 0.0f;
        if (which == 0)      v = a;
        else if (which == 1) v = b;
        else v = s[0].level * a + s[1].level * b + ring * a * b + sub * (subPh < 0.5 ? 1.0f : -1.0f);
        subPh += inc1 * 0.5;
        if (subPh >= 1.0) subPh -= 1.0;
        wave[(size_t) i] = v;
        peak = std::max (peak, std::abs (v));
    }

    if (peak < 1.0e-4f)
    {
        g.setColour (Colours::text);
        g.setFont (Theme::label (11.0f));
        g.drawText (which == 2 ? "all levels at 0" : "no wave turned up", r, juce::Justification::centred);
        return;
    }

    // Each picture fills its screen, except the mix, which is drawn to a
    // fixed scale so you can see Osc levels change its size.
    const float scale = which == 2 ? juce::jmax (peak, 1.0f) : peak;
    juce::Path p;
    for (int i = 0; i <= n; ++i)
    {
        const float x = r.getX() + (float) i;
        const float y = r.getCentreY() - wave[(size_t) i] / scale * r.getHeight() * 0.46f;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (Colours::trace);
    g.strokePath (p, juce::PathStrokeType (1.6f));
}

//==============================================================================
void FilterCurve::paint (juce::Graphics& g)
{
    paintScreen (g);
    auto r = getLocalBounds().toFloat().reduced (4.0f);
    drawFrequencyGrid (g, r);

    const int type = (int) param (ParamIDs::slot (slotIndex, ParamIDs::slotType));
    const bool active = type != FilterType::off && FilterType::isAvailable (type);

    const float minDb = -36.0f, maxDb = 18.0f;
    const float zeroY = juce::jmap (0.0f, minDb, maxDb, r.getBottom(), r.getY());
    g.setColour (Colours::grid);
    g.drawHorizontalLine ((int) zeroY, r.getX(), r.getRight());

    if (! active)
    {
        g.setColour (Colours::text);
        g.setFont (Theme::label (11.0f));
        g.drawText ("off: sound passes straight through",
                    r, juce::Justification::centred);
        return;
    }

    auto& live = processor.live;
    const bool playing = live.noteActive.load();
    const float cutoff = playing ? live.liveCutoffHz[slotIndex].load()
                                 : param (ParamIDs::slot (slotIndex, ParamIDs::slotCutoff));
    const float res  = param (ParamIDs::slot (slotIndex, ParamIDs::slotResonance)) * 0.01f;
    const float ch   = param (ParamIDs::slot (slotIndex, ParamIDs::slotCharacter)) * 0.01f;
    const float gate = type == FilterType::lowpassGate && playing ? live.gateLevel[slotIndex].load() : 1.0f;
    const float effectiveCutoff = cutoff * std::exp2 ((gate - 1.0f) * 6.0f);

    auto path = curvePath (r, minDb, maxDb, [&] (float hz)
    {
        return juce::Decibels::gainToDecibels (slotMagnitude (type, hz, effectiveCutoff, res, ch) * std::max (gate, 1.0e-3f), -100.0f);
    });

    juce::Path fill (path);
    fill.lineTo (r.getRight(), r.getBottom());
    fill.lineTo (r.getX(), r.getBottom());
    fill.closeSubPath();
    g.setColour (Theme::accent.withAlpha (0.22f));
    g.fillPath (fill);
    g.setColour (Colours::trace);
    g.strokePath (path, juce::PathStrokeType (1.6f));

    // Cutoff marker
    const float cx = r.getX() + xForHz (juce::jlimit (minHz, maxHz, effectiveCutoff), r.getWidth());
    g.setColour (Colours::text.withAlpha (0.6f));
    g.drawVerticalLine ((int) cx, r.getY(), r.getBottom());
}

//==============================================================================
void EnvelopeShape::paint (juce::Graphics& g)
{
    paintScreen (g);
    auto r = getLocalBounds().toFloat().reduced (6.0f, 5.0f);

    using namespace ParamIDs;
    const float A = param (filterEnv ? fenvAttack : aenvAttack);
    const float D = param (filterEnv ? fenvDecay : aenvDecay);
    const float S = param (filterEnv ? fenvSustain : aenvSustain) * 0.01f;
    const float R = param (filterEnv ? fenvRelease : aenvRelease);

    // Segment widths follow the square root of each time, so short and long
    // settings both stay readable; sustain gets a fixed quarter.
    const float wa = std::sqrt (A), wd = std::sqrt (D), wr = std::sqrt (R);
    const float scale = r.getWidth() * 0.75f / (wa + wd + wr);
    const float xA = wa * scale, xD = wd * scale, xS = r.getWidth() * 0.25f, xR = wr * scale;

    auto attackCurve  = [] (float t) { const float k = std::log (1.3f / 0.3f); return std::min (1.0f, 1.3f * (1.0f - std::exp (-t * k))); };
    auto fallCurve    = [] (float t) { return std::exp (-t * 9.2f); };

    auto yFor = [&] (float level) { return r.getBottom() - level * r.getHeight(); };

    juce::Path p;
    p.startNewSubPath (r.getX(), yFor (0.0f));
    const int steps = 40;
    for (int i = 1; i <= steps; ++i) { const float t = (float) i / steps; p.lineTo (r.getX() + t * xA, yFor (attackCurve (t))); }
    for (int i = 1; i <= steps; ++i) { const float t = (float) i / steps; p.lineTo (r.getX() + xA + t * xD, yFor (S + (1.0f - S) * fallCurve (t))); }
    p.lineTo (r.getX() + xA + xD + xS, yFor (S));
    for (int i = 1; i <= steps; ++i) { const float t = (float) i / steps; p.lineTo (r.getX() + xA + xD + xS + t * xR, yFor (S * fallCurve (t))); }

    g.setColour (Colours::trace);
    g.strokePath (p, juce::PathStrokeType (1.6f));

    // Where the newest note is.
    auto& live = processor.live;
    const int stage = filterEnv ? live.filterEnvStage.load() : live.ampEnvStage.load();
    if (! live.noteActive.load() || stage == 0)
        return;

    const float secs  = filterEnv ? live.filterEnvSeconds.load() : live.ampEnvSeconds.load();
    const float level = filterEnv ? live.filterEnvLevel.load()   : live.ampEnvLevel.load();
    float x = r.getX();
    switch (stage)
    {
        case 1: x += xA * juce::jlimit (0.0f, 1.0f, secs / std::max (A, 1.0e-4f)); break;
        case 2: x += xA + xD * juce::jlimit (0.0f, 1.0f, secs / std::max (D, 1.0e-4f)); break;
        case 3: x += xA + xD + xS * 0.5f; break;
        case 4: x += xA + xD + xS + xR * juce::jlimit (0.0f, 1.0f, secs / std::max (R, 1.0e-4f)); break;
        default: break;
    }
    g.setColour (Theme::accent);
    g.fillEllipse (x - 3.5f, yFor (level) - 3.5f, 7.0f, 7.0f);
}

//==============================================================================
void LfoShape::paint (juce::Graphics& g)
{
    paintScreen (g);
    auto r = getLocalBounds().toFloat().reduced (6.0f, 5.0f);
    g.setColour (Colours::grid);
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

    const int shape = (int) param (index == 0 ? ParamIDs::lfo1Shape : ParamIDs::lfo2Shape);
    const float smooth = param (index == 0 ? ParamIDs::lfo1Smooth : ParamIDs::lfo2Smooth) * 0.01f;

    // Two cycles, drawn through the same smoothing the sound uses, so
    // Smooth visibly rounds the corners. Random uses a fixed example pattern.
    const int n = juce::jmax (2, (int) r.getWidth());
    const float steps[] = { 0.6f, -0.3f, 0.9f, -0.8f };
    const float smoothCycles = smooth * 0.25f;
    const float coef = smoothCycles > 1.0e-4f ? 1.0f - std::exp (-1.0f / (smoothCycles * (float) n * 0.5f)) : 1.0f;
    float y = SNSynthProcessor::lfoShapeValue (shape, 0.0, steps[3]);
    std::vector<float> v ((size_t) n + 1);
    // run one cycle first so the drawing starts settled
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i <= n; ++i)
        {
            const double t = 2.0 * i / n;
            const double ph = t - std::floor (t);
            const float raw = SNSynthProcessor::lfoShapeValue (shape, ph, steps[juce::jlimit (0, 3, (int) t + pass * 2)]);
            y += (raw - y) * coef;
            v[(size_t) i] = y;
        }

    const auto colour = Mod::sourceColour (index == 0 ? Mod::lfo1 : Mod::lfo2);
    juce::Path p;
    for (int i = 0; i <= n; ++i)
    {
        const float py = r.getCentreY() - v[(size_t) i] * r.getHeight() * 0.46f;
        if (i == 0) p.startNewSubPath (r.getX(), py); else p.lineTo (r.getX() + (float) i, py);
    }
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.6f));

    const float phase = index == 0 ? processor.live.lfoPhase.load() : processor.live.lfo2Phase.load();
    const int xi = juce::jlimit (0, n, (int) (phase * 0.5f * (float) n));
    g.setColour (Theme::accent);
    g.fillEllipse (r.getX() + (float) xi - 3.5f, r.getCentreY() - v[(size_t) xi] * r.getHeight() * 0.46f - 3.5f, 7.0f, 7.0f);
}

//==============================================================================
void EqCurve::paint (juce::Graphics& g)
{
    paintScreen (g);
    auto r = getLocalBounds().toFloat().reduced (4.0f);
    drawFrequencyGrid (g, r);

    const float minDb = -15.0f, maxDb = 15.0f;
    g.setColour (Colours::grid);
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

    const double sr = processor.getSampleRate() > 0 ? processor.getSampleRate() : 48000.0;
    using C = juce::dsp::IIR::Coefficients<float>;
    auto lo = C::makeLowShelf  (sr, 100.0,  0.707f, juce::Decibels::decibelsToGain (param (ParamIDs::eqLow)));
    auto mi = C::makePeakFilter (sr, 1000.0, 0.7f,   juce::Decibels::decibelsToGain (param (ParamIDs::eqMid)));
    auto hi = C::makeHighShelf (sr, 8000.0, 0.707f, juce::Decibels::decibelsToGain (param (ParamIDs::eqHigh)));

    auto path = curvePath (r, minDb, maxDb, [&] (float hz)
    {
        const double f = juce::jmin ((double) hz, sr * 0.49);
        const double m = lo->getMagnitudeForFrequency (f, sr) * mi->getMagnitudeForFrequency (f, sr) * hi->getMagnitudeForFrequency (f, sr);
        return (float) juce::Decibels::gainToDecibels (m, -100.0);
    });
    g.setColour (Colours::trace);
    g.strokePath (path, juce::PathStrokeType (1.6f));
}

//==============================================================================
Numbers::Numbers (SNSynthProcessor& p) : processor (p)
{
    addAndMakeVisible (freeze);
    addAndMakeVisible (grab);
    freeze.setWantsKeyboardFocus (false);
    grab.setWantsKeyboardFocus (false);
    grab.onClick = [this] { capture(); repaint(); };
    setSize (960, 500);
    startTimerHz (2);
}

void Numbers::timerCallback()
{
    if (! freeze.getToggleState())
        capture();
    repaint();
}

void Numbers::capture()
{
    LiveInfo::Frame latest[numRows];
    processor.live.copyLatest (latest, numRows);

    // Keep the last snapshot that had sound in it, so the view doesn't go
    // blank between notes.
    bool hasSound = false;
    for (auto& f : latest)
        hasSound = hasSound || std::abs (f.v[5]) > 1.0e-5f;
    if (hasSound)
        std::copy (latest, latest + numRows, frames);
}

void Numbers::resized()
{
    auto top = getLocalBounds().reduced (12).removeFromTop (26);
    grab.setBounds (top.removeFromRight (90));
    top.removeFromRight (8);
    freeze.setBounds (top.removeFromRight (90));
}

void Numbers::paint (juce::Graphics& g)
{
    g.fillAll (Theme::paper);
    auto area = getLocalBounds().reduced (12);

    const double sr = processor.getSampleRate() > 0 ? processor.getSampleRate() : 48000.0;
    auto header = area.removeFromTop (26);
    g.setColour (Theme::ink);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (juce::String (numRows) + " samples in a row at each stage, each 1/" + juce::String (juce::roundToInt (sr))
                    + " of a second apart (" + juce::String (numRows * 1000.0 / sr, 2) + " ms in total)",
                header, juce::Justification::centredLeft);
    area.removeFromTop (8);

    // Column titles follow the current chain order.
    const auto order = processor.getChainOrder();
    const auto& typeNames = Params::filterTypeNames();
    stageNames[0] = "Oscillators";
    for (int pos = 0; pos < Chain::numBlocks; ++pos)
    {
        const int b = order[(size_t) pos];
        juce::String name = Chain::blockName (b);
        if (Chain::isFilter (b))
            name << " (" << typeNames[(int) processor.parameters.getRawParameterValue (ParamIDs::slot (b, ParamIDs::slotType))->load()] << ")";
        stageNames[pos + 1] = name;
    }
    stageNames[LiveInfo::numStages - 1] = "Output";

    const int labelW = 26;
    const int colW = (area.getWidth() - labelW) / LiveInfo::numStages;
    auto titles = area.removeFromTop (20);
    titles.removeFromLeft (labelW);
    g.setColour (Theme::dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    for (int c = 0; c < LiveInfo::numStages; ++c)
        g.drawText (stageNames[c], titles.removeFromLeft (colW).reduced (4, 0), juce::Justification::centredLeft);

    // Bars are scaled to the biggest value in the snapshot, so small numbers are still visible.
    float scale = 0.05f;
    for (auto& f : frames)
        for (float v : f.v)
            scale = std::max (scale, std::abs (v));
    scale = std::min (scale, 1.0f);

    const int rowH = juce::jmin (24, (area.getHeight() - 18) / numRows);
    const auto mono = juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain);

    for (int row = 0; row < numRows; ++row)
    {
        auto line = area.removeFromTop (rowH);
        g.setColour (Theme::grid);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (juce::String (row + 1), line.removeFromLeft (labelW), juce::Justification::centredLeft);

        for (int c = 0; c < LiveInfo::numStages; ++c)
        {
            auto cell = line.removeFromLeft (colW).reduced (4, 2);
            const float v = frames[row].v[c];
            const bool over = std::abs (v) > 1.0f;

            auto textArea = cell.removeFromLeft (62);
            g.setFont (mono);
            g.setColour (over ? Theme::red : Theme::ink);
            g.drawText ((v >= 0.0f ? "+" : "") + juce::String (v, 4), textArea, juce::Justification::centredRight);

            // Bar: centre line is zero, edges are -1 and +1.
            auto bar = cell.reduced (4, 3).toFloat();
            g.setColour (Colours::grid);
            g.fillRect (bar);
            const float cx = bar.getCentreX();
            const float w = juce::jlimit (-1.0f, 1.0f, v / scale) * bar.getWidth() * 0.5f;
            g.setColour (over ? Theme::red : (c == 0 ? Colours::trace : Colours::trace2));
            g.fillRect (juce::Rectangle<float> (std::min (cx, cx + w), bar.getY(), std::abs (w), bar.getHeight()));
        }
    }

    g.setColour (Theme::dim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Bars: centre is 0, edges are +/-" + juce::String (scale, 2)
                    + ".  Sound runs from -1 to +1; red numbers are beyond that and get squashed (clipped).",
                area.removeFromTop (18), juce::Justification::centredLeft);
}

NumbersWindow::NumbersWindow (SNSynthProcessor& p)
    : juce::DocumentWindow ("SN Synth: Numbers", Theme::paper, juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new Numbers (p), true);
    setResizable (false, false);
    centreWithSize (getWidth(), getHeight());
}

//==============================================================================
namespace
{
    constexpr float meterFloorDb = -48.0f, meterTopDb = 6.0f;
    float toDb (float gain) { return gain > 1.0e-6f ? 20.0f * std::log10 (gain) : -120.0f; }
}

void OutputMeter::tick()
{
    auto& live = processor.live;
    const float peak = live.outPeak.exchange (0.0f);
    const float rms  = live.outRms.load();
    if (live.outClipped.load())
        clip = true;

    // Peak falls back at about 20 dB a second; the held peak line waits 1.5 s.
    peakDb = juce::jmax (toDb (peak), peakDb - 0.7f);
    rmsDb  = toDb (rms);
    if (peakDb >= holdDb) { holdDb = peakDb; holdFrames = 45; }
    else if (--holdFrames <= 0) holdDb = juce::jmax (peakDb, holdDb - 1.0f);
    repaint();
}

void OutputMeter::mouseDown (const juce::MouseEvent&)
{
    clip = false;
    processor.live.outClipped.store (false);
    holdDb = peakDb;
    repaint();
}

void OutputMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto readout = r.removeFromBottom (16.0f);
    auto light = r.removeFromTop (14.0f);

    // Clip light
    g.setColour (clip ? Theme::red : Colours::grid);
    g.fillRect (light.reduced (2.0f, 1.0f));
    g.setColour (clip ? Theme::paper : Colours::text);
    g.setFont (Theme::label (10.0f));
    g.drawText ("CLIP", light, juce::Justification::centred);

    r.removeFromTop (3.0f);
    auto bar = r.withSizeKeepingCentre (juce::jmin (18.0f, r.getWidth() * 0.4f), r.getHeight());
    auto scaleArea = r.withLeft (bar.getRight() + 3.0f);
    g.setColour (Colours::screen);
    g.fillRect (bar);

    auto yFor = [&] (float db)
    {
        return juce::jmap (juce::jlimit (meterFloorDb, meterTopDb, db), meterFloorDb, meterTopDb, bar.getBottom(), bar.getY());
    };
    const float zeroY = yFor (0.0f);

    // Average: solid bar, amber up to 0 dB and red above.
    const float top = yFor (rmsDb);
    if (top < bar.getBottom())
    {
        g.setColour (Colours::trace);
        g.fillRect (bar.withTop (juce::jmax (top, zeroY)));
        if (top < zeroY)
        {
            g.setColour (Theme::red);
            g.fillRect (bar.withTop (top).withBottom (zeroY));
        }
    }
    // Peak: thin line, plus the held peak.
    g.setColour (peakDb > 0.0f ? Theme::red : Colours::trace2);
    g.fillRect (bar.getX(), yFor (peakDb) - 1.0f, bar.getWidth(), 2.0f);
    g.setColour (holdDb > 0.0f ? Theme::red : Theme::ink);
    g.fillRect (bar.getX(), yFor (holdDb) - 0.5f, bar.getWidth(), 1.0f);

    // Scale marks
    g.setFont (Theme::label (9.0f));
    for (float db : { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -48.0f })
    {
        const float y = yFor (db);
        g.setColour (db > -0.5f && db < 0.5f ? Theme::red : Colours::text);
        g.drawHorizontalLine ((int) y, bar.getRight(), bar.getRight() + 3.0f);
        g.drawText (juce::String ((int) db), scaleArea.withY (y - 6.0f).withHeight (12.0f).withTrimmedLeft (4.0f),
                    juce::Justification::centredLeft, false);
    }

    g.setColour (holdDb > 0.0f ? Theme::red : Colours::text);
    g.setFont (Theme::label (10.0f));
    g.drawText (holdDb <= -100.0f ? juce::String ("-inf") : juce::String (holdDb, 1) + " dB", readout,
                juce::Justification::centred);
}
}
