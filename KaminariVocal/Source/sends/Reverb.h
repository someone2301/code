#pragma once

#include "DspCommon.h"
#include <memory>

// Reverb send: 20 original algorithms built on four engines.
//
//   FDN     8-line feedback delay network (Householder mixing) with input diffusion, optional early reflections,
//           per-line damping, and one of five delay-modulation types; optional loop saturation and
//           vintage-digital resolution reduction.
//   Plate   figure-eight allpass/delay tank (two cross-coupled halves) with modulated tank allpasses.
//   Nonlin  irregular multi-tap envelope generator (gated, truncated or reversed envelopes, no feedback).
//   Ambience early-reflection tap set plus a short FDN tail; Attack balances the two.
//
// Each mode chooses the engine, its delay scale range, modulation type, diffusion, damping and nonlinearity.
// The output is always 100 % wet (it is a send return).
namespace kv
{
    enum class ReverbEngine { Fdn, Plate, Nonlin, Ambience };
    enum class ModType { None, Chorus, Random, Detune, Ensemble, WowFlutter };

    struct ReverbModeSpec
    {
        const char* name;
        const char* family;
        const char* description;
        ReverbEngine engine;
        float scaleMin, scaleMax;   // delay-length scale reached at Size 0 % and 100 %
        ModType mod;
        float modDepthMs;           // modulation excursion at Mod Depth 100 %
        float hiMul;                // multiplies the High Cut for the in-loop damping (< 1 = darker)
        int   diffusers;            // number of input allpasses (0..6)
        float diffG;                // input allpass gain at Density 100 %
        float diffScale;            // input allpass length scale (larger = slower build)
        float erLevel;              // early reflections (0 = none)
        int   erPattern;            // which early-reflection tap set
        float saturation;           // loop saturation drive (0 = none)
        float lofiHz;               // sample-hold rate in the loop (0 = full resolution)
        float lofiBits;
        float width;                // 0 = mono return, 1 = full decorrelated stereo
        float outGain;
    };

    inline constexpr int numReverbModes = 20;

