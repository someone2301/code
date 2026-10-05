#pragma once

#include "ModulePanels.h"
#include "Hammer.h"

namespace kvui
{
    // Basic-view module: On switch, one hammer slider (pull down = more effect), a read-out and an Advanced button.
    class ModuleTile : public juce::Component, private juce::Timer
    {
    public:
        ModuleTile (KaminariVocalProcessor& p, const juce::String& name, const char* onId, const char* hammerId, bool inverted,
                    const juce::String& hammerCaption, int moduleIndex)
            : power (p.apvts, onId, "ON", "OFF", "Switches " + name + " on or off."),
              hammer (*p.apvts.getParameter (hammerId), p.hostTempo, hammerCaption, inverted),
              proc (p), title (name), module (moduleIndex)
        {
            setTitle (name);
            hammer.setTitle (name + " " + hammerCaption);
            advanced.setButtonText ("Advanced");
            advanced.setTooltip ("Open " + name + " in the Advanced view.");
            advanced.setTitle ("Open " + name + " advanced controls");
            styleText (readout, 11.0f, colours::mist);
            readout.setJustificationType (juce::Justification::centred);
            for (auto* c : std::initializer_list<juce::Component*> { &power, &hammer, &readout, &advanced })
                addAndMakeVisible (c);
            startTimerHz (15);
        }
        ~ModuleTile() override { stopTimer(); }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (colours::navy900);
            g.fillRoundedRectangle (b, 6.0f);
            g.setColour (colours::navy600);
            g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
            g.setColour (colours::white);
            g.setFont (uiFont (14.0f, true));
            g.drawText (title.toUpperCase(), getLocalBounds().reduced (10, 8).removeFromTop (22), juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (10, 8);
            power.setBounds (b.removeFromTop (22).removeFromRight (50));
            advanced.setBounds (b.removeFromBottom (24));
            b.removeFromBottom (4);
            readout.setBounds (b.removeFromBottom (18));
            hammer.setBounds (b.reduced (4, 2));
        }

        ToggleBox power;
        LightningSlider hammer;
        juce::Label readout;
        juce::TextButton advanced;

    private:
        void timerCallback() override
        {
            juce::String t;
            if (module == KaminariVocalProcessor::ModTune)
            {
                static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                const float midi = proc.tune.detectedMidi.load();
                t = midi < 0 ? juce::String ("no pitch") : juce::String (names[((juce::roundToInt (midi) % 12) + 12) % 12])
                        + juce::String (juce::roundToInt (midi) / 12 - 1) + "  " + juce::String (juce::roundToInt (proc.tune.correctionCents.load())) + " ct";
            }
            else
            {
                const float gr = proc.moduleGr[(size_t) module].load();
                t = "GR " + juce::String (gr > 0.05f ? "-" : (gr < -0.05f ? "+" : "")) + juce::String (std::abs (gr), 1) + " dB";
            }
            if (readout.getText() != t) readout.setText (t, juce::dontSendNotification);
        }

        KaminariVocalProcessor& proc;
        juce::String title;
        int module;
    };
}
