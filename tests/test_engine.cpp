#include "mini_test.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include "MonoVoice.h"
using namespace mnm;

static const fw::Firmware& firmware() { static fw::Firmware f = fw::loadFirmware(mt::requireOsFile()); return f; }

TEST_CASE(engine_boot_and_init)
{
    dsp::DspEngine e(firmware());
    // sine table built at Y:$14A000 (8192 entries): [0] == 0, [2048] == peak
    CHECK_EQ(e.peek(fw::Space::Y, 0x14A000), 0u);
    const uint32_t peak = e.peek(fw::Space::Y, 0x14A000 + 2048);
    CHECK_MSG(peak >= 0x7FFF00 && peak <= 0x7FFFFF, "peak=" << std::hex << peak);
    // aliasing: same word visible as X
    CHECK_EQ(e.peek(fw::Space::X, 0x14A000 + 2048), peak);
    // track struct init ($145CCE): ext buffer pointer, x:(base+$D0) = $2000, x:(base+$CF) = 1
    CHECK_EQ(e.peek(fw::Space::Y, 0x5D1), 0x120000u);
    CHECK_EQ(e.peek(fw::Space::X, 0x5D0), 0x2000u);
    CHECK_EQ(e.peek(fw::Space::X, 0x5CF), 1u);
    CHECK_EQ(e.peek(fw::Space::Y, 0x6D1), 0x128000u);
    CHECK_EQ(e.peek(fw::Space::X, 0x1001B8), 0x145EDBu);
}

TEST_CASE(engine_silent_block)
{
    dsp::DspEngine e(firmware());
    uint32_t blk[52] = {};
    std::array<int32_t, 32> out;
    CHECK(e.renderBlock(blk, out));
    for (auto v : out) CHECK_EQ(v, 0);
    CHECK_MSG(e.stats().instructions < 200000, "instr=" << e.stats().instructions);
}

static double rmsOf(const std::vector<float>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += double(v[i]) * v[i]; return std::sqrt(s / double(b - a)); }

TEST_CASE(engine_sine_table_exact)
{
    dsp::DspEngine e(firmware());
    double maxErr = 0;
    for (int i = 0; i < 8192; ++i) {
        const int32_t s = int32_t(e.peek(fw::Space::Y, 0x14A000 + i) << 8) >> 8;
        maxErr = std::max(maxErr, std::fabs(s - std::sin(2 * M_PI * i / 8192) * 8388607.0));
    }
    CHECK_MSG(maxErr < 512, "sine table max error " << maxErr << " LSB");
}

TEST_CASE(engine_fmpar_pure_carrier_is_sine)
{
    // All modulator ENVs at 0 → operator gains 0 → carrier only: expect a clean 440 Hz sine for A4.
    MonoVoice v(firmware());
    auto& h = v.host();
    h.setMachine(host::Machine::FM_PAR);
    for (int k : {1, 3, 5}) h.setParam(host::Page::SYN, k, 0);
    h.setLevel(100);
    v.warmUp(8);
    const int N = 16384;
    std::vector<float> L(N), R(N);
    h.noteOn(69);
    v.process(L.data(), R.data(), N);
    // Goertzel at 440 Hz vs 880/1320 Hz on the steady part
    auto goertzel = [&](double f) { double s0 = 0, s1 = 0, s2 = 0; const double c = 2 * std::cos(2 * M_PI * f / 44100); for (int i = 8192; i < N; ++i) { s0 = L[i] + c * s1 - s2; s2 = s1; s1 = s0; } return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2); };
    const double f1 = goertzel(440), f2 = goertzel(880), f3 = goertzel(1320);
    CHECK_MSG(f1 > 1.0, "fundamental magnitude " << f1);
    CHECK_MSG(f2 < f1 * 0.01 && f3 < f1 * 0.01, "harmonics: " << f2 / f1 << " " << f3 / f1);
    // period 100.2 samples
    double best = -1; int bestLag = 0;
    for (int lag = 60; lag < 140; ++lag) { double c = 0; for (int i = 8192; i < 12288; ++i) c += double(L[i]) * L[i + lag]; if (c > best) { best = c; bestLag = lag; } }
    CHECK_MSG(bestLag >= 99 && bestLag <= 101, "period=" << bestLag);
}

TEST_CASE(engine_fmpar_note_renders_and_decays)
{
    MonoVoice v(firmware());
    auto& h = v.host();
    h.setMachine(host::Machine::FM_PAR);
    h.setLevel(100);
    v.warmUp(8);
    const int N = 44100;
    std::vector<float> L(N), R(N);
    h.noteOn(69);
    v.process(L.data(), R.data(), N / 2);
    h.noteOff();
    v.process(L.data() + N / 2, R.data() + N / 2, N / 2);
    CHECK(!v.engine().faulted());
    const double rmsOn = rmsOf(L, 2048, 8192), rmsTail = rmsOf(L, N - 4096, N);
    CHECK_MSG(rmsOn > 0.001, "rmsOn=" << rmsOn);
    CHECK_MSG(rmsTail < rmsOn * 0.05, "rmsOn=" << rmsOn << " rmsTail=" << rmsTail);
    // L and R carry the same signal up to the pan/level gains (exact law to be confirmed on hardware)
    const double rmsR = rmsOf(R, 2048, 8192);
    CHECK_MSG(rmsR > 0.001 && rmsR < rmsOn * 1.5 && rmsR > rmsOn * 0.5, "rmsOn=" << rmsOn << " rmsR=" << rmsR);
    // determinism
    MonoVoice v2(firmware()); v2.host().setMachine(host::Machine::FM_PAR); v2.host().setLevel(100); v2.warmUp(8);
    std::vector<float> L2(N), R2(N); v2.host().noteOn(69); v2.process(L2.data(), R2.data(), N / 2); v2.host().noteOff(); v2.process(L2.data() + N / 2, R2.data() + N / 2, N / 2);
    CHECK(L == L2);
    CHECK_MSG(v.engine().stats().totalInstructions / v.engine().stats().blocks < 100000, "instr/block=" << v.engine().stats().totalInstructions / v.engine().stats().blocks);
}

static double renderNoteRms(MonoVoice& v, int note, std::vector<float>& L, std::vector<float>& R)
{
    const int N = 66150;   // 0.5 s note + 1.0 s release
    L.assign(N, 0.f); R.assign(N, 0.f);
    v.host().noteOn(note);
    v.process(L.data(), R.data(), 22050);
    v.host().noteOff();
    v.process(L.data() + 22050, R.data() + 22050, N - 22050);
    return rmsOf(L, 2048, 8192);
}

