#include "PluginEditor.h"

namespace
{
    const char* tabNames[] = { "TUNE", "EQ", "MULTIBAND", "COMPRESSION", "DE-ESS", "RESONANCE" };
}

KaminariVocalEditor::KaminariVocalEditor (KaminariVocalProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      chain (p.presets, {}, "chain"),
      inRail (p.apvts, kvid::inGain, "IN", p.inPeakCh[0], p.inPeakCh[1], lnf),
      outRail (p.apvts, kvid::outGain, "OUT", p.outPeakCh[0], p.outPeakCh[1], lnf),
      eq (p),
      tunePage_ (p), eqPage_ (p), multibandPage (p), compressionPage (p), deEssPage (p), resonancePage (p)
{
    setLookAndFeel (&lnf);
    setTitle (KV_PRODUCT_NAME);
    addAndMakeVisible (root);

    // Basic view: a fuller hammer always means more correction or reduction (Retune Speed and thresholds are inverted).
    // The Compression card also has its makeup Gain slider beside the hammer.
    cards[0] = std::make_unique<kvui::ModuleCard> (p, "Tune", "tn_on", "tn_speed", true, "Retune Speed", KaminariVocalProcessor::ModTune);
    cards[1] = std::make_unique<kvui::ModuleCard> (p, "Multiband", "mb_on", "mb1_thresh", true, "Threshold", KaminariVocalProcessor::ModMultiband);
    cards[2] = std::make_unique<kvui::ModuleCard> (p, "Compression", "lv_on", "lv_peak", false, "Compression", KaminariVocalProcessor::ModCompression);
    cards[3] = std::make_unique<kvui::ModuleCard> (p, "De-ess", "ds_on", "ds_range", false, "Range", KaminariVocalProcessor::ModDeEss);
    cards[4] = std::make_unique<kvui::ModuleCard> (p, "Resonance", "rs_on", "rs_depth", false, "Depth", KaminariVocalProcessor::ModResonance);
    const int cardTab[5] = { TabTune, TabMultiband, TabCompression, TabDeEss, TabResonance };
    for (int i = 0; i < 5; ++i)
    {
        root.addChildComponent (*cards[(size_t) i]);
        cards[(size_t) i]->open.onClick = [this, t = cardTab[i]] { showTab (true, t); };
    }
    root.addChildComponent (eq);
    eq.onSelect = [this] (int band) { eqPage_.select (band); };

    panels = { &tunePage_, &eqPage_, &multibandPage, &compressionPage, &deEssPage, &resonancePage };
    for (int t = 0; t < numTabs; ++t)
    {
        tabs[(size_t) t] = std::make_unique<TabButton>();
        auto& b = *tabs[(size_t) t];
        b.setButtonText (tabNames[t]);
        b.setRadioGroupId (2);
        b.setClickingTogglesState (true);
        b.setTooltip ("Show " + juce::String (tabNames[t]).toLowerCase() + " controls.");
        b.onClick = [this, t] { if (tabs[(size_t) t]->getToggleState()) showTab (true, t); };
        root.addChildComponent (b);
        root.addChildComponent (*panels[(size_t) t]);
    }
    {
        const char* powerIds[] = { "tn_on", "eq_on", "mb_on", "lv_on", "ds_on", "rs_on" };
        for (int t = 0; t < numTabs; ++t)
            tabs[(size_t) t]->setPowerParameter (*p.apvts.getParameter (powerIds[t]), &p.undoManager);
    }
    auto grText = [this] (int m) { return [this, m]
    {
        const float g = proc.moduleGr[(size_t) m].load();
        return (g > 0.05f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : juce::String()) + juce::String (std::abs (g), 1);
    }; };
    tabs[TabTune]->info = [this]
    {
        const float m = proc.tune.detectedMidi.load();
        return m < 0 ? juce::String ("--") : kvui::noteName (juce::roundToInt (m));
    };
    tabs[TabEq]->info = [this]
    {
        int used = 0;
        for (int i = 0; i < 8; ++i) used += proc.apvts.getRawParameterValue ("eq" + juce::String (i + 1) + "_used")->load() > 0.5f ? 1 : 0;
        return juce::String (used) + (used == 1 ? " band" : " bands");
    };
    tabs[TabMultiband]->info = [this] { const int c = juce::roundToInt (proc.apvts.getRawParameterValue ("mb_count")->load()); return juce::String (c) + (c == 1 ? " band" : " bands"); };
    tabs[TabCompression]->info = grText (KaminariVocalProcessor::ModCompression);
    tabs[TabDeEss]->info = grText (KaminariVocalProcessor::ModDeEss);
    tabs[TabResonance]->info = grText (KaminariVocalProcessor::ModResonance);

    for (auto* b : { &basicButton, &advancedButton })
    {
        b->setRadioGroupId (1);
        b->setClickingTogglesState (true);
        b->getProperties().set ("kvStyle", "fill");
        root.addAndMakeVisible (*b);
    }
    basicButton.setTooltip ("Compact view: one control per module and the EQ graph.");
    advancedButton.setTooltip ("All controls, one tab per module.");
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

    animations.setClickingTogglesState (true);
    animations.getProperties().set ("kvStyle", "light");
    animations.setToggleState (proc.hostTempo.animations.load(), juce::dontSendNotification);
    animations.setTooltip ("Animations on or off: the hammers' lightning and the EQ node lightning. Off saves processing on slower computers.");
    animations.setTitle ("Animations");
    animations.onClick = [this] { proc.hostTempo.animations.store (animations.getToggleState()); root.repaint(); };

    for (int z : { 75, 100, 125, 150, 200 }) zoom.addItem (juce::String (z) + "%", z);
    zoom.setSelectedId (juce::roundToInt (proc.uiScale.load() * 100.0f), juce::dontSendNotification);
    zoom.setTooltip ("Window size");
    zoom.setTitle ("Window size");
    zoom.onChange = [this] { setZoom ((float) zoom.getSelectedId() / 100.0f); };

    for (auto* c : std::initializer_list<juce::Component*> { &chain, &abA, &abB, &abCopy, &undo, &redo, &animations, &zoom, &inRail, &outRail })
        root.addAndMakeVisible (c);
    chain.name.setTooltip ("Chain presets set every module at once.");
    chain.onLoaded = [this]
    {
        chain.refresh();
        for (auto* bar : { &tunePage_.preset, &eqPage_.preset, &multibandPage.preset, &compressionPage.preset,
                           &deEssPage.preset, &resonancePage.preset })
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
    for (auto& tabButton : tabs) if (tabButton->isVisible()) tabButton->repaint();
    proc.undoManager.beginNewTransaction();   // groups changes into steps of at most 250 ms
}

void KaminariVocalEditor::showTab (bool advanced, int tab)
{
    tab = juce::jlimit (0, (int) numTabs - 1, tab);
    proc.advancedView.store (advanced);
    proc.advancedTab.store (tab);
    updateView();
}

void KaminariVocalEditor::updateView()
{
    const bool adv = proc.advancedView.load();
    const int tab = proc.advancedTab.load();
    basicButton.setToggleState (! adv, juce::dontSendNotification);
    advancedButton.setToggleState (adv, juce::dontSendNotification);
    for (auto& c : cards) c->setVisible (! adv);
    eq.setVisible (! adv);
    for (int t = 0; t < numTabs; ++t)
    {
        tabs[(size_t) t]->setVisible (adv);
        tabs[(size_t) t]->setToggleState (adv && t == tab, juce::dontSendNotification);
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
#if KV_EDITION_ALT
    // edition badge, so the Alt and the full version are easy to tell apart in a session
    g.setColour (accent);
    g.setFont (font (10.0f, 3, 0.2f));
    g.drawText ("ALT", 160, 33, 40, 12, juce::Justification::centredLeft);
#endif

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
    chain.setBounds (header.removeFromLeft (210));
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
    header.removeFromRight (8);
    animations.setBounds (header.removeFromRight (52));

    auto body = b.reduced (16, 16);
    inRail.setBounds (body.removeFromLeft (62));
    outRail.setBounds (body.removeFromRight (62));
    body.reduce (12, 0);

    if (! proc.advancedView.load())
    {
        eq.setBounds (body.removeFromTop (352));
        body.removeFromTop (12);
        // all module cards (and so their hammers) are the same size
        const int nc = (int) cards.size(), gap = 8;
        const float unit = (float) (body.getWidth() - (nc - 1) * gap) / (float) nc;
        for (size_t i = 0; i < cards.size(); ++i)
        {
            const int w = i + 1 == cards.size() ? body.getWidth() : juce::roundToInt (unit);
            cards[i]->setBounds (body.removeFromLeft (w));
            body.removeFromLeft (gap);
        }
    }
    else
    {
        // Tab widths follow their names. Read-outs (note, gain reduction, bands) are shown in priority order while
        // they fit; the rest of the row is shared out.
        auto tabRow = body.removeFromTop (38);
        const auto nameFont = kvtheme::font (14.0f, 3, 0.08f);
        constexpr int gap = 6, pad = 40, infoGap = 6;   // pad: 10 + bolt 16 + 10, plus 4 spare
        const int infoW[numTabs] = { 26, 50, 48, 34, 30, 34 };
        const int priority[] = { TabCompression, TabDeEss, TabResonance, TabTune, TabMultiband, TabEq };
        int w[numTabs];
        int used = (numTabs - 1) * gap;
        for (int t = 0; t < numTabs; ++t)
        {
            w[t] = pad + (int) juce::GlyphArrangement::getStringWidth (nameFont, tabNames[t]);
            used += w[t];
            tabs[(size_t) t]->showInfo = false;
        }
        for (int t : priority)
            if (used + infoW[t] + infoGap <= tabRow.getWidth())
            {
                w[t] += infoW[t] + infoGap;
                used += infoW[t] + infoGap;
                tabs[(size_t) t]->showInfo = true;
            }
        const int spare = (tabRow.getWidth() - used) / numTabs;
        for (int t = 0; t < numTabs; ++t)
        {
            tabs[(size_t) t]->setBounds (tabRow.removeFromLeft (w[t] + spare));
            tabRow.removeFromLeft (gap);
        }
        body.removeFromTop (10);
        for (auto* panel : panels) panel->setBounds (body);
    }
}

void KaminariVocalEditor::TabButton::paintButton (juce::Graphics& g, bool hover, bool)
{
    using namespace kvtheme;
    const bool on = getToggleState();
    auto r = getLocalBounds().toFloat().reduced (on ? 1.0f : 0.5f);
    g.setColour (on ? navy800 : (hover ? navy800 : navy900));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? accent : navy600);
    g.drawRoundedRectangle (r, 6.0f, on ? 2.0f : 1.0f);
    auto b = getLocalBounds().reduced (10, 0);
    {
        // the module's on/off switch: bright when on, dim when bypassed; a ring on hover, smaller while pressed
        const bool live = power == nullptr || isPowerOn();
        auto icon = b.withWidth (10).toFloat().withSizeKeepingCentre (9.0f, 13.0f);
        if (boltDown) icon = icon.withSizeKeepingCentre (7.5f, 11.0f);
        if (boltHover || boltDown)
        {
            g.setColour ((live ? accent : mist).withAlpha (boltDown ? 0.28f : 0.16f));
            g.fillEllipse (icon.withSizeKeepingCentre (22.0f, 22.0f));
        }
        if (live)
        {
            g.setColour (accent.withAlpha (0.25f));
            g.fillPath (boltPath (icon.expanded (1.5f)));
        }
        g.setColour (live ? (boltHover ? accent.brighter (0.3f) : accent) : steel.withAlpha (boltHover ? 0.9f : 0.6f));
        g.fillPath (boltPath (icon));
    }
    b.removeFromLeft (16);
    g.setColour (white);
    g.setFont (font (14.0f, 3, 0.08f));
    g.drawText (getButtonText(), b, juce::Justification::centredLeft);
    if (info && showInfo)
    {
        const int w = (int) juce::GlyphArrangement::getStringWidth (font (14.0f, 3, 0.08f), getButtonText());
        g.setColour (mist);
        g.setFont (font (12.0f, 0, 0.05f));
        g.drawText (info(), b.withTrimmedLeft (w + 8), juce::Justification::centredLeft);
    }
}

void KaminariVocalEditor::TabButton::setPowerParameter (juce::RangedAudioParameter& p, juce::UndoManager* um)
{
    power = &p;
    powerAtt = std::make_unique<juce::ParameterAttachment> (p, [this] (float) { repaint(); }, um);
}

void KaminariVocalEditor::TabButton::mouseMove (const juce::MouseEvent& e)
{
    const bool h = power != nullptr && boltArea().contains (e.getPosition());
    if (h != boltHover) { boltHover = h; repaint(); }
    juce::TextButton::mouseMove (e);
}

void KaminariVocalEditor::TabButton::mouseExit (const juce::MouseEvent& e)
{
    if (boltHover) { boltHover = false; repaint(); }
    juce::TextButton::mouseExit (e);
}

void KaminariVocalEditor::TabButton::mouseDown (const juce::MouseEvent& e)
{
    if (power != nullptr && boltArea().contains (e.getPosition()))
    {
        boltDown = true;
        repaint();
        return;   // the icon switches the module; it does not change the page
    }
    juce::TextButton::mouseDown (e);
}

void KaminariVocalEditor::TabButton::mouseDrag (const juce::MouseEvent& e)
{
    if (boltDown) return;
    juce::TextButton::mouseDrag (e);
}

void KaminariVocalEditor::TabButton::mouseUp (const juce::MouseEvent& e)
{
    if (boltDown)
    {
        boltDown = false;
        if (boltArea().contains (e.getPosition()) && powerAtt != nullptr)
            powerAtt->setValueAsCompleteGesture (isPowerOn() ? 0.0f : 1.0f);
        repaint();
        return;
    }
    juce::TextButton::mouseUp (e);
}

juce::String KaminariVocalEditor::TabButton::getTooltip()
{
    if (boltHover && power != nullptr)
        return getButtonText() + (isPowerOn() ? ": on. Click the lightning to bypass it." : ": bypassed. Click the lightning to switch it on.");
    return juce::TextButton::getTooltip();
}