    inline const ReverbModeSpec& reverbMode (int index)
    {
        using E = ReverbEngine; using M = ModType;
        static const ReverbModeSpec modes[numReverbModes] = {
            { "Concert Hall", "Halls", "Large, lush hall. Density sets the echo density; chorused modulation.",
              E::Fdn, 0.7f, 1.4f, M::Chorus, 0.9f, 1.0f, 4, 0.70f, 1.0f, 0.0f, 0, 0.0f, 0.0f, 0, 1.0f, 0.55f },
            { "Bright Hall", "Halls", "Brighter initial sound and deeper modulation than Concert Hall.",
              E::Fdn, 0.7f, 1.4f, M::Chorus, 2.0f, 1.6f, 4, 0.70f, 1.0f, 0.0f, 0, 0.0f, 0.0f, 0, 1.0f, 0.55f },
            { "Plate", "Plates", "Bright, highly diffuse, dense plate with chorused modulation.",
              E::Plate, 0.6f, 1.15f, M::Chorus, 0.6f, 1.3f, 4, 0.75f, 1.0f, 0.0f, 0, 0.0f, 0.0f, 0, 1.0f, 0.75f },
            { "Room", "Rooms", "Medium diffusion and early-echo density, somewhat darker, with modulation.",
              E::Fdn, 0.25f, 0.6f, M::Chorus, 0.5f, 0.6f, 2, 0.50f, 0.6f, 0.45f, 0, 0.0f, 0.0f, 0, 0.9f, 0.6f },
            { "Chamber", "Rooms", "Transparent and dense; less coloured than Plate or Room.",
              E::Fdn, 0.4f, 0.85f, M::Random, 0.25f, 1.0f, 5, 0.72f, 0.8f, 0.3f, 1, 0.0f, 0.0f, 0, 1.0f, 0.55f },
            { "Random Space", "Spaces", "Deep, wide, slow-building reverb with randomised delay modulation.",
              E::Fdn, 1.0f, 1.8f, M::Random, 1.6f, 1.0f, 6, 0.78f, 2.6f, 0.0f, 0, 0.0f, 0.0f, 0, 1.0f, 0.5f },
            { "Chorus Space", "Spaces", "Like Random Space, with chorused modulation instead of random.",
              E::Fdn, 1.0f, 1.8f, M::Chorus, 1.6f, 1.0f, 6, 0.78f, 2.6f, 0.0f, 0, 0.0f, 0.0f, 0, 1.0f, 0.5f },
            { "Ambience", "Ambience", "Early reflections plus a short tail. Attack balances early and late. Subtle vocal air.",
              E::Ambience, 0.3f, 1.5f, M::Random, 0.3f, 1.0f, 1, 0.60f, 0.5f, 1.0f, 2, 0.0f, 0.0f, 0, 1.0f, 0.7f },
            { "Sanctuary", "Vintage", "Distinct early reflections, fast-building dense tail, detuned modulation; vintage digital.",
              E::Fdn, 0.6f, 1.3f, M::Detune, 1.2f, 0.85f, 4, 0.80f, 0.5f, 0.7f, 3, 0.0f, 20000.0f, 13.0f, 1.0f, 0.55f },
            { "Dirty Hall", "Dirty", "Warm, gritty vintage-digital hall.",
              E::Fdn, 0.7f, 1.4f, M::Random, 1.0f, 0.75f, 4, 0.70f, 1.0f, 0.0f, 0, 1.6f, 14000.0f, 11.0f, 0.9f, 0.55f },
            { "Dirty Plate", "Dirty", "Warm, gritty, dense and wide vintage-digital plate.",
              E::Plate, 0.6f, 1.15f, M::Random, 0.8f, 0.8f, 4, 0.75f, 1.0f, 0.0f, 0, 1.6f, 14000.0f, 11.0f, 1.25f, 0.7f },
            { "Smooth Plate", "Smooth", "Clear, smooth, transparent plate decay.",
              E::Plate, 0.6f, 1.15f, M::Random, 0.25f, 1.15f, 4, 0.80f, 1.0f, 0.0f, 0, 0.0f, 0.0f, 0, 1.0f, 0.75f },
            { "Smooth Room", "Smooth", "Clear, natural room with a smooth decay.",
              E::Fdn, 0.25f, 0.6f, M::Random, 0.2f, 1.0f, 5, 0.75f, 0.6f, 0.3f, 1, 0.0f, 0.0f, 0, 0.9f, 0.6f },
            { "Smooth Random", "Smooth", "Smooth, natural decay that spans small rooms to large spaces with Size.",
              E::Fdn, 0.2f, 2.0f, M::Random, 0.6f, 1.0f, 5, 0.75f, 1.0f, 0.0f, 0, 0.0f, 0.0f, 0, 1.0f, 0.55f },
            { "Nonlin", "Effects", "Gated, truncated or reverse envelopes. Size sets the length, Attack the envelope shape.",
              E::Nonlin, 0.0f, 1.0f, M::None, 0.0f, 1.0f, 2, 0.60f, 0.4f, 0.0f, 0, 0.0f, 0.0f, 0, 1.0f, 0.8f },
            { "Chaotic Hall", "Chaotic", "Long, animated hall with tape-like wow and flutter and soft saturation.",
              E::Fdn, 0.8f, 1.5f, M::WowFlutter, 1.4f, 0.9f, 4, 0.72f, 1.2f, 0.0f, 0, 1.2f, 0.0f, 0, 1.0f, 0.55f },
            { "Chaotic Chamber", "Chaotic", "Animated chamber with wow and flutter and soft saturation.",
              E::Fdn, 0.4f, 0.85f, M::WowFlutter, 1.0f, 0.9f, 5, 0.72f, 0.8f, 0.3f, 1, 1.2f, 0.0f, 0, 1.0f, 0.55f },
            { "Chaotic Neutral", "Chaotic", "Wow and flutter animation that stays comparatively colourless (no saturation, flat damping).",
              E::Fdn, 0.8f, 1.5f, M::WowFlutter, 1.2f, 1.25f, 4, 0.72f, 1.2f, 0.0f, 0, 0.0f, 0.0f, 0, 1.0f, 0.55f },
            { "Cathedral", "Large", "Very large and open with a long decay, ensemble modulation and high-frequency roll-off.",
              E::Fdn, 1.6f, 2.4f, M::Ensemble, 1.4f, 0.45f, 6, 0.75f, 1.8f, 0.0f, 0, 0.0f, 0.0f, 0, 1.0f, 0.5f },
            { "Palace", "Palace", "Open, realistic room-to-hall character; Size scales it from small rooms to large halls. Vintage digital.",
              E::Fdn, 0.3f, 1.8f, M::Chorus, 0.6f, 0.9f, 4, 0.70f, 1.0f, 0.5f, 3, 0.0f, 24000.0f, 14.0f, 1.0f, 0.55f },
        };
        return modes[std::clamp (index, 0, numReverbModes - 1)];
    }

