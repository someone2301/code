#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Small building blocks shared by the send effects. Everything here is allocation-free after prepare().
namespace kv
{
    constexpr double pi = 3.14159265358979323846;
    constexpr float  twoPi = 6.28318530717958647692f;

    inline float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

    inline float flushDenormal (float x) noexcept { return std::abs (x) < 1.0e-15f ? 0.0f : x; }

    // Circular delay line with fractional (cubic Hermite) reads. Capacity is fixed in prepare().
    class DelayLine
    {
    public:
        void prepare (int maxDelaySamples)
        {
            int size = 1;
            while (size < maxDelaySamples + 4)
                size <<= 1;
            buffer.assign ((size_t) size, 0.0f);
            mask = size - 1;
            writePos = 0;
        }

        void reset() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); writePos = 0; }

        int capacity() const noexcept { return mask - 3; }

        void push (float x) noexcept
        {
            buffer[(size_t) writePos] = x;
            writePos = (writePos + 1) & mask;
        }

        // Delay in samples, measured from the most recently pushed sample (0 = that sample).
        float readInt (int d) const noexcept
        {
            return buffer[(size_t) ((writePos - 1 - d) & mask)];
        }

        float read (float d) const noexcept
        {
            d = std::clamp (d, 1.0f, (float) capacity());
            const int i = (int) d;
            const float f = d - (float) i;
            const float xm1 = readInt (i - 1), x0 = readInt (i), x1 = readInt (i + 1), x2 = readInt (i + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }

    private:
        std::vector<float> buffer;
        int mask = 0, writePos = 0;
    };

    // Schroeder allpass around a (possibly modulated) delay line.
    class Allpass
    {
    public:
        void prepare (int maxDelaySamples) { line.prepare (maxDelaySamples); }
        void reset() noexcept { line.reset(); }

        float process (float x, float delay, float g) noexcept
        {
            const float d = line.read (delay);
            const float v = x + g * d;
            line.push (flushDenormal (v));
            return d - g * v;
        }

    private:
        DelayLine line;
    };

    struct OnePoleLP
    {
        float a = 0.0f, z = 0.0f;
        void setCutoff (float hz, float fs) noexcept { a = std::exp (-twoPi * std::min (hz, 0.49f * fs) / fs); }
        float process (float x) noexcept { z = flushDenormal (x + a * (z - x)); return z; }
        void reset() noexcept { z = 0.0f; }
    };

    struct OnePoleHP
    {
        OnePoleLP lp;
        void setCutoff (float hz, float fs) noexcept { lp.setCutoff (hz, fs); }
        float process (float x) noexcept { return x - lp.process (x); }
        void reset() noexcept { lp.reset(); }
    };

    // RBJ biquad, transposed direct form II.
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        enum Kind { LowPass, HighPass, BandPass, LowShelf, HighShelf, Peak };

        void set (Kind k, float fs, float hz, float q, float gainDb = 0.0f) noexcept
        {
            hz = std::clamp (hz, 5.0f, 0.49f * fs);
            const double w = 2.0 * pi * hz / fs, cw = std::cos (w), sw = std::sin (w);
            const double alpha = sw / (2.0 * std::max (q, 0.05f));
            const double A = std::pow (10.0, gainDb / 40.0), beta = 2.0 * std::sqrt (A) * alpha;
            double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
            switch (k)
            {
                case LowPass:  B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = B0; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
                case HighPass: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = B0; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
                case BandPass: B0 = alpha; B1 = 0; B2 = -alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
                case Peak:     B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A; A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
                case LowShelf:
                    B0 = A * ((A + 1) - (A - 1) * cw + beta); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - beta);
                    A0 = (A + 1) + (A - 1) * cw + beta; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - beta; break;
                case HighShelf:
                    B0 = A * ((A + 1) + (A - 1) * cw + beta); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - beta);
                    A0 = (A + 1) - (A - 1) * cw + beta; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - beta; break;
            }
            b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
            a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
        }

        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = flushDenormal (b1 * x - a1 * y + z2);
            z2 = flushDenormal (b2 * x - a2 * y);
            return y;
        }

        void reset() noexcept { z1 = z2 = 0.0f; }
    };

    // Deterministic xorshift generator (no global state, safe on the audio thread).
    struct Random
    {
        uint32_t s = 0x9e3779b9u;
        explicit Random (uint32_t seed = 0x9e3779b9u) : s (seed ? seed : 1u) {}
        float next() noexcept   // -1 .. 1
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            return (float) (s & 0xffffff) / 8388607.5f - 1.0f;
        }
    };

    // Slowly wandering random value (-1 .. 1): a new target each period, smoothed with a cosine.
    struct SmoothRandom
    {
        Random rng;
        float from = 0, to = 0, phase = 1;
        explicit SmoothRandom (uint32_t seed = 1) : rng (seed) {}
        float next (float rateHz, float fs) noexcept
        {
            phase += rateHz / fs;
            if (phase >= 1.0f)
            {
                phase -= std::floor (phase);
                from = to;
                to = rng.next();
            }
            const float w = 0.5f - 0.5f * std::cos ((float) pi * phase);
            return from + (to - from) * w;
        }
        void reset (uint32_t seed) noexcept { rng = Random (seed); from = to = 0; phase = 1; }
    };

    inline float softClip (float x) noexcept { return std::tanh (x); }

    // LFO waveform at phase 0..1, output -1..1. 0 sine, 1 triangle, 2 square (edges rounded to avoid clicks).
    inline float lfoShape (int shape, float phase) noexcept
    {
        phase -= std::floor (phase);
        switch (shape)
        {
            case 1:  { const float q = phase < 0.25f ? phase : (phase < 0.75f ? 0.5f - phase : phase - 1.0f); return 4.0f * q; }
            case 2:  return std::tanh (6.0f * std::sin (twoPi * phase)) / std::tanh (6.0f);
            default: return std::sin (twoPi * phase);
        }
    }

    // Smooth limiter used on every return so a send can never overload the output.
    // Transparent below 0.8 (about -2 dBFS), approaches 1.0 asymptotically above.
    inline float returnGuard (float x) noexcept
    {
        constexpr float knee = 0.8f;
        const float a = std::abs (x);
        if (a <= knee)
            return x;
        const float over = (a - knee) / (1.0f - knee);
        return std::copysign (knee + (1.0f - knee) * std::tanh (over), x);
    }

    // Reduces resolution and holds samples, the way early digital units did.
    struct LoFi
    {
        float held = 0.0f, counter = 0.0f;
        float process (float x, float holdRatio, float bits) noexcept
        {
            counter += 1.0f;
            if (counter >= holdRatio)
            {
                counter -= holdRatio;
                const float steps = std::pow (2.0f, bits - 1.0f);
                held = std::round (x * steps) / steps;
            }
            return held;
        }
        void reset() noexcept { held = 0.0f; counter = 0.0f; }
    };

    // Exponential parameter smoother (one-pole).
    struct Smoother
    {
        float value = 0, target = 0, coeff = 0;
        void prepare (float fs, float ms) noexcept { coeff = std::exp (-1.0f / (0.001f * ms * fs)); }
        void setTarget (float t) noexcept { target = t; }
        void snap (float v) noexcept { value = target = v; }
        float next() noexcept
        {
            value = target + coeff * (value - target);
            if (std::abs (value - target) < 1.0e-7f)
                value = target;
            return value;
        }
    };

    inline bool isPrime (int n) noexcept
    {
        if (n < 2) return false;
        for (int d = 2; d * d <= n; ++d)
            if (n % d == 0) return false;
        return true;
    }

    inline int nearestPrime (int n) noexcept
    {
        for (int k = 0; k < 1000; ++k)
        {
            if (isPrime (n + k)) return n + k;
            if (n - k > 1 && isPrime (n - k)) return n - k;
        }
        return n;
    }
}
