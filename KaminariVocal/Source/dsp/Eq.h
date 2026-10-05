#pragma once

#include "../sends/DspCommon.h"
#include <complex>

// 8-band minimum-phase EQ (DESIGN.md 2.3). Coefficients are recomputed every 32 samples from smoothed
// frequency, gain and Q, so automation and dragging do not click.
namespace kv
{
    enum EqType { Bell, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass, TiltShelf, FlatTilt };
    inline const char* const eqTypeNames[] = { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt Shelf", "Flat Tilt" };
    inline const int eqSlopes[] = { 6, 12, 18, 24, 36, 48 };

    struct EqBandSettings
    {
        bool used = false, on = true;
        int type = Bell, slopeIndex = 1;
        float freq = 1000.0f, gainDb = 0.0f, q = 1.0f;
    };

    // Coefficients of one band as up to 4 second-order sections (a first-order section is a biquad with b2 = a2 = 0)
    // plus a flat gain. Shared by the audio path and the GUI curve.
    struct EqDesign
    {
        struct Coeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
        Coeffs sec[4];
        int numSections = 0;
        double scale = 1.0;

        static Coeffs rbj (Biquad::Kind k, double fs, double f, double q, double gainDb)
        {
            Biquad b;
            b.set (k, (float) fs, (float) f, (float) q, (float) gainDb);
            return { b.b0, b.b1, b.b2, b.a1, b.a2 };
        }

        static Coeffs firstOrder (bool highPass, double fs, double f)
        {
            const double k = std::tan (pi * std::min (f, 0.49 * fs) / fs);
            const double n = 1.0 / (1.0 + k);
            if (highPass) return { n, -n, 0, (k - 1.0) * n, 0 };
            return { k * n, k * n, 0, (k - 1.0) * n, 0 };
        }

        static EqDesign make (const EqBandSettings& s, double fs)
        {
            EqDesign d;
            const double f = std::clamp ((double) s.freq, 10.0, 0.47 * fs);
            const double q = std::clamp ((double) s.q, 0.025, 40.0);
            const double g = s.gainDb;
            switch (s.type)
            {
                case Bell:      d.sec[d.numSections++] = rbj (Biquad::Peak, fs, f, q, g); break;
                case LowShelf:  d.sec[d.numSections++] = rbj (Biquad::LowShelf, fs, f, std::min (q, 2.0), g); break;
                case HighShelf: d.sec[d.numSections++] = rbj (Biquad::HighShelf, fs, f, std::min (q, 2.0), g); break;
                case Notch:
                {
                    Biquad b; b.set (Biquad::Peak, (float) fs, (float) f, (float) q, -60.0f);
                    d.sec[d.numSections++] = { b.b0, b.b1, b.b2, b.a1, b.a2 };
                    break;
                }
                case BandPass:  d.sec[d.numSections++] = rbj (Biquad::BandPass, fs, f, q, 0); break;
                case TiltShelf:
                    // pivot at f: lows -g/2, highs +g/2
                    d.sec[d.numSections++] = rbj (Biquad::HighShelf, fs, f, 0.5, g);
                    d.scale = std::pow (10.0, -g / 40.0);
                    break;
                case FlatTilt:
                {
                    // three wide shelves give an almost straight dB/octave tilt around the pivot f
                    for (double c : { 0.1, 1.0, 10.0 })
                        d.sec[d.numSections++] = rbj (Biquad::HighShelf, fs, std::clamp (f * c, 20.0, 0.45 * fs), 0.4, g / 3.0);
                    d.scale = std::pow (10.0, -g / 40.0);
                    break;
                }
                case LowCut:
                case HighCut:
                {
                    const bool hp = s.type == LowCut;
                    const int order = eqSlopes[std::clamp (s.slopeIndex, 0, 5)] / 6;
                    if (order % 2 == 1)
                        d.sec[d.numSections++] = firstOrder (hp, fs, f);
                    const int pairs = order / 2;
                    for (int k = 0; k < pairs; ++k)
                    {
                        // Butterworth pole Qs; the user Q scales the sharpest pole (resonance at the corner)
                        const double theta = pi * (2.0 * k + 1.0 + (order % 2 == 1 ? 1.0 : 0.0)) / (2.0 * order);
                        double qk = 1.0 / (2.0 * std::cos (theta));
                        if (order % 2 == 1) qk = 1.0 / (2.0 * std::cos (pi * (k + 1.0) / order));
                        if (k == pairs - 1) qk *= q / 0.7071;
                        d.sec[d.numSections++] = rbj (hp ? Biquad::HighPass : Biquad::LowPass, fs, f, std::max (0.1, qk), 0);
                        if (d.numSections == 4) break;
                    }
                    break;
                }
                default: break;
            }
            return d;
        }

        double magnitudeDb (double f, double fs) const
        {
            const double w = 2.0 * pi * f / fs;
            const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
            double mag = scale;
            for (int i = 0; i < numSections; ++i)
            {
                const auto& c = sec[i];
                mag *= std::abs (c.b0 + c.b1 * z1 + c.b2 * z2) / std::max (1e-12, std::abs (1.0 + c.a1 * z1 + c.a2 * z2));
            }
            return 20.0 * std::log10 (std::max (mag, 1e-9));
        }
    };

    class Equalizer
    {
    public:
        static constexpr int numBands = 8;

        void prepare (double sampleRate)
        {
            fs = sampleRate;
            reset();
            primed = false;
        }

        void reset()
        {
            for (auto& b : bands) for (auto& ch : b.z) for (auto& z : ch) z = {};
        }

        // Processes both channels in place. Bands that are unused or off are skipped.
        void process (float* l, float* r, int n, const EqBandSettings (&s)[numBands], float outGainDb)
        {
            if (! primed)
            {
                for (int i = 0; i < numBands; ++i) snap (i, s[i]);
                primed = true;
            }
            const float outGain = dbToGain (outGainDb);
            for (int start = 0; start < n; start += 32)
            {
                const int m = std::min (32, n - start);
                for (int i = 0; i < numBands; ++i)
                {
                    auto& b = bands[i];
                    const bool active = s[i].used && s[i].on;
                    if (! active) { b.active = false; continue; }
                    if (! b.active || s[i].type != b.type || s[i].slopeIndex != b.slope)
                    {
                        snap (i, s[i]);
                        b.active = true;
                    }
                    // one-pole smoothing (about 20 ms) of frequency (log), gain and Q (log)
                    const double k = 1.0 - std::exp (-m / (0.02 * fs));
                    b.logF += (std::log (std::max (10.0f, s[i].freq)) - b.logF) * k;
                    b.gain += (s[i].gainDb - b.gain) * k;
                    b.logQ += (std::log (std::max (0.025f, s[i].q)) - b.logQ) * k;
                    EqBandSettings cur = s[i];
                    cur.freq = (float) std::exp (b.logF); cur.gainDb = (float) b.gain; cur.q = (float) std::exp (b.logQ);
                    b.design = EqDesign::make (cur, fs);
                    for (int c = 0; c < 2; ++c)
                    {
                        float* x = (c == 0 ? l : r) + start;
                        for (int j = 0; j < m; ++j)
                        {
                            double v = x[j] * b.design.scale;
                            for (int q = 0; q < b.design.numSections; ++q)
                            {
                                const auto& co = b.design.sec[q];
                                auto& z = b.z[c][q];
                                const double y = co.b0 * v + z.z1;
                                z.z1 = co.b1 * v - co.a1 * y + z.z2;
                                z.z2 = co.b2 * v - co.a2 * y;
                                v = y;
                            }
                            x[j] = (float) v;
                        }
                    }
                }
                if (std::abs (outGain - 1.0f) > 1e-6f)
                    for (int j = 0; j < m; ++j) { l[start + j] *= outGain; r[start + j] *= outGain; }
            }
        }

    private:
        void snap (int i, const EqBandSettings& s)
        {
            auto& b = bands[i];
            b.logF = std::log (std::max (10.0f, s.freq));
            b.gain = s.gainDb;
            b.logQ = std::log (std::max (0.025f, s.q));
            b.type = s.type;
            b.slope = s.slopeIndex;
            for (auto& ch : b.z) for (auto& z : ch) z = {};
        }

        struct State { double z1 = 0, z2 = 0; };
        struct Band
        {
            EqDesign design;
            State z[2][4];
            double logF = 0, gain = 0, logQ = 0;
            int type = -1, slope = -1;
            bool active = false;
        };

        double fs = 48000.0;
        Band bands[numBands];
        bool primed = false;
    };
}