    // Which Advanced controls change the sound in a mode. The UI shows only these.
    struct ReverbControlUse { bool decay, modulation, attack; };

    inline ReverbControlUse reverbControlUse (int mode)
    {
        const auto& m = reverbMode (mode);
        const bool nonlin = m.engine == ReverbEngine::Nonlin;
        return { ! nonlin, m.mod != ModType::None, nonlin || m.engine == ReverbEngine::Ambience };
    }

    inline const char* reverbFamilyGuide()
    {
        return "Dirty: vintage grit and character.  Smooth: polished vocals and natural spaces.  "
               "Chaotic: long, animated reverbs that still sit in a mix.  Palace: room sounds from small spaces "
               "to large halls.  Ambience: space that is felt more than heard.";
    }

    struct ReverbSettings
    {
        int   mode = 0;
        float decaySec = 2.0f;     // RT60
        float size = 0.5f;         // 0..1
        float preDelayMs = 20.0f;
        float hiCutHz = 8000.0f;
        float loCutHz = 120.0f;
        float modRateHz = 0.6f;
        float modDepth = 0.3f;     // 0..1
        float density = 0.7f;      // 0..1
        float attack = 0.5f;       // 0..1 (Ambience, Nonlin)
    };

    //==================================================================================================================
    // Modulator producing a delay offset (in samples) for each of up to 8 paths.
    class DelayModulator
    {
    public:
        void reset (uint32_t seed)
        {
            for (int i = 0; i < 8; ++i)
            {
                phase[i] = (float) i / 8.0f;
                rnd[i].reset (seed + 977u * (uint32_t) i);
            }
            shared.reset (seed ^ 0x51ed27u);
            wow = flutter = 0.0f;
        }

        // Advances one sample and fills out[0..n-1] with offsets in samples.
        void next (ModType type, float rateHz, float depthSamples, float fs, int n, float* out) noexcept
        {
            if (type == ModType::None || depthSamples <= 0.0f)
            {
                std::fill (out, out + n, 0.0f);
                return;
            }
            if (type == ModType::WowFlutter)
            {
                // Coherent tape-like pitch drift on every path, plus a little per-path jitter.
                wow += rateHz * 0.5f / fs;          wow -= std::floor (wow);
                flutter += rateHz * 9.3f / fs;      flutter -= std::floor (flutter);
                const float common = 0.7f * std::sin (twoPi * wow) + 0.12f * std::sin (twoPi * flutter)
                                   + 0.3f * shared.next (rateHz * 0.8f, fs);
                for (int i = 0; i < n; ++i)
                    out[i] = depthSamples * (common + 0.15f * rnd[i].next (rateHz * 1.7f, fs));
                return;
            }
            for (int i = 0; i < n; ++i)
            {
                const float r = rateHz * (1.0f + 0.137f * (float) i);
                float v = 0.0f;
                switch (type)
                {
                    case ModType::Chorus:
                        phase[i] += r / fs; phase[i] -= std::floor (phase[i]);
                        v = std::sin (twoPi * phase[i]);
                        break;
                    case ModType::Random:
                        v = rnd[i].next (r, fs);
                        break;
                    case ModType::Detune:
                        // Triangle: the delay ramps at a constant slope, i.e. a steady pitch offset that
                        // flips sign each half period. Paths alternate direction, so they detune against each other.
                        phase[i] += r * 0.5f / fs; phase[i] -= std::floor (phase[i]);
                        v = (4.0f * std::abs (phase[i] - 0.5f) - 1.0f) * ((i & 1) ? -1.0f : 1.0f);
                        break;
                    case ModType::Ensemble:
                        phase[i] += r / fs; phase[i] -= std::floor (phase[i]);
                        v = (std::sin (twoPi * phase[i]) + std::sin (twoPi * phase[i] * 1.41f + 1.3f)
                             + std::sin (twoPi * phase[i] * 0.67f + 2.1f)) / 3.0f;
                        break;
                    case ModType::None:
                    case ModType::WowFlutter:
                        break;   // handled above
                }
                out[i] = depthSamples * v;
            }
        }

