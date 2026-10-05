#pragma once

#include "PluginProcessor.h"
#include "gui/SendPanels.h"

// Root editor. Basic view: compact send strips. Advanced view: one detailed panel per send, chosen with tabs.
// The channel modules (Tune, EQ, Multiband, Compression, De-ess, Resonance) will join both views when they are built.
class KaminariVocalEditor : public juce::AudioProcessorEditor
{
public:
    explicit KaminariVocalEditor (KaminariVocalProcessor&);
    ~KaminariVocalEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void showAdvanced (bool advanced, int send);
    bool isAdvancedShown() const { return proc.advancedView.load(); }
    int  currentSend() const { return proc.advancedSend.load(); }

    kvui::SendStrip& strip (int s) { return *strips[(size_t) s]; }
    kvui::ReverbPanel& reverbPanel() { return reverb; }
    kvui::DelayPanel& delayPanel() { return delay; }
    kvui::WidenerPanel& widenerPanel() { return widener; }
    juce::TextButton& viewButton (bool advanced) { return advanced ? advancedButton : basicButton; }
    juce::TextButton& sendTab (int s) { return tabs[(size_t) s]; }
    PresetBar& chainPresets() { return chain; }

private:
    void updateView();

    KaminariVocalProcessor& proc;
    ModuleLNF lnf { kvui::palette() };
    juce::TooltipWindow tooltips { this, 350 };

    juce::TextButton basicButton { "Basic" }, advancedButton { "Advanced" };
    PresetBar chain;
    Knob inGain, outGain;
    LevelMeter inMeter, outMeter;

    std::array<std::unique_ptr<kvui::SendStrip>, KaminariVocalProcessor::numSends> strips;
    std::array<juce::TextButton, KaminariVocalProcessor::numSends> tabs;
    kvui::ReverbPanel reverb;
    kvui::DelayPanel delay;
    kvui::WidenerPanel widener;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KaminariVocalEditor)
};
