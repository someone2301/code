#pragma once

#include "DspCommon.h"

// Widener send with two separate, selectable algorithms (never layered):
//
//   MicroShift   micro pitch-shift widening after the user-supplied MicroShift manual: the left side is
//                shifted up and the right side down by a few continuously varying cents, each with a
//                continuously varying short delay. Style I / II / III differ in pitch and delay variation,
//                frequency response, saturation and the crossfade ("de-glitch") shape. Detune and Delay scale
//                the style's amounts in percent (100 % = the style's own amount). Focus is the crossover
//                frequency: only the band above it is widened.
//   SideWidener  mono-compatible pseudo-stereo after the user-supplied SideWidener manual: a decorrelated copy
//                of the mid signal is returned as pure side (+ left, - right), so the mono sum of the return is
//                exactly zero. Mode 1 is subtle without time smearing, Mode 3 is widest with a room-like smear,
//                Mode 2 lies between. Tone moves the widened band from midrange only (0) to full range (100).
//                Output sets the return level (-inf .. 0 dB).
//
// The return is 100 % wet. Neither algorithm has a dry/wet Mix control: the send level sets the blend.
// MicroShift's low band (below Focus) is not returned, because the dry vocal already carries it.
namespace kv
{
    struct WidenerSettings
    {
        enum Type { MicroShift, SideWidener };
        int   type = MicroShift;

        int   msStyle = 0;            // 0 = I, 1 = II, 2 = III
        float msDetune = 1.0f;        // 0..2 (0..200 %)
        float msDelay = 1.0f;         // 0..2 (0..200 %)
        float msFocusHz = 20.0f;      // 20 Hz .. 10 kHz

        float swWidth = 0.5f;         // 0..1
        int   swMode = 1;             // 0 = Mode 1, 1 = Mode 2, 2 = Mode 3
        float swTone = 0.5f;          // 0..1
        float swOutputDb = 0.0f;      // -60 (= -inf) .. 0
    };

    inline const char* microShiftStyleDescription (int s)
    {
        static const char* text[3] = {
            "Style I: moderate pitch and delay variation with soft analog-style saturation.",
            "Style II: a different shifting algorithm with more delay variation and a darker, thinner response.",
            "Style III: much wider delay variation, harder saturation and a hard, short de-glitch crossfade.",
        };
        return text[std::clamp (s, 0, 2)];
    }

    inline const char* sideWidenerModeDescription (int m)
    {
        static const char* text[3] = {
            "Mode 1: most subtle width; keeps the time domain from smearing.",
            "Mode 2: between Mode 1 and Mode 3.",
            "Mode 3: maximum width with time-domain smearing, close to a room reverb.",
        };
        return text[std::clamp (m, 0, 2)];
    }

    class MicroShiftEngine
    {
    public:
        void prepare (float sampleRate)
        {
            fs = sampleRate;
            for (auto& l : line) l.prepare ((int) (0.12f * fs));
            reset();
        }

        void reset()
        {
            for (auto& l : line) l.reset();
            for (int c = 0; c < 2; ++c)
            {
                phase[c] = c * 0.25f;
                pitchLfo[c] = delayLfo[c] = (float) c * 0.37f;
                walk[c].reset (0x5151u + (uint32_t) c * 101u);
                hp1[c].reset(); hp2[c].reset(); tone[c].reset(); toneHp[c].reset();
            }
        }

