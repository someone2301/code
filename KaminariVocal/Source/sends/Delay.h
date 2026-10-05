#pragma once

#include "DspCommon.h"

// Delay send (echo). Original implementation; the control set follows the user-supplied echo manual
// (Single / Dual / Ping-Pong modes, styles, saturation, groove, feel, accent, width, L/R offset, prime numbers,
// wobble, diffusion).
//
// Each echo path is two delay stages in series, A then B, with feedback from the end of B back into A.
// Odd repeats leave stage A and even repeats leave stage B, so Groove (stage A longer, B shorter, or the other
// way round) gives shuffle/swing and Accent (different output levels for A and B) gives alternating accents
// without changing the repeat rate. The style's tone and saturation sit inside each stage, so every repeat is
// coloured once more than the one before it. Output is 100 % wet.
namespace kv
{
    struct DelaySettings
    {
        enum Mode { Single, Dual, PingPong };
        enum WobbleShape { Sine, Triangle, Square, RandomWalk, RandomSH };

        int   mode = Single;
        int   style = 1;
        float time1Samples = 18000.0f;   // Echo 1 / Ping
        float time2Samples = 24000.0f;   // Echo 2 / Pong (Dual and Ping-Pong)
        float feedback = 0.3f;           // 0..1
        float loCutHz = 150.0f, hiCutHz = 6000.0f;
        float saturation = 0.3f;         // 0..1
        float width = 0.5f;              // 0..1 (above 0.75 adds out-of-phase spread)
        float offsetMs = 8.0f;           // L/R offset, 0..25 ms
        float accent1 = 0.0f, accent2 = 0.0f;   // -1..1
        float balance = 0.0f;            // -1 (left) .. 1 (right)
        float fbMix = 0.0f;              // 0 = independent, 0.5 = equal, 1 = fully crossed (Dual)
        float fbBalance = 0.0f;          // -1..1 (Dual)
        float groove = 0.0f;             // -1 shuffle .. 1 swing
        float feelMs = 0.0f;             // -50 (rush) .. +50 (drag)
        bool  primeNumbers = false;
        float wobble = 0.0f, wobbleRateHz = 1.0f;
        int   wobbleShape = Sine;
        float wobbleSync = 0.0f;         // -1 rates drift apart .. 0 locked .. 1 phases opposed
        float diffusion = 0.0f, diffusionSize = 0.5f;
        bool  diffusionInLoop = false;
    };

    inline constexpr int numDelayStyles = 6;
    inline const char* delayStyleName (int i)
    {
        static const char* names[numDelayStyles] = { "Clean Digital", "Studio Tape", "Worn Tape", "Analog Bucket", "Lo-Fi Radio", "Diffused" };
        return names[std::clamp (i, 0, numDelayStyles - 1)];
    }
    inline const char* delayStyleDescription (int i)
    {
        static const char* text[numDelayStyles] = {
            "Clean, full-bandwidth repeats. Saturation acts as a gentle limiter.",
            "High-fidelity tape: high-frequency compression and soft low/mid saturation.",
            "Worn tape echo: darker repeats, low-end bump, built-in wow and stronger saturation.",
            "Bucket-brigade analog delay: narrow bandwidth, warm and lightly distorted.",
            "Narrow mid-band radio tone with hard clipping and reduced resolution.",
            "Repeats smeared by built-in diffusion, for reverb-like echoes.",
        };
        return text[std::clamp (i, 0, numDelayStyles - 1)];
    }

    // Tone and saturation applied once per pass through a stage.
    struct EchoColour
    {
        Biquad hp, lp, lp2, shelfIn, shelfOut, bump;
        LoFi lofi;
        int style = -1;

