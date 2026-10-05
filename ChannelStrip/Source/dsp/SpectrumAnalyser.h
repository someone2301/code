#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>

// Collects mono samples on the audio thread; the GUI pulls complete blocks for the FFT.
class SpectrumAnalyser
{
public:
    static constexpr int order = 11;
    static constexpr int size = 1 << order;

    void push (const juce::AudioBuffer<float>& buffer)
    {
        const int chs = buffer.getNumChannels();
        if (chs == 0)
            return;

        const float* l = buffer.getReadPointer (0);
        const float* r = buffer.getReadPointer (chs > 1 ? 1 : 0);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            fifo[(size_t) fifoIndex++] = 0.5f * (l[i] + r[i]);
            if (fifoIndex == size)
            {
                fifoIndex = 0;
                if (! ready.load())
                {
                    block = fifo;
                    ready.store (true);
                }
            }
        }
    }

    bool pull (std::array<float, size>& out)
    {
        if (! ready.load())
            return false;
        out = block;
        ready.store (false);
        return true;
    }

private:
    std::array<float, size> fifo {}, block {};
    int fifoIndex = 0;
    std::atomic<bool> ready { false };
};
