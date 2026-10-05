#include "PluginEditor.h"

namespace
{
    const char* tabNames[] = { "TUNE", "EQ", "MULTIBAND", "COMPRESSION", "DE-ESS", "RESONANCE", "REVERB", "DELAY", "WIDENER" };
}

KaminariVocalEditor::KaminariVocalEditor (KaminariVocalProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      chain (p.presets, {}, "chain"),
      inRail (p.apvts, kvid::inGain, "IN", p.inPeakCh[0], p.inPeakCh[1], lnf),
      outRail (p.apvts, kvid::outGain, "OUT", p.outPeakCh[0], p.outPeakCh[1], lnf),
      eq (p),
      tunePanel_ (p), eqPanel_ (p), multibandPanel (p), compressionPanel (p), deEssPanel (p), resonancePanel (p),
      reverb (p), delay (p), widener (p)
{
    setLookAndFeel (&lnf);
    setTitle ("Kaminari Vocal");
    addAndMakeVisible (root);

    // Basic view: pulling a hammer down always means more effect (thresholds and Retune Speed are inverted)
    cards[0] = std::make_unique<kvui::ModuleCard> (p, "Tune", "tn_on", "tn_speed", true, "Retune Speed", KaminariVocalProcessor::ModTune);
    cards[1] = std::make_unique<kvui::ModuleCard> (p, "Multiband", "mb_on", "mb1_thresh", true, "Threshold", KaminariVocalProcessor::ModMultiband);
    cards[2] = std::make_unique<kvui::ModuleCard> (p, "Compression", "lv_on", "lv_thresh", true, "Compression", KaminariVocalProcessor::ModCompression);
    cards[3] = std::make_unique<kvui::ModuleCard> (p, "De-ess", "ds_on", "ds_thresh", true, "De-ess", KaminariVocalProcessor::ModDeEss);
    cards[4] = std::make_unique<kvui::ModuleCard> (p, "Resonance", "rs_on", "rs_depth", false, "Depth", KaminariVocalProcessor::ModResonance);
    const int cardTab[5] = { TabTune, TabMultiband, TabCompression, TabDeEss, TabResonance };
    for (int i = 0; i < 5; ++i)
    {
        root.addChildComponent (*cards[(size_t) i]);
        cards[(size_t) i]->open.onClick = [this, t = cardTab[i]] { showTab (true, t); };
    }
    root.addChildComponent (eq);
    eq.onSelect = [this] (int band) { eqPanel_.selector.select (band); };

    sendCards[0] = std::make_unique<kvui::SendCard> (p, KaminariVocalProcessor::Reverb, kvid::rvOn, kvid::rvSend, kvid::rvMode, kvid::rvTap, "Reverb",
                                                     [] (int m) { return juce::String (kv::reverbMode (m).name); }, lnf);
    sendCards[1] = std::make_unique<kvui::SendCard> (p, KaminariVocalProcessor::Delay, kvid::dlOn, kvid::dlSend, kvid::dlStyle, kvid::dlTap, "Delay",
                                                     [] (int s) { return juce::String (kv::delayStyleName (s)); }, lnf);
    sendCards[2] = std::make_unique<kvui::SendCard> (p, KaminariVocalProcessor::Widener, kvid::wdOn, kvid::wdSend, kvid::wdType, kvid::wdTap, "Widener",
                                                     [] (int t) { return juce::String (t == 0 ? "MicroShift" : "SideWidener"); }, lnf);
    for (int s = 0; s < KaminariVocalProcessor::numSends; ++s)
    {
        root.addChildComponent (*sendCards[(size_t) s]);
        sendCards[(size_t) s]->open.onClick = [this, s] { showAdvanced (true, s); };
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
        root.addChildComponent (b);
        root.addChildComponent (*panels[(size_t) t]);
    }

    for (auto* b : { &basicButton, &advancedButton })
    {
        b->setRadioGroupId (1);
        b->setClickingTogglesState (true);
        b->getProperties().set ("kvStyle", "fill");
        root.addAndMakeVisible (*b);
    }
    basicButton.setTooltip ("Compact view: one control per module, the EQ graph and the sends.");
    advancedButton.setTooltip ("All controls, one tab per module and send.");
    basicButton.onClick = [this] { if (basicButton.getToggleState()) showTab (false, currentTab()); };
    advancedButton.onClick = [this] { if (advancedButton.getToggleState()) showTab (true, currentTab()); };

    // A/B
    abCopy.setButtonText (juce::String (juce::CharPointer_UTF8 ("A\xe2\x80\xba" "B")));
    for (auto* b : { &abA, &abB })
    {
        b->setRadioGroupId (3);
        b->setClickingTogglesState (true);
        b->getProperties().set ("kvStyle", "light");
    }
    abA.setToggleState (proc.activeAB() == 0, juce::dontSendNotification);
    abB.setToggleState (proc.activeAB() == 1, juce::dontSendNotification);
    abA.setTooltip ("Settings A. Click to switch.");
    abB.setTooltip ("Settings B. Click to switch.");
    abCopy.setTooltip ("Copy the active settings to the other slot.");
    abA.onClick = [this] { if (abA.getToggleState()) proc.selectAB (0); };
    abB.onClick = [this] { if (abB.getToggleState()) proc.selectAB (1); };
    abCopy.onClick = [this] { proc.copyAToB(); };

    undo.getProperties().set ("kvStyle", "undo");
    redo.getProperties().set ("kvStyle", "redo");
    undo.setTooltip ("Undo");
    redo.setTooltip ("Redo");
    undo.setTitle ("Undo");
    redo.setTitle ("Redo");
    undo.onClick = [this] { proc.undoManager.undo(); };
    redo.onClick = [this] { proc.undoManager.redo(); };

    for (int z : { 75, 100, 125, 150, 200 }) zoom.addItem (juce::String (z) + "%", z);
    zoom.setSelectedId (juce::roundToInt (proc.uiScale.load() * 100.0f), juce::dontSendNotification);
    zoom.setTooltip ("Window size");
    zoom.setTitle ("Window size");
    zoom.onChange = [this] { setZoom ((float) zoom.getSelectedId() / 100.0f); };

    for (auto* c : std::initializer_list<juce::Component*> { &chain, &abA, &abB, &abCopy, &undo, &redo, &zoom, &inRail, &outRail })
        root.addAndMakeVisible (c);
    chain.name.setTooltip ("Chain presets set every module and send at once.");
    chain.onLoaded = [this]
    {
        chain.refresh();
        for (auto* bar : { &reverb.header.preset, &delay.header.preset, &widener.header.preset, &tunePanel_.preset, &eqPanel_.preset,
                           &multibandPanel.preset, &compressionPanel.preset, &deEssPanel.preset, &resonancePanel.preset })
            bar->refresh();
    };

    // one undo step per mouse gesture anywhere in the window
    root.addMouseListener (&root, true);

    updateView();
    setZoom (proc.uiScale.load());
    timerCallback();
    startTimerHz (4);
}

KaminariVocalEditor::~KaminariVocalEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void KaminariVocalEditor::setZoom (float scale)
{
    scale = juce::jlimit (0.75f, 2.0f, scale);
    proc.uiScale.store (scale);
    root.setTransform (juce::AffineTransform::scale (scale));
    setSize (juce::roundToInt (baseWidth * scale), juce::roundToInt (baseHeight * scale));
}

void KaminariVocalEditor::resized()
{
    root.setBounds (0, 0, baseWidth, baseHeight);
}

void KaminariVocalEditor::timerCallback()
{
    const int smp = proc.getLatencySamples();
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    const auto t = juce::String (smp) + " smp" + kvui::dot() + juce::String (1000.0 * smp / sr, 1) + " ms";
    if (t != latencyText) { latencyText = t; root.repaint (0, 0, baseWidth, 56); }
    undo.setEnabled (proc.undoManager.canUndo());
    redo.setEnabled (proc.undoManager.canRedo());
    abA.setToggleState (proc.activeAB() == 0, juce::dontSendNotification);
    abB.setToggleState (proc.activeAB() == 1, juce::dontSendNotification);
    proc.undoManager.beginNewTransaction();   // groups changes into steps of at most 250 ms
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
    for (auto& c : cards) c->setVisible (! adv);
    for (auto& s : sendCards) s->setVisible (! adv);
    eq.setVisible (! adv);
    for (int t = 0; t < numTabs; ++t)
    {
        tabs[(size_t) t].setVisible (adv);
        tabs[(size_t) t].setToggleState (adv && t == tab, juce::dontSendNotification);
        panels[(size_t) t]->setVisible (adv && t == tab);
    }
    layoutRoot();
    root.repaint();
}

void KaminariVocalEditor::Root::paint (juce::Graphics& g) { owner.paintRoot (g); }

void KaminariVocalEditor::paintRoot (juce::Graphics& g)
{
    using namespace kvtheme;
    g.fillAll (navy950);
    auto header = juce::Rectangle<int> (0, 0, baseWidth, 56);
    g.setColour (navy900);
    g.fillRect (header);
    g.setColour (navy600);
    g.drawHorizontalLine (55, 0.0f, (float) baseWidth);

    g.setColour (accent);
    g.fillPath (boltPath ({ 14.0f, 13.0f, 22.0f, 28.0f }));
    g.setColour (white);
    g.strokePath (boltPath ({ 14.0f, 13.0f, 22.0f, 28.0f }), juce::PathStrokeType (0.8f));
    g.setFont (font (23.0f, 3, 0.12f));
    g.drawText ("KAMINARI VOCAL", 44, 9, 190, 26, juce::Justification::centredLeft);
    g.setColour (mist);
    g.setFont (font (10.0f, 1, 0.25f));
    g.drawText ("KAMINARI AUDIO", 45, 33, 190, 12, juce::Justification::centredLeft);

    // latency pill, right of the A/B group
    auto pill = juce::Rectangle<float> ((float) abCopy.getRight() + 14.0f, 13.0f, 124.0f, 30.0f);
    g.setColour (navy900);
    g.fillRoundedRectangle (pill, 15.0f);
    g.setColour (navy600);
    g.drawRoundedRectangle (pill, 15.0f, 1.0f);
    g.setColour (accent);
    g.fillPath (boltPath (pill.withWidth (30.0f).reduced (10.0f, 8.0f)));
    g.setColour (white);
    g.setFont (font (12.0f, 1));
    g.drawText (latencyText, pill.withTrimmedLeft (24.0f), juce::Justification::centredLeft);

    if (! proc.advancedView.load())
    {
        auto lbl = juce::Rectangle<int> (90, baseHeight - 76, 18, 64);
        g.setColour (mist);
        g.setFont (font (11.0f, 3, 0.2f));
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, (float) lbl.getCentreX(), (float) lbl.getCentreY()));
        g.drawText ("SENDS", lbl.withSizeKeepingCentre (64, 18), juce::Justification::centred);
    }
}

