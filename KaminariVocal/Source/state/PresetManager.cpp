#include "PresetManager.h"

namespace
{
    struct Scope
    {
        const char* key;
        const char* folder;
        std::vector<const char*> prefixes;     // a parameter belongs to the module if its ID starts with one of these
        std::vector<const char*> exact;        // or equals one of these
        std::vector<const char*> excluded;     // never set by a module preset (On switches, routing, audition)
        std::vector<const char*> excludedSuffixes;
    };

    const std::vector<Scope>& scopes()
    {
        static const std::vector<Scope> s = {
            { "tune", "Tune", {}, { "tn_speed", "tn_humanize" }, {}, {} },
            { "eq", "EQ", { "eq" }, {}, { "eq_on" }, {} },
            { "multiband", "Multiband", { "mb" }, {}, { "mb_on" }, { "_solo" } },
            { "compression", "Compression", { "lv_" }, {}, { "lv_on", "lv_sc_listen", "lv_sc_source" }, {} },
            { "deess", "De-ess", { "ds_" }, {}, { "ds_on", "ds_listen", "ds_audition_trigger", "ds_sc_source" }, {} },
            { "resonance", "Resonance", { "rs_" }, {}, { "rs_on", "rs_bypass", "rs_sc", "rs_sc_listen", "rs_delta" }, { "_listen" } },
            { "reverb", "Reverb", { "rv_" }, {}, { "rv_on", "rv_send", "rv_tap" }, {} },
            { "delay", "Delay", { "dl_" }, {}, { "dl_on", "dl_send", "dl_tap" }, {} },
            { "widener", "Widener", { "wd_" }, {}, { "wd_on", "wd_send", "wd_tap" }, {} },
        };
        return s;
    }

    const Scope* findScope (const juce::String& key)
    {
        for (auto& s : scopes())
            if (key == s.key)
                return &s;
        return nullptr;
    }

    bool inScope (const Scope& s, const juce::String& id)
    {
        for (auto* e : s.excluded)
            if (id == e) return false;
        for (auto* suf : s.excludedSuffixes)
            if (id.endsWith (suf)) return false;
        for (auto* e : s.exact)
            if (id == e) return true;
        for (auto* p : s.prefixes)
            if (id.startsWith (p)) return true;
        return false;
    }

    const char* const fileExtension = ".kvpreset";

    bool sameValue (const juce::var& a, const juce::var& b)
    {
        if (a.isString() || b.isString())
            return a.toString() == b.toString();
        return std::abs ((double) a - (double) b) < 1.0e-4;
    }
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state, const juce::String& factoryJson)
    : apvts (state)
{
    userRoot = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
                   .getChildFile ("Audio/Presets/Kaminari Audio/Kaminari Vocal");
#else
                   .getChildFile ("Kaminari Audio/Kaminari Vocal/Presets");
#endif

    const auto root = juce::JSON::parse (factoryJson);
    if (auto* mods = root.getProperty ("modules", {}).getDynamicObject())
    {
        // keep the order of the design (Tune .. Resonance, then the sends), not the JSON's
        for (auto& s : scopes())
        {
            const auto m = mods->getProperty (s.key);
            if (m.isVoid())
                continue;
            Module mod { s.key, m.getProperty ("title", s.folder).toString(), {} };
            if (auto* arr = m.getProperty ("presets", {}).getArray())
                for (auto& p : *arr)
                    mod.factory.push_back (parsePreset (p));
            modules.push_back (std::move (mod));
        }
    }
    if (auto* arr = root.getProperty ("chains", {}).getArray())
        for (auto& c : *arr)
            chains.push_back (parsePreset (c));
}

PresetManager::Preset PresetManager::parsePreset (const juce::var& v)
{
    Preset p;
    p.name = v.getProperty ("name", "").toString();
    p.description = v.getProperty ("description", "").toString();
    if (auto* obj = v.getProperty ("values", {}).getDynamicObject())
        for (auto& kv : obj->getProperties())
            p.values[kv.name.toString()] = kv.value;
    if (auto* obj = v.getProperty ("modules", {}).getDynamicObject())
        for (auto& kv : obj->getProperties())
            p.modules[kv.name.toString()] = kv.value.toString();
    return p;
}

const PresetManager::Module* PresetManager::findModule (const juce::String& key) const
{
    for (auto& m : modules)
        if (m.key == key)
            return &m;
    return nullptr;
}

