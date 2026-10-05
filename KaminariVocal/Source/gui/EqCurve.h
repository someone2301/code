#pragma once

#include "SendPanels.h"
#include "../dsp/Eq.h"
#include "../dsp/SpectrumAnalyser.h"

// EQ response graph with draggable band nodes. Drag a node: frequency and gain. Mouse wheel on a node: Q.
// Double-click empty space: add a bell there. Double-click a node: gain to 0 dB. Click: select.
class EqCurve : public juce::Component, private juce::Timer
{
public:
    explicit EqCurve (KaminariVocalProcessor& p) : proc (p), state (p.apvts)
    {
        setTitle ("EQ graph");
        setDescription ("Drag a node to change frequency and gain; mouse wheel changes Q; double-click empty space to add a band.");
        startTimerHz (30);
    }

    void setRange (double db) { range = db; repaint(); }
    double getRange() const { return range; }
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
        if (analyserVisible)
        {
            juce::Path spec;
            spec.startNewSubPath (0, b.getBottom());
            for (int x = 0; x <= getWidth(); x += 2)
            {
                const double f0 = freqForX ((float) x), f1 = freqForX ((float) x + 2.0f);
                const float db = analyser.columnDb (f0, f1) + 4.5f * (float) std::log2 (std::max (20.0, f0) / 1000.0);   // 4.5 dB/oct tilt
                const float y = juce::jlimit (0.0f, b.getBottom(), (float) (b.getHeight() * (-db / 90.0)));
                spec.lineTo ((float) x, y);
            }
            spec.lineTo (b.getRight(), b.getBottom());
            spec.closeSubPath();
            g.setColour (mist.withAlpha (0.16f));
            g.fillPath (spec);
            g.setColour (mist.withAlpha (0.45f));
            g.strokePath (spec, juce::PathStrokeType (1.0f));
        }
        g.setColour (navy800);
        for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
            g.drawVerticalLine (juce::roundToInt (xForFreq (f)), 0.0f, b.getBottom());
        const double step = range > 20 ? 10.0 : (range > 9 ? 6.0 : 3.0);
        for (double db = -std::floor (range / step) * step; db <= range; db += step)
            g.drawHorizontalLine (juce::roundToInt (yForDb (db)), 0.0f, b.getRight());
        g.setColour (mist);
        g.setFont (uiFont (10.0f));
        for (auto [f, t] : { std::pair { 50.0, "50" }, { 100.0, "100" }, { 200.0, "200" }, { 500.0, "500" }, { 1000.0, "1k" },
                             { 2000.0, "2k" }, { 5000.0, "5k" }, { 10000.0, "10k" } })
            g.drawText (t, (int) xForFreq (f) + 3, getHeight() - 14, 30, 12, juce::Justification::left);
        for (double db = -std::floor (range / step) * step; db <= range; db += step)
            if (std::abs (db) > 0.1 && std::abs (db) < range - 0.1)
                g.drawText ((db > 0 ? "+" : juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"))) + juce::String ((int) std::abs (db)),
                            6, (int) yForDb (db) - 13, 34, 12, juce::Justification::left);
            else if (std::abs (db) < 0.1)
                g.drawText ("0", 6, (int) yForDb (db) - 13, 34, 12, juce::Justification::left);

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
            if (i == selected)
            {
                g.setColour (bolt.withAlpha (0.18f));
                g.fillEllipse (c.x - r - 7, c.y - r - 7, 2 * r + 14, 2 * r + 14);
            }
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
    void timerCallback() override
    {
        const int mode = proc.analyserMode.load();
        analyserVisible = mode != 2;
        if (analyserVisible)
        {
            auto& src = mode == 0 ? proc.analyserPre : proc.analyserPost;
            const double now = juce::Time::getMillisecondCounterHiRes();
            const double dt = lastMs > 0 ? now - lastMs : 0.0;
            lastMs = now;
            analyser.configure (proc.analyserResolution.load(), proc.analyserSpeed.load(), proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0);
            if (src.samplesWritten() != lastWritten && src.copyLatest (analyser.inputBuffer(), analyser.fftSize()))
            {
                lastWritten = src.samplesWritten();
                lastAudioMs = now;
                analyser.process (dt);
            }
            else if (now - lastAudioMs > 100.0)
                analyser.releaseToFloor (dt);
        }
        repaint();
    }

    SpectrumProcessor analyser;
    unsigned lastWritten = 0;
    double lastMs = 0, lastAudioMs = 0;
    bool analyserVisible = true;

    KaminariVocalProcessor& proc;
    APVTS& state;
    int dragBand = -1;
    double range = 18.0;
};
