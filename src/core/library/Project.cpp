#include "Project.h"
#include <algorithm>
#include <cstring>
#include "Catalog.h"

namespace mnm::project {

using namespace mnm::dump;

namespace {
Kit* kitAt(Dump& d, int pos) { for (auto& k : d.kits) if (k.position == pos) return &k; return nullptr; }
Pattern* patternAt(Dump& d, int pos) { for (auto& p : d.patterns) if (p.position == pos) return &p; return nullptr; }

Kit blankKit()
{
    Kit k;
    std::memset(k.nameRaw, 0xFF, sizeof(k.nameRaw));   // as empty slots come off the unit
    for (auto& t : k.trigTracks) t = 255;
    return k;
}
Pattern blankPattern()
{
    Pattern p{};
    p.version = 6; p.revision = 1; p.patternLength = 16;
    std::memset(p.locksRaw, 0xFF, sizeof(p.locksRaw));
    std::memset(p.noteNBR, 0xFF, sizeof(p.noteNBR));
    for (auto& n : p.midiNotes) n = 0xFFFF;
    for (auto& n : p.chordNotes) n = 0xFFFF;
    p.finalizeLocks();
    return p;
}
// The dump's own empty kit / pattern is the best template for a cleared slot: it is what this unit writes.
Kit emptyKitLike(const Dump& d) { for (const auto& k : d.kits) if (k.isEmptySlot()) return k; return blankKit(); }
Pattern emptyPatternLike(const Dump& d)
{
    for (const auto& p : d.patterns) if (p.empty() && p.locksUsed == 0 && p.midiNotesUsed == 0 && p.chordNotesUsed == 0) return p;
    return blankPattern();
}
Kit& kitSlot(Dump& d, int pos)
{
    if (auto* k = kitAt(d, pos)) return *k;
    Kit k = emptyKitLike(d); k.position = pos;
    Message m; m.id = kKitId; m.version = k.version; m.revision = k.revision; m.position = pos; m.kitIndex = int(d.kits.size());
    d.kits.push_back(k); d.messages.push_back(m);
    return d.kits.back();
}
Pattern& patternSlot(Dump& d, int pos)
{
    if (auto* p = patternAt(d, pos)) return *p;
    Pattern p = emptyPatternLike(d); p.position = pos;
    Message m; m.id = kPatternId; m.version = p.version; m.revision = p.revision; m.position = pos; m.patternIndex = int(d.patterns.size());
    d.patterns.push_back(p); d.messages.push_back(m);
    return d.patterns.back();
}
// content into a slot: the slot keeps its position and its header form (extended position byte)
void assignKit(Kit& slot, const Kit& src) { const int pos = slot.position; const bool ext = slot.extendedPosition; const uint8_t ver = slot.version; slot = src; slot.position = pos; slot.extendedPosition = ext; slot.version = ver; }
void assignPattern(Pattern& slot, const Pattern& src) { const int pos = slot.position; slot = src; slot.position = pos; }
std::string patternContentHash(const Pattern& p) { return catalog::Catalog::patternHash(p, ""); }
} // namespace

bool kitInUse(const Dump& d, int pos) { const auto* k = d.kitAt(pos); return k && !k->isEmptySlot(); }
bool patternInUse(const Dump& d, int pos) { const auto* p = d.patternAt(pos); return p && !p->empty(); }
int firstFreeKitSlot(const Dump& d) { for (int i = 0; i < 128; ++i) if (!kitInUse(d, i)) return i; return -1; }

DumpDiff diffDumps(const Dump& a, const Dump& b)
{
    DumpDiff r;
    for (int pos = 0; pos < 128; ++pos) {
        const bool ua = kitInUse(a, pos), ub = kitInUse(b, pos);
        if (ua || ub) {
            ++r.kitsCompared;
            if (ua && ub && catalog::Catalog::kitHash(*a.kitAt(pos)) == catalog::Catalog::kitHash(*b.kitAt(pos))) ++r.kitsSame; else r.kits.push_back(pos);
        }
        const bool pa = patternInUse(a, pos), pb = patternInUse(b, pos);
        if (pa || pb) {
            ++r.patternsCompared;
            if (pa && pb && patternContentHash(*a.patternAt(pos)) == patternContentHash(*b.patternAt(pos))) ++r.patternsSame; else r.patterns.push_back(pos);
        }
    }
    return r;
}

void clearKit(Dump& d, int pos) { assignKit(kitSlot(d, pos), emptyKitLike(d)); }
void clearPattern(Dump& d, int pos) { assignPattern(patternSlot(d, pos), emptyPatternLike(d)); }
void putKit(Dump& d, int pos, const Kit& kit) { assignKit(kitSlot(d, pos), kit); }

void putPattern(Dump& d, int pos, const Pattern& pattern, int kitSlotRef)
{
    auto& slot = patternSlot(d, pos);
    assignPattern(slot, pattern);
    if (kitSlotRef >= 0) slot.kit = uint8_t(kitSlotRef);
}

int putPatternWithKit(Dump& d, int pos, const Pattern& pattern, const Kit& kit)
{
    int target = -1;
    const auto want = catalog::Catalog::kitHash(kit);
    for (int i = 0; i < 128 && target < 0; ++i) if (kitInUse(d, i) && catalog::Catalog::kitHash(*d.kitAt(i)) == want) target = i;
    if (target < 0) { target = firstFreeKitSlot(d); if (target >= 0) putKit(d, target, kit); }
    putPattern(d, pos, pattern, target);
    return target;
}

void swapKits(Dump& d, int a, int b)
{
    if (a == b) return;
    auto& ka = kitSlot(d, a);
    auto& kb = kitSlot(d, b);   // (kitSlot may grow the vector: take both references afterwards)
    Kit ca = *kitAt(d, a), cb = *kitAt(d, b);
    assignKit(*kitAt(d, a), cb); assignKit(*kitAt(d, b), ca);
    (void) ka; (void) kb;
    for (auto& p : d.patterns) { if (p.kit == a) p.kit = uint8_t(b); else if (p.kit == b) p.kit = uint8_t(a); }
}

void swapPatterns(Dump& d, int a, int b)
{
    if (a == b) return;
    patternSlot(d, a); patternSlot(d, b);
    Pattern ca = *patternAt(d, a), cb = *patternAt(d, b);
    assignPattern(*patternAt(d, a), cb); assignPattern(*patternAt(d, b), ca);
}

void copyKit(Dump& d, int from, int to) { if (from == to) return; kitSlot(d, to); const Kit src = kitSlot(d, from); assignKit(*kitAt(d, to), src); }
void copyPattern(Dump& d, int from, int to) { if (from == to) return; patternSlot(d, to); const Pattern src = patternSlot(d, from); assignPattern(*patternAt(d, to), src); }

std::vector<uint8_t> encodeSlots(const Dump& d, const std::vector<int>& kits, const std::vector<int>& patterns)
{
    std::vector<uint8_t> out;
    auto sorted = [](std::vector<int> v) { std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); return v; };
    for (int pos : sorted(kits)) if (const auto* k = d.kitAt(pos)) { const auto m = encodeKit(*k); out.insert(out.end(), m.begin(), m.end()); }
    for (int pos : sorted(patterns)) if (const auto* p = d.patternAt(pos)) { const auto m = encodePattern(*p); out.insert(out.end(), m.begin(), m.end()); }
    return out;
}

ExportCheck checkExport(const Dump& d, const std::vector<int>& kits, const std::vector<int>& patterns, const std::vector<int>& changedKits)
{
    ExportCheck c;
    auto has = [](const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); };
    for (int pos : patterns) {
        const auto* p = d.patternAt(pos);
        if (!p || p->empty()) continue;
        if (!kitInUse(d, p->kit)) c.patternsWithEmptyKit.push_back(pos);
        else if (has(changedKits, p->kit) && !has(kits, p->kit)) c.patternsKitNotIncluded.push_back(pos);
    }
    for (int pos : kits)
        if (const auto* k = d.kitAt(pos); k && !k->isEmptySlot())
            for (const auto& t : k->tracks) if (t.model == 32 || t.model == 33) { c.kitsNeedingMkII.push_back(pos); break; }
    return c;
}

} // namespace mnm::project
