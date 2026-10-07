#pragma once

#include "DspCommon.h"
#include "../dsp/Dynamics.h"   // toDb, coeffMs
#include <algorithm>
#include <cmath>

// Return processing for the Reverb and Delay sends (DESIGN.md 2.9.6): ducking keyed by the vocal. (Their EQ is the
// main EQ's kv::Equalizer with its own bands.)
namespace kv
{
    struct DuckSettings
    {
        enum Source { Vocal, RawInput };
        bool on = false;
        float threshDb = -30.0f, depthDb = 9.0f, attackMs = 10.0f, releaseMs = 250.0f;
        int source = Vocal;
        float wetGainDb = 0.0f;   // after ducking: brings the return level back up
    };

    // Lowers a return while the key (the vocal) is above the threshold. Full depth is reached 6 dB above it.
    class Ducker
    {
    public:
        void prepare (double sampleRate) { fs = (float) sampleRate; wet.prepare (fs, 30.0f); reset(); }
        void reset() { env = 0; gr = 0; wet.snap (wetTarget); }

        // Returns the largest reduction in this block (dB).
        float process (float* l, float* r, int n, const float* keyL, const float* keyR, const DuckSettings& s)
        {
            wetTarget = s.wetGainDb;
            wet.setTarget (s.wetGainDb);
            const float aC = coeffMs (std::max (0.1f, s.attackMs), fs), rC = coeffMs (std::max (1.0f, s.releaseMs), fs);
            const float envA = coeffMs (0.5f, fs), envR = coeffMs (40.0f, fs);
            float maxGr = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                float target = 0.0f;
                if (s.on)
                {
                    const float k = std::max (std::abs (keyL[i]), std::abs (keyR[i]));
                    env = k > env ? k + envA * (env - k) : k + envR * (env - k);
                    const float over = toDb (env) - s.threshDb;
                    target = s.depthDb * std::clamp (over / 6.0f, 0.0f, 1.0f);
                }
                else env = 0;
                gr = target + (target > gr ? aC : rC) * (gr - target);
                maxGr = std::max (maxGr, gr);
                const float g = dbToGain (wet.next() - gr);
                l[i] *= g; r[i] *= g;
            }
            return maxGr;
        }

    private:
        float fs = 48000.0f, env = 0, gr = 0, wetTarget = 0;
        Smoother wet;
    };
}
