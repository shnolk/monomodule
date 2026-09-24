#include "LibraryModel.h"
#include "library/Project.h"
#include <cstring>

namespace mnm::library {

using namespace mnm::dump;
using mnm::catalog::Catalog;

bool LibraryModel::refresh(bool force)
{
    const auto stamp = m_store.changeStamp();
    if (!force && stamp == m_stamp && m_revision > 0) return false;
    m_projects = m_store.listProjects();
    m_stamp = m_store.changeStamp();   // listProjects may have migrated
    for (auto it = m_states.begin(); it != m_states.end();) {
        bool alive = false;
        for (const auto& p : m_projects) alive = alive || p.id == it->first;
        if (alive) ++it; else { m_stateVersion.erase(it->first); it = m_states.erase(it); }
    }
    std::vector<mnm::catalog::Input> inputs;
    for (const auto& p : m_projects) {
        if (!p.current()) continue;
        if (m_stateVersion[p.id] != p.current()->n) {
            Dump d;
            if (!m_store.loadVersion(p.id, p.current()->n, d)) continue;
            m_states[p.id] = std::move(d); m_stateVersion[p.id] = p.current()->n;
        }
        inputs.push_back({p.id.toStdString(), p.name.toStdString(), &m_states[p.id]});
    }
    m_saved = m_store.listSavedItems();
    m_user = m_store.loadUser();
    std::vector<mnm::catalog::SavedInput> saved;
    for (const auto& s : m_saved) saved.push_back({s.id.toStdString(), s.name.toStdString(), s.parent.toStdString(), s.savedFrom.toStdString(), s.time.formatted("%d %b %Y").toStdString(), s.isKit, s.track, s.kit});
    m_catalog.build(inputs, saved);
    ++m_revision;
    return true;
}

LibraryModel::Slot LibraryModel::projectSlotOfPreset(const std::string& presetId) const
{
    if (const auto* p = m_catalog.preset(presetId))
        for (const auto& s : p->sources) if (!s.saved()) return {juce::String(s.importId), juce::String(s.importName), s.slot, s.track};
    return {};
}

LibraryModel::Slot LibraryModel::projectSlotOfKit(const std::string& kitId) const
{
    if (const auto* k = m_catalog.kit(kitId))
        for (const auto& s : k->sources) if (!s.saved()) return {juce::String(s.importId), juce::String(s.importName), s.slot, -1};
    return {};
}

juce::Result LibraryModel::savePreset(const Kit& kit, int track, const juce::String& name, const juce::String& parentId,
                                      const juce::String& savedFrom, const Slot& into, juce::String* presetIdOut)
{
    SavedItem item;
    item.name = name; item.parent = parentId; item.savedFrom = savedFrom; item.kit = kit; item.track = track; item.isKit = false;
    auto r = m_store.saveItem(item);
    if (r.failed()) return r;
    if (presetIdOut) *presetIdOut = juce::String(Catalog::presetHash(kit.tracks[track], kit.lpKeyTracks(track), kit.hpKeyTracks(track)));
    if (into.valid() && into.kit >= 0 && into.track >= 0) {
        ProjectInfo p; Dump d;
        if (m_store.loadProject(into.projectId, p) && p.current() && m_store.loadVersion(p.id, p.current()->n, d))
            for (auto& k : d.kits)
                if (k.position == into.kit && !k.isEmptySlot()) {
                    const uint8_t type = k.tracks[into.track].type, level = k.tracks[into.track].level;   // routing and level are the kit's
                    k.tracks[into.track] = kit.tracks[track];
                    k.tracks[into.track].type = type; k.tracks[into.track].level = level;
                    const uint8_t bit = uint8_t(1u << into.track);
                    k.lpKeyTrack = uint8_t((k.lpKeyTrack & ~bit) | (kit.lpKeyTracks(track) ? bit : 0));
                    k.hpKeyTrack = uint8_t((k.hpKeyTrack & ~bit) | (kit.hpKeyTracks(track) ? bit : 0));
                    r = m_store.addVersion(p.id, d, "saved", "Saved from " + savedFrom, {"Kit " + juce::String(into.kit + 1).paddedLeft('0', 3) + " " + juce::String(k.name) + ": track " + juce::String(into.track + 1) + " is now " + name}, {}, p.current()->n);
                    break;
                }
    }
    refresh(true);
    return r;
}

juce::Result LibraryModel::saveKit(const Kit& kit, const juce::String& name, const juce::String& parentId,
                                   const juce::String& savedFrom, const Slot& into, juce::String* kitIdOut)
{
    SavedItem item;
    item.name = name; item.parent = parentId; item.savedFrom = savedFrom; item.kit = kit; item.isKit = true;
    // the plugin's kit has no name bytes: the item's name becomes the kit's
    item.kit.name = name.toUpperCase().substring(0, 10).toStdString();
    std::memset(item.kit.nameRaw, 0, sizeof(item.kit.nameRaw));
    std::memcpy(item.kit.nameRaw, item.kit.name.data(), std::min<size_t>(10, item.kit.name.size()));
    auto r = m_store.saveItem(item);
    if (r.failed()) return r;
    if (kitIdOut) *kitIdOut = juce::String(Catalog::kitHash(item.kit));
    if (into.valid() && into.kit >= 0) {
        ProjectInfo p; Dump d;
        if (m_store.loadProject(into.projectId, p) && p.current() && m_store.loadVersion(p.id, p.current()->n, d)) {
            Kit placed = item.kit;
            if (const auto* old = d.kitAt(into.kit); old && !old->isEmptySlot()) { placed.name = old->name; std::memcpy(placed.nameRaw, old->nameRaw, sizeof(placed.nameRaw)); }   // the slot keeps its name
            mnm::project::putKit(d, into.kit, placed);
            r = m_store.addVersion(p.id, d, "saved", "Saved from " + savedFrom, {"Kit " + juce::String(into.kit + 1).paddedLeft('0', 3) + " " + juce::String(placed.name) + " replaced by the kit from the plugin"}, {}, p.current()->n);
        }
    }
    refresh(true);
    return r;
}

} // namespace mnm::library
