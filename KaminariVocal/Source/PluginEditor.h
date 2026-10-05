#pragma once

#include "PluginProcessor.h"
#include "gui/BasicView.h"

// Root editor, 1100 x 760 at 100 % (zoom 75-200 %), laid out after the GUI preview.
//   Header: wordmark, Basic/Advanced, chain presets, A/B, latency, undo/redo, zoom.
//   Basic view: IN/OUT rails, EQ section with analyzer, five module cards with hammer sliders, send row.
//   Advanced view: one tab per module and per send.
class KaminariVocalEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    enum Tab { TabTune, TabEq, TabMultiband, TabCompression, TabDeEss, TabResonance, TabReverb, TabDelay, TabWidener, numTabs };
    static constexpr int baseWidth = 1100, baseHeight = 760;

    explicit KaminariVocalEditor (KaminariVocalProcessor&);
    ~KaminariVocalEditor() override;

    void resized() override;

    void showAdvanced (bool advanced, int send) { showTab (advanced, TabReverb + juce::jlimit (0, 2, send)); }
    void showTab (bool advanced, int tab);
    bool isAdvancedShown() const { return proc.advancedView.load(); }
    int  currentTab() const { return proc.advancedTab.load(); }
    void setZoom (float scale);

    kvui::SendCard& strip (int s) { return *sendCards[(size_t) s]; }
    kvui::ModuleCard& tile (int m) { return *cards[(size_t) m]; }
    kvui::EqSection& eqSection() { return eq; }
    kvui::ReverbPanel& reverbPanel() { return reverb; }
    kvui::DelayPanel& delayPanel() { return delay; }
    kvui::WidenerPanel& widenerPanel() { return widener; }
    kvui::TunePanel& tunePanel() { return tunePanel_; }
    kvui::EqPanel& eqPanel() { return eqPanel_; }
    juce::Component& panel (int tab) { return *panels[(size_t) tab]; }
    juce::TextButton& viewButton (bool advanced) { return advanced ? advancedButton : basicButton; }
    juce::TextButton& tabButton (int t) { return tabs[(size_t) t]; }
    PresetBar& chainPresets() { return chain; }
    juce::Component& rootComponent() { return root; }

private:
    struct Root : juce::Component
    {
        KaminariVocalEditor& owner;
        explicit Root (KaminariVocalEditor& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void resized() override { owner.layoutRoot(); }
        void mouseDown (const juce::MouseEvent&) override {}
    };

    void layoutRoot();
    void paintRoot (juce::Graphics&);
    void updateView();
    void timerCallback() override;

    KaminariVocalProcessor& proc;
    ModuleLNF lnf;
    Root root { *this };
    juce::TooltipWindow tooltips { this, 350 };

    // header
    juce::TextButton basicButton { "BASIC" }, advancedButton { "ADVANCED" };
    PresetBar chain;
    juce::TextButton abA { "A" }, abB { "B" }, abCopy;
    juce::TextButton undo, redo;
    juce::ComboBox zoom;
    juce::String latencyText;

    // rails
    kvui::Rail inRail, outRail;

    // Basic view
    kvui::EqSection eq;
    std::array<std::unique_ptr<kvui::ModuleCard>, 5> cards;
    std::array<std::unique_ptr<kvui::SendCard>, KaminariVocalProcessor::numSends> sendCards;

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
