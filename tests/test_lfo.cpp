// Track LFOs (src/core/host/Lfo.cpp): index mapping, speed, waveforms, trig modes, depth, interlace and
// LFO-to-LFO modulation, checked against values worked out from the arithmetic in the comments.
#include "mini_test.h"
#include "host/HostModel.h"
#include "host/Lfo.h"
using namespace mnm::host;

static constexpr int32_t kTick120 = 2880;   // 24 x 120 BPM

// Runs one full cycle of 8 frames on a fresh TrackLfos: step, then 4 accumulates (frames 2,4,6,8 - the
// accumulate at frame 0 uses the previous delta). Returns the number of steps done.
static void runSteps(TrackLfos& t, int steps, int32_t tick = kTick120)
{
    for (int s = 0; s < steps; ++s) {
        t.interlaceAndAccumulate(tick);   // frame 0 (even): previous delta
        t.step(TrackLfos::kBlocksPerStep, tick);
        for (int f = 1; f < 8; ++f) if ((f & 1) == 0) t.interlaceAndAccumulate(tick);
    }
}

TEST_CASE(lfo_index_mapping)
{
    // raw*N >> 7 on the kit byte; TRIG values seen in real kit dumps: 0 12 37 63 88 114
    CHECK_EQ(TrackLfos::trigModeOf(0 << 16), LfoFree);   CHECK_EQ(TrackLfos::trigModeOf(12 << 16), LfoFree);
    CHECK_EQ(TrackLfos::trigModeOf(37 << 16), LfoTrigged); CHECK_EQ(TrackLfos::trigModeOf(63 << 16), LfoHold);
    CHECK_EQ(TrackLfos::trigModeOf(88 << 16), LfoOne);   CHECK_EQ(TrackLfos::trigModeOf(114 << 16), LfoHalf);
    CHECK_EQ(TrackLfos::pageOf(0 << 16), LfoPagePtch);   CHECK_EQ(TrackLfos::pageOf(14 << 16), LfoPagePtch);
    CHECK_EQ(TrackLfos::pageOf(15 << 16), LfoPageSynt);  CHECK_EQ(TrackLfos::pageOf(120 << 16), LfoPageMidi);
    CHECK_EQ(TrackLfos::destOf(64 << 16), 4);            CHECK_EQ(TrackLfos::destOf(127 << 16), 7);
    CHECK_EQ(TrackLfos::waveOf(121 << 16), 10);          CHECK_EQ(TrackLfos::waveOf(5 << 16), 0);
    CHECK_EQ(TrackLfos::multOf(1 << 16), 0);             CHECK_EQ(TrackLfos::multOf(118 << 16), 6);
    // a modulated shadow: 0x7EFFFF (the clamp) is still index 127-ish
    CHECK_EQ(TrackLfos::waveOf(0x7EFFFF), 10);
    // target words
    CHECK_EQ(TrackLfos::targetWord(LfoPagePtch, 3), 30); CHECK_EQ(TrackLfos::targetWord(LfoPageSynt, 2), 46);
    CHECK_EQ(TrackLfos::targetWord(LfoPageAmp, 5), 5);   CHECK_EQ(TrackLfos::targetWord(LfoPageFilt, 0), 8);
    CHECK_EQ(TrackLfos::targetWord(LfoPageEffx, 7), 23); CHECK_EQ(TrackLfos::targetWord(LfoPageMidi, 0), -1);
}

TEST_CASE(lfo_defaults_and_shadow)
{
    TrackLfos t;
    const int def[8] = {0, 64, 0, 0, 1, 64, 0, 0};
    for (int l = 0; l < 3; ++l)
        for (int k = 0; k < 8; ++k) { CHECK_EQ(t.raw(l, k), def[k]); CHECK_EQ(t.shadow(l, k), def[k] << 16); }
    t.setRaw(1, LfoSpd, 200); CHECK_EQ(t.raw(1, LfoSpd), 127); CHECK_EQ(t.shadow(1, LfoSpd), 127 << 16);
}

