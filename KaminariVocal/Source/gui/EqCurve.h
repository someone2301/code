#pragma once

#include "SendPanels.h"
#include "../dsp/Eq.h"

// EQ response graph with draggable band nodes. Drag a node: frequency and gain. Mouse wheel on a node: Q.
// Double-click empty space: add a bell there. Double-click a node: gain to 0 dB. Click: select.
class EqCurve : public juce::Component, private juce::Timer
{
public:
    explicit EqCurve (KaminariVocalProcessor& p) : proc (p), state (p.apvts)
    {
        setTitle ("EQ graph");
        setDescription ("Drag a node to change frequency and gain; mouse wheel changes Q; double-click empty space to add a band.");
        startTimerHz (20);
    }
    ~EqCurve() override { stopTimer(); }

    std::function<void (int)> onSelect;
    int selected = 0;

    float xForFreq (double f) const { return (float) (std::log (f / 20.0) / std::log (1000.0)) * (float) getWidth(); }
    double freqForX (float x) const { return 20.0 * std::pow (1000.0, juce::jlimit (0.0f, 1.0f, x / (float) getWidth())); }
    float yForDb (double db) const { return (float) (getHeight() * 0.5 - db / range * (getHeight() * 0.5 - 6)); }
    double dbForY (float y) const { return (getHeight() * 0.5 - y) / (getHeight() * 0.5 - 6) * range; }

    void paint (juce::Graphics& g) override
    {
        using namespace kvui::colours;
        auto b = getLocalBounds().toFloat();
        g.setColour (navy950);
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (navy800);
        for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
            g.drawVerticalLine (juce::roundToInt (xForFreq (f)), 0.0f, b.getBottom());
        for (double db : { -12.0, -6.0, 0.0, 6.0, 12.0 })
            g.drawHorizontalLine (juce::roundToInt (yForDb (db)), 0.0f, b.getRight());
        g.setColour (mist);
        g.setFont (uiFont (10.0f));
        for (auto [f, t] : { std::pair { 100.0, "100" }, { 1000.0, "1k" }, { 10000.0, "10k" } })
            g.drawText (t, (int) xForFreq (f) + 3, getHeight() - 14, 30, 12, juce::Justification::left);
        for (double db : { -12.0, 12.0 })
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), 3, (int) yForDb (db) - 6, 30, 12, juce::Justification::left);

        kv::EqBandSettings s[kv::Equalizer::numBands];
        proc.readEqSettings (s);
        const double fs = 48000.0;
        kv::EqDesign designs[kv::Equalizer::numBands];
        for (int i = 0; i < kv::Equalizer::numBands; ++i)
            designs[i] = kv::EqDesign::make (s[i], fs);
        const bool on = state.getRawParameterValue ("eq_on")->load() > 0.5f;

        juce::Path curve;
        for (int x = 0; x <= getWidth(); x += 2)
        {
            const double f = freqForX ((float) x);
            double db = 0;
            for (int i = 0; i < kv::Equalizer::numBands; ++i)
                if (s[i].used && s[i].on) db += designs[i].magnitudeDb (std::min (f, 0.49 * fs), fs);
            const float y = juce::jlimit (0.0f, (float) getHeight(), yForDb (db));
            if (x == 0) curve.startNewSubPath ((float) x, y); else curve.lineTo ((float) x, y);
        }
        g.setColour (bolt.withAlpha (on ? 0.25f : 0.1f));
        g.strokePath (curve, juce::PathStrokeType (6.0f));
        g.setColour (on ? bolt : mist.withAlpha (0.5f));
        g.strokePath (curve, juce::PathStrokeType (2.0f));

        for (int i = 0; i < kv::Equalizer::numBands; ++i)
        {
            if (! s[i].used) continue;
            const auto c = nodePos (s[i]);
            const float r = i == selected ? 10.0f : 8.5f;
            g.setColour (i == selected ? bolt : navy800);
            g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
            g.setColour (s[i].on ? white : mist);
            g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 1.5f);
            g.setColour (i == selected ? navy950 : white);
            g.setFont (uiFont (10.0f, true));
            g.drawText (juce::String (i + 1), juce::Rectangle<float> (2 * r, 2 * r).withCentre (c), juce::Justification::centred);
        }
    }

    juce::Point<float> nodePos (const kv::EqBandSettings& s) const
    {
        const bool gainless = s.type == kv::LowCut || s.type == kv::HighCut || s.type == kv::Notch || s.type == kv::BandPass;
        return { xForFreq (s.freq), yForDb (gainless ? 0.0 : s.gainDb) };
    }

    int bandAt (juce::Point<float> p) const
    {
        kv::EqBandSettings s[kv::Equalizer::numBands];
        proc.readEqSettings (s);
        for (int i = kv::Equalizer::numBands - 1; i >= 0; --i)
            if (s[i].used && nodePos (s[i]).getDistanceFrom (p) < 12.0f) return i;
        return -1;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragBand = bandAt (e.position);
        if (dragBand >= 0)
        {
            select (dragBand);
            for (auto* id : { "freq", "gain" }) param (dragBand, id)->beginChangeGesture();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragBand < 0) return;
        setPlain (param (dragBand, "freq"), (float) freqForX (e.position.x));
        setPlain (param (dragBand, "gain"), (float) juce::jlimit (-30.0, 30.0, dbForY (e.position.y)));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragBand >= 0)
            for (auto* id : { "freq", "gain" }) param (dragBand, id)->endChangeGesture();
        dragBand = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int b = bandAt (e.position);
        if (b >= 0) { gesture (param (b, "gain"), 0.0f); return; }
        for (int i = 0; i < kv::Equalizer::numBands; ++i)
            if (state.getRawParameterValue ("eq" + juce::String (i + 1) + "_used")->load() < 0.5f)
            {
                gesture (param (i, "type"), 0.0f);
                gesture (param (i, "freq"), (float) freqForX (e.position.x));
                gesture (param (i, "gain"), (float) juce::jlimit (-30.0, 30.0, dbForY (e.position.y)));
                gesture (param (i, "used"), 1.0f);
                select (i);
                return;
            }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int b = bandAt (e.position);
        if (b < 0) return;
        auto* q = param (b, "q");
        q->beginChangeGesture();
        q->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, q->getValue() + (w.isReversed ? -w.deltaY : w.deltaY) * 0.05f));
        q->endChangeGesture();
    }

private:
    juce::RangedAudioParameter* param (int band, const char* what)
    {
        return state.getParameter ("eq" + juce::String (band + 1) + "_" + what);
    }
    static void setPlain (juce::RangedAudioParameter* p, float v) { p->setValueNotifyingHost (p->convertTo0to1 (v)); }
    static void gesture (juce::RangedAudioParameter* p, float v) { p->beginChangeGesture(); setPlain (p, v); p->endChangeGesture(); }
    void select (int b) { selected = b; if (onSelect) onSelect (b); repaint(); }
    void timerCallback() override { repaint(); }

    KaminariVocalProcessor& proc;
    APVTS& state;
    int dragBand = -1;
    double range = 18.0;
};
