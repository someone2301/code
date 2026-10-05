#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Look.h"
#include <atomic>

using APVTS = juce::AudioProcessorValueTreeState;

// Rotary slider with a caption, bound to a parameter.
class Knob : public juce::Component
{
public:
    Knob (APVTS& state, const juce::String& paramId, const juce::String& name)
        : apvts (state)
    {
        addAndMakeVisible (slider);
        addAndMakeVisible (label);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
        slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (uiFont (12.0f, true));
        label.setInterceptsMouseClicks (false, false);
        attach (paramId);
    }

    void attach (const juce::String& paramId)
    {
        att.reset();
        att = std::make_unique<APVTS::SliderAttachment> (apvts, paramId, slider);
    }

    void setLNF (juce::LookAndFeel* l)
    {
        slider.setLookAndFeel (l);
        label.setLookAndFeel (l);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        label.setBounds (b.removeFromTop (16));
        slider.setBounds (b);
    }

    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label label;

private:
    APVTS& apvts;
    std::unique_ptr<APVTS::SliderAttachment> att;
};

// Gain-reduction needle meter in two styles.
class VUMeter : public juce::Component, private juce::Timer
{
public:
    enum Style { Fet, Opto };

    VUMeter (std::atomic<float>& source, Style s) : src (source), style (s) { startTimerHz (30); }
    ~VUMeter() override { stopTimer(); }

    void setStyle (Style s) { style = s; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const bool fet = style == Fet;
        auto b = getLocalBounds().toFloat();

        g.setColour (fet ? juce::Colour (0xff0b0b0d) : juce::Colour (0xff2b2d30));
        g.fillRoundedRectangle (b, 9.0f);
        g.setColour (fet ? juce::Colour (0xffa4aab2) : juce::Colour (0xff8a8e94));
        g.drawRoundedRectangle (b.reduced (1.5f), 9.0f, 2.0f);

        auto face = b.reduced (12.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff7efc9), 0.0f, face.getY(),
                                                 juce::Colour (0xffd8c78a), 0.0f, face.getBottom(), false));
        g.fillRoundedRectangle (face, 5.0f);

        const float h = face.getHeight();
        const juce::Point<float> pivot (face.getCentreX(), face.getBottom() + 0.05f * h);
        const float R = 0.92f * h;
        const float theta = 0.85f;
        auto pt = [&] (float radius, float ang) { return juce::Point<float> (pivot.x + radius * std::sin (ang), pivot.y - radius * std::cos (ang)); };

        g.saveState();
        g.reduceClipRegion (face.toNearestInt());

        juce::Path arc;
        arc.addCentredArc (pivot.x, pivot.y, R * 0.98f, R * 0.98f, 0.0f, -theta, theta, true);
        g.setColour (juce::Colour (0xff1c1a14));
        g.strokePath (arc, juce::PathStrokeType (1.2f));

        const int majors[] = { 0, 1, 2, 3, 5, 7, 10, 15, 20 };
        for (int v = 0; v <= 20; ++v)
        {
            const float ang = juce::jmap ((float) v, 0.0f, 20.0f, -theta, theta);
            bool major = false;
            for (int m : majors) major = major || m == v;
            g.setColour (v >= 10 ? juce::Colour (0xffb3261e) : juce::Colour (0xff1c1a14));
            auto p1 = pt (R * (major ? 0.88f : 0.93f), ang), p2 = pt (R * 0.98f, ang);
            g.drawLine (p1.x, p1.y, p2.x, p2.y, major ? 1.6f : 1.0f);
            if (major)
            {
                auto tp = pt (R * 0.78f, ang);
                g.setFont (uiFont (11.0f, true));
                g.drawText (juce::String (v), juce::Rectangle<float> (24.0f, 14.0f).withCentre (tp), juce::Justification::centred);
            }
        }

        g.setColour (juce::Colour (0xff1c1a14));
        g.setFont (uiFont (12.0f, true));
        g.drawText (fet ? "GAIN REDUCTION  dB" : "VU  GAIN REDUCTION",
                    juce::Rectangle<float> (face.getX(), face.getBottom() - 0.3f * h, face.getWidth(), 16.0f),
                    juce::Justification::centred);

        const float ang = juce::jmap (juce::jlimit (0.0f, 21.5f, value), 0.0f, 20.0f, -theta, theta);
        const auto tip = pt (R * 1.02f, ang);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.drawLine (pivot.x + 2.0f, pivot.y + 2.0f, tip.x + 2.0f, tip.y + 2.0f, 1.6f);
        g.setColour (juce::Colour (0xff111111));
        g.drawLine (pivot.x, pivot.y, tip.x, tip.y, 1.8f);

        g.restoreState();

        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), 0.0f, face.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), 0.0f, face.getCentreY(), false));
        g.fillRoundedRectangle (face, 5.0f);
    }

private:
    void timerCallback() override
    {
        const float target = src.load();
        const float next = value + (target - value) * 0.3f;
        if (std::abs (next - value) > 0.005f)
        {
            value = next;
            repaint();
        }
    }

    std::atomic<float>& src;
    Style style;
    float value = 0.0f;
};

// Horizontal gain-reduction bar (de-esser).
class ReductionBar : public juce::Component, private juce::Timer
{
public:
    ReductionBar (std::atomic<float>& source, juce::Colour c) : src (source), colour (c) { startTimerHz (30); }
    ~ReductionBar() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0d0f12));
        g.fillRoundedRectangle (b, 3.0f);
        auto fill = b.reduced (2.0f);
        fill = fill.withWidth (fill.getWidth() * juce::jlimit (0.0f, 1.0f, value / 20.0f));
        g.setColour (colour);
        g.fillRoundedRectangle (fill, 2.0f);
    }

private:
    void timerCallback() override
    {
        const float next = value + (src.load() - value) * 0.35f;
        if (std::abs (next - value) > 0.01f) { value = next; repaint(); }
    }

    std::atomic<float>& src;
    juce::Colour colour;
    float value = 0.0f;
};

// Vertical peak meter, -60 .. +6 dBFS.
class LevelMeter : public juce::Component, private juce::Timer
{
public:
    explicit LevelMeter (std::atomic<float>& source) : src (source) { startTimerHz (30); }
    ~LevelMeter() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0d0f12));
        g.fillRoundedRectangle (b, 2.0f);
        const float db = juce::Decibels::gainToDecibels (level, -60.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        auto bar = b.reduced (2.0f);
        bar = bar.withTop (bar.getBottom() - bar.getHeight() * frac);
        g.setColour (db > -0.5f ? juce::Colour (0xffe5483c) : db > -12.0f ? juce::Colour (0xffe6c14a) : juce::Colour (0xff4cc38a));
        g.fillRect (bar);
    }

private:
    void timerCallback() override
    {
        const float target = src.load();
        level = target > level ? target : level * 0.88f;
        repaint();
    }

    std::atomic<float>& src;
    float level = 0.0f;
};
