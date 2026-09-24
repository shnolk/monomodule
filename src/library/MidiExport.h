// Pattern track -> Standard MIDI File (notes, lengths, swing, slides, p-locks as CCs), for dragging a
// sequence into the DAW. Moved unchanged from the first library browser.
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "library/MnmDump.h"

namespace mnm::library {

juce::MidiFile buildTrackMidiFile(const mnm::dump::Dump& dump, const mnm::dump::Pattern& pat, int track);
juce::String trackMidiFileName(const mnm::dump::Dump& dump, const mnm::dump::Pattern& pat, int track);
// The whole pattern as a format-1 file: one MIDI track per Monomachine track that has trigs, channel = track number.
juce::MidiFile buildPatternMidiFile(const mnm::dump::Dump& dump, const mnm::dump::Pattern& pat);
juce::String machineName(uint8_t model);   // "SWAVE SAW", or "MODEL n" for an unknown id

} // namespace mnm::library
