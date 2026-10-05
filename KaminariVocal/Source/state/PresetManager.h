#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

// Factory and user presets at two levels (DESIGN.md section 2.10):
//
//   module presets  one per module (Tune, EQ, Multiband, Compression, De-ess, Resonance, Reverb, Delay, Widener).
//                   A module preset sets only that module's sound parameters; anything it does not list returns to
//                   its default. It never changes the module's On switch, a send's On/level/tap, or Tune's key,
//                   scale, range and note map.
//   chain presets   the whole plug-in (Basic view and the header in both views): one module preset per module,
//                   then overrides such as On switches and send levels.
//
// Factory presets come from Presets/factory.json (embedded). User presets are XML files in
//   <user presets folder>/<Module>/<name>.kvpreset  and  <user presets folder>/Chains/<name>.kvpreset
// IDs of modules that are not built yet are skipped when loading. Message thread only.
class PresetManager
{
public:
    struct Preset
    {
        juce::String name, description;
        bool factory = true;
        juce::File file;                                  // user presets only
        std::map<juce::String, juce::var> values;         // parameter ID -> plain value (number, bool or choice name)
        std::map<juce::String, juce::String> modules;     // chains only: module key -> module preset name
    };

    struct Module
    {
        juce::String key, title;
        std::vector<Preset> factory;
    };

    PresetManager (juce::AudioProcessorValueTreeState& state, const juce::String& factoryJson);

    // Folder for user presets (default: the standard per-user audio presets location). Tests point it elsewhere.
    void setUserFolder (const juce::File& folder) { userRoot = folder; }
    juce::File getUserFolder() const { return userRoot; }

    const std::vector<Module>& getModules() const { return modules; }
    const Module* findModule (const juce::String& key) const;
    const std::vector<Preset>& getFactoryChains() const { return chains; }

    // Parameter IDs a module preset controls (only parameters that exist in this build).
    juce::StringArray moduleParameterIds (const juce::String& moduleKey) const;

    // Lists factory presets followed by user presets.
    std::vector<Preset> listModulePresets (const juce::String& moduleKey) const;
    std::vector<Preset> listChainPresets() const;

    bool loadModulePreset (const juce::String& moduleKey, const juce::String& name);
    bool loadChainPreset (const juce::String& name);
    bool loadModulePreset (const juce::String& moduleKey, const Preset& p);
    bool loadChainPreset (const Preset& p);

    // Steps through the list (factory then user) and loads the neighbour. delta = +1 or -1.
    bool stepModulePreset (const juce::String& moduleKey, int delta);
    bool stepChainPreset (int delta);

    bool saveModulePreset (const juce::String& moduleKey, const juce::String& name);
    bool saveChainPreset (const juce::String& name);
    bool deleteUserPreset (const Preset& p);

    // Name of the preset loaded last ("" = none) and whether the parameters have changed since.
    juce::String currentModulePreset (const juce::String& moduleKey) const;
    juce::String currentChainPreset() const { return chainName; }
    bool isModuleModified (const juce::String& moduleKey) const;
    bool isChainModified() const;

    // Session state: the names above, stored as properties of the plug-in state tree.
    void writeState (juce::ValueTree& tree) const;
    void readState (const juce::ValueTree& tree);

    // Validation used by the tests: problems with the embedded factory data ("" = none).
    juce::StringArray validateFactoryData() const;

    static juce::String sanitiseName (const juce::String& name);

private:
    static Preset parsePreset (const juce::var& v);
    juce::var currentValue (const juce::String& id) const;
    bool applyValues (const juce::StringArray& scope, const std::map<juce::String, juce::var>& values, bool resetOthers);
    bool setPlainValue (juce::RangedAudioParameter& p, const juce::var& value);
    std::map<juce::String, juce::var> snapshot (const juce::StringArray& ids) const;
    juce::File moduleFolder (const juce::String& moduleKey) const;
    std::vector<Preset> readUserPresets (const juce::File& folder) const;
    bool writePresetFile (const juce::File& file, const juce::String& kind, const juce::String& name, const juce::StringArray& ids) const;
    juce::StringArray allParameterIds() const;

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<Module> modules;
    std::vector<Preset> chains;
    juce::File userRoot;

    std::map<juce::String, juce::String> moduleNames;
    std::map<juce::String, std::map<juce::String, juce::var>> moduleSnapshots;
    juce::String chainName;
    std::map<juce::String, juce::var> chainSnapshot;
};
