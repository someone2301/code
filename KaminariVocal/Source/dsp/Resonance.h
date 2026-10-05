#pragma once

#include "Eq.h"
#include <atomic>

// Resonance suppressor (DESIGN.md 2.7), low-latency mode: a bank of band detectors drives a cascade of
// dynamic peaking cuts at the same centres. Soft mode compares each band with its neighbourhood (adaptive
// threshold), Hard mode with the overall level. Zero latency. Original implementation.
namespace kv
{
    struct ResonanceSettings
    {
        int mode = 0;                       // 0 Soft, 1 Hard
        float depth = 4, detail = 0.5f, attack = 0.5f, release = 0.5f;
        float mix = 1.0f, outGainDb = 0, wetTrimDb = 0, maxCutDb = 41;   // 41 = Off
        bool delta = false, bypass = false;
        int quality = 0;                    // 0 Normal (1/3 oct), 1 High (1/4), 2 Ultra (1/6)
        int stereoMode = 1;                 // 0 L/R, 1 M/S
        float link = 1.0f, focus = 0.0f;
        float detailTiltLo = 0, detailTiltHi = 0, attackTiltLo = 0, attackTiltHi = 0, releaseTiltLo = 0, releaseTiltHi = 0;
        struct Band { bool used = false, on = true; int shape = 4; float freq = 1000, depthDb = 0, q = 1; } curve[8];
    };

    class Resonance
    {
    public:
        static constexpr int maxBands = 64;

        void prepare (double sampleRate)
        {
            fs = (float) sampleRate;
            builtQuality = -1;
            reset();
        }

        void reset()
        {
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < maxBands; ++k) { det[c][k].reset(); cut[c][k].reset(); env[c][k] = 0; red[c][k] = 0; }
        }

        int numBands() const noexcept { return nb; }
        float bandFrequency (int k) const noexcept { return centre[k]; }
        float bandReduction (int k) const noexcept { return shownReduction[k].load (std::memory_order_relaxed); }

        float process (float* l, float* r, int n, const ResonanceSettings& s)
        {
            if (s.quality != builtQuality) build (s.quality);
            computeCurve (s);
            const float depthAmt = std::clamp (s.depth, 0.0f, 20.0f) / 20.0f * 1.5f;
            // Depth also caps the deepest cut (2 dB per Depth step: up to 40 dB at Depth 20); Max Cut can lower it further
            const float depthCap = 2.0f * std::clamp (s.depth, 0.0f, 20.0f);
            const float maxCut = std::min (depthCap, s.maxCutDb >= 40.5f ? 40.0f : std::max (0.0f, s.maxCutDb));
            const float wetTrim = dbToGain (s.wetTrimDb), outGain = dbToGain (s.outGainDb);
            const float focus = std::clamp (s.focus, -1.0f, 1.0f);
            const float chDepth[2] = { depthAmt * (1.0f - std::max (0.0f, focus)), depthAmt * (1.0f + std::min (0.0f, focus)) };
            float meter = 0.0f;

            for (int start = 0; start < n; start += 32)
            {
                const int m = std::min (32, n - start);
                // detectors (input of the block, before the cuts)
                for (int i = 0; i < m; ++i)
                {
                    float a = l[start + i], b = r[start + i];
                    if (s.stereoMode == 1) { const float mm = 0.5f * (a + b), sd = 0.5f * (a - b); a = mm; b = sd; }
                    const float in[2] = { a, b };
                    for (int c = 0; c < 2; ++c)
                        for (int k = 0; k < nb; ++k)
                        {
                            const float v = det[c][k].process (in[c]);
                            env[c][k] = v * v + envC[k] * (env[c][k] - v * v);
                        }
                }
                // targets and smoothing, once per 32 samples
                float lvl[2][maxBands];
                for (int c = 0; c < 2; ++c)
                    for (int k = 0; k < nb; ++k)
                        lvl[c][k] = 10.0f * std::log10 (env[c][k] + 1e-14f);
                float global[2] = { 0, 0 };
                for (int c = 0; c < 2; ++c) { for (int k = 0; k < nb; ++k) global[c] += lvl[c][k]; global[c] /= (float) nb; }
                const int win = std::max (2, (int) std::round (1.0f / spacing));   // neighbourhood of about +-1 octave
                for (int c = 0; c < 2; ++c)
                {
                    for (int k = 0; k < nb; ++k)
                    {
                        const float own = lvl[c][k], linked = std::max (lvl[0][k], lvl[1][k]);
                        const float level = own + (linked - own) * s.link;
                        float ref;
                        if (s.mode == 0)
                        {
                            float sum = 0; int cnt = 0;
                            for (int j = std::max (0, k - win); j <= std::min (nb - 1, k + win); ++j)
                                if (std::abs (j - k) > 1)
                                {
                                    // same linking as the band's own level, so a silent channel cannot pull the reference down
                                    const float linkedJ = std::max (lvl[0][j], lvl[1][j]);
                                    sum += lvl[c][j] + (linkedJ - lvl[c][j]) * s.link;
                                    ++cnt;
                                }
                            ref = cnt > 0 ? sum / (float) cnt : level;
                        }
                        else ref = std::max (global[0], global[1]) + (global[c] - std::max (global[0], global[1])) * (1.0f - s.link) + 6.0f;
                        const float detail = std::clamp (s.detail + tiltFor (centre[k], s.detailTiltLo, s.detailTiltHi) * 0.5f, 0.0f, 1.0f);
                        const float margin = 10.0f - 7.0f * detail;
                        const float excess = level - ref - margin;
                        const float target = level < -90.0f ? 0.0f : std::min (maxCut, std::max (0.0f, excess) * chDepth[c] * curveMult[k]);
                        const float aMs = (1.0f + 39.0f * s.attack) * timeScale[k] * std::pow (2.0f, tiltFor (centre[k], s.attackTiltLo, s.attackTiltHi));
                        const float rMs = (20.0f + 380.0f * s.release) * timeScale[k] * std::pow (2.0f, tiltFor (centre[k], s.releaseTiltLo, s.releaseTiltHi));
                        const float coef = std::exp (-(float) m / (0.001f * (target > red[c][k] ? aMs : rMs) * fs));
                        red[c][k] = target + coef * (red[c][k] - target);
                        meter = std::max (meter, red[c][k]);
                        const float q = qBase * (0.8f + 1.2f * detail);
                        cut[c][k].set (Biquad::Peak, fs, centre[k], q, -red[c][k]);
                    }
                }
                for (int k = 0; k < nb; ++k)
                    shownReduction[k].store (std::max (red[0][k], red[1][k]), std::memory_order_relaxed);

                // apply the cuts
                for (int i = 0; i < m; ++i)
                {
                    const float dryL = l[start + i], dryR = r[start + i];
                    float a = dryL, b = dryR;
                    if (s.stereoMode == 1) { const float mm = 0.5f * (a + b), sd = 0.5f * (a - b); a = mm; b = sd; }
                    for (int k = 0; k < nb; ++k) { a = cut[0][k].process (a); b = cut[1][k].process (b); }
                    if (s.stereoMode == 1) { const float mm = a, sd = b; a = mm + sd; b = mm - sd; }
                    float wl = a * wetTrim, wr = b * wetTrim;
                    float yl = dryL + (wl - dryL) * s.mix, yr = dryR + (wr - dryR) * s.mix;
                    if (s.delta) { yl = dryL - yl; yr = dryR - yr; }
                    if (s.bypass) { yl = dryL; yr = dryR; }
                    l[start + i] = flushDenormal (yl * outGain);
                    r[start + i] = flushDenormal (yr * outGain);
                }
            }
            return meter;
        }

