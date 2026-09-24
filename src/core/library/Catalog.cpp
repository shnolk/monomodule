#include "Catalog.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace mnm::catalog {

using namespace mnm::dump;

namespace {

// Two independent 64-bit FNV-1a streams over the same bytes, printed as 32 hex characters.
struct Hasher {
    uint64_t a = 0xcbf29ce484222325ull, b = 0x84222325cbf29ce4ull;
    void put(const void* data, size_t n)
    {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < n; ++i) {
            a = (a ^ p[i]) * 0x100000001b3ull;
            b = (b ^ (p[i] + 0x9e)) * 0x100000001b3ull;
        }
    }
    void put8(uint8_t v) { put(&v, 1); }
    void put16(uint16_t v) { put8(uint8_t(v)); put8(uint8_t(v >> 8)); }
    void put32(uint32_t v) { put16(uint16_t(v)); put16(uint16_t(v >> 16)); }
    void put64(uint64_t v) { put32(uint32_t(v)); put32(uint32_t(v >> 32)); }
    void putStr(const std::string& s) { put32(uint32_t(s.size())); put(s.data(), s.size()); }
    std::string hex() const
    {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%016llx%016llx", (unsigned long long)a, (unsigned long long)b);
        return buf;
    }
};

void hashTrack(Hasher& h, const KitTrack& t)
{
    h.put8(t.model);
    h.put(t.params, sizeof(t.params));
    h.put(t.destPage, sizeof(t.destPage));
    h.put(t.destParam, sizeof(t.destParam));
    h.put(t.destRange, sizeof(t.destRange));
}

std::string slotKey(const std::string& importId, int slot, int track = -1)
{
    return importId + "/" + std::to_string(slot) + (track >= 0 ? "/" + std::to_string(track) : "");
}

template <typename T>
void addUnique(std::vector<T>& v, const T& x) { if (std::find(v.begin(), v.end(), x) == v.end()) v.push_back(x); }

} // namespace

// ---------------------------------------------------------------------------
// Hashes

std::string Catalog::presetHash(const KitTrack& t, bool lp, bool hp)
{
    Hasher h;
    h.putStr("preset1");
    hashTrack(h, t);
    h.put8(lp ? 1 : 0); h.put8(hp ? 1 : 0);
    return h.hex();
}

std::string Catalog::kitHash(const Kit& k)
{
    Hasher h;
    h.putStr("kit1");
    h.put(k.nameRaw, sizeof(k.nameRaw));
    h.put8(k.version); h.put8(k.revision); h.put8(k.unused461);
    for (const auto& t : k.tracks) { hashTrack(h, t); h.put8(t.type); h.put8(t.level); }
    h.put16(k.patchBusIn);
    h.put(k.trigTracks, sizeof(k.trigTracks));
    h.put8(k.mirrorLR); h.put8(k.mirrorUD); h.put8(k.lpKeyTrack); h.put8(k.hpKeyTrack);
    h.put8(k.trigPortamento); h.put8(k.trigLegatoAmp); h.put8(k.trigLegatoFilter); h.put8(k.trigLegatoLFO);
    h.put8(k.commonMultimode); h.put8(k.commonTiming); h.put8(k.splitKey); h.put8(k.splitRange);
    h.put32(uint32_t(k.tail.size())); if (!k.tail.empty()) h.put(k.tail.data(), k.tail.size());
    return h.hex();
}

