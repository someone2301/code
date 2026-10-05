#include "PluginProcessor.h"
#include "PluginEditor.h"

ChannelStripProcessor::ChannelStripProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", cs::createLayout())
{
    pInGain = raw (ids::inGain);   pPhase = raw (ids::phase);   pMono = raw (ids::mono);
    pPan = raw (ids::pan);         pOutGain = raw (ids::outGain);

    pDeessOn = raw (ids::deessOn);         pDeessFreq = raw (ids::deessFreq);
    pDeessThresh = raw (ids::deessThresh); pDeessRange = raw (ids::deessRange);
    pDeessListen = raw (ids::deessListen);

    pEqOn = raw (ids::eqOn);  pEqPost = raw (ids::eqPost);

    pCompOn = raw (ids::compOn);   pCompMode = raw (ids::compMode);  pCompMix = raw (ids::compMix);
    pFetIn = raw (ids::fetIn);     pFetOut = raw (ids::fetOut);
    pFetAttack = raw (ids::fetAttack);  pFetRelease = raw (ids::fetRelease);  pFetRatio = raw (ids::fetRatio);
    pLaPeak = raw (ids::laPeak);   pLaGain = raw (ids::laGain);   pLaLimit = raw (ids::laLimit);

    for (int b = 0; b < cs::numBands; ++b)
        bandPtrs[(size_t) b] = { raw (cs::eqId (b, "on")), raw (cs::eqId (b, "type")), raw (cs::eqId (b, "freq")),
                                 raw (cs::eqId (b, "gain")), raw (cs::eqId (b, "q")) };
}

bool ChannelStripProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void ChannelStripProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate.store (sampleRate);
    deesser.prepare (sampleRate);
    eq.prepare (sampleRate);
    compressor.prepare (sampleRate);

    inGainSmooth.reset (sampleRate, 0.02);
    outGainSmooth.reset (sampleRate, 0.02);
    panSmooth.reset (sampleRate, 0.02);
    inGainSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pInGain->load()) * (pPhase->load() > 0.5f ? -1.0f : 1.0f));
    outGainSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutGain->load()));
    panSmooth.setCurrentAndTargetValue (pPan->load() / 100.0f);
}

EqParams ChannelStripProcessor::readEq() const
{
    EqParams params;
    for (size_t b = 0; b < (size_t) cs::numBands; ++b)
    {
        const auto& p = bandPtrs[b];
        params[b] = { p.on->load() > 0.5f, juce::roundToInt (p.type->load()), p.freq->load(), p.gain->load(), p.q->load() };
    }
    return params;
}

void ChannelStripProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    publishTempo();

    const int ns = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, ns);

    const int chs = juce::jmin (buffer.getNumChannels(), 2);
    if (chs == 0 || ns == 0)
        return;

    inPeak.store (buffer.getMagnitude (0, ns));

    // Input gain + polarity
    inGainSmooth.setTargetValue (juce::Decibels::decibelsToGain (pInGain->load()) * (pPhase->load() > 0.5f ? -1.0f : 1.0f));
    for (int i = 0; i < ns; ++i)
    {
        const float g = inGainSmooth.getNextValue();
        for (int c = 0; c < chs; ++c)
            buffer.getWritePointer (c)[i] *= g;
    }

    // De-esser
    DeEssParams dp;
    dp.on = pDeessOn->load() > 0.5f;
    dp.freq = pDeessFreq->load();
    dp.threshDb = pDeessThresh->load();
    dp.rangeDb = pDeessRange->load();
    dp.listen = pDeessListen->load() > 0.5f;
    deessGr.store (deesser.process (buffer, dp));

    const bool eqOn = pEqOn->load() > 0.5f;
    const bool eqPost = pEqPost->load() > 0.5f;
    const auto eqParams = readEq();

    if (eqOn && ! eqPost)
        eq.process (buffer, eqParams);

    // Compressor
    if (pCompOn->load() > 0.5f)
    {
        CompParams cp;
        cp.mode = juce::roundToInt (pCompMode->load());
        cp.fetInDb = pFetIn->load();
        cp.fetOutDb = pFetOut->load();
        cp.fetAttackMs = pFetAttack->load();
        cp.fetReleaseMs = pFetRelease->load();
        cp.fetRatioIdx = juce::roundToInt (pFetRatio->load());
        cp.laPeak = pLaPeak->load();
        cp.laGainDb = pLaGain->load();
        cp.laLimit = pLaLimit->load() > 0.5f;
        cp.mix = pCompMix->load() / 100.0f;
        compGr.store (compressor.process (buffer, cp));
    }
    else
    {
        compressor.reset();
        compGr.store (0.0f);
    }

    if (eqOn && eqPost)
        eq.process (buffer, eqParams);

    // Mono sum, pan (constant power, 0 dB at centre) and output gain
    const bool mono = pMono->load() > 0.5f;
    panSmooth.setTargetValue (pPan->load() / 100.0f);
    outGainSmooth.setTargetValue (juce::Decibels::decibelsToGain (pOutGain->load()));

    if (chs == 2)
    {
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);
        for (int i = 0; i < ns; ++i)
        {
            const float pan = panSmooth.getNextValue();
            const float out = outGainSmooth.getNextValue();
            const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            const float gl = std::cos (angle) * juce::MathConstants<float>::sqrt2 * out;
            const float gr = std::sin (angle) * juce::MathConstants<float>::sqrt2 * out;
            float a = l[i], b = r[i];
            if (mono)
                a = b = 0.5f * (a + b);
            l[i] = a * gl;
            r[i] = b * gr;
        }
    }
    else
    {
        auto* m = buffer.getWritePointer (0);
        for (int i = 0; i < ns; ++i)
        {
            panSmooth.getNextValue();
            m[i] *= outGainSmooth.getNextValue();
        }
    }

    outPeak.store (buffer.getMagnitude (0, ns));
    analyser.push (buffer);
}

void ChannelStripProcessor::publishTempo()
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

juce::AudioProcessorEditor* ChannelStripProcessor::createEditor()
{
    return new ChannelStripEditor (*this);
}

void ChannelStripProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("analyserResolution", analyserResolution.load(), nullptr);
    state.setProperty ("analyserSpeed", analyserSpeed.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ChannelStripProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            analyserResolution.store ((int) state.getProperty ("analyserResolution", (int) SpectrumProcessor::High));
            analyserSpeed.store ((int) state.getProperty ("analyserSpeed", (int) SpectrumProcessor::Fast));
            apvts.replaceState (state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChannelStripProcessor();
}
