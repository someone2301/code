#pragma once

#include "SendPanels.h"
#include "../dsp/Eq.h"
#include "../dsp/SpectrumAnalyser.h"

// Pre and post spectra for a display on the 20 Hz .. 20 kHz log axis: pre filled dark, post as a light outline.
// Shared by the EQ, Multiband, Resonance and Compression displays so all analyzers look and move the same.
struct AnalyzerPair
{
    enum Mode { Pre = 0, Post = 1, Off = 2, Both = 3 };
    // Segmented button order is Pre, Post, Both, Off.
    static int modeForSegment (int seg) { static const int m[] = { Pre, Post, Both, Off }; return m[juce::jlimit (0, 3, seg)]; }
    static int segmentForMode (int mode) { static const int s[] = { 0, 1, 3, 2 }; return s[juce::jlimit (0, 3, mode)]; }

    SpectrumProcessor pre, post;

    void update (KaminariVocalProcessor& p, SpectrumAnalyser& srcPre, SpectrumAnalyser& srcPost, int mode)
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const double dt = lastMs > 0 ? now - lastMs : 0.0;
        lastMs = now;
        if (mode == Pre || mode == Both)  feed (p, srcPre, pre, lastWrittenPre, lastAudioPre, now, dt);
        if (mode == Post || mode == Both) feed (p, srcPost, post, lastWrittenPost, lastAudioPost, now, dt);
    }

    // area: where to draw; xFreq (x within area) -> frequency. dB scale: 0 dB at the top, -90 at the bottom (4.5 dB/oct tilt).
    void draw (juce::Graphics& g, juce::Rectangle<float> area, const std::function<double (float)>& freqForX, int mode,
               const juce::String& preLabel = "PRE", const juce::String& postLabel = "POST") const
    {
        if (mode == Pre || mode == Both)
            drawOne (g, area, freqForX, pre, juce::Colour (0xff2b4a82).withAlpha (0.55f), juce::Colour (0xff5d6a88).withAlpha (0.8f));
        if (mode == Post || mode == Both)
            drawOne (g, area, freqForX, post, juce::Colour (0xffa9b8d6).withAlpha (mode == Both ? 0.08f : 0.16f), juce::Colour (0xffa9b8d6).withAlpha (0.55f));
        if (mode == Both)
        {
            g.setFont (uiFont (10.0f, true));
            g.setColour (juce::Colour (0xff5d6a88));
            g.drawText (preLabel, juce::Rectangle<float> (area.getRight() - 150.0f, area.getY() + 6.0f, 70.0f, 12.0f), juce::Justification::centredRight);
            g.setColour (juce::Colour (0xffa9b8d6));
            g.drawText (postLabel, juce::Rectangle<float> (area.getRight() - 76.0f, area.getY() + 6.0f, 70.0f, 12.0f), juce::Justification::centredRight);
        }
    }

private:
    static void drawOne (juce::Graphics& g, juce::Rectangle<float> a, const std::function<double (float)>& freqForX,
                         const SpectrumProcessor& an, juce::Colour fill, juce::Colour line)
    {
        juce::Path spec;
        spec.startNewSubPath (a.getX(), a.getBottom() + 2.0f);
        for (float x = 0.0f; x <= a.getWidth(); x += 2.0f)
        {
            const double f0 = freqForX (x), f1 = freqForX (x + 2.0f);
            const float db = an.columnDb (f0, f1) + 4.5f * (float) std::log2 (std::max (20.0, f0) / 1000.0);
            spec.lineTo (a.getX() + x, a.getY() + juce::jlimit (0.0f, a.getHeight() + 2.0f, a.getHeight() * (-db / 90.0f)));
        }
        spec.lineTo (a.getRight(), a.getBottom() + 2.0f);
        spec.closeSubPath();
        g.setColour (fill);
        g.fillPath (spec);
        g.setColour (line);
        g.strokePath (spec, juce::PathStrokeType (1.0f));
    }

    static void feed (KaminariVocalProcessor& p, SpectrumAnalyser& src, SpectrumProcessor& an, unsigned& lastWritten, double& lastAudioMs,
                      double now, double dt)
    {
        an.configure (p.analyserResolution.load(), p.analyserSpeed.load(), p.getSampleRate() > 0 ? p.getSampleRate() : 48000.0);
        if (src.samplesWritten() != lastWritten && src.copyLatest (an.inputBuffer(), an.fftSize()))
        {
            lastWritten = src.samplesWritten();
            lastAudioMs = now;
            an.process (dt);
        }
        else if (now - lastAudioMs > 100.0)
            an.releaseToFloor (dt);
    }

    unsigned lastWrittenPre = 0, lastWrittenPost = 0;
    double lastMs = 0, lastAudioPre = 0, lastAudioPost = 0;
};