std::string Catalog::patternHash(const Pattern& p, const std::string& kitId)
{
    Hasher h;
    h.putStr("pattern1");
    h.putStr(kitId);
    h.put8(p.version); h.put8(p.revision);
    for (const uint64_t* g : {p.ampTrigs, p.filterTrigs, p.lfoTrigs, p.offTrigs, p.midiNoteOnTrigs, p.midiNoteOffTrigs, p.triglessTrigs,
                              p.chordTrigs, p.midiTriglessTrigs, p.slidePatterns, p.swingPatterns, p.midiSlidePatterns, p.midiSwingPatterns, p.lockPatterns})
        for (int t = 0; t < 6; ++t) h.put64(g[t]);
    h.put32(p.swingAmount);
    h.put(p.noteNBR, sizeof(p.noteNBR));
    h.put8(p.patternLength); h.put8(p.doubleTempo); h.put8(p.kit); h.put8(uint8_t(p.patternTranspose));
    h.put(p.transpose, sizeof(p.transpose)); h.put(p.scale, sizeof(p.scale)); h.put(p.key, sizeof(p.key));
    h.put(p.midiTranspose, sizeof(p.midiTranspose)); h.put(p.midiScale, sizeof(p.midiScale)); h.put(p.midiKey, sizeof(p.midiKey));
    for (const uint8_t* a : {p.arpPlay, p.arpMode, p.arpOctaveRange, p.arpMultiplier, p.arpDestination, p.arpLength,
                             p.midiArpPlay, p.midiArpMode, p.midiArpOctaveRange, p.midiArpMultiplier, p.midiArpLength})
        h.put(a, 6);
    h.put(p.arpPattern, sizeof(p.arpPattern)); h.put(p.midiArpPattern, sizeof(p.midiArpPattern));
    h.put(p.unused, sizeof(p.unused));
    h.put16(p.midiNotesUsed); h.put8(p.chordNotesUsed); h.put8(p.unused2); h.put8(p.locksUsed);
    h.put(p.locks, sizeof(p.locks));   // the canonical lock view (stale bytes of unassigned rows do not make a different pattern)
    for (int i = 0; i < 400; ++i) h.put16(p.midiNotes[i]);
    for (int i = 0; i < 192; ++i) h.put16(p.chordNotes[i]);
    h.put8(p.trailing);
    h.put32(uint32_t(p.tail.size())); if (!p.tail.empty()) h.put(p.tail.data(), p.tail.size());
    return h.hex();
}

// ---------------------------------------------------------------------------
// Build

void Catalog::clear()
{
    presets.clear(); kits.clear(); patterns.clear();
    m_presetIndex.clear(); m_kitIndex.clear(); m_patternIndex.clear();
    m_kitBySlot.clear(); m_patternBySlot.clear(); m_presetBySlot.clear();
}

dump::Kit Catalog::kitForPreset(const PresetItem& p)
{
    Kit k;
    k.name = p.name.substr(0, 10);
    k.tracks[0] = p.track;
    if ((k.tracks[0].type & 7) == 0) k.tracks[0].type |= 1;   // OUT AB
    k.lpKeyTrack = p.lpKeyTrack ? 0x3F : 0; k.hpKeyTrack = p.hpKeyTrack ? 0x3F : 0;
    return k;
}

