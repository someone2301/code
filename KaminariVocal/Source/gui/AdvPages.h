#pragma once

#include "AdvWidgets.h"

// Advanced pages, laid out after the GUI preview artboards (AdvTune, AdvEQ, AdvMultiband, Advanced (Compression),
// AdvDeEss, AdvResonance). Only controls that work in this version are shown.
namespace kvui
{
    using namespace kvtheme;

    inline juce::String noteName (int midi)
    {
        static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (n[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
    }

    inline void setParamPlain (APVTS& s, const juce::String& id, float v)
    {
        auto* p = s.getParameter (id);
        const float n = p->convertTo0to1 (v);
        if (std::abs (n - p->getValue()) > 1e-6f) { p->beginChangeGesture(); p->setValueNotifyingHost (n); p->endChangeGesture(); }
    }

    inline float plain (APVTS& s, const juce::String& id) { return s.getRawParameterValue (id)->load(); }

    //==================================================================================================================
    // TUNE
    class CentsMeter : public juce::Component, private juce::Timer
    {
    public:
        explicit CentsMeter (KaminariVocalProcessor& p) : proc (p) { setTitle ("Pitch meter"); startTimerHz (30); }
        ~CentsMeter() override { stopTimer(); }
        bool hold = false;

        void paint (juce::Graphics& g) override
        {
            const auto c = getLocalBounds().toFloat().getCentre().translated (0, 8);
            const float R = juce::jmin (getWidth(), getHeight()) * 0.42f;
            for (int i = 0; i <= 60; ++i)
            {
                const float tick = -100.0f + i * (200.0f / 60.0f);
                const float a = juce::degreesToRadians (tick / 100.0f * 135.0f);
                const bool lit = correction >= 0 ? (tick >= 0 && tick <= correction) : (tick <= 0 && tick >= correction);
                g.setColour (lit ? accent : navy600);
                const float r0 = R * 0.74f, r1 = R * 0.95f;
                g.drawLine (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a), c.x + r1 * std::sin (a), c.y - r1 * std::cos (a), lit ? 3.2f : 2.6f);
            }
            g.setColour (mist);
            g.setFont (font (11.0f, 0));
            for (auto [mark, t] : { std::pair { -100, "-100" }, { -50, "-50" }, { -25, "-25" }, { 25, "+25" }, { 50, "+50" }, { 100, "+100" } })
            {
                const float a = juce::degreesToRadians (mark / 100.0f * 135.0f), r = R * 1.08f;
                g.drawText (t, juce::Rectangle<float> (40, 14).withCentre ({ c.x + r * std::sin (a), c.y - r * std::cos (a) }), juce::Justification::centred);
            }
            if (midi >= 0)
            {
                const float a = juce::degreesToRadians (juce::jlimit (-100.0f, 100.0f, cents) / 100.0f * 135.0f);
                g.setColour (white);
                g.drawLine (c.x + R * 0.68f * std::sin (a), c.y - R * 0.68f * std::cos (a), c.x + R * 0.98f * std::sin (a), c.y - R * 0.98f * std::cos (a), 2.0f);
            }
            const float inner = R * 0.64f;
            g.setColour (navy950);
            g.fillEllipse (c.x - inner, c.y - inner, inner * 2, inner * 2);
            g.setColour (navy600);
            g.drawEllipse (c.x - inner, c.y - inner, inner * 2, inner * 2, 1.0f);
            g.setColour (white);
            g.setFont (font (inner * 0.62f, 0));
            g.drawText (midi < 0 ? juce::String ("--") : noteName (juce::roundToInt (midi)), juce::Rectangle<float> (inner * 2, inner * 0.8f).withCentre (c.translated (0, -inner * 0.08f)),
                        juce::Justification::centred);
            g.setColour (mist);
            g.setFont (font (12.0f, 0));
            g.drawText (midi < 0 ? juce::String ("no pitch detected")
                                 : (cents >= 0 ? "+" : "") + juce::String (juce::roundToInt (cents)) + " cents" + dot() + "correcting "
                                       + (correction >= 0 ? "+" : "") + juce::String (juce::roundToInt (correction)),
                        juce::Rectangle<float> (inner * 2.2f, 16).withCentre (c.translated (0, inner * 0.42f)), juce::Justification::centred);
        }

    private:
        void timerCallback() override
        {
            if (! visibleInWindow (*this)) return;
            if (hold) return;
            const float m = proc.tune.detectedMidi.load();
            midi = m;
            cents = m < 0 ? 0.0f : (m - (float) juce::roundToInt (m)) * 100.0f;
            correction = proc.tune.correctionCents.load();
            repaint();
        }
        KaminariVocalProcessor& proc;
        float midi = -1, cents = 0, correction = 0;
    };

    class Piano : public juce::Component, private juce::Timer
    {
    public:
        explicit Piano (KaminariVocalProcessor& p) : proc (p) { setTitle ("Scale notes"); startTimerHz (10); }
        ~Piano() override { stopTimer(); }
        static constexpr int lo = 36, hi = 84;
        static bool isBlack (int m) { const int pc = m % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

        void paint (juce::Graphics& g) override
        {
            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            g.setColour (navy950);
            g.fillRect (getLocalBounds());
            g.setColour (navy600);
            g.fillRect (0, 0, getWidth(), 3);
            const int cur = currentNote();
            for (int pass = 0; pass < 2; ++pass)
                for (int m = lo; m <= hi; ++m)
                {
                    if (isBlack (m) != (pass == 1)) continue;
                    const auto r = keyRect (m);
                    const bool inc = included (m % 12);
                    juce::Colour fill = isBlack (m) ? (inc ? juce::Colour (0xff2b3550) : juce::Colour (0xff0b111e))
                                                    : (inc ? juce::Colour (0xffdce3f0) : juce::Colour (0xff6b7489));
                    if (m == cur) fill = accent;
                    g.setColour (fill);
                    g.fillRoundedRectangle (r.toFloat(), 3.0f);
                    if (isBlack (m)) { g.setColour (navy950); g.drawRoundedRectangle (r.toFloat(), 3.0f, 1.0f); }
                    g.setColour (isBlack (m) ? (inc ? mist : navy600) : navy950);
                    g.setFont (font (isBlack (m) ? 8.0f : 10.0f, m % 12 == 0 ? 2 : 1));
                    const juce::String label = m % 12 == 0 ? juce::String ("C") + juce::String (m / 12 - 1) : juce::String (names[m % 12]);
                    g.drawText (label, r.withTrimmedBottom (4).removeFromBottom (12), juce::Justification::centred);
                }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            for (int pass = 1; pass >= 0; --pass)
                for (int m = lo; m <= hi; ++m)
                    if (isBlack (m) == (pass == 1) && keyRect (m).contains (e.getPosition()))
                    {
                        const int pc = m % 12;
                        setParamPlain (proc.apvts, "tn_note_" + juce::String (pc), included (pc) ? 0.0f : 1.0f);
                        setParamPlain (proc.apvts, "tn_scale", 10.0f);   // Custom
                        repaint();
                        return;
                    }
        }

        int countIncluded() const { int n = 0; for (int pc = 0; pc < 12; ++pc) n += included (pc) ? 1 : 0; return n; }

    private:
        bool included (int pc) const { return plain (proc.apvts, "tn_note_" + juce::String (pc)) > 0.5f; }
        int currentNote() const { const float m = proc.tune.detectedMidi.load(); return m < 0 ? -1 : juce::roundToInt (m); }
        juce::Rectangle<int> keyRect (int m) const
        {
            int whites = 0;
            for (int k = lo; k <= hi; ++k) whites += isBlack (k) ? 0 : 1;
            const float ww = (float) getWidth() / (float) whites;
            int idx = 0;
            for (int k = lo; k < m; ++k) idx += isBlack (k) ? 0 : 1;
            if (! isBlack (m))
                return juce::Rectangle<float> (idx * ww, 3.0f, ww - 1.5f, (float) getHeight() - 3.0f).toNearestInt();
            return juce::Rectangle<float> ((idx - 1) * ww + ww * 0.68f, 3.0f, ww * 0.62f, (getHeight() - 3.0f) * 0.6f).toNearestInt();
        }
        void timerCallback() override { if (visibleInWindow (*this)) repaint(); }
        KaminariVocalProcessor& proc;
    };

    // Tremolo options next to the tremolo knobs: tempo sync, waveform and onset.
    class TremoloOptions : public juce::Component
    {
    public:
        explicit TremoloOptions (APVTS& s)
            : sync (s, "tn_trem_sync", "SYNC", "Free rate, or one cycle per note value locked to the host tempo."),
              shape (s, "tn_trem_shape", { "SINE", "TRI", "SQR" }, "Waveform of the level change."),
              onset (s, "tn_trem_onset", "ONSET", "ONSET", "On: the tremolo fades in with each note, using the vibrato's Onset Delay and Rise.")
        {
            sync.caption.setFont (font (10.5f, 1, 0.1f));
            for (auto* c : std::initializer_list<juce::Component*> { &sync, &shape, &onset }) addAndMakeVisible (c);
        }
        void resized() override
        {
            auto b = getLocalBounds();
            sync.setBounds (b.removeFromTop (42));
            b.removeFromTop (6);
            shape.setBounds (b.removeFromTop (24));
            b.removeFromTop (6);
            onset.setBounds (b.removeFromTop (24));
        }
        ChoiceBox sync;
        SegParam shape;
        ToggleBox onset;
    };

    class TunePage : public AdvFrame, private juce::Timer
    {
    public:
        explicit TunePage (KaminariVocalProcessor& p)
            : AdvFrame (p, "Tune", "Real-time pitch correction", "tn_on", "tune"),
              range (p.apvts, "tn_range", "VOCAL RANGE", hintFor ("tn_range")),
              key (p.apvts, "tn_key", "KEY", hintFor ("tn_key")),
              scale (p.apvts, "tn_scale", "SCALE", hintFor ("tn_scale")),
              speed (p.apvts, "tn_speed", "Retune Speed", {}, {}, hintFor ("tn_speed")),
              humanize (p.apvts, "tn_humanize", "Humanize", {}, {}, hintFor ("tn_humanize")),
              meter (p), piano (p),
              correct (p.apvts, "tn_correct", "CORRECT PITCH", "CORRECT PITCH",
                       "On: notes are pulled to the key and scale. Off: no retuning; vibrato and tremolo still work."),
              vibPower (p.apvts, "tn_vib_on", "Vibrato"), tremPower (p.apvts, "tn_trem_on", "Tremolo"),
              vibDepth (p.apvts, "tn_vib_depth", "Depth", "0", "100 CT", "Pitch swing above and below the note, in cents."),
              vibRate (p.apvts, "tn_vib_rate", "Rate", "SLOW", "FAST", "Vibrato speed. Sung vibrato is usually 5 to 7 Hz."),
              vibDelay (p.apvts, "tn_vib_delay", "Onset Delay", "0", "1.5 S", "Time after a note starts before the vibrato begins."),
              vibRise (p.apvts, "tn_vib_rise", "Onset Rise", "0", "1.5 S", "Time the vibrato takes to reach full depth."),
              vibVariation (p.apvts, "tn_vib_variation", "Variation", "STEADY", "HUMAN", "Lets rate and depth wander slightly, like a singer."),
              tremDepth (p.apvts, "tn_trem_depth", "Depth", "0", "100 %", "How far the level dips at each cycle (100 % = to silence)."),
              tremRate (p.apvts, "tn_trem_rate", "Rate", "SLOW", "FAST", "Tremolo speed when Sync is Free."),
              tremStereo (p.apvts, "tn_trem_stereo", "Stereo", "0", "180", "Phase between left and right. 180 degrees moves the vocal side to side (auto-pan)."),
              tremOptions (p.apvts),
              detune (p.apvts, "tn_detune", "DETUNE"),
              scaleAtt (*p.apvts.getParameter ("tn_scale"), [this] (float) { applyScale(); }),
              keyAtt (*p.apvts.getParameter ("tn_key"), [this] (float) { applyScale(); }),
              correctAtt (*p.apvts.getParameter ("tn_correct"), [this] (float) { updateVisibility(); }),
              syncAtt (*p.apvts.getParameter ("tn_trem_sync"), [this] (float) { updateVisibility(); })
        {
            for (auto* c : std::initializer_list<juce::Component*> { &range, &key, &scale, &speed, &humanize, &meter, &piano, &holdButton,
                                                                     &correct, &vibPower, &tremPower, &tremOptions, &detune })
                addAndMakeVisible (c);
            for (auto* k : { &vibDepth, &vibRate, &vibDelay, &vibRise, &vibVariation, &tremDepth, &tremRate, &tremStereo })
            {
                addAndMakeVisible (k);
                k->setLNF (&lnf);
                k->setLabelOverhang (2);
            }
            for (auto* cb : { &range, &key, &scale })
            {
                cb->caption.setFont (font (10.5f, 1, 0.1f));
                cb->caption.setText (cb->caption.getText().toUpperCase(), juce::dontSendNotification);
            }
            holdButton.setButtonText ("Hold display");
            holdButton.setClickingTogglesState (true);
            holdButton.setTooltip ("Freezes the pitch meter.");
            holdButton.onClick = [this] { meter.hold = holdButton.getToggleState(); };
            correctAtt.sendInitialUpdate();
            detune.setStripCaption (true);
            detune.box.setTooltip ("Detune: moves every target note by up to 100 cents (shown as the A4 reference).\n"
                                   "With Correct Pitch off it shifts sung notes by this amount. Drag up or down, double-click to type.");
            startTimerHz (8);
        }
        ~TunePage() override
        {
            stopTimer();
            for (auto* k : { &vibDepth, &vibRate, &vibDelay, &vibRise, &vibVariation, &tremDepth, &tremRate, &tremStereo })
                k->setLNF (nullptr);
        }

        void updateVisibility()
        {
            const bool on = choiceIndex (proc.apvts, "tn_correct") != 0;
            speed.setVisible (on);
            humanize.setVisible (on);
            tremRate.setVisible (choiceIndex (proc.apvts, "tn_trem_sync") == 0);
            if (! getBounds().isEmpty()) resized();
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            AdvFrame::paint (g);
            drawGroup (g, strip, navy800);
            g.setColour (mist);
            g.setFont (font (10.5f, 1, 0.1f));
            g.drawText ("LATENCY", latencyArea.getX(), strip.getY() + 10, latencyArea.getWidth(), 14, juce::Justification::centred);
            g.drawText ("TRACKING", strip.getRight() - 106, strip.getY() + 10, 90, 14, juce::Justification::centred);
            const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
            const int lat = kv::Tune::latencyFor (sr);
            g.setColour (white);
            g.setFont (font (12.5f, 0));
            g.drawText (juce::String (lat) + " smp" + dot() + juce::String (1000.0 * lat / sr, 1) + " ms",
                        latencyArea.getX(), strip.getY() + 28, latencyArea.getWidth(), 16, juce::Justification::centred);
            g.setColour (voiced ? accent : navy600);
            g.fillEllipse ((float) strip.getRight() - 90.0f, (float) strip.getY() + 32.0f, 9.0f, 9.0f);
            g.setColour (white);
            g.drawText (voiced ? "Voiced" : "Unvoiced", strip.getRight() - 76, strip.getY() + 28, 70, 16, juce::Justification::centredLeft);
            g.setColour (mist);
            g.setFont (font (11.5f, 0));
            if (speed.isVisible())
            {
                g.drawText ("0 ms = instant, hard-tuned sound", speed.getBounds().translated (0, speed.getHeight() + 2).withHeight (16).expanded (30, 0), juce::Justification::centred);
                g.drawText ("Keeps held notes natural", humanize.getBounds().translated (0, humanize.getHeight() + 2).withHeight (16).expanded (30, 0), juce::Justification::centred);
            }
            else
            {
                g.drawFittedText ("Pitch correction is off. Notes are not retuned; vibrato and tremolo still work.",
                                  speed.getBounds().withWidth (170).translated (-10, 30), juce::Justification::centredTop, 3, 1.0f);
            }

            // vibrato and tremolo groups: power, title, live read-out
            for (auto* grp : { &vibGrp, &tremGrp })
            {
                if (grp->area.isEmpty()) continue;
                drawGroup (g, grp->area, navy950.interpolatedWith (navy900, 0.5f));
                g.setColour (white);
                g.setFont (font (12.0f, 2, 0.12f));
                g.drawText (grp->title, grp->area.getX() + 40, grp->area.getY() + 7, 120, 18, juce::Justification::centredLeft);
                g.setColour (accent);
                g.setFont (font (11.5f, 0));
                g.drawText (grp == &vibGrp ? vibText : tremText, grp->area.getRight() - 170, grp->area.getY() + 7, 158, 18, juce::Justification::centredRight);
            }
            auto legend = juce::Rectangle<int> (piano.getX(), piano.getY() - 24, piano.getWidth(), 18);
            g.drawText ("Scale notes: click a key to include or exclude it" + dot() + juce::String (piano.countIncluded()) + " of 12" + dot()
                        + "editing switches Scale to Custom", legend, juce::Justification::centredLeft);
            int x = legend.getRight();
            for (auto [c, t] : { std::pair { accent, "current note" }, { juce::Colour (0xff6b7489), "excluded" }, { juce::Colour (0xffdce3f0), "included" } })
            {
                const int w = (int) juce::GlyphArrangement::getStringWidth (font (11.5f, 0), t) + 18;
                x -= w + 10;
                g.setColour (c);
                g.fillRoundedRectangle ((float) x, (float) legend.getCentreY() - 5.0f, 10.0f, 10.0f, 2.0f);
                g.setColour (mist);
                g.drawText (t, x + 14, legend.getY(), w, legend.getHeight(), juce::Justification::centredLeft);
            }
        }

        void layoutContent (juce::Rectangle<int> b) override
        {
            strip = b.removeFromTop (60);
            auto s = strip.reduced (12, 6);
            range.setBounds (s.removeFromLeft (128)); s.removeFromLeft (12);
            key.setBounds (s.removeFromLeft (70)); s.removeFromLeft (12);
            scale.setBounds (s.removeFromLeft (140)); s.removeFromLeft (12);
            detune.setBounds (s.removeFromLeft (140).withTrimmedTop (2));
            latencyArea = { detune.getRight() + 10, strip.getY(), strip.getRight() - 110 - (detune.getRight() + 10), strip.getHeight() };
            piano.setBounds (b.removeFromBottom (72));
            b.removeFromBottom (28);
            layoutGroups (b.removeFromBottom (138), { &vibGrp, &tremGrp }, 92);
            for (auto [grp, pw] : { std::pair { &vibGrp, (juce::Component*) &vibPower }, { &tremGrp, (juce::Component*) &tremPower } })
                pw->setBounds (grp->area.getX() + 10, grp->area.getY() + 5, 22, 22);
            b.removeFromBottom (8);
            auto mid = b.reduced (30, 4);
            auto left = mid.removeFromLeft (170);
            correct.setBounds (left.removeFromTop (26).withSizeKeepingCentre (140, 26));
            speed.setBounds (left.withSizeKeepingCentre (130, juce::jmin (left.getHeight() - 18, 130)));
            auto right = mid.removeFromRight (170);
            right.removeFromTop (26);
            humanize.setBounds (right.withSizeKeepingCentre (130, juce::jmin (right.getHeight() - 18, 130)));
            auto centre = mid.withSizeKeepingCentre (300, mid.getHeight());
            holdButton.setBounds (centre.removeFromBottom (26).withSizeKeepingCentre (110, 24));
            meter.setBounds (centre);
        }

    private:
        void timerCallback() override
        {
            if (! visibleInWindow (*this)) return;
            const bool v = proc.tune.detectedMidi.load() >= 0;
            if (v != voiced) { voiced = v; repaint (strip); }
            const bool vibOn = choiceIndex (proc.apvts, "tn_vib_on") != 0, tremOn = choiceIndex (proc.apvts, "tn_trem_on") != 0;
            const float vc = proc.tune.vibratoCents.load(), tg = proc.tune.tremoloGain.load();
            const auto vt = vibOn ? (std::abs (vc) < 0.5f ? juce::String ("waiting for a note") : (vc > 0 ? "+" : "") + juce::String (juce::roundToInt (vc)) + " ct now") : juce::String ("off");
            const auto tt = tremOn ? juce::String (juce::Decibels::gainToDecibels (tg, -60.0f), 1) + " dB now" : juce::String ("off");
            if (vt != vibText || tt != tremText) { vibText = vt; tremText = tt; repaint (vibGrp.area.getUnion (tremGrp.area)); }
        }

        void applyScale()
        {
            const int sc = juce::roundToInt (plain (proc.apvts, "tn_scale"));
            auto* sp = proc.apvts.getParameter ("tn_scale");
            auto* kp = proc.apvts.getParameter ("tn_key");
            const int s2 = juce::roundToInt (sp->convertFrom0to1 (sp->getValue()));
            juce::ignoreUnused (sc);
            if (s2 >= 10) return;
            bool on[12];
            kv::scaleNotes (juce::roundToInt (kp->convertFrom0to1 (kp->getValue())), s2, on);
            for (int n = 0; n < 12; ++n)
            {
                auto* p = proc.apvts.getParameter ("tn_note_" + juce::String (n));
                if ((p->getValue() > 0.5f) != on[n]) { p->beginChangeGesture(); p->setValueNotifyingHost (on[n] ? 1.0f : 0.0f); p->endChangeGesture(); }
            }
        }

        ChoiceBox range, key, scale;
        RangeKnob speed, humanize;
        CentsMeter meter;
        Piano piano;
        juce::TextButton holdButton;
        ModuleLNF lnf;

    public:
        ToggleBox correct;
        PowerButton vibPower, tremPower;
        RangeKnob vibDepth, vibRate, vibDelay, vibRise, vibVariation, tremDepth, tremRate, tremStereo;
        TremoloOptions tremOptions;
        Field detune;

    private:
        SendGroup vibGrp { "VIBRATO", { &vibDepth, &vibRate, &vibDelay, &vibRise, &vibVariation } },
                  tremGrp { "TREMOLO", { &tremDepth, &tremRate, &tremStereo }, nullptr, 0, &tremOptions, 130, 108, 4 };
        juce::String vibText, tremText;
        juce::Rectangle<int> strip, latencyArea;
        bool voiced = false;
        juce::ParameterAttachment scaleAtt, keyAtt, correctAtt, syncAtt;
    };

    //==================================================================================================================
    // EQ
    // Keyboard under the EQ graph, on the graph's frequency axis and drawn like the Tune page's keyboard.
    // Click a key or drag along the keys to sweep the selected band from note to note (Shift: no snapping).
    // The selected band's note is lit; notes holding other bands show a dot in the band's colour.
    class EqPiano : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        EqPiano (KaminariVocalProcessor& p, EqCurve& c) : proc (p), curve (c)
        {
            setTitle ("EQ keyboard");
            setTooltip ("Click or drag along the keys to move the selected band from note to note. Hold Shift for a free sweep.");
            startTimerHz (10);
        }
        ~EqPiano() override { stopTimer(); }
        int band = 0;

        void paint (juce::Graphics& g) override
        {
            g.setColour (navy950);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
            g.setColour (navy600);
            g.fillRect (0, 0, getWidth(), 3);
            const int cur = band >= 0 ? noteOfBand (band) : -1000;
            for (int pass = 0; pass < 2; ++pass)
                for (int m = loNote(); m <= hiNote(); ++m)
                {
                    if (Piano::isBlack (m) != (pass == 1)) continue;
                    const auto r = keyRect (m);
                    if (r.getRight() < 0 || r.getX() > getWidth()) continue;
                    const bool black = Piano::isBlack (m);
                    juce::Colour fill = black ? juce::Colour (0xff1a2236) : juce::Colour (0xffdce3f0);
                    if (m == hover) fill = black ? juce::Colour (0xff2b4a82) : juce::Colour (0xffb8f3ff);
                    if (m == cur && band >= 0) fill = EqCurve::bandColour (band);
                    g.setColour (fill);
                    g.fillRoundedRectangle (r, black ? 2.0f : 3.0f);
                    if (black) { g.setColour (navy950); g.drawRoundedRectangle (r, 2.0f, 1.0f); }
                    else       { g.setColour (juce::Colour (0xff6b7489)); g.drawVerticalLine (juce::roundToInt (r.getRight()), r.getY(), r.getBottom()); }
                    if (! black && m % 12 == 0 && r.getWidth() > 9.0f)
                    {
                        g.setColour (m == cur ? navy950 : navy800);
                        g.setFont (font (9.5f, 2));
                        g.drawText ("C" + juce::String (m / 12 - 1), r.withTrimmedBottom (3.0f).removeFromBottom (11.0f).expanded (6.0f, 0.0f), juce::Justification::centred);
                    }
                }
            // other bands: a dot on their key
            for (int i = 0; i < kv::Equalizer::numBands; ++i)
            {
                if (i == band || ! used (i)) continue;
                const int m = noteOfBand (i);
                if (m < loNote() || m > hiNote()) continue;
                const auto r = keyRect (m);
                g.setColour (EqCurve::bandColour (i));
                g.fillEllipse (r.getCentreX() - 3.0f, r.getY() + 6.0f, 6.0f, 6.0f);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override { const int m = noteAt (e.position); if (m != hover) { hover = m; repaint(); } }
        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (band < 0 || ! used (band)) return;
            auto* p = proc.apvts.getParameter (curve.getPrefix() + juce::String (band + 1) + "_freq");
            p->beginChangeGesture();
            sweeping = true;
            sweepTo (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override { if (sweeping) sweepTo (e); }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (! sweeping) return;
            proc.apvts.getParameter (curve.getPrefix() + juce::String (band + 1) + "_freq")->endChangeGesture();
            sweeping = false;
        }

    private:
        static double freqOf (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }
        int loNote() const { return juce::roundToInt (std::ceil (69.0 + 12.0 * std::log2 (20.0 / 440.0))); }
        int hiNote() const { return juce::roundToInt (std::floor (69.0 + 12.0 * std::log2 (20000.0 / 440.0))); }
        float xOf (double midi) const { return curve.xForFreq (freqOf (midi)); }

        // White keys are contiguous; black keys sit on top, centred on their note.
        juce::Rectangle<float> keyRect (int m) const
        {
            const float h = (float) getHeight() - 3.0f;
            if (Piano::isBlack (m))
            {
                const float w = (xOf (m + 0.5) - xOf (m - 0.5)) * 0.85f;
                return { xOf (m) - w * 0.5f, 3.0f, w, h * 0.58f };
            }
            const float l = xOf (m - (Piano::isBlack (m - 1) ? 1.0 : 0.5)), r = xOf (m + (Piano::isBlack (m + 1) ? 1.0 : 0.5));
            return { l, 3.0f, r - l, h };
        }

        int noteAt (juce::Point<float> p) const
        {
            for (int pass = 1; pass >= 0; --pass)
                for (int m = loNote(); m <= hiNote(); ++m)
                    if (Piano::isBlack (m) == (pass == 1) && keyRect (m).contains (p))
                        return m;
            return -1;
        }

        bool used (int i) const { return plain (proc.apvts, curve.getPrefix() + juce::String (i + 1) + "_used") > 0.5f; }
        int noteOfBand (int i) const
        {
            const float f = plain (proc.apvts, curve.getPrefix() + juce::String (i + 1) + "_freq");
            return juce::roundToInt (69.0 + 12.0 * std::log2 (f / 440.0));
        }

        void sweepTo (const juce::MouseEvent& e)
        {
            double f = curve.freqForX (e.position.x);
            if (! e.mods.isShiftDown())
            {
                const int m = noteAt ({ juce::jlimit (0.0f, (float) getWidth() - 1.0f, e.position.x), juce::jlimit (4.0f, (float) getHeight() - 2.0f, e.position.y) });
                if (m >= 0) f = freqOf (m);
            }
            auto* p = proc.apvts.getParameter (curve.getPrefix() + juce::String (band + 1) + "_freq");
            p->setValueNotifyingHost (p->convertTo0to1 ((float) f));
            hover = noteAt (e.position);
            repaint();
        }

        void timerCallback() override { if (visibleInWindow (*this)) repaint(); }
        KaminariVocalProcessor& proc;
        EqCurve& curve;
        int hover = -1;
        bool sweeping = false;
    };

    // Floating panel for the selected band of any EQ (power, shape, slope, FREQ / GAIN / Q, solo, band navigation, delete).
    struct EqBandPanel : juce::Component
    {
        void build (KaminariVocalProcessor& p, int eqTarget, int band, juce::LookAndFeel& l, std::function<void (int)> nav)
        {
            const juce::String pre = KaminariVocalProcessor::eqPrefix (eqTarget) + juce::String (band + 1) + "_";
            target = eqTarget;
            removeAllChildren();
            bandNo = band;
            power = std::make_unique<PowerButton> (p.apvts, pre + "on", "Band " + juce::String (band + 1));
            type = std::make_unique<ChoiceBox> (p.apvts, (pre + "type").toRawUTF8(), "", hintFor ("_type"));
            slope = std::make_unique<ChoiceBox> (p.apvts, (pre + "slope").toRawUTF8(), "", hintFor ("_slope"));
            freq = std::make_unique<RangeKnob> (p.apvts, pre + "freq", "Freq", "10 Hz", "30 kHz");
            gain = std::make_unique<RangeKnob> (p.apvts, pre + "gain", "Gain", "-30", "+30");
            q = std::make_unique<RangeKnob> (p.apvts, pre + "q", "Q", "0.025", "40");
            auto* fp = p.apvts.getParameter (pre + "freq");
            // a cut filter picked from the list starts from a flat (Butterworth) corner instead of the bell's Q of 1
            type->onUserChange = [&p, pre] (int t)
            {
                if ((t == kv::LowCut || t == kv::HighCut) && std::abs (plain (p.apvts, pre + "q") - 1.0f) < 0.001f)
                    setParamPlain (p.apvts, pre + "q", 0.71f);
            };
            freq->setValueText ([fp, this]
            {
                const float f = fp->convertFrom0to1 (fp->getValue());
                if (! (showNotes && showNotes())) return kvp::freqText (f);   // note and cents only with the keyboard shown
                const float m = 69.0f + 12.0f * std::log2 (f / 440.0f);
                const int n = juce::roundToInt (m);
                return kvp::freqText (f) + dot() + noteName (n) + " " + (m - n >= 0 ? "+" : "") + juce::String (juce::roundToInt ((m - n) * 100)) + " ct";
            });
            for (auto* k : { freq.get(), gain.get(), q.get() }) k->setLNF (&l);
            prev.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xb9")));
            next.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xba")));
            del.setButtonText ("x");
            prev.onClick = [nav] { nav (-1); };
            next.onClick = [nav] { nav (1); };
            del.setTooltip ("Remove this band");
            del.onClick = [&p, pre] { setParamPlain (p.apvts, pre + "used", 0.0f); };
            use.setButtonText ("Add band");
            use.onClick = [&p, pre] { setParamPlain (p.apvts, pre + "used", 1.0f); };
            solo.setButtonText ("SOLO");
            solo.setClickingTogglesState (true);
            solo.setToggleState (p.eqSoloFor (eqTarget).load() == band, juce::dontSendNotification);
            solo.setTooltip ("Hear only what this band works on: around a bell, below a low shelf or low cut, above a high shelf or high cut.");
            solo.onClick = [&p, band, this] { p.eqSoloFor (target).store (solo.getToggleState() ? band : -1); };
            proc = &p;
            for (auto* c : std::initializer_list<juce::Component*> { power.get(), type.get(), slope.get(), freq.get(), gain.get(), q.get(), &prev, &next, &del, &use, &solo })
                addAndMakeVisible (c);
            usedId = pre + "used";
            state = &p.apvts;
            resized();
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (navy800.withAlpha (0.95f));
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
            g.setColour (accent);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);
            g.setColour (white);
            g.setFont (font (14.0f, 1));
            g.drawText (juce::String (bandNo + 1), prev.getRight(), next.getY(), next.getX() - prev.getRight(), next.getHeight(), juce::Justification::centred);
        }
        void resized() override
        {
            if (power == nullptr) return;
            const bool used = state != nullptr && state->getRawParameterValue (usedId)->load() > 0.5f;
            for (auto* c : std::initializer_list<juce::Component*> { power.get(), type.get(), slope.get(), freq.get(), gain.get(), q.get(), &del, &solo })
                c->setVisible (used);
            if (proc != nullptr) solo.setToggleState (proc->eqSoloFor (target).load() == bandNo, juce::dontSendNotification);
            use.setVisible (false);   // an unused band's panel is never shown (double-click the graph to add one)
            auto b = getLocalBounds().reduced (12, 10);
            auto left = b.removeFromLeft (96);
            power->setBounds (left.removeFromTop (28).removeFromLeft (28));
            left.removeFromTop (6);
            type->setBounds (left.removeFromTop (30).withTrimmedTop (-14));
            left.removeFromTop (6);
            slope->setBounds (left.removeFromTop (30).withTrimmedTop (-14));
            auto right = b.removeFromRight (112);
            auto nav = right.removeFromTop (28);
            prev.setBounds (nav.removeFromLeft (26));
            del.setBounds (nav.removeFromRight (26));
            nav.removeFromRight (6);
            next.setBounds (nav.removeFromRight (26));
            right.removeFromTop (10);
            solo.setBounds (right.removeFromTop (26));
            use.setBounds (getLocalBounds().withSizeKeepingCentre (140, 30));
            const int w = b.getWidth() / 3;
            freq->setBounds (b.removeFromLeft (w));
            gain->setBounds (b.removeFromLeft (w));
            q->setBounds (b);
        }
        std::unique_ptr<PowerButton> power;
        std::unique_ptr<ChoiceBox> type, slope;
        std::unique_ptr<RangeKnob> freq, gain, q;
        juce::TextButton prev, next, del, use, solo;
        KaminariVocalProcessor* proc = nullptr;
        int bandNo = 0, target = 0;
        juce::String usedId;
        APVTS* state = nullptr;
        std::function<bool()> showNotes;   // note and cents beside the frequency (set by the editor: keyboard shown)
    };

    // The EQ editor used by every EQ in the plug-in: graph (EqCurve), keyboard (EqPiano), floating band panel and a
    // bar with the keyboard switch, Pre / Post analyzer toggles (both can be on), resolution and speed. `extra`
    // (optional) sits at the right end of the bar, e.g. the main EQ's output knob.
    class EqEditor : public juce::Component, private juce::Timer
    {
    public:
        EqEditor (KaminariVocalProcessor& p, int eqTarget, juce::Component* extraControl = nullptr)
            : proc (p), target (eqTarget), curve (p, eqTarget), piano (p, curve),
              analyser ([&p, eqTarget] { return p.eqAnalyserModeFor (eqTarget).load(); },
                        [&p, eqTarget] (int m) { p.eqAnalyserModeFor (eqTarget).store (m); }, curve.preName(), curve.postName()),
              pianoToggle ("Piano"), extra (extraControl)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &curve, &panel, &analyser, &resolution, &speed, &pianoToggle })
                addAndMakeVisible (c);
            addChildComponent (piano);
            if (extra != nullptr) addAndMakeVisible (extra);
            resolution.addItemList (SpectrumProcessor::resolutionNames(), 1);
            speed.addItemList (SpectrumProcessor::speedNames(), 1);
            resolution.setSelectedId (p.analyserResolution.load() + 1, juce::dontSendNotification);
            speed.setSelectedId (p.analyserSpeed.load() + 1, juce::dontSendNotification);
            resolution.setTooltip ("Analyzer resolution (FFT size).");
            speed.setTooltip ("Analyzer fall-back speed.");
            resolution.onChange = [this] { proc.analyserResolution.store (resolution.getSelectedId() - 1); };
            speed.onChange = [this] { proc.analyserSpeed.store (speed.getSelectedId() - 1); };
            pianoToggle.setClickingTogglesState (true);
            // the side-chain EQ starts without the keyboard (it shapes the detector, notes matter less there)
            const bool pianoOn = eqTarget != KaminariVocalProcessor::EqSideChain;
            pianoToggle.setToggleState (pianoOn, juce::dontSendNotification);
            pianoToggle.setTooltip ("Show the keyboard under the graph: click or drag along the keys to sweep the selected band from note to note.");
            pianoToggle.onClick = [this] { piano.setVisible (pianoToggle.getToggleState()); select (selected); resized(); repaint(); };
            piano.setVisible (pianoOn);
            curve.onSelect = [this] (int b) { select (b); };
            panel.showNotes = [this] { return pianoToggle.getToggleState(); };
            select (firstUsed());
            startTimerHz (6);
        }
        ~EqEditor() override { stopTimer(); proc.eqSoloFor (target).store (-1); }