// EQ response graph with draggable band nodes (Basic view and the EQ page).
//   - each band's own response is shaded between its curve and 0 dB, in the band's colour (as in Pro-Q)
//   - analyzer: pre-EQ, post-EQ or both at once (pre filled dark, post as a light outline over it)
//   - click empty space: a new band at that frequency; its type follows the frequency (see typeForFrequency)
//     and keeping the mouse down drags it straight away
//   - drag a node: frequency and gain; mouse wheel on a node: Q; double-click a node: gain to 0 dB
// Curves that fall below the graph simply leave it (no line along the bottom edge).
class EqCurve : public juce::Component, private juce::Timer
{
public:
    static int modeForSegment (int seg) { return AnalyzerPair::modeForSegment (seg); }
    static int segmentForMode (int mode) { return AnalyzerPair::segmentForMode (mode); }

    // Band type for a click at a frequency: 0-60 Hz low cut, 60-150 Hz low shelf, 150 Hz-8 kHz bell,
    // 8-15 kHz high shelf, above 15 kHz high cut.
    static int typeForFrequency (double f)
    {
        if (f <= 60.0) return kv::LowCut;
        if (f <= 150.0) return kv::LowShelf;
        if (f <= 8000.0) return kv::Bell;
        if (f <= 15000.0) return kv::HighShelf;
        return kv::HighCut;
    }

    static juce::Colour bandColour (int i)
    {
        static const juce::Colour c[] = { juce::Colour (0xff5ce1ff), juce::Colour (0xff8fb8ff), juce::Colour (0xffc39bff), juce::Colour (0xffff8fa3),
                                          juce::Colour (0xffffb547), juce::Colour (0xff7ee0a1), juce::Colour (0xff4fd1c5), juce::Colour (0xfff4f7fc) };
        return c[juce::jlimit (0, 7, i)];
    }

