#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

struct Palette
{
    juce::Colour body, ring, pointer, tick, text, btnOn, btnOff, btnText, btnOnText;
};

inline juce::Font uiFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

// Knob + button styling shared by every module, parameterised by a palette.
class ModuleLNF : public juce::LookAndFeel_V4
{
public:
    explicit ModuleLNF (Palette p) : pal (p)
    {
        setColour (juce::Slider::textBoxTextColourId, pal.text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, pal.pointer.withAlpha (0.4f));
        setColour (juce::Label::textColourId, pal.text);
        setColour (juce::TextButton::textColourOffId, pal.btnText);
        setColour (juce::TextButton::textColourOnId, pal.btnOnText);
        setColour (juce::ComboBox::backgroundColourId, pal.btnOff);
        setColour (juce::ComboBox::textColourId, pal.btnText);
        setColour (juce::ComboBox::outlineColourId, pal.ring.withAlpha (0.6f));
        setColour (juce::ComboBox::arrowColourId, pal.btnText);
        setColour (juce::PopupMenu::backgroundColourId, pal.btnOff);
        setColour (juce::PopupMenu::textColourId, pal.btnText);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return uiFont (juce::jmin (13.0f, (float) buttonHeight * 0.5f), true);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (3.0f);
        const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const float angle = startAngle + sliderPos * (endAngle - startAngle);

        // scale ticks
        g.setColour (pal.tick);
        for (int i = 0; i <= 10; ++i)
        {
            const float a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
            const float s = std::sin (a), co = -std::cos (a);
            const float len = (i % 5 == 0) ? 0.2f : 0.12f;
            g.drawLine (c.x + s * r * (1.0f - len), c.y + co * r * (1.0f - len), c.x + s * r, c.y + co * r,
                        (i % 5 == 0) ? 1.6f : 1.0f);
        }

        const float kr = r * 0.74f;
        // drop shadow
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (c.x - kr, c.y - kr + 2.0f, kr * 2.0f, kr * 2.0f);
        // body
        juce::ColourGradient grad (pal.body.brighter (0.35f), c.x - kr * 0.5f, c.y - kr * 0.7f,
                                   pal.body.darker (0.5f), c.x + kr * 0.6f, c.y + kr, true);
        g.setGradientFill (grad);
        g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
        g.setColour (pal.ring);
        g.drawEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f, 1.5f);
        // inner cap
        const float cr = kr * 0.62f;
        g.setColour (pal.body.darker (0.25f));
        g.drawEllipse (c.x - cr, c.y - cr, cr * 2.0f, cr * 2.0f, 1.0f);
        // pointer
        juce::Path p;
        p.addRoundedRectangle (-1.8f, -kr * 0.92f, 3.6f, kr * 0.52f, 1.5f);
        g.setColour (pal.pointer);
        g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        auto bounds = b.getLocalBounds().toFloat().reduced (1.5f);
        const bool on = b.getToggleState();
        auto fill = on ? pal.btnOn : pal.btnOff;
        if (highlighted) fill = fill.brighter (0.12f);
        if (down) fill = fill.darker (0.15f);

        if (on)
        {
            g.setColour (pal.btnOn.withAlpha (0.35f));
            g.fillRoundedRectangle (bounds.expanded (1.5f), 5.0f);
        }
        g.setGradientFill (juce::ColourGradient (fill.brighter (0.15f), 0.0f, bounds.getY(),
                                                 fill.darker (0.2f), 0.0f, bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (pal.ring.withAlpha (0.7f));
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
    }

    Palette pal;
};
