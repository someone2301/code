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
    sendPtrs[Reverb]  = { raw (kvid::rvOn), raw (kvid::rvSend), raw (kvid::rvTap) };
    sendPtrs[Delay]   = { raw (kvid::dlOn), raw (kvid::dlSend), raw (kvid::dlTap) };
    sendPtrs[Widener] = { raw (kvid::wdOn), raw (kvid::wdSend), raw (kvid::wdTap) };

    // A new instance starts on the "Default" chain preset (a saved session replaces it in setStateInformation).
    presets.loadChainPreset ("Default");
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
    preTap.setSize (2, block, false, false, true);
    sendIn.setSize (2, block, false, false, true);
    sendOut.setSize (2, block, false, false, true);
    returns.setSize (2, block, false, false, true);
    work.setSize (2, block, false, false, true);
    dryCopy.setSize (2, block, false, false, true);
    soloIn.setSize (2, block, false, false, true);
    scDetector.setSize (1, block, false, false, true);
    routeBuf.setSize (2, block, false, false, true);
    for (auto& f : feedBuf) f.setSize (2, block, false, false, true);
    retSoloIn.setSize (2, block, false, false, true);

    tune.prepare (sampleRate);
    compHistory.setHop ((int) std::lround (sampleRate / 375.0));
    deessHistory.setHop ((int) std::lround (sampleRate / 200.0));   // 1200 points = 6 s
    eq.prepare (sampleRate);
    multiband.prepare (sampleRate, block);
    compressor.prepare (sampleRate);
    deesser.prepare (sampleRate, block);
    resonance.prepare (sampleRate, block);
    distortion.prepare (sampleRate, block);
    distortionIdle = true;
    readModuleSettings();
    for (int m = 0; m < numModules; ++m)
    {
        moduleFade[(size_t) m].reset (sampleRate, 0.01);
        moduleFade[(size_t) m].setCurrentAndTargetValue (moduleOn[m] ? 1.0f : 0.0f);
        moduleGr[(size_t) m].store (0.0f);
    }

    reverb.prepare (sampleRate, block);
    delay.prepare (sampleRate, block);
    widener.prepare (sampleRate, block);
    for (int k = 0; k < 2; ++k)
    {
        returnEq[k].prepare (sampleRate);
        ducker[k].prepare (sampleRate);
        duckGr[(size_t) k].store (0.0f);
    }
    flanger.prepare (sampleRate, block);

    inGainSmooth.reset (sampleRate, 0.02);
    outGainSmooth.reset (sampleRate, 0.02);
    inGainSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw (kvid::inGain)->load()));
    outGainSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw (kvid::outGain)->load()));
    for (int s = 0; s < numSends; ++s)
    {
        sendSmooth[(size_t) s].reset (sampleRate, 0.02);
        const bool on = sendPtrs[(size_t) s].on->load() > 0.5f;
        sendSmooth[(size_t) s].setCurrentAndTargetValue (on ? kvp::sendGain (sendPtrs[(size_t) s].level->load()) : 0.0f);
        returnFade[(size_t) s].reset (sampleRate, 0.01);
        returnFade[(size_t) s].setCurrentAndTargetValue (on ? 1.0f : 0.0f);
        idle[(size_t) s] = true;
        returnPeak[(size_t) s].store (0.0f);
        returnRms[(size_t) s].store (0.0f);
    }
    // Tune's fixed delay is always reported (also with Tune off); the sends add none.
    pendingLatency.store (computeLatency());
    setLatencySamples (pendingLatency.load());
}

