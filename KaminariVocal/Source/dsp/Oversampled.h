#pragma once

#include <juce_dsp/juce_dsp.h>
#include <memory>

// Runs a stereo processor at 1x, 2x or 4x the session rate (Off / 2x / 4x).
//
// One instance of the processor is prepared for each rate, so switching never allocates on the audio thread; the
// newly selected instance and its filters start from a clean state. Up- and down-sampling use JUCE's linear-phase
// half-band FIR filters with an integer latency (latencyFor), which the plug-in adds to its reported latency while
// the module is on. Proc needs prepare (double sampleRate), reset() and float process (float*, float*, int, Settings).
namespace kv
{
    template <class Proc>
    class Oversampled
    {
    public:
        void prepare (double sampleRate, int maxBlock)
        {
            for (int k = 0; k < 3; ++k)
                procs[k].prepare (sampleRate * (double) (1 << k));
            for (int k = 0; k < 2; ++k)
            {
                os[k] = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) (k + 1),
                                                                           juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                                                                           true, true);
                os[k]->initProcessing ((size_t) maxBlock);
                latency[k] = (int) std::lround (os[k]->getLatencyInSamples());
            }
            current = 0;
        }

        void reset()
        {
            for (auto& p : procs) p.reset();
            for (auto& o : os) if (o) o->reset();
        }

        // Extra latency in samples for an oversampling choice (0 = Off).
        int latencyFor (int factorIndex) const noexcept { return factorIndex <= 0 ? 0 : latency[std::min (factorIndex, 2) - 1]; }

        // The instance that is running (the GUI reads its read-outs).
        Proc& active() noexcept { return procs[current]; }
        const Proc& active() const noexcept { return procs[current]; }
        int factorIndex() const noexcept { return current; }

        // factorIndex: 0 Off, 1 = 2x, 2 = 4x. settingsFor (factor) returns the settings for the running rate
        // (for sample counts such as lookahead that scale with it).
        template <class SettingsFn>
        float process (float* l, float* r, int n, int factorIndex, SettingsFn&& settingsFor)
        {
            factorIndex = std::clamp (factorIndex, 0, 2);
            if (factorIndex != current)
            {
                procs[factorIndex].reset();
                if (factorIndex > 0) os[factorIndex - 1]->reset();
                current = factorIndex;
            }
            if (current == 0)
                return procs[0].process (l, r, n, settingsFor (1));
            float* ch[2] = { l, r };
            juce::dsp::AudioBlock<float> block (ch, 2, (size_t) n);
            auto up = os[current - 1]->processSamplesUp (block);
            const float result = procs[current].process (up.getChannelPointer (0), up.getChannelPointer (1), (int) up.getNumSamples(),
                                                         settingsFor (1 << current));
            os[current - 1]->processSamplesDown (block);
            return result;
        }

    private:
        Proc procs[3];
        std::unique_ptr<juce::dsp::Oversampling<float>> os[2];
        int latency[2] { 0, 0 };
        int current = 0;
    };
}
