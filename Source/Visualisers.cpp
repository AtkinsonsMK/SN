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
    paintScreen (g);
    auto r = getLocalBounds().toFloat().reduced (4.0f);
    g.setColour (Colours::grid);
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

    auto& live = processor.live;
    const float hz = live.noteHz.load();
    const double sr = processor.getSampleRate() > 0 ? processor.getSampleRate() : 48000.0;
    if (! live.noteActive.load() || hz <= 0.0f)
    {
        g.setColour (Colours::text);
        g.setFont (11.0f);
        g.drawText ("play a note", r, juce::Justification::centred);
        return;
    }

    // Two cycles of the newest note, lined up on a rising zero crossing.
    const int period = juce::jlimit (8, 4096, (int) (sr / hz));
    const int window = juce::jmin (2 * period, 6000);
    std::vector<LiveInfo::Frame> frames ((size_t) (window + period + 1));
    live.copyLatest (frames.data(), (int) frames.size());

    int start = 0;
    for (int i = 1; i <= period; ++i)
        if (frames[(size_t) i - 1].v[0] < 0.0f && frames[(size_t) i].v[0] >= 0.0f) { start = i; break; }

    for (int stage : { 5, 0 }) // output underneath, oscillator on top
    {
        float peak = 0.02f;
        for (int i = 0; i < window; ++i)
            peak = std::max (peak, std::abs (frames[(size_t) (start + i)].v[stage]));

        juce::Path p;
        for (int i = 0; i < window; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / (float) (window - 1);
            const float y = r.getCentreY() - frames[(size_t) (start + i)].v[stage] / peak * r.getHeight() * 0.45f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (stage == 0 ? Colours::trace : Colours::trace2.withAlpha (0.8f));
        g.strokePath (p, juce::PathStrokeType (stage == 0 ? 1.6f : 1.2f));
    }

    g.setColour (Colours::text);
    g.setFont (10.0f);
    g.drawText ("osc", r.removeFromTop (12.0f), juce::Justification::topLeft);
    g.setColour (Colours::trace2);
    g.drawText ("out", r.removeFromTop (12.0f), juce::Justification::topLeft);
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
        g.setFont (11.0f);
        g.drawText (type == FilterType::off ? "off: sound passes straight through" : "coming in a later build",
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
    g.setColour (Colours::trace.withAlpha (0.15f));
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
    g.setColour (juce::Colours::white);
    g.fillEllipse (x - 3.5f, yFor (level) - 3.5f, 7.0f, 7.0f);
}

//==============================================================================
void LfoShape::paint (juce::Graphics& g)
{
    paintScreen (g);
    auto r = getLocalBounds().toFloat().reduced (6.0f, 5.0f);
    auto yFor = [&] (float phase) { const float v = phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase; return r.getCentreY() - v * r.getHeight() * 0.5f; };

    juce::Path p;
    p.startNewSubPath (r.getX(), yFor (0.0f));
    p.lineTo (r.getX() + r.getWidth() * 0.5f, yFor (0.5f));
    p.lineTo (r.getRight(), yFor (1.0f));
    g.setColour (Colours::trace);
    g.strokePath (p, juce::PathStrokeType (1.6f));

    const float phase = processor.live.lfoPhase.load();
    g.setColour (juce::Colours::white);
    g.fillEllipse (r.getX() + phase * r.getWidth() - 3.5f, yFor (phase) - 3.5f, 7.0f, 7.0f);
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
    setSize (820, 500);
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
    g.fillAll (juce::Colour (0xff1d1f24));
    auto area = getLocalBounds().reduced (12);

    const double sr = processor.getSampleRate() > 0 ? processor.getSampleRate() : 48000.0;
    auto header = area.removeFromTop (26);
    g.setColour (juce::Colour (0xffe6e6e6));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (juce::String (numRows) + " samples in a row at each stage, each 1/" + juce::String (juce::roundToInt (sr))
                    + " of a second apart (" + juce::String (numRows * 1000.0 / sr, 2) + " ms in total)",
                header, juce::Justification::centredLeft);
    area.removeFromTop (8);

    // Column titles follow the current filter order.
    const auto order = processor.getSlotOrder();
    const auto& typeNames = Params::filterTypeNames();
    stageNames[0] = "Oscillator";
    for (int pos = 0; pos < 3; ++pos)
    {
        const int slot = order[(size_t) pos];
        const int type = (int) processor.parameters.getRawParameterValue (ParamIDs::slot (slot, ParamIDs::slotType))->load();
        stageNames[pos + 1] = "Filter " + juce::String (slot + 1) + " (" + typeNames[type] + ")";
    }
    stageNames[4] = "After EQ";
    stageNames[5] = "Output";

    const int labelW = 26;
    const int colW = (area.getWidth() - labelW) / LiveInfo::numStages;
    auto titles = area.removeFromTop (20);
    titles.removeFromLeft (labelW);
    g.setColour (juce::Colour (0xff9aa0a8));
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
        g.setColour (juce::Colour (0xff5a606a));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (juce::String (row + 1), line.removeFromLeft (labelW), juce::Justification::centredLeft);

        for (int c = 0; c < LiveInfo::numStages; ++c)
        {
            auto cell = line.removeFromLeft (colW).reduced (4, 2);
            const float v = frames[row].v[c];
            const bool over = std::abs (v) > 1.0f;

            auto textArea = cell.removeFromLeft (62);
            g.setFont (mono);
            g.setColour (over ? juce::Colour (0xffe0604a) : juce::Colour (0xffe6e6e6));
            g.drawText ((v >= 0.0f ? "+" : "") + juce::String (v, 4), textArea, juce::Justification::centredRight);

            // Bar: centre line is zero, edges are -1 and +1.
            auto bar = cell.reduced (4, 3).toFloat();
            g.setColour (Colours::grid);
            g.fillRect (bar);
            const float cx = bar.getCentreX();
            const float w = juce::jlimit (-1.0f, 1.0f, v / scale) * bar.getWidth() * 0.5f;
            g.setColour (over ? juce::Colour (0xffe0604a) : (c == 0 ? Colours::trace : Colours::trace2));
            g.fillRect (juce::Rectangle<float> (std::min (cx, cx + w), bar.getY(), std::abs (w), bar.getHeight()));
        }
    }

    g.setColour (juce::Colour (0xff9aa0a8));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Bars: centre is 0, edges are +/-" + juce::String (scale, 2)
                    + ".  Sound runs from -1 to +1; red numbers are beyond that and get squashed (clipped).",
                area.removeFromTop (18), juce::Justification::centredLeft);
}

NumbersWindow::NumbersWindow (SNSynthProcessor& p)
    : juce::DocumentWindow ("SN Synth: Numbers", juce::Colour (0xff1d1f24), juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new Numbers (p), true);
    setResizable (false, false);
    centreWithSize (getWidth(), getHeight());
}
}
