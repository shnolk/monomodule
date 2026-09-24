#include "HostModel.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace mnm::host {

// MNM_LEGACY_SLEW=1: slew the page words on every block as versions <= 0.7.5 did (24x faster than the
// hardware). Debug aid for bisecting a regression against the pre-0.7.6 behaviour; the LFO schedule is unaffected.
static const bool kLegacySlew = std::getenv("MNM_LEGACY_SLEW") != nullptr;

HostModel::HostModel()
{
    setMachine(Machine::GND);
    m_words[PitchMod] = kPitchModRest;
}

void HostModel::setMachine(Machine m)
{
    m_machine = m;
    for (int k = 0; k < 8; ++k) {
        m_raw[8 + k]  = isFxMachine(m) ? kDefaultAmpFx[k] : kDefaultAmp[k];
        m_raw[16 + k] = kDefaultFilt[k];
        m_raw[24 + k] = kDefaultEfx[k];
        m_raw[k] = machineDef(m) ? machineDef(m)->defaults[k] : 0;
    }
    forceInit();
}

void HostModel::setParam(Page page, int param, int value)
{
    m_raw[int(page) * 8 + param] = std::clamp(value, 0, 127);
}

void HostModel::setLevel(int v) { m_level = std::clamp(v, 0, 127); }

// Word 37: the kit's routing byte (out buses + input flags), the
// bus-input select, and the key-tracking bits. Versions up to 0.8.0 put the machine index in the low byte
// and never set bits 9/11, i.e. ran with both filters' key tracking off.
uint32_t HostModel::routingWord() const
{
    return m_outBuses | m_routing | (m_lpKeyTrack ? kRouteLpKeyTrack : 0u) | (m_hpKeyTrack ? kRouteHpKeyTrack : 0u);
}

void HostModel::setMasterTuneHz(double hz)
{
    // Master tune is a u16 of about 65536*f/440 (65255 at 400.0 Hz .. 65535 at 440.0 Hz); the DSP adds
    // word 38 straight to the pitch index (2048 units/octave), so the value must reach the
    // DSP sign-extended from 16 bits: 440 Hz → 65535 → -1, 400 Hz → 65255 → -281 = -1.65 semitones.
    const long v = std::clamp(std::lround(65536.0 * hz / 440.0), 0L, 65535L);
    m_tune = uint32_t(int32_t(int16_t(v))) & 0xFFFFFF;
}

void HostModel::setBpm(double bpm) { m_tick = uint32_t(std::clamp(std::lround(24.0 * bpm), 720L, 7200L)); }
void HostModel::setTick(uint32_t tick) { m_tick = std::max(1u, tick); }

// The note state is a single byte: a note-off followed by a note-on inside one frame leaves a note-on (the
// off is moot, the on retrigs), a note-on followed by a note-off leaves a note-off. Versions up to 0.9.0
// OR-ed the two into 0x83, which the kernel read as a note-off, so touching or overlapping DAW notes
// (the previous note's off and the next note's on in the same ~1 ms frame) never sounded.
void HostModel::noteOn(int note)
{
    m_pitch = pitchWord(std::clamp(note, 0, 127));
    m_pendingNote = 1;
    m_pendingFiltTrig = 1;
    m_lfos.trig();
}

void HostModel::noteOff() { m_pendingNote = 2; }
void HostModel::forceInit() { m_pendingInit = true; }

void HostModel::settle()
{
    for (int k = 0; k < 24; ++k) m_words[k] = uint32_t(m_raw[8 + k]) << 16;      // AMP, FILT, EFX
    for (int k = 0; k < 8; ++k)  m_words[Syn0 + k] = uint32_t(m_raw[k]) << 16;   // SYN
    m_words[PitchMod] = kPitchModRest;
    m_words[Level] = uint32_t(m_level) << 16;
    rebuild(false);
}

// Every block word moves a quarter of the way to its target.
void HostModel::smoothing()
{
    auto slew = [](uint32_t& cur, uint32_t target) { cur = (3u * cur + target) >> 2; };
    for (int k = 0; k < 24; ++k) slew(m_words[k], uint32_t(m_raw[8 + k]) << 16);      // AMP, FILT, EFX
    for (int k = 0; k < 8; ++k)  slew(m_words[Syn0 + k], uint32_t(m_raw[k]) << 16);   // SYN
    // word 30 = pitch modulation sum, bipolar around 64<<16 (DSP: pitch += w30*0xB000*2/2^24 - 0x5800,
    // i.e. 352 pitch units per step; 64 cancels the -0x5800 exactly). The LFO PTCH page adds to it.
    slew(m_words[PitchMod], kPitchModRest);
    slew(m_words[Level], uint32_t(m_level) << 16);
    rebuild(m_lfos.trigPending());   // re-apply with the pending LFO trig as it stands
}

void HostModel::rebuild(bool trigPending)
{
    m_block.w = m_words;
    m_lfos.apply(m_block.w.data(), trigPending);
}

// Frame start: trigger/info words from the note state, LFO apply, then the frame-counted LFO housekeeping.
void HostModel::frame()
{
    const uint32_t f = m_frame++;   // the schedule tests the value before incrementing it
    auto& w = m_words;
    for (int k = 0; k < 5; ++k)  w[Extra0 + k] = uint32_t(m_raw[64 + k]) << 16;
    w[W29] = 0;
    w[FiltTrig] = m_pendingFiltTrig;
    w[W33] = 0; w[W34] = 0;
    w[TickRecip] = 0x800000u / m_tick;
    w[MachineIdx] = uint32_t(m_machine);
    w[Mode] = routingWord();
    w[Tune] = m_tune;   // sign-extended 16-bit pitch offset
    w[Peek] = 0;
    // word 40: 0x81 note on, 2 note off, bit 7 also the init/trig flag of a machine assign
    w[NoteTrig] = (m_pendingNote == 1 ? 0x81u : m_pendingNote == 2 ? 0x02u : 0u) | (m_pendingInit ? 0x80u : 0u);
    w[Pitch] = m_pitch;
    w[Tick] = m_tick;
    w[W43] = 0;
    m_pendingNote = 0; m_pendingInit = false; m_pendingFiltTrig = 0;   // consumed
    rebuild(false);
    if ((f & 1) == 0) m_lfos.interlaceAndAccumulate(int32_t(m_tick));
    if ((f & 7) == 1 || kLegacySlew) smoothing();
    if ((f & 7) == 0) m_lfos.step(TrackLfos::kBlocksPerStep, int32_t(m_tick));
    w[FiltTrig] = 0; w[W33] = 0; w[NoteTrig] = 0;   // one-shots: the block built above carries them once
}

const ParamBlock& HostModel::nextBlock()
{
    const uint32_t phase = m_blockCount % kBlocksPerFrame;
    ++m_blockCount;
    if (phase == 0) frame();
    else {
        if (kLegacySlew) smoothing();
        m_block.w[FiltTrig] = 0; m_block.w[W33] = 0; m_block.w[NoteTrig] = 0;   // cleared after the upload
    }
    return m_block;
}

} // namespace mnm::host
