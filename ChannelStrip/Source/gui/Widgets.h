#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Look.h"
#include <atomic>

using APVTS = juce::AudioProcessorValueTreeState;

// Rotary slider with the shared editing rules: vertical drag (Shift = 10 % speed), double-click resets to
// the parameter default, mouse wheel (Shift = finer), arrow / Page / Home / End keys, right-click menu
// (Enter Value, Reset, Copy, Paste), a hover tooltip with name, value and hints, and a focus outline.
// Every change outside a mouse drag is wrapped in its own host gesture.
class ParamSlider : public juce::Slider
{
public:
    ParamSlider() : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow)
    {
        setWantsKeyboardFocus (true);
    }

    void bind (juce::RangedAudioParameter* p, const juce::String& name)
    {
        param = p;
        displayName = p != nullptr ? p->getName (64) : name;
        setTitle (p != nullptr ? p->getName (64) : name);
        if (p != nullptr)
            setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    }

    // Normalised position 0..1 of the current value.
    float position() { return (float) valueToProportionOfLength (getValue()); }

    // Call at the start of a drag; applyDrag then moves from the current value.
    void startDrag() { dragPos = position(); }

    // A vertical drag of dy pixels (positive = up = more). 200 px covers the full range.
    void applyDrag (float dy, bool fine)
    {
        dragPos = juce::jlimit (0.0f, 1.0f, dragPos + dy / 200.0f * (fine ? 0.1f : 1.0f));
        setValue (proportionOfLengthToValue (dragPos), juce::sendNotificationSync);
    }

    // Moves the value by a fraction of the full range as one host gesture.
    void nudge (float delta) { setPositionAsGesture (position() + delta); }

    void setPositionAsGesture (float pos)
    {
        if (param != nullptr) param->beginChangeGesture();
        setValue (proportionOfLengthToValue (juce::jlimit (0.0f, 1.0f, pos)), juce::sendNotificationSync);
        if (param != nullptr) param->endChangeGesture();
    }

    void resetToDefault()
    {
        if (param != nullptr)
            setPositionAsGesture ((float) valueToProportionOfLength (param->convertFrom0to1 (param->getDefaultValue())));
    }

    juce::String getTooltip() override
    {
        return displayName + ": " + getTextFromValue (getValue())
             + "\nDrag up or down (Shift = fine). Double-click to reset. Right-click for more.";
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            showMenu();
            return;
        }
        grabKeyboardFocus();
        lastY = e.position.y;
        startDrag();
        juce::Slider::mouseDown (e);   // starts the host gesture
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || ! isEnabled())
            return;
        const float dy = lastY - e.position.y;
        lastY = e.position.y;
        applyDrag (dy, e.mods.isShiftDown());
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const float d = w.isReversed ? -w.deltaY : w.deltaY;
        nudge (d * (e.mods.isShiftDown() ? 0.02f : 0.2f));
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const float step = key.getModifiers().isShiftDown() ? 0.001f : 0.01f;
        const int code = key.getKeyCode();
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)   { nudge (step); return true; }
        if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)  { nudge (-step); return true; }
        if (code == juce::KeyPress::pageUpKey)   { nudge (0.1f); return true; }
        if (code == juce::KeyPress::pageDownKey) { nudge (-0.1f); return true; }
        if (code == juce::KeyPress::homeKey) { setPositionAsGesture (0.0f); return true; }
        if (code == juce::KeyPress::endKey)  { setPositionAsGesture (1.0f); return true; }
        if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { resetToDefault(); return true; }
        if (code == juce::KeyPress::returnKey) { showTextBox(); return true; }
        return juce::Slider::keyPressed (key);
    }

    void paint (juce::Graphics& g) override
    {
        juce::Slider::paint (g);
        if (hasKeyboardFocus (false))
        {
            juce::Path ring, dashed;
            ring.addRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f);
            const float dashes[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, ring, dashes, 2);
            g.setColour (juce::Colour (0xff5ce1ff));
            g.fillPath (dashed);
        }
    }

    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override   { repaint(); }

private:
    void showMenu()
    {
        juce::PopupMenu m;
        m.addItem ("Enter Value...", [this] { showTextBox(); });
        m.addItem ("Reset to Default", [this] { resetToDefault(); });
        m.addSeparator();
        m.addItem ("Copy Value", [this] { juce::SystemClipboard::copyTextToClipboard (getTextFromValue (getValue())); });
        m.addItem ("Paste Value", [this]
        {
            const auto v = getValueFromText (juce::SystemClipboard::getTextFromClipboard().trim());
            setPositionAsGesture ((float) valueToProportionOfLength (juce::jlimit (getMinimum(), getMaximum(), v)));
        });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }

    juce::RangedAudioParameter* param = nullptr;
    juce::String displayName;
    float lastY = 0.0f, dragPos = 0.0f;
};

// Rotary knob with a caption, bound to a parameter.
class Knob : public juce::Component
{
public:
    Knob (APVTS& state, const juce::String& paramId, const juce::String& name)
        : apvts (state), caption (name)
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
        slider.bind (apvts.getParameter (paramId), caption);   // after the attachment has set the range
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

    ParamSlider slider;
    juce::Label label;

private:
    APVTS& apvts;
    juce::String caption;
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
