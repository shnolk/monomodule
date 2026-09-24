// Audio preview: the sequencer (pattern -> events), the preview specs and the six-track kit render with routing.
#include "mini_test.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include "preview/KitRenderer.h"
#include "preview/Preview.h"
#include "host/Machines.h"
using namespace mnm;
using namespace mnm::preview;
using namespace mnm::dump;

static const fw::Firmware& firmware() { static fw::Firmware f = fw::loadFirmware(mt::requireOsFile()); return f; }

static Pattern blank(int len)
{
    Pattern p;
    p.patternLength = uint8_t(len);
    std::memset(p.locksRaw, 0, sizeof(p.locksRaw));
    p.finalizeLocks();
    return p;
}

static KitTrack synth(host::Machine m, int outBuses = 1)
{
    KitTrack t;
    t.model = uint8_t(m);
    const auto* def = host::machineDef(m);
    for (int k = 0; k < 8; ++k) {
        t.params[k] = def->defaults[k];
        t.params[8 + k] = host::isFxMachine(m) ? host::kDefaultAmpFx[k] : host::kDefaultAmp[k];
        t.params[16 + k] = host::kDefaultFilt[k];
        t.params[24 + k] = host::kDefaultEfx[k];
    }
    t.level = 100;
    t.type = uint8_t(outBuses);
    return t;
}

TEST_CASE(preview_sequencer_notes_offs_swing_and_locks)
{
    Pattern p = blank(16);
    p.ampTrigs[0] = (1ull << 0) | (1ull << 4) | (1ull << 8);
    p.offTrigs[0] = 1ull << 6;
    p.noteNBR[0][0] = 60; p.noteNBR[0][4] = 62; p.noteNBR[0][8] = 64;
    p.swingPatterns[0] = 1ull << 4;   // step 5 swung
    p.swingAmount = 20;               // 70 %
    p.patternTranspose = 2;
    p.transpose[0] = -1;
    // a lock on SYN A (param 0) at step 1 only
    p.lockPatterns[0] = 1ull << 0;
    std::memset(p.locksRaw, 255, sizeof(p.locksRaw));
    p.locksRaw[0][0] = 99;
    p.finalizeLocks();
    Kit kit; kit.tracks[0] = synth(host::Machine::SAW); kit.tracks[0].params[0] = 10;
    const auto ev = sequencePattern(p, &kit, {120.0, 1});
    const double step = 44100 * 60.0 / 120 / 4;   // 5512.5 frames
    CHECK_EQ(loopFrames(p, 120.0), uint32_t(std::lround(16 * step)));
    // step 1: lock then note-on; step 5: swung note (revert of the lock precedes it); step 7: off; step 9: note; end: off
    CHECK_EQ(ev.size(), size_t(7));
    CHECK(ev[0].kind == Event::Param); CHECK_EQ(ev[0].frame, 0u); CHECK_EQ(int(ev[0].a), 0); CHECK_EQ(int(ev[0].b), 99);
    CHECK(ev[1].kind == Event::NoteOn); CHECK_EQ(ev[1].frame, 0u); CHECK_EQ(int(ev[1].a), 61);   // 60 + 2 - 1
    const auto swung = uint32_t(std::lround(4 * step + (2 * step * 70) / 100 - step));
    CHECK(ev[2].kind == Event::Param); CHECK_EQ(ev[2].frame, swung); CHECK_EQ(int(ev[2].b), 10);   // back to the kit value
    CHECK(ev[3].kind == Event::NoteOn); CHECK_EQ(ev[3].frame, swung); CHECK_EQ(int(ev[3].a), 63);
    CHECK(ev[4].kind == Event::NoteOff); CHECK_EQ(ev[4].frame, uint32_t(std::lround(6 * step)));
    CHECK(ev[5].kind == Event::NoteOn); CHECK_EQ(ev[5].frame, uint32_t(std::lround(8 * step)));
    CHECK(ev[6].kind == Event::NoteOff); CHECK_EQ(ev[6].frame, loopFrames(p, 120.0));
    // two loops: the second pass is offset by one loop and the note at step 1 repeats
    const auto ev2 = sequencePattern(p, &kit, {120.0, 2});
    CHECK_EQ(ev2.size(), size_t(13));
    bool found = false;
    for (const auto& e : ev2) if (e.kind == Event::NoteOn && e.frame == loopFrames(p, 120.0) && e.a == 61) found = true;
    CHECK(found);
    CHECK_EQ(ev2.back().frame, 2 * loopFrames(p, 120.0));
}

