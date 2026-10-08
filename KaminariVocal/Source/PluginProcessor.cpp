#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <BinaryData.h>

KaminariVocalProcessor::KaminariVocalProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "KAMINARI_VOCAL", kvp::createLayout()),
      presets (apvts, juce::String::fromUTF8 (BinaryData::factory_json, BinaryData::factory_jsonSize))
{
    for (auto* prm : getParameters())
        prm->addListener (this);

    // A new instance starts on the "Default" chain preset (a saved session replaces it in setStateInformation).
    presets.loadChainPreset ("Default");
}

KaminariVocalProcessor::~KaminariVocalProcessor()
{
    cancelPendingUpdate();
    for (auto* prm : getParameters())
        prm->removeListener (this);
}

bool KaminariVocalProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    // mono -> mono, mono -> stereo, stereo -> stereo
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void KaminariVocalProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRateHz = sampleRate;
    const int block = juce::jmax (32, samplesPerBlock);
    work.setSize (2, block, false, false, true);
    dryCopy.setSize (2, block, false, false, true);
    soloIn.setSize (2, block, false, false, true);
    scDetector.setSize (1, block, false, false, true);

    tune.prepare (sampleRate);
    compHistory.setHop ((int) std::lround (sampleRate / 375.0));
    deessHistory.setHop ((int) std::lround (sampleRate / 200.0));   // 1200 points = 6 s
    eq.prepare (sampleRate);
    multiband.prepare (sampleRate, block);
    compressor.prepare (sampleRate);
    deesser.prepare (sampleRate);
    resonance.prepare (sampleRate, block);
    readModuleSettings();
    for (int m = 0; m < numModules; ++m)
    {
        moduleFade[(size_t) m].reset (sampleRate, 0.01);
        moduleFade[(size_t) m].setCurrentAndTargetValue (moduleOn[m] ? 1.0f : 0.0f);
        moduleGr[(size_t) m].store (0.0f);
    }

    inGainSmooth.reset (sampleRate, 0.02);
    outGainSmooth.reset (sampleRate, 0.02);
    inGainSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw (kvid::inGain)->load()));
    outGainSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw (kvid::outGain)->load()));
    paramsDirty.store (true);   // settings that depend on the sample rate are read again on the first block
    // Tune's fixed delay is always reported (also with Tune off).
    pendingLatency.store (computeLatency());
    setLatencySamples (pendingLatency.load());
}

int KaminariVocalProcessor::computeLatency() const
{
    // oversampling filters add latency only while their module is on
    auto osLat = [this] (const char* onId, const char* osId, int (*lat) (const KaminariVocalProcessor&, int))
    { return raw (onId)->load() > 0.5f ? lat (*this, juce::roundToInt (raw (osId)->load())) : 0; };
    const int os = osLat ("mb_on", "mb_os", [] (const KaminariVocalProcessor& p, int f) { return p.multiband.latencyFor (f); })
                 + osLat ("rs_on", "rs_os", [] (const KaminariVocalProcessor& p, int f) { return p.resonance.latencyFor (f); });
    return kv::Tune::latencyFor (sampleRateHz) + os;
}

void KaminariVocalProcessor::readEqSettings (kv::EqBandSettings (&out)[kv::Equalizer::numBands]) const
{
    readEqBands ("eq", out);
}

void KaminariVocalProcessor::readEqBands (const juce::String& prefix, kv::EqBandSettings (&out)[kv::Equalizer::numBands]) const
{
    for (int b = 0; b < kv::Equalizer::numBands; ++b)
    {
        const juce::String p = prefix + juce::String (b + 1) + "_";
        auto& e = out[b];
        e.used = raw (p + "used")->load() > 0.5f;
        e.on = raw (p + "on")->load() > 0.5f;
        e.type = juce::roundToInt (raw (p + "type")->load());
        e.freq = raw (p + "freq")->load();
        e.gainDb = raw (p + "gain")->load();
        e.q = raw (p + "q")->load();
        e.slopeIndex = juce::roundToInt (raw (p + "slope")->load());
    }
}

