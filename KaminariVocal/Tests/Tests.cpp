#include <numeric>
// Offline checks for Kaminari Vocal Alt: levels, the channel modules, persistence, presets and the editor.
// Build with -DKV_BUILD_TESTS=ON and run: KaminariVocalTests [snapshot_dir]
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>
#include <functional>

// The pass-through tests compare samples for exact (bit-identical) equality on purpose.
#pragma GCC diagnostic ignored "-Wfloat-equal"

namespace
{
    int failures = 0;
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("[%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failures;
    }

    void setParam (KaminariVocalProcessor& p, const char* id, float value)
    {
        auto* prm = p.apvts.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    }

    float getParam (KaminariVocalProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }

    void prepare (KaminariVocalProcessor& p)
    {
        p.setPlayConfigDetails (2, 2, sr, block);
        p.prepareToPlay (sr, block);
    }

    using Source = std::function<float (int channel, long sample)>;

    // A vocal-like test signal: a 220 Hz tone with harmonics in 300 ms notes and gaps, plus a little noise.
    float vocal (int, long n)
    {
        const double t = (double) n / sr;
        const double env = std::fmod (t, 0.4) < 0.3 ? 1.0 : 0.0;
        const double f = 220.0;
        double s = 0.5 * std::sin (2 * kv::pi * f * t) + 0.25 * std::sin (2 * kv::pi * 2 * f * t) + 0.12 * std::sin (2 * kv::pi * 3 * f * t);
        kv::Random rnd ((uint32_t) n * 2654435761u + 7u);   // deterministic per sample
        return (float) (0.5 * env * s) + 0.01f * rnd.next();
    }

    struct Render { juce::AudioBuffer<float> in, out; };

    // Renders `seconds` of audio; `perBlock` may change parameters before each block.
    // Switches every channel module off, so the dry path is a pure delay of the reported latency.
    void neutral (KaminariVocalProcessor& p)
    {
        for (auto* id : { "tn_on", "eq_on", "mb_on", "lv_on", "ds_on", "rs_on" })
            setParam (p, id, 0.0f);
    }

