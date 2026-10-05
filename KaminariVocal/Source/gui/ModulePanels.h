#pragma once

#include "AutoControls.h"
#include "EqCurve.h"

namespace kvui
{
    // Gain-reduction read-out: horizontal bar plus value (positive = reduction, negative = boost).
    class GrMeter : public juce::Component, private juce::Timer
    {
    public:
        GrMeter (std::atomic<float>& source, juce::String label) : src (source), text (std::move (label))
        {
            setTitle (text + " gain reduction");
            startTimerHz (30);
        }
        ~GrMeter() override { stopTimer(); }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (colours::mist);
            g.setFont (uiFont (11.0f, true));
            g.drawText (text, b.removeFromLeft (28), juce::Justification::centredLeft);
            auto val = b.removeFromRight (64);
            g.drawText ((value > 0.05f ? "-" : (value < -0.05f ? "+" : "")) + juce::String (std::abs (value), 1) + " dB", val, juce::Justification::centredRight);
            auto bar = b.reduced (4, (int) (b.getHeight() * 0.3f));
            g.setColour (colours::navy950);
            g.fillRoundedRectangle (bar, 2.0f);
            const float frac = juce::jlimit (0.0f, 1.0f, std::abs (value) / 24.0f);
            g.setColour (value >= 0 ? colours::bolt : colours::amber);
            g.fillRoundedRectangle (bar.removeFromRight (bar.getWidth() * frac), 2.0f);
        }

