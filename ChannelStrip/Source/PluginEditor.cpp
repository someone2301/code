#include "PluginEditor.h"

ChannelStripEditor::ChannelStripEditor (ChannelStripProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      lnf (palettes::dark()),
      inKnob (p.apvts, ids::inGain, "INPUT"),
      panKnob (p.apvts, ids::pan, "PAN"),
      outKnob (p.apvts, ids::outGain, "OUTPUT"),
      inMeter (p.inPeak), outMeter (p.outPeak),
      deess (p), eqPanel (p), comp (p)
{
    for (auto* k : { &inKnob, &panKnob, &outKnob })
    {
        k->setLNF (&lnf);
        addAndMakeVisible (*k);
    }
    for (auto* b : { &phaseBtn, &monoBtn })
    {
        b->setLookAndFeel (&lnf);
        addAndMakeVisible (*b);
    }
    phaseAtt = std::make_unique<APVTS::ButtonAttachment> (p.apvts, ids::phase, phaseBtn);
    monoAtt = std::make_unique<APVTS::ButtonAttachment> (p.apvts, ids::mono, monoBtn);

    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
    addAndMakeVisible (deess);
    addAndMakeVisible (eqPanel);
    addAndMakeVisible (comp);

    setSize (1100, 850);
}

ChannelStripEditor::~ChannelStripEditor()
{
    for (auto* b : { &phaseBtn, &monoBtn })
        b->setLookAndFeel (nullptr);
}

void ChannelStripEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff12151a), 0.0f, 0.0f, juce::Colour (0xff0a0c0f), 0.0f, (float) getHeight(), false));
    g.fillAll();

    g.setColour (juce::Colour (0xfff2f4f7));
    g.setFont (uiFont (26.0f, true));
    g.drawText ("CHANNEL STRIP", 20, 12, 320, 34, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff8b95a3));
    g.setFont (uiFont (12.0f));
    g.drawText ("de-ess  >  EQ  >  compressor  >  pan  >  out", 20, 46, 340, 16, juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xff3a4350));
    g.drawHorizontalLine (156, 10.0f, (float) getWidth() - 10.0f);
}

void ChannelStripEditor::resized()
{
    inMeter.setBounds (400, 24, 10, 120);
    inKnob.setBounds (416, 30, 96, 96);
    phaseBtn.setBounds (528, 40, 50, 26);
    monoBtn.setBounds (528, 72, 50, 26);
    panKnob.setBounds (594, 30, 96, 96);
    outKnob.setBounds (706, 30, 96, 96);
    outMeter.setBounds (808, 24, 10, 120);

    deess.setBounds (10, 166, 240, 340);
    eqPanel.setBounds (258, 166, 832, 340);
    comp.setBounds (10, 514, 1080, 326);
}