    explicit EqCurve (KaminariVocalProcessor& p) : proc (p), state (p.apvts)
    {
        setTitle ("EQ graph");
        setDescription ("Click empty space to add a band; drag a node to change frequency and gain; mouse wheel changes Q.");
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
        g.reduceClipRegion (getLocalBounds());

        // analyzers: pre-EQ (dark fill) under post-EQ (light outline + faint fill)
        analyzers.draw (g, b, [this] (float x) { return freqForX (x); }, proc.analyserMode.load(), "PRE EQ", "POST EQ");

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
        const int solo = proc.eqSolo.load();
        // keep paths just outside the graph instead of clamping to its edge, so a deep cut leaves no line at the bottom
        auto yFor = [this] (double db) { return juce::jlimit (-20.0f, (float) getHeight() + 20.0f, yForDb (db)); };
        const float y0 = yForDb (0.0);

        // each band's own area between its curve and 0 dB
        for (int i = 0; i < kv::Equalizer::numBands; ++i)
        {
            if (! s[i].used || ! s[i].on) continue;
            juce::Path area;
            area.startNewSubPath (0.0f, y0);
            for (int x = 0; x <= getWidth(); x += 2)
                area.lineTo ((float) x, yFor (designs[i].magnitudeDb (std::min (freqForX ((float) x), 0.49 * fs), fs)));
            area.lineTo ((float) getWidth(), y0);
            area.closeSubPath();
            const auto col = bandColour (i);
            g.setColour (col.withAlpha ((i == selected ? 0.26f : 0.12f) * (on ? 1.0f : 0.5f)));
            g.fillPath (area);
            if (i == selected)
            {
                g.setColour (col.withAlpha (0.55f));
                g.strokePath (area, juce::PathStrokeType (1.0f));
            }
        }

        // the sum of all bands
        juce::Path curve;
        for (int x = 0; x <= getWidth(); x += 2)
        {
            const double f = freqForX ((float) x);
            double db = 0;
            for (int i = 0; i < kv::Equalizer::numBands; ++i)
                if (s[i].used && s[i].on) db += designs[i].magnitudeDb (std::min (f, 0.49 * fs), fs);
            if (x == 0) curve.startNewSubPath ((float) x, yFor (db)); else curve.lineTo ((float) x, yFor (db));
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
            const auto col = bandColour (i);
            if (i == selected)
            {
                g.setColour (col.withAlpha (0.18f));
                g.fillEllipse (c.x - r - 7, c.y - r - 7, 2 * r + 14, 2 * r + 14);
            }
            g.setColour (i == selected ? col : navy800);
            g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
            g.setColour (s[i].on ? col.brighter (0.3f) : mist.withAlpha (0.6f));
            g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 1.6f);
            g.setColour (i == selected ? navy950 : white);
            g.setFont (uiFont (10.0f, true));
            g.drawText (juce::String (i + 1), juce::Rectangle<float> (2 * r, 2 * r).withCentre (c), juce::Justification::centred);
            if (i == solo)
            {
                g.setColour (juce::Colour (0xffffb547));
                g.setFont (uiFont (9.5f, true));
                g.drawText ("SOLO", juce::Rectangle<float> (40.0f, 12.0f).withCentre ({ c.x, c.y - r - 10.0f }), juce::Justification::centred);
            }
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

    // Adds a band at a point of the graph; returns its index or -1 when all eight are in use.
    int addBandAt (juce::Point<float> p)
    {
        for (int i = 0; i < kv::Equalizer::numBands; ++i)
            if (state.getRawParameterValue ("eq" + juce::String (i + 1) + "_used")->load() < 0.5f)
            {
                const double f = freqForX (p.x);
                const int type = typeForFrequency (f);
                const bool cut = type == kv::LowCut || type == kv::HighCut;
                const float gain = cut ? 0.0f : (float) juce::jlimit (-30.0, 30.0, std::round (dbForY (p.y) * 10.0) / 10.0);
                gesture (param (i, "type"), (float) type);
                gesture (param (i, "freq"), (float) f);
                gesture (param (i, "gain"), gain);
                gesture (param (i, "q"), type == kv::Bell ? 1.0f : 0.71f);
                gesture (param (i, "slope"), 1.0f);      // 12 dB/oct for the cut filters
                gesture (param (i, "on"), 1.0f);
                gesture (param (i, "used"), 1.0f);
                select (i);
                return i;
            }
        return -1;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragBand = bandAt (e.position);
        if (dragBand < 0 && ! e.mods.isPopupMenu())
        {
            dragBand = addBandAt (e.position);
            createdBand = dragBand;
            createdMs = juce::Time::getMillisecondCounterHiRes();
        }
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
        const int type = juce::roundToInt (state.getRawParameterValue ("eq" + juce::String (dragBand + 1) + "_type")->load());
        if (type != kv::LowCut && type != kv::HighCut && type != kv::Notch && type != kv::BandPass)
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
        // the second click of a double-click that created this band must not reset it
        if (b >= 0 && ! (b == createdBand && juce::Time::getMillisecondCounterHiRes() - createdMs < 800.0))
            gesture (param (b, "gain"), 0.0f);
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
        analyzers.update (proc, proc.analyserPre, proc.analyserPost, proc.analyserMode.load());
        repaint();
    }

    AnalyzerPair analyzers;

    KaminariVocalProcessor& proc;
    APVTS& state;
    int dragBand = -1, createdBand = -1;
    double createdMs = 0;
    double range = 18.0;
};
