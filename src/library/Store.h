// The on-disk Monomachine library, shared by the standalone app and the plugins.
//
//   <root>/projects/<id>/project.json              name, device, pack flag
//   <root>/projects/<id>/versions/<nnnn>/version.json   kind (imported | saved | exported | restored), time, parent,
//                                                  title, change list, note, counts, stateOf (see below)
//   <root>/projects/<id>/versions/<nnnn>/dump.json      the full decoded state ("format": 2), rebuilds the sysex
//   <root>/projects/<id>/versions/<nnnn>/original.syx   imports: the bytes as received
//   <root>/projects/<id>/versions/<nnnn>/export.syx     exports: the bytes that were written
//   <root>/items/presets/<id>.json, items/kits/<id>.json   sounds saved from the plugins (name, tags, parent, the kit)
//   <root>/user.json                               favourites and tags, keyed by catalog id
//
// A PROJECT is one Monomachine's memory over time. Its VERSIONS are immutable: an import, a saved edit session, an
// export and a restore each add one; nothing is ever rewritten, so every earlier state stays reachable. Exports and
// restores do not change the state, so they carry no dump.json of their own: "stateOf" names the version whose
// state they are. A PACK is a project that is only a source of sounds (imported as "items only").
//
// <root> = ~/Library/Application Support/Shnolk/Monomachine/Library, or $MNM_LIBRARY_DIR. Ids are time-ordered
// random strings; names are metadata, never file names. The pre-1.1 layout (imports/<id>/) is migrated on first
// use: each import becomes a project with one version; the old folder is kept beside it as imports-migrated/.
#pragma once
#include <juce_core/juce_core.h>
#include "library/MnmDump.h"
#include "library/Project.h"
#include <map>
#include <optional>
#include <vector>

namespace mnm::library {

juce::String newId();   // sortable: ms since the epoch in base 36, then 6 random characters

struct VersionInfo {
    int n = 0;                      // 1, 2, 3 ...
    juce::String kind;              // imported | saved | exported | restored
    juce::String title, note, sourceFile, exportFile;
    juce::StringArray changes;
    juce::Time time;
    int parent = 0;                 // the version this one was made from (0 = none)
    int stateOf = 0;                // the version holding this one's state (itself for imported / saved)
    int kits = 0, patterns = 0, usedPatterns = 0, namedKits = 0, songs = 0, globals = 0, damaged = 0, unknown = 0;
    juce::File dir;
    juce::String label() const { return "v" + juce::String(n); }
};

struct ProjectInfo {
    juce::String id, name, device;
    bool pack = false;                       // sounds only: not shown as a unit's memory
    std::vector<VersionInfo> versions;       // newest first
    juce::File dir;
    const VersionInfo* current() const { return versions.empty() ? nullptr : &versions.front(); }
    const VersionInfo* version(int n) const { for (const auto& v : versions) if (v.n == n) return &v; return nullptr; }
};

enum class ImportMode { NewProject, NewVersion, Pack };

// A sound saved from a plugin. The kit carries the data: for a preset only tracks[track] matters.
struct SavedItem {
    juce::String id, name, parent, savedFrom;   // parent = the catalog id it was made from ("" = none)
    juce::StringArray tags;
    juce::Time time;
    bool isKit = false;
    int track = 0;
    mnm::dump::Kit kit;
};

struct UserData {
    juce::StringArray favourites;                         // catalog ids
    std::map<juce::String, juce::StringArray> tags;       // catalog id -> tags
    bool isFavourite(const juce::String& id) const { return favourites.contains(id); }
    juce::StringArray allTags() const;
};

class Store {
public:
    static juce::File defaultRoot();
    explicit Store(juce::File root = defaultRoot());
    const juce::File& root() const { return m_root; }

    // Projects, newest first (by id). Also runs the one-time migrations.
    std::vector<ProjectInfo> listProjects();
    bool loadProject(const juce::String& id, ProjectInfo& out) const;
    bool renameProject(const juce::String& id, const juce::String& name);
    bool deleteProject(const juce::String& id);

    // Import: a new project, a new version of `projectId`, or a pack. The file is always archived as received.
    juce::Result importSysexFile(const juce::File& syx, ImportMode mode, const juce::String& projectId, ProjectInfo* out = nullptr);
    juce::Result importSysexData(const void* data, size_t size, const juce::String& name, const juce::String& sourceFile,
                                 ImportMode mode, const juce::String& projectId, ProjectInfo* out = nullptr);
    // The project whose current state a fresh dump resembles most (at least half of the used slots identical).
    struct Similar { juce::String projectId, projectName; int version = 0; mnm::project::DumpDiff diff; };
    std::optional<Similar> findSimilar(const mnm::dump::Dump& dump);

    bool loadVersion(const juce::String& projectId, int n, mnm::dump::Dump& out) const;   // follows stateOf
    juce::File originalFile(const juce::String& projectId, int n) const;                  // imports only
    // A new version with a new state (a saved edit session, a save from a plugin).
    juce::Result addVersion(const juce::String& projectId, const mnm::dump::Dump& state, const juce::String& kind, const juce::String& title,
                            const juce::StringArray& changes, const juce::String& note, int parent, VersionInfo* out = nullptr);
    // Restore: a new version equal to an old one.
    juce::Result restoreVersion(const juce::String& projectId, int n, VersionInfo* out = nullptr);
    // Export to `dest` and record it. kits / patterns null = the whole state (unedited messages byte for byte).
    juce::Result exportVersion(const juce::String& projectId, int n, const juce::File& dest, const std::vector<int>* kits,
                               const std::vector<int>* patterns, VersionInfo* out = nullptr);
    int lastExportedOrImported(const ProjectInfo& p) const;   // the version a "changed since" export compares with

    // Sounds saved from the plugins.
    juce::Result saveItem(const SavedItem& item, juce::String* idOut = nullptr);
    std::vector<SavedItem> listSavedItems() const;
    bool deleteItem(const juce::String& id);

    UserData loadUser() const;
    bool saveUser(const UserData& u) const;

    // Cheap change signal for pollers (app and plugins).
    juce::int64 changeStamp() const;

private:
    juce::File projectDir(const juce::String& id) const { return m_root.getChildFile("projects").getChildFile(id); }
    juce::File versionDir(const juce::String& id, int n) const { return projectDir(id).getChildFile("versions").getChildFile(juce::String(n).paddedLeft('0', 4)); }
    bool readVersion(const juce::File& dir, VersionInfo& out) const;
    juce::Result writeVersion(const juce::String& projectId, VersionInfo& v, const mnm::dump::Dump* state);
    int nextVersionNumber(const juce::String& projectId) const;
    void migrateLegacyLayout();
    void migrateImports();
    juce::File m_root;
    bool m_migrated = false;
};

// JSON form of a dump ("format": 2). Lossless: dumpFromJson(dumpToJson(d)) encodes to the same bytes.
juce::var dumpToJson(const mnm::dump::Dump& dump);
bool dumpFromJson(const juce::var& json, mnm::dump::Dump& out);
juce::var kitToJson(const mnm::dump::Kit& kit);
bool kitFromJson(const juce::var& json, mnm::dump::Kit& out);
juce::var patternToJson(const mnm::dump::Pattern& pat);
bool patternFromJson(const juce::var& json, mnm::dump::Pattern& out);

// Names used in the JSON and the UI.
const char* fxInputName(int input);    // 0..6 -> NEIBOR .. BUS EF
juce::String outBusName(int mask);     // bits AB/CD/EF -> "AB+CD", "-" for none
const char* assignSourceName(int s);   // +PB -PB +MW -MW VEL KEY

} // namespace mnm::library
