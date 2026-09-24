// Machine indices (= the model numbers in kit sysex = the DSP's machine slots) and each machine's default
// parameter values.
#pragma once
#include <array>
#include <cstdint>

namespace mnm::host {

enum class Machine : uint8_t {
    GND = 0, SIN = 1, NOIS = 2, SID = 3, SAW = 4, PULS = 5, WAVE = 6, BBOX = 7,
    FM_STAT = 8, FM_PAR = 9, FM_DYN = 10, VO6 = 11, THRU = 12, REVERB = 13, ENS = 14, CHORUS = 15,
    DYNAMIX = 16, RINGMOD = 17, PHASER = 18, FLANGER = 19, DDRW = 32, DENS = 33
};

enum class Page : uint8_t { SYN = 0, AMP = 1, FILT = 2, EFX = 3 };

constexpr std::array<uint8_t, 8> kDefaultAmp  = {0, 0, 64, 64, 64, 64, 64, 0};     // ATK HOLD DEC REL DIST VOL PAN PORT
constexpr std::array<uint8_t, 8> kDefaultFilt = {0, 127, 0, 0, 0, 32, 64, 64};     // BASE WDTH HPQ LPQ ATK DEC BOFS WOFS
constexpr std::array<uint8_t, 8> kDefaultEfx  = {64, 64, 0, 64, 64, 28, 0, 127};   // EQF EQG SRR DTIM DSND DFB DBAS DWID

// Per-machine SYN page description: labels, defaults and display
struct MachineDef {
    Machine machine;
    const char* name;
    const char* labels[8];
    std::array<uint8_t, 8> defaults;
    uint8_t bipolarMask; // bit k set: hardware displays this parameter as value-64 (-64..+63)
};
constexpr MachineDef kFmStatDef = {Machine::FM_STAT, "FM+ STAT", {"1FRQ", "1FIN", "1ENV", "1FB", "2FRQ", "2VOL", "TONE", "TUNE"}, {60, 64, 80, 30, 80, 64, 98, 64}, 0x82};
constexpr MachineDef kFmParDef  = {Machine::FM_PAR,  "FM+ PAR",  {"1FRQ", "1ENV", "2FRQ", "2ENV", "3FRQ", "3ENV", "TONE", "TUNE"}, {60, 64, 80, 64, 102, 80, 98, 64}, 0x80};
constexpr MachineDef kFmDynDef  = {Machine::FM_DYN,  "FM+ DYN",  {"1FRQ", "1FEN", "1VOL", "1VEN", "2FRQ", "2ENV", "2FB", "TUNE"}, {64, 64, 64, 64, 74, 80, 30, 64}, 0x80};
constexpr MachineDef kSidDef  = {Machine::SID,  "SID 6581", {"PW", "PWAD", "PWRS", "WAVE", "MOD", "MSRC", "MFRQ", "TUNE"}, {0, 0, 96, 0, 0, 0, 64, 64}, 0xC0};
constexpr MachineDef kVo6Def  = {Machine::VO6,  "VO-6",     {"VOC1", "VOC2", "V-SW", "VOIC", "CONS", "CLEN", "CVOL", "TUNE"}, {64, 64, 96, 0, 0, 64, 64, 64}, 0x80};
constexpr MachineDef kWaveDef = {Machine::WAVE, "DPRO WAVE", {"WAVE", "WP", "WPM", "WPRS", "SYNC", "SFRQ", "-", "TUNE"}, {0, 0, 0, 96, 0, 0, 0, 64}, 0x80};
constexpr MachineDef kBboxDef = {Machine::BBOX, "DPRO BBOX", {"PTCH", "STRT", "-", "-", "RTRG", "RTIM", "-", "-"}, {64, 0, 0, 0, 0, 0, 0, 0}, 0x00};
constexpr MachineDef kDdrwDef = {Machine::DDRW, "DPRO DDRW", {"WAV1", "MIX", "WAV2", "TIME", "BR1", "WID", "BR2", "TUNE"}, {0, 64, 0, 0, 0, 0, 0, 64}, 0x82};
constexpr MachineDef kDensDef = {Machine::DENS, "DPRO DENS", {"PCH2", "PCH3", "PCH4", "WAVE", "-", "CHRL", "CHRW", "TUNE"}, {63, 63, 63, 0, 0, 0, 0, 64}, 0x80};
constexpr MachineDef kGndDef  = {Machine::GND,  "GND ---",  {"-", "-", "-", "-", "-", "-", "-", "-"}, {0, 0, 0, 0, 0, 0, 0, 0}, 0x00};
constexpr MachineDef kSinDef  = {Machine::SIN,  "GND SIN",  {"-", "-", "-", "-", "-", "-", "-", "TUNE"}, {0, 0, 0, 0, 0, 0, 0, 64}, 0x80};
constexpr MachineDef kNoisDef = {Machine::NOIS, "GND NOIS", {"ST", "RED", "STON", "-", "-", "-", "-", "TUNE"}, {0, 0, 0, 0, 0, 0, 0, 64}, 0x80};
constexpr MachineDef kSawDef  = {Machine::SAW,  "SWAVE SAW",  {"UNIL", "UNIW", "UNIX", "-", "SUBX", "SUB1", "SUB2", "TUNE"}, {0, 0, 0, 0, 0, 0, 0, 64}, 0x80};
constexpr MachineDef kPulsDef = {Machine::PULS, "SWAVE PULS", {"UNIL", "UNIW", "SUB1", "SUB2", "PW", "PWAD", "PWRS", "TUNE"}, {0, 0, 0, 0, 64, 0, 0, 64}, 0x80};
constexpr MachineDef kEnsDef  = {Machine::ENS,  "SWAVE ENS",  {"PCH2", "PCH3", "PCH4", "WAVE", "PW", "CHRL", "CHRW", "TUNE"}, {63, 63, 63, 0, 64, 0, 127, 64}, 0x80};
constexpr MachineDef kDynamixDef = {Machine::DYNAMIX, "DYNAMIX", {"ATK", "REL", "THRS", "MIX", "RAT", "GAIN", "RMS", "INP"}, {64, 64, 64, 127, 0, 0, 0, 64}, 0x00};
constexpr MachineDef kRingmodDef = {Machine::RINGMOD, "RINGMOD", {"WAVE", "EXT", "-", "MIX", "-", "-", "-", "INP"}, {0, 0, 0, 0, 0, 0, 0, 64}, 0x00};
constexpr MachineDef kPhaserDef  = {Machine::PHASER,  "PHASER",  {"CNTR", "DEP", "SPD", "MIX", "FB", "WID", "-", "INP"}, {64, 64, 64, 127, 45, 0, 127, 64}, 0x00};
constexpr MachineDef kFlangerDef = {Machine::FLANGER, "FLANGER", {"DEL", "DEP", "SPD", "MIX", "FB", "WID", "-", "INP"}, {64, 64, 64, 127, 45, 0, 127, 64}, 0x00};
constexpr MachineDef kThruDef   = {Machine::THRU,   "THRU",   {"-", "-", "-", "-", "-", "-", "-", "INP"}, {0, 0, 0, 0, 0, 0, 0, 64}, 0x00};
constexpr MachineDef kReverbDef = {Machine::REVERB, "REVERB", {"DEC", "DAMP", "GATE", "MIX", "HP", "LP", "-", "INP"}, {64, 2, 127, 32, 0, 127, 0, 64}, 0x00};
constexpr MachineDef kChorusDef = {Machine::CHORUS, "CHORUS", {"DEL", "DEP", "SPD", "MIX", "FB", "WID", "LP", "INP"}, {64, 64, 64, 127, 0, 0, 127, 64}, 0x00};

constexpr MachineDef kMachineDefs[] = {kFmStatDef, kFmParDef, kFmDynDef, kGndDef, kSinDef, kNoisDef,
    kSawDef, kPulsDef, kEnsDef, kSidDef, kWaveDef, kBboxDef, kDdrwDef, kDensDef, kVo6Def, kThruDef, kReverbDef, kChorusDef, kDynamixDef, kRingmodDef, kPhaserDef, kFlangerDef};
constexpr int kNumMachineDefs = 22;
inline const MachineDef* machineDef(Machine m) { for (auto& d : kMachineDefs) if (d.machine == m) return &d; return nullptr; }
// FX machines: THRU (12), REVERB (13), CHORUS..FLANGER (15..19). SWAVE-ENS is 14 and sits inside that
// range, so it must be excluded explicitly (treating it as FX held its envelope open and fed it from
// the side-chain, which is why it appeared to sustain after note-off).
constexpr bool isFxMachine(Machine m)
{
    return m == Machine::THRU || m == Machine::REVERB || (m >= Machine::CHORUS && m <= Machine::FLANGER);
}

// Block word 37 is the track's routing/flag word (from the kit byte "types[t]" + the 2-bit-per-track bus
// field of patchBusIn). Bits 0-2 = OUT BUS AB/CD/EF the track's output is added to (add, or replace when the
// track also reads that bus = insert). Machine input: bit 4 = the aux buffer, which holds the previous track's output (NEIBOR) or,
// with bits 6/7 set, a copy of ADC L / R / both (INP A / INP B / INP A+B); bit 3 = read mix bus AB/CD/EF
// selected by bits 12/13/14 (BUS AB/CD/EF). Bit 9 = the low-pass filter tracks the note pitch, bit 11 = the
// high-pass does (KIT > ASSIGN > KEY: LPF/HPF, kit bytes lpKeyTrack/hpKeyTrack bit t, on by default).
// Bit 8 = global routing AB=MIX, bit 10 = 6xMONO.
constexpr uint32_t kRouteOutAB = 1u << 0, kRouteOutCD = 1u << 1, kRouteOutEF = 1u << 2;
constexpr uint32_t kRouteBusIn = 1u << 3, kRouteAuxIn = 1u << 4, kRouteAdcL = 1u << 6, kRouteAdcR = 1u << 7;
constexpr uint32_t kRouteBusAB = 1u << 12, kRouteBusCD = 1u << 13, kRouteBusEF = 1u << 14;
constexpr uint32_t kRouteLpKeyTrack = 1u << 9, kRouteHpKeyTrack = 1u << 11;
constexpr uint32_t kRouteExternalStereoIn = kRouteAdcL | kRouteAdcR | kRouteAuxIn;   // INP A+B

// FX machine input source, in the order of the sysex "set track routing" message ($5C) and KIT > EDIT.
enum class FxInput : uint8_t { Neighbor = 0, InpA = 1, InpB = 2, InpAB = 3, BusAB = 4, BusCD = 5, BusEF = 6 };
constexpr const char* kFxInputNames[7] = {"NEIBOR", "INP A", "INP B", "INP AB", "BUS AB", "BUS CD", "BUS EF"};
// The input bits of word 37 for a source (types byte bits 3/4/6/7 + bus field).
// Note for the emulation: the DSP fills the aux buffer / mix-bus rings itself on the hardware; here every
// internal source is mixed by the plugin and handed to the DSP through the ADC ring, so the bits actually
// sent for NEIBOR and BUS sources are those of INP A+B (dspInputBits). INP A / INP B keep their own bits:
// the DSP then copies the one ADC channel to both sides, as on the hardware.
constexpr uint32_t fxInputBits(FxInput in)
{
    switch (in) {
    case FxInput::Neighbor: return kRouteAuxIn;
    case FxInput::InpA: return kRouteAuxIn | kRouteAdcL;
    case FxInput::InpB: return kRouteAuxIn | kRouteAdcR;
    case FxInput::InpAB: return kRouteAuxIn | kRouteAdcL | kRouteAdcR;
    case FxInput::BusAB: return kRouteBusIn | kRouteBusAB;
    case FxInput::BusCD: return kRouteBusIn | kRouteBusCD;
    case FxInput::BusEF: return kRouteBusIn | kRouteBusEF;
    }
    return kRouteAuxIn;
}
constexpr uint32_t dspInputBits(FxInput in)
{
    return in == FxInput::InpA || in == FxInput::InpB || in == FxInput::InpAB ? fxInputBits(in) : kRouteExternalStereoIn;
}
// AMP page for an FX track: envelope held open (DEC/REL = 127 = infinite), as on the hardware
constexpr std::array<uint8_t, 8> kDefaultAmpFx = {0, 0, 127, 127, 64, 64, 64, 0};

constexpr const char* kAmpLabels[8]   = {"ATK", "HOLD", "DEC", "REL", "DIST", "VOL", "PAN", "PORT"};
constexpr const char* kFiltLabels[8]  = {"BASE", "WDTH", "HPQ", "LPQ", "ATK", "DEC", "BOFS", "WOFS"};
constexpr const char* kEfxLabels[8]   = {"EQF", "EQG", "SRR", "DTIM", "DSND", "DFB", "DBAS", "DWID"};

// Hardware display: AMP DIST/PAN, FILT BOFS/WOFS, EFX EQG/DSND show as value-64 (-64..+63).
constexpr uint8_t kAmpBipolarMask = 0x50, kFiltBipolarMask = 0xC0, kEfxBipolarMask = 0x12;

} // namespace mnm::host
