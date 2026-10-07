#pragma once

#include "DspCommon.h"

// Widener send with two separate, selectable algorithms (never layered):
//
//   MicroShift   micro pitch-shift widening after the user-supplied MicroShift manual: the left side is
//                shifted up and the right side down by a few continuously varying cents, each with a
//                continuously varying short delay. Splices are matched to the waveform (MicroShiftEngine), so the
//                shifter does not flange or warble. Style I / II / III differ in pitch and delay variation,
//                frequency response, saturation and splice length. Detune and Delay scale
//                the style's amounts in percent (100 % = the style's own amount). Focus is the crossover
//                frequency: only the band above it is widened.
//   SideWidener  mono-compatible pseudo-stereo after the user-supplied SideWidener manual: a decorrelated copy
//                of the mid signal is returned as pure side (+ left, - right), so the mono sum of the return is
//                exactly zero. The copy comes from allpass chains (2, 4 or 6 stages), which avoid the evenly spaced
//                comb notches of a plain delay. Mode 1 is subtle without time smearing, Mode 3 is widest with a
//                room-like smear, Mode 2 lies between. Tone moves the widened band from midrange only (0) to full range (100).
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
            "Style I: smooth and open; moderate pitch and delay variation with light analog-style saturation.",
            "Style II: a different shifting algorithm with more delay variation and a darker, thinner response.",
            "Style III: widest; wandering delays, firmer saturation and shorter splices.",
        };
        return text[std::clamp (s, 0, 2)];
    }

    inline const char* sideWidenerModeDescription (int m)
    {
        static const char* text[3] = {
            "Mode 1: most subtle width; short phase scrambling, no audible time smearing.",
            "Mode 2: between Mode 1 and Mode 3.",
            "Mode 3: maximum width with time-domain smearing, close to a room reverb.",
        };
        return text[std::clamp (m, 0, 2)];
    }

    // Micro pitch shifter, one per side (left up, right down). It reads its delay line with one tap whose delay
    // drifts at (1 - ratio) samples per sample, which shifts the pitch. When the tap drifts out of its range it
    // splices to a new position one "jump" away. The splice point is the lag (within +-searchMs of that jump) whose
    // waveform best matches the outgoing tap (normalised cross-correlation), and the two are crossfaded with
    // equal-gain raised-cosine curves. Matched splices add in phase, so there is no comb filtering or flanging during
    // the crossfade, and only one tap is heard the rest of the time.
    class MicroShiftEngine
    {
    public:
        void prepare (float sampleRate)
        {
            fs = sampleRate;
            for (auto& l : line) l.prepare ((int) (0.16f * fs));
            reset();
        }

        void reset()
        {
            for (auto& l : line) l.reset();
            for (int c = 0; c < 2; ++c)
            {
                pitchLfo[c] = delayLfo[c] = (float) c * 0.37f;
                walk[c].reset (0x5151u + (uint32_t) c * 101u);
                hp1[c].reset(); hp2[c].reset(); tone[c].reset(); toneHp[c].reset();
                tap[c] = { };
                tap[c].d = -1.0f;   // placed on the first sample
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
            const float jump = st.jumpMs * 0.001f * fs;              // splice distance
            const float xfLen = st.crossfadeMs * 0.001f * fs;        // splice crossfade length
            const float detune = std::clamp (s.msDetune, 0.0f, 2.0f), delayAmt = std::clamp (s.msDelay, 0.0f, 2.0f);

            for (int i = 0; i < n; ++i)
            {
                const float x[2] = { inL[i], inR[i] };
                float y[2];
                for (int c = 0; c < 2; ++c)
                {
                    // slowly varying detune (cents) and delay (ms)
                    pitchLfo[c] += st.pitchRateHz * (c == 0 ? 1.0f : 1.13f) / fs;
                    pitchLfo[c] -= std::floor (pitchLfo[c]);
                    delayLfo[c] += st.delayRateHz * (c == 0 ? 1.0f : 0.87f) / fs;
                    delayLfo[c] -= std::floor (delayLfo[c]);
                    const float cents = (c == 0 ? 1.0f : -1.0f) * st.cents * detune
                                      * (1.0f + st.pitchVar * std::sin (twoPi * pitchLfo[c]));
                    const float var = st.randomDelay ? walk[c].next (st.delayRateHz, fs) : std::sin (twoPi * delayLfo[c]);
                    const float baseMs = std::max (1.0f, (st.delayMs[c] + st.delayVarMs * var) * delayAmt);
                    const float base = baseMs * 0.001f * fs + compareLen() + 2.0f;   // room to compare ahead of the tap

                    float in = x[c];
                    if (st.drive > 0.0f)
                        in = st.asymmetric ? softClip ((in + 0.1f * in * in) * st.drive) / st.drive
                                           : softClip (in * st.drive) / st.drive;
                    line[c].push (in);

                    auto& t = tap[c];
                    if (t.d < 0.0f) t.d = jump;   // start in the middle of the range
                    const float ratio = std::pow (2.0f, cents / 1200.0f);
                    const float drift = 1.0f - ratio;
                    t.d += drift;
                    if (t.xf >= 0.0f) t.d2 += drift;

                    // out of range: splice one jump back towards the middle, at the best-matching lag
                    if (t.xf < 0.0f && (t.d < 0.0f || t.d > 2.0f * jump))
                    {
                        const float target = t.d < 0.0f ? t.d + jump : t.d - jump;
                        t.d2 = bestSplice (line[c], base + t.d, base + target, st.searchMs * 0.001f * fs) - base;
                        t.xf = 0.0f;
                    }

                    float v = line[c].read (base + t.d);
                    if (t.xf >= 0.0f)
                    {
                        const float a = 0.5f - 0.5f * std::cos ((float) pi * std::min (1.0f, t.xf / xfLen));   // 0 -> 1
                        v = (1.0f - a) * v + a * line[c].read (base + t.d2);
                        t.xf += 1.0f;
                        if (t.xf >= xfLen) { t.d = t.d2; t.xf = -1.0f; }
                    }
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
            float jumpMs, crossfadeMs, searchMs;
            float drive;
            bool  asymmetric;
            float lowPassHz, highPassHz;
        };

        // I: smooth and open. II: darker and thinner, more delay movement. III: widest, wandering delays, firmer drive.
        static constexpr Style styles[3] = {
            { 9.0f,  0.20f, 0.13f, { 7.0f, 11.0f }, 1.0f, 0.21f, false, 22.0f, 12.0f, 6.0f, 1.15f, false, 16000.0f, 20.0f },
            { 7.0f,  0.30f, 0.09f, { 9.0f, 14.0f }, 2.0f, 0.17f, false, 26.0f, 14.0f, 6.0f, 0.0f,  false, 10000.0f, 150.0f },
            { 11.0f, 0.20f, 0.11f, { 5.0f, 17.0f }, 4.0f, 0.07f, true,  18.0f, 8.0f,  5.0f, 1.5f,  true,  13000.0f, 20.0f },
        };

        int compareLen() const noexcept { return (int) (0.006f * fs); }   // 6 ms compared at a splice

        // Delay (in samples) near `target` (within +-search) whose next compareLen samples best match those of the
        // current tap at `from`: normalised cross-correlation, coarse step then refined.
        float bestSplice (const DelayLine& l, float from, float target, float search) const
        {
            const int len = compareLen();
            const int lo = std::max (len + 2, (int) (target - search)), hi = std::min (l.capacity() - 4, (int) (target + search));
            if (hi <= lo) return target;
            auto score = [&] (int d)
            {
                double xy = 0, yy = 1.0e-12;
                for (int j = 0; j < len; j += 2)
                {
                    const float a = l.readInt ((int) from - j), b = l.readInt (d - j);
                    xy += (double) a * b;
                    yy += (double) b * b;
                }
                return xy / std::sqrt (yy);
            };
            int best = (int) target;
            double bestScore = -1.0e30;
            for (int d = lo; d <= hi; d += 3)
                if (const double sc = score (d); sc > bestScore) { bestScore = sc; best = d; }
            for (int d = std::max (lo, best - 2); d <= std::min (hi, best + 2); ++d)
                if (const double sc = score (d); sc > bestScore) { bestScore = sc; best = d; }
            return (float) best + (from - std::floor (from));   // keep the fractional part so the splice is seamless
        }

        struct Tap { float d = -1.0f, d2 = 0.0f, xf = -1.0f; };

        float fs = 48000.0f;
        DelayLine line[2];
        Tap tap[2];
        float pitchLfo[2] {}, delayLfo[2] {};
        SmoothRandom walk[2];
        Biquad hp1[2], hp2[2], tone[2], toneHp[2];
    };

    class SideWidenerEngine
    {
    public:
        void prepare (float sampleRate)
        {
            fs = sampleRate;
            for (auto& a : ap) a.prepare ((int) (0.05f * fs));
            reset();
        }

        void reset()
        {
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
            // side at most 0.7 x the mid: in each speaker (mid +- side) the dips where the decorrelated copy is out of
            // phase stay above about -10.5 dB instead of cancelling completely
            const float g = 0.7f * std::clamp (s.swWidth, 0.0f, 1.0f) * out;
            const int mode = std::clamp (s.swMode, 0, 2);

            for (int i = 0; i < n; ++i)
            {
                const float m = 0.5f * (inL[i] + inR[i]);
                // decorrelated copy of the mid: short allpass chains scramble the phase without the evenly spaced comb
                // notches a plain delay leaves in each speaker; more and longer stages = wider and more diffuse
                static constexpr float apMs[3][6] = { { 1.3f, 2.9f, 0, 0, 0, 0 },
                                                      { 1.7f, 3.1f, 5.3f, 7.9f, 0, 0 },
                                                      { 5.3f, 7.9f, 11.3f, 17.9f, 29.3f, 41.1f } };
                static constexpr int stages[3] = { 2, 4, 6 };
                static constexpr float apGain[3] = { 0.5f, 0.55f, 0.6f };
                float side = m;
                for (int k = 0; k < stages[mode]; ++k)
                    side = ap[k].process (side, ms (apMs[mode][k]), apGain[mode]);
                side = g * lp.process (hp.process (side));
                outL[i] = flushDenormal (side);
                outR[i] = flushDenormal (-side);   // pure side: the mono sum of the return is zero
            }
        }

    private:
        float ms (float v) const noexcept { return v * 0.001f * fs; }

        float fs = 48000.0f;
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
