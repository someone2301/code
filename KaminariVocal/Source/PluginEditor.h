#pragma once

#include "PluginProcessor.h"
#include "gui/ModuleTile.h"

// Root editor (1100 x 760).
//   Basic view:    EQ graph, five module tiles with hammer sliders, three compact send strips.
//   Advanced view: one tab per module and per send, each with its own preset bar.
// The header holds Basic/Advanced, the chain preset bar and the latency read-out.
class KaminariVocalEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    enum Tab { TabTune, TabEq, TabMultiband, TabCompression, TabDeEss, TabResonance, TabReverb, TabDelay, TabWidener, numTabs };

    explicit KaminariVocalEditor (KaminariVocalProcessor&);
    ~KaminariVocalEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void showAdvanced (bool advanced, int send) { showTab (advanced, TabReverb + juce::jlimit (0, 2, send)); }
    void showTab (bool advanced, int tab);
    bool isAdvancedShown() const { return proc.advancedView.load(); }
    int  currentTab() const { return proc.advancedTab.load(); }

    kvui::SendStrip& strip (int s) { return *strips[(size_t) s]; }
    kvui::ModuleTile& tile (int m) { return *tiles[(size_t) m]; }
    kvui::ReverbPanel& reverbPanel() { return reverb; }
    kvui::DelayPanel& delayPanel() { return delay; }
    kvui::WidenerPanel& widenerPanel() { return widener; }
    kvui::TunePanel& tunePanel() { return tunePanel_; }
    kvui::EqPanel& eqPanel() { return eqPanel_; }
    juce::Component& panel (int tab) { return *panels[(size_t) tab]; }
    juce::TextButton& viewButton (bool advanced) { return advanced ? advancedButton : basicButton; }
    juce::TextButton& tabButton (int t) { return tabs[(size_t) t]; }
    PresetBar& chainPresets() { return chain; }

private:
    void updateView();
    void timerCallback() override;

    KaminariVocalProcessor& proc;
    ModuleLNF lnf { kvui::palette() };
    juce::TooltipWindow tooltips { this, 350 };

    juce::TextButton basicButton { "Basic" }, advancedButton { "Advanced" };
    PresetBar chain;
    juce::Label latencyLabel;
    Knob inGain, outGain;
    LevelMeter inMeter, outMeter;

    // Basic view
    EqCurve basicEq;
    kvui::ToggleBox eqPower;
    std::array<std::unique_ptr<kvui::ModuleTile>, 5> tiles;
    std::array<std::unique_ptr<kvui::SendStrip>, KaminariVocalProcessor::numSends> strips;

    // Advanced view
    std::array<juce::TextButton, numTabs> tabs;
    kvui::TunePanel tunePanel_;
    kvui::EqPanel eqPanel_;
    kvui::MultibandPanel multibandPanel;
    kvui::CompressionPanel compressionPanel;
    kvui::DeEssPanel deEssPanel;
    kvui::ResonancePanel resonancePanel;
    kvui::ReverbPanel reverb;
    kvui::DelayPanel delay;
    kvui::WidenerPanel widener;
    std::array<juce::Component*, numTabs> panels {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KaminariVocalEditor)
};
