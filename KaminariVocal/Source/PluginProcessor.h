#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "params/Params.h"
#include "HostTempo.h"

// Kaminari Vocal processor.
//
// Signal path (DESIGN.md section 3 and 3.1):
//   input -> In Gain -> [channel modules] -> pre-fader tap -> Out Gain -> post-fader tap -> dry out
//   each send: tap * send level -> effect (100 % wet) -> return guard -> summed with the dry out
//
// The dry signal is never processed by a send: the output is the dry signal plus the three returns.
// The channel modules (Tune, EQ, Multiband, Compression, De-ess, Resonance) are specified in DESIGN.md and are
// not part of this build yet; the pre-fader tap sits where they will end.
class KaminariVocalProcessor : public juce::AudioProcessor
{
public:
    enum SendIndex { Reverb, Delay, Widener, numSends };

    KaminariVocalProcessor();
    ~KaminariVocalProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Kaminari Vocal"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }   // longest reverb decay plus delay feedback

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    HostTempo hostTempo;

    // Meters for the GUI (peak, linear).
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };
    std::array<std::atomic<float>, numSends> returnPeak {};

    // Tests read the RMS of each return over the last processed block.
    std::array<std::atomic<float>, numSends> returnRms {};

    // Non-parameter UI state saved with the session.
    std::atomic<bool> advancedView { false };
    std::atomic<int> advancedSend { Reverb };

    // Delay times in samples from the current settings and host tempo (also used by the GUI read-out).
    float delayTimeSamples (int echo, double bpm) const;

private:
    using Raw = std::atomic<float>*;
    Raw raw (const char* id) const { return apvts.getRawParameterValue (id); }
    void publishTempo (double& bpm);

    kv::ReverbSettings readReverb() const;
    kv::DelaySettings readDelay (double bpm) const;
    kv::WidenerSettings readWidener() const;

    struct SendPtrs { Raw on, level, tap; };
    std::array<SendPtrs, numSends> sendPtrs;

    kv::ReverbSend reverb;
    kv::DelaySend delay;
    kv::WidenerSend widener;

    juce::SmoothedValue<float> inGainSmooth, outGainSmooth;
    void resetSend (int s);

    std::array<juce::SmoothedValue<float>, numSends> sendSmooth;   // send level (0 while the send is off)
    std::array<juce::SmoothedValue<float>, numSends> returnFade;   // 1 = send on, 0 = off (10 ms fade)
    std::array<bool, numSends> idle {};                            // effect reset and not processing

    juce::AudioBuffer<float> preTap, sendIn, sendOut, returns;
    double sampleRateHz = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KaminariVocalProcessor)
};
