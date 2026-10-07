#pragma once

#include "SendPanels.h"

// Builds the right control for a parameter from its type: float/int -> knob, choice -> combo box,
// bool -> toggle button. Each control gets the parameter's name and a one-sentence tooltip.
namespace kvui
{
    inline juce::String hintFor (const juce::String& id)
    {
        static const std::pair<const char*, const char*> hints[] = {
            { "tn_speed", "How fast notes are pulled to pitch. 0 ms = instant, hard-tuned sound." },
            { "tn_humanize", "Slows correction on held notes so they keep natural movement." },
            { "tn_key", "Key of the song." }, { "tn_scale", "Notes the vocal is pulled to. Editing the note keys switches to Custom." },
            { "tn_range", "Expected pitch range of the singer; narrows detection for reliability." },
            { "_freq", "Frequency of the band." }, { "_gain", "Boost or cut." }, { "_q", "Width: higher is narrower." },
            { "_slope", "Steepness of a cut filter in dB per octave." }, { "_type", "Filter shape." },
            { "_used", "Adds or removes the band." }, { "eq_out_gain", "Level after the EQ." },
            { "mb_count", "Number of bands (1 to 6)." }, { "mb_slope", "Steepness of the band filters." },
            { "_lo", "Lower edge of the band." }, { "_hi", "Upper edge of the band." },
            { "_thresh", "Level where processing starts." }, { "_ratio", "How strongly level beyond the threshold is changed." },
            { "_attack", "How fast processing reacts." }, { "_release", "How fast it recovers." },
            { "_knee", "How gradually processing starts around the threshold." },
            { "mb1_range", "Maximum gain change. Negative = downward, positive = upward." },
            { "_range", "Maximum gain change in dB." }, { "_mode", "Compress or expand." }, { "_solo", "Listen to this band only." },
            { "lv_style", "Clean: transparent. Vocal: automatic ratio. Opto: slow and soft. Classic: feedback. Punch: lets transients through." },
            { "lv_auto_release", "Program-dependent release: short peaks recover fast, sustained reduction slowly." },
            { "lv_auto_gain", "Automatic makeup: half the static reduction at 0 dBFS, at most 12 dB." },
            { "lv_hold", "Time the reduction is held before release." }, { "lv_lookahead", "Reacts before peaks arrive. Adds latency." },
            { "lv_mix", "Parallel compression. Above 100 % pushes past the compressed sound." },
            { "lv_dry", "Adds the uncompressed signal at this level." }, { "lv_sc_level", "Gain into the detector only." },
            { "lv_wet_gain", "Level of the compressed signal before Mix." }, { "_stereo_link", "How much both channels share one detector." },
            { "_out_gain", "Level after the module." }, { "lv_detector", "Peak follows every peak; Smooth reacts to average level." },
            { "ds_det_lo", "Lower edge of the sibilance detection range. Split band also starts here." },
            { "ds_det_hi", "Upper edge of the sibilance detection range." },
            { "ds_detect", "Voice Focus: band-pass detector. Full Band: everything above the low edge." },
            { "ds_process", "Split Band reduces only the highs; Wideband turns the whole vocal down." },
            { "ds_link_mode", "Process stereo, mid only or side only." }, { "ds_listen", "Hear the detector signal." },
            { "ds_audition_trigger", "Hear only what is removed." }, { "ds_lookahead", "Catches the start of esses. Adds latency." },
            { "rs_mode", "Soft: adaptive threshold. Hard: level-dependent." }, { "rs_depth", "Overall amount of resonance reduction." },
            { "rs_detail", "High: narrow, specific cuts. Low: broad build-ups." }, { "rs_mix", "Blend with the unprocessed signal." },
            { "rs_delta", "Hear only what is removed." }, { "rs_bypass", "Bypass inside the module (keeps processing for glitch-free A/B)." },
            { "rs_quality", "Band density: Normal 1/3, High 1/4, Ultra 1/6 octave." },
            { "rs_stereo_mode", "Process left/right or mid/side." }, { "rs_link", "How much the channels share detection." },
            { "rs_focus", "Balance of processing between the two channels." }, { "rs_max_cut", "Limit on the deepest cut." },
            { "rs_wet_trim", "Level of the processed signal before Mix." }, { "_tilt_lo", "Adjusts the setting below about 500 Hz." },
            { "_tilt_hi", "Adjusts the setting above about 2 kHz." }, { "_depth", "Raises (more suppression) or lowers the depth curve here." },
            { "_shape", "Shape of the depth-curve band." }, { "_on", "Switches the module or band on or off." },
        };
        for (auto& h : hints)
            if (id == h.first) return h.second;
        for (auto& h : hints)
            if (h.first[0] == '_' && id.endsWith (h.first)) return h.second;
        return {};
    }

