#include "Sequencer.h"
#include <algorithm>
#include <cmath>

namespace mnm::preview {

using namespace mnm::dump;

namespace {
double stepFrames(const Pattern& pat, double bpm)
{
    const double sixteenth = kSampleRate * 60.0 / std::max(1.0, bpm) / 4.0;
    return pat.doubleTempo ? sixteenth / 2.0 : sixteenth;
}
int clampLen(const Pattern& pat) { return std::max(1, std::min(64, int(pat.patternLength))); }
}

bool paramTarget(int index, int& page, int& param)
{
    if (index < 0 || index >= 56) return false;
    page = index / 8; param = index % 8;
    return true;
}

uint32_t loopFrames(const Pattern& pat, double bpm)
{
    return uint32_t(std::lround(clampLen(pat) * stepFrames(pat, bpm)));
}

std::vector<Event> sequencePattern(const Pattern& pat, const Kit* kit, const SequenceOptions& opt)
{
    std::vector<Event> ev;
    const int len = clampLen(pat);
    const double step = stepFrames(pat, opt.bpm);
    const uint32_t loop = loopFrames(pat, opt.bpm);
    const int swing = pat.swingPercent();
    const int loops = std::max(1, opt.loops);

    for (int t = 0; t < 6; ++t) {
        // the lock rows of this track, with the value last sent so a revert is only sent once
        struct Row { int row, param, last; };
        std::vector<Row> rows;
        for (int r = 0; r < 62; ++r)
            if (pat.lockTracks[r] == t && pat.lockParams[r] >= 0 && pat.lockParams[r] < 56) rows.push_back({r, pat.lockParams[r], -1});
        bool sounding = false;
        for (int l = 0; l < loops; ++l) {
            for (int j = 0; j < len; ++j) {
                const bool noteTrig = (pat.ampTrigs[t] >> j) & 1;
                const bool offTrig = (pat.offTrigs[t] >> j) & 1;
                const bool trigless = (pat.triglessTrigs[t] >> j) & 1;
                if (!noteTrig && !offTrig && !trigless) continue;
                double f = l * double(loop) + j * step;
                if (((pat.swingPatterns[t] >> j) & 1) && swing > 50) f += (2.0 * step * swing) / 100.0 - step;
                const auto frame = uint32_t(std::lround(f));
                if (noteTrig || trigless)
                    for (auto& row : rows) {
                        int v = pat.locks[row.row][j];
                        if (v == 255) v = kit ? kit->tracks[t].params[row.param] : -1;   // unlocked step: back to the kit value
                        if (v >= 0 && v != row.last) { ev.push_back({frame, uint8_t(t), Event::Param, int16_t(row.param), int16_t(v)}); row.last = v; }
                    }
                if (noteTrig) {
                    const int raw = pat.noteNBR[t][j];
                    if (raw > 127) continue;   // chord trig: not modelled on a mono track
                    const int note = std::clamp(raw + int(pat.patternTranspose) + (pat.scale[t] == 0 ? int(pat.transpose[t]) : 0), 0, 127);
                    ev.push_back({frame, uint8_t(t), Event::NoteOn, int16_t(note), 0});
                    sounding = true;
                } else if (offTrig && sounding) {
                    ev.push_back({frame, uint8_t(t), Event::NoteOff, 0, 0});
                    sounding = false;
                }
            }
        }
        if (sounding) ev.push_back({uint32_t(loops) * loop, uint8_t(t), Event::NoteOff, 0, 0});
    }
    std::stable_sort(ev.begin(), ev.end(), [](const Event& a, const Event& b) { return a.frame < b.frame; });
    return ev;
}

} // namespace mnm::preview
