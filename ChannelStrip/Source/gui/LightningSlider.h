#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Look.h"
#include "../HostTempo.h"
#include <vector>

// Vertical slider drawn as a thunder cloud with a row of lightning strikes below it.
//
// The user pulls a grip straight down from the centre of the cloud. Pulling down raises the parameter
// value and switches on more strikes across the width, from the centre outwards; pushing up switches them
// off in reverse order. An active strike always glows over its full length. The drawing is driven only by
// the parameter's value (through a ParameterAttachment), so automation, preset loads and host changes move
// it exactly like a mouse drag does.
//
// Active strikes glow with a soft pulse locked to the host's beat position (HostTempo). Without tempo or
// with the transport stopped the glow is steady.
class LightningSlider : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    struct Colours
    {
        juce::Colour cloudDark  { 0xff0e1426 };
        juce::Colour cloudLight { 0xff56627f };
        juce::Colour latent     { 0xff1c3563 };   // strikes that are off
        juce::Colour bolt       { 0xff5ce1ff };   // electric accent (glow)
        juce::Colour core       { 0xfff4f7fc };   // white core of an active strike
        juce::Colour text       { 0xfff4f7fc };
        juce::Colour subText    { 0xffa9b8d6 };
        juce::Colour editBack   { 0xff0b1a33 };
    };

    LightningSlider (juce::RangedAudioParameter& p, const HostTempo& tempoSource, const juce::String& captionText)
        : param (p),
          tempo (tempoSource),
          caption (captionText),
          seed ((juce::uint32) p.getParameterID().hashCode()),
          attachment (p, [this] (float v) { parameterChanged (v); }, nullptr)
    {
        setWantsKeyboardFocus (true);
        setTitle (param.getName (64));
        setDescription ("Vertical slider. Pull down from the cloud to increase, push up to decrease.");
        setHelpText ("Shift-drag for fine control. Double-click to reset. Double-click the value to type it.");

        valueLabel.setJustificationType (juce::Justification::centred);
        valueLabel.setFont (uiFont (12.0f, true));
        valueLabel.setColour (juce::Label::textColourId, colours.text);
        valueLabel.setColour (juce::Label::textWhenEditingColourId, colours.text);
        valueLabel.setColour (juce::Label::backgroundWhenEditingColourId, colours.editBack);
        valueLabel.setColour (juce::Label::outlineWhenEditingColourId, colours.bolt);
        valueLabel.setEditable (false, true, false);
        valueLabel.setAccessible (false);   // the slider itself reports the value
        valueLabel.onTextChange = [this] { setFromText (valueLabel.getText()); };
        addAndMakeVisible (valueLabel);

        attachment.sendInitialUpdate();
    }

    void setColours (const Colours& c)
    {
        colours = c;
        valueLabel.setColour (juce::Label::textColourId, colours.text);
        repaint();
    }

    // Parameter's normalised value 0..1.
    float getLitFraction() const noexcept { return norm; }
    float getCurrentGlow() const noexcept { return glow; }
    juce::String getValueText() const { return param.getCurrentValueAsText() + labelSuffix(); }

    int getNumBolts() const noexcept { return numBolts; }

    // Activation 0..1 of strike i (0 = leftmost). Strikes switch on from the centre outwards:
    // value v activates v * numBolts strikes in that order; the next one fades in.
    float boltLevel (int i) const noexcept
    {
        if (i < 0 || i >= numBolts)
            return 0.0f;
        const float rank = (float) rankOf (i);
        return juce::jlimit (0.0f, 1.0f, norm * (float) numBolts - rank);
    }

    // Moves the value as a vertical drag of dy pixels would (positive = down = more).
    void dragBy (float dy, bool fine)
    {
        const float h = juce::jmax (1.0f, pullRange());
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
                repaint (artArea().toNearestInt().expanded (4));
        }
    }

    //==================================================================================================================
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (colours.subText);
        g.setFont (uiFont (11.0f, true));
        g.drawText (caption, b.removeFromTop (captionHeight), juce::Justification::centred);

        const bool enabled = isEnabled();
        const float base = strokeBase();
        const float pulse = 0.45f + 0.55f * glow;   // kept subtle
        const auto stroke = [] (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded); };

        // strikes (drawn first so they emerge from under the cloud)
        for (int i = 0; i < numBolts; ++i)
        {
            const float level = enabled ? boltLevel (i) : 0.0f;
            const auto& bolt = bolts[(size_t) i];

            if (level < 1.0f)
                for (size_t k = 0; k < bolt.paths.size(); ++k)
                {
                    g.setColour (colours.latent.withAlpha (0.55f * (1.0f - level)));
                    g.strokePath (bolt.paths[k], stroke (base * weightOf (k)));
                }

            if (level <= 0.0f)
                continue;

            for (size_t k = 0; k < bolt.paths.size(); ++k)
            {
                const float w = base * weightOf (k);
                g.setColour (colours.bolt.withAlpha (0.08f * level * pulse));
                g.strokePath (bolt.paths[k], stroke (w * 6.0f));
                g.setColour (colours.bolt.withAlpha (0.20f * level * pulse));
                g.strokePath (bolt.paths[k], stroke (w * 3.0f));
                g.setColour (colours.bolt.withAlpha (0.55f * level * pulse));
                g.strokePath (bolt.paths[k], stroke (w * 1.7f));
                g.setColour (colours.core.withAlpha (0.95f * level));
                g.strokePath (bolt.paths[k], stroke (w * 0.85f));
            }
        }

        // cloud: dark body, lit from below by the active strikes
        const auto cloud = cloudArea();
        g.setColour (colours.cloudDark);
        g.fillPath (cloudPath);
        for (const auto& puff : puffRects)   // back to front, each puff lit from above
        {
            juce::ColourGradient shade (colours.cloudLight, puff.getCentreX() - puff.getWidth() * 0.15f, puff.getY() + puff.getHeight() * 0.15f,
                                        colours.cloudDark, puff.getCentreX(), puff.getBottom(), true);
            g.setGradientFill (shade);
            g.fillEllipse (puff);
        }
        const float active = enabled ? norm : 0.0f;
        if (active > 0.0f)
        {
            juce::ColourGradient under (colours.bolt.withAlpha (0.45f * pulse * juce::jmin (1.0f, active * 1.5f)),
                                        cloud.getCentreX(), cloud.getBottom(),
                                        colours.bolt.withAlpha (0.0f),
                                        cloud.getCentreX() + cloud.getWidth() * 0.55f, cloud.getBottom(), true);
            g.setGradientFill (under);
            g.fillPath (cloudPath);
        }
        g.setColour (colours.cloudLight.withAlpha (0.35f));
        g.strokePath (cloudPath, juce::PathStrokeType (0.8f));

        // pull grip hanging from the centre of the cloud
        const float gy = gripY();
        const float cx = cloud.getCentreX();
        g.setColour (colours.core.withAlpha (enabled ? 0.55f : 0.25f));
        g.drawLine (cx, cloud.getBottom() - 2.0f, cx, gy, 1.2f);
        const auto grip = juce::Rectangle<float> (gripWidth(), 6.0f).withCentre ({ cx, gy });
        g.setColour (colours.bolt.withAlpha ((enabled ? 0.35f : 0.1f) * pulse));
        g.fillRoundedRectangle (grip.expanded (3.0f), 5.0f);
        g.setColour (enabled ? colours.core : colours.subText);
        g.fillRoundedRectangle (grip, 3.0f);

        if (hasKeyboardFocus (false))
        {
            juce::Path ring, dashed;
            ring.addRoundedRectangle (grip.expanded (6.0f), 7.0f);
            const float dashes[] = { 3.0f, 2.5f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, ring, dashes, 2);
            g.setColour (colours.bolt);
            g.fillPath (dashed);
        }
    }

    void resized() override
    {
        valueLabel.setBounds (getLocalBounds().removeFromBottom (valueHeight));
        rebuildGeometry();
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
        // wheel down = more strikes, matching the drag direction
        stepBy (-dy * (e.mods.isShiftDown() ? 0.02f : 0.2f));
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const bool fine = key.getModifiers().isShiftDown();
        const float step = fine ? 0.001f : 0.01f;
        const int code = key.getKeyCode();

        // Down/PageDown add strikes (increase), matching the drag direction.
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

    // One strike: paths grouped by thickness (trunk, branches, filaments).
    struct Bolt
    {
        std::vector<juce::Path> paths;
    };

    static constexpr float captionHeight = 16.0f;
    static constexpr int valueHeight = 18;
    static constexpr float weights[3] = { 1.0f, 0.5f, 0.28f };

    static float weightOf (size_t k) noexcept { return weights[juce::jmin<size_t> (k, 2)]; }

    juce::Rectangle<float> artArea() const
    {
        auto b = getLocalBounds().toFloat();
        b.removeFromTop (captionHeight);
        b.removeFromBottom ((float) valueHeight + 2.0f);
        return b;
    }

    juce::Rectangle<float> cloudArea() const
    {
        const auto a = artArea();
        return a.withHeight (juce::jlimit (18.0f, 64.0f, a.getHeight() * 0.30f)).withTrimmedTop (2.0f);
    }

    float gripWidth() const { return juce::jlimit (14.0f, 28.0f, artArea().getWidth() * 0.22f); }

    float pullRange() const
    {
        const auto a = artArea();
        return juce::jmax (1.0f, a.getBottom() - 6.0f - (cloudArea().getBottom() + 6.0f));
    }

    float gripY() const { return cloudArea().getBottom() + 6.0f + norm * pullRange(); }

    float strokeBase() const { return juce::jlimit (0.9f, 2.6f, artArea().getWidth() / (float) juce::jmax (1, numBolts) * 0.11f); }

    // Centre-out order: rank 0 is the middle strike, then alternately right and left of it.
    int rankOf (int i) const noexcept
    {
        const int centre = numBolts / 2;
        const int d = i - centre;
        if (d == 0)
            return 0;
        return d > 0 ? 2 * d - 1 : -2 * d;
    }

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
                    + "\nPull down from the cloud to increase, Shift for fine, double-click to reset.");
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

    // Jagged path heading in direction `angle` (radians from straight down), with side branches.
    void growBranch (juce::Random& rnd, Bolt& bolt, juce::Point<float> start, float angle, float length,
                     int depth, float minX, float maxX, float bottom, float step)
    {
        juce::Path& path = bolt.paths[(size_t) juce::jmin (depth, 2)];
        path.startNewSubPath (start);
        auto p = start;
        const int steps = juce::jmax (3, (int) (length / step));
        for (int s = 0; s < steps && p.y < bottom; ++s)
        {
            const float a = angle + (rnd.nextFloat() - 0.5f) * (depth == 0 ? 0.8f : 0.9f);
            p = { juce::jlimit (minX, maxX, p.x + std::sin (a) * step), juce::jmin (bottom, p.y + std::cos (a) * step) };
            path.lineTo (p);

            if (depth < 2 && s > 0 && rnd.nextFloat() < (depth == 0 ? 0.22f : 0.18f))
            {
                const float side = rnd.nextBool() ? 1.0f : -1.0f;
                growBranch (rnd, bolt, p, angle + side * (0.55f + 0.5f * rnd.nextFloat()),
                            length * (depth == 0 ? 0.3f + 0.25f * rnd.nextFloat() : 0.35f + 0.2f * rnd.nextFloat()),
                            depth + 1, minX, maxX, bottom, step * 0.75f);
                path.startNewSubPath (p);   // continue the parent from the fork point
            }
        }
    }

    // Deterministic per parameter and size: strike count follows the width (odd, 3..9).
    void rebuildGeometry()
    {
        const auto art = artArea();
        const auto cloud = cloudArea();

        int n = juce::jlimit (3, 9, juce::roundToInt (art.getWidth() / 16.0f));
        if (n % 2 == 0)
            --n;
        numBolts = n;

        juce::Random rnd ((juce::int64) seed);
        bolts.assign ((size_t) numBolts, Bolt {});
        const float colW = art.getWidth() / (float) numBolts;
        const float top = cloud.getBottom() - cloud.getHeight() * 0.25f;
        const float bottom = art.getBottom() - 2.0f;
        const float step = juce::jmax (3.0f, (bottom - top) / 14.0f);

        for (int i = 0; i < numBolts; ++i)
        {
            auto& bolt = bolts[(size_t) i];
            bolt.paths.assign (3, juce::Path());
            const float cx = art.getX() + colW * ((float) i + 0.3f + 0.4f * rnd.nextFloat());
            // outer strikes lean outwards a little, like a storm front
            const float lean = ((float) i - (float) (numBolts - 1) * 0.5f) / (float) numBolts * 0.35f;
            growBranch (rnd, bolt, { cx, top }, lean, bottom - top, 0,
                        art.getX() + 1.0f, art.getRight() - 1.0f, bottom, step);
        }

        // cloud outline from overlapping puffs (unit coordinates inside the cloud box)
        cloudPath.clear();
        puffRects.clear();
        // x, y, w, h in the cloud box; listed back to front (upper billows first, low dark base last)
        static constexpr float puffs[][4] = {
            { 0.20f, 0.00f, 0.30f, 0.62f }, { 0.44f, -0.06f, 0.34f, 0.70f }, { 0.66f, 0.08f, 0.26f, 0.55f },
            { 0.02f, 0.28f, 0.26f, 0.52f }, { 0.12f, 0.18f, 0.30f, 0.60f }, { 0.34f, 0.16f, 0.32f, 0.66f },
            { 0.56f, 0.20f, 0.30f, 0.60f }, { 0.76f, 0.30f, 0.24f, 0.50f },
            { 0.06f, 0.52f, 0.30f, 0.46f }, { 0.30f, 0.54f, 0.40f, 0.46f }, { 0.62f, 0.52f, 0.32f, 0.46f }
        };
        for (const auto& pf : puffs)
        {
            const juce::Rectangle<float> r (cloud.getX() + pf[0] * cloud.getWidth(), cloud.getY() + pf[1] * cloud.getHeight(),
                                            pf[2] * cloud.getWidth(), pf[3] * cloud.getHeight());
            puffRects.push_back (r);
            cloudPath.addEllipse (r);
        }
        cloudPath.setUsingNonZeroWinding (true);
    }

    juce::RangedAudioParameter& param;
    const HostTempo& tempo;
    juce::String caption;
    juce::uint32 seed;
    juce::ParameterAttachment attachment;
    juce::Label valueLabel;
    Colours colours;

    std::vector<Bolt> bolts;
    juce::Path cloudPath;
    std::vector<juce::Rectangle<float>> puffRects;
    int numBolts = 0;

    float norm = 0.0f, dragNorm = 0.0f, lastY = 0.0f, glow = HostTempo::steadyGlow;
    bool dragging = false;

    juce::VBlankAttachment vblank { this, std::function<void()> ([this] { updateGlow (juce::Time::getMillisecondCounterHiRes()); }) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LightningSlider)
};