TEST_CASE(engine_fm_stat_and_dyn_render_and_machine_switch)
{
    MonoVoice v(firmware());
    auto& h = v.host();
    h.setLevel(100);
    std::vector<float> L, R;
    double rms[3];
    int i = 0;
    for (auto m : {host::Machine::FM_STAT, host::Machine::FM_DYN, host::Machine::FM_PAR}) {
        h.setMachine(m);            // switch with init bit, like the plugin's dropdown
        v.warmUp(8);
        rms[i] = renderNoteRms(v, 60, L, R);
        CHECK(!v.engine().faulted());
        CHECK_MSG(rms[i] > 0.001, "machine " << int(m) << " rms=" << rms[i]);
        const double tail = rmsOf(L, 66150 - 2048, 66150);
        CHECK_MSG(tail < rms[i] * 0.05, "machine " << int(m) << " does not decay: " << tail << " vs " << rms[i]);
        ++i;
    }
    // the three machines must sound different from each other (not the same buffer)
    MonoVoice a(firmware()), b(firmware());
    a.host().setMachine(host::Machine::FM_STAT); a.host().setLevel(100); a.warmUp(8);
    b.host().setMachine(host::Machine::FM_DYN);  b.host().setLevel(100); b.warmUp(8);
    std::vector<float> La, Ra, Lb, Rb;
    renderNoteRms(a, 60, La, Ra); renderNoteRms(b, 60, Lb, Rb);
    CHECK(La != Lb);
}

TEST_CASE(engine_thru_machine_passes_input)
{
    // THRU with external stereo routing: both channels pass with 23 samples latency (16 block + 7 in the
    // kernel chain), inverted polarity, no crosstalk. Noise input (a sine would alias the lag estimate).
    MonoVoice v(firmware());
    auto& h = v.host();
    h.setMachine(host::Machine::THRU);
    h.setRouting(host::kRouteExternalStereoIn);
    h.setParam(host::Page::SYN, 7, 64);    // INP (gain = INP²·8: 127 would clip a 0.25 input)
    h.setLevel(127);
    v.warmUp(8);
    h.noteOn(60);   // opens the amp envelope (DEC/REL 127 keep it open)
    const int N = 16384;
    std::vector<float> inL(N), inR(N), outL(N), outR(N);
    unsigned seed = 1;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (float((seed >> 8) & 0xffff) / 32768.f - 1.f) * 0.2f; };
    for (int i = 0; i < N; ++i) { inL[i] = rnd(); inR[i] = rnd(); }
    v.processFx(inL.data(), inR.data(), outL.data(), outR.data(), N);
    CHECK(!v.engine().faulted());
    auto corrAt = [&](const std::vector<float>& in, const std::vector<float>& out, int lag) { double c = 0, a = 0, b = 0; for (int i = 4096; i < N - 256; ++i) { c += in[i] * out[i + lag]; a += in[i] * in[i]; b += out[i + lag] * out[i + lag]; } return c / std::sqrt(a * b); };
    int bestLag = 0; double best = 0;
    for (int lag = 0; lag < 256; ++lag) { const double r = std::fabs(corrAt(inL, outL, lag)); if (r > best) { best = r; bestLag = lag; } }
    CHECK_MSG(bestLag == 23 && best > 0.8, "L corr " << best << " at lag " << bestLag);
    CHECK_MSG(std::fabs(corrAt(inR, outR, 23)) > 0.8, "R corr " << corrAt(inR, outR, 23));
    CHECK_MSG(std::fabs(corrAt(inL, outR, 23)) < 0.1 && std::fabs(corrAt(inR, outL, 23)) < 0.1, "crosstalk");
}

TEST_CASE(engine_reverb_and_chorus_process_input)
{
    for (auto m : {host::Machine::REVERB, host::Machine::CHORUS}) {
        MonoVoice v(firmware());
        auto& h = v.host();
        h.setMachine(m);
        h.setRouting(host::kRouteExternalStereoIn);
        h.setLevel(127);
        v.warmUp(8);
        h.noteOn(60);
        const int N = 44100;
        std::vector<float> inL(N, 0.f), inR(N, 0.f), outL(N), outR(N);
        for (int i = 0; i < 2205; ++i) { inL[i] = 0.5f * std::sin(2 * M_PI * 440.0 * i / 44100.0); inR[i] = inL[i]; }   // 50 ms burst
        v.processFx(inL.data(), inR.data(), outL.data(), outR.data(), N);
        CHECK(!v.engine().faulted());
        const double burst = rmsOf(outL, 256, 2205), after = rmsOf(outL, 4410, 8820), late = rmsOf(outL, 30000, 44100);
        CHECK_MSG(burst > 0.01, "machine " << int(m) << " burst rms " << burst);
        if (m == host::Machine::REVERB) CHECK_MSG(after > burst * 0.02, "reverb tail missing: " << after << " vs " << burst);
        CHECK_MSG(late < burst, "machine " << int(m) << " does not settle: " << late);
    }
}

TEST_CASE(engine_gnd_and_swave_machines_render)
{
    // SIN, NOIS, SAW, PULS, ENS: render a note, expect signal then decay; sanity-check the character.
    for (auto m : {host::Machine::SIN, host::Machine::NOIS, host::Machine::SAW, host::Machine::PULS, host::Machine::ENS}) {
        MonoVoice v(firmware());
        auto& h = v.host();
        h.setMachine(m);
        h.setLevel(100);
        v.warmUp(8);
        std::vector<float> L, R;
        const double rms = renderNoteRms(v, 60, L, R);
        CHECK(!v.engine().faulted());
        CHECK_MSG(rms > 0.001, "machine " << int(m) << " rms=" << rms);
        // every machine, ENS included, must release: ENS "sustaining" was isFxMachine() misclassifying
        // index 14 as an FX machine (AMP DEC/REL loaded as 127)
        {
            const double tail = rmsOf(L, 66150 - 2048, 66150);
            CHECK_MSG(tail < rms * 0.05, "machine " << int(m) << " does not decay: " << tail << " vs " << rms);
        }
        // zero-crossing density separates noise from tonal machines
        int zc = 0;
        for (int i = 4096; i < 12288; ++i) zc += (L[i] >= 0) != (L[i + 1] >= 0);
        if (m == host::Machine::NOIS) CHECK_MSG(zc > 2000, "NOIS zc=" << zc);
        else CHECK_MSG(zc < 2000, "machine " << int(m) << " zc=" << zc);
    }
    // SIN at defaults is a pure sine: same check as the FM+ carrier
    MonoVoice v(firmware());
    v.host().setMachine(host::Machine::SIN);
    v.host().setLevel(100);
    v.warmUp(8);
    const int N = 16384;
    std::vector<float> L(N), R(N);
    v.host().noteOn(69);
    v.process(L.data(), R.data(), N);
    auto goertzel = [&](double f) { double s0 = 0, s1 = 0, s2 = 0; const double c = 2 * std::cos(2 * M_PI * f / 44100); for (int i = 8192; i < N; ++i) { s0 = L[i] + c * s1 - s2; s2 = s1; s1 = s0; } return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2); };
    CHECK_MSG(goertzel(880) < goertzel(440) * 0.02 && goertzel(1320) < goertzel(440) * 0.02, "SIN not pure: " << goertzel(880) / goertzel(440) << " " << goertzel(1320) / goertzel(440));
}