TEST_CASE(preview_sequencer_skips_chords_and_midi_page_locks)
{
    Pattern p = blank(4);
    p.ampTrigs[2] = 1ull << 0;
    p.noteNBR[2][0] = 255;   // chord trig
    p.triglessTrigs[2] = 1ull << 2;
    p.lockPatterns[2] = 1ull << 60;   // MIDI page parameter: outside the seven pages
    std::memset(p.locksRaw, 255, sizeof(p.locksRaw));
    p.finalizeLocks();
    const auto ev = sequencePattern(p, nullptr, {120.0, 1});
    CHECK(ev.empty());
    int page = 0, param = 0;
    CHECK(paramTarget(45, page, param)); CHECK_EQ(page, 5); CHECK_EQ(param, 5);   // LFO2 SPD
    CHECK(!paramTarget(56, page, param));
}

TEST_CASE(preview_specs)
{
    Kit kit;
    kit.position = 3;
    kit.tracks[0] = synth(host::Machine::SAW);
    kit.tracks[1] = synth(host::Machine::FM_PAR);
    kit.tracks[2] = synth(host::Machine::REVERB);
    const Pattern demo = demoPattern(kit);
    CHECK_EQ(int(demo.patternLength), 16);
    CHECK_EQ(int(demo.kit), 3);
    CHECK_EQ(demo.noteTrigCount(0), 2); CHECK_EQ(demo.noteTrigCount(1), 2); CHECK_EQ(demo.noteTrigCount(2), 0); CHECK_EQ(demo.noteTrigCount(3), 0);
    CHECK((demo.ampTrigs[0] & 1) != 0); CHECK((demo.ampTrigs[1] & 4) != 0);
    PreviewOptions opt;
    const auto ps = patternPreview(kit, demo, opt);   // 16 steps at 120 = 2 s -> 2 loops for 4 s
    CHECK_EQ(ps.loops, 2);
    CHECK_EQ(ps.frames, 2 * ps.loopFrames + uint32_t(std::lround(1.5 * 44100)));
    CHECK(ps.stems);
    const auto pr = presetPreview(kit.tracks[0], true, false, opt);
    CHECK(!pr.stems);
    CHECK_EQ(int(pr.kit.tracks[0].model), int(host::Machine::SAW));
    CHECK_EQ(int(pr.kit.tracks[0].type), 1);
    CHECK(pr.kit.lpKeyTracks(0)); CHECK(!pr.kit.hpKeyTracks(0));
    CHECK_EQ(pr.events.size(), size_t(2));
    CHECK(pr.events[0].kind == Event::NoteOn); CHECK_EQ(int(pr.events[0].a), 60);
    CHECK(pr.events[1].kind == Event::NoteOff); CHECK_EQ(pr.events[1].frame, uint32_t(std::lround(44100 * 1.0)));
    const auto fx = presetPreview(kit.tracks[2], true, true, opt);   // FX preset: saw on T1 -> NEIBOR -> reverb on T2
    CHECK_EQ(int(fx.kit.tracks[0].model), int(host::Machine::SAW));
    CHECK_EQ(int(fx.kit.tracks[0].type), 0);
    CHECK_EQ(int(fx.kit.tracks[1].model), int(host::Machine::REVERB));
    CHECK_EQ(fx.kit.fxInput(1), 0);
    CHECK_EQ(fx.kit.outBuses(1), 1);
    CHECK_EQ(int(fx.events[0].track), 0);
}

