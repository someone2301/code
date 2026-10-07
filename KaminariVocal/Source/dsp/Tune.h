#pragma once

#include "../sends/DspCommon.h"
#include <array>
#include <atomic>
#include <vector>

// Real-time pitch correction (DESIGN.md 2.2, section 4). Original implementation with two modes:
//
//   Tracking (default, 4 ms): for recording and live monitoring.
//     - pitch: YIN on a low-passed, decimated (about 12 kHz) copy of the input, every 16 decimated samples (1.3 ms),
//       with octave protection (stays with the current period when a half / double candidate is just as good),
//       voicing hysteresis (a stricter threshold to start a note than to continue it, three misses to end it) and a
//       3-point median against single outliers. The pitch is extrapolated from the analysis window's centre to the
//       sample being played, so fast vibrato is followed without lag.
//     - correction: at a note's start the target follows the voice at once; on a held note it is chosen from the
//       note's vibrato-free centre, so vibrato does not flip notes; a new note is taken as soon as the voice clearly
//       leaves the old one. Fast glides between notes pass through (the correction relaxes above ~18 semitones per
//       second). Retune Speed also sets how much of the singer's vibrato is kept: 0 ms corrects every instant (flat,
//       hard tune), slower settings move the note's centre and keep the vibrato. Humanize: slower correction on held
//       notes. Timing matched to reference renders of a real vocal by an established tuner.
//     - shifting: a delay-line read head moving at the pitch ratio; when it drifts out of its window it splices one
//       period away, at the lag whose waveform best matches (normalised cross-correlation), with a raised-cosine
//       crossfade.
//   High quality (switchable, about 9 to 21 ms by vocal range): for mixing.
//     - the same analysis and shifter, but every correction is time-aligned with the audio (lookahead): the pitch
//       and correction of the exact sample being played are used, with no prediction, so notes are corrected from
//       their first period and transitions land exactly. (A PSOLA shifter was tried here; on a real vocal it was
//       measurably rougher, so it was dropped.)
// With Tune off the signal is delayed by the reported latency of the selected mode.
//
// Vibrato and tremolo (DESIGN.md 2.2.1) are part of Tune and need Tune on; "Correct pitch" off keeps them without
// retuning.
//   - vibrato: a sine pitch modulation added to the correction on sung (voiced) notes. Each new note restarts its
//     onset: nothing for Onset Delay, then a fade-in over Onset Rise. Variation lets rate and depth wander.
//   - tremolo: amplitude modulation after the shifter (free rate or locked to the host tempo), with a stereo phase
//     offset (180 degrees = auto-pan) and an optional onset that follows the vibrato's.
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
        int quality = 0;            // 0 Tracking (low latency), 1 High quality (lookahead, formant-preserving)
        float speedMs = 40, humanize = 0.2f;
        bool correct = true;        // false: no retuning (vibrato and tremolo still work)
        float detuneCents = 0.0f;   // moves the note grid (Correct on) or shifts sung notes by this much (Correct off)

        bool vibOn = false;
        float vibCents = 30, vibRateHz = 5.5f, vibDelayMs = 250, vibRiseMs = 300, vibVariation = 0.2f;

        bool tremOn = false;
        float tremDepth = 0.5f;     // 0..1 (1 = down to silence at the trough)
        float tremRateHz = 5.0f;
        double tremBeats = 0.0;     // > 0: one cycle per this many beats, locked to the host
        int tremShape = 0;          // 0 sine, 1 triangle, 2 square
        float tremStereo = 0.0f;    // 0..1 = 0..180 degrees between left and right
        bool tremOnset = false;     // tremolo fades in with the vibrato onset

        // host transport for the chunk being processed (set by the processor)
        bool playing = false;
        double ppq = 0.0, bpm = 120.0;
    };

    class Tune
    {
    public:
        enum Quality { Tracking = 0, HighQuality = 1 };

        static double lowestFreq (int range) { static const double f[4] = { 175, 110, 80, 60 }; return f[std::clamp (range, 0, 3)]; }

        // Reported latency in samples. Tracking: 4 ms. High quality: 1.1 of the range's longest periods plus 3 ms, enough
        // for an analysis frame describing the sample being played to exist (its window centre lags by about one
        // longest period plus a hop).
        static int latencyFor (double fs, int quality = Tracking, int range = 1)
        {
            if (quality != HighQuality) return (int) std::lround (192.0 * fs / 48000.0);
            return (int) std::ceil (1.1 * fs / lowestFreq (range) + 0.003 * fs);
        }

        void prepare (double sampleRate)
        {
            fs = (float) sampleRate;
            decim = std::max (1, (int) std::lround (fs / 12000.0f));
            dfs = fs / (float) decim;
            const int pmaxAll = (int) std::ceil (fs / 60.0);
            const int maxLatency = latencyFor (sampleRate, HighQuality, 3);
            for (auto& d : line) d.prepare (maxLatency + 4 * pmaxAll + 256);
            ring.assign (4096, 0.0f);
            aa1.set (Biquad::LowPass, fs, 0.4f * dfs, 0.7071f);
            aa2.set (Biquad::LowPass, fs, 0.4f * dfs, 0.7071f);
            configured = false;
            reset();
        }

        void reset()
        {
            for (auto& d : line) d.reset();
            std::fill (ring.begin(), ring.end(), 0.0f);
            ringPos = 0; decCount = 0; hopCount = 0; nIn = 0;
            aa1.reset(); aa2.reset();
            latency = latencyFor (fs, quality, range);
            delay = (float) latency; fadeDelay = delay; fade = 0.0f;
            correction = 0.0f; desired = 0.0f; targetNote = -1; heldSamples = 0; noteSamples = 0;
            voiced = false; level = 0.0f; primed = false; onMix = 1.0f;
            tauTrack = 0.0f; missCount = 0; med[0] = med[1] = med[2] = 0.0f; medCount = 0;
            centre = 0.0f; jumpSamples = 0; slope = 0.0f; historyPos = 0; noteFrames = 0; jumpFrames = 0; lastMidi = -1.0f; lastFrameTime = 0;
            frameCount = 0; frameHead = 0;
            vibPhase = 0.0f; vibEnv = 0.0f; vibMix = 0.0f; tremPhase = 0.0; tremMix = 0.0f; tremDepthNow = 0.0f;
            wander.reset (0x51u); wanderDepth.reset (0x77u);
        }

        // Read-outs for the GUI.
        std::atomic<float> detectedMidi { -1.0f }, correctionCents { 0.0f };
        std::atomic<float> vibratoCents { 0.0f }, tremoloGain { 1.0f };
        int getLatency() const noexcept { return latency; }

        void process (float* l, float* r, int n, const TuneSettings& s)
        {
            const int q = s.quality == HighQuality ? HighQuality : Tracking, rg = std::clamp (s.range, 0, 3);
            if (! configured || q != quality || (q == HighQuality && rg != range))
            {
                quality = q; range = rg; configured = true;   // a new mode starts from a clean state (and a new latency)
                reset();
            }
            range = rg;
            const float onTarget = s.on ? 1.0f : 0.0f;
            if (! primed)
            {
                onMix = onTarget;
                tremMix = s.tremOn ? 1.0f : 0.0f;
                vibMix = s.vibOn ? 1.0f : 0.0f;
                tremDepthNow = s.tremDepth;
                primed = true;
            }
            const float fadeStep = 1.0f / (0.01f * fs);
            const double beatsPerSample = s.bpm / 60.0 / (double) fs;
            float lastTremGain = 1.0f;
            for (int i = 0; i < n; ++i)
            {
                line[0].push (l[i]);
                line[1].push (r[i]);
                ++nIn;
                analyse (0.5f * (l[i] + r[i]), s);

                float wet[2];
                if (quality == HighQuality) synthesiseHQ (s, wet);
                else synthesiseTracking (s, wet);

                onMix += (onTarget > onMix ? 1.0f : -1.0f) * fadeStep;
                onMix = std::clamp (onMix, 0.0f, 1.0f);

                // tremolo gain per channel
                tremMix = std::clamp (tremMix + ((s.tremOn ? 1.0f : 0.0f) > tremMix ? 1.0f : -1.0f) * fadeStep, 0.0f, 1.0f);
                tremDepthNow += (s.tremDepth - tremDepthNow) * 0.002f;
                float tremGain[2] = { 1.0f, 1.0f };
                if (tremMix > 0.0f)
                {
                    double ph;
                    if (s.tremBeats > 0.0 && s.playing)
                        ph = (s.ppq + (double) i * beatsPerSample) / s.tremBeats;
                    else
                    {
                        const double rate = s.tremBeats > 0.0 ? s.bpm / 60.0 / s.tremBeats : (double) s.tremRateHz;
                        tremPhase += rate / (double) fs;
                        tremPhase -= std::floor (tremPhase);
                        ph = tremPhase;
                    }
                    const float depth = tremDepthNow * tremMix * onMix * (s.tremOnset ? vibEnv : 1.0f);
                    for (int c = 0; c < 2; ++c)
                    {
                        const float lfo = lfoShape (s.tremShape, (float) (ph - std::floor (ph)) + (c == 1 ? 0.5f * s.tremStereo : 0.0f));
                        tremGain[c] = 1.0f - depth * 0.5f * (1.0f - lfo);
                    }
                }
                lastTremGain = 0.5f * (tremGain[0] + tremGain[1]);

                for (int c = 0; c < 2; ++c)
                {
                    const float dry = line[c].readInt (latency);
                    (c == 0 ? l : r)[i] = (dry + (wet[c] - dry) * onMix) * tremGain[c];
                }
            }
            correctionCents.store (shownCorrection * 100.0f, std::memory_order_relaxed);
            detectedMidi.store (shownMidi, std::memory_order_relaxed);
            vibratoCents.store (lastVib * 100.0f * onMix, std::memory_order_relaxed);
            tremoloGain.store (lastTremGain, std::memory_order_relaxed);
        }

    private:
        //==============================================================================================================
        // Tracking mode: read head at the correction ratio, matched splices.
        // High quality, time-aligned: the correction and period of the exact sample being played (from the lookahead
        // frames, no prediction), applied by the same matched-splice shifter as Tracking.
        void synthesiseHQ (const TuneSettings& s, float (&wet)[2])
        {
            const long long m = nIn - 1 - latency;
            if (m < 0) { wet[0] = wet[1] = 0.0f; return; }
            const Frame* fm = frameAt (m);
            const bool v = fm != nullptr && fm->voiced;
            const float vib = vibrato (s, v);
            float corr = 0.0f, period = 0.0f;
            valuesAt ((double) m, corr, period);
            shownCorrection = corr;
            shownMidi = v ? fm->midi : -1.0f;
            spliceStep (corr + vib, period > 0.0f ? period : 0.01f * fs, ! v && std::abs (corr) < 0.01f && std::abs (vib) < 0.001f, wet);
        }

        void synthesiseTracking (const TuneSettings& s, float (&wet)[2])
        {
            const float vib = vibrato (s, voiced);
            smoothCorrection (s, 1.0f);
            shownCorrection = correction;
            shownMidi = voiced ? lastMidi : -1.0f;
            spliceStep (correction + vib, periodNow > 0.0f ? periodNow : 0.01f * fs, ! voiced && std::abs (correction) < 0.01f && std::abs (vib) < 0.001f, wet);
        }

        // Delay-line shifter: the read head moves at the pitch ratio; when it leaves a window of 1.3 periods around the
        // latency it splices one period back or forward at the best-matching lag, with a crossfade.
        void spliceStep (float semitones, float period, bool idle, float (&wet)[2])
        {
            const float ratio = std::pow (2.0f, semitones / 12.0f);
            delay += 1.0f - ratio;
            const int cmp = compareLength (period);
            // the head may wander 1.3 periods around the latency: a splice moves it one period (+-0.15 for the match),
            // so it always lands inside the window and never bounces straight back
            const float lo = std::max ((float) cmp + 4.0f, (float) latency - 0.65f * period);
            const float hi = lo + 1.3f * period;
            if (fade <= 0.0f && (delay < lo || delay > hi))
            {
                fadeDelay = delay;   // old head fades out
                const float target = delay + (delay < lo ? period : -period);
                delay = bestSplice (delay, target, 0.15f * period, (float) cmp, cmp);
                fade = 1.0f;
                fadeLen = std::clamp (0.6f * period, 24.0f, 0.006f * fs);
            }
            if (idle)
                delay += ((float) latency - delay) * 0.0005f;   // drift back to the reported latency when idle
            for (int c = 0; c < 2; ++c)
            {
                float w = line[c].read (delay);
                if (fade > 0.0f)
                {
                    const float g = 0.5f - 0.5f * std::cos ((float) pi * fade);   // 1 -> 0
                    w = w * (1.0f - g) + line[c].read (fadeDelay) * g;
                }
                wet[c] = w;
            }
            if (fade > 0.0f)
            {
                fadeDelay += 1.0f - ratio;
                fade -= 1.0f / fadeLen;
            }
        }

        int compareLength (float period) const { return std::max (16, (int) std::min (0.002f * fs, 0.5f * period)); }

        // Delay near `target` (within +-search) whose next `len` samples best match those of the current head.
        float bestSplice (float from, float target, float search, float minDelay, int len) const
        {
            const int lo = std::max ((int) std::ceil (minDelay), (int) (target - search));
            const int hi = std::min (line[0].capacity() - 4, (int) (target + search));
            if (hi <= lo) return std::max (minDelay, target);
            const int f0 = (int) std::lround (from);
            auto score = [&] (int d)
            {
                double xy = 0, yy = 1.0e-12;
                for (int j = 0; j < len; j += 2)
                {
                    const float a = line[0].readInt (f0 - j) + line[1].readInt (f0 - j);
                    const float b = line[0].readInt (d - j) + line[1].readInt (d - j);
                    xy += (double) a * b; yy += (double) b * b;
                }
                return xy / std::sqrt (yy);
            };
            int best = (int) std::lround (target);
            double bestScore = -1.0e30;
            for (int d = lo; d <= hi; d += 2)
                if (const double sc = score (d); sc > bestScore) { bestScore = sc; best = d; }
            for (int d = std::max (lo, best - 1); d <= std::min (hi, best + 1); ++d)
                if (const double sc = score (d); sc > bestScore) { bestScore = sc; best = d; }
            return (float) best + (from - std::floor (from));
        }

        //==============================================================================================================
        // High quality mode: analysis frames stamped with the time they describe; the correction is read for the exact
        // sample being played.
        struct Frame { long long time = 0; float midi = -1.0f, period = 0.0f, corr = 0.0f; bool voiced = false; };

        const Frame* frameAt (long long t) const
        {
            if (frameCount == 0) return nullptr;
            const Frame* best = nullptr;
            for (int k = 0; k < frameCount; ++k)   // newest first
            {
                const Frame& f = frames[(size_t) ((frameHead - 1 - k) & (numFrames - 1))];
                best = &f;
                if (f.time <= t) break;
            }
            return best;
        }

        // Correction and period at input time t, interpolated between the frames around it.
        void valuesAt (double t, float& corr, float& period) const
        {
            corr = 0.0f; period = 0.0f;
            if (frameCount == 0) return;
            for (int k = 0; k < frameCount - 1; ++k)
            {
                const Frame& b = frames[(size_t) ((frameHead - 1 - k) & (numFrames - 1))];
                const Frame& a = frames[(size_t) ((frameHead - 2 - k) & (numFrames - 1))];
                if ((double) a.time <= t && t <= (double) b.time)
                {
                    const float u = b.time > a.time ? (float) ((t - (double) a.time) / (double) (b.time - a.time)) : 0.0f;
                    corr = a.corr + (b.corr - a.corr) * u;
                    period = a.voiced && b.voiced ? a.period + (b.period - a.period) * u : (u < 0.5f ? a.period : b.period);
                    return;
                }
            }
            if (const Frame* f = frameAt ((long long) t)) { corr = f->corr; period = f->period; }
        }

        float correctionAt (long long t) const
        {
            float c, p;
            valuesAt ((double) t, c, p);
            return c;
        }

        // Vibrato offset in semitones for the current sample.
        float vibrato (const TuneSettings& s, bool isVoiced)
        {
            if (isVoiced) ++noteSamples; else noteSamples = 0;
            const float t = (float) noteSamples / fs * 1000.0f;   // ms since the note started
            float target = 0.0f;
            if (isVoiced)
                target = s.vibRiseMs <= 0.0f ? (t >= s.vibDelayMs ? 1.0f : 0.0f)
                                              : std::clamp ((t - s.vibDelayMs) / s.vibRiseMs, 0.0f, 1.0f);
            // the onset itself is shaped by Rise; this only removes clicks (5 ms) and releases over 30 ms
            const float tc = target > vibEnv ? 0.005f : 0.03f;
            vibEnv += (target - vibEnv) * (1.0f - std::exp (-1.0f / (tc * fs)));
            vibMix = std::clamp (vibMix + ((s.vibOn ? 1.0f : 0.0f) > vibMix ? 1.0f : -1.0f) / (0.01f * fs), 0.0f, 1.0f);
            if (vibMix <= 0.0f) { lastVib = 0.0f; return 0.0f; }
            const float w = s.vibVariation;
            const float rate = s.vibRateHz * (1.0f + 0.25f * w * wander.next (0.7f, fs));
            const float depth = s.vibCents * 0.01f * (1.0f + 0.4f * w * wanderDepth.next (0.5f, fs));
            vibPhase += rate / fs;
            vibPhase -= std::floor (vibPhase);
            lastVib = depth * vibEnv * vibMix * std::sin (twoPi * vibPhase);
            return lastVib;
        }

        //==============================================================================================================
        // Analysis (both modes)
        void analyse (float x, const TuneSettings& s)
        {
            level = std::abs (x) > level ? std::abs (x) : level * 0.9995f;
            const float y = aa2.process (aa1.process (x));
            if (++decCount < decim) return;
            decCount = 0;
            ring[(size_t) ringPos] = y;
            ringPos = (ringPos + 1) & 4095;
            if (++hopCount >= hop)
            {
                hopCount = 0;
                detect (s);
            }
        }

        void detect (const TuneSettings& s)
        {
            static const float maxF[4] = { 1100, 700, 520, 350 };
            const int minLag = std::max (2, (int) (dfs / maxF[range]));
            const int maxLag = std::min (maxLagCap, (int) (dfs / (float) lowestFreq (range)) + 2);
            const int w = maxLag;
            auto at = [this] (int back) { return ring[(size_t) ((ringPos - 1 - back) & 4095)]; };

            // YIN: difference function and its cumulative mean normalised form for every lag
            float d[maxLagCap + 2], cmnd[maxLagCap + 2];
            float running = 0.0f;
            cmnd[0] = 1.0f; d[0] = 0.0f;
            for (int tau = 1; tau <= maxLag + 1; ++tau)
            {
                float acc2 = 0.0f;
                for (int j = 0; j < w; ++j) { const float diff = at (j) - at (j + tau); acc2 += diff * diff; }
                d[tau] = acc2;
                running += acc2;
                cmnd[tau] = running > 0.0f ? acc2 * (float) tau / running : 1.0f;
            }
            auto localMin = [&] (int t)
            {
                while (t + 1 <= maxLag && cmnd[t + 1] < cmnd[t]) ++t;
                while (t - 1 > minLag && cmnd[t - 1] < cmnd[t]) --t;
                return t;
            };
            // voicing hysteresis: a stricter threshold to start a note than to continue one
            const float threshold = voiced ? 0.25f : 0.15f;
            int best = -1;
            for (int tau = minLag + 1; tau <= maxLag; ++tau)
                if (cmnd[tau] < threshold) { best = localMin (tau); break; }
            // octave protection: while a note continues, prefer the lag near the current period when it is about as good
            if (best > 0 && voiced && tauTrack > 0.0f)
            {
                for (float k : { 2.0f, 0.5f, 3.0f, 1.0f / 3.0f })
                {
                    const float cand = (float) best * k;
                    if (std::abs (cand - tauTrack) < 0.08f * tauTrack && cand >= (float) minLag && cand <= (float) maxLag)
                    {
                        const int c = localMin ((int) std::lround (cand));
                        if (cmnd[c] < 0.3f) { best = c; break; }
                    }
                }
            }
            const bool loudEnough = level > 0.003f;   // about -50 dBFS
            if (best < 0 || ! loudEnough)
            {
                if (! loudEnough || ++missCount >= 3) { setUnvoiced (s); return; }
                pushFrame (s, lastMidi, periodNow, voiced);   // a short miss inside a note keeps the note
                return;
            }
            missCount = 0;
            // parabolic interpolation of the minimum
            float tauF = (float) best;
            if (best > 1 && best < maxLag)
            {
                const float den = d[best - 1] - 2.0f * d[best] + d[best + 1];
                if (std::abs (den) > 1e-12f)
                    tauF += std::clamp (0.5f * (d[best - 1] - d[best + 1]) / den, -0.5f, 0.5f);
            }
            tauTrack = tauF;
            float midi = 69.0f + 12.0f * std::log2 (dfs / tauF / 440.0f);
            // 3-point median against single outliers
            med[medCount % 3] = midi;
            ++medCount;
            if (voiced && medCount >= 3)
            {
                const float a = med[0], b = med[1], c = med[2];
                midi = std::max (std::min (a, b), std::min (std::max (a, b), c));
            }
            periodNow = fs / (440.0f * std::pow (2.0f, (midi - 69.0f) / 12.0f));
            pushFrame (s, midi, periodNow, true);
        }

        void setUnvoiced (const TuneSettings& s)
        {
            missCount = 3;
            medCount = 0;
            tauTrack = 0.0f;
            periodNow = 0.0f;
            pushFrame (s, -1.0f, 0.0f, false);
        }

        // One analysis result: note logic, correction target, and (High quality) a time-stamped frame.
        void pushFrame (const TuneSettings& s, float midi, float period, bool isVoiced)
        {
            const float dt = (float) (hop * decim);
            // the time the estimate describes: the middle of the samples the difference function compared (window plus
            // one period), one hop more when the 3-point median picked the middle value, plus the anti-alias filters' delay
            const int w = std::min (maxLagCap, (int) (dfs / (float) lowestFreq (range)) + 2) * decim;
            const float back = 0.5f * ((float) w + (isVoiced ? period : 0.0f)) + (isVoiced && medCount >= 3 ? (float) (hop * decim) : 0.0f)
                             + 0.0001f * fs;
            const long long time = nIn - (long long) std::lround (back);
            // Octave errors of the detector (common on high, closed vowels) are folded back to the note's octave; the
            // correction only depends on the pitch class, so this is safe.
            if (isVoiced && voiced && noteFrames > 0)
                midi += 12.0f * std::round ((centre - midi) / 12.0f);
            // slope (semitones per sample) for extrapolation to the played sample in Tracking mode
            if (isVoiced && voiced && lastMidi >= 0.0f)
            {
                const float sl = (midi - lastMidi) / dt;
                slope += (sl - slope) * 0.5f;
            }
            else slope = 0.0f;
            const bool wasVoiced = voiced;
            voiced = isVoiced;
            lastMidi = isVoiced ? midi : -1.0f;

            float target = 0.0f;
            if (isVoiced)
            {
                // The note's centre: at the start of a note (scoops, fast phrases) the last ~20 ms; once the note is
                // held, the average over one vibrato cycle (~190 ms), so vibrato does not move the target note.
                // A new note starts when the recent pitch stays away from that average by more than 0.75 semitones.
                if (! wasVoiced) { startNote(); targetNote = -1; }
                pushHistory (midi);
                const float grid = s.detuneCents / 100.0f;   // Detune: every target note sits this many semitones off 12-TET A440
                const float shortMean = historyMean (std::max (1, (int) (0.02f * fs / dt)));
                // A new note: the last 15 ms have left the held note's centre (its vibrato-free average) by more than
                // 0.5 semitones for 10 ms, or by more than 0.7 at once (a legato step or a jump), and lie nearer
                // another note (timing matched to reference renders of a real vocal). Vibrato swings around the centre, so it does not count. The note's history restarts
                // so its average describes only the new note.
                if (targetNote >= 0 && noteFrames > 2)
                {
                    const float recent = historyMean (std::max (1, (int) (0.015f * fs / dt)));
                    const float ref = (float) noteFrames * dt > 0.11f * fs ? centre : (float) targetNote + grid;
                    const float away = std::abs (recent - ref);
                    const bool otherNote = std::abs (recent - ((float) targetNote + grid)) > 0.55f;
                    if (away > 0.5f && otherNote) ++jumpFrames; else jumpFrames = 0;
                    if (otherNote && (away > 0.7f || (float) jumpFrames * dt > 0.01f * fs))
                    {
                        startNote();
                        pushHistory (midi);
                        targetNote = -1;
                    }
                }
                // The note's centre: at the start of a note (scoops, fast phrases) the last ~20 ms, so the target follows
                // the voice at once; once the note is held, the average over one vibrato cycle (~190 ms, leaving out the
                // first 60 ms, the scoop or glide into it), so vibrato does not move the target note.
                const int onsetFrames = (int) (0.06f * fs / dt);
                const int longFrames = noteFrames - onsetFrames > (int) (0.05f * fs / dt) ? noteFrames - onsetFrames : noteFrames;
                const float longMean = historyMean (std::min (longFrames, std::max (1, (int) (0.19f * fs / dt))));
                const bool settled = (float) noteFrames * dt > 0.11f * fs;
                centre = settled ? longMean : shortMean;
                heldSamples += (long) dt;

                int bestNote = -1;
                float bestDist = 1e9f;
                for (int note = (int) std::floor (centre - grid) - 7; note <= (int) std::ceil (centre - grid) + 7; ++note)
                {
                    if (! s.notes[((note % 12) + 12) % 12]) continue;
                    float dist = std::abs ((float) note + grid - centre);
                    if (note == targetNote && settled) dist -= 0.06f;   // a little hysteresis once the note is held
                    if (dist < bestDist) { bestDist = dist; bestNote = note; }
                }
                if (bestNote != targetNote) { heldSamples = 0; noteSamples = 0; }
                targetNote = bestNote;
                if (s.correct)
                {
                    // Retune Speed also sets how much of the singer's own vibrato stays: 0 ms corrects every instant
                    const float flatten = s.speedMs <= 0.0f ? 1.0f : std::exp (-s.speedMs / 30.0f);
                    const float ref = flatten * midi + (1.0f - flatten) * centre;
                    target = bestNote >= 0 ? std::clamp ((float) bestNote + grid - ref, -6.0f, 6.0f) : 0.0f;
                    // Glides between notes pass through: while the voice moves fast (above ~18 semitones per second)
                    // the correction relaxes, down to 35 % at 70 st/s, so a slide is not chopped into steps. This
                    // matches how established tuners treat transitions (measured on reference renders).
                    const float stPerSec = std::abs (slope) * fs;
                    target *= 1.0f - 0.65f * std::clamp ((stPerSec - 18.0f) / 52.0f, 0.0f, 1.0f);
                }
                else target = s.detuneCents / 100.0f;
            }
            desired = target;
            lastFrameTime = time;

            if (quality == HighQuality)
            {
                // the correction is smoothed in frame steps and stored with the frame's time
                smoothCorrection (s, dt);
                Frame f;
                f.time = time; f.midi = isVoiced ? midi : -1.0f; f.period = period; f.voiced = isVoiced; f.corr = correction;
                frames[(size_t) (frameHead & (numFrames - 1))] = f;
                ++frameHead;
                frameCount = std::min (frameCount + 1, numFrames);
            }
        }

        void startNote() { noteFrames = 0; jumpFrames = 0; heldSamples = 0; }
        void pushHistory (float m)
        {
            history[(size_t) (historyPos++ & (historySize - 1))] = m;
            ++noteFrames;
        }
        float historyMean (int count) const
        {
            count = std::clamp (count, 1, std::min (noteFrames, historySize));
            float sum = 0.0f;
            for (int k = 1; k <= count; ++k) sum += history[(size_t) ((historyPos - k) & (historySize - 1))];
            return sum / (float) count;
        }

        // Moves the correction towards the target over `steps` samples (Retune Speed, Humanize).
        void smoothCorrection (const TuneSettings& s, float steps)
        {
            float target = desired;
            if (quality == Tracking && voiced && s.correct)
            {
                // follow the pitch between analyses: extrapolate from the window centre to the sample being played
                const float flatten = s.speedMs <= 0.0f ? 1.0f : std::exp (-s.speedMs / 30.0f);
                const float ahead = (float) (nIn - lastFrameTime - latency);
                target -= flatten * std::clamp (slope * ahead, -0.5f, 0.5f);
            }
            float tc = s.correct ? std::max (0.0f, s.speedMs) : 15.0f;
            if (voiced && s.correct)
            {
                const float held = (float) heldSamples / fs;
                if (held > 0.15f)   // Humanize: held notes are corrected more gently
                    tc = tc * (1.0f + 3.0f * s.humanize) + 80.0f * s.humanize;
            }
            else if (! voiced) tc = 30.0f;   // unvoiced: release the correction
            if (tc <= 0.0f) correction = target;
            else correction += (target - correction) * (1.0f - std::exp (-steps / (0.001f * tc * fs)));
        }

        static constexpr int maxLagCap = 610, hop = 16, numFrames = 1024;
        float fs = 48000.0f, dfs = 12000.0f;
        int quality = Tracking, range = 1;
        bool configured = false;
        int latency = 192, decim = 4, decCount = 0, hopCount = 0, ringPos = 0;
        long long nIn = 0;
        DelayLine line[2];
        std::vector<float> ring;
        Biquad aa1, aa2;
        float delay = 192, fadeDelay = 192, fade = 0, fadeLen = 32;
        float correction = 0, desired = 0, periodNow = 0, onMix = 1, level = 0;
        float shownCorrection = 0, shownMidi = -1.0f;
        int targetNote = -1, missCount = 0, medCount = 0;
        float tauTrack = 0, med[3] {}, centre = 0, slope = 0, lastMidi = -1.0f;
        long heldSamples = 0, noteSamples = 0, jumpSamples = 0;
        static constexpr int historySize = 512;
        std::array<float, historySize> history {};
        int historyPos = 0, noteFrames = 0, jumpFrames = 0;
        long long lastFrameTime = 0;
        std::array<Frame, numFrames> frames {};
        int frameCount = 0;
        long long frameHead = 0;
        float vibPhase = 0, vibEnv = 0, vibMix = 0, lastVib = 0, tremMix = 0, tremDepthNow = 0;
        double tremPhase = 0;
        SmoothRandom wander { 0x51u }, wanderDepth { 0x77u };
        bool voiced = false, primed = false;
    };
}
