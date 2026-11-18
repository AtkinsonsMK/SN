#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The look: light, Swiss, Aesop-like. Warm paper, black ink, hairlines,
// square corners, one electric accent, and a fine grain over everything.
namespace Theme
{
    inline const juce::Colour paper   { 0xffe9e5dc }; // background
    inline const juce::Colour panel   { 0xffefece5 }; // panels, a shade lighter
    inline const juce::Colour screen  { 0xffdcd7cc }; // displays, a shade darker
    inline const juce::Colour ink     { 0xff1b1b19 }; // text, lines, knobs
    inline const juce::Colour dim     { 0xff77736a }; // secondary text
    inline const juce::Colour line    { 0xff1b1b19 }; // hairlines
    inline const juce::Colour grid    { 0xffc9c3b6 }; // grid lines inside displays
    inline const juce::Colour accent  { 0xffff4f1a }; // the one "future wave" colour
    inline const juce::Colour trace2  { 0xff3a5bd9 }; // second trace (Numbers, peak line)
    inline const juce::Colour red     { 0xffd9301a }; // over 0 dB

    inline float hairline() { return 1.0f; }

    inline juce::Font label (float size = 11.0f)  { return juce::Font (juce::FontOptions (size)); }
    inline juce::Font title (float size = 11.0f)  { return juce::Font (juce::FontOptions (size, juce::Font::bold)).withExtraKerningFactor (0.08f); }
    inline juce::Font value (float size = 12.0f)  { return juce::Font (juce::FontOptions (size)).withExtraKerningFactor (0.02f); }

    // A tile of fine noise, drawn over the whole panel at low strength.
    inline const juce::Image& grain()
    {
        static const juce::Image tile = []
        {
            juce::Image img (juce::Image::ARGB, 256, 256, true);
            juce::Random rng (7);
            for (int y = 0; y < img.getHeight(); ++y)
                for (int x = 0; x < img.getWidth(); ++x)
                {
                    const float n = rng.nextFloat();
                    if (n < 0.5f)
                        img.setPixelAt (x, y, juce::Colour (0, 0, 0).withAlpha ((0.5f - n) * 0.16f));
                    else
                        img.setPixelAt (x, y, juce::Colour (255, 255, 255).withAlpha ((n - 0.5f) * 0.20f));
                }
            return img;
        }();
        return tile;
    }