void KaminariVocalEditor::layoutRoot()
{
    auto b = juce::Rectangle<int> (0, 0, baseWidth, baseHeight);
    auto header = b.removeFromTop (56).reduced (14, 13);
    header.removeFromLeft (222);
    auto views = header.removeFromLeft (180);
    basicButton.setBounds (views.removeFromLeft (80));
    advancedButton.setBounds (views);
    header.removeFromLeft (16);
    chain.setBounds (header.removeFromLeft (290));
    header.removeFromLeft (12);
    auto ab = header.removeFromLeft (84);
    abA.setBounds (ab.removeFromLeft (26));
    abB.setBounds (ab.removeFromLeft (26));
    abCopy.setBounds (ab);
    zoom.setBounds (header.removeFromRight (62));
    header.removeFromRight (8);
    redo.setBounds (header.removeFromRight (34));
    header.removeFromRight (6);
    undo.setBounds (header.removeFromRight (34));

    auto body = b.reduced (16, 16);
    inRail.setBounds (body.removeFromLeft (62));
    outRail.setBounds (body.removeFromRight (62));
    body.reduce (12, 0);

    if (! proc.advancedView.load())
    {
        eq.setBounds (body.removeFromTop (352));
        body.removeFromTop (12);
        auto sends = body.removeFromBottom (64);
        body.removeFromBottom (12);
        const int w = (body.getWidth() - 4 * 10) / 5;
        for (auto& c : cards) { c->setBounds (body.removeFromLeft (w)); body.removeFromLeft (10); }
        sends.removeFromLeft (28);
        const int sw = (sends.getWidth() - 2 * 10) / 3;
        for (auto& s : sendCards) { s->setBounds (sends.removeFromLeft (sw)); sends.removeFromLeft (10); }
    }
    else
    {
        auto tabRow = body.removeFromTop (34);
        const int tw = (tabRow.getWidth() - 8 * 4) / numTabs;
        for (auto& t : tabs) { t.setBounds (tabRow.removeFromLeft (tw)); tabRow.removeFromLeft (4); }
        body.removeFromTop (8);
        for (auto* panel : panels) panel->setBounds (body);
    }
}
