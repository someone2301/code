#pragma once

#include <juce_dsp/juce_dsp.h>
#include "../PluginProcessor.h"
#include "Look.h"

inline const juce::Colour bandColours[cs::numBands] = {
    juce::Colour (0xffff5f5f), juce::Colour (0xffffa24a), juce::Colour (0xffeedb4d),
    juce::Colour (0xff5cdc7e), juce::Colour (0xff4cb6ff), juce::Colour (0xffb584ff)
};

// Analyser + curve + draggable band nodes.
class EQDisplay : public juce::Component
{
public:
    explicit EQDisplay (ChannelStripProcessor& p) : proc (p), apvts (p.apvts) {}

    // Pulls the newest audio from the processor and updates the analyser (called once per display frame).
    void refreshAnalyser (double nowMs)
    {
        analyser.configure (proc.analyserResolution.load(), proc.analyserSpeed.load(), proc.currentSampleRate.load());
        const double dt = lastFrameMs > 0.0 ? nowMs - lastFrameMs : 0.0;
        lastFrameMs = nowMs;
        if (proc.analyser.samplesWritten() != lastWritten
            && proc.analyser.copyLatest (analyser.inputBuffer(), analyser.fftSize()))
        {
            lastWritten = proc.analyser.samplesWritten();
            lastAudioMs = nowMs;
            analyser.process (dt);
        }
        else if (nowMs - lastAudioMs > 100.0)
        {
            analyser.releaseToFloor (dt);   // host stopped sending audio: let the display fall
        }
        repaint();
    }

    std::function<void (int)> onSelect;
    int selected = 0;

    void setSelected (int b)
    {
        selected = b;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto full = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1a1e25), 0.0f, 0.0f, juce::Colour (0xff0e1115), 0.0f, full.getBottom(), false));
        g.fillRoundedRectangle (full, 6.0f);

        const auto plot = plotArea();
        const double sr = proc.currentSampleRate.load();

        // grid
        g.setFont (uiFont (10.0f));
        const float freqs[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
        for (float f : freqs)
        {
            const float x = freqToX (f);
            g.setColour (juce::Colour (0xff2a313b));
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            g.setColour (juce::Colour (0xff7a8594));
            g.drawText (f >= 1000 ? juce::String (f / 1000.0f, 0) + "k" : juce::String ((int) f),
                        juce::Rectangle<float> (40.0f, 12.0f).withCentre ({ x, plot.getBottom() + 8.0f }), juce::Justification::centred);
        }
        for (int db = -24; db <= 24; db += 6)
        {
            const float y = gainToY ((float) db);
            g.setColour (db == 0 ? juce::Colour (0xff48515e) : juce::Colour (0xff232932));
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            g.setColour (juce::Colour (0xff7a8594));
            g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (26.0f, 12.0f).withCentre ({ plot.getX() - 14.0f, y }),
                        juce::Justification::centredRight);
        }

        // spectrum: one column per 2 px, loudest bin per column, tilted 4.5 dB/oct around 1 kHz
        if (analyser.fftSize() > 0)
        {
            juce::Path sp;
            sp.startNewSubPath (plot.getX(), plot.getBottom());
            for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
            {
                const double f0 = xToFreq (x - 1.0f), f1 = xToFreq (x + 1.0f);
                float db = analyser.columnDb (f0, f1);
                db += 4.5f * (float) std::log2 (std::sqrt (f0 * f1) / 1000.0);
                const float y = plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 90.0f) / 90.0f);
                sp.lineTo (x, y);
            }
            sp.lineTo (plot.getRight(), plot.getBottom());
            sp.closeSubPath();
            g.setColour (juce::Colour (0x40508fc8));
            g.fillPath (sp);
            g.setColour (juce::Colour (0x90a9b8d6));
            g.strokePath (sp, juce::PathStrokeType (1.0f));
        }

        // curves
        const auto params = readParams();
        juce::Path curve;
        for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
        {
            const float y = gainToY (juce::jlimit (-24.0f, 24.0f, (float) Equalizer::responseDb (params, xToFreq (x), sr)));
            x == plot.getX() ? curve.startNewSubPath (x, y) : curve.lineTo (x, y);
        }
        juce::Path fill (curve);
        fill.lineTo (plot.getRight(), gainToY (0.0f));
        fill.lineTo (plot.getX(), gainToY (0.0f));
        fill.closeSubPath();
        g.setColour (juce::Colour (0x30ffd27a));
        g.fillPath (fill);
        g.setColour (juce::Colour (0xffffd27a));
        g.strokePath (curve, juce::PathStrokeType (2.0f));

        // selected band curve
        if (params[(size_t) selected].on)
        {
            EqParams only {};
            only[(size_t) selected] = params[(size_t) selected];
            juce::Path bp;
            for (float x = plot.getX(); x <= plot.getRight(); x += 3.0f)
            {
                const float y = gainToY (juce::jlimit (-24.0f, 24.0f, (float) Equalizer::responseDb (only, xToFreq (x), sr)));
                x == plot.getX() ? bp.startNewSubPath (x, y) : bp.lineTo (x, y);
            }
            g.setColour (bandColours[selected].withAlpha (0.85f));
            g.strokePath (bp, juce::PathStrokeType (1.2f));
        }

        // nodes
        for (int b = 0; b < cs::numBands; ++b)
        {
            if (! params[(size_t) b].on)
                continue;
            const auto c = nodePos (params[(size_t) b]);
            const bool sel = b == selected;
            g.setColour (bandColours[b].withAlpha (0.25f));
            g.fillEllipse (c.x - (sel ? 14.0f : 11.0f), c.y - (sel ? 14.0f : 11.0f), sel ? 28.0f : 22.0f, sel ? 28.0f : 22.0f);
            g.setColour (bandColours[b]);
            g.fillEllipse (c.x - 8.0f, c.y - 8.0f, 16.0f, 16.0f);
            g.setColour (juce::Colour (0xff10131a));
            g.setFont (uiFont (11.0f, true));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (16.0f, 16.0f).withCentre (c), juce::Justification::centred);
        }

        g.setColour (juce::Colour (0xff6c7787));
        g.setFont (uiFont (10.5f));
        g.drawText ("double-click: add / remove band    drag: freq + gain    wheel: Q",
                    plot.withHeight (14.0f).translated (-6.0f, 2.0f), juce::Justification::topRight);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int hit = hitBand (e.position);
        if (hit < 0)
            return;
        selected = hit;
        dragging = hit;
        for (auto* id : { "freq", "gain" })
            if (auto* prm = apvts.getParameter (cs::eqId (hit, id)))
                prm->beginChangeGesture();
        if (onSelect) onSelect (hit);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0)
            return;
        setParam (cs::eqId (dragging, "freq"), (float) juce::jlimit (20.0, 20000.0, xToFreq (e.position.x)));
        if (bandHasGain (readBand (dragging).type))
            setParam (cs::eqId (dragging, "gain"), juce::jlimit (-24.0f, 24.0f, yToGain (e.position.y)));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging < 0)
            return;
        for (auto* id : { "freq", "gain" })
            if (auto* prm = apvts.getParameter (cs::eqId (dragging, id)))
                prm->endChangeGesture();
        dragging = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int hit = hitBand (e.position);
        if (hit >= 0)
        {
            setParamBool (cs::eqId (hit, "on"), false);
            return;
        }

        // enable the disabled band whose default frequency is closest to the click
        const double f = xToFreq (e.position.x);
        int best = -1;
        double bestDist = 1.0e9;
        for (int b = 0; b < cs::numBands; ++b)
        {
            if (readBand (b).on)
                continue;
            const double d = std::abs (std::log (f / cs::bandDefaults[b].freq));
            if (d < bestDist) { bestDist = d; best = b; }
        }
        if (best < 0)
            return;

        setParam (cs::eqId (best, "freq"), (float) juce::jlimit (20.0, 20000.0, f));
        if (bandHasGain (readBand (best).type))
            setParam (cs::eqId (best, "gain"), juce::jlimit (-24.0f, 24.0f, yToGain (e.position.y)));
        setParamBool (cs::eqId (best, "on"), true);
        selected = best;
        if (onSelect) onSelect (best);
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        int target = hitBand (e.position);
        if (target < 0)
            return;
        selected = target;
        if (onSelect) onSelect (target);
        const float q = readBand (target).q * std::exp (w.deltaY * 1.2f);
        auto* prm = apvts.getParameter (cs::eqId (target, "q"));
        prm->beginChangeGesture();
        setParam (cs::eqId (target, "q"), juce::jlimit (0.1f, 18.0f, q));
        prm->endChangeGesture();
        repaint();
    }

