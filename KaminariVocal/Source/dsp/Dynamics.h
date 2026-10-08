#pragma once

#include "../sends/DspCommon.h"
#include "Eq.h"

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
    // Compression (Alt edition): one optical, LA-2A style mode. Compression (the LA-2A's Peak Reduction) sets how hard
    // the side chain drives the opto cell; Gain is the output makeup. The side-chain EQ shapes what the detector hears.
    struct CompressorSettings
    {
        float peakReduction = 50.0f;   // 0..100
        float gainDb = 0.0f;           // makeup, -12 .. +24 dB

        // Side-chain detection EQ: shapes what the detector hears (the audio itself is not filtered). Same bands and
        // filter designs as the main EQ.
        static constexpr int numScBands = 8;
        EqBandSettings scBand[numScBands];
    };

    class Compressor
    {
    public:
        // The opto model (original implementation, modelled on published LA-2A behaviour):
        //  - side-chain gain from Compression: (Compression - 50) x 0.4 dB, against a fixed -20 dBFS threshold. Calibrated on a
        //    dry vocal peaking at -6 dBFS: 30 touches only the loudest words, 50 levels about 4 dB (7 dB on peaks), 100 about
        //    19 dB (25 dB on peaks);
        //  - a soft 12 dB knee; the ratio rises from about 3:1 near the knee to about 6:1 far above it;
        //  - the cell responds in about 10 ms; it releases in two stages: half of the reduction in about 60 ms, the rest
        //    in 0.5 to 3.5 s, slower the longer and harder it has been compressing (the cell's memory).
        static constexpr float thresholdDb = -20.0f, kneeDb = 12.0f;

        static float sideChainGainDb (float peakReduction) noexcept { return (std::clamp (peakReduction, 0.0f, 100.0f) - 50.0f) * 0.4f; }

        // Static reduction (dB) for a detector level already including the side-chain gain.
        static float staticGr (float levelDb) noexcept
        {
            const float over = levelDb - thresholdDb;
            if (over <= -0.5f * kneeDb) return 0.0f;
            const float ratio = 3.0f + 3.0f * std::clamp ((over - 6.0f) / 30.0f, 0.0f, 1.0f);
            const float slope = 1.0f - 1.0f / ratio;
            if (over < 0.5f * kneeDb)
            {
                const float x = over + 0.5f * kneeDb;
                return slope * x * x / (2.0f * kneeDb);
            }
            return slope * over;
        }

        void prepare (double sampleRate)
        {
            fs = (float) sampleRate;
            reset();
        }

        void reset()
        {
            env = 0.0f; grFast = grSlow = 0.0f; memory = 0.0f; makeupNow = -1.0f;
            for (auto& ch : scState) for (auto& b : ch) for (auto& z : b) z = {};
        }

        // detectorOut (optional, n samples): the mono signal the detector hears, after the side-chain bands
        float process (float* l, float* r, int n, const CompressorSettings& s, float* detectorOut = nullptr)
        {
            bool scOn[CompressorSettings::numScBands];
            bool anySc = false;
            for (int k = 0; k < CompressorSettings::numScBands; ++k)
            {
                const auto& sb = s.scBand[k];
                scOn[k] = sb.used && sb.on;
                anySc = anySc || scOn[k];
                if (! scOn[k]) continue;
                const bool fresh = ! scWasActive[k] || sb.type != scType[k] || sb.slopeIndex != scSlope[k];
                scDesign[k] = EqDesign::make (sb, fs);
                scType[k] = sb.type; scSlope[k] = sb.slopeIndex;
                if (fresh) for (auto& ch : scState) for (auto& z : ch[k]) z = {};   // a band switched on starts from silence
            }
            for (int k = 0; k < CompressorSettings::numScBands; ++k)
                scWasActive[k] = scOn[k];

            const float scGain = dbToGain (sideChainGainDb (s.peakReduction));
            const bool off = s.peakReduction <= 0.0f;
            const float envAttack = coeffMs (1.0f, fs), envRelease = coeffMs (30.0f, fs);
            const float cellAttack = coeffMs (10.0f, fs), fastRelease = coeffMs (60.0f, fs);
            const float memUp = coeffMs (2000.0f, fs), memDown = coeffMs (5000.0f, fs);
            const float slowRelease = coeffMs (500.0f + 3000.0f * memory, fs);   // per block: memory changes slowly
            const float makeup = dbToGain (s.gainDb), makeupGlide = coeffMs (20.0f, fs);
            if (makeupNow < 0.0f) makeupNow = makeup;   // first block after a reset: no glide
            float maxGr = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                float peak = 0.0f, det = 0.0f;
                for (int c = 0; c < 2; ++c)
                {
                    float src = (c == 0 ? l : r)[i];
                    if (anySc)
                        for (int k = 0; k < CompressorSettings::numScBands; ++k)
                            if (scOn[k]) src = scFilterSample (c, k, src);
                    det += 0.5f * src;
                    peak = std::max (peak, std::abs (src));
                }
                if (detectorOut != nullptr) detectorOut[i] = det;
                peak *= scGain;
                env = peak > env ? peak + envAttack * (env - peak) : peak + envRelease * (env - peak);
                const float target = off ? 0.0f : staticGr (toDb (env));

                // opto cell: both stages follow a rising reduction; on release one half recovers fast, one slowly
                for (float* g : { &grFast, &grSlow })
                    if (target > *g) *g = target + cellAttack * (*g - target);
                grFast = grFast > target ? target + fastRelease * (grFast - target) : grFast;
                grSlow = grSlow > target ? target + slowRelease * (grSlow - target) : grSlow;
                const float gr = 0.5f * (grFast + grSlow);
                // memory: charges while the cell is working hard, so long, heavy compression releases more slowly
                const float memTarget = gr > 3.0f ? 1.0f : 0.0f;
                memory = memTarget + (memTarget > memory ? memUp : memDown) * (memory - memTarget);
                maxGr = std::max (maxGr, gr);

                makeupNow = makeup + makeupGlide * (makeupNow - makeup);   // Gain changes glide over about 20 ms
                const float g = dbToGain (-gr) * makeupNow;
                l[i] = flushDenormal (l[i] * g);
                r[i] = flushDenormal (r[i] * g);
            }
            return maxGr;
        }

    private:
        float fs = 48000.0f;
        float env = 0.0f, grFast = 0.0f, grSlow = 0.0f, memory = 0.0f, makeupNow = -1.0f;
        struct ScState { double z1 = 0, z2 = 0; };
        EqDesign scDesign[CompressorSettings::numScBands];
        ScState scState[2][CompressorSettings::numScBands][4];
        int scType[CompressorSettings::numScBands] {}, scSlope[CompressorSettings::numScBands] {};
        bool scWasActive[CompressorSettings::numScBands] {};

        float scFilterSample (int c, int k, float x) noexcept
        {
            const auto& d = scDesign[k];
            double v = x * d.scale;
            for (int q = 0; q < d.numSections; ++q)
            {
                const auto& co = d.sec[q];
                auto& z = scState[c][k][q];
                const double y = co.b0 * v + z.z1;
                z.z1 = co.b1 * v - co.a1 * y + z.z2;
                z.z2 = co.b2 * v - co.a2 * y;
                v = y;
            }
            return (float) v;
        }
    };

    //==================================================================================================================
    // De-ess (Alt edition): Frequency and Range only. The detector compares the level above Frequency with the level
    // of the whole vocal, so it works the same at any input level (no threshold to set): reduction starts when the
    // sibilant band comes within 12 dB of the full signal and grows 1 dB per dB, up to Range. Split band: only the
    // part above Frequency is turned down (complementary split, so with no reduction the output equals the input).
    struct DeEsserSettings
    {
        float freqHz = 5000.0f, rangeDb = 6.0f;
    };

    class DeEsser
    {
    public:
        static constexpr float relativeThresholdDb = -12.0f, gateDb = -60.0f;

        void prepare (double sampleRate)
        {
            fs = (float) sampleRate;
            reset();
        }

        void reset()
        {
            for (int c = 0; c < 2; ++c) { hp[c].reset(); split[c].reset(); }
            envHi = envAll = 0.0f; gr = 0.0f;
        }

        float process (float* l, float* r, int n, const DeEsserSettings& s)
        {
            const float f = std::clamp (s.freqHz, 1000.0f, 0.45f * fs);
            for (int c = 0; c < 2; ++c)
            {
                hp[c].set (Biquad::HighPass, fs, f, 0.707f);
                split[c].set (Biquad::LowPass, fs, f, 0.707f);
            }
            const float aC = coeffMs (0.5f, fs), rC = coeffMs (60.0f, fs), grRelease = coeffMs (40.0f, fs);
            float maxGr = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float in[2] = { l[i], r[i] };
                float hiPeak = 0.0f, allPeak = 0.0f;
                for (int c = 0; c < 2; ++c)
                {
                    hiPeak = std::max (hiPeak, std::abs (hp[c].process (in[c])));
                    allPeak = std::max (allPeak, std::abs (in[c]));
                }
                envHi = hiPeak > envHi ? hiPeak + aC * (envHi - hiPeak) : hiPeak + rC * (envHi - hiPeak);
                envAll = allPeak > envAll ? allPeak + aC * (envAll - allPeak) : allPeak + rC * (envAll - allPeak);
                const float rel = toDb (envHi) - toDb (envAll);
                const float target = toDb (envAll) < gateDb ? 0.0f : std::clamp (rel - relativeThresholdDb, 0.0f, std::max (0.0f, s.rangeDb));
                gr = target > gr ? target : target + grRelease * (gr - target);
                maxGr = std::max (maxGr, gr);
                const float g = dbToGain (-gr);
                for (int c = 0; c < 2; ++c)
                {
                    const float low = split[c].process (in[c]);
                    const float y = gr <= 0.0f ? in[c] : low + (in[c] - low) * g;   // no reduction: exact pass-through
                    (c == 0 ? l : r)[i] = flushDenormal (y);
                }
            }
            return maxGr;
        }

    private:
        float fs = 48000.0f;
        Biquad hp[2], split[2];
        float envHi = 0.0f, envAll = 0.0f, gr = 0.0f;
    };

    //==================================================================================================================
    struct MultibandBandSettings
    {
        float lo = 100, hi = 500, threshDb = -24, ratio = 2, attackMs = 10, releaseMs = 150, kneeDb = 6, rangeDb = -6, gainDb = 0;
        bool expand = false, solo = false;
        bool bypass = false;   // band passes unprocessed
        bool mute = false;     // band removed from the output
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
                    if (bs.bypass) change = 0.0f;
                    b.gainDb = change;
                    if (std::abs (change) > std::abs (meter)) meter = change;
                    const float g = bs.mute ? 0.0f : (bs.bypass ? 1.0f : dbToGain (change + bs.gainDb));
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