        void configure (int st, float fs, float loCut, float hiCut)
        {
            style = st;
            hp.set (Biquad::HighPass, fs, loCut, 0.707f);
            float styleLp = 20000.0f, styleHp = 20.0f;
            switch (st)
            {
                case 1: styleLp = 15000.0f; break;
                case 2: styleLp = 4500.0f; break;
                case 3: styleLp = 2800.0f; styleHp = 80.0f; break;
                case 4: styleLp = 3000.0f; styleHp = 500.0f; break;
                case 5: styleLp = 9000.0f; break;
                default: break;
            }
            lp.set (Biquad::LowPass, fs, std::min (hiCut, styleLp), 0.707f);
            lp2.set (Biquad::LowPass, fs, st == 3 || st == 4 ? styleLp : 0.45f * fs, 0.707f);
            if (styleHp > loCut)
                hp.set (Biquad::HighPass, fs, styleHp, 0.707f);
            shelfIn.set (Biquad::HighShelf, fs, 3000.0f, 0.707f, 6.0f);
            shelfOut.set (Biquad::HighShelf, fs, 3000.0f, 0.707f, -6.0f);
            bump.set (Biquad::Peak, fs, 90.0f, 1.0f, 2.5f);
        }

        float process (float x, float sat, float fs) noexcept
        {
            x = lp.process (hp.process (x));
            switch (style)
            {
                case 0: { const float d = 1.0f + 2.0f * sat; return returnGuard (x * d) / d; }
                case 1: { const float d = 1.0f + 4.0f * sat; return shelfOut.process (softClip (shelfIn.process (x) * d) / d); }
                case 2: { const float d = 1.0f + 6.0f * sat; return softClip (bump.process (x) * d) / d; }
                case 3: { const float d = 1.0f + 3.0f * sat; const float y = lp2.process (x); return softClip ((y + 0.15f * y * y) * d) / d; }
                case 4: { const float d = 1.0f + 10.0f * sat;
                          const float y = std::clamp (lp2.process (x) * d, -1.0f, 1.0f) / d;
                          return lofi.process (y, std::max (1.0f, fs / 11025.0f), 8.0f); }
                default: { const float d = 1.0f + 2.0f * sat; return softClip (x * d) / d; }
            }
        }

        void reset() noexcept { hp.reset(); lp.reset(); lp2.reset(); shelfIn.reset(); shelfOut.reset(); bump.reset(); lofi.reset(); }
    };

    struct Diffuser
    {
        Allpass ap[4];
        void prepare (float fs) { for (auto& a : ap) a.prepare ((int) (0.03f * fs)); }
        void reset() { for (auto& a : ap) a.reset(); }
        float process (float x, float amount, float size, float fs) noexcept
        {
            if (amount <= 0.0f)
                return x;
            static constexpr float ms[4] = { 1.3f, 2.9f, 4.7f, 7.1f };
            for (int k = 0; k < 4; ++k)
                x = ap[k].process (x, std::max (2.0f, ms[k] * (0.3f + 2.7f * size) * 0.001f * fs), 0.7f * amount);
            return x;
        }
    };

    class DelaySend
    {
    public:
        void prepare (double sampleRate, int)
        {
            fs = (float) sampleRate;
            const int cap = (int) (3.6f * fs);
            for (auto& c : chains)
            {
                c.a.prepare (cap);
                c.b.prepare (cap);
                c.diffLoop.prepare (fs);
                c.diffPost.prepare (fs);
            }
            for (auto& s : timeSmooth) s.prepare (fs, 60.0f);
            reset();
        }

        void reset()
        {
            for (auto& c : chains)
            {
                c.a.reset(); c.b.reset(); c.colA.reset(); c.colB.reset(); c.diffLoop.reset(); c.diffPost.reset();
            }
            for (int k = 0; k < 4; ++k)
            {
                wobPhase[k] = 0.0f;
                wobRandom[k].reset (0x777u + 131u * (uint32_t) k);
                shRandom[k] = Random (0x999u + 17u * (uint32_t) k);
                shValue[k] = 0.0f;
            }
            primed = false;
        }

