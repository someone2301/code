#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

struct CompParams
{
    int   mode = 0;                    // 0 = FET (1176-style), 1 = opto (LA-2A-style)
    float fetInDb = 8.0f, fetOutDb = -4.0f, fetAttackMs = 0.1f, fetReleaseMs = 200.0f;
    int   fetRatioIdx = 0;             // 4, 8, 12, 20, all-buttons
    float laPeak = 35.0f, laGainDb = 6.0f;
    bool  laLimit = false;
    float mix = 1.0f;                  // 0..1 parallel mix
};

// Stereo-linked compressor with two character modes. Returns the peak gain reduction (dB) of each block.
class Compressor
{
public:
    void prepare (double newSampleRate)
    {
        sr = newSampleRate;
        driveSmooth.reset (sr, 0.02);
        outSmooth.reset (sr, 0.02);
        mixSmooth.reset (sr, 0.02);
        driveSmooth.setCurrentAndTargetValue (1.0f);
        outSmooth.setCurrentAndTargetValue (1.0f);
        mixSmooth.setCurrentAndTargetValue (1.0f);
        reset();
    }

    void reset()
    {
        grFet = grFast = grSlow = avgGr = 0.0f;
    }

    float process (juce::AudioBuffer<float>& buffer, const CompParams& p)
    {
        const int n = buffer.getNumSamples();
        const int chs = juce::jmin (buffer.getNumChannels(), 2);
        float* d[2] = { nullptr, nullptr };
        for (int c = 0; c < chs; ++c)
            d[c] = buffer.getWritePointer (c);

        const bool fet = p.mode == 0;
        static constexpr float ratios[5] = { 4.0f, 8.0f, 12.0f, 20.0f, 32.0f };
        const int ratioIdx = juce::jlimit (0, 4, p.fetRatioIdx);
        const float fetRatio = ratios[ratioIdx];
        const bool allButtons = ratioIdx == 4;
        const float fetKnee = allButtons ? 3.0f : 1.0f;

        const float fetAtk = expCoef (juce::jmax (0.005f, p.fetAttackMs));
        const float fetRel = expCoef (juce::jmax (5.0f, p.fetReleaseMs));

        const float laThreshold = -2.0f - p.laPeak * 0.45f;
        const float laRatio = p.laLimit ? 10.0f : 3.0f;
        const float laAtkFast = expCoef (10.0f), laRelFast = expCoef (60.0f), laAtkSlow = expCoef (40.0f);
        const float laRelSlow = expCoef (500.0f + 3000.0f * juce::jlimit (0.0f, 1.0f, avgGr / 12.0f));
        const float avgCoef = expCoef (1000.0f);

        driveSmooth.setTargetValue (fet ? juce::Decibels::decibelsToGain (p.fetInDb) : 1.0f);
        outSmooth.setTargetValue (juce::Decibels::decibelsToGain (fet ? p.fetOutDb : p.laGainDb));
        mixSmooth.setTargetValue (juce::jlimit (0.0f, 1.0f, p.mix));

        float maxGr = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float drive = driveSmooth.getNextValue();
            const float outGain = outSmooth.getNextValue();
            const float mix = mixSmooth.getNextValue();

            float peak = 0.0f;
            for (int c = 0; c < chs; ++c)
                peak = juce::jmax (peak, std::abs (d[c][i]) * drive);
            const float level = juce::Decibels::gainToDecibels (peak, -100.0f);

            float grDb;
            if (fet)
            {
                const float target = gainComputer (level, -10.0f, fetRatio, fetKnee);
                grFet = target > grFet ? fetAtk * grFet + (1.0f - fetAtk) * target
                                       : fetRel * grFet + (1.0f - fetRel) * target;
                grDb = grFet;
            }
            else
            {
                const float target = gainComputer (level, laThreshold, laRatio, 12.0f);
                grFast = target > grFast ? laAtkFast * grFast + (1.0f - laAtkFast) * target
                                         : laRelFast * grFast + (1.0f - laRelFast) * target;
                grSlow = target > grSlow ? laAtkSlow * grSlow + (1.0f - laAtkSlow) * target
                                         : laRelSlow * grSlow + (1.0f - laRelSlow) * target;
                grDb = juce::jmax (grFast, grSlow * 0.7f);
                avgGr = avgCoef * avgGr + (1.0f - avgCoef) * grDb;
            }

            const float gain = juce::Decibels::decibelsToGain (-grDb);

            for (int c = 0; c < chs; ++c)
            {
                const float dry = d[c][i];
                float wet = dry * drive * gain * outGain;
                if (fet && allButtons)
                    wet = std::tanh (wet * 1.3f) / 1.3f;
                d[c][i] = dry + (wet - dry) * mix;
            }

            maxGr = juce::jmax (maxGr, grDb);
        }

        // Flush denormals out of the envelope state.
        if (grFet < 1.0e-6f) grFet = 0.0f;
        if (grFast < 1.0e-6f) grFast = 0.0f;
        if (grSlow < 1.0e-6f) grSlow = 0.0f;
        if (avgGr < 1.0e-6f) avgGr = 0.0f;

        return maxGr;
    }

private:
    static float gainComputer (float levelDb, float thresholdDb, float ratio, float kneeDb) noexcept
    {
        const float over = levelDb - thresholdDb;
        const float slope = 1.0f - 1.0f / ratio;
        const float knee = juce::jmax (kneeDb, 0.01f);
        if (2.0f * over < -knee)
            return 0.0f;
        if (2.0f * std::abs (over) <= knee)
        {
            const float x = over + knee * 0.5f;
            return slope * x * x / (2.0f * knee);
        }
        return slope * over;
    }

    float expCoef (float ms) const noexcept
    {
        return (float) std::exp (-1.0 / (ms * 0.001 * sr));
    }

    double sr = 44100.0;
    float grFet = 0.0f, grFast = 0.0f, grSlow = 0.0f, avgGr = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> driveSmooth, outSmooth, mixSmooth;
};
