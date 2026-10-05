#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <BinaryData.h>

KaminariVocalProcessor::KaminariVocalProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KAMINARI_VOCAL", kvp::createLayout()),
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
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void KaminariVocalProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRateHz = sampleRate;
    const int block = juce::jmax (32, samplesPerBlock);
    preTap.setSize (2, block, false, false, true);
    sendIn.setSize (2, block, false, false, true);
    sendOut.setSize (2, block, false, false, true);
    returns.setSize (2, block, false, false, true);

    reverb.prepare (sampleRate, block);
    delay.prepare (sampleRate, block);
    widener.prepare (sampleRate, block);

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
    setLatencySamples (0);   // sends add no latency: the dry path is never delayed
}

void KaminariVocalProcessor::resetSend (int s)
{
    if (s == Reverb)  reverb.reset();
    if (s == Delay)   delay.reset();
    if (s == Widener) widener.reset();
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

    const int total = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, total);
    const int chs = juce::jmin (buffer.getNumChannels(), 2);
    if (chs == 0 || total == 0)
        return;

    inPeak.store (buffer.getMagnitude (0, total));

    const auto rvSettings = readReverb();
    const auto dlSettings = readDelay (bpm);
    const auto wdSettings = readWidener();
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
        float* io[2] = { buffer.getWritePointer (0, start), buffer.getWritePointer (chs > 1 ? 1 : 0, start) };

        // dry path: input gain -> (channel modules) -> pre-fader tap -> output gain
        for (int i = 0; i < n; ++i)
        {
            const float gi = inGainSmooth.getNextValue();
            const float go = outGainSmooth.getNextValue();
            for (int c = 0; c < 2; ++c)
            {
                const float x = io[c][i] * gi;
                preTap.setSample (c, i, x);
                if (c < chs)
                    io[c][i] = x * go;
            }
        }

        // returns are summed into a scratch buffer first, so every send reads the same dry tap
        returns.clear (0, n);
        returns.clear (1, n);

        for (int s = 0; s < numSends; ++s)
        {
            auto& level = sendSmooth[(size_t) s];
            auto& fade = returnFade[(size_t) s];
            const bool inputLive = level.isSmoothing() || level.getTargetValue() > 0.0f;

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
                    const float tap = preFader[(size_t) s] ? preTap.getSample (c, i) : io[c < chs ? c : 0][i];
                    sendIn.setSample (c, i, tap * g);
                }
            }

            const float* in[2] = { sendIn.getReadPointer (0), sendIn.getReadPointer (1) };
            float* out[2] = { sendOut.getWritePointer (0), sendOut.getWritePointer (1) };
            if (s == Reverb)  reverb.process (in[0], in[1], out[0], out[1], n, rvSettings);
            if (s == Delay)   delay.process (in[0], in[1], out[0], out[1], n, dlSettings);
            if (s == Widener) widener.process (in[0], in[1], out[0], out[1], n, wdSettings);

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
                    energy[(size_t) s] += (double) y * y;
                    blockPeak = juce::jmax (blockPeak, std::abs (y));
                    returns.addSample (c, i, y);
                }
            }
            peak[(size_t) s] = juce::jmax (peak[(size_t) s], blockPeak);

            // tail has decayed below -140 dBFS with no input: stop processing until something is sent again
            if (! inputLive && blockPeak < 1.0e-7f)
            {
                resetSend (s);
                idle[(size_t) s] = true;
            }
        }

        // dry + returns. A mono output receives the sum of both return channels at half level.
        for (int i = 0; i < n; ++i)
        {
            if (chs == 2)
            {
                io[0][i] += returns.getSample (0, i);
                io[1][i] += returns.getSample (1, i);
            }
            else
            {
                io[0][i] += 0.5f * (returns.getSample (0, i) + returns.getSample (1, i));
            }
        }
    }

    for (int s = 0; s < numSends; ++s)
    {
        returnRms[(size_t) s].store ((float) std::sqrt (energy[(size_t) s] / (2.0 * total)));
        returnPeak[(size_t) s].store (peak[(size_t) s]);
    }
    outPeak.store (buffer.getMagnitude (0, total));
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

juce::AudioProcessorEditor* KaminariVocalProcessor::createEditor()
{
    return new KaminariVocalEditor (*this);
}

void KaminariVocalProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("ui_view", advancedView.load() ? "advanced" : "basic", nullptr);
    state.setProperty ("ui_send", advancedSend.load(), nullptr);
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
            advancedView.store (state.getProperty ("ui_view", "basic").toString() == "advanced");
            advancedSend.store (juce::jlimit (0, (int) numSends - 1, (int) state.getProperty ("ui_send", 0)));
            apvts.replaceState (state);
            presets.readState (state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KaminariVocalProcessor();
}