    private:
        void timerCallback() override
        {
            const float t = src.load();
            const float next = std::abs (t) > std::abs (value) ? t : value + (t - value) * 0.25f;
            if (std::abs (next - value) > 0.01f) { value = next; repaint(); }
        }
        std::atomic<float>& src;
        juce::String text;
        float value = 0.0f;
    };

    // Common frame of an Advanced module panel: On switch, title, module preset bar, content below.
    class ModulePanel : public juce::Component
    {
    public:
        ModulePanel (KaminariVocalProcessor& p, const juce::String& title, const juce::String& subtitle, const char* onId, const juce::String& moduleKey)
            : proc (p), controls (p.apvts, lnf), power (p.apvts, onId, title.toUpperCase() + " ON", title.toUpperCase() + " OFF",
                                                        "Switches " + title + " on or off."),
              preset (p.presets, moduleKey, title.toLowerCase()), titleText (title), subText (subtitle)
        {
            addAndMakeVisible (power);
            addAndMakeVisible (preset);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (colours::white);
            g.setFont (uiFont (18.0f, true));
            g.drawText (titleText.toUpperCase(), 140, 12, 200, 26, juce::Justification::centredLeft);
            g.setColour (colours::mist);
            g.setFont (uiFont (12.0f));
            g.drawText (subText, 140 + 8 + (int) juce::GlyphArrangement::getStringWidth (uiFont (18.0f, true), titleText.toUpperCase()), 12, 360, 26,
                        juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (12);
            auto top = b.removeFromTop (30);
            power.setBounds (top.removeFromLeft (120));
            preset.setBounds (top.removeFromRight (330));
            b.removeFromTop (10);
            layoutContent (b);
        }

        virtual void layoutContent (juce::Rectangle<int> area) = 0;

        KaminariVocalProcessor& proc;
        ModuleLNF lnf { palette() };
        ControlSet controls;
        ToggleBox power;
        PresetBar preset;
        juce::String titleText, subText;
    };

    // Row of numbered buttons that picks which band's controls are shown.
    class BandSelector : public juce::Component
    {
    public:
        BandSelector (int count, const juce::String& what)
        {
            for (int i = 0; i < count; ++i)
            {
                auto* b = buttons.add (new juce::TextButton (juce::String (i + 1)));
                b->setRadioGroupId (77);
                b->setClickingTogglesState (true);
                b->setTooltip ("Show " + what + " " + juce::String (i + 1));
                b->setTitle (what + " " + juce::String (i + 1));
                b->onClick = [this, i] { if (buttons[i]->getToggleState() && onChange) onChange (i); };
                addAndMakeVisible (b);
            }
            buttons[0]->setToggleState (true, juce::dontSendNotification);
        }
        void select (int i) { buttons[i]->setToggleState (true, juce::sendNotificationSync); }
        void resized() override
        {
            auto b = getLocalBounds();
            for (auto* btn : buttons) { btn->setBounds (b.removeFromLeft (34)); b.removeFromLeft (4); }
        }
        std::function<void (int)> onChange;
        juce::OwnedArray<juce::TextButton> buttons;
    };

    //==================================================================================================================
    class TunePanel : public ModulePanel
    {
    public:
        explicit TunePanel (KaminariVocalProcessor& p)
            : ModulePanel (p, "Tune", "Real-time pitch correction - 96 smp", "tn_on", "tune"),
              scaleAttachment (*p.apvts.getParameter ("tn_scale"), [this] (float) { scaleOrKeyChanged(); }),
              keyAttachment (*p.apvts.getParameter ("tn_key"), [this] (float) { scaleOrKeyChanged(); })
        {
            main = controls.addAll (*this, { "tn_key", "tn_scale", "tn_range", "tn_speed", "tn_humanize" });
            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            for (int n = 0; n < 12; ++n)
            {
                auto* b = notes.add (new juce::TextButton (names[n]));
                b->setClickingTogglesState (true);
                b->setTooltip (juce::String (names[n]) + ": click to include or exclude. Editing a note switches Scale to Custom.");
                b->setTitle ("Note " + juce::String (names[n]));
                noteAttachments.add (new APVTS::ButtonAttachment (p.apvts, "tn_note_" + juce::String (n), *b));
                b->onClick = [this] { setChoice ("tn_scale", 10); };   // Custom
                addAndMakeVisible (b);
            }
            readout.setJustificationType (juce::Justification::centred);
            readout.setFont (uiFont (34.0f, true));
            readout.setColour (juce::Label::textColourId, colours::white);
            readout.setTitle ("Detected pitch");
            addAndMakeVisible (readout);
            detail.setJustificationType (juce::Justification::centred);
            styleText (detail, 12.0f, colours::mist);
            detail.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (detail);
            timer.callback = [this] { refreshReadout(); };
            timer.startTimerHz (15);
        }

        void layoutContent (juce::Rectangle<int> area) override
        {
            flowControls (area.removeFromTop (100), { main[0], main[1], main[2] });
            auto mid = area.removeFromTop (150);
            main[3]->setBounds (mid.removeFromLeft (110).withSizeKeepingCentre (100, 110));
            main[4]->setBounds (mid.removeFromRight (110).withSizeKeepingCentre (100, 110));
            readout.setBounds (mid.removeFromTop (90));
            detail.setBounds (mid.removeFromTop (24));
            area.removeFromTop (10);
            auto keys = area.removeFromTop (40);
            const int w = keys.getWidth() / 12;
            for (auto* b : notes) b->setBounds (keys.removeFromLeft (w).reduced (2, 0));
        }

        void refreshReadout()
        {
            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            const float midi = proc.tune.detectedMidi.load();
            if (midi < 0) { readout.setText ("--", juce::dontSendNotification); detail.setText ("no pitch detected", juce::dontSendNotification); return; }
            const int n = juce::roundToInt (midi);
            const float cents = (midi - (float) n) * 100.0f;
            readout.setText (juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1), juce::dontSendNotification);
            detail.setText ((cents >= 0 ? "+" : "") + juce::String (juce::roundToInt (cents)) + " cents  -  correcting "
                            + juce::String (juce::roundToInt (proc.tune.correctionCents.load())) + " cents", juce::dontSendNotification);
        }

        juce::OwnedArray<juce::TextButton> notes;
        juce::Label readout, detail;

    private:
        struct Callback : juce::Timer { std::function<void()> callback; void timerCallback() override { callback(); } } timer;

        void setChoice (const char* id, int index)
        {
            auto* p = proc.apvts.getParameter (id);
            const float v = p->convertTo0to1 ((float) index);
            if (std::abs (p->getValue() - v) > 1e-6f) { p->beginChangeGesture(); p->setValueNotifyingHost (v); p->endChangeGesture(); }
        }

        // A named scale writes the 12 note switches so they show what is being corrected to.
        void scaleOrKeyChanged()
        {
            auto* sp = proc.apvts.getParameter ("tn_scale");
            auto* kp = proc.apvts.getParameter ("tn_key");
            const int scale = juce::roundToInt (sp->convertFrom0to1 (sp->getValue()));
            if (scale >= 10) return;
            bool on[12];
            kv::scaleNotes (juce::roundToInt (kp->convertFrom0to1 (kp->getValue())), scale, on);
            for (int n = 0; n < 12; ++n)
            {
                auto* p = proc.apvts.getParameter ("tn_note_" + juce::String (n));
                if ((p->getValue() > 0.5f) != on[n]) { p->beginChangeGesture(); p->setValueNotifyingHost (on[n] ? 1.0f : 0.0f); p->endChangeGesture(); }
            }
        }

        std::vector<juce::Component*> main;
        juce::OwnedArray<APVTS::ButtonAttachment> noteAttachments;
        juce::ParameterAttachment scaleAttachment, keyAttachment;
    };

    //==================================================================================================================
    class EqPanel : public ModulePanel
    {
    public:
        explicit EqPanel (KaminariVocalProcessor& p)
            : ModulePanel (p, "EQ", "8 bands - zero latency", "eq_on", "eq"), curve (p), selector (8, "band")
        {
            addAndMakeVisible (curve);
            addAndMakeVisible (selector);
            for (int b = 1; b <= 8; ++b)
            {
                const juce::String pre = "eq" + juce::String (b) + "_";
                bands.push_back (controls.addAll (*this, { pre + "used", pre + "on", pre + "type", pre + "freq", pre + "gain", pre + "q", pre + "slope" }));
            }
            out = controls.add (*this, "eq_out_gain", "Output");
            selector.onChange = [this] (int i) { show (i); };
            curve.onSelect = [this] (int i) { selector.select (i); };
            show (0);
        }

        void show (int band)
        {
            current = band;
            curve.selected = band;
            for (size_t i = 0; i < bands.size(); ++i)
                for (auto* c : bands[i]) c->setVisible ((int) i == band);
            resized();
        }

        void layoutContent (juce::Rectangle<int> area) override
        {
            curve.setBounds (area.removeFromTop (juce::jmax (160, area.getHeight() - 150)));
            area.removeFromTop (8);
            selector.setBounds (area.removeFromTop (26).removeFromLeft (8 * 38));
            area.removeFromTop (6);
            auto items = bands[(size_t) current];
            items.push_back (out);
            flowControls (area, items);
        }

        EqCurve curve;
        BandSelector selector;
        std::vector<std::vector<juce::Component*>> bands;
        juce::Component* out = nullptr;
        int current = 0;
    };

    //==================================================================================================================
    class MultibandPanel : public ModulePanel
    {
    public:
        explicit MultibandPanel (KaminariVocalProcessor& p)
            : ModulePanel (p, "Multiband", "Up to 6 bands - default one low-mid band", "mb_on", "multiband"),
              selector (6, "band"), gr (p.moduleGr[KaminariVocalProcessor::ModMultiband], "GR")
        {
            global = controls.addAll (*this, { "mb_count", "mb_slope", "mb_detector" });
            for (int b = 1; b <= 6; ++b)
            {
                const juce::String pre = "mb" + juce::String (b) + "_";
                bands.push_back (controls.addAll (*this, { pre + "mode", pre + "solo", pre + "lo", pre + "hi", pre + "thresh", pre + "ratio",
                                                           pre + "range", pre + "attack", pre + "release", pre + "knee", pre + "gain" }));
            }
            addAndMakeVisible (selector);
            addAndMakeVisible (gr);
            selector.onChange = [this] (int i) { show (i); };
            show (0);
        }

        void show (int band)
        {
            current = band;
            for (size_t i = 0; i < bands.size(); ++i)
                for (auto* c : bands[i]) c->setVisible ((int) i == band);
            resized();
        }

        void layoutContent (juce::Rectangle<int> area) override
        {
            auto top = area.removeFromTop (100);
            gr.setBounds (top.removeFromRight (260).withSizeKeepingCentre (260, 24));
            flowControls (top, global);
            selector.setBounds (area.removeFromTop (26).removeFromLeft (6 * 38));
            area.removeFromTop (8);
            flowControls (area, bands[(size_t) current]);
        }

        std::vector<juce::Component*> global;
        std::vector<std::vector<juce::Component*>> bands;
        BandSelector selector;
        GrMeter gr;
        int current = 0;
    };

    //==================================================================================================================
    class CompressionPanel : public ModulePanel
    {
    public:
        explicit CompressionPanel (KaminariVocalProcessor& p)
            : ModulePanel (p, "Compression", "Clean feed-forward compressor - parallel mix", "lv_on", "compression"),
              gr (p.moduleGr[KaminariVocalProcessor::ModCompression], "GR")
        {
            items = controls.addAll (*this, { "lv_style", "lv_detector", "lv_auto_release", "lv_auto_gain",
                                              "lv_thresh", "lv_ratio", "lv_attack", "lv_release", "lv_knee", "lv_range", "lv_hold",
                                              "lv_lookahead", "lv_mix", "lv_dry", "lv_wet_gain", "lv_sc_level", "lv_stereo_link", "lv_out_gain" });
            addAndMakeVisible (gr);
            styleText (makeup, 12.0f, colours::mist);
            addAndMakeVisible (makeup);
            timer.callback = [this] { makeup.setText ("Auto makeup +" + juce::String (proc.compMakeup.load(), 1) + " dB", juce::dontSendNotification); };
            timer.startTimerHz (8);
        }

        void layoutContent (juce::Rectangle<int> area) override
        {
            auto top = area.removeFromTop (30);
            gr.setBounds (top.removeFromLeft (300));
            makeup.setBounds (top.withTrimmedLeft (20));
            area.removeFromTop (6);
            flowControls (area, items);
        }

        std::vector<juce::Component*> items;
        GrMeter gr;
        juce::Label makeup;

    private:
        struct Callback : juce::Timer { std::function<void()> callback; void timerCallback() override { callback(); } } timer;
    };

    //==================================================================================================================
    class DeEssPanel : public ModulePanel
    {
    public:
        explicit DeEssPanel (KaminariVocalProcessor& p)
            : ModulePanel (p, "De-ess", "Split-band sibilance control", "ds_on", "deess"),
              gr (p.moduleGr[KaminariVocalProcessor::ModDeEss], "GR")
        {
            items = controls.addAll (*this, { "ds_mode", "ds_detect", "ds_process", "ds_link_mode",
                                              "ds_thresh", "ds_range", "ds_det_lo", "ds_det_hi", "ds_lookahead", "ds_stereo_link",
                                              "ds_listen", "ds_audition_trigger" });
            addAndMakeVisible (gr);
        }

        void layoutContent (juce::Rectangle<int> area) override
        {
            gr.setBounds (area.removeFromTop (30).removeFromLeft (300));
            area.removeFromTop (6);
            flowControls (area, items);
        }

        std::vector<juce::Component*> items;
        GrMeter gr;
    };

    //==================================================================================================================
    // Per-band reduction of the resonance suppressor, drawn as a curve hanging from 0 dB.
    class ReductionGraph : public juce::Component, private juce::Timer
    {
    public:
        explicit ReductionGraph (kv::Resonance& r) : res (r) { setTitle ("Resonance reduction graph"); startTimerHz (25); }
        ~ReductionGraph() override { stopTimer(); }
        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (colours::navy900);
            g.fillRoundedRectangle (b, 4.0f);
            g.setColour (colours::navy600);
            g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);
            const int n = res.numBands();
            if (n == 0) return;
            juce::Path p;
            p.startNewSubPath (0, 2);
            for (int k = 0; k < n; ++k)
            {
                const float x = (float) (std::log (res.bandFrequency (k) / 20.0) / std::log (1000.0)) * b.getWidth();
                p.lineTo (x, 2 + juce::jmin (1.0f, res.bandReduction (k) / 24.0f) * (b.getHeight() - 4));
            }
            p.lineTo (b.getWidth(), 2);
            p.closeSubPath();
            g.setColour (colours::bolt.withAlpha (0.35f));
            g.fillPath (p);
            g.setColour (colours::bolt);
            g.strokePath (p, juce::PathStrokeType (1.5f));
            g.setColour (colours::mist);
            g.setFont (uiFont (10.0f));
            g.drawText ("reduction (0 to 24 dB)", b.reduced (6, 4), juce::Justification::bottomLeft);
        }
    private:
        void timerCallback() override { repaint(); }
        kv::Resonance& res;
    };

    class ResonancePanel : public ModulePanel
    {
    public:
        explicit ResonancePanel (KaminariVocalProcessor& p)
            : ModulePanel (p, "Resonance", "Resonance suppressor - low latency (0 smp)", "rs_on", "resonance"),
              graph (p.resonance), selector (8, "depth-curve band"), gr (p.moduleGr[KaminariVocalProcessor::ModResonance], "GR")
        {
            main = controls.addAll (*this, { "rs_mode", "rs_quality", "rs_stereo_mode", "rs_delta", "rs_bypass",
                                             "rs_depth", "rs_detail", "rs_attack", "rs_release", "rs_mix", "rs_wet_trim", "rs_out_gain",
                                             "rs_max_cut", "rs_link", "rs_focus",
                                             "rs_detail_tilt_lo", "rs_detail_tilt_hi", "rs_attack_tilt_lo", "rs_attack_tilt_hi",
                                             "rs_release_tilt_lo", "rs_release_tilt_hi" });
            for (int b = 1; b <= 8; ++b)
            {
                const juce::String pre = "rs_b" + juce::String (b) + "_";
                bands.push_back (controls.addAll (*this, { pre + "used", pre + "on", pre + "shape", pre + "freq", pre + "depth", pre + "q" }));
            }
            addAndMakeVisible (graph);
            addAndMakeVisible (selector);
            addAndMakeVisible (gr);
            selector.onChange = [this] (int i) { show (i); };
            show (0);
        }

        void show (int band)
        {
            current = band;
            for (size_t i = 0; i < bands.size(); ++i)
                for (auto* c : bands[i]) c->setVisible ((int) i == band);
            resized();
        }

        void layoutContent (juce::Rectangle<int> area) override
        {
            auto top = area.removeFromTop (90);
            gr.setBounds (top.removeFromBottom (24).removeFromLeft (300));
            graph.setBounds (top.reduced (0, 2));
            area.removeFromTop (6);
            auto bandArea = area.removeFromBottom (136);
            flowControls (area, main, 4);
            selector.setBounds (bandArea.removeFromTop (26).removeFromLeft (8 * 38));
            bandArea.removeFromTop (6);
            flowControls (bandArea, bands[(size_t) current]);
        }

        ReductionGraph graph;
        std::vector<juce::Component*> main;
        std::vector<std::vector<juce::Component*>> bands;
        BandSelector selector;
        GrMeter gr;
        int current = 0;
    };
}