TEST_CASE(engine_remaining_fx_machines_process)
{
    for (auto m : {host::Machine::DYNAMIX, host::Machine::RINGMOD, host::Machine::PHASER, host::Machine::FLANGER}) {
        MonoVoice v(firmware());
        auto& h = v.host();
        h.setMachine(m);
        h.setRouting(host::kRouteExternalStereoIn);
        h.setLevel(127);
        v.warmUp(8);
        h.noteOn(60);
        const int N = 44100;
        std::vector<float> inL(N, 0.f), inR(N, 0.f), outL(N), outR(N);
        for (int i = 0; i < 4410; ++i) { inL[i] = 0.4f * std::sin(2 * M_PI * 440.0 * i / 44100.0); inR[i] = inL[i]; }
        v.processFx(inL.data(), inR.data(), outL.data(), outR.data(), N);
        CHECK(!v.engine().faulted());
        const double burst = rmsOf(outL, 256, 4410), late = rmsOf(outL, 35000, 44100);
        CHECK_MSG(burst > 0.005, "machine " << int(m) << " burst rms " << burst);
        CHECK_MSG(late < burst * 0.5, "machine " << int(m) << " does not settle: " << late << " vs " << burst);
    }
}

// ---- host-side LFOs driving the engine (src/core/host/Lfo.cpp) ----

static void setLfo(host::HostModel& h, int lfo, std::initializer_list<int> raw)
{
    int k = 0;
    for (int v : raw) h.setLfoParam(lfo, k++, v);
}

TEST_CASE(engine_lfo_square_on_volume)
{
    // SAW with the envelope held open; LFO1 SQR -> AMP VOL, SPD 127 MULT 1x, DPTH 127, 120 BPM:
    // inc = ((127<<6)*2880*24 + half)/0xF23FA << 6 = 567<<6 per step, half period = 0x400000/36288 steps
    // x 384 samples = 1.006 s. VOL alternates between 0x7EFFFF and 0.
    MonoVoice v(firmware());
    auto& h = v.host();
    h.setMachine(host::Machine::SAW);
    for (int k = 0; k < 8; ++k) h.setParam(host::Page::AMP, k, host::kDefaultAmpFx[k]);   // DEC/REL 127
    h.setLevel(100); h.setBpm(120.0);
    setLfo(h, 0, {35, 5 * 16 + 8, 0, 51, 1, 127, 0, 127});
    v.warmUp(8);
    const int N = 44100 * 3;
    std::vector<float> L(N), R(N);
    h.noteOn(60);
    v.process(L.data(), R.data(), N);
    CHECK(!v.engine().faulted());
    const int blk = 384;
    std::vector<double> rms;
    for (int i = 0; i + blk <= N; i += blk) rms.push_back(rmsOf(L, size_t(i), size_t(i + blk)));
    const double peak = *std::max_element(rms.begin(), rms.end());
    CHECK_MSG(peak > 0.005, "peak rms " << peak);
    std::vector<int> edges;
    for (size_t i = 1; i < rms.size(); ++i) if ((rms[i] > peak * 0.5) != (rms[i - 1] > peak * 0.5)) edges.push_back(int(i));
    CHECK_MSG(edges.size() >= 2, "edges " << edges.size());
    for (size_t i = 1; i < edges.size(); ++i) {
        const double half = (edges[i] - edges[i - 1]) * blk / 44100.0;
        CHECK_MSG(half > 0.95 && half < 1.06, "half period " << half << " s");
    }
    // the low half is silent (VOL 0), not just quieter
    double lo = 0; int n = 0;
    for (size_t i = size_t(edges[0]) + 4; i < size_t(edges[1]) - 4; ++i) { lo += rms[i]; ++n; }
    CHECK_MSG(n > 0 && lo / n < peak * 0.02, "low half rms " << lo / n << " vs peak " << peak);
}

TEST_CASE(engine_lfo_triangle_on_pitch)
{
    // FM+PAR pure carrier at A4, envelope held; LFO1 PTCH 2OCT TRI DPTH 127 SPD 64 (4 s cycle at 120 BPM):
    // the LFO arithmetic gives +-((127*128)>>2)*48 = +-195072 on word 30 = +-2.98 steps x 352 units
    // = +-0.51 octave, i.e. ~+6.1 semitones at 1 s and ~-6.1 at 3 s (the "2OCT" label is nominal; hardware A/B pending).
    MonoVoice v(firmware());
    auto& h = v.host();
    h.setMachine(host::Machine::FM_PAR);
    for (int k : {1, 3, 5}) h.setParam(host::Page::SYN, k, 0);
    for (int k = 0; k < 8; ++k) h.setParam(host::Page::AMP, k, host::kDefaultAmpFx[k]);
    h.setLevel(100); h.setBpm(120.0);
    setLfo(h, 0, {0, 64, 0, 5, 1, 64, 0, 127});
    v.warmUp(8);
    const int N = 44100 * 4;
    std::vector<float> L(N), R(N);
    h.noteOn(69);
    v.process(L.data(), R.data(), N);
    CHECK(!v.engine().faulted());
    auto freqAt = [&](double t) {
        const int a = int(t * 44100) - 1024, b = a + 2048;
        std::vector<int> z;
        for (int i = a; i < b - 1; ++i) if (L[size_t(i)] < 0 && L[size_t(i + 1)] >= 0) z.push_back(i);
        return z.size() > 2 ? 44100.0 * double(z.size() - 1) / double(z.back() - z.front()) : 0.0;
    };
    auto semis = [](double f) { return 12.0 * std::log2(f / 440.0); };
    const double f0 = freqAt(0.05), f1 = freqAt(1.0), f2 = freqAt(2.0), f3 = freqAt(3.0);
    CHECK_MSG(std::fabs(semis(f0)) < 0.5, "start " << f0 << " Hz");
    CHECK_MSG(semis(f1) > 5.6 && semis(f1) < 6.6, "peak " << f1 << " Hz = " << semis(f1) << " st");
    CHECK_MSG(std::fabs(semis(f2)) < 0.5, "zero crossing " << f2 << " Hz");
    CHECK_MSG(semis(f3) > -6.6 && semis(f3) < -5.6, "trough " << f3 << " Hz = " << semis(f3) << " st");
}

