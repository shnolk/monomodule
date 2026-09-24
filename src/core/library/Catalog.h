// The catalog: a read-only view over the immutable imports that gives every preset (a kit track), kit and
// used pattern a content-based identity and links them to each other. Identical items found in several
// dumps (the same factory kit imported twice, one track used in three kits) merge into one entry that lists
// its sources, so the UI can show "in kits", "used in patterns" and "from dumps" without touching the
// archival record. Rebuilt from the dumps whenever the library changes; nothing here is stored.
//
// Identity: a preset is its machine, the 72 parameters, the ASSIGN matrix and the two key-tracking flags
// (level and routing are the kit's mix settings and stay with the kit); a kit is its whole content; a
// pattern is its content plus the kit it resolves to (slot `kit` of its own dump), so a pattern entry
// always has one kit. GND tracks, empty kit slots and empty patterns are not catalogued.
//
// Later stages add saved and edited items as files of their own (presets/, kits/, patterns/ with a
// `parent` reference); they join the catalog with the same kind of id, and `Source::kind` tells them apart.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "MnmDump.h"

namespace mnm::catalog {

// Where an item was found: a slot of a project's current version (importId / importName = the project's id and
// name), or a sound saved from a plugin (importId empty, importName = "Saved from ...").
struct Source {
    std::string importId, importName;
    int slot = -1;     // kit or pattern slot in that state
    int track = -1;    // presets: the track of that kit
    bool saved() const { return importId.empty(); }
};

// A sound saved from a plugin (the store's items/): it joins the catalog under its own name, with a link to the item
// it was made from. For a preset only kit.tracks[track] and its key-tracking bits matter.
struct SavedInput {
    std::string itemId, name, parent, savedFrom, time;
    bool isKit = false;
    int track = 0;
    dump::Kit kit;
};

struct PresetItem {
    std::string id;                  // content hash
    std::string name;                // "<KIT NAME> T<n>" of the first source
    uint8_t model = 0;               // machine (host::Machine value)
    dump::KitTrack track;            // the data (from the first source)
    bool lpKeyTrack = true, hpKeyTrack = true;
    bool saved = false;              // saved from a plugin (its name is the user's)
    std::string itemId, parentId, savedFrom, savedAt;
    std::vector<Source> sources;
    std::vector<std::string> kitIds;       // kits containing it, first-found order
    std::vector<std::string> patternIds;   // patterns whose kit contains it
};

struct KitItem {
    std::string id;
    std::string name;
    dump::Kit kit;                   // the data (from the first source; its position is that source's slot)
    std::string presetIds[6];        // empty for a GND track
    bool saved = false;
    std::string itemId, parentId, savedFrom, savedAt;
    std::vector<Source> sources;
    std::vector<std::string> patternIds;
};

struct PatternItem {
    std::string id;
    std::string name;                // "<slot> <import name>" of the first source, e.g. "A01 My Dump"
    dump::Pattern pattern;
    std::string kitId;               // empty when the pattern's kit slot is empty
    std::string presetIds[6];        // the kit's presets (empty without a kit / for GND tracks)
    std::vector<Source> sources;
};

// One import to catalogue.
struct Input {
    std::string importId, importName;
    const dump::Dump* dump = nullptr;
};

struct Catalog {
    std::vector<PresetItem> presets;
    std::vector<KitItem> kits;
    std::vector<PatternItem> patterns;

    // Replaces the contents; inputs in library order (newest first is fine). Saved sounds come first, so an item
    // that also sits in a project keeps the name the user gave it.
    void build(const std::vector<Input>& inputs, const std::vector<SavedInput>& saved = {});
    // A one-track kit carrying a preset (track 0, OUT AB), for the code paths that take a kit + track.
    static dump::Kit kitForPreset(const PresetItem& p);
    void clear();

    const PresetItem* preset(const std::string& id) const;
    const KitItem* kit(const std::string& id) const;
    const PatternItem* pattern(const std::string& id) const;
    // The catalog id of a dump slot, or "" when that slot is not catalogued (empty kit / empty pattern / GND track).
    std::string kitIdOf(const std::string& importId, int slot) const;
    std::string patternIdOf(const std::string& importId, int slot) const;
    std::string presetIdOf(const std::string& importId, int kitSlot, int track) const;

    // Content hashes (32 hex characters), also used by later stages for saved items.
    static std::string presetHash(const dump::KitTrack& t, bool lpKeyTrack, bool hpKeyTrack);
    static std::string kitHash(const dump::Kit& k);
    static std::string patternHash(const dump::Pattern& p, const std::string& kitId);

private:
    std::map<std::string, size_t> m_presetIndex, m_kitIndex, m_patternIndex;
    std::map<std::string, std::string> m_kitBySlot, m_patternBySlot, m_presetBySlot;   // "<import>/<slot>[/<track>]" -> id
};

} // namespace mnm::catalog
