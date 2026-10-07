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
        void timerCallback() override { repaint(); }
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
            g.drawText ("CORRECTION", strip.getRight() - 250, strip.getY() + 10, 130, 14, juce::Justification::centredRight);
            g.drawText ("TRACKING", strip.getRight() - 106, strip.getY() + 10, 90, 14, juce::Justification::centred);
            const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
            const int lat = kv::Tune::latencyFor (sr);
            g.setColour (white);
            g.setFont (font (12.5f, 0));
            g.drawText (juce::String (lat) + " smp" + dot() + juce::String (1000.0 * lat / sr, 1) + " ms",
                        strip.getRight() - 250, strip.getY() + 28, 130, 16, juce::Justification::centredRight);
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
            range.setBounds (s.removeFromLeft (170)); s.removeFromLeft (16);
            key.setBounds (s.removeFromLeft (80)); s.removeFromLeft (16);
            scale.setBounds (s.removeFromLeft (150)); s.removeFromLeft (16);
            detune.setBounds (s.removeFromLeft (150).withTrimmedTop (2));
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
        juce::Rectangle<int> strip;
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
            const int cur = noteOfBand (band);
            for (int pass = 0; pass < 2; ++pass)
                for (int m = loNote(); m <= hiNote(); ++m)
                {
                    if (Piano::isBlack (m) != (pass == 1)) continue;
                    const auto r = keyRect (m);
                    if (r.getRight() < 0 || r.getX() > getWidth()) continue;
                    const bool black = Piano::isBlack (m);
                    juce::Colour fill = black ? juce::Colour (0xff1a2236) : juce::Colour (0xffdce3f0);
                    if (m == hover) fill = black ? juce::Colour (0xff2b4a82) : juce::Colour (0xffb8f3ff);
                    if (m == cur) fill = EqCurve::bandColour (band);
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
            if (! used (band)) return;
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

        void timerCallback() override { repaint(); }
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
            freq->setValueText ([fp]
            {
                const float f = fp->convertFrom0to1 (fp->getValue());
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
            use.setVisible (! used);
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
            pianoToggle.setToggleState (true, juce::dontSendNotification);
            pianoToggle.setTooltip ("Show the keyboard under the graph: click or drag along the keys to sweep the selected band from note to note.");
            pianoToggle.onClick = [this] { piano.setVisible (pianoToggle.getToggleState()); resized(); repaint(); };
            piano.setVisible (true);
            curve.onSelect = [this] (int b) { select (b); };
            select (firstUsed());
            startTimerHz (6);
        }
        ~EqEditor() override { stopTimer(); proc.eqSoloFor (target).store (-1); }

        void select (int band)
        {
            selected = juce::jlimit (0, 7, band);
            curve.selected = selected;
            piano.band = selected;
            auto& solo = proc.eqSoloFor (target);
            if (solo.load() >= 0) solo.store (selected);
            panel.build (proc, target, selected, lnf, [this] (int d) { select ((selected + d + 8) % 8); });
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
            return 0;
        }
        void timerCallback() override { panel.resized(); }
        juce::Component* extra = nullptr;
        juce::Rectangle<int> bar;
        int selected = 0;
    };

    class EqPage : public AdvFrame
    {
    public:
        explicit EqPage (KaminariVocalProcessor& p)
            : AdvFrame (p, "EQ", "8 bands" + dot() + "zero latency" + dot() + "click to add a band, drag nodes, wheel = Q", "eq_on", "eq"),
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
    inline juce::Colour bandColour (int k)
    {
        static const juce::Colour c[] = { juce::Colour (0xff8fb8ff), juce::Colour (0xffc39bff), juce::Colour (0xff5ce1ff),
                                          juce::Colour (0xffffb547), juce::Colour (0xff7ee0a1), juce::Colour (0xffff8fa3) };
        return c[juce::jlimit (0, 5, k)];
    }

    class MultibandDisplay : public juce::Component, private juce::Timer
    {
    public:
        explicit MultibandDisplay (KaminariVocalProcessor& p) : proc (p)
        {
            setTitle ("Multiband bands");
            setDescription ("Click empty space to add a band there; drag a band or its edges to move it.");
            startTimerHz (25);
        }
        ~MultibandDisplay() override { stopTimer(); }
        std::function<void (int)> onSelect;
        int selected = 0;
        int analyzerMode = AnalyzerPair::Both;

        float xFor (double f) const { return (float) (std::log (f / 20.0) / std::log (1000.0)) * getWidth(); }
        double fFor (float x) const { return 20.0 * std::pow (1000.0, juce::jlimit (0.0f, 1.0f, x / (float) getWidth())); }
        float yFor (float db) const { return getHeight() * 0.45f - db / 12.0f * getHeight() * 0.4f; }

        void paint (juce::Graphics& g) override
        {
            g.setColour (navy950);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
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
                g.setColour (amber.withAlpha (0.8f));
                g.drawText ((db > 0 ? "+" : "") + juce::String (db), getWidth() - 30, (int) yFor ((float) db) - 7, 26, 14, juce::Justification::centredRight);
            }
            const int count = juce::roundToInt (plain (proc.apvts, "mb_count"));
            for (int k = 0; k < count; ++k)
            {
                const auto pre = "mb" + juce::String (k + 1) + "_";
                const float x0 = xFor (plain (proc.apvts, pre + "lo")), x1 = xFor (plain (proc.apvts, pre + "hi"));
                const bool muted = plain (proc.apvts, pre + "mute") > 0.5f, bypassed = plain (proc.apvts, pre + "bypass") > 0.5f;
                const auto col = (muted || bypassed) ? steel : bandColour (k);
                g.setColour (col.withAlpha (k == selected ? 0.16f : 0.08f));
                g.fillRect (juce::Rectangle<float> (x0, 0, x1 - x0, (float) getHeight() - 18));
                g.setColour (col.withAlpha (0.7f));
                const float dashes[] = { 4.0f, 3.0f };
                g.drawDashedLine ({ x0, 0, x0, (float) getHeight() - 18 }, dashes, 2, 1.0f);
                g.drawDashedLine ({ x1, 0, x1, (float) getHeight() - 18 }, dashes, 2, 1.0f);
                // gain change of the band right now (reduction below the 0 line, boost above)
                const float change = proc.mbBandChange[(size_t) k].load() + plain (proc.apvts, pre + "gain");
                const float cx = (x0 + x1) * 0.5f, y = yFor (change);
                juce::Path hump;
                hump.startNewSubPath (x0, yFor (0));
                hump.quadraticTo (cx, y * 2.0f - yFor (0) * 1.0f, x1, yFor (0));
                g.setColour (col.withAlpha (0.35f));
                g.fillPath (hump);
                g.setColour (col);
                g.fillEllipse (cx - 7, y - 7, 14, 14);
                g.setColour (white);
                g.drawEllipse (cx - 7, y - 7, 14, 14, k == selected ? 2.5f : 1.2f);
                g.setFont (font (11.0f, 1));
                g.drawText ("Band " + juce::String (k + 1), juce::Rectangle<float> (cx - 30, 6, 60, 14), juce::Justification::centred);
                const juce::String flag = plain (proc.apvts, pre + "solo") > 0.5f ? "SOLO" : (muted ? "MUTE" : (bypassed ? "BYPASS" : ""));
                if (flag.isNotEmpty())
                {
                    g.setColour (flag == "SOLO" ? amber : mist);
                    g.drawText (flag, juce::Rectangle<float> (cx - 30, 20, 60, 14), juce::Justification::centred);
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
        }

    private:
        void timerCallback() override
        {
            analyzers.update (proc, proc.mbAnalyserPre, proc.mbAnalyserPost, analyzerMode);
            repaint();
        }
        KaminariVocalProcessor& proc;
        AnalyzerPair analyzers;
        int dragBand = -1, dragEdge = -1;
        float startLo = 0, startHi = 0, startX = 0;
    };

    class MultibandPage : public AdvFrame, private juce::Timer
    {
    public:
        explicit MultibandPage (KaminariVocalProcessor& p)
            : AdvFrame (p, "Multiband", "Bands only where needed" + dot() + "compress or expand, downward or upward", "mb_on", "multiband"),
              display (p), slope (p.apvts, "mb_slope", { "6", "12", "24" }, "Crossover slope in dB/oct"),
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
            slope.setBounds (r.removeFromLeft (96));
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

    // COMPRESSION: knee inset, scrolling level history, threshold line, meters.
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
            knee = b.removeFromLeft (juce::jmin (b.getHeight(), 150));
            meters = b.removeFromRight (56);
            main = b.reduced (4, 0);
            // knee / transfer curve
            g.setColour (navy900);
            g.fillRect (knee);
            g.setColour (navy800);
            for (int i = 1; i < 4; ++i) { g.drawVerticalLine (knee.getX() + knee.getWidth() * i / 4, (float) knee.getY(), (float) knee.getBottom());
                                          g.drawHorizontalLine (knee.getY() + knee.getHeight() * i / 4, (float) knee.getX(), (float) knee.getRight()); }
            const float th = plain (proc.apvts, "lv_thresh"), ratio = plain (proc.apvts, "lv_ratio"), kn = plain (proc.apvts, "lv_knee"), range = plain (proc.apvts, "lv_range");
            juce::Path tc;
            for (int i = 0; i <= 60; ++i)
            {
                const float in = -60.0f + i, out = in - std::min (range, kv::downwardGr (in, th, ratio, kn));
                const float x = knee.getX() + (in + 60.0f) / 60.0f * knee.getWidth(), y = knee.getBottom() - (out + 60.0f) / 60.0f * knee.getHeight();
                if (i == 0) tc.startNewSubPath (x, y); else tc.lineTo (x, y);
            }
            g.setColour (accent);
            g.strokePath (tc, juce::PathStrokeType (2.0f));
            g.setColour (amber.withAlpha (0.7f));
            const float tx = knee.getX() + (th + 60.0f) / 60.0f * knee.getWidth();
            g.drawVerticalLine ((int) tx, (float) knee.getY(), (float) knee.getBottom());

            // history: input area, output line, gain reduction from the top
            std::array<kv::LevelHistory<KaminariVocalProcessor::historySize>::Entry, (size_t) KaminariVocalProcessor::historySize> h;
            proc.compHistory.read (h);
            const int n = KaminariVocalProcessor::historySize;
            juce::Path inArea, outLine, grLine;
            inArea.startNewSubPath ((float) main.getX(), (float) main.getBottom());
            for (int i = 0; i < n; ++i)
            {
                const float x = main.getX() + main.getWidth() * (float) i / (n - 1);
                const float inDb = juce::Decibels::gainToDecibels (h[(size_t) i].in, -60.0f), outDb = juce::Decibels::gainToDecibels (h[(size_t) i].out, -60.0f);
                inArea.lineTo (x, yFor (inDb));
                if (i == 0) { outLine.startNewSubPath (x, yFor (outDb)); grLine.startNewSubPath (x, main.getY() + std::max (0.0f, h[0].gr) / 24.0f * main.getHeight()); }
                else { outLine.lineTo (x, yFor (outDb)); grLine.lineTo (x, main.getY() + juce::jlimit (0.0f, 1.0f, h[(size_t) i].gr / 24.0f) * main.getHeight()); }
            }
            inArea.lineTo ((float) main.getRight(), (float) main.getBottom());
            inArea.closeSubPath();
            g.setColour (mist.withAlpha (0.22f));
            g.fillPath (inArea);
            g.setColour (white.withAlpha (0.65f));
            g.strokePath (outLine, juce::PathStrokeType (1.0f));
            g.setColour (accent);
            g.strokePath (grLine, juce::PathStrokeType (2.0f));
            const float dashes[] = { 4.0f, 4.0f };
            g.setColour (white.withAlpha (0.7f));
            g.drawDashedLine ({ (float) main.getX(), yFor (th), (float) main.getRight(), yFor (th) }, dashes, 2, 1.0f);
            g.setColour (amber);
            g.setFont (font (11.0f, 0));
            g.drawText ("threshold " + minusSign (th, 0) + " dB", main.getRight() - 120, (int) yFor (th) + 2, 116, 14, juce::Justification::centredRight);
            g.setColour (mist);
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
        void timerCallback() override { repaint(); }
        KaminariVocalProcessor& proc;
        juce::Rectangle<int> knee, main, meters;
    };

    class CompressionPage : public AdvFrame
    {
    public:
        explicit CompressionPage (KaminariVocalProcessor& p)
            : AdvFrame (p, "Compression", "Style, timing, parallel Dry, auto gain", "lv_on", "compression"),
              display (p), scEq (p, KaminariVocalProcessor::EqSideChain),
              view ({ "Level", "Side-chain EQ" }, "Compression display"),
              thresh (p.apvts, "lv_thresh", "Threshold", "-50 dB", "0 dB"),
              ratio (p.apvts, "lv_ratio", "Ratio", "1:1", "20:1"),
              attack (p.apvts, "lv_attack", "Attack", "fast", "slow"),
              release (p.apvts, "lv_release", "Release", "fast", "slow"),
              mix (p.apvts, "lv_mix", "Mix", "0 %", "200 %"),
              out (p.apvts, "lv_out_gain", "Output"),
              dry (p.apvts, "lv_dry", "Dry"),
              wet (p.apvts, "lv_wet_gain", "Wet"),
              style (p.apvts, "lv_style", "STYLE", hintFor ("lv_style")),
              knee (p.apvts, "lv_knee", "Knee", "hard", "soft"),
              range (p.apvts, "lv_range", "Range", "0 dB", "40 dB"),
              lookahead (p.apvts, "lv_lookahead", "Lookahead", "0 ms", "20 ms"),
              hold (p.apvts, "lv_hold", "Hold", "0 ms", "500 ms"),
              scLevel (p.apvts, "lv_sc_level", "Side chain level", "-36", "+36"),
              link (p.apvts, "lv_stereo_link", "Stereo link", "0 %", "100 %"),
              detector (p.apvts, "lv_detector", { "Peak", "Smooth" }),
              autoRelease (p.apvts, "lv_auto_release", "AUTO", "AUTO", hintFor ("lv_auto_release")),
              autoGain (p.apvts, "lv_auto_gain", "AUTO GAIN", "AUTO GAIN", hintFor ("lv_auto_gain")),
              styleAtt (*p.apvts.getParameter ("lv_style"), [this] (float v)
              {
                  // Vocal style sets its own ratio (2:1 .. 8:1 from the overshoot): the Ratio knob does nothing there
                  ratio.setInactive (juce::roundToInt (v) == kv::CompressorSettings::Vocal ? "Auto" : "",
                                     "Vocal style sets the ratio automatically (2:1 to 8:1, rising with the level above the threshold). "
                                     "Choose another style to set it by hand.");
              })
        {
            for (auto* c : std::initializer_list<juce::Component*> { &display, &thresh, &ratio, &attack, &release, &mix, &out, &dry, &wet, &style,
                                                                     &knee, &range, &lookahead, &hold, &scLevel, &link, &detector, &autoRelease, &autoGain,
                                                                     &view })
                addAndMakeVisible (c);
            addChildComponent (scEq);
            view.setSelected (0);
            view.buttons[1]->setTooltip ("Side-chain detection EQ: shapes what the compressor reacts to (the audio is not filtered). "
                                         "Same editor as the main EQ: click to add a band, drag, wheel = Q.");
            view.onChange = [this] (int v) { showSideChain (v == 1); };
            for (auto* k : { &thresh, &ratio, &attack, &release, &mix, &out, &dry, &wet }) k->setLNF (&lnf);
            styleAtt.sendInitialUpdate();
        }

        RangeKnob& ratioKnob() { return ratio; }
        EqEditor& sideChainEq() { return scEq; }
        // Side-chain EQ view: the EQ editor takes the display and the controls area (it needs the room of the main EQ
        // page for its band panel); "Level" brings the compressor's display and controls back.
        void showSideChain (bool sc)
        {
            scShown = sc;
            view.setSelected (sc ? 1 : 0);
            display.setVisible (! sc);
            scEq.setVisible (sc);
            for (auto* c : mainControls()) c->setVisible (! sc);
            if (! sc) proc.scEqSolo.store (-1);
            resized();
            repaint();
        }
        bool sideChainShown() const noexcept { return scShown; }

        void paint (juce::Graphics& g) override
        {
            AdvFrame::paint (g);
            if (! scShown) drawGroup (g, controls);
            drawGroup (g, sideChain);
            g.setColour (mist);
            g.setFont (font (11.0f, 2, 0.1f));
            g.drawText ("SIDE CHAIN", sideChain.getX() + 12, sideChain.getY() + 6, 200, 14, juce::Justification::centredLeft);
            g.drawText ("DISPLAY", view.getX(), view.getY() - 16, 120, 14, juce::Justification::centredLeft);
            g.drawText ("DETECTOR", detector.getX(), detector.getY() - 16, 120, 14, juce::Justification::centredLeft);
        }

        void layoutContent (juce::Rectangle<int> b) override
        {
            const int scH = 78;
            const auto displayArea = b.removeFromTop (b.getHeight() - scH - 10 - 210 - 10);
            display.setBounds (displayArea);
            b.removeFromTop (10);
            sideChain = b.removeFromBottom (scH);
            b.removeFromBottom (10);
            controls = b;
            auto c = controls.reduced (12, 10);
            auto row = c.removeFromTop (128);
            c.removeFromTop (8);
            auto sliders = c.removeFromTop (46);
            const int w = row.getWidth() / 9;
            thresh.setBounds (row.removeFromLeft (w + 20));
            ratio.setBounds (row.removeFromLeft (w));
            style.setBounds (row.removeFromLeft (w + 20).withSizeKeepingCentre (w + 10, 46));
            attack.setBounds (row.removeFromLeft (w));
            auto rel = row.removeFromLeft (w);
            release.setBounds (rel.removeFromTop (rel.getHeight() - 26));
            autoRelease.setBounds (rel.withSizeKeepingCentre (60, 22));
            mix.setBounds (row.removeFromLeft (w).removeFromTop (102));
            auto right = row;
            auto top = right.removeFromTop (96);
            const int w2 = top.getWidth() / 3;
            out.setBounds (top.removeFromLeft (w2));
            dry.setBounds (top.removeFromLeft (w2));
            wet.setBounds (top);
            autoGain.setBounds (right.withSizeKeepingCentre (110, 24));
            const int sw = (sliders.getWidth() - 3 * 30) / 4;
            for (auto* s : { &knee, &range, &lookahead, &hold }) { s->setBounds (sliders.removeFromLeft (sw)); sliders.removeFromLeft (30); }
            auto sc = sideChain.reduced (12, 8).withTrimmedTop (20);
            scLevel.setBounds (sc.removeFromLeft (200).withHeight (44));
            sc.removeFromLeft (40);
            link.setBounds (sc.removeFromLeft (200).withHeight (44));
            sc.removeFromLeft (40);
            detector.setBounds (sc.removeFromLeft (130).withHeight (26).translated (0, 6));
            view.setBounds (sc.removeFromRight (240).withHeight (26).translated (0, 6));
            scEq.setBounds (displayArea.getUnion (controls));
        }

    private:
        ModuleLNF lnf;
        CompDisplay display;
        EqEditor scEq;
        Segmented view;
        bool scShown = false;
        std::vector<juce::Component*> mainControls()
        {
            return { &thresh, &ratio, &attack, &release, &mix, &out, &dry, &wet, &style, &knee, &range, &lookahead, &hold, &autoRelease, &autoGain };
        }
        RangeKnob thresh, ratio, attack, release, mix, out, dry, wet;
        ChoiceBox style;
        HSlider knee, range, lookahead, hold, scLevel, link;
        SegParam detector;
        ToggleBox autoRelease, autoGain;
        juce::ParameterAttachment styleAtt;
        juce::Rectangle<int> controls, sideChain;
    };

    //==================================================================================================================
    // DE-ESS: waveform with the de-essed parts highlighted, threshold line, meters.
    class DeEssDisplay : public juce::Component, private juce::Timer
    {
    public:
        explicit DeEssDisplay (KaminariVocalProcessor& p) : proc (p) { setTitle ("De-ess display"); startTimerHz (30); }
        ~DeEssDisplay() override { stopTimer(); }
        void paint (juce::Graphics& g) override
        {
            g.setColour (navy950);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
            auto b = getLocalBounds().reduced (6);
            auto meters = b.removeFromRight (56);
            auto scale = b.removeFromRight (34);
            std::array<kv::LevelHistory<KaminariVocalProcessor::historySize>::Entry, (size_t) KaminariVocalProcessor::historySize> h;
            proc.deessHistory.read (h);
            const int n = KaminariVocalProcessor::historySize;
            const float mid = (float) b.getCentreY(), half = b.getHeight() * 0.5f;
            auto amp = [&] (float lin) { return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (lin, -48.0f) + 48.0f) / 48.0f) * half; };
            g.setColour (navy800);
            for (int db : { 0, -6, -12, -18, -24, -36, -48 })
            {
                const float y = b.getY() + (float) -db / 48.0f * b.getHeight();
                g.drawHorizontalLine ((int) y, (float) b.getX(), (float) b.getRight());
                g.setColour (mist);
                g.setFont (font (10.0f, 0));
                g.drawText (juce::String (db), scale.getX(), (int) y - 6, scale.getWidth() - 4, 12, juce::Justification::centredRight);
                g.setColour (navy800);
            }
            for (int i = 0; i < n; ++i)
            {
                const float x = b.getX() + b.getWidth() * (float) i / n;
                const float a = amp (h[(size_t) i].in);
                const bool essed = h[(size_t) i].gr > 0.5f;
                g.setColour (essed ? accent.withAlpha (0.85f) : mist.withAlpha (0.45f));
                g.fillRect (juce::Rectangle<float> (x, mid - a, juce::jmax (1.0f, b.getWidth() / (float) n - 0.4f), a * 2));
            }
            const float th = plain (proc.apvts, "ds_thresh");
            const float ty = mid - juce::jlimit (0.0f, 1.0f, (th + 48.0f) / 48.0f) * half;
            g.setColour (amber);
            g.drawHorizontalLine ((int) ty, (float) b.getX(), (float) b.getRight());
            auto m = meters.toFloat().reduced (4, 2);
            const float w = (m.getWidth() - 8) / 3;
            drawVMeter (g, m.removeFromLeft (w), juce::Decibels::gainToDecibels (h[(size_t) n - 1].in, -60.0f), false, mist);
            m.removeFromLeft (4);
            drawVMeter (g, m.removeFromLeft (w), juce::Decibels::gainToDecibels (h[(size_t) n - 1].out, -60.0f), false, white);
            m.removeFromLeft (4);
            drawVMeter (g, m, proc.moduleGr[KaminariVocalProcessor::ModDeEss].load(), true, accent);
        }
    private:
        void timerCallback() override { repaint(); }
        KaminariVocalProcessor& proc;
    };

    // Two-handle frequency range (2-20 kHz, log) for the de-esser detection band. Drag the band to move both edges.
    class FreqRange : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        FreqRange (APVTS& s) : state (s) { setTitle ("De-ess detection range"); setTooltip ("Drag a handle to move one edge, or the band to move both."); }
        float xFor (float f) const { return (float) (std::log (f / 2000.0) / std::log (10.0)) * (getWidth() - 12.0f) + 6.0f; }
        float fFor (float x) const { return 2000.0f * std::pow (10.0f, juce::jlimit (0.0f, 1.0f, (x - 6.0f) / (getWidth() - 12.0f))); }
        void paint (juce::Graphics& g) override
        {
            const float lo = plain (state, "ds_det_lo"), hi = plain (state, "ds_det_hi");
            auto r = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), 20.0f);
            g.setColour (navy950);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (navy600);
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
            const float x0 = xFor (juce::jmax (2000.0f, lo)), x1 = xFor (hi);
            g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.3f), x0, 0, accent.withAlpha (0.7f), (x0 + x1) * 0.5f, 0, false));
            g.fillRect (juce::Rectangle<float> (x0, r.getY() + 2, x1 - x0, r.getHeight() - 4));
            for (float x : { x0, x1 })
            {
                juce::Path tri;
                tri.addTriangle (x - 7, r.getBottom(), x + 7, r.getBottom(), x, r.getY());
                g.setColour (white);
                g.fillPath (tri);
            }
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const float x0 = xFor (plain (state, "ds_det_lo")), x1 = xFor (plain (state, "ds_det_hi"));
            mode = std::abs (e.position.x - x0) < 9 ? 0 : (std::abs (e.position.x - x1) < 9 ? 1 : (e.position.x > x0 && e.position.x < x1 ? 2 : -1));
            startX = e.position.x; startLo = plain (state, "ds_det_lo"); startHi = plain (state, "ds_det_hi");
            for (auto* id : { "ds_det_lo", "ds_det_hi" }) state.getParameter (id)->beginChangeGesture();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            auto set = [this] (const char* id, float v) { auto* p = state.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
            if (mode == 0) set ("ds_det_lo", juce::jmin (fFor (e.position.x), plain (state, "ds_det_hi") / 1.2f));
            else if (mode == 1) set ("ds_det_hi", juce::jmax (fFor (e.position.x), plain (state, "ds_det_lo") * 1.2f));
            else if (mode == 2)
            {
                const float k = fFor (e.position.x) / fFor (startX);
                set ("ds_det_lo", startLo * k);
                set ("ds_det_hi", startHi * k);
            }
            repaint();
        }
        void mouseUp (const juce::MouseEvent&) override { for (auto* id : { "ds_det_lo", "ds_det_hi" }) state.getParameter (id)->endChangeGesture(); }
    private:
        APVTS& state;
        int mode = -1;
        float startX = 0, startLo = 0, startHi = 0;
    };

    class DeEssPage : public AdvFrame, private juce::Timer
    {
    public:
        explicit DeEssPage (KaminariVocalProcessor& p)
            : AdvFrame (p, "De-ess", "Highlighted parts are being de-essed" + dot() + "timing is automatic", "ds_on", "deess"),
              display (p), range (p.apvts),
              thresh (p.apvts, "ds_thresh", "Threshold", "-60 dB", "0 dB"),
              rangeKnob (p.apvts, "ds_range", "Range", "0 dB", "24 dB"),
              link (p.apvts, "ds_stereo_link", "Stereo link", "0 %", "100 %"),
              lookahead (p.apvts, "ds_lookahead", "Lookahead", "0 ms", "15 ms"),
              mode (p.apvts, "ds_mode", { "SINGLE VOCAL", "ALLROUND" }),
              process (p.apvts, "ds_process", { "SPLIT BAND", "WIDE BAND" }),
              detect (p.apvts, "ds_detect", { "VOICE FOCUS", "FULL BAND" }),
              linkMode (p.apvts, "ds_link_mode", { "STEREO", "MID", "SIDE" }),
              listen (p.apvts, "ds_listen", "Audition", "Audition", hintFor ("ds_listen")),
              trigger (p.apvts, "ds_audition_trigger", "Removed only", "Removed only", hintFor ("ds_audition_trigger")),
              os (p.apvts, "ds_os", { "OFF", "2X", "4X" }, "Oversampling: runs the de-esser at 2x or 4x the session rate. Adds latency while De-ess is on.")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &display, &range, &thresh, &rangeKnob, &link, &lookahead, &mode, &process,
                                                                     &detect, &linkMode, &listen, &trigger, &os })
                addAndMakeVisible (c);
            for (auto* k : { &thresh, &rangeKnob, &link, &lookahead }) k->setLNF (&lnf);
            startTimerHz (10);
        }
        ~DeEssPage() override { stopTimer(); }

        void paint (juce::Graphics& g) override
        {
            AdvFrame::paint (g);
            drawGroup (g, controls);
            g.setColour (accent);
            g.setFont (font (14.0f, 2));
            g.drawText (kvp::freqText (plain (proc.apvts, "ds_det_lo")), range.getX() - 10, range.getBottom() + 4, 80, 18, juce::Justification::centredLeft);
            g.drawText (kvp::freqText (plain (proc.apvts, "ds_det_hi")), range.getRight() - 70, range.getBottom() + 4, 80, 18, juce::Justification::centredRight);
            g.setColour (mist);
            g.setFont (font (11.0f, 1, 0.1f));
            for (auto* c : std::initializer_list<juce::Component*> { &mode, &process, &detect, &linkMode, &os })
            {
                const char* t = c == &mode ? "MODE" : c == &process ? "PROCESSING" : c == &detect ? "DETECTION" : c == &os ? "OVERSAMPLING" : "CHANNELS";
                g.drawText (t, c->getX() - 110, c->getY(), 100, c->getHeight(), juce::Justification::centredRight);
            }
        }

        void layoutContent (juce::Rectangle<int> b) override
        {
            display.setBounds (b.removeFromTop (190));
            b.removeFromTop (10);
            controls = b.removeFromTop (230);
            auto c = controls.reduced (16, 12);
            auto left = c.removeFromLeft (300);
            auto knobs = left.removeFromTop (130);
            thresh.setBounds (knobs.removeFromLeft (150));
            rangeKnob.setBounds (knobs);
            left.removeFromTop (8);
            range.setBounds (left.removeFromTop (24));
            listen.setBounds (left.withTrimmedTop (28).withSizeKeepingCentre (110, 26).withY (left.getY() + 26));
            auto right = c.removeFromRight (170);
            link.setBounds (right.removeFromTop (100));
            lookahead.setBounds (right.removeFromTop (100));
            c.removeFromLeft (130);
            auto rows = c.withSizeKeepingCentre (c.getWidth(), 5 * 32 + 28);
            for (auto* s : std::initializer_list<juce::Component*> { &mode, &process, &detect, &linkMode, &os })
            {
                s->setBounds (rows.removeFromTop (27).removeFromLeft (s == &linkMode || s == &os ? 210 : 230));
                rows.removeFromTop (5);
            }
            trigger.setBounds (rows.removeFromTop (26).removeFromLeft (130));
        }

    private:
        void timerCallback() override { repaint (controls); }
        ModuleLNF lnf;
        DeEssDisplay display;
        FreqRange range;
        RangeKnob thresh, rangeKnob, link, lookahead;
        SegParam mode, process, detect, linkMode;
        ToggleBox listen, trigger;
        SegParam os;
        juce::Rectangle<int> controls;
    };

    //==================================================================================================================
    // RESONANCE
    class ResGraph : public juce::Component, private juce::Timer
    {
    public:
        explicit ResGraph (KaminariVocalProcessor& p) : proc (p) { setTitle ("Resonance graph"); startTimerHz (25); }
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
            // depth curve
            juce::Path curve;
            for (int x = 0; x <= getWidth(); x += 3)
            {
                const double f = fFor ((float) x);
                double db = 0;
                for (int k = 0; k < 8; ++k) db += bandDb (k, f);
                const float y = juce::jlimit (0.0f, (float) getHeight(), yCurve (db));
                if (x == 0) curve.startNewSubPath ((float) x, y); else curve.lineTo ((float) x, y);
            }
            g.setColour (white);
            g.strokePath (curve, juce::PathStrokeType (2.0f));
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
        double bandDb (int k, double f) const
        {
            if (! used (k)) return 0;
            const auto pre = "rs_b" + juce::String (k + 1) + "_";
            if (plain (proc.apvts, pre + "on") < 0.5f) return 0;
            static const int shapeToType[8] = { kv::LowCut, kv::LowShelf, kv::HighShelf, kv::HighCut, kv::Bell, kv::BandPass, kv::Notch, kv::TiltShelf };
            kv::EqBandSettings e;
            e.type = shapeToType[juce::jlimit (0, 7, juce::roundToInt (plain (proc.apvts, pre + "shape")))];
            e.freq = plain (proc.apvts, pre + "freq"); e.gainDb = plain (proc.apvts, pre + "depth"); e.q = plain (proc.apvts, pre + "q");
            double m = kv::EqDesign::make (e, 48000.0).magnitudeDb (std::min (f, 23000.0), 48000.0);
            if (e.type == kv::BandPass) m = std::max (-24.0, m) + e.gainDb;
            return juce::jlimit (-24.0, 24.0, m);
        }
        void timerCallback() override
        {
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
        void timerCallback() override { panel.resized(); panel.repaint(); }

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

    //==================================================================================================================
    // DISTORTION
    // Choice parameter shown as a grid of tiles (name + short description).
    class ChoiceTiles : public juce::Component
    {
    public:
        ChoiceTiles (APVTS& s, const char* id, std::vector<std::pair<juce::String, juce::String>> items, int columns)
            : param (*s.getParameter (id)), entries (std::move (items)), cols (columns),
              att (param, [this] (float v) { selected = juce::roundToInt (v); repaint(); if (onChange) onChange (selected); }, nullptr)
        {
            setTitle (param.getName (64));
            att.sendInitialUpdate();
        }
        std::function<void (int)> onChange;
        int current() const { return selected; }
        const juce::String& description (int i) const { return entries[(size_t) juce::jlimit (0, (int) entries.size() - 1, i)].second; }
        void paint (juce::Graphics& g) override
        {
            for (int i = 0; i < (int) entries.size(); ++i)
            {
                const bool on = i == selected, hover = i == hovered;
                auto r = cell (i).toFloat().reduced (on ? 1.0f : 0.5f);
                g.setColour (on ? navy800 : (hover ? navy800.withAlpha (0.6f) : navy900));
                g.fillRoundedRectangle (r, 5.0f);
                g.setColour (on ? accent : navy600);
                g.drawRoundedRectangle (r, 5.0f, on ? 1.6f : 1.0f);
                g.setColour (on ? white : white.withAlpha (0.85f));
                g.setFont (font (14.0f, on ? 2 : 1, 0.04f));
                g.drawText (entries[(size_t) i].first, r.reduced (12.0f, 0.0f), juce::Justification::centredLeft);
                if (on)
                {
                    g.setColour (accent);
                    g.fillEllipse (r.getRight() - 16.0f, r.getCentreY() - 3.5f, 7.0f, 7.0f);
                }
            }
        }
        void mouseMove (const juce::MouseEvent& e) override { const int h = hit (e.getPosition()); if (h != hovered) { hovered = h; repaint(); } }
        void mouseExit (const juce::MouseEvent&) override { hovered = -1; repaint(); }
        void mouseDown (const juce::MouseEvent& e) override { const int h = hit (e.getPosition()); if (h >= 0) att.setValueAsCompleteGesture ((float) h); }
    private:
        juce::Rectangle<int> cell (int i) const
        {
            const int rows = ((int) entries.size() + cols - 1) / cols, gap = 6;
            const int w = (getWidth() - (cols - 1) * gap) / cols, h = (getHeight() - (rows - 1) * gap) / rows;
            return { (i % cols) * (w + gap), (i / cols) * (h + gap), w, h };
        }
        int hit (juce::Point<int> p) const { for (int i = 0; i < (int) entries.size(); ++i) if (cell (i).contains (p)) return i; return -1; }
        juce::RangedAudioParameter& param;
        std::vector<std::pair<juce::String, juce::String>> entries;
        int cols, selected = 0, hovered = -1;
        juce::ParameterAttachment att;
    };

    // Transfer curve of the selected style at the current Drive and Bias, with the latest peak marked.
    class DistortionCurve : public juce::Component, private juce::Timer
    {
    public:
        explicit DistortionCurve (KaminariVocalProcessor& p) : proc (p) { setTitle ("Distortion curve"); startTimerHz (20); }
        ~DistortionCurve() override { stopTimer(); }
        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (navy950);
            g.fillRoundedRectangle (b, 5.0f);
            g.setColour (navy600);
            g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, 1.0f);
            auto plot = b.reduced (14.0f, 14.0f).withTrimmedTop (8.0f);
            plot = plot.withSizeKeepingCentre (juce::jmin (plot.getWidth(), plot.getHeight()), juce::jmin (plot.getWidth(), plot.getHeight()));
            g.setColour (navy800);
            g.drawHorizontalLine (juce::roundToInt (plot.getCentreY()), plot.getX(), plot.getRight());
            g.drawVerticalLine (juce::roundToInt (plot.getCentreX()), plot.getY(), plot.getBottom());
            g.drawRect (plot, 1.0f);
            // unity line
            g.setColour (steel.withAlpha (0.6f));
            g.drawLine (plot.getX(), plot.getBottom(), plot.getRight(), plot.getY(), 1.0f);

            auto& s = proc.apvts;
            const int style = juce::roundToInt (plainValue (s, "dt_style"));
            const float drive = juce::Decibels::decibelsToGain (plainValue (s, "dt_drive"));
            const float bias = plainValue (s, "dt_bias") * 0.01f;
            juce::Path path;
            const int n = (int) plot.getWidth();
            for (int i = 0; i <= n; ++i)
            {
                const float x = -1.0f + 2.0f * (float) i / (float) n;
                const float y = juce::jlimit (-1.2f, 1.2f, kv::Distortion::curve (x * drive, style, bias));
                const float px = plot.getX() + (float) i, py = plot.getCentreY() - y * plot.getHeight() * 0.5f;
                if (i == 0) path.startNewSubPath (px, py); else path.lineTo (px, py);
            }
            g.setColour (accent);
            g.strokePath (path, juce::PathStrokeType (2.0f));

            // latest peak into the shaper, as an input level on the curve
            const bool on = choiceIndex (s, "dt_on") != 0;
            if (on && peak > 1.0e-4f)
            {
                const float xin = juce::jlimit (0.0f, 1.0f, peak / drive);
                const float y = juce::jlimit (-1.2f, 1.2f, kv::Distortion::curve (xin * drive, style, bias));
                const float px = plot.getCentreX() + xin * plot.getWidth() * 0.5f, py = plot.getCentreY() - y * plot.getHeight() * 0.5f;
                g.setColour (amber.withAlpha (0.3f));
                g.fillEllipse (px - 8.0f, py - 8.0f, 16.0f, 16.0f);
                g.setColour (amber);
                g.fillEllipse (px - 4.0f, py - 4.0f, 8.0f, 8.0f);
            }
            g.setColour (mist);
            g.setFont (font (11.0f, 0));
            g.drawText ("in", plot.withY (plot.getBottom() + 1.0f).withHeight (12.0f), juce::Justification::centredRight);
            g.drawText ("out", juce::Rectangle<float> (b.getX() + 8.0f, b.getY() + 6.0f, 40.0f, 12.0f), juce::Justification::centredLeft);
            g.setColour (on ? accent : steel);
            g.drawText (on ? "peak " + juce::String (overDb, 1) + " dB into the curve" : juce::String ("module off"),
                        b.reduced (8.0f, 6.0f).removeFromTop (12.0f), juce::Justification::centredRight);
        }
    private:
        void timerCallback() override
        {
            const float o = proc.distortion.peakOver.load();
            overDb = o;
            const float lin = juce::Decibels::decibelsToGain (o);
            peak = o > 0.0f ? lin : peak * 0.8f;
            repaint();
        }
        KaminariVocalProcessor& proc;
        float peak = 0, overDb = 0;
    };

    class DistortionPage : public AdvFrame
    {
    public:
        explicit DistortionPage (KaminariVocalProcessor& p)
            : AdvFrame (p, "Distortion", "Saturation and drive" + dot() + "after Compression, before De-ess", "dt_on", "distortion"),
              curve (p),
              styles (p.apvts, "dt_style", { { "Tape", "Soft, symmetric saturation; highs soften as Drive rises." },
                                             { "Tube", "Asymmetric saturation that adds even harmonics. Bias adds more." },
                                             { "Warm", "The gentlest curve: thickens without obvious distortion." },
                                             { "Fuzz", "High-gain clipping for aggressive, buzzy tones." },
                                             { "Clip", "Hard clipping: bright and edgy." },
                                             { "Lo-Fi", "Soft clip plus bit and sample-rate reduction (Crush)." } }, 3),
              drive (p.apvts, "dt_drive", "Drive", "0 DB", "36 DB", "Gain into the saturation curve. More drive = more distortion."),
              bias (p.apvts, "dt_bias", "Bias", "SYM", "ASYM", "Makes the curve asymmetric, adding even harmonics."),
              crush (p.apvts, "dt_crush", "Crush", "OFF", "MAX", "Lo-Fi: fewer bits and a lower sample rate."),
              lowCut (p.apvts, "dt_lowcut", "Low Cut", "OFF", "1K", "Removes lows before the curve so they stay clean and tight."),
              tone (p.apvts, "dt_tone", "Tone", "DARK", "BRIGHT", "Tilts the distorted sound darker or brighter around 1 kHz."),
              mix (p.apvts, "dt_mix", "Mix", "DRY", "WET", "Blend of distorted and clean vocal. Lower values give parallel distortion."),
              out (p.apvts, "dt_out", "Output", "-24", "+12", "Level of the distorted signal."),
              autoGain (p.apvts, "dt_auto_gain", "AUTO GAIN", "AUTO GAIN", "Keeps the distorted level close to the input level, so Drive changes tone, not loudness."),
              os (p.apvts, "dt_os", { "OFF", "2X", "4X" }, "Oversampling reduces harsh aliasing at high Drive. Adds a few samples of latency while the module is on.")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &curve, &styles, &os, &outOptions })
                addAndMakeVisible (c);
            outOptions.addAndMakeVisible (autoGain);
            for (auto* k : { &drive, &bias, &crush, &lowCut, &tone, &mix, &out })
            {
                addAndMakeVisible (k);
                k->setLNF (&lnf);
                k->setLabelOverhang (2);
            }
            styles.onChange = [this] (int st) { crush.setVisible (st == kv::DistortionSettings::LoFi); if (! getBounds().isEmpty()) resized(); repaint(); };
            crush.setVisible (styles.current() == kv::DistortionSettings::LoFi);
        }
        ~DistortionPage() override { for (auto* k : { &drive, &bias, &crush, &lowCut, &tone, &mix, &out }) k->setLNF (nullptr); }

        void paint (juce::Graphics& g) override
        {
            AdvFrame::paint (g);
            g.setColour (mist);
            g.setFont (font (11.0f, 1, 0.1f));
            g.drawText ("STYLE", styles.getX(), styles.getY() - 18, 100, 14, juce::Justification::centredLeft);
            g.setColour (white);
            g.setFont (font (13.0f, 0));
            g.drawFittedText (styles.description (styles.current()), descArea, juce::Justification::topLeft, 2, 1.0f);
            for (auto* grp : { &driveGrp, &toneGrp, &outGrp, &qualGrp })
                if (! grp->area.isEmpty())
                    drawTitledGroup (g, grp->area, grp->title);
            const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
            const int lat = proc.distortion.latencyFor (choiceIndex (proc.apvts, "dt_os"));
            g.setColour (mist);
            g.setFont (font (11.5f, 0));
            g.drawFittedText ("Latency while on: " + juce::String (lat) + " smp (" + juce::String (1000.0 * lat / sr, 2) + " ms)",
                              os.getBounds().translated (0, 34).withHeight (30).expanded (6, 0), juce::Justification::centredTop, 2, 1.0f);
        }

        void layoutContent (juce::Rectangle<int> b) override
        {
            auto top = b.removeFromTop (260);
            curve.setBounds (top.removeFromLeft (320));
            top.removeFromLeft (20);
            top.removeFromTop (22);
            styles.setBounds (top.removeFromTop (150));
            top.removeFromTop (12);
            descArea = top.removeFromTop (40);
            b.removeFromTop (16);
            layoutGroups (b.removeFromTop (juce::jmin (b.getHeight(), 200)), { &driveGrp, &toneGrp, &outGrp, &qualGrp }, 104, 22);
            if (! outOptions.getBounds().isEmpty()) autoGain.setBounds (outOptions.getLocalBounds().withSizeKeepingCentre (outOptions.getWidth(), 28));
        }

    private:
        ModuleLNF lnf;
        DistortionCurve curve;
        ChoiceTiles styles;
        juce::Rectangle<int> descArea;

    public:
        RangeKnob drive, bias, crush, lowCut, tone, mix, out;
        ToggleBox autoGain;
        SegParam os;

    private:
        juce::Component outOptions;
        SendGroup driveGrp { "DRIVE", { &drive, &bias, &crush }, &drive, 30 }, toneGrp { "TONE", { &lowCut, &tone } },
                  outGrp { "OUTPUT", { &mix, &out }, nullptr, 0, &outOptions, 120, 28 },
                  qualGrp { "OVERSAMPLING", {}, nullptr, 0, &os, 150, 28, -14 };
    };
}
