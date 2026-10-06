#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../sends/Reverb.h"
#include "../sends/Delay.h"
#include "../sends/Widener.h"
#include "../dsp/Flanger.h"
#include "../dsp/Eq.h"

// Permanent parameter IDs (see docs/KaminariVocal/DESIGN.md, section 2.9). Never rename or reuse an ID.
namespace kvid
{
    // global
    inline constexpr const char* inGain  = "gl_in_gain";
    inline constexpr const char* outGain = "gl_out_gain";

    // per send: on, level, tap point
    inline constexpr const char* rvOn = "rv_on",  * rvSend = "rv_send",  * rvTap = "rv_tap";
    inline constexpr const char* dlOn = "dl_on",  * dlSend = "dl_send",  * dlTap = "dl_tap";
    inline constexpr const char* wdOn = "wd_on",  * wdSend = "wd_send",  * wdTap = "wd_tap";

    // reverb
    inline constexpr const char* rvMode = "rv_mode", * rvDecay = "rv_decay", * rvSize = "rv_size",
                                 * rvPreDelay = "rv_predelay", * rvHiCut = "rv_hicut", * rvLoCut = "rv_locut",
                                 * rvModRate = "rv_mod_rate", * rvModDepth = "rv_mod_depth",
                                 * rvDensity = "rv_density", * rvAttack = "rv_attack";

    // delay
    inline constexpr const char* dlMode = "dl_mode", * dlStyle = "dl_style",
                                 * dlT1Unit = "dl_t1_unit", * dlT1Ms = "dl_t1_ms", * dlT1Note = "dl_t1_note",
                                 * dlT2Unit = "dl_t2_unit", * dlT2Ms = "dl_t2_ms", * dlT2Note = "dl_t2_note",
                                 * dlFeedback = "dl_feedback", * dlLoCut = "dl_locut", * dlHiCut = "dl_hicut",
                                 * dlSaturation = "dl_saturation", * dlWidth = "dl_width", * dlOffset = "dl_offset",
                                 * dlAccent = "dl_accent", * dlAccent2 = "dl_accent2", * dlBalance = "dl_balance",
                                 * dlFbMix = "dl_fb_mix", * dlFbBal = "dl_fb_bal", * dlGroove = "dl_groove",
                                 * dlFeel = "dl_feel", * dlPrime = "dl_prime", * dlWobble = "dl_wobble",
                                 * dlWobbleRate = "dl_wobble_rate", * dlWobbleShape = "dl_wobble_shape",
                                 * dlWobbleSync = "dl_wobble_sync", * dlDiffusion = "dl_diffusion",
                                 * dlDiffSize = "dl_diff_size", * dlDiffLoop = "dl_diff_loop";

    // widener
    inline constexpr const char* wdType = "wd_type",
                                 * msStyle = "wd_ms_style", * msDetune = "wd_ms_detune", * msDelay = "wd_ms_delay",
                                 * msFocus = "wd_ms_focus",
                                 * swWidth = "wd_sw_width", * swMode = "wd_sw_mode", * swTone = "wd_sw_tone",
                                 * swOutput = "wd_sw_output";

    // flanger
    inline constexpr const char* flOn = "fl_on", * flMix = "fl_mix", * flRate = "fl_rate", * flSync = "fl_sync", * flDepth = "fl_depth", * flDelay = "fl_delay",
                                 * flFeedback = "fl_feedback", * flStereo = "fl_stereo", * flShape = "fl_shape",
                                 * flHiCut = "fl_hicut";
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

    // Send levels: -60 dB is the bottom of the range and means fully off (gain 0).
    constexpr float sendOffDb = -60.0f;
    inline float sendGain (float db) { return db <= sendOffDb + 0.001f ? 0.0f : juce::Decibels::decibelsToGain (db); }
    inline juce::String sendText (float db, int)
    {
        return db <= sendOffDb + 0.001f ? juce::String ("Off") : juce::String (db, 1) + " dB";
    }
    inline float sendFromText (const juce::String& t)
    {
        return t.trim().equalsIgnoreCase ("off") || t.trim() == "-inf" ? sendOffDb : t.getFloatValue();
    }

