// Track LFOs. On the Monomachine the LFOs are not part of the DSP sound engine: they are computed by the
// host in 32-bit integer arithmetic, and each LFO *adds* its value to its target parameter word (or to another
// LFO's parameter, or to the pitch-modulation word 30) before the parameter block is sent to the DSP.
// Integer widths and truncation are chosen so the results match the hardware's output bit for bit.
//
// Schedule (one call per 16-frame DSP block, a "frame" = every 3rd block; f = frame counter):
//   every frame   apply()                    add the LFO values to the un-modulated block words
//   even f        interlaceAndAccumulate()   interlace timers, acc += delta (4 times per step)
//   f % 8 == 1    apply() again after the block words were slewed (HostModel::smoothing)
//   f % 8 == 0    step()                     advance the phase, evaluate the waveform, compute the new delta
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace mnm::host {

enum LfoParam : int { LfoPage = 0, LfoDest, LfoTrig, LfoWave, LfoMult, LfoSpd, LfoIntl, LfoDpth };
enum LfoTrigMode : int { LfoFree = 0, LfoTrigged = 1, LfoHold = 2, LfoOne = 3, LfoHalf = 4 };
enum LfoPageIndex : int { LfoPagePtch = 0, LfoPageSynt, LfoPageAmp, LfoPageFilt, LfoPageEffx, LfoPageLfo1, LfoPageLfo2, LfoPageLfo3, LfoPageMidi };

struct LfoState {
    int32_t pos = 0;             // phase, 0..0x7FFFFF = one cycle
    int32_t out = 0;             // waveform output, -0x800000..0x800000
    int32_t hold = 0;            // output latched at the last LFO trig (HOLD mode reads this)
    int32_t inc = 0;             // phase increment of the last step
    int32_t rem = 0;             // remainder of the speed division, carried between steps
    int32_t toggle = 0;          // interlace gate; 0 = the LFO is not applied this frame
    int32_t countdown = 0;       // interlace timer
    int32_t rndA = 0x012D3D16;   // RND generator state and its power-on seed
    int32_t rndB = 0x0000029A;
    int8_t rndSeg = 0;           // RND segment (pos >> 20) of the last drawn value
    int32_t acc = 0;             // the value currently added to the target
    int32_t delta = 0;           // added to acc on every even frame
    int32_t start = 0;           // waveform start value, applied on an LFO trig (TRIG/ONE/HALF)
    int8_t page = 0, dest = 0;   // the target, latched when the LFO was last evaluated
};

// The three LFOs of one track.
class TrackLfos {
public:
    static constexpr int kNumLfos = 3;
    static constexpr int kBlockWords = 52;
    static constexpr int kBlocksPerStep = 24;   // 8 frames x 3 blocks: DSP blocks between two steps
    static constexpr int32_t kDivisor = 0xF23FA, kDivisorHalf = 0x791FD;   // 992250 = 44100 x 22.5

    TrackLfos();

    void setRaw(int lfo, int k, int raw);   // kit byte 0..127 (also refreshes the shadow, like a knob edit)
    int raw(int lfo, int k) const { return m_raw[size_t(lfo)][size_t(k)]; }
    void trig() { m_trig = 1; }             // LFO trig pending, set by note-on; consumed by step()
    bool trigPending() const { return m_trig != 0; }

    void step(int32_t elapsed, int32_t tick);      // elapsed = DSP blocks since the last step
    void interlaceAndAccumulate(int32_t tick);
    // Rebuilds the LFO parameter shadow (raw << 16) and adds every gated LFO's value to its target: a block
    // word in `words` (which must hold the un-modulated values) or a shadow parameter of this track.
    // trigPending is the pending LFO trig as the smoothing pass sees it.
    void apply(uint32_t* words, bool trigPending);

    const LfoState& state(int lfo) const { return m_s[size_t(lfo)]; }
    LfoState& state(int lfo) { return m_s[size_t(lfo)]; }
    int32_t shadow(int lfo, int k) const { return m_shadow[size_t(lfo)][size_t(k)]; }

    // List index of a parameter: raw * N >> 7 on the high word of the shadow (the +0x7F branch only
    // matters for negative words, which the clamp in apply() never produces).
    static int index(int32_t hiWord, int n) { int32_t v = hiWord * n; if (v < 0) v += 0x7F; return v >> 7; }
    static int pageOf(int32_t shadow) { return index(int16_t(shadow >> 16), 9); }
    static int destOf(int32_t shadow) { return int32_t(uint32_t(shadow) << 3) >> 23; }   // (x<<3)>>23
    static int trigModeOf(int32_t shadow) { return index(int16_t(shadow >> 16), 5); }
    static int waveOf(int32_t shadow) { return index(int16_t(shadow >> 16), 11); }
    static int multOf(int32_t shadow) { return index(int16_t(shadow >> 16), 7); }

    // Block word a (page, dest) pair modulates, or -1 for the LFO pages and MIDI.
    static int targetWord(int page, int dest);

private:
    void evaluate(int lfo, int32_t restart, int32_t inc);

    std::array<std::array<uint8_t, 8>, kNumLfos> m_raw{};
    std::array<std::array<int32_t, 8>, kNumLfos> m_shadow{};   // raw << 16, plus LFO->LFO modulation after apply()
    std::array<LfoState, kNumLfos> m_s{};
    int32_t m_trig = 0;
};

} // namespace mnm::host
