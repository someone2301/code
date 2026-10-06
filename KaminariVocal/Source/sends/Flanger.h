#pragma once

#include "DspCommon.h"
#include <atomic>

// Flanger send (DESIGN.md 2.14). Original implementation: a short delay (Delay, 0.1 .. 10 ms) swept upwards by an LFO
// (Depth, up to +6 ms) with feedback (positive or negative, soft-limited in the loop) and a high cut on the repeats.
// Left and right LFOs are offset by Stereo Phase. The rate runs free or locks to the host tempo.
//
// The return is 100 % wet. The flanging comb forms where the return meets the dry vocal, so it is deepest when the
// return is about as loud as the dry signal (send level near 0 dB) and gentler at lower send levels.
namespace kv
{
    struct FlangerSettings
    {
        float rateHz = 0.3f;
        double syncBeats = 0.0;    // > 0: one sweep per this many beats
        float depth = 0.6f;        // 0..1
        float delayMs = 1.5f;      // 0.1 .. 10
        float feedback = 0.4f;     // -0.95 .. 0.95
        float stereo = 0.5f;       // 0..1 = 0..180 degrees
        int shape = 1;             // 0 sine, 1 triangle
        float hiCutHz = 12000.0f;
        bool playing = false;
        double ppq = 0.0, bpm = 120.0;
    };

    class FlangerSend
    {
    public:
        static constexpr float maxSweepMs = 6.0f;

        void prepare (double sampleRate, int)
        {
            fs = (float) sampleRate;
            for (auto& d : line) d.prepare ((int) ((10.0f + maxSweepMs + 1.0f) * 0.001f * fs) + 8);
            delaySmooth.prepare (fs, 30.0f);
            depthSmooth.prepare (fs, 30.0f);
            reset();
        }

        void reset()
        {
            for (auto& d : line) d.reset();
            for (auto& f : damp) f.reset();
            fb[0] = fb[1] = 0.0f;
            phase = 0.0;
            primed = false;
        }

        // LFO position 0..1 of the left channel after the last block (GUI).
        std::atomic<float> lfoNow { 0.0f };

        void process (const float* inL, const float* inR, float* outL, float* outR, int n, const FlangerSettings& s)
        {
            if (! primed) { delaySmooth.snap (s.delayMs); depthSmooth.snap (s.depth); primed = true; }
            delaySmooth.setTarget (s.delayMs);
            depthSmooth.setTarget (s.depth);
            for (auto& f : damp) f.setCutoff (s.hiCutHz, fs);
            const float g = std::clamp (s.feedback, -0.95f, 0.95f);
            const double beatsPerSample = s.bpm / 60.0 / (double) fs;
            const float* in[2] = { inL, inR };
            float* out[2] = { outL, outR };
            float lastPh = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                double ph;
                if (s.syncBeats > 0.0 && s.playing)
                    ph = (s.ppq + (double) i * beatsPerSample) / s.syncBeats;
                else
                {
                    const double rate = s.syncBeats > 0.0 ? s.bpm / 60.0 / s.syncBeats : (double) s.rateHz;
                    phase += rate / (double) fs;
                    phase -= std::floor (phase);
                    ph = phase;
                }
                const float base = delaySmooth.next(), depth = depthSmooth.next();
                for (int c = 0; c < 2; ++c)
                {
                    const float p = (float) (ph - std::floor (ph)) + (c == 1 ? 0.5f * s.stereo : 0.0f);
                    const float lfo = 0.5f + 0.5f * lfoShape (s.shape, p - 0.25f);   // 0..1, starts at the bottom
                    const float d = (base + maxSweepMs * depth * lfo) * 0.001f * fs;
                    const float y = line[c].read (d - 1.0f);   // read before this sample's push: d - 1 gives a delay of d
                    // feedback is soft-limited and damped so high settings ring but never run away
                    fb[c] = damp[c].process (y);
                    line[c].push (in[c][i] + softClip (g * fb[c]));
                    out[c][i] = fb[c];
                }
                lastPh = (float) (ph - std::floor (ph));
            }
            lfoNow.store (lastPh, std::memory_order_relaxed);
        }

    private:
        float fs = 48000.0f;
        DelayLine line[2];
        OnePoleLP damp[2];
        float fb[2] { 0, 0 };
        double phase = 0.0;
        Smoother delaySmooth, depthSmooth;
        bool primed = false;
    };
}
