#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Look.h"
#include "../HostTempo.h"
#include <vector>

// Vertical slider drawn as a branching lightning bolt.
//
// The bolt hangs from the top of the track. The lit part ends at the thumb: dragging down raises the
// parameter value and lights more of the trunk and its forks, dragging up dims them in reverse order.
// The drawing is driven only by the parameter's value (through a ParameterAttachment), so automation,
// preset loads and host changes move it exactly like a mouse drag does.
//
// Lit branches glow with a soft pulse locked to the host's beat position (HostTempo). Without tempo or
// with the transport stopped the glow is steady.
class LightningSlider : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    struct Colours
    {
        juce::Colour track   { 0xff0b1a33 };   // navy-900
        juce::Colour outline { 0xff2b4a82 };   // navy-600
        juce::Colour dim     { 0xff1c3563 };
        juce::Colour bolt    { 0xff5ce1ff };   // electric accent
        juce::Colour core    { 0xfff4f7fc };   // white
        juce::Colour text    { 0xfff4f7fc };
        juce::Colour subText { 0xffa9b8d6 };
    };

    LightningSlider (juce::RangedAudioParameter& p, const HostTempo& tempoSource, const juce::String& captionText)
        : param (p),
          tempo (tempoSource),
          caption (captionText),
          attachment (p, [this] (float v) { parameterChanged (v); }, nullptr)
    {
        setWantsKeyboardFocus (true);
        setTitle (param.getName (64));
        setDescription ("Vertical slider. Drag down to increase, up to decrease.");
        setHelpText ("Shift-drag for fine control. Double-click to reset. Double-click the value to type it.");

        valueLabel.setJustificationType (juce::Justification::centred);
        valueLabel.setFont (uiFont (12.0f, true));
        valueLabel.setColour (juce::Label::textColourId, colours.text);
        valueLabel.setColour (juce::Label::textWhenEditingColourId, colours.text);
        valueLabel.setColour (juce::Label::backgroundWhenEditingColourId, colours.track);
        valueLabel.setColour (juce::Label::outlineWhenEditingColourId, colours.bolt);
        valueLabel.setEditable (false, true, false);
        valueLabel.setAccessible (false);   // the slider itself reports the value
        valueLabel.onTextChange = [this] { setFromText (valueLabel.getText()); };
        addAndMakeVisible (valueLabel);

        buildBolt ((juce::uint32) param.getParameterID().hashCode());
        attachment.sendInitialUpdate();
    }

    void setColours (const Colours& c)
    {
        colours = c;
        valueLabel.setColour (juce::Label::textColourId, colours.text);
        repaint();
    }

    // Lit fraction 0..1 (the parameter's normalised value).
    float getLitFraction() const noexcept { return norm; }
    float getCurrentGlow() const noexcept { return glow; }
    juce::String getValueText() const { return param.getCurrentValueAsText() + labelSuffix(); }

    // Moves the value as a vertical drag of dy pixels would (positive = down = more).
    void dragBy (float dy, bool fine)
    {
        const float h = juce::jmax (1.0f, trackArea().getHeight());
        dragNorm = juce::jlimit (0.0f, 1.0f, dragNorm + dy / h * (fine ? 0.1f : 1.0f));
        attachment.setValueAsPartOfGesture (param.convertFrom0to1 (dragNorm));
    }

    void resetToDefault() { setNormalised (param.getDefaultValue()); }

    void stepBy (float delta) { setNormalised (param.getValue() + delta); }

    // Inside a drag the change joins the open gesture (a double-click arrives mid-gesture);
    // otherwise it is a gesture of its own.
    void setNormalised (float n)
    {
        const float v = param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n));
        if (dragging)
        {
            dragNorm = juce::jlimit (0.0f, 1.0f, n);
            attachment.setValueAsPartOfGesture (v);
        }
        else
        {
            attachment.setValueAsCompleteGesture (v);
        }
    }

    // Recomputes the pulse from the host transport; repaints only when the glow moved.
    void updateGlow (double nowMs)
    {
        const auto s = tempo.read();
        const float g = HostTempo::glow (HostTempo::beatsAt (s, nowMs), s.bpm);
        if (std::abs (g - glow) > 0.004f)
        {
            glow = g;
            if (norm > 0.0f)
                repaint (trackArea().toNearestInt().expanded (2));
        }
    }

    //==================================================================================================================
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const auto track = trackArea();

        g.setColour (colours.subText);
        g.setFont (uiFont (11.0f, true));
        g.drawText (caption, b.removeFromTop (captionHeight), juce::Justification::centred);

        const bool enabled = isEnabled();
        g.setColour (colours.track);
        g.fillRoundedRectangle (track, 5.0f);
        g.setColour (colours.outline);
        g.drawRoundedRectangle (track, 5.0f, 1.0f);

        // cloud cap the bolt hangs from
        g.setColour (colours.outline);
        g.fillRoundedRectangle (track.getX() + 5.0f, track.getY() + 4.0f, track.getWidth() - 10.0f, 3.0f, 1.5f);

        const float base = strokeBase();
        auto strokeAll = [&] (float widthScale, juce::Colour c)
        {
            for (size_t i = 0; i < paths.size(); ++i)
            {
                g.setColour (c);
                g.strokePath (paths[i], juce::PathStrokeType (base * weights[i] * widthScale,
                                                              juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
            }
        };

        // unlit bolt
        strokeAll (1.0f, colours.dim);

        // lit part: everything above the thumb
        const float thumbY = thumbPosition();
        if (norm > 0.0f && enabled)
        {
            g.saveState();
            g.reduceClipRegion (track.withBottom (thumbY).toNearestInt());
            const float level = 0.45f + 0.55f * glow;   // pulse range is kept subtle
            strokeAll (5.0f, colours.bolt.withAlpha (0.10f * level));
            strokeAll (3.0f, colours.bolt.withAlpha (0.22f * level));
            strokeAll (1.8f, colours.bolt.withAlpha (0.55f * level));
            strokeAll (0.9f, colours.core.withAlpha (0.95f));
            g.restoreState();
        }

        // thumb
        const auto thumb = juce::Rectangle<float> (track.getWidth() * 0.72f, 4.0f).withCentre ({ track.getCentreX(), thumbY });
        g.setColour (enabled ? colours.core : colours.subText);
        g.fillRoundedRectangle (thumb, 2.0f);
        g.setColour (colours.bolt.withAlpha (enabled ? 0.8f : 0.3f));
        g.drawRoundedRectangle (thumb.expanded (1.0f), 2.5f, 1.0f);

        if (hasKeyboardFocus (false))
        {
            juce::Path ring;
            ring.addRoundedRectangle (track.expanded (3.0f), 7.0f);
            juce::Path dashed;
            const float dashes[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, ring, dashes, 2);
            g.setColour (colours.bolt);
            g.fillPath (dashed);
        }

        if (! enabled)
        {
            g.setColour (colours.track.withAlpha (0.5f));
            g.fillRoundedRectangle (track, 5.0f);
        }
    }

    void resized() override
    {
        valueLabel.setBounds (getLocalBounds().removeFromBottom (valueHeight));
        rebuildPaths();
    }

    //==================================================================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            showMenu();
            return;
        }
        grabKeyboardFocus();
        lastY = e.position.y;
        beginDrag();
    }

    // One host gesture per drag, so automation records one undo step.
    void beginDrag()
    {
        dragNorm = param.getValue();
        attachment.beginGesture();
        dragging = true;
    }

    void endDrag()
    {
        if (dragging)
            attachment.endGesture();
        dragging = false;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;
        const float dy = e.position.y - lastY;
        lastY = e.position.y;
        dragBy (dy, e.mods.isShiftDown());
    }

    void mouseUp (const juce::MouseEvent&) override { endDrag(); }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            resetToDefault();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const float dy = w.isReversed ? -w.deltaY : w.deltaY;
        // wheel down = more lightning, matching the drag direction
        stepBy (-dy * (e.mods.isShiftDown() ? 0.02f : 0.2f));
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const bool fine = key.getModifiers().isShiftDown();
        const float step = fine ? 0.001f : 0.01f;
        const int code = key.getKeyCode();

        // Down/PageDown light more (increase), matching the drag direction.
        if (code == juce::KeyPress::downKey || code == juce::KeyPress::rightKey) { stepBy (step); return true; }
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::leftKey)    { stepBy (-step); return true; }
        if (code == juce::KeyPress::pageDownKey) { stepBy (0.1f); return true; }
        if (code == juce::KeyPress::pageUpKey)   { stepBy (-0.1f); return true; }
        if (code == juce::KeyPress::homeKey) { setNormalised (0.0f); return true; }
        if (code == juce::KeyPress::endKey)  { setNormalised (1.0f); return true; }
        if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { resetToDefault(); return true; }
        if (code == juce::KeyPress::returnKey) { valueLabel.showEditor(); return true; }
        return false;
    }

    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override   { repaint(); }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<juce::AccessibilityHandler> (
            *this, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
            juce::AccessibilityHandler::Interfaces { std::make_unique<ValueInterface> (*this) });
    }