TEST_CASE(engine_lfo_to_lfo_and_determinism)
{
    // LFO2 SQR -> LFO1 SPD makes LFO1 (TRI -> VOL) alternate between slow and fast; the render is deterministic.
    auto render = [](std::vector<float>& L) {
        MonoVoice v(firmware());
        auto& h = v.host();
        h.setMachine(host::Machine::SAW);
        for (int k = 0; k < 8; ++k) h.setParam(host::Page::AMP, k, host::kDefaultAmpFx[k]);
        h.setLevel(100); h.setBpm(120.0);
        setLfo(h, 0, {35, 5 * 16 + 8, 0, 5, 1, 32, 0, 64});
        setLfo(h, 1, {78, 5 * 16 + 8, 0, 51, 1, 127, 0, 127});   // page 78 = LFO1
        v.warmUp(8);
        const int N = 44100 * 3;
        L.assign(size_t(N), 0.f); std::vector<float> R(size_t(N), 0.f);
        h.noteOn(60);
        v.process(L.data(), R.data(), N);
        CHECK(!v.engine().faulted());
        return h.lfos().state(1).acc;
    };
    std::vector<float> a, b;
    const int32_t acc1 = render(a); render(b);
    CHECK(a == b);
    CHECK(acc1 != 0);
    const double rms = rmsOf(a, 0, a.size());
    CHECK_MSG(rms > 0.001, "rms " << rms);
    // LFO1's speed is modulated: its phase increment differs between the two halves of LFO2's square
    MonoVoice v(firmware());
    auto& h = v.host();
    h.setMachine(host::Machine::SAW); h.setBpm(120.0);
    setLfo(h, 0, {35, 5 * 16 + 8, 0, 5, 1, 32, 0, 64});
    setLfo(h, 1, {78, 5 * 16 + 8, 0, 51, 1, 127, 0, 127});   // page 78 = LFO1
    v.warmUp(8);
    std::vector<int32_t> incs;
    for (int b2 = 0; b2 < 24 * 400; ++b2) { h.nextBlock(); if (b2 % 24 == 0) incs.push_back(h.lfos().state(0).inc); }
    const int32_t mn = *std::min_element(incs.begin() + 4, incs.end()), mx = *std::max_element(incs.begin() + 4, incs.end());
    CHECK_MSG(mx > mn * 2, "LFO1 inc range " << mn << ".." << mx);
}

// ---- SID-6581 and VO-6 (engine paths enabled in v0.7.6) ----

TEST_CASE(engine_sid_renders)
{
    // Defaults: TRI wave at C4 -> tonal, ~261.6 Hz, decays (AMP DEC 64)
    MonoVoice v(firmware());
    auto& h = v.host();
    h.setMachine(host::Machine::SID);
    h.setLevel(100);
    v.warmUp(8);
    std::vector<float> L, R;
    const double rms = renderNoteRms(v, 60, L, R);
    CHECK(!v.engine().faulted());
    CHECK_MSG(rms > 0.001, "SID rms=" << rms);
    CHECK_MSG(rmsOf(L, 66150 - 2048, 66150) < rms * 0.05, "SID does not decay");
    int zc = 0;
    for (int i = 4096; i < 12288; ++i) zc += (L[size_t(i)] >= 0) != (L[size_t(i + 1)] >= 0);
    CHECK_MSG(zc > 80 && zc < 120, "SID TRI zc=" << zc << " (expected ~97 for 261.6 Hz)");
    // NOIS wave (raw 110 -> index 4) is noise
    h.setParam(host::Page::SYN, 3, 110);
    const double rmsN = renderNoteRms(v, 60, L, R);
    CHECK_MSG(rmsN > 0.001, "SID NOIS rms=" << rmsN);
    zc = 0;
    for (int i = 4096; i < 12288; ++i) zc += (L[size_t(i)] >= 0) != (L[size_t(i + 1)] >= 0);
    CHECK_MSG(zc > 300, "SID NOIS zc=" << zc);
    // PW is symmetric about 64: 0 and 127 give the same square, 64 a 100 % pulse (near silence)
    auto pulseRms = [&](int pw) {
        MonoVoice p(firmware());
        p.host().setMachine(host::Machine::SID); p.host().setLevel(100);
        p.host().setParam(host::Page::SYN, 0, pw); p.host().setParam(host::Page::SYN, 3, 60);   // PULS
        for (int k = 0; k < 8; ++k) p.host().setParam(host::Page::AMP, k, host::kDefaultAmpFx[k]);
        p.warmUp(8);
        std::vector<float> l, r; renderNoteRms(p, 60, l, r);
        return rmsOf(l, 8820, 17640);
    };
    const double p0 = pulseRms(0), p64 = pulseRms(64), p127 = pulseRms(127);
    CHECK_MSG(p0 > 0.05 && p127 > 0.05 && std::fabs(p0 - p127) < p0 * 0.05, "PW 0/127: " << p0 << " " << p127);
    CHECK_MSG(p64 < p0 * 0.1, "PW 64: " << p64 << " vs " << p0);
}

TEST_CASE(engine_vo6_renders)
{
    // Defaults (no consonant): a vowel at C3, tonal, decays; V-SW OFF with a consonant leaves only the consonant;
    // VOIC 127 whispers (noisy).
    MonoVoice v(firmware());
    auto& h = v.host();
    h.setMachine(host::Machine::VO6);
    h.setLevel(100);
    v.warmUp(8);
    std::vector<float> L, R;
    const double rms = renderNoteRms(v, 48, L, R);
    CHECK(!v.engine().faulted());
    CHECK_MSG(rms > 0.001, "VO-6 rms=" << rms);
    CHECK_MSG(rmsOf(L, 66150 - 2048, 66150) < rms * 0.05, "VO-6 does not decay");
    int zc = 0;
    for (int i = 4096; i < 12288; ++i) zc += (L[size_t(i)] >= 0) != (L[size_t(i + 1)] >= 0);
    CHECK_MSG(zc < 400, "VO-6 vowel zc=" << zc);
    // whisper
    h.setParam(host::Page::SYN, 3, 127);
    const double rmsW = renderNoteRms(v, 48, L, R);
    CHECK_MSG(rmsW > 0.001, "whisper rms=" << rmsW);
    // aperiodic, unlike the vowel: the normalised autocorrelation at the note's period (C3 = 337 samples) collapses
    auto periodicity = [](const std::vector<float>& x) { const int lag = 337; double c = 0, t = 0; for (int i = 4096; i < 12288; ++i) { c += double(x[size_t(i)]) * x[size_t(i + lag)]; t += double(x[size_t(i)]) * x[size_t(i)]; } return c / t; };
    const double perW = periodicity(L);
    std::vector<float> Lv, Rv;
    { MonoVoice p(firmware()); p.host().setMachine(host::Machine::VO6); p.host().setLevel(100); p.warmUp(8); renderNoteRms(p, 48, Lv, Rv); }
    const double perV = periodicity(Lv);
    CHECK_MSG(perV > 0.5 && perW < perV * 0.6, "whisper periodicity=" << perW << " vs vowel " << perV);
    // consonant N (raw 60 -> index 9) with the vowel switched off: only the consonant, much quieter than
    // with V-SW ON, where the vowel follows (fresh voices: a held envelope would carry the previous vowel over)
    auto lateRms = [&](int vsw) {
        MonoVoice p(firmware());
        p.host().setMachine(host::Machine::VO6); p.host().setLevel(100);
        p.host().setParam(host::Page::SYN, 2, vsw); p.host().setParam(host::Page::SYN, 4, 60); p.host().setParam(host::Page::SYN, 6, 127);
        for (int k = 0; k < 8; ++k) p.host().setParam(host::Page::AMP, k, host::kDefaultAmpFx[k]);
        p.warmUp(8);
        std::vector<float> l, r; renderNoteRms(p, 48, l, r);
        return rmsOf(l, 8820, 17640);
    };
    const double late = lateRms(0), lateOn = lateRms(96);
    CHECK_MSG(lateOn > late * 3, "V-SW off " << late << " vs on " << lateOn);
}

