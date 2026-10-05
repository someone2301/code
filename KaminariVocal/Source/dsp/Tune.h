#pragma once

#include "../sends/DspCommon.h"
#include <atomic>

// Real-time pitch correction (DESIGN.md 2.2, section 4). Original implementation:
//   - pitch detection: YIN difference function on a low-passed, decimated (about 12 kHz) copy of the input
//   - target: nearest enabled note of the key and scale, with hysteresis
//   - Retune Speed: time constant of the correction; Humanize: slower correction on held notes
//   - shifting: a delay-line read head moving at the pitch ratio; when it drifts outside its window it jumps by
//     exactly one detected pitch period (with a short crossfade), so whole periods are repeated or dropped.
// The plug-in reports a fixed 96 samples at 48 kHz (2.0 ms); with Tune off the signal is delayed by the same amount.
// The instantaneous delay while correcting varies by up to about one pitch period around that.
namespace kv
{
    inline const int scaleIntervals[10][12] = {
        { 1,1,1,1,1,1,1,1,1,1,1,1 },   // Chromatic
        { 1,0,1,0,1,1,0,1,0,1,0,1 },   // Major
        { 1,0,1,1,0,1,0,1,1,0,1,0 },   // Natural Minor
        { 1,0,1,1,0,1,0,1,1,0,0,1 },   // Harmonic Minor
        { 1,0,1,1,0,1,0,1,0,1,0,1 },   // Melodic Minor
        { 1,0,1,0,1,0,0,1,0,1,0,0 },   // Major Pentatonic
        { 1,0,0,1,0,1,0,1,0,0,1,0 },   // Minor Pentatonic
        { 1,0,0,1,0,1,1,1,0,0,1,0 },   // Blues
        { 1,0,1,1,0,1,0,1,0,1,1,0 },   // Dorian
        { 1,0,1,0,1,1,0,1,0,1,1,0 },   // Mixolydian
    };

    // Pitch classes (C = 0) enabled by a key and a named scale (scale index 0..9).
    inline void scaleNotes (int key, int scale, bool out[12])
    {
        for (int pc = 0; pc < 12; ++pc)
            out[pc] = scaleIntervals[std::clamp (scale, 0, 9)][((pc - key) % 12 + 12) % 12] != 0;
    }

    struct TuneSettings
    {
        bool on = true;
        bool notes[12] = { true, true, true, true, true, true, true, true, true, true, true, true };
        int range = 1;              // 0 High, 1 Middle, 2 Low, 3 Deep
        float speedMs = 40, humanize = 0.2f;
    };

    class Tune
    {
    public:
        static int latencyFor (double fs) { return (int) std::lround (96.0 * fs / 48000.0); }

        void prepare (double sampleRate)
        {
            fs = (float) sampleRate;
            latency = latencyFor (sampleRate);
            decim = std::max (1, (int) std::lround (fs / 12000.0f));
            dfs = fs / (float) decim;
            for (auto& d : line) d.prepare ((int) (0.06f * fs) + latency + 64);
            ring.assign (2048, 0.0f);
            aa1.set (Biquad::LowPass, fs, 0.4f * dfs, 0.7071f);
            aa2.set (Biquad::LowPass, fs, 0.4f * dfs, 0.7071f);
            reset();
        }

        void reset()
        {
            for (auto& d : line) d.reset();
            std::fill (ring.begin(), ring.end(), 0.0f);
            ringPos = 0; decCount = 0; hopCount = 0;
            aa1.reset(); aa2.reset();
            delay = (float) latency; fadeDelay = delay; fade = 0.0f;
            correction = 0.0f; targetNote = -1; heldSamples = 0; periodSamples = 0; voiced = false;
            onMix = 1.0f; level = 0.0f; primed = false;
        }

        // Read-outs for the GUI.
        std::atomic<float> detectedMidi { -1.0f }, correctionCents { 0.0f };

        void process (float* l, float* r, int n, const TuneSettings& s)
        {
            const float onTarget = s.on ? 1.0f : 0.0f;
            if (! primed) { onMix = onTarget; primed = true; }
            const float fadeStep = 1.0f / (0.01f * fs);
            for (int i = 0; i < n; ++i)
            {
                const float mono = 0.5f * (l[i] + r[i]);
                analyse (mono, s);

                line[0].push (l[i]);
                line[1].push (r[i]);

                // read-head movement at the correction ratio
                const float ratio = std::pow (2.0f, correction / 12.0f);
                delay += 1.0f - ratio;
                const float period = periodSamples > 0.0f ? periodSamples : 0.01f * fs;
                const float lo = std::max (24.0f, (float) latency - 0.5f * period);
                const float hi = lo + period;
                if (fade <= 0.0f && (delay < lo || delay > hi))
                {
                    fadeDelay = delay;                       // old head fades out
                    delay += delay < lo ? period : -period;  // new head one period away
                    fade = 1.0f;
                    fadeLen = std::clamp (0.5f * period, 16.0f, 0.004f * fs);
                }
                if (! voiced && std::abs (correction) < 0.01f)
                    delay += ((float) latency - delay) * 0.0005f;   // drift back to the reported latency when idle

                onMix += (onTarget > onMix ? 1.0f : -1.0f) * fadeStep;
                onMix = std::clamp (onMix, 0.0f, 1.0f);
                for (int c = 0; c < 2; ++c)
                {
                    float wet = line[c].read (delay);
                    if (fade > 0.0f)
                    {
                        const float g = 0.5f - 0.5f * std::cos ((float) pi * fade);   // 1 -> 0
                        wet = wet * (1.0f - g) + line[c].read (fadeDelay) * g;
                    }
                    const float dry = line[c].readInt (latency);
                    (c == 0 ? l : r)[i] = dry + (wet - dry) * onMix;
                }
                if (fade > 0.0f)
                {
                    fadeDelay += 1.0f - ratio;
                    fade -= 1.0f / fadeLen;
                }
            }
            correctionCents.store (correction * 100.0f, std::memory_order_relaxed);
        }

