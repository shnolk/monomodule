// A project's state is a decoded dump: one Monomachine's whole memory (128 kit slots, 8 banks x 16 pattern slots,
// songs and globals as received). This file holds what the library does with such states, JUCE-free and tested:
//   - comparing two states slot by slot (what changed between versions; whether a fresh dump is a newer state of
//     a known project),
//   - the slot operations of the app's edit mode (clear, put, move / swap, copy), which keep the dump encodable:
//     a slot's message stays where it is in the file, only its content changes.
// Slots are addressed by position (kits 0-127, patterns 0-127 = A01..H16).
#pragma once
#include <string>
#include <vector>
#include "MnmDump.h"

namespace mnm::project {

struct DumpDiff {
    std::vector<int> kits, patterns;   // positions whose content differs (one side empty counts)
    int kitsCompared = 0, patternsCompared = 0;   // slots that are in use on at least one side
    int kitsSame = 0, patternsSame = 0;           // of those, identical ones
    bool identical() const { return kits.empty() && patterns.empty(); }
    // 0..1: the share of used slots that are identical. Empty slots do not count: two unrelated dumps are both
    // mostly empty.
    double similarity() const { const int n = kitsCompared + patternsCompared; return n ? double(kitsSame + patternsSame) / n : 0.0; }
};
DumpDiff diffDumps(const dump::Dump& a, const dump::Dump& b);

bool kitInUse(const dump::Dump& d, int pos);        // a named kit
bool patternInUse(const dump::Dump& d, int pos);    // a pattern with at least one note trig
int firstFreeKitSlot(const dump::Dump& d);          // -1 when all 128 are named

// Slot operations. They create the slot's message when the dump does not carry one (a partial dump).
void clearKit(dump::Dump& d, int pos);              // back to an empty slot (the dump's own empty kit as the template)
void clearPattern(dump::Dump& d, int pos);
void putKit(dump::Dump& d, int pos, const dump::Kit& kit);
// Puts a pattern into a slot. `kitSlot` >= 0 rewrites the pattern's kit reference (a pattern from another project
// needs its kit placed first; see putPatternWithKit).
void putPattern(dump::Dump& d, int pos, const dump::Pattern& pattern, int kitSlot = -1);
// A pattern from elsewhere together with the kit it was written for: the kit goes into the slot that already holds
// an identical kit, else into the first free kit slot. Returns the kit slot used, or -1 when there is no room (the
// pattern is then placed with its own reference untouched).
int putPatternWithKit(dump::Dump& d, int pos, const dump::Pattern& pattern, const dump::Kit& kit);
// Move = swap: the two slots exchange their content. For kits the patterns' kit references follow, so every
// pattern keeps playing the kit it was written for.
void swapKits(dump::Dump& d, int a, int b);
void swapPatterns(dump::Dump& d, int a, int b);
void copyKit(dump::Dump& d, int from, int to);
void copyPattern(dump::Dump& d, int from, int to);

// The sysex of chosen slots only (kit messages, then pattern messages, in slot order): a partial export.
std::vector<uint8_t> encodeSlots(const dump::Dump& d, const std::vector<int>& kits, const std::vector<int>& patterns);

// Pre-flight checks of an export.
struct ExportCheck {
    std::vector<int> patternsWithEmptyKit;      // used patterns whose kit slot is empty in this state
    std::vector<int> patternsKitNotIncluded;    // partial export: pattern included, its kit changed but is not
    std::vector<int> kitsNeedingMkII;           // kits using DPRO-DDRW / DPRO-DENS
};
ExportCheck checkExport(const dump::Dump& d, const std::vector<int>& kits, const std::vector<int>& patterns, const std::vector<int>& changedKits);

} // namespace mnm::project
