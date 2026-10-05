#pragma once

#include "Biquad.h"
#include "../Params.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

enum BandType { Bell = 0, LowShelf, HighShelf, HighPass, LowPass, Notch };

struct EqBandParams
{
    bool  on = false;
    int   type = Bell;
    float freq = 1000.0f, gainDb = 0.0f, q = 1.0f;

    bool operator== (const EqBandParams& o) const noexcept
    {
        return on == o.on && type == o.type && freq == o.freq && gainDb == o.gainDb && q == o.q;
    }
    bool operator!= (const EqBandParams& o) const noexcept { return ! (*this == o); }
};

using EqParams = std::array<EqBandParams, cs::numBands>;

inline bool bandHasGain (int type) noexcept
{
    return type == Bell || type == LowShelf || type == HighShelf;
}

inline BiquadCoeffs makeBandCoeffs (const EqBandParams& b, double sampleRate)
{
    using K = biquad::Kind;
    switch (b.type)
    {
        case LowShelf:  return biquad::make (K::LowShelf,  sampleRate, b.freq, b.q, b.gainDb);
        case HighShelf: return biquad::make (K::HighShelf, sampleRate, b.freq, b.q, b.gainDb);
        case HighPass:  return biquad::make (K::HighPass,  sampleRate, b.freq, b.q);
        case LowPass:   return biquad::make (K::LowPass,   sampleRate, b.freq, b.q);
        case Notch:     return biquad::make (K::Notch,     sampleRate, b.freq, b.q);
        default:        return biquad::make (K::Peak,      sampleRate, b.freq, b.q, b.gainDb);
    }
}

// Six-band parametric EQ.
class Equalizer
{
public:
    void prepare (double newSampleRate)
    {
        sr = newSampleRate;
        for (auto& st : state)
            st.cached = EqBandParams { false, -1, -1.0f, -1.0f, -1.0f };
        reset();
    }

    void reset()
    {
        for (auto& st : state)
            for (auto& f : st.filter)
                f.reset();
    }

    void process (juce::AudioBuffer<float>& buffer, const EqParams& params)
    {
        const int n = buffer.getNumSamples();
        const int chs = juce::jmin (buffer.getNumChannels(), 2);

        for (int b = 0; b < cs::numBands; ++b)
        {
            auto& st = state[(size_t) b];
            const auto& p = params[(size_t) b];

            if (! p.on)
            {
                st.cached.on = false;
                continue;
            }

            if (p != st.cached)
            {
                const auto c = makeBandCoeffs (p, sr);
                for (auto& f : st.filter)
                {
                    f.c = c;
                    if (! st.cached.on)
                        f.reset();
                }
                st.cached = p;
            }

            for (int c = 0; c < chs; ++c)
            {
                auto* d = buffer.getWritePointer (c);
                auto& f = st.filter[c];
                for (int i = 0; i < n; ++i)
                    d[i] = f.process (d[i]);
            }
        }
    }

    // Combined magnitude response in dB (used by the GUI).
    static double responseDb (const EqParams& params, double freq, double sampleRate)
    {
        double db = 0.0;
        for (const auto& p : params)
            if (p.on)
                db += 20.0 * std::log10 (std::max (makeBandCoeffs (p, sampleRate).magnitude (freq, sampleRate), 1.0e-9));
        return db;
    }

private:
    struct BandState
    {
        Biquad filter[2];
        EqBandParams cached;
    };

    double sr = 44100.0;
    std::array<BandState, cs::numBands> state;
};