        void process (const float* inL, const float* inR, float* outL, float* outR, int n, const WidenerSettings& s)
        {
            const auto& st = styles[std::clamp (s.msStyle, 0, 2)];
            const float focus = std::clamp (s.msFocusHz, 20.0f, 10000.0f);
            for (int c = 0; c < 2; ++c)
            {
                hp1[c].set (Biquad::HighPass, fs, focus, 0.7071f);   // two Butterworth stages = 24 dB/oct crossover
                hp2[c].set (Biquad::HighPass, fs, focus, 0.7071f);
                tone[c].set (Biquad::LowPass, fs, st.lowPassHz, 0.707f);
                toneHp[c].set (Biquad::HighPass, fs, st.highPassHz, 0.707f);
            }
            const float window = st.windowMs * 0.001f * fs;
            const float detune = std::clamp (s.msDetune, 0.0f, 2.0f), delayAmt = std::clamp (s.msDelay, 0.0f, 2.0f);

            for (int i = 0; i < n; ++i)
            {
                const float x[2] = { inL[i], inR[i] };
                float y[2];
                for (int c = 0; c < 2; ++c)
                {
                    // continuously varying detune (cents) and delay (ms)
                    pitchLfo[c] += st.pitchRateHz * (c == 0 ? 1.0f : 1.13f) / fs;
                    pitchLfo[c] -= std::floor (pitchLfo[c]);
                    delayLfo[c] += st.delayRateHz * (c == 0 ? 1.0f : 0.87f) / fs;
                    delayLfo[c] -= std::floor (delayLfo[c]);
                    const float cents = (c == 0 ? 1.0f : -1.0f) * st.cents * detune
                                      * (1.0f + st.pitchVar * std::sin (twoPi * pitchLfo[c]));
                    const float var = st.randomDelay ? walk[c].next (st.delayRateHz, fs) : std::sin (twoPi * delayLfo[c]);
                    const float baseMs = std::max (0.5f, (st.delayMs[c] + st.delayVarMs * var) * delayAmt);
                    const float base = baseMs * 0.001f * fs;

                    float in = x[c];
                    if (st.drive > 0.0f)
                        in = st.asymmetric ? softClip ((in + 0.2f * in * in) * st.drive) / st.drive
                                           : softClip (in * st.drive) / st.drive;
                    line[c].push (in);

                    // two read taps sweep across a window in opposite halves; their crossfade hides the wrap
                    const float ratio = std::pow (2.0f, cents / 1200.0f);
                    phase[c] += (1.0f - ratio) / window;
                    phase[c] -= std::floor (phase[c]);
                    const float p1 = phase[c], p2 = p1 + 0.5f - std::floor (p1 + 0.5f);
                    const float g1 = crossfade (p1, st.hardDeglitch), g2 = crossfade (p2, st.hardDeglitch);
                    float v = g1 * line[c].read (base + p1 * window) + g2 * line[c].read (base + p2 * window);
                    v = toneHp[c].process (tone[c].process (v));
                    y[c] = hp2[c].process (hp1[c].process (v));
                }
                outL[i] = flushDenormal (y[0]);
                outR[i] = flushDenormal (y[1]);
            }
        }

    private:
        struct Style
        {
            float cents, pitchVar, pitchRateHz;
            float delayMs[2], delayVarMs, delayRateHz;
            bool  randomDelay;
            float windowMs;
            bool  hardDeglitch;
            float drive;
            bool  asymmetric;
            float lowPassHz, highPassHz;
        };

        static constexpr Style styles[3] = {
            { 9.0f, 0.25f, 0.13f, { 7.0f, 11.0f }, 1.5f, 0.21f, false, 40.0f, false, 1.8f, false, 16000.0f, 20.0f },
            { 7.0f, 0.35f, 0.09f, { 9.0f, 14.0f }, 2.5f, 0.17f, false, 55.0f, false, 0.0f, false, 10000.0f, 150.0f },
            { 11.0f, 0.20f, 0.11f, { 5.0f, 17.0f }, 6.0f, 0.07f, true, 25.0f, true, 2.5f, true, 13000.0f, 20.0f },
        };

        static float crossfade (float p, bool hard) noexcept
        {
            if (! hard)
                return std::sin ((float) pi * p);
            // trapezoid: full level for most of the window, short 15 % ramps at the ends
            return std::clamp (std::min (p, 1.0f - p) / 0.15f, 0.0f, 1.0f);
        }

        float fs = 48000.0f;
        DelayLine line[2];
        float phase[2] {}, pitchLfo[2] {}, delayLfo[2] {};
        SmoothRandom walk[2];
        Biquad hp1[2], hp2[2], tone[2], toneHp[2];
    };

    class SideWidenerEngine
    {
    public:
        void prepare (float sampleRate)
        {
            fs = sampleRate;
            line.prepare ((int) (0.05f * fs));
            for (auto& a : ap) a.prepare ((int) (0.05f * fs));
            reset();
        }