juce::StringArray PresetManager::allParameterIds() const
{
    juce::StringArray ids;
    for (auto* p : apvts.processor.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            ids.add (r->getParameterID());
    return ids;
}

juce::StringArray PresetManager::moduleParameterIds (const juce::String& moduleKey) const
{
    juce::StringArray ids;
    if (auto* s = findScope (moduleKey))
        for (auto& id : allParameterIds())
            if (inScope (*s, id))
                ids.add (id);
    return ids;
}

juce::var PresetManager::currentValue (const juce::String& id) const
{
    auto* p = apvts.getParameter (id);
    if (p == nullptr)
        return {};
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p))
        return c->getCurrentChoiceName();
    if (auto* b = dynamic_cast<juce::AudioParameterBool*> (p))
        return b->get();
    return (double) p->convertFrom0to1 (p->getValue());
}

bool PresetManager::setPlainValue (juce::RangedAudioParameter& p, const juce::var& value)
{
    float norm = 0.0f;
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (&p))
    {
        int index = value.isString() ? c->choices.indexOf (value.toString()) : (int) value;
        if (index < 0)
            return false;
        norm = c->convertTo0to1 ((float) juce::jlimit (0, c->choices.size() - 1, index));
    }
    else if (dynamic_cast<juce::AudioParameterBool*> (&p) != nullptr)
    {
        norm = (bool) value ? 1.0f : 0.0f;
    }
    else
    {
        const auto& range = p.getNormalisableRange();
        norm = p.convertTo0to1 (juce::jlimit (range.start, range.end, (float) (double) value));
    }
    if (std::abs (norm - p.getValue()) > 1.0e-7f)
    {
        p.beginChangeGesture();
        p.setValueNotifyingHost (norm);
        p.endChangeGesture();
    }
    return true;
}

bool PresetManager::applyValues (const juce::StringArray& scope, const std::map<juce::String, juce::var>& values, bool resetOthers)
{
    bool ok = true;
    for (auto& id : scope)
    {
        auto* p = apvts.getParameter (id);
        if (p == nullptr)
            continue;
        const auto it = values.find (id);
        if (it != values.end())
            ok = setPlainValue (*p, it->second) && ok;
        else if (resetOthers)
            setPlainValue (*p, (double) p->convertFrom0to1 (p->getDefaultValue()));
    }
    return ok;
}

std::map<juce::String, juce::var> PresetManager::snapshot (const juce::StringArray& ids) const
{
    std::map<juce::String, juce::var> m;
    for (auto& id : ids)
        m[id] = currentValue (id);
    return m;
}

std::vector<PresetManager::Preset> PresetManager::listModulePresets (const juce::String& moduleKey) const
{
    std::vector<Preset> list;
    if (auto* m = findModule (moduleKey))
        list = m->factory;
    for (auto& p : readUserPresets (moduleFolder (moduleKey)))
        list.push_back (p);
    return list;
}

std::vector<PresetManager::Preset> PresetManager::listChainPresets() const
{
    auto list = chains;
    for (auto& p : readUserPresets (userRoot.getChildFile ("Chains")))
        list.push_back (p);
    return list;
}

bool PresetManager::loadModulePreset (const juce::String& moduleKey, const juce::String& name)
{
    for (auto& p : listModulePresets (moduleKey))
        if (p.name == name)
            return loadModulePreset (moduleKey, p);
    return false;
}

bool PresetManager::loadModulePreset (const juce::String& moduleKey, const Preset& p)
{
    const auto ids = moduleParameterIds (moduleKey);
    if (findScope (moduleKey) == nullptr)
        return false;
    const bool ok = applyValues (ids, p.values, true);
    moduleNames[moduleKey] = p.name;
    moduleSnapshots[moduleKey] = snapshot (ids);
    return ok;
}

bool PresetManager::loadChainPreset (const juce::String& name)
{
    for (auto& p : listChainPresets())
        if (p.name == name)
            return loadChainPreset (p);
    return false;
}

bool PresetManager::loadChainPreset (const Preset& p)
{
    bool ok = true;
    if (p.factory)
    {
        // a factory chain is built from module presets plus overrides; unlisted globals return to defaults
        for (auto& m : modules)
        {
            const auto it = p.modules.find (m.key);
            ok = (it != p.modules.end() ? loadModulePreset (m.key, it->second) : loadModulePreset (m.key, m.factory.front())) && ok;
        }
        juce::StringArray rest;
        for (auto& id : allParameterIds())
        {
            bool covered = false;
            for (auto& m : modules)
                covered = covered || moduleParameterIds (m.key).contains (id);
            if (! covered)
                rest.add (id);
        }
        ok = applyValues (rest, p.values, true) && ok;
        // overrides may also touch module parameters (e.g. a Basic hammer)
        juce::StringArray overrides;
        for (auto& kv : p.values)
            if (! rest.contains (kv.first) && apvts.getParameter (kv.first) != nullptr)
                overrides.add (kv.first);
        ok = applyValues (overrides, p.values, false) && ok;
    }
    else
    {
        // a user chain stores every parameter
        ok = applyValues (allParameterIds(), p.values, false);
        for (auto& kv : p.modules)
            moduleNames[kv.first] = kv.second;
    }
    for (auto& m : modules)
        moduleSnapshots[m.key] = snapshot (moduleParameterIds (m.key));
    chainName = p.name;
    chainSnapshot = snapshot (allParameterIds());
    return ok;
}

