#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

// Audio side: keeps the most recent mono samples in a ring. The GUI copies the latest window
// at its own frame rate, so every display frame analyses fresh audio (overlapping windows)
// and the refresh rate does not drop at high resolution.
class SpectrumAnalyser
{
public:
    static constexpr int ringSize = 1 << 15;   // holds the largest window (16384 at 96 kHz+)

    void push (const juce::AudioBuffer<float>& buffer) noexcept
    {
        const int chs = buffer.getNumChannels();
        if (chs == 0)
            return;

        const float* l = buffer.getReadPointer (0);
        const float* r = buffer.getReadPointer (chs > 1 ? 1 : 0);
        auto w = writePos.load (std::memory_order_relaxed);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            ring[(size_t) (w++ & (ringSize - 1))].store (0.5f * (l[i] + r[i]), std::memory_order_relaxed);

        writePos.store (w, std::memory_order_release);
    }

    // GUI thread: copies the newest n samples (oldest first). Returns false until enough audio arrived.
    bool copyLatest (float* dest, int n) const noexcept
    {
        const auto w = writePos.load (std::memory_order_acquire);
        if (n > ringSize || w < (unsigned) n)
            return false;
        for (int i = 0; i < n; ++i)
            dest[i] = ring[(size_t) ((w - (unsigned) n + (unsigned) i) & (ringSize - 1))].load (std::memory_order_relaxed);
        return true;
    }

    unsigned samplesWritten() const noexcept { return writePos.load (std::memory_order_acquire); }

private:
    std::array<std::atomic<float>, ringSize> ring {};
    std::atomic<unsigned> writePos { 0 };
};

// GUI side: FFT, display ballistics and per-pixel lookup.
class SpectrumProcessor
{
public:
    enum Resolution { Low = 0, Medium, High, Maximum };
    enum Speed { VerySlow = 0, Slow, MediumSpeed, Fast, VeryFast };

    static juce::StringArray resolutionNames() { return { "Low", "Medium", "High", "Maximum" }; }
    static juce::StringArray speedNames()      { return { "Very Slow", "Slow", "Medium", "Fast", "Very Fast" }; }

    // FFT length at 48 kHz; doubled per octave of sample rate above that so the time window stays constant.
    static int baseOrder (int res) noexcept { return 10 + juce::jlimit (0, 3, res); }   // 1024 .. 8192

    // Release time constant of the display in ms. Attack is immediate.
    static double releaseMs (int speed) noexcept
    {
        static constexpr double t[] = { 2000.0, 900.0, 400.0, 150.0, 50.0 };
        return t[juce::jlimit (0, 4, speed)];
    }

    static constexpr float floorDb = -120.0f;

    void configure (int res, int speedIn, double sampleRate)
    {
        int order = baseOrder (res);
        for (double rate = sampleRate; rate > 66000.0 && order < 14; rate *= 0.5)
            ++order;

        speed = speedIn;
        if (order == currentOrder && std::abs (sampleRate - sr) < 0.5)
            return;

        currentOrder = order;
        sr = sampleRate;
        size = 1 << order;
        fft = std::make_unique<juce::dsp::FFT> (order);
        window.assign ((size_t) size, 0.0f);
        juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), (size_t) size,
                                                                 juce::dsp::WindowingFunction<float>::hann, false);
        double sum = 0.0;
        for (auto v : window)
            sum += v;
        amplitudeScale = (float) (2.0 / sum);   // a full-scale sine reads 0 dB
        input.assign ((size_t) size, 0.0f);
        work.assign ((size_t) size * 2, 0.0f);
        spectrum.assign ((size_t) size / 2 + 1, floorDb);
    }

    int fftSize() const noexcept { return size; }
    double sampleRate() const noexcept { return sr; }
    float* inputBuffer() noexcept { return input.data(); }

    // Analyse the samples currently in inputBuffer(); dtMs = time since the previous frame.
    void process (double dtMs)
    {
        if (fft == nullptr)
            return;
        for (int i = 0; i < size; ++i)
            work[(size_t) i] = input[(size_t) i] * window[(size_t) i];
        std::fill (work.begin() + size, work.end(), 0.0f);
        fft->performFrequencyOnlyForwardTransform (work.data(), true);

        const float k = (float) (1.0 - std::exp (-juce::jmax (0.0, dtMs) / releaseMs (speed)));
        for (size_t i = 0; i < spectrum.size(); ++i)
        {
            const float db = juce::Decibels::gainToDecibels (work[i] * amplitudeScale, floorDb);
            spectrum[i] = db >= spectrum[i] ? db : spectrum[i] + (db - spectrum[i]) * k;
        }
    }

    void releaseToFloor (double dtMs)
    {
        const float k = (float) (1.0 - std::exp (-juce::jmax (0.0, dtMs) / releaseMs (speed)));
        for (auto& v : spectrum)
            v += (floorDb - v) * k;
    }

    // Level for a display column covering [f0, f1): the loudest bin when the column spans bins
    // (keeps narrow peaks visible), interpolated between bins when it is narrower than one bin.
    float columnDb (double f0, double f1) const noexcept
    {
        if (spectrum.empty())
            return floorDb;
        const double binHz = sr / size;
        const double b0 = f0 / binHz, b1 = f1 / binHz;
        const int last = (int) spectrum.size() - 1;

        if (b1 - b0 >= 1.0)
        {
            float m = floorDb;
            for (int i = juce::jlimit (0, last, (int) std::ceil (b0)); i <= juce::jlimit (0, last, (int) b1); ++i)
                m = juce::jmax (m, spectrum[(size_t) i]);
            return m;
        }

        const double c = 0.5 * (b0 + b1);
        const int i0 = juce::jlimit (0, last - 1, (int) c);
        const float fr = (float) juce::jlimit (0.0, 1.0, c - i0);
        return spectrum[(size_t) i0] * (1.0f - fr) + spectrum[(size_t) i0 + 1] * fr;
    }

private:
    int currentOrder = -1, size = 0, speed = Fast;
    double sr = 48000.0;
    float amplitudeScale = 1.0f;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, input, work, spectrum;
};