void KaminariVocalProcessor::readModuleSettings()
{
    auto f = [this] (const juce::String& id) { return raw (id)->load(); };
    auto b = [this] (const juce::String& id) { return raw (id)->load() > 0.5f; };
    auto i = [this] (const juce::String& id) { return juce::roundToInt (raw (id)->load()); };

    moduleOn[ModTune] = b ("tn_on");
    moduleOn[ModEq] = b ("eq_on");
    moduleOn[ModMultiband] = b ("mb_on");
    moduleOn[ModCompression] = b ("lv_on");
    moduleOn[ModDeEss] = b ("ds_on");
    moduleOn[ModResonance] = b ("rs_on");

    // Tune: a named scale decides the notes; Custom uses the 12 note switches
    auto& t = tuneSettings;
    t.on = moduleOn[ModTune];
    const int scale = i ("tn_scale");
    if (scale >= 10)
        for (int n = 0; n < 12; ++n) t.notes[n] = b ("tn_note_" + juce::String (n));
    else
        kv::scaleNotes (i ("tn_key"), scale, t.notes);
    t.range = i ("tn_range");
    t.speedMs = f ("tn_speed");
    t.humanize = f ("tn_humanize") / 100.0f;
    t.correct = b ("tn_correct");
    t.detuneCents = f ("tn_detune");
    t.vibOn = b ("tn_vib_on");
    t.vibCents = f ("tn_vib_depth"); t.vibRateHz = f ("tn_vib_rate");
    t.vibDelayMs = f ("tn_vib_delay"); t.vibRiseMs = f ("tn_vib_rise"); t.vibVariation = f ("tn_vib_variation") / 100.0f;
    t.tremOn = b ("tn_trem_on");
    t.tremDepth = f ("tn_trem_depth") / 100.0f; t.tremRateHz = f ("tn_trem_rate");
    t.tremBeats = kvp::tremoloSyncBeats (i ("tn_trem_sync"));
    t.tremShape = i ("tn_trem_shape"); t.tremStereo = f ("tn_trem_stereo") / 180.0f; t.tremOnset = b ("tn_trem_onset");

    readEqSettings (eqSettings);
    eqOutGain = f ("eq_out_gain");

    auto& m = mbSettings;
    m.count = i ("mb_count");
    m.slopeIndex = i ("mb_slope");
    m.smoothDetector = i ("mb_detector") == 1;
    mbOs = i ("mb_os"); rsOs = i ("rs_os");
    for (int k = 0; k < 6; ++k)
    {
        const juce::String p = "mb" + juce::String (k + 1) + "_";
        auto& bs = m.band[k];
        bs.lo = f (p + "lo"); bs.hi = f (p + "hi"); bs.threshDb = f (p + "thresh"); bs.ratio = f (p + "ratio");
        bs.attackMs = f (p + "attack"); bs.releaseMs = f (p + "release"); bs.kneeDb = f (p + "knee");
        bs.rangeDb = f (p + "range"); bs.gainDb = f (p + "gain"); bs.expand = i (p + "mode") == 1; bs.solo = b (p + "solo");
        bs.bypass = b (p + "bypass"); bs.mute = b (p + "mute");
    }

    auto& c = compSettings;
    c.peakReduction = f ("lv_peak");
    c.gainDb = f ("lv_gain");
    readEqBands ("lv_sc", c.scBand);

    dsSettings.freqHz = f ("ds_det_lo");
    dsSettings.rangeDb = f ("ds_range");

    auto& r = rsSettings;
    r.mode = i ("rs_mode"); r.depth = f ("rs_depth"); r.detail = f ("rs_detail") / 100.0f;
    r.attack = f ("rs_attack") / 100.0f; r.release = f ("rs_release") / 100.0f; r.mix = f ("rs_mix") / 100.0f;
    r.outGainDb = f ("rs_out_gain"); r.wetTrimDb = f ("rs_wet_trim"); r.maxCutDb = f ("rs_max_cut");
    r.delta = b ("rs_delta"); r.bypass = b ("rs_bypass"); r.quality = i ("rs_quality"); r.stereoMode = i ("rs_stereo_mode");
    r.link = f ("rs_link") / 100.0f; r.focus = f ("rs_focus") / 100.0f;
    r.detailTiltLo = f ("rs_detail_tilt_lo"); r.detailTiltHi = f ("rs_detail_tilt_hi");
    r.attackTiltLo = f ("rs_attack_tilt_lo"); r.attackTiltHi = f ("rs_attack_tilt_hi");
    r.releaseTiltLo = f ("rs_release_tilt_lo"); r.releaseTiltHi = f ("rs_release_tilt_hi");
    for (int k = 0; k < 8; ++k)
    {
        const juce::String p = "rs_b" + juce::String (k + 1) + "_";
        auto& cb = r.curve[k];
        cb.used = b (p + "used"); cb.on = b (p + "on"); cb.shape = i (p + "shape");
        cb.freq = f (p + "freq"); cb.depthDb = f (p + "depth"); cb.q = f (p + "q");
    }
}

