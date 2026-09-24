// Drag-and-drop transfer between the Library app and the plugins: a track (.mnmtrack) or a whole kit
// (.mnmkit) as a JSON file the plugin editors accept as a file drop, and pattern MIDI (.mid) for the DAW.
// The JSON carries the complete kit (kitToJson) plus the track index, so nothing is lost on the way and a
// track drop can also apply the kit-level flags that belong to it (routing, key tracking).
#pragma once
#include <juce_core/juce_core.h>
#include "library/MnmDump.h"

namespace mnm::library {

constexpr const char* kTrackFileExtension = ".mnmtrack";
constexpr const char* kKitFileExtension = ".mnmkit";

enum class TransferKind { None, Track, Kit };

struct TransferPayload {
    TransferKind kind = TransferKind::None;
    juce::String name;        // display name ("SUPERWAVES T1", "SUPERWAVES")
    mnm::dump::Kit kit;
    int track = -1;           // for Track: which of kit.tracks
};

juce::var trackToTransferJson(const mnm::dump::Kit& kit, int track, const juce::String& name);
juce::var kitToTransferJson(const mnm::dump::Kit& kit, const juce::String& name);
bool transferFromJson(const juce::var& json, TransferPayload& out);
bool readTransferFile(const juce::File& file, TransferPayload& out);
bool isTransferFile(const juce::String& path, TransferKind kind);   // by extension

// Files handed to the OS for an external drag live in a per-process temp folder, cleared on first use.
juce::File dragDirectory();
juce::File writeTrackDragFile(const mnm::dump::Kit& kit, int track, const juce::String& baseName);
juce::File writeKitDragFile(const mnm::dump::Kit& kit, const juce::String& baseName);
// track = -1: the whole pattern as a multi-track SMF (one MIDI track per Monomachine track with trigs).
juce::File writePatternMidiDragFile(const mnm::dump::Dump& dump, const mnm::dump::Pattern& pat, int track);

} // namespace mnm::library