TEST_CASE(lfo_speed_one_page_at_spd_64)
{
    // SPD 64, MULT 1x, 120 BPM: inc = ((64<<6)*2880*24 + half)/0xF23FA << 6 = 285 << 6 = 18240 per step,
    // 0x800000/18240 = 459.9 steps x 384 samples = 4.00 s = 2 bars (MULT 2X at SPD 64 spans one 16-step page)
    TrackLfos t;
    t.step(TrackLfos::kBlocksPerStep, kTick120);
    CHECK_EQ(t.state(0).inc, 18240);
    CHECK_EQ(t.state(0).pos, 18240);
    // remainder carried: (64<<6)*2880*24 = 283115520; q = (283115520 + 0 + 0x791FD)/0xF23FA = 285; rem = 283115520 - 285*992250 = 324270
    CHECK_EQ(t.state(0).rem, 283115520 - 285 * 992250);
    // second step: q = (283115520 + 324270 + 496125)/992250 = 285 (283935915/992250 = 286.15 -> 286)
    t.step(TrackLfos::kBlocksPerStep, kTick120);
    CHECK_EQ(t.state(0).inc, 286 << 6);
    // MULT 2x doubles it
    TrackLfos u; u.setRaw(0, LfoMult, 27);
    u.step(TrackLfos::kBlocksPerStep, kTick120);
    CHECK_EQ(u.state(0).inc, 18240 * 2);
    // SPD 0: no movement, remainder stays 0
    TrackLfos z; z.setRaw(0, LfoSpd, 0);
    z.step(TrackLfos::kBlocksPerStep, kTick120);
    CHECK_EQ(z.state(0).inc, 0); CHECK_EQ(z.state(0).pos, 0);
}

TEST_CASE(lfo_waveforms_at_key_positions)
{
    // Drive a FREE LFO with a chosen phase increment so pos lands exactly on the branch boundaries.
    // TRI with SPD 127 MULT 64x: inc = ((127<<6)*2880*24+half)/0xF23FA << 12; whatever it is, we set pos directly.
    auto at = [](int waveRaw, int32_t pos, int32_t out0 = 0) {
        TrackLfos t; t.setRaw(0, LfoWave, waveRaw); t.setRaw(0, LfoPage, 21); t.setRaw(0, LfoDest, 64); t.setRaw(0, LfoDpth, 127);
        t.state(0).pos = pos; t.state(0).out = out0; t.setRaw(0, LfoSpd, 0);
        t.step(TrackLfos::kBlocksPerStep, kTick120);   // inc 0: evaluates at pos unchanged
        return t.state(0).out;
    };
    // TRI (raw 5), ITRI 16, SAW 28, ISAW 39, SQR 51, ISQR 63, EXP 74, IEXP 86, RMP 98, IRMP 109, RND 121
    CHECK_EQ(at(5, 0), 0); CHECK_EQ(at(5, 0x1FFFFF), 0x7FFFFC); CHECK_EQ(at(5, 0x200000), 0x800000);
    CHECK_EQ(at(5, 0x400000), 0); CHECK_EQ(at(5, 0x5FFFFF), -0x7FFFFC); CHECK_EQ(at(5, 0x600000), -0x800000); CHECK_EQ(at(5, 0x7FFFFF), -4);
    CHECK_EQ(at(16, 0x1FFFFF), -0x7FFFFC); CHECK_EQ(at(16, 0x400000), 0); CHECK_EQ(at(16, 0x600000), 0x800000);
    CHECK_EQ(at(28, 0), -0x800000); CHECK_EQ(at(28, 0x400000), 0); CHECK_EQ(at(28, 0x7FFFFF), 0x7FFFFE);
    CHECK_EQ(at(39, 0), 0x800000); CHECK_EQ(at(39, 0x7FFFFF), -0x7FFFFE);
    CHECK_EQ(at(51, 0), 0x800000); CHECK_EQ(at(51, 0x3FFFFF), 0x800000); CHECK_EQ(at(51, 0x400000), 0); CHECK_EQ(at(51, 0x400001), -0x800000);   // pos 0x800000 is only reachable through ONE (tested below)
    CHECK_EQ(at(63, 0), -0x800000); CHECK_EQ(at(63, 0x400001), 0x800000);
    CHECK_EQ(at(98, 0x3FFFFF), 0x7FFFFE); CHECK_EQ(at(98, 0x400000), 0x800000); CHECK_EQ(at(98, 0x400001), 0);
    CHECK_EQ(at(109, 0x3FFFFF), -0x7FFFFE); CHECK_EQ(at(109, 0x400000), -0x800000); CHECK_EQ(at(109, 0x7FFFFF), 0);
    // EXP with inc 0 keeps its value (decay term is 0); the restart sets full scale
    CHECK_EQ(at(74, 0x100000, 0x123456), 0x123456);
    {
        TrackLfos t; t.setRaw(0, LfoWave, 74); t.setRaw(0, LfoTrig, 37); t.setRaw(0, LfoPage, 21); t.setRaw(0, LfoDpth, 127);
        t.trig(); t.step(TrackLfos::kBlocksPerStep, kTick120);   // TRIG mode + trig: pos = 0, restart
        CHECK_EQ(t.state(0).out, 0x800000); CHECK_EQ(t.state(0).pos, 0);
        t.step(TrackLfos::kBlocksPerStep, kTick120);              // decays by (out>>10)*(inc>>5)>>6 with inc 18240
        CHECK_EQ(t.state(0).out, 0x800000 - ((0x800000 >> 10) * (t.state(0).inc >> 5) >> 6));   // inc = 286<<6 here (carried remainder)
        TrackLfos i; i.setRaw(0, LfoWave, 86); i.setRaw(0, LfoTrig, 37); i.setRaw(0, LfoPage, 21);
        i.trig(); i.step(TrackLfos::kBlocksPerStep, kTick120);
        CHECK_EQ(i.state(0).out, -0x800000);
    }
    // RND: seeded generator, first value = ((0x12D3D16 + 0x29A) & 0x7FFFFF) - 0x400000, held across the segment
    {
        TrackLfos t; t.setRaw(0, LfoWave, 121); t.setRaw(0, LfoTrig, 37); t.setRaw(0, LfoPage, 21);
        t.trig(); t.step(TrackLfos::kBlocksPerStep, kTick120);
        const int32_t first = ((0x012D3D16 + 0x29A) & 0x7FFFFF) - 0x400000;
        CHECK_EQ(t.state(0).out, first);
        t.step(TrackLfos::kBlocksPerStep, kTick120);   // same segment (pos 18240 >> 20 == 0): unchanged
        CHECK_EQ(t.state(0).out, first);
        t.state(0).pos = 0x100000 - 18240;             // next step enters segment 1
        t.step(TrackLfos::kBlocksPerStep, kTick120);
        const int32_t second = ((0x012D3D16 + 0x29A + 0x012D3D16) & 0x7FFFFF) - 0x400000;
        CHECK_EQ(t.state(0).out, second);
        CHECK(t.state(0).out >= -0x400000 && t.state(0).out < 0x400000);
    }
}

