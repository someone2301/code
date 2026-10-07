#pragma once

#include "AutoControls.h"
#include "EqCurve.h"
#include "Hammer.h"

// Basic view and shared chrome, laid out after the GUI preview (Main artboard).
namespace kvui
{
    inline juce::String dot() { return juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")); }
    inline juce::String minusSign (float v, int decimals)
    {
        const auto t = juce::String (std::abs (v), decimals);
        return v < -0.0001f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + t : t;
    }

    // Bolt power button bound to a bool parameter.
    class PowerButton : public juce::TextButton
    {
    public:
        PowerButton (APVTS& s, const juce::String& id, const juce::String& what)
        {
            getProperties().set ("kvStyle", "power");
            setClickingTogglesState (true);
            setTitle (what + " on/off");
            setTooltip (what + ": click to switch on or off.");
            att = std::make_unique<APVTS::ButtonAttachment> (s, id, *this);
        }
    private:
        std::unique_ptr<APVTS::ButtonAttachment> att;
    };

    // Vertical segmented peak meter (24 segments: white, amber in the top quarter, red for the top two).
    class SegMeter : public juce::Component, private juce::Timer
    {
    public:
        explicit SegMeter (std::atomic<float>& s) : src (s) { startTimerHz (30); }
        ~SegMeter() override { stopTimer(); }
        void paint (juce::Graphics& g) override
        {
            using namespace kvtheme;
            constexpr int n = 24;
            const float gap = 2.0f, h = (getHeight() - gap * (n - 1)) / n;
            const float db = juce::Decibels::gainToDecibels (level, -60.0f);
            const int lit = juce::roundToInt (juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 63.0f) * n);
            for (int i = 0; i < n; ++i)
            {
                juce::Colour c = navy800;
                if (i < lit) c = i >= 22 ? red : (i >= 18 ? amber : white);
                g.setColour (c);
                g.fillRoundedRectangle (0.0f, getHeight() - (i + 1) * (h + gap) + gap, (float) getWidth(), h, 1.0f);
            }
        }
        float peakDb() const { return juce::Decibels::gainToDecibels (hold, -100.0f); }
    private:
        void timerCallback() override
        {
            const float t = src.load();
            level = t > level ? t : level * 0.85f;
            hold = std::max (hold * 0.999f, t);
            repaint();
        }
        std::atomic<float>& src;
        float level = 0, hold = 0;
    };

