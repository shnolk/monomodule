#include "Lfo.h"

namespace mnm::host {

namespace {

// 32-bit integer arithmetic: results wrap, shifts are arithmetic.
inline int32_t i32(int64_t v) { return int32_t(uint32_t(uint64_t(v))); }
inline int32_t mul32(int32_t a, int32_t b) { return i32(int64_t(a) * int64_t(b)); }
inline int32_t hi16(int32_t v) { return int32_t(int16_t(v >> 16)); }                 // sign-extended high word
inline int32_t clampTarget(int64_t v) { return v < 0 ? 0 : v > 0x7EFFFF ? 0x7EFFFF : int32_t(v); }

// PTCH-page range multipliers per DEST (1/12 2/12 7/12 1OCT 2OCT 4OCT 8OCT 16OC), 2 units per semitone
constexpr int32_t kPtchRange[8] = {2, 4, 14, 24, 48, 96, 192, 384};
// Waveform start value per WAVE index (the PTCH path uses its high word)
constexpr int32_t kStartValue[16] = {0, 0, -0x800000, 0x800000, 0x800000, -0x800000, 0x800000, -0x800000, 0, 0, 0, 0, 0, 0, 0, 0};

// ---- waveform generators (11 waves; the unused indices play SAW) ----
// pos is 0..0x7FFFFF, the outputs span -0x800000..0x800000. Note the zero at the exact half/full-cycle
// positions that ONE/HALF stop on.
using WaveFn = void (*)(LfoState&, int32_t restart, int32_t inc);

void wTri(LfoState& s, int32_t, int32_t)
{
    const int32_t p = s.pos;
    if (p <= 0x1FFFFF) s.out = i32(int64_t(p) << 2);
    else if (p > 0x5FFFFF) s.out = i32((int64_t(p) << 2) - 0x2000000);
    else s.out = i32(0x1000000 - (int64_t(p) << 2));
}
void wITri(LfoState& s, int32_t, int32_t)
{
    const int32_t p = s.pos;
    if (p <= 0x1FFFFF) s.out = i32(-(int64_t(p) << 2));
    else if (p > 0x5FFFFF) s.out = i32(0x2000000 - (int64_t(p) << 2));
    else s.out = i32((int64_t(p) << 2) - 0x1000000);
}
void wSaw(LfoState& s, int32_t, int32_t)   { s.out = i32((int64_t(s.pos) << 1) - 0x800000); }
void wISaw(LfoState& s, int32_t, int32_t)  { s.out = i32(0x800000 - (int64_t(s.pos) << 1)); }
void wSqr(LfoState& s, int32_t, int32_t)
{
    const int32_t p = s.pos;
    if (p == 0x800000 || p == 0x400000) s.out = 0;
    else s.out = p <= 0x3FFFFF ? 0x800000 : -0x800000;
}
void wISqr(LfoState& s, int32_t, int32_t)
{
    const int32_t p = s.pos;
    if (p == 0x800000 || p == 0x400000) s.out = 0;
    else s.out = p <= 0x3FFFFF ? -0x800000 : 0x800000;
}
// EXP: restart to full scale, then decay by (out>>10)*(inc>>5)>>6 per step
void expStep(LfoState& s, int32_t inc)
{
    const int32_t d = mul32(s.out >> 10, inc >> 5) >> 6;
    s.out = i32(int64_t(s.out) - d);
}
void wExp(LfoState& s, int32_t restart, int32_t inc)  { if (restart) s.out = 0x800000; else expStep(s, inc); }
void wIExp(LfoState& s, int32_t restart, int32_t inc) { if (restart) s.out = -0x800000; else expStep(s, inc); }
void wRmp(LfoState& s, int32_t, int32_t)
{
    const int32_t p = s.pos;
    if (p <= 0x3FFFFF) s.out = i32(int64_t(p) << 1);
    else s.out = p == 0x400000 ? 0x800000 : 0;
}
void wIRmp(LfoState& s, int32_t, int32_t)
{
    const int32_t p = s.pos;
    if (p <= 0x3FFFFF) s.out = i32(-(int64_t(p) << 1));
    else s.out = p == 0x400000 ? -0x800000 : 0;
}
// RND: 8 steps per cycle; a new value (Fibonacci generator, low 23 bits, centred: -0x400000..0x3FFFFF)
// when the step changes or on a restart
void wRnd(LfoState& s, int32_t restart, int32_t)
{
    const int32_t p = s.pos;
    if (p == 0x800000 || p == 0x400000) { s.out = 0; return; }
    const int8_t seg = int8_t(p >> 20);
    if (!restart && seg == s.rndSeg) return;
    s.rndSeg = seg;
    const int32_t sum = i32(int64_t(s.rndA) + s.rndB);
    s.out = (sum & 0x7FFFFF) - 0x400000;
    s.rndB = s.rndA;
    s.rndA = sum;
}

constexpr WaveFn kWave[16] = {wTri, wITri, wSaw, wISaw, wSqr, wISqr, wExp, wIExp, wRmp, wIRmp, wRnd, wSaw, wSaw, wSaw, wSaw, wSaw};

} // namespace

TrackLfos::TrackLfos()
{
    static constexpr uint8_t kDefaults[8] = {0, 64, 0, 0, 1, 64, 0, 0};   // PTCH, 2OCT, FREE, TRI, 1X, 64, 0, 0
    for (int l = 0; l < kNumLfos; ++l)
        for (int k = 0; k < 8; ++k) setRaw(l, k, kDefaults[k]);
}

void TrackLfos::setRaw(int lfo, int k, int raw)
{
    raw = raw < 0 ? 0 : raw > 127 ? 127 : raw;
    m_raw[size_t(lfo)][size_t(k)] = uint8_t(raw);
    m_shadow[size_t(lfo)][size_t(k)] = int32_t(raw) << 16;
}

int TrackLfos::targetWord(int page, int dest)
{
    switch (page) {
    case LfoPagePtch: return 30;
    case LfoPageSynt: return 44 + dest;
    case LfoPageAmp:  return dest;
    case LfoPageFilt: return 8 + dest;
    case LfoPageEffx: return 16 + dest;
    default: return -1;   // LFO1-3 shadow (handled in apply) and MIDI (no DSP effect)
    }
}

// Advances the phase of the track's three LFOs. elapsed = DSP blocks since the last step.
void TrackLfos::step(int32_t elapsed, int32_t tick)
{
    const int32_t trackTrig = m_trig;   // the pending LFO trig, consumed here
    m_trig = 0;
    for (int l = 0; l < kNumLfos; ++l) {
        auto& s = m_s[size_t(l)];
        const auto& sh = m_shadow[size_t(l)];
        const int mode = trigModeOf(sh[LfoTrig]);
        int32_t restart = mode == LfoHold ? 0 : trackTrig;
        // speed: ((SPD<<6) * tick * elapsed + rem + half) / divisor, remainder carried, then << (6 + MULT)
        int32_t d2 = mul32(mul32(sh[LfoSpd] >> 10, tick), elapsed);
        const int32_t rem = s.rem;
        int32_t q = i32(int64_t(rem) + d2 + kDivisorHalf);
        q = q / kDivisor;                                   // quotient truncated toward zero
        s.rem = i32(int64_t(d2) + rem - int64_t(q) * kDivisor);
        const int32_t inc = i32(int64_t(uint32_t(q)) << (6 + multOf(sh[LfoMult])));
        s.inc = inc;
        if (restart != 0 && mode > LfoFree) {
            s.pos = 0;
            evaluate(l, 1, inc);
        } else {
            int32_t p = i32(int64_t(s.pos) + inc);
            s.pos = p;
            restart = 0;
            if (mode == LfoHalf && p > 0x400000) s.pos = 0x400000;
            else if (p > 0x7FFFFF) {
                if (mode == LfoOne) s.pos = 0x800000;
                else { s.pos = i32(int64_t(p) - 0x800000); restart = 1; }
            }
            evaluate(l, restart, inc);
        }
        if (trackTrig) { s.hold = s.out; s.countdown = 0x791D0000; s.toggle = 1; }
    }
}

// Waveform -> target value (scaled by DPTH and, on the PTCH page, the DEST range), delta
// towards it (reached in four accumulates), and the start value for retrigs.
void TrackLfos::evaluate(int lfo, int32_t restart, int32_t inc)
{
    auto& s = m_s[size_t(lfo)];
    const auto& sh = m_shadow[size_t(lfo)];
    const int wave = waveOf(sh[LfoWave]);
    const int mode = trigModeOf(sh[LfoTrig]);
    kWave[wave & 0xF](s, restart, inc);
    const int32_t src = mode == LfoHold ? s.hold : s.out;
    const int32_t startValue = kStartValue[wave & 0xF];
    const bool retrig = mode != LfoFree && mode != LfoHold;
    if (pageOf(sh[LfoPage]) == LfoPagePtch) {
        const int32_t range = kPtchRange[destOf(sh[LfoDest]) & 7];
        const int32_t dpth = hi16(sh[LfoDpth]);
        int32_t v = i32(int64_t(dpth) * hi16(src)) >> 2;
        v = mul32(v, range);
        s.delta = i32(int64_t(v) - s.acc) >> 2;
        if (retrig) s.start = mul32(mul32(hi16(startValue), dpth) >> 2, range);
    } else {
        const int32_t dpth = sh[LfoDpth] >> 11;   // raw << 5
        const int32_t v = mul32(src >> 12, dpth);
        s.delta = i32(int64_t(v) - s.acc) >> 2;
        if (retrig) s.start = mul32(startValue >> 12, dpth);
    }
    // the target is resolved now and kept until the next evaluation
    s.page = int8_t(pageOf(sh[LfoPage]));
    s.dest = int8_t(destOf(sh[LfoDest]) & 7);
}

// Even frames: interlace timers, then acc += delta for every LFO.
void TrackLfos::interlaceAndAccumulate(int32_t tick)
{
    for (int l = 0; l < kNumLfos; ++l) {
        auto& s = m_s[size_t(l)];
        const int32_t intl = m_shadow[size_t(l)][LfoIntl];
        if (intl == 0) { s.toggle = 1; continue; }
        if (s.countdown < 0) {
            s.countdown = i32(int64_t(s.countdown) + 0x78F00000);
            s.toggle = s.toggle == 0 ? 1 : 0;
        } else {
            s.countdown = i32(int64_t(s.countdown) - mul32(intl >> 6, tick));
        }
    }
    for (auto& s : m_s) s.acc = i32(int64_t(s.acc) + s.delta);
}

// For each LFO whose interlace gate is set, add its value to the target and
// clamp to 0..0x7EFFFF. With the trig flag pending, TRIG/ONE/HALF LFOs restart from their start value.
void TrackLfos::apply(uint32_t* words, bool trigPending)
{
    for (int l = 0; l < kNumLfos; ++l)
        for (int k = 0; k < 8; ++k) m_shadow[size_t(l)][size_t(k)] = int32_t(m_raw[size_t(l)][size_t(k)]) << 16;
    for (int l = 0; l < kNumLfos; ++l) {
        auto& s = m_s[size_t(l)];
        if (s.toggle == 0) continue;
        const auto& sh = m_shadow[size_t(l)];
        int32_t value = s.acc;
        if (trigPending) {
            const int mode = trigModeOf(sh[LfoTrig]);
            if (mode != LfoFree && mode != LfoHold) { s.acc = s.start; value = s.start; }
        }
        if (value == 0) continue;   // 0 is not added (and not clamped)
        const int page = s.page, dest = s.dest;
        if (page >= LfoPageLfo1 && page <= LfoPageLfo3) {
            auto& t = m_shadow[size_t(page - LfoPageLfo1)][size_t(dest)];
            t = clampTarget(int64_t(t) + value);
        } else if (const int w = targetWord(page, dest); w >= 0) {
            words[w] = uint32_t(clampTarget(int64_t(int32_t(words[w])) + value));
        }
    }
}

} // namespace mnm::host