TEST_CASE(lfo_trig_modes)
{
    auto make = [](int trigRaw) { TrackLfos t; t.setRaw(0, LfoTrig, trigRaw); t.setRaw(0, LfoPage, 21); t.setRaw(0, LfoDpth, 127); return t; };
    // FREE ignores the trig: phase keeps running
    { auto t = make(0); t.step(24, kTick120); t.trig(); t.step(24, kTick120); CHECK_EQ(t.state(0).pos, 2 * 18240 + 64); }
    // TRIG restarts at 0 and runs
    { auto t = make(37); t.step(24, kTick120); t.trig(); t.step(24, kTick120); CHECK_EQ(t.state(0).pos, 0); t.step(24, kTick120); CHECK_EQ(t.state(0).pos, 18240); }   // q = 285 again: the carried remainder went negative on the trig step
    // HOLD: free-running phase, output latched at the trig
    { auto t = make(63); t.setRaw(0, LfoWave, 28);   // SAW
      t.step(24, kTick120); const int32_t pos1 = t.state(0).pos;
      t.trig(); t.step(24, kTick120);
      CHECK_EQ(t.state(0).pos, pos1 + t.state(0).inc);   // not restarted
      CHECK_EQ(t.state(0).hold, t.state(0).out);
      const int32_t held = t.state(0).hold;
      t.step(24, kTick120); CHECK_EQ(t.state(0).hold, held); CHECK(t.state(0).out != held);
      // delta is computed from the held value: target = (hold>>12) * (127<<5)
      const int32_t target = (held >> 12) * (127 << 5);
      CHECK_EQ(t.state(0).delta, (target - t.state(0).acc) >> 2);
    }
    // ONE: stops at 0x800000 after one cycle; HALF stops at 0x400000
    { auto t = make(88); t.setRaw(0, LfoSpd, 127); t.setRaw(0, LfoMult, 118); t.trig();
      for (int i = 0; i < 40; ++i) t.step(24, kTick120);
      CHECK_EQ(t.state(0).pos, 0x800000); CHECK_EQ(t.state(0).out, 0);   // TRI at the full cycle = 0
      t.step(24, kTick120); CHECK_EQ(t.state(0).pos, 0x800000); }
    { auto t = make(114); t.setRaw(0, LfoSpd, 127); t.setRaw(0, LfoMult, 118); t.trig();
      for (int i = 0; i < 40; ++i) t.step(24, kTick120);
      CHECK_EQ(t.state(0).pos, 0x400000); }
    // wrap: FREE LFO wraps modulo 0x800000 and RND/EXP get a restart at the wrap
    { auto t = make(0); t.setRaw(0, LfoWave, 74); t.setRaw(0, LfoSpd, 127); t.setRaw(0, LfoMult, 118);
      t.step(24, kTick120); const int32_t inc = t.state(0).inc; CHECK(inc > 0x100000);
      int wraps = 0; int32_t prev = t.state(0).pos;
      for (int i = 0; i < 20; ++i) { t.step(24, kTick120); if (t.state(0).pos < prev) { ++wraps; CHECK_EQ(t.state(0).out, 0x800000); } prev = t.state(0).pos; }
      CHECK(wraps >= 2); }
}