    private:
        float phase[8] {};
        SmoothRandom rnd[8], shared;
        float wow = 0, flutter = 0;
    };

    //==================================================================================================================
    class ReverbSend
    {
    public:
        void prepare (double sampleRate, int /*maxBlock*/)
        {
            fs = (float) sampleRate;
            const int maxPre = (int) (0.26f * fs);
            for (auto& p : pre) p.prepare (maxPre);
            for (auto& l : fdn) l.prepare ((int) (0.35f * fs));
            for (auto& d : diff) for (auto& a : d) a.prepare ((int) (0.2f * fs));
            for (auto& a : tankAp) a.prepare ((int) (0.2f * fs));
            for (auto& d : tankDelay) d.prepare ((int) (0.25f * fs));
            for (auto& d : nonlinLine) d.prepare ((int) (0.75f * fs));
            for (auto& d : erLine) d.prepare ((int) (0.1f * fs));
            reset();
        }

        void reset()
        {
            for (auto& p : pre) p.reset();
            for (auto& l : fdn) l.reset();
            for (auto& d : diff) for (auto& a : d) a.reset();
            for (auto& a : tankAp) a.reset();
            for (auto& d : tankDelay) d.reset();
            for (auto& d : nonlinLine) d.reset();
            for (auto& d : erLine) d.reset();
            for (auto& f : damp) f.reset();
            for (auto& f : inLP) f.reset();
            for (auto& f : inHP) f.reset();
            for (auto& f : lofi) f.reset();
            tankState[0] = tankState[1] = 0.0f;
            mod.reset (0x1234u + (uint32_t) activeMode * 31u);
            modeFade = 1.0f;
        }

        // Adds nothing to the input; writes the wet return into outL/outR (overwritten, not summed).
        void process (const float* inL, const float* inR, float* outL, float* outR, int n, const ReverbSettings& s)
        {
            if (s.mode != activeMode)
            {
                pendingMode = s.mode;   // fade the old algorithm out, then switch
            }
            configure (s);

            for (int i = 0; i < n; ++i)
            {
                if (pendingMode >= 0)
                {
                    modeFade -= 1.0f / (0.01f * fs);
                    if (modeFade <= 0.0f)
                    {
                        activeMode = pendingMode;
                        pendingMode = -1;
                        reset();
                        modeFade = 0.0f;
                        configure (s);
                    }
                }
                else if (modeFade < 1.0f)
                {
                    modeFade = std::min (1.0f, modeFade + 1.0f / (0.01f * fs));
                }

                float l = inL[i], r = inR[i];
                pre[0].push (l); pre[1].push (r);
                l = pre[0].read (preSamples); r = pre[1].read (preSamples);
                l = inLP[0].process (inHP[0].process (l));
                r = inLP[1].process (inHP[1].process (r));

                float yl = 0.0f, yr = 0.0f;
                switch (spec->engine)
                {
                    case ReverbEngine::Fdn:      processFdn (l, r, yl, yr, s); break;
                    case ReverbEngine::Plate:    processPlate (l, r, yl, yr); break;
                    case ReverbEngine::Nonlin:   processNonlin (l, r, yl, yr); break;
                    case ReverbEngine::Ambience: processAmbience (l, r, yl, yr, s); break;
                }

                // stereo width of the return (mid/side), then the mode's output level
                const float m = 0.5f * (yl + yr), sd = 0.5f * (yl - yr) * spec->width;
                const float g = spec->outGain * modeFade;
                outL[i] = flushDenormal ((m + sd) * g);
                outR[i] = flushDenormal ((m - sd) * g);
            }
        }

