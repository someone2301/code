#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Look.h"
#include "../HostTempo.h"

// Kaminari Vocal: `inverted` maps a fuller hammer to a lower parameter value (thresholds, Retune Speed),
// so pulling down always means more effect (DESIGN.md 6.5).
#include <vector>

// Vertical slider drawn as a war hammer wrapped in lightning.
//
// The hammer stands head up. Dragging down raises the parameter value: the hammer fills with colour from the
// top of the head down towards the pommel, and more electric arcs crackle around it (switched on in a fixed
// centre-out order, grey when off, white with a glow when on); dragging up empties it in reverse. The drawing
// is driven only by the parameter's value (through a ParameterAttachment), so automation, preset loads and
// host changes move it exactly like a mouse drag does.
//
// The glow and arcs pulse softly with the host's beat position (HostTempo). Without tempo or with the
// transport stopped the glow is steady.
class LightningSlider : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    struct Colours
    {
        juce::Colour steelDark  { 0xff1e2740 };
        juce::Colour steelLight { 0xff5d6a88 };
        juce::Colour latent     { 0xff7a8499 };   // arcs that are off: grey
        juce::Colour bolt       { 0xff5ce1ff };   // electric accent (fill, glow)
        juce::Colour core       { 0xfff4f7fc };   // white core of an active arc
        juce::Colour text       { 0xfff4f7fc };
        juce::Colour subText    { 0xffa9b8d6 };
        juce::Colour editBack   { 0xff0b1a33 };
    };

    LightningSlider (juce::RangedAudioParameter& p, const HostTempo& tempoSource, const juce::String& captionText, bool invertedMapping = false)
        : inverted (invertedMapping),
          param (p),
          tempo (tempoSource),
          caption (captionText),
          seed ((juce::uint32) p.getParameterID().hashCode()),
          attachment (p, [this] (float v) { parameterChanged (v); }, nullptr)
    {
        setWantsKeyboardFocus (true);
        setTitle (param.getName (64));
        setDescription ("Vertical slider. Drag down to fill the hammer and increase, up to decrease.");
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

    // Activation 0..1 of arc i. Arcs switch on in a fixed centre-out order:
    // value v activates v * numBolts arcs in that order; the next one fades in.
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
        attachment.setValueAsPartOfGesture (param.convertFrom0to1 (toParam (dragNorm)));
    }

    void resetToDefault() { setNormalised (toFill (param.getDefaultValue())); }

    void stepBy (float delta) { setNormalised (norm + delta); }

    // fill (0 = empty, 1 = full) <-> parameter's normalised value
    float toParam (float fill) const noexcept { return inverted ? 1.0f - fill : fill; }
    float toFill (float value) const noexcept { return inverted ? 1.0f - value : value; }

    // Inside a drag the change joins the open gesture (a double-click arrives mid-gesture);
    // otherwise it is a gesture of its own.
    void setNormalised (float n)
    {
        const float v = param.convertFrom0to1 (toParam (juce::jlimit (0.0f, 1.0f, n)));
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
                repaint();
        }
    }

    //==================================================================================================================
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        if (captionHeight > 0.0f)
        {
            g.setColour (colours.subText);
            g.setFont (uiFont (11.0f, true));
            g.drawText (caption, b.removeFromTop (captionHeight), juce::Justification::centred);
        }

        const bool enabled = isEnabled();
        const float pulse = 0.45f + 0.55f * glow;   // kept subtle
        const auto art = artArea();
        const float fillY = fillLevel();
        const auto stroke = [] (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };

        // aura behind the filled part
        if (enabled && norm > 0.0f)
        {
            const auto aura = juce::Rectangle<float> (headWidth() * 1.8f + 20.0f, (fillY - art.getY()) + 24.0f)
                                  .withCentre ({ art.getCentreX(), (art.getY() + fillY) * 0.5f });
            juce::ColourGradient halo (colours.bolt.withAlpha ((0.15f + 0.4f * norm) * pulse), aura.getCentreX(), aura.getCentreY(),
                                       colours.bolt.withAlpha (0.0f), aura.getRight(), aura.getCentreY(), true);
            g.setGradientFill (halo);
            g.fillEllipse (aura);
        }

        // arcs that are off
        for (int i = 0; i < numBolts; ++i)
        {
            const float level = enabled ? boltLevel (i) : 0.0f;
            if (level < 1.0f)
            {
                g.setColour (colours.latent.withAlpha (0.35f * (1.0f - level)));
                g.strokePath (arcs[(size_t) i], stroke (1.0f));
            }
        }

        // steel body
        g.setGradientFill (juce::ColourGradient (colours.steelLight, art.getCentreX() - headWidth() * 0.3f, art.getY(),
                                                 colours.steelDark, art.getCentreX() + headWidth() * 0.5f, art.getY(), false));
        g.fillPath (body);

        // colour fill from the top down to the value
        if (norm > 0.0f)
        {
            g.saveState();
            g.reduceClipRegion (body);
            g.reduceClipRegion (art.withBottom (fillY).toNearestInt().expanded (1, 0));
            juce::ColourGradient fill (colours.core, 0.0f, art.getY(), colours.bolt.darker (0.5f), 0.0f, art.getBottom(), false);
            fill.addColour (0.35, colours.bolt);
            g.setGradientFill (fill);
            g.fillRect (art.withBottom (fillY));
            g.restoreState();
        }
        g.setColour (colours.subText.withAlpha (0.7f));
        g.strokePath (body, juce::PathStrokeType (1.0f));
        g.setColour (juce::Colour (0xff0b1a33).withAlpha (0.55f));
        g.strokePath (detail, juce::PathStrokeType (1.1f));

        // fill edge
        if (norm > 0.0f)
        {
            g.saveState();
            g.reduceClipRegion (body);
            g.setColour (colours.core);
            g.drawLine (art.getX(), fillY, art.getRight(), fillY, 2.0f);
            g.restoreState();
        }

        // arcs that are on: glow + white core
        for (int i = 0; i < numBolts; ++i)
        {
            const float level = enabled ? boltLevel (i) : 0.0f;
            if (level <= 0.0f)
                continue;
            g.setColour (colours.bolt.withAlpha (0.35f * level * pulse));
            g.strokePath (arcs[(size_t) i], stroke (4.0f));
            g.setColour (colours.core.withAlpha (0.95f * level));
            g.strokePath (arcs[(size_t) i], stroke (1.3f));
        }

        if (hasKeyboardFocus (false))
        {
            juce::Path ring, dashed;
            ring.addRoundedRectangle (headBounds().expanded (5.0f), 6.0f);
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
        dragNorm = toFill (param.getValue());
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
            owner.setNormalised (owner.toFill (owner.param.convertTo0to1 (owner.param.getNormalisableRange().snapToLegalValue ((float) v))));
        }
        void setValueAsString (const juce::String& t) override { owner.setFromText (t); }
        AccessibleValueRange getRange() const override
        {
            const auto& r = owner.param.getNormalisableRange();
            return { { r.start, r.end }, r.interval };
        }

        LightningSlider& owner;
    };

    float captionHeight = 16.0f;
    int valueHeight = 18;