    // Owns a set of auto-built controls keyed by parameter ID.
    class ControlSet
    {
    public:
        ControlSet (APVTS& s, juce::LookAndFeel& l) : state (s), lnf (l) {}
        ~ControlSet()
        {
            for (auto& k : knobs) k->setLNF (nullptr);
        }

        juce::Component* add (juce::Component& parent, const juce::String& id, const juce::String& caption = {})
        {
            auto* p = state.getParameter (id);
            jassert (p != nullptr);
            const auto name = caption.isNotEmpty() ? caption : shortName (p->getName (64));
            juce::Component* c = nullptr;
            if (dynamic_cast<juce::AudioParameterChoice*> (p) != nullptr)
            {
                choices.push_back (std::make_unique<ChoiceBox> (state, id.toRawUTF8(), name, hintFor (id)));
                c = choices.back().get();
            }
            else if (dynamic_cast<juce::AudioParameterBool*> (p) != nullptr)
            {
                toggles.push_back (std::make_unique<ToggleBox> (state, id.toRawUTF8(), name.toUpperCase() + " ON", name.toUpperCase() + " OFF", hintFor (id)));
                c = toggles.back().get();
            }
            else
            {
                knobs.push_back (std::make_unique<Knob> (state, id, name, hintFor (id)));
                knobs.back()->setLNF (&lnf);
                c = knobs.back().get();
            }
            parent.addAndMakeVisible (c);
            byId[id] = c;
            return c;
        }

        std::vector<juce::Component*> addAll (juce::Component& parent, const juce::StringArray& ids)
        {
            std::vector<juce::Component*> v;
            for (auto& id : ids) v.push_back (add (parent, id));
            return v;
        }

        juce::Component* get (const juce::String& id) const
        {
            auto it = byId.find (id);
            return it != byId.end() ? it->second : nullptr;
        }

        // "EQ 3 Freq" -> "Freq", "Compression Attack" -> "Attack"
        static juce::String shortName (const juce::String& full)
        {
            static const char* prefixes[] = { "Compression ", "De-Ess ", "Resonance Band ", "Resonance ", "Multiband ", "EQ ", "Band ", "Tune " };
            auto n = full;
            for (auto* pre : prefixes)
                if (n.startsWith (pre)) { n = n.substring ((int) strlen (pre)); break; }
            if (n.isNotEmpty() && juce::CharacterFunctions::isDigit (n[0]))
                n = n.fromFirstOccurrenceOf (" ", false, false);
            return n;
        }

    private:
        APVTS& state;
        juce::LookAndFeel& lnf;
        std::vector<std::unique_ptr<Knob>> knobs;
        std::vector<std::unique_ptr<ChoiceBox>> choices;
        std::vector<std::unique_ptr<ToggleBox>> toggles;
        std::map<juce::String, juce::Component*> byId;
    };

    // Lays out controls: knobs 78 x 96, choice boxes 120 x 44, toggles 110 x 28 (vertically centred).
    inline void flowControls (juce::Rectangle<int> area, const std::vector<juce::Component*>& items, int gap = 6)
    {
        int x = area.getX(), y = area.getY();
        const int rowH = 96;
        for (auto* c : items)
        {
            if (c == nullptr || ! c->isVisible()) continue;
            int w = 78, h = rowH, dy = 0;
            if (dynamic_cast<ChoiceBox*> (c) != nullptr) { w = 130; h = 44; dy = 20; }
            else if (dynamic_cast<ToggleBox*> (c) != nullptr) { w = 120; h = 28; dy = 34; }
            if (x + w > area.getRight() && x > area.getX()) { x = area.getX(); y += rowH + gap; }
            c->setBounds (x, y + dy, w, h);
            x += w + gap;
        }
    }
}
