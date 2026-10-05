#include "PluginEditor.h"

namespace
{
    const char* tabNames[] = { "TUNE", "EQ", "MULTIBAND", "COMPRESSION", "DE-ESS", "RESONANCE", "REVERB", "DELAY", "WIDENER" };
}

KaminariVocalEditor::KaminariVocalEditor (KaminariVocalProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      chain (p.presets, {}, "chain"),
      inGain (p.apvts, kvid::inGain, "IN", "Input gain before the channel and the sends."),
      outGain (p.apvts, kvid::outGain, "OUT", "Output gain of the dry vocal. Post-fader sends follow it."),
      inMeter (p.inPeak), outMeter (p.outPeak),
      basicEq (p),
      eqPower (p.apvts, "eq_on", "EQ ON", "EQ OFF", "Switches the EQ on or off."),
      tunePanel_ (p), eqPanel_ (p), multibandPanel (p), compressionPanel (p), deEssPanel (p), resonancePanel (p),
      reverb (p), delay (p), widener (p)
{
    setLookAndFeel (&lnf);
    setTitle ("Kaminari Vocal");

    // Basic view: pulling a hammer down always means more effect (thresholds and Retune Speed are inverted)
    tiles[0] = std::make_unique<kvui::ModuleTile> (p, "Tune", "tn_on", "tn_speed", true, "Retune Speed", KaminariVocalProcessor::ModTune);
    tiles[1] = std::make_unique<kvui::ModuleTile> (p, "Multiband", "mb_on", "mb1_thresh", true, "Threshold", KaminariVocalProcessor::ModMultiband);
    tiles[2] = std::make_unique<kvui::ModuleTile> (p, "Compression", "lv_on", "lv_thresh", true, "Compression", KaminariVocalProcessor::ModCompression);
    tiles[3] = std::make_unique<kvui::ModuleTile> (p, "De-ess", "ds_on", "ds_thresh", true, "De-ess", KaminariVocalProcessor::ModDeEss);
    tiles[4] = std::make_unique<kvui::ModuleTile> (p, "Resonance", "rs_on", "rs_depth", false, "Depth", KaminariVocalProcessor::ModResonance);
    const int tileTab[5] = { TabTune, TabMultiband, TabCompression, TabDeEss, TabResonance };
    for (int i = 0; i < 5; ++i)
    {
        addChildComponent (*tiles[(size_t) i]);
        tiles[(size_t) i]->advanced.onClick = [this, t = tileTab[i]] { showTab (true, t); };
    }
    addChildComponent (basicEq);
    addChildComponent (eqPower);
    basicEq.onSelect = [this] (int band) { eqPanel_.selector.select (band); };

    strips[0] = std::make_unique<kvui::SendStrip> (p, KaminariVocalProcessor::Reverb, kvid::rvOn, kvid::rvSend, kvid::rvMode, "Reverb",
                                                   [] (int m) { return juce::String (kv::reverbMode (m).name); });
    strips[1] = std::make_unique<kvui::SendStrip> (p, KaminariVocalProcessor::Delay, kvid::dlOn, kvid::dlSend, kvid::dlStyle, "Delay",
                                                   [] (int s) { return juce::String (kv::delayStyleName (s)); });
    strips[2] = std::make_unique<kvui::SendStrip> (p, KaminariVocalProcessor::Widener, kvid::wdOn, kvid::wdSend, kvid::wdType, "Widener",
                                                   [] (int t) { return juce::String (t == 0 ? "MicroShift" : "SideWidener"); });
    for (int s = 0; s < KaminariVocalProcessor::numSends; ++s)
    {
        addChildComponent (*strips[(size_t) s]);
        strips[(size_t) s]->advanced.onClick = [this, s] { showAdvanced (true, s); };
    }

    panels = { &tunePanel_, &eqPanel_, &multibandPanel, &compressionPanel, &deEssPanel, &resonancePanel, &reverb, &delay, &widener };
    for (int t = 0; t < numTabs; ++t)
    {
        auto& b = tabs[(size_t) t];
        b.setButtonText (tabNames[t]);
        b.setRadioGroupId (2);
        b.setClickingTogglesState (true);
        b.setTooltip ("Show " + juce::String (tabNames[t]).toLowerCase() + (t >= TabReverb ? " send" : "") + " controls.");
        b.onClick = [this, t] { if (tabs[(size_t) t].getToggleState()) showTab (true, t); };
        addChildComponent (b);
        addChildComponent (*panels[(size_t) t]);
    }

    for (auto* b : { &basicButton, &advancedButton })
    {
        b->setRadioGroupId (1);
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
    }
    basicButton.setTooltip ("Compact view: one control per module, the EQ graph and the sends.");
    advancedButton.setTooltip ("All controls, one tab per module and send.");
    basicButton.onClick = [this] { if (basicButton.getToggleState()) showTab (false, currentTab()); };
    advancedButton.onClick = [this] { if (advancedButton.getToggleState()) showTab (true, currentTab()); };

    kvui::styleText (latencyLabel, 12.0f, kvui::colours::mist);
    latencyLabel.setJustificationType (juce::Justification::centredRight);
    latencyLabel.setTooltip ("Latency reported to the host: Tune's fixed delay plus any lookahead.");
    for (auto* c : std::initializer_list<juce::Component*> { &inGain, &outGain, &inMeter, &outMeter, &chain, &latencyLabel })
        addAndMakeVisible (c);
    chain.name.setTooltip ("Chain presets set every module and send at once.");
    chain.onLoaded = [this]
    {
        chain.refresh();
        for (auto* bar : { &reverb.header.preset, &delay.header.preset, &widener.header.preset, &tunePanel_.preset, &eqPanel_.preset,
                           &multibandPanel.preset, &compressionPanel.preset, &deEssPanel.preset, &resonancePanel.preset })
            bar->refresh();
    };

    setSize (1100, 760);
    updateView();
    timerCallback();
    startTimerHz (4);
}

KaminariVocalEditor::~KaminariVocalEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void KaminariVocalEditor::timerCallback()
{
    const int smp = proc.getLatencySamples();
    const double ms = 1000.0 * smp / juce::jmax (1.0, proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0);
    latencyLabel.setText (juce::String (smp) + " smp · " + juce::String (ms, 1) + " ms", juce::dontSendNotification);
}

void KaminariVocalEditor::showTab (bool advanced, int tab)
{
    tab = juce::jlimit (0, (int) numTabs - 1, tab);
    proc.advancedView.store (advanced);
    proc.advancedTab.store (tab);
    if (tab >= TabReverb)
        proc.advancedSend.store (tab - TabReverb);
    updateView();
}

void KaminariVocalEditor::updateView()
{
    const bool adv = proc.advancedView.load();
    const int tab = proc.advancedTab.load();
    basicButton.setToggleState (! adv, juce::dontSendNotification);
    advancedButton.setToggleState (adv, juce::dontSendNotification);
    for (auto& t : tiles) t->setVisible (! adv);
    for (auto& s : strips) s->setVisible (! adv);
    basicEq.setVisible (! adv);
    eqPower.setVisible (! adv);
    for (int t = 0; t < numTabs; ++t)
    {
        tabs[(size_t) t].setVisible (adv);
        tabs[(size_t) t].setToggleState (adv && t == tab, juce::dontSendNotification);
        panels[(size_t) t]->setVisible (adv && t == tab);
    }
    resized();
    repaint();
}

void KaminariVocalEditor::paint (juce::Graphics& g)
{
    using namespace kvui::colours;
    g.fillAll (navy950);
    auto header = getLocalBounds().removeFromTop (52);
    g.setColour (navy900);
    g.fillRect (header);

    juce::Path glyph;
    glyph.startNewSubPath (24.0f, 14.0f);
    glyph.lineTo (16.0f, 28.0f);
    glyph.lineTo (22.0f, 28.0f);
    glyph.lineTo (18.0f, 40.0f);
    glyph.lineTo (28.0f, 24.0f);
    glyph.lineTo (22.0f, 24.0f);
    glyph.closeSubPath();
    g.setColour (bolt);
    g.fillPath (glyph);
    g.setColour (white);
    g.setFont (uiFont (21.0f, true));
    g.drawText ("KAMINARI VOCAL", 36, 0, 220, 52, juce::Justification::centredLeft);

    if (! proc.advancedView.load())
    {
        g.setColour (mist);
        g.setFont (uiFont (11.0f, true));
        g.drawText ("SENDS", 104, getHeight() - 186, 100, 14, juce::Justification::centredLeft);
    }
}

void KaminariVocalEditor::resized()
{
    auto b = getLocalBounds();
    auto header = b.removeFromTop (52);
    header.removeFromLeft (250);
    auto views = header.removeFromLeft (180).withSizeKeepingCentre (180, 28);
    basicButton.setBounds (views.removeFromLeft (90));
    advancedButton.setBounds (views);
    header.removeFromLeft (20);
    chain.setBounds (header.removeFromLeft (380).withSizeKeepingCentre (380, 28));
    latencyLabel.setBounds (header.reduced (16, 0));

    auto left = b.removeFromLeft (96).reduced (8);
    auto right = b.removeFromRight (96).reduced (8);
    inGain.setBounds (left.removeFromBottom (100));
    inMeter.setBounds (left.withSizeKeepingCentre (14, left.getHeight() - 16));
    outGain.setBounds (right.removeFromBottom (100));
    outMeter.setBounds (right.withSizeKeepingCentre (14, right.getHeight() - 16));

    auto content = b.reduced (8, 10);
    if (! proc.advancedView.load())
    {
        auto eqArea = content.removeFromTop (220);
        eqPower.setBounds (eqArea.removeFromTop (26).removeFromLeft (110));
        eqArea.removeFromTop (4);
        basicEq.setBounds (eqArea);
        content.removeFromTop (10);
        auto row = content.removeFromTop (270);
        const int w = (row.getWidth() - 4 * 10) / 5;
        for (auto& t : tiles) { t->setBounds (row.removeFromLeft (w)); row.removeFromLeft (10); }
        content.removeFromTop (24);
        auto sends = content;
        const int sw = (sends.getWidth() - 2 * 10) / 3;
        for (auto& s : strips) { s->setBounds (sends.removeFromLeft (sw)); sends.removeFromLeft (10); }
    }
    else
    {
        auto tabRow = content.removeFromTop (28);
        const int tw = (tabRow.getWidth() - 8 * 4) / numTabs;
        for (auto& t : tabs) { t.setBounds (tabRow.removeFromLeft (tw)); tabRow.removeFromLeft (4); }
        content.removeFromTop (6);
        for (auto* panel : panels) panel->setBounds (content);
    }
}
