// Monomodule One: the text of the machine picker (group blurbs and one-line machine descriptions).
// ASCII caps only: it is drawn in the LCD's small-4x5
// face, which has no lowercase, and juce::String(const char*) decodes literals as Latin-1, so a UTF-8
// dash or ellipsis would render as mojibake. No JUCE dependency so the tests can check every spec
// machine has an entry.
#pragma once
#include <cstdint>

namespace mnm::onetext {

struct GroupText {
    const char* group;   // uispec::Machine::group
    const char* title;   // header text when the group has no logo artwork
    const char* blurb;
};

struct MachineText {
    uint8_t index;       // uispec::Machine::index
    const char* blurb;
};

inline constexpr GroupText kGroups[] = {
    {"GND",   "GND",       "BASE-LEVEL SOUNDS"},
    {"SID",   "SID",       "COMMODORE 64 SOUND EMULATION. CRISP / GRITTY SOUND."},
    {"SWAVE", "SUPERWAVE", "STACKED ANALOG OSCILLATORS. THICK, WARM SOUND."},
    {"DPRO",  "DIGIPRO",   "RAW DIGITAL WAVEFORMS W/ SAMPLER. HARSH AND SHARP SOUND."},
    {"FM+",   "FM+",       "COMPLEX FREQUENCY MODULATION SYNTHESIS MADE SIMPLE."},
    {"VO",    "VO",        "FORMANT VOICE SYNTHESIS: VOWELS, CONSONANTS, WHISPER."},
    {"FX",    "FX",        "AUDIO EFFECTS, REQUIRES AN AUDIO INPUT."},
};

// One line of small-4x5 at the picker's column width (73 LCD px, about 14 characters; tests/test_uispec.cpp checks).
inline constexpr MachineText kMachines[] = {
    {0,  "EMPTY CHANNEL"},
    {1,  "SINE WAVE"},
    {2,  "WHITE NOISE"},
    {3,  "C64 SOUND CHIP"},
    {4,  "UNISON SAWTOOTH"},
    {5,  "UNISON PULSE"},
    {14, "STRING ENSEMBLE"},
    {6,  "32 WAVEFORMS"},
    {7,  "DRUM SAMPLES"},
    {32, "USER WAVES"},
    {33, "WAVE ENSEMBLE"},
    {8,  "STATIC RATIOS"},
    {9,  "PARALLEL MODS"},
    {10, "DYNAMIC FM"},
    {11, "FORMANT VOICE"},
    {12, "PASS-THROUGH"},
    {13, "GATED REVERB"},
    {15, "STEREO CHORUS"},
    {16, "COMPRESSOR"},
    {17, "RING MODULATOR"},
    {18, "PHASER"},
    {19, "FLANGER"},
};

inline constexpr bool sameText(const char* a, const char* b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == 0 && *b == 0;
}

inline constexpr const GroupText* groupText(const char* group)
{
    for (const auto& g : kGroups) if (sameText(g.group, group)) return &g;
    return nullptr;
}

inline constexpr const char* machineBlurb(int index)
{
    for (const auto& m : kMachines) if (int(m.index) == index) return m.blurb;
    return nullptr;
}

} // namespace mnm::onetext