    inline const juce::StringArray& noteNames()
    {
        static const juce::StringArray n { "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
        return n;
    }

    // Note value index (0 = 1/2 .. 5 = 1/64) and unit (1 Note, 2 Dot, 3 Trip) to beats.
    inline double noteBeats (int noteIndex, int unit)
    {
        double beats = 2.0 / std::pow (2.0, juce::jlimit (0, 5, noteIndex));
        if (unit == 2) beats *= 1.5;
        if (unit == 3) beats *= 2.0 / 3.0;
        return beats;
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
    inline const juce::StringArray& flangerSyncNames()
    {
        static const juce::StringArray n { "Free", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8" };
        return n;
    }
    inline double flangerSyncBeats (int i)
    {
        static const double b[] = { 0.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5 };
        return b[juce::jlimit (0, 6, i)];
    }

    inline juce::StringArray reverbModeNames()
    {
        juce::StringArray a;
        for (int i = 0; i < kv::numReverbModes; ++i)
            a.add (kv::reverbMode (i).name);
        return a;
    }

    inline juce::StringArray delayStyleNames()
    {
        juce::StringArray a;
        for (int i = 0; i < kv::numDelayStyles; ++i)
            a.add (kv::delayStyleName (i));
        return a;
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
        for (int sl : kv::eqSlopes) slopes.add (String (sl));
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
        addChoice ("lv_style", "Compression Style", { "Clean", "Vocal", "Opto", "Classic", "Punch" }, 0);
        addFloat  ("lv_thresh", "Compression Threshold", { -50.0f, 0.0f, 0.1f }, -14.0f, dbText);
        addFloat  ("lv_ratio", "Compression Ratio", { 1.0f, 20.0f, 0.01f, 0.4f }, 3.0f, ratioTx);
        addFloat  ("lv_attack", "Compression Attack", logRange (0.005f, 250.0f), 8.0f, msText);
        addFloat  ("lv_release", "Compression Release", logRange (10.0f, 2000.0f), 150.0f, msText);
        addBool   ("lv_auto_release", "Compression Auto Release", true);
        addFloat  ("lv_knee", "Compression Knee", { 0.0f, 24.0f, 0.1f }, 8.0f, dbText);
        addFloat  ("lv_range", "Compression Range", { 0.0f, 40.0f, 0.1f }, 15.0f, dbText);
        addFloat  ("lv_hold", "Compression Hold", { 0.0f, 500.0f, 0.1f, 0.5f }, 0.0f, msText);
        addFloat  ("lv_lookahead", "Compression Lookahead", { 0.0f, 20.0f, 0.01f }, 0.0f, [] (float v, int) { return v < 0.005f ? String ("Off") : String (v, 1) + " ms"; });
        addChoice ("lv_detector", "Compression Detector", { "Peak", "Smooth" }, 1);
        addFloat  ("lv_mix", "Compression Mix", { 0.0f, 200.0f, 0.1f }, 100.0f, pctText);
        addFloat  ("lv_wet_gain", "Compression Wet Gain", { -12.0f, 12.0f, 0.01f }, 0.0f, dbText);
        addFloat  ("lv_dry", "Compression Dry", { -60.0f, 36.0f, 0.1f }, -60.0f, [] (float v, int) { return v <= -60.0f ? String ("Off") : String (v, 1) + " dB"; });
        addFloat  ("lv_sc_level", "Compression Side Chain Level", { -36.0f, 36.0f, 0.1f }, 0.0f, dbText);
        addFloat  ("lv_stereo_link", "Compression Stereo Link", { 0.0f, 100.0f, 0.1f }, 100.0f, pctText);
        addFloat  ("lv_out_gain", "Compression Output", { -24.0f, 24.0f, 0.01f }, 0.0f, dbText);
        addBool   ("lv_auto_gain", "Compression Auto Gain", true);
        // side-chain detection bands (shape what the detector hears)
        const float scFreqs[4] = { 120, 600, 3000, 8000 };
        for (int b = 1; b <= 4; ++b)
        {
            const String p = "lv_sc" + String (b) + "_", n = "Compression SC " + String (b) + " ";
            addBool   (p + "used", n + "Used", false);
            addBool   (p + "on", n + "Active", true);
            addChoice (p + "type", n + "Type", { "Bell", "Low Cut", "High Cut", "Low Shelf", "High Shelf" }, 0);
            addFloat  (p + "freq", n + "Freq", logRange (20.0f, 20000.0f), scFreqs[b - 1], hzText, freqFromText);
            addFloat  (p + "gain", n + "Gain", { -24.0f, 24.0f, 0.01f }, 0.0f, dbText);
            addFloat  (p + "q", n + "Q", logRange (0.1f, 18.0f), 1.0f, num);
        }

        // Distortion
        addBool   ("dt_on", "Distortion On", false);
        addChoice ("dt_style", "Distortion Style", { "Tape", "Tube", "Warm", "Fuzz", "Clip", "Lo-Fi" }, 0);
        addFloat  ("dt_drive", "Distortion Drive", { 0.0f, 36.0f, 0.01f }, 9.0f, dbText);
        addFloat  ("dt_tone", "Distortion Tone", { -100.0f, 100.0f, 0.1f }, 0.0f, bip);
        addFloat  ("dt_bias", "Distortion Bias", { 0.0f, 100.0f, 0.1f }, 0.0f, pctText);
        addFloat  ("dt_lowcut", "Distortion Low Cut", logRange (20.0f, 1000.0f), 20.0f,
                   [] (float v, int) { return v <= 20.5f ? String ("Off") : freqText (v); }, freqFromText);
        addFloat  ("dt_crush", "Distortion Crush", { 0.0f, 100.0f, 0.1f }, 40.0f, pctText);
        addFloat  ("dt_mix", "Distortion Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, pctText);
        addFloat  ("dt_out", "Distortion Output", { -24.0f, 12.0f, 0.01f }, 0.0f, dbText);
        addBool   ("dt_auto_gain", "Distortion Auto Gain", true);
        addChoice ("dt_os", "Distortion Oversampling", { "Off", "2x", "4x" }, 1);

        // De-ess
        addBool   ("ds_on", "De-Ess On", true);
        addFloat  ("ds_thresh", "De-Ess Threshold", { -60.0f, 0.0f, 0.1f }, -28.0f, dbText);
        addFloat  ("ds_range", "De-Ess Range", { 0.0f, 24.0f, 0.1f }, 8.0f, dbText);
        addFloat  ("ds_det_lo", "De-Ess Low Edge", logRange (1000.0f, 16000.0f), 3500.0f, hzText, freqFromText);
        addFloat  ("ds_det_hi", "De-Ess High Edge", logRange (2000.0f, 20000.0f), 8600.0f, hzText, freqFromText);
        addChoice ("ds_detect", "De-Ess Detection", { "Voice Focus", "Full Band" }, 0);
        addChoice ("ds_process", "De-Ess Processing", { "Split Band", "Wideband" }, 0);
        addChoice ("ds_mode", "De-Ess Mode", { "Single Vocal", "Allround" }, 0);
        addFloat  ("ds_lookahead", "De-Ess Lookahead", { 0.0f, 15.0f, 0.01f }, 0.0f, [] (float v, int) { return v < 0.005f ? String ("Off") : String (v, 1) + " ms"; });
        addFloat  ("ds_stereo_link", "De-Ess Stereo Link", { 0.0f, 100.0f, 0.1f }, 100.0f, pctText);
        addChoice ("ds_link_mode", "De-Ess Link Mode", { "Stereo", "Mid", "Side" }, 0);
        addBool   ("ds_listen", "De-Ess Detector Listen", false);
        addBool   ("ds_audition_trigger", "De-Ess Audition Triggering", false);
        addChoice ("ds_os", "De-Ess Oversampling", { "Off", "2x", "4x" }, 0);

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
        auto addBool = [&] (const char* id, const String& name, bool def)
        {
            layout.add (std::make_unique<AudioParameterBool> (pid (id), name, def));
        };
        auto addChoice = [&] (const char* id, const String& name, const StringArray& items, int def)
        {
            layout.add (std::make_unique<AudioParameterChoice> (pid (id), name, items, def));
        };

        auto dbText   = [] (float v, int) { return String (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; };
        auto pctText  = [] (float v, int) { return String (roundToInt (v)) + " %"; };
        auto bipText  = [] (float v, int) { return std::abs (v) < 0.05f ? String ("0") : (v > 0 ? "+" : "") + String (roundToInt (v)); };
        auto hzText   = [] (float v, int) { return freqText (v); };
        auto msText   = [] (float v, int) { return String (v, v < 10.0f ? 1 : 0) + " ms"; };
        auto secText  = [] (float v, int) { return String (v, v < 10.0f ? 2 : 1) + " s"; };
        auto rateText = [] (float v, int) { return String (v, 2) + " Hz"; };
        const StringArray taps { "Post-fader", "Pre-fader" };
        const StringArray units { "Time", "Note", "Dot", "Trip" };

        addModuleParameters (layout);

        addFloat (kvid::inGain,  "Input Gain",  { -24.0f, 24.0f, 0.01f }, 0.0f, dbText);
        addFloat (kvid::outGain, "Output Gain", { -24.0f, 24.0f, 0.01f }, 0.0f, dbText);

        const NormalisableRange<float> sendRange { sendOffDb, 6.0f, 0.01f, 2.2f };

        // Reverb send
        addBool   (kvid::rvOn, "Reverb Send On", false);
        addFloat  (kvid::rvSend, "Reverb Send", sendRange, -12.0f, sendText, {}, sendFromText);
        addChoice (kvid::rvTap, "Reverb Send Tap", taps, 0);
        addChoice (kvid::rvMode, "Reverb Mode", reverbModeNames(), 0);
        addFloat  (kvid::rvDecay, "Reverb Decay", logRange (0.2f, 20.0f), 2.2f, secText);
        addFloat  (kvid::rvSize, "Reverb Size", { 0.0f, 100.0f, 0.1f }, 50.0f, pctText);
        addFloat  (kvid::rvPreDelay, "Reverb Pre-delay", { 0.0f, 250.0f, 0.1f, 0.5f }, 20.0f, msText);
        addFloat  (kvid::rvHiCut, "Reverb High Cut", logRange (1000.0f, 20000.0f), 8000.0f, hzText, {}, freqFromText);
        addFloat  (kvid::rvLoCut, "Reverb Low Cut", logRange (20.0f, 1000.0f), 150.0f, hzText, {}, freqFromText);
        addFloat  (kvid::rvModRate, "Reverb Mod Rate", logRange (0.05f, 5.0f), 0.6f, rateText);
        addFloat  (kvid::rvModDepth, "Reverb Mod Depth", { 0.0f, 100.0f, 0.1f }, 40.0f, pctText);
        addFloat  (kvid::rvDensity, "Reverb Density", { 0.0f, 100.0f, 0.1f }, 70.0f, pctText);
        addFloat  (kvid::rvAttack, "Reverb Attack", { 0.0f, 100.0f, 0.1f }, 50.0f, pctText);

        // Delay send
        addBool   (kvid::dlOn, "Delay Send On", false);
        addFloat  (kvid::dlSend, "Delay Send", sendRange, -15.0f, sendText, {}, sendFromText);
        addChoice (kvid::dlTap, "Delay Send Tap", taps, 0);
        addChoice (kvid::dlMode, "Delay Mode", { "Single", "Dual", "Ping-Pong" }, 0);
        addChoice (kvid::dlStyle, "Delay Style", delayStyleNames(), 1);
        addChoice (kvid::dlT1Unit, "Delay Echo 1 Unit", units, 1);
        addFloat  (kvid::dlT1Ms, "Delay Echo 1 Time", logRange (1.0f, 2500.0f), 375.0f, msText);
        addChoice (kvid::dlT1Note, "Delay Echo 1 Note", noteNames(), 2);
        addChoice (kvid::dlT2Unit, "Delay Echo 2 Unit", units, 1);
        addFloat  (kvid::dlT2Ms, "Delay Echo 2 Time", logRange (1.0f, 2500.0f), 500.0f, msText);
        addChoice (kvid::dlT2Note, "Delay Echo 2 Note", noteNames(), 1);
        addFloat  (kvid::dlFeedback, "Delay Feedback", { 0.0f, 100.0f, 0.1f }, 30.0f, pctText);
        addFloat  (kvid::dlLoCut, "Delay Low Cut", logRange (20.0f, 2000.0f), 150.0f, hzText, {}, freqFromText);
        addFloat  (kvid::dlHiCut, "Delay High Cut", logRange (1000.0f, 20000.0f), 6000.0f, hzText, {}, freqFromText);
        addFloat  (kvid::dlSaturation, "Delay Saturation", { 0.0f, 100.0f, 0.1f }, 25.0f, pctText);
        addFloat  (kvid::dlWidth, "Delay Width", { 0.0f, 100.0f, 0.1f }, 50.0f, pctText);
        addFloat  (kvid::dlOffset, "Delay L/R Offset", { 0.0f, 25.0f, 0.01f }, 8.0f, msText);
        addFloat  (kvid::dlAccent, "Delay Accent", { -100.0f, 100.0f, 0.1f }, 0.0f, bipText);
        addFloat  (kvid::dlAccent2, "Delay Accent 2", { -100.0f, 100.0f, 0.1f }, 0.0f, bipText);
        addFloat  (kvid::dlBalance, "Delay Balance", { -100.0f, 100.0f, 0.1f }, 0.0f, bipText);
        addFloat  (kvid::dlFbMix, "Delay Feedback Mix", { 0.0f, 100.0f, 0.1f }, 0.0f, pctText);
        addFloat  (kvid::dlFbBal, "Delay Feedback Balance", { -100.0f, 100.0f, 0.1f }, 0.0f, bipText);
        addFloat  (kvid::dlGroove, "Delay Groove", { -100.0f, 100.0f, 0.1f }, 0.0f, bipText);
        addFloat  (kvid::dlFeel, "Delay Feel", { -50.0f, 50.0f, 0.01f }, 0.0f, msText);
        addBool   (kvid::dlPrime, "Delay Prime Numbers", false);
        addFloat  (kvid::dlWobble, "Delay Wobble", { 0.0f, 100.0f, 0.1f }, 0.0f, pctText);
        addFloat  (kvid::dlWobbleRate, "Delay Wobble Rate", logRange (0.05f, 10.0f), 1.0f, rateText);
        addChoice (kvid::dlWobbleShape, "Delay Wobble Shape", { "Sine", "Triangle", "Square", "Random Walk", "Random S/H" }, 0);
        addFloat  (kvid::dlWobbleSync, "Delay Wobble Sync", { -100.0f, 100.0f, 0.1f }, 0.0f, bipText);
        addFloat  (kvid::dlDiffusion, "Delay Diffusion", { 0.0f, 100.0f, 0.1f }, 0.0f, pctText);
        addFloat  (kvid::dlDiffSize, "Delay Diffusion Size", { 0.0f, 100.0f, 0.1f }, 50.0f, pctText);
        addChoice (kvid::dlDiffLoop, "Delay Diffusion Position", { "Post", "Loop" }, 0);

        // Reverb and Delay returns: EQ, ducking, wet gain (DESIGN.md 2.9.6); Delay / Reverb routing
        for (auto [pre, name] : { std::pair<const char*, const char*> { "rv_", "Reverb" }, { "dl_", "Delay" } })
        {
            const String p (pre), n (name);
            auto qText = [] (float v, int) { return String (v, 2); };
            const char* bandNames[] = { "High Pass", "Bell 1", "Bell 2", "Low Pass" };
            const float freqs[] = { 100.0f, 400.0f, 3000.0f, 10000.0f };
            for (int b = 1; b <= 4; ++b)
            {
                const String bp = p + "eq" + String (b) + "_", bn = n + " EQ " + bandNames[b - 1] + " ";
                addBool ((bp + "on").toRawUTF8(), bn + "On", false);
                addFloat ((bp + "freq").toRawUTF8(), bn + "Freq", logRange (20.0f, 20000.0f), freqs[b - 1], hzText, {}, freqFromText);
                if (b == 2 || b == 3)
                    addFloat ((bp + "gain").toRawUTF8(), bn + "Gain", { -18.0f, 18.0f, 0.01f }, 0.0f, dbText);
                addFloat ((bp + "q").toRawUTF8(), bn + "Q", logRange (0.1f, 18.0f), 1.0f, qText);
            }
            addBool ((p + "duck_on").toRawUTF8(), n + " Ducking On", false);
            addFloat ((p + "duck_thresh").toRawUTF8(), n + " Ducking Threshold", { -60.0f, 0.0f, 0.1f }, -30.0f, dbText);
            addFloat ((p + "duck_depth").toRawUTF8(), n + " Ducking Depth", { 0.0f, 30.0f, 0.1f }, 9.0f, dbText);
            addFloat ((p + "duck_attack").toRawUTF8(), n + " Ducking Attack", logRange (0.1f, 200.0f), 10.0f, msText);
            addFloat ((p + "duck_release").toRawUTF8(), n + " Ducking Release", logRange (10.0f, 2000.0f), 250.0f, msText);
            addChoice ((p + "duck_source").toRawUTF8(), n + " Ducking Source", { "Vocal", "Raw Input" }, 0);
            addFloat ((p + "wet_gain").toRawUTF8(), n + " Wet Gain", { -24.0f, 12.0f, 0.01f }, 0.0f, dbText);
        }
        addChoice ("fx_route", "Delay / Reverb Routing", { "Off", "Delay into Reverb", "Reverb into Delay" }, 0);
        addFloat  ("fx_route_amt", "Delay / Reverb Routing Amount", { 0.0f, 100.0f, 0.1f }, 30.0f, pctText);

        // Widener send
        addBool   (kvid::wdOn, "Widener Send On", false);
        addFloat  (kvid::wdSend, "Widener Send", sendRange, -12.0f, sendText, {}, sendFromText);
        addChoice (kvid::wdTap, "Widener Send Tap", taps, 0);
        addChoice (kvid::wdType, "Widener Type", { "MicroShift", "SideWidener" }, 0);
        addChoice (kvid::msStyle, "MicroShift Style", { "I", "II", "III" }, 0);
        addFloat  (kvid::msDetune, "MicroShift Detune", { 0.0f, 200.0f, 0.1f }, 100.0f, pctText);
        addFloat  (kvid::msDelay, "MicroShift Delay", { 0.0f, 200.0f, 0.1f }, 100.0f, pctText);
        addFloat  (kvid::msFocus, "MicroShift Focus", logRange (20.0f, 10000.0f), 20.0f, hzText, {}, freqFromText);
        addFloat  (kvid::swWidth, "SideWidener Width", { 0.0f, 100.0f, 0.1f }, 50.0f,
                   [] (float v, int) { return String (roundToInt (v)); });
        addChoice (kvid::swMode, "SideWidener Mode", { "Mode 1", "Mode 2", "Mode 3" }, 0);
        addFloat  (kvid::swTone, "SideWidener Tone", { 0.0f, 100.0f, 0.1f }, 50.0f,
                   [] (float v, int) { return String (roundToInt (v)); });
        addFloat  (kvid::swOutput, "SideWidener Output", { -60.0f, 0.0f, 0.01f, 2.5f }, 0.0f,
                   [] (float v, int) { return v <= -60.0f ? String ("-inf dB") : String (v, 1) + " dB"; });

        // Flanger (channel module between Compression and Distortion)
        addBool   (kvid::flOn, "Flanger On", false);
        addFloat  (kvid::flMix, "Flanger Mix", { 0.0f, 100.0f, 0.1f }, 50.0f, pctText);
        addFloat  (kvid::flRate, "Flanger Rate", logRange (0.02f, 10.0f), 0.3f, [] (float v, int) { return String (v, v < 1.0f ? 2 : 1) + " Hz"; });
        addChoice (kvid::flSync, "Flanger Sync", flangerSyncNames(), 0);
        addFloat  (kvid::flDepth, "Flanger Depth", { 0.0f, 100.0f, 0.1f }, 60.0f, pctText);
        addFloat  (kvid::flDelay, "Flanger Delay", logRange (0.1f, 10.0f), 1.5f, [] (float v, int) { return String (v, 2) + " ms"; });
        addFloat  (kvid::flFeedback, "Flanger Feedback", { -95.0f, 95.0f, 0.1f }, 40.0f, [] (float v, int) { return String (roundToInt (v)) + " %"; });
        addFloat  (kvid::flStereo, "Flanger Stereo Phase", { 0.0f, 180.0f, 1.0f }, 90.0f, [] (float v, int) { return String (roundToInt (v)) + String (CharPointer_UTF8 ("\xc2\xb0")); });
        addChoice (kvid::flShape, "Flanger Shape", { "Sine", "Triangle" }, 1);
        addFloat  (kvid::flHiCut, "Flanger High Cut", logRange (1000.0f, 20000.0f), 12000.0f, hzText, {}, freqFromText);

        return layout;
    }
}
