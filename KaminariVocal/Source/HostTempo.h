#pragma once

#include <atomic>
#include <cmath>
#include <optional>

// Host transport as last seen by the audio thread, handed to the GUI without locks.
// The audio thread writes once per block (single writer); the GUI reads with a sequence lock.
// The GUI derives the beat position from the most recent host anchor, so it is re-anchored
// to the host's own ppq position on every block and cannot drift.
struct TempoSnapshot
{
    bool   valid = false;     // host supplied both tempo and ppq position
    bool   playing = false;
    double bpm = 0.0;
    double ppq = 0.0;         // beat position at the start of the block
    double anchorMs = 0.0;    // juce::Time::getMillisecondCounterHiRes() when the block was processed
};

class HostTempo
{
public:
    // A host anchor older than this is treated as unavailable (host stopped calling processBlock).
    static constexpr double maxAnchorAgeMs = 250.0;

    // Audio thread only.
    void publish (const TempoSnapshot& s) noexcept
    {
        const auto s0 = seq.load (std::memory_order_relaxed);
        seq.store (s0 + 1, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        valid.store (s.valid, std::memory_order_relaxed);
        playing.store (s.playing, std::memory_order_relaxed);
        bpm.store (s.bpm, std::memory_order_relaxed);
        ppq.store (s.ppq, std::memory_order_relaxed);
        anchorMs.store (s.anchorMs, std::memory_order_relaxed);
        seq.store (s0 + 2, std::memory_order_release);
    }

    TempoSnapshot read() const noexcept
    {
        TempoSnapshot s;
        for (;;)
        {
            const auto s1 = seq.load (std::memory_order_acquire);
            if ((s1 & 1u) != 0)
                continue;
            s.valid = valid.load (std::memory_order_relaxed);
            s.playing = playing.load (std::memory_order_relaxed);
            s.bpm = bpm.load (std::memory_order_relaxed);
            s.ppq = ppq.load (std::memory_order_relaxed);
            s.anchorMs = anchorMs.load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);
            if (seq.load (std::memory_order_relaxed) == s1)
                return s;
        }
    }

    // Beat position at nowMs, or nothing when the transport is stopped or the host gives no tempo.
    static std::optional<double> beatsAt (const TempoSnapshot& s, double nowMs) noexcept
    {
        if (! s.valid || ! s.playing || s.bpm <= 0.0)
            return std::nullopt;
        const double age = nowMs - s.anchorMs;
        if (age < -maxAnchorAgeMs || age > maxAnchorAgeMs)
            return std::nullopt;
        return s.ppq + std::fmax (0.0, age) * s.bpm / 60000.0;
    }

    // Beats per glow pulse: one per beat, halved until the pulse is at most maxPulseHz.
    static double beatsPerPulse (double bpmIn) noexcept
    {
        constexpr double maxPulseHz = 2.4;
        double beats = 1.0;
        while (bpmIn / 60.0 / beats > maxPulseHz && beats < 64.0)
            beats *= 2.0;
        return beats;
    }

    // Glow level 0..1. Peaks on the beat, follows a cosine (no hard flashes).
    // Without tempo or transport the glow is steady.
    static float glow (std::optional<double> beats, double bpmIn) noexcept
    {
        if (! beats.has_value())
            return steadyGlow;
        const double period = beatsPerPulse (bpmIn);
        double phase = std::fmod (*beats / period, 1.0);
        if (phase < 0.0)
            phase += 1.0;
        return (float) (0.5 + 0.5 * std::cos (2.0 * 3.14159265358979323846 * phase));
    }

    static constexpr float steadyGlow = 0.6f;

private:
    std::atomic<unsigned> seq { 0 };
    std::atomic<bool> valid { false }, playing { false };
    std::atomic<double> bpm { 0.0 }, ppq { 0.0 }, anchorMs { 0.0 };
};