TEST_CASE(lfo_depth_scaling_and_accumulate)
{
    // Page target: DPTH 127, SQR high = 0x800000 -> target (0x800000>>12)*(127<<5) = 0x7F0000 = 127 steps
    TrackLfos t; t.setRaw(0, LfoPage, 35); t.setRaw(0, LfoDest, 5 * 16 + 8); t.setRaw(0, LfoWave, 51); t.setRaw(0, LfoDpth, 127); t.setRaw(0, LfoSpd, 0);
    t.step(24, kTick120);
    CHECK_EQ(t.state(0).delta, 0x7F0000 >> 2);
    CHECK_EQ(int(t.state(0).page), LfoPageAmp); CHECK_EQ(int(t.state(0).dest), 5);
    for (int i = 0; i < 4; ++i) t.interlaceAndAccumulate(kTick120);
    CHECK_EQ(t.state(0).acc, 0x7F0000);
    // applied to AMP VOL (word 5) on top of the un-modulated word, clamped to 0x7EFFFF
    uint32_t words[52] = {}; words[5] = 64u << 16;
    t.apply(words, false);
    CHECK_EQ(words[5], 0x7EFFFFu);
    // DPTH 64: half the span, added to the un-modulated word
    TrackLfos h; h.setRaw(0, LfoPage, 35); h.setRaw(0, LfoDest, 5 * 16 + 8); h.setRaw(0, LfoWave, 51); h.setRaw(0, LfoDpth, 64); h.setRaw(0, LfoSpd, 0);
    h.step(24, kTick120); for (int i = 0; i < 4; ++i) h.interlaceAndAccumulate(kTick120);
    CHECK_EQ(h.state(0).acc, 0x400000);
    words[5] = 10u << 16; h.apply(words, false); CHECK_EQ(words[5], (10u << 16) + 0x400000u);
    // negative: ISQR at pos 0 = -0x800000 -> -0x7F0000, clamps at 0
    TrackLfos n; n.setRaw(0, LfoPage, 35); n.setRaw(0, LfoDest, 5 * 16 + 8); n.setRaw(0, LfoWave, 63); n.setRaw(0, LfoDpth, 127); n.setRaw(0, LfoSpd, 0);
    n.step(24, kTick120); for (int i = 0; i < 4; ++i) n.interlaceAndAccumulate(kTick120);
    CHECK_EQ(n.state(0).acc, -0x7F0000);
    words[5] = 64u << 16; n.apply(words, false); CHECK_EQ(words[5], 0u);
    words[5] = 127u << 16; n.apply(words, false); CHECK_EQ(words[5], (127u << 16) - 0x7F0000u);
    // PTCH page: DPTH 127, DEST 2OCT (range 48), TRI peak 0x800000 -> ((127*0x80)>>2)*48 = 195072
    TrackLfos p; p.setRaw(0, LfoPage, 0); p.setRaw(0, LfoDest, 64); p.setRaw(0, LfoWave, 5); p.setRaw(0, LfoDpth, 127); p.setRaw(0, LfoSpd, 0);
    p.state(0).pos = 0x200000;
    p.step(24, kTick120);
    CHECK_EQ(p.state(0).delta, 195072 >> 2);
    for (int i = 0; i < 4; ++i) p.interlaceAndAccumulate(kTick120);
    words[30] = 64u << 16; p.apply(words, false); CHECK_EQ(words[30], (64u << 16) + 195072u);
    // the delta is (target - acc)/4: after settling, a new evaluation at the same position yields delta 0
    p.step(24, kTick120); CHECK_EQ(p.state(0).delta, 0);
}

