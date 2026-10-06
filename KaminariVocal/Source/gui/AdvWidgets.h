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
            : caption (captionText.toUpperCase()), lo (loText), hi (hiText)
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
        // How far the range labels may extend beyond the ring (smaller keeps neighbouring knobs' labels apart).
        void setLabelOverhang (int px) { overhang = px; resized(); repaint(); }
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
                g.drawFittedText (lo, juce::jmax (0, ring.getX() - overhang), y, ring.getWidth() / 2 + overhang - 2, 12, juce::Justification::centredLeft, 1, 0.7f);
                g.drawFittedText (hi, ring.getCentreX() + 2, y, juce::jmin (getWidth() - ring.getCentreX(), ring.getWidth() / 2 + overhang) - 2, 12, juce::Justification::centredRight, 1, 0.7f);
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
            const int d = juce::jmin (b.getWidth() - (lo.isNotEmpty() || hi.isNotEmpty() ? 2 * overhang : 0), b.getHeight());
            slider.setBounds (b.withSizeKeepingCentre (d, d));
        }

        ParamSlider slider;
        juce::Label value;

    private:
        juce::String caption, lo, hi;
        std::unique_ptr<APVTS::SliderAttachment> att;
        std::function<juce::String()> valueFn;
        int overhang = 13;
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
            if (stripCaption)
            {
                g.setColour (kvtheme::mist);
                g.setFont (kvtheme::font (10.5f, 1, 0.1f));
                g.drawText (caption, 4, 0, getWidth() - 4, 15, juce::Justification::centredLeft);
                return;
            }
            g.setColour (kvtheme::white);
            g.setFont (kvtheme::font (12.0f, 0));
            g.drawText (caption, 0, 0, getWidth(), 15, juce::Justification::centred);
        }
        void resized() override { box.setBounds (0, 17, getWidth(), getHeight() - 17); }
        // Small, left-aligned caption like the combo boxes in a page's top strip.
        void setStripCaption (bool on) { stripCaption = on; repaint(); }
        ValueBox box;
    private:
        juce::String caption;
        bool stripCaption = false;
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

    //==================================================================================================================
    // Shared by the Advanced pages: plain values, titled groups and the group layout.
    using namespace kvtheme;

    inline float plainValue (juce::RangedAudioParameter& p) { return p.convertFrom0to1 (p.getValue()); }
    inline float plainValue (APVTS& s, const char* id) { return plainValue (*s.getParameter (id)); }

    // Titled group box used on the send pages.
    inline void drawTitledGroup (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
    {
        drawGroup (g, r, navy950.interpolatedWith (navy900, 0.5f));
        g.setColour (mist);
        g.setFont (font (11.0f, 2, 0.12f));
        g.drawText (title, r.getX() + 12, r.getY() + 6, r.getWidth() - 24, 16, juce::Justification::centredLeft);
    }

    // Small upper-case caption above a control.
    inline void drawCaption (juce::Graphics& g, const juce::Component& c, const juce::String& text)
    {
        if (! c.isVisible()) return;
        g.setColour (mist);
        g.setFont (font (11.0f, 1, 0.1f));
        g.drawText (text, c.getX(), c.getY() - 17, juce::jmax (c.getWidth(), 160), 14, juce::Justification::centredLeft);
    }

    // Lays knobs out in a row of equal cells inside a group (skips hidden ones). Returns the used width.
    inline void layoutRow (juce::Rectangle<int> r, std::initializer_list<juce::Component*> items, int cell)
    {
        for (auto* c : items)
        {
            if (c == nullptr || ! c->isVisible()) continue;
            c->setBounds (r.removeFromLeft (cell));
        }
    }

    inline int visibleCount (std::initializer_list<juce::Component*> items)
    {
        int n = 0;
        for (auto* c : items) if (c != nullptr && c->isVisible()) ++n;
        return n;
    }

    // A titled group of knobs (plus an optional extra control on the right) on the send pages.
    struct SendGroup
    {
        SendGroup (juce::String t, std::vector<juce::Component*> k, juce::Component* b = nullptr, int be = 0,
                   juce::Component* e = nullptr, int ew = 0, int eh = 28, int edy = 0)
            : title (std::move (t)), knobs (std::move (k)), big (b), bigExtra (be), extra (e), extraW (ew), extraH (eh), extraDy (edy) {}

        juce::String title;
        std::vector<juce::Component*> knobs;
        juce::Component* big = nullptr;       // knob drawn larger (gets bigExtra more width and the full height)
        int bigExtra = 0;
        juce::Component* extra = nullptr;     // e.g. a combo, toggle or segmented control
        int extraW = 0, extraH = 28, extraDy = 0;
        juce::Rectangle<int> area;

        int visibleKnobs() const { int n = 0; for (auto* k : knobs) if (k->isVisible()) ++n; return n; }
        bool used() const { return visibleKnobs() > 0 || extra != nullptr; }
        int extraWidth() const { return (extra != nullptr ? extraW : 0) + (big != nullptr && big->isVisible() ? bigExtra : 0); }
    };

    // Lays the used groups out across the full row: equal knob cells (at most maxCell wide), spare width shared out.
    inline void layoutGroups (juce::Rectangle<int> row, std::initializer_list<SendGroup*> groups, int maxCell, int smallTrim = 0)
    {
        constexpr int pad = 10, gap = 10, titleH = 24;
        int count = 0, knobs = 0, extras = 0;
        for (auto* g : groups)
        {
            g->area = {};
            if (! g->used()) continue;
            ++count; knobs += g->visibleKnobs(); extras += g->extraWidth();
        }
        if (count == 0) return;
        const int fixed = count * 2 * pad + (count - 1) * gap + extras;
        const int cell = knobs > 0 ? juce::jmin (maxCell, (row.getWidth() - fixed) / knobs) : 0;
        const int spare = (row.getWidth() - fixed - knobs * cell) / count;
        for (auto* g : groups)
        {
            if (! g->used()) continue;
            const int content = g->visibleKnobs() * cell + g->extraWidth();
            g->area = row.removeFromLeft (content + 2 * pad + spare);
            row.removeFromLeft (gap);
            auto inner = g->area.reduced (pad, 0).withTrimmedTop (titleH).withTrimmedBottom (6);
            inner = inner.withSizeKeepingCentre (content, inner.getHeight());
            for (auto* k : g->knobs)
            {
                if (! k->isVisible()) continue;
                // keep each ring at least 18 px narrower than its cell so neighbouring range labels stay apart
                // (a RangeKnob's ring is its height minus 39 px of labels); rows stay bottom-aligned
                const int w = k == g->big ? cell + g->bigExtra : cell;
                auto r = inner.removeFromLeft (w);
                if (k != g->big) r = r.withTrimmedTop (smallTrim);
                k->setBounds (r.withTop (juce::jmax (r.getY(), r.getBottom() - (w - 18 + 39))));
            }
            if (g->extra != nullptr)
            {
                auto e = inner.removeFromLeft (g->extraW).reduced (4, 0);
                g->extra->setBounds (e.withSizeKeepingCentre (e.getWidth(), g->extraH).translated (0, g->extraDy));
            }
        }
    }

}
