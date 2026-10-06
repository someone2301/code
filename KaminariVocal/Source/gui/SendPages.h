#pragma once

#include "AdvWidgets.h"

// Advanced send pages (Reverb, Delay, Widener) and the Flanger module page, laid out after the GUI preview artboards AdvReverb, AdvDelay,
// AdvWidener and AdvWidenerSide: a shared send header, then titled control groups with range-labelled knobs and a
// small display of what the send does. Only controls that change the sound in the current mode are shown.
namespace kvui
{
    using namespace kvtheme;

    //==================================================================================================================
    // ON / OFF button with the bolt icon, bound to a bool parameter.
    class OnButton : public juce::Button
    {
    public:
        OnButton (APVTS& s, const char* id, const juce::String& what) : juce::Button (what + " on/off")
        {
            setClickingTogglesState (true);
            setTitle (what + " on/off");
            setTooltip (what + ": click to switch the send and its return on or off. Off fades the return out.");
            setWantsKeyboardFocus (true);
            att = std::make_unique<APVTS::ButtonAttachment> (s, id, *this);
        }
        void paintButton (juce::Graphics& g, bool hover, bool) override
        {
            const bool on = getToggleState();
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (hover ? navy800.brighter (0.08f) : navy800);
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (on ? accent : navy600);
            g.drawRoundedRectangle (r, 5.0f, on ? 1.4f : 1.0f);
            auto b = getLocalBounds().reduced (10, 0);
            const auto bolt = boltPath (b.removeFromLeft (10).toFloat().withSizeKeepingCentre (9.0f, 13.0f));
            if (on) { g.setColour (accent); g.fillPath (bolt); }
            else    { g.setColour (mist); g.strokePath (bolt, juce::PathStrokeType (1.1f)); }
            g.setColour (on ? white : mist);
            g.setFont (font (13.0f, 2, 0.08f));
            g.drawText (on ? "ON" : "OFF", b, juce::Justification::centred);
        }
    private:
        std::unique_ptr<APVTS::ButtonAttachment> att;
    };

    //==================================================================================================================
    // On / send level / tap point / return meter / module presets: the band at the top of every send page.
    class SendHeader : public juce::Component, private juce::Timer
    {
    public:
        SendHeader (KaminariVocalProcessor& p, int send, const char* onId, const char* levelId, const char* tapId, const juce::String& name,
                    const juce::String& moduleKey)
            : preset (p.presets, moduleKey, name.toLowerCase()),
              power (p.apvts, onId, name),
              level (p.apvts, levelId, "Send", {}, {}, "Level sent to the " + name.toLowerCase() + ". Off sends nothing; the dry vocal is unchanged."),
              tap (p.apvts, tapId, { "Post-fader", "Pre-fader" }, "Post-fader follows Output Gain. Pre-fader taps the vocal before Output Gain."),
              proc (p), sendIndex (send), title (name.toUpperCase())
        {
            for (auto* c : std::initializer_list<juce::Component*> { &power, &level, &tap, &preset })
                addAndMakeVisible (c);
            level.setLNF (&lnf);
            level.value.setColour (juce::Label::textColourId, white);
            startTimerHz (30);
        }
        ~SendHeader() override { stopTimer(); level.setLNF (nullptr); }

        void paint (juce::Graphics& g) override
        {
            g.setColour (navy800.withAlpha (0.55f));
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
            drawCaption (g, tap, "TAP POINT");
            drawCaption (g, preset, title + " PRESET");
            // return meter
            g.setColour (mist);
            g.setFont (font (11.0f, 1, 0.1f));
            g.drawText ("RETURN", meterArea.getX(), meterArea.getY() - 17, 120, 14, juce::Justification::centredLeft);
            auto bar = meterArea.toFloat();
            g.setColour (navy950);
            g.fillRoundedRectangle (bar, 2.0f);
            g.setColour (navy600);
            g.drawRoundedRectangle (bar.reduced (0.5f), 2.0f, 1.0f);
            const float db = juce::Decibels::gainToDecibels (meter, -60.0f);
            g.setColour (db > -1.0f ? amber : accent);
            g.fillRoundedRectangle (bar.reduced (2.0f).withWidth ((bar.getWidth() - 4.0f) * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f)), 1.5f);
            g.setColour (mist);
            g.setFont (font (12.0f, 0));
            g.drawText (db <= -59.9f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92\xe2\x88\x9e dB")) : minusSign (db, 1) + " dB",
                        meterArea.getX(), meterArea.getBottom() + 4, 120, 14, juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (12, 8);
            power.setBounds (b.removeFromLeft (64).withSizeKeepingCentre (60, 30));
            b.removeFromLeft (14);
            level.setBounds (b.removeFromLeft (72));
            b.removeFromLeft (16);
            tap.setBounds (b.removeFromLeft (160).withSizeKeepingCentre (160, 28).translated (0, 8));
            b.removeFromLeft (20);
            meterArea = b.removeFromLeft (150).withSizeKeepingCentre (150, 12).translated (0, 2);
            preset.setBounds (b.removeFromRight (290).withSizeKeepingCentre (290, 28).translated (0, 8));
        }

        ModuleLNF lnf;
        PresetBar preset;
        OnButton power;
        RangeKnob level;
        SegParam tap;

    private:
        void timerCallback() override
        {
            const float m = proc.returnPeak[(size_t) sendIndex].load();
            const float next = m > meter ? m : meter * 0.85f;
            if (std::abs (next - meter) > 1.0e-5f) { meter = next; repaint (meterArea.expanded (2, 20)); }
        }
        KaminariVocalProcessor& proc;
        int sendIndex;
        juce::String title;
        juce::Rectangle<int> meterArea;
        float meter = 0;
    };

    //==================================================================================================================
    // REVERB
    // One of the 20 mode buttons: name on the left, family tag on the right.
    class ModeTile : public juce::Button
    {
    public:
        explicit ModeTile (int m) : juce::Button (kv::reverbMode (m).name), mode (m)
        {
            setRadioGroupId (0x5250);
            setTooltip (juce::String (kv::reverbMode (m).name) + ": " + kv::reverbMode (m).description);
        }
        void paintButton (juce::Graphics& g, bool hover, bool) override
        {
            const bool on = getToggleState();
            auto r = getLocalBounds().toFloat().reduced (on ? 1.0f : 0.5f);
            g.setColour (on ? navy800 : (hover ? navy800.withAlpha (0.6f) : navy900));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (on ? accent : navy600);
            g.drawRoundedRectangle (r, 4.0f, on ? 1.6f : 1.0f);
            auto b = getLocalBounds().reduced (7, 0);
            const juce::String fam = juce::String (kv::reverbMode (mode).family).toUpperCase();
            g.setColour (on ? accent : mist);
            g.setFont (font (9.5f, 1, 0.08f));
            const int fw = (int) juce::GlyphArrangement::getStringWidth (font (9.5f, 1, 0.08f), fam) + 2;
            g.drawText (fam, b.removeFromRight (fw), juce::Justification::centredRight);
            g.setColour (on ? white : white.withAlpha (0.9f));
            g.setFont (font (13.0f, on ? 2 : 0));
            g.drawFittedText (getButtonText(), b, juce::Justification::centredLeft, 1, 0.85f);
        }
        int mode;
    };

    // Shape of the reverb return over time: pre-delay, build-up and decay (or the Nonlin envelope).
    class ReverbEnvelope : public juce::Component, private juce::Timer
    {
    public:
        explicit ReverbEnvelope (APVTS& s) : state (s) { setTitle ("Reverb envelope"); startTimerHz (15); }
        ~ReverbEnvelope() override { stopTimer(); }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (navy950);
            g.fillRoundedRectangle (b, 5.0f);
            g.setColour (navy600);
            g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, 1.0f);

            const int m = juce::roundToInt (plainValue (state, kvid::rvMode));
            const auto& spec = kv::reverbMode (m);
            const float pre = plainValue (state, kvid::rvPreDelay) * 0.001f;
            const float decay = plainValue (state, kvid::rvDecay);
            const float size = plainValue (state, kvid::rvSize) * 0.01f;
            const float attack = plainValue (state, kvid::rvAttack) * 0.01f;
            const float density = plainValue (state, kvid::rvDensity) * 0.01f;
            const bool nonlin = spec.engine == kv::ReverbEngine::Nonlin;
            const bool ambience = spec.engine == kv::ReverbEngine::Ambience;
            const float nlLen = (60.0f + 640.0f * size) * 0.001f;
            const float span = juce::jmax (0.4f, pre + (nonlin ? nlLen * 1.25f : decay * 0.9f));

            auto plot = b.reduced (10.0f, 8.0f).withTrimmedTop (14.0f).withTrimmedBottom (14.0f);
            auto xOf = [&] (float t) { return plot.getX() + plot.getWidth() * t / span; };
            // grid: one line per second (or per 100 ms on short spans)
            float step = 10.0f;
            for (float st : { 0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f })
                if (span / st <= 4.0f) { step = st; break; }
            g.setFont (font (10.0f, 0));
            for (int k = 1; (float) k * step < span; ++k)
            {
                const float t = (float) k * step;
                g.setColour (navy800);
                g.drawVerticalLine (juce::roundToInt (xOf (t)), plot.getY(), plot.getBottom());
                g.setColour (steel);
                g.drawText (t < 0.999f ? juce::String (juce::roundToInt (t * 1000.0f)) + " ms" : juce::String (t, std::abs (t - std::round (t)) < 1.0e-3f ? 0 : 1) + " s",
                            juce::Rectangle<float> (xOf (t) - 30.0f, plot.getBottom() + 1.0f, 60.0f, 12.0f), juce::Justification::centred);
            }

            auto ampAt = [&] (float t) -> float
            {
                const float u = t - pre;
                if (u < 0.0f) return 0.0f;
                if (nonlin)
                {
                    if (u > nlLen) return 0.0f;
                    const float x = u / nlLen;
                    float e;
                    if (attack <= 0.5f) e = 1.0f - attack / 0.5f * 0.85f * x;
                    else { const float k = (attack - 0.5f) / 0.5f; e = (1.0f - k) * (1.0f - 0.85f * x) + k * (0.08f + 0.92f * std::pow (x, 1.5f)); }
                    return e * juce::jmin (1.0f, u / 0.004f);
                }
                const float build = 0.006f + 0.05f * spec.diffScale * (0.4f + 0.6f * density);
                const float tail = std::pow (10.0f, -3.0f * u / juce::jmax (0.05f, decay)) * (1.0f - std::exp (-u / build));
                if (! ambience) return tail;
                return tail * std::sin (attack * 1.5707963f);
            };

