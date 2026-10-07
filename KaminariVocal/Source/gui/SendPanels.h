#pragma once

#include "../PluginProcessor.h"
#include "Widgets.h"
#include "PresetBar.h"

namespace kvui
{
    namespace colours
    {
        inline const juce::Colour navy950 { 0xff070f1f }, navy900 { 0xff0b1a33 }, navy800 { 0xff12264a },
                                  navy600 { 0xff2b4a82 }, white { 0xfff4f7fc }, mist { 0xffa9b8d6 },
                                  bolt { 0xff5ce1ff }, amber { 0xffffb547 };
    }

    inline Palette palette()
    {
        using namespace colours;
        return { navy800, navy600, bolt, navy600, white, bolt.withAlpha (0.85f), navy900, white, navy950 };
    }

    // Lays out the visible components left to right in rows of fixed-size cells.
    inline void flow (juce::Rectangle<int> area, const std::vector<juce::Component*>& items, int w, int h, int gap = 8)
    {
        int x = area.getX(), y = area.getY();
        for (auto* c : items)
        {
            if (c == nullptr || ! c->isVisible())
                continue;
            if (x + w > area.getRight() && x > area.getX())
            {
                x = area.getX();
                y += h + gap;
            }
            c->setBounds (x, y, w, h);
            x += w + gap;
        }
    }

    // Caption + combo box bound to a choice parameter.
    // Combo box that can tell a choice the user picked from one made by automation, presets or the host.
    class UserComboBox : public juce::ComboBox
    {
    public:
        void showPopup() override { userPicking = true; juce::ComboBox::showPopup(); }
        bool userPicking = false;
    };

    class ChoiceBox : public juce::Component
    {
    public:
        ChoiceBox (APVTS& state, const char* paramId, const juce::String& captionText, const juce::String& tip,
                   std::function<void (juce::ComboBox&)> fill = nullptr)
        {
            auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId));
            jassert (p != nullptr);
            if (fill != nullptr)
                fill (box);
            else
                box.addItemList (p->choices, 1);
            box.setTitle (p->getName (64));
            box.setDescription (tip);
            box.setTooltip (p->getName (64) + "\n" + tip);
            caption.setText (captionText, juce::dontSendNotification);
            caption.setJustificationType (juce::Justification::centredLeft);
            caption.setFont (uiFont (11.0f, true));
            caption.setColour (juce::Label::textColourId, colours::mist);
            caption.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (caption);
            addAndMakeVisible (box);
            att = std::make_unique<APVTS::ComboBoxAttachment> (state, paramId, box);
            box.onChange = [this]
            {
                if (! box.userPicking) return;
                box.userPicking = false;
                if (onUserChange) onUserChange (box.getSelectedItemIndex());
            };
        }

        void resized() override
        {
            auto b = getLocalBounds();
            caption.setBounds (b.removeFromTop (16));
            box.setBounds (b.removeFromTop (26));
        }

        // Called only when the user picks an item from the list (after the parameter has changed).
        std::function<void (int)> onUserChange;
        UserComboBox box;
        juce::Label caption;

    private:
        std::unique_ptr<APVTS::ComboBoxAttachment> att;
    };

    // Toggle button bound to a bool or two-choice parameter.
    class ToggleBox : public juce::TextButton
    {
    public:
        ToggleBox (APVTS& state, const char* paramId, const juce::String& onText, const juce::String& offText, const juce::String& tip)
            : on (onText), off (offText)
        {
            setClickingTogglesState (true);
            auto* p = state.getParameter (paramId);
            setTitle (p->getName (64));
            setDescription (tip);
            setTooltip (p->getName (64) + "\n" + tip + "\nClick or press Space to toggle.");
            setWantsKeyboardFocus (true);
            onStateChange = [this] { setButtonText (getToggleState() ? on : off); };
            att = std::make_unique<APVTS::ButtonAttachment> (state, paramId, *this);
            setButtonText (getToggleState() ? on : off);
        }

    private:
        juce::String on, off;
        std::unique_ptr<APVTS::ButtonAttachment> att;
    };

    // Horizontal return meter (peak, -60 .. 0 dBFS) with a caption.
    class ReturnMeter : public juce::Component, private juce::Timer
    {
    public:
        explicit ReturnMeter (std::atomic<float>& source) : src (source)
        {
            setTitle ("Return level");
            startTimerHz (30);
        }
        ~ReturnMeter() override { stopTimer(); }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (colours::navy950);
            g.fillRoundedRectangle (b, 3.0f);
            g.setColour (colours::navy600);
            g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);
            const float db = juce::Decibels::gainToDecibels (level, -60.0f);
            const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            auto bar = b.reduced (2.0f);
            g.setColour (db > -1.0f ? colours::amber : colours::bolt);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * frac), 2.0f);
        }

    private:
        void timerCallback() override
        {
            const float target = src.load();
            const float next = target > level ? target : level * 0.85f;
            if (std::abs (next - level) > 1.0e-5f) { level = next; repaint(); }
        }

        std::atomic<float>& src;
        float level = 0.0f;
    };

    // Current plain value of a parameter. Reads the parameter object, not the APVTS atomic: attachment callbacks
    // run before APVTS has updated its atomic.
    inline int choiceIndex (APVTS& state, const char* id)
    {
        auto* p = state.getParameter (id);
        return juce::roundToInt (p->convertFrom0to1 (p->getValue()));
    }

    inline void styleText (juce::Label& l, float size, juce::Colour c, bool bold = false)
    {
        l.setFont (uiFont (size, bold));
        l.setColour (juce::Label::textColourId, c);
        l.setJustificationType (juce::Justification::topLeft);
        l.setMinimumHorizontalScale (1.0f);   // wrap onto more lines instead of squeezing the text
        l.setInterceptsMouseClicks (false, false);
    }
}