        int getActiveMode() const noexcept { return activeMode; }

        // Switches algorithm without a crossfade (used when the send starts from silence).
        void forceMode (int mode)
        {
            activeMode = std::clamp (mode, 0, numReverbModes - 1);
            pendingMode = -1;
            reset();
        }

    private:
        static constexpr float fdnBaseMs[8] = { 43.1f, 51.7f, 59.3f, 67.9f, 73.3f, 83.1f, 91.7f, 101.3f };
        static constexpr float diffBaseMs[6] = { 4.71f, 3.59f, 12.73f, 9.30f, 17.13f, 23.91f };
        static constexpr float erPatterns[4][8] = {
            { 7.1f, 11.3f, 17.9f, 23.3f, 29.1f, 37.7f, 43.1f, 53.9f },     // room-like
            { 5.3f, 8.9f, 13.1f, 16.7f, 21.1f, 24.3f, 28.9f, 33.7f },      // dense, even
            { 3.1f, 5.3f, 7.9f, 11.2f, 13.7f, 17.3f, 23.1f, 31.7f },       // close ambience
            { 11.0f, 19.0f, 31.0f, 41.0f, 53.0f, 61.0f, 73.0f, 89.0f },    // distinct, spaced
        };

        void configure (const ReverbSettings& s)
        {
            spec = &reverbMode (activeMode);
            const float size = std::clamp (s.size, 0.0f, 1.0f);
            scale = spec->scaleMin + (spec->scaleMax - spec->scaleMin) * size;
            preSamples = std::max (1.0f, s.preDelayMs * 0.001f * fs);
            for (int c = 0; c < 2; ++c)
            {
                inLP[c].setCutoff (std::min (s.hiCutHz * (spec->hiMul > 1.0f ? 1.0f : 1.4f), 0.45f * fs), fs);
                inHP[c].setCutoff (s.loCutHz, fs);
            }
            const float loopCut = std::min (s.hiCutHz * spec->hiMul, 0.45f * fs);
            for (auto& d : damp) d.setCutoff (loopCut, fs);
            modDepthSamples = spec->modDepthMs * 0.001f * fs * std::clamp (s.modDepth, 0.0f, 1.0f);
            diffG = spec->diffG * (0.35f + 0.65f * std::clamp (s.density, 0.0f, 1.0f));
            const float t60 = std::max (0.1f, s.decaySec);
            for (int k = 0; k < 8; ++k)
            {
                lineSamples[k] = fdnBaseMs[k] * 0.001f * fs * scale * (spec->engine == ReverbEngine::Ambience ? 0.35f : 1.0f);
                lineGain[k] = std::pow (10.0f, -3.0f * lineSamples[k] / (t60 * fs));
            }
            // plate tank: the loop is roughly the sum of one half's elements
            const float tankScale = 0.5f + 0.7f * size;
            static constexpr float tankMs[8] = { 22.58f, 149.6f, 60.48f, 124.99f, 30.51f, 141.7f, 89.24f, 106.28f };
            for (int k = 0; k < 8; ++k)
                tankSamples[k] = tankMs[k] * 0.001f * fs * tankScale;
            const float loop = tankSamples[0] + tankSamples[1] + tankSamples[2] + tankSamples[3];
            tankDecay = std::min (0.98f, std::pow (10.0f, -3.0f * loop / (t60 * fs)));
            // nonlin
            nonlinLen = (60.0f + 640.0f * size) * 0.001f * fs;
            attack = std::clamp (s.attack, 0.0f, 1.0f);
            modRate = s.modRateHz;
            for (int c = 0; c < 2; ++c)
            {
                Random jitter (0xabcdu + (uint32_t) c * 7919u);   // fixed, irregular tap pattern
                for (int k = 0; k < nonlinTaps; ++k)
                {
                    const float u = ((float) k + 0.5f + 0.42f * jitter.next()) / (float) nonlinTaps;
                    const float sign = jitter.next() > 0.0f ? 1.0f : -1.0f;
                    nonlinPos[c][k] = u;
                    nonlinGain[c][k] = sign * nonlinEnvelope (u);
                }
            }
        }