int KaminariVocalProcessor::computeLatency() const
{
    const double fs = sampleRateHz;
    const int la = (int) std::lround (raw ("lv_lookahead")->load() * 0.001 * fs)
                 + (int) std::lround (raw ("ds_lookahead")->load() * 0.001 * fs);
    // Distortion's oversampling filters add latency only while the module is on
    // oversampling filters add latency only while their module is on
    auto osLat = [this] (const char* onId, const char* osId, int (*lat) (const KaminariVocalProcessor&, int))
    { return raw (onId)->load() > 0.5f ? lat (*this, juce::roundToInt (raw (osId)->load())) : 0; };
    const int os = osLat ("dt_on", "dt_os", [] (const KaminariVocalProcessor& p, int f) { return p.distortion.latencyFor (f); })
                 + osLat ("mb_on", "mb_os", [] (const KaminariVocalProcessor& p, int f) { return p.multiband.latencyFor (f); })
                 + osLat ("ds_on", "ds_os", [] (const KaminariVocalProcessor& p, int f) { return p.deesser.latencyFor (f); })
                 + osLat ("rs_on", "rs_os", [] (const KaminariVocalProcessor& p, int f) { return p.resonance.latencyFor (f); });
    return kv::Tune::latencyFor (fs) + la + os;
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
    moduleOn[ModDistortion] = b ("dt_on");
    moduleOn[ModFlanger] = b ("fl_on");

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

    auto& dt = dtSettings;
    dt.on = moduleOn[ModDistortion];
    dt.style = i ("dt_style"); dt.driveDb = f ("dt_drive"); dt.tone = f ("dt_tone") / 100.0f; dt.bias = f ("dt_bias") / 100.0f;
    dt.lowCutHz = f ("dt_lowcut"); dt.crush = f ("dt_crush") / 100.0f; dt.mix = f ("dt_mix") / 100.0f; dt.outDb = f ("dt_out");
    dt.autoGain = b ("dt_auto_gain"); dt.oversampling = i ("dt_os");

    readEqSettings (eqSettings);
    eqOutGain = f ("eq_out_gain");

    auto& m = mbSettings;
    m.count = i ("mb_count");
    m.slopeIndex = i ("mb_slope");
    m.smoothDetector = i ("mb_detector") == 1;
    mbOs = i ("mb_os"); dsOs = i ("ds_os"); rsOs = i ("rs_os");
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
    c = {};
    c.lookaheadSamples = (int) std::lround (f ("lv_lookahead") * 0.001 * sampleRateHz);
    if (moduleOn[ModCompression])
    {
        c.style = i ("lv_style"); c.threshDb = f ("lv_thresh"); c.ratio = f ("lv_ratio"); c.attackMs = f ("lv_attack");
        c.releaseMs = f ("lv_release"); c.autoRelease = b ("lv_auto_release"); c.kneeDb = f ("lv_knee"); c.rangeDb = f ("lv_range");
        c.holdMs = f ("lv_hold"); c.smoothDetector = i ("lv_detector") == 1; c.mix = f ("lv_mix") / 100.0f;
        c.wetGainDb = f ("lv_wet_gain"); c.dryDb = f ("lv_dry"); c.scLevelDb = f ("lv_sc_level");
        c.stereoLink = f ("lv_stereo_link") / 100.0f; c.outGainDb = f ("lv_out_gain"); c.autoGain = b ("lv_auto_gain");
    }
    else
    {
        // off: no reduction or gain change, but the lookahead delay stays so the latency does not change
        c.threshDb = 0; c.rangeDb = 0; c.autoGain = false;
    }
    readEqBands ("lv_sc", c.scBand);

    auto& d = dsSettings;
    d = {};
    d.lookaheadSamples = (int) std::lround (f ("ds_lookahead") * 0.001 * sampleRateHz);
    d.rangeDb = 0;
    if (moduleOn[ModDeEss])
    {
        d.threshDb = f ("ds_thresh"); d.rangeDb = f ("ds_range"); d.detLo = f ("ds_det_lo"); d.detHi = f ("ds_det_hi");
        d.fullBand = i ("ds_detect") == 1; d.wideband = i ("ds_process") == 1;
        d.stereoLink = f ("ds_stereo_link") / 100.0f; d.linkMode = i ("ds_link_mode");
        d.listen = b ("ds_listen"); d.audition = b ("ds_audition_trigger");
    }

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

    // EQ, Multiband and Resonance crossfade 10 ms against their input when switched on or off
    auto crossfaded = [&] (int m, auto&& run)
    {
        auto& fade = moduleFade[(size_t) m];
        fade.setTargetValue (moduleOn[m] ? 1.0f : 0.0f);
        if (! fade.isSmoothing() && fade.getTargetValue() <= 0.0f)
        {
            moduleGr[(size_t) m].store (0.0f);
            return;
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
    // Compression and De-ess always run (their lookahead delay must stay in the path); "off" means neutral settings.
    dryCopy.copyFrom (0, 0, l, n);
    dryCopy.copyFrom (1, 0, r, n);
    moduleGr[ModCompression].store (compressor.process (l, r, n, compSettings, scDetector.getWritePointer (0)));
    {
        // side-chain EQ band solo: hear the region that band works on (taken from the compressor's input)
        const int so = scEqSolo.load();
        if (so >= 0 && so < kv::CompressorSettings::numScBands && compSettings.scBand[so].used && moduleOn[ModCompression])
        {
            setSoloFilter (scSoloBp, compSettings.scBand[so], sampleRateHz);
            for (int i = 0; i < n; ++i) { l[i] = scSoloBp[0].process (dryCopy.getSample (0, i)); r[i] = scSoloBp[1].process (dryCopy.getSample (1, i)); }
        }
    }
    compInAnalyser.push (dryCopy.getReadPointer (0), dryCopy.getReadPointer (1), n);
    compScAnalyser.push (scDetector.getReadPointer (0), scDetector.getReadPointer (0), n);
    compMakeup.store (compressor.currentMakeup());
    compHistory.push (dryCopy.getReadPointer (0), dryCopy.getReadPointer (1), l, r, n);
    // Flanger: no latency; resets when switched on again so no old sweep tail plays
    {
        auto& fade = moduleFade[ModFlanger];
        const bool active = moduleOn[ModFlanger] || fade.isSmoothing() || fade.getCurrentValue() > 0.0f;
        if (active && flangerIdle) flanger.reset();
        flangerIdle = ! active;
        crossfaded (ModFlanger, [&] { flanger.process (l, r, l, r, n, flSettings); moduleGr[ModFlanger].store (flanger.lfoNow.load()); });
        if (moduleOn[ModFlanger] || fade.isSmoothing()) flangerScope.push (l, r, n);
    }
    // Distortion: skipped entirely while off (no latency); restarts from a clean state
    {
        auto& fade = moduleFade[ModDistortion];
        const bool active = moduleOn[ModDistortion] || fade.isSmoothing() || fade.getCurrentValue() > 0.0f;
        if (active && distortionIdle) distortion.reset();
        distortionIdle = ! active;
        crossfaded (ModDistortion, [&] { distortion.process (l, r, n, dtSettings); moduleGr[ModDistortion].store (distortion.peakOver.load()); });
    }
    dryCopy.copyFrom (0, 0, l, n);
    dryCopy.copyFrom (1, 0, r, n);
    // oversampled only while on (off it runs neutral at the session rate, keeping only its lookahead delay)
    moduleGr[ModDeEss].store (deesser.process (l, r, n, moduleOn[ModDeEss] ? dsOs : 0, [&] (int factor)
    {
        auto d = dsSettings;
        d.lookaheadSamples *= factor;
        return d;
    }));
    deessHistory.push (dryCopy.getReadPointer (0), dryCopy.getReadPointer (1), l, r, n);
    crossfaded (ModResonance, [&]
    {
        rsAnalyserPre.push (l, r, n);
        moduleGr[ModResonance].store (resonance.process (l, r, n, rsOs, [&] (int) { return rsSettings; }));
        rsAnalyserPost.push (l, r, n);
    });
}

void KaminariVocalProcessor::resetSend (int s)
{
    if (s == Reverb)  reverb.reset();
    if (s == Delay)   delay.reset();
    if (s == Reverb || s == Delay)
    {
        returnEq[s == Delay ? 1 : 0].reset();
        ducker[s == Delay ? 1 : 0].reset();
        duckGr[s == Delay ? 1u : 0u].store (0.0f);
    }
    if (s == Widener) widener.reset();
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

kv::DuckSettings KaminariVocalProcessor::readDuck (int r) const
{
    const juce::String p = r == 0 ? "rv_" : "dl_";
    kv::DuckSettings d;
    d.on = raw (p + "duck_on")->load() > 0.5f;
    d.threshDb = raw (p + "duck_thresh")->load();
    d.depthDb = raw (p + "duck_depth")->load();
    d.attackMs = raw (p + "duck_attack")->load();
    d.releaseMs = raw (p + "duck_release")->load();
    d.source = juce::roundToInt (raw (p + "duck_source")->load());
    d.wetGainDb = raw (p + "wet_gain")->load();
    return d;
}

float KaminariVocalProcessor::delayTimeSamples (int echo, double bpm) const
{
    const bool second = echo == 2;
    const int unit = juce::roundToInt (raw (second ? kvid::dlT2Unit : kvid::dlT1Unit)->load());
    if (unit == 0)
        return raw (second ? kvid::dlT2Ms : kvid::dlT1Ms)->load() * 0.001f * (float) sampleRateHz;
    const int note = juce::roundToInt (raw (second ? kvid::dlT2Note : kvid::dlT1Note)->load());
    const double beats = kvp::noteBeats (note, unit);
    return (float) (beats * 60.0 / juce::jlimit (20.0, 400.0, bpm) * sampleRateHz);
}

kv::ReverbSettings KaminariVocalProcessor::readReverb() const
{
    kv::ReverbSettings s;
    s.mode = juce::roundToInt (raw (kvid::rvMode)->load());
    s.decaySec = raw (kvid::rvDecay)->load();
    s.size = raw (kvid::rvSize)->load() / 100.0f;
    s.preDelayMs = raw (kvid::rvPreDelay)->load();
    s.hiCutHz = raw (kvid::rvHiCut)->load();
    s.loCutHz = raw (kvid::rvLoCut)->load();
    s.modRateHz = raw (kvid::rvModRate)->load();
    s.modDepth = raw (kvid::rvModDepth)->load() / 100.0f;
    s.density = raw (kvid::rvDensity)->load() / 100.0f;
    s.attack = raw (kvid::rvAttack)->load() / 100.0f;
    return s;
}

kv::DelaySettings KaminariVocalProcessor::readDelay (double bpm) const
{
    kv::DelaySettings s;
    s.mode = juce::roundToInt (raw (kvid::dlMode)->load());
    s.style = juce::roundToInt (raw (kvid::dlStyle)->load());
    s.time1Samples = delayTimeSamples (1, bpm);
    s.time2Samples = delayTimeSamples (2, bpm);
    s.feedback = raw (kvid::dlFeedback)->load() / 100.0f;
    s.loCutHz = raw (kvid::dlLoCut)->load();
    s.hiCutHz = raw (kvid::dlHiCut)->load();
    s.saturation = raw (kvid::dlSaturation)->load() / 100.0f;
    s.width = raw (kvid::dlWidth)->load() / 100.0f;
    s.offsetMs = raw (kvid::dlOffset)->load();
    s.accent1 = raw (kvid::dlAccent)->load() / 100.0f;
    s.accent2 = raw (kvid::dlAccent2)->load() / 100.0f;
    s.balance = raw (kvid::dlBalance)->load() / 100.0f;
    s.fbMix = raw (kvid::dlFbMix)->load() / 100.0f;
    s.fbBalance = raw (kvid::dlFbBal)->load() / 100.0f;
    s.groove = raw (kvid::dlGroove)->load() / 100.0f;
    s.feelMs = raw (kvid::dlFeel)->load();
    s.primeNumbers = raw (kvid::dlPrime)->load() > 0.5f;
    s.wobble = raw (kvid::dlWobble)->load() / 100.0f;
    s.wobbleRateHz = raw (kvid::dlWobbleRate)->load();
    s.wobbleShape = juce::roundToInt (raw (kvid::dlWobbleShape)->load());
    s.wobbleSync = raw (kvid::dlWobbleSync)->load() / 100.0f;
    s.diffusion = raw (kvid::dlDiffusion)->load() / 100.0f;
    s.diffusionSize = raw (kvid::dlDiffSize)->load() / 100.0f;
    s.diffusionInLoop = raw (kvid::dlDiffLoop)->load() > 0.5f;
    return s;
}

kv::FlangerSettings KaminariVocalProcessor::readFlanger (double bpm) const
{
    kv::FlangerSettings s;
    s.rateHz = raw (kvid::flRate)->load();
    s.syncBeats = kvp::flangerSyncBeats (juce::roundToInt (raw (kvid::flSync)->load()));
    s.depth = raw (kvid::flDepth)->load() / 100.0f;
    s.delayMs = raw (kvid::flDelay)->load();
    s.feedback = raw (kvid::flFeedback)->load() / 100.0f;
    s.stereo = raw (kvid::flStereo)->load() / 180.0f;
    s.shape = juce::roundToInt (raw (kvid::flShape)->load());
    s.hiCutHz = raw (kvid::flHiCut)->load();
    s.mix = raw (kvid::flMix)->load() / 100.0f;
    s.bpm = bpm;
    return s;
}

kv::WidenerSettings KaminariVocalProcessor::readWidener() const
{
    kv::WidenerSettings s;
    s.type = juce::roundToInt (raw (kvid::wdType)->load());
    s.msStyle = juce::roundToInt (raw (kvid::msStyle)->load());
    s.msDetune = raw (kvid::msDetune)->load() / 100.0f;
    s.msDelay = raw (kvid::msDelay)->load() / 100.0f;
    s.msFocusHz = raw (kvid::msFocus)->load();
    s.swWidth = raw (kvid::swWidth)->load() / 100.0f;
    s.swMode = juce::roundToInt (raw (kvid::swMode)->load());
    s.swTone = raw (kvid::swTone)->load() / 100.0f;
    s.swOutputDb = raw (kvid::swOutput)->load();
    return s;
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

    readModuleSettings();
    const int latencyNow = computeLatency();
    if (latencyNow != pendingLatency.load())
    {
        pendingLatency.store (latencyNow);
        triggerAsyncUpdate();   // hosts expect setLatencySamples from the message thread
    }

    inPeak.store (buffer.getMagnitude (0, total));
    for (int c = 0; c < 2; ++c)
        inPeakCh[(size_t) c].store (buffer.getMagnitude (juce::jmin (c, chs - 1), 0, total));

    const auto rvSettings = readReverb();
    const auto dlSettings = readDelay (bpm);
    const auto wdSettings = readWidener();
    readEqBands ("rv_eq", retEqSettings[0]);
    readEqBands ("dl_eq", retEqSettings[1]);
    int retSolo[2];
    bool retEqActive[2];
    for (int k = 0; k < 2; ++k)
    {
        const int so = returnEqSolo[k].load();
        retSolo[k] = so >= 0 && so < kv::Equalizer::numBands && retEqSettings[k][so].used ? so : -1;
        retEqActive[k] = false;
        for (auto& b : retEqSettings[k]) retEqActive[k] = retEqActive[k] || (b.used && b.on);
        if (retSolo[k] >= 0) setSoloFilter (retSoloBp[k], retEqSettings[k][retSolo[k]], sampleRateHz);
    }
    const kv::DuckSettings duck[2] = { readDuck (0), readDuck (1) };
    // Routing between the sends: each send may feed one other send. A feed that would close a loop is dropped
    // (checked in the order Reverb, Delay, Widener), and the sends run so that every source comes before its target.
    std::array<int, numSends> feedTo;
    feedTo.fill (-1);
    std::array<float, numSends> feedAmt {};
    {
        static const char* ids[numSends] = { "fx_rv_feed", "fx_dl_feed", "fx_wd_feed" };
        static const char* amts[numSends] = { "fx_rv_feed_amt", "fx_dl_feed_amt", "fx_wd_feed_amt" };
        for (int s = 0; s < numSends; ++s)
        {
            const int choice = juce::roundToInt (raw (ids[s])->load());   // 0 Off, then the other sends in index order
            const int target = choice <= 0 ? -1 : (choice - 1 >= s ? choice : choice - 1);
            feedAmt[(size_t) s] = raw (amts[s])->load() / 100.0f;
            bool loops = false;
            for (int t = target, steps = 0; t >= 0 && steps < numSends; t = feedTo[(size_t) t], ++steps)
                if (t == s) loops = true;
            feedBlocked[(size_t) s].store (target >= 0 && loops);
            if (target >= 0 && ! loops) feedTo[(size_t) s] = target;
        }
    }
    std::array<int, numSends> order {};
    {
        std::array<bool, numSends> placed {};
        int k = 0;
        while (k < numSends)
            for (int s = 0; s < numSends; ++s)
            {
                if (placed[(size_t) s]) continue;
                bool ready = true;   // every send feeding s is placed already
                for (int src = 0; src < numSends; ++src)
                    if (! placed[(size_t) src] && feedTo[(size_t) src] == s) ready = false;
                if (ready) { placed[(size_t) s] = true; order[(size_t) k++] = s; }
            }
    }
    std::array<float, 2> duckMax {};
    flSettings = readFlanger (bpm);
    inGainSmooth.setTargetValue (juce::Decibels::decibelsToGain (raw (kvid::inGain)->load()));
    outGainSmooth.setTargetValue (juce::Decibels::decibelsToGain (raw (kvid::outGain)->load()));

    std::array<bool, numSends> preFader {};
    for (int s = 0; s < numSends; ++s)
    {
        const auto& p = sendPtrs[(size_t) s];
        const bool on = p.on->load() > 0.5f;
        sendSmooth[(size_t) s].setTargetValue (on ? kvp::sendGain (p.level->load()) : 0.0f);
        returnFade[(size_t) s].setTargetValue (on ? 1.0f : 0.0f);
        preFader[(size_t) s] = p.tap->load() > 0.5f;
    }

    std::array<double, numSends> energy {};
    std::array<float, numSends> peak {};
    const int capacity = preTap.getNumSamples();

    for (int start = 0; start < total; start += capacity)
    {
        const int n = juce::jmin (capacity, total - start);
        chunkBpm = bpm;
        chunkPlaying = transport.valid && transport.playing;
        chunkPpq = transport.ppq + (double) start * bpm / 60.0 / sampleRateHz;
        flSettings.playing = chunkPlaying;
        flSettings.ppq = chunkPpq;
        float* out2[2] = { buffer.getWritePointer (0, start), buffer.getWritePointer (chs > 1 ? 1 : 0, start) };
        float* io[2] = { work.getWritePointer (0), work.getWritePointer (1) };

        // dry path: input gain -> channel modules -> pre-fader tap -> output gain
        for (int i = 0; i < n; ++i)
        {
            const float gi = inGainSmooth.getNextValue();
            io[0][i] = out2[0][i] * gi;
            io[1][i] = out2[chs > 1 ? 1 : 0][i] * gi;
        }
        processModules (io[0], io[1], n);
        for (int i = 0; i < n; ++i)
        {
            const float go = outGainSmooth.getNextValue();
            for (int c = 0; c < 2; ++c)
            {
                preTap.setSample (c, i, io[c][i]);
                io[c][i] *= go;
            }
        }

        // returns are summed into a scratch buffer first, so every send reads the same dry tap
        returns.clear (0, n);
        returns.clear (1, n);

        std::array<bool, numSends> fed {};   // feedBuf[s] holds returns fed into send s this chunk
        for (int s = 0; s < numSends; ++s) { feedBuf[(size_t) s].clear (0, n); feedBuf[(size_t) s].clear (1, n); }
        for (int s : order)
        {
            auto& level = sendSmooth[(size_t) s];
            auto& fade = returnFade[(size_t) s];
            const bool fromRoute = fed[(size_t) s];
            const int target = feedTo[(size_t) s];
            const bool feeds = target >= 0 && feedAmt[(size_t) s] > 0.0f;
            const bool inputLive = level.isSmoothing() || level.getTargetValue() > 0.0f || fromRoute;

            // Nothing to do: the send is off (return faded out), or nothing is being sent and the tail has died.
            if (! fade.isSmoothing() && fade.getTargetValue() <= 0.0f)
            {
                if (! idle[(size_t) s]) resetSend (s);
                idle[(size_t) s] = true;
                level.setCurrentAndTargetValue (level.getTargetValue());
                continue;
            }
            if (! inputLive && idle[(size_t) s])
                continue;
            if (idle[(size_t) s])
            {
                // starting from silence: switch algorithms at once instead of crossfading from the old one
                if (s == Reverb)  reverb.forceMode (rvSettings.mode);
                if (s == Widener) widener.forceType (wdSettings.type);
            }
            idle[(size_t) s] = false;

            for (int i = 0; i < n; ++i)
            {
                const float g = level.getNextValue();
                for (int c = 0; c < 2; ++c)
                {
                    const float tap = preFader[(size_t) s] ? preTap.getSample (c, i) : io[c][i];
                    sendIn.setSample (c, i, tap * g);
                }
            }
            if (fromRoute)
                for (int c = 0; c < 2; ++c)
                    sendIn.addFrom (c, 0, feedBuf[(size_t) s], c, 0, n);

            const float* in[2] = { sendIn.getReadPointer (0), sendIn.getReadPointer (1) };
            float* out[2] = { sendOut.getWritePointer (0), sendOut.getWritePointer (1) };
            if (s == Reverb)  reverb.process (in[0], in[1], out[0], out[1], n, rvSettings);
            if (s == Delay)   delay.process (in[0], in[1], out[0], out[1], n, dlSettings);
            if (s == Widener) widener.process (in[0], in[1], out[0], out[1], n, wdSettings);
            if (feeds && s == Widener)
                for (int c = 0; c < 2; ++c) routeBuf.copyFrom (c, 0, out[c], n);
            const int ri = s == Reverb ? 0 : (s == Delay ? 1 : -1);
            if (ri >= 0)
            {
                retAnalyserPre[ri].push (out[0], out[1], n);
                if (retSolo[ri] >= 0)
                    for (int c = 0; c < 2; ++c) retSoloIn.copyFrom (c, 0, out[c], n);
                if (retEqActive[ri])
                    returnEq[ri].process (out[0], out[1], n, retEqSettings[ri], 0.0f);
                if (retSolo[ri] >= 0)   // band audition from the EQ's input, as on the main EQ
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < n; ++i) out[c][i] = retSoloBp[ri][c].process (retSoloIn.getSample (c, i));
                retAnalyserPost[ri].push (out[0], out[1], n);
                if (feeds)
                    for (int c = 0; c < 2; ++c) routeBuf.copyFrom (c, 0, out[c], n);   // after the EQ, before ducking and the fade
                const bool rawKey = duck[ri].source == kv::DuckSettings::RawInput;   // the input before the channel modules
                const float* key[2] = { rawKey ? out2[0] : io[0], rawKey ? out2[chs > 1 ? 1 : 0] : io[1] };
                duckMax[(size_t) ri] = juce::jmax (duckMax[(size_t) ri], ducker[ri].process (out[0], out[1], n, key[0], key[1], duck[ri]));
            }

            float blockPeak = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float f = fade.getNextValue();   // 10 ms fade when the send is switched on or off
                for (int c = 0; c < 2; ++c)
                {
                    float y = out[c][i];
                    if (! std::isfinite (y))
                        y = 0.0f;
                    y = kv::returnGuard (y) * f;
                    if (feeds)
                    {
                        const float rb = routeBuf.getSample (c, i);
                        routeBuf.setSample (c, i, std::isfinite (rb) ? kv::returnGuard (rb) * f : 0.0f);
                    }
                    energy[(size_t) s] += (double) y * y;
                    blockPeak = juce::jmax (blockPeak, std::abs (y));
                    returns.addSample (c, i, y);
                }
            }
            peak[(size_t) s] = juce::jmax (peak[(size_t) s], blockPeak);
            if (feeds)
            {
                for (int c = 0; c < 2; ++c)
                    feedBuf[(size_t) target].addFrom (c, 0, routeBuf, c, 0, n, feedAmt[(size_t) s]);
                fed[(size_t) target] = true;
            }

            // tail has decayed below -140 dBFS with no input: stop processing until something is sent again
            if (! inputLive && blockPeak < 1.0e-7f)
            {
                resetSend (s);
                idle[(size_t) s] = true;
            }
        }

        // dry + returns. A mono output receives the average of both channels.
        for (int i = 0; i < n; ++i)
        {
            const float yl = io[0][i] + returns.getSample (0, i), yr = io[1][i] + returns.getSample (1, i);
            if (chs == 2) { out2[0][i] = yl; out2[1][i] = yr; }
            else          out2[0][i] = 0.5f * (yl + yr);
        }
    }

    for (int k = 0; k < 2; ++k)
        duckGr[(size_t) k].store (duckMax[(size_t) k]);
    for (int s = 0; s < numSends; ++s)
    {
        returnRms[(size_t) s].store ((float) std::sqrt (energy[(size_t) s] / (2.0 * total)));
        returnPeak[(size_t) s].store (peak[(size_t) s]);
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
    state.setProperty ("ui_send", advancedSend.load(), nullptr);
    state.setProperty ("ui_tab", advancedTab.load(), nullptr);
    state.setProperty ("ui_scale", uiScale.load(), nullptr);
    state.setProperty ("analyser_mode", analyserMode.load(), nullptr);
    state.setProperty ("analyser_resolution", analyserResolution.load(), nullptr);
    state.setProperty ("analyser_speed", analyserSpeed.load(), nullptr);
    state.setProperty ("state_version", stateVersion, nullptr);
    for (auto* m : { "tune", "eq", "multiband", "compression", "distortion", "deess", "resonance", "reverb", "delay", "widener", "flanger" })
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
            advancedSend.store (juce::jlimit (0, (int) numSends - 1, (int) state.getProperty ("ui_send", 0)));
            int tab = (int) state.getProperty ("ui_tab", 8);
            if (loadedStateVersion < 3 && tab >= 4)
                tab = tab >= 6 ? 7 : tab + 1;   // v3: Distortion tab inserted before De-ess; sends were 6..8
            if (loadedStateVersion < 4 && tab >= 4)
                tab += 1;                       // v4: Flanger tab inserted before Distortion
            advancedTab.store (juce::jlimit (0, 8, tab));
            uiScale.store (juce::jlimit (0.75f, 2.0f, (float) state.getProperty ("ui_scale", 1.0f)));
            analyserMode.store (juce::jlimit (0, 3, (int) state.getProperty ("analyser_mode", 3)));
            analyserResolution.store (juce::jlimit (0, 3, (int) state.getProperty ("analyser_resolution", (int) SpectrumProcessor::High)));
            analyserSpeed.store (juce::jlimit (0, 4, (int) state.getProperty ("analyser_speed", (int) SpectrumProcessor::Fast)));
            apvts.replaceState (state);
            presets.readState (state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KaminariVocalProcessor();
}