        void process (const float* inL, const float* inR, float* outL, float* outR, int n, const DelaySettings& s)
        {
            const int style = std::clamp (s.style, 0, numDelayStyles - 1);
            for (auto& c : chains)
            {
                c.colA.configure (style, fs, s.loCutHz, s.hiCutHz);
                c.colB.configure (style, fs, s.loCutHz, s.hiCutHz);
            }

            const float g = std::clamp (s.groove, -1.0f, 1.0f) / 3.0f;   // up to a triplet feel
            const bool ping = s.mode == DelaySettings::PingPong;
            const bool dual = s.mode == DelaySettings::Dual;
            float t1 = s.time1Samples, t2 = (dual || ping) ? s.time2Samples : s.time1Samples;
            float stage[4] = { t1 * (1.0f + g), t1 * (1.0f - g), t2 * (1.0f + g), t2 * (1.0f - g) };
            if (ping)
            {
                stage[0] = t1 * (1.0f + g);   // ping
                stage[1] = t2 * (1.0f - g);   // pong
            }
            for (auto& t : stage)
            {
                t = std::clamp (t, 2.0f, 3.3f * fs);
                if (s.primeNumbers)
                    t = (float) nearestPrime ((int) std::lround (t));
            }
            if (! primed)
            {
                for (int k = 0; k < 4; ++k) timeSmooth[k].snap (stage[k]);
                primed = true;
            }
            for (int k = 0; k < 4; ++k) timeSmooth[k].setTarget (stage[k]);

            const float fb = 0.97f * std::clamp (s.feedback, 0.0f, 1.0f);
            const float fbL = fb * (1.0f - std::max (0.0f, s.fbBalance));
            const float fbR = fb * (1.0f - std::max (0.0f, -s.fbBalance));
            const float w = std::clamp (s.width, 0.0f, 1.0f);
            const float widthIn = std::min (1.0f, w / 0.75f);
            const float sideGain = w <= 0.75f ? widthIn : 1.0f + 2.0f * (w - 0.75f) / 0.25f;
            const float offset = ping ? 0.0f : s.offsetMs * 0.001f * fs * widthIn;
            const float feel = s.feelMs * 0.001f * fs;
            const float wobDepth = s.wobble * s.wobble * 0.005f * fs + (style == 2 ? 0.00025f * fs : 0.0f);
            const float diffAmount = std::clamp (s.diffusion + (style == 5 ? 0.6f : 0.0f), 0.0f, 1.0f);
            const float balL = s.balance > 0.0f ? 1.0f - s.balance : 1.0f;
            const float balR = s.balance < 0.0f ? 1.0f + s.balance : 1.0f;
            const bool useBalance = dual || ping;

            auto accentGains = [] (float a, float& ga, float& gb)
            {
                ga = a < 0.0f ? 1.0f + 0.8f * a : 1.0f;   // a < 0 emphasises the even (off-beat) repeats
                gb = a > 0.0f ? 1.0f - 0.8f * a : 1.0f;   // a > 0 emphasises the odd repeats
            };
            float accA[2], accB[2];
            accentGains (ping ? 0.0f : s.accent1, accA[0], accB[0]);
            accentGains (ping ? 0.0f : (dual ? s.accent2 : s.accent1), accA[1], accB[1]);

            for (int i = 0; i < n; ++i)
            {
                float tA[2], tB[2];
                const float sa0 = timeSmooth[0].next(), sb0 = timeSmooth[1].next();
                const float sa1 = timeSmooth[2].next(), sb1 = timeSmooth[3].next();
                tA[0] = sa0; tB[0] = sb0;
                tA[1] = dual ? sa1 : sa0; tB[1] = dual ? sb1 : sb0;

                float wob[4];
                wobble (s, wobDepth, wob);

                float yl = 0.0f, yr = 0.0f;
                if (ping)
                {
                    auto& c = chains[0];
                    const float m = 0.5f * (inL[i] + inR[i]);   // ping-pong always sums the input to mono
                    const float yA = c.a.read (tA[0] + wob[0]);
                    const float yB = c.b.read (tB[0] + wob[1]);
                    float inA = c.colA.process (m + fb * yB, s.saturation, fs);
                    if (s.diffusionInLoop) inA = c.diffLoop.process (inA, diffAmount, s.diffusionSize, fs);
                    c.a.push (flushDenormal (inA));
                    c.b.push (flushDenormal (c.colB.process (yA, s.saturation, fs)));
                    yl = c.a.read (std::max (1.0f, tA[0] + feel + wob[0]));
                    yr = c.b.read (std::max (1.0f, tB[0] + feel + wob[1]));
                    if (! s.diffusionInLoop)
                    {
                        yl = c.diffPost.process (yl, diffAmount, s.diffusionSize, fs);
                        yr = chains[1].diffPost.process (yr, diffAmount, s.diffusionSize, fs);
                    }
                }
                else
                {
                    float yBnow[2] = { chains[0].b.read (tB[0] + wob[1]), chains[1].b.read (tB[1] + wob[3]) };
                    const float mix = dual ? std::clamp (s.fbMix, 0.0f, 1.0f) : 0.0f;
                    float out[2];
                    for (int c = 0; c < 2; ++c)
                    {
                        auto& ch = chains[c];
                        const float x = c == 0 ? inL[i] : inR[i];
                        const float fbc = dual ? (c == 0 ? fbL : fbR) : fb;
                        const float back = (1.0f - mix) * yBnow[c] + mix * yBnow[1 - c];
                        const float yA = ch.a.read (tA[c] + wob[2 * c]);
                        float inA = ch.colA.process (x + fbc * back, s.saturation, fs);
                        if (s.diffusionInLoop) inA = ch.diffLoop.process (inA, diffAmount, s.diffusionSize, fs);
                        ch.a.push (flushDenormal (inA));
                        ch.b.push (flushDenormal (ch.colB.process (fbc * yA, s.saturation, fs)));
                        const float shift = feel + (c == 1 ? offset : 0.0f);
                        float y = accA[c] * ch.a.read (std::max (1.0f, tA[c] + shift + wob[2 * c]))
                                + accB[c] * ch.b.read (std::max (1.0f, tB[c] + shift + wob[2 * c + 1]));
                        if (! s.diffusionInLoop)
                            y = ch.diffPost.process (y, diffAmount, s.diffusionSize, fs);
                        out[c] = y;
                    }
                    yl = out[0];
                    yr = out[1];
                }

                if (useBalance) { yl *= balL; yr *= balR; }
                const float m = 0.5f * (yl + yr), sd = 0.5f * (yl - yr) * sideGain;
                outL[i] = flushDenormal (m + sd);
                outR[i] = flushDenormal (m - sd);
            }
        }

