// The LCD artwork of the UI: six bitmap fonts, the dial, the list-icon families and the machine-group logos. None of it is part of
// this source tree or of the binaries. It is read out of the user's own Monomachine OS file when that file
// is selected (installRomArt / ensureRomArt); only the places where OS 1.32B keeps it are listed in
// RomArt.cpp. Until an OS file is present, and for an image that does not validate (another OS version),
// the UI draws with a plain built-in stand-in face (RomArt.cpp, fallback*) and generated dials; list
// parameters then show the drawn rotary switch in place of an icon, and machine groups their name in place of a logo.
//
// Threading: the artwork is read while painting and written by an install, both without locks. Install only
// from the thread that paints (the message thread; the main thread of the console tools). Storage of an
// earlier install is never freed, so a Bitmap pointer taken before an install stays valid.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mnm::uispec {

// 1-bit image; row r, pixel x is lit when bit (63 - x) of rows[r] is set (row 0 = top).
struct Bitmap {
    uint8_t w = 0, h = 0;
    const uint64_t* rows = nullptr;
    bool lit(int x, int y) const { return (rows[y] >> (63 - x)) & 1u; }
};

// Proportional bitmap font: glyph(c) is null for a code without a glyph (advance by adv then).
struct Font {
    uint8_t h = 0, adv = 0;
    const Bitmap* bitmaps = nullptr;
    const int16_t* index = nullptr;   // 128 entries, -1 = no glyph
    const Bitmap* glyph(int c) const { return c >= 0 && c < 128 && index[c] >= 0 ? &bitmaps[index[c]] : nullptr; }
};

struct Art {
    Font bold8, small4x5, tiny3x5, square5x5, digitsTop, digitsBottom;
    Bitmap dialRing;                  // 11x13
    Bitmap groupTie;                  // 7x3
    Bitmap ringPlain;                 // 11x11 plain ring (base of the drawn rotary switch)
    const Bitmap* dialDot[128] = {};  // 7x7 pointer frame per raw value, composite at (2, 4); never null
    // list-icon families; every entry is null while the stand-in is active
    const Bitmap* iconLfoDest[8] = {};     // LFO DEST: target knob A-H on a page pictogram
    const Bitmap* iconToggle[2] = {};
    const Bitmap* iconFmRatio[24] = {};
    const Bitmap* iconEnsPitch[33] = {};
    const Bitmap* iconFmDynFrq[128] = {};
    const Bitmap* iconSidWave[5] = {};
    const Bitmap* iconDproSync[3] = {};
    const Bitmap* iconDproWave[32] = {};
    const Bitmap* iconVoCons[21] = {};
    const Bitmap* iconDdrwWave[64] = {};
    const Bitmap* iconLfoPage[9] = {};
    const Bitmap* iconLfoWave[11] = {};    // menu (WAVE index) order
    // The machine-group logos of the boot splash, in the image's order: SWAVE SID DPRO FM+ VO (GND and FX have
    // none). Null without an OS file; the UI prints the group's name then.
    const Bitmap* groupLogo[5] = {};
    // SWAVE holds only the wave emblem of its splash logo: the lettering beside it is illegible at that size, so
    // the UI sets SUPER / WAVE in the small-4x5 face instead (groupLogoWords).
    bool fromRom = false;
};

// The artwork in use: the stand-in until an install succeeds. The object itself never moves.
Art& art();

// Reads the artwork out of a decompressed main OS image (section 0 of the OS file, load base 0x200000) and
// makes it the artwork in use. Every descriptor is bounds- and shape-checked first; on failure nothing
// changes and `error` says what did not fit.
bool installRomArt(const std::vector<uint8_t>& mainOs, std::string* error = nullptr);

// installRomArt for an OS file on disk. Does nothing (and returns true) when the artwork in use already
// came from this file (same path, size and modification time), so it is cheap to call on every editor open.
bool ensureRomArt(const std::filesystem::path& osFile, std::string* error = nullptr);

// The logo of a machine group (uispec::Machine::group), or null: GND/FX, or no OS file in use.
const Bitmap* groupLogo(const char* group);
// Two words the UI sets left of that logo in the small-4x5 face, or null: only SWAVE ("SUPER", "WAVE").
const char* const* groupLogoWords(const char* group);

// The rectangle of a bitmap that holds its lit pixels (the logos carry blank rows under the artwork).
struct Bounds { int x = 0, y = 0, w = 0, h = 0; };
Bounds litBounds(const Bitmap& b);
inline const Font& kFontBold8 = art().bold8;
inline const Font& kFontSmall4x5 = art().small4x5;
inline const Font& kFontTiny3x5 = art().tiny3x5;
inline const Font& kFontSquare5x5 = art().square5x5;
inline const Font& kFontDigitsTop = art().digitsTop;
inline const Font& kFontDigitsBottom = art().digitsBottom;
inline const Bitmap& kDialRing = art().dialRing;
inline const Bitmap& kGroupTie = art().groupTie;
inline const Bitmap& kRingPlain = art().ringPlain;
inline const auto& kDialDot = art().dialDot;
inline const auto& kIconLfoDest = art().iconLfoDest;
inline const auto& kIconToggle = art().iconToggle;
inline const auto& kIconFmRatio = art().iconFmRatio;
inline const auto& kIconEnsPitch = art().iconEnsPitch;
inline const auto& kIconFmDynFrq = art().iconFmDynFrq;
inline const auto& kIconSidWave = art().iconSidWave;
inline const auto& kIconDproSync = art().iconDproSync;
inline const auto& kIconDproWave = art().iconDproWave;
inline const auto& kIconVoCons = art().iconVoCons;
inline const auto& kIconDdrwWave = art().iconDdrwWave;
inline const auto& kIconLfoPage = art().iconLfoPage;
inline const auto& kIconLfoWave = art().iconLfoWave;

} // namespace mnm::uispec
