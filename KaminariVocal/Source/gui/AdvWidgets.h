#pragma once

#include "BasicView.h"

// Controls for the Advanced pages, styled after the GUI preview.
namespace kvui
{
    // Ring knob with small range labels under the ring, an upper-case caption and an accent value.
    class RangeKnob : public juce::Component
    {
    public:
        RangeKnob (APVTS& s, const juce::String& id, const juce::String& captionText, const juce::String& loText = {},
                   const juce::String& hiText = {}, const juce::String& hint = {})
            : state (s), caption (captionText.toUpperCase()), lo (loText), hi (hiText)
        {
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
            att = std::make_unique<APVTS::SliderAttachment> (s, id, slider);
            auto* p = s.getParameter (id);
            slider.bind (p, captionText);
            slider.setHint (hint.isNotEmpty() ? hint : hintFor (id));
            const auto& r = p->getNormalisableRange();
            slider.getProperties().set ("kvBipolar", r.start < 0.0f && r.end > 0.0f && std::abs (p->convertFrom0to1 (p->getDefaultValue())) < 1e-6f);
            value.setJustificationType (juce::Justification::centred);
            value.setFont (kvtheme::font (12.5f, 1));
            value.setColour (juce::Label::textColourId, kvtheme::accent);
            value.setEditable (false, true, false);
            value.onTextChange = [this, p] { p->beginChangeGesture(); p->setValueNotifyingHost (p->getValueForText (value.getText())); p->endChangeGesture(); refresh(); };
            slider.onValueChange = [this] { refresh(); };
            slider.onEnterValue = [this] { value.showEditor(); };
            addAndMakeVisible (slider);
            addAndMakeVisible (value);
            refresh();
        }
        void setLNF (juce::LookAndFeel* l) { slider.setLookAndFeel (l); }
        void refresh() { value.setText (slider.getTextFromValue (slider.getValue()), juce::dontSendNotification); }
        void setValueText (std::function<juce::String()> f) { valueFn = std::move (f); slider.onValueChange = [this] { value.setText (valueFn(), juce::dontSendNotification); }; value.setText (valueFn(), juce::dontSendNotification); }

        void paint (juce::Graphics& g) override
        {
            using namespace kvtheme;
            const auto ring = slider.getBounds();
            g.setColour (mist);
            g.setFont (font (10.0f, 0));
            if (lo.isNotEmpty() || hi.isNotEmpty())
            {
                const int y = ring.getBottom() - 4;
                g.drawText (lo, juce::jmax (0, ring.getX() - 13), y, ring.getWidth() / 2 + 13, 12, juce::Justification::centredLeft);
                g.drawText (hi, ring.getCentreX(), y, juce::jmin (getWidth() - ring.getCentreX(), ring.getWidth() / 2 + 13), 12, juce::Justification::centredRight);
            }
            g.setColour (white);
            g.setFont (font (11.5f, 2, 0.06f));
            g.drawText (caption, 0, value.getY() - 15, getWidth(), 14, juce::Justification::centred);
        }

        void resized() override
        {
            auto b = getLocalBounds();
            value.setBounds (b.removeFromBottom (16));
            b.removeFromBottom (15);
            if (lo.isNotEmpty() || hi.isNotEmpty()) b.removeFromBottom (8);
            const int d = juce::jmin (b.getWidth() - (lo.isNotEmpty() || hi.isNotEmpty() ? 26 : 0), b.getHeight());
            slider.setBounds (b.withSizeKeepingCentre (d, d));
        }

        ParamSlider slider;
        juce::Label value;

    private:
        APVTS& state;
        juce::String caption, lo, hi;
        std::unique_ptr<APVTS::SliderAttachment> att;
        std::function<juce::String()> valueFn;
    };

    // Horizontal parameter slider with range labels and a caption underneath.
    class HSlider : public juce::Component
    {
    public:
        HSlider (APVTS& s, const juce::String& id, const juce::String& captionText, const juce::String& loText, const juce::String& hiText)
            : caption (captionText.toUpperCase()), lo (loText), hi (hiText)
        {
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            att = std::make_unique<APVTS::SliderAttachment> (s, id, slider);
            slider.bind (s.getParameter (id), captionText);
            slider.setHint (hintFor (id));
            addAndMakeVisible (slider);
        }
        void paint (juce::Graphics& g) override
        {
            using namespace kvtheme;
            g.setColour (mist);
            g.setFont (font (10.0f, 0));
            g.drawText (lo, 0, 16, getWidth() / 2, 12, juce::Justification::centredLeft);
            g.drawText (hi, getWidth() / 2, 16, getWidth() / 2, 12, juce::Justification::centredRight);
            g.setColour (white);
            g.setFont (font (11.5f, 2, 0.06f));
            g.drawText (caption, 0, 30, getWidth(), 14, juce::Justification::centred);
        }
        void resized() override { slider.setBounds (0, 0, getWidth(), 16); }
        ParamSlider slider;
    private:
        juce::String caption, lo, hi;
        std::unique_ptr<APVTS::SliderAttachment> att;
    };

