#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Look.h"
#include "../HostTempo.h"

// Kaminari Vocal: `inverted` maps a fuller hammer to a lower parameter value (thresholds, Retune Speed),
// so pulling down always means more effect (DESIGN.md 6.5).
#include <algorithm>
#include <vector>

// Vertical slider drawn as a war hammer wrapped in lightning.
//
// The hammer stands head up. Dragging down raises the parameter value: the hammer fills with colour from the
// top of the head down towards the pommel; dragging up empties it. The drawing is driven only by the parameter's
// value (through a ParameterAttachment), so automation, preset loads and host changes move it exactly like a drag.
//
// Past an activation point (activationPoint) the hammer charges up: a white-cored, blue-edged aura with rising
// sparks and wisps, and procedural lightning in layers - fast small arcs crawling on the hammer, branching
// medium bolts, and slower large surges that flash the aura. Nothing is drawn at rest. Bolt count, brightness,
// speed and reach all follow the slider position continuously (intensity()); timing is random, so the
// pattern never repeats. The aura pulses softly with the host's beat position (HostTempo).
class LightningSlider : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    struct Colours
    {
        juce::Colour steelDark  { 0xff1e2740 };
        juce::Colour steelLight { 0xff5d6a88 };
        juce::Colour latent     { 0xff7a8499 };   // unused (kept so colour sets stay source compatible)
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

        rng.setSeed ((juce::int64) seed);
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

    // Lightning starts once the fill passes this point; below it the hammer is calm.
    static constexpr float activationPoint = 0.1f;

    // 0 at or below the activation point, rising to 1 at a full hammer.
    float intensity() const noexcept
    {
        if (! isEnabled()) return 0.0f;
        const float x = juce::jlimit (0.0f, 1.0f, (norm - activationPoint) / (1.0f - activationPoint));
        return x <= 0.0f ? 0.0f : std::pow (x, 0.8f);
    }

    int activeBolts() const noexcept { return (int) bolts.size(); }
    int activeParticles() const noexcept { return (int) particles.size(); }

    // Advances the animation by ms in 16 ms steps (snapshots and tests; the display normally runs it on v-blank).
    void advanceAnimation (double ms)
    {
        for (double t = 0; t < ms; t += 16.0)
            tick (animMs + 16.0);
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
        const bool glowMoved = std::abs (g - glow) > 0.004f;
        if (glowMoved) glow = g;
        const bool animating = intensity() > 0.0f || ! bolts.empty() || ! particles.empty();
        if (animating) tick (nowMs);
        else animMs = nowMs;
        if (animating || (glowMoved && norm > 0.0f))
            repaint();
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

        const float I = intensity();
        if (I > 0.0f && animMs <= 0.0)
            advanceAnimation (480.0);   // first paint (or a snapshot): start mid-animation, not empty
        if (enabled && I > 0.0f)
            paintAura (g, I, pulse);
        if (enabled)
            paintParticles (g, false);

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

        if (enabled)
        {
            paintBolts (g);
            paintParticles (g, true);
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
        b.removeFromTop (juce::jmin (16.0f, b.getHeight() * 0.1f));   // room above the head for the aura's flame
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

    // Hammer outline and engraving. Deterministic per size.
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
    }

    //==================================================================================================================
    // Charged-up effect: aura, lightning and particles

    enum Layer { Crawl, Branch, Wrap, Surge };

    struct Bolt
    {
        juce::Path path;
        double born = 0, life = 100;
        float width = 1.0f;
        int layer = Crawl;
    };

    struct Particle
    {
        juce::Point<float> pos, vel;
        double born = 0, life = 600;
        float size = 1.5f;
        bool wisp = false;
        float phase = 0;
    };

    float rand01() { return rng.nextFloat(); }
    float randIn (float lo, float hi) { return lo + (hi - lo) * rng.nextFloat(); }

    // Grip and pommel area (below the head).
    juce::Rectangle<float> gripBox() const
    {
        const auto a = artArea();
        const auto head = headBounds();
        const float gw = juce::jlimit (5.0f, 9.0f, head.getWidth() * 0.12f);
        return juce::Rectangle<float>::leftTopRightBottom (a.getCentreX() - gw, head.getBottom(), a.getCentreX() + gw, a.getBottom());
    }

    // A point on the hammer's outline (head sides and top, grip, pommel), with the outward direction there.
    std::pair<juce::Point<float>, float> pointOnHammer (bool headBias)
    {
        const auto a = artArea();
        const auto head = headBounds();
        const float cx = a.getCentreX();
        const float gw = juce::jlimit (5.0f, 9.0f, head.getWidth() * 0.12f);
        const float side = rand01() < 0.5f ? -1.0f : 1.0f;
        const float u = rand01();
        if (headBias || u < 0.6f)
        {
            if (rand01() < 0.25f)   // top edge, pointing up
                return { { randIn (head.getX() + 4.0f, head.getRight() - 4.0f), head.getY() }, -juce::MathConstants<float>::halfPi + randIn (-0.6f, 0.6f) };
            return { { cx + side * head.getWidth() * 0.5f, randIn (head.getY() + 3.0f, head.getBottom() - 3.0f) },
                     (side > 0 ? 0.0f : juce::MathConstants<float>::pi) + randIn (-0.7f, 0.5f) * side };
        }
        const float y = randIn (head.getBottom() + 4.0f, a.getBottom() - 2.0f);
        return { { cx + side * (gw * 0.5f + 1.0f), y }, (side > 0 ? 0.0f : juce::MathConstants<float>::pi) + randIn (-0.8f, 0.8f) };
    }

    // Jagged bolt from p along angle, with branches.
    void addJagged (juce::Path& path, juce::Point<float> p, float angle, float length, int segments, float jag, int branchDepth)
    {
        path.startNewSubPath (p);
        const float step = length / (float) segments;
        for (int k = 0; k < segments; ++k)
        {
            angle += randIn (-0.55f, 0.55f);
            const juce::Point<float> dir { std::cos (angle), std::sin (angle) };
            const juce::Point<float> nrm { -dir.y, dir.x };
            p = p + dir * step + nrm * randIn (-jag, jag);
            const auto room = getLocalBounds().toFloat().reduced (4.0f);
            p = { juce::jlimit (room.getX(), room.getRight(), p.x), juce::jlimit (room.getY(), room.getBottom(), p.y) };
            path.lineTo (p);
            if (branchDepth > 0 && k < segments - 1 && rand01() < 0.38f)
            {
                juce::Path branch;
                addJagged (branch, p, angle + (rand01() < 0.5f ? -1.0f : 1.0f) * randIn (0.35f, 0.9f),
                           length * randIn (0.25f, 0.5f), juce::jmax (2, segments / 2), jag * 0.8f, branchDepth - 1);
                path.addPath (branch);
                path.startNewSubPath (p);   // continue the trunk from the fork
            }
        }
    }

    void spawnBolt (int layer, float I)
    {
        if (bolts.size() >= 110) return;
        const auto a = artArea();
        const float reach = juce::jmax (a.getWidth(), a.getHeight());
        Bolt b;
        b.layer = layer;
        b.born = animMs;
        const float faster = 1.25f - 0.55f * I;   // brighter = faster
        switch (layer)
        {
            case Crawl:
            {
                // small arcs crackling all over the hammer: on the head and grip as well as just off their edges
                juce::Point<float> p;
                float ang;
                if (rand01() < 0.55f)
                {
                    const auto box = rand01() < 0.6f ? headBounds() : gripBox();
                    p = { randIn (box.getX(), box.getRight()), randIn (box.getY(), box.getBottom()) };
                    ang = randIn (0.0f, juce::MathConstants<float>::twoPi);
                }
                else
                {
                    auto [q, a0] = pointOnHammer (true);
                    p = q;
                    ang = a0 + juce::MathConstants<float>::pi * randIn (0.4f, 0.6f) * (rand01() < 0.5f ? 1.0f : -1.0f);
                }
                addJagged (b.path, p, ang, reach * randIn (0.05f, 0.12f) * (0.7f + 0.6f * I), 3 + rng.nextInt (3), 2.2f, rand01() < 0.25f ? 1 : 0);
                b.life = randIn (45.0f, 120.0f) * faster;
                b.width = randIn (0.7f, 1.15f);
                break;
            }
            case Wrap:
            {
                // a bolt across the hammer from one side to the other, as if wrapped around it
                const bool onHead = rand01() < 0.55f;
                const auto box = onHead ? headBounds() : gripBox();
                const bool fromLeft = rand01() < 0.5f;
                const float span = box.getWidth() + randIn (14.0f, 30.0f) * (0.6f + 0.6f * I);
                const juce::Point<float> p { fromLeft ? box.getX() - span * 0.2f : box.getRight() + span * 0.2f, randIn (box.getY(), box.getBottom()) };
                addJagged (b.path, p, (fromLeft ? 0.0f : juce::MathConstants<float>::pi) + randIn (-0.35f, 0.35f), span * 1.25f,
                           6 + rng.nextInt (4), 2.8f, 1);
                b.life = randIn (70.0f, 160.0f) * faster;
                b.width = randIn (0.9f, 1.4f);
                break;
            }
            case Branch:
            {
                auto [p, ang] = pointOnHammer (false);
                addJagged (b.path, p, ang, reach * randIn (0.14f, 0.28f) * (0.6f + 0.7f * I), 5 + rng.nextInt (4), 3.0f, 1 + (I > 0.5f ? 1 : 0));
                b.life = randIn (110.0f, 240.0f) * faster;
                b.width = randIn (1.1f, 1.7f);
                break;
            }
            default:
            {
                // surge: either a long bolt thrown well clear of the hammer, or one running the hammer's length
                if (rand01() < 0.45f)
                {
                    const auto a2 = artArea();
                    const bool up = rand01() < 0.6f;
                    const juce::Point<float> p { a2.getCentreX() + randIn (-headBounds().getWidth() * 0.4f, headBounds().getWidth() * 0.4f),
                                                 up ? a2.getBottom() : a2.getY() };
                    addJagged (b.path, p, (up ? -1.0f : 1.0f) * juce::MathConstants<float>::halfPi + randIn (-0.2f, 0.2f), a2.getHeight() * 1.05f,
                               10 + rng.nextInt (5), 4.0f, 2);
                }
                else
                {
                    auto [p, ang] = pointOnHammer (rand01() < 0.6f);
                    addJagged (b.path, p, ang, reach * randIn (0.32f, 0.55f) * (0.75f + 0.5f * I), 8 + rng.nextInt (5), 4.5f, 2);
                }
                b.life = randIn (180.0f, 380.0f) * faster;
                b.width = randIn (1.5f, 2.2f);
                break;
            }
        }
        bolts.push_back (std::move (b));
    }

    void spawnParticle (float I, bool wisp)
    {
        if (particles.size() >= 140) return;
        const auto head = headBounds();
        const auto a = artArea();
        Particle q;
        q.wisp = wisp;
        q.born = animMs;
        const float spread = head.getWidth() * (0.6f + 0.5f * I);
        q.pos = { a.getCentreX() + randIn (-spread, spread), randIn (head.getY(), a.getBottom()) };
        q.vel = { randIn (-8.0f, 8.0f), -randIn (18.0f, 55.0f) * (0.6f + 0.9f * I) };   // px per second, rising
        q.life = wisp ? randIn (500.0f, 1100.0f) : randIn (300.0f, 800.0f);
        q.size = wisp ? randIn (5.0f, 11.0f) : randIn (0.6f, 1.4f);
        q.phase = randIn (0.0f, juce::MathConstants<float>::twoPi);
        particles.push_back (q);
    }

    void tick (double nowMs)
    {
        const double dtMs = animMs > 0.0 ? juce::jlimit (0.0, 100.0, nowMs - animMs) : 16.0;
        animMs = nowMs;
        const float dt = (float) dtMs * 0.001f;
        const float I = intensity();

        bolts.erase (std::remove_if (bolts.begin(), bolts.end(), [&] (const Bolt& b) { return animMs - b.born > b.life; }), bolts.end());
        for (auto& q : particles) q.pos += q.vel * dt;
        particles.erase (std::remove_if (particles.begin(), particles.end(), [&] (const Particle& q) { return animMs - q.born > q.life; }),
                         particles.end());
        surge = std::max (0.0f, surge - dt * 3.5f);
        if (I <= 0.0f)
            return;

        // spawn rates per second; jittered so the rhythm never settles
        auto emit = [&] (float& acc, float rate, auto&& make)
        {
            acc += rate * dt * randIn (0.4f, 1.6f);
            while (acc >= 1.0f) { acc -= 1.0f; make(); }
        };
        emit (accCrawl,  24.0f + 230.0f * I,        [&] { spawnBolt (Crawl, I); });
        emit (accWrap,   2.0f + 22.0f * I,          [&] { spawnBolt (Wrap, I); });
        emit (accBranch, 2.0f + 36.0f * I * I,      [&] { spawnBolt (Branch, I); });
        emit (accSpark,  5.0f + 40.0f * I,          [&] { spawnParticle (I, false); });
        emit (accWisp,   1.5f + 9.0f * I,           [&] { spawnParticle (I, true); });

        if (nextSurgeMs <= 0.0)
            nextSurgeMs = animMs + randIn (300.0f, 1200.0f);
        if (animMs >= nextSurgeMs && I > 0.15f)
        {
            const int n = 1 + (int) (I * 2.0f + rand01() * 1.5f);
            for (int k = 0; k < n; ++k) spawnBolt (Surge, I);
            surge = juce::jmin (1.0f, 0.55f + 0.45f * I);
            nextSurgeMs = animMs + (2200.0f - 1750.0f * I) * randIn (0.4f, 1.5f);
        }
    }

    // Super Saiyan 2 style aura: a jagged flame of sharp tongues that lean upwards and flicker fast, following the
    // hammer's silhouette (wide around the head, narrow along the grip), in three layers (translucent blue, lighter
    // blue, white core) with crisp outlines. Tongue tips are kept inside the control (shortened, never cut off).
    juce::Path auraLayer (float pad, int tongues, float tongueLen, float t, int seedOffset) const
    {
        const auto a = artArea();
        const auto head = headBounds();
        const auto room = getLocalBounds().toFloat().reduced (2.0f);
        const float cx = a.getCentreX();
        const float gripHalf = juce::jlimit (5.0f, 9.0f, head.getWidth() * 0.12f) + 4.0f;
        // half-width of the silhouette at height y (+ pad): the head, then a short taper to the grip
        auto halfWidth = [&] (float y)
        {
            const float taper = juce::jlimit (0.0f, 1.0f, (y - head.getBottom()) / (head.getHeight() * 0.6f));
            return pad + head.getWidth() * 0.5f + (gripHalf - head.getWidth() * 0.5f) * taper * 0.75f;
        };
        const float top = head.getY() - pad * 0.6f, bottom = a.getBottom() + pad * 0.4f;
        // contour: right side bottom -> top, over the top, left side top -> bottom, under the pommel
        std::vector<juce::Point<float>> pts;
        constexpr int side = 24, cap = 10;
        for (int k = 0; k <= side; ++k) { const float y = bottom + (top - bottom) * (float) k / side; pts.push_back ({ cx + halfWidth (y), y }); }
        for (int k = 1; k < cap; ++k)
        {
            const float th = juce::MathConstants<float>::pi * (float) k / cap;
            const float w = halfWidth (top);
            pts.push_back ({ cx + w * std::cos (th), top - pad * 0.5f * std::sin (th) });
        }
        for (int k = 0; k <= side; ++k) { const float y = top + (bottom - top) * (float) k / side; pts.push_back ({ cx - halfWidth (y), y }); }
        for (int k = 1; k < cap; ++k)
        {
            const float th = juce::MathConstants<float>::pi * (float) k / cap;
            const float w = halfWidth (bottom);
            pts.push_back ({ cx - w * std::cos (th), bottom + pad * 0.3f * std::sin (th) });
        }
        const int n = (int) pts.size();
        juce::Path p;
        const int step = juce::jmax (1, n / tongues);
        for (int k = 0; k < n; k += step)
        {
            const auto v = pts[(size_t) k];
            if (k == 0) p.startNewSubPath (v); else p.lineTo (v);
            // tongue halfway to the next valley: out along the contour's normal, leaning up like a flame
            const int m = std::min (n - 1, k + step / 2);
            const auto b = pts[(size_t) m];
            const auto tangent = pts[(size_t) std::min (n - 1, m + 1)] - pts[(size_t) std::max (0, m - 1)];
            juce::Point<float> nrm { tangent.y, -tangent.x };
            nrm = nrm / std::max (0.001f, nrm.getDistanceFromOrigin());
            if ((b - juce::Point<float> (cx, a.getCentreY())).getDotProduct (nrm) < 0.0f) nrm = -nrm;   // outward
            juce::Point<float> dir { nrm.x * 0.6f, nrm.y * 0.6f - 0.8f };
            dir = dir / std::max (0.001f, dir.getDistanceFromOrigin());
            const float h = (float) ((k * 7919 + seedOffset * 104729) % 1000) / 1000.0f;   // fixed per tongue
            const float flick = 0.55f + 0.45f * std::sin (t * (17.0f + 9.0f * h) + h * 40.0f) * std::sin (t * (5.0f + 3.0f * h) + h * 13.0f);
            const float upness = juce::jlimit (0.0f, 1.0f, 0.5f - 0.5f * nrm.y);          // taller where the contour faces up
            const float len = tongueLen * (0.45f + 0.75f * upness) * (0.45f + 0.75f * flick);
            auto tip = b + dir * len;
            tip = { juce::jlimit (room.getX(), room.getRight(), tip.x), juce::jlimit (room.getY(), room.getBottom(), tip.y) };
            p.lineTo (tip);
        }
        p.closeSubPath();
        return p;
    }

    void paintAura (juce::Graphics& g, float I, float pulse)
    {
        const float t = (float) (animMs * 0.001);
        const float bright = juce::jlimit (0.0f, 1.0f, (0.4f + 0.55f * I) * (0.8f + 0.2f * pulse) + 0.35f * surge);
        const float tongue = 8.0f + 18.0f * I + 6.0f * surge;
        const juce::Path outer = auraLayer (6.0f + 6.0f * I, 26, tongue, t, 1);
        const juce::Path mid   = auraLayer (3.0f + 4.0f * I, 22, tongue * 0.7f, t * 1.3f, 2);
        const juce::Path core  = auraLayer (1.0f + 2.0f * I, 18, tongue * 0.45f, t * 1.7f, 3);

        const auto st = [] (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded); };
        g.setColour (colours.bolt.withAlpha (0.10f * bright));
        g.strokePath (outer, st (6.0f));                                    // soft glow outside the outline
        g.setColour (colours.bolt.withAlpha (0.28f * bright));
        g.fillPath (outer);
        g.setColour (colours.bolt.brighter (0.25f).withAlpha (0.9f * bright));
        g.strokePath (outer, st (1.5f));
        g.setColour (colours.bolt.brighter (0.5f).withAlpha (0.34f * bright));
        g.fillPath (mid);
        g.setColour (colours.core.withAlpha (0.55f * bright));
        g.strokePath (mid, st (1.0f));
        g.setColour (colours.core.withAlpha (0.6f * bright));
        g.fillPath (core);
        g.setColour (colours.core.withAlpha (0.85f * bright));
        g.strokePath (core, st (0.8f));
    }

    void paintBolts (juce::Graphics& g)
    {
        const auto stroke = [] (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded); };
        for (auto& b : bolts)
        {
            const float age = (float) ((animMs - b.born) / b.life);
            float alpha = std::sqrt (juce::jlimit (0.0f, 1.0f, 1.0f - age));
            alpha *= rand01() < 0.18f ? 0.25f : randIn (0.75f, 1.0f);   // flicker / strobe
            if (alpha <= 0.02f) continue;
            const float w = b.width * (b.layer == Surge ? 1.0f + 0.4f * surge : 1.0f);
            g.setColour (colours.bolt.withAlpha (0.14f * alpha));
            g.strokePath (b.path, stroke (w * 7.0f));
            g.setColour (colours.bolt.withAlpha (0.7f * alpha));
            g.strokePath (b.path, stroke (w * 2.6f));
            g.setColour (colours.core.withAlpha (alpha));
            g.strokePath (b.path, stroke (w * 0.9f));
        }
    }

    // front = sparks drawn over the hammer; behind = wisps drawn under it
    void paintParticles (juce::Graphics& g, bool front)
    {
        for (auto& q : particles)
        {
            if (q.wisp == front) continue;
            const float age = (float) ((animMs - q.born) / q.life);
            const float alpha = juce::jlimit (0.0f, 1.0f, std::sin (juce::MathConstants<float>::pi * juce::jlimit (0.0f, 1.0f, age)));
            if (q.wisp)
            {
                // a short curling streak, rising
                juce::Path w;
                const float sway = std::sin (q.phase + (float) animMs * 0.006f) * 3.0f;
                w.startNewSubPath (q.pos.x, q.pos.y + q.size);
                w.quadraticTo (q.pos.x + sway, q.pos.y + q.size * 0.4f, q.pos.x - sway * 0.5f, q.pos.y - q.size * 0.4f);
                g.setColour (colours.bolt.withAlpha (0.35f * alpha));
                g.strokePath (w, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (colours.core.withAlpha (0.25f * alpha));
                g.strokePath (w, juce::PathStrokeType (0.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            else
            {
                juce::ColourGradient halo (colours.bolt.withAlpha (0.55f * alpha), q.pos.x, q.pos.y, colours.bolt.withAlpha (0.0f),
                                           q.pos.x + q.size * 2.2f, q.pos.y, true);
                g.setGradientFill (halo);
                g.fillEllipse (q.pos.x - q.size * 2.2f, q.pos.y - q.size * 2.2f, q.size * 4.4f, q.size * 4.4f);
                g.setColour (colours.core.withAlpha (0.95f * alpha));
                g.fillEllipse (q.pos.x - q.size * 0.6f, q.pos.y - q.size * 0.6f, q.size * 1.2f, q.size * 1.2f);
            }
        }
    }

    std::vector<Bolt> bolts;
    std::vector<Particle> particles;
    juce::Random rng;
    double animMs = 0, nextSurgeMs = 0;
    float surge = 0, accCrawl = 0, accWrap = 0, accBranch = 0, accSpark = 0, accWisp = 0;

    bool inverted = false;
    juce::RangedAudioParameter& param;
    const HostTempo& tempo;
    juce::String caption;
    juce::uint32 seed;
    juce::ParameterAttachment attachment;
    juce::Label valueLabel;
    Colours colours;

    juce::Path body, detail;

    float norm = 0.0f, dragNorm = 0.0f, lastY = 0.0f, glow = HostTempo::steadyGlow;
    bool dragging = false;

    juce::VBlankAttachment vblank { this, std::function<void()> ([this] { updateGlow (juce::Time::getMillisecondCounterHiRes()); }) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LightningSlider)
};
