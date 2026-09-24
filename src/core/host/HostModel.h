// Host-side model of one track: builds the 52-word parameter block the DSP sound engine reads, the way the
// Monomachine's main processor does. One call to nextBlock() per 16-frame DSP block.
//
// Timing: blocks come in groups of three; the first of each group is a "frame" (48 samples). Per frame the
// trigger words are latched from the note state and the LFO values are added to the block; on even frames the
// LFOs accumulate; every 8th frame (f%8 == 1) the page words slew towards their raw values,
// (3w + raw<<16) >> 2; every 8th frame (f%8 == 0) the LFOs step. The tick (word 42) is 24 x BPM, word 35 its
// reciprocal.
#pragma once
#include <array>
#include <cstdint>
#include "Lfo.h"
#include "Machines.h"

namespace mnm::host {

enum Word : int {
    AmpAtk = 0, AmpHold, AmpDec, AmpRel, AmpDist, AmpVol, AmpPan, AmpPort,
    FiltBase = 8, FiltWdth, FiltHpq, FiltLpq, FiltAtk, FiltDec, FiltBofs, FiltWofs,
    EfxEqf = 16, EfxEqg, EfxSrr, EfxDtim, EfxDsnd, EfxDfb, EfxDbas, EfxDwid,
    Extra0 = 24, Extra4 = 28, W29 = 29, PitchMod = 30, Level = 31, FiltTrig = 32, W33 = 33, W34 = 34,
    TickRecip = 35, MachineIdx = 36, Mode = 37, Tune = 38, Peek = 39, NoteTrig = 40, Pitch = 41, Tick = 42, W43 = 43,
    Syn0 = 44
};

struct ParamBlock { std::array<uint32_t, 52> w{}; };

class HostModel {
public:
    static constexpr int kBlocksPerFrame = 3;
    static constexpr uint32_t kPitchModRest = 64u << 16;   // word 30 at rest

    HostModel();

    void setMachine(Machine m);                       // also loads page defaults (as machine assign does)
    // Word 37 routing (see Machines.h). The emulated track receives every input through the ADC path
    // (setInputFrames), so the plugin mixes NEIBOR/BUS sources host-side and this only records the
    // hardware's flag bits; setRouting(bits) replaces the input part (bits 3/4/6/7/12-14) wholesale.
    void setRouting(uint32_t bits) { m_routing = bits; }
    void setOutBuses(uint32_t mask) { m_outBuses = mask & 7u; }             // OUT BUS AB/CD/EF bits
    void setKeyTracking(bool lowPass, bool highPass) { m_lpKeyTrack = lowPass; m_hpKeyTrack = highPass; }
    uint32_t routingWord() const;                      // word 37 as sent (without the machine-independent one-shots)
    Machine machine() const { return m_machine; }
    void setParam(Page page, int param, int value);   // 0..127
    int  param(Page page, int param) const { return m_raw[int(page) * 8 + param]; }
    void setLfoParam(int lfo, int k, int value) { m_lfos.setRaw(lfo, k, value); }   // 0..127, LFO pages 4-6
    int  lfoParam(int lfo, int k) const { return m_lfos.raw(lfo, k); }
    TrackLfos& lfos() { return m_lfos; }
    const TrackLfos& lfos() const { return m_lfos; }
    void setLevel(int v);                              // 0..127
    void setMasterTuneHz(double hz);                   // 400.0..440.0
    void setBpm(double bpm);                           // tick = 24 x BPM, clamped 30..300 BPM
    void setTick(uint32_t tick);                       // raw tick value (word 42); word 35 = 0x800000/tick
    uint32_t tick() const { return m_tick; }

    void noteOn(int midiNote);                         // 0..127; also the LFO trig
    void noteOff();
    void forceInit();                                  // next frame carries the init/trig bit

    // Snaps the slewed words to their targets. Not a hardware behaviour (the unit glides in over ~0.2 s
    // after a kit load); used so a freshly created engine starts from a settled state.
    void settle();

    const ParamBlock& nextBlock();                     // call once per 16-frame block
    const ParamBlock& current() const { return m_block; }
    uint32_t frameCount() const { return m_frame; }

    static uint32_t pitchWord(int note) { return uint32_t((note << 11) / 12); }

private:
    void frame();        // frame start (first block of a group of three)
    void smoothing();    // slew + re-apply the LFOs
    void rebuild(bool trigPending);   // m_block = m_words + LFO modulation

    std::array<int, 72> m_raw{};    // kit bytes: SYN 0-7 AMP 8-15 FILT 16-23 EFX 24-31 ... extras 64-68
    std::array<uint32_t, 52> m_words{};   // un-modulated block words (restored before the LFOs are re-applied)
    ParamBlock m_block;
    TrackLfos m_lfos;
    int m_level = 100;
    uint32_t m_tune = 0xFFFFFF, m_tick = 2880;
    // Note state for the next frame, as the OS keeps it in T+0 (one byte: 1 = note on, 2 = note off; the last
    // event before the frame wins) and the init/trig flag of machine assign (bit 7 of word 40).
    int m_pendingNote = 0;
    bool m_pendingInit = false;
    uint32_t m_pendingFiltTrig = 0;
    uint32_t m_pitch = pitchWord(60);
    uint32_t m_blockCount = 0, m_frame = 0;
    Machine m_machine = Machine::GND;
    uint32_t m_routing = 0, m_outBuses = kRouteOutAB;
    bool m_lpKeyTrack = true, m_hpKeyTrack = true;   // the hardware's kit default (KIT > ASSIGN > KEY: LPF/HPF on)
};

} // namespace mnm::host