TEST_CASE(lfo_retrig_start_value_and_pending_trig)
{
    // SAW in TRIG mode on AMP VOL: start value = (-0x800000>>12) * (127<<5) = -0x7F0000
    TrackLfos t; t.setRaw(0, LfoPage, 35); t.setRaw(0, LfoDest, 5 * 16 + 8); t.setRaw(0, LfoWave, 28); t.setRaw(0, LfoTrig, 37); t.setRaw(0, LfoDpth, 127);
    t.trig(); t.step(24, kTick120);
    CHECK_EQ(t.state(0).start, -0x7F0000);
    // the smoothing's apply with the trig still pending resets acc to the start value (DPTH 64: start -0x400000)
    TrackLfos u; u.setRaw(0, LfoPage, 35); u.setRaw(0, LfoDest, 5 * 16 + 8); u.setRaw(0, LfoWave, 28); u.setRaw(0, LfoTrig, 37); u.setRaw(0, LfoDpth, 64);
    u.trig(); u.step(24, kTick120); CHECK_EQ(u.state(0).start, -0x400000);
    u.trig();
    uint32_t words[52] = {}; words[5] = 100u << 16;
    u.apply(words, true);
    CHECK_EQ(u.state(0).acc, -0x400000); CHECK_EQ(words[5], (100u << 16) - 0x400000u);
    // FREE mode ignores the pending trig (acc unchanged)
    TrackLfos f; f.setRaw(0, LfoPage, 35); f.setRaw(0, LfoDest, 5 * 16 + 8); f.setRaw(0, LfoWave, 28); f.setRaw(0, LfoDpth, 127);
    f.step(24, kTick120); f.interlaceAndAccumulate(kTick120);   // opens the interlace gate (closed until the first even frame)
    f.state(0).acc = 12345; f.trig(); words[5] = 0; f.apply(words, true);
    CHECK_EQ(f.state(0).acc, 12345); CHECK_EQ(words[5], 12345u);
}

TEST_CASE(lfo_interlace)
{
    // INTL 0: the gate is forced on every even frame
    TrackLfos t; t.setRaw(0, LfoPage, 35); t.setRaw(0, LfoDpth, 127); t.setRaw(0, LfoWave, 51); t.setRaw(0, LfoSpd, 0);
    CHECK_EQ(t.state(0).toggle, 0);
    t.interlaceAndAccumulate(kTick120); CHECK_EQ(t.state(0).toggle, 1);
    // INTL 64: countdown -= (64<<10)*tick per even frame from 0: first call goes negative, second reloads and toggles
    t.setRaw(0, LfoIntl, 64);
    t.state(0).countdown = 0; t.state(0).toggle = 1;
    t.interlaceAndAccumulate(kTick120); CHECK_EQ(t.state(0).countdown, -(64 << 10) * kTick120); CHECK_EQ(t.state(0).toggle, 1);
    t.interlaceAndAccumulate(kTick120); CHECK_EQ(t.state(0).countdown, -(64 << 10) * kTick120 + 0x78F00000); CHECK_EQ(t.state(0).toggle, 0);
    // gated off: apply adds nothing
    t.step(24, kTick120); for (int i = 0; i < 4; ++i) t.interlaceAndAccumulate(kTick120);
    CHECK(t.state(0).acc != 0);
    uint32_t words[52] = {}; words[TrackLfos::targetWord(LfoPageAmp, 4)] = 64u << 16;
    const bool gated = t.state(0).toggle == 0;
    t.apply(words, false);
    if (gated) CHECK_EQ(words[4], 64u << 16); else CHECK(words[4] != (64u << 16));
    // period: 0x78F00000 / ((64<<10)*2880) = 10.75 even frames per half cycle
    int toggles = 0; TrackLfos p; p.setRaw(0, LfoIntl, 64); int32_t last = p.state(0).toggle;
    for (int i = 0; i < 100; ++i) { p.interlaceAndAccumulate(kTick120); if (p.state(0).toggle != last) { ++toggles; last = p.state(0).toggle; } }
    CHECK_MSG(toggles >= 8 && toggles <= 10, "toggles=" << toggles);
    // an LFO trig restarts the interlace timer with the gate open
    p.trig(); p.step(24, kTick120); CHECK_EQ(p.state(0).countdown, 0x791D0000); CHECK_EQ(p.state(0).toggle, 1);
}

