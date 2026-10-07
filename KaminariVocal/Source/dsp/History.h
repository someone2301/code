#pragma once

#include <array>
#include <atomic>
#include <cmath>

// Lock-free history of short-term levels for the GUI displays (single writer: audio thread).
// Each entry covers `hop` samples: input peak, output peak (linear) and gain change in dB.
namespace kv
{
    template <int N>
    class LevelHistory
    {
    public:
        struct Entry { float in = 0, out = 0, gr = 0; };

        void setHop (int samples) noexcept { hop = samples > 0 ? samples : 1; }

        // Audio thread, before push(): the processor's own gain reduction for this block (dB). When given, the history
        // stores it instead of the in / out peak ratio (a split-band de-esser barely changes the broadband peak).
        void noteReduction (float db) noexcept { accGr = std::fmax (accGr, db); hasGr = true; }

        // Audio thread: feed matching input / output blocks.
        void push (const float* inL, const float* inR, const float* outL, const float* outR, int n) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                accIn = std::fmax (accIn, std::fmax (std::fabs (inL[i]), std::fabs (inR[i])));
                accOut = std::fmax (accOut, std::fmax (std::fabs (outL[i]), std::fabs (outR[i])));
                if (++count >= hop)
                {
                    const auto w = writePos.load (std::memory_order_relaxed);
                    auto& e = data[(size_t) (w % (unsigned) N)];
                    e.in.store (accIn, std::memory_order_relaxed);
                    e.out.store (accOut, std::memory_order_relaxed);
                    const float gr = hasGr ? accGr : (accIn > 1e-6f ? 20.0f * std::log10 (accIn / std::fmax (accOut, 1e-9f)) : 0.0f);
                    e.gr.store (gr, std::memory_order_relaxed);
                    accGr = 0.0f;
                    writePos.store (w + 1, std::memory_order_release);
                    accIn = accOut = 0.0f;
                    count = 0;
                }
            }
        }

        // GUI thread: newest-last copy of the history.
        void read (std::array<Entry, (size_t) N>& out) const noexcept
        {
            const auto w = writePos.load (std::memory_order_acquire);
            for (int i = 0; i < N; ++i)
            {
                const auto& e = data[(size_t) ((w + (unsigned) i) % (unsigned) N)];
                out[(size_t) i] = { e.in.load (std::memory_order_relaxed), e.out.load (std::memory_order_relaxed), e.gr.load (std::memory_order_relaxed) };
            }
        }

    private:
        struct Slot { std::atomic<float> in { 0 }, out { 0 }, gr { 0 }; };
        std::array<Slot, (size_t) N> data {};
        std::atomic<unsigned> writePos { 0 };
        float accIn = 0, accOut = 0, accGr = 0;
        bool hasGr = false;
        int count = 0, hop = 128;
    };
}
