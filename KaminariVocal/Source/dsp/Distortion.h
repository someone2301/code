#pragma once

#include "../sends/DspCommon.h"
#include <juce_dsp/juce_dsp.h>
#include <atomic>

// Distortion module (DESIGN.md 2.13). Original implementation:
//   input -> Low Cut (12 dB/oct, before the shaper so lows stay clean) -> Drive -> [oversampled shaper]
//         -> DC blocker -> Tone (tilt around 1 kHz) -> Auto Gain -> Output -> Mix with the (latency-aligned) dry
// Styles:
//   Tape  soft symmetric saturation with gentle high-frequency softening at high drive
//   Tube  asymmetric soft saturation (adds even harmonics); Bias adds more asymmetry
//   Warm  cubic soft clip, the mildest curve
//   Fuzz  high-gain arctangent clipping
//   Clip  hard clipping
//   Lo-Fi soft clip followed by bit and sample-rate reduction (Crush) at the base rate, aliasing on purpose
// Oversampling (Off, 2x, 4x) uses linear-phase half-band filters with an integer latency, which the plug-in reports
// while the module is on. Auto Gain matches the output level to the input level (slow, about 400 ms).
namespace kv
{
    struct DistortionSettings
    {
        bool on = false;
        enum Style { Tape, Tube, Warm, Fuzz, Clip, LoFi };
        int style = Tape;
        float driveDb = 9.0f;
        float tone = 0.0f;         // -1 dark .. +1 bright
        float bias = 0.0f;         // 0..1
        float lowCutHz = 20.0f;    // 20 = off
        float crush = 0.4f;        // 0..1 (Lo-Fi)
        float mix = 1.0f;          // 0..1
        float outDb = 0.0f;
        bool autoGain = true;
        int oversampling = 1;      // 0 off, 1 = 2x, 2 = 4x
    };