private:
    struct ValueInterface : public juce::AccessibilityValueInterface
    {
        explicit ValueInterface (LightningSlider& s) : owner (s) {}

        bool isReadOnly() const override { return false; }
        double getCurrentValue() const override { return owner.param.convertFrom0to1 (owner.param.getValue()); }
        juce::String getCurrentValueAsString() const override { return owner.getValueText(); }
        void setValue (double v) override
        {
            owner.setNormalised (owner.param.convertTo0to1 (owner.param.getNormalisableRange().snapToLegalValue ((float) v)));
        }
        void setValueAsString (const juce::String& t) override { owner.setFromText (t); }
        AccessibleValueRange getRange() const override
        {
            const auto& r = owner.param.getNormalisableRange();
            return { { r.start, r.end }, r.interval };
        }

        LightningSlider& owner;
    };

    struct Stroke
    {
        std::vector<juce::Point<float>> points;   // unit square, y down
        float weight;
    };

    static constexpr float captionHeight = 16.0f;
    static constexpr int valueHeight = 18;

    juce::Rectangle<float> trackArea() const
    {
        auto b = getLocalBounds().toFloat();
        b.removeFromTop (captionHeight + 2.0f);
        b.removeFromBottom ((float) valueHeight + 2.0f);
        const float w = juce::jlimit (24.0f, 64.0f, juce::jmin (b.getWidth(), b.getHeight() * 0.42f));
        return b.withSizeKeepingCentre (w, b.getHeight());
    }

    juce::Rectangle<float> boltArea() const
    {
        const auto t = trackArea();
        return t.reduced (t.getWidth() * 0.12f, 0.0f).withTrimmedTop (9.0f).withTrimmedBottom (6.0f);
    }

    float thumbPosition() const
    {
        const auto t = trackArea();
        return juce::jmap (norm, t.getY() + 8.0f, t.getBottom() - 4.0f);
    }

    float strokeBase() const { return juce::jlimit (1.0f, 4.0f, trackArea().getWidth() * 0.055f); }

    juce::String labelSuffix() const
    {
        const auto l = param.getLabel();
        return l.isEmpty() ? juce::String() : " " + l;
    }

    void parameterChanged (float newValue)
    {
        norm = juce::jlimit (0.0f, 1.0f, param.convertTo0to1 (newValue));
        valueLabel.setText (getValueText(), juce::dontSendNotification);
        setTooltip (param.getName (64) + ": " + getValueText()
                    + "\nDrag down to increase, Shift for fine, double-click to reset.");
        if (auto* h = getAccessibilityHandler())
            h->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
        repaint();
    }

    void setFromText (const juce::String& text)
    {
        setNormalised (param.getValueForText (text.trim()));
        valueLabel.setText (getValueText(), juce::dontSendNotification);
    }

    void showMenu()
    {
        juce::PopupMenu m;
        m.addItem ("Enter Value...", [this] { valueLabel.showEditor(); });
        m.addItem ("Reset to Default", [this] { resetToDefault(); });
        m.addSeparator();
        m.addItem ("Copy Value", [this] { juce::SystemClipboard::copyTextToClipboard (param.getCurrentValueAsText()); });
        m.addItem ("Paste Value", [this] { setFromText (juce::SystemClipboard::getTextFromClipboard()); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }

    // Deterministic bolt shape per parameter: a zig-zag trunk with forks and a few sub-forks.
    void buildBolt (juce::uint32 seed)
    {
        juce::Random rnd ((juce::int64) seed);
        strokes.clear();

        Stroke trunk { {}, 1.0f };
        constexpr int segs = 11;
        float x = 0.5f;
        for (int i = 0; i <= segs; ++i)
        {
            const float y = (float) i / (float) segs;
            if (i > 0)
                x = juce::jlimit (0.2f, 0.8f, 0.5f + ((i % 2 == 0) ? 1.0f : -1.0f) * (0.08f + 0.14f * rnd.nextFloat()));
            trunk.points.push_back ({ x, y });
        }
        strokes.push_back (trunk);

        for (int i = 1; i < segs - 1; ++i)
        {
            if (rnd.nextFloat() < 0.35f && i % 2 == 0)
                continue;
            const auto start = trunk.points[(size_t) i];
            const float dir = start.x < 0.5f ? -1.0f : 1.0f;
            Stroke fork { { start }, 0.55f };
            auto p = start;
            const int n = 2 + rnd.nextInt (3);
            for (int k = 0; k < n; ++k)
            {
                p = { juce::jlimit (0.02f, 0.98f, p.x + dir * (0.07f + 0.09f * rnd.nextFloat()) * (k % 2 == 0 ? 1.0f : 0.4f)),
                      juce::jmin (1.0f, p.y + 0.04f + 0.05f * rnd.nextFloat()) };
                fork.points.push_back (p);
            }
            strokes.push_back (fork);

            if (fork.points.size() > 2 && rnd.nextFloat() < 0.5f)
            {
                auto q = fork.points[1];
                Stroke sub { { q }, 0.35f };
                for (int k = 0; k < 2; ++k)
                {
                    q = { juce::jlimit (0.02f, 0.98f, q.x - dir * (0.04f + 0.05f * rnd.nextFloat())),
                          juce::jmin (1.0f, q.y + 0.035f + 0.03f * rnd.nextFloat()) };
                    sub.points.push_back (q);
                }
                strokes.push_back (sub);
            }
        }
    }

    void rebuildPaths()
    {
        const auto area = boltArea();
        paths.clear();
        weights.clear();
        for (const auto& s : strokes)
        {
            juce::Path p;
            for (size_t i = 0; i < s.points.size(); ++i)
            {
                const juce::Point<float> pt { area.getX() + s.points[i].x * area.getWidth(),
                                              area.getY() + s.points[i].y * area.getHeight() };
                i == 0 ? p.startNewSubPath (pt) : p.lineTo (pt);
            }
            paths.push_back (p);
            weights.push_back (s.weight);
        }
    }

    juce::RangedAudioParameter& param;
    const HostTempo& tempo;
    juce::String caption;
    juce::ParameterAttachment attachment;
    juce::Label valueLabel;
    Colours colours;

    std::vector<Stroke> strokes;
    std::vector<juce::Path> paths;
    std::vector<float> weights;

    float norm = 0.0f, dragNorm = 0.0f, lastY = 0.0f, glow = HostTempo::steadyGlow;
    bool dragging = false;

    juce::VBlankAttachment vblank { this, std::function<void()> ([this] { updateGlow (juce::Time::getMillisecondCounterHiRes()); }) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LightningSlider)
};
