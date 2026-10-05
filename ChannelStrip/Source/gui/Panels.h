#pragma once

#include "Widgets.h"
#include "EQDisplay.h"
#include "LightningSlider.h"

namespace palettes
{
    inline Palette dark()
    {
        return { juce::Colour (0xff2b313b), juce::Colour (0xff8b95a3), juce::Colour (0xfff2f4f7), juce::Colour (0xff7c8696),
                 juce::Colour (0xffd5dbe4), juce::Colour (0xffe8a33d), juce::Colour (0xff2b313b), juce::Colour (0xffc9d0da),
                 juce::Colour (0xff141414) };
    }
    inline Palette fet()
    {
        return { juce::Colour (0xff17181a), juce::Colour (0xffb8bec6), juce::Colour (0xffffffff), juce::Colour (0xffb8bec6),
                 juce::Colour (0xffe3e6ea), juce::Colour (0xffd23a2e), juce::Colour (0xff1f2125), juce::Colour (0xffd9dde2),
                 juce::Colour (0xffffffff) };
    }
    inline Palette opto()
    {
        return { juce::Colour (0xff23252a), juce::Colour (0xffe0e3e7), juce::Colour (0xfff4f4f4), juce::Colour (0xff2a2c30),
                 juce::Colour (0xff1b1c1f), juce::Colour (0xff1f2328), juce::Colour (0xffb7bbc1), juce::Colour (0xff1b1c1f),
                 juce::Colour (0xfff2f2f2) };
    }
}

inline void drawScrew (juce::Graphics& g, juce::Point<float> c, juce::Colour base)
{
    g.setColour (base.darker (0.5f));
    g.fillEllipse (c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f);
    g.setColour (base.brighter (0.2f));
    g.fillEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f);
    g.setColour (base.darker (0.7f));
    g.drawLine (c.x - 3.5f, c.y + 1.5f, c.x + 3.5f, c.y - 1.5f, 1.3f);
}