    private:
        void wobble (const DelaySettings& s, float depth, float* out) noexcept
        {
            if (depth <= 0.0f)
            {
                std::fill (out, out + 4, 0.0f);
                return;
            }
            const float drift = std::max (0.0f, -s.wobbleSync), opposed = std::max (0.0f, s.wobbleSync);
            for (int k = 0; k < 4; ++k)
            {
                const float rate = s.wobbleRateHz * (1.0f + drift * 0.31f * (float) k);
                wobPhase[k] += rate / fs;
                const bool wrapped = wobPhase[k] >= 1.0f;
                wobPhase[k] -= std::floor (wobPhase[k]);
                // right-channel paths (k = 2, 3) move towards the opposite phase as Sync turns clockwise
                float ph = wobPhase[k] + (k >= 2 ? 0.5f * opposed : 0.0f);
                ph -= std::floor (ph);
                float v = 0.0f;
                switch (s.wobbleShape)
                {
                    case DelaySettings::Triangle:   v = 4.0f * std::abs (ph - 0.5f) - 1.0f; break;
                    case DelaySettings::Square:     v = std::tanh (8.0f * std::sin (twoPi * ph)); break;
                    case DelaySettings::RandomWalk: v = wobRandom[k].next (rate, fs); break;
                    case DelaySettings::RandomSH:   if (wrapped) shValue[k] = shRandom[k].next(); v = shValue[k]; break;
                    default:                        v = std::sin (twoPi * ph); break;
                }
                out[k] = depth * (1.0f + v);   // keeps the read position at or after the nominal time
            }
        }

        struct Chain
        {
            DelayLine a, b;
            EchoColour colA, colB;
            Diffuser diffLoop, diffPost;
        };

        float fs = 48000.0f;
        Chain chains[2];
        Smoother timeSmooth[4];
        bool primed = false;
        float wobPhase[4] {};
        SmoothRandom wobRandom[4];
        Random shRandom[4];
        float shValue[4] {};
    };
}