TEST_CASE(lfo_to_lfo_modulation)
{
    // LFO1 -> LFO2 SPD (page LFO2 = raw 92, DEST 5 = raw 5*16+8), SQR high, DPTH 127: LFO2's shadow SPD = 64<<16 + 0x7F0000 (clamped to 0x7EFFFF)
    TrackLfos t; t.setRaw(0, LfoPage, 92); t.setRaw(0, LfoDest, 5 * 16 + 8); t.setRaw(0, LfoWave, 51); t.setRaw(0, LfoDpth, 127); t.setRaw(0, LfoSpd, 0);
    t.setRaw(1, LfoPage, 35); t.setRaw(1, LfoDest, 5 * 16 + 8);
    t.step(24, kTick120); for (int i = 0; i < 4; ++i) t.interlaceAndAccumulate(kTick120);
    uint32_t words[52] = {};
    t.apply(words, false);
    CHECK_EQ(t.shadow(1, LfoSpd), 0x7EFFFF);
    CHECK_EQ(t.shadow(0, LfoSpd), 0);   // untouched
    // the next step of LFO2 runs at the modulated speed: (0x7EFFFF>>10)*2880*24 ... > the unmodulated 18240
    t.step(24, kTick120);
    CHECK(t.state(1).inc > 18240);
    // and the shadow is rebuilt from raw before every apply (no compounding)
    t.apply(words, false); CHECK_EQ(t.shadow(1, LfoSpd), 0x7EFFFF);
}

TEST_CASE(hostmodel_lfo_schedule)
{
    // LFO1 SQR on AMP VOL, SPD 0 (static high): after settling, word 5 = 64<<16 + 0x7F0000 clamped = 0x7EFFFF
    HostModel h; h.setMachine(Machine::SAW); h.setBpm(120.0); h.settle();
    h.setLfoParam(0, LfoPage, 35); h.setLfoParam(0, LfoDest, 5 * 16 + 8); h.setLfoParam(0, LfoWave, 51); h.setLfoParam(0, LfoDpth, 127); h.setLfoParam(0, LfoSpd, 0);
    CHECK_EQ(h.nextBlock().w[AmpVol], 64u << 16);   // frame 0: step computes the delta, nothing applied yet
    uint32_t last = 0;
    for (int b = 0; b < 3 * 9; ++b) last = h.nextBlock().w[AmpVol];
    CHECK_EQ(last, 0x7EFFFFu);
    // the un-modulated words are untouched: LFO off -> back to 64<<16 within a frame
    h.setLfoParam(0, LfoDpth, 0);   // next step at frame 16 computes delta = -acc/4; acc is 0 after frame 24
    for (int b = 0; b < 3 * 20; ++b) last = h.nextBlock().w[AmpVol];
    CHECK_EQ(last, 64u << 16);
    // words 42/35 carry the tick: 24 x 120 = 2880, 0x800000/2880 = 2912
    CHECK_EQ(h.current().w[Tick], 2880u); CHECK_EQ(h.current().w[TickRecip], 2912u);
}