inline void drawPanelFrame (juce::Graphics& g, juce::Rectangle<float> b, juce::Colour top, juce::Colour bottom, juce::Colour edge)
{
    g.setGradientFill (juce::ColourGradient (top, 0.0f, b.getY(), bottom, 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle (b, 8.0f);
    g.setColour (edge);
    g.drawRoundedRectangle (b.reduced (1.0f), 8.0f, 1.5f);
}

// ---------------------------------------------------------------------------------------------------------------------
// Toggle buttons say their state in words (not colour alone) and explain themselves on hover.
inline void describeToggle (juce::Button& b, const juce::String& onText, const juce::String& offText, const juce::String& tip)
{
    b.setTooltip (tip);
    b.setClickingTogglesState (true);
    auto update = [&b, onText, offText] { b.setButtonText (b.getToggleState() ? onText : offText); };
    b.onStateChange = update;
    update();
}

class DeEsserPanel : public juce::Component
{
public:
    explicit DeEsserPanel (ChannelStripProcessor& p)
        : lnf (palettes::dark()),
          freq (p.apvts, ids::deessFreq, "FREQUENCY"),
          thresh (*p.apvts.getParameter (ids::deessThresh), p.hostTempo, "THRESHOLD"),
          range (p.apvts, ids::deessRange, "RANGE"),
          bar (p.deessGr, juce::Colour (0xffe8a33d))
    {
        for (auto* k : { &freq, &range })
        {
            k->setLNF (&lnf);
            addAndMakeVisible (*k);
        }
        addAndMakeVisible (thresh);
        for (auto* b : { &onBtn, &listenBtn })
        {
            b->setLookAndFeel (&lnf);
            addAndMakeVisible (*b);
        }
        onAtt = std::make_unique<APVTS::ButtonAttachment> (p.apvts, ids::deessOn, onBtn);
        listenAtt = std::make_unique<APVTS::ButtonAttachment> (p.apvts, ids::deessListen, listenBtn);
        describeToggle (onBtn, "ON", "OFF", "De-esser on or bypassed");
        describeToggle (listenBtn, "LISTENING", "LISTEN", "Hear only the band the de-esser reacts to");
        addAndMakeVisible (bar);
    }

    ~DeEsserPanel() override
    {
        for (auto* b : { &onBtn, &listenBtn })
            b->setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        drawPanelFrame (g, getLocalBounds().toFloat(), juce::Colour (0xff222831), juce::Colour (0xff171b21), juce::Colour (0xff3a4350));
        g.setColour (juce::Colour (0xffe8a33d));
        g.setFont (uiFont (15.0f, true));
        g.drawText ("DE-ESSER", 14, 8, 120, 24, juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff8b95a3));
        g.setFont (uiFont (11.0f));
        g.drawText ("split-band", 14, 28, 120, 14, juce::Justification::centredLeft);
        g.drawText ("GAIN REDUCTION", 14, getHeight() - 78, 200, 14, juce::Justification::centredLeft);
        g.drawText ("0", 14, getHeight() - 40, 20, 12, juce::Justification::centredLeft);
        g.drawText ("20 dB", getWidth() - 54, getHeight() - 40, 40, 12, juce::Justification::centredRight);
    }

    void resized() override
    {
        onBtn.setBounds (getWidth() - 66, 10, 52, 24);
        listenBtn.setBounds (getWidth() - 140, 10, 70, 24);
        freq.setBounds (10, 48, 108, 100);
        range.setBounds (10, 152, 108, 100);
        thresh.setBounds (118, 44, 116, 214);
        bar.setBounds (14, getHeight() - 62, getWidth() - 28, 16);
    }

private:
    ModuleLNF lnf;
    Knob freq;
    LightningSlider thresh;
    Knob range;
    juce::TextButton onBtn { "ON" }, listenBtn { "LISTEN" };
    std::unique_ptr<APVTS::ButtonAttachment> onAtt, listenAtt;
    ReductionBar bar;
};

// ---------------------------------------------------------------------------------------------------------------------
class EqPanel : public juce::Component
{
public:
    explicit EqPanel (ChannelStripProcessor& p)
        : apvts (p.apvts),
          lnf (palettes::dark()),
          display (p),
          freq (p.apvts, cs::eqId (0, "freq"), "FREQ"),
          gain (p.apvts, cs::eqId (0, "gain"), "GAIN"),
          q (p.apvts, cs::eqId (0, "q"), "Q")
    {
        addAndMakeVisible (display);
        for (auto* k : { &freq, &gain, &q })
        {
            k->setLNF (&lnf);
            addAndMakeVisible (*k);
        }
        for (auto* b : { &onBtn, &postBtn, &bandOnBtn })
        {
            b->setLookAndFeel (&lnf);
            addAndMakeVisible (*b);
        }
        typeBox.setLookAndFeel (&lnf);
        typeBox.addItemList (juce::StringArray { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch" }, 1);
        addAndMakeVisible (typeBox);

        for (auto* box : { &resBox, &speedBox })
        {
            box->setLookAndFeel (&lnf);
            addAndMakeVisible (*box);
        }
        resBox.addItemList (SpectrumProcessor::resolutionNames(), 1);
        speedBox.addItemList (SpectrumProcessor::speedNames(), 1);
        resBox.setTooltip ("Analyser resolution. Higher settings resolve low frequencies more finely.");
        speedBox.setTooltip ("Analyser release speed. Faster settings show level changes more quickly.");
        resBox.setSelectedItemIndex (p.analyserResolution.load(), juce::dontSendNotification);
        speedBox.setSelectedItemIndex (p.analyserSpeed.load(), juce::dontSendNotification);
        auto* proc = &p;
        resBox.onChange = [this, proc] { proc->analyserResolution.store (resBox.getSelectedItemIndex()); };
        speedBox.onChange = [this, proc] { proc->analyserSpeed.store (speedBox.getSelectedItemIndex()); };

        for (int i = 0; i < cs::numBands; ++i)
        {
            auto& b = bandBtn[(size_t) i];
            b.setButtonText (juce::String (i + 1));
            b.setLookAndFeel (&lnf);
            b.onClick = [this, i] { selectBand (i); };
            addAndMakeVisible (b);
        }

        onAtt = std::make_unique<APVTS::ButtonAttachment> (p.apvts, ids::eqOn, onBtn);
        postAtt = std::make_unique<APVTS::ButtonAttachment> (p.apvts, ids::eqPost, postBtn);
        describeToggle (onBtn, "EQ ON", "EQ OFF", "EQ on or bypassed");
        describeToggle (postBtn, "POST COMP", "PRE COMP", "Place the EQ before or after the compressor");

        display.onSelect = [this] (int b) { bind (b); };
        bind (2);
        display.setSelected (2);
    }

    ~EqPanel() override
    {
        for (auto* b : { &onBtn, &postBtn, &bandOnBtn })
            b->setLookAndFeel (nullptr);
        for (auto& b : bandBtn)
            b.setLookAndFeel (nullptr);
        typeBox.setLookAndFeel (nullptr);
        resBox.setLookAndFeel (nullptr);
        speedBox.setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        drawPanelFrame (g, getLocalBounds().toFloat(), juce::Colour (0xff222831), juce::Colour (0xff171b21), juce::Colour (0xff3a4350));
        g.setColour (juce::Colour (0xffe8a33d));
        g.setFont (uiFont (15.0f, true));
        g.drawText ("PARAMETRIC EQ", 14, 4, 200, 24, juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff8b95a3));
        g.setFont (uiFont (11.0f));
        g.drawText ("6 bands", 140, 4, 80, 24, juce::Justification::centredLeft);
        g.drawText ("ANALYSER", 236, 4, 70, 24, juce::Justification::centredLeft);
        for (int i = 0; i < cs::numBands; ++i)
        {
            auto r = bandBtn[(size_t) i].getBounds().toFloat();
            g.setColour (bandColours[i]);
            g.fillRoundedRectangle (r.getX() + 3.0f, r.getBottom() + 3.0f, r.getWidth() - 6.0f, 3.0f, 1.5f);
        }
        g.setColour (juce::Colour (0xff8b95a3));
        g.drawText ("BAND", 14, getHeight() - 82, 60, 12, juce::Justification::centredLeft);
        g.drawText ("TYPE", 222, getHeight() - 82, 60, 12, juce::Justification::centredLeft);
    }

    void resized() override
    {
        postBtn.setBounds (getWidth() - 116, 6, 102, 22);
        resBox.setBounds (300, 6, 100, 22);
        speedBox.setBounds (406, 6, 100, 22);
        onBtn.setBounds (getWidth() - 174, 6, 52, 22);
        display.setBounds (8, 32, getWidth() - 16, 212);

        const int y = getHeight() - 66;
        for (int i = 0; i < cs::numBands; ++i)
            bandBtn[(size_t) i].setBounds (14 + i * 34, y - 4, 30, 26);
        bandOnBtn.setBounds (14, y + 36, 100, 24);
        typeBox.setBounds (222, y - 2, 124, 26);
        freq.setBounds (380, getHeight() - 90, 110, 88);
        gain.setBounds (500, getHeight() - 90, 110, 88);
        q.setBounds (620, getHeight() - 90, 110, 88);
    }

private:
    void selectBand (int b)
    {
        display.setSelected (b);
        bind (b);
    }

    void bind (int b)
    {
        bandOnAtt.reset();
        typeAtt.reset();
        bandOnAtt = std::make_unique<APVTS::ButtonAttachment> (apvts, cs::eqId (b, "on"), bandOnBtn);
        describeToggle (bandOnBtn, "BAND ON", "BAND OFF", "Selected band active or removed from the curve");
        typeAtt = std::make_unique<APVTS::ComboBoxAttachment> (apvts, cs::eqId (b, "type"), typeBox);
        freq.attach (cs::eqId (b, "freq"));
        gain.attach (cs::eqId (b, "gain"));
        q.attach (cs::eqId (b, "q"));
        for (int i = 0; i < cs::numBands; ++i)
            bandBtn[(size_t) i].setToggleState (i == b, juce::dontSendNotification);
    }

    APVTS& apvts;
    ModuleLNF lnf;
    EQDisplay display;
    Knob freq, gain, q;
    juce::TextButton onBtn { "EQ ON" }, postBtn { "POST COMP" }, bandOnBtn { "BAND ON" };
    juce::TextButton bandBtn[cs::numBands];
    juce::ComboBox typeBox, resBox, speedBox;
    std::unique_ptr<APVTS::ButtonAttachment> onAtt, postAtt, bandOnAtt;
    std::unique_ptr<APVTS::ComboBoxAttachment> typeAtt;
};

// ---------------------------------------------------------------------------------------------------------------------
class CompressorPanel : public juce::Component
{
public:
    explicit CompressorPanel (ChannelStripProcessor& p)
        : fetLNF (palettes::fet()), laLNF (palettes::opto()),
          meter (p.compGr, VUMeter::Fet),
          fetIn   (*p.apvts.getParameter (ids::fetIn), p.hostTempo, "INPUT"),
          fetOut  (p.apvts, ids::fetOut,     "OUTPUT"),
          fetAtk  (p.apvts, ids::fetAttack,  "ATTACK"),
          fetRel  (p.apvts, ids::fetRelease, "RELEASE"),
          laPeak  (*p.apvts.getParameter (ids::laPeak), p.hostTempo, "PEAK REDUCTION"),
          laGain  (p.apvts, ids::laGain,     "GAIN"),
          mix     (p.apvts, ids::compMix,    "MIX"),
          modeAtt (*p.apvts.getParameter (ids::compMode), [this] (float v) { setMode (juce::roundToInt (v)); }),
          ratioAtt (*p.apvts.getParameter (ids::fetRatio), [this] (float v) { setRatio (juce::roundToInt (v)); }),
          limitAtt (*p.apvts.getParameter (ids::laLimit), [this] (float v) { setLimit (v > 0.5f); })
    {
        addAndMakeVisible (meter);
        for (auto* k : { &fetOut, &fetAtk, &fetRel, &laGain, &mix })
            addAndMakeVisible (*k);
        addAndMakeVisible (fetIn);
        addAndMakeVisible (laPeak);

        // the opto face is light grey: dark caption and value text keep the contrast
        LightningSlider::Colours optoText;
        optoText.text = juce::Colour (0xff1b1c1f);
        optoText.subText = juce::Colour (0xff2a2c30);
        laPeak.setColours (optoText);
        for (auto* b : { &onBtn, &modeFetBtn, &modeOptoBtn, &compressBtn, &limitBtn })
            addAndMakeVisible (*b);

        const char* ratioNames[5] = { "4", "8", "12", "20", "ALL" };
        for (int i = 0; i < 5; ++i)
        {
            ratioBtn[i].setButtonText (ratioNames[i]);
            ratioBtn[i].onClick = [this, i] { ratioAtt.setValueAsCompleteGesture ((float) i); };
            addAndMakeVisible (ratioBtn[i]);
        }

        modeFetBtn.onClick = [this] { modeAtt.setValueAsCompleteGesture (0.0f); };
        modeOptoBtn.onClick = [this] { modeAtt.setValueAsCompleteGesture (1.0f); };
        compressBtn.onClick = [this] { limitAtt.setValueAsCompleteGesture (0.0f); };
        limitBtn.onClick = [this] { limitAtt.setValueAsCompleteGesture (1.0f); };

        onAtt = std::make_unique<APVTS::ButtonAttachment> (p.apvts, ids::compOn, onBtn);
        describeToggle (onBtn, "ON", "OFF", "Compressor on or bypassed");

        modeAtt.sendInitialUpdate();
        ratioAtt.sendInitialUpdate();
        limitAtt.sendInitialUpdate();
    }

    ~CompressorPanel() override { applyLNF (nullptr); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        if (mode == 0)
        {
            drawPanelFrame (g, b, juce::Colour (0xff2a2c31), juce::Colour (0xff0f1012), juce::Colour (0xffa4aab2));
            g.setColour (juce::Colour (0xffd8dce1));
            g.setFont (uiFont (22.0f, true));
            g.drawText ("FET COMPRESSOR", 380, 10, 330, 30, juce::Justification::centredLeft);
            g.setColour (juce::Colour (0xffd23a2e));
            g.setFont (uiFont (12.0f, true));
            g.drawText ("1176-STYLE  PEAK LIMITING AMPLIFIER", 380, 36, 330, 14, juce::Justification::centredLeft);
            g.setColour (juce::Colour (0xffb8bec6));
            g.drawText ("RATIO", 496, 184, 120, 14, juce::Justification::centredLeft);
            for (auto c : screws (b)) drawScrew (g, c, juce::Colour (0xff8d939b));
        }
        else
        {
            drawPanelFrame (g, b, juce::Colour (0xffd2d5d9), juce::Colour (0xff9ea2a8), juce::Colour (0xff4a4d52));
            g.setColour (juce::Colour (0xff1b1c1f));
            g.setFont (uiFont (22.0f, true));
            g.drawText ("OPTO COMPRESSOR", 380, 10, 330, 30, juce::Justification::centredLeft);
            g.setFont (uiFont (12.0f, true));
            g.drawText ("LA-2A-STYLE  LEVELING AMPLIFIER", 380, 36, 330, 14, juce::Justification::centredLeft);
            for (auto c : screws (b)) drawScrew (g, c, juce::Colour (0xff6a6e74));
        }
    }

    void resized() override
    {
        const int w = getWidth();
        modeFetBtn.setBounds (20, 12, 170, 28);
        modeOptoBtn.setBounds (196, 12, 170, 28);
        onBtn.setBounds (w - 90, 12, 64, 28);
        meter.setBounds (20, 62, 330, 236);

        fetIn.setBounds  (366, 56, 124, 242);
        fetOut.setBounds (490, 62, 120, 112);
        fetAtk.setBounds (610, 62, 120, 112);
        fetRel.setBounds (730, 62, 120, 112);
        for (int i = 0; i < 5; ++i)
            ratioBtn[i].setBounds (496 + i * 74, 204, 66, 44);

        laPeak.setBounds (390, 52, 200, 246);
        laGain.setBounds (610, 62, 200, 236);

        mix.setBounds (w - 210, 62, 120, 112);
        compressBtn.setBounds (w - 210, 204, 110, 30);
        limitBtn.setBounds (w - 210, 242, 110, 30);
    }

private:
    static std::array<juce::Point<float>, 4> screws (juce::Rectangle<float> b)
    {
        return { juce::Point<float> { b.getX() + 10.0f, b.getY() + 10.0f }, { b.getRight() - 10.0f, b.getY() + 10.0f },
                 { b.getX() + 10.0f, b.getBottom() - 10.0f }, { b.getRight() - 10.0f, b.getBottom() - 10.0f } };
    }

    void applyLNF (juce::LookAndFeel* current)
    {
        fetOut.setLNF (current == nullptr ? nullptr : &fetLNF);
        fetAtk.setLNF (current == nullptr ? nullptr : &fetLNF);
        fetRel.setLNF (current == nullptr ? nullptr : &fetLNF);
        laGain.setLNF (current == nullptr ? nullptr : &laLNF);
        mix.setLNF (current);
        for (auto* b : { &onBtn, &modeFetBtn, &modeOptoBtn, &compressBtn, &limitBtn })
            b->setLookAndFeel (current);
        for (auto& b : ratioBtn)
            b.setLookAndFeel (current == nullptr ? nullptr : &fetLNF);
    }

    void setMode (int m)
    {
        mode = m;
        const bool fet = m == 0;
        meter.setStyle (fet ? VUMeter::Fet : VUMeter::Opto);
        applyLNF (fet ? static_cast<juce::LookAndFeel*> (&fetLNF) : &laLNF);
        for (auto* k : { &fetOut, &fetAtk, &fetRel })
            k->setVisible (fet);
        fetIn.setVisible (fet);
        for (auto& b : ratioBtn)
            b.setVisible (fet);
        laGain.setVisible (! fet);
        laPeak.setVisible (! fet);
        compressBtn.setVisible (! fet);
        limitBtn.setVisible (! fet);
        modeFetBtn.setToggleState (fet, juce::dontSendNotification);
        modeOptoBtn.setToggleState (! fet, juce::dontSendNotification);
        repaint();
    }

    void setRatio (int r)
    {
        for (int i = 0; i < 5; ++i)
            ratioBtn[i].setToggleState (i == r, juce::dontSendNotification);
    }

    void setLimit (bool limit)
    {
        compressBtn.setToggleState (! limit, juce::dontSendNotification);
        limitBtn.setToggleState (limit, juce::dontSendNotification);
    }

    int mode = 0;
    ModuleLNF fetLNF, laLNF;
    VUMeter meter;
    LightningSlider fetIn;
    Knob fetOut, fetAtk, fetRel;
    LightningSlider laPeak;
    Knob laGain, mix;
    juce::TextButton onBtn { "ON" }, modeFetBtn { "FET  1176-style" }, modeOptoBtn { "OPTO  LA-2A-style" },
                     compressBtn { "COMPRESS" }, limitBtn { "LIMIT" };
    juce::TextButton ratioBtn[5];
    std::unique_ptr<APVTS::ButtonAttachment> onAtt;
    juce::ParameterAttachment modeAtt, ratioAtt, limitAtt;
};
