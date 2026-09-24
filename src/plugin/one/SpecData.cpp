// Display description of every machine's knobs (see SpecData.h).
#include "SpecData.h"

namespace mnm::uispec {

// ---- list / readout value names ----
static const char* const kValues0[2] = {"OFF", "ON"};
static const char* const kValues1[5] = {"TRI", "SAW", "PULS", "MIX", "NOIS"};
static const char* const kValues2[4] = {"OFF", "RING", "SYNC", "R+S"};
static const char* const kValues3[2] = {"MFRQ", "PRCH"};
static const char* const kValues4[33] = {"-12", "-11", "-10", "-09", "-08", "2/3", "-07", "-06", "-05", "3/4", "-04", "4/5", "5/6", "-03", "-02", "-01", "OFF", "+01", "+02", "+03", "6/5", "5/4", "+04", "4/3", "+05", "+06", "+07", "3/2", "+08", "+09", "+10", "+11", "+12"};
static const char* const kValues5[32] = {"S01", "S02", "S03", "S04", "S05", "S06", "S07", "S08", "S09", "S10", "S11", "S12", "S13", "S14", "S15", "S16", "S17", "S18", "S19", "S20", "S21", "S22", "S23", "S24", "S25", "S26", "S27", "S28", "S29", "S30", "S31", "S32"};
static const char* const kValues6[3] = {"OFF", "SFRQ", "PRCH"};
static const char* const kValues7[64] = {"D01", "D02", "D03", "D04", "D05", "D06", "D07", "D08", "D09", "D10", "D11", "D12", "D13", "D14", "D15", "D16", "D17", "D18", "D19", "D20", "D21", "D22", "D23", "D24", "D25", "D26", "D27", "D28", "D29", "D30", "D31", "D32", "D33", "D34", "D35", "D36", "D37", "D38", "D39", "D40", "D41", "D42", "D43", "D44", "D45", "D46", "D47", "D48", "D49", "D50", "D51", "D52", "D53", "D54", "D55", "D56", "D57", "D58", "D59", "D60", "D61", "D62", "D63", "D64"};
static const char* const kValues8[24] = {"1/64", "1/32", "1/16", "3/32", "1/8", "3/16", "1/4", "5/16", "3/8", "5/32", "7/16", "1/2", "5/8", "3/4", "7/8", "1", "1.25", "1.5", "1.75", "2", "2.5", "3", "3.5", "4"};
static const char* const kValues9[128] = {"0.0", "1/64", "1/32", ".046", "1/16", ".078", "3/32", ".109", "1/8", ".140", "5/32", ".171", "3/16", ".203", ".218", ".234", "1/4", ".265", ".281", ".296", "5/16", ".328", ".343", ".359", "3/8", ".390", ".406", ".421", "7/16", ".453", ".468", ".484", "1/2", ".515", ".531", ".546", "9/16", ".578", ".593", ".609", "5/8", ".640", ".656", ".671", ".687", ".703", ".718", ".734", "3/4", ".765", ".781", ".796", ".812", ".828", ".843", ".859", "7/8", ".890", ".906", ".921", ".937", ".953", ".968", ".984", "1.0", "1.01", "1.03", "1.04", "1.06", "1.07", "1.09", "1.10", "1.12", "1.14", "1.15", "1.17", "1.18", "1.20", "1.21", "1.23", "1.25", "1.26", "1.28", "1.29", "1.31", "1.32", "1.34", "1.35", "1.37", "1.39", "1.40", "1.42", "1.43", "1.45", "1.46", "1.48", "1.5", "1.51", "1.53", "1.54", "1.56", "1.57", "1.59", "1.60", "1.62", "1.64", "1.65", "1.67", "1.68", "1.70", "1.71", "1.73", "1.75", "1.76", "1.78", "1.79", "1.81", "1.82", "1.84", "1.85", "1.87", "1.89", "1.90", "1.92", "1.93", "1.95", "1.96", "2.0"};
static const char* const kValues10[128] = {"0.0", ".000", ".001", ".002", ".003", ".006", ".008", ".011", "1/64", ".019", ".024", ".029", ".035", ".041", ".047", ".054", "1/16", ".070", ".079", ".088", ".097", ".107", ".118", ".129", "9/64", ".152", ".165", ".177", ".191", ".205", ".219", ".234", "1/4", ".265", ".282", ".299", ".316", ".334", ".352", ".371", ".390", ".410", ".430", ".451", ".472", ".494", ".516", ".539", "9/16", ".586", ".610", ".635", ".660", ".685", ".711", ".738", ".765", ".793", ".821", ".849", ".878", ".908", ".938", ".968", "1.0", "1.03", "1.06", "1.09", "1.12", "1.16", "1.19", "1.23", "1.26", "1.30", "1.33", "1.37", "1.41", "1.44", "1.48", "1.52", "1.56", "1.60", "1.64", "1.68", "1.72", "1.76", "1.80", "1.84", "1.89", "1.93", "1.97", "2.02", "2.06", "2.11", "2.15", "2.20", "2.25", "2.29", "2.34", "2.39", "2.44", "2.49", "2.54", "2.59", "2.64", "2.69", "2.74", "2.79", "2.84", "2.90", "2.95", "3.0", "3.06", "3.11", "3.17", "3.22", "3.28", "3.34", "3.39", "3.45", "3.51", "3.57", "3.63", "3.69", "3.75", "3.81", "3.87", "4.0"};
static const char* const kValues11[21] = {"-", "B", "D", "F", "G", "H", "J", "K", "L", "M", "N", "P", "R", "RR", "S", "SJ", "T", "TH", "TJ", "V", "Z"};

const Machine kMachines[kNumMachines] = {
    {0, "GND", "GND", "GND-GND", false, true, {
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
    }},
    {1, "GND", "SIN", "GND-SIN", false, true, {
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"TUNE", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
    }},
    {2, "GND", "NOIS", "GND-NOIS", false, true, {
        {"ST", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"RED", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"STON", Display::List, false, 0, 127, 2, Icons::Toggle, kValues0},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"TUNE", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
    }},
    {3, "SID", "6581", "SID-6581", false, true, {
        {"PW", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"PWAD", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"PWRS", Display::List, false, 96, 127, 2, Icons::Toggle, kValues0},
        {"WAVE", Display::List, false, 0, 127, 5, Icons::SidWave, kValues1},
        {"MOD", Display::List, false, 0, 127, 4, Icons::SidWave, kValues2},
        {"MSRC", Display::List, false, 0, 127, 2, Icons::Toggle, kValues3},
        {"MFRQ", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
        {"TUNE", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
    }},
    {4, "SWAVE", "SAW", "SWAVE-SAW", false, true, {
        {"UNIL", Display::Numeric, true, 0, 127, 128, Icons::None, nullptr},
        {"UNIW", Display::Numeric, true, 0, 127, 128, Icons::None, nullptr},
        {"UNIX", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"SUBX", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"SUB1", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"SUB2", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"TUNE", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
    }},
    {5, "SWAVE", "PULS", "SWAVE-PULS", false, true, {
        {"UNIL", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"UNIW", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"SUB1", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"SUB2", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"PW", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
        {"PWAD", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"PWRS", Display::List, false, 0, 127, 2, Icons::Toggle, kValues0},
        {"TUNE", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
    }},
    {14, "SWAVE", "ENS", "SWAVE-ENS", false, true, {
        {"PCH2", Display::List, false, 63, 127, 33, Icons::EnsPitch, kValues4},
        {"PCH3", Display::List, false, 63, 127, 33, Icons::EnsPitch, kValues4},
        {"PCH4", Display::List, false, 63, 127, 33, Icons::EnsPitch, kValues4},
        {"WAVE", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"PW", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
        {"CHRL", Display::Numeric, true, 0, 127, 128, Icons::None, nullptr},
        {"CHRW", Display::Numeric, false, 127, 127, 128, Icons::None, nullptr},
        {"TUNE", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
    }},
    {6, "DPRO", "WAVE", "DPRO-WAVE", false, true, {
        {"WAVE", Display::List, false, 0, 127, 32, Icons::DproWave, kValues5},
        {"WP", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"WPM", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"WPRS", Display::List, false, 96, 127, 2, Icons::Toggle, kValues0},
        {"SYNC", Display::List, false, 0, 127, 3, Icons::DproSync, kValues6},
        {"SFRQ", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"TUNE", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
    }},
    {7, "DPRO", "BBOX", "DPRO-BBOX", false, true, {
        {"PTCH", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
        {"STRT", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"RTRG", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"RTIM", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
    }},
    {32, "DPRO", "DDRW", "DPRO-DDRW", false, true, {
        {"WAV1", Display::List, false, 0, 127, 64, Icons::DdrwWave, kValues7},
        {"MIX", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
        {"WAV2", Display::List, false, 0, 127, 64, Icons::DdrwWave, kValues7},
        {"TIME", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"BR1", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"WID", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"BR2", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"TUNE", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
    }},
    {33, "DPRO", "DENS", "DPRO-DENS", false, true, {
        {"PCH2", Display::List, false, 63, 127, 33, Icons::EnsPitch, kValues4},
        {"PCH3", Display::List, false, 63, 127, 33, Icons::EnsPitch, kValues4},
        {"PCH4", Display::List, false, 63, 127, 33, Icons::EnsPitch, kValues4},
        {"WAVE", Display::List, false, 0, 127, 64, Icons::DdrwWave, kValues7},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"CHRL", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"CHRW", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"TUNE", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
    }},
    {8, "FM+", "STAT", "FM+-STAT", false, true, {
        {"1FRQ", Display::List, false, 60, 127, 24, Icons::FmRatio, kValues8},
        {"1FIN", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
        {"1ENV", Display::Numeric, false, 80, 127, 128, Icons::None, nullptr},
        {"1FB", Display::Numeric, false, 30, 127, 128, Icons::None, nullptr},
        {"2FRQ", Display::List, false, 80, 127, 24, Icons::FmRatio, kValues8},
        {"2VOL", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
        {"TONE", Display::Numeric, false, 98, 127, 128, Icons::None, nullptr},
        {"TUNE", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
    }},
    {9, "FM+", "PAR", "FM+-PAR", false, true, {
        {"1FRQ", Display::List, false, 60, 127, 24, Icons::FmRatio, kValues8},
        {"1ENV", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
        {"2FRQ", Display::List, false, 80, 127, 24, Icons::FmRatio, kValues8},
        {"2ENV", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
        {"3FRQ", Display::List, false, 102, 127, 24, Icons::FmRatio, kValues8},
        {"3ENV", Display::Numeric, false, 80, 127, 128, Icons::None, nullptr},
        {"TONE", Display::Numeric, false, 98, 127, 128, Icons::None, nullptr},
        {"TUNE", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
    }},
    {10, "FM+", "DYN", "FM+-DYN", false, true, {
        {"1FRQ", Display::Readout, false, 64, 127, 128, Icons::FmDynFrq, kValues9},
        {"1FEN", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
        {"1VOL", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
        {"1VEN", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
        {"2FRQ", Display::Readout, false, 74, 127, 128, Icons::FmDynFrq, kValues10},
        {"2ENV", Display::Numeric, false, 80, 127, 128, Icons::None, nullptr},
        {"2FB", Display::Numeric, false, 30, 127, 128, Icons::None, nullptr},
        {"TUNE", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
    }},
    {11, "VO", "VO-6", "VO-VO-6", false, true, {
        {"VOC1", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
        {"VOC2", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
        {"V-SW", Display::List, false, 96, 127, 2, Icons::Toggle, kValues0},
        {"VOIC", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"CONS", Display::List, false, 0, 127, 21, Icons::VoCons, kValues11},
        {"CLEN", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
        {"CVOL", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
        {"TUNE", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
    }},
    {12, "FX", "THRU", "FX-THRU", true, true, {
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"INP", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
    }},
    {13, "FX", "REVERB", "FX-REVERB", true, true, {
        {"DEC", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
        {"DAMP", Display::Numeric, false, 2, 127, 128, Icons::None, nullptr},
        {"GATE", Display::Numeric, false, 127, 127, 128, Icons::None, nullptr},
        {"MIX", Display::Numeric, false, 32, 127, 128, Icons::None, nullptr},
        {"HP", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"LP", Display::Numeric, false, 127, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"INP", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
    }},
    {15, "FX", "CHORUS", "FX-CHORUS", true, true, {
        {"DEL", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
        {"DEP", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
        {"SPD", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
        {"MIX", Display::Numeric, false, 127, 127, 128, Icons::None, nullptr},
        {"FB", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"WID", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"LP", Display::Numeric, false, 127, 127, 128, Icons::None, nullptr},
        {"INP", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
    }},
    {16, "FX", "DYNAMIX", "FX-DYNAMIX", true, true, {
        {"ATK", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
        {"REL", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
        {"THRS", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
        {"MIX", Display::Numeric, false, 127, 127, 128, Icons::None, nullptr},
        {"RAT", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"GAIN", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"RMS", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"INP", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
    }},
    {17, "FX", "RINGMOD", "FX-RINGMOD", true, true, {
        {"WAVE", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"EXT", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"MIX", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 0, 127, 128, Icons::None, nullptr},
        {"INP", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
    }},
    {18, "FX", "PHASER", "FX-PHASER", true, true, {
        {"CNTR", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
        {"DEP", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
        {"SPD", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
        {"MIX", Display::Numeric, false, 127, 127, 128, Icons::None, nullptr},
        {"FB", Display::Bipolar, false, 45, 127, 128, Icons::None, nullptr},
        {"WID", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 127, 127, 128, Icons::None, nullptr},
        {"INP", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
    }},
    {19, "FX", "FLANGER", "FX-FLANGER", true, true, {
        {"DEL", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
        {"DEP", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
        {"SPD", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
        {"MIX", Display::Numeric, false, 127, 127, 128, Icons::None, nullptr},
        {"FB", Display::Bipolar, false, 45, 127, 128, Icons::None, nullptr},
        {"WID", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
        {"---", Display::Blank, false, 127, 127, 128, Icons::None, nullptr},
        {"INP", Display::Bipolar, false, 64, 127, 128, Icons::None, nullptr},
    }},
};

const SharedPage kSharedPages[3] = {
    {"AMP", {"ATK", "HOLD", "DEC", "REL", "DIST", "VOL", "PAN", "PORT"}, {0, 0, 64, 64, 64, 64, 64, 0}, 0x50},
    {"FILT", {"BASE", "WDTH", "HPQ", "LPQ", "ATK", "DEC", "BOFS", "WOFS"}, {0, 127, 0, 0, 0, 32, 64, 64}, 0xC0},
    {"EFX", {"EQF", "EQG", "SRR", "DTIM", "DSND", "DFB", "DBAS", "DWID"}, {64, 64, 0, 64, 64, 28, 0, 127}, 0x12},
};
const uint8_t kAmpDefaultsFx[8] = {0, 0, 127, 127, 64, 64, 64, 0};

// ---- LFO page (identical for LFO1-3) ----
const char* const kLfoPageNames[9] = {"PTCH", "SYNT", "AMP", "FILT", "EFFX", "LFO1", "LFO2", "LFO3", "MIDI"};
const char* const kLfoDestNames[9][8] = {
    {"1/12", "2/12", "7/12", "1OCT", "2OCT", "4OCT", "8OCT", "16OC"},
    {"PAR1", "PAR2", "PAR3", "PAR4", "PAR5", "PAR6", "PAR7", "PAR8"},
    {"ATK", "HOLD", "DEC", "REL", "DIST", "VOL", "PAN", "PORT"},
    {"BASE", "WDTH", "HPQ", "LPQ", "ATK", "DEC", "BOFS", "WOFS"},
    {"EQF", "EQG", "SRR", "DTIM", "DSND", "DFB", "DBAS", "DWID"},
    {"PAGE", "DEST", "TRIG", "WAVE", "MULT", "SPD", "INTL", "DPTH"},
    {"PAGE", "DEST", "TRIG", "WAVE", "MULT", "SPD", "INTL", "DPTH"},
    {"PAGE", "DEST", "TRIG", "WAVE", "MULT", "SPD", "INTL", "DPTH"},
    {"LEN", "VEL", "PB", "PCHG", "CC 1", "CC 2", "CC 3", "CC 4"},
};
const char* const kLfoTrigNames[5] = {"FREE", "TRIG", "HOLD", "ONE", "HALF"};
const char* const kLfoWaveNames[11] = {"TRI", "ITRI", "SAW", "ISAW", "SQR", "ISQR", "EXP", "IEXP", "RMP", "IRMP", "RND"};
const char* const kLfoMultNames[7] = {"1X", "2X", "4X", "8X", "16X", "32X", "64X"};
const Param kLfoParams[8] = {
    {"PAGE", Display::List, false, 0, 127, 9, Icons::LfoPage, kLfoPageNames},
    {"DEST", Display::List, false, 64, 127, 8, Icons::LfoDest, kLfoDestNames[0]},   // names follow PAGE; the UI swaps the table
    {"TRIG", Display::List, false, 0, 127, 5, Icons::Switch, kLfoTrigNames},
    {"WAVE", Display::List, false, 0, 127, 11, Icons::LfoWave, kLfoWaveNames},
    {"MULT", Display::List, false, 1, 127, 7, Icons::Switch, kLfoMultNames},
    {"SPD", Display::Numeric, false, 64, 127, 128, Icons::None, nullptr},
    {"INTL", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
    {"DPTH", Display::Numeric, false, 0, 127, 128, Icons::None, nullptr},
};

} // namespace mnm::uispec
