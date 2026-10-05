#pragma once

#include <cmath>
#include <complex>

// RBJ cookbook biquads. Coefficients are normalised so a0 == 1.
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    double magnitude (double freq, double sampleRate) const
    {
        const double w = 2.0 * 3.14159265358979323846 * freq / sampleRate;
        const std::complex<double> z1 = std::polar (1.0, -w);
        const std::complex<double> z2 = z1 * z1;
        const auto num = b0 + b1 * z1 + b2 * z2;
        const auto den = 1.0 + a1 * z1 + a2 * z2;
        return std::abs (num) / std::max (std::abs (den), 1.0e-12);
    }
};

namespace biquad
{
    enum class Kind { Peak, LowShelf, HighShelf, HighPass, LowPass, Notch, BandPass };

    inline BiquadCoeffs make (Kind kind, double sampleRate, double freq, double q, double gainDb = 0.0)
    {
        freq = std::fmin (std::fmax (freq, 10.0), sampleRate * 0.49);
        q = std::fmax (q, 0.05);

        const double w0 = 2.0 * 3.14159265358979323846 * freq / sampleRate;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double beta = 2.0 * std::sqrt (A) * alpha;

        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

        switch (kind)
        {
            case Kind::Peak:
                b0 = 1 + alpha * A;  b1 = -2 * cw;  b2 = 1 - alpha * A;
                a0 = 1 + alpha / A;  a1 = -2 * cw;  a2 = 1 - alpha / A;
                break;
            case Kind::LowShelf:
                b0 = A * ((A + 1) - (A - 1) * cw + beta);
                b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 = A * ((A + 1) - (A - 1) * cw - beta);
                a0 = (A + 1) + (A - 1) * cw + beta;
                a1 = -2 * ((A - 1) + (A + 1) * cw);
                a2 = (A + 1) + (A - 1) * cw - beta;
                break;
            case Kind::HighShelf:
                b0 = A * ((A + 1) + (A - 1) * cw + beta);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - beta);
                a0 = (A + 1) - (A - 1) * cw + beta;
                a1 = 2 * ((A - 1) - (A + 1) * cw);
                a2 = (A + 1) - (A - 1) * cw - beta;
                break;
            case Kind::HighPass:
                b0 = (1 + cw) / 2;  b1 = -(1 + cw);  b2 = (1 + cw) / 2;
                a0 = 1 + alpha;     a1 = -2 * cw;    a2 = 1 - alpha;
                break;
            case Kind::LowPass:
                b0 = (1 - cw) / 2;  b1 = 1 - cw;     b2 = (1 - cw) / 2;
                a0 = 1 + alpha;     a1 = -2 * cw;    a2 = 1 - alpha;
                break;
            case Kind::Notch:
                b0 = 1;  b1 = -2 * cw;  b2 = 1;
                a0 = 1 + alpha;  a1 = -2 * cw;  a2 = 1 - alpha;
                break;
            case Kind::BandPass:
                b0 = alpha;  b1 = 0;  b2 = -alpha;
                a0 = 1 + alpha;  a1 = -2 * cw;  a2 = 1 - alpha;
                break;
        }

        BiquadCoeffs c;
        c.b0 = b0 / a0;  c.b1 = b1 / a0;  c.b2 = b2 / a0;
        c.a1 = a1 / a0;  c.a2 = a2 / a0;
        return c;
    }
}

// Transposed direct form II, double precision state.
struct Biquad
{
    BiquadCoeffs c;
    double z1 = 0.0, z2 = 0.0;

    float process (float x) noexcept
    {
        const double in = x;
        const double y = c.b0 * in + z1;
        z1 = c.b1 * in - c.a1 * y + z2;
        z2 = c.b2 * in - c.a2 * y;
        return (float) y;
    }

    void reset() noexcept { z1 = z2 = 0.0; }
};
