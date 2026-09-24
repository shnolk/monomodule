// Skins: the two colours the whole UI is drawn in (the LCD's ink and paper), chosen by the user and shared by
// every plugin and the Library app through the shared settings file, like the OS file selection.
//   DEFAULT       black on white (the hardware's LCD, as the UI was designed)
//   INVERTED      white on black
//   LOW CONTRAST  dark grey on light grey
//   CUSTOM        any two colours, entered as hex
// Every drawing routine reads lcd::ink / lcd::paper (Lcd.h) at paint time, so applying a skin is a matter of
// setting the two colours and repainting; derived shades (dimmed text, hover hatching) are alphas of the two.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "SharedSettings.h"

namespace mnm::plugin::skin {

enum class Preset : int { Default = 0, Inverted, LowContrast, Custom };
constexpr int kNumPresets = 4;
constexpr const char* kPresetNames[kNumPresets] = {"DEFAULT", "INVERTED", "LOW CONTRAST", "CUSTOM"};

struct Skin {
    Preset preset = Preset::Default;
    juce::Colour ink{0xff000000}, paper{0xffffffff};   // the colours in use (for a preset: its own)
    bool operator==(const Skin& o) const { return preset == o.preset && ink == o.ink && paper == o.paper; }
    bool operator!=(const Skin& o) const { return !(*this == o); }
};

inline Skin presetSkin(Preset p, juce::Colour customInk = juce::Colour(0xff000000), juce::Colour customPaper = juce::Colour(0xffffffff))
{
    switch (p) {
    case Preset::Inverted:    return {p, juce::Colour(0xffffffff), juce::Colour(0xff000000)};
    case Preset::LowContrast: return {p, juce::Colour(0xff5c5c5c), juce::Colour(0xffc4c4c4)};
    case Preset::Custom:      return {p, customInk.withAlpha(1.0f), customPaper.withAlpha(1.0f)};
    default:                  return {Preset::Default, juce::Colour(0xff000000), juce::Colour(0xffffffff)};
    }
}

// The colours in use. Lcd.h's lcd::ink / lcd::paper are references to these.
inline juce::Colour& inkColour() { static juce::Colour c{0xff000000}; return c; }
inline juce::Colour& paperColour() { static juce::Colour c{0xffffffff}; return c; }
inline Skin& current() { static Skin s; return s; }

// "RRGGBB" or "#RRGGBB" (also "RGB"); false when the text is not a colour.
inline bool parseHex(juce::String text, juce::Colour& out)
{
    text = text.trim().trimCharactersAtStart("#").toUpperCase();
    if (text.length() == 3) text = juce::String::charToString(text[0]) + text[0] + text[1] + text[1] + text[2] + text[2];
    if (text.length() != 6 || !text.containsOnly("0123456789ABCDEF")) return false;
    out = juce::Colour(0xff000000u | uint32_t(text.getHexValue32()));
    return true;
}
inline juce::String hexOf(juce::Colour c) { return c.toDisplayString(false).toUpperCase(); }

// Makes `s` the skin in use (message thread; the caller repaints).
inline void apply(const Skin& s) { current() = s; inkColour() = s.ink; paperColour() = s.paper; }

// The skin saved in the shared settings ("skin" = preset index, "skinInk"/"skinPaper" = the custom colours).
inline Skin load()
{
    const int p = juce::jlimit(0, kNumPresets - 1, loadSharedSetting("skin", "0").getIntValue());
    juce::Colour ink(0xff000000), paper(0xffffffff);
    parseHex(loadSharedSetting("skinInk"), ink);
    parseHex(loadSharedSetting("skinPaper"), paper);
    return presetSkin(Preset(p), ink, paper);
}
inline void save(const Skin& s)
{
    saveSharedSetting("skin", juce::String(int(s.preset)));
    if (s.preset == Preset::Custom) { saveSharedSetting("skinInk", hexOf(s.ink)); saveSharedSetting("skinPaper", hexOf(s.paper)); }
}

// The custom colours to start a CUSTOM edit from: the saved ones, else the colours in use.
inline juce::Colour savedCustomInk() { juce::Colour c = inkColour(); parseHex(loadSharedSetting("skinInk"), c); return c; }
inline juce::Colour savedCustomPaper() { juce::Colour c = paperColour(); parseHex(loadSharedSetting("skinPaper"), c); return c; }

} // namespace mnm::plugin::skin
