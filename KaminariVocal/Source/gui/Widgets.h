#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Look.h"
#include <atomic>

using APVTS = juce::AudioProcessorValueTreeState;

// True when the component and all its parents below the window are visible, i.e. it is on the current view or page.
// Displays skip their timer work (analyzers, history smoothing, animation) while they are on a hidden page, so the
// visible graphs get the message thread to themselves. Unlike isShowing() it does not look at the window itself (the
// host shows and hides that; the off-screen tests never show it).
inline bool visibleInWindow (const juce::Component& c)
{
    for (auto* p = &c; p->getParentComponent() != nullptr; p = p->getParentComponent())
        if (! p->isVisible()) return false;
    return true;
}

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

    juce::RangedAudioParameter* getParam() const noexcept { return param; }

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

    // One-sentence description shown in the tooltip between the value and the editing hints.
    void setHint (const juce::String& text) { hint = text; setDescription (text); }

    juce::String getTooltip() override
    {
        return displayName + ": " + getTextFromValue (getValue())
             + (hint.isNotEmpty() ? "\n" + hint : juce::String())
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
        juce::Slider::mouseDown (e);   // starts the host gesture (a horizontal slider also jumps to the click)
        startDrag();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || ! isEnabled())
            return;
        if (isHorizontal() || isVertical())
        {
            // linear sliders follow the mouse, like any fader
            juce::Slider::mouseDrag (e);
            return;
        }
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
        if (code == juce::KeyPress::returnKey) { enterValue(); return true; }
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
        m.addItem ("Enter Value...", [this] { enterValue(); });
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

    void enterValue() { if (onEnterValue) onEnterValue(); else showTextBox(); }

    juce::RangedAudioParameter* param = nullptr;
    juce::String displayName, hint;

public:
    std::function<void()> onEnterValue;   // set by Knob: opens its value label for typing

private:
    float lastY = 0.0f, dragPos = 0.0f;
};

// Rotary knob with a caption, bound to a parameter.
class Knob : public juce::Component
{
public:
    // Ring knob with the name and the value underneath (as in the GUI preview). Double-click the value to type one.
    Knob (APVTS& state, const juce::String& paramId, const juce::String& name, const juce::String& hint = {})
        : apvts (state), caption (name)
    {
        addAndMakeVisible (slider);
        addAndMakeVisible (label);
        addAndMakeVisible (value);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (kvtheme::font (11.5f, 0));
        label.setColour (juce::Label::textColourId, kvtheme::mist);
        label.setInterceptsMouseClicks (false, false);
        value.setJustificationType (juce::Justification::centred);
        value.setFont (kvtheme::font (13.0f, 1));
        value.setEditable (false, true, false);
        value.setTooltip ("Double-click to type a value.");
        value.onTextChange = [this]
        {
            if (auto* p = apvts.getParameter (paramIdStr))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->getValueForText (value.getText().trim()));
                p->endChangeGesture();
            }
            refreshValue();
        };
        attach (paramId);
        slider.setHint (hint);
        slider.onEnterValue = [this] { value.showEditor(); };
    }

    void attach (const juce::String& paramId)
    {
        paramIdStr = paramId;
        att.reset();
        att = std::make_unique<APVTS::SliderAttachment> (apvts, paramId, slider);
        slider.bind (apvts.getParameter (paramId), caption);   // after the attachment has set the range
        if (auto* p = apvts.getParameter (paramId))
        {
            const auto& r = p->getNormalisableRange();
            slider.getProperties().set ("kvBipolar", r.start < 0.0f && r.end > 0.0f && std::abs (p->convertFrom0to1 (p->getDefaultValue())) < 1e-6f);
        }
        slider.onValueChange = [this] { refreshValue(); };
        refreshValue();
    }

    void refreshValue()
    {
        value.setText (slider.getTextFromValue (slider.getValue()), juce::dontSendNotification);
    }

    void setLNF (juce::LookAndFeel* l)
    {
        slider.setLookAndFeel (l);
        label.setLookAndFeel (l);
        value.setLookAndFeel (l);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        if (value.isVisible()) value.setBounds (b.removeFromBottom (17));
        if (label.isVisible()) label.setBounds (b.removeFromBottom (15));
        slider.setBounds (b);
    }

    ParamSlider slider;
    juce::Label label, value;

private:
    APVTS& apvts;
    juce::String caption, paramIdStr;
    std::unique_ptr<APVTS::SliderAttachment> att;
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
