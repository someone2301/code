#pragma once

#include "../sends/DspCommon.h"

// Compression, De-ess and Multiband (DESIGN.md 2.4-2.6). Original implementations.
namespace kv
{
    inline float toDb (float x) noexcept { return 20.0f * std::log10 (std::max (x, 1.0e-9f)); }

    // Static curve with a soft knee: gain reduction (dB, >= 0) for a detector level.
    inline float downwardGr (float levelDb, float threshDb, float ratio, float kneeDb) noexcept
    {
        const float over = levelDb - threshDb;
        const float slope = 1.0f - 1.0f / std::max (1.0f, ratio);
        if (kneeDb > 0.01f && std::abs (over) < 0.5f * kneeDb)
        {
            const float x = over + 0.5f * kneeDb;
            return slope * x * x / (2.0f * kneeDb);
        }
        return over > 0.0f ? slope * over : 0.0f;
    }

    inline float coeffMs (float ms, float fs) noexcept { return ms <= 0.0f ? 0.0f : std::exp (-1.0f / (0.001f * ms * fs)); }

    //==================================================================================================================
    struct CompressorSettings
    {
        enum Style { Clean, Vocal, Opto, Classic, Punch };
        int style = Clean;
        float threshDb = -14, ratio = 3, attackMs = 8, releaseMs = 150, kneeDb = 8, rangeDb = 15;
        bool autoRelease = true, smoothDetector = true, autoGain = true;
        float holdMs = 0, mix = 1.0f, wetGainDb = 0, dryDb = -60, scLevelDb = 0, outGainDb = 0, stereoLink = 1.0f;
        int lookaheadSamples = 0;
    };

    class Compressor
    {
    public:
        void prepare (double sampleRate)
        {
            fs = (float) sampleRate;
            for (auto& d : look) d.prepare ((int) (0.021 * fs) + 4);
            makeup.prepare (fs, 200.0f);
            reset();
        }

        void reset()
        {
            for (auto& d : look) d.reset();
            gr = 0; env[0] = env[1] = 0; hold = 0; slow = 0; lastOut[0] = lastOut[1] = 0;
            rms[0] = rms[1] = 0;
            makeup.snap (0.0f);
        }

        // Static makeup used by Auto Gain (DESIGN.md 2.4): half the reduction a 0 dBFS signal gets, at most 12 dB.
        static float staticMakeup (const CompressorSettings& s)
        {
            const float g = std::min (s.rangeDb, downwardGr (0.0f, s.threshDb, s.ratio, s.kneeDb));
            return std::min (12.0f, 0.5f * g);
        }