bool PresetManager::stepModulePreset (const juce::String& moduleKey, int delta)
{
    const auto list = listModulePresets (moduleKey);
    if (list.empty())
        return false;
    int index = -1;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].name == currentModulePreset (moduleKey))
            index = (int) i;
    const int n = (int) list.size();
    const int next = index < 0 ? (delta > 0 ? 0 : n - 1) : ((index + delta) % n + n) % n;
    return loadModulePreset (moduleKey, list[(size_t) next]);
}

bool PresetManager::stepChainPreset (int delta)
{
    const auto list = listChainPresets();
    if (list.empty())
        return false;
    int index = -1;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].name == chainName)
            index = (int) i;
    const int n = (int) list.size();
    const int next = index < 0 ? (delta > 0 ? 0 : n - 1) : ((index + delta) % n + n) % n;
    return loadChainPreset (list[(size_t) next]);
}

juce::String PresetManager::sanitiseName (const juce::String& name)
{
    return juce::File::createLegalFileName (name.trim()).substring (0, 64);
}

juce::File PresetManager::moduleFolder (const juce::String& moduleKey) const
{
    auto* s = findScope (moduleKey);
    return userRoot.getChildFile (s != nullptr ? s->folder : "Other");
}

bool PresetManager::writePresetFile (const juce::File& file, const juce::String& kind, const juce::String& name, const juce::StringArray& ids) const
{
    juce::XmlElement xml ("KaminariVocalPreset");
    xml.setAttribute ("format", 1);
    xml.setAttribute ("kind", kind);
    xml.setAttribute ("name", name);
    for (auto& id : ids)
    {
        auto* e = xml.createNewChildElement ("Param");
        e->setAttribute ("id", id);
        const auto v = currentValue (id);
        e->setAttribute ("value", v.toString());
        e->setAttribute ("type", v.isString() ? "choice" : (v.isBool() ? "bool" : "number"));
    }
    if (kind == "chain")
        for (auto& kv : moduleNames)
        {
            auto* e = xml.createNewChildElement ("Module");
            e->setAttribute ("key", kv.first);
            e->setAttribute ("preset", kv.second);
        }
    file.getParentDirectory().createDirectory();
    return xml.writeTo (file);
}

std::vector<PresetManager::Preset> PresetManager::readUserPresets (const juce::File& folder) const
{
    std::vector<Preset> list;
    auto files = folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension);
    files.sort();
    for (auto& f : files)
    {
        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr || ! xml->hasTagName ("KaminariVocalPreset"))
            continue;
        Preset p;
        p.factory = false;
        p.file = f;
        p.name = xml->getStringAttribute ("name", f.getFileNameWithoutExtension());
        for (auto* e : xml->getChildWithTagNameIterator ("Param"))
        {
            const auto type = e->getStringAttribute ("type");
            const auto text = e->getStringAttribute ("value");
            juce::var v = type == "choice" ? juce::var (text)
                        : type == "bool"   ? juce::var (text == "1" || text.equalsIgnoreCase ("true"))
                                           : juce::var (text.getDoubleValue());
            p.values[e->getStringAttribute ("id")] = v;
        }
        for (auto* e : xml->getChildWithTagNameIterator ("Module"))
            p.modules[e->getStringAttribute ("key")] = e->getStringAttribute ("preset");
        list.push_back (std::move (p));
    }
    return list;
}

bool PresetManager::saveModulePreset (const juce::String& moduleKey, const juce::String& name)
{
    const auto clean = sanitiseName (name);
    if (clean.isEmpty() || findScope (moduleKey) == nullptr)
        return false;
    const auto ids = moduleParameterIds (moduleKey);
    if (! writePresetFile (moduleFolder (moduleKey).getChildFile (clean + fileExtension), "module:" + moduleKey, clean, ids))
        return false;
    moduleNames[moduleKey] = clean;
    moduleSnapshots[moduleKey] = snapshot (ids);
    return true;
}