void Catalog::build(const std::vector<Input>& inputs, const std::vector<SavedInput>& saved)
{
    clear();
    auto addPreset = [&](const KitTrack& tr, bool lp, bool hp, const std::string& name) -> PresetItem& {
        const std::string pid = presetHash(tr, lp, hp);
        auto pit = m_presetIndex.find(pid);
        if (pit == m_presetIndex.end()) {
            PresetItem p;
            p.id = pid; p.name = name; p.model = tr.model; p.track = tr; p.lpKeyTrack = lp; p.hpKeyTrack = hp;
            pit = m_presetIndex.emplace(pid, presets.size()).first;
            presets.push_back(std::move(p));
        }
        return presets[pit->second];
    };
    // a kit and its six presets; returns the kit's index
    auto addKit = [&](const Kit& k, const std::string& importId, const std::string& importName) -> size_t {
        const std::string kid = kitHash(k);
        auto it = m_kitIndex.find(kid);
        if (it == m_kitIndex.end()) {
            KitItem item;
            item.id = kid; item.name = k.name; item.kit = k;
            it = m_kitIndex.emplace(kid, kits.size()).first;
            kits.push_back(std::move(item));
        }
        const size_t index = it->second;
        kits[index].sources.push_back({importId, importName, importId.empty() ? -1 : k.position, -1});
        if (!importId.empty()) m_kitBySlot[slotKey(importId, k.position)] = kid;
        for (int t = 0; t < 6; ++t) {
            const auto& tr = k.tracks[t];
            if (tr.model == 0) continue;   // GND ---: nothing to preset
            auto& preset = addPreset(tr, k.lpKeyTracks(t), k.hpKeyTracks(t), k.name + " T" + std::to_string(t + 1));
            preset.sources.push_back({importId, importName, importId.empty() ? -1 : k.position, t});
            addUnique(preset.kitIds, kid);
            kits[index].presetIds[t] = preset.id;
            if (!importId.empty()) m_presetBySlot[slotKey(importId, k.position, t)] = preset.id;
        }
        return index;
    };
    // saved sounds first: their names are the user's
    for (const auto& sv : saved) {
        const std::string from = "Saved from " + (sv.savedFrom.empty() ? std::string("a plugin") : sv.savedFrom);
        if (sv.isKit) {
            Kit k = sv.kit;
            if (!sv.name.empty()) k.name = sv.name;   // the user's name (a kit saved from Six has none of its own)
            if (k.isEmptySlot()) continue;
            auto& item = kits[addKit(k, "", from)];
            item.saved = true; item.itemId = sv.itemId; item.parentId = sv.parent; item.savedFrom = sv.savedFrom; item.savedAt = sv.time;
        } else {
            const int t = std::clamp(sv.track, 0, 5);
            if (sv.kit.tracks[t].model == 0) continue;
            auto& p = addPreset(sv.kit.tracks[t], sv.kit.lpKeyTracks(t), sv.kit.hpKeyTracks(t), sv.name);
            p.saved = true; p.itemId = sv.itemId; p.parentId = sv.parent; p.savedFrom = sv.savedFrom; p.savedAt = sv.time;
            p.sources.push_back({"", from, -1, t});
        }
    }
    // kits and their presets next, so patterns can resolve their kit
    for (const auto& in : inputs) {
        if (!in.dump) continue;
        for (const auto& k : in.dump->kits) if (!k.isEmptySlot()) addKit(k, in.importId, in.importName);
    }
    for (const auto& in : inputs) {
        if (!in.dump) continue;
        for (const auto& p : in.dump->patterns) {
            if (p.empty()) continue;
            const std::string kid = kitIdOf(in.importId, p.kit);
            const std::string pid = patternHash(p, kid);
            auto it = m_patternIndex.find(pid);
            if (it == m_patternIndex.end()) {
                PatternItem item;
                item.id = pid; item.name = patternSlotName(p.position) + " " + in.importName; item.pattern = p; item.kitId = kid;
                if (const auto* k = kit(kid)) for (int t = 0; t < 6; ++t) item.presetIds[t] = k->presetIds[t];
                it = m_patternIndex.emplace(pid, patterns.size()).first;
                patterns.push_back(std::move(item));
            }
            patterns[it->second].sources.push_back({in.importId, in.importName, p.position, -1});
            m_patternBySlot[slotKey(in.importId, p.position)] = pid;
        }
    }
    // back links: kit -> patterns, preset -> patterns
    for (const auto& p : patterns) {
        if (auto kit = m_kitIndex.find(p.kitId); kit != m_kitIndex.end()) addUnique(kits[kit->second].patternIds, p.id);
        for (const auto& pid : p.presetIds)
            if (!pid.empty()) if (auto pr = m_presetIndex.find(pid); pr != m_presetIndex.end()) addUnique(presets[pr->second].patternIds, p.id);
    }
}

// ---------------------------------------------------------------------------
// Lookups

const PresetItem* Catalog::preset(const std::string& id) const { auto it = m_presetIndex.find(id); return it == m_presetIndex.end() ? nullptr : &presets[it->second]; }
const KitItem* Catalog::kit(const std::string& id) const { auto it = m_kitIndex.find(id); return it == m_kitIndex.end() ? nullptr : &kits[it->second]; }
const PatternItem* Catalog::pattern(const std::string& id) const { auto it = m_patternIndex.find(id); return it == m_patternIndex.end() ? nullptr : &patterns[it->second]; }

std::string Catalog::kitIdOf(const std::string& importId, int slot) const { auto it = m_kitBySlot.find(slotKey(importId, slot)); return it == m_kitBySlot.end() ? std::string() : it->second; }
std::string Catalog::patternIdOf(const std::string& importId, int slot) const { auto it = m_patternBySlot.find(slotKey(importId, slot)); return it == m_patternBySlot.end() ? std::string() : it->second; }
std::string Catalog::presetIdOf(const std::string& importId, int kitSlot, int track) const { auto it = m_presetBySlot.find(slotKey(importId, kitSlot, track)); return it == m_presetBySlot.end() ? std::string() : it->second; }

} // namespace mnm::catalog
