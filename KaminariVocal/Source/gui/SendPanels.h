#pragma once

#include "../PluginProcessor.h"
#include "Widgets.h"

namespace kvui
{
    namespace colours
    {
        inline const juce::Colour navy950 { 0xff070f1f }, navy900 { 0xff0b1a33 }, navy800 { 0xff12264a },
                                  navy600 { 0xff2b4a82 }, white { 0xfff4f7fc }, mist { 0xffa9b8d6 },
                                  bolt { 0xff5ce1ff }, amber { 0xffffb547 };
    }

    inline Palette palette()
    {
        using namespace colours;
        return { navy800, navy600, bolt, navy600, white, bolt.withAlpha (0.85f), navy900, white, navy950 };
    }

    // Lays out the visible components left to right in rows of fixed-size cells.
    inline void flow (juce::Rectangle<int> area, const std::vector<juce::Component*>& items, int w, int h, int gap = 8)
    {
        int x = area.getX(), y = area.getY();
        for (auto* c : items)
        {
            if (c == nullptr || ! c->isVisible())
                continue;
            if (x + w > area.getRight() && x > area.getX())
            {
                x = area.getX();
                y += h + gap;
            }
            c->setBounds (x, y, w, h);
            x += w + gap;
        }
    }

    // Caption + combo box bound to a choice parameter.
    class ChoiceBox : public juce::Component
    {
    public:
        ChoiceBox (APVTS& state, const char* paramId, const juce::String& captionText, const juce::String& tip,
                   std::function<void (juce::ComboBox&)> fill = nullptr)
        {
            auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId));
            jassert (p != nullptr);
            if (fill != nullptr)
                fill (box);
            else
                box.addItemList (p->choices, 1);
            box.setTitle (p->getName (64));
            box.setDescription (tip);
            box.setTooltip (p->getName (64) + "\n" + tip);
            caption.setText (captionText, juce::dontSendNotification);
            caption.setJustificationType (juce::Justification::centredLeft);
            caption.setFont (uiFont (11.0f, true));
            caption.setColour (juce::Label::textColourId, colours::mist);
            caption.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (caption);
            addAndMakeVisible (box);
            att = std::make_unique<APVTS::ComboBoxAttachment> (state, paramId, box);
        }

        void resized() override
        {
            auto b = getLocalBounds();
            caption.setBounds (b.removeFromTop (16));
            box.setBounds (b.removeFromTop (26));
        }

        juce::ComboBox box;
        juce::Label caption;

    private:
        std::unique_ptr<APVTS::ComboBoxAttachment> att;
    };

    // Toggle button bound to a bool or two-choice parameter.
    class ToggleBox : public juce::TextButton
    {
    public:
        ToggleBox (APVTS& state, const char* paramId, const juce::String& onText, const juce::String& offText, const juce::String& tip)
            : on (onText), off (offText)
        {
            setClickingTogglesState (true);
            auto* p = state.getParameter (paramId);
            setTitle (p->getName (64));
            setDescription (tip);
            setTooltip (p->getName (64) + "\n" + tip + "\nClick or press Space to toggle.");
            setWantsKeyboardFocus (true);
            onStateChange = [this] { setButtonText (getToggleState() ? on : off); };
            att = std::make_unique<APVTS::ButtonAttachment> (state, paramId, *this);
            setButtonText (getToggleState() ? on : off);
        }

    private:
        juce::String on, off;
        std::unique_ptr<APVTS::ButtonAttachment> att;
    };

    // Horizontal return meter (peak, -60 .. 0 dBFS) with a caption.
    class ReturnMeter : public juce::Component, private juce::Timer
    {
    public:
        explicit ReturnMeter (std::atomic<float>& source) : src (source)
        {
            setTitle ("Return level");
            startTimerHz (30);
        }
        ~ReturnMeter() override { stopTimer(); }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (colours::navy950);
            g.fillRoundedRectangle (b, 3.0f);
            g.setColour (colours::navy600);
            g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);
            const float db = juce::Decibels::gainToDecibels (level, -60.0f);
            const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            auto bar = b.reduced (2.0f);
            g.setColour (db > -1.0f ? colours::amber : colours::bolt);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * frac), 2.0f);
        }

    private:
        void timerCallback() override
        {
            const float target = src.load();
            const float next = target > level ? target : level * 0.85f;
            if (std::abs (next - level) > 1.0e-5f) { level = next; repaint(); }
        }

        std::atomic<float>& src;
        float level = 0.0f;
    };

    // Current plain value of a parameter. Reads the parameter object, not the APVTS atomic: attachment callbacks
    // run before APVTS has updated its atomic.
    inline int choiceIndex (APVTS& state, const char* id)
    {
        auto* p = state.getParameter (id);
        return juce::roundToInt (p->convertFrom0to1 (p->getValue()));
    }

    inline void styleText (juce::Label& l, float size, juce::Colour c, bool bold = false)
    {
        l.setFont (uiFont (size, bold));
        l.setColour (juce::Label::textColourId, c);
        l.setJustificationType (juce::Justification::topLeft);
        l.setMinimumHorizontalScale (1.0f);   // wrap onto more lines instead of squeezing the text
        l.setInterceptsMouseClicks (false, false);
    }

    // On / send level / tap point / return meter, shown at the top of each Advanced send panel.
    class SendHeader : public juce::Component
    {
    public:
        SendHeader (KaminariVocalProcessor& p, int send, const char* onId, const char* levelId, const char* tapId, const juce::String& name)
            : power (p.apvts, onId, name + " ON", name + " OFF", "Switches the " + name.toLowerCase() + " send and its return on or off. Off fades the return out."),
              level (p.apvts, levelId, "Send", "Level sent to the " + name.toLowerCase() + ". Off sends nothing; the dry vocal is unchanged."),
              tap (p.apvts, tapId, "Tap point", "Post-fader follows Output Gain. Pre-fader taps the vocal before Output Gain."),
              meter (p.returnPeak[(size_t) send])
        {
            addAndMakeVisible (power);
            addAndMakeVisible (level);
            addAndMakeVisible (tap);
            addAndMakeVisible (meter);
            meterCaption.setText ("Return", juce::dontSendNotification);
            styleText (meterCaption, 11.0f, colours::mist, true);
            addAndMakeVisible (meterCaption);
        }

        void resized() override
        {
            auto b = getLocalBounds();
            power.setBounds (b.removeFromLeft (120).withSizeKeepingCentre (116, 30));
            b.removeFromLeft (8);
            level.setBounds (b.removeFromLeft (84));
            b.removeFromLeft (8);
            tap.setBounds (b.removeFromLeft (140).withSizeKeepingCentre (140, 44));
            b.removeFromLeft (16);
            auto m = b.removeFromLeft (220).withSizeKeepingCentre (220, 34);
            meterCaption.setBounds (m.removeFromTop (16));
            meter.setBounds (m.removeFromTop (12));
        }

        ToggleBox power;
        Knob level;
        ChoiceBox tap;
        ReturnMeter meter;
        juce::Label meterCaption;
    };

    //==================================================================================================================
    class ReverbPanel : public juce::Component
    {
    public:
        explicit ReverbPanel (KaminariVocalProcessor& p)
            : header (p, KaminariVocalProcessor::Reverb, kvid::rvOn, kvid::rvSend, kvid::rvTap, "Reverb"),
              mode (p.apvts, kvid::rvMode, "Mode", "Reverb algorithm. Each mode is a different algorithm, not a preset.",
                    [] (juce::ComboBox& box)
                    {
                        juce::String family;
                        for (int i = 0; i < kv::numReverbModes; ++i)
                        {
                            const auto& m = kv::reverbMode (i);
                            if (family != m.family)
                            {
                                family = m.family;
                                box.addSectionHeading (family);
                            }
                            box.addItem (m.name, i + 1);
                        }
                    }),
              decay (p.apvts, kvid::rvDecay, "Decay", "Time for the tail to fall by 60 dB."),
              size (p.apvts, kvid::rvSize, "Size", "Scale of the space. In Nonlin it sets the envelope length."),
              preDelay (p.apvts, kvid::rvPreDelay, "Pre-delay", "Gap between the vocal and the start of the reverb."),
              hiCut (p.apvts, kvid::rvHiCut, "High Cut", "Darkens the reverb: input filter and in-tail damping."),
              loCut (p.apvts, kvid::rvLoCut, "Low Cut", "Removes low end from the reverb input."),
              modRate (p.apvts, kvid::rvModRate, "Mod Rate", "Speed of the delay modulation in the tail."),
              modDepth (p.apvts, kvid::rvModDepth, "Mod Depth", "Amount of delay modulation (chorus, random, detune, ensemble or wow/flutter, by mode)."),
              density (p.apvts, kvid::rvDensity, "Density", "Echo density: strength of the input diffusion."),
              attack (p.apvts, kvid::rvAttack, "Attack", "Ambience: balance of early reflections (0) and tail (100). Nonlin: gated (0), truncated (50), reverse (100)."),
              modeAttachment (*p.apvts.getParameter (kvid::rvMode), [this] (float v) { modeChanged (juce::roundToInt (v)); })
        {
            for (auto* c : std::initializer_list<juce::Component*> { &header, &mode, &decay, &size, &preDelay, &hiCut, &loCut,
                                                                     &modRate, &modDepth, &density, &attack, &guide, &description })
                addAndMakeVisible (c);
            for (auto* k : { &decay, &size, &preDelay, &hiCut, &loCut, &modRate, &modDepth, &density, &attack })
                k->setLNF (&lnf);
            guide.setText (kv::reverbFamilyGuide(), juce::dontSendNotification);
            styleText (guide, 12.0f, colours::mist);
            styleText (description, 13.0f, colours::white, true);
            modeAttachment.sendInitialUpdate();
        }

        ~ReverbPanel() override
        {
            for (auto* k : { &decay, &size, &preDelay, &hiCut, &loCut, &modRate, &modDepth, &density, &attack })
                k->setLNF (nullptr);
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (12);
            header.setBounds (b.removeFromTop (86));
            b.removeFromTop (8);
            auto row = b.removeFromTop (46);
            mode.setBounds (row.removeFromLeft (200));
            row.removeFromLeft (16);
            description.setBounds (row);
            guide.setBounds (b.removeFromTop (36));
            b.removeFromTop (8);
            flow (b, { &decay, &size, &preDelay, &hiCut, &loCut, &modRate, &modDepth, &density, &attack }, 84, 100);
        }

        void modeChanged (int m)
        {
            const auto use = kv::reverbControlUse (m);
            decay.setVisible (use.decay);
            modRate.setVisible (use.modulation);
            modDepth.setVisible (use.modulation);
            attack.setVisible (use.attack);
            description.setText (juce::String (kv::reverbMode (m).name) + ": " + kv::reverbMode (m).description, juce::dontSendNotification);
            resized();
        }

        ModuleLNF lnf { palette() };
        SendHeader header;
        ChoiceBox mode;
        Knob decay, size, preDelay, hiCut, loCut, modRate, modDepth, density, attack;
        juce::Label guide, description;

    private:
        juce::ParameterAttachment modeAttachment;
    };

    //==================================================================================================================
    class DelayPanel : public juce::Component
    {
    public:
        explicit DelayPanel (KaminariVocalProcessor& p)
            : header (p, KaminariVocalProcessor::Delay, kvid::dlOn, kvid::dlSend, kvid::dlTap, "Delay"),
              mode (p.apvts, kvid::dlMode, "Mode", "Single: one echo time. Dual: independent left and right echoes. Ping-Pong: echoes alternate left and right (input summed to mono)."),
              style (p.apvts, kvid::dlStyle, "Style", "Tone and saturation character of the repeats."),
              unit1 (p.apvts, kvid::dlT1Unit, "Echo 1", "Time in milliseconds, or a note value (straight, dotted or triplet) synced to the host tempo."),
              note1 (p.apvts, kvid::dlT1Note, "Note", "Note value for Echo 1."),
              unit2 (p.apvts, kvid::dlT2Unit, "Echo 2", "Time in milliseconds, or a note value synced to the host tempo."),
              note2 (p.apvts, kvid::dlT2Note, "Note", "Note value for Echo 2."),
              shape (p.apvts, kvid::dlWobbleShape, "Wobble Shape", "Waveform of the wobble pitch variation."),
              diffPos (p.apvts, kvid::dlDiffLoop, "Diffusion", "Post: every repeat equally diffused. Loop: each repeat more diffused than the last."),
              time1 (p.apvts, kvid::dlT1Ms, "Time 1", "Echo 1 time. Changing it while audio plays bends the pitch, like tape."),
              time2 (p.apvts, kvid::dlT2Ms, "Time 2", "Echo 2 time."),
              feedback (p.apvts, kvid::dlFeedback, "Feedback", "Number of repeats. Limited below runaway; the style's saturation also bounds the level."),
              loCut (p.apvts, kvid::dlLoCut, "Low Cut", "Removes low end from every repeat."),
              hiCut (p.apvts, kvid::dlHiCut, "High Cut", "Darkens every repeat."),
              saturation (p.apvts, kvid::dlSaturation, "Saturation", "Style-dependent drive inside the echo loop."),
              width (p.apvts, kvid::dlWidth, "Width", "Stereo spread of the echoes. Above 75 % out-of-phase content pushes them past the speakers."),
              offset (p.apvts, kvid::dlOffset, "L/R Offset", "Small time difference between left and right. Needs Width above 0."),
              accent (p.apvts, kvid::dlAccent, "Accent", "Right: odd repeats louder. Left: even (off-beat) repeats louder."),
              accent2 (p.apvts, kvid::dlAccent2, "Accent 2", "Accent for Echo 2 (right channel)."),
              balance (p.apvts, kvid::dlBalance, "Balance", "Relative level of the left and right echoes."),
              fbMix (p.apvts, kvid::dlFbMix, "FB Mix", "0: independent echoes. 50: equal feedback into both. 100: each echo feeds the other."),
              fbBal (p.apvts, kvid::dlFbBal, "FB Bal", "Right: more feedback on the right echo. Left: more on the left."),
              groove (p.apvts, kvid::dlGroove, "Groove", "Left: shuffle. Right: swing. Shifts alternate repeats towards a triplet feel."),
              feel (p.apvts, kvid::dlFeel, "Feel", "Moves every echo behind (drag, +) or ahead of (rush, -) the beat."),
              wobble (p.apvts, kvid::dlWobble, "Wobble", "Tape-like pitch wobble on the repeats."),
              wobbleRate (p.apvts, kvid::dlWobbleRate, "Rate", "Wobble speed."),
              wobbleSync (p.apvts, kvid::dlWobbleSync, "Sync", "Left: paths drift to different rates. Centre: locked. Right: left and right wobble in opposition."),
              diffusion (p.apvts, kvid::dlDiffusion, "Diffusion", "Smears the repeats; high settings become reverb-like."),
              diffSize (p.apvts, kvid::dlDiffSize, "Diff Size", "Character of the diffusion, from phasing (small) to reverb-like (large)."),
              prime (p.apvts, kvid::dlPrime, "PRIME ON", "PRIME OFF", "Rounds echo times to prime sample counts to avoid resonant build-up with short times and feedback."),
              modeAttachment (*p.apvts.getParameter (kvid::dlMode), [this] (float) { updateVisibility(); }),
              unit1Attachment (*p.apvts.getParameter (kvid::dlT1Unit), [this] (float) { updateVisibility(); }),
              unit2Attachment (*p.apvts.getParameter (kvid::dlT2Unit), [this] (float) { updateVisibility(); }),
              styleAttachment (*p.apvts.getParameter (kvid::dlStyle), [this] (float v)
              {
                  styleText_.setText (kv::delayStyleDescription (juce::roundToInt (v)), juce::dontSendNotification);
              }),
              apvts (p.apvts)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &header, &mode, &style, &unit1, &note1, &unit2, &note2, &shape, &diffPos, &prime, &styleText_ })
                addAndMakeVisible (c);
            for (auto* k : knobs())
            {
                addAndMakeVisible (k);
                k->setLNF (&lnf);
            }
            styleText (styleText_, 12.0f, colours::mist);
            modeAttachment.sendInitialUpdate();
            styleAttachment.sendInitialUpdate();
        }

        ~DelayPanel() override
        {
            for (auto* k : knobs())
                k->setLNF (nullptr);
        }

        std::vector<Knob*> knobs()
        {
            return { &time1, &time2, &feedback, &loCut, &hiCut, &saturation, &width, &offset, &accent, &accent2, &balance,
                     &fbMix, &fbBal, &groove, &feel, &wobble, &wobbleRate, &wobbleSync, &diffusion, &diffSize };
        }

        void updateVisibility()
        {
            const int m = choiceIndex (apvts, kvid::dlMode);
            const bool dual = m == kv::DelaySettings::Dual, ping = m == kv::DelaySettings::PingPong;
            const bool ms1 = choiceIndex (apvts, kvid::dlT1Unit) == 0;
            const bool ms2 = choiceIndex (apvts, kvid::dlT2Unit) == 0;
            unit1.caption.setText (ping ? "Ping" : "Echo 1", juce::dontSendNotification);
            unit2.caption.setText (ping ? "Pong" : "Echo 2", juce::dontSendNotification);
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
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (12);
            header.setBounds (b.removeFromTop (86));
            b.removeFromTop (8);
            auto row = b.removeFromTop (46);
            for (auto* c : std::initializer_list<juce::Component*> { &mode, &style })
            {
                c->setBounds (row.removeFromLeft (130));
                row.removeFromLeft (10);
            }
            for (auto* c : std::initializer_list<juce::Component*> { &unit1, &note1, &unit2, &note2 })
            {
                if (! c->isVisible()) continue;
                c->setBounds (row.removeFromLeft (84));
                row.removeFromLeft (8);
            }
            styleText_.setBounds (row.reduced (4, 0));
            b.removeFromTop (8);
            auto controls = b.removeFromTop (b.getHeight() - 52);
            flow (controls, { &time1, &time2, &feedback, &loCut, &hiCut, &saturation, &width, &offset, &accent, &accent2, &balance,
                              &fbMix, &fbBal, &groove, &feel, &wobble, &wobbleRate, &wobbleSync, &diffusion, &diffSize }, 76, 96, 6);
            auto bottom = b;
            shape.setBounds (bottom.removeFromLeft (130).withHeight (44));
            bottom.removeFromLeft (10);
            diffPos.setBounds (bottom.removeFromLeft (110).withHeight (44));
            bottom.removeFromLeft (10);
            prime.setBounds (bottom.removeFromLeft (110).withTrimmedTop (16).withHeight (26));
        }

        ModuleLNF lnf { palette() };
        SendHeader header;
        ChoiceBox mode, style, unit1, note1, unit2, note2, shape, diffPos;
        Knob time1, time2, feedback, loCut, hiCut, saturation, width, offset, accent, accent2, balance, fbMix, fbBal,
             groove, feel, wobble, wobbleRate, wobbleSync, diffusion, diffSize;
        ToggleBox prime;
        juce::Label styleText_;

    private:
        juce::ParameterAttachment modeAttachment, unit1Attachment, unit2Attachment, styleAttachment;
        APVTS& apvts;
    };

    //==================================================================================================================
    class WidenerPanel : public juce::Component
    {
    public:
        explicit WidenerPanel (KaminariVocalProcessor& p)
            : header (p, KaminariVocalProcessor::Widener, kvid::wdOn, kvid::wdSend, kvid::wdTap, "Widener"),
              type (p.apvts, kvid::wdType, "Widener", "MicroShift and SideWidener are separate algorithms. Only the selected one runs."),
              msStyle (p.apvts, kvid::msStyle, "Style", "I, II and III differ in pitch and delay variation, tone, saturation and de-glitching."),
              msDetune (p.apvts, kvid::msDetune, "Detune", "Amount of continuously varying micro pitch shift. 100 % = the style's own amount."),
              msDelay (p.apvts, kvid::msDelay, "Delay", "Amount of continuously varying delay. 100 % = the style's own amount."),
              msFocus (p.apvts, kvid::msFocus, "Focus", "Crossover: only content above this frequency is widened, keeping the low end stable."),
              swMode (p.apvts, kvid::swMode, "Mode", "Mode 1: subtle, no smear. Mode 3: widest, room-like smear. Mode 2: between."),
              swWidth (p.apvts, kvid::swWidth, "Width", "Amount of side signal added. The mono sum is not affected."),
              swTone (p.apvts, kvid::swTone, "Tone", "0: widen the midrange only. 100: widen all frequencies."),
              swOutput (p.apvts, kvid::swOutput, "Output", "Level of the widened signal (-inf to 0 dB)."),
              typeAttachment (*p.apvts.getParameter (kvid::wdType), [this] (float) { update(); }),
              msStyleAttachment (*p.apvts.getParameter (kvid::msStyle), [this] (float) { update(); }),
              swModeAttachment (*p.apvts.getParameter (kvid::swMode), [this] (float) { update(); }),
              apvts (p.apvts)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &header, &type, &msStyle, &msDetune, &msDelay, &msFocus,
                                                                     &swMode, &swWidth, &swTone, &swOutput, &note, &description })
                addAndMakeVisible (c);
            for (auto* k : { &msDetune, &msDelay, &msFocus, &swWidth, &swTone, &swOutput })
                k->setLNF (&lnf);
            styleText (note, 12.0f, colours::mist);
            styleText (description, 13.0f, colours::white, true);
            typeAttachment.sendInitialUpdate();
        }

        ~WidenerPanel() override
        {
            for (auto* k : { &msDetune, &msDelay, &msFocus, &swWidth, &swTone, &swOutput })
                k->setLNF (nullptr);
        }

        void update()
        {
            const bool micro = choiceIndex (apvts, kvid::wdType) == kv::WidenerSettings::MicroShift;
            for (auto* c : std::initializer_list<juce::Component*> { &msStyle, &msDetune, &msDelay, &msFocus })
                c->setVisible (micro);
            for (auto* c : std::initializer_list<juce::Component*> { &swMode, &swWidth, &swTone, &swOutput })
                c->setVisible (! micro);
            if (micro)
            {
                description.setText (kv::microShiftStyleDescription (choiceIndex (apvts, kvid::msStyle)),
                                     juce::dontSendNotification);
                note.setText ("MicroShift: left is shifted up and right down by a few varying cents, each with a varying short delay. "
                              "No Mix control: the return is 100 % wet, so the send level sets the blend. Content below Focus is not returned.",
                              juce::dontSendNotification);
            }
            else
            {
                description.setText (kv::sideWidenerModeDescription (choiceIndex (apvts, kvid::swMode)),
                                     juce::dontSendNotification);
                note.setText ("SideWidener: returns only side signal (left = +, right = -), so the mono mix is unchanged. "
                              "Bypass is the send's ON switch.", juce::dontSendNotification);
            }
            resized();
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (12);
            header.setBounds (b.removeFromTop (86));
            b.removeFromTop (8);
            auto row = b.removeFromTop (46);
            type.setBounds (row.removeFromLeft (160));
            row.removeFromLeft (12);
            if (msStyle.isVisible()) { msStyle.setBounds (row.removeFromLeft (90)); row.removeFromLeft (12); }
            if (swMode.isVisible())  { swMode.setBounds (row.removeFromLeft (110)); row.removeFromLeft (12); }
            description.setBounds (row);
            note.setBounds (b.removeFromTop (40));
            b.removeFromTop (8);
            flow (b, { &msDetune, &msDelay, &msFocus, &swWidth, &swTone, &swOutput }, 96, 110);
        }

        ModuleLNF lnf { palette() };
        SendHeader header;
        ChoiceBox type, msStyle;
        Knob msDetune, msDelay, msFocus;
        ChoiceBox swMode;
        Knob swWidth, swTone, swOutput;
        juce::Label note, description;

    private:
        juce::ParameterAttachment typeAttachment, msStyleAttachment, swModeAttachment;
        APVTS& apvts;
    };

    //==================================================================================================================
    // Compact send strip for the Basic view: on/off, send level, current mode, return meter, and a button that opens
    // the send's Advanced panel.
    class SendStrip : public juce::Component
    {
    public:
        SendStrip (KaminariVocalProcessor& p, int send, const char* onId, const char* levelId, const char* modeId,
                   const juce::String& name, std::function<juce::String (int)> modeText)
            : title (name),
              power (p.apvts, onId, "ON", "OFF", "Switches the " + name.toLowerCase() + " send and its return on or off."),
              level (p.apvts, levelId, name + " Send", "Level sent to the " + name.toLowerCase() + ". The return is 100 % wet; the dry vocal is unchanged."),
              meter (p.returnPeak[(size_t) send]),
              modeLabelText (std::move (modeText)),
              modeAttachment (*p.apvts.getParameter (modeId), [this] (float v)
              {
                  mode.setText (modeLabelText (juce::roundToInt (v)), juce::dontSendNotification);
              })
        {
            setTitle (name + " send");
            level.setLNF (&lnf);
            styleText (mode, 12.0f, colours::mist);
            mode.setJustificationType (juce::Justification::centred);
            advanced.setButtonText ("Advanced");
            advanced.setTooltip ("Open the " + name.toLowerCase() + " send's detailed controls.");
            advanced.setTitle ("Open " + name.toLowerCase() + " advanced controls");
            for (auto* c : std::initializer_list<juce::Component*> { &power, &level, &meter, &mode, &advanced })
                addAndMakeVisible (c);
            modeAttachment.sendInitialUpdate();
        }

        ~SendStrip() override { level.setLNF (nullptr); }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (colours::navy900);
            g.fillRoundedRectangle (b, 6.0f);
            g.setColour (colours::navy600);
            g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
            g.setColour (colours::white);
            g.setFont (uiFont (15.0f, true));
            g.drawText (title.toUpperCase(), getLocalBounds().reduced (12, 8).removeFromTop (22), juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (12, 8);
            auto top = b.removeFromTop (22);
            power.setBounds (top.removeFromRight (56));
            b.removeFromTop (4);
            advanced.setBounds (b.removeFromBottom (26));
            b.removeFromBottom (6);
            meter.setBounds (b.removeFromBottom (10));
            b.removeFromBottom (4);
            mode.setBounds (b.removeFromBottom (18));
            level.setBounds (b.withSizeKeepingCentre (juce::jmin (b.getWidth(), 120), b.getHeight()));
        }

        juce::String title;
        ToggleBox power;
        ModuleLNF lnf { palette() };
        Knob level;
        ReturnMeter meter;
        juce::Label mode;
        juce::TextButton advanced;

    private:
        std::function<juce::String (int)> modeLabelText;
        juce::ParameterAttachment modeAttachment;
    };
}
