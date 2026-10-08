#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <BinaryData.h>

// Kaminari Vocal look: navy and white with an electric accent (DESIGN.md 6.1), Barlow fonts (SIL OFL, embedded),
// ring knobs and rounded controls matching the GUI preview.
namespace kvtheme
{
    inline const juce::Colour navy950 { 0xff070f1f }, navy900 { 0xff0b1a33 }, navy800 { 0xff12264a }, navy600 { 0xff2b4a82 },
                              white { 0xfff4f7fc }, mist { 0xffa9b8d6 }, accent { 0xff5ce1ff }, amber { 0xffffb547 }, red { 0xffff4d5e },
                              steel { 0xff5d6a88 };

    inline juce::Typeface::Ptr face (int which)
    {
        static juce::Typeface::Ptr faces[4] = {
            juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowRegular_ttf, BinaryData::BarlowRegular_ttfSize),
            juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowSemiBold_ttf, BinaryData::BarlowSemiBold_ttfSize),
            juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowBold_ttf, BinaryData::BarlowBold_ttfSize),
            juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowCondensedBold_ttf, BinaryData::BarlowCondensedBold_ttfSize),
        };
        return faces[juce::jlimit (0, 3, which)];
    }

    // weight: 0 regular, 1 semibold, 2 bold, 3 condensed bold (titles)
    inline juce::Font font (float size, int weight = 0, float kerning = 0.0f)
    {
        return juce::Font (juce::FontOptions (face (weight)).withHeight (size)).withExtraKerningFactor (kerning);
    }

    inline juce::Path boltPath (juce::Rectangle<float> r)
    {
        // the lightning glyph used on power buttons and the wordmark (24 x 28 design box)
        juce::Path p;
        p.startNewSubPath (14, 1); p.lineTo (3, 16); p.lineTo (10, 16); p.lineTo (8, 27); p.lineTo (21, 11); p.lineTo (14, 11); p.closeSubPath();
        p.applyTransform (p.getTransformToScaleToFit (r, true));
        return p;
    }
}

inline juce::Font uiFont (float size, bool bold = false) { return kvtheme::font (size, bold ? 1 : 0); }

struct Palette
{
    juce::Colour body, ring, pointer, tick, text, btnOn, btnOff, btnText, btnOnText;
};