    // IN / OUT rail: two meters, peak read-out and a gain knob.
    class Rail : public juce::Component, private juce::Timer
    {
    public:
        Rail (APVTS& s, const char* gainId, const juce::String& name, std::atomic<float>& l, std::atomic<float>& r, juce::LookAndFeel& lnf)
            : title (name), ml (l), mr (r), knob (s, gainId, {}, name == "IN" ? "Input gain before the channel and the sends."
                                                                              : "Output gain of the dry vocal. Post-fader sends follow it.")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &ml, &mr, &knob, &peak })
                addAndMakeVisible (c);
            knob.setLNF (&lnf);
            knob.label.setVisible (false);
            knob.value.setColour (juce::Label::textColourId, kvtheme::mist);
            knob.value.setFont (kvtheme::font (12.0f, 0));
            peak.setJustificationType (juce::Justification::centred);
            peak.setFont (kvtheme::font (12.0f, 1));
            startTimerHz (4);
        }
        ~Rail() override { stopTimer(); }
        void paint (juce::Graphics& g) override
        {
            using namespace kvtheme;
            auto b = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (navy900);
            g.fillRoundedRectangle (b, 6.0f);
            g.setColour (navy600);
            g.drawRoundedRectangle (b, 6.0f, 1.0f);
            g.setColour (mist);
            g.setFont (font (13.0f, 3, 0.15f));
            g.drawText (title, getLocalBounds().removeFromTop (34), juce::Justification::centred);
        }
        void resized() override
        {
            knob.setBounds (getLocalBounds().removeFromBottom (92).reduced (1, 8));
            auto b = getLocalBounds().reduced (8, 10);
            b.removeFromTop (24);
            b.removeFromBottom (86);
            peak.setBounds (b.removeFromBottom (20));
            b.removeFromBottom (6);
            auto m = b.withSizeKeepingCentre (24, b.getHeight());
            ml.setBounds (m.removeFromLeft (10));
            mr.setBounds (m.removeFromRight (10));
        }
    private:
        void timerCallback() override
        {
            const float p = std::max (ml.peakDb(), mr.peakDb());
            peak.setText (p <= -99.0f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92\xe2\x88\x9e")) : minusSign (p, 1), juce::dontSendNotification);
        }
        juce::String title;
        SegMeter ml, mr;
    public:
        Knob knob;
    private:
        juce::Label peak;
    };

    // Value box: shows a parameter's text; drag up/down to change, double-click to type (EQ band fields).
    class ValueBox : public juce::Label
    {
    public:
        ValueBox() { setJustificationType (juce::Justification::centred); setFont (kvtheme::font (12.5f, 0)); setEditable (false, true, false); }
        void bind (juce::RangedAudioParameter* p)
        {
            param = p;
            att.reset();
            if (p != nullptr)
                att = std::make_unique<juce::ParameterAttachment> (*p, [this] (float) { setText (param->getCurrentValueAsText(), juce::dontSendNotification); });
            if (att) att->sendInitialUpdate();
            onTextChange = [this] { if (param && att) att->setValueAsCompleteGesture (param->convertFrom0to1 (param->getValueForText (getText()))); };
            setTooltip (p != nullptr ? p->getName (64) + "\nDrag up or down, double-click to type." : juce::String());
        }
        void paint (juce::Graphics& g) override
        {
            using namespace kvtheme;
            g.setColour (navy800);
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f);
            g.setColour (navy600);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
            if (! isBeingEdited())
            {
                g.setColour (white);
                g.setFont (getFont());
                g.drawText (getText(), getLocalBounds(), juce::Justification::centred);
            }
        }
        void mouseDown (const juce::MouseEvent& e) override { if (param && att) { att->beginGesture(); start = param->getValue(); startY = e.position.y; } }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! param || ! att) return;
            const float n = juce::jlimit (0.0f, 1.0f, start + (startY - e.position.y) / 200.0f * (e.mods.isShiftDown() ? 0.1f : 1.0f));
            att->setValueAsPartOfGesture (param->convertFrom0to1 (n));
        }
        void mouseUp (const juce::MouseEvent&) override { if (att) att->endGesture(); }
        juce::RangedAudioParameter* getParam() const noexcept { return param; }
    private:
        juce::RangedAudioParameter* param = nullptr;
        std::unique_ptr<juce::ParameterAttachment> att;
        float start = 0, startY = 0;
    };

    // Segmented choice built from buttons (Pre / Post / Off and similar). Not bound to a parameter.
    class Segmented : public juce::Component
    {
    public:
        Segmented (const juce::StringArray& items, const juce::String& what)
        {
            for (int i = 0; i < items.size(); ++i)
            {
                auto* b = buttons.add (new juce::TextButton (items[i]));
                b->setRadioGroupId (juce::Random::getSystemRandom().nextInt() & 0xffff);
                b->setClickingTogglesState (true);
                b->getProperties().set ("kvStyle", "seg");
                b->setTooltip (what + ": " + items[i]);
                b->onClick = [this, i] { if (buttons[i]->getToggleState() && onChange) onChange (i); };
                addAndMakeVisible (b);
            }
            const int group = 1000 + juce::Random::getSystemRandom().nextInt (100000);
            for (auto* b : buttons) b->setRadioGroupId (group);
        }
        void setSelected (int i) { if (juce::isPositiveAndBelow (i, buttons.size())) buttons[i]->setToggleState (true, juce::dontSendNotification); }
        void resized() override
        {
            auto b = getLocalBounds();
            const int w = b.getWidth() / juce::jmax (1, buttons.size());
            for (auto* btn : buttons) btn->setBounds (b.removeFromLeft (w));
        }
        std::function<void (int)> onChange;
        juce::OwnedArray<juce::TextButton> buttons;
    };

    //==================================================================================================================
    // EQ block of the Basic view: header (power, name, band count, analyzer settings, zoom), graph, selected-band row.
    class EqSection : public juce::Component, private juce::Timer
    {
    public:
        explicit EqSection (KaminariVocalProcessor& p)
            : proc (p), power (p.apvts, "eq_on", "EQ"), curve (p),
              analyser ([&p] { return p.analyserMode.load(); }, [&p] (int m) { p.analyserMode.store (m); }), zoomOut ("-"), zoomIn ("+"),
              active ("Active"), solo ("Solo"), del ("Delete")
        {
            setTitle ("EQ");
            for (auto* c : std::initializer_list<juce::Component*> { &power, &curve, &analyser, &resolution, &speed, &zoomOut, &zoomIn,
                                                                     &type, &freq, &gain, &q, &active, &solo, &del, &bandCount })
                addAndMakeVisible (c);
            styleText (bandCount, 12.0f, colours::mist);
            bandCount.setJustificationType (juce::Justification::centredLeft);
            resolution.addItemList (SpectrumProcessor::resolutionNames(), 1);
            speed.addItemList (SpectrumProcessor::speedNames(), 1);
            resolution.setSelectedId (p.analyserResolution.load() + 1, juce::dontSendNotification);
            speed.setSelectedId (p.analyserSpeed.load() + 1, juce::dontSendNotification);
            resolution.setTooltip ("Analyzer resolution (FFT size).");
            speed.setTooltip ("Analyzer fall-back speed.");
            resolution.onChange = [this] { proc.analyserResolution.store (resolution.getSelectedId() - 1); };
            speed.onChange = [this] { proc.analyserSpeed.store (speed.getSelectedId() - 1); };
            zoomOut.setTooltip ("Zoom out (larger dB range).");
            zoomIn.setTooltip ("Zoom in (smaller dB range).");
            zoomOut.onClick = [this] { zoom (+1); };
            zoomIn.onClick = [this] { zoom (-1); };
            for (const auto& n : kv::eqTypeNames) type.addItem (n, type.getNumItems() + 1);
            type.onChange = [this] { setParam ("type", (float) (type.getSelectedId() - 1)); };
            type.setTooltip ("Filter shape of the selected band.");
            active.setClickingTogglesState (true);
            active.onClick = [this] { setParam ("on", active.getToggleState() ? 1.0f : 0.0f); };
            active.setTooltip ("Switches the selected band on or off (bypass, not delete).");
            solo.setClickingTogglesState (true);
            solo.onClick = [this] { proc.eqSolo.store (solo.getToggleState() ? selected : -1); };
            solo.setTooltip ("Hear only what the selected band works on: around a bell, below a low shelf or low cut, above a high shelf or high cut.");
            del.onClick = [this] { setParam ("used", 0.0f); };
            del.setTooltip ("Removes the selected band.");
            curve.onSelect = [this] (int b) { select (b); };
            select (firstUsed());
            startTimerHz (8);
        }
        ~EqSection() override { stopTimer(); proc.eqSolo.store (-1); }

        // b < 0: no band selected (a click on empty graph space): the selected-band row is hidden
        void select (int b)
        {
            const bool none = b < 0;
            for (auto* c : std::initializer_list<juce::Component*> { &type, &freq, &gain, &q, &active, &solo, &del })
                c->setVisible (! none);
            if (none)
            {
                selected = -1;
                curve.selected = -1;
                proc.eqSolo.store (-1);
                if (onSelect) onSelect (-1);
                repaint();
                return;
            }
            selected = juce::jlimit (0, 7, b);
            curve.selected = selected;
            const juce::String pre = "eq" + juce::String (selected + 1) + "_";
            freq.bind (proc.apvts.getParameter (pre + "freq"));
            gain.bind (proc.apvts.getParameter (pre + "gain"));
            q.bind (proc.apvts.getParameter (pre + "q"));
            if (proc.eqSolo.load() >= 0) proc.eqSolo.store (selected);
            timerCallback();
            if (onSelect) onSelect (selected);
            repaint();
        }

        std::function<void (int)> onSelect;

        void paint (juce::Graphics& g) override
        {
            using namespace kvtheme;
            auto b = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (navy900);
            g.fillRoundedRectangle (b, 6.0f);
            g.setColour (navy600);
            g.drawRoundedRectangle (b, 6.0f, 1.0f);
            g.setColour (white);
            g.setFont (font (19.0f, 3, 0.08f));
            g.drawText ("EQ", 52, 10, 40, 28, juce::Justification::centredLeft);
            g.setColour (mist);
            g.setFont (font (12.0f, 0));
            const auto r = analyser.getBounds();
            g.drawText ("Analyzer", r.getX() - 62, r.getY(), 56, r.getHeight(), juce::Justification::centredRight);
            g.drawText ("Resolution", resolution.getX() - 70, r.getY(), 64, r.getHeight(), juce::Justification::centredRight);
            g.drawText ("Speed", speed.getX() - 46, r.getY(), 40, r.getHeight(), juce::Justification::centredRight);
            g.drawText ("Zoom", zoomOut.getX() - 44, r.getY(), 38, r.getHeight(), juce::Justification::centredRight);
            const auto f = type.getBounds();
            if (selected < 0) return;
            g.drawText ("Selected", 16, f.getY(), 60, f.getHeight(), juce::Justification::centredLeft);
            g.setColour (white);
            g.setFont (font (12.5f, 1));
            g.drawText ("Band " + juce::String (selected + 1), 72, f.getY(), 60, f.getHeight(), juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (16, 10);
            auto top = b.removeFromTop (28);
            power.setBounds (top.removeFromLeft (28));
            top.removeFromLeft (44);
            bandCount.setBounds (top.removeFromLeft (100));
            zoomIn.setBounds (top.removeFromRight (26));
            top.removeFromRight (6);
            zoomOut.setBounds (top.removeFromRight (26));
            top.removeFromRight (52);
            speed.setBounds (top.removeFromRight (74).reduced (0, 2));
            top.removeFromRight (52);
            resolution.setBounds (top.removeFromRight (74).reduced (0, 2));
            top.removeFromRight (118);
            analyser.setBounds (top.removeFromRight (110).reduced (0, 2));
            b.removeFromTop (8);
            auto bottom = b.removeFromBottom (26);
            b.removeFromBottom (8);
            curve.setBounds (b);
            bottom.removeFromLeft (124);
            type.setBounds (bottom.removeFromLeft (110));
            bottom.removeFromLeft (8);
            freq.setBounds (bottom.removeFromLeft (70));
            bottom.removeFromLeft (8);
            gain.setBounds (bottom.removeFromLeft (70));
            bottom.removeFromLeft (8);
            q.setBounds (bottom.removeFromLeft (62));
            del.setBounds (bottom.removeFromRight (72));
            bottom.removeFromRight (8);
            solo.setBounds (bottom.removeFromRight (56));
            bottom.removeFromRight (8);
            active.setBounds (bottom.removeFromRight (70));
        }

        KaminariVocalProcessor& proc;
        PowerButton power;
        EqCurve curve;
        AnalyzerToggles analyser;
        juce::ComboBox resolution, speed, type;
        juce::TextButton zoomOut, zoomIn;
        ValueBox freq, gain, q;
        juce::TextButton active, solo, del;
        juce::Label bandCount;
        int selected = 0;

    private:
        int firstUsed() const
        {
            for (int i = 0; i < 8; ++i)
                if (proc.apvts.getRawParameterValue ("eq" + juce::String (i + 1) + "_used")->load() > 0.5f) return i;
            return -1;
        }
        void setParam (const char* what, float v)
        {
            auto* p = proc.apvts.getParameter ("eq" + juce::String (selected + 1) + "_" + what);
            const float n = p->convertTo0to1 (v);
            if (std::abs (n - p->getValue()) > 1e-6f) { p->beginChangeGesture(); p->setValueNotifyingHost (n); p->endChangeGesture(); }
        }
        void zoom (int dir)
        {
            static const double ranges[] = { 6.0, 12.0, 18.0, 30.0 };
            int i = 2;
            for (int k = 0; k < 4; ++k) if (std::abs (ranges[k] - curve.getRange()) < 0.1) i = k;
            curve.setRange (ranges[juce::jlimit (0, 3, i + dir)]);
        }
        void timerCallback() override
        {
            int used = 0;
            for (int i = 0; i < 8; ++i) used += proc.apvts.getRawParameterValue ("eq" + juce::String (i + 1) + "_used")->load() > 0.5f ? 1 : 0;
            bandCount.setText (juce::String (used) + " of 8 bands", juce::dontSendNotification);
            if (selected >= 0 && proc.apvts.getRawParameterValue ("eq" + juce::String (selected + 1) + "_used")->load() < 0.5f) select (-1);
            if (selected < 0) return;
            const juce::String pre = "eq" + juce::String (selected + 1) + "_";
            type.setSelectedId (juce::roundToInt (proc.apvts.getRawParameterValue (pre + "type")->load()) + 1, juce::dontSendNotification);
            active.setToggleState (proc.apvts.getRawParameterValue (pre + "on")->load() > 0.5f, juce::dontSendNotification);
            solo.setToggleState (proc.eqSolo.load() == selected, juce::dontSendNotification);
            const bool isUsed = proc.apvts.getRawParameterValue (pre + "used")->load() > 0.5f;
            for (auto* c : std::initializer_list<juce::Component*> { &type, &freq, &gain, &q, &active, &solo, &del })
                c->setEnabled (isUsed);
        }
    };

    //==================================================================================================================
    // Module card: power, title, hammer (pull down = more), caption, value, GR bar and a footer line.
    // Tuning range view for the Basic Tune card: detected note -> target note, sharp / flat and by how much, a
    // -50..+50 cent scale with the span being corrected shaded, and a centre mark that lights when the voice is in tune.
    class TuneRangeView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        explicit TuneRangeView (KaminariVocalProcessor& p) : proc (p)
        {
            setTitle ("Tuning");
            setTooltip ("Detected note and the note it is tuned to. The bar shows how far the voice is from that note "
                        "(left flat, right sharp) and, shaded, the part being corrected.");
            startTimerHz (30);
        }
        ~TuneRangeView() override { stopTimer(); }

        static juce::String name (int midi)
        {
            static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            return juce::String (n[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
        }

        // Read-outs (also used by the tests)
        bool hasPitch() const noexcept { return detected >= 0.0f; }
        int targetNote() const noexcept { return target; }
        float deviationCents() const noexcept { return deviation; }   // detected pitch relative to the target note
        float correctingCents() const noexcept { return correcting; }
        static constexpr float inTuneCents = 5.0f;

        void update (float detectedMidi, float correctionCents)
        {
            detected = detectedMidi;
            if (detected < 0.0f) { correcting = 0; repaint(); return; }
            target = juce::roundToInt (detected + correctionCents / 100.0f);
            deviation = (detected - (float) target) * 100.0f;
            correcting = correctionCents;
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            using namespace kvtheme;
            auto b = getLocalBounds().toFloat();
            auto text = b.removeFromTop (15.0f);
            const bool live = hasPitch() && isEnabled();
            g.setFont (font (12.0f, 1));
            if (! live)
            {
                g.setColour (steel);
                g.drawText ("-- no pitch", text, juce::Justification::centred);
            }
            else
            {
                const bool centred = std::abs (deviation) < inTuneCents;
                juce::String status = centred ? juce::String ("in tune")
                                              : (deviation > 0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x99\xaf +")) : juce::String (juce::CharPointer_UTF8 ("\xe2\x99\xad \xe2\x88\x92")))
                                                    + juce::String (juce::roundToInt (std::abs (deviation))) + " ct";
                const int detectedNote = juce::roundToInt (detected);
                const juce::String notes = name (detectedNote) + (detectedNote == target ? juce::String() : juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + name (target));
                g.setColour (white);
                g.drawText (notes, text, juce::Justification::centredLeft);
                g.setColour (centred ? accent : (std::abs (deviation) > 25.0f ? amber : mist));
                g.drawText (status, text, juce::Justification::centredRight);
            }
            // scale
            b.removeFromTop (5.0f);
            auto bar = b.removeFromTop (9.0f);
            g.setColour (navy950);
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (navy600);
            g.drawRoundedRectangle (bar.reduced (0.5f), 3.0f, 1.0f);
            auto xFor = [&] (float c) { return bar.getCentreX() + juce::jlimit (-50.0f, 50.0f, c) / 50.0f * (bar.getWidth() * 0.5f - 3.0f); };
            g.setColour (navy600);
            for (float c : { -25.0f, 25.0f }) g.drawVerticalLine ((int) xFor (c), bar.getY() + 2.0f, bar.getBottom() - 2.0f);
            if (live)
            {
                // the span being corrected: from where the voice is towards the centre
                const float x0 = xFor (deviation), x1 = xFor (deviation + correcting);
                g.setColour (accent.withAlpha (0.35f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (std::min (x0, x1), bar.getY() + 2.0f, std::max (x0, x1), bar.getBottom() - 2.0f));
            }
            const bool centred = live && std::abs (deviation) < inTuneCents;
            g.setColour (centred ? accent : mist.withAlpha (0.7f));
            g.fillRect (juce::Rectangle<float> (bar.getCentreX() - (centred ? 1.5f : 0.75f), bar.getY() - 2.0f, centred ? 3.0f : 1.5f, bar.getHeight() + 4.0f));
            if (live)
            {
                const float x = xFor (deviation);
                g.setColour (white);
                juce::Path tri;
                tri.addTriangle (x - 4.0f, bar.getY() - 4.0f, x + 4.0f, bar.getY() - 4.0f, x, bar.getY() + 2.0f);
                g.fillPath (tri);
                g.fillRect (juce::Rectangle<float> (x - 1.0f, bar.getY(), 2.0f, bar.getHeight()));
            }
        }

    private:
        void timerCallback() override { update (proc.tune.detectedMidi.load(), proc.tune.correctionCents.load()); }
        KaminariVocalProcessor& proc;
        float detected = -1.0f, deviation = 0, correcting = 0;
        int target = 0;
    };

    class ModuleCard : public juce::Component, private juce::Timer
    {
    public:
        ModuleCard (KaminariVocalProcessor& p, const juce::String& name, const char* onId, const char* hammerId, bool inverted,
                    const juce::String& captionText, int moduleIndex)
            : power (p.apvts, onId, name),
              hammer (*p.apvts.getParameter (hammerId), p.hostTempo, captionText, inverted),
              proc (p), title (name), caption (captionText), module (moduleIndex),
              hammerParam (*p.apvts.getParameter (hammerId))
        {
            setTitle (name);
            hammer.setCompact (true);
            hammer.setTitle (name + " " + captionText);
            addAndMakeVisible (power);
            addAndMakeVisible (hammer);
            addAndMakeVisible (open);
            open.getProperties().set ("kvStyle", "ghost");
            open.setButtonText (juce::String (juce::CharPointer_UTF8 ("Advanced \xe2\x80\xba")));
            open.setTooltip ("Open " + name + " in the Advanced view.");
            if (module == KaminariVocalProcessor::ModTune)
            {
                tuning = std::make_unique<TuneRangeView> (p);
                addAndMakeVisible (*tuning);
                for (auto* id : { "tn_key", "tn_scale" })
                {
                    auto* cb = tuneBoxes.add (new juce::ComboBox());
                    auto* prm = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id));
                    cb->addItemList (prm->choices, 1);
                    cb->setTitle (prm->getName (64));
                    cb->setTooltip (prm->getName (64));
                    tuneAtts.add (new APVTS::ComboBoxAttachment (p.apvts, id, *cb));
                    addAndMakeVisible (cb);
                }
            }
            startTimerHz (12);
        }
        ~ModuleCard() override { stopTimer(); }

        void paint (juce::Graphics& g) override
        {
            using namespace kvtheme;
            auto b = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (navy900);
            g.fillRoundedRectangle (b, 6.0f);
            g.setColour (navy600);
            g.drawRoundedRectangle (b, 6.0f, 1.0f);
            g.setColour (white);
            g.setFont (font (18.0f, 3, 0.1f));
            const int noteW = getWidth() >= 170 ? 66 : 40;
            g.drawFittedText (title.toUpperCase(), 46, 10, getWidth() - (module == KaminariVocalProcessor::ModTune ? 50 + noteW : 54), 28,
                              juce::Justification::centredLeft, 1, 0.55f);
            if (module == KaminariVocalProcessor::ModTune && noteText.isNotEmpty())
            {
                const auto r = juce::Rectangle<int> (getWidth() - noteW, 14, noteW - 8, 20);
                g.setColour (accent);
                g.fillEllipse ((float) r.getX(), r.getCentreY() - 3.5f, 7, 7);
                g.setColour (mist);
                g.setFont (font (12.0f, 0));
                g.drawText (getWidth() >= 170 ? noteText : noteText.upToFirstOccurrenceOf (" ", false, false), r.withTrimmedLeft (11), juce::Justification::centredLeft);
            }
            auto info = infoArea;
            if (module == KaminariVocalProcessor::ModTune)
            {
                // one line: "Retune Speed  18 ms" (the tuning view sits below it)
                juce::AttributedString line;
                line.append (caption + "  ", font (12.0f, 0), mist);
                line.append (valueText, font (14.0f, 1), white);
                line.setJustification (juce::Justification::centred);
                line.setWordWrap (juce::AttributedString::none);
                line.draw (g, info.toFloat());
            }
            else
            {
                g.setColour (mist);
                g.setFont (font (12.0f, 0));
                g.drawText (caption, info.removeFromTop (16), juce::Justification::centred);
                g.setColour (white);
                g.setFont (font (15.0f, 1));
                g.drawText (valueText, info.removeFromTop (20), juce::Justification::centred);
            }
            if (module != KaminariVocalProcessor::ModTune)
            {
                auto row = grArea;
                g.setColour (mist);
                g.setFont (font (12.0f, 0));
                const bool sat = module == KaminariVocalProcessor::ModDistortion, lfo = module == KaminariVocalProcessor::ModFlanger;
                g.drawText (sat ? "SAT" : (lfo ? "LFO" : "GR"), row.removeFromLeft (sat || lfo ? 30 : 24), juce::Justification::centredLeft);
                auto val = row.removeFromRight (52);
                g.setColour (white);
                g.drawText (grText, val, juce::Justification::centredRight);
                auto bar = row.reduced (4, 4).toFloat();
                g.setColour (navy800);
                g.fillRoundedRectangle (bar, 2.0f);
                if (lfo)
                {
                    // sweep position: a dot moving along the bar
                    const float x = bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * std::sin (kv::twoPi * gr));
                    g.setColour (accent);
                    g.fillEllipse (x - 4.0f, bar.getCentreY() - 4.0f, 8.0f, 8.0f);
                }
                else
                {
                    const float frac = juce::jlimit (0.0f, 1.0f, std::abs (gr) / (sat ? 24.0f : 12.0f));
                    g.setColour (sat ? amber : (gr >= 0 ? accent : amber));
                    const float fw = juce::jmax (frac > 0.001f ? 4.0f : 0.0f, bar.getWidth() * frac);
                    g.fillRoundedRectangle (sat ? bar.removeFromLeft (fw) : bar.removeFromRight (fw), 2.0f);
                }
                g.setColour (mist);
                g.drawFittedText (footer, footerArea, juce::Justification::centredLeft, 1, 0.8f);
            }
            else
            {
                g.setColour (mist);
                g.setFont (font (10.0f, 1, 0.1f));
                const char* labels[] = { "KEY", "SCALE" };
                for (int i = 0; i < tuneBoxes.size(); ++i)
                    g.drawText (labels[i], tuneBoxes[i]->getX(), tuneBoxes[i]->getY() - 13, 60, 12, juce::Justification::centredLeft);
            }
        }

        // Every card gives its hammer the same area: the controls below it take the same height on every card.
        static constexpr int controlsHeight = 112;

        void resized() override
        {
            auto b = getLocalBounds().reduced (10, 8);
            power.setBounds (b.getX(), b.getY() + 2, 28, 28);
            b.removeFromTop (36);
            const auto art = b.removeFromTop (juce::jmax (40, b.getHeight() - controlsHeight));
            hammer.setBounds (art.withSizeKeepingCentre (juce::jmin (art.getWidth(), 140), art.getHeight()));
            auto bottom = b.removeFromBottom (24);
            open.setBounds (bottom);
            b.removeFromBottom (4);
            if (module == KaminariVocalProcessor::ModTune)
            {
                // Retune Speed line, tuning view, KEY / SCALE
                auto row = b.removeFromBottom (24);
                b.removeFromBottom (14);
                const int w = (row.getWidth() - 4) / 2;
                for (auto* cb : tuneBoxes) { cb->setBounds (row.removeFromLeft (w).reduced (1, 0)); row.removeFromLeft (4); }
                tuning->setBounds (b.removeFromBottom (28).reduced (2, 0));
                infoArea = b.removeFromBottom (18);
                return;
            }
            footerArea = b.removeFromBottom (18);
            b.removeFromBottom (2);
            grArea = b.removeFromBottom (20);
            b.removeFromBottom (4);
            infoArea = b.removeFromBottom (38);
        }

        PowerButton power;
        LightningSlider hammer;
        juce::TextButton open;

    private:
        void timerCallback() override
        {
            auto& a = proc.apvts;
            auto v = [&] (const char* id) { return a.getRawParameterValue (id)->load(); };
            juce::String val = hammerParam.getCurrentValueAsText(), foot;
            switch (module)
            {
                case KaminariVocalProcessor::ModTune:
                {
                    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                    const float midi = proc.tune.detectedMidi.load();
                    const int n = juce::roundToInt (midi);
                    noteText = midi < 0 ? juce::String ("--") : juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1)
                                   + " " + (midi - n >= 0 ? "+" : "") + juce::String (juce::roundToInt ((midi - n) * 100)) + " ct";
                    break;
                }
                case KaminariVocalProcessor::ModMultiband:
                {
                    const int count = juce::roundToInt (v ("mb_count"));
                    foot = juce::String (count) + (count == 1 ? " band" : " bands") + dot()
                         + kvp::freqText (v ("mb1_lo")) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) + kvp::freqText (v ("mb1_hi"));
                    break;
                }
                case KaminariVocalProcessor::ModCompression:
                {
                    const float t = v ("lv_thresh");
                    val = juce::String (juce::roundToInt (-t / 50.0f * 100.0f)) + " % (" + minusSign (t, 0) + " dB)";
                    foot = v ("lv_auto_gain") > 0.5f ? "Auto makeup +" + juce::String (proc.compMakeup.load(), 1) + " dB" : juce::String ("Auto makeup off");
                    break;
                }
                case KaminariVocalProcessor::ModDeEss:
                {
                    const float t = v ("ds_thresh");
                    val = juce::String (juce::roundToInt (-t / 60.0f * 100.0f)) + " % (" + minusSign (t, 0) + " dB)";
                    foot = "Above " + kvp::freqText (v ("ds_det_lo")) + dot() + (v ("ds_process") < 0.5f ? "split band" : "wide band");
                    break;
                }
                case KaminariVocalProcessor::ModDistortion:
                {
                    static const char* styles[] = { "Tape", "Tube", "Warm", "Fuzz", "Clip", "Lo-Fi" };
                    foot = juce::String (styles[juce::jlimit (0, 5, juce::roundToInt (v ("dt_style")))]) + dot() + "mix "
                         + juce::String (juce::roundToInt (v ("dt_mix"))) + " %";
                    break;
                }
                case KaminariVocalProcessor::ModFlanger:
                {
                    const int sync = juce::roundToInt (v (kvid::flSync));
                    foot = (sync == 0 ? proc.apvts.getParameter (kvid::flRate)->getCurrentValueAsText() : kvp::flangerSyncNames()[sync])
                         + dot() + "fb " + juce::String (juce::roundToInt (v (kvid::flFeedback))) + " %";
                    break;
                }
                case KaminariVocalProcessor::ModResonance:
                {
                    static const char* q[] = { "Normal", "High", "Ultra" };
                    foot = juce::String ("Low latency") + dot() + "0 smp" + dot() + q[juce::jlimit (0, 2, juce::roundToInt (v ("rs_quality")))];
                    break;
                }
                default: break;
            }
            gr = proc.moduleGr[(size_t) module].load();
            const auto g = module == KaminariVocalProcessor::ModFlanger
                               ? (proc.apvts.getRawParameterValue (kvid::flOn)->load() > 0.5f ? juce::String ("sweeping") : juce::String ("off"))
                           : module == KaminariVocalProcessor::ModDistortion
                               ? juce::String (gr, 1) + " dB"
                               : (gr > 0.05f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : (gr < -0.05f ? "+" : ""))
                                     + juce::String (std::abs (gr), 1) + " dB";
            if (val != valueText || foot != footer || g != grText) { valueText = val; footer = foot; grText = g; repaint(); }
            else if (module == KaminariVocalProcessor::ModTune) repaint (getWidth() - 80, 10, 80, 30);
            else if (module == KaminariVocalProcessor::ModFlanger) repaint (grArea);
        }

        KaminariVocalProcessor& proc;
        juce::String title, caption, valueText, footer, grText, noteText;
        int module;
        juce::RangedAudioParameter& hammerParam;
        juce::Rectangle<int> infoArea, grArea, footerArea;
        float gr = 0;
        juce::OwnedArray<juce::ComboBox> tuneBoxes;
        juce::OwnedArray<APVTS::ComboBoxAttachment> tuneAtts;
        std::unique_ptr<TuneRangeView> tuning;

    public:
        TuneRangeView* tuningView() { return tuning.get(); }
    };

    //==================================================================================================================
    // Send card for the Basic view's send row.
    class SendCard : public juce::Component, private juce::Timer
    {
    public:
        SendCard (KaminariVocalProcessor& p, int send, const char* onId, const char* levelId, const char* modeId, const char* tapId,
                  const juce::String& name, std::function<juce::String (int)> modeTextFn, juce::LookAndFeel& lnf)
            : power (p.apvts, onId, "ON", "OFF", "Switches the " + name.toLowerCase() + " send and its return on or off."),
              level (p.apvts, levelId, {}, "Level sent to the " + name.toLowerCase() + ". The return is 100 % wet; the dry vocal is unchanged."),
              proc (p), title (name), sendIndex (send),
              modeParam (*p.apvts.getParameter (modeId)), tapParam (*p.apvts.getParameter (tapId)), modeText (std::move (modeTextFn))
        {
            setTitle (name + " send");
            level.setLNF (&lnf);
            level.label.setVisible (false);
            level.value.setVisible (false);
            open.setButtonText (juce::String (juce::CharPointer_UTF8 ("ADV \xe2\x80\xba")));
            open.setTooltip ("Open the " + name.toLowerCase() + " send's Advanced controls.");
            for (auto* c : std::initializer_list<juce::Component*> { &power, &level, &open })
                addAndMakeVisible (c);
            startTimerHz (20);
        }
        ~SendCard() override { stopTimer(); level.setLNF (nullptr); }

        void paint (juce::Graphics& g) override
        {
            using namespace kvtheme;
            auto b = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (navy900);
            g.fillRoundedRectangle (b, 6.0f);
            g.setColour (navy600);
            g.drawRoundedRectangle (b, 6.0f, 1.0f);
            g.setColour (white);
            g.setFont (font (compact ? 14.5f : 16.0f, 3, 0.1f));
            g.drawText (title.toUpperCase(), titleArea, juce::Justification::centredLeft);
            auto t = textArea;
            if (compact)
            {
                // one line: level, then mode and tap point; meter underneath
                auto line = t.removeFromTop (16);
                g.setFont (font (13.0f, 1));
                const int lw = (int) juce::GlyphArrangement::getStringWidth (font (13.0f, 1), levelText) + 6;
                g.drawText (levelText, line.removeFromLeft (lw), juce::Justification::centredLeft);
                g.setColour (mist);
                g.setFont (font (11.0f, 0));
                g.drawText (detail, line, juce::Justification::centredLeft, true);
                t.removeFromTop (3);
            }
            else
            {
                g.setFont (font (14.0f, 1));
                g.drawText (levelText, t.removeFromTop (18), juce::Justification::centredLeft);
                g.setColour (mist);
                g.setFont (font (11.5f, 0));
                g.drawText (detail, t.removeFromTop (16), juce::Justification::centredLeft, true);
            }
            auto bar = t.removeFromTop (compact ? 8 : 10).reduced (0, compact ? 1 : 2).toFloat();
            g.setColour (navy950);
            g.fillRoundedRectangle (bar, 2.0f);
            g.setColour (navy600);
            g.drawRoundedRectangle (bar, 2.0f, 1.0f);
            const float db = juce::Decibels::gainToDecibels (meter, -60.0f);
            g.setColour (accent);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f)), 2.0f);
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (10, 6);
            compact = getWidth() < 260;
            if (compact)
            {
                // narrow card: knob on the left; title, ON and ADV on top; level, detail and meter below
                level.setBounds (b.removeFromLeft (46).withSizeKeepingCentre (44, 44));
                b.removeFromLeft (6);
                auto top = b.removeFromTop (22);
                open.setBounds (top.removeFromRight (40).withSizeKeepingCentre (40, 20));
                top.removeFromRight (4);
                power.setBounds (top.removeFromRight (38).withSizeKeepingCentre (38, 20));
                titleArea = top;
                textArea = b.withTrimmedTop (2).withHeight (b.getHeight());
                return;
            }
            titleArea = { 12, 6, 90, 22 };
            auto left = b.removeFromLeft (84);
            power.setBounds (left.withTrimmedTop (28).withHeight (24).withWidth (52));
            level.setBounds (b.removeFromLeft (52).withSizeKeepingCentre (48, 48));
            b.removeFromLeft (8);
            open.setBounds (b.removeFromRight (54).withSizeKeepingCentre (54, 26));
            b.removeFromRight (8);
            textArea = b.withSizeKeepingCentre (b.getWidth(), 46);
        }

        ToggleBox power;
        Knob level;
        juce::TextButton open;

    private:
        void timerCallback() override
        {
            const auto lv = proc.apvts.getParameter (sendIndex == 0 ? kvid::rvSend : sendIndex == 1 ? kvid::dlSend : kvid::wdSend)->getCurrentValueAsText();
            const auto d = modeText (juce::roundToInt (modeParam.convertFrom0to1 (modeParam.getValue()))) + dot()
                         + (tapParam.getValue() > 0.5f ? "pre-fader" : "post-fader");
            const float m = proc.returnPeak[(size_t) sendIndex].load();
            meter = m > meter ? m : meter * 0.8f;
            levelText = lv; detail = d;
            repaint();
        }

        KaminariVocalProcessor& proc;
        juce::String title, levelText, detail;
        int sendIndex;
        juce::RangedAudioParameter& modeParam;
        juce::RangedAudioParameter& tapParam;
        std::function<juce::String (int)> modeText;
        juce::Rectangle<int> textArea, titleArea;
        float meter = 0;
        bool compact = false;
    };
}