            juce::Path curve, fill;
            const int n = juce::jmax (2, (int) plot.getWidth());
            for (int i = 0; i <= n; ++i)
            {
                const float t = span * (float) i / (float) n;
                const float y = plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, ampAt (t));
                if (i == 0) { curve.startNewSubPath (plot.getX(), y); fill.startNewSubPath (plot.getX(), plot.getBottom()); }
                else curve.lineTo (xOf (t), y);
                fill.lineTo (xOf (t), y);
            }
            fill.lineTo (plot.getRight(), plot.getBottom());
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.35f), 0, plot.getY(), accent.withAlpha (0.04f), 0, plot.getBottom(), false));
            g.fillPath (fill);
            g.setColour (accent);
            g.strokePath (curve, juce::PathStrokeType (1.6f));

            if (ambience)
            {
                // early reflections, louder as Attack moves towards 0
                const float er = std::cos (attack * 1.5707963f);
                static const float taps[] = { 0.007f, 0.013f, 0.019f, 0.026f, 0.034f, 0.043f, 0.055f, 0.068f };
                g.setColour (white.withAlpha (0.85f));
                for (int k = 0; k < 8; ++k)
                {
                    const float x = xOf (pre + taps[k] * (0.6f + size));
                    const float h = plot.getHeight() * er * (1.0f - 0.08f * (float) k);
                    g.fillRect (juce::Rectangle<float> (x - 1.0f, plot.getBottom() - h, 2.0f, h));
                }
            }

            // pre-delay marker
            const float px = xOf (pre);
            g.setColour (amber);
            for (float y = plot.getY(); y < plot.getBottom(); y += 5.0f)
                g.drawLine (px, y, px, juce::jmin (y + 2.5f, plot.getBottom()), 1.0f);
            g.setFont (font (11.0f, 0));
            g.drawText ("pre-delay", juce::Rectangle<float> (px + 4.0f, b.getY() + 4.0f, 70.0f, 14.0f), juce::Justification::centredLeft);
            g.setColour (mist);
            const juce::String info = nonlin ? juce::String (juce::roundToInt (nlLen * 1000.0f)) + " ms envelope"
                                             : "RT60 " + juce::String (decay, decay < 10.0f ? 2 : 1) + " s";
            g.drawText (info, b.reduced (10.0f, 4.0f).removeFromTop (14.0f), juce::Justification::centredRight);
        }

    private:
        void timerCallback() override
        {
            float sum = 0;
            for (auto* id : { kvid::rvMode, kvid::rvPreDelay, kvid::rvDecay, kvid::rvSize, kvid::rvAttack, kvid::rvDensity })
                sum = sum * 1.37f + state.getParameter (id)->getValue();
            if (std::abs (sum - last) > 1.0e-7f) { last = sum; repaint(); }
        }
        APVTS& state;
        float last = -1;
    };

    class ReverbPanel : public juce::Component
    {
    public:
        explicit ReverbPanel (KaminariVocalProcessor& p)
            : header (p, KaminariVocalProcessor::Reverb, kvid::rvOn, kvid::rvSend, kvid::rvTap, "Reverb", "reverb"),
              envelope (p.apvts),
              decay (p.apvts, kvid::rvDecay, "Decay", "0.2 s", "20 s", "Time for the tail to fall by 60 dB."),
              size (p.apvts, kvid::rvSize, "Size", "SMALL", "LARGE", "Scale of the space. In Nonlin it sets the envelope length."),
              preDelay (p.apvts, kvid::rvPreDelay, "Pre-delay", "0", "250", "Gap between the vocal and the start of the reverb."),
              hiCut (p.apvts, kvid::rvHiCut, "High Cut", "DARK", "OPEN", "Darkens the reverb: input filter and in-tail damping."),
              loCut (p.apvts, kvid::rvLoCut, "Low Cut", "FULL", "THIN", "Removes low end from the reverb input."),
              modRate (p.apvts, kvid::rvModRate, "Rate", "SLOW", "FAST", "Speed of the delay modulation in the tail."),
              modDepth (p.apvts, kvid::rvModDepth, "Depth", "MIN", "MAX", "Amount of delay modulation (chorus, random, detune, ensemble or wow/flutter, by mode)."),
              density (p.apvts, kvid::rvDensity, "Density", "SPARSE", "DENSE", "Echo density: strength of the input diffusion."),
              attack (p.apvts, kvid::rvAttack, "Attack", "EARLY", "LATE", "Ambience: balance of early reflections (0) and tail (100). Nonlin: gated (0), truncated (50), reverse (100)."),
              modeParam (*p.apvts.getParameter (kvid::rvMode)),
              modeAttachment (modeParam, [this] (float v) { modeChanged (juce::roundToInt (v)); }, &p.undoManager)
        {
            addAndMakeVisible (header);
            addAndMakeVisible (envelope);
            for (int m = 0; m < kv::numReverbModes; ++m)
            {
                auto* t = tiles.add (new ModeTile (m));
                t->setClickingTogglesState (true);
                t->onClick = [this, m] { if (tiles[m]->getToggleState()) modeAttachment.setValueAsCompleteGesture ((float) m); };
                addAndMakeVisible (t);
            }
            for (auto* k : knobs())
            {
                addAndMakeVisible (k);
                k->setLNF (&lnf);
                k->setLabelOverhang (2);
            }
            modeAttachment.sendInitialUpdate();
        }

        ~ReverbPanel() override
        {
            for (auto* k : knobs())
                k->setLNF (nullptr);
        }

        std::vector<RangeKnob*> knobs() { return { &decay, &size, &preDelay, &hiCut, &loCut, &modRate, &modDepth, &density, &attack }; }

        void paint (juce::Graphics& g) override
        {
            const auto& spec = kv::reverbMode (current);
            g.setColour (mist);
            g.setFont (font (11.0f, 1, 0.1f));
            g.drawText ("MODE" + dot() + "20 ALGORITHMS", gridArea.getX(), gridArea.getY() - 18, 200, 14, juce::Justification::centredLeft);
            juce::AttributedString desc;
            desc.append (spec.name, font (13.0f, 2), white);
            desc.append (dot() + spec.description, font (12.5f, 0), mist);
            desc.setJustification (juce::Justification::centredRight);
            desc.setWordWrap (juce::AttributedString::none);
            desc.draw (g, juce::Rectangle<int> (gridArea.getX() + 160, gridArea.getY() - 20, gridArea.getWidth() - 160, 18).toFloat());

            // family guide
            juce::AttributedString guide;
            const char* fam[][2] = { { "Dirty", "vintage grit and character" }, { "Smooth", "polished vocals, natural spaces" },
                                     { "Chaotic", "long, animated reverbs that sit in a mix" }, { "Palace", "small rooms to large halls" },
                                     { "Ambience", "felt more than heard" } };
            for (auto& f : fam)
            {
                guide.append (juce::String (f[0]) + "  ", font (12.0f, 2), white);
                guide.append (juce::String (f[1]) + "      ", font (12.0f, 0), mist);
            }
            guide.setWordWrap (juce::AttributedString::byWord);
            guide.draw (g, guideArea.toFloat());

            for (auto* grp : { &space, &tone, &mod, &diff })
                if (! grp->area.isEmpty())
                    drawTitledGroup (g, grp->area, grp->title);

            g.setColour (steel);
            g.setFont (font (11.5f, 0));
            g.drawText ("Only controls that change the selected mode are shown. Attack appears for Ambience and Nonlin; "
                        "Decay and modulation are hidden for Nonlin.", footer, juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto b = getLocalBounds();
            header.setBounds (b.removeFromTop (80));
            b.removeFromTop (30);
            auto top = b.removeFromTop (4 * 30 + 3 * 5);
            envelope.setBounds (top.removeFromRight (196));
            top.removeFromRight (12);
            gridArea = top;
            const int cols = 5, gap = 5;
            const int cw = (top.getWidth() - (cols - 1) * gap) / cols;
            for (int i = 0; i < tiles.size(); ++i)
                tiles[i]->setBounds (top.getX() + (i % cols) * (cw + gap), top.getY() + (i / cols) * (30 + gap), cw, 30);
            b.removeFromTop (8);
            guideArea = b.removeFromTop (34);
            b.removeFromTop (8);

            // groups: SPACE (large Decay + Size + Pre-delay), TONE, MODULATION, DIFFUSION
            layoutGroups (b.removeFromTop (juce::jmin (b.getHeight() - 26, 180)), { &space, &tone, &mod, &diff }, 104, 22);
            b.removeFromTop (8);
            footer = b.removeFromTop (18);
        }

        void modeChanged (int m)
        {
            current = juce::jlimit (0, kv::numReverbModes - 1, m);
            if (current >= tiles.size()) return;
            tiles[current]->setToggleState (true, juce::dontSendNotification);
            const auto use = kv::reverbControlUse (current);
            decay.setVisible (use.decay);
            modRate.setVisible (use.modulation);
            modDepth.setVisible (use.modulation);
            attack.setVisible (use.attack);
            resized();
            repaint();
        }

        ModuleLNF lnf;
        SendHeader header;
        ReverbEnvelope envelope;
        juce::OwnedArray<ModeTile> tiles;
        RangeKnob decay, size, preDelay, hiCut, loCut, modRate, modDepth, density, attack;

    private:
        SendGroup space { "SPACE", { &decay, &size, &preDelay }, &decay, 34 }, tone { "TONE", { &hiCut, &loCut } },
                  mod { "MODULATION", { &modRate, &modDepth } }, diff { "DIFFUSION", { &density, &attack } };
        juce::Rectangle<int> gridArea, guideArea, footer;
        int current = 0;
        juce::RangedAudioParameter& modeParam;
        juce::ParameterAttachment modeAttachment;
    };

    //==================================================================================================================
    // DELAY
    // Echo pattern: the dry hit and the following repeats with their levels (feedback, accent) and timing (groove, feel).
    class EchoGraph : public juce::Component, private juce::Timer
    {
    public:
        explicit EchoGraph (KaminariVocalProcessor& p) : proc (p), state (p.apvts) { setTitle ("Echo pattern"); startTimerHz (15); }
        ~EchoGraph() override { stopTimer(); }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (navy950);
            g.fillRoundedRectangle (b, 5.0f);
            g.setColour (navy600);
            g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, 1.0f);

            const auto t = proc.hostTempo.read();
            const double bpm = t.bpm > 0.0 ? t.bpm : 120.0;
            const int mode = juce::roundToInt (plainValue (state, kvid::dlMode));
            const bool dual = mode == kv::DelaySettings::Dual, ping = mode == kv::DelaySettings::PingPong;
            const float t1 = echoSeconds (1, bpm), t2 = echoSeconds (2, bpm);
            const float fb = plainValue (state, kvid::dlFeedback) * 0.01f;
            const float acc = plainValue (state, kvid::dlAccent) * 0.01f;
            const float groove = plainValue (state, kvid::dlGroove) * 0.01f;
            const float feel = plainValue (state, kvid::dlFeel) * 0.001f;

            struct Hit { float time, level; int side; };   // side: 0 both, 1 left, 2 right
            std::vector<Hit> hits;
            const float span = juce::jmax (0.25f, juce::jmax (t1, (dual || ping) ? t2 : 0.0f) * 6.5f);
            if (dual)
            {
                for (int s = 1; s <= 2; ++s)
                {
                    const float dt = s == 1 ? t1 : t2;
                    float lv = 1.0f;
                    for (int k = 1; dt * (float) k + feel < span && k < 40; ++k, lv *= fb)
                        hits.push_back ({ dt * (float) k + feel, lv, s });
                }
            }
            else
            {
                float time = 0.0f, lv = 1.0f;
                for (int k = 1; k < 40; ++k)
                {
                    const float dt = ping ? (k % 2 == 1 ? t1 : t2) : t1;
                    time += dt * (1.0f + ((k % 2 == 0) ? 1.0f : -1.0f) * groove * 0.33f * (ping ? 0.0f : 1.0f));
                    if (time + feel >= span) break;
                    const float a = ping ? 1.0f : (k % 2 == 1 ? 1.0f + juce::jmax (0.0f, acc) * 0.5f : 1.0f - juce::jmax (0.0f, acc) * 0.5f)
                                                  * (k % 2 == 0 ? 1.0f + juce::jmax (0.0f, -acc) * 0.5f : 1.0f - juce::jmax (0.0f, -acc) * 0.5f);
                    hits.push_back ({ time + feel, lv * a, ping ? (k % 2 == 1 ? 1 : 2) : 0 });
                    lv *= fb;
                }
            }

            auto plot = b.reduced (12.0f, 10.0f).withTrimmedBottom (14.0f);
            auto xOf = [&] (float s) { return plot.getX() + 4.0f + (plot.getWidth() - 8.0f) * s / span; };
            const float w = juce::jlimit (3.0f, 7.0f, plot.getWidth() / 60.0f);
            g.setColour (white);
            g.fillRoundedRectangle (juce::Rectangle<float> (xOf (0.0f) - w * 0.5f, plot.getY(), w, plot.getHeight()), 1.5f);
            for (auto& h : hits)
            {
                const float hh = plot.getHeight() * juce::jlimit (0.0f, 1.0f, h.level) * 0.92f;
                if (hh < 1.0f) continue;
                g.setColour (h.side == 2 ? amber.withAlpha (0.9f) : accent);
                g.fillRoundedRectangle (juce::Rectangle<float> (xOf (h.time) - w * 0.5f, plot.getBottom() - hh, w, hh), 1.5f);
            }
            g.setColour (mist);
            g.setFont (font (11.0f, 0));
            auto lab = b.reduced (12.0f, 4.0f).removeFromBottom (14.0f);
            g.drawText ("dry", lab, juce::Justification::centredLeft);
            const bool two = dual || ping;
            const bool synced = unitOf (1) != 0 || (two && unitOf (2) != 0);
            juce::String info = timeText (1, t1);
            if (dual) info = "L " + info + dot() + "R " + timeText (2, t2);
            if (ping) info = info + " / " + timeText (2, t2);
            if (synced) info += dot() + juce::String (juce::roundToInt (bpm)) + " BPM" + (t.bpm <= 0.0 ? " (default)" : "");
            g.drawText (info, lab.withTrimmedLeft (30.0f), juce::Justification::centredRight);
            if (two)
            {
                auto legend = b.reduced (12.0f, 6.0f).removeFromTop (12.0f);
                g.setFont (font (10.0f, 1, 0.08f));
                g.setColour (amber);
                g.drawText (ping ? "PONG" : "RIGHT", legend.removeFromRight (40.0f), juce::Justification::centredRight);
                g.setColour (accent);
                g.drawText (ping ? "PING" : "LEFT", legend.removeFromRight (40.0f), juce::Justification::centredRight);
            }
        }

    private:
        int unitOf (int echo) const { return juce::roundToInt (plainValue (state, echo == 1 ? kvid::dlT1Unit : kvid::dlT2Unit)); }
        float echoSeconds (int echo, double bpm) const
        {
            const int unit = unitOf (echo);
            if (unit == 0) return plainValue (state, echo == 1 ? kvid::dlT1Ms : kvid::dlT2Ms) * 0.001f;
            const int note = juce::roundToInt (plainValue (state, echo == 1 ? kvid::dlT1Note : kvid::dlT2Note));
            return (float) (kvp::noteBeats (note, unit) * 60.0 / juce::jlimit (20.0, 400.0, bpm));
        }
        juce::String timeText (int echo, float seconds) const
        {
            const int unit = unitOf (echo);
            if (unit == 0) return juce::String (juce::roundToInt (seconds * 1000.0f)) + " ms";
            const int note = juce::roundToInt (plainValue (state, echo == 1 ? kvid::dlT1Note : kvid::dlT2Note));
            static const char* suffix[] = { "", "", " dot", " trip" };
            return kvp::noteNames()[note] + suffix[unit];
        }
        void timerCallback() override { repaint(); }
        KaminariVocalProcessor& proc;
        APVTS& state;
    };

    class DelayPanel : public juce::Component
    {
    public:
        explicit DelayPanel (KaminariVocalProcessor& p)
            : header (p, KaminariVocalProcessor::Delay, kvid::dlOn, kvid::dlSend, kvid::dlTap, "Delay", "delay"),
              graph (p),
              mode (p.apvts, kvid::dlMode, { "Single", "Dual", "Ping-Pong" }, "Single: one echo time. Dual: independent left and right echoes. Ping-Pong: echoes alternate left and right."),
              style (p.apvts, kvid::dlStyle, "", "Tone and saturation character of the repeats."),
              unit1 (p.apvts, kvid::dlT1Unit, { "Time", "Note", "Dot", "Trip" }, "Time in milliseconds, or a note value (straight, dotted or triplet) synced to the host tempo."),
              note1 (p.apvts, kvid::dlT1Note, "", "Note value for Echo 1."),
              unit2 (p.apvts, kvid::dlT2Unit, { "Time", "Note", "Dot", "Trip" }, "Time in milliseconds, or a note value synced to the host tempo."),
              note2 (p.apvts, kvid::dlT2Note, "", "Note value for Echo 2."),
              time1 (p.apvts, kvid::dlT1Ms, ""), time2 (p.apvts, kvid::dlT2Ms, ""),
              feedback (p.apvts, kvid::dlFeedback, "Feedback", "MIN", "MAX", "Number of repeats. Limited below runaway; the style's saturation also bounds the level."),
              loCut (p.apvts, kvid::dlLoCut, "Low Cut", "FULL", "THIN", "Removes low end from every repeat."),
              hiCut (p.apvts, kvid::dlHiCut, "High Cut", "DARK", "OPEN", "Darkens every repeat."),
              saturation (p.apvts, kvid::dlSaturation, "Saturation", "CLEAN", "DRIVE", "Style-dependent drive inside the echo loop."),
              width (p.apvts, kvid::dlWidth, "Width", "MONO", "WIDE", "Stereo spread of the echoes. Above 75 % out-of-phase content pushes them past the speakers."),
              offset (p.apvts, kvid::dlOffset, "L/R Offset", "0", "25 ms", "Small time difference between left and right. Needs Width above 0."),
              accent (p.apvts, kvid::dlAccent, "Accent", "EVEN", "ODD", "Right: odd repeats louder. Left: even (off-beat) repeats louder."),
              accent2 (p.apvts, kvid::dlAccent2, "Accent 2", "EVEN", "ODD", "Accent for Echo 2 (right channel)."),
              balance (p.apvts, kvid::dlBalance, "Balance", "L", "R", "Relative level of the left and right echoes."),
              fbMix (p.apvts, kvid::dlFbMix, "FB Mix", "SEP", "CROSS", "0: independent echoes. 50: equal feedback into both. 100: each echo feeds the other."),
              fbBal (p.apvts, kvid::dlFbBal, "FB Bal", "L", "R", "Right: more feedback on the right echo. Left: more on the left."),
              groove (p.apvts, kvid::dlGroove, "Groove", "SHUF", "SWING", "Left: shuffle. Right: swing. Shifts alternate repeats towards a triplet feel."),
              feel (p.apvts, kvid::dlFeel, "Feel", "RUSH", "DRAG", "Moves every echo behind (drag, +) or ahead of (rush, -) the beat."),
              wobble (p.apvts, kvid::dlWobble, "Wobble", "OFF", "MAX", "Tape-like pitch wobble on the repeats."),
              wobbleRate (p.apvts, kvid::dlWobbleRate, "Rate", "SLOW", "FAST", "Wobble speed."),
              wobbleSync (p.apvts, kvid::dlWobbleSync, "Sync", "DRIFT", "OPP", "Left: paths drift to different rates. Centre: locked. Right: left and right wobble in opposition."),
              shape (p.apvts, kvid::dlWobbleShape, "SHAPE", "Waveform of the wobble pitch variation."),
              diffusion (p.apvts, kvid::dlDiffusion, "Amount", "OFF", "MAX", "Smears the repeats; high settings become reverb-like."),
              diffSize (p.apvts, kvid::dlDiffSize, "Size", "SMALL", "LARGE", "Character of the diffusion, from phasing (small) to reverb-like (large)."),
              diffPos (p.apvts, kvid::dlDiffLoop, { "Post", "Loop" }, "Post: every repeat equally diffused. Loop: each repeat more diffused than the last."),
              prime (p.apvts, kvid::dlPrime, "PRIME", "PRIME", "Rounds echo times to prime sample counts to avoid resonant build-up with short times and feedback."),
              modeAttachment (*p.apvts.getParameter (kvid::dlMode), [this] (float) { updateVisibility(); }),
              unit1Attachment (*p.apvts.getParameter (kvid::dlT1Unit), [this] (float) { updateVisibility(); }),
              unit2Attachment (*p.apvts.getParameter (kvid::dlT2Unit), [this] (float) { updateVisibility(); }),
              styleAttachment (*p.apvts.getParameter (kvid::dlStyle), [this] (float v) { styleText = kv::delayStyleDescription (juce::roundToInt (v)); repaint(); }),
              apvts (p.apvts)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &header, &graph, &mode, &style, &unit1, &note1, &unit2, &note2, &time1, &time2,
                                                                     &shape, &diffPos, &prime })
                addAndMakeVisible (c);
            for (auto* k : knobs())
            {
                addAndMakeVisible (k);
                k->setLNF (&lnf);
                k->setLabelOverhang (2);
            }
            style.caption.setVisible (false);
            note1.caption.setVisible (false);
            note2.caption.setVisible (false);
            shape.caption.setText ("SHAPE", juce::dontSendNotification);
            modeAttachment.sendInitialUpdate();
            styleAttachment.sendInitialUpdate();
        }

        ~DelayPanel() override
        {
            for (auto* k : knobs())
                k->setLNF (nullptr);
        }

        std::vector<RangeKnob*> knobs()
        {
            return { &feedback, &loCut, &hiCut, &saturation, &width, &offset, &accent, &accent2, &balance,
                     &fbMix, &fbBal, &groove, &feel, &wobble, &wobbleRate, &wobbleSync, &diffusion, &diffSize };
        }

        void updateVisibility()
        {
            const int m = choiceIndex (apvts, kvid::dlMode);
            dual = m == kv::DelaySettings::Dual;
            ping = m == kv::DelaySettings::PingPong;
            const bool ms1 = choiceIndex (apvts, kvid::dlT1Unit) == 0;
            const bool ms2 = choiceIndex (apvts, kvid::dlT2Unit) == 0;
            time1.setVisible (ms1);
            note1.setVisible (! ms1);
            unit2.setVisible (dual || ping);
            time2.setVisible ((dual || ping) && ms2);
            note2.setVisible ((dual || ping) && ! ms2);
            offset.setVisible (! ping);
            accent.setVisible (! ping);
            accent2.setVisible (dual);
            balance.setVisible (dual || ping);
            fbMix.setVisible (dual);
            fbBal.setVisible (dual);
            resized();
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            drawCaption (g, mode, "MODE");
            drawCaption (g, unit1, ping ? "PING" : (dual ? "ECHO 1 (LEFT)" : "ECHO"));
            drawCaption (g, unit2, ping ? "PONG" : "ECHO 2 (RIGHT)");
            drawCaption (g, diffPos, "POSITION");
            g.setColour (mist);
            g.setFont (font (11.0f, 1, 0.1f));
            for (auto& c : captions)
                g.drawText (c.second, c.first.getX(), c.first.getY() - 17, c.first.getWidth() + 40, 14, juce::Justification::centredLeft);
            g.setColour (mist);
            g.setFont (font (12.0f, 0));
            g.drawText (styleText, styleArea, juce::Justification::centredLeft, true);
            for (auto* grp : { &echo, &stereo, &dualGrp, &timing, &wob, &diff })
                if (! grp->area.isEmpty())
                    drawTitledGroup (g, grp->area, grp->title);
        }

        void resized() override
        {
            auto b = getLocalBounds();
            header.setBounds (b.removeFromTop (80));
            b.removeFromTop (28);
            auto top = b.removeFromTop (110);
            graph.setBounds (top.removeFromRight (300).withTrimmedTop (-10));
            top.removeFromRight (14);
            captions.clear();
            // a combo or value box sits in a 28 px slot; its own caption area (16 or 17 px) is placed above the slot
            auto slot = [&] (juce::Component& c, juce::Rectangle<int> r, int own, const juce::String& caption)
            {
                c.setBounds (r.withY (r.getY() - own).withHeight (r.getHeight() + own));
                captions.push_back ({ r, caption });
            };
            auto r1 = top.removeFromTop (28);
            mode.setBounds (r1.removeFromLeft (230));
            r1.removeFromLeft (16);
            slot (style, r1.removeFromLeft (180).withHeight (26), 16, "STYLE");
            top.removeFromTop (26);
            auto r2 = top.removeFromTop (28);
            unit1.setBounds (r2.removeFromLeft (180));
            r2.removeFromLeft (8);
            const auto s1 = r2.removeFromLeft (76);
            if (time1.isVisible()) slot (time1, s1, 17, "TIME"); else slot (note1, s1.withHeight (26), 16, "NOTE");
            r2.removeFromLeft (18);
            if (unit2.isVisible())
            {
                unit2.setBounds (r2.removeFromLeft (180));
                r2.removeFromLeft (8);
                const auto s2 = r2.removeFromLeft (76);
                if (time2.isVisible()) slot (time2, s2, 17, "TIME"); else slot (note2, s2.withHeight (26), 16, "NOTE");
            }
            styleArea = top.withTrimmedTop (6).withHeight (18);

            b.removeFromTop (14);
            dualGrp.title = ping ? "PING-PONG" : "DUAL";
            layoutGroups (b.removeFromTop (126), { &echo, &stereo, &dualGrp }, 100);
            b.removeFromTop (10);
            layoutGroups (b.removeFromTop (126), { &timing, &wob, &diff }, 100);
        }

        ModuleLNF lnf;
        SendHeader header;
        EchoGraph graph;
        SegParam mode;
        ChoiceBox style;
        SegParam unit1;
        ChoiceBox note1;
        SegParam unit2;
        ChoiceBox note2;
        Field time1, time2;
        RangeKnob feedback, loCut, hiCut, saturation, width, offset, accent, accent2, balance, fbMix, fbBal,
                  groove, feel, wobble, wobbleRate, wobbleSync;
        ChoiceBox shape;
        RangeKnob diffusion, diffSize;
        SegParam diffPos;
        ToggleBox prime;

    private:
        SendGroup echo { "ECHO", { &feedback, &loCut, &hiCut, &saturation } }, stereo { "STEREO", { &width, &offset, &accent } },
                  dualGrp { "DUAL", { &accent2, &balance, &fbMix, &fbBal } },
                  timing { "TIMING", { &groove, &feel }, nullptr, 0, &prime, 90 },
                  wob { "WOBBLE", { &wobble, &wobbleRate, &wobbleSync }, nullptr, 0, &shape, 120, 44, -6 },
                  diff { "DIFFUSION", { &diffusion, &diffSize }, nullptr, 0, &diffPos, 110, 28, 8 };
        juce::ParameterAttachment modeAttachment, unit1Attachment, unit2Attachment, styleAttachment;
        APVTS& apvts;
        juce::String styleText;
        juce::Rectangle<int> styleArea;
        std::vector<std::pair<juce::Rectangle<int>, juce::String>> captions;
        bool dual = false, ping = false;
    };

    //==================================================================================================================
    // WIDENER
    // Square style buttons with a status light (I, II, III or 1, 2, 3), bound to a choice parameter.
    class LedButtons : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        LedButtons (APVTS& s, const char* id, const juce::StringArray& labels, const juce::String& tip)
            : param (*s.getParameter (id)), names (labels),
              att (param, [this] (float v) { selected = juce::roundToInt (v); repaint(); }, nullptr)
        {
            setTitle (param.getName (64));
            setTooltip (param.getName (64) + "\n" + tip);
            att.sendInitialUpdate();
        }
        void paint (juce::Graphics& g) override
        {
            for (int i = 0; i < names.size(); ++i)
            {
                auto r = cellBounds (i).toFloat();
                auto key = r.withTrimmedTop (18.0f).reduced (2.0f);
                const bool on = i == selected;
                g.setColour (on ? white : mist);
                g.setFont (font (12.0f, 2, 0.08f));
                g.drawText (names[i], r.removeFromTop (16.0f), juce::Justification::centred);
                g.setColour (on ? navy600 : navy800);
                g.fillRoundedRectangle (key, 5.0f);
                g.setColour (on ? accent : navy600);
                g.drawRoundedRectangle (key.reduced (0.5f), 5.0f, on ? 1.5f : 1.0f);
                const auto led = juce::Rectangle<float> (key.getRight() - 12.0f, key.getY() + 6.0f, 6.0f, 6.0f);
                if (on)
                {
                    g.setColour (accent.withAlpha (0.35f));
                    g.fillEllipse (led.expanded (3.0f));
                }
                g.setColour (on ? accent : navy950);
                g.fillEllipse (led);
                g.setColour (on ? white.withAlpha (0.12f) : white.withAlpha (0.04f));
                g.drawRoundedRectangle (key.reduced (6.0f), 3.0f, 1.0f);
            }
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            for (int i = 0; i < names.size(); ++i)
                if (cellBounds (i).contains (e.getPosition()))
                    att.setValueAsCompleteGesture ((float) i);
        }
    private:
        juce::Rectangle<int> cellBounds (int i) const
        {
            const int w = getWidth() / juce::jmax (1, names.size());
            return { i * w, 0, w, getHeight() };
        }
        juce::RangedAudioParameter& param;
        juce::StringArray names;
        juce::ParameterAttachment att;
        int selected = 0;
    };

    // MicroShift: the widened band above Focus. SideWidener: the side image around the unchanged mid.
    class WidenerGraph : public juce::Component, private juce::Timer
    {
    public:
        explicit WidenerGraph (APVTS& s) : state (s) { setTitle ("Widener display"); startTimerHz (15); }
        ~WidenerGraph() override { stopTimer(); }
        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (navy950);
            g.fillRoundedRectangle (b, 5.0f);
            g.setColour (navy600);
            g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, 1.0f);
            const bool micro = juce::roundToInt (plainValue (state, kvid::wdType)) == kv::WidenerSettings::MicroShift;
            auto plot = b.reduced (10.0f, 8.0f);
            g.setFont (font (11.0f, 0));
            if (micro)
            {
                plot.removeFromTop (16.0f);
                auto axis = plot.removeFromBottom (14.0f);
                const float focus = plainValue (state, kvid::msFocus);
                const float amount = juce::jlimit (0.15f, 1.0f, (plainValue (state, kvid::msDetune) + plainValue (state, kvid::msDelay)) / 300.0f);
                auto xOf = [&] (float hz) { return plot.getX() + plot.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); };
                for (float hz : { 100.0f, 1000.0f, 10000.0f })
                {
                    g.setColour (navy800);
                    g.drawVerticalLine (juce::roundToInt (xOf (hz)), plot.getY(), plot.getBottom());
                    g.setColour (steel);
                    g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz), juce::Rectangle<float> (xOf (hz) - 20.0f, axis.getY(), 40.0f, 14.0f), juce::Justification::centred);
                }
                juce::Path curve, fill;
                const float top = plot.getBottom() - plot.getHeight() * (0.35f + 0.6f * amount);
                for (int i = 0; i <= (int) plot.getWidth(); ++i)
                {
                    const float x = plot.getX() + (float) i;
                    const float hz = 20.0f * std::pow (1000.0f, (float) i / plot.getWidth());
                    const float r = hz / juce::jmax (20.0f, focus);
                    const float mag = focus <= 20.5f ? 1.0f : (r * r) / std::sqrt (1.0f + r * r * r * r);   // 12 dB/oct high-pass
                    const float y = plot.getBottom() - (plot.getBottom() - top) * mag;
                    if (i == 0) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, plot.getBottom()); }
                    else curve.lineTo (x, y);
                    fill.lineTo (x, y);
                }
                fill.lineTo (plot.getRight(), plot.getBottom());
                fill.closeSubPath();
                g.setColour (accent.withAlpha (0.16f));
                g.fillPath (fill);
                g.setColour (accent);
                g.strokePath (curve, juce::PathStrokeType (1.6f));
                g.setColour (mist);
                g.drawText ("widened band (returned)", b.reduced (10.0f, 6.0f).removeFromTop (14.0f), juce::Justification::centredLeft);
                g.drawText ("Focus " + kvp::freqText (focus), b.reduced (10.0f, 6.0f).removeFromTop (14.0f), juce::Justification::centredRight);
            }
            else
            {
                const float width = plainValue (state, kvid::swWidth) * 0.01f;
                const float tone = plainValue (state, kvid::swTone) * 0.01f;
                const auto base = juce::Point<float> (plot.getCentreX(), plot.getBottom() - 6.0f);
                const float len = plot.getHeight() - 22.0f;
                const int lines = 9;
                const float spread = 0.15f + 1.25f * width;
                for (int side = -1; side <= 1; side += 2)
                    for (int k = 1; k <= lines; ++k)
                    {
                        const float a = side * spread * (float) k / (float) lines;
                        const float l = len * (0.55f + 0.45f * (1.0f - (float) k / (float) lines * (1.0f - tone)));
                        g.setColour (accent.withAlpha (0.25f + 0.6f * (float) k / (float) lines));
                        g.drawLine (base.x, base.y, base.x + std::sin (a) * l, base.y - std::cos (a) * l, 1.0f);
                    }
                g.setColour (white);
                g.drawLine (base.x, base.y, base.x, base.y - len, 2.0f);
                g.setColour (mist);
                g.drawText ("mid (dry, unchanged)", juce::Rectangle<float> (base.x + 6.0f, plot.getY(), 160.0f, 14.0f), juce::Justification::centredLeft);
                g.drawText ("L +side", plot.removeFromBottom (14.0f), juce::Justification::centredLeft);
                g.drawText ("R " + juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + "side", b.reduced (10.0f, 8.0f).removeFromBottom (14.0f), juce::Justification::centredRight);
            }
        }
    private:
        void timerCallback() override
        {
            float sum = 0;
            for (auto* id : { kvid::wdType, kvid::msFocus, kvid::msDetune, kvid::msDelay, kvid::swWidth, kvid::swTone })
                sum = sum * 1.37f + state.getParameter (id)->getValue();
            if (std::abs (sum - last) > 1.0e-7f) { last = sum; repaint(); }
        }
        APVTS& state;
        float last = -1;
    };

    class WidenerPanel : public juce::Component
    {
    public:
        explicit WidenerPanel (KaminariVocalProcessor& p)
            : header (p, KaminariVocalProcessor::Widener, kvid::wdOn, kvid::wdSend, kvid::wdTap, "Widener", "widener"),
              graph (p.apvts),
              type (p.apvts, kvid::wdType, { "MicroShift", "SideWidener" }, "MicroShift and SideWidener are separate algorithms. Only the selected one runs."),
              msStyle (p.apvts, kvid::msStyle, { "I", "II", "III" }, "I, II and III differ in pitch and delay variation, tone, saturation and de-glitching."),
              msDetune (p.apvts, kvid::msDetune, "Detune", "MIN", "MAX", "Amount of continuously varying micro pitch shift. 100 % = the style's own amount."),
              msDelay (p.apvts, kvid::msDelay, "Delay", "TIGHT", "LOOSE", "Amount of continuously varying delay. 100 % = the style's own amount."),
              msFocus (p.apvts, kvid::msFocus, "Focus", "20 HZ", "10K", "Crossover: only content above this frequency is widened, keeping the low end stable."),
              swMode (p.apvts, kvid::swMode, { "1", "2", "3" }, "Mode 1: subtle, no smear. Mode 3: widest, room-like smear. Mode 2: between."),
              swWidth (p.apvts, kvid::swWidth, "Width", "MIN", "MAX", "Amount of side signal added. The mono sum is not affected."),
              swTone (p.apvts, kvid::swTone, "Tone", "MID", "FULL", "0: widen the midrange only. 100: widen all frequencies."),
              swOutput (p.apvts, kvid::swOutput, "Output", "-INF", "0 DB", "Level of the widened signal (-inf to 0 dB)."),
              typeAttachment (*p.apvts.getParameter (kvid::wdType), [this] (float) { update(); }),
              msStyleAttachment (*p.apvts.getParameter (kvid::msStyle), [this] (float) { update(); }),
              swModeAttachment (*p.apvts.getParameter (kvid::swMode), [this] (float) { update(); }),
              apvts (p.apvts)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &header, &graph, &type, &msStyle, &msDetune, &msDelay, &msFocus,
                                                                     &swMode, &swWidth, &swTone, &swOutput })
                addAndMakeVisible (c);
            for (auto* k : { &msDetune, &msDelay, &msFocus, &swWidth, &swTone, &swOutput })
            {
                k->setLNF (&lnf);
                k->setLabelOverhang (2);
            }
            typeAttachment.sendInitialUpdate();
        }

        ~WidenerPanel() override
        {
            for (auto* k : { &msDetune, &msDelay, &msFocus, &swWidth, &swTone, &swOutput })
                k->setLNF (nullptr);
        }

        void update()
        {
            micro = choiceIndex (apvts, kvid::wdType) == kv::WidenerSettings::MicroShift;
            for (auto* c : std::initializer_list<juce::Component*> { &msStyle, &msDetune, &msDelay, &msFocus })
                c->setVisible (micro);
            for (auto* c : std::initializer_list<juce::Component*> { &swMode, &swWidth, &swTone, &swOutput })
                c->setVisible (! micro);
            if (micro)
            {
                description = kv::microShiftStyleDescription (choiceIndex (apvts, kvid::msStyle));
                note = "Left is shifted up and right down by a few continuously varying cents, each with a varying short delay. "
                       "No Mix control: the return is 100 % wet, so the send level sets the blend. Content below Focus is not returned.";
            }
            else
            {
                description = kv::sideWidenerModeDescription (choiceIndex (apvts, kvid::swMode));
                note = "Returns only side signal (left +, right " + juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"))
                     + "), so the mono mix is unchanged. Bypass is the send's ON switch.";
            }
            resized();
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            drawCaption (g, type, "TYPE" + dot() + "ONE AT A TIME");
            g.setColour (white);
            g.setFont (font (13.5f, 2));
            g.drawText (description, descArea, juce::Justification::centredLeft, true);
            drawTitledGroup (g, groupArea, micro ? "MICROSHIFT" : "SIDEWIDENER");
            g.setColour (mist);
            g.setFont (font (11.0f, 2, 0.12f));
            const auto& sel = micro ? (juce::Component&) msStyle : (juce::Component&) swMode;
            g.drawText (micro ? "STYLE" : "MODE", sel.getX(), sel.getBottom() + 4, sel.getWidth(), 14, juce::Justification::centred);
            g.setColour (steel);
            g.setFont (font (12.0f, 0));
            g.drawFittedText (note, noteArea, juce::Justification::topLeft, 3, 1.0f);
        }

        void resized() override
        {
            auto b = getLocalBounds();
            header.setBounds (b.removeFromTop (80));
            b.removeFromTop (28);
            auto r = b.removeFromTop (28);
            type.setBounds (r.removeFromLeft (220));
            r.removeFromLeft (18);
            descArea = r;
            b.removeFromTop (16);
            auto body = b.removeFromTop (220);
            graph.setBounds (body.removeFromRight (340));
            body.removeFromRight (14);
            groupArea = body;
            auto inner = body.reduced (16, 0).withTrimmedTop (34).withTrimmedBottom (14);
            auto sel = inner.removeFromLeft (170);
            (micro ? (juce::Component&) msStyle : (juce::Component&) swMode).setBounds (sel.withSizeKeepingCentre (162, 66).translated (0, -10));
            inner.removeFromLeft (10);
            const int cell = inner.getWidth() / 3;
            layoutRow (inner, { &msDetune, &msDelay, &msFocus, &swWidth, &swTone, &swOutput }, cell);
            for (auto* k : { &msDetune, &msDelay, &msFocus, &swWidth, &swTone, &swOutput })
                k->setBounds (k->getBounds().withSizeKeepingCentre (juce::jmin (cell - 10, 92), juce::jmin (k->getHeight(), 140)));
            b.removeFromTop (12);
            noteArea = b.removeFromTop (54);
        }

        ModuleLNF lnf;
        SendHeader header;
        WidenerGraph graph;
        SegParam type;
        LedButtons msStyle;
        RangeKnob msDetune, msDelay, msFocus;
        LedButtons swMode;
        RangeKnob swWidth, swTone, swOutput;

    private:
        juce::ParameterAttachment typeAttachment, msStyleAttachment, swModeAttachment;
        APVTS& apvts;
        bool micro = true;
        juce::String description, note;
        juce::Rectangle<int> descArea, groupArea, noteArea;
    };

    //==================================================================================================================
    // FLANGER
    // Delay time of the left (accent) and right (amber) sweep over two LFO cycles, with the current position.
    class SweepDisplay : public juce::Component, private juce::Timer
    {
    public:
        explicit SweepDisplay (KaminariVocalProcessor& p) : proc (p), state (p.apvts) { setTitle ("Flanger sweep"); startTimerHz (30); }
        ~SweepDisplay() override { stopTimer(); }
        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (navy950);
            g.fillRoundedRectangle (b, 5.0f);
            g.setColour (navy600);
            g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, 1.0f);
            const float base = plainValue (state, kvid::flDelay), depth = plainValue (state, kvid::flDepth) * 0.01f;
            const float stereo = plainValue (state, kvid::flStereo) / 180.0f;
            const int shape = juce::roundToInt (plainValue (state, kvid::flShape));
            const float top = base + kv::Flanger::maxSweepMs * depth;
            const float yMax = juce::jmax (2.0f, top * 1.15f);
            auto plot = b.reduced (12.0f, 10.0f).withTrimmedTop (14.0f).withTrimmedBottom (12.0f);
            g.setColour (navy800);
            for (int k = 1; k < 4; ++k) g.drawVerticalLine (juce::roundToInt (plot.getX() + plot.getWidth() * (float) k / 4.0f), plot.getY(), plot.getBottom());
            for (int c = 1; c >= 0; --c)
            {
                juce::Path path;
                const int n = (int) plot.getWidth();
                for (int i = 0; i <= n; ++i)
                {
                    const float ph = 2.0f * (float) i / (float) n + (c == 1 ? 0.5f * stereo : 0.0f);
                    const float lfo = 0.5f + 0.5f * kv::lfoShape (shape, ph - 0.25f);
                    const float d = base + kv::Flanger::maxSweepMs * depth * lfo;
                    const float x = plot.getX() + (float) i, y = plot.getBottom() - plot.getHeight() * d / yMax;
                    if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
                }
                g.setColour (c == 0 ? accent : amber.withAlpha (0.85f));
                g.strokePath (path, juce::PathStrokeType (c == 0 ? 2.0f : 1.4f));
            }
            // current LFO position (left channel), on both drawn cycles
            const bool on = choiceIndex (state, kvid::flOn) != 0;
            if (on)
                for (int k = 0; k < 2; ++k)
                {
                    const float ph = pos + (float) k;
                    const float lfo = 0.5f + 0.5f * kv::lfoShape (shape, ph - 0.25f);
                    const float d = base + kv::Flanger::maxSweepMs * depth * lfo;
                    const float x = plot.getX() + plot.getWidth() * ph / 2.0f, y = plot.getBottom() - plot.getHeight() * d / yMax;
                    g.setColour (white);
                    g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
                }
            g.setFont (font (11.0f, 0));
            g.setColour (mist);
            const float notchLo = 500.0f / top, notchHi = 500.0f / base;   // first comb notch in Hz: 1 / (2 d), d in ms
            g.drawText ("delay " + juce::String (base, 2) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) + juce::String (top, 2) + " ms"
                        + dot() + "first notch " + kvp::freqText (notchLo) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) + kvp::freqText (notchHi),
                        b.reduced (12.0f, 4.0f).removeFromTop (14.0f), juce::Justification::centredLeft);
            g.setFont (font (10.0f, 1, 0.08f));
            g.setColour (accent);
            g.drawText ("LEFT", b.reduced (12.0f, 4.0f).removeFromBottom (12.0f), juce::Justification::centredLeft);
            g.setColour (amber);
            g.drawText ("RIGHT", b.reduced (12.0f, 4.0f).removeFromBottom (12.0f).withTrimmedLeft (34.0f), juce::Justification::centredLeft);
            g.setColour (mist);
            g.drawText ("2 cycles", b.reduced (12.0f, 4.0f).removeFromBottom (12.0f), juce::Justification::centredRight);
        }
    private:
        void timerCallback() override { pos = proc.flanger.lfoNow.load(); repaint(); }
        KaminariVocalProcessor& proc;
        APVTS& state;
        float pos = 0;
    };

    class FlangerOptions : public juce::Component
    {
    public:
        explicit FlangerOptions (APVTS& s)
            : sync (s, kvid::flSync, "SYNC", "Free rate, or one sweep per bar or note value, locked to the host tempo."),
              shape (s, kvid::flShape, { "SINE", "TRIANGLE" }, "Waveform of the sweep. Triangle sweeps evenly; sine lingers at the ends.")
        {
            addAndMakeVisible (sync);
            addAndMakeVisible (shape);
        }
        void resized() override
        {
            auto b = getLocalBounds();
            sync.setBounds (b.removeFromTop (42));
            b.removeFromTop (8);
            shape.setBounds (b.removeFromTop (26));
        }
        ChoiceBox sync;
        SegParam shape;
    };

    class FlangerPage : public AdvFrame
    {
    public:
        explicit FlangerPage (KaminariVocalProcessor& p)
            : AdvFrame (p, "Flanger", "Swept comb filter" + dot() + "after Compression, before Distortion", "fl_on", "flanger"),
              sweep (p),
              rate (p.apvts, kvid::flRate, "Rate", "SLOW", "FAST", "Sweep speed when Sync is Free."),
              depth (p.apvts, kvid::flDepth, "Depth", "MIN", "MAX", "How far the delay sweeps (up to 6 ms above Delay)."),
              delay (p.apvts, kvid::flDelay, "Delay", "0.1", "10 MS", "Shortest delay of the sweep. Short = high, metallic notches; long = chorus-like."),
              feedback (p.apvts, kvid::flFeedback, "Feedback", "-95", "+95", "Resonance of the sweep. Negative values give a hollow tone."),
              stereo (p.apvts, kvid::flStereo, "Stereo", "0", "180", "Phase between the left and right sweeps. 0 = mono sweep."),
              hiCut (p.apvts, kvid::flHiCut, "High Cut", "DARK", "OPEN", "Darkens the swept copy and its feedback."),
              mix (p.apvts, kvid::flMix, "Mix", "DRY", "WET", "Blend of the swept copy and the dry vocal. 50 % gives the deepest flanging."),
              options (p.apvts),
              state (p.apvts),
              syncAtt (*p.apvts.getParameter (kvid::flSync), [this] (float) { update(); })
        {
            for (auto* c : std::initializer_list<juce::Component*> { &sweep, &options })
                addAndMakeVisible (c);
            for (auto* k : knobs())
            {
                addAndMakeVisible (k);
                k->setLNF (&lnf);
                k->setLabelOverhang (2);
            }
            syncAtt.sendInitialUpdate();
        }
        ~FlangerPage() override { for (auto* k : knobs()) k->setLNF (nullptr); }
        std::vector<RangeKnob*> knobs() { return { &rate, &depth, &delay, &feedback, &stereo, &hiCut, &mix }; }

        void update()
        {
            rate.setVisible (choiceIndex (state, kvid::flSync) == 0);
            if (! getBounds().isEmpty()) resized();
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            AdvFrame::paint (g);
            for (auto* grp : { &sweepGrp, &delayGrp, &stereoGrp, &toneGrp, &outGrp })
                if (! grp->area.isEmpty())
                    drawTitledGroup (g, grp->area, grp->title);
            g.setColour (steel);
            g.setFont (font (12.0f, 0));
            g.drawFittedText ("Mix 50 % gives the deepest flanging; higher values move towards a pure pitch wobble. "
                              "Feedback above about 80 % gives the resonant jet sound. No latency.",
                              noteArea, juce::Justification::topLeft, 2, 1.0f);
        }

        void layoutContent (juce::Rectangle<int> b) override
        {
            sweep.setBounds (b.removeFromTop (200));
            b.removeFromTop (14);
            layoutGroups (b.removeFromTop (160), { &sweepGrp, &delayGrp, &stereoGrp, &toneGrp, &outGrp }, 104, 16);
            b.removeFromTop (10);
            noteArea = b.removeFromTop (40);
        }

        SweepDisplay sweep;
        RangeKnob rate, depth, delay, feedback, stereo, hiCut, mix;
        FlangerOptions options;

    private:
        ModuleLNF lnf;
        APVTS& state;
        SendGroup sweepGrp { "SWEEP", { &rate, &depth }, nullptr, 0, &options, 140, 76, 2 }, delayGrp { "DELAY", { &delay, &feedback } },
                  stereoGrp { "STEREO", { &stereo } }, toneGrp { "TONE", { &hiCut } }, outGrp { "OUTPUT", { &mix } };
        juce::Rectangle<int> noteArea;
        juce::ParameterAttachment syncAtt;
    };

    //==================================================================================================================
    // SENDS page: title, one tab per send (on light, name, send level) and the selected send's panel.
    //==================================================================================================================
    // RETURN EQ, DUCKING AND ROUTING (Reverb and Delay, DESIGN.md 2.9.6)
    // Spectrum of the return before and after its EQ, the EQ's response and its four band nodes
    // (1 high pass, 2 and 3 bells, 4 low pass).
    //   - drag a node: frequency (and gain for a bell); mouse wheel on a node: Q; double-click a node: band off
    //   - click empty space: switches on the band that fits there (high pass below 200 Hz, low pass above 8 kHz, else a bell
    //     that is off) and moves it to the click
    class ReturnEqDisplay : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        static constexpr float rangeDb = 18.0f;

        ReturnEqDisplay (KaminariVocalProcessor& p, int returnIndex) : proc (p), state (p.apvts), ri (returnIndex)
        {
            setTitle (ri == 0 ? "Reverb return EQ" : "Delay return EQ");
            setTooltip ("Drag a band to move it; mouse wheel sets its Q; double-click switches it off. Click empty space to switch on a band there.");
            startTimerHz (30);
        }
        ~ReturnEqDisplay() override { stopTimer(); }

        std::function<void (int)> onSelect;
        int selectedBand() const noexcept { return selected; }
        void select (int b) { selected = b; if (onSelect) onSelect (b); repaint(); }

        float xForFreq (double f) const { return (float) (std::log (f / 20.0) / std::log (1000.0)) * (float) getWidth(); }
        double freqForX (float x) const { return 20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) x / juce::jmax (1, getWidth()))); }
        float yForDb (double db) const { return (float) (0.5 - db / (2.0 * rangeDb)) * (float) getHeight(); }
        double dbForY (float y) const { return (0.5 - (double) y / juce::jmax (1, getHeight())) * 2.0 * rangeDb; }

        juce::String id (int b, const char* what) const { return juce::String (ri == 0 ? "rv_eq" : "dl_eq") + juce::String (b + 1) + "_" + what; }
        kv::ReturnEqSettings::Band band (int b) const
        {
            kv::ReturnEqSettings::Band x;
            x.on = state.getRawParameterValue (id (b, "on"))->load() > 0.5f;
            x.freq = state.getRawParameterValue (id (b, "freq"))->load();
            x.q = state.getRawParameterValue (id (b, "q"))->load();
            x.gainDb = kv::ReturnEqSettings::hasGain (b) ? state.getRawParameterValue (id (b, "gain"))->load() : 0.0f;
            return x;
        }
        juce::Point<float> nodePos (int b) const { const auto x = band (b); return { xForFreq (x.freq), yForDb (x.gainDb) }; }

        static double magnitudeDb (int k, const kv::ReturnEqSettings::Band& b, double f, double fs)
        {
            const auto c = kv::ReturnEq::design (k, b, (float) fs);
            const double w = 2.0 * kv::pi * f / fs;
            const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
            const double m = std::abs ((double) c.b0 + (double) c.b1 * z1 + (double) c.b2 * z2)
                             / std::max (1e-12, std::abs (1.0 + (double) c.a1 * z1 + (double) c.a2 * z2));
            return 20.0 * std::log10 (std::max (m, 1e-9));
        }

        void paint (juce::Graphics& g) override
        {
            auto a = getLocalBounds().toFloat();
            g.setColour (navy950);
            g.fillRoundedRectangle (a, 4.0f);
            g.saveState();
            g.reduceClipRegion (getLocalBounds());
            g.setFont (font (9.5f, 0));
            for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
            {
                const float x = xForFreq (f);
                g.setColour (navy800);
                g.drawVerticalLine ((int) x, 0.0f, a.getBottom());
                g.setColour (mist.withAlpha (0.6f));
                g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) x + 3, getHeight() - 13, 30, 12, juce::Justification::centredLeft);
            }
            g.setColour (navy800);
            for (double db : { -12.0, -6.0, 6.0, 12.0 }) g.drawHorizontalLine ((int) yForDb (db), 0.0f, a.getRight());
            g.setColour (navy600);
            g.drawHorizontalLine ((int) yForDb (0.0), 0.0f, a.getRight());
            analyzers.draw (g, a, [this] (float x) { return freqForX (x); }, AnalyzerPair::Both, "BEFORE EQ", "AFTER EQ");

            const double fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
            const int solo = proc.returnEqSolo[ri].load();
            bool any = false;
            for (int k = 0; k < kv::ReturnEqSettings::numBands; ++k)
            {
                const auto b = band (k);
                if (! b.on) continue;
                any = true;
                juce::Path p;
                for (float x = 0.0f; x <= a.getWidth(); x += 2.0f)
                {
                    const float y = juce::jlimit (-20.0f, a.getHeight() + 20.0f, yForDb (magnitudeDb (k, b, freqForX (x), fs)));
                    if (x < 1.0f) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                juce::Path fill (p);
                fill.lineTo (a.getRight(), yForDb (0.0));
                fill.lineTo (0.0f, yForDb (0.0));
                fill.closeSubPath();
                const auto col = EqCurve::bandColour (k);
                g.setColour (col.withAlpha (k == selected ? 0.24f : 0.1f));
                g.fillPath (fill);
                g.setColour (col.withAlpha (0.55f));
                g.strokePath (p, juce::PathStrokeType (1.0f));
            }
            if (any)
            {
                juce::Path total;
                for (float x = 0.0f; x <= a.getWidth(); x += 2.0f)
                {
                    double db = 0.0;
                    for (int k = 0; k < kv::ReturnEqSettings::numBands; ++k)
                        if (const auto b = band (k); b.on) db += magnitudeDb (k, b, freqForX (x), fs);
                    const float y = juce::jlimit (-20.0f, a.getHeight() + 20.0f, yForDb (db));
                    if (x < 1.0f) total.startNewSubPath (x, y); else total.lineTo (x, y);
                }
                g.setColour (white.withAlpha (0.9f));
                g.strokePath (total, juce::PathStrokeType (1.6f));
            }
            for (int k = 0; k < kv::ReturnEqSettings::numBands; ++k)
            {
                const auto b = band (k);
                const auto pt = nodePos (k);
                const auto col = b.on ? EqCurve::bandColour (k) : steel;
                const float r = k == selected ? 8.0f : 6.5f;
                g.setColour (navy950);
                g.fillEllipse (pt.x - r, pt.y - r, 2 * r, 2 * r);
                g.setColour (col);
                g.drawEllipse (pt.x - r, pt.y - r, 2 * r, 2 * r, k == selected ? 2.0f : 1.4f);
                g.setFont (font (9.5f, 1));
                g.drawText (juce::String (k + 1), juce::Rectangle<float> (pt.x - r, pt.y - r, 2 * r, 2 * r), juce::Justification::centred);
                if (k == solo)
                {
                    g.setColour (amber);
                    g.drawText ("SOLO", juce::Rectangle<float> (pt.x - 20, pt.y - r - 14, 40, 12), juce::Justification::centred);
                }
            }
            g.restoreState();
            g.setColour (navy600);
            g.drawRoundedRectangle (a.reduced (0.5f), 4.0f, 1.0f);
        }

        int bandAt (juce::Point<float> p) const
        {
            for (int k = kv::ReturnEqSettings::numBands; --k >= 0;)
                if (nodePos (k).getDistanceFrom (p) < 11.0f)
                    return k;
            return -1;
        }

        // Band that a click on empty space switches on (-1: all that fit are on already).
        int bandForClick (double f) const
        {
            if (f < 200.0 && ! band (0).on) return 0;
            if (f > 8000.0 && ! band (3).on) return 3;
            for (int k : { 1, 2 }) if (! band (k).on) return k;
            return -1;
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragBand = bandAt (e.position);
            if (dragBand < 0 && ! e.mods.isPopupMenu())
            {
                const double f = freqForX (e.position.x);
                dragBand = bandForClick (f);
                if (dragBand >= 0)
                {
                    gesture (param (dragBand, "freq"), (float) f);
                    if (kv::ReturnEqSettings::hasGain (dragBand))
                        gesture (param (dragBand, "gain"), (float) juce::jlimit ((double) -rangeDb, (double) rangeDb, std::round (dbForY (e.position.y) * 10.0) / 10.0));
                    gesture (param (dragBand, "on"), 1.0f);
                    createdBand = dragBand;
                    createdMs = juce::Time::getMillisecondCounterHiRes();
                }
            }
            if (dragBand >= 0)
            {
                select (dragBand);
                param (dragBand, "freq")->beginChangeGesture();
                if (kv::ReturnEqSettings::hasGain (dragBand)) param (dragBand, "gain")->beginChangeGesture();
            }
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragBand < 0) return;
            setPlain (param (dragBand, "freq"), (float) freqForX (e.position.x));
            if (kv::ReturnEqSettings::hasGain (dragBand))
                setPlain (param (dragBand, "gain"), (float) juce::jlimit ((double) -rangeDb, (double) rangeDb, dbForY (e.position.y)));
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragBand >= 0)
            {
                param (dragBand, "freq")->endChangeGesture();
                if (kv::ReturnEqSettings::hasGain (dragBand)) param (dragBand, "gain")->endChangeGesture();
            }
            dragBand = -1;
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            const int b = bandAt (e.position);
            if (b >= 0 && ! (b == createdBand && juce::Time::getMillisecondCounterHiRes() - createdMs < 800.0))
                gesture (param (b, "on"), 0.0f);
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
        juce::RangedAudioParameter* param (int b, const char* what) { return state.getParameter (id (b, what)); }
        static void setPlain (juce::RangedAudioParameter* p, float v) { p->setValueNotifyingHost (p->convertTo0to1 (v)); }
        static void gesture (juce::RangedAudioParameter* p, float v) { p->beginChangeGesture(); setPlain (p, v); p->endChangeGesture(); }
        void timerCallback() override
        {
            analyzers.update (proc, proc.retAnalyserPre[ri], proc.retAnalyserPost[ri], AnalyzerPair::Both);
            repaint();
        }

        KaminariVocalProcessor& proc;
        APVTS& state;
        int ri;
        AnalyzerPair analyzers;
        int selected = -1, dragBand = -1, createdBand = -1;
        double createdMs = 0;
    };

    // One return-EQ band: on/off, solo, frequency, gain (bells) and Q.
    class ReturnEqBand : public juce::Component, private juce::Timer
    {
        KaminariVocalProcessor& proc;
        int ri, k;

    public:
        ReturnEqBand (KaminariVocalProcessor& p, int returnIndex, int bandIndex)
            : proc (p), ri (returnIndex), k (bandIndex),
              on (p.apvts, (pre (returnIndex, bandIndex) + "on").toRawUTF8(), "ON", "OFF", "Switch this band on or off (off = bypassed)."),
              freq (p.apvts, pre (returnIndex, bandIndex) + "freq", "Freq"),
              q (p.apvts, pre (returnIndex, bandIndex) + "q", "Q")
        {
            if (kv::ReturnEqSettings::hasGain (k))
            {
                gain = std::make_unique<RangeKnob> (p.apvts, pre (ri, k) + "gain", "Gain");
                addAndMakeVisible (*gain);
            }
            solo.setButtonText ("SOLO");
            solo.setClickingTogglesState (true);
            solo.setTooltip (k == 0 ? "Hear only what the high pass removes (below its frequency)."
                             : k == 3 ? "Hear only what the low pass removes (above its frequency)."
                                      : "Hear only the region around this bell.");
            solo.onClick = [this] { proc.returnEqSolo[ri].store (solo.getToggleState() ? k : -1); };
            for (auto* c : std::initializer_list<juce::Component*> { &on, &solo, &freq, &q })
                addAndMakeVisible (c);
            startTimerHz (10);
        }
        ~ReturnEqBand() override { stopTimer(); }

        static juce::String name (int k) { static const char* n[] = { "HIGH PASS", "BELL 1", "BELL 2", "LOW PASS" }; return n[k]; }
        void setKnobLNF (juce::LookAndFeel* l) { freq.setLNF (l); q.setLNF (l); if (gain) gain->setLNF (l); }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (navy800.withAlpha (selected ? 0.8f : 0.45f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (selected ? EqCurve::bandColour (k) : navy600);
            g.drawRoundedRectangle (r, 6.0f, selected ? 1.6f : 1.0f);
            g.setColour (EqCurve::bandColour (k));
            g.setFont (font (11.0f, 2, 0.1f));
            g.drawText (juce::String (k + 1) + "  " + name (k), 10, 6, getWidth() - 20, 14, juce::Justification::centredLeft);
        }
        void resized() override
        {
            auto b = getLocalBounds().reduced (8, 6);
            b.removeFromTop (18);
            auto row = b.removeFromTop (24);
            on.setBounds (row.removeFromLeft (52));
            row.removeFromLeft (6);
            solo.setBounds (row.removeFromLeft (56));
            b.removeFromTop (4);
            const int n = gain ? 3 : 2;
            const int w = b.getWidth() / n;
            freq.setBounds (b.removeFromLeft (w));
            if (gain) gain->setBounds (b.removeFromLeft (w));
            q.setBounds (b);
        }
        void setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }
        juce::TextButton solo;
        ToggleBox on;
        RangeKnob freq, q;
        std::unique_ptr<RangeKnob> gain;

    private:
        static juce::String pre (int r, int b) { return juce::String (r == 0 ? "rv_eq" : "dl_eq") + juce::String (b + 1) + "_"; }
        void timerCallback() override
        {
            solo.setToggleState (proc.returnEqSolo[ri].load() == k, juce::dontSendNotification);
            // a band that is off shows its settings dimmed
            const bool live = on.getToggleState();
            for (auto* c : std::initializer_list<juce::Component*> { &freq, &q, gain.get() })
                if (c != nullptr) c->setAlpha (live ? 1.0f : 0.45f);
        }
        bool selected = false;
    };

    // Overlay that replaces a Reverb or Delay panel's controls (below its header) with the return EQ, or with
    // ducking, wet gain and Delay / Reverb routing.
    class ReturnFxView : public juce::Component, private juce::Timer
    {
        KaminariVocalProcessor& proc;
        int ri;

    public:
        enum View { Eq, Duck };

        ReturnFxView (KaminariVocalProcessor& p, int returnIndex)
            : proc (p), ri (returnIndex), display (p, returnIndex),
              duckOn (p.apvts, (pfx (returnIndex) + "duck_on").toRawUTF8(), "DUCKING ON", "DUCKING OFF",
                      "Lowers the return while the vocal is above the threshold, so the effect fills the gaps between phrases."),
              source (p.apvts, pfx (returnIndex) + "duck_source", { "Vocal", "Raw input" },
                      "Vocal: the processed vocal you hear. Raw input: the vocal before the channel modules (follows the natural dynamics)."),
              thresh (p.apvts, pfx (returnIndex) + "duck_thresh", "Threshold", "-60", "0"),
              depth (p.apvts, pfx (returnIndex) + "duck_depth", "Depth", "0 dB", "30 dB"),
              attack (p.apvts, pfx (returnIndex) + "duck_attack", "Attack", "fast", "slow"),
              release (p.apvts, pfx (returnIndex) + "duck_release", "Release", "fast", "slow"),
              wet (p.apvts, pfx (returnIndex) + "wet_gain", "Wet Gain", "-24", "+12", "Return level after ducking: makes up the level the ducking takes away."),
              route (p.apvts, "fx_route", { "Off", "Delay > Reverb", "Reverb > Delay" },
                     "Feeds one return into the other. One direction at a time, so the two can never feed back."),
              routeAmt (p.apvts, "fx_route_amt", "Amount", "0 %", "100 %", "How much of the source return is sent into the other effect.")
        {
            setOpaque (true);
            addAndMakeVisible (display);
            for (int k = 0; k < kv::ReturnEqSettings::numBands; ++k)
            {
                auto* b = bands.add (new ReturnEqBand (p, ri, k));
                b->setKnobLNF (&lnf);
                addAndMakeVisible (b);
            }
            display.onSelect = [this] (int b) { for (int k = 0; k < bands.size(); ++k) bands[k]->setSelected (k == b); };
            for (auto* k : { &thresh, &depth, &attack, &release, &wet, &routeAmt }) { k->setLNF (&lnf); addChildComponent (k); }
            for (auto* c : std::initializer_list<juce::Component*> { &duckOn, &source, &route }) addChildComponent (c);
            setView (Eq);
            startTimerHz (30);
        }
        ~ReturnFxView() override
        {
            stopTimer();
            for (auto* b : bands) b->setKnobLNF (nullptr);
            for (auto* k : { &thresh, &depth, &attack, &release, &wet, &routeAmt }) k->setLNF (nullptr);
        }

        void setView (int v)
        {
            view = v;
            const bool eq = v == Eq;
            display.setVisible (eq);
            for (auto* b : bands) b->setVisible (eq);
            for (auto* c : std::initializer_list<juce::Component*> { &duckOn, &source, &route, &thresh, &depth, &attack, &release, &wet, &routeAmt })
                c->setVisible (! eq);
            resized();
            repaint();
        }
        int currentView() const noexcept { return view; }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (navy900);
            if (view == Eq)
            {
                g.setColour (mist);
                g.setFont (font (11.0f, 1, 0.1f));
                g.drawText ((ri == 0 ? "REVERB" : "DELAY") + juce::String (" RETURN EQ") + dot() + "applied to the wet signal only",
                            display.getX(), display.getY() - 18, 400, 14, juce::Justification::centredLeft);
                return;
            }
            drawTitledGroup (g, duckArea, "DUCKING");
            drawTitledGroup (g, wetArea, "OUTPUT");
            drawTitledGroup (g, routeArea, "DELAY / REVERB ROUTING");
            drawCaption (g, source, "SOURCE");
            // ducking meter
            auto m = meterArea.toFloat();
            g.setColour (mist);
            g.setFont (font (11.0f, 1, 0.1f));
            g.drawText ("DUCKING", meterArea.getX(), meterArea.getY() - 17, 120, 14, juce::Justification::centredLeft);
            g.setColour (navy950);
            g.fillRoundedRectangle (m, 2.0f);
            g.setColour (navy600);
            g.drawRoundedRectangle (m.reduced (0.5f), 2.0f, 1.0f);
            g.setColour (accent);
            g.fillRoundedRectangle (m.reduced (2.0f).withWidth ((m.getWidth() - 4.0f) * juce::jlimit (0.0f, 1.0f, shownGr / 30.0f)), 1.5f);
            g.setColour (mist);
            g.setFont (font (12.0f, 0));
            g.drawText (shownGr < 0.05f ? juce::String ("0.0 dB") : minusSign (-shownGr, 1) + " dB", meterArea.getX(), meterArea.getBottom() + 4, 120, 14,
                        juce::Justification::centredLeft);
            g.setColour (steel);
            g.setFont (font (11.5f, 0));
            const juce::String note = juce::roundToInt (plainValue (proc.apvts, "fx_route")) == 0
                                          ? juce::String ("Routing is off: each return is fed only by its own send.")
                                          : juce::String ("The source return (after its EQ, before ducking) is added to the other effect's input.");
            g.drawFittedText (note, routeNote, juce::Justification::centredLeft, 2);
        }

        void resized() override
        {
            auto b = getLocalBounds();
            if (view == Eq)
            {
                b.removeFromTop (22);
                display.setBounds (b.removeFromTop (juce::jmax (120, b.getHeight() - 190 - 12)));
                b.removeFromTop (12);
                const int gap = 10, w = (b.getWidth() - 3 * gap) / 4;
                for (auto* band : bands) { band->setBounds (b.removeFromLeft (w)); b.removeFromLeft (gap); }
                return;
            }
            b.removeFromTop (8);
            auto top = b.removeFromTop (190);
            duckArea = top.removeFromLeft (top.getWidth() * 3 / 4 - 6);
            top.removeFromLeft (12);
            wetArea = top;
            auto d = duckArea.reduced (12, 8).withTrimmedTop (22);
            auto ctl = d.removeFromLeft (170);
            duckOn.setBounds (ctl.removeFromTop (28).withWidth (140));
            ctl.removeFromTop (28);
            source.setBounds (ctl.removeFromTop (28).withWidth (160));
            ctl.removeFromTop (24);
            meterArea = ctl.removeFromTop (12).withWidth (150);
            d.removeFromLeft (12);
            const int kw = d.getWidth() / 4;
            for (auto* k : { &thresh, &depth, &attack, &release }) k->setBounds (d.removeFromLeft (kw).withHeight (130));
            wet.setBounds (wetArea.reduced (12, 8).withTrimmedTop (22).withHeight (130));
            b.removeFromTop (12);
            routeArea = b.removeFromTop (110);
            auto r = routeArea.reduced (12, 8).withTrimmedTop (22);
            route.setBounds (r.removeFromLeft (360).withSizeKeepingCentre (360, 28));
            r.removeFromLeft (20);
            routeAmt.setBounds (r.removeFromLeft (110));
            r.removeFromLeft (20);
            routeNote = r;
        }

        ReturnEqDisplay display;
        juce::OwnedArray<ReturnEqBand> bands;
        ToggleBox duckOn;
        SegParam source;
        RangeKnob thresh, depth, attack, release, wet;
        SegParam route;
        RangeKnob routeAmt;

    private:
        static juce::String pfx (int r) { return r == 0 ? "rv_" : "dl_"; }
        void timerCallback() override
        {
            const float gr = proc.duckGr[(size_t) ri].load();
            const float next = gr > shownGr ? gr : shownGr * 0.9f;
            if (std::abs (next - shownGr) > 0.01f) { shownGr = next; if (view == Duck) repaint (meterArea.expanded (2, 20)); }
            const bool routeOn = juce::roundToInt (plainValue (proc.apvts, "fx_route")) != 0;
            if (routeAmt.isEnabled() != routeOn)
            {
                routeAmt.setEnabled (routeOn);
                routeAmt.setAlpha (routeOn ? 1.0f : 0.38f);
                repaint (routeNote);
            }
            const bool duck = plainValue (proc.apvts, (pfx (ri) + "duck_on").toRawUTF8()) > 0.5f;
            for (auto* k : { &thresh, &depth, &attack, &release })
                k->setAlpha (duck ? 1.0f : 0.45f);
            source.setAlpha (duck ? 1.0f : 0.45f);
        }

        ModuleLNF lnf;
        int view = Eq;
        float shownGr = 0;
        juce::Rectangle<int> duckArea, wetArea, routeArea, meterArea, routeNote;
    };

    class SendTab : public juce::Button
    {
    public:
        SendTab (KaminariVocalProcessor& p, const juce::String& name, const char* onId, const char* levelId)
            : juce::Button (name), onParam (*p.apvts.getParameter (onId)), levelParam (*p.apvts.getParameter (levelId))
        {
            setRadioGroupId (91);
            setClickingTogglesState (true);
            setTooltip ("Show the " + name.toLowerCase() + " send.");
        }
        void paintButton (juce::Graphics& g, bool hover, bool) override
        {
            const bool sel = getToggleState(), on = onParam.getValue() > 0.5f;
            auto r = getLocalBounds().toFloat().reduced (sel ? 1.0f : 0.5f);
            g.setColour (sel || hover ? navy800 : navy900);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (sel ? accent : navy600);
            g.drawRoundedRectangle (r, 6.0f, sel ? 2.0f : 1.0f);
            auto b = getLocalBounds().reduced (12, 0);
            const auto dotR = b.removeFromLeft (8).toFloat().withSizeKeepingCentre (7.0f, 7.0f);
            if (on) { g.setColour (accent.withAlpha (0.3f)); g.fillEllipse (dotR.expanded (2.5f)); }
            g.setColour (on ? accent : navy600);
            g.fillEllipse (dotR);
            b.removeFromLeft (10);
            const auto name = getButtonText().toUpperCase();
            g.setColour (white);
            g.setFont (font (13.5f, 2, 0.1f));
            g.drawText (name, b, juce::Justification::centredLeft);
            const int w = (int) juce::GlyphArrangement::getStringWidth (font (13.5f, 2, 0.1f), name);
            g.setColour (mist);
            g.setFont (font (12.5f, 0, 0.04f));
            g.drawText (on ? levelParam.getCurrentValueAsText() : juce::String ("off"), b.withTrimmedLeft (w + 10), juce::Justification::centredLeft);
        }
    private:
        juce::RangedAudioParameter& onParam;
        juce::RangedAudioParameter& levelParam;
    };

    class SendsPage : public juce::Component, private juce::Timer
    {
    public:
        SendsPage (KaminariVocalProcessor& p, ReverbPanel& r, DelayPanel& d, WidenerPanel& w)
            : panels { &r, &d, &w }, rvFx (p, 0), dlFx (p, 1)
        {
            const char* viewNames[] = { "SOUND", "EQ", "DUCK & ROUTE" };
            const char* viewTips[] = { "The effect's own controls.", "Four-band EQ on the return (wet signal only), with its spectrum.",
                                       "Ducking from the vocal, wet gain, and Delay / Reverb routing." };
            for (int v = 0; v < 3; ++v)
            {
                auto* b = viewButtons.add (new juce::TextButton (viewNames[v]));
                b->setRadioGroupId (92);
                b->setClickingTogglesState (true);
                b->setTooltip (viewTips[v]);
                b->setConnectedEdges ((v > 0 ? juce::Button::ConnectedOnLeft : 0) | (v < 2 ? juce::Button::ConnectedOnRight : 0));
                b->onClick = [this, v] { if (viewButtons[v]->getToggleState()) { view = v; update(); } };
                addChildComponent (b);
            }
            addChildComponent (rvFx);
            addChildComponent (dlFx);
            const char* names[] = { "Reverb", "Delay", "Widener" };
            const char* on[] = { kvid::rvOn, kvid::dlOn, kvid::wdOn };
            const char* lv[] = { kvid::rvSend, kvid::dlSend, kvid::wdSend };
            for (int i = 0; i < numPanels; ++i)
            {
                auto* b = tabs.add (new SendTab (p, names[i], on[i], lv[i]));
                b->onClick = [this, i] { if (tabs[i]->getToggleState()) { current = i; if (onChange) onChange (i); update(); } };
                addAndMakeVisible (b);
                addChildComponent (*panels[(size_t) i]);
            }
            update();
            startTimerHz (5);
        }
        ~SendsPage() override { stopTimer(); }
        static constexpr int numPanels = 3;
        void show (int i) { current = juce::jlimit (0, numPanels - 1, i); update(); }
        int shown() const { return current; }
        // 0 = the effect's controls, 1 = return EQ, 2 = ducking and routing (Reverb and Delay)
        void showView (int v) { view = juce::jlimit (0, 2, v); update(); }
        int shownView() const { return view; }
        ReturnFxView& returnFx (int r) { return r == 0 ? rvFx : dlFx; }
        std::function<void (int)> onChange;

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (navy900);
            g.fillRoundedRectangle (b, 6.0f);
            g.setColour (navy600);
            g.drawRoundedRectangle (b, 6.0f, 1.0f);
            // bolt badge, title, subtitle
            auto badge = juce::Rectangle<float> (16.0f, 12.0f, 28.0f, 28.0f);
            g.setColour (navy800);
            g.fillRoundedRectangle (badge, 5.0f);
            g.setColour (accent);
            g.drawRoundedRectangle (badge, 5.0f, 1.2f);
            g.fillPath (boltPath (badge.withSizeKeepingCentre (11.0f, 15.0f)));
            g.setColour (white);
            g.setFont (font (21.0f, 3, 0.1f));
            g.drawText ("SENDS", 54, 12, 100, 28, juce::Justification::centredLeft);
            g.setColour (mist);
            g.setFont (font (13.0f, 0));
            g.drawText ("Reverb, delay and widener" + dot() + "returns are 100 % wet and added to the unchanged dry vocal", 128, 12, 640, 28,
                        juce::Justification::centredLeft);
        }
        void resized() override
        {
            auto b = getLocalBounds().reduced (16, 12);
            b.removeFromTop (38);
            auto row = b.removeFromTop (34);
            for (auto* t : tabs) { t->setBounds (row.removeFromLeft (150)); row.removeFromLeft (8); }
            auto views = row.removeFromRight (330).withSizeKeepingCentre (330, 30);
            for (auto* v : viewButtons) v->setBounds (views.removeFromLeft (110));
            b.removeFromTop (12);
            for (auto* p : panels) p->setBounds (b);
            rvFx.setBounds (b.withTrimmedTop (90));
            dlFx.setBounds (b.withTrimmedTop (90));
        }
    private:
        void timerCallback() override { for (auto* t : tabs) t->repaint(); }
        void update()
        {
            for (int i = 0; i < numPanels; ++i)
            {
                tabs[i]->setToggleState (i == current, juce::dontSendNotification);
                panels[(size_t) i]->setVisible (i == current);
            }
            const bool hasFx = current == 0 || current == 1;
            for (int v = 0; v < viewButtons.size(); ++v)
            {
                viewButtons[v]->setVisible (hasFx);
                viewButtons[v]->setToggleState (v == view, juce::dontSendNotification);
            }
            rvFx.setVisible (current == 0 && view > 0);
            dlFx.setVisible (current == 1 && view > 0);
            if (view > 0) { rvFx.setView (view == 1 ? ReturnFxView::Eq : ReturnFxView::Duck); dlFx.setView (view == 1 ? ReturnFxView::Eq : ReturnFxView::Duck); }
            rvFx.toFront (false);
            dlFx.toFront (false);
        }
        std::array<juce::Component*, numPanels> panels;
        ReturnFxView rvFx, dlFx;
        juce::OwnedArray<SendTab> tabs;
        juce::OwnedArray<juce::TextButton> viewButtons;
        int current = 0, view = 0;
    };
}