static double peak(const std::vector<float>& v) { double m = 0; for (float x : v) m = std::max(m, double(std::fabs(x))); return m; }

TEST_CASE(preview_render_routing)
{
    // T1 SAW (no OUT BUS) -> NEIBOR -> T2 REVERB (OUT AB); T3 FM+ PAR on OUT CD; T4 GND; T5/T6 empty
    Kit kit;
    kit.tracks[0] = synth(host::Machine::SAW, 0);
    kit.tracks[1] = synth(host::Machine::REVERB, 1); kit.tracks[1].type |= host::kRouteAuxIn;
    kit.tracks[2] = synth(host::Machine::FM_PAR, 2);
    kit.lpKeyTrack = kit.hpKeyTrack = 0x3F;
    Pattern p = blank(8);
    p.ampTrigs[0] = 1; p.noteNBR[0][0] = 60;
    p.ampTrigs[2] = 1; p.noteNBR[2][0] = 48;
    PreviewOptions opt; opt.minSeconds = 0.5; opt.tailSeconds = 0.5;
    const auto spec = patternPreview(kit, p, opt);
    CHECK_EQ(spec.loops, 1);
    KitRenderer r(firmware());
    const auto t0 = std::chrono::steady_clock::now();
    r.loadKit(spec.kit, opt.bpm);
    const auto t1 = std::chrono::steady_clock::now();
    std::vector<float> mix, stem[6];
    CHECK(r.render(spec.events, spec.frames, [&](uint32_t, const RenderBlock& b) {
        for (int i = 0; i < RenderBlock::kFrames; ++i) {
            mix.push_back(b.mixL[size_t(i)]);
            for (int t = 0; t < 6; ++t) stem[t].push_back(b.stemL[size_t(t)][size_t(i)]);
        }
    }));
    const auto t2 = std::chrono::steady_clock::now();
    const double loadMs = std::chrono::duration<double, std::milli>(t1 - t0).count(), renderMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::printf("    [preview] loadKit %.0f ms, render of %.2f s: %.0f ms\n", loadMs, spec.frames / 44100.0, renderMs);
    CHECK(mix.size() >= spec.frames);
    CHECK_MSG(peak(stem[0]) > 0.01, "saw stem peak " << peak(stem[0]));      // its own output, though it has no OUT BUS
    CHECK_MSG(peak(stem[1]) > 0.001, "reverb stem peak " << peak(stem[1]));  // fed from the saw
    CHECK_MSG(peak(stem[2]) > 0.01, "fm stem peak " << peak(stem[2]));
    CHECK_EQ(peak(stem[3]), 0.0); CHECK_EQ(peak(stem[4]), 0.0); CHECK_EQ(peak(stem[5]), 0.0);
    CHECK_MSG(peak(mix) > 0.01, "mix peak " << peak(mix));
    // the mix is AB + CD + EF: the dry saw is not in it (no OUT BUS), the reverb and the FM are
    std::vector<float> sum(mix.size());
    for (size_t i = 0; i < mix.size(); ++i) sum[i] = stem[1][i] + stem[2][i];
    double diff = 0; for (size_t i = 0; i < mix.size(); ++i) diff = std::max(diff, double(std::fabs(sum[i] - mix[i])));
    CHECK_MSG(diff < 1e-6, "mix != reverb + fm, max diff " << diff);
    // a second load renders the same audio again (state is reset between renders)
    r.loadKit(spec.kit, opt.bpm);
    std::vector<float> mix2;
    CHECK(r.render(spec.events, spec.frames, [&](uint32_t, const RenderBlock& b) { for (int i = 0; i < RenderBlock::kFrames; ++i) mix2.push_back(b.mixL[size_t(i)]); }));
    CHECK(mix2 == mix);
    // cancel stops the render
    std::atomic<bool> cancel{true};
    int blocks = 0;
    CHECK(!r.render(spec.events, spec.frames, [&](uint32_t, const RenderBlock&) { ++blocks; }, &cancel));
    CHECK_EQ(blocks, 0);
}
