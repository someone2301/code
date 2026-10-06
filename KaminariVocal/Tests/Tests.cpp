#include <numeric>
// Offline checks for the Kaminari Vocal sends: routing, levels, bypass, effect selection, persistence and the editor.
// Build with -DKV_BUILD_TESTS=ON and run: KaminariVocalTests [snapshot_dir]
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>
#include <functional>

// The routing tests compare samples for exact (bit-identical) equality on purpose.
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

    float impulse (int, long n) { return n == 0 ? 1.0f : 0.0f; }

    struct Render { juce::AudioBuffer<float> in, out; };

    // Renders `seconds` of audio; `perBlock` may change parameters before each block.
    // Switches every channel module off, so the dry path is a pure delay of the reported latency.
    void neutral (KaminariVocalProcessor& p)
    {
        for (auto* id : { "tn_on", "eq_on", "mb_on", "lv_on", "fl_on", "dt_on", "ds_on", "rs_on" })
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

    double rmsOfReturn (const Render& r, int from = 0)
    {
        double e = 0;
        long n = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = from; i < r.out.getNumSamples(); ++i, ++n)
            {
                const double d = r.out.getSample (c, i) - r.in.getSample (c, i);
                e += d * d;
            }
        return std::sqrt (e / std::max (1L, n));
    }

    std::vector<float> returnSignal (const Render& r, int ch)
    {
        std::vector<float> v ((size_t) r.out.getNumSamples());
        for (int i = 0; i < r.out.getNumSamples(); ++i)
            v[(size_t) i] = r.out.getSample (ch, i) - r.in.getSample (ch, i);
        return v;
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

    // Energy envelope in 20 ms frames (for comparing reverb algorithms independent of fine phase).
    std::vector<float> envelope (const std::vector<float>& v)
    {
        const int frame = (int) (0.02 * sr);
        std::vector<float> e;
        for (size_t i = 0; i + (size_t) frame <= v.size(); i += (size_t) frame)
        {
            double s = 0;
            for (int k = 0; k < frame; ++k) s += (double) v[i + (size_t) k] * v[i + (size_t) k];
            e.push_back ((float) std::sqrt (s / frame));
        }
        return e;
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

    constexpr int numSends = KaminariVocalProcessor::numSends;
    const char* sendOn[]    = { kvid::rvOn, kvid::dlOn, kvid::wdOn };
    const char* sendLevel[] = { kvid::rvSend, kvid::dlSend, kvid::wdSend };
    const char* sendName[]  = { "reverb", "delay", "widener" };
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    // ---- routing and levels ---------------------------------------------------------------------------------------
    {
        KaminariVocalProcessor p;
        neutral (p);
        prepare (p);
        const auto r = render (p, 1.0, vocal);
        check (maxDiff (r) == 0.0f, "default state: all sends off, output is bit-identical to the input");
    }

    for (int s = 0; s < numSends; ++s)
    {
        KaminariVocalProcessor p;
        neutral (p);
        setParam (p, sendOn[s], 1.0f);
        setParam (p, sendLevel[s], kvp::sendOffDb);
        prepare (p);
        const auto r = render (p, 1.0, vocal);
        check (maxDiff (r) == 0.0f, juce::String (sendName[s]) + " send on at minimum level: output is bit-identical to the dry signal");
    }

    for (int s = 0; s < numSends; ++s)
    {
        std::vector<double> levels;
        bool othersSilent = true, finite = true;
        for (float db : { -30.0f, -18.0f, -6.0f })
        {
            KaminariVocalProcessor p;
            neutral (p);
            setParam (p, sendOn[s], 1.0f);
            setParam (p, sendLevel[s], db);
            prepare (p);
            double own = 0.0;
            const auto r = render (p, 1.5, vocal, [&] (int)
            {
                own = std::max (own, (double) p.returnRms[(size_t) s].load());
                for (int o = 0; o < numSends; ++o)
                    if (o != s && p.returnRms[(size_t) o].load() != 0.0f)
                        othersSilent = false;
            });
            for (int i = 0; i < r.out.getNumSamples(); ++i)
                finite = finite && std::isfinite (r.out.getSample (0, i)) && std::isfinite (r.out.getSample (1, i));
            levels.push_back (rmsOfReturn (r));
        }
        check (levels[0] > 0.0 && levels[0] < levels[1] && levels[1] < levels[2],
               juce::String (sendName[s]) + " send: raising the level raises its return ("
               + juce::String (juce::Decibels::gainToDecibels (levels[0]), 1) + " / "
               + juce::String (juce::Decibels::gainToDecibels (levels[1]), 1) + " / "
               + juce::String (juce::Decibels::gainToDecibels (levels[2]), 1) + " dB)");
        check (othersSilent, juce::String (sendName[s]) + " send: the other returns stay at exactly zero");
        check (finite, juce::String (sendName[s]) + " send: output is finite");
    }

    // dry is unchanged: output minus the returns equals the gained input
    {
        KaminariVocalProcessor p;
        neutral (p);
        setParam (p, kvid::rvOn, 1.0f);
        setParam (p, kvid::rvSend, -6.0f);
        setParam (p, kvid::outGain, -6.0f);
        prepare (p);
        KaminariVocalProcessor dryOnly;
        neutral (dryOnly);
        setParam (dryOnly, kvid::outGain, -6.0f);
        prepare (dryOnly);
        const auto wet = render (p, 1.0, vocal);
        const auto dry = render (dryOnly, 1.0, vocal);
        // the reverb is mono-correlated with the input only through its own return; compare the first 5 ms,
        // before the pre-delay (20 ms) lets any return through
        float m = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < (int) (0.005 * sr); ++i)
                m = std::max (m, std::abs (wet.out.getSample (c, i) - dry.out.getSample (c, i)));
        check (m == 0.0f, "reverb on: before the return arrives the output equals the dry-only output exactly");
    }

    // pre/post fader
    {
        double post = 0, pre = 0;
        for (int tap = 0; tap < 2; ++tap)
        {
            KaminariVocalProcessor p;
            neutral (p);
            setParam (p, kvid::dlOn, 1.0f);
            setParam (p, kvid::dlSend, -6.0f);
            setParam (p, kvid::dlFeedback, 0.0f);
            setParam (p, kvid::dlTap, (float) tap);
            setParam (p, kvid::outGain, -12.0f);
            prepare (p);
            double e = 0;
            render (p, 1.5, vocal, [&] (int) { e += p.returnRms[1].load(); });
            (tap == 0 ? post : pre) = e;
        }
        const double diffDb = juce::Decibels::gainToDecibels (pre / post);
        check (std::abs (diffDb - 12.0) < 1.5, "pre-fader send ignores Output Gain, post-fader follows it (difference "
               + juce::String (diffDb, 1) + " dB for -12 dB Output Gain)");
    }

    // bypass
    for (int s = 0; s < numSends; ++s)
    {
        KaminariVocalProcessor p;
        neutral (p);
        setParam (p, sendOn[s], 1.0f);
        setParam (p, sendLevel[s], 0.0f);
        prepare (p);
        const int offBlock = (int) (1.0 * sr / block);
        const auto r = render (p, 2.0, vocal, [&] (int b) { if (b == offBlock) setParam (p, sendOn[s], 0.0f); });
        const bool wasOn = rmsOfReturn (r, 0) > 1e-4;
        check (wasOn && maxDiff (r, 1.0f, (offBlock + 3) * block) == 0.0f,
               juce::String (sendName[s]) + " send switched off: after the 10 ms fade the output equals the dry signal exactly");
    }

    // clipping: every send at +6 dB with a full-scale input
    {
        KaminariVocalProcessor p;
        neutral (p);
        for (int s = 0; s < numSends; ++s)
        {
            setParam (p, sendOn[s], 1.0f);
            setParam (p, sendLevel[s], 6.0f);
        }
        setParam (p, kvid::dlFeedback, 100.0f);
        setParam (p, kvid::rvDecay, 20.0f);
        prepare (p);
        float peak[numSends] = {};
        render (p, 4.0, [] (int c, long n) { return (float) std::sin (2 * kv::pi * 300.0 * n / sr + c); },
                [&] (int) { for (int s = 0; s < numSends; ++s) peak[s] = std::max (peak[s], p.returnPeak[(size_t) s].load()); });
        check (peak[0] <= 1.0f && peak[1] <= 1.0f && peak[2] <= 1.0f,
               "full-scale input, all sends +6 dB, 100 % feedback, 20 s decay: every return peak <= 0 dBFS ("
               + juce::String (peak[0], 3) + ", " + juce::String (peak[1], 3) + ", " + juce::String (peak[2], 3) + ")");
    }

    // ---- reverb ---------------------------------------------------------------------------------------------------
    {
        std::vector<std::vector<float>> envs, irs;
        bool allFinite = true, allNonZero = true;
        for (int m = 0; m < kv::numReverbModes; ++m)
        {
            KaminariVocalProcessor p;
            neutral (p);
            setParam (p, kvid::rvOn, 1.0f);
            setParam (p, kvid::rvSend, 0.0f);
            setParam (p, kvid::rvMode, (float) m);
            setParam (p, kvid::rvPreDelay, 0.0f);
            prepare (p);
            const auto r = render (p, 2.0, impulse);
            auto ret = returnSignal (r, 0);
            const auto retR = returnSignal (r, 1);
            for (size_t i = 0; i < ret.size(); ++i)
            {
                allFinite = allFinite && std::isfinite (ret[i]) && std::isfinite (retR[i]);
                ret[i] = 0.5f * (ret[i] + retR[i]);
            }
            double e = 0;
            for (auto v : ret) e += (double) v * v;
            allNonZero = allNonZero && e > 1e-8;
            envs.push_back (envelope (ret));
            irs.push_back (ret);
        }
        double worst = -1.0;
        juce::String worstPair;
        for (int a = 0; a < kv::numReverbModes; ++a)
            for (int b = a + 1; b < kv::numReverbModes; ++b)
            {
                const double c = correlation (irs[(size_t) a], irs[(size_t) b]);
                if (c > worst) { worst = c; worstPair = juce::String (kv::reverbMode (a).name) + " / " + kv::reverbMode (b).name; }
            }
        check (allFinite && allNonZero, "all 20 reverb modes produce a finite, non-zero return");
        check (worst < 0.9, "all 20 reverb modes have different impulse responses (highest correlation "
               + juce::String (worst, 3) + ": " + worstPair + ")");
    }

    {
        // Decay changes the tail of an FDN mode; Attack changes Nonlin and Ambience; Decay is hidden for Nonlin
        auto tailEnergy = [] (int mode, const char* id, float value)
        {
            KaminariVocalProcessor p;
            neutral (p);
            setParam (p, kvid::rvOn, 1.0f);
            setParam (p, kvid::rvSend, 0.0f);
            setParam (p, kvid::rvMode, (float) mode);
            setParam (p, id, value);
            prepare (p);
            const auto r = render (p, 3.0, impulse);
            const auto v = returnSignal (r, 0);
            double late = 0;
            for (size_t i = (size_t) (1.0 * sr); i < v.size(); ++i) late += (double) v[i] * v[i];
            return std::pair<double, std::vector<float>> (late, v);
        };
        const auto shortTail = tailEnergy (0, kvid::rvDecay, 0.5f), longTail = tailEnergy (0, kvid::rvDecay, 8.0f);
        check (longTail.first > 100.0 * shortTail.first, "Concert Hall: Decay 8 s leaves far more energy after 1 s than Decay 0.5 s");

        const auto gated = tailEnergy (14, kvid::rvAttack, 0.0f), reverse = tailEnergy (14, kvid::rvAttack, 100.0f);
        const auto eg = envelope (gated.second), er = envelope (reverse.second);
        check (eg.size() > 10 && er[2] < er[8] && eg[2] > er[2], "Nonlin: Attack 0 is a flat gate, Attack 100 rises (reverse envelope)");

        const auto ambER = tailEnergy (7, kvid::rvAttack, 0.0f), ambLate = tailEnergy (7, kvid::rvAttack, 100.0f);
        check (correlation (ambER.second, ambLate.second) < 0.5, "Ambience: Attack moves the balance between early reflections and tail");

        check (! kv::reverbControlUse (14).decay && kv::reverbControlUse (14).attack && ! kv::reverbControlUse (14).modulation,
               "Nonlin exposes Attack, hides Decay and modulation (they have no effect)");
        check (kv::reverbControlUse (0).decay && ! kv::reverbControlUse (0).attack, "Concert Hall exposes Decay, hides Attack");
    }

    // ---- delay ----------------------------------------------------------------------------------------------------
    {
        KaminariVocalProcessor p;
        neutral (p);
        setParam (p, kvid::dlOn, 1.0f);
        setParam (p, kvid::dlSend, 0.0f);
        setParam (p, kvid::dlStyle, 0.0f);        // clean
        setParam (p, kvid::dlT1Unit, 1.0f);       // note, 1/4 at the default 120 BPM = 500 ms
        setParam (p, kvid::dlT1Note, 1.0f);
        setParam (p, kvid::dlFeedback, 0.0f);
        setParam (p, kvid::dlWidth, 0.0f);
        setParam (p, kvid::dlLoCut, 20.0f);
        setParam (p, kvid::dlHiCut, 20000.0f);
        prepare (p);
        const auto r = render (p, 1.5, impulse);
        const auto v = returnSignal (r, 0);
        const auto peakAt = (int) (std::max_element (v.begin(), v.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); }) - v.begin());
        double late = 0;
        for (size_t i = (size_t) (0.6 * sr); i < v.size(); ++i) late += (double) v[i] * v[i];
        check (std::abs (peakAt - 24000) <= 3, "delay 1/4 note at 120 BPM: echo at 500 ms (sample " + juce::String (peakAt) + ")");
        check (late < 1e-6, "delay with 0 % feedback: a single echo");
    }
    {
        KaminariVocalProcessor p;
        neutral (p);
        setParam (p, kvid::dlOn, 1.0f);
        setParam (p, kvid::dlSend, 0.0f);
        setParam (p, kvid::dlMode, 2.0f);         // ping-pong
        setParam (p, kvid::dlT1Unit, 0.0f);
        setParam (p, kvid::dlT1Ms, 200.0f);
        setParam (p, kvid::dlT2Unit, 0.0f);
        setParam (p, kvid::dlT2Ms, 100.0f);
        setParam (p, kvid::dlWidth, 75.0f);
        setParam (p, kvid::dlFeedback, 0.0f);
        prepare (p);
        const auto r = render (p, 0.6, impulse);
        const auto l = returnSignal (r, 0), rr = returnSignal (r, 1);
        auto energyAround = [] (const std::vector<float>& v, double t)
        {
            double e = 0;
            for (int i = (int) ((t - 0.01) * sr); i < (int) ((t + 0.02) * sr); ++i) e += (double) v[(size_t) i] * v[(size_t) i];
            return e;
        };
        check (energyAround (l, 0.2) > 100 * energyAround (rr, 0.2) && energyAround (rr, 0.3) > 100 * energyAround (l, 0.3),
               "ping-pong: ping (200 ms) on the left, pong (200 + 100 ms) on the right");
    }
    {
        KaminariVocalProcessor p;
        neutral (p);
        setParam (p, kvid::dlOn, 1.0f);
        setParam (p, kvid::dlSend, 6.0f);
        setParam (p, kvid::dlFeedback, 100.0f);
        setParam (p, kvid::dlT1Unit, 0.0f);
        setParam (p, kvid::dlT1Ms, 30.0f);
        setParam (p, kvid::dlStyle, 0.0f);
        setParam (p, kvid::dlSaturation, 0.0f);
        prepare (p);
        float peak = 0;
        render (p, 10.0, vocal, [&] (int) { peak = std::max (peak, p.returnPeak[1].load()); });
        check (peak <= 1.0f && peak > 0.01f, "delay at 100 % feedback with short time stays bounded for 10 s (peak " + juce::String (peak, 3) + ")");
    }
    {
        // groove: swing moves the first echo later and the second earlier relative to the straight grid
        KaminariVocalProcessor p;
        neutral (p);
        setParam (p, kvid::dlOn, 1.0f);
        setParam (p, kvid::dlSend, 0.0f);
        setParam (p, kvid::dlStyle, 0.0f);
        setParam (p, kvid::dlT1Unit, 0.0f);
        setParam (p, kvid::dlT1Ms, 300.0f);
        setParam (p, kvid::dlGroove, 100.0f);
        setParam (p, kvid::dlFeedback, 50.0f);
        setParam (p, kvid::dlWidth, 0.0f);
        prepare (p);
        const auto v = returnSignal (render (p, 1.0, impulse), 0);
        auto peakNear = [&] (double t)
        {
            int best = 0; float bv = 0;
            for (int i = (int) ((t - 0.15) * sr); i < (int) ((t + 0.15) * sr); ++i)
                if (std::abs (v[(size_t) i]) > bv) { bv = std::abs (v[(size_t) i]); best = i; }
            return best;
        };
        const int first = peakNear (0.4), second = peakNear (0.6);
        check (std::abs (first - (int) (0.4 * sr)) < 10 && std::abs (second - (int) (0.6 * sr)) < 10,
               "delay groove (full swing): echoes at 400 ms and 600 ms instead of 300 / 600 ms");
    }

    // ---- widener --------------------------------------------------------------------------------------------------
    {
        auto run = [] (int type, std::function<void (KaminariVocalProcessor&)> extra = nullptr)
        {
            auto p = std::make_unique<KaminariVocalProcessor>();
            neutral (*p);
            setParam (*p, kvid::wdOn, 1.0f);
            setParam (*p, kvid::wdSend, 0.0f);
            setParam (*p, kvid::wdType, (float) type);
            if (extra) extra (*p);
            prepare (*p);
            return render (*p, 1.0, vocal);
        };
        const auto micro = run (0), side = run (1);
        double monoSum = 0, sideEnergy = 0;
        for (int i = 0; i < side.out.getNumSamples(); ++i)
        {
            const double rl = side.out.getSample (0, i) - side.in.getSample (0, i);
            const double rr = side.out.getSample (1, i) - side.in.getSample (1, i);
            monoSum = std::max (monoSum, std::abs (rl + rr));
            sideEnergy += rl * rl;
        }
        check (sideEnergy > 1e-3 && monoSum < 1e-6, "SideWidener return is pure side: its mono sum is zero (mono compatible)");
        check (correlation (returnSignal (micro, 0), returnSignal (side, 0)) < 0.5, "MicroShift and SideWidener produce different returns");

        const auto ml = returnSignal (micro, 0), mr = returnSignal (micro, 1);
        check (correlation (ml, mr) < 0.98 && rmsOfReturn (micro) > 1e-3, "MicroShift return differs left to right (widening)");

        const auto focused = run (0, [] (KaminariVocalProcessor& p) { setParam (p, kvid::msFocus, 10000.0f); });
        check (rmsOfReturn (focused) < 0.05 * rmsOfReturn (micro), "MicroShift Focus 10 kHz returns almost nothing from a 220 Hz vocal tone");

        const auto noDetune = run (0, [] (KaminariVocalProcessor& p) { setParam (p, kvid::msDetune, 0.0f); setParam (p, kvid::msDelay, 0.0f); });
        check (correlation (returnSignal (noDetune, 0), ml) < 0.99, "MicroShift Detune and Delay change the return");

        // switching at run time: after the switch only SideWidener runs (the return stays pure side)
        KaminariVocalProcessor p;
        neutral (p);
        setParam (p, kvid::wdOn, 1.0f);
        setParam (p, kvid::wdSend, 0.0f);
        prepare (p);
        const int switchBlock = (int) (0.5 * sr / block);
        const auto r = render (p, 1.0, vocal, [&] (int b) { if (b == switchBlock) setParam (p, kvid::wdType, 1.0f); });
        double worstMono = 0;
        for (int i = (switchBlock + 3) * block; i < r.out.getNumSamples(); ++i)
            worstMono = std::max (worstMono, (double) std::abs (r.out.getSample (0, i) - r.in.getSample (0, i)
                                                               + r.out.getSample (1, i) - r.in.getSample (1, i)));
        check (worstMono < 1e-6, "switching MicroShift -> SideWidener replaces the algorithm instead of layering both");
    }


    // ---- channel modules ------------------------------------------------------------------------------------------
    {
        KaminariVocalProcessor p;
        neutral (p);
        prepare (p);
        check (p.getLatencySamples() == 96, "reported latency is 96 samples at 48 kHz (Tune's fixed delay), also with Tune off");
        const auto r = render (p, 0.5, vocal);
        check (maxDiff (r) == 0.0f, "all modules off: the output is the input delayed by exactly the reported latency");

        setParam (p, "lv_lookahead", 5.0f);
        setParam (p, "lv_on", 1.0f);
        juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi; buf.clear();
        p.processBlock (buf, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        check (p.getLatencySamples() == 96 + 240, "Compression lookahead 5 ms adds 240 samples to the reported latency");
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
        neutral (p);
        setParam (p, kvid::wdOn, 1.0f);
        setParam (p, kvid::wdSend, 0.0f);
        setParam (p, kvid::wdType, 1.0f);   // SideWidener: pure side return
        p.setRateAndBufferSizeDetails (sr, block);
        p.prepareToPlay (sr, block);
        juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi;
        double side = 0, midErr = 0;
        for (int b = 0; b < 100; ++b)
        {
            for (int i = 0; i < block; ++i) { buf.setSample (0, i, vocal (0, (long) b * block + i)); buf.setSample (1, i, 0.0f); }
            juce::AudioBuffer<float> in (1, block); in.copyFrom (0, 0, buf, 0, 0, block);
            p.processBlock (buf, midi);
            for (int i = 0; i < block; ++i)
            {
                const double l = buf.getSample (0, i), r = buf.getSample (1, i);
                side += (l - r) * (l - r);
                if (b > 2) midErr = std::max (midErr, std::abs (0.5 * (l + r) - 0.0));
            }
        }
        check (side > 1e-3, "mono-in / stereo-out: the widener return is stereo (left and right differ)");
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
        // Compression: -6 dBFS sine, threshold -20, 4:1, hard knee, no makeup -> about -16.5 dBFS
        KaminariVocalProcessor p;
        neutral (p);
        setParam (p, "lv_on", 1.0f); setParam (p, "lv_thresh", -20.0f); setParam (p, "lv_ratio", 4.0f); setParam (p, "lv_knee", 0.0f);
        setParam (p, "lv_range", 40.0f); setParam (p, "lv_auto_gain", 0.0f); setParam (p, "lv_auto_release", 0.0f);
        prepare (p);
        const auto r = render (p, 1.0, sine (1000.0, -6.0f));
        const float out = toneDb (r.out, 0, 1000.0, 36000, 48000);
        check (std::abs (out + 16.5f) < 1.5f, "Compression 4:1 at -20 dB: a -6 dBFS tone comes out at " + juce::String (out, 1) + " dBFS (expected about -16.5)");
        check (p.moduleGr[KaminariVocalProcessor::ModCompression].load() > 8.0f, "Compression reports its gain reduction to the meter");
    }
    {
        // De-ess: 7 kHz is reduced by about its range, 300 Hz is untouched (split band)
        KaminariVocalProcessor p;
        neutral (p);
        setParam (p, "ds_on", 1.0f); setParam (p, "ds_thresh", -40.0f); setParam (p, "ds_range", 8.0f);
        prepare (p);
        auto r = render (p, 0.5, sine (7000.0, -10.0f));
        const float ess = toneDb (r.out, 0, 7000.0, 12000, 24000) - toneDb (r.in, 0, 7000.0, 12000, 24000);
        r = render (p, 0.5, sine (300.0, -10.0f));
        const float low = toneDb (r.out, 0, 300.0, 12000, 24000) - toneDb (r.in, 0, 300.0, 12000, 24000);
        check (ess < -6.0f && std::abs (low) < 0.3f, "De-ess: 7 kHz reduced " + juce::String (-ess, 1) + " dB, 300 Hz changed " + juce::String (low, 2) + " dB");
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
            for (int k = 0; k < p.resonance.numBands(); ++k) maxRed = std::max (maxRed, (double) p.resonance.bandReduction (k));
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
        check (b.loadedStateVersion == KaminariVocalProcessor::stateVersion && xml != nullptr && (int) xml->getIntAttribute ("engine_reverb") == 1,
               "sessions store a state version and per-module engine versions");
    }

    {
        // CPU estimate: every module and send on, Resonance at Ultra quality (information only, no pass/fail)
        KaminariVocalProcessor p;
        p.presets.loadChainPreset ("Pop Lead");
        setParam (p, "rs_on", 1.0f); setParam (p, "rs_quality", 2.0f); setParam (p, kvid::wdOn, 1.0f);
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
            { kvid::rvOn, 1 }, { kvid::rvSend, -9 }, { kvid::rvTap, 1 }, { kvid::rvMode, 12 }, { kvid::rvDecay, 4.5f },
            { kvid::rvAttack, 30 }, { kvid::dlOn, 1 }, { kvid::dlMode, 1 }, { kvid::dlStyle, 3 }, { kvid::dlT2Note, 3 },
            { kvid::dlGroove, -40 }, { kvid::dlPrime, 1 }, { kvid::dlWobbleShape, 4 }, { kvid::wdOn, 1 }, { kvid::wdType, 1 },
            { kvid::msStyle, 2 }, { kvid::msFocus, 640 }, { kvid::swMode, 2 }, { kvid::swTone, 15 }, { kvid::swOutput, -6 } };
        for (auto& [id, v] : values) setParam (a, id, v);
        a.advancedView.store (true);
        a.advancedSend.store (2);
        juce::MemoryBlock state;
        a.getStateInformation (state);

        KaminariVocalProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());
        bool same = true;
        for (auto& [id, v] : values)
            if (std::abs (getParam (b, id) - getParam (a, id)) > 1e-3f) { same = false; std::printf ("  mismatch %s\n", id); }
        check (same, "every send parameter survives a save / load round trip");
        check (b.advancedView.load() && b.advancedSend.load() == 2, "Basic/Advanced view and the open send panel are saved with the session");
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
        check (moduleCount == 11 && presetCount >= 80 && pm.getFactoryChains().size() >= 12,
               "factory presets for all 11 modules (" + juce::String (presetCount) + " module presets) and "
               + juce::String ((int) pm.getFactoryChains().size()) + " chain presets");

        setParam (p, kvid::rvOn, 1.0f);
        setParam (p, kvid::rvSend, -3.0f);
        setParam (p, kvid::rvModRate, 3.0f);
        setParam (p, kvid::dlFeedback, 77.0f);
        check (pm.loadModulePreset ("reverb", "Vocal Plate"), "load reverb module preset 'Vocal Plate'");
        check (juce::roundToInt (getParam (p, kvid::rvMode)) == 2 && std::abs (getParam (p, kvid::rvDecay) - 1.8f) < 0.01f
               && std::abs (getParam (p, kvid::rvPreDelay) - 30.0f) < 0.05f,
               "module preset sets its listed values (Plate, 1.8 s, 30 ms)");
        check (std::abs (getParam (p, kvid::rvModRate) - 0.6f) < 0.01f, "module preset returns unlisted module values to their defaults");
        check (getParam (p, kvid::rvOn) > 0.5f && std::abs (getParam (p, kvid::rvSend) + 3.0f) < 0.01f,
               "module preset leaves the send's On switch and level alone");
        check (std::abs (getParam (p, kvid::dlFeedback) - 77.0f) < 0.05f, "reverb preset does not touch the delay");
        check (! pm.isModuleModified ("reverb") && pm.currentModulePreset ("reverb") == "Vocal Plate", "freshly loaded preset is not marked modified");
        setParam (p, kvid::rvSize, 90.0f);
        check (pm.isModuleModified ("reverb"), "changing a reverb control marks the reverb preset modified");
        check (! pm.isModuleModified ("widener"), "untouched modules are not marked modified");

        check (pm.loadChainPreset ("Pop Lead"), "load chain preset 'Pop Lead'");
        check (getParam (p, kvid::rvOn) > 0.5f && std::abs (getParam (p, kvid::rvSend) + 14.0f) < 0.01f
               && juce::roundToInt (getParam (p, kvid::rvMode)) == 2
               && juce::roundToInt (getParam (p, kvid::dlT1Note)) == 2 && std::abs (getParam (p, kvid::msFocus) - 2000.0f) < 1.0f
               && std::abs (getParam (p, kvid::dlFeedback) - 35.0f) < 0.05f,
               "chain preset loads each module preset and its send overrides");
        check (pm.currentChainPreset() == "Pop Lead" && pm.currentModulePreset ("delay") == "1/8 Throw"
               && pm.currentModulePreset ("tune") == "Tight Pop", "chain preset records the module presets it used (built or not)");
        pm.loadChainPreset ("Default");
        check (getParam (p, kvid::rvOn) < 0.5f && getParam (p, kvid::dlOn) < 0.5f && getParam (p, kvid::wdOn) < 0.5f,
               "chain preset 'Default' switches the sends off");

        // every factory preset of the built modules loads and plays without problems
        bool allOk = true;
        prepare (p);
        for (auto key : { "reverb", "delay", "widener" })
            for (auto& pr : pm.findModule (key)->factory)
            {
                allOk = pm.loadModulePreset (key, pr) && allOk;
                setParam (p, kvid::rvOn, 1.0f); setParam (p, kvid::dlOn, 1.0f); setParam (p, kvid::wdOn, 1.0f);
                setParam (p, kvid::rvSend, 0.0f); setParam (p, kvid::dlSend, 0.0f); setParam (p, kvid::wdSend, 0.0f);
                const auto r = render (p, 0.3, vocal);
                for (int i = 0; i < r.out.getNumSamples(); ++i)
                    allOk = allOk && std::isfinite (r.out.getSample (0, i)) && std::abs (r.out.getSample (0, i)) < 4.0f;
            }
        check (allOk, "every reverb, delay and widener factory preset loads and plays (finite, bounded)");
        for (auto& c : pm.getFactoryChains())
            allOk = pm.loadChainPreset (c) && allOk;
        check (allOk, "every chain preset loads");

        // user presets
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kv-preset-test-" + juce::String (juce::Random::getSystemRandom().nextInt()));
        pm.setUserFolder (tmp);
        pm.loadModulePreset ("delay", "Worn Tape Echo");
        setParam (p, kvid::dlFeedback, 62.0f);
        check (pm.saveModulePreset ("delay", "My Echo / test"), "save a user delay preset");
        bool listed = false;
        for (auto& pr : pm.listModulePresets ("delay"))
            listed = listed || (! pr.factory && pr.name == PresetManager::sanitiseName ("My Echo / test"));
        check (listed && tmp.getChildFile ("Delay").getNumberOfChildFiles (juce::File::findFiles) == 1,
               "user preset is listed after the factory presets and stored in the Delay folder");
        pm.loadModulePreset ("delay", "Slapback");
        check (pm.loadModulePreset ("delay", PresetManager::sanitiseName ("My Echo / test"))
               && std::abs (getParam (p, kvid::dlFeedback) - 62.0f) < 0.05f && juce::roundToInt (getParam (p, kvid::dlStyle)) == 2,
               "user preset restores its values (feedback 62 %, Worn Tape)");
        setParam (p, kvid::wdSend, -7.0f);
        check (pm.saveChainPreset ("My Chain"), "save a user chain preset");
        pm.loadChainPreset ("Default");
        check (pm.loadChainPreset ("My Chain") && std::abs (getParam (p, kvid::wdSend) + 7.0f) < 0.01f
               && std::abs (getParam (p, kvid::dlFeedback) - 62.0f) < 0.05f, "user chain preset restores every parameter");
        for (auto& pr : pm.listModulePresets ("delay"))
            if (! pr.factory) pm.deleteUserPreset (pr);
        check (tmp.getChildFile ("Delay").getNumberOfChildFiles (juce::File::findFiles) == 0, "user preset can be deleted");
        tmp.deleteRecursively();

        // stepping and session state
        pm.loadModulePreset ("widener", "MicroShift Classic");
        pm.stepModulePreset ("widener", 1);
        const auto afterNext = pm.currentModulePreset ("widener");
        pm.stepModulePreset ("widener", -1);
        check (afterNext == "Subtle Doubler" && pm.currentModulePreset ("widener") == "MicroShift Classic", "next / previous step through the list");
        pm.loadChainPreset ("R&B Smooth");
        juce::MemoryBlock state;
        p.getStateInformation (state);
        KaminariVocalProcessor q;
        q.setStateInformation (state.getData(), (int) state.getSize());
        check (q.presets.currentChainPreset() == "R&B Smooth" && q.presets.currentModulePreset ("reverb") == "Big Ballad Hall"
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

    // ---- distortion ------------------------------------------------------------------------------------------------
    {
        KaminariVocalProcessor p;
        neutral (p);
        setParam (p, "dt_os", 1.0f);
        prepare (p);
        const int base = p.getLatencySamples();
        auto r = render (p, 0.5, vocal);
        check (maxDiff (r) == 0.0f, "Distortion off: no effect and no added latency");

        setParam (p, "dt_on", 1.0f);
        setParam (p, "dt_style", 4.0f);   // Clip
        setParam (p, "dt_drive", 18.0f);
        prepare (p);
        const int withOs = p.getLatencySamples();
        check (withOs > base && withOs - base < 64, "Distortion on with 2x oversampling adds " + juce::String (withOs - base) + " samples of latency");
        r = render (p, 1.0, sine (1000.0, -12.0f));
        const float h1 = toneDb (r.out, 0, 1000.0, 12000, 48000), h3 = toneDb (r.out, 0, 3000.0, 12000, 48000);
        const float h3in = toneDb (r.in, 0, 3000.0, 12000, 48000);
        check (h3 - h1 > -30.0f && h3in < -120.0f, "Clip at 18 dB drive adds harmonics (3rd harmonic " + juce::String (h3 - h1, 1) + " dB below the fundamental)");
        const float inDb = toneDb (r.in, 0, 1000.0, 12000, 48000);
        check (std::abs (h1 - inDb) < 4.0f, "Auto Gain keeps the level close to the input (" + juce::String (h1 - inDb, 1) + " dB)");

        // Mix 0: the output is the input, delayed by exactly the reported latency
        setParam (p, "dt_mix", 0.0f);
        prepare (p);
        r = render (p, 0.5, vocal);
        check (maxDiff (r) < 1.0e-6f, "Distortion Mix 0 %: the dry path stays aligned with the reported latency (max diff "
               + juce::String (maxDiff (r), 7) + ")");

        // every style, every oversampling setting: finite and bounded on a hot vocal
        bool ok = true;
        for (int st = 0; st < 6; ++st)
            for (int os = 0; os < 3; ++os)
            {
                setParam (p, "dt_style", (float) st);
                setParam (p, "dt_os", (float) os);
                setParam (p, "dt_mix", 100.0f);
                setParam (p, "dt_drive", 36.0f);
                setParam (p, "dt_bias", 60.0f);
                prepare (p);
                r = render (p, 0.5, vocal);
                for (int i = 0; i < r.out.getNumSamples(); ++i)
                    ok = ok && std::isfinite (r.out.getSample (0, i)) && std::abs (r.out.getSample (0, i)) < 4.0f;
            }
        check (ok, "Distortion: every style and oversampling setting stays finite and bounded at full drive and bias");
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

    // ---- flanger ---------------------------------------------------------------------------------------------------
    {
        // static comb (no sweep, no feedback), Mix 50 %: dry + copy delayed 1 ms cancel at 500 Hz and add at 1 kHz
        KaminariVocalProcessor p;
        neutral (p);
        setParam (p, kvid::flOn, 1.0f);
        setParam (p, kvid::flMix, 50.0f);
        setParam (p, kvid::flDepth, 0.0f);
        setParam (p, kvid::flDelay, 1.0f);
        setParam (p, kvid::flFeedback, 0.0f);
        setParam (p, kvid::flHiCut, 20000.0f);
        prepare (p);
        check (p.getLatencySamples() == kv::Tune::latencyFor (sr), "Flanger adds no latency");
        auto noise = [] (int, long n) { kv::Random rnd ((uint32_t) n * 2654435761u + 3u); return 0.3f * rnd.next(); };
        auto r = render (p, 0.5, sine (500.0, -12.0f));
        const float notch = toneDb (r.out, 0, 500.0, 4800, 24000);
        r = render (p, 0.5, sine (1000.0, -12.0f));
        const float peak = toneDb (r.out, 0, 1000.0, 4800, 24000);
        check (peak - notch > 40.0f && std::abs (peak + 12.0f) < 0.5f, "Flanger in the chain, Mix 50 %: 500 Hz cancelled to "
               + juce::String (notch, 1) + " dBFS, 1 kHz passes at " + juce::String (peak, 1) + " dBFS (input -12)");
        setParam (p, kvid::flFeedback, 95.0f);
        setParam (p, kvid::flDepth, 100.0f);
        prepare (p);
        r = render (p, 3.0, [] (int c, long n) { return (float) std::sin (2 * kv::pi * 300.0 * n / sr + c); });
        bool bounded = true;
        for (int i = 0; i < r.out.getNumSamples(); ++i) bounded = bounded && std::isfinite (r.out.getSample (0, i)) && std::abs (r.out.getSample (0, i)) < 4.0f;
        check (bounded, "Flanger at 95 % feedback and full depth stays finite and bounded");
        neutral (p);
        prepare (p);
        r = render (p, 0.5, noise);
        check (maxDiff (r) == 0.0f, "Flanger off: the signal passes unchanged");
    }

    // ---- editor ---------------------------------------------------------------------------------------------------
    {
        KaminariVocalProcessor p;
        prepare (p);
        std::unique_ptr<KaminariVocalEditor> ed (dynamic_cast<KaminariVocalEditor*> (p.createEditor()));
        check (ed != nullptr, "editor opens");
        check (ed->strip (0).isVisible() && ! ed->panel (KaminariVocalEditor::TabSends).isVisible(), "Basic view shows the compact send strips");

        ed->strip (1).open.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        check (ed->isAdvancedShown() && ed->panel (KaminariVocalEditor::TabSends).isVisible() && ed->delayPanel().isVisible() && ! ed->strip (1).isVisible(),
               "a strip's Advanced button opens that send's Advanced panel");

        ed->showAdvanced (true, 0);
        setParam (p, kvid::rvMode, 14.0f);   // Nonlin
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        auto& rv = ed->reverbPanel();
        check (! rv.decay.isVisible() && rv.attack.isVisible() && ! rv.modDepth.isVisible(), "Nonlin: Decay and modulation hidden, Attack shown");
        setParam (p, kvid::rvMode, 0.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        check (rv.decay.isVisible() && ! rv.attack.isVisible() && rv.modDepth.isVisible(), "Concert Hall: Decay and modulation shown, Attack hidden");

        auto& dl = ed->delayPanel();
        setParam (p, kvid::dlMode, 0.0f);
        setParam (p, kvid::dlT1Unit, 0.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        check (dl.time1.isVisible() && ! dl.unit2.isVisible() && ! dl.fbMix.isVisible() && dl.accent.isVisible(),
               "delay Single mode: one echo time, no Dual-only controls");
        setParam (p, kvid::dlMode, 2.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        check (dl.unit2.isVisible() && dl.balance.isVisible() && ! dl.accent.isVisible() && ! dl.offset.isVisible(),
               "delay Ping-Pong mode: Ping and Pong times and Balance; no Accent or L/R Offset");

        auto& wd = ed->widenerPanel();
        setParam (p, kvid::wdType, 1.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        check (! wd.msDetune.isVisible() && wd.swWidth.isVisible() && wd.swMode.isVisible(), "widener SideWidener: only SideWidener controls");
        setParam (p, kvid::wdType, 0.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        check (wd.msDetune.isVisible() && wd.msFocus.isVisible() && ! wd.swWidth.isVisible(), "widener MicroShift: only MicroShift controls");

        p.presets.loadChainPreset ("Pop Lead");
        ed->chainPresets().onLoaded();
        check (ed->chainPresets().displayText() == "Pop Lead" && rv.header.preset.displayText() == "Vocal Plate",
               "header shows the chain preset; the reverb panel shows its module preset");
        setParam (p, kvid::rvDecay, 5.0f);
        check (rv.header.preset.displayText() == "Vocal Plate *" && ed->chainPresets().displayText() == "Pop Lead *",
               "editing a control marks the module and chain presets modified");

        check (rv.header.level.slider.getTitle() == "Reverb Send" && wd.msFocus.slider.getTooltip().contains ("Crossover"),
               "controls carry accessible names and tooltips");

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
            const char* tabNamesT[] = { "Tune", "EQ", "Multiband", "Compression", "Flanger", "Distortion", "De-ess", "Resonance" };
            for (int t = 0; t < KaminariVocalEditor::TabSends; ++t)
            {
                ed->showTab (true, t);
                visit (ed->panel (t), tabNamesT[t]);
            }
            for (int s2 = 0; s2 < KaminariVocalProcessor::numSends; ++s2)
            {
                ed->showAdvanced (true, s2);
                visit (ed->panel (KaminariVocalEditor::TabSends), juce::String ("Send ") + sendName[s2]);
            }
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
                    const juce::String pre = "eq" + juce::String (bandNo++) + "_";
                    typesOk = typesOk && getParam (p, (pre + "used").toRawUTF8()) > 0.5f
                                      && juce::roundToInt (getParam (p, (pre + "type").toRawUTF8())) == type
                                      && std::abs (getParam (p, (pre + "freq").toRawUTF8()) / (float) f - 1.0f) < 0.03f;
                }
                check (typesOk, "EQ click-to-create: 40 Hz low cut, 100 Hz low shelf, 1 kHz bell, 10 kHz high shelf, 18 kHz high cut");
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
            setParam (p, "lv_style", 1.0f);   // Vocal
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            const bool ratioOff = ! ed->compressionPanel().ratioKnob().isEnabled() && ed->compressionPanel().ratioKnob().value.getText() == "Auto";
            setParam (p, "lv_style", 0.0f);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            check (ratioOff && ed->compressionPanel().ratioKnob().isEnabled(), "Compression Vocal style: the Ratio knob is dimmed, disabled and reads Auto");
            check (dead.isEmpty() && tested > 60, "every knob, slider and value box responds to a drag (" + juce::String (tested) + " tested, "
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
            const char* names[] = { "tune", "eq", "multiband", "compression", "flanger", "distortion", "deess", "resonance" };
            for (int t = 0; t < KaminariVocalEditor::TabSends; ++t)
            {
                ed->showTab (true, t);
                for (int k = 0; k < 4; ++k) { render (p, 0.1, vocal); juce::MessageManager::getInstance()->runDispatchLoopUntil (40); }
                save (juce::String ("adv_") + names[t] + ".png");
            }
            const char* sendNames[] = { "reverb", "delay", "widener" };
            for (int s2 = 0; s2 < numSends; ++s2)
            {
                ed->showAdvanced (true, s2);
                for (int k = 0; k < 4; ++k) { render (p, 0.1, vocal); juce::MessageManager::getInstance()->runDispatchLoopUntil (40); }
                save (juce::String ("adv_") + sendNames[s2] + ".png");
            }
            // other modes of the send pages
            auto shot = [&] (int send, const char* id, float v, const char* name)
            {
                setParam (p, id, v);
                ed->showAdvanced (true, send);
                for (int k = 0; k < 3; ++k) { render (p, 0.1, vocal); juce::MessageManager::getInstance()->runDispatchLoopUntil (40); }
                save (name);
            };
            shot (0, kvid::rvMode, 14.0f, "adv_reverb_nonlin.png");
            shot (0, kvid::rvMode, 7.0f, "adv_reverb_ambience.png");
            shot (1, kvid::dlMode, 1.0f, "adv_delay_dual.png");
            shot (1, kvid::dlMode, 2.0f, "adv_delay_pingpong.png");
            shot (2, kvid::wdType, 1.0f, "adv_widener_side.png");
        }
    }

    std::printf ("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