        // band < 0: no band selected (a click on empty graph space): the band settings close
        void select (int band)
        {
            if (band >= 0 && plain (proc.apvts, curve.getPrefix() + juce::String (juce::jlimit (0, 7, band) + 1) + "_used") < 0.5f)
                band = -1;   // an unused band has no settings to show
            if (band < 0)
            {
                selected = -1;
                curve.selected = -1;
                piano.band = -1;
                proc.eqSoloFor (target).store (-1);
                panel.setVisible (false);
                repaint();
                return;
            }
            selected = juce::jlimit (0, 7, band);
            curve.selected = selected;
            piano.band = selected;
            auto& solo = proc.eqSoloFor (target);
            if (solo.load() >= 0) solo.store (selected);
            panel.setVisible (true);
            panel.build (proc, target, selected, lnf, [this] (int d)
            {
                // step to the next band in use
                for (int k = 1; k <= 8; ++k)
                {
                    const int c = ((selected + d * k) % 8 + 8) % 8;
                    if (plain (proc.apvts, curve.getPrefix() + juce::String (c + 1) + "_used") > 0.5f) { select (c); return; }
                }
            });
            resized();
        }
        int selectedBand() const noexcept { return selected; }

        void paint (juce::Graphics& g) override
        {
            drawGroup (g, bar, navy900);
            g.setColour (mist);
            g.setFont (font (12.0f, 0));
            g.drawText ("Analyzer", analyser.getX() - 64, bar.getY(), 58, bar.getHeight(), juce::Justification::centredRight);
            g.drawText ("Resolution", resolution.getX() - 70, bar.getY(), 64, bar.getHeight(), juce::Justification::centredRight);
            g.drawText ("Speed", speed.getX() - 46, bar.getY(), 40, bar.getHeight(), juce::Justification::centredRight);
        }