void KaminariVocalProcessor::processModules (float* l, float* r, int n)
{
    tuneSettings.playing = chunkPlaying;
    tuneSettings.ppq = chunkPpq;
    tuneSettings.bpm = chunkBpm;
    tune.process (l, r, n, tuneSettings);   // always runs: its fixed delay is part of the reported latency
    analyserPre.push (l, r, n);

    // Every module except Tune crossfades 10 ms against its input when switched on or off, and is skipped while off.
    auto crossfaded = [&] (int m, auto&& run)
    {
        auto& fade = moduleFade[(size_t) m];
        fade.setTargetValue (moduleOn[m] ? 1.0f : 0.0f);
        if (! fade.isSmoothing() && fade.getTargetValue() <= 0.0f)
        {
            moduleGr[(size_t) m].store (0.0f);
            return false;
        }
        dryCopy.copyFrom (0, 0, l, n);
        dryCopy.copyFrom (1, 0, r, n);
        run();
        if (fade.isSmoothing() || fade.getCurrentValue() < 1.0f)
            for (int i = 0; i < n; ++i)
            {
                const float g = fade.getNextValue();
                l[i] = dryCopy.getSample (0, i) + (l[i] - dryCopy.getSample (0, i)) * g;
                r[i] = dryCopy.getSample (1, i) + (r[i] - dryCopy.getSample (1, i)) * g;
            }
        return true;
    };

    const int solo = eqSolo.load();
    const bool soloing = solo >= 0 && solo < kv::Equalizer::numBands && moduleOn[ModEq];
    if (soloing)
    {
        soloIn.copyFrom (0, 0, l, n);   // the audition is taken from the EQ's input (a cut has removed that region after it)
        soloIn.copyFrom (1, 0, r, n);
    }
    crossfaded (ModEq, [&] { eq.process (l, r, n, eqSettings, eqOutGain); });
    if (soloing)
    {
        setSoloFilter (soloBp, eqSettings[solo], sampleRateHz);
        for (int i = 0; i < n; ++i) { l[i] = soloBp[0].process (soloIn.getSample (0, i)); r[i] = soloBp[1].process (soloIn.getSample (1, i)); }
    }
    analyserPost.push (l, r, n);
    crossfaded (ModMultiband, [&]
    {
        mbAnalyserPre.push (l, r, n);
        moduleGr[ModMultiband].store (multiband.process (l, r, n, mbOs, [&] (int) { return mbSettings; }));
        mbAnalyserPost.push (l, r, n);
    });
    for (int k = 0; k < 6; ++k)
        mbBandChange[(size_t) k].store (moduleOn[ModMultiband] && k < mbSettings.count ? multiband.active().bandChange (k) : 0.0f);

    // Compression: the level display compares the compressor's input with its output
    soloIn.copyFrom (0, 0, l, n);
    soloIn.copyFrom (1, 0, r, n);
    const bool compRan = crossfaded (ModCompression, [&]
    {
        moduleGr[ModCompression].store (compressor.process (l, r, n, compSettings, scDetector.getWritePointer (0)));
    });
    if (compRan)
    {
        compInAnalyser.push (soloIn.getReadPointer (0), soloIn.getReadPointer (1), n);
        compScAnalyser.push (scDetector.getReadPointer (0), scDetector.getReadPointer (0), n);
        // side-chain EQ band solo: hear the region that band works on (taken from the compressor's input)
        const int so = scEqSolo.load();
        if (so >= 0 && so < kv::CompressorSettings::numScBands && compSettings.scBand[so].used && moduleOn[ModCompression])
        {
            setSoloFilter (scSoloBp, compSettings.scBand[so], sampleRateHz);
            for (int i = 0; i < n; ++i) { l[i] = scSoloBp[0].process (soloIn.getSample (0, i)); r[i] = scSoloBp[1].process (soloIn.getSample (1, i)); }
        }
    }
    else
        compressor.reset();   // switched on again: start from a released cell
    compHistory.push (soloIn.getReadPointer (0), soloIn.getReadPointer (1), l, r, n);

    soloIn.copyFrom (0, 0, l, n);
    soloIn.copyFrom (1, 0, r, n);
    if (! crossfaded (ModDeEss, [&] { moduleGr[ModDeEss].store (deesser.process (l, r, n, dsSettings)); }))
        deesser.reset();
    deessHistory.noteReduction (moduleOn[ModDeEss] ? moduleGr[ModDeEss].load() : 0.0f);
    deessHistory.push (soloIn.getReadPointer (0), soloIn.getReadPointer (1), l, r, n);
    crossfaded (ModResonance, [&]
    {
        rsAnalyserPre.push (l, r, n);
        moduleGr[ModResonance].store (resonance.process (l, r, n, rsOs, [&] (int) { return rsSettings; }));
        rsAnalyserPost.push (l, r, n);
    });
}