    inline void paintGrain (juce::Graphics& g, juce::Rectangle<int> area, float opacity = 1.0f)
    {
        g.setTiledImageFill (grain(), 0, 0, opacity);
        g.fillRect (area);
    }

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            setColour (juce::ResizableWindow::backgroundColourId, paper);
            setColour (juce::Label::textColourId, ink);
            setColour (juce::Slider::textBoxTextColourId, ink);
            setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.3f));
            setColour (juce::TextEditor::textColourId, ink);
            setColour (juce::TextEditor::backgroundColourId, panel);
            setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.3f));
            setColour (juce::CaretComponent::caretColourId, ink);
            setColour (juce::ComboBox::backgroundColourId, panel);
            setColour (juce::ComboBox::textColourId, ink);
            setColour (juce::ComboBox::outlineColourId, line);
            setColour (juce::ComboBox::arrowColourId, ink);
            setColour (juce::PopupMenu::backgroundColourId, panel);
            setColour (juce::PopupMenu::textColourId, ink);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, ink);
            setColour (juce::PopupMenu::highlightedTextColourId, paper);
            setColour (juce::TextButton::buttonColourId, panel);
            setColour (juce::TextButton::buttonOnColourId, ink);
            setColour (juce::TextButton::textColourOffId, ink);
            setColour (juce::TextButton::textColourOnId, paper);
            setColour (juce::ToggleButton::textColourId, ink);
            setColour (juce::ToggleButton::tickColourId, ink);
            setColour (juce::ToggleButton::tickDisabledColourId, dim);
            setColour (juce::TooltipWindow::backgroundColourId, ink);
            setColour (juce::TooltipWindow::textColourId, paper);
            setColour (juce::TooltipWindow::outlineColourId, ink);
            setColour (juce::DocumentWindow::textColourId, ink);

            auto scheme = getLightColourScheme();
            scheme.setUIColour (ColourScheme::windowBackground, paper);
            scheme.setUIColour (ColourScheme::widgetBackground, panel);
            scheme.setUIColour (ColourScheme::menuBackground, panel);
            scheme.setUIColour (ColourScheme::outline, line);
            scheme.setUIColour (ColourScheme::defaultText, ink);
            scheme.setUIColour (ColourScheme::defaultFill, ink);
            scheme.setUIColour (ColourScheme::highlightedText, paper);
            scheme.setUIColour (ColourScheme::highlightedFill, ink);
            scheme.setUIColour (ColourScheme::menuText, ink);
            setColourScheme (scheme);
            // setColourScheme resets some colours; set ours again.
            setColour (juce::PopupMenu::highlightedBackgroundColourId, ink);
            setColour (juce::PopupMenu::highlightedTextColourId, paper);
        }

        // Knob: a thin ink circle, a hairline track, the value as a heavier
        // accent arc, and a straight pointer. No shading, no thumb.
        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                               float start, float end, juce::Slider& s) override
        {
            auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (10.0f);
            const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const auto c = bounds.getCentre();
            const float angle = start + pos * (end - start);
            const bool enabled = s.isEnabled();
            const auto inkCol = ink.withMultipliedAlpha (enabled ? 1.0f : 0.4f);

            // Track
            juce::Path track;
            track.addCentredArc (c.x, c.y, radius, radius, 0.0f, start, end, true);
            g.setColour (grid);
            g.strokePath (track, juce::PathStrokeType (1.0f));

            // Value (from the bottom, or from the centre for -/+ knobs)
            const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
            const float from = bipolar ? start + (float) s.valueToProportionOfLength (0.0) * (end - start) : start;
            juce::Path value;
            value.addCentredArc (c.x, c.y, radius, radius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle) + 0.0001f, true);
            g.setColour (accent.withMultipliedAlpha (enabled ? 1.0f : 0.4f));
            g.strokePath (value, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

            // Body
            const float bodyR = radius * 0.72f;
            g.setColour (panel);
            g.fillEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c));
            g.setColour (inkCol);
            g.drawEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c), 1.0f);

            // Pointer
            const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));
            g.drawLine ({ c + dir * (bodyR * 0.25f), c + dir * bodyR }, 1.5f);
        }

        juce::Label* createSliderTextBox (juce::Slider& s) override
        {
            auto* l = juce::LookAndFeel_V4::createSliderTextBox (s);
            l->setFont (value (12.0f));
            l->setColour (juce::Label::textColourId, ink);
            l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
            l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
            return l;
        }

        // Buttons: square, hairline outline, filled ink when on.
        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& fill,
                                   bool over, bool down) override
        {
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            const bool on = b.getToggleState();
            g.setColour (on ? fill : (down ? grid : (over ? panel.darker (0.03f) : panel)));
            g.fillRect (r);
            g.setColour (b.findColour (juce::TextButton::textColourOffId).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
            g.drawRect (r, 1.0f);
        }

        juce::Font getTextButtonFont (juce::TextButton&, int) override { return title (11.0f); }

        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
        {
            g.setFont (getTextButtonFont (b, b.getHeight()));
            g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                             .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
            g.drawText (b.getButtonText().toUpperCase(), b.getLocalBounds(), juce::Justification::centred);
        }

        // Toggles: a small square box, filled with a cross-free solid square when on.
        void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
        {
            auto r = b.getLocalBounds().toFloat();
            auto box = r.removeFromLeft (r.getHeight()).withSizeKeepingCentre (11.0f, 11.0f);
            g.setColour (ink.withMultipliedAlpha (over ? 1.0f : 0.9f));
            g.drawRect (box, 1.0f);
            if (b.getToggleState())
            {
                g.setColour (accent);
                g.fillRect (box.reduced (2.5f));
            }
            g.setColour (b.findColour (juce::ToggleButton::textColourId));
            g.setFont (label (12.0f));
            g.drawFittedText (b.getButtonText(), r.withTrimmedLeft (5.0f).toNearestInt(), juce::Justification::centredLeft, 2);
        }

        void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
        {
            auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
            g.setColour (panel);
            g.fillRect (r);
            g.setColour (line);
            g.drawRect (r, 1.0f);
            const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
            juce::Path arrow;
            arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
            arrow.lineTo (ax, ay + 2.0f);
            arrow.lineTo (ax + 4.0f, ay - 2.0f);
            g.setColour (box.findColour (juce::ComboBox::arrowColourId));
            g.strokePath (arrow, juce::PathStrokeType (1.2f));
        }

        juce::Font getComboBoxFont (juce::ComboBox&) override { return value (13.0f); }
        juce::Font getPopupMenuFont() override { return value (13.0f); }

        void positionComboBoxText (juce::ComboBox& box, juce::Label& l) override
        {
            l.setBounds (8, 1, box.getWidth() - 28, box.getHeight() - 2);
            l.setFont (getComboBoxFont (box));
        }

        void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
        {
            g.fillAll (panel);
            g.setColour (line);
            g.drawRect (0, 0, w, h, 1);
        }

        int getPopupMenuBorderSize() override { return 1; }
    };
}
