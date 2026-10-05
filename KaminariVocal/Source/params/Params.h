#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../sends/Reverb.h"
#include "../sends/Delay.h"
#include "../sends/Widener.h"

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

        return layout;
    }
}