// ---- DigiPRO machines (engine paths enabled in v0.7.6) ----

TEST_CASE(engine_dpro_wave_and_bbox)
{
    // WAVE: 32 factory waveforms in the payload; different waves sound different, all at the note pitch
    double rmsWave[3]; int zc[3]; int i = 0;
    std::vector<float> keep;
    for (int w : {5, 20, 24}) {   // sine, sawtooth, noise
        MonoVoice v(firmware());
        auto& h = v.host();
        h.setMachine(host::Machine::WAVE);
        h.setParam(host::Page::SYN, 0, (w * 256 + 128) / 64);   // list raw for wave index w
        for (int k = 0; k < 8; ++k) h.setParam(host::Page::AMP, k, host::kDefaultAmpFx[k]);
        h.setLevel(100);
        v.warmUp(8);
        std::vector<float> L, R;
        rmsWave[i] = renderNoteRms(v, 60, L, R);
        CHECK(!v.engine().faulted());
        CHECK_MSG(rmsWave[i] > 0.001, "WAVE " << w << " rms=" << rmsWave[i]);
        zc[i] = 0;
        for (int s = 4096; s < 12288; ++s) zc[i] += (L[size_t(s)] >= 0) != (L[size_t(s + 1)] >= 0);
        if (i == 0) keep = L;
        ++i;
    }
    CHECK_MSG(zc[0] > 80 && zc[0] < 120, "WAVE sine zc=" << zc[0] << " (261.6 Hz -> ~97)");
    CHECK_MSG(zc[2] > 300, "WAVE noise zc=" << zc[2]);
    CHECK(rmsWave[0] != rmsWave[1]);
    // BBOX: one sample per key, keys differ, RTRG extends the sound
    auto bbox = [&](int note, int rtrg, int rtim, std::vector<float>& L) {
        MonoVoice v(firmware());
        auto& h = v.host();
        h.setMachine(host::Machine::BBOX); h.setLevel(100);
        h.setParam(host::Page::SYN, 4, rtrg); h.setParam(host::Page::SYN, 5, rtim);
        v.warmUp(8);
        std::vector<float> R;
        renderNoteRms(v, note, L, R);
        CHECK(!v.engine().faulted());
        int last = 0;
        for (size_t s = 0; s < L.size(); ++s) if (std::fabs(L[s]) > 0.003f) last = int(s);
        return last;   // last audible sample
    };
    std::vector<float> bd, crash, tom, bdRetrig;
    const int lenBd = bbox(48, 0, 0, bd), lenCrash = bbox(59, 0, 0, crash), lenTom = bbox(50, 0, 0, tom), lenRetrig = bbox(48, 60, 20, bdRetrig);
    CHECK_MSG(lenBd > 1000 && lenCrash > lenBd, "BD " << lenBd << " crash " << lenCrash);
    CHECK(bd != tom);
    CHECK_MSG(lenRetrig > lenBd * 3, "retrig " << lenRetrig << " vs single " << lenBd);
    // the two-octave keymap repeats with "adjusted pitch": C2 and C4 play BD1 too, but not identically
    std::vector<float> low, high;
    const int lenLow = bbox(36, 0, 0, low), lenHigh = bbox(60, 0, 0, high);
    CHECK_MSG(lenLow > 1000 && lenHigh > 1000, "octaves: " << lenLow << " " << lenHigh);
    CHECK(low != bd && high != bd && low != high);
}

TEST_CASE(engine_digibank_slot_format)
{
    // buildSlot lays out the mip levels (1024, 512, ... 4) and band-limits each one; a square keeps its
    // fundamental at every level and has no energy above a level's Nyquist
    std::vector<double> cycle(1024);
    for (int i = 0; i < 1024; ++i) cycle[size_t(i)] = i < 512 ? 1.0 : -1.0;
    std::array<int32_t, dsp::Digibank::kSlotWords> slot;
    dsp::Digibank::buildSlot(cycle, dsp::Digibank::kGain, slot);
    for (int l = 0; l < dsp::Digibank::kLevels; ++l) {
        const int n = dsp::Digibank::kLevelSize[l], off = dsp::Digibank::kLevelOffset[l];
        double sum = 0, fund = 0;
        for (int i = 0; i < n; ++i) { sum += slot[size_t(off + i)]; fund += slot[size_t(off + i)] * std::sin(2 * M_PI * i / n); }
        CHECK_MSG(std::fabs(sum) < 64.0 * n, "level " << l << " has DC " << sum);
        CHECK_MSG(fund > 0.4 * dsp::Digibank::kGain * n / 2, "level " << l << " fundamental " << fund);
        // the crossing structure of a square: level 0 crosses zero twice, so does every level
        int zc = 0;
        for (int i = 0; i < n; ++i) zc += (slot[size_t(off + i)] >= 0) != (slot[size_t(off + (i + 1) % n)] >= 0);
        CHECK_MSG(zc == 2, "level " << l << " zc " << zc);
    }
    for (int i = 2044; i < 2048; ++i) CHECK_EQ(slot[size_t(i)], 0);
    // peak stays inside the DSP's linear range
    int32_t peak = 0;
    for (auto v : slot) peak = std::max(peak, std::abs(v));
    CHECK_MSG(peak < 0x200000, "peak " << peak);
    // the factory waves decode: record 5 is a sine, record 20 a sawtooth
    const auto sine = dsp::Digibank::waveRecord(firmware(), 5);
    CHECK_EQ(sine.size(), size_t(512));
    double err = 0, amp = 0;
    for (size_t i = 0; i < 512; ++i) amp = std::max(amp, std::fabs(sine[i]));
    for (size_t i = 0; i < 512; ++i) err = std::max(err, std::fabs(sine[i] - amp * std::sin(2 * M_PI * 2 * double(i) / 512.0 + 0)));
    CHECK_MSG(amp > 0.5, "sine amplitude " << amp);
    const auto bank = dsp::Digibank::standIn(firmware());
    for (int s = 0; s < 64; ++s) {
        int32_t p = 0; for (auto v : bank->slot[size_t(s)]) p = std::max(p, std::abs(v));
        CHECK_MSG(p > 1000 && p < 0x200000, "slot " << s << " peak " << p);
    }
}

