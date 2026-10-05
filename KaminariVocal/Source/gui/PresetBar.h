#pragma once

#include "../state/PresetManager.h"
#include "Look.h"

// Preset selector: previous / name menu / next / save. moduleKey empty = chain (whole plug-in) presets.
// The name shows "*" when parameters changed since the preset was loaded.
class PresetBar : public juce::Component, private juce::Timer
{
public:
    PresetBar (PresetManager& manager, const juce::String& module, const juce::String& what)
        : presets (manager), moduleKey (module), noun (what)
    {
        prev.setButtonText ("<");
        next.setButtonText (">");
        save.setButtonText ("Save");
        prev.setTooltip ("Previous " + noun + " preset");
        next.setTooltip ("Next " + noun + " preset");
        save.setTooltip ("Save the current settings as a user " + noun + " preset");
        prev.setTitle ("Previous " + noun + " preset");
        next.setTitle ("Next " + noun + " preset");
        save.setTitle ("Save " + noun + " preset");
        name.setTitle (noun + " preset");
        prev.onClick = [this] { step (-1); };
        next.onClick = [this] { step (1); };
        name.onClick = [this] { showMenu(); };
        save.onClick = [this] { askName(); };
        for (auto* c : { &prev, &name, &next, &save })
            addAndMakeVisible (c);
        refresh();
        startTimerHz (4);
    }

    ~PresetBar() override { stopTimer(); }

    void resized() override
    {
        auto b = getLocalBounds();
        prev.setBounds (b.removeFromLeft (26));
        b.removeFromLeft (2);
        save.setBounds (b.removeFromRight (52));
        b.removeFromRight (4);
        next.setBounds (b.removeFromRight (26));
        b.removeFromRight (2);
        name.setBounds (b);
    }

    // Shown on the name button: "<name>" or "<name> *" when modified, "No preset" before any load.
    juce::String displayText() const
    {
        const auto current = isChain() ? presets.currentChainPreset() : presets.currentModulePreset (moduleKey);
        if (current.isEmpty())
            return "No preset";
        const bool modified = isChain() ? presets.isChainModified() : presets.isModuleModified (moduleKey);
        return current + (modified ? " *" : "");
    }

    std::vector<PresetManager::Preset> list() const
    {
        return isChain() ? presets.listChainPresets() : presets.listModulePresets (moduleKey);
    }

    bool load (const PresetManager::Preset& p)
    {
        const bool ok = isChain() ? presets.loadChainPreset (p) : presets.loadModulePreset (moduleKey, p);
        refresh();
        if (onLoaded) onLoaded();
        return ok;
    }

    std::function<void()> onLoaded;   // lets the editor refresh other bars after a chain load
    juce::TextButton prev, name, next, save;

    void refresh()
    {
        const auto text = displayText();
        if (name.getButtonText() != text)
            name.setButtonText (text);
        juce::String description;
        const auto current = isChain() ? presets.currentChainPreset() : presets.currentModulePreset (moduleKey);
        for (auto& p : list())
            if (p.name == current)
                description = p.description;
        name.setTooltip ((description.isNotEmpty() ? current + ": " + description + "\n" : juce::String())
                         + "Click to choose a " + noun + " preset."
                         + (isChain() ? juce::String() : "\nA " + noun + " preset changes only the " + noun + " settings."));
    }

private:
    bool isChain() const { return moduleKey.isEmpty(); }

    void timerCallback() override { refresh(); }

    void step (int delta)
    {
        if (isChain()) presets.stepChainPreset (delta);
        else presets.stepModulePreset (moduleKey, delta);
        refresh();
        if (onLoaded) onLoaded();
    }

    void showMenu()
    {
        const auto items = list();
        const auto current = isChain() ? presets.currentChainPreset() : presets.currentModulePreset (moduleKey);
        juce::PopupMenu m;
        m.addSectionHeader ("Factory");
        bool anyUser = false;
        for (auto& p : items)
        {
            if (! p.factory && ! anyUser)
            {
                m.addSectionHeader ("User");
                anyUser = true;
            }
            m.addItem (p.name, true, p.name == current, [this, p] { load (p); });
        }
        m.addSeparator();
        m.addItem ("Save As...", [this] { askName(); });
        for (auto& p : items)
            if (! p.factory && p.name == current)
                m.addItem ("Delete \"" + p.name + "\"", [this, p] { presets.deleteUserPreset (p); refresh(); });
        m.addItem ("Show User Presets Folder", [this]
        {
            auto folder = presets.getUserFolder();
            folder.createDirectory();
            folder.revealToUser();
        });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&name));
    }

    void askName()
    {
        auto* w = new juce::AlertWindow ("Save " + noun + " preset", "Name for the user preset:", juce::MessageBoxIconType::NoIcon, this);
        const auto current = isChain() ? presets.currentChainPreset() : presets.currentModulePreset (moduleKey);
        w->addTextEditor ("name", current.isNotEmpty() ? current + " copy" : "My " + noun);
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<PresetBar> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
        {
            if (safe != nullptr && result == 1)
            {
                const auto text = w->getTextEditorContents ("name");
                if (safe->isChain()) safe->presets.saveChainPreset (text);
                else safe->presets.saveModulePreset (safe->moduleKey, text);
                safe->refresh();
            }
        }), true);
    }

    PresetManager& presets;
    juce::String moduleKey, noun;
};
