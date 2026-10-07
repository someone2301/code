#pragma once

#include "PluginProcessor.h"
#include "gui/AdvPages.h"
#include "gui/SendPages.h"

// Root editor, 1100 x 760 at 100 % (zoom 75-200 %), laid out after the GUI preview.
//   Header: wordmark, Basic/Advanced, chain presets, A/B, latency, undo/redo, zoom.
//   Basic view: IN/OUT rails, EQ section with analyzer, seven module cards with hammer sliders, send row.
//   Advanced view: one tab per module and per send.
class KaminariVocalEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    enum Tab { TabTune, TabEq, TabMultiband, TabCompression, TabFlanger, TabDistortion, TabDeEss, TabResonance, TabSends, numTabs };
    static constexpr int baseWidth = 1100, baseHeight = 760;

    explicit KaminariVocalEditor (KaminariVocalProcessor&);
    ~KaminariVocalEditor() override;

    void resized() override;

    void showAdvanced (bool advanced, int send) { sendsPage.show (send); proc.advancedSend.store (juce::jlimit (0, (int) KaminariVocalProcessor::numSends - 1, send)); showTab (advanced, TabSends); }
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
    kvui::FlangerPage& flangerPanel() { return flangerPage; }
    kvui::DistortionPage& distortionPanel() { return distortionPage; }
    kvui::CompressionPage& compressionPanel() { return compressionPage; }
    kvui::SendsPage& sendsView() { return sendsPage; }
    kvui::MultibandPage& multibandPanel() { return multibandPage; }
    kvui::ResonancePage& resonancePanel() { return resonancePage; }
    kvui::TunePage& tunePage() { return tunePage_; }
    kvui::EqPage& eqPage() { return eqPage_; }
    juce::Component& panel (int tab) { return *panels[(size_t) tab]; }
    juce::TextButton& viewButton (bool advanced) { return advanced ? advancedButton : basicButton; }
    juce::TextButton& tabButton (int t) { return *tabs[(size_t) t]; }
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
    juce::TextButton undo, redo, animations { "ANIM" };
    juce::ComboBox zoom;
    juce::String latencyText;

    // rails
    kvui::Rail inRail, outRail;

    // Basic view
    kvui::EqSection eq;
    std::array<std::unique_ptr<kvui::ModuleCard>, 7> cards;
    std::array<std::unique_ptr<kvui::SendCard>, KaminariVocalProcessor::numSends> sendCards;

    // Advanced view
    // Module tab. Its lightning icon is the module's on/off switch (bright = on, dim = bypassed); clicking anywhere
    // else on the tab shows the module's page.
    struct TabButton : juce::TextButton
    {
        std::function<juce::String()> info;
        bool send = false;
        bool showInfo = true;   // off when the tab row is too narrow for it
        void paintButton (juce::Graphics&, bool, bool) override;

        void setPowerParameter (juce::RangedAudioParameter& p, juce::UndoManager* um);
        bool isPowerOn() const { return power != nullptr && power->getValue() > 0.5f; }
        juce::Rectangle<int> boltArea() const { return { 0, 0, 30, getHeight() }; }
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        juce::String getTooltip() override;

    private:
        juce::RangedAudioParameter* power = nullptr;
        std::unique_ptr<juce::ParameterAttachment> powerAtt;
        bool boltHover = false, boltDown = false;
    };
    std::array<std::unique_ptr<TabButton>, numTabs> tabs;
    kvui::TunePage tunePage_;
    kvui::EqPage eqPage_;
    kvui::MultibandPage multibandPage;
    kvui::CompressionPage compressionPage;
    kvui::FlangerPage flangerPage;
    kvui::DistortionPage distortionPage;
    kvui::DeEssPage deEssPage;
    kvui::ResonancePage resonancePage;
    kvui::ReverbPanel reverb;
    kvui::DelayPanel delay;
    kvui::WidenerPanel widener;
    kvui::SendsPage sendsPage { proc, reverb, delay, widener };
    std::array<juce::Component*, numTabs> panels {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KaminariVocalEditor)
};