        float lofiStage (int ch, float x) noexcept
        {
            if (spec->lofiHz <= 0.0f)
                return x;
            return lofi[ch].process (x, std::max (1.0f, fs / spec->lofiHz), spec->lofiBits);
        }

        float saturate (float x) const noexcept
        {
            if (spec->saturation <= 0.0f)
                return x;
            return softClip (x * spec->saturation) / spec->saturation;
        }

        void diffuse (int ch, float& x) noexcept
        {
            for (int k = 0; k < spec->diffusers; ++k)
            {
                const float d = diffBaseMs[k] * spec->diffScale * (ch == 0 ? 1.0f : 1.07f) * 0.001f * fs * std::sqrt (scale);
                x = diff[ch][k].process (x, std::max (2.0f, d), diffG * (k < 2 ? 1.0f : 0.85f));
            }
        }

        float earlyReflections (int ch, float x) noexcept
        {
            erLine[ch].push (x);
            const auto& pat = erPatterns[std::clamp (spec->erPattern, 0, 3)];
            float y = 0.0f, g = 0.9f;
            for (int k = 0; k < 8; ++k)
            {
                const float t = pat[k] * (ch == 0 ? 1.0f : 1.093f) * std::min (scale, 1.6f) * 0.001f * fs;
                y += ((k + ch) & 1 ? -g : g) * erLine[ch].read (std::max (1.0f, t));
                g *= 0.82f;
            }
            return y * 0.45f;
        }

        void processFdn (float l, float r, float& yl, float& yr, const ReverbSettings&)
        {
            float er[2] = { 0.0f, 0.0f };
            if (spec->erLevel > 0.0f)
            {
                er[0] = earlyReflections (0, l);
                er[1] = earlyReflections (1, r);
            }
            diffuse (0, l);
            diffuse (1, r);
            yl = yr = 0.0f;
            runFdn (l, r, yl, yr);
            yl += spec->erLevel * er[0];
            yr += spec->erLevel * er[1];
        }

        void runFdn (float l, float r, float& yl, float& yr)
        {
            float offs[8];
            mod.next (spec->mod, modRate, modDepthSamples, fs, 8, offs);
            float out[8], sum = 0.0f;
            for (int k = 0; k < 8; ++k)
            {
                out[k] = fdn[k].read (lineSamples[k] + offs[k] + modDepthSamples + 2.0f);
                out[k] = damp[k].process (out[k]) * lineGain[k];
                out[k] = saturate (out[k]);
                sum += out[k];
            }
            const float h = sum * (2.0f / 8.0f);
            for (int k = 0; k < 8; ++k)
            {
                const float inj = (k & 1) ? r : l;
                float v = out[k] - h + inj * ((k & 2) ? -0.5f : 0.5f);
                v = lofiStage (k & 1, v);
                fdn[k].push (flushDenormal (v));
            }
            yl = (out[0] - out[2] + out[4] - out[6] + 0.5f * (out[1] + out[5])) * 0.6f;
            yr = (out[1] - out[3] + out[5] - out[7] + 0.5f * (out[2] + out[6])) * 0.6f;
        }

