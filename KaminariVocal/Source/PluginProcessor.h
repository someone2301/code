#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "params/Params.h"
#include "HostTempo.h"
#include "state/PresetManager.h"
#include "dsp/Eq.h"
#include "dsp/Dynamics.h"
#include "dsp/Resonance.h"
#include "dsp/Tune.h"
#include "dsp/Oversampled.h"
#include "dsp/SpectrumAnalyser.h"
#include "dsp/History.h"

// Kaminari Vocal processor.
//
// Signal path (Alt edition, see docs/KaminariVocal/VERSIONS.md):
//   input -> In Gain -> Tune -> EQ -> Multiband -> Compression -> De-ess -> Resonance -> Out Gain -> output
// Layouts: mono -> mono, mono -> stereo, stereo -> stereo. A mono input is processed as dual mono.
class KaminariVocalProcessor : public juce::AudioProcessor,
                               private juce::AsyncUpdater,
                               private juce::AudioProcessorParameter::Listener
{
public:
    // Processing and tab order.
    enum ModuleIndex { ModTune, ModEq, ModMultiband, ModCompression, ModDeEss, ModResonance, numModules };

    // Saved with every session; raise when a later version must convert old sessions (see DESIGN.md 2.11).
    // 3: Distortion tab inserted before De-ess (saved Advanced tab indices from 4 on move up by one).
    // 4: Flanger moved from the sends into the chain; its tab sits before Distortion (indices from 4 on move up again).
    // 5: Alt edition: six module tabs (0..5), no Flanger, Distortion or sends.
    static constexpr int stateVersion = 5;
    // Algorithm version per module, saved with the session so a later, improved algorithm can keep old
    // sessions sounding the same. All modules are at version 1.
    static constexpr int engineVersion = 1;

    KaminariVocalProcessor();
    ~KaminariVocalProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return KV_PRODUCT_NAME; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }   // longest compressor release

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
    std::array<std::atomic<float>, 2> inPeakCh {}, outPeakCh {};

    // EQ analyzer: audio before and after the EQ; display settings are saved with the session.
    SpectrumAnalyser analyserPre, analyserPost;
    std::atomic<int> analyserMode { 3 };   // 0 Pre, 1 Post, 2 Off, 3 Pre and Post together
    std::atomic<int> analyserResolution { SpectrumProcessor::High }, analyserSpeed { SpectrumProcessor::Fast };
    // Compression side chain: EQ band being auditioned (-1 = none; not saved)
    std::atomic<int> scEqSolo { -1 };
    // analyzer display (bit 0 = pre, bit 1 = post as AnalyzerPair modes) of the side-chain EQ graph
    std::atomic<int> scAnalyserMode { 3 };

    // The EQs that share the main EQ's band layout and editor.
    enum EqTarget { EqMain, EqSideChain, numEqTargets };
    static juce::String eqPrefix (int t) { return t == EqSideChain ? "lv_sc" : "eq"; }
    static juce::String eqOnId (int t)   { return t == EqSideChain ? "lv_on" : "eq_on"; }
    std::atomic<int>& eqSoloFor (int t) { return t == EqSideChain ? scEqSolo : eqSolo; }
    std::atomic<int>& eqAnalyserModeFor (int t) { return t == EqSideChain ? scAnalyserMode : analyserMode; }
    SpectrumAnalyser& eqAnalyserFor (int t, bool post)
    {
        if (t == EqSideChain) return post ? compScAnalyser : compInAnalyser;
        return post ? analyserPost : analyserPre;
    }
    void readEqBands (const juce::String& prefix, kv::EqBandSettings (&out)[kv::Equalizer::numBands]) const;
    // Band audition filter: around a bell / notch / band pass, below a low shelf or low cut, above a high shelf or high cut.
    static void setSoloFilter (kv::Biquad (&bp)[2], const kv::EqBandSettings& bs, double fs);
    std::atomic<int> eqSolo { -1 };         // band being auditioned (-1 = none); not saved

    // Level histories for the Compression and De-ess displays (about 2.7 ms per entry at 48 kHz).
    static constexpr int historySize = 360, deessHistorySize = 1200;
    kv::LevelHistory<historySize> compHistory;
    kv::LevelHistory<deessHistorySize> deessHistory;   // 6 s at 200 points per second

    // A/B comparison: two parameter snapshots; the active one is live.
    void selectAB (int slot);
    void copyAToB();
    int activeAB() const { return abSlot; }
    juce::UndoManager undoManager;
    std::atomic<float> uiScale { 1.0f };

    // Gain reduction per module in dB (Multiband can be negative = boost), for the GUI meters.
    std::array<std::atomic<float>, numModules> moduleGr {};
    std::array<std::atomic<float>, 6> mbBandChange {};   // per-band gain change in dB (Multiband display)

    kv::Tune tune;              // GUI reads its pitch read-outs
    kv::Oversampled<kv::Resonance> resonance;   // GUI reads the running instance's per-band reduction (resonance.active())

    // Pre/post analyzers of the Multiband and Resonance displays (fed only while the module is on).
    SpectrumAnalyser mbAnalyserPre, mbAnalyserPost, rsAnalyserPre, rsAnalyserPost;
    // Compression: the main signal entering the compressor and the signal its detector hears (after the side-chain bands)
    SpectrumAnalyser compInAnalyser, compScAnalyser;
    kv::Equalizer eq;

    // Session state read back from the last setStateInformation (tests and future migrations).
    int loadedStateVersion = stateVersion;

    // Non-parameter UI state saved with the session.
    std::atomic<bool> advancedView { false };
    std::atomic<int> advancedTab { 0 };   // Advanced view tab: 0..5 modules

    // Total reported latency: Tune's fixed delay plus Multiband / Resonance oversampling while those are on.
    int computeLatency() const;

    // Settings for the GUI's EQ curve (same values the audio thread uses).
    void readEqSettings (kv::EqBandSettings (&out)[kv::Equalizer::numBands]) const;

