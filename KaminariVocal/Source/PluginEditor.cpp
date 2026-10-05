#include "PluginEditor.h"

namespace
{
    const char* sendNames[] = { "Reverb", "Delay", "Widener" };
}

KaminariVocalEditor::KaminariVocalEditor (KaminariVocalProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      inGain (p.apvts, kvid::inGain, "IN", "Input gain before the channel and the sends."),
      outGain (p.apvts, kvid::outGain, "OUT", "Output gain of the dry vocal. Post-fader sends follow it."),
      inMeter (p.inPeak), outMeter (p.outPeak),
      reverb (p), delay (p), widener (p)
{
    setLookAndFeel (&lnf);
    setTitle ("Kaminari Vocal");

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

        auto& t = tabs[(size_t) s];
        t.setButtonText (sendNames[s]);
        t.setRadioGroupId (2);
        t.setClickingTogglesState (true);
        t.setTooltip ("Show the " + juce::String (sendNames[s]).toLowerCase() + " send's controls.");
        t.onClick = [this, s] { if (tabs[(size_t) s].getToggleState()) showAdvanced (true, s); };
        addChildComponent (t);
    }
    addChildComponent (reverb);
    addChildComponent (delay);
    addChildComponent (widener);

    for (auto* b : { &basicButton, &advancedButton })
    {
        b->setRadioGroupId (1);
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
    }
    basicButton.setTooltip ("Compact view: send on/off and level.");
    advancedButton.setTooltip ("Detailed controls for each send.");
    basicButton.onClick = [this] { if (basicButton.getToggleState()) showAdvanced (false, currentSend()); };
    advancedButton.onClick = [this] { if (advancedButton.getToggleState()) showAdvanced (true, currentSend()); };

    for (auto* c : std::initializer_list<juce::Component*> { &inGain, &outGain, &inMeter, &outMeter })
        addAndMakeVisible (c);

    setSize (1040, 640);
    updateView();
}

KaminariVocalEditor::~KaminariVocalEditor()
{
    setLookAndFeel (nullptr);
}

void KaminariVocalEditor::showAdvanced (bool advanced, int send)
{
    proc.advancedView.store (advanced);
    proc.advancedSend.store (juce::jlimit (0, (int) KaminariVocalProcessor::numSends - 1, send));
    updateView();
}

void KaminariVocalEditor::updateView()
{
    const bool adv = proc.advancedView.load();
    const int send = proc.advancedSend.load();
    basicButton.setToggleState (! adv, juce::dontSendNotification);
    advancedButton.setToggleState (adv, juce::dontSendNotification);
    for (int s = 0; s < KaminariVocalProcessor::numSends; ++s)
    {
        strips[(size_t) s]->setVisible (! adv);
        tabs[(size_t) s].setVisible (adv);
        tabs[(size_t) s].setToggleState (adv && s == send, juce::dontSendNotification);
    }
    reverb.setVisible (adv && send == KaminariVocalProcessor::Reverb);
    delay.setVisible (adv && send == KaminariVocalProcessor::Delay);
    widener.setVisible (adv && send == KaminariVocalProcessor::Widener);
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

    // wordmark with a small bolt glyph
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
    g.drawText ("KAMINARI VOCAL", 36, 0, 260, 52, juce::Justification::centredLeft);

    g.setColour (mist);
    g.setFont (uiFont (12.0f));
    g.drawText ("Sends: returns are 100 % wet and added to the unchanged dry vocal  |  0 smp latency",
                getLocalBounds().removeFromTop (52).withTrimmedLeft (480).withTrimmedRight (16), juce::Justification::centredRight);

    if (! proc.advancedView.load())
    {
        g.setColour (mist);
        g.setFont (uiFont (12.0f, true));
        g.drawText ("SENDS", 112, 70, 200, 18, juce::Justification::centredLeft);
    }
}

void KaminariVocalEditor::resized()
{
    auto b = getLocalBounds();
    auto header = b.removeFromTop (52);
    auto views = header.withTrimmedLeft (300).removeFromLeft (180).withSizeKeepingCentre (180, 28);
    basicButton.setBounds (views.removeFromLeft (90));
    advancedButton.setBounds (views);

    auto left = b.removeFromLeft (96).reduced (8);
    auto right = b.removeFromRight (96).reduced (8);
    inGain.setBounds (left.removeFromBottom (100));
    inMeter.setBounds (left.withSizeKeepingCentre (14, left.getHeight() - 16));
    outGain.setBounds (right.removeFromBottom (100));
    outMeter.setBounds (right.withSizeKeepingCentre (14, right.getHeight() - 16));

    auto content = b.reduced (8, 12);
    if (! proc.advancedView.load())
    {
        content.removeFromTop (24);
        auto row = content.removeFromTop (300);
        const int w = (row.getWidth() - 2 * 12) / 3;
        for (int s = 0; s < KaminariVocalProcessor::numSends; ++s)
        {
            strips[(size_t) s]->setBounds (row.removeFromLeft (w));
            row.removeFromLeft (12);
        }
    }
    else
    {
        auto tabRow = content.removeFromTop (30);
        for (auto& t : tabs)
        {
            t.setBounds (tabRow.removeFromLeft (120));
            tabRow.removeFromLeft (6);
        }
        content.removeFromTop (6);
        for (auto* panel : std::initializer_list<juce::Component*> { &reverb, &delay, &widener })
            panel->setBounds (content);
    }
}