bool PresetManager::saveChainPreset (const juce::String& name)
{
    const auto clean = sanitiseName (name);
    if (clean.isEmpty())
        return false;
    if (! writePresetFile (userRoot.getChildFile ("Chains").getChildFile (clean + fileExtension), "chain", clean, allParameterIds()))
        return false;
    chainName = clean;
    chainSnapshot = snapshot (allParameterIds());
    return true;
}

bool PresetManager::deleteUserPreset (const Preset& p)
{
    return ! p.factory && p.file.existsAsFile() && p.file.deleteFile();
}

juce::String PresetManager::currentModulePreset (const juce::String& moduleKey) const
{
    const auto it = moduleNames.find (moduleKey);
    return it != moduleNames.end() ? it->second : juce::String();
}

bool PresetManager::isModuleModified (const juce::String& moduleKey) const
{
    const auto it = moduleSnapshots.find (moduleKey);
    if (it == moduleSnapshots.end())
        return false;
    for (auto& kv : it->second)
        if (! sameValue (kv.second, currentValue (kv.first)))
            return true;
    return false;
}

bool PresetManager::isChainModified() const
{
    if (chainName.isEmpty())
        return false;
    for (auto& kv : chainSnapshot)
        if (! sameValue (kv.second, currentValue (kv.first)))
            return true;
    return false;
}

void PresetManager::writeState (juce::ValueTree& tree) const
{
    tree.setProperty ("preset_chain", chainName, nullptr);
    for (auto& kv : moduleNames)
        tree.setProperty ("preset_" + kv.first, kv.second, nullptr);
}

void PresetManager::readState (const juce::ValueTree& tree)
{
    chainName = tree.getProperty ("preset_chain", "").toString();
    moduleNames.clear();
    for (auto& s : scopes())
    {
        const auto n = tree.getProperty (juce::String ("preset_") + s.key, "").toString();
        if (n.isNotEmpty())
            moduleNames[s.key] = n;
    }
    // A restored session is the reference point for "modified".
    moduleSnapshots.clear();
    for (auto& kv : moduleNames)
        moduleSnapshots[kv.first] = snapshot (moduleParameterIds (kv.first));
    chainSnapshot = chainName.isNotEmpty() ? snapshot (allParameterIds()) : std::map<juce::String, juce::var>();
}

juce::StringArray PresetManager::validateFactoryData() const
{
    juce::StringArray problems;
    if (modules.size() != scopes().size())
        problems.add ("expected " + juce::String ((int) scopes().size()) + " modules, found " + juce::String ((int) modules.size()));

    auto checkValue = [&] (const juce::String& where, const juce::String& id, const juce::var& v)
    {
        auto* p = apvts.getParameter (id);
        if (p == nullptr)
            return;   // module not built yet: skipped at load time
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p))
        {
            if (! v.isString() || ! c->choices.contains (v.toString()))
                problems.add (where + ": " + id + " has no choice '" + v.toString() + "'");
        }
        else if (dynamic_cast<juce::AudioParameterBool*> (p) != nullptr)
        {
            if (! v.isBool())
                problems.add (where + ": " + id + " should be true/false");
        }
        else
        {
            const auto& r = p->getNormalisableRange();
            const double d = (double) v;
            if (v.isString() || d < r.start - 1e-6 || d > r.end + 1e-6)
                problems.add (where + ": " + id + " = " + v.toString() + " is outside " + juce::String (r.start) + " .. " + juce::String (r.end));
        }
    };

    for (auto& m : modules)
    {
        auto* scope = findScope (m.key);
        juce::StringArray names;
        for (auto& p : m.factory)
        {
            if (names.contains (p.name))
                problems.add (m.key + ": duplicate preset name " + p.name);
            names.add (p.name);
            for (auto& kv : p.values)
            {
                if (! inScope (*scope, kv.first))
                    problems.add (m.key + "/" + p.name + ": " + kv.first + " is not a " + m.key + " parameter");
                checkValue (m.key + "/" + p.name, kv.first, kv.second);
            }
        }
    }
    for (auto& c : chains)
    {
        for (auto& kv : c.modules)
        {
            auto* m = findModule (kv.first);
            bool found = false;
            if (m != nullptr)
                for (auto& p : m->factory)
                    found = found || p.name == kv.second;
            if (! found)
                problems.add ("chain " + c.name + ": unknown " + kv.first + " preset '" + kv.second + "'");
        }
        for (auto& kv : c.values)
            checkValue ("chain " + c.name, kv.first, kv.second);
    }
    return problems;
}