void KaminariVocalProcessor::setSoloFilter (kv::Biquad (&bp)[2], const kv::EqBandSettings& bs, double fs)
{
    auto kind = kv::Biquad::BandPass;
    float q = std::max (0.3f, bs.q);
    if (bs.type == kv::LowShelf || bs.type == kv::LowCut)        { kind = kv::Biquad::LowPass;  q = 0.7071f; }
    else if (bs.type == kv::HighShelf || bs.type == kv::HighCut) { kind = kv::Biquad::HighPass; q = 0.7071f; }
    else if (bs.type == kv::TiltShelf || bs.type == kv::FlatTilt) q = 0.5f;
    for (auto& b : bp) b.set (kind, (float) fs, bs.freq, q);
}

void KaminariVocalProcessor::readAllSettings (double bpm)
{
    settingsBpm = bpm;
    readModuleSettings();
    const int latencyNow = computeLatency();
    if (latencyNow != pendingLatency.load())
    {
        pendingLatency.store (latencyNow);
        triggerAsyncUpdate();   // hosts expect setLatencySamples from the message thread
    }
    inGainSmooth.setTargetValue (juce::Decibels::decibelsToGain (raw (kvid::inGain)->load()));
    outGainSmooth.setTargetValue (juce::Decibels::decibelsToGain (raw (kvid::outGain)->load()));
}

void KaminariVocalProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    double bpm = 120.0;
    publishTempo (bpm);
    const auto transport = hostTempo.read();

    const int total = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    // a mono input feeding a stereo output is copied to both channels (dual mono)
    if (numIn == 1 && numOut >= 2 && buffer.getNumChannels() >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, total);
    else
        for (int c = numIn; c < numOut; ++c)
            buffer.clear (c, 0, total);
    const int chs = juce::jmin (numOut, buffer.getNumChannels(), 2);
    if (chs == 0 || total == 0)
        return;

    if (paramsDirty.exchange (false) || std::abs (bpm - settingsBpm) > 1.0e-9)
        readAllSettings (bpm);

    inPeak.store (buffer.getMagnitude (0, total));
    for (int c = 0; c < 2; ++c)
        inPeakCh[(size_t) c].store (buffer.getMagnitude (juce::jmin (c, chs - 1), 0, total));

    const int capacity = work.getNumSamples();
    for (int start = 0; start < total; start += capacity)
    {
        const int n = juce::jmin (capacity, total - start);
        chunkBpm = bpm;
        chunkPlaying = transport.valid && transport.playing;
        chunkPpq = transport.ppq + (double) start * bpm / 60.0 / sampleRateHz;
        float* out2[2] = { buffer.getWritePointer (0, start), buffer.getWritePointer (chs > 1 ? 1 : 0, start) };
        float* io[2] = { work.getWritePointer (0), work.getWritePointer (1) };

        for (int i = 0; i < n; ++i)
        {
            const float gi = inGainSmooth.getNextValue();
            io[0][i] = out2[0][i] * gi;
            io[1][i] = out2[chs > 1 ? 1 : 0][i] * gi;
        }
        processModules (io[0], io[1], n);
        // output gain. A mono output receives the average of both channels.
        for (int i = 0; i < n; ++i)
        {
            const float go = outGainSmooth.getNextValue();
            const float yl = io[0][i] * go, yr = io[1][i] * go;
            if (chs == 2) { out2[0][i] = yl; out2[1][i] = yr; }
            else          out2[0][i] = 0.5f * (yl + yr);
        }
    }

    outPeak.store (buffer.getMagnitude (0, total));
    for (int c = 0; c < 2; ++c)
        outPeakCh[(size_t) c].store (buffer.getMagnitude (juce::jmin (c, chs - 1), 0, total));
}

