// Offline checks for the channel strip DSP, plus a headless snapshot of the editor.
// Build with -DCHANNELSTRIP_BUILD_TESTS=ON and run: ChannelStripTests [snapshot_dir]
#include "PluginProcessor.h"
#include "gui/LightningSlider.h"
#include <thread>
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

    // Host tempo hand-off and beat-synced glow
    {
        HostTempo ht;
        TempoSnapshot t;
        t.valid = true; t.playing = true; t.bpm = 120.0; t.ppq = 8.0; t.anchorMs = 1000.0;
        ht.publish (t);
        const auto r = ht.read();
        check (r.valid && r.playing && r.bpm == 120.0 && r.ppq == 8.0, "host tempo snapshot round-trips");
        const auto beats = HostTempo::beatsAt (r, 1250.0);
        check (beats.has_value() && std::abs (*beats - 8.5) < 1.0e-9, "beat position extrapolates from the host anchor (8.5 beats)");
        check (! HostTempo::beatsAt (r, 1000.0 + HostTempo::maxAnchorAgeMs + 1.0).has_value(), "stale host anchor falls back to steady glow");
        check (std::abs (HostTempo::glow (8.0, 120.0) - 1.0f) < 1.0e-5f && HostTempo::glow (8.5, 120.0) < 1.0e-5f, "glow peaks on the beat and is lowest between beats");
        t.playing = false;
        ht.publish (t);
        check (! HostTempo::beatsAt (ht.read(), 1000.0).has_value()
               && HostTempo::glow (std::nullopt, 0.0) == HostTempo::steadyGlow, "stopped transport gives steady glow");
        bool slowEnough = true;
        for (double bpm = 40.0; bpm <= 400.0; bpm += 1.0)
            slowEnough = slowEnough && bpm / 60.0 / HostTempo::beatsPerPulse (bpm) <= 2.4;
        check (slowEnough, "glow pulse never exceeds 2.4 Hz (40-400 BPM)");
    }

    // Lightning slider: binding, direction, defaults, host updates, accessibility
    {
        ChannelStripProcessor p;
        auto& prm = *p.apvts.getParameter (ids::inGain);
        const auto rangeBefore = prm.getNormalisableRange();
        const float defBefore = prm.getDefaultValue();

        LightningSlider sl (prm, p.hostTempo, "INPUT");
        sl.setBounds (0, 0, 96, 236);

        prm.setValueNotifyingHost (prm.convertTo0to1 (12.0f));   // host automation on the message thread
        check (std::abs (sl.getLitFraction() - prm.convertTo0to1 (12.0f)) < 1.0e-6f, "slider follows host automation");

        std::thread host ([&prm] { prm.setValueNotifyingHost (prm.convertTo0to1 (-6.0f)); });
        host.join();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        check (std::abs (sl.getLitFraction() - prm.convertTo0to1 (-6.0f)) < 1.0e-6f, "slider follows a change from another thread");

        sl.beginDrag();
        const float before = prm.convertFrom0to1 (prm.getValue());
        const float litBefore = sl.getLitFraction();
        sl.dragBy (40.0f, false);
        const float down = prm.convertFrom0to1 (prm.getValue());
        const float litDown = sl.getLitFraction();
        sl.dragBy (-40.0f, false);
        const float back = prm.convertFrom0to1 (prm.getValue());
        sl.dragBy (40.0f, true);
        const float fine = prm.convertFrom0to1 (prm.getValue());
        sl.endDrag();
        check (down > before && litDown > litBefore, "dragging down raises the value and lights more");
        check (std::abs (back - before) < 0.02f, "dragging back up dims in reverse to the same value");
        check (fine > back && (fine - back) < 0.15f * (down - before), "Shift-drag moves at about 10 % speed");

        sl.resetToDefault();
        check (std::abs (prm.getValue() - defBefore) < 1.0e-6f, "double-click resets to the parameter default");

        // strikes switch on from the centre outwards, each one fully
        {
            const int n = sl.getNumBolts();
            const int c = n / 2;
            sl.setNormalised (1.0f / (float) n);
            bool centreOnly = sl.boltLevel (c) > 0.999f;
            for (int i = 0; i < n; ++i)
                centreOnly = centreOnly && (i == c || sl.boltLevel (i) < 1.0e-4f);
            sl.setNormalised (3.0f / (float) n);
            const bool three = sl.boltLevel (c - 1) > 0.999f && sl.boltLevel (c + 1) > 0.999f && sl.boltLevel (0) < 1.0e-4f;
            sl.setNormalised (1.0f);
            bool all = true;
            for (int i = 0; i < n; ++i)
                all = all && sl.boltLevel (i) > 0.999f;
            sl.setNormalised (0.0f);
            bool none = true;
            for (int i = 0; i < n; ++i)
                none = none && sl.boltLevel (i) < 1.0e-4f;
            check (n % 2 == 1 && n >= 3 && centreOnly && three && all && none,
                   "strikes activate from the centre outwards (" + juce::String (n) + " strikes)");
            sl.resetToDefault();
        }

        sl.beginDrag();                         // a double-click arrives while the gesture of its second click is open
        sl.dragBy (30.0f, false);
        sl.resetToDefault();
        sl.endDrag();
        check (std::abs (prm.getValue() - defBefore) < 1.0e-6f, "reset inside a drag joins the open gesture");

        const auto rangeAfter = prm.getNormalisableRange();
        check (rangeAfter.start == rangeBefore.start && rangeAfter.end == rangeBefore.end
               && rangeAfter.interval == rangeBefore.interval && prm.getDefaultValue() == defBefore,
               "parameter range and default are unchanged");

        auto handlerOwner = sl.createAccessibilityHandler();   // no native window in this headless test
        auto* handler = handlerOwner.get();
        auto* value = handler != nullptr ? handler->getValueInterface() : nullptr;
        const bool accOk = handler != nullptr && value != nullptr
                           && handler->getRole() == juce::AccessibilityRole::slider
                           && sl.getTitle() == "Input Gain"
                           && value->getRange().getMinimumValue() == -24.0 && value->getRange().getMaximumValue() == 24.0
                           && value->getCurrentValueAsString().contains ("dB");
        check (accOk, "accessible slider: name '" + sl.getTitle() + "', value '" + (value ? value->getCurrentValueAsString() : juce::String()) + "'");
        if (value != nullptr)
            value->setValue (3.0);
        check (std::abs (prm.convertFrom0to1 (prm.getValue()) - 3.0f) < 1.0e-3f, "accessibility value set reaches the parameter");

        TempoSnapshot t;
        t.valid = true; t.playing = true; t.bpm = 100.0; t.ppq = 4.0; t.anchorMs = 5000.0;
        p.hostTempo.publish (t);
        sl.updateGlow (5000.0);
        const float onBeat = sl.getCurrentGlow();
        t.ppq = 4.5; t.anchorMs = 5300.0;                            // next block: half a beat later (300 ms at 100 BPM)
        p.hostTempo.publish (t);
        sl.updateGlow (5300.0);
        const float offBeat = sl.getCurrentGlow();
        t.playing = false;
        p.hostTempo.publish (t);
        sl.updateGlow (5400.0);
        check (onBeat > 0.99f && offBeat < 0.01f && std::abs (sl.getCurrentGlow() - HostTempo::steadyGlow) < 1.0e-6f,
               "slider glow follows host beats and is steady when stopped (" + juce::String (onBeat, 2) + ", "
               + juce::String (offBeat, 2) + ", " + juce::String (sl.getCurrentGlow(), 2) + ")");
    }

    // Analyser: high resolution, fast release
    {
        SpectrumAnalyser ring;
        SpectrumProcessor sp;
        sp.configure (SpectrumProcessor::High, SpectrumProcessor::Fast, 48000.0);
        check (sp.fftSize() == 4096, "High resolution uses a 4096-point FFT at 48 kHz");
        SpectrumProcessor sp96;
        sp96.configure (SpectrumProcessor::High, SpectrumProcessor::Fast, 96000.0);
        check (sp96.fftSize() == 8192, "FFT length doubles at 96 kHz (same time window)");

        juce::AudioBuffer<float> b (2, 512);
        double ph1 = 0.0, ph2 = 0.0;
        auto feed = [&] (float a1, float f1, float a2, float f2)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float s = a1 * (float) std::sin (ph1) + a2 * (float) std::sin (ph2);
                ph1 += 2.0 * juce::MathConstants<double>::pi * f1 / 48000.0;
                ph2 += 2.0 * juce::MathConstants<double>::pi * f2 / 48000.0;
                b.setSample (0, i, s);
                b.setSample (1, i, s);
            }
            ring.push (b);
        };
        for (int i = 0; i < 16; ++i)
            feed (0.5f, 1000.0f, 0.0f, 0.0f);
        check (ring.copyLatest (sp.inputBuffer(), sp.fftSize()), "analyser ring returns the latest window");
        sp.process (16.0);
        const float peak = sp.columnDb (990.0, 1010.0);
        check (std::abs (peak - (-6.02f)) < 1.6f, "1 kHz at -6 dBFS reads " + juce::String (peak, 1) + " dB");

        for (int i = 0; i < 16; ++i)
            feed (0.5f, 100.0f, 0.5f, 140.0f);
        ring.copyLatest (sp.inputBuffer(), sp.fftSize());
        sp.process (16.0);
        const float a = sp.columnDb (98.0, 102.0), mid = sp.columnDb (118.0, 122.0), c = sp.columnDb (138.0, 142.0);
        check (a - mid > 10.0f && c - mid > 10.0f, "High resolution separates 100 Hz and 140 Hz (dip " + juce::String (a - mid, 1) + " dB)");

        auto releaseAfter = [&] (int speed)
        {
            SpectrumProcessor s2;
            s2.configure (SpectrumProcessor::High, speed, 48000.0);
            std::fill (s2.inputBuffer(), s2.inputBuffer() + s2.fftSize(), 0.0f);
            ring.copyLatest (s2.inputBuffer(), s2.fftSize());
            s2.process (16.0);
            const float start = s2.columnDb (98.0, 102.0);
            std::fill (s2.inputBuffer(), s2.inputBuffer() + s2.fftSize(), 0.0f);
            s2.process (100.0);
            return start - s2.columnDb (98.0, 102.0);
        };
        check (releaseAfter (SpectrumProcessor::Fast) > 2.0f * releaseAfter (SpectrumProcessor::Slow), "Fast speed releases quicker than Slow");
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
        dir.createDirectory();
        for (int mode = 0; mode < 2; ++mode)
        {
            setParam (p, ids::compMode, (float) mode);
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            ed->setBounds (0, 0, 1100, 850);
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
