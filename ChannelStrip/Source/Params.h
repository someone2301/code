#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

namespace ids
{
    inline constexpr const char* inGain       = "inGain";
    inline constexpr const char* phase        = "phase";
    inline constexpr const char* mono         = "mono";
    inline constexpr const char* pan          = "pan";
    inline constexpr const char* outGain      = "outGain";

    inline constexpr const char* deessOn      = "deessOn";
    inline constexpr const char* deessFreq    = "deessFreq";
    inline constexpr const char* deessThresh  = "deessThresh";
    inline constexpr const char* deessRange   = "deessRange";
    inline constexpr const char* deessListen  = "deessListen";

    inline constexpr const char* eqOn         = "eqOn";
    inline constexpr const char* eqPost       = "eqPost";

    inline constexpr const char* compOn       = "compOn";
    inline constexpr const char* compMode     = "compMode";
    inline constexpr const char* compMix      = "compMix";
    inline constexpr const char* fetIn        = "fetIn";
    inline constexpr const char* fetOut       = "fetOut";
    inline constexpr const char* fetAttack    = "fetAttack";
    inline constexpr const char* fetRelease   = "fetRelease";
    inline constexpr const char* fetRatio     = "fetRatio";
    inline constexpr const char* laPeak       = "laPeak";
    inline constexpr const char* laGain       = "laGain";
    inline constexpr const char* laLimit      = "laLimit";
}

namespace cs
{
    constexpr int numBands = 6;

    // 0-based band index, ids are 1-based ("eq1_freq" ...)
    inline juce::String eqId (int band, const char* what)
    {
        return "eq" + juce::String (band + 1) + "_" + what;
    }

    struct BandDefault { int type; float freq; };
    inline constexpr BandDefault bandDefaults[numBands] = {
        { 3, 30.0f }, { 1, 100.0f }, { 0, 400.0f }, { 0, 1500.0f }, { 0, 5000.0f }, { 2, 12000.0f }
    };

    inline juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return { lo, hi,
                 [] (float s, float e, float p) { return s * std::pow (e / s, p); },
                 [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
                 [] (float s, float e, float v) { return juce::jlimit (s, e, v); } };
    }

    inline juce::String freqText (float v)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    }

    inline float freqFromText (const juce::String& t)
    {
        auto s = t.trim().toLowerCase();
        auto v = s.getFloatValue();
        return s.contains ("k") ? v * 1000.0f : v;
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        auto pid = [] (const String& s) { return ParameterID { s, 1 }; };

        auto addFloat = [&] (const String& id, const String& name, NormalisableRange<float> range, float def,
                             std::function<String (float, int)> toText,
                             std::function<float (const String&)> fromText = nullptr)
        {
            auto attr = AudioParameterFloatAttributes().withStringFromValueFunction (std::move (toText));
            if (fromText != nullptr)
                attr = attr.withValueFromStringFunction (std::move (fromText));
            layout.add (std::make_unique<AudioParameterFloat> (pid (id), name, range, def, attr));
        };
        auto addBool = [&] (const String& id, const String& name, bool def)
        {
            layout.add (std::make_unique<AudioParameterBool> (pid (id), name, def));
        };

        auto dbText = [] (float v, int) { return String (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; };
        auto pctText = [] (float v, int) { return String (roundToInt (v)) + " %"; };
        auto numText = [] (float v, int) { return String (roundToInt (v)); };
        auto freqTx = [] (float v, int) { return freqText (v); };
        auto qText = [] (float v, int) { return String (v, 2); };
        auto msText = [] (float v, int) { return v < 1.0f ? String (roundToInt (v * 1000.0f)) + " us" : String (v, v < 10.0f ? 1 : 0) + " ms"; };

        // Gain stage
        addFloat (ids::inGain,  "Input Gain",  { -24.0f, 24.0f, 0.01f }, 0.0f, dbText);
        addBool  (ids::phase,   "Phase Invert", false);
        addBool  (ids::mono,    "Mono", false);
        addFloat (ids::pan,     "Pan", { -100.0f, 100.0f, 0.1f }, 0.0f,
                  [] (float v, int) { return v == 0.0f ? String ("C") : (String (roundToInt (std::abs (v))) + (v < 0 ? " L" : " R")); });
        addFloat (ids::outGain, "Output Gain", { -24.0f, 24.0f, 0.01f }, 0.0f, dbText);

        // De-esser
        addBool  (ids::deessOn,     "De-Ess On", false);
        addFloat (ids::deessFreq,   "De-Ess Frequency", logRange (2000.0f, 12000.0f), 6500.0f, freqTx, freqFromText);
        addFloat (ids::deessThresh, "De-Ess Threshold", { -60.0f, 0.0f, 0.1f }, -30.0f, dbText);
        addFloat (ids::deessRange,  "De-Ess Range", { 0.0f, 24.0f, 0.1f }, 10.0f, dbText);
        addBool  (ids::deessListen, "De-Ess Listen", false);

        // EQ
        addBool (ids::eqOn,   "EQ On", true);
        addBool (ids::eqPost, "EQ Post Compressor", false);
        for (int b = 0; b < numBands; ++b)
        {
            const auto n = String (b + 1);
            addBool (eqId (b, "on"), "EQ " + n + " On", false);
            layout.add (std::make_unique<AudioParameterChoice> (pid (eqId (b, "type")), "EQ " + n + " Type",
                        StringArray { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch" },
                        bandDefaults[b].type));
            addFloat (eqId (b, "freq"), "EQ " + n + " Freq", logRange (20.0f, 20000.0f), bandDefaults[b].freq, freqTx, freqFromText);
            addFloat (eqId (b, "gain"), "EQ " + n + " Gain", { -24.0f, 24.0f, 0.01f }, 0.0f, dbText);
            addFloat (eqId (b, "q"),    "EQ " + n + " Q", logRange (0.1f, 18.0f), 1.0f, qText);
        }

        // Compressor
        addBool (ids::compOn, "Comp On", false);
        layout.add (std::make_unique<AudioParameterChoice> (pid (ids::compMode), "Comp Mode",
                    StringArray { "FET", "Opto" }, 0));
        addFloat (ids::compMix, "Comp Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, pctText);

        addFloat (ids::fetIn,      "FET Input",   { 0.0f, 40.0f, 0.01f }, 8.0f, dbText);
        addFloat (ids::fetOut,     "FET Output",  { -30.0f, 12.0f, 0.01f }, -4.0f, dbText);
        addFloat (ids::fetAttack,  "FET Attack",  logRange (0.02f, 0.8f), 0.1f, msText);
        addFloat (ids::fetRelease, "FET Release", logRange (50.0f, 1100.0f), 200.0f, msText);
        layout.add (std::make_unique<AudioParameterChoice> (pid (ids::fetRatio), "FET Ratio",
                    StringArray { "4:1", "8:1", "12:1", "20:1", "ALL" }, 0));

        addFloat (ids::laPeak, "Opto Peak Reduction", { 0.0f, 100.0f, 0.1f }, 35.0f, numText);
        addFloat (ids::laGain, "Opto Gain", { 0.0f, 40.0f, 0.01f }, 6.0f, dbText);
        addBool  (ids::laLimit, "Opto Limit", false);

        (void) numText;
        return layout;
    }
}