private:
    juce::Rectangle<float> plotArea() const
    {
        return getLocalBounds().toFloat().reduced (8.0f).withTrimmedLeft (22.0f).withTrimmedBottom (14.0f);
    }

    float freqToX (double f) const
    {
        const auto p = plotArea();
        return p.getX() + p.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0));
    }
    double xToFreq (float x) const
    {
        const auto p = plotArea();
        return 20.0 * std::pow (1000.0, (double) ((x - p.getX()) / p.getWidth()));
    }
    float gainToY (float db) const
    {
        const auto p = plotArea();
        return p.getY() + p.getHeight() * (1.0f - (db + 24.0f) / 48.0f);
    }
    float yToGain (float y) const
    {
        const auto p = plotArea();
        return (1.0f - (y - p.getY()) / p.getHeight()) * 48.0f - 24.0f;
    }

    EqBandParams readBand (int b) const
    {
        auto get = [&] (const char* what) { return apvts.getRawParameterValue (cs::eqId (b, what))->load(); };
        return { get ("on") > 0.5f, juce::roundToInt (get ("type")), get ("freq"), get ("gain"), get ("q") };
    }

    EqParams readParams() const
    {
        EqParams p;
        for (int b = 0; b < cs::numBands; ++b)
            p[(size_t) b] = readBand (b);
        return p;
    }

    juce::Point<float> nodePos (const EqBandParams& b) const
    {
        return { freqToX (b.freq), gainToY (bandHasGain (b.type) ? b.gainDb : 0.0f) };
    }

    int hitBand (juce::Point<float> pos) const
    {
        for (int b = cs::numBands - 1; b >= 0; --b)
        {
            const auto bp = readBand (b);
            if (bp.on && nodePos (bp).getDistanceFrom (pos) < 11.0f)
                return b;
        }
        return -1;
    }

    void setParam (const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    void setParamBool (const juce::String& id, bool v)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (v ? 1.0f : 0.0f);
            p->endChangeGesture();
        }
    }

    ChannelStripProcessor& proc;
    APVTS& apvts;
    int dragging = -1;

    SpectrumProcessor analyser;
    double lastFrameMs = 0.0, lastAudioMs = 0.0;
    unsigned lastWritten = 0;
    juce::VBlankAttachment vblank { this, std::function<void()> ([this] { refreshAnalyser (juce::Time::getMillisecondCounterHiRes()); }) };
};