    // Renders `seconds` of audio; the output is shifted back by the reported latency, so it lines up with the input.
    Render render (KaminariVocalProcessor& p, double seconds, Source src, std::function<void (int)> perBlock = nullptr)
    {
        const int latency = p.getLatencySamples();
        const int wanted = (int) std::ceil (seconds * sr / block);
        const int blocks = wanted + (latency + block - 1) / block;
        Render r, raw;
        raw.in.setSize (2, blocks * block);
        raw.out.setSize (2, blocks * block);
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b)
        {
            if (perBlock) perBlock (b);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < block; ++i)
                {
                    const float x = src (c, (long) b * block + i);
                    buf.setSample (c, i, x);
                    raw.in.setSample (c, b * block + i, x);
                }
            p.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c)
                raw.out.copyFrom (c, b * block, buf, c, 0, block);
        }
        r.in.setSize (2, wanted * block);
        r.out.setSize (2, wanted * block);
        for (int c = 0; c < 2; ++c)
        {
            r.in.copyFrom (c, 0, raw.in, c, 0, wanted * block);
            r.out.copyFrom (c, 0, raw.out, c, latency, wanted * block);
        }
        return r;
    }

    // Largest |out - in * gain| over a range of samples.
    float maxDiff (const Render& r, float gain = 1.0f, int from = 0)
    {
        float m = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (int i = from; i < r.out.getNumSamples(); ++i)
                m = std::max (m, std::abs (r.out.getSample (c, i) - r.in.getSample (c, i) * gain));
        return m;
    }

    double correlation (const std::vector<float>& a, const std::vector<float>& b)
    {
        double ab = 0, aa = 0, bb = 0;
        for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
        {
            ab += (double) a[i] * b[i];
            aa += (double) a[i] * a[i];
            bb += (double) b[i] * b[i];
        }
        return ab / std::sqrt (std::max (1e-30, aa * bb));
    }

    // Level (dB) of one frequency in a channel over a sample range (Goertzel).
    float toneDb (const juce::AudioBuffer<float>& b, int ch, double freq, int from, int to)
    {
        const double w = 2.0 * kv::pi * freq / sr, cw = 2.0 * std::cos (w);
        double s1 = 0, s2 = 0;
        for (int i = from; i < to; ++i) { const double s0 = b.getSample (ch, i) + cw * s1 - s2; s2 = s1; s1 = s0; }
        const double power = s1 * s1 + s2 * s2 - cw * s1 * s2;
        return (float) juce::Decibels::gainToDecibels (2.0 * std::sqrt (std::max (0.0, power)) / (to - from), -200.0);
    }

    Source sine (double freq, float db)
    {
        const float a = juce::Decibels::decibelsToGain (db);
        return [=] (int, long n) { return a * (float) std::sin (2 * kv::pi * freq * n / sr); };
    }

    // Fundamental frequency from zero crossings (for clean single tones).
    double zeroCrossFreq (const juce::AudioBuffer<float>& b, int ch, int from, int to)
    {
        int first = -1, last = -1, count = 0;
        for (int i = from + 1; i < to; ++i)
            if (b.getSample (ch, i - 1) < 0.0f && b.getSample (ch, i) >= 0.0f)
            {
                if (first < 0) first = i; else { last = i; ++count; }
            }
        return count > 0 ? sr * count / (double) (last - first) : 0.0;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    // ---- optional benchmark (KV_BENCH=<block size>): audio cost per block and drawing cost per frame -------------------------
    if (std::getenv ("KV_BENCH") != nullptr)
    {
        auto runDsp = [] (const char* name, std::function<void (KaminariVocalProcessor&)> setup)
        {
            const int bs = juce::jmax (32, std::atoi (std::getenv ("KV_BENCH")));   // KV_BENCH=<block size>, e.g. 128
            KaminariVocalProcessor p;
            setup (p);
            p.setPlayConfigDetails (2, 2, sr, bs);
            p.prepareToPlay (sr, bs);
            juce::AudioBuffer<float> buf (2, bs); juce::MidiBuffer midi;
            const int blocks = (int) (20.0 * sr / bs);
            std::vector<double> t ((size_t) blocks);
            long n = 0;
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < bs; ++i, ++n) { const float v = vocal (0, n); buf.setSample (0, i, v); buf.setSample (1, i, v); }
                const auto t0 = juce::Time::getHighResolutionTicks();
                p.processBlock (buf, midi);
                t[(size_t) b] = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
            }
            double sum = 0; for (double v : t) sum += v;
            std::sort (t.begin(), t.end());
            const double budget = bs / sr;
            std::printf ("[BENCH] %-46s mean %5.1f %%  99.9th pct %5.1f %%  worst %5.1f %% of the block time\n", name,
                         100.0 * sum / blocks / budget, 100.0 * t[(size_t) (blocks * 0.999)] / budget, 100.0 * t.back() / budget);
        };
        auto busy = [] (KaminariVocalProcessor& p)
        {
            p.presets.loadChainPreset ("Pop Lead");
            for (auto* id : { "tn_on", "eq_on", "mb_on", "lv_on", "ds_on", "rs_on" }) setParam (p, id, 1.0f);
        };
        runDsp ("every module off (base cost)", [] (KaminariVocalProcessor& p) { neutral (p); });
        for (auto [label, id] : { std::pair { "only Tune", "tn_on" }, { "only EQ", "eq_on" }, { "only Multiband", "mb_on" },
                                  { "only Compression", "lv_on" }, { "only De-ess", "ds_on" }, { "only Resonance", "rs_on" } })
            runDsp (label, [id = id] (KaminariVocalProcessor& p) { p.presets.loadChainPreset ("Pop Lead"); neutral (p); setParam (p, id, 1.0f); });
        runDsp ("typical: Tune, EQ, Compression, De-ess", [] (KaminariVocalProcessor& p)
        {
            p.presets.loadChainPreset ("Pop Lead");
            neutral (p);
            for (auto* id : { "tn_on", "eq_on", "lv_on", "ds_on" }) setParam (p, id, 1.0f);
        });
        runDsp ("default session", [] (KaminariVocalProcessor&) {});
        runDsp ("every module on", busy);
        runDsp ("every module + 4x oversampling (2)", [&] (KaminariVocalProcessor& p) { busy (p); for (auto* id : { "mb_os", "rs_os" }) setParam (p, id, 2.0f); });

        // drawing: one frame of each animated view, software renderer
        auto timePaint = [] (const char* name, juce::Component& c, float scale, std::function<void()> tick)
        {
            juce::Image img (juce::Image::ARGB, juce::roundToInt (c.getWidth() * scale), juce::roundToInt (c.getHeight() * scale), true);
            const int frames = 120;
            double total = 0, worst = 0;
            for (int f = 0; f < frames; ++f)
            {
                if (tick) tick();
                juce::Graphics g (img);
                g.addTransform (juce::AffineTransform::scale (scale));
                const auto t0 = juce::Time::getHighResolutionTicks();
                c.paintEntireComponent (g, true);
                const double dt = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
                total += dt; worst = std::max (worst, dt);
            }
            std::printf ("[BENCH] draw %-41s mean %5.2f ms  worst %5.2f ms per frame (%.0f %% of one 60 Hz frame)\n", name,
                         1000.0 * total / frames, 1000.0 * worst, 100.0 * (total / frames) / (1.0 / 60.0));
        };
        KaminariVocalProcessor p;
        prepare (p);
        setParam (p, "mb1_thresh", -50.0f);
        LightningSlider h (*p.apvts.getParameter ("mb1_thresh"), p.hostTempo, "Threshold", true);
        h.setCompact (true);
        h.setBounds (0, 0, 150, 180);
        timePaint ("one hammer at full pull (1x)", h, 1.0f, [&] { h.advanceAnimation (16.7); });
        timePaint ("one hammer at full pull (2x Retina)", h, 2.0f, [&] { h.advanceAnimation (16.7); });
        busy (p);
        for (int b = 1; b <= 6; ++b) setParam (p, ("eq" + juce::String (b) + "_used").toRawUTF8(), 1.0f);
        std::unique_ptr<KaminariVocalEditor> ed (dynamic_cast<KaminariVocalEditor*> (p.createEditor()));
        ed->showTab (false, KaminariVocalEditor::TabTune);
        timePaint ("whole window, Basic view (1x)", *ed, 1.0f, nullptr);
        timePaint ("whole window, Basic view (2x Retina)", *ed, 2.0f, nullptr);
        auto& curve = ed->eqSection().curve;
        timePaint ("Basic EQ graph, 6 bands (2x Retina)", curve, 2.0f, nullptr);
        float gain = 0.0f;
        timePaint ("Basic EQ graph, a band being dragged (2x)", curve, 2.0f, [&] { gain = gain > 10.0f ? -10.0f : gain + 0.5f; setParam (p, "eq2_gain", gain); });
        ed->showTab (true, KaminariVocalEditor::TabEq);
        timePaint ("whole window, EQ page (2x Retina)", *ed, 2.0f, nullptr);
        ed->showTab (true, KaminariVocalEditor::TabMultiband);
        timePaint ("whole window, Multiband page (2x Retina)", *ed, 2.0f, nullptr);
        ed->showTab (true, KaminariVocalEditor::TabResonance);
        timePaint ("whole window, Resonance page (2x Retina)", *ed, 2.0f, nullptr);
        return 0;
    }

    // ---- levels and layouts ---------------------------------------------------------------------------------------
    {
        KaminariVocalProcessor p;
        neutral (p);
        prepare (p);
        check (p.getLatencySamples() == 96, "reported latency is 96 samples (2 ms) at 48 kHz (Tune's fixed delay), also with Tune off");
        const auto r = render (p, 1.0, vocal);
        check (maxDiff (r) == 0.0f, "every module off: the output is the input delayed by exactly the reported latency");
        setParam (p, kvid::outGain, -6.0f);
        prepare (p);
        const auto g = render (p, 0.5, vocal);
        check (maxDiff (g, juce::Decibels::decibelsToGain (-6.0f)) < 1.0e-6f, "Output Gain -6 dB scales the output by exactly -6 dB");
    }
    {
        KaminariVocalProcessor p;
        juce::AudioProcessor::BusesLayout monoStereo;
        monoStereo.inputBuses.add (juce::AudioChannelSet::mono());
        monoStereo.outputBuses.add (juce::AudioChannelSet::stereo());
        juce::AudioProcessor::BusesLayout stereoMono;
        stereoMono.inputBuses.add (juce::AudioChannelSet::stereo());
        stereoMono.outputBuses.add (juce::AudioChannelSet::mono());
        check (p.checkBusesLayoutSupported (monoStereo) && ! p.checkBusesLayoutSupported (stereoMono),
               "mono-in / stereo-out is supported (stereo-in / mono-out is not)");
        p.setBusesLayout (monoStereo);
        p.presets.loadChainPreset ("Pop Lead");
        p.setRateAndBufferSizeDetails (sr, block);
        p.prepareToPlay (sr, block);
        juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi;
        double diff = 0, level = 0;
        for (int b = 0; b < 100; ++b)
        {
            for (int i = 0; i < block; ++i) { buf.setSample (0, i, vocal (0, (long) b * block + i)); buf.setSample (1, i, 0.0f); }
            p.processBlock (buf, midi);
            for (int i = 0; i < block; ++i)
            {
                diff = std::max (diff, (double) std::abs (buf.getSample (0, i) - buf.getSample (1, i)));
                level = std::max (level, (double) std::abs (buf.getSample (0, i)));
            }
        }
        check (level > 0.01 && diff == 0.0, "mono-in / stereo-out: the vocal is processed as dual mono (both outputs identical)");
    }

    // ---- channel modules ------------------------------------------------------------------------------------------
    {
        // the pass-through above, with Tune on as well: still delayed by exactly the reported latency when not correcting
        KaminariVocalProcessor p;
        neutral (p);
        prepare (p);
        check (kv::Tune::latencyFor (sr) == 96 && kv::Tune::latencyFor (96000.0) == 192, "Tune latency: 96 samples at 48 kHz, 192 at 96 kHz (2 ms)");
    }
    {
        // EQ: a +6 dB bell at 1 kHz; a 24 dB/oct low cut at 100 Hz
        KaminariVocalProcessor p;
        neutral (p);
        setParam (p, "eq_on", 1.0f);
        setParam (p, "eq1_used", 1.0f); setParam (p, "eq1_type", 0.0f); setParam (p, "eq1_freq", 1000.0f); setParam (p, "eq1_gain", 6.0f); setParam (p, "eq1_q", 1.0f);
        prepare (p);
        auto r = render (p, 0.5, sine (1000.0, -20.0f));
        const float g = toneDb (r.out, 0, 1000.0, 12000, 24000) - toneDb (r.in, 0, 1000.0, 12000, 24000);
        check (std::abs (g - 6.0f) < 0.3f, "EQ bell +6 dB at 1 kHz gives +" + juce::String (g, 2) + " dB");
        setParam (p, "eq1_type", 3.0f); setParam (p, "eq1_freq", 100.0f); setParam (p, "eq1_slope", 3.0f); setParam (p, "eq1_q", 0.71f);
        prepare (p);
        r = render (p, 0.5, sine (30.0, -20.0f));
        const float cut = toneDb (r.out, 0, 30.0, 12000, 24000) - toneDb (r.in, 0, 30.0, 12000, 24000);
        check (cut < -35.0f, "EQ low cut 100 Hz, 24 dB/oct: 30 Hz down " + juce::String (-cut, 1) + " dB");
        kv::EqBandSettings e; e.used = true; e.type = kv::HighShelf; e.freq = 5000; e.gainDb = 4; e.q = 0.71f;
        check (std::abs (kv::EqDesign::make (e, sr).magnitudeDb (18000.0, sr) - 4.0) < 0.5, "EQ curve math: high shelf +4 dB reads +4 dB at 18 kHz");
    }
    {
        // Compression (LA-2A style): Compression 0 and Gain 0 leave the signal untouched; switched off it is untouched
        for (int variant = 0; variant < 2; ++variant)
        {
            KaminariVocalProcessor p;
            neutral (p);
            setParam (p, "lv_on", variant == 0 ? 1.0f : 0.0f);
            setParam (p, "lv_peak", variant == 0 ? 0.0f : 80.0f);
            setParam (p, "lv_gain", 0.0f);
            prepare (p);
            const auto r = render (p, 1.0, vocal);
            check (maxDiff (r) == 0.0f, variant == 0 ? "Compression 0 with Gain 0: output is bit-identical to the input"
                                                     : "Compression switched off: output is bit-identical to the input");
        }
    }
    {
        // more Compression = more reduction of a steady -6 dBFS tone; Gain adds exactly its dB on top
        auto level = [] (float peak, float gain)
        {
            KaminariVocalProcessor p;
            neutral (p);
            setParam (p, "lv_on", 1.0f); setParam (p, "lv_peak", peak); setParam (p, "lv_gain", gain);
            prepare (p);
            const auto r = render (p, 2.0, sine (1000.0, -6.0f));
            return std::pair { toneDb (r.out, 0, 1000.0, 72000, 96000), p.moduleGr[KaminariVocalProcessor::ModCompression].load() };
        };
        const auto none = level (0.0f, 0.0f), light = level (30.0f, 0.0f), mid = level (50.0f, 0.0f), heavy = level (75.0f, 0.0f);
        const auto made = level (50.0f, 6.0f), plus = level (0.0f, 6.0f);
        check (std::abs (none.first + 6.0f) < 0.1f && light.first < none.first - 1.0f && mid.first < light.first - 2.0f && heavy.first < mid.first - 2.0f,
               "Compression 0 / 30 / 50 / 75 on a -6 dBFS tone: " + juce::String (none.first, 1) + " / " + juce::String (light.first, 1) + " / "
               + juce::String (mid.first, 1) + " / " + juce::String (heavy.first, 1) + " dBFS");
        check (std::abs (made.first - mid.first - 6.0f) < 0.2f && std::abs (plus.first - none.first - 6.0f) < 0.1f,
               "Gain +6 dB raises the output by " + juce::String (made.first - mid.first, 2) + " dB (compressing) and "
               + juce::String (plus.first - none.first, 2) + " dB (not compressing)");
        check (mid.second > 4.0f && std::abs (mid.second - (-6.0f - mid.first)) < 1.0f,
               "Compression reports its gain reduction to the meter (" + juce::String (mid.second, 1) + " dB)");
    }
    {
        // optical release: after two seconds of heavy reduction, about half lets go within 200 ms, the rest over seconds
        KaminariVocalProcessor p;
        neutral (p);
        setParam (p, "lv_on", 1.0f); setParam (p, "lv_peak", 60.0f); setParam (p, "lv_gain", 0.0f);
        prepare (p);
        auto src = [] (int, long n) { return (n < (long) (2.0 * sr) ? 0.5f : 0.02f) * (float) std::sin (2 * kv::pi * 1000.0 * n / sr); };
        std::vector<float> gr;
        render (p, 4.0, src, [&] (int) { gr.push_back (p.moduleGr[KaminariVocalProcessor::ModCompression].load()); });
        auto at = [&] (double t) { return gr[(size_t) juce::jlimit (0, (int) gr.size() - 1, (int) (t * sr / block) + 1)]; };
        const float held = at (1.95), fast = at (2.2), slow = at (3.5);
        check (held > 6.0f && fast > 0.25f * held && fast < 0.7f * held && slow > 0.05f * held && slow < 0.8f * fast,
               "opto release: " + juce::String (held, 1) + " dB held; " + juce::String (fast, 1) + " dB 200 ms after the loud part ends; "
               + juce::String (slow, 1) + " dB after 1.5 s");
    }
    {
        // De-ess: esses are found relative to the vocal, so level does not matter; only the highs are turned down
        auto ds = [] (float freq, float range, double toneHz, float db)
        {
            KaminariVocalProcessor p;
            neutral (p);
            setParam (p, "ds_on", 1.0f); setParam (p, "ds_det_lo", freq); setParam (p, "ds_range", range);
            prepare (p);
            const auto r = render (p, 0.5, sine (toneHz, db));
            return toneDb (r.out, 0, toneHz, 12000, 24000) - toneDb (r.in, 0, toneHz, 12000, 24000);
        };
        const float ess = ds (5000.0f, 8.0f, 7000.0, -10.0f), quiet = ds (5000.0f, 8.0f, 7000.0, -40.0f), low = ds (5000.0f, 8.0f, 300.0, -10.0f);
        check (ess < -5.0f && ess > -9.0f && std::abs (quiet - ess) < 1.0f && std::abs (low) < 0.01f,
               "De-ess (Range 8 dB, Frequency 5 kHz): 7 kHz reduced " + juce::String (-ess, 1) + " dB at -10 dBFS and " + juce::String (-quiet, 1)
               + " dB at -40 dBFS; 300 Hz changed " + juce::String (low, 3) + " dB");
        const float at5k = ds (5000.0f, 10.0f, 4000.0, -10.0f), at10k = ds (10000.0f, 10.0f, 4000.0, -10.0f);
        check (at5k < -2.0f && std::abs (at10k) < 0.01f, "De-ess Frequency: a 4 kHz tone is reduced " + juce::String (-at5k, 1)
               + " dB at 5 kHz and left alone at 10 kHz (" + juce::String (at10k, 3) + " dB)");
        const float none = ds (5000.0f, 0.0f, 7000.0, -10.0f);
        check (std::abs (none) < 1.0e-4f, "De-ess Range 0: nothing is reduced (" + juce::String (none, 4) + " dB)");
    }
    {
        // Multiband default band (100-500 Hz): a loud 200 Hz tone is reduced, 3 kHz is not
        KaminariVocalProcessor p;
        neutral (p);
        setParam (p, "mb_on", 1.0f);
        prepare (p);
        auto r = render (p, 0.5, sine (200.0, -6.0f));
        const float lowMid = toneDb (r.out, 0, 200.0, 12000, 24000) - toneDb (r.in, 0, 200.0, 12000, 24000);
        r = render (p, 0.5, sine (3000.0, -6.0f));
        const float high = toneDb (r.out, 0, 3000.0, 12000, 24000) - toneDb (r.in, 0, 3000.0, 12000, 24000);
        check (lowMid < -3.0f && std::abs (high) < 0.3f, "Multiband: 200 Hz reduced " + juce::String (-lowMid, 1) + " dB, 3 kHz changed " + juce::String (high, 2) + " dB");
    }
    {
        // Resonance: a narrow ringing tone on top of broadband noise is cut
        KaminariVocalProcessor p;
        neutral (p);
        setParam (p, "rs_on", 1.0f); setParam (p, "rs_depth", 20.0f);
        prepare (p);
        auto src = [] (int c, long n) { kv::Random rnd ((uint32_t) n * 747796405u + (uint32_t) c + 1u);
                                       return 0.05f * rnd.next() + 0.3f * (float) std::sin (2 * kv::pi * 2000.0 * n / sr); };
        const auto r = render (p, 1.0, src);
        const float ring = toneDb (r.out, 0, 2000.0, 24000, 48000) - toneDb (r.in, 0, 2000.0, 24000, 48000);
        check (ring < -3.0f, "Resonance depth 20: a 2 kHz ring over noise is reduced " + juce::String (-ring, 1) + " dB");
    }
    {
        // Resonance must not cut broadband noise (no resonances), in mid/side or left/right
        for (int stereo = 0; stereo < 2; ++stereo)
        {
            KaminariVocalProcessor p;
            neutral (p);
            setParam (p, "rs_on", 1.0f); setParam (p, "rs_depth", 5.0f); setParam (p, "rs_stereo_mode", (float) stereo);
            prepare (p);
            const auto r = render (p, 1.0, [] (int c, long n) { kv::Random rnd ((uint32_t) n * 2246822519u + (uint32_t) c + 9u); return 0.1f * rnd.next(); });
            double maxRed = 0;
            for (int k = 0; k < p.resonance.active().numBands(); ++k) maxRed = std::max (maxRed, (double) p.resonance.active().bandReduction (k));
            check (maxRed < 2.0, juce::String ("Resonance leaves white noise alone (") + (stereo ? "M/S" : "L/R") + ", largest cut " + juce::String (maxRed, 1) + " dB)");
        }
    }
    {
        // Tune: A4 + 30 cents, chromatic, retune speed 0 -> pulled to 440 Hz; Tune off leaves it alone
        KaminariVocalProcessor p;
        neutral (p);
        setParam (p, "tn_on", 1.0f); setParam (p, "tn_speed", 0.0f); setParam (p, "tn_humanize", 0.0f);
        prepare (p);
        const double in = 440.0 * std::pow (2.0, 0.30 / 12.0);
        auto r = render (p, 1.0, sine (in, -12.0f));
        const double f = zeroCrossFreq (r.out, 0, 24000, 48000);
        const double cents = 1200.0 * std::log2 (f / 440.0);
        check (std::abs (cents) < 5.0, "Tune pulls A4 +30 cents to " + juce::String (cents, 1) + " cents (input " + juce::String (1200.0 * std::log2 (in / 440.0), 1) + ")");
        setParam (p, "tn_scale", 1.0f);   // C major: A is in the scale
        setParam (p, "tn_key", 0.0f);
        const double inB = 440.0 * std::pow (2.0, 1.4 / 12.0);   // between A# and B: B is the nearest C-major note
        prepare (p);
        r = render (p, 1.0, sine (inB, -12.0f));
        const double fB = zeroCrossFreq (r.out, 0, 24000, 48000);
        check (std::abs (1200.0 * std::log2 (fB / (440.0 * std::pow (2.0, 2.0 / 12.0)))) < 8.0, "Tune in C major corrects to B (493.9 Hz): " + juce::String (fB, 1) + " Hz");
        neutral (p);
        prepare (p);
        r = render (p, 0.5, sine (in, -12.0f));
        check (maxDiff (r) == 0.0f, "Tune off: the signal passes unchanged (delayed by the reported latency)");
    }
    {
        // the whole chain with a busy chain preset stays finite and bounded
        KaminariVocalProcessor p;
        p.presets.loadChainPreset ("Pop Lead");
        setParam (p, "rs_on", 1.0f);
        prepare (p);
        const auto r = render (p, 2.0, vocal);
        bool ok = true; float peak = 0;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < r.out.getNumSamples(); ++i) { const float v = r.out.getSample (c, i); ok = ok && std::isfinite (v); peak = std::max (peak, std::abs (v)); }
        check (ok && peak < 2.0f, "Pop Lead chain with every module on: finite and bounded (peak " + juce::String (peak, 2) + ")");
    }
    {
        KaminariVocalProcessor a;
        juce::MemoryBlock st;
        a.getStateInformation (st);
        KaminariVocalProcessor b;
        b.setStateInformation (st.getData(), (int) st.getSize());
        auto xml = juce::AudioProcessor::getXmlFromBinary (st.getData(), (int) st.getSize());
        check (b.loadedStateVersion == KaminariVocalProcessor::stateVersion && xml != nullptr && (int) xml->getIntAttribute ("engine_tune") == 1
               && ! xml->hasAttribute ("engine_reverb"), "sessions store a state version and per-module engine versions (this edition's modules only)");
    }

    {
        // CPU estimate: every module on, Resonance at Ultra quality (information only, no pass/fail)
        KaminariVocalProcessor p;
        p.presets.loadChainPreset ("Pop Lead");
        setParam (p, "rs_on", 1.0f); setParam (p, "rs_quality", 2.0f);
        prepare (p);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        render (p, 10.0, vocal);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("[INFO] full chain, 10 s of stereo audio at 48 kHz processed in %.0f ms (%.1f %% of one core on this machine)\n", ms, ms / 100.0);
    }

    // ---- persistence ----------------------------------------------------------------------------------------------
    {
        KaminariVocalProcessor a;
        const std::vector<std::pair<const char*, float>> values = {
            { "lv_peak", 63.0f }, { "lv_gain", 4.5f }, { "ds_det_lo", 6500.0f }, { "ds_range", 9.0f }, { "tn_speed", 22.0f },
            { "eq1_used", 1.0f }, { "eq1_freq", 820.0f }, { "lv_sc1_used", 1.0f }, { "lv_sc1_freq", 140.0f }, { "rs_depth", 7.0f },
            { kvid::inGain, -3.0f } };
        for (auto& [id, v] : values) setParam (a, id, v);
        a.advancedView.store (true);
        a.advancedTab.store (KaminariVocalProcessor::ModDeEss);
        juce::MemoryBlock state;
        a.getStateInformation (state);

        KaminariVocalProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());
        bool same = true;
        for (auto& [id, v] : values)
            if (std::abs (getParam (b, id) - getParam (a, id)) > 1e-3f) { same = false; std::printf ("  mismatch %s\n", id); }
        check (same, "module parameters survive a save / load round trip");
        check (b.advancedView.load() && b.advancedTab.load() == KaminariVocalProcessor::ModDeEss, "Basic/Advanced view and the open tab are saved with the session");

        // a session from the full edition (state version 4) opens on the matching tab, or on Tune for a tab this edition lacks
        auto fromFull = [&] (int tab, int version = 4)
        {
            auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
            xml->setAttribute ("state_version", version);
            xml->setAttribute ("ui_tab", tab);
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary (*xml, old);
            KaminariVocalProcessor c;
            c.setStateInformation (old.getData(), (int) old.getSize());
            return c.advancedTab.load();
        };
        check (fromFull (3) == KaminariVocalProcessor::ModCompression && fromFull (6) == KaminariVocalProcessor::ModDeEss
               && fromFull (7) == KaminariVocalProcessor::ModResonance && fromFull (5) == KaminariVocalProcessor::ModTune
               && fromFull (8) == KaminariVocalProcessor::ModTune && fromFull (4, 2) == KaminariVocalProcessor::ModDeEss
               && fromFull (5, 3) == KaminariVocalProcessor::ModDeEss && fromFull (7, 3) == KaminariVocalProcessor::ModTune,
               "full-edition sessions (any version): Compression, De-ess and Resonance tabs map across; Flanger, Distortion and Sends open on Tune");
    }

    // ---- presets --------------------------------------------------------------------------------------------------
    {
        KaminariVocalProcessor p;
        auto& pm = p.presets;
        const auto problems = pm.validateFactoryData();
        for (auto& pr : problems) std::printf ("  %s\n", pr.toRawUTF8());
        check (problems.isEmpty(), "factory preset data is valid (names, IDs, choice names and ranges)");
        check (pm.currentChainPreset() == "Default" && ! pm.isChainModified(), "a new instance starts on the Default chain preset");

        int moduleCount = 0, presetCount = 0;
        for (auto& m : pm.getModules()) { ++moduleCount; presetCount += (int) m.factory.size(); }
        check (moduleCount == 6 && presetCount >= 40 && pm.getFactoryChains().size() >= 12,
               "factory presets for all 6 modules (" + juce::String (presetCount) + " module presets) and "
               + juce::String ((int) pm.getFactoryChains().size()) + " chain presets");

        setParam (p, "lv_on", 0.0f);
        setParam (p, "lv_gain", 11.0f);
        setParam (p, "ds_range", 3.0f);
        check (pm.loadModulePreset ("compression", "Upfront Pop"), "load compression module preset 'Upfront Pop'");
        check (std::abs (getParam (p, "lv_peak") - 62.0f) < 0.05f && std::abs (getParam (p, "lv_gain") - 7.0f) < 0.01f,
               "module preset sets its listed values (Compression 62, Gain +7 dB)");
        check (getParam (p, "lv_on") < 0.5f, "module preset leaves the module's On switch alone");
        check (std::abs (getParam (p, "ds_range") - 3.0f) < 0.05f, "compression preset does not touch the de-esser");
        check (! pm.isModuleModified ("compression") && pm.currentModulePreset ("compression") == "Upfront Pop", "freshly loaded preset is not marked modified");
        setParam (p, "lv_peak", 20.0f);
        check (pm.isModuleModified ("compression"), "changing a compression control marks the compression preset modified");
        check (! pm.isModuleModified ("eq"), "untouched modules are not marked modified");

        check (pm.loadChainPreset ("Pop Lead"), "load chain preset 'Pop Lead'");
        check (std::abs (getParam (p, "lv_peak") - 62.0f) < 0.05f && std::abs (getParam (p, "ds_range") - 10.0f) < 0.05f
               && getParam (p, "lv_on") > 0.5f && getParam (p, "mb_on") > 0.5f,
               "chain preset loads each module preset and its overrides");
        check (pm.currentChainPreset() == "Pop Lead" && pm.currentModulePreset ("deess") == "Bright Singer"
               && pm.currentModulePreset ("tune") == "Tight Pop", "chain preset records the module presets it used");

        // every factory preset loads and plays without problems
        bool allOk = true;
        prepare (p);
        for (auto& m : pm.getModules())
            for (auto& pr : m.factory)
            {
                allOk = pm.loadModulePreset (m.key, pr) && allOk;
                for (auto* id : { "tn_on", "eq_on", "mb_on", "lv_on", "ds_on", "rs_on" }) setParam (p, id, 1.0f);
                const auto r = render (p, 0.2, vocal);
                for (int i = 0; i < r.out.getNumSamples(); ++i)
                    allOk = allOk && std::isfinite (r.out.getSample (0, i)) && std::abs (r.out.getSample (0, i)) < 4.0f;
            }
        check (allOk, "every module factory preset loads and plays (finite, bounded)");
        for (auto& c : pm.getFactoryChains())
            allOk = pm.loadChainPreset (c) && allOk;
        check (allOk, "every chain preset loads");

        // user presets
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kv-preset-test-" + juce::String (juce::Random::getSystemRandom().nextInt()));
        pm.setUserFolder (tmp);
        pm.loadModulePreset ("compression", "Smooth Leveler");
        setParam (p, "lv_gain", 7.5f);
        check (pm.saveModulePreset ("compression", "My Leveler / test"), "save a user compression preset");
        bool listed = false;
        for (auto& pr : pm.listModulePresets ("compression"))
            listed = listed || (! pr.factory && pr.name == PresetManager::sanitiseName ("My Leveler / test"));
        check (listed && tmp.getChildFile ("Compression").getNumberOfChildFiles (juce::File::findFiles) == 1,
               "user preset is listed after the factory presets and stored in the Compression folder");
        pm.loadModulePreset ("compression", "Gentle Glue");
        check (pm.loadModulePreset ("compression", PresetManager::sanitiseName ("My Leveler / test"))
               && std::abs (getParam (p, "lv_gain") - 7.5f) < 0.01f && std::abs (getParam (p, "lv_peak") - 50.0f) < 0.05f,
               "user preset restores its values (Compression 50, Gain +7.5 dB)");
        setParam (p, "ds_det_lo", 7700.0f);
        check (pm.saveChainPreset ("My Chain"), "save a user chain preset");
        pm.loadChainPreset ("Default");
        check (pm.loadChainPreset ("My Chain") && std::abs (getParam (p, "ds_det_lo") - 7700.0f) < 1.0f
               && std::abs (getParam (p, "lv_gain") - 7.5f) < 0.01f, "user chain preset restores every parameter");
        for (auto& pr : pm.listModulePresets ("compression"))
            if (! pr.factory) pm.deleteUserPreset (pr);
        check (tmp.getChildFile ("Compression").getNumberOfChildFiles (juce::File::findFiles) == 0, "user preset can be deleted");
        tmp.deleteRecursively();

        // stepping and session state
        pm.loadModulePreset ("compression", "Clean Vocal");
        pm.stepModulePreset ("compression", 1);
        const auto afterNext = pm.currentModulePreset ("compression");
        pm.stepModulePreset ("compression", -1);
        check (afterNext == "Smooth Leveler" && pm.currentModulePreset ("compression") == "Clean Vocal", "next / previous step through the list");
        pm.loadChainPreset ("R&B Smooth");
        juce::MemoryBlock state;
        p.getStateInformation (state);
        KaminariVocalProcessor q;
        q.setStateInformation (state.getData(), (int) state.getSize());
        check (q.presets.currentChainPreset() == "R&B Smooth" && q.presets.currentModulePreset ("compression") == "Smooth Leveler"
               && ! q.presets.isChainModified(), "preset names are saved with the session");
    }

    // ---- tune: vibrato and tremolo --------------------------------------------------------------------------------
    {
        // pitch of a steady tone in 25 ms frames (zero crossings), in cents relative to a reference
        auto pitchTrack = [] (const juce::AudioBuffer<float>& b, int from, int to, double ref)
        {
            std::vector<double> cents;
            const int frame = (int) (0.025 * sr);
            for (int i = from; i + frame <= to; i += frame / 2)
            {
                const double f = zeroCrossFreq (b, 0, i, i + frame);
                if (f > 0) cents.push_back (1200.0 * std::log2 (f / ref));
            }
            return cents;
        };
        const double in = 440.0 * std::pow (2.0, 0.30 / 12.0);   // A4 + 30 cents

        KaminariVocalProcessor p;
        neutral (p);
        setParam (p, "tn_on", 1.0f);
        setParam (p, "tn_correct", 0.0f);
        prepare (p);
        auto r = render (p, 1.0, sine (in, -12.0f));
        const double fOff = zeroCrossFreq (r.out, 0, 24000, 48000);
        check (std::abs (1200.0 * std::log2 (fOff / in)) < 3.0, "Correct pitch off: the tone keeps its own pitch ("
               + juce::String (1200.0 * std::log2 (fOff / 440.0), 1) + " cents from A4)");

        // Detune with Correct Pitch off: sung notes are shifted by the Detune amount
        setParam (p, "tn_detune", -30.0f);
        prepare (p);
        r = render (p, 1.0, sine (in, -12.0f));
        const double fDet = zeroCrossFreq (r.out, 0, 24000, 48000);
        check (std::abs (1200.0 * std::log2 (fDet / in) + 30.0) < 3.0, "Detune -30 cents with Correct Pitch off shifts the tone by "
               + juce::String (1200.0 * std::log2 (fDet / in), 1) + " cents");
        // Detune with Correct Pitch on: the note grid moves (A4 +40 cents is pulled to A4 +50 cents, not to A4)
        setParam (p, "tn_correct", 1.0f);
        setParam (p, "tn_speed", 0.0f);
        setParam (p, "tn_humanize", 0.0f);
        setParam (p, "tn_detune", 50.0f);
        prepare (p);
        const double in40 = 440.0 * std::pow (2.0, 0.40 / 12.0);
        r = render (p, 1.0, sine (in40, -12.0f));
        const double fGrid = zeroCrossFreq (r.out, 0, 24000, 48000);
        check (std::abs (1200.0 * std::log2 (fGrid / 440.0) - 50.0) < 5.0, "Detune +50 cents moves the grid: A4 +40 cents corrects to "
               + juce::String (1200.0 * std::log2 (fGrid / 440.0), 1) + " cents (expected +50)");
        setParam (p, "tn_detune", 0.0f);
        setParam (p, "tn_correct", 0.0f);

        setParam (p, "tn_vib_on", 1.0f);
        setParam (p, "tn_vib_depth", 50.0f);
        setParam (p, "tn_vib_rate", 5.0f);
        setParam (p, "tn_vib_delay", 200.0f);
        setParam (p, "tn_vib_rise", 0.0f);
        setParam (p, "tn_vib_variation", 0.0f);
        prepare (p);
        r = render (p, 2.0, sine (in, -12.0f));
        const auto early = pitchTrack (r.out, (int) (0.06 * sr), (int) (0.16 * sr), in);
        const auto late = pitchTrack (r.out, (int) (0.6 * sr), (int) (1.8 * sr), in);
        double earlySpan = 0, lo = 1e9, hi = -1e9;
        for (double c : early) earlySpan = std::max (earlySpan, std::abs (c));
        for (double c : late) { lo = std::min (lo, c); hi = std::max (hi, c); }
        check (earlySpan < 8.0, "vibrato waits for its onset delay (pitch within " + juce::String (earlySpan, 1) + " cents in the first 160 ms)");
        check (hi - lo > 60.0 && hi - lo < 130.0, "vibrato depth 50 cents swings the pitch by about 100 cents peak to peak ("
               + juce::String (hi - lo, 0) + ")");

        // tremolo: level dips by the depth at the tremolo rate
        KaminariVocalProcessor t;
        neutral (t);
        setParam (t, "tn_on", 1.0f);
        setParam (t, "tn_correct", 0.0f);
        setParam (t, "tn_trem_on", 1.0f);
        setParam (t, "tn_trem_depth", 50.0f);
        setParam (t, "tn_trem_rate", 4.0f);
        prepare (t);
        r = render (t, 1.5, sine (300.0, -12.0f));
        auto frameRms = [] (const juce::AudioBuffer<float>& b, int ch)
        {
            std::vector<float> v;
            const int frame = (int) (0.01 * sr);
            for (int i = (int) (0.2 * sr); i + frame <= b.getNumSamples(); i += frame)
            {
                double e = 0;
                for (int k = 0; k < frame; ++k) e += (double) b.getSample (ch, i + k) * b.getSample (ch, i + k);
                v.push_back ((float) std::sqrt (e / frame));
            }
            return v;
        };
        auto env = frameRms (r.out, 0);
        const float mx = *std::max_element (env.begin(), env.end()), mn = *std::min_element (env.begin(), env.end());
        check (std::abs (juce::Decibels::gainToDecibels (mn / mx) + 6.0f) < 1.5f, "tremolo depth 50 %: level dips by about 6 dB ("
               + juce::String (juce::Decibels::gainToDecibels (mn / mx), 1) + " dB)");

        setParam (t, "tn_trem_stereo", 180.0f);
        setParam (t, "tn_trem_depth", 100.0f);
        prepare (t);
        r = render (t, 1.5, sine (300.0, -12.0f));
        const auto envL = frameRms (r.out, 0), envR = frameRms (r.out, 1);
        std::vector<float> dl, dr;
        const float ml = std::accumulate (envL.begin(), envL.end(), 0.0f) / (float) envL.size();
        const float mr = std::accumulate (envR.begin(), envR.end(), 0.0f) / (float) envR.size();
        for (size_t i = 0; i < envL.size(); ++i) { dl.push_back (envL[i] - ml); dr.push_back (envR[i] - mr); }
        check (correlation (dl, dr) < -0.8, "tremolo stereo phase 180: left and right move in opposition (auto-pan), correlation "
               + juce::String (correlation (dl, dr), 2));

        neutral (t);
        prepare (t);
        r = render (t, 0.5, sine (in, -12.0f));   // a tone that never lands exactly on zero (the chain flushes values below 1e-15)
        check (maxDiff (r) == 0.0f, "Tune off: tremolo and vibrato settings have no effect");
    }

    // ---- Compression side-chain detection bands ----------------------------------------------------------------------
    {
        auto compLevel = [] (std::function<void (KaminariVocalProcessor&)> sc)
        {
            KaminariVocalProcessor p;
            neutral (p);
            setParam (p, "lv_on", 1.0f);
            setParam (p, "lv_peak", 80.0f); setParam (p, "lv_gain", 0.0f);
            if (sc) sc (p);
            prepare (p);
            const auto r = render (p, 1.0, sine (100.0, -12.0f));
            return toneDb (r.out, 0, 100.0, 24000, 48000);
        };
        const float plainComp = compLevel (nullptr);
        const float lowCut = compLevel ([] (KaminariVocalProcessor& p)
        {
            setParam (p, "lv_sc1_used", 1.0f); setParam (p, "lv_sc1_type", (float) kv::LowCut); setParam (p, "lv_sc1_freq", 2000.0f);
        });
        const float cutOff = compLevel ([] (KaminariVocalProcessor& p)
        {
            setParam (p, "lv_sc1_used", 1.0f); setParam (p, "lv_sc1_type", (float) kv::LowCut); setParam (p, "lv_sc1_freq", 2000.0f); setParam (p, "lv_sc1_on", 0.0f);
        });
        const float boost = compLevel ([] (KaminariVocalProcessor& p)
        {
            setParam (p, "lv_sc2_used", 1.0f); setParam (p, "lv_sc2_type", 0.0f); setParam (p, "lv_sc2_freq", 100.0f); setParam (p, "lv_sc2_gain", 12.0f);
        });
        check (plainComp < -20.0f && lowCut > plainComp + 10.0f && std::abs (cutOff - plainComp) < 0.2f && boost < plainComp - 2.0f,
               "Compression side chain: 100 Hz tone " + juce::String (plainComp, 1) + " dBFS; with a 2 kHz low cut in the detector "
               + juce::String (lowCut, 1) + " dBFS (detector no longer hears it); band switched off " + juce::String (cutOff, 1)
               + " dBFS; +12 dB bell at 100 Hz " + juce::String (boost, 1) + " dBFS (more reduction)");
    }

    // ---- oversampling (Multiband, Resonance) and Multiband bypass / mute -----------------------------------
    {
        struct Case { const char* on; const char* os; float factor; const char* name; };
        for (auto c : { Case { "mb_on", "mb_os", 1.0f, "Multiband 2x" }, Case { "rs_on", "rs_os", 2.0f, "Resonance 4x" } })
        {
            KaminariVocalProcessor p;
            neutral (p);
            setParam (p, c.on, 1.0f);
            setParam (p, "rs_depth", 0.0f);   // a pure tone is a resonance: with depth 0 Resonance leaves it alone
            prepare (p);
            const int base = p.getLatencySamples();
            setParam (p, c.os, c.factor);
            prepare (p);
            const int lat = p.getLatencySamples();
            const auto r = render (p, 1.0, sine (1000.0, -18.0f));
            bool finite = true;
            for (int i = 0; i < r.out.getNumSamples(); ++i) finite = finite && std::isfinite (r.out.getSample (0, i));
            const float level = toneDb (r.out, 0, 1000.0, 12000, 48000);
            // aligned: the output is the input (as processed) at the reported latency, so it matches the input closely
            double err = 0, ref = 0;
            for (int i = 12000; i < 48000; ++i) { const double d = r.out.getSample (0, i) - r.in.getSample (0, i); err += d * d; ref += (double) r.in.getSample (0, i) * r.in.getSample (0, i); }
            const double alignDb = 10.0 * std::log10 (std::max (1e-30, err / ref));
            check (finite && lat > base && std::abs (level + 18.0f) < 1.5f && alignDb < -20.0,
                   juce::String (c.name) + " oversampling: +" + juce::String (lat - base) + " samples latency, tone level " + juce::String (level, 1)
                   + " dBFS, output aligned with the reported latency (residual " + juce::String (alignDb, 1) + " dB)");
        }

        auto mbLevel = [] (const char* flag)
        {
            KaminariVocalProcessor p;
            neutral (p);
            setParam (p, "mb_on", 1.0f);
            setParam (p, "mb_count", 1.0f);
            setParam (p, "mb1_lo", 100.0f); setParam (p, "mb1_hi", 400.0f);
            setParam (p, "mb1_thresh", -50.0f); setParam (p, "mb1_ratio", 10.0f); setParam (p, "mb1_range", -24.0f);
            if (flag != nullptr) setParam (p, flag, 1.0f);
            prepare (p);
            const auto r = render (p, 1.0, sine (200.0, -12.0f));
            return toneDb (r.out, 0, 200.0, 24000, 48000);
        };
        const float comp = mbLevel (nullptr), byp = mbLevel ("mb1_bypass"), mute = mbLevel ("mb1_mute");
        check (comp < -20.0f && std::abs (byp + 12.0f) < 0.5f && mute < -30.0f, "Multiband band: compressed " + juce::String (comp, 1)
               + " dBFS, Bypass " + juce::String (byp, 1) + " dBFS (unchanged), Mute " + juce::String (mute, 1) + " dBFS (removed)");
    }

    // ---- EQ solo: auditions what each band type works on --------------------------------------------------------------
    {
        auto soloLevels = [] (int type)
        {
            KaminariVocalProcessor p;
            neutral (p);
            setParam (p, "eq_on", 1.0f);
            setParam (p, "eq1_used", 1.0f);
            setParam (p, "eq1_type", (float) type);
            setParam (p, "eq1_freq", 1000.0f);
            setParam (p, "eq1_gain", 0.0f);
            prepare (p);
            p.eqSolo.store (0);
            const auto lo = render (p, 0.5, sine (150.0, -12.0f)), hi = render (p, 0.5, sine (6000.0, -12.0f));
            return std::pair { toneDb (lo.out, 0, 150.0, 4800, 24000), toneDb (hi.out, 0, 6000.0, 4800, 24000) };
        };
        const auto cut = soloLevels (kv::LowCut), shelf = soloLevels (kv::HighShelf), bell = soloLevels (kv::Bell);
        check (cut.first > -14.0f && cut.second < -40.0f, "EQ solo on a 1 kHz low cut plays the lows it removes (150 Hz "
               + juce::String (cut.first, 1) + " dB, 6 kHz " + juce::String (cut.second, 1) + " dB)");
        check (shelf.second > -14.0f && shelf.first < -40.0f, "EQ solo on a 1 kHz high shelf plays the highs (6 kHz "
               + juce::String (shelf.second, 1) + " dB, 150 Hz " + juce::String (shelf.first, 1) + " dB)");
        check (bell.first < -25.0f && bell.second < -25.0f, "EQ solo on a 1 kHz bell plays only the region around 1 kHz");
    }

    // ---- editor ---------------------------------------------------------------------------------------------------
    {
        KaminariVocalProcessor p;
        prepare (p);
        std::unique_ptr<KaminariVocalEditor> ed (dynamic_cast<KaminariVocalEditor*> (p.createEditor()));
        check (ed != nullptr, "editor opens");
        check (ed->tile (0).isVisible() && ! ed->panel (KaminariVocalEditor::TabCompression).isVisible(), "Basic view shows the module cards");
        auto* makeup = ed->tile (2).makeupSlider();
        check (makeup != nullptr && makeup->isVisible() && makeup->getParam() == p.apvts.getParameter ("lv_gain")
               && makeup->getRight() <= ed->tile (2).getWidth() && ed->tile (2).hammer.getRight() <= makeup->getX() + 2,
               "Basic Compression card: a makeup Gain slider beside the hammer");
        check (ed->tile (0).makeupSlider() == nullptr && ed->tile (3).makeupSlider() == nullptr, "only the Compression card has the Gain slider");

        ed->tile (2).open.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        check (ed->isAdvancedShown() && ed->panel (KaminariVocalEditor::TabCompression).isVisible() && ! ed->tile (2).isVisible(),
               "a card's Advanced button opens that module's tab");

        p.presets.loadChainPreset ("Pop Lead");
        ed->chainPresets().onLoaded();
        check (ed->chainPresets().displayText() == "Pop Lead" && ed->compressionPanel().preset.displayText() == "Upfront Pop",
               "header shows the chain preset; the compression page shows its module preset");
        setParam (p, "lv_peak", 33.0f);
        check (ed->compressionPanel().preset.displayText() == "Upfront Pop *" && ed->chainPresets().displayText() == "Pop Lead *",
               "editing a control marks the module and chain presets modified");
        check (ed->compressionPanel().compressionKnob().slider.getTitle() == "Compression" && ed->deEssPanel().frequencyKnob().slider.getTitle() == "De-Ess Frequency"
               && ed->compressionPanel().gainKnob().slider.getTooltip().contains ("akeup"),
               "controls carry accessible names and tooltips");
        check (KaminariVocalEditor::numTabs == 6, "six module tabs: Tune, EQ, Multiband, Compression, De-ess, Resonance");

        // every continuous control on every page responds to a mouse drag the way a user would make it:
        // up for knobs and value boxes, sideways for horizontal sliders, down for the Basic hammers
        {
            auto event = [] (juce::Component& c, juce::Point<float> pos, juce::Point<float> down, bool dragged)
            {
                return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos,
                                         juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), juce::MouseInputSource::defaultPressure,
                                         0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
            };
            auto drag = [&] (juce::Component& c, juce::Point<float> delta)
            {
                const auto start = c.getLocalBounds().getCentre().toFloat();
                c.mouseDown (event (c, start, start, false));
                for (int i = 1; i <= 8; ++i) c.mouseDrag (event (c, start + delta * ((float) i / 8.0f), start, true));
                c.mouseUp (event (c, start + delta, start, true));
            };
            juce::StringArray dead;
            int tested = 0;
            std::function<void (juce::Component&, const juce::String&)> visit = [&] (juce::Component& c, const juce::String& where)
            {
                if (! c.isVisible() || c.getWidth() <= 0 || ! c.isEnabled()) return;
                juce::RangedAudioParameter* prm = nullptr;
                juce::Point<float> delta;
                if (auto* s = dynamic_cast<ParamSlider*> (&c))
                {
                    prm = s->getParam();
                    delta = s->isHorizontal() ? juce::Point<float> (60.0f, 0.0f) : juce::Point<float> (0.0f, -60.0f);
                }
                else if (auto* vb = dynamic_cast<kvui::ValueBox*> (&c))
                {
                    prm = vb->getParam();
                    delta = { 0.0f, -60.0f };
                }
                if (prm != nullptr && dynamic_cast<juce::AudioParameterFloat*> (prm) != nullptr)
                {
                    ++tested;
                    prm->setValueNotifyingHost (0.4f);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
                    const float before = prm->getValue();
                    drag (c, delta);
                    if (std::abs (prm->getValue() - before) < 0.02f)
                        dead.addIfNotAlreadyThere (where + ": " + prm->getName (64));
                    prm->setValueNotifyingHost (prm->getDefaultValue());
                    return;
                }
                for (auto* child : c.getChildren())
                    visit (*child, where);
            };
            const char* tabNamesT[] = { "Tune", "EQ", "Multiband", "Compression", "De-ess", "Resonance" };
            for (int t = 0; t < KaminariVocalEditor::numTabs; ++t)
            {
                ed->showTab (true, t);
                visit (ed->panel (t), tabNamesT[t]);
            }
            ed->showTab (false, 0);
            visit (ed->tile (2), "Basic Compression card");
            for (auto& line : dead) std::printf ("  no response: %s\n", line.toRawUTF8());
            // EQ: clicking empty graph space creates a band whose type follows the frequency
            {
                ed->showTab (true, KaminariVocalEditor::TabEq);
                for (int i = 1; i <= 8; ++i) setParam (p, ("eq" + juce::String (i) + "_used").toRawUTF8(), 0.0f);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                auto& curve = ed->eqPage().curve;
                const std::pair<double, int> clicks[] = { { 40.0, kv::LowCut }, { 100.0, kv::LowShelf }, { 1000.0, kv::Bell },
                                                          { 10000.0, kv::HighShelf }, { 18000.0, kv::HighCut } };
                bool typesOk = true;
                int bandNo = 1;
                for (auto [f, type] : clicks)
                {
                    const juce::Point<float> at (curve.xForFreq (f), curve.yForDb (3.0));
                    curve.mouseDown (event (curve, at, at, false));
                    curve.mouseUp (event (curve, at, at, false));
                    curve.mouseDoubleClick (event (curve, at, at, false));
                    const juce::String pre = "eq" + juce::String (bandNo++) + "_";
                    typesOk = typesOk && getParam (p, (pre + "used").toRawUTF8()) > 0.5f
                                      && juce::roundToInt (getParam (p, (pre + "type").toRawUTF8())) == type
                                      && std::abs (getParam (p, (pre + "freq").toRawUTF8()) / (float) f - 1.0f) < 0.03f;
                }
                check (typesOk, "EQ double-click-to-create: 40 Hz low cut, 100 Hz low shelf, 1 kHz bell, 10 kHz high shelf, 18 kHz high cut");
                {
                    // a single click on empty graph space adds nothing and closes the band settings
                    const juce::Point<float> empty (curve.xForFreq (3000.0), curve.yForDb (-12.0));
                    curve.mouseDown (event (curve, empty, empty, false));
                    curve.mouseUp (event (curve, empty, empty, false));
                    check (getParam (p, "eq6_used") < 0.5f && curve.selected == -1 && ! ed->eqPage().editor.panel.isVisible(),
                           "EQ: a single click on empty space adds no band and hides the band settings");
                }
                // the keyboard sweeps the selected band to the note under the mouse
                auto& piano = ed->eqPage().piano;
                ed->eqPage().select (2);
                const juce::Point<float> a4 (curve.xForFreq (440.0), (float) piano.getHeight() - 6.0f);
                piano.mouseDown (event (piano, a4, a4, false));
                const juce::Point<float> c5 (curve.xForFreq (523.25), (float) piano.getHeight() - 6.0f);
                piano.mouseDrag (event (piano, c5, a4, true));
                piano.mouseUp (event (piano, c5, a4, true));
                check (std::abs (getParam (p, "eq3_freq") - 523.25f) < 0.5f, "EQ keyboard: dragging from A4 to C5 sweeps the selected band to "
                       + juce::String (getParam (p, "eq3_freq"), 1) + " Hz (C5 = 523.3 Hz)");
                for (int i = 1; i <= 8; ++i) setParam (p, ("eq" + juce::String (i) + "_used").toRawUTF8(), 0.0f);
            }
            // Multiband: clicking empty display space adds a band around the click
            {
                ed->showTab (true, KaminariVocalEditor::TabMultiband);
                setParam (p, "mb_count", 1.0f);
                setParam (p, "mb1_lo", 100.0f); setParam (p, "mb1_hi", 400.0f);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                auto& disp = ed->multibandPanel().display;
                const juce::Point<float> at (disp.xFor (3000.0), 60.0f);
                disp.mouseDown (event (disp, at, at, false));
                disp.mouseUp (event (disp, at, at, false));
                const float lo = getParam (p, "mb2_lo"), hi = getParam (p, "mb2_hi");
                check (juce::roundToInt (getParam (p, "mb_count")) == 2 && lo < 3000.0f && hi > 3000.0f && hi / lo < 2.2f,
                       "Multiband: clicking the display at 3 kHz adds band 2 (" + juce::String (lo, 0) + " - " + juce::String (hi, 0) + " Hz)");
                setParam (p, "mb_count", 1.0f);
            }
            // Compression side chain: the same EQ editor as the main EQ; clicks create bands typed by frequency, a drag
            // moves one, and the selected node crackles with lightning
            {
                ed->showTab (true, KaminariVocalEditor::TabCompression);
                ed->compressionPanel().showSideChain (true);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                auto& sc = ed->compressionPanel().sideChainEq().curve;
                bool typesOk = sc.getWidth() > 200 && sc.isVisible();
                int k = 1;
                for (auto [f, type] : { std::pair<double, int> { 40.0, kv::LowCut }, { 3000.0, kv::Bell }, { 18000.0, kv::HighCut } })
                {
                    const juce::Point<float> at (sc.xForFreq (f), sc.yForDb (4.0));
                    sc.mouseDown (event (sc, at, at, false));
                    sc.mouseUp (event (sc, at, at, false));
                    sc.mouseDoubleClick (event (sc, at, at, false));
                    const juce::String pre = "lv_sc" + juce::String (k) + "_";
                    typesOk = typesOk && getParam (p, (pre + "used").toRawUTF8()) > 0.5f && juce::roundToInt (getParam (p, (pre + "type").toRawUTF8())) == type
                              && std::abs (std::log2 (getParam (p, (pre + "freq").toRawUTF8()) / f)) < 0.05 && sc.selected == k - 1;
                    ++k;
                }
                const bool bellGain = getParam (p, "lv_sc2_gain") > 2.0f;
                kv::EqBandSettings bands[8];
                p.readEqBands ("lv_sc", bands);
                const auto n2 = sc.nodePos (bands[1]);
                const juce::Point<float> to (sc.xForFreq (5000.0), sc.yForDb (-6.0));
                sc.mouseDown (event (sc, n2, n2, false));
                sc.mouseDrag (event (sc, to, n2, true));
                sc.mouseUp (event (sc, to, n2, true));
                const bool dragged = std::abs (getParam (p, "lv_sc2_freq") - 5000.0f) < 100.0f && std::abs (getParam (p, "lv_sc2_gain") + 6.0f) < 0.3f;
                juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
                const int sparks = sc.nodeBolts();
                check (typesOk && bellGain && dragged && sparks > 0,
                       "Compression side-chain EQ (main EQ editor): double-clicks create a 40 Hz low cut, a 3 kHz bell and an 18 kHz high cut; dragging band 2 sets "
                       + juce::String (getParam (p, "lv_sc2_freq"), 0) + " Hz / " + juce::String (getParam (p, "lv_sc2_gain"), 1) + " dB; "
                       + juce::String (sparks) + " bolts around the selected node");
                for (int i = 1; i <= 8; ++i) setParam (p, ("lv_sc" + juce::String (i) + "_used").toRawUTF8(), 0.0f);
                ed->compressionPanel().showSideChain (false);
            }
            // Basic tuning view: detected and target note, deviation and the span being corrected
            {
                kvui::TuneRangeView view (p);
                view.update (57.30f, -30.0f);   // A3 +30 ct, pulled down 30 ct
                const bool sharp = view.targetNote() == 57 && std::abs (view.deviationCents() - 30.0f) < 0.01f;
                view.update (58.60f, 40.0f);    // A#3 +60 ct = B3 -40 ct, pulled up 40 ct
                const bool flat = view.targetNote() == 59 && std::abs (view.deviationCents() + 40.0f) < 0.01f;
                view.update (60.02f, -2.0f);
                const bool centred = view.targetNote() == 60 && std::abs (view.deviationCents()) < kvui::TuneRangeView::inTuneCents;
                view.update (-1.0f, 0.0f);
                check (sharp && flat && centred && ! view.hasPitch(), "Basic tuning view: A3 +30 ct reads 30 ct sharp of A3; A#3 +60 ct reads "
                       "40 ct flat of B3; C4 +2 ct reads in tune; no pitch reads none");
            }
            // tab lightning icons: a click switches the module on or off and leaves the page as it is
            {
                ed->showTab (true, KaminariVocalEditor::TabEq);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                auto& tabB = ed->tabButton (KaminariVocalEditor::TabDeEss);
                juce::Component& tab = tabB;
                const float before = getParam (p, "ds_on");
                const juce::Point<float> bolt (14.0f, (float) tab.getHeight() * 0.5f);
                tab.mouseDown (event (tab, bolt, bolt, false));
                tab.mouseUp (event (tab, bolt, bolt, false));
                const float after = getParam (p, "ds_on");
                tab.mouseDown (event (tab, bolt, bolt, false));
                tab.mouseUp (event (tab, bolt, bolt, false));
                const bool pageKept = ed->tabButton (KaminariVocalEditor::TabEq).getToggleState() && ! tabB.getToggleState();
                check ((before > 0.5f) != (after > 0.5f) && std::abs (getParam (p, "ds_on") - before) < 0.01f && pageKept,
                       "tab lightning icon: a click switches De-ess " + juce::String (after > 0.5f ? "on" : "off")
                       + ", a second click back; the EQ page stays shown");
            }
            // EQ band panel: note and cents beside the frequency only while the keyboard is shown; slopes read dB/oct
            {
                auto& edq = ed->eqPage().editor;
                setParam (p, "eq1_used", 1.0f);
                edq.select (0);
                const bool withPiano = edq.panel.freq->value.getText().contains ("ct");
                edq.pianoToggle.setToggleState (false, juce::sendNotificationSync);
                const bool withoutPiano = ! edq.panel.freq->value.getText().contains ("ct");
                edq.pianoToggle.setToggleState (true, juce::sendNotificationSync);
                const bool unit = p.apvts.getParameter ("eq1_slope")->getCurrentValueAsText().endsWith ("dB/oct");
                check (withPiano && withoutPiano && unit, "EQ band panel: note and cents shown only with the keyboard on; slope reads "
                       + p.apvts.getParameter ("eq1_slope")->getCurrentValueAsText());
            }
            // displays on hidden pages do no work; the visible one updates
            {
                ed->showTab (true, KaminariVocalEditor::TabEq);
                auto& deessView = ed->deEssPanel().historyDisplay();
                const int before = deessView.updates();
                render (p, 0.3, vocal);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
                const int hidden = deessView.updates() - before;
                ed->showTab (true, KaminariVocalEditor::TabDeEss);
                render (p, 0.3, vocal);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
                const int shown = deessView.updates() - before - hidden;
                check (hidden == 0 && shown > 2, "the De-ess display skips its updates while its page is hidden (" + juce::String (hidden)
                       + " in 120 ms) and updates once shown (" + juce::String (shown) + " in 120 ms)");
            }
            // EQ graph: the curve follows a parameter change on the next frame (cached curves are rebuilt when a band changes)
            {
                ed->showTab (false, 0);
                auto& curve = ed->eqSection().curve;
                setParam (p, "eq1_used", 1.0f); setParam (p, "eq1_type", 0.0f); setParam (p, "eq1_freq", 1000.0f); setParam (p, "eq1_gain", 0.0f);
                auto curveY = [&]
                {
                    auto img = curve.createComponentSnapshot (curve.getLocalBounds());
                    const int x = juce::roundToInt (curve.xForFreq (1000.0)) - 16;   // beside the node, on the curve
                    for (int y = 0; y < img.getHeight(); ++y)
                        if (img.getPixelAt (x, y).getBrightness() > 0.85f) return y;
                    return -1;
                };
                const int flat = curveY();
                setParam (p, "eq1_gain", 12.0f);
                const int boosted = curveY();
                check (flat > 0 && boosted > 0 && boosted < flat - 20, "EQ graph redraws the curve right after a gain change (curve at y "
                       + juce::String (flat) + ", then " + juce::String (boosted) + ")");
                setParam (p, "eq1_used", 0.0f);
            }
            check (dead.isEmpty() && tested > 40, "every knob, slider and value box responds to a drag (" + juce::String (tested) + " tested, "
                   + juce::String (dead.size()) + " not responding)");
        }

        if (argc > 1)
        {
            juce::File dir (argv[1]);
            dir.createDirectory();
            auto save = [&] (const juce::String& name)
            {
                auto img = ed->createComponentSnapshot (ed->getLocalBounds());
                juce::FileOutputStream out (dir.getChildFile (name));
                out.setPosition (0);
                out.truncate();
                juce::PNGImageFormat().writeImageToStream (img, out);
            };
            p.presets.loadChainPreset ("Pop Lead");
            ed->chainPresets().onLoaded();
            setParam (p, "rs_on", 1.0f);
            render (p, 1.5, vocal);   // feed audio so meters, analyzer and read-outs show something
            juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
            ed->showTab (false, 0);
            for (int k = 0; k < 6; ++k)
            {
                render (p, 0.1, vocal);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
            }
            save ("basic.png");
            const char* names[] = { "tune", "eq", "multiband", "compression", "deess", "resonance" };
            for (int t = 0; t < KaminariVocalEditor::numTabs; ++t)
            {
                ed->showTab (true, t);
                for (int k = 0; k < 4; ++k) { render (p, 0.1, vocal); juce::MessageManager::getInstance()->runDispatchLoopUntil (40); }
                save (juce::String ("adv_") + names[t] + ".png");
            }
            // Multiband with three bands, one of them working, styled like the EQ
            setParam (p, "mb_on", 1.0f); setParam (p, "mb_count", 3.0f);
            setParam (p, "mb1_lo", 120.0f); setParam (p, "mb1_hi", 450.0f); setParam (p, "mb1_thresh", -40.0f);
            setParam (p, "mb2_lo", 450.0f); setParam (p, "mb2_hi", 2500.0f);
            setParam (p, "mb3_lo", 4000.0f); setParam (p, "mb3_hi", 12000.0f); setParam (p, "mb3_gain", 3.0f);
            ed->showTab (true, KaminariVocalEditor::TabMultiband);
            for (int k = 0; k < 4; ++k) { render (p, 0.1, vocal); juce::MessageManager::getInstance()->runDispatchLoopUntil (40); }
            save ("adv_multiband_bands.png");
            // hammer: nothing at rest; lightning and particles past the activation point, more as the hammer fills
            {
                auto activity = [&] (float fill)
                {
                    setParam (p, "mb1_thresh", -50.0f * fill);
                    LightningSlider h (*p.apvts.getParameter ("mb1_thresh"), p.hostTempo, "Threshold", true);
                    h.setCompact (true);
                    h.setBounds (0, 0, 150, 180);
                    double bolts = 0, parts = 0;
                    for (int k = 0; k < 200; ++k) { h.advanceAnimation (16.0); bolts += h.activeBolts(); parts += h.activeParticles(); }
                    return std::make_tuple (h.intensity(), bolts / 200.0, parts / 200.0);
                };
                const auto [i0, b0, p0] = activity (0.05f);
                const auto [i1, b1, p1] = activity (0.4f);
                const auto [i2, b2, p2] = activity (1.0f);
                check (i0 <= 0.0f && b0 <= 0.0 && p0 <= 0.0 && b1 > 0.5 && b2 > 1.5 * b1 && p2 > p1 && i2 > i1,
                       "hammer: at rest no lightning or particles; at 40 % " + juce::String (b1, 1) + " bolts and " + juce::String (p1, 0)
                       + " particles on average, full " + juce::String (b2, 1) + " bolts and " + juce::String (p2, 0) + " particles");
            }
            {
                // the ANIM switch: off stops the hammer lightning and is saved with the session
                setParam (p, "mb1_thresh", -50.0f);
                LightningSlider h (*p.apvts.getParameter ("mb1_thresh"), p.hostTempo, "Threshold", true);
                h.setBounds (0, 0, 150, 180);
                p.hostTempo.animations.store (false);
                double now = juce::Time::getMillisecondCounterHiRes();
                int bolts = 0;
                for (int k = 0; k < 30; ++k) { now += 16.7; h.updateGlow (now); bolts += h.activeBolts() + h.activeParticles(); }
                juce::MemoryBlock st;
                p.getStateInformation (st);
                KaminariVocalProcessor q;
                q.setStateInformation (st.getData(), (int) st.getSize());
                check (bolts == 0 && ! q.hostTempo.animations.load(), "Animations off: no hammer lightning, and the setting is saved with the session");
                p.hostTempo.animations.store (true);
            }
            // hammer close-up at rest, just past activation, half and full
            {
                juce::Image sheet (juce::Image::ARGB, 4 * 300, 360, true);
                juce::Graphics sg (sheet);
                sg.fillAll (juce::Colour (0xff0b1a33));
                const float levels[] = { 0.05f, 0.3f, 0.65f, 1.0f };
                for (int k = 0; k < 4; ++k)
                {
                    setParam (p, "mb1_thresh", -50.0f * levels[k]);   // threshold: inverted, so a fuller hammer is a lower threshold
                    LightningSlider h (*p.apvts.getParameter ("mb1_thresh"), p.hostTempo, "Threshold", true);
                    h.setCompact (true);
                    h.setBounds (0, 0, 150, 180);
                    h.advanceAnimation (700.0);
                    auto img = h.createComponentSnapshot (h.getLocalBounds(), true, 2.0f);
                    sg.drawImageAt (img, k * 300, 0);
                }
                juce::FileOutputStream o (dir.getChildFile ("hammer_levels.png"));
                o.setPosition (0); o.truncate();
                juce::PNGImageFormat().writeImageToStream (sheet, o);
            }
            // De-ess working on bursts of hiss
            {
                setParam (p, "ds_on", 1.0f); setParam (p, "ds_det_lo", 5000.0f); setParam (p, "ds_range", 12.0f);
                ed->showTab (true, KaminariVocalEditor::TabDeEss);
                juce::Random rnd (7);
                auto sibilant = [&rnd] (int, long n)
                {
                    // syllables (smooth swells and gaps) with an "s" at the end of each word
                    const double t = (double) n / sr;
                    const double w = std::fmod (t, 0.8);
                    const float syll = w < 0.5 ? (float) std::pow (std::sin (kv::pi * w / 0.5), 0.7) * (0.6f + 0.4f * (float) std::sin (kv::pi * 2.0 * t * 3.1))
                                               : 0.0f;
                    const float voice = 0.3f * std::abs (syll) * (float) (std::sin (2.0 * kv::pi * 220.0 * t) + 0.4 * std::sin (2.0 * kv::pi * 440.0 * t));
                    const bool ess = w > 0.5 && w < 0.68;
                    const float hiss = ess ? 0.22f * (float) std::sin (kv::pi * (w - 0.5) / 0.18) * (rnd.nextFloat() * 2.0f - 1.0f) : 0.0f;
                    return voice + hiss;
                };
                float maxGr = 0;
                for (int k = 0; k < 12; ++k)
                {
                    render (p, 0.5, sibilant, [&] (int) { maxGr = std::max (maxGr, p.moduleGr[KaminariVocalProcessor::ModDeEss].load()); });
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                }
                save ("adv_deess_active.png");
                std::array<kv::LevelHistory<KaminariVocalProcessor::deessHistorySize>::Entry, (size_t) KaminariVocalProcessor::deessHistorySize> h;
                p.deessHistory.read (h);
                float shown = 0;
                for (auto& e : h) shown = std::max (shown, e.gr);
                check (maxGr > 3.0f && shown > 0.5f * maxGr, "De-ess display shows the de-esser's own reduction on hiss bursts (up to "
                       + juce::String (shown, 1) + " dB; the de-esser reached " + juce::String (maxGr, 1) + " dB)");
            }
            // compression side-chain EQ view
            setParam (p, "lv_sc1_used", 1.0f); setParam (p, "lv_sc1_type", (float) kv::LowCut); setParam (p, "lv_sc1_freq", 150.0f);
            setParam (p, "lv_sc2_used", 1.0f); setParam (p, "lv_sc2_freq", 5500.0f); setParam (p, "lv_sc2_gain", 6.0f);
            ed->showTab (true, KaminariVocalEditor::TabCompression);
            ed->compressionPanel().showSideChain (true);
            ed->compressionPanel().sideChainEq().select (1);
            for (int k = 0; k < 4; ++k) { render (p, 0.1, vocal); juce::MessageManager::getInstance()->runDispatchLoopUntil (40); }
            save ("adv_compression_sc.png");
            ed->compressionPanel().showSideChain (false);
        }
    }

    std::printf ("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
