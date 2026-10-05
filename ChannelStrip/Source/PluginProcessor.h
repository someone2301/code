#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "dsp/Compressor.h"
#include "dsp/DeEsser.h"
#include "dsp/Equalizer.h"
#include "dsp/SpectrumAnalyser.h"
#include "HostTempo.h"

class ChannelStripProcessor : public juce::AudioProcessor
{
public:
    ChannelStripProcessor();
    ~ChannelStripProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Channel Strip"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Meter values read by the GUI.
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, compGr { 0.0f }, deessGr { 0.0f };
    std::atomic<double> currentSampleRate { 44100.0 };
    SpectrumAnalyser analyser;
    HostTempo hostTempo;                                   // transport for the GUI's beat-synced glow

    // Analyser display settings (GUI state, saved with the session; not automatable).
    std::atomic<int> analyserResolution { SpectrumProcessor::High }, analyserSpeed { SpectrumProcessor::Fast };

private:
    using Raw = std::atomic<float>*;
    Raw raw (const juce::String& id) const { return apvts.getRawParameterValue (id); }

    struct BandPtrs { Raw on, type, freq, gain, q; };
    std::array<BandPtrs, cs::numBands> bandPtrs;
    Raw pInGain, pPhase, pMono, pPan, pOutGain,
        pDeessOn, pDeessFreq, pDeessThresh, pDeessRange, pDeessListen,
        pEqOn, pEqPost,
        pCompOn, pCompMode, pCompMix, pFetIn, pFetOut, pFetAttack, pFetRelease, pFetRatio, pLaPeak, pLaGain, pLaLimit;

    EqParams readEq() const;
    void publishTempo();

    DeEsser deesser;
    Equalizer eq;
    Compressor compressor;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inGainSmooth, outGainSmooth, panSmooth;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStripProcessor)
};