    private:
        void analyse (float x, const TuneSettings& s)
        {
            level = std::abs (x) > level ? std::abs (x) : level * 0.9995f;
            const float y = aa2.process (aa1.process (x));
            if (++decCount < decim) { smoothCorrection (s); return; }
            decCount = 0;
            ring[(size_t) ringPos] = y;
            ringPos = (ringPos + 1) & 2047;
            if (++hopCount >= 64)   // about 5 ms at 12 kHz
            {
                hopCount = 0;
                detect (s);
            }
            smoothCorrection (s);
        }

        void detect (const TuneSettings& s)
        {
            static const float minF[4] = { 175, 110, 80, 60 }, maxF[4] = { 1100, 700, 520, 350 };
            const int r = std::clamp (s.range, 0, 3);
            const int minLag = std::max (2, (int) (dfs / maxF[r]));
            const int maxLag = std::min (600, (int) (dfs / minF[r]) + 2);
            const int w = maxLag;
            auto at = [this] (int back) { return ring[(size_t) ((ringPos - 1 - back) & 2047)]; };

            // YIN: difference, cumulative mean normalised difference, absolute threshold
            float cmnd[612];
            float running = 0.0f;
            cmnd[0] = 1.0f;
            int best = -1;
            for (int tau = 1; tau <= maxLag; ++tau)
            {
                float d = 0.0f;
                for (int j = 0; j < w; ++j)
                {
                    const float diff = at (j) - at (j + tau);
                    d += diff * diff;
                }
                running += d;
                cmnd[tau] = running > 0.0f ? d * (float) tau / running : 1.0f;
                if (best < 0 && tau > minLag && cmnd[tau] < 0.15f && cmnd[tau] < cmnd[tau - 1])
                {
                    // walk to the local minimum
                    int t = tau;
                    while (t + 1 <= maxLag)
                    {
                        float d2 = 0.0f;
                        for (int j = 0; j < w; ++j) { const float diff = at (j) - at (j + t + 1); d2 += diff * diff; }
                        const float next = (running + d2) > 0.0f ? d2 * (float) (t + 1) / (running + d2) : 1.0f;
                        if (next >= cmnd[t]) break;
                        running += d2;
                        cmnd[++t] = next;
                    }
                    best = t;
                    break;
                }
            }
            const bool loudEnough = level > 0.003f;   // about -50 dBFS
            if (best < 0 || ! loudEnough)
            {
                voiced = false;
                periodSamples = 0.0f;
                detectedMidi.store (-1.0f, std::memory_order_relaxed);
                return;
            }
            // parabolic interpolation of the minimum
            float tauF = (float) best;
            if (best > 1 && best < maxLag)
            {
                float dm = 0, dp = 0;
                for (int j = 0; j < w; ++j)
                {
                    const float a = at (j) - at (j + best - 1), b = at (j) - at (j + best + 1);
                    dm += a * a; dp += b * b;
                }
                float d0 = 0;
                for (int j = 0; j < w; ++j) { const float a = at (j) - at (j + best); d0 += a * a; }
                const float den = dm - 2.0f * d0 + dp;
                if (std::abs (den) > 1e-12f)
                    tauF += std::clamp (0.5f * (dm - dp) / den, -0.5f, 0.5f);
            }
            const float f0 = dfs / tauF;
            periodSamples = fs / f0;
            voiced = true;
            const float midi = 69.0f + 12.0f * std::log2 (f0 / 440.0f);
            detectedMidi.store (midi, std::memory_order_relaxed);

            // nearest enabled note, with hysteresis towards the current target
            int bestNote = -1;
            float bestDist = 1e9f;
            for (int note = (int) std::floor (midi) - 7; note <= (int) std::ceil (midi) + 7; ++note)
            {
                if (! s.notes[((note % 12) + 12) % 12]) continue;
                float dist = std::abs ((float) note - midi);
                if (note == targetNote) dist -= 0.15f;
                if (dist < bestDist) { bestDist = dist; bestNote = note; }
            }
            if (bestNote != targetNote) heldSamples = 0;
            targetNote = bestNote;
            desired = bestNote >= 0 ? (float) bestNote - midi : 0.0f;
        }

        void smoothCorrection (const TuneSettings& s)
        {
            float target = voiced ? std::clamp (desired, -2.0f, 2.0f) : 0.0f;
            float tc = std::max (0.0f, s.speedMs);
            if (voiced)
            {
                heldSamples += 1;
                const float held = (float) heldSamples / fs;
                if (held > 0.15f)   // Humanize: held notes are corrected more gently
                    tc = tc * (1.0f + 3.0f * s.humanize) + 80.0f * s.humanize;
            }
            else tc = 30.0f;        // unvoiced: release the correction
            if (tc <= 0.0f) correction = target;
            else correction += (target - correction) * (1.0f - std::exp (-1.0f / (0.001f * tc * fs)));
            // desired drifts with the detected pitch between analyses: keep it relative to the last detection
        }

        float fs = 48000.0f, dfs = 12000.0f;
        int latency = 96, decim = 4, decCount = 0, hopCount = 0, ringPos = 0;
        DelayLine line[2];
        std::vector<float> ring;
        Biquad aa1, aa2;
        float delay = 96, fadeDelay = 96, fade = 0, fadeLen = 32;
        float correction = 0, desired = 0, periodSamples = 0, onMix = 1, level = 0;
        int targetNote = -1;
        long heldSamples = 0;
        bool voiced = false, primed = false;
    };
}