    // Segmented control bound to a choice parameter (or to the two states of a bool).
    class SegParam : public juce::Component
    {
    public:
        SegParam (APVTS& s, const juce::String& id, const juce::StringArray& labels, const juce::String& tip = {})
            : param (*s.getParameter (id)), seg (labels, param.getName (64)),
              att (param, [this] (float v) { seg.setSelected (juce::roundToInt (v)); }, nullptr)
        {
            for (auto* b : seg.buttons) b->setTooltip (param.getName (64) + ": " + b->getButtonText() + (tip.isNotEmpty() ? "\n" + tip : juce::String()));
            seg.onChange = [this] (int i) { att.setValueAsCompleteGesture ((float) i); };
            addAndMakeVisible (seg);
            att.sendInitialUpdate();
        }
        void resized() override { seg.setBounds (getLocalBounds()); }
    private:
        juce::RangedAudioParameter& param;
        Segmented seg;
        juce::ParameterAttachment att;
    };

    // Small caption + value box (drag or type) for compact fields.
    class Field : public juce::Component
    {
    public:
        Field (APVTS& s, const juce::String& id, const juce::String& captionText) : caption (captionText)
        {
            box.bind (s.getParameter (id));
            addAndMakeVisible (box);
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (kvtheme::white);
            g.setFont (kvtheme::font (12.0f, 0));
            g.drawText (caption, 0, 0, getWidth(), 15, juce::Justification::centred);
        }
        void resized() override { box.setBounds (0, 17, getWidth(), getHeight() - 17); }
        ValueBox box;
    private:
        juce::String caption;
    };

    // Rounded section background used to group controls.
    inline void drawGroup (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour fill = kvtheme::navy900)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (kvtheme::navy600.withAlpha (0.7f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
    }

    //==================================================================================================================
    // Frame of every Advanced page: power, title, subtitle, "<MODULE> PRESET" and the module preset bar.
    class AdvFrame : public juce::Component
    {
    public:
        AdvFrame (KaminariVocalProcessor& p, const juce::String& title, const juce::String& subtitle, const char* onId, const juce::String& moduleKey)
            : proc (p), titleText (title), subText (subtitle),
              preset (p.presets, moduleKey, title.toLowerCase())
        {
            if (onId != nullptr)
            {
                power = std::make_unique<PowerButton> (p.apvts, onId, title);
                addAndMakeVisible (*power);
            }
            if (moduleKey.isNotEmpty()) addAndMakeVisible (preset);
            else preset.setVisible (false);
        }

        void paint (juce::Graphics& g) override
        {
            using namespace kvtheme;
            auto b = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (navy900);
            g.fillRoundedRectangle (b, 6.0f);
            g.setColour (navy600);
            g.drawRoundedRectangle (b, 6.0f, 1.0f);
            const int x0 = power != nullptr ? 54 : 18;
            g.setColour (white);
            g.setFont (font (21.0f, 3, 0.1f));
            g.drawText (titleText.toUpperCase(), x0, 14, 220, 26, juce::Justification::centredLeft);
            const int tw = (int) juce::GlyphArrangement::getStringWidth (font (21.0f, 3, 0.1f), titleText.toUpperCase());
            g.setColour (mist);
            g.setFont (font (13.0f, 0));
            g.drawText (subText, x0 + tw + 14, 14, 420, 26, juce::Justification::centredLeft);
            if (preset.isVisible())
            {
                g.setFont (font (11.0f, 1, 0.1f));
                g.drawText (titleText.toUpperCase() + " PRESET", preset.getX() - 150, 14, 140, 26, juce::Justification::centredRight);
            }
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (16, 12);
            auto top = b.removeFromTop (30);
            if (power) power->setBounds (top.removeFromLeft (28).withSizeKeepingCentre (28, 28));
            preset.setBounds (top.removeFromRight (290));
            b.removeFromTop (10);
            layoutContent (b);
        }

        virtual void layoutContent (juce::Rectangle<int>) {}

        KaminariVocalProcessor& proc;
        juce::String titleText, subText;
        std::unique_ptr<PowerButton> power;
        PresetBar preset;
    };
}
