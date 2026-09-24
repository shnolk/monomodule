// Pattern -> timed events for an offline render (the audio preview): the internal sequencer's note trigs,
// note-off trigs, trigless trigs and parameter locks of every track, at a tempo, looped, as frames at
// 44.1 kHz. JUCE-free; shares its reading of the pattern with library/MidiExport (swing, lengths, locks).
//
// What is modelled: 16th steps (32nds at 2x tempo), swing on the swung steps, note = step note +
// pattern transpose + track transpose (chromatic tracks), a note trig retrigs the mono track, a note-off
// trig releases it, locks apply at trigged steps and revert to the kit value at the next unlocked trig.
// Not modelled: chord trigs (skipped), arps, slides (the emulated track retrigs; portamento is a kit
// setting the renderer applies as is), trig tracks, the MIDI sequencer tracks.
#pragma once
#include <cstdint>
#include <vector>
#include "library/MnmDump.h"

namespace mnm::preview {

constexpr int kSampleRate = 44100;

struct Event {
    enum Kind : uint8_t { NoteOn, NoteOff, Param };
    uint32_t frame = 0;
    uint8_t track = 0;
    Kind kind = NoteOn;
    int16_t a = 0;   // NoteOn: MIDI note; Param: kit parameter index 0..55 (SYN AMP FILT EFX LFO1-3)
    int16_t b = 0;   // Param: value 0..127
};

struct SequenceOptions {
    double bpm = 120.0;
    int loops = 1;
};

// Frames of one pass through the pattern at the tempo (length x step), before swing.
uint32_t loopFrames(const dump::Pattern& pat, double bpm);
// The events of `loops` passes, sorted by frame (a step's locks precede its note-on). `kit` supplies the
// values locks revert to; null = no reverts. A final note-off on every track ends the last pass.
std::vector<Event> sequencePattern(const dump::Pattern& pat, const dump::Kit* kit, const SequenceOptions& opt);

// Where a kit parameter index lands in the host model: page 0-3 + param, or LFO l + param (page = 4 + l).
// Returns false for indices outside the seven pages (MIDI page, extras).
bool paramTarget(int index, int& page, int& param);

} // namespace mnm::preview