        void resized() override
        {
            auto b = getLocalBounds();
            bar = b.removeFromBottom (34);
            b.removeFromBottom (8);
            if (pianoToggle.getToggleState()) { piano.setBounds (b.removeFromBottom (juce::jmin (46, b.getHeight() / 5))); b.removeFromBottom (2); }
            curve.setBounds (b);
            panel.setBounds (b.withSizeKeepingCentre (juce::jmin (540, b.getWidth() - 20), 128).withY (b.getBottom() - 140));
            auto r = bar.reduced (8, 4);
            pianoToggle.setBounds (r.removeFromLeft (64));
            r.removeFromLeft (76);
            analyser.setBounds (r.removeFromLeft (160));
            r.removeFromLeft (76);
            resolution.setBounds (r.removeFromLeft (90));
            r.removeFromLeft (56);
            speed.setBounds (r.removeFromLeft (90));
            if (extra != nullptr) extra->setBounds (r.removeFromRight (150).withTrimmedTop (-2));
        }

        KaminariVocalProcessor& proc;
        int target;
        ModuleLNF lnf;
        EqCurve curve;
        EqPiano piano;
        EqBandPanel panel;
        AnalyzerToggles analyser;
        juce::ComboBox resolution, speed;
        juce::TextButton pianoToggle;