TEST_CASE(engine_ddrw_and_dens_render)
{
    // DDRW with the stand-in bank: slot 32 (SIN) sustains at the note pitch with no decay, at C4 and C7
    // (different mip levels); DENS plays the same slot; both differ from each other.
    auto play = [&](host::Machine m, int slot, int note, std::vector<float>& L) {
        MonoVoice v(firmware());
        auto& h = v.host();
        h.setMachine(m); h.setLevel(100);
        h.setParam(host::Page::SYN, m == host::Machine::DDRW ? 0 : 3, slot * 2);   // WAV1 / WAVE list raw (index = raw >> 1)
        if (m == host::Machine::DDRW) h.setParam(host::Page::SYN, 2, slot * 2);       // WAV2 too: MIX 64 blends both
        for (int k = 0; k < 8; ++k) h.setParam(host::Page::AMP, k, host::kDefaultAmpFx[k]);
        v.warmUp(8);
        const int N = 22050;
        L.assign(size_t(N), 0.f); std::vector<float> R(size_t(N), 0.f);
        h.noteOn(note);
        v.process(L.data(), R.data(), N);
        CHECK(!v.engine().faulted());
        int zc = 0;
        for (int i = 13230; i < 17640; ++i) zc += (L[size_t(i)] >= 0) != (L[size_t(i + 1)] >= 0);
        return zc;   // crossings in 0.1 s
    };
    std::vector<float> a, b, c, d;
    const int zcC4 = play(host::Machine::DDRW, 32, 60, a);
    CHECK_MSG(zcC4 >= 50 && zcC4 <= 54, "DDRW C4 zc " << zcC4);
    CHECK_MSG(rmsOf(a, 17640, 22050) > 0.8 * rmsOf(a, 2205, 4410) && rmsOf(a, 17640, 22050) > 0.01, "DDRW decays: " << rmsOf(a, 2205, 4410) << " -> " << rmsOf(a, 17640, 22050));
    const int zcC7 = play(host::Machine::DDRW, 32, 96, b);
    CHECK_MSG(zcC7 >= 414 && zcC7 <= 422, "DDRW C7 zc " << zcC7);
    const int zcDens = play(host::Machine::DENS, 32, 60, c);
    CHECK_MSG(zcDens >= 50 && zcDens <= 54, "DENS C4 zc " << zcDens);
    CHECK_MSG(rmsOf(c, 17640, 22050) > 0.8 * rmsOf(c, 2205, 4410) && rmsOf(c, 17640, 22050) > 0.002, "DENS decays");
    CHECK(a != c);
    // a factory wave (slot 20, sawtooth) sounds different from the sine
    play(host::Machine::DDRW, 20, 60, d);
    CHECK(a != d && rmsOf(d, 2205, 4410) > 0.005);
}

TEST_CASE(engine_filter_key_tracking_bits)
{
    // Word 37 bit 9 / bit 11 make the low-pass / high-pass follow the note (the kit
    // default). With BASE 0 and WDTH 16 the band sits two octaves under the note up to the note itself when
    // tracking is on, so the harmonic balance (2nd/1st) is the same for any note; with tracking off the
    // band is fixed and the balance changes with the note.
    auto h2h1 = [](bool lp, bool hp, int note) {
        MonoVoice v(firmware());
        auto& h = v.host();
        h.setMachine(host::Machine::SAW);
        h.setParam(host::Page::FILT, 0, 0);    // BASE
        h.setParam(host::Page::FILT, 1, 16);   // WDTH: two octaves
        h.setParam(host::Page::AMP, 2, 127); h.setParam(host::Page::AMP, 3, 127);   // hold the note
        h.setKeyTracking(lp, hp);
        v.warmUp(8);
        h.noteOn(note);
        const int N = 16384;
        std::vector<float> L(N), R(N);
        v.process(L.data(), R.data(), N);
        auto goertzel = [&](double f) { double s0 = 0, s1 = 0, s2 = 0; const double c = 2 * std::cos(2 * M_PI * f / 44100); for (int i = 8192; i < N; ++i) { s0 = L[size_t(i)] + c * s1 - s2; s2 = s1; s1 = s0; } return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2)); };
        const double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        return std::make_pair(goertzel(f0), goertzel(2 * f0));   // fundamental, 2nd harmonic
    };
    const auto onLow = h2h1(true, true, 48), onHigh = h2h1(true, true, 72);
    const auto offLow = h2h1(false, false, 48), offHigh = h2h1(false, false, 72);
    CHECK_MSG(onLow.first > 1e-3 && onHigh.first > 1e-3, "tracking on: the band must pass the fundamental: " << onLow.first << " " << onHigh.first);
    const double balOnLow = onLow.second / onLow.first, balOnHigh = onHigh.second / onHigh.first;
    CHECK_MSG(std::fabs(balOnLow - balOnHigh) < 0.25 * std::max(balOnLow, balOnHigh), "tracking on: balance differs with the note: " << balOnLow << " vs " << balOnHigh);
    // tracking off: the band no longer follows the note, so the fundamental of one of the notes falls out of it
    const double keepLow = offLow.first / onLow.first, keepHigh = offHigh.first / onHigh.first;
    CHECK_MSG(std::min(keepLow, keepHigh) < 0.5, "tracking off should leave a fixed band: fundamental kept " << keepLow << " / " << keepHigh);
    // the default is the hardware's: both on
    MonoVoice v(firmware());
    CHECK_EQ(v.host().routingWord() & (host::kRouteLpKeyTrack | host::kRouteHpKeyTrack), host::kRouteLpKeyTrack | host::kRouteHpKeyTrack);
}

TEST_CASE(engine_note_after_note_off_in_same_frame_sounds)
{
    // A DAW sends the previous note's off and the next note's on at the same sample for touching or overlapping
    // notes; both land in one frame and the new note must still sound (it did not up to 0.9.0).
    MonoVoice v(firmware());
    auto& h = v.host();
    h.setMachine(host::Machine::SAW);
    h.setLevel(100);
    v.warmUp(8);
    const int N = 8192;
    std::vector<float> L(N), R(N);
    h.noteOn(48);
    v.process(L.data(), R.data(), 2048);
    h.noteOff();
    h.noteOn(60);   // same frame as the off
    v.process(L.data(), R.data(), N);
    const double after = rmsOf(L, 1024, size_t(N));
    CHECK_MSG(after > 1e-3, "note after a same-frame note-off is silent: rms " << after);
    // and a note that ends before the frame closes is (correctly) not started
    h.noteOff();
    std::vector<float> tailL(44100), tailR(44100);
    v.process(tailL.data(), tailR.data(), 44100);   // let the release die out
    h.noteOn(60); h.noteOff();
    v.process(L.data(), R.data(), N);
    CHECK_MSG(rmsOf(L, 4096, size_t(N)) < 1e-4, "on+off within a frame should not sound");
}