        float process (float* l, float* r, int n, const CompressorSettings& s)
        {
            const float ratioBase = s.ratio;
            float knee = s.kneeDb, attack = s.attackMs, release = s.releaseMs;
            switch (s.style)
            {
                case CompressorSettings::Vocal: knee = std::max (knee, 12.0f); break;
                case CompressorSettings::Opto:  knee = std::max (knee, 18.0f); attack *= 3.0f; release *= 1.5f; break;
                case CompressorSettings::Punch: attack = std::max (attack, 5.0f); break;
                default: break;
            }
            const float aC = coeffMs (attack, fs), rC = coeffMs (release, fs);
            const float rmsC = coeffMs (10.0f, fs);
            const float holdSamples = s.holdMs * 0.001f * fs;
            const float sc = dbToGain (s.scLevelDb);
            const float wetGain = dbToGain (s.wetGainDb);
            const float dryGain = s.dryDb <= -60.0f ? 0.0f : dbToGain (s.dryDb);
            const float outGain = dbToGain (s.outGainDb);
            makeup.setTarget (s.autoGain ? staticMakeup (s) : 0.0f);
            const int la = std::clamp (s.lookaheadSamples, 0, look[0].capacity() - 2);
            float maxGr = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                float x[2] = { l[i], r[i] };
                float det[2];
                for (int c = 0; c < 2; ++c)
                {
                    const float src = (s.style == CompressorSettings::Classic ? lastOut[c] : x[c]) * sc;   // Classic = feedback
                    if (s.smoothDetector)
                    {
                        rms[c] = src * src + rmsC * (rms[c] - src * src);
                        det[c] = toDb (std::sqrt (rms[c]) * 1.4142f);
                    }
                    else det[c] = toDb (std::abs (src));
                }
                const float linked = std::max (det[0], det[1]);
                const float level = linked;   // stereo link: one gain for both channels at 100 %
                float ratio = ratioBase;
                if (s.style == CompressorSettings::Vocal)   // automatic ratio: grows with the overshoot
                    ratio = std::clamp (2.0f + std::max (0.0f, level - s.threshDb) / 6.0f, 2.0f, 8.0f);

                float target = std::min (s.rangeDb, downwardGr (level, s.threshDb, ratio, knee));
                if (s.stereoLink < 1.0f)
                {
                    // partially unlinked: blend towards the louder channel's own reduction (applied to both, kept simple)
                    const float own = std::min (s.rangeDb, downwardGr (std::min (det[0], det[1]), s.threshDb, ratio, knee));
                    target = own + (target - own) * s.stereoLink;
                }

                if (target > gr)
                {
                    gr = target + aC * (gr - target);
                    hold = holdSamples;
                }
                else if (hold > 0.0f)
                {
                    hold -= 1.0f;
                }
                else
                {
                    float rel = rC;
                    if (s.autoRelease)
                    {
                        // program dependent: short peaks recover fast, sustained reduction recovers slowly
                        slow = target + coeffMs (400.0f, fs) * (slow - target);
                        rel = coeffMs (release * (slow > 3.0f ? 1.6f : 0.6f), fs);
                    }
                    gr = target + rel * (gr - target);
                }
                maxGr = std::max (maxGr, gr);

                const float g = dbToGain (-gr + makeup.next());
                for (int c = 0; c < 2; ++c)
                {
                    look[c].push (x[c]);
                    const float delayed = la > 0 ? look[c].readInt (la) : x[c];
                    float wet = delayed * g * wetGain;
                    if (s.style == CompressorSettings::Punch)
                        wet = softClip (wet * 1.2f) / 1.2f;
                    lastOut[c] = wet;
                    // Mix 0..200 %: above 100 % pushes past the compressed signal
                    float y = delayed + (wet - delayed) * s.mix + delayed * dryGain;
                    (c == 0 ? l : r)[i] = flushDenormal (y * outGain);
                }
            }
            return maxGr;
        }

        float currentMakeup() const noexcept { return makeup.value; }