    private:
        int firstUsed() const
        {
            for (int i = 0; i < 8; ++i)
                if (plain (proc.apvts, curve.getPrefix() + juce::String (i + 1) + "_used") > 0.5f) return i;
            return -1;
        }
        void timerCallback() override
        {
            if (! visibleInWindow (*this)) return;
            // a band deleted from its panel (or by a preset) closes the settings
            if (selected >= 0 && plain (proc.apvts, curve.getPrefix() + juce::String (selected + 1) + "_used") < 0.5f) { select (-1); return; }
            panel.resized();
        }
        juce::Component* extra = nullptr;
        juce::Rectangle<int> bar;
        int selected = 0;
    };

    class EqPage : public AdvFrame
    {
    public:
        explicit EqPage (KaminariVocalProcessor& p)
            : AdvFrame (p, "EQ", "8 bands" + dot() + "zero latency" + dot() + "double-click to add a band, drag nodes, wheel = Q", "eq_on", "eq"),
              out (p.apvts, "eq_out_gain", "Output"), editor (p, KaminariVocalProcessor::EqMain, &out),
              curve (editor.curve), piano (editor.piano)
        {
            addAndMakeVisible (editor);
        }

        void select (int band) { editor.select (band); }
        void layoutContent (juce::Rectangle<int> b) override { editor.setBounds (b); }

        Knob out;
        EqEditor editor;
        EqCurve& curve;
        EqPiano& piano;
    };

    //==================================================================================================================
    // MULTIBAND
    // Band colours are the EQ's, so band N looks the same on both graphs.
    inline juce::Colour bandColour (int k) { return EqCurve::bandColour (k); }

    class MultibandDisplay : public juce::Component, private juce::Timer
    {
    public:
        explicit MultibandDisplay (KaminariVocalProcessor& p) : proc (p)
        {
            setTitle ("Multiband bands");
            setDescription ("Click empty space to add a band there; drag a band or its edges to move it.");
            for (int k = 0; k < 6; ++k)
            {
                const juce::String pre = "mb" + juce::String (k + 1) + "_";
                auto& b = ptrs[(size_t) k];
                b = { p.apvts.getRawParameterValue (pre + "lo"), p.apvts.getRawParameterValue (pre + "hi"), p.apvts.getRawParameterValue (pre + "gain"),
                      p.apvts.getRawParameterValue (pre + "mute"), p.apvts.getRawParameterValue (pre + "bypass"), p.apvts.getRawParameterValue (pre + "solo") };
            }
            countPtr = p.apvts.getRawParameterValue ("mb_count");
            slopePtr = p.apvts.getRawParameterValue ("mb_slope");
            onPtr = p.apvts.getRawParameterValue ("mb_on");
            setOpaque (true);
            startTimerHz (30);
        }
        ~MultibandDisplay() override { stopTimer(); }
        std::function<void (int)> onSelect;
        int selected = 0;
        int analyzerMode = AnalyzerPair::Both;

        float xFor (double f) const { return (float) (std::log (f / 20.0) / std::log (1000.0)) * getWidth(); }
        double fFor (float x) const { return 20.0 * std::pow (1000.0, juce::jlimit (0.0f, 1.0f, x / (float) getWidth())); }
        float yFor (float db) const { return getHeight() * 0.45f - db / 12.0f * getHeight() * 0.4f; }

        // Shape of a band on the graph: flat between its edges, falling off outside them as steeply as the crossovers.
        static float bandWeight (double f, double lo, double hi, int slopeIndex)
        {
            const double n = slopeIndex <= 0 ? 1.0 : (slopeIndex == 1 ? 2.0 : 4.0);
            return (float) (1.0 / std::sqrt ((1.0 + std::pow (lo / f, 2.0 * n)) * (1.0 + std::pow (f / hi, 2.0 * n))));
        }

        // Drawn like the EQ: each band is shaded between its curve (its gain change right now, plus its Gain) and 0 dB
        // in its colour, its range is lightly tinted, and a numbered node sits at its centre (the selected one with
        // lightning, as on the EQ).
        void paint (juce::Graphics& g) override
        {
            g.fillAll (navy900);   // opaque: the page is not redrawn behind the graph every frame
            g.setColour (navy950);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
            const float plotBottom = (float) getHeight() - 18.0f;
            analyzers.draw (g, getLocalBounds().toFloat().withTrimmedBottom (18.0f), [this] (float x) { return fFor (x); }, analyzerMode,
                            "IN", "OUT");
            g.setColour (navy800);
            for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
                g.drawVerticalLine (juce::roundToInt (xFor (f)), 0.0f, (float) getHeight());
            g.setColour (mist);
            g.setFont (font (11.0f, 0));
            for (auto [f, t] : { std::pair { 50.0, "50" }, { 100.0, "100" }, { 200.0, "200" }, { 500.0, "500" }, { 1000.0, "1k" }, { 2000.0, "2k" }, { 5000.0, "5k" }, { 10000.0, "10k" } })
                g.drawText (t, (int) xFor (f) - 15, getHeight() - 16, 30, 14, juce::Justification::centred);
            for (int db : { 6, 3, 0, -3, -6 })
            {
                g.setColour (db == 0 ? navy600 : navy800);
                g.drawHorizontalLine (juce::roundToInt (yFor ((float) db)), 0.0f, (float) getWidth());
                g.setColour (mist);
                g.drawText ((db > 0 ? "+" : "") + juce::String (db), getWidth() - 30, (int) yFor ((float) db) - 7, 26, 14, juce::Justification::centredRight);
            }
            const int count = juce::jlimit (0, 6, juce::roundToInt (countPtr->load()));
            const int slope = juce::roundToInt (slopePtr->load());
            const bool on = onPtr->load() > 0.5f;
            const float y0 = yFor (0.0f);
            for (int k = 0; k < count; ++k)
            {
                const auto& b = ptrs[(size_t) k];
                const double lo = b.lo->load(), hi = std::max (lo * 1.01, (double) b.hi->load());
                const float x0 = xFor (lo), x1 = xFor (hi);
                const bool muted = b.mute->load() > 0.5f, bypassed = b.bypass->load() > 0.5f;
                const bool live = on && ! muted && ! bypassed;
                const auto col = live ? bandColour (k) : steel;
                // the band's range, lightly tinted, with its edges
                g.setColour (col.withAlpha (k == selected ? 0.08f : 0.04f));
                g.fillRect (juce::Rectangle<float> (x0, 0, x1 - x0, plotBottom));
                g.setColour (col.withAlpha (k == selected ? 0.55f : 0.3f));
                g.drawVerticalLine (juce::roundToInt (x0), 0.0f, plotBottom);
                g.drawVerticalLine (juce::roundToInt (x1), 0.0f, plotBottom);
                // its own area between its curve and 0 dB (reduction below the 0 line, boost above)
                const float change = muted ? -12.0f : proc.mbBandChange[(size_t) k].load() + b.gain->load();
                juce::Path area;
                area.startNewSubPath (0.0f, y0);
                for (int x = 0; x <= getWidth(); x += 3)
                    area.lineTo ((float) x, yFor (change * bandWeight (fFor ((float) x), lo, hi, slope)));
                area.lineTo ((float) getWidth(), y0);
                area.closeSubPath();
                g.setColour (col.withAlpha (k == selected ? 0.26f : 0.12f));
                g.fillPath (area);
                if (k == selected)
                {
                    g.setColour (col.withAlpha (0.55f));
                    g.strokePath (area, juce::PathStrokeType (1.0f));
                }
                // numbered node at the band's centre
                const juce::Point<float> c { xFor (std::sqrt (lo * hi)), yFor (change) };
                const float r = k == selected ? 10.0f : 8.5f;
                kvfx::paintOrb (g, c, r, col, k == selected, live);
                if (k == selected && proc.hostTempo.animations.load (std::memory_order_relaxed)) nodeFx.paint (g, c, r, col, animMs);
                g.setColour (white);
                g.setFont (font (10.0f, 1));
                g.drawText (juce::String (k + 1), juce::Rectangle<float> (2 * r, 2 * r).withCentre (c.translated (0.0f, 0.5f)), juce::Justification::centred);
                const juce::String flag = b.solo->load() > 0.5f ? "SOLO" : (muted ? "MUTE" : (bypassed ? "BYPASS" : ""));
                if (flag.isNotEmpty())
                {
                    g.setColour (flag == "SOLO" ? amber : mist);
                    g.setFont (font (9.5f, 1));
                    g.drawText (flag, juce::Rectangle<float> (48.0f, 12.0f).withCentre ({ c.x, c.y - r - 10.0f }), juce::Justification::centred);
                }
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragEdge = -1; dragBand = -1;
            const int count = juce::roundToInt (plain (proc.apvts, "mb_count"));
            for (int k = 0; k < count; ++k)
            {
                const auto pre = "mb" + juce::String (k + 1) + "_";
                const float x0 = xFor (plain (proc.apvts, pre + "lo")), x1 = xFor (plain (proc.apvts, pre + "hi"));
                if (std::abs (e.position.x - x0) < 6) { dragBand = k; dragEdge = 0; break; }
                if (std::abs (e.position.x - x1) < 6) { dragBand = k; dragEdge = 1; break; }
                if (e.position.x > x0 && e.position.x < x1) { dragBand = k; dragEdge = 2; startLo = plain (proc.apvts, pre + "lo"); startHi = plain (proc.apvts, pre + "hi"); startX = e.position.x; break; }
            }
            if (dragBand < 0 && count < 6 && e.position.y < getHeight() - 18 && ! e.mods.isPopupMenu())
            {
                // empty space: a new band an octave wide around the click (other settings at their defaults)
                const juce::String pre = "mb" + juce::String (count + 1) + "_";
                for (auto* prm : proc.apvts.processor.getParameters())
                    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm); rp != nullptr && rp->getParameterID().startsWith (pre))
                    { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->getDefaultValue()); rp->endChangeGesture(); }
                const double f = fFor (e.position.x);
                setParamPlain (proc.apvts, pre + "lo", (float) juce::jlimit (20.0, 16000.0, f / std::sqrt (2.0)));
                setParamPlain (proc.apvts, pre + "hi", (float) juce::jlimit (40.0, 20000.0, f * std::sqrt (2.0)));
                setParamPlain (proc.apvts, "mb_count", (float) (count + 1));
                dragBand = count;
                dragEdge = 2;
                startLo = plain (proc.apvts, pre + "lo"); startHi = plain (proc.apvts, pre + "hi"); startX = e.position.x;
            }
            if (dragBand >= 0) { selected = dragBand; if (onSelect) onSelect (dragBand); }
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragBand < 0) return;
            const auto pre = "mb" + juce::String (dragBand + 1) + "_";
            auto set = [this, &pre] (const char* what, float v) { auto* p = proc.apvts.getParameter (pre + what); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
            if (dragEdge == 0) set ("lo", (float) fFor (e.position.x));
            else if (dragEdge == 1) set ("hi", (float) fFor (e.position.x));
            else
            {
                const float ratio = (float) (fFor (e.position.x) / fFor (startX));
                set ("lo", startLo * ratio);
                set ("hi", startHi * ratio);
            }
            repaint();   // follow the mouse at once
        }

    private:
        void timerCallback() override
        {
            if (! visibleInWindow (*this)) return;
            analyzers.update (proc, proc.mbAnalyserPre, proc.mbAnalyserPost, analyzerMode);
            animMs = juce::Time::getMillisecondCounterHiRes();
            if (selected >= 0 && selected < juce::roundToInt (countPtr->load()) && proc.hostTempo.animations.load (std::memory_order_relaxed))
                nodeFx.tick (animMs, 10.0f);
            repaint();
        }
        KaminariVocalProcessor& proc;
        AnalyzerPair analyzers;
        kvfx::NodeLightning nodeFx;
        double animMs = 0;
        struct BandPtrs { std::atomic<float>* lo, * hi, * gain, * mute, * bypass, * solo; };
        std::array<BandPtrs, 6> ptrs {};
        std::atomic<float>* countPtr = nullptr, * slopePtr = nullptr, * onPtr = nullptr;
        int dragBand = -1, dragEdge = -1;
        float startLo = 0, startHi = 0, startX = 0;
    };

    class MultibandPage : public AdvFrame, private juce::Timer
    {
    public:
        explicit MultibandPage (KaminariVocalProcessor& p)
            : AdvFrame (p, "Multiband", "Bands only where needed" + dot() + "compress or expand, downward or upward", "mb_on", "multiband"),
              display (p), slope (p.apvts, "mb_slope", { "6 dB/oct", "12 dB/oct", "24 dB/oct" }, "Crossover slope"),
              detector (p.apvts, "mb_detector", { "Peak", "Smooth" }),
              os (p.apvts, "mb_os", { "Off", "2x", "4x" }, "Oversampling: runs the bands at 2x or 4x the session rate. Adds latency while Multiband is on."),
              analyzer ([this] { return display.analyzerMode; }, [this] (int m) { display.analyzerMode = m; }, "In", "Out")
        {
            addAndMakeVisible (display);
            addAndMakeVisible (os);
            addAndMakeVisible (analyzer);
            addAndMakeVisible (panel);
            addAndMakeVisible (slope);
            addAndMakeVisible (detector);
            addAndMakeVisible (addBand);
            addAndMakeVisible (removeBand);
            addBand.setButtonText ("+ Add band");
            removeBand.setButtonText ("- Remove band");
            addBand.onClick = [this] { setParamPlain (proc.apvts, "mb_count", juce::jmin (6.0f, plain (proc.apvts, "mb_count") + 1.0f)); select (juce::roundToInt (plain (proc.apvts, "mb_count")) - 1); };
            removeBand.onClick = [this] { setParamPlain (proc.apvts, "mb_count", juce::jmax (1.0f, plain (proc.apvts, "mb_count") - 1.0f)); select (0); };
            display.onSelect = [this] (int k) { select (k); };
            select (0);
            startTimerHz (4);
        }
        ~MultibandPage() override { stopTimer(); }

        void select (int k)
        {
            display.selected = k;
            panel.build (proc, k, lnf);
            resized();
        }

        void paint (juce::Graphics& g) override
        {
            AdvFrame::paint (g);
            drawGroup (g, bar);
            g.setColour (mist);
            g.setFont (font (12.0f, 0));
            g.drawText ("Slope", slope.getX() - 44, bar.getY(), 40, bar.getHeight(), juce::Justification::centredRight);
            g.drawText ("Detector", detector.getX() - 60, bar.getY(), 56, bar.getHeight(), juce::Justification::centredRight);
            g.drawText ("Oversampling", os.getX() - 86, bar.getY(), 82, bar.getHeight(), juce::Justification::centredRight);
            g.drawText ("Analyzer", analyzer.getX() - 62, bar.getY(), 58, bar.getHeight(), juce::Justification::centredRight);
            g.setColour (steel);
            g.drawText ("click the display to add a band", display.getX() + 8, display.getY() + 22, 260, 14, juce::Justification::centredLeft);
        }

        void layoutContent (juce::Rectangle<int> b) override
        {
            bar = b.removeFromBottom (34);
            b.removeFromBottom (8);
            display.setBounds (b);
            panel.setBounds (b.withSizeKeepingCentre (700, 150).withY (b.getBottom() - 170));
            auto r = bar.reduced (8, 4);
            addBand.setBounds (r.removeFromLeft (92));
            r.removeFromLeft (6);
            removeBand.setBounds (r.removeFromLeft (110));
            r.removeFromLeft (50);
            slope.setBounds (r.removeFromLeft (220));
            r.removeFromLeft (66);
            detector.setBounds (r.removeFromLeft (110));
            r.removeFromLeft (92);
            os.setBounds (r.removeFromLeft (110));
            analyzer.setBounds (r.removeFromRight (150));
        }

        ModuleLNF lnf;
        MultibandDisplay display;

        struct BandPanel : juce::Component
        {
            void build (KaminariVocalProcessor& p, int k, juce::LookAndFeel& l)
            {
                removeAllChildren();
                band = k;
                const juce::String pre = "mb" + juce::String (k + 1) + "_";
                thresh = std::make_unique<RangeKnob> (p.apvts, pre + "thresh", "Threshold", "-60 dB", "0 dB");
                range = std::make_unique<RangeKnob> (p.apvts, pre + "range", "Range", "-24", "+24");
                attack = std::make_unique<RangeKnob> (p.apvts, pre + "attack", "Attack", "1", "100");
                release = std::make_unique<RangeKnob> (p.apvts, pre + "release", "Release", "20", "1000");
                gain = std::make_unique<RangeKnob> (p.apvts, pre + "gain", "Output", "-24", "+24");
                mode = std::make_unique<SegParam> (p.apvts, pre + "mode", juce::StringArray { "COMPRESS", "EXPAND" });
                ratio = std::make_unique<HSlider> (p.apvts, pre + "ratio", "Ratio", "1:1", "10:1");
                knee = std::make_unique<HSlider> (p.apvts, pre + "knee", "Knee", "hard", "soft");
                lo = std::make_unique<Field> (p.apvts, pre + "lo", "Low edge");
                hi = std::make_unique<Field> (p.apvts, pre + "hi", "High edge");
                solo = std::make_unique<ToggleBox> (p.apvts, (pre + "solo").toRawUTF8(), "SOLO", "SOLO", hintFor ("_solo"));
                bypass = std::make_unique<ToggleBox> (p.apvts, (pre + "bypass").toRawUTF8(), "BYPASS", "BYPASS", "Passes this band unprocessed.");
                mute = std::make_unique<ToggleBox> (p.apvts, (pre + "mute").toRawUTF8(), "MUTE", "MUTE", "Removes this band from the output.");
                for (auto* kb : { thresh.get(), range.get(), attack.get(), release.get(), gain.get() }) kb->setLNF (&l);
                for (auto* c : std::initializer_list<juce::Component*> { thresh.get(), range.get(), attack.get(), release.get(), gain.get(), mode.get(),
                                                                         ratio.get(), knee.get(), lo.get(), hi.get(), solo.get(), bypass.get(), mute.get() })
                    addAndMakeVisible (c);
                resized();
            }
            void paint (juce::Graphics& g) override
            {
                g.setColour (navy800.withAlpha (0.96f));
                g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
                g.setColour (bandColour (band));
                g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.2f);
                g.setFont (font (13.0f, 2));
                g.drawText ("BAND " + juce::String (band + 1), 14, 8, 80, 18, juce::Justification::centredLeft);
            }
            void resized() override
            {
                if (thresh == nullptr) return;
                auto b = getLocalBounds().reduced (12, 8);
                b.removeFromTop (14);
                auto right = b.removeFromRight (190);
                lo->setBounds (right.removeFromTop (40).removeFromLeft (70));
                hi->setBounds (lo->getBounds().translated (78, 0));
                right.removeFromTop (8);
                auto toggles = right.removeFromTop (28);
                const int tw = (toggles.getWidth() - 8) / 3;
                solo->setBounds (toggles.removeFromLeft (tw));
                toggles.removeFromLeft (4);
                bypass->setBounds (toggles.removeFromLeft (tw));
                toggles.removeFromLeft (4);
                mute->setBounds (toggles);
                auto row = b.removeFromTop (78);
                const int w = row.getWidth() / 5;
                for (auto* kb : { thresh.get(), range.get(), attack.get(), release.get(), gain.get() }) kb->setBounds (row.removeFromLeft (w));
                b.removeFromTop (8);
                mode->setBounds (b.removeFromLeft (160).withHeight (26));
                b.removeFromLeft (20);
                ratio->setBounds (b.removeFromLeft (150).withHeight (44));
                b.removeFromLeft (20);
                knee->setBounds (b.removeFromLeft (150).withHeight (44));
            }
            std::unique_ptr<RangeKnob> thresh, range, attack, release, gain;
            std::unique_ptr<SegParam> mode;
            std::unique_ptr<HSlider> ratio, knee;
            std::unique_ptr<Field> lo, hi;
            std::unique_ptr<ToggleBox> solo, bypass, mute;
            int band = 0;
        } panel;

    private:
        // the band count can drop (preset, automation, Remove band): keep the panel on an existing band
        void timerCallback() override
        {
            if (! visibleInWindow (*this)) return;
            const int count = juce::roundToInt (plain (proc.apvts, "mb_count"));
            if (display.selected >= count) select (count - 1);
        }
        SegParam slope, detector, os;
        AnalyzerToggles analyzer;
        juce::TextButton addBand, removeBand;
        juce::Rectangle<int> bar;
    };

    //==================================================================================================================
    // Shared vertical meters (in / out / gain reduction) for the dynamics displays.
    inline void drawVMeter (juce::Graphics& g, juce::Rectangle<float> r, float db, bool fromTop, juce::Colour col)
    {
        g.setColour (navy800);
        g.fillRect (r);
        const float frac = juce::jlimit (0.0f, 1.0f, fromTop ? db / 24.0f : (db + 60.0f) / 60.0f);
        g.setColour (col);
        if (fromTop) g.fillRect (r.withHeight (r.getHeight() * frac));
        else g.fillRect (r.withTop (r.getBottom() - r.getHeight() * frac));
    }

    // COMPRESSION: scrolling level history (input area, output line, gain reduction from the top) and meters.
    class CompDisplay : public juce::Component, private juce::Timer
    {
    public:
        explicit CompDisplay (KaminariVocalProcessor& p) : proc (p) { setTitle ("Compression level display"); startTimerHz (30); }
        ~CompDisplay() override { stopTimer(); }

        float yFor (float db) const { return main.getY() + juce::jlimit (0.0f, 1.0f, -db / 60.0f) * main.getHeight(); }

        void paint (juce::Graphics& g) override
        {
            g.setColour (navy950);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
            auto b = getLocalBounds().reduced (4);
            meters = b.removeFromRight (56);
            main = b.reduced (4, 0);

            std::array<kv::LevelHistory<KaminariVocalProcessor::historySize>::Entry, (size_t) KaminariVocalProcessor::historySize> h;
            proc.compHistory.read (h);
            const int n = KaminariVocalProcessor::historySize;
            juce::Path inArea, outLine, grLine;
            inArea.startNewSubPath ((float) main.getX(), (float) main.getBottom());
            for (int i = 0; i < n; ++i)
            {
                const float x = main.getX() + main.getWidth() * (float) i / (n - 1);
                const float inDb = juce::Decibels::gainToDecibels (h[(size_t) i].in, -60.0f), outDb = juce::Decibels::gainToDecibels (h[(size_t) i].out, -60.0f);
                const float grY = main.getY() + juce::jlimit (0.0f, 1.0f, h[(size_t) i].gr / 24.0f) * main.getHeight();
                inArea.lineTo (x, yFor (inDb));
                if (i == 0) { outLine.startNewSubPath (x, yFor (outDb)); grLine.startNewSubPath (x, grY); }
                else { outLine.lineTo (x, yFor (outDb)); grLine.lineTo (x, grY); }
            }
            inArea.lineTo ((float) main.getRight(), (float) main.getBottom());
            inArea.closeSubPath();
            g.setColour (mist.withAlpha (0.22f));
            g.fillPath (inArea);
            g.setColour (white.withAlpha (0.65f));
            g.strokePath (outLine, juce::PathStrokeType (1.0f));
            g.setColour (accent);
            g.strokePath (grLine, juce::PathStrokeType (2.0f));
            g.setColour (mist);
            g.setFont (font (11.0f, 0));
            for (int db : { -10, -20, -30, -40, -50 })
                g.drawText (juce::String (db), main.getRight() - 30, (int) yFor ((float) db) - 7, 28, 14, juce::Justification::centredRight);
            // meters: in, out, gain reduction
            const auto& last = h[(size_t) n - 1];
            auto m = meters.toFloat().reduced (4, 2);
            const float w = (m.getWidth() - 8) / 3;
            drawVMeter (g, m.removeFromLeft (w), juce::Decibels::gainToDecibels (last.in, -60.0f), false, mist);
            m.removeFromLeft (4);
            drawVMeter (g, m.removeFromLeft (w), juce::Decibels::gainToDecibels (last.out, -60.0f), false, white);
            m.removeFromLeft (4);
            drawVMeter (g, m, proc.moduleGr[KaminariVocalProcessor::ModCompression].load(), true, accent);
        }

    private:
        void timerCallback() override { if (visibleInWindow (*this)) repaint(); }
        KaminariVocalProcessor& proc;
        juce::Rectangle<int> main, meters;
    };

    // Compression (Alt edition): one optical, LA-2A style mode with two controls, plus the side-chain EQ view.
    class CompressionPage : public AdvFrame
    {
    public:
        explicit CompressionPage (KaminariVocalProcessor& p)
            : AdvFrame (p, "Compression", "Optical leveler (LA-2A style): Compression and Gain", "lv_on", "compression"),
              display (p), scEq (p, KaminariVocalProcessor::EqSideChain),
              gain (p.apvts, "lv_gain", "Gain", "-12 dB", "+24 dB"),
              peak (p.apvts, "lv_peak", "Compression", "0", "100")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &display, &gain, &peak })
                addAndMakeVisible (c);
            addChildComponent (scEq);
            const char* names[] = { "COMPRESSOR", "SIDE-CHAIN EQ" };
            const char* tips[] = { "Level display and the compressor's controls.",
                                   "Side-chain detection EQ: shapes what the compressor reacts to (the audio is not filtered). "
                                   "Same editor as the main EQ." };
            for (int v = 0; v < 2; ++v)
            {
                auto* b = viewButtons.add (new juce::TextButton (names[v]));
                b->setRadioGroupId (93);
                b->setClickingTogglesState (true);
                b->setTooltip (tips[v]);
                b->setConnectedEdges (v == 0 ? juce::Button::ConnectedOnRight : juce::Button::ConnectedOnLeft);
                b->onClick = [this, v] { if (viewButtons[v]->getToggleState()) showSideChain (v == 1); };
                addAndMakeVisible (b);
            }
            viewButtons[0]->setToggleState (true, juce::dontSendNotification);
            for (auto* k : { &gain, &peak }) k->setLNF (&lnf);
        }

        RangeKnob& compressionKnob() { return peak; }
        RangeKnob& gainKnob() { return gain; }
        EqEditor& sideChainEq() { return scEq; }
        // Side-chain EQ view: the EQ editor takes the display and the controls area; "Compressor" brings them back.
        void showSideChain (bool sc)
        {
            scShown = sc;
            for (int v = 0; v < viewButtons.size(); ++v) viewButtons[v]->setToggleState ((v == 1) == sc, juce::dontSendNotification);
            display.setVisible (! sc);
            scEq.setVisible (sc);
            for (auto* c : { (juce::Component*) &gain, (juce::Component*) &peak }) c->setVisible (! sc);
            if (! sc) proc.scEqSolo.store (-1);
            resized();
            repaint();
        }
        bool sideChainShown() const noexcept { return scShown; }

        void paint (juce::Graphics& g) override
        {
            AdvFrame::paint (g);
            g.setColour (mist);
            g.setFont (font (11.0f, 1, 0.1f));
            g.drawText (scShown ? "SIDE-CHAIN EQ" + dot() + "shapes what the detector hears, the audio is not filtered"
                                : "LEVEL" + dot() + "gain-reduction history and meters",
                        viewRow.getX(), viewRow.getY(), viewRow.getWidth() - 240, viewRow.getHeight(), juce::Justification::centredLeft);
            if (scShown) return;
            drawGroup (g, controls);
            g.setColour (mist);
            g.setFont (font (12.0f, 0));
            g.drawFittedText ("One optical mode. Compression sets how hard the vocal drives the cell: it reacts in about 10 ms "
                              "and lets go in two stages, quickly at first, then over one to several seconds, slower after long, "
                              "heavy leveling. Gain is the makeup after it. Use the side-chain EQ to make it react less to lows "
                              "or more to a harsh range.",
                              notes, juce::Justification::centredLeft, 5, 1.0f);
        }

        void layoutContent (juce::Rectangle<int> b) override
        {
            viewRow = b.removeFromTop (30);
            auto vr = viewRow;
            for (int v = viewButtons.size(); --v >= 0;) viewButtons[v]->setBounds (vr.removeFromRight (120));
            b.removeFromTop (8);
            const auto all = b;
            controls = b.removeFromBottom (190);
            b.removeFromBottom (10);
            display.setBounds (b);
            auto c = controls.reduced (24, 12);
            const int knob = 150;
            gain.setBounds (c.removeFromLeft (knob));
            c.removeFromLeft (30);
            peak.setBounds (c.removeFromLeft (knob));
            c.removeFromLeft (40);
            notes = c;
            scEq.setBounds (all);
        }

    private:
        ModuleLNF lnf;
        CompDisplay display;
        EqEditor scEq;
        juce::OwnedArray<juce::TextButton> viewButtons;
        juce::Rectangle<int> viewRow, controls, notes;
        bool scShown = false;
        RangeKnob gain, peak;
    };

    //==================================================================================================================
    // DE-ESS: a smooth, scrolling level display in the style of a modern de-esser: the input level as a soft filled
    // shape, the part the de-esser removes highlighted between the input and output levels, the gain reduction as a
    // smooth curve hanging from the top, the threshold line, and meters. 6 seconds of history.
    class DeEssDisplay : public juce::Component, private juce::Timer
    {
    public:
        static constexpr int N = KaminariVocalProcessor::deessHistorySize;
        static constexpr float seconds = 6.0f, floorDb = -60.0f, grScaleDb = 24.0f;

        explicit DeEssDisplay (KaminariVocalProcessor& p) : proc (p) { setTitle ("De-ess display"); startTimerHz (60); }
        ~DeEssDisplay() override { stopTimer(); }

        // Smooths a series (5-point weighted average) so the curves read as one flowing line.
        static void smooth (std::vector<float>& v)
        {
            std::vector<float> o (v.size());
            for (size_t i = 0; i < v.size(); ++i)
            {
                float acc = 0, w = 0;
                for (int k = -2; k <= 2; ++k)
                {
                    const auto j = (size_t) juce::jlimit (0, (int) v.size() - 1, (int) i + k);
                    const float wk = 3.0f - (float) std::abs (k);
                    acc += v[j] * wk; w += wk;
                }
                o[i] = acc / w;
            }
            v.swap (o);
        }

        // Path through points with quadratic curves via the midpoints (no corners).
        static juce::Path flowing (const std::vector<juce::Point<float>>& pts)
        {
            juce::Path p;
            if (pts.empty()) return p;
            p.startNewSubPath (pts.front());
            for (size_t i = 1; i + 1 < pts.size(); ++i)
                p.quadraticTo (pts[i], (pts[i] + pts[i + 1]) * 0.5f);
            p.lineTo (pts.back());
            return p;
        }

        void paint (juce::Graphics& g) override
        {
            auto all = getLocalBounds().toFloat();
            juce::ColourGradient bg (navy800.withAlpha (0.5f), all.getCentreX(), all.getY(), navy950, all.getCentreX(), all.getBottom(), false);
            g.setGradientFill (bg);
            g.fillRoundedRectangle (all, 6.0f);
            auto b = getLocalBounds().reduced (8);
            auto meters = b.removeFromRight (64);
            auto scale = b.removeFromRight (34);
            b.removeFromBottom (16);   // time axis
            const auto plot = b.toFloat();
            // waveform: mirrored around the centre line, amplitude on a dB scale (0 dB at the edges, -60 dB at the centre)
            const float mid = plot.getCentreY(), half = plot.getHeight() * 0.5f - 2.0f;
            auto ampFor = [&] (float db) { return juce::jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb) * half; };

            // grid: dB lines on both halves, the centre line, seconds
            g.setFont (font (10.0f, 0));
            for (int db : { 0, -12, -24, -36, -48 })
            {
                const float a = ampFor ((float) db);
                g.setColour (navy800);
                g.drawHorizontalLine ((int) (mid - a), plot.getX(), plot.getRight());
                g.drawHorizontalLine ((int) (mid + a), plot.getX(), plot.getRight());
                g.setColour (mist.withAlpha (0.7f));
                g.drawText (juce::String (db), scale.getX(), (int) (mid - a) - 6, scale.getWidth() - 4, 12, juce::Justification::centredRight);
            }
            g.setColour (navy600);
            g.drawHorizontalLine ((int) mid, plot.getX(), plot.getRight());
            for (int sec = 1; sec < (int) seconds; ++sec)
            {
                const float x = plot.getRight() - plot.getWidth() * (float) sec / seconds;
                g.setColour (navy800.withAlpha (0.6f));
                g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
                g.setColour (mist.withAlpha (0.6f));
                g.drawText ("-" + juce::String (sec) + " s", (int) x - 20, (int) plot.getBottom() + 2, 40, 12, juce::Justification::centred);
            }

            if (n > 1)
            {
                std::vector<juce::Point<float>> inTop, outTop, grPts;
                inTop.reserve ((size_t) n); outTop.reserve ((size_t) n); grPts.reserve ((size_t) n);
                for (int i = 0; i < n; ++i)
                {
                    const float x = plot.getX() + plot.getWidth() * (float) i / (float) (n - 1);
                    inTop.push_back ({ x, mid - ampFor (inDb[(size_t) i]) });
                    outTop.push_back ({ x, mid - ampFor (outDb[(size_t) i]) });
                    grPts.push_back ({ x, plot.getY() + juce::jlimit (0.0f, 1.0f, grDb[(size_t) i] / grScaleDb) * plot.getHeight() * 0.22f });
                }
                auto mirrored = [&] (const std::vector<juce::Point<float>>& top)
                {
                    juce::Path p = flowing (top);
                    std::vector<juce::Point<float>> bottom;
                    bottom.reserve (top.size());
                    for (auto it = top.rbegin(); it != top.rend(); ++it) bottom.push_back ({ it->x, 2.0f * mid - it->y });
                    p.lineTo (bottom.front());
                    for (size_t k = 1; k + 1 < bottom.size(); ++k)
                        p.quadraticTo (bottom[k], (bottom[k] + bottom[k + 1]) * 0.5f);
                    p.lineTo (bottom.back());
                    p.closeSubPath();
                    return p;
                };
                // input waveform (the full shape), then the output on top of it: what is left visible of the input,
                // at the edges, is what the de-esser removed
                const auto inShape = mirrored (inTop), outShape = mirrored (outTop);
                juce::ColourGradient rem (accent.withAlpha (0.95f), 0, plot.getY(), accent.withAlpha (0.95f), 0, plot.getBottom(), false);
                rem.addColour (0.5, accent.withAlpha (0.45f));
                g.setGradientFill (rem);
                g.fillPath (inShape);
                juce::ColourGradient wave (mist.withAlpha (0.75f), 0, plot.getY(), mist.withAlpha (0.75f), 0, plot.getBottom(), false);
                wave.addColour (0.5, mist.withAlpha (0.28f));
                g.setGradientFill (wave);
                g.fillPath (outShape);
                g.setColour (white.withAlpha (0.8f));
                g.strokePath (outShape, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                // gain reduction hanging from the top
                auto grLine = flowing (grPts);
                juce::Path grFill (grLine);
                grFill.lineTo (plot.getRight(), plot.getY());
                grFill.lineTo (plot.getX(), plot.getY());
                grFill.closeSubPath();
                juce::ColourGradient gg (accent.withAlpha (0.5f), 0, plot.getY(), accent.withAlpha (0.05f), 0, plot.getY() + plot.getHeight() * 0.22f, false);
                g.setGradientFill (gg);
                g.fillPath (grFill);
                g.setColour (accent);
                g.strokePath (grLine, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // meters: input, output, reduction
            auto m = meters.toFloat().reduced (4, 2).withTrimmedBottom (16);
            const float w = (m.getWidth() - 8) / 3;
            drawVMeter (g, m.removeFromLeft (w), n > 0 ? inDb.back() : -60.0f, false, mist);
            m.removeFromLeft (4);
            drawVMeter (g, m.removeFromLeft (w), n > 0 ? outDb.back() : -60.0f, false, white);
            m.removeFromLeft (4);
            drawVMeter (g, m, proc.moduleGr[KaminariVocalProcessor::ModDeEss].load(), true, accent);
        }

        int points() const noexcept { return n; }
        int updates() const noexcept { return updateCount; }   // timer updates done (none while the page is hidden)

    private:
        void timerCallback() override
        {
            if (! visibleInWindow (*this)) return;
            std::array<kv::LevelHistory<N>::Entry, (size_t) N> h;
            proc.deessHistory.read (h);
            inDb.resize ((size_t) N); outDb.resize ((size_t) N); grDb.resize ((size_t) N);
            for (int i = 0; i < N; ++i)
            {
                inDb[(size_t) i] = juce::Decibels::gainToDecibels (h[(size_t) i].in, floorDb);
                outDb[(size_t) i] = juce::Decibels::gainToDecibels (h[(size_t) i].out, floorDb);
                grDb[(size_t) i] = juce::jmax (0.0f, h[(size_t) i].gr);
            }
            smooth (inDb); smooth (outDb); smooth (grDb);
            // the removed part follows the de-esser's own reduction (a split-band cut barely moves the broadband peak)
            for (int i = 0; i < N; ++i) outDb[(size_t) i] = std::max (floorDb, inDb[(size_t) i] - grDb[(size_t) i]);
            n = N;
            ++updateCount;
            repaint();
        }
        KaminariVocalProcessor& proc;
        std::vector<float> inDb, outDb, grDb;
        int n = 0, updateCount = 0;
    };

    // De-ess frequency on a 2-12 kHz log strip: the highlighted range above the handle is what the detector listens to
    // and what is turned down. Drag anywhere to move it.
    class FreqRange : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        FreqRange (APVTS& s) : state (s) { setTitle ("De-ess frequency"); setTooltip ("Drag to set the de-ess frequency: everything above it is detected."); }
        float xFor (float f) const { return (float) (std::log (f / 2000.0) / std::log (6.0)) * (getWidth() - 12.0f) + 6.0f; }
        float fFor (float x) const { return 2000.0f * std::pow (6.0f, juce::jlimit (0.0f, 1.0f, (x - 6.0f) / (getWidth() - 12.0f))); }
        void paint (juce::Graphics& g) override
        {
            const float lo = plain (state, "ds_det_lo");
            auto r = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), 20.0f);
            g.setColour (navy950);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (navy600);
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
            const float x0 = xFor (lo), x1 = r.getRight() - 2.0f;
            g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.7f), x0, 0, accent.withAlpha (0.25f), x1, 0, false));
            g.fillRect (juce::Rectangle<float> (x0, r.getY() + 2, x1 - x0, r.getHeight() - 4));
            juce::Path tri;
            tri.addTriangle (x0 - 7, r.getBottom(), x0 + 7, r.getBottom(), x0, r.getY());
            g.setColour (white);
            g.fillPath (tri);
        }
        void mouseDown (const juce::MouseEvent& e) override { state.getParameter ("ds_det_lo")->beginChangeGesture(); mouseDrag (e); }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            auto* p = state.getParameter ("ds_det_lo");
            p->setValueNotifyingHost (p->convertTo0to1 (fFor (e.position.x)));
            repaint();
        }
        void mouseUp (const juce::MouseEvent&) override { state.getParameter ("ds_det_lo")->endChangeGesture(); }
    private:
        APVTS& state;
    };

    // De-ess (Alt edition): Frequency and Range only. Esses are found by how loud the highs above Frequency are
    // against the whole vocal, so the same setting works at any input level.
    class DeEssPage : public AdvFrame, private juce::Timer
    {
    public:
        explicit DeEssPage (KaminariVocalProcessor& p)
            : AdvFrame (p, "De-ess", "Waveform: highlighted edges are what the de-esser removes" + dot() + "6 s history", "ds_on", "deess"),
              display (p), range (p.apvts),
              freq (p.apvts, "ds_det_lo", "Frequency", "2 kHz", "12 kHz"),
              rangeKnob (p.apvts, "ds_range", "Range", "0 dB", "24 dB")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &display, &range, &freq, &rangeKnob })
                addAndMakeVisible (c);
            for (auto* k : { &freq, &rangeKnob }) k->setLNF (&lnf);
            startTimerHz (10);
        }
        ~DeEssPage() override { stopTimer(); }

        RangeKnob& frequencyKnob() { return freq; }
        DeEssDisplay& historyDisplay() { return display; }
        RangeKnob& rangeControl() { return rangeKnob; }

        void paint (juce::Graphics& g) override
        {
            AdvFrame::paint (g);
            drawGroup (g, controls);
            g.setColour (accent);
            g.setFont (font (14.0f, 2));
            g.drawText ("Above " + kvp::freqText (plain (proc.apvts, "ds_det_lo")), range.getX(), range.getBottom() + 4, range.getWidth(), 18, juce::Justification::centred);
            g.setColour (mist);
            g.setFont (font (12.0f, 0));
            g.drawFittedText ("Frequency: esses above it are detected, and only that range is turned down. "
                              "Range: the most it is turned down on an ess.",
                              notes, juce::Justification::centredLeft, 3, 1.0f);
        }

        void layoutContent (juce::Rectangle<int> b) override
        {
            controls = b.removeFromBottom (190);
            b.removeFromBottom (10);
            display.setBounds (b);   // the display takes all the remaining height
            auto c = controls.reduced (24, 12);
            freq.setBounds (c.removeFromLeft (150));
            c.removeFromLeft (30);
            rangeKnob.setBounds (c.removeFromLeft (150));
            c.removeFromLeft (40);
            auto right = c.withSizeKeepingCentre (c.getWidth(), 110);
            range.setBounds (right.removeFromTop (24));
            right.removeFromTop (30);
            notes = right;
        }

    private:
        void timerCallback() override { if (visibleInWindow (*this)) repaint (controls); }
        ModuleLNF lnf;
        DeEssDisplay display;
        FreqRange range;
        RangeKnob freq, rangeKnob;
        juce::Rectangle<int> controls, notes;
    };

    //==================================================================================================================
    // RESONANCE
    class ResGraph : public juce::Component, private juce::Timer
    {
    public:
        explicit ResGraph (KaminariVocalProcessor& p) : proc (p) { setTitle ("Resonance graph"); setOpaque (true); startTimerHz (25); }
        int analyzerMode = AnalyzerPair::Both;
        ~ResGraph() override { stopTimer(); }
        std::function<void (int)> onSelect;
        int selected = 0;
        float xFor (double f) const { return (float) (std::log (f / 20.0) / std::log (1000.0)) * getWidth(); }
        double fFor (float x) const { return 20.0 * std::pow (1000.0, juce::jlimit (0.0f, 1.0f, x / (float) getWidth())); }
        float yCurve (double db) const { return getHeight() * 0.62f - (float) db / 24.0f * getHeight() * 0.5f; }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.fillAll (navy900);   // opaque: the page is not redrawn behind the graph every frame
            g.setColour (navy950);
            g.fillRoundedRectangle (b, 4.0f);
            // reduction hanging from the top (Max Cut dashed)
            const auto& r = proc.resonance.active();
            const int n = r.numBands();
            const float zero = getHeight() * 0.62f;
            // area from the bottom up to the 0 dB line; each band's reduction carves a notch downwards
            const float depthPx = b.getHeight() - zero;
            juce::Path red;
            red.startNewSubPath (0, b.getBottom());
            red.lineTo (0, zero);
            for (int k = 0; k < n; ++k)
                red.lineTo (xFor (r.bandFrequency (k)), zero + juce::jlimit (0.0f, 1.0f, r.bandReduction (k) / 24.0f) * depthPx);
            red.lineTo (b.getWidth(), zero);
            red.lineTo (b.getWidth(), b.getBottom());
            red.closeSubPath();
            g.setColour (juce::Colour (0xff2d5490));
            g.fillPath (red);
            // input and output spectra above the reduction (where the cuts come from and what is left)
            analyzers.draw (g, b.withTrimmedTop (18.0f).withHeight (zero - 18.0f), [this] (float x) { return fFor (x); }, analyzerMode, "IN", "OUT");
            g.setColour (navy800);
            for (double f : { 100.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 })
                g.drawVerticalLine (juce::roundToInt (xFor (f)), 0.0f, b.getBottom());
            g.setColour (white.withAlpha (0.85f));
            g.setFont (font (11.0f, 0));
            for (auto [f, t] : { std::pair { 100.0, "100" }, { 250.0, "250" }, { 500.0, "500" }, { 1000.0, "1k" }, { 2000.0, "2k" }, { 4000.0, "4k" }, { 8000.0, "8k" } })
                g.drawText (t, (int) xFor (f) - 15, 6, 30, 14, juce::Justification::centred);
            // depth curve (rebuilt only when a band or the size changes)
            updateCurve();
            g.setColour (white);
            g.fillPath (curveLine);
            for (int k = 0; k < 8; ++k)
            {
                if (! used (k)) continue;
                const auto pre = "rs_b" + juce::String (k + 1) + "_";
                const float x = xFor (plain (proc.apvts, pre + "freq")), y = yCurve (plain (proc.apvts, pre + "depth"));
                g.setColour (bandColour (k));
                g.fillEllipse (x - 8, y - 8, 16, 16);
                g.setColour (white);
                g.drawEllipse (x - 8, y - 8, 16, 16, k == selected ? 2.5f : 1.2f);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            drag = nodeAt (e.position);
            if (drag >= 0) { selected = drag; if (onSelect) onSelect (drag); }
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (drag < 0) return;
            const auto pre = "rs_b" + juce::String (drag + 1) + "_";
            auto* f = proc.apvts.getParameter (pre + "freq");
            auto* d = proc.apvts.getParameter (pre + "depth");
            f->setValueNotifyingHost (f->convertTo0to1 ((float) fFor (e.position.x)));
            d->setValueNotifyingHost (d->convertTo0to1 (juce::jlimit (-24.0f, 24.0f, (getHeight() * 0.62f - e.position.y) / (getHeight() * 0.5f) * 24.0f)));
            repaint();   // follow the mouse at once
        }
        void mouseUp (const juce::MouseEvent&) override { drag = -1; }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (nodeAt (e.position) >= 0) return;
            for (int k = 0; k < 8; ++k)
                if (! used (k))
                {
                    const auto pre = "rs_b" + juce::String (k + 1) + "_";
                    setParamPlain (proc.apvts, pre + "freq", (float) fFor (e.position.x));
                    setParamPlain (proc.apvts, pre + "depth", juce::jlimit (-24.0f, 24.0f, (getHeight() * 0.62f - e.position.y) / (getHeight() * 0.5f) * 24.0f));
                    setParamPlain (proc.apvts, pre + "used", 1.0f);
                    selected = k;
                    if (onSelect) onSelect (k);
                    return;
                }
        }

    private:
        bool used (int k) const { return plain (proc.apvts, "rs_b" + juce::String (k + 1) + "_used") > 0.5f; }
        int nodeAt (juce::Point<float> p) const
        {
            for (int k = 0; k < 8; ++k)
            {
                if (! used (k)) continue;
                const auto pre = "rs_b" + juce::String (k + 1) + "_";
                if (juce::Point<float> (xFor (plain (proc.apvts, pre + "freq")), yCurve (plain (proc.apvts, pre + "depth"))).getDistanceFrom (p) < 11) return k;
            }
            return -1;
        }
        struct CurveBand { bool active = false; int type = 0; float freq = 0, gainDb = 0, q = 0; bool operator!= (const CurveBand& o) const
                           { return active != o.active || type != o.type || ! juce::exactlyEqual (freq, o.freq) || ! juce::exactlyEqual (gainDb, o.gainDb)
                                    || ! juce::exactlyEqual (q, o.q); } };
        void updateCurve()
        {
            static const int shapeToType[8] = { kv::LowCut, kv::LowShelf, kv::HighShelf, kv::HighCut, kv::Bell, kv::BandPass, kv::Notch, kv::TiltShelf };
            std::array<CurveBand, 8> now;
            for (int k = 0; k < 8; ++k)
            {
                const auto pre = "rs_b" + juce::String (k + 1) + "_";
                auto& c = now[(size_t) k];
                c.active = used (k) && plain (proc.apvts, pre + "on") > 0.5f;
                if (! c.active) continue;
                c.type = shapeToType[juce::jlimit (0, 7, juce::roundToInt (plain (proc.apvts, pre + "shape")))];
                c.freq = plain (proc.apvts, pre + "freq"); c.gainDb = plain (proc.apvts, pre + "depth"); c.q = plain (proc.apvts, pre + "q");
            }
            bool changed = getWidth() != curveW || getHeight() != curveH;
            for (int k = 0; k < 8; ++k) changed = changed || now[(size_t) k] != curveBands[(size_t) k];
            if (! changed) return;
            curveBands = now; curveW = getWidth(); curveH = getHeight();
            kv::EqDesign designs[8];
            for (int k = 0; k < 8; ++k)
                if (now[(size_t) k].active)
                {
                    kv::EqBandSettings e;
                    e.type = now[(size_t) k].type; e.freq = now[(size_t) k].freq; e.gainDb = now[(size_t) k].gainDb; e.q = now[(size_t) k].q;
                    designs[k] = kv::EqDesign::make (e, 48000.0);
                }
            juce::Path curve;
            for (int x = 0; x <= getWidth(); x += 3)
            {
                const double f = fFor ((float) x);
                double db = 0;
                for (int k = 0; k < 8; ++k)
                {
                    const auto& c = curveBands[(size_t) k];
                    if (! c.active) continue;
                    double m = designs[k].magnitudeDb (std::min (f, 23000.0), 48000.0);
                    if (c.type == kv::BandPass) m = std::max (-24.0, m) + c.gainDb;
                    db += juce::jlimit (-24.0, 24.0, m);
                }
                const float y = juce::jlimit (0.0f, (float) getHeight(), yCurve (db));
                if (x == 0) curve.startNewSubPath ((float) x, y); else curve.lineTo ((float) x, y);
            }
            curveLine.clear();
            juce::PathStrokeType (2.0f).createStrokedPath (curveLine, curve);
        }
        std::array<CurveBand, 8> curveBands {};
        juce::Path curveLine;
        int curveW = -1, curveH = -1;
        void timerCallback() override
        {
            if (! visibleInWindow (*this)) return;
            analyzers.update (proc, proc.rsAnalyserPre, proc.rsAnalyserPost, analyzerMode);
            repaint();
        }
        AnalyzerPair analyzers;
        KaminariVocalProcessor& proc;
        int drag = -1;
    };

    class ResonancePage : public AdvFrame, private juce::Timer
    {
    public:
        explicit ResonancePage (KaminariVocalProcessor& p)
            : AdvFrame (p, "Resonance", "Resonant suppressor" + dot() + "low latency (0 smp)", "rs_on", "resonance"),
              graph (p),
              mode (p.apvts, "rs_mode", { "soft", "hard" }),
              depth (p.apvts, "rs_depth", "Depth"), detail (p.apvts, "rs_detail", "Detail"),
              attack (p.apvts, "rs_attack", "Attack"), release (p.apvts, "rs_release", "Release"),
              stereo (p.apvts, "rs_stereo_mode", { "L/R", "M/S" }),
              link (p.apvts, "rs_link", "link"), focus (p.apvts, "rs_focus", "focus"),
              dLo (p.apvts, "rs_detail_tilt_lo", "detail lo"), dHi (p.apvts, "rs_detail_tilt_hi", "detail hi"),
              aLo (p.apvts, "rs_attack_tilt_lo", "attack lo"), aHi (p.apvts, "rs_attack_tilt_hi", "attack hi"),
              rLo (p.apvts, "rs_release_tilt_lo", "release lo"), rHi (p.apvts, "rs_release_tilt_hi", "release hi"),
              maxCut (p.apvts, "rs_max_cut", "max cut"), wetTrim (p.apvts, "rs_wet_trim", "wet trim"),
              mix (p.apvts, "rs_mix", "mix"), out (p.apvts, "rs_out_gain", "out"),
              quality (p.apvts, "rs_quality", { "normal", "high", "ultra" }),
              os (p.apvts, "rs_os", { "off", "2x", "4x" }, "Oversampling: runs the resonance bands at 2x or 4x the session rate. Adds latency while Resonance is on."),
              analyzer ([this] { return graph.analyzerMode; }, [this] (int m) { graph.analyzerMode = m; }, "In", "Out"),
              bypass (p.apvts, "rs_bypass", "bypass", "bypass", hintFor ("rs_bypass")),
              delta (p.apvts, "rs_delta", "delta", "delta", hintFor ("rs_delta"))
        {
            for (auto* c : std::initializer_list<juce::Component*> { &graph, &mode, &depth, &detail, &attack, &release, &stereo, &link, &focus,
                                                                     &dLo, &dHi, &aLo, &aHi, &rLo, &rHi, &maxCut, &wetTrim, &mix, &out, &quality,
                                                                     &bypass, &delta, &panel, &os, &analyzer })
                addAndMakeVisible (c);
            for (auto* k : { &depth, &detail, &attack, &release }) k->setLNF (&lnf);
            graph.onSelect = [this] (int k) { panel.build (proc, k); resized(); };
            panel.build (p, 0);
            startTimerHz (4);
        }
        ~ResonancePage() override { stopTimer(); }
        void timerCallback() override { if (visibleInWindow (*this)) { panel.resized(); panel.repaint(); } }

        void paint (juce::Graphics& g) override
        {
            AdvFrame::paint (g);
            drawGroup (g, col1);
            drawGroup (g, col2);
            drawGroup (g, bar);
            g.setColour (mist);
            g.setFont (font (11.0f, 0));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 stereo \xe2\x80\x94")), col2.getX(), col2.getY() + 6, col2.getWidth(), 14, juce::Justification::centred);
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 tilt \xe2\x80\x94")), col2.getX(), dLo.getY() - 16, col2.getWidth(), 14, juce::Justification::centred);
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 scale \xe2\x80\x94")), col2.getX(), maxCut.getY() - 16, col2.getWidth(), 14, juce::Justification::centred);
            g.drawText ("quality", quality.getX() - 56, bar.getY(), 50, bar.getHeight(), juce::Justification::centredRight);
            g.drawText ("oversampling", os.getX() - 82, bar.getY(), 78, bar.getHeight(), juce::Justification::centredRight);
            g.drawText ("analyzer", analyzer.getX() - 60, graph.getY() - 18, 56, 14, juce::Justification::centredRight);
            g.setColour (white.withAlpha (0.8f));
            g.drawText ("drag a node: freq and depth" + dot() + "double-click: add a depth-curve band", graph.getX(), graph.getY() - 18, graph.getWidth(), 14, juce::Justification::centredRight);
        }

        void layoutContent (juce::Rectangle<int> b) override
        {
            bar = b.removeFromBottom (36);
            b.removeFromBottom (8);
            col1 = b.removeFromLeft (175);
            b.removeFromLeft (8);
            col2 = b.removeFromLeft (115);
            b.removeFromLeft (10);
            graph.setBounds (b.withTrimmedTop (20));
            analyzer.setBounds (juce::Rectangle<int> (graph.getX() + 60, graph.getY() - 20, 150, 18));
            panel.setBounds (graph.getBounds().withSizeKeepingCentre (430, 92).withY (graph.getBottom() - 100));
            auto c = col1.reduced (10, 10);
            mode.setBounds (c.removeFromTop (28).reduced (12, 0));
            c.removeFromTop (6);
            depth.setBounds (c.removeFromTop (130));
            detail.setBounds (c.removeFromTop (100));
            auto ar = c.removeFromTop (90);
            attack.setBounds (ar.removeFromLeft (ar.getWidth() / 2));
            release.setBounds (ar);
            auto c2 = col2.reduced (8, 8);
            c2.removeFromTop (16);
            stereo.setBounds (c2.removeFromTop (26));
            c2.removeFromTop (4);
            link.setBounds (c2.removeFromTop (38));
            focus.setBounds (c2.removeFromTop (38));
            c2.removeFromTop (18);
            for (auto pair : { std::pair { &dLo, &dHi }, { &aLo, &aHi }, { &rLo, &rHi } })
            {
                auto row = c2.removeFromTop (38);
                pair.first->setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (1, 0));
                pair.second->setBounds (row.reduced (1, 0));
            }
            c2.removeFromTop (18);
            maxCut.setBounds (c2.removeFromTop (38));
            wetTrim.setBounds (c2.removeFromTop (38));
            auto r = bar.reduced (8, 5);
            bypass.setBounds (r.removeFromLeft (80));
            r.removeFromLeft (6);
            delta.setBounds (r.removeFromLeft (80));
            quality.setBounds (r.removeFromRight (170));
            r.removeFromRight (64);
            os.setBounds (r.removeFromRight (120));
            r.removeFromRight (90);
            out.setBounds (r.removeFromRight (90).withTrimmedTop (-10));
            r.removeFromRight (10);
            mix.setBounds (r.removeFromRight (90).withTrimmedTop (-10));
        }

        struct BandPanel : juce::Component
        {
            void build (KaminariVocalProcessor& p, int k)
            {
                removeAllChildren();
                band = k;
                const juce::String pre = "rs_b" + juce::String (k + 1) + "_";
                usedId = pre + "used";
                state = &p.apvts;
                on = std::make_unique<PowerButton> (p.apvts, pre + "on", "Depth band " + juce::String (k + 1));
                shape = std::make_unique<SegParam> (p.apvts, pre + "shape", juce::StringArray { "LC", "LS", "HS", "HC", "Bell", "BP", "BR", "Tilt" });
                freq = std::make_unique<Field> (p.apvts, pre + "freq", "freq");
                depth = std::make_unique<Field> (p.apvts, pre + "depth", "depth");
                q = std::make_unique<Field> (p.apvts, pre + "q", "q");
                del.setButtonText ("x");
                del.setTooltip ("Remove this depth-curve band");
                del.onClick = [&p, pre] { setParamPlain (p.apvts, pre + "used", 0.0f); };
                for (auto* c : std::initializer_list<juce::Component*> { on.get(), shape.get(), freq.get(), depth.get(), q.get(), &del })
                    addAndMakeVisible (c);
                resized();
            }
            void paint (juce::Graphics& g) override
            {
                g.setColour (navy900.withAlpha (0.96f));
                g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
                g.setColour (accent);
                g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);
                g.setColour (white);
                g.setFont (font (11.0f, 0));
                g.drawText ("shape", shape->getX(), 6, shape->getWidth(), 14, juce::Justification::centred);
                const bool isUsed = state != nullptr && state->getRawParameterValue (usedId)->load() > 0.5f;
                if (! isUsed)
                {
                    g.setColour (mist);
                    g.drawText ("Band " + juce::String (band + 1) + " not in use: double-click the graph to add a band.", getLocalBounds(), juce::Justification::centred);
                }
            }
            void resized() override
            {
                if (on == nullptr) return;
                const bool isUsed = state != nullptr && state->getRawParameterValue (usedId)->load() > 0.5f;
                for (auto* c : std::initializer_list<juce::Component*> { on.get(), shape.get(), freq.get(), depth.get(), q.get(), &del }) c->setVisible (isUsed);
                auto b = getLocalBounds().reduced (10, 8);
                on->setBounds (b.removeFromLeft (28).withSizeKeepingCentre (28, 28));
                b.removeFromLeft (8);
                shape->setBounds (b.removeFromLeft (180).withTrimmedTop (16).withHeight (26));
                b.removeFromLeft (8);
                del.setBounds (b.removeFromRight (26).withSizeKeepingCentre (26, 26));
                const int w = b.getWidth() / 3;
                freq->setBounds (b.removeFromLeft (w).reduced (3, 6));
                depth->setBounds (b.removeFromLeft (w).reduced (3, 6));
                q->setBounds (b.reduced (3, 6));
            }
            std::unique_ptr<PowerButton> on;
            std::unique_ptr<SegParam> shape;
            std::unique_ptr<Field> freq, depth, q;
            juce::TextButton del;
            int band = 0;
            juce::String usedId;
            APVTS* state = nullptr;
        } panel;

    private:
        ModuleLNF lnf;
        ResGraph graph;
        SegParam mode;
        RangeKnob depth, detail, attack, release;
        SegParam stereo;
        Field link, focus, dLo, dHi, aLo, aHi, rLo, rHi, maxCut, wetTrim, mix, out;
        SegParam quality, os;
        AnalyzerToggles analyzer;
        ToggleBox bypass, delta;
        juce::Rectangle<int> col1, col2, bar;
    };
}