    private:
        static float tiltFor (float f, float lo, float hi) noexcept
        {
            if (f < 500.0f) return lo / 100.0f;
            if (f > 2000.0f) return hi / 100.0f;
            return 0.0f;
        }

        void build (int quality)
        {
            builtQuality = quality;
            spacing = quality == 2 ? 1.0f / 6.0f : (quality == 1 ? 0.25f : 1.0f / 3.0f);
            qBase = 1.0f / (std::pow (2.0f, spacing * 0.5f) - std::pow (2.0f, -spacing * 0.5f));
            nb = 0;
            const float top = std::min (18000.0f, 0.43f * fs);
            for (float f = 30.0f; f < top && nb < maxBands; f *= std::pow (2.0f, spacing))
            {
                centre[nb] = f;
                timeScale[nb] = std::clamp (std::sqrt (1000.0f / f), 0.4f, 2.5f);
                envC[nb] = std::exp (-1.0f / (0.003f * timeScale[nb] * fs));
                for (int c = 0; c < 2; ++c) det[c][nb].set (Biquad::BandPass, fs, f, qBase);
                ++nb;
            }
            reset();
        }

        void computeCurve (const ResonanceSettings& s)
        {
            static const int shapeToType[8] = { LowCut, LowShelf, HighShelf, HighCut, Bell, BandPass, Notch, TiltShelf };
            for (int k = 0; k < nb; ++k)
            {
                double db = 0.0;
                for (auto& b : s.curve)
                {
                    if (! b.used || ! b.on) continue;
                    EqBandSettings e;
                    e.type = shapeToType[std::clamp (b.shape, 0, 7)];
                    e.freq = b.freq; e.gainDb = b.depthDb; e.q = std::clamp (b.q, 0.1f, 10.0f); e.slopeIndex = 1;
                    double m = EqDesign::make (e, fs).magnitudeDb (centre[k], fs);
                    if (e.type == BandPass) m = std::max (-24.0, m) + b.depthDb;   // bandpass: depth inside the band only
                    db += m;
                }
                curveMult[k] = (float) std::clamp (std::pow (10.0, db / 20.0), 0.0, 4.0);
            }
        }

        float fs = 48000.0f, spacing = 1.0f / 3.0f, qBase = 4.3f;
        int nb = 0, builtQuality = -1;
        float centre[maxBands] {}, timeScale[maxBands] {}, envC[maxBands] {}, curveMult[maxBands] {};
        Biquad det[2][maxBands], cut[2][maxBands];
        float env[2][maxBands] {}, red[2][maxBands] {};
        std::atomic<float> shownReduction[maxBands] {};
    };
}