    private:
        float fs = 48000.0f;
        DelayLine look[2];
        Smoother makeup;
        float gr = 0, env[2] {}, hold = 0, slow = 0, lastOut[2] {}, rms[2] {};
    };

    //==================================================================================================================
    struct DeEsserSettings
    {
        float threshDb = -28, rangeDb = 8, detLo = 3500, detHi = 8600;
        bool fullBand = false, wideband = false, allround = false;
        float stereoLink = 1.0f;
        int linkMode = 0;          // 0 Stereo, 1 Mid, 2 Side
        int lookaheadSamples = 0;
        bool listen = false, audition = false;
    };

    class DeEsser
    {
    public:
        void prepare (double sampleRate)
        {
            fs = (float) sampleRate;
            for (auto& d : look) d.prepare ((int) (0.016 * fs) + 4);
            reset();
        }

        void reset()
        {
            for (auto& d : look) d.reset();
            for (int c = 0; c < 2; ++c) { hp[c].reset(); lp[c].reset(); split[c].reset(); env[c] = 0; gr[c] = 0; }
        }

        float process (float* l, float* r, int n, const DeEsserSettings& s)
        {
            const float lo = std::clamp (s.detLo, 1000.0f, 0.45f * fs), hi = std::clamp (std::max (s.detHi, lo * 1.2f), 1200.0f, 0.45f * fs);
            for (int c = 0; c < 2; ++c)
            {
                hp[c].set (Biquad::HighPass, fs, lo, 0.707f);
                lp[c].set (Biquad::LowPass, fs, hi, 0.707f);
                split[c].set (Biquad::LowPass, fs, lo, 0.707f);   // split follows the detector's low edge
            }
            const float aC = coeffMs (0.3f, fs);
            const float relFast = coeffMs (s.allround ? 60.0f : 30.0f, fs), relSlow = coeffMs (s.allround ? 150.0f : 80.0f, fs);
            const int la = std::clamp (s.lookaheadSamples, 0, look[0].capacity() - 2);
            float maxGr = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                // work in L/R, or on mid or side only
                float a = l[i], b = r[i];
                if (s.linkMode != 0) { const float m = 0.5f * (a + b), sd = 0.5f * (a - b); a = s.linkMode == 1 ? m : sd; b = s.linkMode == 1 ? sd : m; }
                float in[2] = { a, b };
                float lvl[2], detSig[2];
                for (int c = 0; c < 2; ++c)
                {
                    detSig[c] = s.fullBand ? hp[c].process (in[c]) : lp[c].process (hp[c].process (in[c]));
                    const float x = std::abs (detSig[c]);
                    env[c] = x > env[c] ? x + aC * (env[c] - x) : x + (env[c] > 0.05f ? relSlow : relFast) * (env[c] - x);
                    lvl[c] = toDb (env[c]);
                }
                const float linked = std::max (lvl[0], lvl[1]);
                float out[2];
                for (int c = 0; c < 2; ++c)
                {
                    const bool processed = s.linkMode == 0 || c == 0;   // in Mid/Side mode only the chosen part is treated
                    const float level = linked + (lvl[c] - linked) * (1.0f - s.stereoLink);
                    const float target = processed ? std::min (s.rangeDb, std::max (0.0f, level - s.threshDb) * (5.0f / 6.0f)) : 0.0f;
                    gr[c] = target;   // envelope already smooths; the reduction follows it directly
                    maxGr = std::max (maxGr, gr[c]);
                    look[c].push (in[c]);
                    const float x = la > 0 ? look[c].readInt (la) : in[c];
                    const float g = dbToGain (-gr[c]);
                    float y;
                    if (gr[c] <= 0.0f) { y = x; split[c].process (x); }   // no reduction: exact pass-through
                    else if (s.wideband) y = x * g;
                    else
                    {
                        const float low = split[c].process (x);
                        y = low + (x - low) * g;   // complementary split: low + high = x
                    }
                    if (s.audition) y = x - y;      // hear only what is removed
                    if (s.listen) y = detSig[c];    // hear the detector signal
                    out[c] = y;
                }
                if (s.linkMode != 0)
                {
                    const float m = s.linkMode == 1 ? out[0] : out[1], sd = s.linkMode == 1 ? out[1] : out[0];
                    out[0] = m + sd; out[1] = m - sd;
                }
                l[i] = flushDenormal (out[0]);
                r[i] = flushDenormal (out[1]);
            }
            return maxGr;
        }

    private:
        float fs = 48000.0f;
        DelayLine look[2];
        Biquad hp[2], lp[2], split[2];
        float env[2] {}, gr[2] {};
    };

    //==================================================================================================================
    struct MultibandBandSettings
    {
        float lo = 100, hi = 500, threshDb = -24, ratio = 2, attackMs = 10, releaseMs = 150, kneeDb = 6, rangeDb = -6, gainDb = 0;
        bool expand = false, solo = false;
    };

    struct MultibandSettings
    {
        int count = 1;
        int slopeIndex = 1;   // 6, 12, 24 dB/oct
        bool smoothDetector = true;
        MultibandBandSettings band[6];
    };

    class Multiband
    {
    public:
        void prepare (double sampleRate)
        {
            fs = (float) sampleRate;
            reset();
        }

        void reset()
        {
            for (auto& b : bands)
            {
                for (auto& c : b.hp) for (auto& f : c) f.reset();
                for (auto& c : b.lp) for (auto& f : c) f.reset();
                for (auto& f : b.hp1) f.reset();
                for (auto& f : b.lp1) f.reset();
                b.env[0] = b.env[1] = 0; b.gainDb = 0;
            }
        }

        // Returns the largest gain change in dB (positive = reduction, negative = boost) for the GR meter.
        float process (float* l, float* r, int n, const MultibandSettings& s)
        {
            const int count = std::clamp (s.count, 1, 6);
            const int stages = s.slopeIndex == 2 ? 2 : 1;
            bool anySolo = false;
            for (int k = 0; k < count; ++k) anySolo = anySolo || s.band[k].solo;
            for (int k = 0; k < count; ++k)
            {
                auto& b = bands[k];
                const float lo = std::clamp (s.band[k].lo, 20.0f, 0.4f * fs);
                const float hi = std::clamp (std::max (s.band[k].hi, lo * 1.5f), 30.0f, 0.45f * fs);
                for (int c = 0; c < 2; ++c)
                {
                    for (int st = 0; st < 2; ++st)
                    {
                        b.hp[c][st].set (Biquad::HighPass, fs, lo, 0.7071f);
                        b.lp[c][st].set (Biquad::LowPass, fs, hi, 0.7071f);
                    }
                    b.hp1[c].setCutoff (lo, fs);
                    b.lp1[c].setCutoff (hi, fs);
                }
                b.ac = coeffMs (s.band[k].attackMs, fs);
                b.rc = coeffMs (s.band[k].releaseMs, fs);
            }
            float meter = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float x[2] = { l[i], r[i] };
                float y[2] = { anySolo ? 0.0f : x[0], anySolo ? 0.0f : x[1] };
                for (int k = 0; k < count; ++k)
                {
                    auto& b = bands[k];
                    const auto& bs = s.band[k];
                    float band[2];
                    for (int c = 0; c < 2; ++c)
                    {
                        float v = x[c];
                        if (s.slopeIndex == 0) v = b.lp1[c].process (b.hp1[c].process (v));   // 6 dB/oct
                        else for (int st = 0; st < stages; ++st) v = b.lp[c][st].process (b.hp[c][st].process (v));
                        band[c] = v;
                        const float a = s.smoothDetector ? v * v : std::abs (v);
                        b.env[c] = a > b.env[c] ? a + b.ac * (b.env[c] - a) : a + b.rc * (b.env[c] - a);
                    }
                    const float e = std::max (b.env[0], b.env[1]);
                    const float level = s.smoothDetector ? 10.0f * std::log10 (std::max (e * 2.0f, 1e-18f)) : toDb (e);
                    // gain change in dB: negative range = downward, positive = upward
                    const float ratio = std::max (1.0f, bs.ratio);
                    float change = 0.0f;
                    const float above = level - bs.threshDb, below = bs.threshDb - level;
                    const float softAbove = above + 0.5f * bs.kneeDb > 0 ? std::max (above, 0.0f) : 0.0f;
                    if (! bs.expand)
                        change = bs.rangeDb < 0 ? -std::min (-bs.rangeDb, softAbove * (1.0f - 1.0f / ratio))
                                                : std::min (bs.rangeDb, std::max (0.0f, below) * (1.0f - 1.0f / ratio));
                    else
                        change = bs.rangeDb < 0 ? -std::min (-bs.rangeDb, std::max (0.0f, below) * (ratio - 1.0f))
                                                : std::min (bs.rangeDb, std::max (0.0f, above) * (ratio - 1.0f));
                    b.gainDb = change;
                    if (std::abs (change) > std::abs (meter)) meter = change;
                    const float g = dbToGain (change + bs.gainDb);
                    for (int c = 0; c < 2; ++c)
                    {
                        if (anySolo) { if (bs.solo) y[c] += band[c] * g; }
                        else y[c] += band[c] * (g - 1.0f);   // x - band * (1 - g): exact at 0 dB
                    }
                }
                l[i] = flushDenormal (y[0]);
                r[i] = flushDenormal (y[1]);
            }
            return -meter;
        }

        // Current gain change of band k in dB (negative = reduction), for the GUI.
        float bandChange (int k) const noexcept { return bands[std::clamp (k, 0, 5)].gainDb; }

    private:
        struct Band
        {
            Biquad hp[2][2], lp[2][2];
            OnePoleHP hp1[2];
            OnePoleLP lp1[2];
            float env[2] {}, gainDb = 0, ac = 0, rc = 0;
        };
        float fs = 48000.0f;
        Band bands[6];
    };
}