    class Distortion
    {
    public:
        void prepare (double sampleRate, int maxBlock)
        {
            fs = (float) sampleRate;
            for (int k = 0; k < 2; ++k)
            {
                os[k] = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) (k + 1),
                                                                           juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                                                                           true, true);
                os[k]->initProcessing ((size_t) maxBlock);
                osLatency[k] = (int) std::lround (os[k]->getLatencyInSamples());
            }
            for (auto& d : dryLine) d.prepare (maxBlock + std::max (osLatency[0], osLatency[1]) + 8);
            block.setSize (2, maxBlock, false, false, true);
            gainSmooth.prepare (fs, 20.0f);
            autoSmooth.prepare (fs, 50.0f);
            reset();
        }

        void reset()
        {
            for (auto& o : os) if (o) o->reset();
            for (auto& d : dryLine) d.reset();
            for (auto& f : lowCut) f.reset();
            for (auto& f : toneLo) f.reset();
            for (auto& f : toneHi) f.reset();
            for (auto& f : dc) f.reset();
            for (auto& f : tapeLp) f.reset();
            for (auto& l : lofi) l.reset();
            inEnv = outEnv = 0.0f;
            gainSmooth.snap (1.0f);
            autoSmooth.snap (1.0f);
            peakOver.store (0.0f);
        }

        // Latency in samples for an oversampling choice (0 for Off).
        int latencyFor (int oversampling) const noexcept { return oversampling <= 0 ? 0 : osLatency[std::min (oversampling, 2) - 1]; }

        // How far the driven signal went above the shaper's knee in the last block (dB, for the GUI).
        std::atomic<float> peakOver { 0.0f };

        void process (float* l, float* r, int n, const DistortionSettings& s)
        {
            float* ch[2] = { l, r };
            const int osIndex = std::clamp (s.oversampling, 0, 2);
            if (osIndex != lastOs) { if (osIndex > 0) os[osIndex - 1]->reset(); lastOs = osIndex; }
            // the dry copy for Mix is delayed by the same amount as the processed path
            const int lat = latencyFor (osIndex);

            // filters (block rate)
            for (int c = 0; c < 2; ++c)
            {
                if (s.lowCutHz > 20.5f) lowCut[c].set (Biquad::HighPass, fs, s.lowCutHz, 0.7071f);
                const float tilt = s.tone * 6.0f;
                toneLo[c].set (Biquad::LowShelf, fs, 1000.0f, 0.5f, -tilt);
                toneHi[c].set (Biquad::HighShelf, fs, 1000.0f, 0.5f, tilt);
                dc[c].setCutoff (12.0f, fs);
            }

            // keep the dry signal (pre everything) for the mix and the auto gain reference
            float over = 0.0f;
            const float drive = dbToGain (s.driveDb);
            for (int c = 0; c < 2; ++c)
            {
                auto* w = block.getWritePointer (c);
                for (int i = 0; i < n; ++i)
                {
                    const float x = ch[c][i];
                    dryLine[c].push (x);
                    float y = s.lowCutHz > 20.5f ? lowCut[c].process (x) : x;
                    y *= drive;
                    over = std::max (over, std::abs (y));
                    w[i] = y;
                }
            }
            peakOver.store (over > 1.0e-9f ? std::max (0.0f, 20.0f * std::log10 (over)) : 0.0f, std::memory_order_relaxed);

            // shaper, oversampled
            juce::dsp::AudioBlock<float> ab (block.getArrayOfWritePointers(), 2, (size_t) n);
            if (osIndex > 0)
            {
                auto up = os[osIndex - 1]->processSamplesUp (ab);
                const float osFs = fs * (float) (1 << osIndex);
                for (size_t c = 0; c < up.getNumChannels(); ++c)
                {
                    tapeLp[c].setCutoff (std::max (3000.0f, 16000.0f - 300.0f * s.driveDb), osFs);
                    auto* p = up.getChannelPointer (c);
                    for (size_t i = 0; i < up.getNumSamples(); ++i)
                        p[i] = shape (p[i], s, (int) c);
                }
                os[osIndex - 1]->processSamplesDown (ab);
            }
            else
            {
                for (int c = 0; c < 2; ++c)
                {
                    tapeLp[c].setCutoff (std::max (3000.0f, 16000.0f - 300.0f * s.driveDb), fs);
                    auto* p = block.getWritePointer (c);
                    for (int i = 0; i < n; ++i)
                        p[i] = shape (p[i], s, c);
                }
            }

            // Lo-Fi: bit and sample-rate reduction at the base rate (aliasing is the point)
            if (s.style == DistortionSettings::LoFi && s.crush > 0.0f)
            {
                const float bits = 16.0f - 12.0f * s.crush;               // 16 .. 4 bits
                const float hold = 1.0f + 15.0f * s.crush * s.crush;      // 1 .. 16 samples
                for (int c = 0; c < 2; ++c)
                {
                    auto* p = block.getWritePointer (c);
                    for (int i = 0; i < n; ++i) p[i] = lofi[c].process (p[i], hold, bits);
                }
            }

            // DC blocker, tone, level matching, output and mix
            const float outGain = dbToGain (s.outDb);
            const float envCoeff = 1.0f - std::exp (-1.0f / (0.4f * fs));
            for (int i = 0; i < n; ++i)
            {
                float y[2], dry[2];
                for (int c = 0; c < 2; ++c)
                {
                    float v = block.getSample (c, i);
                    v -= dc[c].process (v);
                    v = toneHi[c].process (toneLo[c].process (v));
                    y[c] = v;
                    dry[c] = lat > 0 ? dryLine[c].readInt (lat + (n - 1 - i)) : ch[c][i];   // whole block already pushed
                }

                float autoGain = 1.0f;
                if (s.autoGain)
                {
                    const float inPow = 0.5f * (dry[0] * dry[0] + dry[1] * dry[1]);
                    const float outPow = 0.5f * (y[0] * y[0] + y[1] * y[1]);
                    inEnv += (inPow - inEnv) * envCoeff;
                    outEnv += (outPow - outEnv) * envCoeff;
                    if (inEnv > 1.0e-7f && outEnv > 1.0e-9f)
                        autoGain = std::clamp (std::sqrt (inEnv / outEnv), 0.0625f, 4.0f);   // -24 .. +12 dB
                    else
                        autoGain = autoSmooth.value;
                }
                autoSmooth.setTarget (autoGain);
                const float ag = autoSmooth.next();
                gainSmooth.setTarget (outGain);
                const float og = gainSmooth.next();
                for (int c = 0; c < 2; ++c)
                {
                    const float wet = y[c] * ag * og;
                    ch[c][i] = dry[c] + (wet - dry[c]) * s.mix;
                }
            }
        }

    private:
        float shape (float x, const DistortionSettings& s, int c) noexcept
        {
            const float y = curve (x, s.style, s.bias);
            return s.style == DistortionSettings::Tape ? tapeLp[c].process (y) : y;
        }

    public:
        // Static transfer curve of a style (input already multiplied by Drive). Also drawn by the GUI.
        static float curve (float x, int style, float bias) noexcept
        {
            switch (style)
            {
                case DistortionSettings::Tape:
                {
                    const float b = 0.3f * bias;
                    return std::tanh (x + b) - std::tanh (b);
                }
                case DistortionSettings::Tube:
                {
                    const float b = 0.2f + 0.6f * bias;
                    // asymmetric: the positive half saturates harder than the negative half
                    const float v = x + b;
                    const float y = v > 0.0f ? std::tanh (v) : std::tanh (0.6f * v) / 0.6f;
                    return 0.85f * (y - std::tanh (b));
                }
                case DistortionSettings::Warm:
                {
                    const float v = std::clamp (x * 0.67f + 0.25f * bias, -1.0f, 1.0f);
                    const float b = std::clamp (0.25f * bias, -1.0f, 1.0f);
                    return 1.5f * ((v - v * v * v / 3.0f) - (b - b * b * b / 3.0f));
                }
                case DistortionSettings::Fuzz:
                {
                    const float v = x * 3.0f + 0.5f * bias;
                    return 0.9f * (0.6366f * std::atan (4.0f * v) - 0.6366f * std::atan (2.0f * bias));
                }
                case DistortionSettings::Clip:
                {
                    const float b = 0.3f * bias;
                    return std::clamp (x + b, -1.0f, 1.0f) - std::clamp (b, -1.0f, 1.0f);
                }
                case DistortionSettings::LoFi:
                default:
                    return std::tanh (x + 0.2f * bias) - std::tanh (0.2f * bias);
            }
        }

    private:
        float fs = 48000.0f;
        std::unique_ptr<juce::dsp::Oversampling<float>> os[2];
        int osLatency[2] { 0, 0 }, lastOs = -1;
        juce::AudioBuffer<float> block;
        DelayLine dryLine[2];
        Biquad lowCut[2], toneLo[2], toneHi[2];
        OnePoleLP dc[2], tapeLp[2];
        LoFi lofi[2];
        float inEnv = 0, outEnv = 0;
        Smoother gainSmooth, autoSmooth;
    };
}
