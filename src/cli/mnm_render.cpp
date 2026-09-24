// Offline renderer: plays one note on one machine and writes the result to a WAV file.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#include "MonoVoice.h"
#include "util/Wav.h"

using namespace mnm;

static void usage()
{
    std::fprintf(stderr,
        "mnm-render [--fw os.syx] [--machine \"FM+ PAR\"|SAW|REVERB|...] [--note 60] [--dur 1.0] [--tail 1.0] [--level 100] [--tune 440]\n"
        "           [--bpm 120 | --tick N] [--syn a,b,c,d,e,f,g,h] [--amp ...] [--filt ...] [--efx ...]\n"
        "           [--lfo1 page,dest,trig,wave,mult,spd,intl,dpth] [--lfo2 ...] [--lfo3 ...]   (raw kit bytes, as the CC map)\n"
        "           [--dump-blocks file] [--interp] --out out.wav\n");
}

static bool parseList(const char* s, int out[8])
{
    std::stringstream ss(s); std::string t; int n = 0;
    while (std::getline(ss, t, ',') && n < 8) out[n++] = std::atoi(t.c_str());
    return n == 8;
}

int main(int argc, char** argv)
{
    std::string fwPath = MNM_OS_SYX, outPath, dumpPath;
    int note = 60, level = 100; host::Machine machine = host::Machine::FM_PAR; double dur = 1.0, tail = 1.0, tune = 440.0, bpm = 120.0; uint32_t tick = 0;
    int syn[8], amp[8], filt[8], efx[8], lfo[3][8]; bool hasSyn = false, hasAmp = false, hasFilt = false, hasEfx = false, hasLfo[3] = {};
    for (int i = 1; i < argc; ++i) {
        auto arg = [&](const char* k) { return std::strcmp(argv[i], k) == 0 && i + 1 < argc; };
        if (arg("--fw")) fwPath = argv[++i];
        else if (arg("--out")) outPath = argv[++i];
        else if (arg("--machine")) {
            const std::string m = argv[++i];
            const host::MachineDef* found = nullptr;
            for (const auto& d : host::kMachineDefs) if (m == d.name) found = &d;
            if (!found) { std::fprintf(stderr, "unknown machine '%s'; one of:", m.c_str()); for (const auto& d : host::kMachineDefs) std::fprintf(stderr, " \"%s\"", d.name); std::fprintf(stderr, "\n"); return 2; }
            machine = found->machine;
        }
        else if (arg("--note")) note = std::atoi(argv[++i]);
        else if (arg("--dur")) dur = std::atof(argv[++i]);
        else if (arg("--tail")) tail = std::atof(argv[++i]);
        else if (arg("--level")) level = std::atoi(argv[++i]);
        else if (arg("--tune")) tune = std::atof(argv[++i]);
        else if (arg("--tick")) tick = uint32_t(std::atoi(argv[++i]));
        else if (arg("--bpm")) bpm = std::atof(argv[++i]);
        else if (arg("--lfo1")) hasLfo[0] = parseList(argv[++i], lfo[0]);
        else if (arg("--lfo2")) hasLfo[1] = parseList(argv[++i], lfo[1]);
        else if (arg("--lfo3")) hasLfo[2] = parseList(argv[++i], lfo[2]);
        else if (arg("--syn")) hasSyn = parseList(argv[++i], syn);
        else if (arg("--amp")) hasAmp = parseList(argv[++i], amp);
        else if (arg("--filt")) hasFilt = parseList(argv[++i], filt);
        else if (arg("--efx")) hasEfx = parseList(argv[++i], efx);
        else if (arg("--dump-blocks")) dumpPath = argv[++i];
        else { usage(); return 2; }
    }
    if (outPath.empty()) { usage(); return 2; }
    if (fwPath.empty()) { std::fprintf(stderr, "no Monomachine OS file: pass --fw Elektron_SFX6-60_OS1.32B.syx\n"); return 2; }
    try {
        const auto fw = fw::loadFirmware(fwPath);
        std::fprintf(stderr, "firmware %s (%s)\n", fw.version.c_str(), fwPath.c_str());
        MonoVoice v(fw);
        auto& h = v.host();
        h.setMachine(machine);
        for (int k = 0; k < 8; ++k) {
            if (hasSyn) h.setParam(host::Page::SYN, k, syn[k]);
            if (hasAmp) h.setParam(host::Page::AMP, k, amp[k]);
            if (hasFilt) h.setParam(host::Page::FILT, k, filt[k]);
            if (hasEfx) h.setParam(host::Page::EFX, k, efx[k]);
        }
        for (int l = 0; l < 3; ++l)
            if (hasLfo[l]) for (int k = 0; k < 8; ++k) h.setLfoParam(l, k, lfo[l][k]);
        h.setLevel(level); h.setMasterTuneHz(tune); h.setBpm(bpm);
        if (tick) h.setTick(tick);   // override (e.g. --tick 345 reproduces the pre-0.7.6 tempo mapping)
        v.warmUp(8);   // settled parameters, like a loaded kit
        FILE* dump = dumpPath.empty() ? nullptr : std::fopen(dumpPath.c_str(), "w");
        const int onFrames = int(dur * 44100), totalFrames = int((dur + tail) * 44100);
        std::vector<int32_t> pcm; pcm.reserve(size_t(totalFrames) * 2);
        std::array<int32_t, 32> blk;
        h.noteOn(note);
        for (int f = 0; f < totalFrames; f += 16) {
            if (f >= onFrames && f - 16 < onFrames) h.noteOff();
            const auto& b = h.nextBlock();
            if (!v.engine().renderBlock(b.w.data(), blk)) { std::fprintf(stderr, "DSP fault: %s\n", v.engine().faultReason().c_str()); return 1; }
            if (dump) {
                std::fprintf(dump, "block %d in:", f / 16);
                for (auto x : b.w) std::fprintf(dump, " %06x", x);
                std::fprintf(dump, "\n out:");
                for (auto x : blk) std::fprintf(dump, " %06x", uint32_t(x) & 0xFFFFFF);
                std::fprintf(dump, "\n");
            }
            pcm.insert(pcm.end(), blk.begin(), blk.end());
        }
        if (dump) std::fclose(dump);
        if (!util::writeWav24(outPath, pcm, 2, 44100)) { std::fprintf(stderr, "cannot write %s\n", outPath.c_str()); return 1; }
        const auto& st = v.engine().stats();
        std::fprintf(stderr, "wrote %s: %d frames, %u blocks, %.0f instr/block avg\n", outPath.c_str(), totalFrames, st.blocks, double(st.totalInstructions) / st.blocks);
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "error: %s\n", e.what()); return 1; }
}
