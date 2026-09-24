#include "KitRenderer.h"
#include <algorithm>

namespace mnm::preview {

using namespace mnm::host;
using namespace mnm::dump;

KitRenderer::KitRenderer(const fw::Firmware& fw)
{
    for (auto& t : m_tracks) t.voice = std::make_unique<MonoVoice>(fw);
}

void KitRenderer::loadKit(const Kit& kit, double bpm)
{
    m_error.clear();
    for (int t = 0; t < kTracks; ++t) {
        auto& tr = m_tracks[size_t(t)];
        const auto& kt = kit.tracks[t];
        const Machine m = Machine(kt.model);
        tr.active = kt.model != 0 && machineDef(m) != nullptr;
        tr.fx = tr.active && isFxMachine(m);
        tr.input = FxInput(std::clamp(kit.fxInput(t), 0, 6));
        tr.outBuses = kit.outBuses(t);
        if (!tr.active) continue;
        tr.voice->reset();
        auto& h = tr.voice->host();
        h.setMachine(m);   // machine index + init/trig bit, then the kit's values overwrite the page defaults
        for (int k = 0; k < 8; ++k) {
            h.setParam(Page::SYN, k, kt.params[k]);
            h.setParam(Page::AMP, k, kt.params[8 + k]);
            h.setParam(Page::FILT, k, kt.params[16 + k]);
            h.setParam(Page::EFX, k, kt.params[24 + k]);
            for (int l = 0; l < 3; ++l) h.setLfoParam(l, k, kt.params[32 + 8 * l + k]);
        }
        h.setLevel(kt.level);
        h.setOutBuses(uint32_t(tr.outBuses));
        h.setRouting(tr.fx ? dspInputBits(tr.input) : 0u);   // internal sources are mixed here and enter through the ADC path
        h.setKeyTracking(kit.lpKeyTracks(t), kit.hpKeyTracks(t));
        h.setMasterTuneHz(440.0);
        h.setBpm(bpm);
        if (tr.fx) h.noteOn(60);   // FX machines must be trigged to pass audio
        tr.voice->warmUp(64);
    }
}

bool KitRenderer::render(const std::vector<Event>& events, uint32_t frames,
                         const std::function<void(uint32_t, const RenderBlock&)>& sink, const std::atomic<bool>* cancel)
{
    constexpr int N = RenderBlock::kFrames;
    std::array<size_t, kTracks> cursor{};   // next event per track (events are sorted by frame)
    std::array<std::vector<size_t>, kTracks> perTrack;
    for (size_t i = 0; i < events.size(); ++i) perTrack[events[i].track % kTracks].push_back(i);
    std::array<std::array<float, N>, 3> busL{}, busR{};
    std::array<float, N> zero{};
    RenderBlock blk;
    for (uint32_t f0 = 0; f0 < frames; f0 += N) {
        if (cancel && cancel->load()) return false;
        for (auto& b : busL) b.fill(0.f);
        for (auto& b : busR) b.fill(0.f);
        for (int t = 0; t < kTracks; ++t) {
            auto& tr = m_tracks[size_t(t)];
            auto& L = blk.stemL[size_t(t)];
            auto& R = blk.stemR[size_t(t)];
            if (!tr.active) { L.fill(0.f); R.fill(0.f); continue; }
            auto& h = tr.voice->host();
            // events due in this block take effect at its start, as the hardware latches the note state per frame
            auto& list = perTrack[size_t(t)];
            for (auto& c = cursor[size_t(t)]; c < list.size() && events[list[c]].frame < f0 + N; ++c) {
                const auto& e = events[list[c]];
                switch (e.kind) {
                case Event::NoteOn: h.noteOn(e.a); break;
                case Event::NoteOff: h.noteOff(); break;
                case Event::Param: {
                    int page = 0, param = 0;
                    if (!paramTarget(e.a, page, param)) break;
                    if (page < 4) h.setParam(Page(page), param, e.b); else h.setLfoParam(page - 4, param, e.b);
                    break;
                }
                }
            }
            if (tr.fx) {
                const float* srcL = zero.data();
                const float* srcR = zero.data();
                switch (tr.input) {
                case FxInput::Neighbor: if (t > 0) { srcL = blk.stemL[size_t(t - 1)].data(); srcR = blk.stemR[size_t(t - 1)].data(); } break;
                case FxInput::BusAB: case FxInput::BusCD: case FxInput::BusEF: {
                    const size_t b = size_t(int(tr.input) - int(FxInput::BusAB));
                    srcL = busL[b].data(); srcR = busR[b].data();
                    break;
                }
                default: break;   // INP A/B/AB: the plugin's side-chain, silent in an offline render
                }
                tr.voice->processFx(srcL, srcR, L.data(), R.data(), N);
            } else {
                tr.voice->process(L.data(), R.data(), N);
            }
            if (tr.voice->engine().faulted()) { m_error = "track " + std::to_string(t + 1) + ": " + tr.voice->engine().faultReason(); return false; }
            for (int b = 0; b < 3; ++b) {
                if (!((tr.outBuses >> b) & 1)) continue;
                const bool insert = tr.fx && int(tr.input) == int(FxInput::BusAB) + b;
                for (int i = 0; i < N; ++i) {
                    if (insert) { busL[size_t(b)][size_t(i)] = L[size_t(i)]; busR[size_t(b)][size_t(i)] = R[size_t(i)]; }
                    else { busL[size_t(b)][size_t(i)] += L[size_t(i)]; busR[size_t(b)][size_t(i)] += R[size_t(i)]; }
                }
            }
        }
        for (int i = 0; i < N; ++i) {
            blk.mixL[size_t(i)] = busL[0][size_t(i)] + busL[1][size_t(i)] + busL[2][size_t(i)];
            blk.mixR[size_t(i)] = busR[0][size_t(i)] + busR[1][size_t(i)] + busR[2][size_t(i)];
        }
        sink(f0, blk);
    }
    return true;
}

} // namespace mnm::preview
