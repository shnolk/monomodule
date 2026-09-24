// The library as the plugins see it: the store, the current state of every project, the sounds saved from the
// plugins, and the catalog over both. Loaded lazily (parsing the projects takes a moment) and refreshed when the
// store's change stamp moves; one instance is shared by all plugin instances of a process
// (juce::SharedResourcePointer<LibraryModel>). Message thread only.
#pragma once
#include "Store.h"
#include "library/Catalog.h"

namespace mnm::library {

class LibraryModel {
public:
    LibraryModel() = default;
    // Loads on first use, reloads when the library changed on disk. Returns true when the catalog was rebuilt.
    bool refresh(bool force = false);
    const mnm::catalog::Catalog& catalog() const { return m_catalog; }
    const std::vector<ProjectInfo>& projects() const { return m_projects; }
    const UserData& user() const { return m_user; }
    const mnm::dump::Dump* state(const juce::String& projectId) const { auto it = m_states.find(projectId); return it == m_states.end() ? nullptr : &it->second; }
    int revision() const { return m_revision; }   // bumps with every rebuild: views compare it

    // Where a catalogued preset / kit sits in a project (its first project source), if anywhere.
    struct Slot { juce::String projectId, projectName; int kit = -1, track = -1; bool valid() const { return projectId.isNotEmpty(); } };
    Slot projectSlotOfPreset(const std::string& presetId) const;
    Slot projectSlotOfKit(const std::string& kitId) const;

    // Saving from a plugin never overwrites: the sound becomes an item of its own (with a link to what it was made
    // from). `into` valid = it is also put into that project slot, as a new version of the project, so the next
    // sysex export carries the edit back to the hardware.
    juce::Result savePreset(const mnm::dump::Kit& kit, int track, const juce::String& name, const juce::String& parentId,
                            const juce::String& savedFrom, const Slot& into, juce::String* presetIdOut = nullptr);
    juce::Result saveKit(const mnm::dump::Kit& kit, const juce::String& name, const juce::String& parentId,
                         const juce::String& savedFrom, const Slot& into, juce::String* kitIdOut = nullptr);

private:
    Store m_store;
    std::vector<ProjectInfo> m_projects;
    std::map<juce::String, mnm::dump::Dump> m_states;
    std::map<juce::String, int> m_stateVersion;
    std::vector<SavedItem> m_saved;
    UserData m_user;
    mnm::catalog::Catalog m_catalog;
    juce::int64 m_stamp = -1;
    int m_revision = 0;
};

} // namespace mnm::library