        void processPlate (float l, float r, float& yl, float& yr)
        {
            float x = 0.5f * (l + r);
            diffuse (0, x);
            float offs[2];
            mod.next (spec->mod, modRate, modDepthSamples, fs, 2, offs);

            // half A is fed by the input plus the end of half B, and vice versa
            float a = x + tankDecay * tankState[1];
            a = tankAp[0].process (a, tankSamples[0] + offs[0] + modDepthSamples + 2.0f, -0.7f);
            tankDelay[0].push (a);
            a = damp[0].process (tankDelay[0].read (tankSamples[1])) * tankDecay;
            a = saturate (lofiStage (0, a));
            a = tankAp[1].process (a, tankSamples[2], 0.5f);
            tankDelay[1].push (a);
            const float endA = tankDelay[1].read (tankSamples[3]);

            float b = x + tankDecay * endA;
            b = tankAp[2].process (b, tankSamples[4] + offs[1] + modDepthSamples + 2.0f, -0.7f);
            tankDelay[2].push (b);
            b = damp[1].process (tankDelay[2].read (tankSamples[5])) * tankDecay;
            b = saturate (lofiStage (1, b));
            b = tankAp[3].process (b, tankSamples[6], 0.5f);
            tankDelay[3].push (b);
            const float endB = tankDelay[3].read (tankSamples[7]);

            tankState[0] = endA;
            tankState[1] = endB;

            // output taps spread through both halves (fractions of each element's length)
            yl = tankDelay[2].read (tankSamples[5] * 0.08f) + tankDelay[2].read (tankSamples[5] * 0.66f)
               - tankDelay[3].read (tankSamples[7] * 0.45f) + tankDelay[0].read (tankSamples[1] * 0.52f)
               - tankDelay[1].read (tankSamples[3] * 0.23f);
            yr = tankDelay[0].read (tankSamples[1] * 0.11f) + tankDelay[0].read (tankSamples[1] * 0.73f)
               - tankDelay[1].read (tankSamples[3] * 0.51f) + tankDelay[2].read (tankSamples[5] * 0.47f)
               - tankDelay[3].read (tankSamples[7] * 0.29f);
            yl *= 0.6f;
            yr *= 0.6f;
        }

        // Envelope of the Nonlin taps at position u (0..1) of the pattern.
        float nonlinEnvelope (float u) const noexcept
        {
            if (attack <= 0.5f)
            {
                const float a = attack / 0.5f;                  // 0 = gated (flat), 1 = truncated decay
                return 1.0f - a * 0.85f * u;
            }
            const float b = (attack - 0.5f) / 0.5f;             // 1 = reverse (rising)
            return (1.0f - b) * (1.0f - 0.85f * u) + b * (0.08f + 0.92f * std::pow (u, 1.5f));
        }

        void processNonlin (float l, float r, float& yl, float& yr)
        {
            nonlinLine[0].push (l);
            nonlinLine[1].push (r);
            float acc[2] = { 0.0f, 0.0f };
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < nonlinTaps; ++k)
                    acc[c] += nonlinGain[c][k] * nonlinLine[c].read (std::max (1.0f, nonlinPos[c][k] * nonlinLen));
            yl = acc[0] * 0.3f;
            yr = acc[1] * 0.3f;
            diffuse (0, yl);
            diffuse (1, yr);
        }

        void processAmbience (float l, float r, float& yl, float& yr, const ReverbSettings&)
        {
            float er[2] = { earlyReflections (0, l), earlyReflections (1, r) };
            diffuse (0, er[0]);
            diffuse (1, er[1]);
            float tl = 0.0f, tr = 0.0f;
            runFdn (l, r, tl, tr);
            // Attack: 0 = early reflections only, 1 = tail only (equal-power balance)
            const float ge = std::cos (attack * 1.5707963f), gl = std::sin (attack * 1.5707963f);
            yl = ge * er[0] * 1.6f + gl * tl;
            yr = ge * er[1] * 1.6f + gl * tr;
        }

        float fs = 48000.0f;
        int activeMode = 0, pendingMode = -1;
        float modeFade = 1.0f;
        const ReverbModeSpec* spec = &reverbMode (0);

        DelayLine pre[2];
        OnePoleLP inLP[2];
        OnePoleHP inHP[2];
        float preSamples = 1.0f, scale = 1.0f, diffG = 0.7f, modDepthSamples = 0.0f, modRate = 0.6f;

        DelayLine fdn[8];
        OnePoleLP damp[8];
        float lineSamples[8] {}, lineGain[8] {};
        DelayModulator mod;
        LoFi lofi[2];

        Allpass diff[2][6];
        DelayLine erLine[2];

        Allpass tankAp[4];
        DelayLine tankDelay[4];
        float tankSamples[8] {}, tankDecay = 0.5f, tankState[2] {};

        DelayLine nonlinLine[2];
        static constexpr int nonlinTaps = 40;
        float nonlinLen = 1000.0f, attack = 0.5f;
        float nonlinPos[2][nonlinTaps] {}, nonlinGain[2][nonlinTaps] {};
    };
}
