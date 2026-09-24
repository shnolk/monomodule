// DigiPRO Digibank: the 64 user waveforms DPRO-DDRW and DPRO-DENS (MKII machines) read from DSP
// external memory Y:$150000 + slot*$800. On the unit they are uploaded from the +Drive, which is not
// part of the OS file, so the plugin builds a stand-in bank.
//
// Slot format, as the DDRW/DENS machines read it: a *mip-mapped
// single cycle* of 24-bit samples: 1024 samples at +0, then band-limited copies of 512 at +$400,
// 256 at +$600, 128 at +$700, 64 at +$780, 32 at +$7C0, 16 at +$7E0, 8 at +$7F0 and 4 at +$7F8
// (2044 words; the last 4 are unused). The machines pick the level from the leading-bit count of
// the phase increment, i.e. by pitch. Levels are read with modulo addressing and interpolated;
// samples above ~2^21 saturate the interpolator, so the bank is kept well below that.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "firmware/Firmware.h"

namespace mnm::dsp {

struct Digibank {
    static constexpr int kSlots = 64, kSlotWords = 2048, kLevel0 = 1024, kLevels = 9;
    static constexpr uint32_t kBase = 0x150000;
    static constexpr int kLevelOffset[kLevels] = {0x000, 0x400, 0x600, 0x700, 0x780, 0x7C0, 0x7E0, 0x7F0, 0x7F8};
    static constexpr int kLevelSize[kLevels]   = {1024, 512, 256, 128, 64, 32, 16, 8, 4};

    std::array<std::array<int32_t, kSlotWords>, kSlots> slot{};
    std::array<std::string, kSlots> name{};

    // Builds one slot from a single cycle (any length >= 8, values nominally -1..1): band-limited
    // resampling to 1024 and to every mip level (FFT, harmonics above each level's Nyquist dropped,
    // DC removed). `gain` is the 24-bit value of 1.0. kGain = 12-bit sample << 8: the DSP's
    // interpolator is linear up to ~2^21, and at this level DDRW plays the factory sine within 15 % of
    // the loudness the WAVE machine gives it (other waves land 1.1-1.7x; WAVE is not a plain playback
    // of its records, so an exact match needs the real bank and a hardware A/B).
    static constexpr double kGain = 524288.0;   // 2^19
    static void buildSlot(const std::vector<double>& cycle, double gain, std::array<int32_t, kSlotWords>& out);

    // Stand-in for the factory Digibank: slots 0-31 are the 32 DPRO-WAVE waveforms of the OS payload
    // (512-sample 12-bit records at P:$101D7B, played as one period like the WAVE machine does, at
    // the loudness the WAVE machine gives them), slots 32-63 a set of classic single-cycle shapes.
    static std::shared_ptr<const Digibank> standIn(const fw::Firmware& fw);

    // The 33 DPRO-WAVE records of the payload as 512-sample cycles (12-bit two's complement pairs,
    // high half first), scaled to -1..1; empty if the payload lacks the record.
    static std::vector<double> waveRecord(const fw::Firmware& fw, int index);
};

} // namespace mnm::dsp
