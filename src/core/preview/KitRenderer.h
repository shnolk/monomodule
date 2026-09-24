// Offline six-track render of a kit: the routing model of Monomodule Six (tracks render in index order,
// an FX track hears its neighbour or a mix bus as the tracks before it left it, OUT BUS adds a track to
// the AB/CD/EF buses and a bus-fed FX track replaces the bus it reads = insert), driven by timed events
// (preview/Sequencer.h) applied at 16-frame block boundaries like the hardware's control rate. Every block
// hands back the mix (the three buses summed) and the six tracks' own outputs (before the buses, so a
// track without an OUT BUS is still audible on its own). JUCE-free; the audio preview and mnm-libtool
// use it.
#pragma once
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "MonoVoice.h"
#include "Sequencer.h"

namespace mnm::preview {

struct RenderBlock {
    static constexpr int kFrames = 16;
    std::array<float, kFrames> mixL{}, mixR{};
    std::array<std::array<float, kFrames>, 6> stemL{}, stemR{};
};

class KitRenderer {
public:
    static constexpr int kTracks = 6;
    explicit KitRenderer(const fw::Firmware& fw);

    // Loads the kit: per track the machine, the seven pages, level, OUT BUS, FX input and key tracking, the
    // tempo (tick), a DSP reset for every track that plays and the same warm-up for all of them so the frame
    // counters (and so the LFO phases) stay in step, as Six does. GND and unknown machines stay silent.
    void loadKit(const dump::Kit& kit, double bpm);
    // Renders `frames` (rounded up to whole blocks), applying the events (sorted by frame) at the boundary of
    // the block each falls in; sink(firstFrame, block) per block. Returns false when a DSP faulted or
    // `cancel` was raised.
    bool render(const std::vector<Event>& events, uint32_t frames,
                const std::function<void(uint32_t, const RenderBlock&)>& sink, const std::atomic<bool>* cancel = nullptr);
    const std::string& error() const { return m_error; }

private:
    struct Track {
        std::unique_ptr<MonoVoice> voice;
        bool active = false, fx = false;
        host::FxInput input = host::FxInput::Neighbor;
        int outBuses = 0;
    };
    std::array<Track, kTracks> m_tracks;
    std::string m_error;
};

} // namespace mnm::preview
