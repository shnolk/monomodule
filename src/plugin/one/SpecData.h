// Display description of every machine's knobs: labels, defaults, display type and value lists, as the
// hardware's LCD presents them. Storage is always raw 0..127 (step 1, MIDI CC 48-55); everything here describes the *display*.
#pragma once
#include <cstdint>
#include "RomArt.h"   // Bitmap, Font and the artwork itself (read from the user's OS file, not generated)

namespace mnm::uispec {

enum class Display : uint8_t { Blank, Numeric, Bipolar, List, Readout };
// Switch: no glyph set in the OS file (LFO TRIG/MULT); drawn as the plain ring with a pointer line per position.
// The other families name an icon array of RomArt.h.
enum class Icons : uint8_t { None, Toggle, FmRatio, EnsPitch, FmDynFrq, SidWave, DproSync, DproWave, VoCons, DdrwWave, LfoPage, LfoWave, LfoDest, Switch };

struct Param {
    const char* label;        // as the LCD shows it ("---" when blank)
    Display display;
    bool tieRight;            // draw the group-tie arch to the knob on the right
    uint8_t defaultRaw;
    uint8_t maxRaw;           // 127 for every knob
    uint8_t valueCount;       // 128 for continuous/readout, N < 128 for a list (Readout: one name per raw step)
    Icons icons;
    const char* const* values;   // valueCount names for List/Readout, else null
};

struct Machine {
    uint8_t index;            // sysex/MCL model number = host::Machine value
    const char* group;
    const char* name;
    const char* displayName;  // "SWAVE-SAW"
    bool isFx;
    bool supported;           // false: the engine path is not implemented yet; the UI shows a preview
    Param params[8];
};

struct SharedPage {
    const char* name;
    const char* labels[8];
    uint8_t defaults[8];
    uint8_t bipolarMask;
};

constexpr int kNumMachines = 22;
extern const Machine kMachines[kNumMachines];   // menu order
extern const SharedPage kSharedPages[3];        // AMP, FILT, EFX
extern const uint8_t kAmpDefaultsFx[8];         // AMP page of an FX track: envelope held open

// LFO page (the three LFOs of a track are identical): PAGE DEST TRIG WAVE MULT SPD INTL DPTH.
constexpr int kNumLfos = 3;
extern const Param kLfoParams[8];
extern const char* const kLfoPageNames[9];
extern const char* const kLfoDestNames[9][8];   // DEST names per PAGE index (PTCH ranges, PAR1-8, AMP, FILT, EFX, LFO x3, MIDI)
extern const char* const kLfoTrigNames[5];
extern const char* const kLfoWaveNames[11];
extern const char* const kLfoMultNames[7];

// Raw <-> list index (unequal buckets; identity when n = 128).
constexpr int listIndex(int raw, int n) { return ((2 * raw + 1) * n) >> 8; }
constexpr int listRawMid(int idx, int n) { return (idx * 256 + 128) / (2 * n); }

inline const Bitmap* icon(Icons f, int i)
{
    switch (f) {
    case Icons::Toggle: return i >= 0 && i < 2 ? kIconToggle[i] : nullptr;
    case Icons::FmRatio: return i >= 0 && i < 24 ? kIconFmRatio[i] : nullptr;
    case Icons::EnsPitch: return i >= 0 && i < 33 ? kIconEnsPitch[i] : nullptr;
    case Icons::FmDynFrq: return i >= 0 && i < 128 ? kIconFmDynFrq[i] : nullptr;
    case Icons::SidWave: return i >= 0 && i < 5 ? kIconSidWave[i] : nullptr;
    case Icons::DproSync: return i >= 0 && i < 3 ? kIconDproSync[i] : nullptr;
    case Icons::DproWave: return i >= 0 && i < 32 ? kIconDproWave[i] : nullptr;
    case Icons::VoCons: return i >= 0 && i < 21 ? kIconVoCons[i] : nullptr;
    case Icons::DdrwWave: return i >= 0 && i < 64 ? kIconDdrwWave[i] : nullptr;
    case Icons::LfoPage: return i >= 0 && i < 9 ? kIconLfoPage[i] : nullptr;
    case Icons::LfoWave: return i >= 0 && i < 11 ? kIconLfoWave[i] : nullptr;
    case Icons::LfoDest: return i >= 0 && i < 8 ? kIconLfoDest[i] : nullptr;
    default: return nullptr;
    }
}

// Name the panel prints for a List/Readout parameter at this raw value; null for other types.
inline const char* valueName(const Param& p, int raw)
{
    if (p.display == Display::List) return p.values[listIndex(raw, p.valueCount)];
    if (p.display == Display::Readout) return p.values[raw < 0 ? 0 : raw >= p.valueCount ? p.valueCount - 1 : raw];
    return nullptr;
}

inline const Machine* machineByIndex(int index)
{
    for (const auto& m : kMachines) if (m.index == index) return &m;
    return nullptr;
}

} // namespace mnm::uispec