public:
    // Compact: no caption above and no value below (the Basic-view card draws its own).
    void setCompact (bool c)
    {
        captionHeight = c ? 0.0f : 16.0f;
        valueHeight = c ? 0 : 18;
        valueLabel.setVisible (! c);
        resized();
    }

private:

    juce::Rectangle<float> artArea() const
    {
        auto b = getLocalBounds().toFloat();
        b.removeFromTop (captionHeight);
        b.removeFromBottom ((float) valueHeight + (valueHeight > 0 ? 2.0f : 0.0f));
        return b.reduced (0.0f, 2.0f);
    }

    float headWidth() const { return juce::jmin (artArea().getWidth() * 0.42f, 64.0f); }

    juce::Rectangle<float> headBounds() const
    {
        const auto a = artArea();
        return { a.getCentreX() - headWidth() * 0.5f, a.getY(), headWidth(), a.getHeight() * 0.36f };
    }

    // The fill runs from the top of the head to the bottom of the pommel.
    float fillLevel() const { const auto a = artArea(); return a.getY() + norm * a.getHeight(); }

    float pullRange() const { return juce::jmax (1.0f, artArea().getHeight()); }

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
        norm = juce::jlimit (0.0f, 1.0f, toFill (param.convertTo0to1 (newValue)));
        valueLabel.setText (getValueText(), juce::dontSendNotification);
        setTooltip (param.getName (64) + ": " + getValueText()
                    + "\nPull the fader down to increase, Shift for fine, double-click to reset.");
        if (auto* h = getAccessibilityHandler())
            h->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
        repaint();
    }

    void setFromText (const juce::String& text)
    {
        setNormalised (toFill (param.getValueForText (text.trim())));
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

    // Hammer outline, engraving and the arcs around it. Deterministic per parameter and size.
    void rebuildGeometry()
    {
        const auto a = artArea();
        const auto head = headBounds();
        const float cx = a.getCentreX(), ch = juce::jmin (5.0f, head.getHeight() * 0.2f);
        const float gw = juce::jlimit (5.0f, 9.0f, head.getWidth() * 0.12f), collarH = 5.0f, pomH = 7.0f;
        const float gripTop = head.getBottom() + collarH, gripBot = a.getBottom() - pomH;

        body.clear();
        body.startNewSubPath (head.getX() + ch, head.getY());
        body.lineTo (head.getRight() - ch, head.getY());
        body.lineTo (head.getRight(), head.getY() + ch);
        body.lineTo (head.getRight(), head.getBottom() - ch);
        body.lineTo (head.getRight() - ch, head.getBottom());
        body.lineTo (head.getX() + ch, head.getBottom());
        body.lineTo (head.getX(), head.getBottom() - ch);
        body.lineTo (head.getX(), head.getY() + ch);
        body.closeSubPath();
        body.addRectangle (cx - gw - 2.0f, head.getBottom(), 2.0f * gw + 4.0f, collarH);          // collar
        body.addRectangle (cx - gw * 0.5f, gripTop, gw, juce::jmax (1.0f, gripBot - gripTop));     // grip
        body.startNewSubPath (cx - gw, gripBot);                                                  // pommel
        body.lineTo (cx + gw, gripBot);
        body.lineTo (cx + gw - 2.0f, gripBot + pomH);
        body.lineTo (cx - gw + 2.0f, gripBot + pomH);
        body.closeSubPath();
        body.setUsingNonZeroWinding (true);

        detail.clear();
        detail.startNewSubPath (head.getX() + 6.0f, head.getY() + 6.0f);
        detail.lineTo (head.getRight() - 6.0f, head.getY() + 6.0f);
        detail.startNewSubPath (head.getX() + 6.0f, head.getBottom() - 6.0f);
        detail.lineTo (head.getRight() - 6.0f, head.getBottom() - 6.0f);
        const float my = head.getCentreY();
        detail.startNewSubPath (cx - 6.0f, my - 5.0f);
        detail.lineTo (cx, my + 5.0f);
        detail.lineTo (cx + 6.0f, my - 5.0f);
        for (float y = gripTop + 4.0f; y < gripBot - 2.0f; y += 5.0f)
        {
            detail.startNewSubPath (cx - gw * 0.5f, y + 3.0f);
            detail.lineTo (cx + gw * 0.5f, y);
        }

        numBolts = 7;
        juce::Random rnd ((juce::int64) seed);
        arcs.assign ((size_t) numBolts, juce::Path());
        for (int i = 0; i < numBolts; ++i)
        {
            const float side = (i % 2 == 0) ? -1.0f : 1.0f;
            const float y0 = a.getY() + 4.0f + (a.getHeight() - 8.0f) * ((float) i + 0.5f) / (float) numBolts;
            const float edge = y0 < head.getBottom() ? head.getWidth() * 0.5f : (y0 < gripBot ? gw * 0.5f + 1.0f : gw);
            juce::Point<float> p { cx + side * (edge + 1.0f), y0 };
            auto& arc = arcs[(size_t) i];
            arc.startNewSubPath (p);
            const int len = 4 + rnd.nextInt (3);
            for (int k = 0; k < len; ++k)
            {
                p = { juce::jlimit (a.getX(), a.getRight(), p.x + side * (3.0f + rnd.nextFloat() * 5.0f)),
                      p.y + (rnd.nextFloat() - 0.6f) * 10.0f };
                arc.lineTo (p);
            }
            arc.lineTo (p.x - side * (2.0f + rnd.nextFloat() * 4.0f), p.y + 4.0f + rnd.nextFloat() * 6.0f);
        }
    }

    bool inverted = false;
    juce::RangedAudioParameter& param;
    const HostTempo& tempo;
    juce::String caption;
    juce::uint32 seed;
    juce::ParameterAttachment attachment;
    juce::Label valueLabel;
    Colours colours;

    juce::Path body, detail;
    std::vector<juce::Path> arcs;
    int numBolts = 0;

    float norm = 0.0f, dragNorm = 0.0f, lastY = 0.0f, glow = HostTempo::steadyGlow;
    bool dragging = false;

    juce::VBlankAttachment vblank { this, std::function<void()> ([this] { updateGlow (juce::Time::getMillisecondCounterHiRes()); }) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LightningSlider)
};
