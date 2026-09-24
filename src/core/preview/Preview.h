// What an audio preview plays, per catalog item kind (the library's stage 4):
//   pattern  the pattern on its kit, looped to a minimum length, then a tail for releases and delays;
//            the mix and the six tracks' own outputs come out of the same render
//   kit      its best pattern (the one using the most tracks, then the most trigs), or a demo pattern
//            that plays the kit's synth tracks one after another and then together
//   preset   one note on the track alone; an FX preset processes a plain SWAVE-SAW note on its neighbour
// The renders are cheap (about 30x real time per track), so nothing is stored: the app renders on demand
// and keeps recent results in memory.
#pragma once
#include <string>
#include <vector>
#include "library/Catalog.h"
#include "Sequencer.h"

namespace mnm::preview {

struct PreviewOptions {
    double bpm = 120.0;          // patterns carry no tempo (it is global on the unit)
    double minSeconds = 4.0;     // loop a pattern until at least this long ...
    int maxLoops = 4;            // ... but no more than this often
    double tailSeconds = 1.5;    // after the last note-off
    double presetSeconds = 1.0;  // the note of a preset preview
    int presetNote = 60;
};

struct PreviewSpec {
    dump::Kit kit;
    std::vector<Event> events;
    uint32_t frames = 0;     // total length
    uint32_t loopFrames = 0; // one pass of the pattern
    int loops = 1;
    bool stems = true;       // the six track outputs are of interest (patterns and kits)
};

PreviewSpec patternPreview(const dump::Kit& kit, const dump::Pattern& pat, const PreviewOptions& opt);
PreviewSpec presetPreview(const dump::KitTrack& track, bool lpKeyTrack, bool hpKeyTrack, const PreviewOptions& opt);
// The fallback for a kit no pattern uses: 16 steps, each synth track in turn on 2 steps, all of them on step 13.
dump::Pattern demoPattern(const dump::Kit& kit);
// The id of the kit's pattern to preview (most tracks with trigs, then most trigs, then first found), "" for none.
std::string choosePreviewPattern(const catalog::Catalog& cat, const catalog::KitItem& kit);

} // namespace mnm::preview