// SR.SM arithmetic saturation (our patch to the emulator, in the interpreter and both JIT backends): a data-ALU
// result outside the 48-bit range is limited to $00:7FFFFF:FFFFFF / $FF:800000:000000. The chorus LFO relies
// on it. A small program runs under the JIT and the interpreter; both must produce the saturated halves.
#include "dsp56kEmu/assembler.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/peripherals.h"
static std::vector<uint32_t> runSmProgram(bool jit)
{
    dsp::DspEngine e(firmware());
    e.setUseJit(jit);
    dsp56k::Assembler as;
    uint32_t pc = 0x0D00;
    auto emit = [&](const char* text) {
        const auto r = as.assemble(text);
        CHECK_MSG(r.success(), "cannot assemble " << text);
        e.poke(fw::Space::P, pc++, r.word[0]);
        if (r.wordCount > 1) e.poke(fw::Space::P, pc++, r.word[1]);
    };
    emit("move #>$180300,sr");           // SM (SR bit 20) + CE + the interrupt mask, as the kernel runs
    emit("move #>$7fffff,x0");
    emit("move #>$7fffff,y0");
    emit("move #>$7fffff,a");            // A = $00:7FFFFF:000000
    emit("add x0,a");                    // $00:FFFFFE:000000 -> saturates to $00:7FFFFF:FFFFFF
    emit("movep a1,x:<<$ffffc7");
    emit("movep a0,x:<<$ffffc7");
    emit("move #>$800000,b");            // B = $FF:800000:000000
    emit("sub x0,b");                    // $FF:000001:000000 -> $FF:800000:000000
    emit("movep b1,x:<<$ffffc7");
    emit("movep b0,x:<<$ffffc7");
    emit("mpy x0,y0,a");                 // $00:7FFFFE:000002, in range
    emit("mac x0,y0,a");                 // $00:FFFFFC:000004 -> $00:7FFFFF:FFFFFF
    emit("movep a1,x:<<$ffffc7");
    emit("movep a0,x:<<$ffffc7");
    emit("move #>$100000,a");
    emit("add x0,a");                    // $00:8FFFFF:000000 -> saturates
    emit("movep a1,x:<<$ffffc7");
    emit("move #>$080300,sr");           // SM off
    emit("move #>$100000,a");
    emit("add x0,a");                    // same sum, no saturation: $8FFFFF
    emit("movep a1,x:<<$ffffc7");
    emit("move #>$400000,x0");
    emit("mpyi #>$fffc20,x0,a");         // 0.5 x (-$3E0/2^23): a = $FF:FFFE10:000000 (the sign must reach bit 55)
    emit("movep a2,x:<<$ffffc7");
    emit("movep a1,x:<<$ffffc7");
    emit("movep a0,x:<<$ffffc7");
    emit("maci #>$fffc20,x0,a");         // twice that
    emit("movep a2,x:<<$ffffc7");
    emit("movep a1,x:<<$ffffc7");
    emit("move #>$180300,sr");           // SM on, same multiply: in range, unchanged
    emit("mpyi #>$fffc20,x0,a");
    emit("movep a2,x:<<$ffffc7");
    emit("movep a1,x:<<$ffffc7");
    pc = 0x0D00 + 200;
    e.dsp().setPC(0x0D00);
    auto& hi = e.periph().getHI08();
    uint64_t n = 0;
    while (hi.txData().size() < 15 && n++ < 100000) { if (jit) e.dsp().exec(); else e.dsp().execInterpreter(); }
    std::vector<uint32_t> out;
    while (hi.hasTX()) out.push_back(hi.readTX() & 0xFFFFFF);
    return out;
}

TEST_CASE(engine_sm_saturation_jit_matches_interpreter)
{
    const auto interp = runSmProgram(false);
    const auto jit = runSmProgram(true);
    const std::vector<uint32_t> expected = {0x7FFFFF, 0xFFFFFF, 0x800000, 0x000000, 0x7FFFFF, 0xFFFFFF, 0x7FFFFF, 0x8FFFFF,
                                            0xFFFFFF, 0xFFFE10, 0x000000, 0xFFFFFF, 0xFFFC20, 0xFFFFFF, 0xFFFE10};   // a2 reads sign-extended
    auto hex = [](const std::vector<uint32_t>& v) { std::ostringstream o; for (auto x : v) o << std::hex << x << " "; return o.str(); };
    CHECK_MSG(interp == expected, "interpreter: " << hex(interp));
    CHECK_MSG(jit == expected, "jit: " << hex(jit));
}

