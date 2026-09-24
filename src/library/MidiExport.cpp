#include "MidiExport.h"
#include "host/Machines.h"

namespace mnm::library {

using namespace mnm::dump;

juce::String machineName(uint8_t model)
{
    if (const auto* def = host::machineDef(host::Machine(model))) return def->name;
    return "MODEL " + juce::String(int(model));
}

namespace {
constexpr int kPPQ = 480;

// CC numbers as the instrument plugins (and the hardware) map them.
int ccForParam(int p)
{
    if (p < 8) return 48 + p;          // SYN
    if (p < 16) return 56 + (p - 8);   // AMP
    if (p < 24) return 72 + (p - 16);  // FILT
    if (p < 32) return 80 + (p - 24);  // EFX
    if (p < 40) return 88 + (p - 32);  // LFO1
    if (p < 48) return 104 + (p - 40); // LFO2
    if (p < 56) return 112 + (p - 48); // LFO3
    return -1;
}
} // namespace

static juce::MidiMessageSequence trackSequence(const Dump& dump, const Pattern& pat, int track, int channel);

juce::MidiFile buildTrackMidiFile(const Dump& dump, const Pattern& pat, int track)
{
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote(kPPQ);
    mf.addTrack(trackSequence(dump, pat, track, 1));
    return mf;
}

juce::MidiFile buildPatternMidiFile(const Dump& dump, const Pattern& pat)
{
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote(kPPQ);
    for (int t = 0; t < 6; ++t)
        if (pat.noteTrigCount(t) > 0) mf.addTrack(trackSequence(dump, pat, t, t + 1));   // channel t+1: Six's track t+1
    if (mf.getNumTracks() == 0) mf.addTrack(trackSequence(dump, pat, 0, 1));
    return mf;
}

static juce::MidiMessageSequence trackSequence(const Dump& dump, const Pattern& pat, int track, int channel)
{
    const int len = juce::jlimit(1, 64, int(pat.patternLength));
    const int stepTicks = pat.doubleTempo ? kPPQ / 8 : kPPQ / 4;   // 32nds at 2x tempo
    const int swing = pat.swingPercent();

    auto tickOf = [&](int j) {
        int t = j * stepTicks;
        if (((pat.swingPatterns[track] >> j) & 1) && swing > 50)
            t += (2 * stepTicks * swing) / 100 - stepTicks;
        return t;
    };

    const Kit* kit = dump.kitAt(pat.kit);

    juce::MidiMessageSequence seq;
    const juce::String name = juce::String(patternSlotName(pat.position)) + " T" + juce::String(track + 1)
        + (kit ? " " + machineName(kit->tracks[track].model) : juce::String());
    seq.addEvent(juce::MidiMessage::textMetaEvent(3, name), 0.0);

    struct LockRow { int param; int cc; int lastSent; };
    std::vector<std::pair<int, LockRow>> rows;
    for (int r = 0; r < 62; ++r)
        if (pat.lockTracks[r] == track && pat.lockParams[r] >= 0) {
            const int cc = ccForParam(pat.lockParams[r]);
            if (cc > 0) rows.push_back({r, {pat.lockParams[r], cc, -1}});
        }

    for (int j = 0; j < len; ++j) {
        const bool noteTrig = (pat.ampTrigs[track] >> j) & 1;
        const bool anyTrig = noteTrig || ((pat.triglessTrigs[track] >> j) & 1);
        if (!anyTrig) continue;
        const double t = tickOf(j);

        for (auto& [r, row] : rows) {
            int v = pat.locks[r][j];
            if (v == 255) v = kit ? kit->tracks[track].params[row.param] : -1;
            if (v >= 0 && v != row.lastSent) {
                seq.addEvent(juce::MidiMessage::controllerEvent(channel, row.cc, v), t);
                row.lastSent = v;
            }
        }

        const int note = pat.noteNBR[track][j];
        if (!noteTrig || note > 127) continue;   // 255 = chord trig (chord notes not exported yet)
        const int outNote = juce::jlimit(0, 127, note + pat.patternTranspose
                                                 + (pat.scale[track] == 0 ? pat.transpose[track] : 0));

        int endTick = len * stepTicks;
        bool endIsTrig = false;
        for (int k = j + 1; k < len; ++k) {
            const bool trig = (pat.ampTrigs[track] >> k) & 1;
            const bool off = (pat.offTrigs[track] >> k) & 1;
            if (trig || off) { endTick = tickOf(k); endIsTrig = trig; break; }
        }
        if (endIsTrig) {
            int nextStep = int(endTick) / stepTicks;
            if ((pat.slidePatterns[track] >> juce::jlimit(0, 63, nextStep)) & 1)
                endTick += stepTicks / 4;
        }
        seq.addEvent(juce::MidiMessage::noteOn(channel, outNote, (juce::uint8)100), t);
        seq.addEvent(juce::MidiMessage::noteOff(channel, outNote), double(endTick));
    }

    seq.addEvent(juce::MidiMessage::textMetaEvent(6, "loop end"), double(len * stepTicks));
    seq.updateMatchedPairs();
    seq.sort();
    return seq;
}

juce::String trackMidiFileName(const Dump& dump, const Pattern& pat, int track)
{
    return juce::String(dump.file) + "-" + juce::String(patternSlotName(pat.position))
        + "-T" + juce::String(track + 1) + ".mid";
}

} // namespace mnm::library
