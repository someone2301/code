#pragma once

#include "DspCommon.h"
#include "../dsp/Dynamics.h"   // toDb, coeffMs
#include <algorithm>
#include <cmath>

// Return processing for the Reverb and Delay sends (DESIGN.md 2.9.6): a four-band EQ on the wet signal and ducking
// keyed by the vocal.
namespace kv
{
    // Band 1 is a 12 dB/oct high pass, bands 2 and 3 are bells, band 4 is a 12 dB/oct low pass.
    struct ReturnEqSettings
    {
        static constexpr int numBands = 4;
        enum Kind { HighPass, Bell1, Bell2, LowPass };
        struct Band { bool on = false; float freq = 1000.0f, gainDb = 0.0f, q = 1.0f; };
        Band band[numBands];
        int solo = -1;   // audition one band's region (-1: off)

        bool anyOn() const noexcept { for (auto& b : band) if (b.on) return true; return false; }
        static bool hasGain (int k) noexcept { return k == Bell1 || k == Bell2; }
    };

    class ReturnEq
    {
    public:
        static constexpr int sub = 32;   // coefficients follow the smoothed settings every 32 samples

        void prepare (double sampleRate) { fs = (float) sampleRate; reset(); }
        void reset()
        {
            for (auto& ch : f) for (auto& b : ch) b.reset();
            for (auto& s : soloF) s.reset();
            primed = false;
        }

        static Biquad design (int k, const ReturnEqSettings::Band& b, float fs)
        {
            Biquad q;
            switch (k)
            {
                case ReturnEqSettings::HighPass: q.set (Biquad::HighPass, fs, b.freq, 0.7071f * std::clamp (b.q, 0.5f, 4.0f)); break;
                case ReturnEqSettings::LowPass:  q.set (Biquad::LowPass,  fs, b.freq, 0.7071f * std::clamp (b.q, 0.5f, 4.0f)); break;
                default:                         q.set (Biquad::Peak,     fs, b.freq, std::clamp (b.q, 0.1f, 18.0f), b.gainDb); break;
            }
            return q;
        }

        // Region a band works on, for solo: below a high pass, above a low pass, around a bell.
        static Biquad soloDesign (int k, const ReturnEqSettings::Band& b, float fs)
        {
            Biquad q;
            if (k == ReturnEqSettings::HighPass)     q.set (Biquad::LowPass, fs, b.freq, 0.7071f);
            else if (k == ReturnEqSettings::LowPass) q.set (Biquad::HighPass, fs, b.freq, 0.7071f);
            else                                     q.set (Biquad::BandPass, fs, b.freq, std::max (0.3f, b.q));
            return q;
        }

        void process (float* l, float* r, int n, const ReturnEqSettings& s)
        {
            if (! primed) { cur = s; primed = true; }
            const float a = std::exp (-(float) sub / (0.02f * fs));   // ~20 ms glide for frequency and gain
            for (int start = 0; start < n; start += sub)
            {
                const int m = std::min (sub, n - start);
                for (int k = 0; k < ReturnEqSettings::numBands; ++k)
                {
                    auto& c = cur.band[k];
                    const auto& t = s.band[k];
                    if (t.on && ! c.on) { c = t; for (auto& ch : f) ch[k].reset(); }   // a band switched on starts fresh
                    c.on = t.on;
                    c.freq = std::exp (std::log (t.freq) + a * (std::log (c.freq) - std::log (t.freq)));
                    c.gainDb = t.gainDb + a * (c.gainDb - t.gainDb);
                    c.q = t.q + a * (c.q - t.q);
                    if (! c.on) continue;
                    const auto d = design (k, c, fs);
                    for (auto& ch : f) { auto& b = ch[k]; b.b0 = d.b0; b.b1 = d.b1; b.b2 = d.b2; b.a1 = d.a1; b.a2 = d.a2; }
                }
                const int solo = s.solo >= 0 && s.solo < ReturnEqSettings::numBands ? s.solo : -1;
                if (solo != lastSolo) { for (auto& q : soloF) q.reset(); lastSolo = solo; }
                if (solo >= 0)
                {
                    const auto d = soloDesign (solo, cur.band[solo], fs);
                    for (auto& b : soloF) { b.b0 = d.b0; b.b1 = d.b1; b.b2 = d.b2; b.a1 = d.a1; b.a2 = d.a2; }
                }
                for (int i = start; i < start + m; ++i)
                {
                    float x[2] = { l[i], r[i] };
                    for (int c = 0; c < 2; ++c)
                    {
                        float y = x[c];
                        if (solo >= 0)
                            y = soloF[c].process (y);
                        else
                            for (int k = 0; k < ReturnEqSettings::numBands; ++k)
                                if (cur.band[k].on) y = f[c][k].process (y);
                        x[c] = y;
                    }
                    l[i] = x[0]; r[i] = x[1];
                }
            }
        }

    private:
        float fs = 48000.0f;
        Biquad f[2][ReturnEqSettings::numBands];
        Biquad soloF[2];
        ReturnEqSettings cur;
        bool primed = false;
        int lastSolo = -1;
    };

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
