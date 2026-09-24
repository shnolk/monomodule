#include "Preview.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "host/Machines.h"

namespace mnm::preview {

using namespace mnm::dump;

namespace {
Pattern blankPattern(int length)
{
    Pattern p;
    p.patternLength = uint8_t(std::clamp(length, 1, 64));
    std::memset(p.locksRaw, 0, sizeof(p.locksRaw));
    p.finalizeLocks();
    return p;
}
void fillDefaults(KitTrack& t, host::Machine m)
{
    t.model = uint8_t(m);
    const auto* def = host::machineDef(m);
    for (int k = 0; k < 8; ++k) {
        t.params[k] = def ? def->defaults[k] : 0;
        t.params[8 + k] = host::isFxMachine(m) ? host::kDefaultAmpFx[k] : host::kDefaultAmp[k];
        t.params[16 + k] = host::kDefaultFilt[k];
        t.params[24 + k] = host::kDefaultEfx[k];
    }
    t.level = 100;
}
}

PreviewSpec patternPreview(const Kit& kit, const Pattern& pat, const PreviewOptions& opt)
{
    PreviewSpec s;
    s.kit = kit;
    s.loopFrames = std::max(1u, loopFrames(pat, opt.bpm));
    const double loopSeconds = double(s.loopFrames) / kSampleRate;
    s.loops = std::clamp(int(std::ceil(opt.minSeconds / loopSeconds)), 1, std::max(1, opt.maxLoops));
    s.events = sequencePattern(pat, &kit, {opt.bpm, s.loops});
    s.frames = uint32_t(s.loops) * s.loopFrames + uint32_t(std::lround(opt.tailSeconds * kSampleRate));
    s.stems = true;
    return s;
}

PreviewSpec presetPreview(const KitTrack& track, bool lpKeyTrack, bool hpKeyTrack, const PreviewOptions& opt)
{
    Kit kit;
    kit.name = "PREVIEW";
    for (auto& t : kit.tracks) t = KitTrack{};
    const bool fx = host::isFxMachine(host::Machine(track.model));
    int trig = 0;
    if (fx) {
        // a plain saw on track 1 (no OUT BUS, so it is heard through the effect only) -> NEIBOR -> the preset on track 2
        fillDefaults(kit.tracks[0], host::Machine::SAW);
        kit.tracks[0].type = 0;
        kit.tracks[1] = track;
        kit.tracks[1].type = uint8_t(host::kRouteOutAB | host::kRouteAuxIn);   // OUT AB, input NEIBOR
        kit.tracks[1].level = 100;
        if (lpKeyTrack) kit.lpKeyTrack |= 2; if (hpKeyTrack) kit.hpKeyTrack |= 2;
        kit.lpKeyTrack |= 1; kit.hpKeyTrack |= 1;
    } else {
        kit.tracks[0] = track;
        kit.tracks[0].type = uint8_t(host::kRouteOutAB);
        kit.tracks[0].level = 100;
        if (lpKeyTrack) kit.lpKeyTrack |= 1; if (hpKeyTrack) kit.hpKeyTrack |= 1;
    }
    // one note on the trigged track for presetSeconds at the preview tempo (the pass ends with a note-off)
    const double stepSeconds = 60.0 / opt.bpm / 4.0;
    Pattern pat = blankPattern(std::max(1, int(std::lround(opt.presetSeconds / stepSeconds))));
    pat.ampTrigs[trig] = 1;
    pat.noteNBR[trig][0] = uint8_t(std::clamp(opt.presetNote, 0, 127));
    PreviewSpec s;
    s.kit = kit;
    s.loopFrames = std::max(1u, loopFrames(pat, opt.bpm));
    s.loops = 1;
    s.events = sequencePattern(pat, &kit, {opt.bpm, 1});
    s.frames = s.loopFrames + uint32_t(std::lround(opt.tailSeconds * kSampleRate));
    s.stems = false;
    return s;
}

Pattern demoPattern(const Kit& kit)
{
    Pattern p = blankPattern(16);
    p.kit = uint8_t(std::clamp(kit.position, 0, 127));
    int i = 0;
    for (int t = 0; t < 6; ++t) {
        const auto& tr = kit.tracks[t];
        if (tr.model == 0 || host::isFxMachine(host::Machine(tr.model))) continue;
        const int on = (i++ % 6) * 2;   // 0 2 4 6 8 10
        p.ampTrigs[t] |= 1ull << on;
        p.offTrigs[t] |= 1ull << (on + 1);
        p.ampTrigs[t] |= 1ull << 12;
        p.offTrigs[t] |= 1ull << 15;
        for (int j = 0; j < 64; ++j) p.noteNBR[t][j] = 60;
    }
    return p;
}

std::string choosePreviewPattern(const catalog::Catalog& cat, const catalog::KitItem& kit)
{
    std::string best;
    long bestScore = -1;
    for (const auto& id : kit.patternIds) {
        const auto* p = cat.pattern(id);
        if (!p) continue;
        int tracks = 0, trigs = 0;
        for (int t = 0; t < 6; ++t) { const int n = p->pattern.noteTrigCount(t); if (n) ++tracks; trigs += n; }
        const long score = long(tracks) * 100000 + trigs;
        if (score > bestScore) { bestScore = score; best = id; }
    }
    return best;
}

} // namespace mnm::preview
