#pragma once

#include "PluginProcessor.h"
#include "gui/Panels.h"

class ChannelStripEditor : public juce::AudioProcessorEditor
{
public:
    explicit ChannelStripEditor (ChannelStripProcessor&);
    ~ChannelStripEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ChannelStripProcessor& proc;
    ModuleLNF lnf;

    juce::TooltipWindow tooltips { this, 350 };
    LightningSlider inSlider, outSlider;
    Knob panKnob;
    juce::TextButton phaseBtn { juce::String::fromUTF8 ("\xc3\x98") }, monoBtn { "MONO" };
    std::unique_ptr<APVTS::ButtonAttachment> phaseAtt, monoAtt;
    LevelMeter inMeter, outMeter;

    DeEsserPanel deess;
    EqPanel eqPanel;
    CompressorPanel comp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStripEditor)
};
