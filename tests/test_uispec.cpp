// The generated UI-spec tables (src/plugin/one/SpecData.*) must agree with the host-side machine
// table on everything both describe: labels, defaults, blank slots, the FM ratio list, the
// shared pages, and the raw -> list index mapping.
#include "mini_test.h"
#include "SpecData.h"
#include "host/Machines.h"
#include "MachineText.h"
#include <cstring>
#include <filesystem>
#include <string>

using namespace mnm;
namespace ls = mnm::uispec;

// The LCD artwork comes from the OS file at run time (RomArt.h). Tests that look at the hardware's pixels
// need that file and skip those checks without it; everything else runs on the stand-in face too.
static bool romArt()
{
    static const bool ok = !mt::osFile().empty() && ls::ensureRomArt(mt::osFile());
    return ok;
}

// Without an OS file the UI must still draw: a complete caps face in every size, generated dials, no icons.
// Registered before the first romArt() call, so it sees the stand-in.
TEST_CASE(uispec_standin_art)
{
    const ls::Art& a = ls::art();
    CHECK(!a.fromRom);   // this is the first test of the binary that touches the artwork
    CHECK_EQ(int(a.bold8.h), 8); CHECK_EQ(int(a.tiny3x5.h), 5); CHECK_EQ(int(a.small4x5.h), 5); CHECK_EQ(int(a.square5x5.h), 5);
    CHECK_EQ(int(a.digitsTop.h) + int(a.digitsBottom.h), 10);
    for (const ls::Font* f : {&a.bold8, &a.tiny3x5, &a.small4x5, &a.square5x5})
        for (char c : std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-/.:()%#")) CHECK_MSG(f->glyph(c) != nullptr, c);
    for (char c : std::string("0123456789%./")) CHECK(a.digitsTop.glyph(c) && a.digitsBottom.glyph(c));
    CHECK(a.bold8.glyph('z') == nullptr);
    CHECK_EQ(int(a.dialRing.w), 11); CHECK_EQ(int(a.dialRing.h), 13); CHECK(a.dialRing.lit(5, 0));
    CHECK_EQ(int(a.ringPlain.w), 11); CHECK_EQ(int(a.ringPlain.h), 11);
    CHECK_EQ(int(a.groupTie.w), 7); CHECK_EQ(int(a.groupTie.h), 3);
    for (int v = 0; v < 128; ++v) CHECK(a.dialDot[v] && a.dialDot[v]->w == 7 && a.dialDot[v]->h == 7);
    CHECK(a.dialDot[64]->lit(3, 0));   // 64 = 12 o'clock
    CHECK(ls::icon(ls::Icons::Toggle, 0) == nullptr && ls::icon(ls::Icons::LfoDest, 0) == nullptr && ls::groupLogo("SID") == nullptr);
    // an image that is not the MAIN OS is refused and changes nothing
    std::string err;
    CHECK(!ls::installRomArt(std::vector<uint8_t>(0x80000, 0), &err) && !err.empty() && !ls::art().fromRom);
}

TEST_CASE(uispec_matches_host_machine_table)
{
    const bool art = romArt();
    CHECK_EQ(ls::kNumMachines, 22);   // every machine of OS 1.32B, MKII DDRW/DENS included
    int supported = 0;
    for (const auto& m : ls::kMachines) {
        const auto* def = host::machineDef(host::Machine(m.index));
        CHECK_EQ(def != nullptr, m.supported);   // supported <=> the engine path has a machine table entry
        for (int k = 0; k < 8; ++k) {
            const auto& p = m.params[k];
            if (p.display == ls::Display::List || p.display == ls::Display::Readout) {
                CHECK(p.values != nullptr && p.icons != ls::Icons::None);
                for (int i = 0; i < p.valueCount; ++i) CHECK(p.values[i] != nullptr && (!art || ls::icon(p.icons, i) != nullptr));   // an icon per entry, given the OS file
            }
        }
        if (!def) continue;
        ++supported;
        for (int k = 0; k < 8; ++k) {
            const auto& p = m.params[k];
            const bool blank = p.display == ls::Display::Blank;
            const std::string specLabel = blank ? "-" : p.label;
            CHECK_EQ(specLabel, std::string(def->labels[k]));
            CHECK_EQ(int(p.defaultRaw), int(def->defaults[k]));
            if (p.display == ls::Display::List || p.display == ls::Display::Readout) {
                CHECK(p.values != nullptr);
                for (int i = 0; i < p.valueCount; ++i) CHECK(p.values[i] != nullptr && p.values[i][0] != '\0');
            }
        }
        CHECK_EQ(m.isFx, host::isFxMachine(host::Machine(m.index)));
    }
    CHECK_EQ(supported, host::kNumMachineDefs);
}

TEST_CASE(uispec_preview_machines)
{
    // every machine has an engine path since v0.7.6 (SID, VO-6, DPRO WAVE/BBOX/DDRW/DENS); the spec's
    // headline facts about the formerly previewed pages still hold
    for (int idx : {3, 6, 7, 11, 32, 33}) { const auto* m = ls::machineByIndex(idx); CHECK(m != nullptr && m->supported); }
    const auto* sid = ls::machineByIndex(3);
    CHECK(sid->params[3].icons == ls::Icons::SidWave && sid->params[4].icons == ls::Icons::SidWave);   // MOD reuses WAVE's icons
    CHECK_EQ(std::string(ls::valueName(sid->params[2], 96)), std::string("ON"));                         // PWRS defaults ON
    CHECK_EQ(std::string(ls::valueName(sid->params[5], 0)), std::string("MFRQ"));
    const auto* wave = ls::machineByIndex(6);
    CHECK_EQ(int(wave->params[0].valueCount), 32); CHECK_EQ(std::string(wave->params[0].values[31]), std::string("S32"));
    CHECK(wave->params[4].icons == ls::Icons::DproSync && wave->params[6].display == ls::Display::Blank);
    const auto* bbox = ls::machineByIndex(7);
    CHECK(bbox->params[7].display == ls::Display::Blank);                                               // BBOX has no TUNE
    CHECK_EQ(std::string(bbox->params[4].label), std::string("RTRG"));                                  // knobs E/F, not C/D
    const auto* vo = ls::machineByIndex(11);
    CHECK_EQ(int(vo->params[4].valueCount), 21); CHECK_EQ(std::string(vo->params[4].values[17]), std::string("TH"));
    const auto* ddrw = ls::machineByIndex(32);
    CHECK(ddrw->params[0].icons == ls::Icons::DdrwWave); CHECK_EQ(int(ddrw->params[0].valueCount), 64);
    const auto* dens = ls::machineByIndex(33);
    CHECK(dens->params[0].icons == ls::Icons::EnsPitch);
}

TEST_CASE(uispec_list_index)
{
    for (int v = 0; v < 128; ++v) CHECK_EQ(ls::listIndex(v, 128), v);
    CHECK_EQ(ls::listIndex(60, 24), 11); CHECK_EQ(ls::listIndex(80, 24), 15); CHECK_EQ(ls::listIndex(102, 24), 19);   // FM+ defaults
    CHECK_EQ(ls::listIndex(63, 33), 16);                                                                            // ENS PCH centre = OFF
    // the FM ratio knobs are 24-step lists on both FM+ machines that have them
    for (int idx : {8, 9}) for (int k : {0, 2, 4}) if (idx == 8 && k == 2) continue; else {
        const auto& p = ls::machineByIndex(idx)->params[k];
        CHECK(p.display == ls::Display::List && p.valueCount == 24 && p.icons == ls::Icons::FmRatio);
    }
    for (int i = 0; i < 24; ++i) CHECK_EQ(ls::listIndex(ls::listRawMid(i, 24), 24), i);
    for (int i = 0; i < 33; ++i) CHECK_EQ(ls::listIndex(ls::listRawMid(i, 33), 33), i);
}

TEST_CASE(uispec_shared_pages_match_host)
{
    for (int k = 0; k < 8; ++k) {
        CHECK_EQ(int(ls::kSharedPages[0].defaults[k]), int(host::kDefaultAmp[k]));
        CHECK_EQ(int(ls::kSharedPages[1].defaults[k]), int(host::kDefaultFilt[k]));
        CHECK_EQ(int(ls::kSharedPages[2].defaults[k]), int(host::kDefaultEfx[k]));
        CHECK_EQ(int(ls::kAmpDefaultsFx[k]), int(host::kDefaultAmpFx[k]));
        CHECK_EQ(std::string(ls::kSharedPages[0].labels[k]), std::string(host::kAmpLabels[k]));
        CHECK_EQ(std::string(ls::kSharedPages[1].labels[k]), std::string(host::kFiltLabels[k]));
        CHECK_EQ(std::string(ls::kSharedPages[2].labels[k]), std::string(host::kEfxLabels[k]));
    }
    CHECK_EQ(int(ls::kSharedPages[0].bipolarMask), int(host::kAmpBipolarMask));
    CHECK_EQ(int(ls::kSharedPages[1].bipolarMask), int(host::kFiltBipolarMask));
    CHECK_EQ(int(ls::kSharedPages[2].bipolarMask), int(host::kEfxBipolarMask));
}

TEST_CASE(uispec_glyphs_and_fonts)
{
    CHECK_EQ(ls::listIndex(37, 5), 1); CHECK_EQ(ls::listIndex(88, 5), 3);   // TRIG kit bytes seen in real dumps
    if (!romArt()) return;
    CHECK(ls::art().fromRom);
    CHECK(ls::kFontBold8.glyph(93) != nullptr && ls::kFontBold8.glyph(93)->w == 5);      // the national letters at [ \ ]
    CHECK(ls::kFontBold8.glyph(97) != nullptr && ls::kFontBold8.glyph(97)->w == 10);     // symbol slots start at 0x61
    CHECK(ls::kFontBold8.glyph(94) == nullptr && ls::kFontBold8.glyph(106) == nullptr);
    CHECK_EQ(int(ls::kDialRing.w), 11); CHECK_EQ(int(ls::kDialRing.h), 13);
    CHECK_EQ(int(ls::kGroupTie.w), 7);  CHECK_EQ(int(ls::kGroupTie.h), 3);
    CHECK(ls::kDialRing.lit(5, 0));                 // top tick
    for (int v = 0; v < 128; ++v) { CHECK_EQ(int(ls::kDialDot[v]->w), 7); CHECK_EQ(int(ls::kDialDot[v]->h), 7); }
    CHECK(ls::kDialDot[64]->lit(3, 0));             // 64 = 12 o'clock
    CHECK(ls::kDialDot[0]->lit(1, 5));              // 0 = lower-left
    CHECK(ls::kDialDot[127]->lit(5, 5));            // 127 = lower-right
    CHECK_EQ(int(ls::kFontBold8.h), 8);
    for (char c : std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-/.")) CHECK(ls::kFontBold8.glyph(c) != nullptr);
    CHECK(ls::kFontBold8.glyph('z') == nullptr);    // no lowercase in the LCD fonts
    CHECK_EQ(int(ls::kFontDigitsTop.h) + int(ls::kFontDigitsBottom.h), 10);
    CHECK(ls::icon(ls::Icons::Toggle, 1) != nullptr && ls::icon(ls::Icons::Toggle, 2) == nullptr);
    CHECK(ls::icon(ls::Icons::EnsPitch, 32) != nullptr);
    CHECK(ls::icon(ls::Icons::FmDynFrq, 127) != nullptr);
}

TEST_CASE(uispec_display_types_and_quirks)
{
    // spec quirks that must be reproduced (docs/06-quirks.md)
    const auto* fmpar = ls::machineByIndex(9);
    CHECK(fmpar != nullptr);
    CHECK_EQ(std::string(fmpar->params[0].values[9]), std::string("5/32"));   // mislabelled ladder index 9
    CHECK_EQ(std::string(ls::valueName(fmpar->params[0], 60)), std::string("1/2"));
    const auto* ens = ls::machineByIndex(14);
    CHECK_EQ(std::string(ls::valueName(ens->params[0], 63)), std::string("OFF"));
    CHECK_EQ(std::string(ens->params[0].values[27]), std::string("3/2"));
    CHECK(ens->params[5].tieRight);                                          // CHRL ties to CHRW
    const auto* saw = ls::machineByIndex(4);
    CHECK(saw->params[0].tieRight && saw->params[1].tieRight && !saw->params[2].tieRight);
    const auto* phaser = ls::machineByIndex(18);
    CHECK(phaser->params[6].display == ls::Display::Blank);
    CHECK_EQ(int(phaser->params[6].defaultRaw), 127);                        // blank yet non-zero default
    CHECK(phaser->params[7].display == ls::Display::Bipolar);                // INP bipolar on PHASER...
    CHECK(ls::machineByIndex(13)->params[7].display == ls::Display::Numeric); // ...plain on REVERB
    const auto* dyn = ls::machineByIndex(10);
    CHECK(dyn->params[0].display == ls::Display::Readout);
    CHECK_EQ(std::string(ls::valueName(dyn->params[0], 64)), std::string("1.0"));
    CHECK_EQ(std::string(ls::valueName(dyn->params[4], 74)), std::string("1.33"));
    const auto* puls = ls::machineByIndex(5);
    CHECK(puls->params[6].display == ls::Display::List && puls->params[6].icons == ls::Icons::Toggle);
    CHECK_EQ(std::string(ls::valueName(puls->params[6], 0)), std::string("OFF"));
    CHECK_EQ(std::string(ls::valueName(puls->params[6], 96)), std::string("ON"));
}

TEST_CASE(uispec_lfo_page)
{
    static const char* labels[8] = {"PAGE", "DEST", "TRIG", "WAVE", "MULT", "SPD", "INTL", "DPTH"};
    static const int defaults[8] = {0, 64, 0, 0, 1, 64, 0, 0};
    for (int k = 0; k < 8; ++k) {
        CHECK_EQ(std::string(ls::kLfoParams[k].label), std::string(labels[k]));
        CHECK_EQ(int(ls::kLfoParams[k].defaultRaw), defaults[k]);
    }
    CHECK_EQ(int(ls::kLfoParams[0].valueCount), 9);  CHECK_EQ(std::string(ls::valueName(ls::kLfoParams[0], 0)), std::string("PTCH"));
    CHECK_EQ(std::string(ls::valueName(ls::kLfoParams[0], 120)), std::string("MIDI"));
    CHECK_EQ(int(ls::kLfoParams[1].valueCount), 8);  CHECK_EQ(std::string(ls::valueName(ls::kLfoParams[1], 64)), std::string("2OCT"));
    CHECK_EQ(int(ls::kLfoParams[2].maxRaw), 127);     CHECK_EQ(std::string(ls::valueName(ls::kLfoParams[2], 114)), std::string("HALF"));
    CHECK_EQ(int(ls::kLfoParams[2].valueCount), 5);   CHECK_EQ(std::string(ls::valueName(ls::kLfoParams[2], 63)), std::string("HOLD"));
    CHECK(ls::kLfoParams[2].display == ls::Display::List);
    CHECK_EQ(int(ls::kLfoParams[3].valueCount), 11); CHECK_EQ(std::string(ls::valueName(ls::kLfoParams[3], 121)), std::string("RND"));
    CHECK_EQ(int(ls::kLfoParams[4].valueCount), 7);  CHECK_EQ(std::string(ls::valueName(ls::kLfoParams[4], 1)), std::string("1X"));
    CHECK_EQ(std::string(ls::valueName(ls::kLfoParams[4], 118)), std::string("64X"));
    CHECK_EQ(std::string(ls::kLfoDestNames[1][0]), std::string("PAR1"));
    CHECK_EQ(std::string(ls::kLfoDestNames[3][0]), std::string("BASE"));
    CHECK_EQ(std::string(ls::kLfoDestNames[8][7]), std::string("CC 4"));
    CHECK(ls::kLfoParams[2].icons == ls::Icons::Switch && ls::kLfoParams[4].icons == ls::Icons::Switch);
    // MCL agrees on the raw positions of the list entries (MNMParams.h)
    CHECK_EQ(ls::listIndex(78, 9), 5); CHECK_EQ(ls::listIndex(28, 11), 2); CHECK_EQ(ls::listIndex(45, 7), 2);
    if (!romArt()) return;   // the rest looks at the hardware's icons
    for (int i = 0; i < 9; ++i) CHECK(ls::icon(ls::Icons::LfoPage, i) != nullptr);
    for (int i = 0; i < 8; ++i) { const auto* ic = ls::icon(ls::Icons::LfoDest, i); CHECK(ic && ic->w == 17 && ic->h == 11); }
    CHECK(ls::kIconLfoDest[4]->lit(5, 10) && !ls::kIconLfoDest[0]->lit(5, 10));   // E-H variants carry the bottom bar
    CHECK_EQ(int(ls::kRingPlain.w), 11); CHECK_EQ(int(ls::kRingPlain.h), 11);
    for (int i = 0; i < 11; ++i) CHECK(ls::icon(ls::Icons::LfoWave, i) != nullptr);
    // WAVE icons in menu order (the OS file keeps them alphabetically): TRI rises from the middle, ITRI dips first,
    // SAW rises left-to-right, SQR starts high, EXP starts at the top-left, RND is the blocky one
    auto lit = [](int i, int x, int y) { return ls::icon(ls::Icons::LfoWave, i)->lit(x, y); };
    CHECK(lit(0, 4, 0) && !lit(0, 12, 0));      // TRI: peak on the left
    CHECK(lit(1, 12, 0) && !lit(1, 4, 0));      // ITRI: peak on the right
    CHECK(lit(2, 12, 0) && lit(2, 4, 8));       // SAW: rising
    CHECK(lit(3, 4, 0) && lit(3, 12, 8));       // ISAW: falling
    CHECK(lit(4, 0, 0) && lit(4, 16, 8));       // SQR: high first
    CHECK(lit(5, 16, 0) && lit(5, 0, 8));       // ISQR: low first
    CHECK(lit(6, 2, 0) && lit(6, 16, 8));       // EXP: decays from the top
    CHECK(lit(7, 2, 8) && lit(7, 16, 0));       // IEXP: mirrored
    CHECK(lit(8, 8, 0) && lit(8, 16, 8));       // RMP: rises to the middle, then flat low
    CHECK(lit(9, 0, 0) && lit(9, 16, 0));       // IRMP: falls to the middle, then flat high
    CHECK(lit(10, 7, 1) && lit(10, 12, 8));     // RND: blocks
}

// The machine picker's text (MachineText.h) covers the spec's machines and groups; the group logos come from the OS file.
TEST_CASE(uispec_picker_text_and_logos)
{
    auto ascii = [](const char* s) { for (; *s; ++s) if (static_cast<unsigned char>(*s) < 32 || static_cast<unsigned char>(*s) > 126) return false; return true; };
    for (const auto& m : ls::kMachines) {
        const char* b = mnm::onetext::machineBlurb(m.index);
        CHECK_MSG(b != nullptr && b[0] != '\0' && ascii(b), m.displayName);
        const auto* g = mnm::onetext::groupText(m.group);
        CHECK_MSG(g != nullptr && g->blurb[0] != '\0' && ascii(g->blurb) && ascii(g->title), m.group);
    }
    // a machine description is one line of small-4x5 in a picker column: 73 LCD px at the editor's width
    // ((1185 - 6 * 6) / 7 columns - 2 * 3 border - 2 * 6 padding = 146 screen px at 2x)
    auto width = [](const ls::Font& f, const char* s) { int w = 0; for (; *s; ++s) { const auto* g = f.glyph(static_cast<unsigned char>(*s)); w += (g ? g->w : f.adv) + 1; } return w - 1; };
    for (const auto& m : ls::kMachines) CHECK_MSG(width(ls::kFontSmall4x5, mnm::onetext::machineBlurb(m.index)) <= 73, m.displayName);
    // the splash has a logo for every group but GND and FX; the picker prints those names (and every name without an OS file)
    const bool art = romArt();
    for (const auto& m : ls::kMachines) {
        const bool plain = std::strcmp(m.group, "GND") == 0 || std::strcmp(m.group, "FX") == 0;
        const auto* logo = ls::groupLogo(m.group);
        CHECK_MSG((logo == nullptr) == (plain || !art), m.group);
        if (logo) { const auto lb = ls::litBounds(*logo); CHECK_MSG(lb.w >= 9 && lb.w <= 31 && lb.h >= 8 && lb.h <= 11, m.group); }
    }
    if (art) {   // table order SWAVE SID DPRO FM+ VO: DigiPRO is the widest, FM+ has the plus sign's bar on its 5th row
        CHECK_EQ(int(ls::groupLogo("DPRO")->w), 31); CHECK_EQ(int(ls::groupLogo("VO")->w), 18);
        // SWAVE keeps only the 9x8 wave emblem of its splash logo; the UI sets SUPER / WAVE beside it in type
        CHECK_EQ(int(ls::groupLogo("SWAVE")->w), 9); CHECK_EQ(int(ls::groupLogo("SWAVE")->h), 8);
        CHECK(ls::groupLogoWords("SWAVE") != nullptr && std::string(ls::groupLogoWords("SWAVE")[0]) == "SUPER" && ls::groupLogoWords("SID") == nullptr);
        for (const char* g : {"SID", "FM+", "VO"}) CHECK_MSG(ls::litBounds(*ls::groupLogo(g)).h == 9, g);   // the 9-row body the drawing is sized by
        CHECK_EQ(ls::litBounds(*ls::groupLogo("DPRO")).h, 11);                                               // plus the g's descender
        const auto* fm = ls::groupLogo("FM+");
        CHECK(fm->w == 24 && fm->lit(23, 4) && !fm->lit(23, 3));
    }
}
