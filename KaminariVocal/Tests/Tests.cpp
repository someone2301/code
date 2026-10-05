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
    Render render (KaminariVocalProcessor& p, double seconds, Source src, std::function<void (int)> perBlock = nullptr)
    {
        const int blocks = (int) std::ceil (seconds * sr / block);
        Render r;
        r.in.setSize (2, blocks * block);
        r.out.setSize (2, blocks * block);
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
                    r.in.setSample (c, b * block + i, x);
                }
            p.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c)
                r.out.copyFrom (c, b * block, buf, c, 0, block);
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
        prepare (p);
        const auto r = render (p, 1.0, vocal);
        check (maxDiff (r) == 0.0f, "default state: all sends off, output is bit-identical to the input");
    }

    for (int s = 0; s < 3; ++s)
    {
        KaminariVocalProcessor p;
        setParam (p, sendOn[s], 1.0f);
        setParam (p, sendLevel[s], kvp::sendOffDb);
        prepare (p);
        const auto r = render (p, 1.0, vocal);
        check (maxDiff (r) == 0.0f, juce::String (sendName[s]) + " send on at minimum level: output is bit-identical to the dry signal");
    }

    for (int s = 0; s < 3; ++s)
    {
        std::vector<double> levels;
        bool othersSilent = true, finite = true;
        for (float db : { -30.0f, -18.0f, -6.0f })
        {
            KaminariVocalProcessor p;
            setParam (p, sendOn[s], 1.0f);
            setParam (p, sendLevel[s], db);
            prepare (p);
            double own = 0.0;
            const auto r = render (p, 1.5, vocal, [&] (int)
            {
                own = std::max (own, (double) p.returnRms[(size_t) s].load());
                for (int o = 0; o < 3; ++o)
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
        check (othersSilent, juce::String (sendName[s]) + " send: the other two returns stay at exactly zero");
        check (finite, juce::String (sendName[s]) + " send: output is finite");
    }

    // dry is unchanged: output minus the returns equals the gained input
    {
        KaminariVocalProcessor p;
        setParam (p, kvid::rvOn, 1.0f);
        setParam (p, kvid::rvSend, -6.0f);
        setParam (p, kvid::outGain, -6.0f);
        prepare (p);
        KaminariVocalProcessor dryOnly;
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
    for (int s = 0; s < 3; ++s)
    {
        KaminariVocalProcessor p;
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
        for (int s = 0; s < 3; ++s)
        {
            setParam (p, sendOn[s], 1.0f);
            setParam (p, sendLevel[s], 6.0f);
        }
        setParam (p, kvid::dlFeedback, 100.0f);
        setParam (p, kvid::rvDecay, 20.0f);
        prepare (p);
        float peak[3] = {};
        render (p, 4.0, [] (int c, long n) { return (float) std::sin (2 * kv::pi * 300.0 * n / sr + c); },
                [&] (int) { for (int s = 0; s < 3; ++s) peak[s] = std::max (peak[s], p.returnPeak[(size_t) s].load()); });
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

    // ---- editor ---------------------------------------------------------------------------------------------------
    {
        KaminariVocalProcessor p;
        prepare (p);
        std::unique_ptr<KaminariVocalEditor> ed (dynamic_cast<KaminariVocalEditor*> (p.createEditor()));
        check (ed != nullptr, "editor opens");
        check (ed->strip (0).isVisible() && ! ed->reverbPanel().isVisible(), "Basic view shows the compact send strips");

        ed->strip (1).advanced.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        check (ed->isAdvancedShown() && ed->delayPanel().isVisible() && ! ed->strip (1).isVisible(),
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

        check (rv.header.level.slider.getTitle() == "Reverb Send" && wd.msFocus.slider.getTooltip().contains ("Crossover"),
               "controls carry accessible names and tooltips");

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
            ed->showAdvanced (false, 0);
            save ("basic.png");
            ed->showAdvanced (true, 0);
            save ("adv_reverb.png");
            ed->showAdvanced (true, 1);
            setParam (p, kvid::dlMode, 1.0f);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            save ("adv_delay.png");
            ed->showAdvanced (true, 2);
            save ("adv_widener_microshift.png");
            setParam (p, kvid::wdType, 1.0f);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            save ("adv_widener_sidewidener.png");
        }
    }

    std::printf ("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
