#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "params/Params.h"
#include "HostTempo.h"
#include "state/PresetManager.h"
#include "dsp/Eq.h"
#include "dsp/Dynamics.h"
#include "dsp/Resonance.h"
#include "dsp/Tune.h"

// Kaminari Vocal processor.
//
// Signal path (DESIGN.md section 3 and 3.1):
//   input -> In Gain -> [channel modules] -> pre-fader tap -> Out Gain -> post-fader tap -> dry out
//   each send: tap * send level -> effect (100 % wet) -> return guard -> summed with the dry out
//
// The dry signal is never processed by a send: the output is the dry signal plus the three returns.
// Channel modules, in order: Tune -> EQ -> Multiband -> Compression -> De-ess -> Resonance.
// Layouts: mono -> mono, mono -> stereo, stereo -> stereo. A mono input is processed as dual mono, so the
// sends' stereo returns stay stereo on a mono-in/stereo-out track.
class KaminariVocalProcessor : public juce::AudioProcessor,
                               private juce::AsyncUpdater
{
public:
    enum SendIndex { Reverb, Delay, Widener, numSends };
    enum ModuleIndex { ModTune, ModEq, ModMultiband, ModCompression, ModDeEss, ModResonance, numModules };

    // Saved with every session; raise when a later version must convert old sessions (see DESIGN.md 2.11).
    static constexpr int stateVersion = 2;
    // Algorithm version per module, saved with the session so a later, improved algorithm can keep old
    // sessions sounding the same. All modules are at version 1.
    static constexpr int engineVersion = 1;

    KaminariVocalProcessor();
    ~KaminariVocalProcessor() override { cancelPendingUpdate(); }

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
    PresetManager presets;   // factory and user presets, module and chain level (message thread only)
    HostTempo hostTempo;

    // Meters for the GUI (peak, linear).
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };
    std::array<std::atomic<float>, numSends> returnPeak {};

    // Tests read the RMS of each return over the last processed block.
    std::array<std::atomic<float>, numSends> returnRms {};

    // Gain reduction per module in dB (Multiband can be negative = boost), for the GUI meters.
    std::array<std::atomic<float>, numModules> moduleGr {};
    std::atomic<float> compMakeup { 0.0f };

    kv::Tune tune;              // GUI reads its pitch read-outs
    kv::Resonance resonance;    // GUI reads its per-band reduction
    kv::Equalizer eq;

    // Session state read back from the last setStateInformation (tests and future migrations).
    int loadedStateVersion = stateVersion;

    // Non-parameter UI state saved with the session.
    std::atomic<bool> advancedView { false };
    std::atomic<int> advancedSend { Reverb };
    std::atomic<int> advancedTab { 0 };   // Advanced view tab: 0..5 modules, 6..8 sends

    // Delay times in samples from the current settings and host tempo (also used by the GUI read-out).
    float delayTimeSamples (int echo, double bpm) const;

    // Total reported latency: Tune's fixed delay plus any Compression / De-ess lookahead.
    int computeLatency() const;

    // Settings for the GUI's EQ curve (same values the audio thread uses).
    void readEqSettings (kv::EqBandSettings (&out)[kv::Equalizer::numBands]) const;

private:
    using Raw = std::atomic<float>*;
    void handleAsyncUpdate() override { setLatencySamples (pendingLatency.load()); }
    void processModules (float* l, float* r, int n);
    void readModuleSettings();

    kv::TuneSettings tuneSettings;
    kv::EqBandSettings eqSettings[kv::Equalizer::numBands];
    kv::MultibandSettings mbSettings;
    kv::CompressorSettings compSettings;
    kv::DeEsserSettings dsSettings;
    kv::ResonanceSettings rsSettings;
    bool moduleOn[numModules] {};
    float eqOutGain = 0.0f;

    kv::Multiband multiband;
    kv::Compressor compressor;
    kv::DeEsser deesser;
    std::array<juce::SmoothedValue<float>, numModules> moduleFade;   // 10 ms bypass crossfades (EQ, Multiband, Resonance)
    juce::AudioBuffer<float> work, dryCopy;
    std::atomic<int> pendingLatency { 0 };
    Raw raw (const char* id) const { return apvts.getRawParameterValue (id); }
    Raw raw (const juce::String& id) const { return apvts.getRawParameterValue (id); }
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