void KaminariVocalProcessor::publishTempo (double& bpmOut)
{
    TempoSnapshot t;
    t.anchorMs = juce::Time::getMillisecondCounterHiRes();
    if (auto* ph = getPlayHead())
    {
        if (const auto pos = ph->getPosition())
        {
            const auto bpm = pos->getBpm();
            const auto ppq = pos->getPpqPosition();
            t.playing = pos->getIsPlaying();
            if (bpm.hasValue() && *bpm > 0.0)
                bpmOut = *bpm;
            if (bpm.hasValue() && ppq.hasValue() && *bpm > 0.0)
            {
                t.valid = true;
                t.bpm = *bpm;
                t.ppq = *ppq;
            }
        }
    }
    hostTempo.publish (t);
}

void KaminariVocalProcessor::selectAB (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == abSlot)
        return;
    abState[abSlot] = apvts.copyState();                // keep the slot being left
    if (abState[slot].isValid())
        apvts.replaceState (abState[slot].createCopy());
    abSlot = slot;
    paramsDirty.store (true);
}

void KaminariVocalProcessor::copyAToB()
{
    // copies the live settings into the other slot
    abState[1 - abSlot] = apvts.copyState();
}

juce::AudioProcessorEditor* KaminariVocalProcessor::createEditor()
{
    return new KaminariVocalEditor (*this);
}

void KaminariVocalProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("ui_view", advancedView.load() ? "advanced" : "basic", nullptr);
    state.setProperty ("ui_tab", advancedTab.load(), nullptr);
    state.setProperty ("ui_scale", uiScale.load(), nullptr);
    state.setProperty ("ui_animations", hostTempo.animations.load(), nullptr);
    state.setProperty ("analyser_mode", analyserMode.load(), nullptr);
    state.setProperty ("analyser_resolution", analyserResolution.load(), nullptr);
    state.setProperty ("analyser_speed", analyserSpeed.load(), nullptr);
    state.setProperty ("state_version", stateVersion, nullptr);
    for (auto* m : { "tune", "eq", "multiband", "compression", "deess", "resonance" })
        state.setProperty (juce::String ("engine_") + m, engineVersion, nullptr);
    presets.writeState (state);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void KaminariVocalProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            // Sessions saved before versioning have no state_version (= 1). Future conversions go here.
            loadedStateVersion = (int) state.getProperty ("state_version", 1);
            advancedView.store (state.getProperty ("ui_view", "basic").toString() == "advanced");
            // Alt sessions start at version 5. A session from the full edition keeps its tab only when it is a module this
            // edition has: its tab is first brought to the full edition's v4 order (0..3 Tune..Compression, 4 Flanger,
            // 5 Distortion, 6 De-ess, 7 Resonance, 8 Sends), then mapped; Flanger, Distortion and Sends open on Tune.
            int tab = (int) state.getProperty ("ui_tab", 0);
            if (loadedStateVersion < 5)
            {
                if (loadedStateVersion < 3 && tab >= 4)
                    tab = tab >= 6 ? 7 : tab + 1;   // v3: Distortion tab inserted before De-ess; sends were 6..8
                if (loadedStateVersion < 4 && tab >= 4)
                    tab += 1;                       // v4: Flanger tab inserted before Distortion
                tab = tab <= 3 ? tab : (tab == 6 ? (int) ModDeEss : (tab == 7 ? (int) ModResonance : (int) ModTune));
            }
            advancedTab.store (juce::jlimit (0, (int) numModules - 1, tab));
            uiScale.store (juce::jlimit (0.75f, 2.0f, (float) state.getProperty ("ui_scale", 1.0f)));
            hostTempo.animations.store ((bool) state.getProperty ("ui_animations", true));
            analyserMode.store (juce::jlimit (0, 3, (int) state.getProperty ("analyser_mode", 3)));
            analyserResolution.store (juce::jlimit (0, 3, (int) state.getProperty ("analyser_resolution", (int) SpectrumProcessor::High)));
            analyserSpeed.store (juce::jlimit (0, 4, (int) state.getProperty ("analyser_speed", (int) SpectrumProcessor::Fast)));
            apvts.replaceState (state);
            presets.readState (state);
            paramsDirty.store (true);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KaminariVocalProcessor();
}