// Shared LookAndFeel. Button styles via component properties:
//   "kvStyle" = "fill"   toggled = accent fill with dark text (view switch, segments)
//   "kvStyle" = "power"  bolt icon button (module On), text ignored
//   default             toggled = accent outline with white text
class ModuleLNF : public juce::LookAndFeel_V4
{
public:
    explicit ModuleLNF (Palette = {})
    {
        using namespace kvtheme;
        setColour (juce::ResizableWindow::backgroundColourId, navy950);
        setColour (juce::Slider::textBoxTextColourId, white);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.4f));
        setColour (juce::Label::textColourId, white);
        setColour (juce::Label::textWhenEditingColourId, white);
        setColour (juce::Label::backgroundWhenEditingColourId, navy800);
        setColour (juce::Label::outlineWhenEditingColourId, accent);
        setColour (juce::TextEditor::backgroundColourId, navy800);
        setColour (juce::TextEditor::textColourId, white);
        setColour (juce::TextEditor::outlineColourId, navy600);
        setColour (juce::TextEditor::focusedOutlineColourId, accent);
        setColour (juce::TextButton::buttonColourId, navy800);
        setColour (juce::TextButton::textColourOffId, white);
        setColour (juce::TextButton::textColourOnId, navy950);
        setColour (juce::ComboBox::backgroundColourId, navy800);
        setColour (juce::ComboBox::textColourId, white);
        setColour (juce::ComboBox::outlineColourId, navy600);
        setColour (juce::ComboBox::arrowColourId, mist);
        setColour (juce::PopupMenu::backgroundColourId, navy900);
        setColour (juce::PopupMenu::textColourId, white);
        setColour (juce::PopupMenu::headerTextColourId, mist);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, navy600);
        setColour (juce::PopupMenu::highlightedTextColourId, white);
        setColour (juce::TooltipWindow::backgroundColourId, navy800);
        setColour (juce::TooltipWindow::textColourId, white);
        setColour (juce::TooltipWindow::outlineColourId, accent);
        setColour (juce::AlertWindow::backgroundColourId, navy900);
        setColour (juce::AlertWindow::textColourId, white);
        setColour (juce::AlertWindow::outlineColourId, accent);
        setDefaultSansSerifTypeface (face (0));
    }

    static juce::Typeface::Ptr face (int w) { return kvtheme::face (w); }

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override
    {
        return f.isBold() ? kvtheme::face (1) : juce::LookAndFeel_V4::getTypefaceForFont (f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return kvtheme::font (juce::jmin (13.0f, h * 0.48f), 1, 0.06f); }
    juce::Font getComboBoxFont (juce::ComboBox& b) override { return kvtheme::font (juce::jmin (13.0f, b.getHeight() * 0.5f), 0); }
    juce::Font getPopupMenuFont() override { return kvtheme::font (14.0f, 0); }
    juce::Font getLabelFont (juce::Label& l) override { return l.getFont().getTypefacePtr() == nullptr ? kvtheme::font (13.0f) : l.getFont(); }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        using namespace kvtheme;
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (2.0f);
        const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const float ring = r * 0.86f, thick = juce::jmax (2.5f, r * 0.13f);
        const bool bipolar = s.getProperties()["kvBipolar"];
        const float angle = startAngle + pos * (endAngle - startAngle);

        juce::Path bg;
        bg.addCentredArc (c.x, c.y, ring, ring, 0.0f, startAngle, endAngle, true);
        g.setColour (navy600);
        g.strokePath (bg, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path arc;
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        if (std::abs (angle - from) > 0.01f)
        {
            arc.addCentredArc (c.x, c.y, ring, ring, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
            g.setColour (s.isEnabled() ? accent : mist.withAlpha (0.5f));
            g.strokePath (arc, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        const float body = r * 0.6f;
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (c.x - body, c.y - body + 1.5f, body * 2, body * 2);
        g.setGradientFill (juce::ColourGradient (navy800.brighter (0.25f), c.x, c.y - body, navy800.darker (0.3f), c.x, c.y + body, false));
        g.fillEllipse (c.x - body, c.y - body, body * 2, body * 2);
        g.setColour (navy600);
        g.drawEllipse (c.x - body, c.y - body, body * 2, body * 2, 1.0f);

        juce::Path ptr;
        ptr.addRoundedRectangle (-1.4f, -body * 0.95f, 2.8f, body * 0.6f, 1.4f);
        g.setColour (white);
        g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override
    {
        using namespace kvtheme;
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const auto style = b.getProperties()["kvStyle"].toString();
        const bool on = b.getToggleState();
        const float radius = 5.0f;
        juce::Colour fill = navy800, edge = navy600;
        if (style == "fill" && on) { fill = accent; edge = accent; }
        else if (style == "light" && on) { fill = white; edge = white; }
        else if (style == "seg" && on) { fill = navy600; edge = navy600; }
        else if (style == "seg" || style == "light" || style == "combo" || style == "undo" || style == "redo") {}
        else if (on) { edge = accent; }
        if (style == "ghost" && ! on) fill = juce::Colours::transparentBlack;
        if (hover) fill = fill == accent ? fill.brighter (0.1f) : fill.brighter (0.08f);
        if (down) fill = fill.darker (0.1f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, radius);
        g.setColour (edge);
        g.drawRoundedRectangle (r, radius, on && style != "fill" ? 1.4f : 1.0f);
        if (style == "power")
        {
            const auto icon = r.withSizeKeepingCentre (r.getHeight() * 0.42f, r.getHeight() * 0.56f);
            auto bolt = boltPath (icon);
            if (on) { g.setColour (accent); g.fillPath (bolt); }
            else { g.setColour (mist); g.strokePath (bolt, juce::PathStrokeType (1.2f)); }
        }
        if (style == "undo" || style == "redo")
        {
            // curved arrow
            const auto c = r.getCentre();
            juce::Path a;
            const float rad = r.getHeight() * 0.22f;
            const bool undo = style == "undo";
            a.addCentredArc (c.x, c.y + 2.0f, rad, rad, 0.0f, undo ? -2.2f : 2.2f, 0.0f, true);
            g.setColour (mist);
            g.strokePath (a, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const auto tip = a.getPointAlongPath (0.0f);
            juce::Path head;
            const float d = undo ? -1.0f : 1.0f;
            head.addTriangle (tip.x - 3.0f * d, tip.y - 4.0f, tip.x + 4.0f * d, tip.y, tip.x - 3.0f * d, tip.y + 4.0f);
            g.fillPath (head);
        }
        if (b.hasKeyboardFocus (false))
        {
            g.setColour (accent.withAlpha (0.8f));
            g.drawRoundedRectangle (r.expanded (1.5f), radius + 1.0f, 1.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        using namespace kvtheme;
        const auto style = b.getProperties()["kvStyle"].toString();
        if (style == "power" || style == "undo" || style == "redo")
            return;
        const bool on = b.getToggleState();
        juce::Colour c = white;
        if ((style == "fill" || style == "light") && on) c = navy950;
        else if (! on && (style == "seg" || style == "fill" || style == "light" || b.getClickingTogglesState())) c = mist;
        if (! b.isEnabled()) c = c.withAlpha (0.4f);
        g.setColour (c);
        if (style == "combo")
        {
            g.setFont (font (13.5f, 0));
            g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (10, 0).withTrimmedRight (14), juce::Justification::centredLeft, 1, 0.9f);
            drawDownArrow (g, b.getLocalBounds().toFloat().removeFromRight (20.0f));
            return;
        }
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1, 0.8f);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        using namespace kvtheme;
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (navy800);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (box.hasKeyboardFocus (true) ? accent : navy600);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
        drawDownArrow (g, juce::Rectangle<float> ((float) w - 16.0f, 0.0f, 10.0f, (float) h));
    }

    static void drawDownArrow (juce::Graphics& g, juce::Rectangle<float> r)
    {
        juce::Path tri;
        const auto c = r.getCentre();
        tri.addTriangle (c.x - 3.5f, c.y - 2.0f, c.x + 3.5f, c.y - 2.0f, c.x, c.y + 2.5f);
        g.setColour (kvtheme::mist);
        g.fillPath (tri);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (4, 1, box.getWidth() - 20, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        using namespace kvtheme;
        g.fillAll (navy900);
        g.setColour (accent);
        g.drawRect (0, 0, w, h, 1);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        using namespace kvtheme;
        if (style == juce::Slider::LinearVertical)
        {
            // same track and cap as the horizontal faders, standing up; the fill rises from the bottom
            auto track = juce::Rectangle<float> (x + w * 0.5f - 5.0f, (float) y, 10.0f, (float) h);
            g.setColour (navy950);
            g.fillRoundedRectangle (track, 3.0f);
            g.setColour (navy600);
            g.drawRoundedRectangle (track, 3.0f, 1.0f);
            g.setColour (accent.withAlpha (0.55f));
            g.fillRoundedRectangle (track.withTop (pos).reduced (1.0f), 2.0f);
            g.setColour (white);
            g.fillRoundedRectangle (juce::Rectangle<float> (track.getX() - 3.0f, pos - 4.0f, track.getWidth() + 6.0f, 8.0f), 2.0f);
            return;
        }
        if (style != juce::Slider::LinearHorizontal)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
            return;
        }
        auto track = juce::Rectangle<float> ((float) x, y + h * 0.5f - 6.0f, (float) w, 12.0f);
        g.setColour (navy950);
        g.fillRoundedRectangle (track, 3.0f);
        g.setColour (navy600);
        g.drawRoundedRectangle (track, 3.0f, 1.0f);
        g.setColour (navy600);
        g.fillRoundedRectangle (track.withRight (pos).reduced (1.0f), 2.0f);
        g.setColour (white);
        g.fillRoundedRectangle (juce::Rectangle<float> (pos - 4.0f, track.getY() - 2.0f, 8.0f, track.getHeight() + 4.0f), 2.0f);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h) override
    {
        using namespace kvtheme;
        g.fillAll (navy800);
        g.setColour (accent);
        g.drawRect (0, 0, w, h, 1);
        g.setColour (white);
        g.setFont (font (13.0f, 0));
        g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (8, 5), juce::Justification::topLeft, 8, 1.0f);
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        juce::AttributedString s;
        s.append (text, kvtheme::font (13.0f, 0));
        juce::TextLayout tl;
        tl.createLayout (s, 260.0f);
        const int w = (int) tl.getWidth() + 18, h = (int) tl.getHeight() + 12;
        return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 18,
                                     pos.y > parent.getCentreY() ? pos.y - (h + 6) : pos.y + 6, w, h).constrainedWithin (parent);
    }
};