// Differential run of the JIT against the interpreter on the chorus scenario (MNM_LOCKSTEP=1 to enable; slow):
// both engines receive the same blocks and input; within a block the JIT single-steps (one instruction per
// block) and the interpreter is stepped to the same instruction count before the registers are compared. The
// first mismatch is printed with the instruction that produced it. Debug aid for a JIT backend (the x64 one
// under Rosetta: arch -x86_64 mnm-tests), not a regression test. It found the x64 sign-extension bug of the
// mpyi/maci immediates (a 32-bit mov of the materialised immediate). Known stop: block 35 of the chorus, r4 after
// the LUA at the end of the inner loop at P:$147767 differs between the interpreter and both JIT backends alike
// (a modulo wrap at the loop's end; the register is reloaded three instructions later, the outputs agree).
#include <cstdlib>
TEST_CASE(engine_lockstep_jit_vs_interpreter_chorus)
{
    if (!std::getenv("MNM_LOCKSTEP")) return;
    mt::setEnv("MNM_DSP_INTERP", nullptr);
    dsp::DspEngine ej(firmware());
    ej.setJitMaxInstructionsPerBlock(1);
    ej.reset();
    mt::setEnv("MNM_DSP_INTERP", "1");
    dsp::DspEngine ei(firmware());
    mt::setEnv("MNM_DSP_INTERP", nullptr);
    for (auto* e : {&ej, &ei}) e->setDigibank(dsp::Digibank::standIn(firmware()));
    host::HostModel h;
    h.setMachine(host::Machine::CHORUS);
    h.setRouting(host::kRouteExternalStereoIn);
    h.setLevel(127);
    h.settle();
    const int N = 44100;
    std::vector<float> inL(N, 0.f);
    for (int i = 0; i < 2205; ++i) inL[i] = 0.5f * std::sin(2 * M_PI * 440.0 * i / 44100.0);
    auto toDsp = [](float x) { return int32_t(std::lrint(std::max(-1.0f, std::min(x, 0.99999988f)) * 8388608.0f)); };
    // the plain register file: the JIT writes its cached registers back when exec() returns, and readDebugRegs()
    // must not be used here (it touches the peripherals, which consumes the HI08 data the kernel is waiting for)
    auto regsOf = [](dsp::DspEngine& e) { dsp56k::DspRegs r = e.dsp().regs(); return r; };
    // Registers that differ after the step and that the step wrote in either engine (the interpreter's init
    // leaves stale values in registers the kernel writes before reading; those are not divergences).
    auto diffRegs = [](const dsp56k::DspRegs& a, const dsp56k::DspRegs& b, const dsp56k::DspRegs& a0, const dsp56k::DspRegs& b0) {
        std::ostringstream o;
        auto c = [&](const std::string& n, auto va, auto vb, auto va0, auto vb0) { if (va != vb && (va != va0 || vb != vb0)) o << " " << n << "=" << std::hex << (uint64_t)va << "/" << (uint64_t)vb; };
        c("a", a.a.var, b.a.var, a0.a.var, b0.a.var); c("b", a.b.var, b.b.var, a0.b.var, b0.b.var);
        auto lo = [](const dsp56k::TReg48& r) { return uint32_t(r.var & 0xFFFFFF); };   // x0/x1, y0/y1 are written separately
        auto hi = [](const dsp56k::TReg48& r) { return uint32_t((r.var >> 24) & 0xFFFFFF); };
        c("x0", lo(a.x), lo(b.x), lo(a0.x), lo(b0.x)); c("x1", hi(a.x), hi(b.x), hi(a0.x), hi(b0.x));
        c("y0", lo(a.y), lo(b.y), lo(a0.y), lo(b0.y)); c("y1", hi(a.y), hi(b.y), hi(a0.y), hi(b0.y));
        for (int i = 0; i < 8; ++i) {
            c("r" + std::to_string(i), a.r[i].var, b.r[i].var, a0.r[i].var, b0.r[i].var);
            c("n" + std::to_string(i), a.n[i].var, b.n[i].var, a0.n[i].var, b0.n[i].var);
            c("m" + std::to_string(i), a.m[i].var, b.m[i].var, a0.m[i].var, b0.m[i].var);
        }
        // the CCR byte is left out: the JIT computes condition codes lazily, so its SR is stale between instructions
        c("sr", a.sr.var & ~0xFFu, b.sr.var & ~0xFFu, a0.sr.var & ~0xFFu, b0.sr.var & ~0xFFu); c("pc", a.pc.var, b.pc.var, a0.pc.var, b0.pc.var);
        c("la", a.la.var, b.la.var, a0.la.var, b0.la.var); c("lc", a.lc.var, b.lc.var, a0.lc.var, b0.lc.var); c("sp", a.sp.var, b.sp.var, a0.sp.var, b0.sp.var);
        return o.str();
    };
    bool noteSent = false;
    for (int blk = 0; blk * 16 < N; ++blk) {
        if (blk == 8 && !noteSent) { h.noteOn(60); noteSent = true; }   // as the test: warm-up, then the note
        int32_t in[32];
        for (int i = 0; i < 16; ++i) { const int f = blk * 16 + i; in[2 * i] = in[2 * i + 1] = toDsp(f < N ? inL[size_t(f)] : 0.f); }
        const auto& b = h.nextBlock();
        for (auto* e : {&ej, &ei}) { e->setInputFrames(in); e->sendBlock(b.w.data()); }
        auto& hj = ej.periph().getHI08();
        auto& hi = ei.periph().getHI08();
        uint64_t steps = 0;
        // Both engines step one at a time and keep a short history of (pc, lc, registers); the earliest common
        // position is the comparison point (the engines stop at different points around DO loops, so neither can
        // simply be run to the other's PC). Entry 0 of each history is the last synced state.
        struct St { uint32_t pc, lc; dsp56k::DspRegs r; };
        auto st = [&](dsp::DspEngine& e) { St x; x.r = e.dsp().regs(); x.pc = x.r.pc.var; x.lc = x.r.lc.var; return x; };
        std::vector<St> hist_j{st(ej)}, hist_i{st(ei)};
        while (hj.txData().size() < 32) {
            ej.dsp().exec(); hist_j.push_back(st(ej));
            ei.dsp().execInterpreter(); hist_i.push_back(st(ei));
            // the newest state of either engine against the other's history (the interpreter runs a whole DO loop
            // inside one step, the JIT single-steps through it: the common position can be thousands of steps apart)
            size_t mi = 0, mj = 0;
            for (size_t j = 1; j < hist_i.size() && !mi; ++j)
                if (hist_j.back().pc == hist_i[j].pc && hist_j.back().lc == hist_i[j].lc) { mi = hist_j.size() - 1; mj = j; }
            for (size_t i = 1; i < hist_j.size() && !mi; ++i)
                if (hist_i.back().pc == hist_j[i].pc && hist_i.back().lc == hist_j[i].lc) { mi = i; mj = hist_i.size() - 1; }
            if (!mi) {
                if (hist_j.size() > 20000) {
                    std::string pj, pi;
                    for (size_t i = 0; i < 12 && i < hist_j.size(); ++i) { char b[16]; std::snprintf(b, sizeof b, " $%06x", hist_j[i].pc); pj += b; }
                    for (size_t i = 0; i < 12 && i < hist_i.size(); ++i) { char b[16]; std::snprintf(b, sizeof b, " $%06x", hist_i[i].pc); pi += b; }
                    std::printf("    LOCKSTEP: paths diverged in block %d after step %llu\n      jit:   %s\n      interp:%s\n", blk, (unsigned long long)steps, pj.c_str(), pi.c_str());
                    CHECK(false);
                }
                continue;
            }
            const auto d = diffRegs(hist_j[mi].r, hist_i[mj].r, hist_j[0].r, hist_i[0].r);
            if (!d.empty()) {
                const uint32_t pc = hist_j[mi - 1].pc;   // the JIT's last position before the compared one
                const auto& before = hist_j[mi - 1].r;
                std::string dis;
                ej.dsp().disassembler().disassemble(dis, ej.peek(fw::Space::P, pc), ej.peek(fw::Space::P, pc + 1), before.sr.var, before.omr.var, pc);
                std::string pathStr;
                for (size_t i = 0; i <= mi; ++i) { char b[16]; std::snprintf(b, sizeof b, " $%06x", hist_j[i].pc); pathStr += b; }
                std::printf("    LOCKSTEP mismatch at block %d step %llu, after P:$%06x  %s   (jit path:%s)\n      before: a=%llx b=%llx x=%llx y=%llx sr=%x\n      diff (jit/interp):%s\n",
                            blk, (unsigned long long)steps, pc, dis.c_str(), pathStr.c_str(), (unsigned long long)before.a.var, (unsigned long long)before.b.var,
                            (unsigned long long)before.x.var, (unsigned long long)before.y.var, before.sr.var, d.c_str());
                CHECK_MSG(false, "JIT diverged from the interpreter");
            }
            hist_j.erase(hist_j.begin(), hist_j.begin() + long(mi));   // the matched state becomes entry 0
            hist_i.erase(hist_i.begin(), hist_i.begin() + long(mj));
            if (++steps > 4000000) { CHECK_MSG(false, "block " << blk << " did not finish"); }
        }
        while (hi.txData().size() < 32) ei.dsp().execInterpreter();
        for (int k = 0; k < 32; ++k) { const auto a = hj.readTX() & 0xFFFFFF, c = hi.readTX() & 0xFFFFFF; CHECK_MSG(a == c, "block " << blk << " word " << k << ": jit " << std::hex << a << " interp " << c); }
        if (blk % 500 == 0) std::printf("    lockstep: block %d ok (%llu instr)\n", blk, (unsigned long long)ej.dsp().getInstructionCounter());
    }
}
