#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/Eq.h"

// Permanent parameter IDs (see docs/KaminariVocal/DESIGN.md, section 2.9). Never rename or reuse an ID.
namespace kvid
{
    // global
    inline constexpr const char* inGain  = "gl_in_gain";
    inline constexpr const char* outGain = "gl_out_gain";
}

namespace kvp
{
    inline juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return { lo, hi,
                 [] (float s, float e, float p) { return s * std::pow (e / s, p); },
                 [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
                 [] (float s, float e, float v) { return juce::jlimit (s, e, v); } };
    }

    inline juce::String freqText (float v)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    }

    inline float freqFromText (const juce::String& t)
    {
        const auto s = t.trim().toLowerCase();
        const float v = s.getFloatValue();
        return s.contains ("k") ? v * 1000.0f : v;
    }

    // Tempo-synced LFO choices (index 0 = Free). Beats per LFO cycle.
    inline const juce::StringArray& tremoloSyncNames()
    {
        static const juce::StringArray n { "Free", "1/2", "1/4", "1/4 dot", "1/4 trip", "1/8", "1/8 dot", "1/8 trip", "1/16", "1/16 trip" };
        return n;
    }
    inline double tremoloSyncBeats (int i)
    {
        static const double b[] = { 0.0, 2.0, 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 1.0 / 6.0 };
        return b[juce::jlimit (0, 9, i)];
    }

    // Channel modules (DESIGN.md 2.2 - 2.7). IDs are permanent.
    inline void addModuleParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
    {
        using namespace juce;
        auto pid = [] (const String& s) { return ParameterID { s, 1 }; };
        auto addFloat = [&] (const String& id, const String& name, NormalisableRange<float> range, float def,
                             std::function<String (float, int)> toText, std::function<float (const String&)> fromText = nullptr)
        {
            auto attr = AudioParameterFloatAttributes().withStringFromValueFunction (std::move (toText));
            if (fromText != nullptr) attr = attr.withValueFromStringFunction (std::move (fromText));
            layout.add (std::make_unique<AudioParameterFloat> (pid (id), name, range, def, attr));
        };
        auto addBool = [&] (const String& id, const String& name, bool def) { layout.add (std::make_unique<AudioParameterBool> (pid (id), name, def)); };
        auto addChoice = [&] (const String& id, const String& name, const StringArray& items, int def)
        { layout.add (std::make_unique<AudioParameterChoice> (pid (id), name, items, def)); };

        auto dbText  = [] (float v, int) { return String (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; };
        auto pctText = [] (float v, int) { return String (roundToInt (v)) + " %"; };
        auto bip     = [] (float v, int) { return std::abs (v) < 0.05f ? String ("0") : (v > 0 ? "+" : "") + String (roundToInt (v)); };
        auto hzText  = [] (float v, int) { return freqText (v); };
        auto msText  = [] (float v, int) { return v < 1.0f ? String (v, 2) + " ms" : String (v, v < 10.0f ? 1 : 0) + " ms"; };
        auto num     = [] (float v, int) { return String (v, 2); };
        auto ratioTx = [] (float v, int) { return String (v, v < 10.0f ? 1 : 0) + ":1"; };

        // Tune
        addBool   ("tn_on", "Tune On", true);
        addChoice ("tn_key", "Key", { "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" }, 0);
        addChoice ("tn_scale", "Scale", { "Chromatic", "Major", "Natural Minor", "Harmonic Minor", "Melodic Minor", "Major Pentatonic",
                                          "Minor Pentatonic", "Blues", "Dorian", "Mixolydian", "Custom" }, 0);
        addChoice ("tn_range", "Vocal Range", { "High", "Middle", "Low", "Deep" }, 1);
        addFloat  ("tn_speed", "Retune Speed", { 0.0f, 400.0f, 0.1f, 0.4f }, 40.0f, [] (float v, int) { return String (roundToInt (v)) + " ms"; });
        addFloat  ("tn_humanize", "Humanize", { 0.0f, 100.0f, 0.1f }, 20.0f, pctText);
        static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        for (int n = 0; n < 12; ++n)
            addBool ("tn_note_" + String (n), String ("Note ") + noteNames[n], true);
        addBool   ("tn_correct", "Tune Correct Pitch", true);
        addFloat  ("tn_detune", "Tune Detune", { -100.0f, 100.0f, 0.1f }, 0.0f, [] (float v, int)
        {
            const auto a4 = 440.0 * std::pow (2.0, v / 1200.0);
            return (std::abs (v) < 0.05f ? String ("0") : (v > 0 ? "+" : "") + String (v, 1)) + " ct (A4 " + String (a4, 1) + " Hz)";
        }, [] (const String& t) { return t.getFloatValue(); });
        addBool   ("tn_vib_on", "Vibrato On", false);
        addFloat  ("tn_vib_depth", "Vibrato Depth", { 0.0f, 100.0f, 0.1f }, 30.0f, [] (float v, int) { return String (roundToInt (v)) + " ct"; });
        addFloat  ("tn_vib_rate", "Vibrato Rate", { 1.0f, 12.0f, 0.01f }, 5.5f, [] (float v, int) { return String (v, 2) + " Hz"; });
        addFloat  ("tn_vib_delay", "Vibrato Onset Delay", { 0.0f, 1500.0f, 1.0f, 0.6f }, 250.0f, [] (float v, int) { return String (roundToInt (v)) + " ms"; });
        addFloat  ("tn_vib_rise", "Vibrato Onset Rise", { 0.0f, 1500.0f, 1.0f, 0.6f }, 300.0f, [] (float v, int) { return String (roundToInt (v)) + " ms"; });
        addFloat  ("tn_vib_variation", "Vibrato Variation", { 0.0f, 100.0f, 0.1f }, 20.0f, pctText);
        addBool   ("tn_trem_on", "Tremolo On", false);
        addFloat  ("tn_trem_depth", "Tremolo Depth", { 0.0f, 100.0f, 0.1f }, 40.0f, pctText);
        addFloat  ("tn_trem_rate", "Tremolo Rate", logRange (0.5f, 20.0f), 5.0f, [] (float v, int) { return String (v, 2) + " Hz"; });
        addChoice ("tn_trem_sync", "Tremolo Sync", tremoloSyncNames(), 0);
        addChoice ("tn_trem_shape", "Tremolo Shape", { "Sine", "Triangle", "Square" }, 0);
        addFloat  ("tn_trem_stereo", "Tremolo Stereo Phase", { 0.0f, 180.0f, 1.0f }, 0.0f, [] (float v, int) { return String (roundToInt (v)) + String (CharPointer_UTF8 ("\xc2\xb0")); });
        addBool   ("tn_trem_onset", "Tremolo Follows Onset", false);

        // EQ
        addBool  ("eq_on", "EQ On", true);
        addFloat ("eq_out_gain", "EQ Output", { -24.0f, 24.0f, 0.01f }, 0.0f, dbText);
        const float eqFreqs[8] = { 80, 200, 500, 1000, 2500, 5000, 10000, 15000 };
        StringArray types, slopes;
        for (auto* t : kv::eqTypeNames) types.add (t);
        for (int sl : kv::eqSlopes) slopes.add (String (sl) + " dB/oct");
        for (int b = 1; b <= 8; ++b)
        {
            const String p = "eq" + String (b) + "_", n = "EQ " + String (b) + " ";
            addBool   (p + "used", n + "Used", false);
            addBool   (p + "on", n + "Active", true);
            addChoice (p + "type", n + "Type", types, 0);
            addFloat  (p + "freq", n + "Freq", logRange (10.0f, 30000.0f), eqFreqs[b - 1], hzText, freqFromText);
            addFloat  (p + "gain", n + "Gain", { -30.0f, 30.0f, 0.01f }, 0.0f, dbText);
            addFloat  (p + "q", n + "Q", logRange (0.025f, 40.0f), 1.0f, num);
            addChoice (p + "slope", n + "Slope", slopes, 1);
        }
        // The compressor's side-chain detection EQ uses the same eight-band layout as the main EQ
        // (prefix + N + "_" + used / on / type / freq / gain / q / slope), so one editor serves both.
        auto addEqBandSet = [&] (const String& prefix, const String& namePrefix)
        {
            for (int b = 1; b <= 8; ++b)
            {
                const String p = prefix + String (b) + "_", n = namePrefix + " " + String (b) + " ";
                addBool   (p + "used", n + "Used", false);
                addBool   (p + "on", n + "Active", true);
                addChoice (p + "type", n + "Type", types, 0);
                addFloat  (p + "freq", n + "Freq", logRange (10.0f, 30000.0f), eqFreqs[b - 1], hzText, freqFromText);
                addFloat  (p + "gain", n + "Gain", { -30.0f, 30.0f, 0.01f }, 0.0f, dbText);
                addFloat  (p + "q", n + "Q", logRange (0.025f, 40.0f), 1.0f, num);
                addChoice (p + "slope", n + "Slope", slopes, 1);
            }
        };

        // Multiband
        addBool   ("mb_on", "Multiband On", false);
        layout.add (std::make_unique<AudioParameterInt> (pid ("mb_count"), "Multiband Bands", 1, 6, 1));
        addChoice ("mb_slope", "Multiband Crossover Slope", { "6", "12", "24" }, 1);
        addChoice ("mb_detector", "Multiband Detector", { "Peak", "Smooth" }, 1);
        addChoice ("mb_os", "Multiband Oversampling", { "Off", "2x", "4x" }, 0);
        const float mbLo[6] = { 100, 500, 2000, 5000, 9000, 14000 }, mbHi[6] = { 500, 2000, 5000, 9000, 14000, 20000 };
        for (int b = 1; b <= 6; ++b)
        {
            const String p = "mb" + String (b) + "_", n = "Band " + String (b) + " ";
            addFloat  (p + "lo", n + "Low Edge", logRange (20.0f, 16000.0f), mbLo[b - 1], hzText, freqFromText);
            addFloat  (p + "hi", n + "High Edge", logRange (40.0f, 20000.0f), mbHi[b - 1], hzText, freqFromText);
            addFloat  (p + "thresh", n + "Threshold", { -60.0f, 0.0f, 0.1f }, -24.0f, dbText);
            addFloat  (p + "ratio", n + "Ratio", { 1.0f, 10.0f, 0.01f, 0.5f }, 2.0f, ratioTx);
            addFloat  (p + "attack", n + "Attack", logRange (1.0f, 100.0f), 10.0f, msText);
            addFloat  (p + "release", n + "Release", logRange (20.0f, 1000.0f), 150.0f, msText);
            addFloat  (p + "knee", n + "Knee", { 0.0f, 24.0f, 0.1f }, 6.0f, dbText);
            addFloat  (p + "range", n + "Range", { -24.0f, 24.0f, 0.1f }, -6.0f, dbText);
            addFloat  (p + "gain", n + "Gain", { -24.0f, 24.0f, 0.01f }, 0.0f, dbText);
            addChoice (p + "mode", n + "Mode", { "Compress", "Expand" }, 0);
            addBool   (p + "solo", n + "Solo", false);
            addBool   (p + "bypass", n + "Bypass", false);
            addBool   (p + "mute", n + "Mute", false);
        }

        // Compression
        addBool   ("lv_on", "Compression On", true);
        // LA-2A style optical leveler: Compression drives the side chain (like Peak Reduction), Gain is the make-up.
        addFloat  ("lv_peak", "Compression", { 0.0f, 100.0f, 0.1f }, 40.0f, pctText);
        addFloat  ("lv_gain", "Compression Gain", { -12.0f, 24.0f, 0.01f }, 0.0f, dbText);
        addEqBandSet ("lv_sc", "Compression SC EQ");   // side-chain detection EQ: shapes what the detector hears

        // De-ess
        addBool   ("ds_on", "De-Ess On", true);
        addFloat  ("ds_det_lo", "De-Ess Frequency", logRange (2000.0f, 12000.0f), 5000.0f, hzText, freqFromText);   // detects (and reduces) above this
        addFloat  ("ds_range", "De-Ess Range", { 0.0f, 24.0f, 0.1f }, 6.0f, dbText);

        // Resonance
        addBool   ("rs_on", "Resonance On", false);
        addChoice ("rs_mode", "Resonance Mode", { "Soft", "Hard" }, 0);
        addFloat  ("rs_depth", "Resonance Depth", { 0.0f, 20.0f, 0.01f }, 4.0f, [] (float v, int) { return String (v, 1); });
        addFloat  ("rs_detail", "Resonance Detail", { 0.0f, 100.0f, 0.1f }, 50.0f, pctText);
        addFloat  ("rs_attack", "Resonance Attack", { 0.0f, 100.0f, 0.1f }, 50.0f, pctText);
        addFloat  ("rs_release", "Resonance Release", { 0.0f, 100.0f, 0.1f }, 50.0f, pctText);
        addFloat  ("rs_mix", "Resonance Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, pctText);
        addFloat  ("rs_out_gain", "Resonance Out Gain", { -12.0f, 12.0f, 0.01f }, 0.0f, dbText);
        addBool   ("rs_delta", "Resonance Delta", false);
        addBool   ("rs_bypass", "Resonance Bypass", false);
        addChoice ("rs_quality", "Resonance Quality", { "Normal", "High", "Ultra" }, 0);
        addChoice ("rs_os", "Resonance Oversampling", { "Off", "2x", "4x" }, 0);
        addChoice ("rs_stereo_mode", "Resonance Stereo Mode", { "Left/Right", "Mid/Side" }, 1);
        addFloat  ("rs_link", "Resonance Link", { 0.0f, 100.0f, 0.1f }, 100.0f, pctText);
        addFloat  ("rs_focus", "Resonance Stereo Focus", { -100.0f, 100.0f, 0.1f }, 0.0f, bip);
        for (auto* t : { "detail", "attack", "release" })
            for (auto* side : { "lo", "hi" })
                addFloat (String ("rs_") + t + "_tilt_" + side, String ("Resonance ") + String (t).substring (0, 1).toUpperCase() + String (t).substring (1)
                          + " Tilt " + (String (side) == "lo" ? "Low" : "High"), { -100.0f, 100.0f, 0.1f }, 0.0f, bip);
        addFloat  ("rs_max_cut", "Resonance Max Cut", { 1.0f, 41.0f, 0.1f }, 41.0f, [] (float v, int) { return v >= 40.5f ? String ("Off") : String (v, 1) + " dB"; });
        addFloat  ("rs_wet_trim", "Resonance Wet Trim", { -12.0f, 12.0f, 0.01f }, 0.0f, dbText);
        for (int b = 1; b <= 8; ++b)
        {
            const String p = "rs_b" + String (b) + "_", n = "Resonance Band " + String (b) + " ";
            addBool   (p + "used", n + "Used", false);
            addBool   (p + "on", n + "On", true);
            addChoice (p + "shape", n + "Shape", { "Low cut", "Low shelf", "High shelf", "High cut", "Bell", "Bandpass", "Band reject", "Tilt" }, 4);
            addFloat  (p + "freq", n + "Freq", logRange (20.0f, 20000.0f), 1000.0f, hzText, freqFromText);
            addFloat  (p + "depth", n + "Depth", { -24.0f, 24.0f, 0.01f }, 0.0f, dbText);
            addFloat  (p + "q", n + "Q", logRange (0.1f, 10.0f), 1.0f, num);
        }
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;
        auto pid = [] (const char* s) { return ParameterID { s, 1 }; };

        auto addFloat = [&] (const char* id, const String& name, NormalisableRange<float> range, float def,
                             std::function<String (float, int)> toText, const String& label = {},
                             std::function<float (const String&)> fromText = nullptr)
        {
            auto attr = AudioParameterFloatAttributes().withStringFromValueFunction (std::move (toText)).withLabel (label);
            if (fromText != nullptr)
                attr = attr.withValueFromStringFunction (std::move (fromText));
            layout.add (std::make_unique<AudioParameterFloat> (pid (id), name, range, def, attr));
        };
        auto dbText = [] (float v, int) { return String (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; };

        addModuleParameters (layout);

        addFloat (kvid::inGain,  "Input Gain",  { -24.0f, 24.0f, 0.01f }, 0.0f, dbText);
        addFloat (kvid::outGain, "Output Gain", { -24.0f, 24.0f, 0.01f }, 0.0f, dbText);

        return layout;
    }
}
