#pragma once

#include "Biquad.h"
#include <juce_audio_basics/juce_audio_basics.h>

struct DeEssParams
{
    bool  on = false;
    float freq = 6500.0f;
    float threshDb = -30.0f;
    float rangeDb = 10.0f;
    bool  listen = false;
};

// Split-band de-esser: a band-pass around the sibilance region is detected and attenuated, the rest passes untouched.
class DeEsser
{
public:
    void prepare (double newSampleRate)
    {
        sr = newSampleRate;
        attackCoef = (float) std::exp (-1.0 / (0.0005 * sr));
        releaseCoef = (float) std::exp (-1.0 / (0.040 * sr));
        lastFreq = -1.0f;
        reset();
    }

    void reset()
    {
        for (auto& f : bp)
            f.reset();
        gr = 0.0f;
    }

    float process (juce::AudioBuffer<float>& buffer, const DeEssParams& p)
    {
        if (! p.on)
        {
            gr = 0.0f;
            return 0.0f;
        }

        if (p.freq != lastFreq)
        {
            const auto c = biquad::make (biquad::Kind::BandPass, sr, p.freq, 0.9);
            for (auto& f : bp)
                f.c = c;
            lastFreq = p.freq;
        }

        const int n = buffer.getNumSamples();
        const int chs = juce::jmin (buffer.getNumChannels(), 2);
        float* d[2] = { nullptr, nullptr };
        for (int c = 0; c < chs; ++c)
            d[c] = buffer.getWritePointer (c);

        constexpr float ratio = 6.0f;
        float maxGr = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            float band[2] = { 0.0f, 0.0f };
            float peak = 0.0f;
            for (int c = 0; c < chs; ++c)
            {
                band[c] = bp[c].process (d[c][i]);
                peak = juce::jmax (peak, std::abs (band[c]));
            }

            const float level = juce::Decibels::gainToDecibels (peak, -100.0f);
            const float over = level - p.threshDb;
            const float target = over > 0.0f ? juce::jmin (p.rangeDb, over * (1.0f - 1.0f / ratio)) : 0.0f;
            gr = target > gr ? attackCoef * gr + (1.0f - attackCoef) * target
                             : releaseCoef * gr + (1.0f - releaseCoef) * target;

            const float g = juce::Decibels::decibelsToGain (-gr);
            for (int c = 0; c < chs; ++c)
                d[c][i] = p.listen ? band[c] : d[c][i] - band[c] * (1.0f - g);

            maxGr = juce::jmax (maxGr, gr);
        }

        if (gr < 1.0e-6f) gr = 0.0f;
        return maxGr;
    }

private:
    double sr = 44100.0;
    float attackCoef = 0.0f, releaseCoef = 0.0f, gr = 0.0f, lastFreq = -1.0f;
    Biquad bp[2];
};