private:
    using Raw = std::atomic<float>*;
    void handleAsyncUpdate() override { setLatencySamples (pendingLatency.load()); }
    // Any parameter change marks the settings for re-reading on the next block (reading them all every block
    // costs more than most modules at small buffer sizes).
    void parameterValueChanged (int, float) override { paramsDirty.store (true); }
    void parameterGestureChanged (int, bool) override {}
    void readAllSettings (double bpm);
    void processModules (float* l, float* r, int n);
    void readModuleSettings();

    kv::TuneSettings tuneSettings;
    kv::EqBandSettings eqSettings[kv::Equalizer::numBands];
    kv::MultibandSettings mbSettings;
    kv::CompressorSettings compSettings;
    kv::DeEsserSettings dsSettings;
    kv::ResonanceSettings rsSettings;
    double chunkPpq = 0.0, chunkBpm = 120.0;
    bool chunkPlaying = false;
    bool moduleOn[numModules] {};
    float eqOutGain = 0.0f;

    kv::Oversampled<kv::Multiband> multiband;
    kv::Compressor compressor;
    kv::DeEsser deesser;
    int mbOs = 0, rsOs = 0;
    std::array<juce::SmoothedValue<float>, numModules> moduleFade;   // 10 ms bypass crossfades (EQ, Multiband, Resonance)
    juce::AudioBuffer<float> work, dryCopy, soloIn, scDetector;
    kv::Biquad scSoloBp[2];
    kv::Biquad soloBp[2];
    juce::ValueTree abState[2];
    int abSlot = 0;
    std::atomic<int> pendingLatency { 0 };
    Raw raw (const char* id) const { return apvts.getRawParameterValue (id); }
    Raw raw (const juce::String& id) const { return apvts.getRawParameterValue (id); }
    void publishTempo (double& bpm);

    std::atomic<bool> paramsDirty { true };
    double settingsBpm = -1.0;

    juce::SmoothedValue<float> inGainSmooth, outGainSmooth;

    double sampleRateHz = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KaminariVocalProcessor)
};
