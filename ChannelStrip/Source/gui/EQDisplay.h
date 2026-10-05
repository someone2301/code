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
    explicit EQDisplay (ChannelStripProcessor& p) : proc (p), apvts (p.apvts)
    {
        setWantsKeyboardFocus (true);
        setTitle ("EQ graph");
        setDescription ("Drag a node to change frequency and gain. Left and right arrows select a band, "
                        "up and down change its gain, Command or Ctrl with left and right change its frequency.");
    }

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
        selection.clearQuick();
        selection.add (b);
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

        // other selected bands
        for (int b : selection)
            if (b != selected && params[(size_t) b].on)
            {
                const auto c = nodePos (params[(size_t) b]);
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.drawEllipse (c.x - 12.0f, c.y - 12.0f, 24.0f, 24.0f, 1.5f);
            }

        // hover highlight and read-out
        if (hovered >= 0 && params[(size_t) hovered].on)
        {
            const auto& hb = params[(size_t) hovered];
            const auto c = nodePos (hb);
            g.setColour (juce::Colours::white);
            g.drawEllipse (c.x - 10.0f, c.y - 10.0f, 20.0f, 20.0f, 1.5f);
            static const char* typeNames[] = { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch" };
            const juce::String line1 = "Band " + juce::String (hovered + 1) + " · " + typeNames[juce::jlimit (0, 5, hb.type)];
            const juce::String line2 = cs::freqText (hb.freq) + (bandHasGain (hb.type) ? " · " + juce::String (hb.gainDb, 1) + " dB" : juce::String())
                                     + " · Q " + juce::String (hb.q, 2);
            auto box = juce::Rectangle<float> (150.0f, 34.0f).withPosition (c.x + 14.0f, c.y - 40.0f);
            if (box.getRight() > plot.getRight()) box.setX (c.x - 164.0f);
            if (box.getY() < plot.getY()) box.setY (c.y + 14.0f);
            g.setColour (juce::Colour (0xf012264a));
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (juce::Colour (0xff5ce1ff));
            g.drawRoundedRectangle (box, 4.0f, 1.0f);
            g.setColour (juce::Colours::white);
            g.setFont (uiFont (11.0f, true));
            g.drawText (line1, box.reduced (6.0f, 3.0f).removeFromTop (14.0f), juce::Justification::centredLeft);
            g.setFont (uiFont (11.0f));
            g.drawText (line2, box.reduced (6.0f, 3.0f).removeFromBottom (14.0f), juce::Justification::centredLeft);
        }

        if (rubberBand)
        {
            g.setColour (juce::Colour (0x305ce1ff));
            g.fillRect (rubber);
            g.setColour (juce::Colour (0xff5ce1ff));
            g.drawRect (rubber, 1.0f);
        }

        if (hasKeyboardFocus (false))
        {
            juce::Path ring, dashed;
            ring.addRoundedRectangle (full.reduced (1.5f), 6.0f);
            const float dashes[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, ring, dashes, 2);
            g.setColour (juce::Colour (0xff5ce1ff));
            g.fillPath (dashed);
        }

        g.setColour (juce::Colour (0xff6c7787));
        g.setFont (uiFont (10.5f));
        g.drawText ("double-click: add band / gain to 0 dB    drag (Shift = fine)    wheel: Q    right-click: menu",
                    plot.withHeight (14.0f).translated (-6.0f, 2.0f), juce::Justification::topRight);
    }

    //==================================================================================================================
    // Selection and editing helpers (also used by the tests)

    juce::Array<int> selection;
    int hovered = -1;

    juce::Point<float> nodePosition (int b) const { return nodePos (readBand (b)); }

    // Active bands under a point, top-most first.
    juce::Array<int> bandsAt (juce::Point<float> pos) const
    {
        juce::Array<int> hits;
        for (int b = cs::numBands - 1; b >= 0; --b)
        {
            const auto bp = readBand (b);
            if (bp.on && nodePos (bp).getDistanceFrom (pos) < 11.0f)
                hits.add (b);
        }
        return hits;
    }

    // Repeated clicks on the same spot cycle through overlapping nodes.
    int pickAt (juce::Point<float> pos)
    {
        const auto hits = bandsAt (pos);
        if (hits.isEmpty())
            return -1;
        int pick = hits[0];
        if (hits.size() > 1 && pos.getDistanceFrom (lastClickPos) < 4.0f && hits.contains (selected))
            pick = hits[(hits.indexOf (selected) + 1) % hits.size()];
        lastClickPos = pos;
        return pick;
    }

    void selectBand (int b)
    {
        selected = b;
        selection.clearQuick();
        selection.add (b);
        if (onSelect) onSelect (b);
        repaint();
    }

    void selectNext (int dir)
    {
        for (int k = 1; k <= cs::numBands; ++k)
        {
            const int b = ((selected + dir * k) % cs::numBands + cs::numBands) % cs::numBands;
            if (readBand (b).on) { selectBand (b); return; }
        }
    }

    void resetGain (int b) { setParamGesture (cs::eqId (b, "gain"), 0.0f); }

    void nudgeGain (float db)
    {
        for (int b : selection)
            if (bandHasGain (readBand (b).type))
                setParamGesture (cs::eqId (b, "gain"), juce::jlimit (-24.0f, 24.0f, readBand (b).gainDb + db));
    }

    void nudgeFreq (float semitones)
    {
        for (int b : selection)
            setParamGesture (cs::eqId (b, "freq"), juce::jlimit (20.0f, 20000.0f, readBand (b).freq * std::pow (2.0f, semitones / 12.0f)));
    }

    void deleteBand (int b)
    {
        setParamBool (cs::eqId (b, "on"), false);
        selection.removeFirstMatchingValue (b);
        repaint();
    }

    //==================================================================================================================
    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto hits = bandsAt (e.position);
        const int h = hits.isEmpty() ? -1 : hits[0];
        if (h != hovered) { hovered = h; repaint(); }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hovered >= 0) { hovered = -1; repaint(); }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        const int hit = pickAt (e.position);

        if (e.mods.isPopupMenu())
        {
            if (hit >= 0)
            {
                if (! selection.contains (hit))
                    selectBand (hit);
                showBandMenu (hit);
            }
            return;
        }

        if (hit < 0)
        {
            rubberBand = true;
            rubberStart = e.position;
            rubber = {};
            if (! e.mods.isShiftDown())
                selection.clearQuick();
            repaint();
            return;
        }

        if (e.mods.isShiftDown() && ! selection.isEmpty())
        {
            if (selection.contains (hit) && selection.size() > 1) selection.removeFirstMatchingValue (hit);
            else selection.addIfNotAlreadyThere (hit);
        }
        else if (! selection.contains (hit))
        {
            selection.clearQuick();
            selection.add (hit);
        }
        selected = hit;
        if (onSelect) onSelect (hit);

        dragStarts.clearQuick();
        for (int b : selection)
        {
            dragStarts.add (nodePos (readBand (b)));
            for (auto* id : { "freq", "gain" })
                if (auto* prm = apvts.getParameter (cs::eqId (b, id)))
                    prm->beginChangeGesture();
        }
        dragOffset = {};
        lastPos = e.position;
        dragging = hit;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (rubberBand)
        {
            rubber = juce::Rectangle<float> (rubberStart, e.position);
            for (int b = 0; b < cs::numBands; ++b)
            {
                const auto bp = readBand (b);
                if (bp.on && rubber.contains (nodePos (bp)))
                    selection.addIfNotAlreadyThere (b);
            }
            repaint();
            return;
        }
        if (dragging < 0)
            return;

        const float factor = e.mods.isShiftDown() ? 0.1f : 1.0f;   // Shift = fine
        dragOffset += (e.position - lastPos) * factor;
        lastPos = e.position;
        for (int k = 0; k < selection.size(); ++k)
        {
            const int b = selection[k];
            const auto p = dragStarts[k] + dragOffset;
            setParam (cs::eqId (b, "freq"), (float) juce::jlimit (20.0, 20000.0, xToFreq (p.x)));
            if (bandHasGain (readBand (b).type))
                setParam (cs::eqId (b, "gain"), juce::jlimit (-24.0f, 24.0f, yToGain (p.y)));
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (rubberBand)
        {
            rubberBand = false;
            if (! selection.isEmpty() && ! selection.contains (selected))
                selected = selection[0];
            repaint();
            return;
        }
        if (dragging < 0)
            return;
        for (int b : selection)
            for (auto* id : { "freq", "gain" })
                if (auto* prm = apvts.getParameter (cs::eqId (b, id)))
                    prm->endChangeGesture();
        dragging = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto hits = bandsAt (e.position);
        if (! hits.isEmpty())
        {
            resetGain (hits.contains (selected) ? selected : hits[0]);   // gain to 0 dB; the band stays
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
        selectBand (best);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const auto hits = bandsAt (e.position);
        if (hits.isEmpty())
            return;
        const int target = hits.contains (selected) ? selected : hits[0];
        selectBand (target);
        const float q = readBand (target).q * std::exp (w.deltaY * (e.mods.isShiftDown() ? 0.12f : 1.2f));
        setParamGesture (cs::eqId (target, "q"), juce::jlimit (0.1f, 18.0f, q));
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto mods = key.getModifiers();
        const int code = key.getKeyCode();
        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
        {
            const int dir = code == juce::KeyPress::rightKey ? 1 : -1;
            if (mods.isCommandDown()) nudgeFreq ((float) dir * (mods.isShiftDown() ? 0.25f : 1.0f));
            else selectNext (dir);
            return true;
        }
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        {
            nudgeGain ((code == juce::KeyPress::upKey ? 1.0f : -1.0f) * (mods.isShiftDown() ? 0.1f : 0.5f));
            return true;
        }
        if ((code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) && readBand (selected).on)
        {
            deleteBand (selected);
            return true;
        }
        return false;
    }

    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override   { repaint(); }

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

    void setParam (const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    void setParamGesture (const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    }

    void showBandMenu (int b)
    {
        static const char* typeNames[] = { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch" };
        juce::PopupMenu types;
        for (int t = 0; t < 6; ++t)
            types.addItem (typeNames[t], true, readBand (b).type == t, [this, b, t] { setParamGesture (cs::eqId (b, "type"), (float) t); });
        juce::PopupMenu m;
        m.addSubMenu ("Type", types);
        m.addItem ("Gain to 0 dB", bandHasGain (readBand (b).type), false, [this, b] { resetGain (b); });
        m.addSeparator();
        m.addItem ("Delete Band", [this, b] { deleteBand (b); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
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
    juce::Array<juce::Point<float>> dragStarts;
    juce::Point<float> dragOffset, lastPos, lastClickPos { -100.0f, -100.0f }, rubberStart;
    juce::Rectangle<float> rubber;
    bool rubberBand = false;

    SpectrumProcessor analyser;
    double lastFrameMs = 0.0, lastAudioMs = 0.0;
    unsigned lastWritten = 0;
    juce::VBlankAttachment vblank { this, std::function<void()> ([this] { refreshAnalyser (juce::Time::getMillisecondCounterHiRes()); }) };
};
