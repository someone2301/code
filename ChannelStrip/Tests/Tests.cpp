// Offline checks for the channel strip DSP, plus a headless snapshot of the editor.
// Build with -DCHANNELSTRIP_BUILD_TESTS=ON and run: ChannelStripTests [snapshot_dir]
#include "PluginProcessor.h"
#include <cstdio>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("[%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failures;
    }

    void setParam (ChannelStripProcessor& p, const juce::String& id, float value)
    {
        auto* prm = p.apvts.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    }

    struct Result { float rmsL, rmsR; float peakL; bool finite; };

    // Feeds a sine (optionally left only) and returns RMS of the last half second.
    Result run (ChannelStripProcessor& p, float freq, float levelDb, bool leftOnly = false, double sr = 48000.0)
    {
        p.setPlayConfigDetails (2, 2, sr, 512);
        p.prepareToPlay (sr, 512);

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        const float amp = juce::Decibels::decibelsToGain (levelDb);
        double phase = 0.0;
        const int blocks = (int) (2.0 * sr / 512);
        double sumL = 0, sumR = 0, peak = 0;
        long counted = 0;
        bool finite = true;

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float s = amp * (float) std::sin (phase);
                phase += 2.0 * juce::MathConstants<double>::pi * freq / sr;
                buf.setSample (0, i, s);
                buf.setSample (1, i, leftOnly ? 0.0f : s);
            }
            p.processBlock (buf, midi);

            if (b >= blocks - (int) (0.5 * sr / 512))
                for (int i = 0; i < 512; ++i)
                {
                    const float l = buf.getSample (0, i), r = buf.getSample (1, i);
                    finite = finite && std::isfinite (l) && std::isfinite (r);
                    sumL += l * l;
                    sumR += r * r;
                    peak = std::max (peak, (double) std::abs (l));
                    ++counted;
                }
        }
        return { (float) std::sqrt (sumL / counted), (float) std::sqrt (sumR / counted), (float) peak, finite };
    }

    float db (float x) { return juce::Decibels::gainToDecibels (x, -120.0f); }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const float inRms = juce::Decibels::decibelsToGain (-6.0f) / std::sqrt (2.0f);

    {
        ChannelStripProcessor p;
        auto r = run (p, 1000.0f, -6.0f);
        check (std::abs (db (r.rmsL) - db (inRms)) < 0.1f && std::abs (db (r.rmsR) - db (inRms)) < 0.1f, "default settings are transparent");
    }
    {
        ChannelStripProcessor p;
        setParam (p, ids::inGain, 6.0f);
        setParam (p, ids::outGain, -6.0f);
        auto r = run (p, 1000.0f, -6.0f);
        check (std::abs (db (r.rmsL) - db (inRms)) < 0.1f, "+6 dB in / -6 dB out cancel");
    }
    {
        ChannelStripProcessor p;
        setParam (p, ids::phase, 1.0f);
        p.setPlayConfigDetails (2, 2, 48000.0, 64);
        p.prepareToPlay (48000.0, 64);
        juce::AudioBuffer<float> b (2, 64);
        juce::MidiBuffer m;
        for (int i = 0; i < 20; ++i)
        {
            for (int s = 0; s < 64; ++s) { b.setSample (0, s, 0.5f); b.setSample (1, s, 0.5f); }
            p.processBlock (b, m);
        }
        check (std::abs (b.getSample (0, 63) + 0.5f) < 1.0e-4f, "phase invert flips polarity");
    }
    {
        ChannelStripProcessor p;
        setParam (p, ids::mono, 1.0f);
        auto r = run (p, 1000.0f, -6.0f, true);
        check (std::abs (r.rmsL - r.rmsR) < 1.0e-4f && std::abs (db (r.rmsL) - (db (inRms) - 6.02f)) < 0.2f, "mono sums L-only input to both channels at -6 dB");
    }
    {
        ChannelStripProcessor p;
        setParam (p, ids::pan, 100.0f);
        auto r = run (p, 1000.0f, -6.0f);
        check (r.rmsL < 1.0e-4f && r.rmsR > inRms, "hard right pan silences left");
    }
    {
        ChannelStripProcessor p;
        setParam (p, cs::eqId (2, "on").toRawUTF8(), 1.0f);
        setParam (p, cs::eqId (2, "freq").toRawUTF8(), 1000.0f);
        setParam (p, cs::eqId (2, "gain").toRawUTF8(), 12.0f);
        auto r = run (p, 1000.0f, -30.0f);
        const float gainDb = db (r.rmsL) - db (juce::Decibels::decibelsToGain (-30.0f) / std::sqrt (2.0f));
        check (std::abs (gainDb - 12.0f) < 0.3f, "EQ bell +12 dB measures " + juce::String (gainDb, 2) + " dB");
    }
    {
        ChannelStripProcessor p;
        setParam (p, cs::eqId (0, "on").toRawUTF8(), 1.0f);
        setParam (p, cs::eqId (0, "freq").toRawUTF8(), 1000.0f);
        auto r = run (p, 100.0f, -6.0f);
        check (db (r.rmsL) < db (inRms) - 30.0f, "EQ high-pass at 1 kHz attenuates 100 Hz by >30 dB");
    }
    {
        ChannelStripProcessor p;
        setParam (p, ids::compOn, 1.0f);
        setParam (p, ids::fetIn, 20.0f);
        setParam (p, ids::fetOut, 0.0f);
        auto r = run (p, 1000.0f, -6.0f);
        const float gr = p.compGr.load();
        check (r.finite && gr > 3.0f, "FET compressor reports " + juce::String (gr, 1) + " dB gain reduction");
        setParam (p, ids::fetRatio, 4.0f);
        auto r2 = run (p, 1000.0f, -6.0f);
        check (r2.finite && r2.peakL < 1.5f, "FET 'all buttons' stays finite and bounded");
    }
    {
        ChannelStripProcessor p;
        setParam (p, ids::compOn, 1.0f);
        setParam (p, ids::compMode, 1.0f);
        setParam (p, ids::laPeak, 60.0f);
        setParam (p, ids::laGain, 0.0f);
        auto r = run (p, 1000.0f, -6.0f);
        const float gr = p.compGr.load();
        check (r.finite && gr > 1.0f && db (r.rmsL) < db (inRms) - 1.0f, "Opto compressor reports " + juce::String (gr, 1) + " dB gain reduction");
    }
    {
        ChannelStripProcessor p;
        setParam (p, ids::compOn, 1.0f);
        setParam (p, ids::fetIn, 20.0f);
        setParam (p, ids::compMix, 0.0f);
        auto r = run (p, 1000.0f, -6.0f);
        check (std::abs (db (r.rmsL) - db (inRms)) < 0.1f, "compressor mix 0% passes dry signal");
    }
    {
        ChannelStripProcessor p;
        setParam (p, ids::deessOn, 1.0f);
        setParam (p, ids::deessFreq, 7000.0f);
        setParam (p, ids::deessThresh, -40.0f);
        setParam (p, ids::deessRange, 12.0f);
        auto hi = run (p, 7000.0f, -10.0f);
        const float inHi = juce::Decibels::decibelsToGain (-10.0f) / std::sqrt (2.0f);
        const float reduction = db (inHi) - db (hi.rmsL);
        check (reduction > 5.0f && reduction < 14.0f, "de-esser reduces 7 kHz by " + juce::String (reduction, 1) + " dB");
        auto lo = run (p, 300.0f, -10.0f);
        check (std::abs (db (lo.rmsL) - db (inHi)) < 0.7f, "de-esser leaves 300 Hz alone");
    }
    {
        ChannelStripProcessor p;
        auto r = run (p, 1000.0f, -6.0f, false, 44100.0);
        check (r.finite, "runs at 44.1 kHz");
        auto r2 = run (p, 1000.0f, -6.0f, false, 96000.0);
        check (r2.finite, "runs at 96 kHz");
    }

    // Headless editor snapshot
    {
        ChannelStripProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        setParam (p, ids::compOn, 1.0f);
        setParam (p, ids::deessOn, 1.0f);
        setParam (p, cs::eqId (1, "on").toRawUTF8(), 1.0f);
        setParam (p, cs::eqId (1, "gain").toRawUTF8(), 4.0f);
        setParam (p, cs::eqId (2, "on").toRawUTF8(), 1.0f);
        setParam (p, cs::eqId (2, "freq").toRawUTF8(), 350.0f);
        setParam (p, cs::eqId (2, "gain").toRawUTF8(), -3.5f);
        setParam (p, cs::eqId (4, "on").toRawUTF8(), 1.0f);
        setParam (p, cs::eqId (4, "freq").toRawUTF8(), 4500.0f);
        setParam (p, cs::eqId (4, "gain").toRawUTF8(), 5.0f);
        setParam (p, cs::eqId (5, "on").toRawUTF8(), 1.0f);
        setParam (p, cs::eqId (5, "gain").toRawUTF8(), 2.5f);

        // pink-ish noise through the strip so the analyser and meters have content
        juce::Random rnd (1);
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer m;
        for (int blk = 0; blk < 40; ++blk)
        {
            float lp = 0.0f;
            for (int i = 0; i < 512; ++i)
            {
                lp += 0.15f * (rnd.nextFloat() * 2.0f - 1.0f - lp);
                const float s = 0.8f * lp + 0.02f * (rnd.nextFloat() - 0.5f);
                b.setSample (0, i, s);
                b.setSample (1, i, s);
            }
            p.processBlock (b, m);
        }

        const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : ".");
        for (int mode = 0; mode < 2; ++mode)
        {
            setParam (p, ids::compMode, (float) mode);
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            ed->setBounds (0, 0, 1100, 780);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            const auto file = dir.getChildFile (mode == 0 ? "editor_fet.png" : "editor_opto.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat png;
            check (out.openedOk() && png.writeImageToStream (img, out), "wrote " + file.getFullPathName());
        }
    }

    std::printf ("%d failure(s)\n", failures);
    return failures;
}