        void reset()
        {
            line.reset();
            for (auto& a : ap) a.reset();
            hp.reset(); lp.reset();
        }

        void process (const float* inL, const float* inR, float* outL, float* outR, int n, const WidenerSettings& s)
        {
            const float tone = std::clamp (s.swTone, 0.0f, 1.0f);
            // Tone 0: midrange only (350 Hz .. 4.5 kHz). Tone 100: full range.
            hp.set (Biquad::HighPass, fs, 350.0f * std::pow (20.0f / 350.0f, tone), 0.707f);
            lp.set (Biquad::LowPass, fs, std::min (4500.0f * std::pow (20000.0f / 4500.0f, tone), 0.45f * fs), 0.707f);
            const float out = s.swOutputDb <= -60.0f ? 0.0f : dbToGain (s.swOutputDb);
            const float g = std::clamp (s.swWidth, 0.0f, 1.0f) * out;
            const int mode = std::clamp (s.swMode, 0, 2);

            for (int i = 0; i < n; ++i)
            {
                const float m = 0.5f * (inL[i] + inR[i]);
                line.push (m);
                float side = 0.0f;
                if (mode == 0)
                {
                    side = line.read (ms (3.5f));
                }
                else if (mode == 1)
                {
                    side = 0.55f * line.read (ms (7.3f)) - 0.45f * line.read (ms (13.1f))
                         + 0.35f * line.read (ms (19.7f)) - 0.25f * line.read (ms (27.9f));
                }
                else
                {
                    static constexpr float apMs[6] = { 5.3f, 7.9f, 11.3f, 17.9f, 29.3f, 41.1f };
                    side = m;
                    for (int k = 0; k < 6; ++k)
                        side = ap[k].process (side, ms (apMs[k]), 0.6f);
                }
                side = g * lp.process (hp.process (side));
                outL[i] = flushDenormal (side);
                outR[i] = flushDenormal (-side);   // pure side: the mono sum of the return is zero
            }
        }

    private:
        float ms (float v) const noexcept { return v * 0.001f * fs; }

        float fs = 48000.0f;
        DelayLine line;
        Allpass ap[6];
        Biquad hp, lp;
    };

    class WidenerSend
    {
    public:
        void prepare (double sampleRate, int)
        {
            fs = (float) sampleRate;
            micro.prepare (fs);
            side.prepare (fs);
            reset();
        }

        void reset()
        {
            micro.reset();
            side.reset();
            fade = 1.0f;
            pending = -1;
        }

        void process (const float* inL, const float* inR, float* outL, float* outR, int n, const WidenerSettings& s)
        {
            if (s.type != active && pending < 0)
                pending = s.type;

            // Only the selected algorithm runs. A switch fades the old one out over 10 ms, resets it,
            // then fades the new one in.
            WidenerSettings run = s;
            run.type = active;
            if (active == WidenerSettings::MicroShift)
                micro.process (inL, inR, outL, outR, n, run);
            else
                side.process (inL, inR, outL, outR, n, run);

            const float step = 1.0f / (0.01f * fs);
            for (int i = 0; i < n; ++i)
            {
                if (pending >= 0)
                {
                    fade = std::max (0.0f, fade - step);
                    if (fade <= 0.0f)
                    {
                        // remaining samples of this block stay silent; the new type starts next block
                        for (int k = i; k < n; ++k) outL[k] = outR[k] = 0.0f;
                        micro.reset();
                        side.reset();
                        active = pending;
                        pending = -1;
                        return;
                    }
                }
                else if (fade < 1.0f)
                {
                    fade = std::min (1.0f, fade + step);
                }
                outL[i] *= fade;
                outR[i] *= fade;
            }
        }

        int getActiveType() const noexcept { return active; }

        // Switches algorithm without a crossfade (used when the send starts from silence).
        void forceType (int type)
        {
            active = type == WidenerSettings::SideWidener ? WidenerSettings::SideWidener : WidenerSettings::MicroShift;
            reset();
        }

    private:
        float fs = 48000.0f;
        MicroShiftEngine micro;
        SideWidenerEngine side;
        int active = WidenerSettings::MicroShift, pending = -1;
        float fade = 1.0f;
    };
}
