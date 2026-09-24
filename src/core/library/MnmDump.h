#pragma once
// Lossless codec for Monomachine sysex dumps (kits, patterns; songs/globals/unknown messages are kept verbatim).
// The wire format is the one documented by the open-source MCL project (github.com/jmamma/MCL).
// Framing: F0 00 20 3C 03 00 <id> <version> <revision> <position> <payload...>
//          <cksumHi> <cksumLo> <lenHi> <lenLo> F7
// Payload is 8-bit data packed 7-bit (groups of 1 MSB byte + 7 data bytes), with an RLE
// layer on the decoded stream (byte with bit7 set = repeat count, next byte = value).
// Checksum = 14-bit sum of bytes [9 .. N-6]; length = N - 10 (position + payload + trailer).
// A parsed Dump keeps every message of the file in order with its raw bytes; kits and patterns are
// decoded field by field including everything the plugins do not use, so encodeDump() reproduces the
// file byte for byte and an edited kit/pattern re-encodes in the hardware's own format.
#include <array>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace mnm::dump {

constexpr uint8_t kGlobalId = 0x50, kKitId = 0x52, kPatternId = 0x67, kSongId = 0x69;

struct KitTrack {
    uint8_t model = 0;      // hardware machine model id (matches host::Machine values)
    uint8_t type = 0;       // the track's routing byte = DSP flag byte: bits 0-2 OUT BUS AB/CD/EF, bit 4 aux input
                            // (NEIBOR, or INP A/B/A+B with bits 6/7), bit 3 mix-bus input (which bus: Kit::busIn)
    uint8_t level = 0;
    uint8_t params[72] = {};   // [0..7] SYN, [8..15] AMP, [16..23] FILT, [24..31] EFX, [32..55] LFO1-3, [56..63] MIDI page, [64..71] extra
    // ASSIGN modifier matrix: 6 sources (+PB -PB +MW -MW VEL KEY) x 2 destination slots
    uint8_t destPage[6][2] = {};
    uint8_t destParam[6][2] = {};
    int8_t destRange[6][2] = {};
};

struct Kit {
    int position = 0;
    std::string name;             // printable part of nameRaw
    uint8_t nameRaw[11] = {};     // as on the wire (empty slots carry 0xFF)
    uint8_t version = 2, revision = 1;   // header bytes; version 0x40 = extended form with a second position byte
    bool extendedPosition = false;
    uint8_t unused461 = 0;
    KitTrack tracks[6];
    uint16_t patchBusIn = 0;      // 2-bit field per track (bits 2t..2t+1): the mix bus an FX track reads (1 AB, 2 CD, 3 EF)
    uint8_t trigTracks[6] = {};   // trig-track assignment per track (255 = off)
    uint8_t mirrorLR = 0, mirrorUD = 0;
    uint8_t lpKeyTrack = 0, hpKeyTrack = 0;   // bit t = track t's low-pass / high-pass filter tracks the key (KIT > ASSIGN > KEY)
    uint8_t trigPortamento = 0, trigLegatoAmp = 0, trigLegatoFilter = 0, trigLegatoLFO = 0;
    uint8_t commonMultimode = 0, commonTiming = 0, splitKey = 0, splitRange = 0;
    std::vector<uint8_t> tail;    // decoded bytes past the known layout (normally empty)

    bool isEmptySlot() const { return name.empty(); }

    // Decoded routing of a track (see KitTrack::type): OUT BUS bits (1 AB, 2 CD, 4 EF) and the FX input
    // source in the order of the sysex "set track routing" message: 0 NEIBOR, 1 INP A, 2 INP B, 3 INP A+B,
    // 4 BUS AB, 5 BUS CD, 6 BUS EF. The input is meaningful for FX machines only (synth tracks keep stale bits).
    int outBuses(int track) const { return tracks[track].type & 7; }
    int fxInput(int track) const
    {
        const uint8_t t = tracks[track].type;
        if (t & 0x08) { const int bus = (patchBusIn >> (2 * track)) & 3; return bus == 0 ? 0 : 3 + bus; }
        if (t & 0x10) { const int adc = (t >> 6) & 3; return adc; }   // 0 NEIBOR, 1 INP A, 2 INP B, 3 INP A+B
        return 0;
    }
    bool lpKeyTracks(int track) const { return (lpKeyTrack >> track) & 1; }
    bool hpKeyTracks(int track) const { return (hpKeyTrack >> track) & 1; }
};

struct Pattern {
    int position = 0;
    uint8_t version = 6, revision = 1;
    // Trig groups in wire order (MCL MNMPattern); bit j = step j.
    uint64_t ampTrigs[6] = {};       // note trigs
    uint64_t filterTrigs[6] = {};
    uint64_t lfoTrigs[6] = {};
    uint64_t offTrigs[6] = {};       // note-off steps
    uint64_t midiNoteOnTrigs[6] = {};
    uint64_t midiNoteOffTrigs[6] = {};
    uint64_t triglessTrigs[6] = {};  // lock-only trigs (no envelope restart)
    uint64_t chordTrigs[6] = {};
    uint64_t midiTriglessTrigs[6] = {};
    uint64_t slidePatterns[6] = {};
    uint64_t swingPatterns[6] = {};
    uint64_t midiSlidePatterns[6] = {};
    uint64_t midiSwingPatterns[6] = {};
    uint32_t swingAmount = 0;        // wire format: percent above 50 (0..30)
    uint64_t lockPatterns[6] = {};   // bit j = param j locked on this track
    uint8_t noteNBR[6][64] = {};     // note per step (255 = none / chord trig)
    uint8_t patternLength = 16;      // steps
    uint8_t doubleTempo = 0;
    uint8_t kit = 0;                 // linked kit slot
    int8_t patternTranspose = 0;
    int8_t transpose[6] = {};
    uint8_t scale[6] = {};           // 0 = chromatic
    uint8_t key[6] = {};
    int8_t midiTranspose[6] = {};
    uint8_t midiScale[6] = {};
    uint8_t midiKey[6] = {};
    uint8_t arpPlay[6] = {}, arpMode[6] = {}, arpOctaveRange[6] = {}, arpMultiplier[6] = {}, arpDestination[6] = {}, arpLength[6] = {};
    uint8_t arpPattern[6][16] = {};
    uint8_t midiArpPlay[6] = {}, midiArpMode[6] = {}, midiArpOctaveRange[6] = {}, midiArpMultiplier[6] = {}, midiArpLength[6] = {};
    uint8_t midiArpPattern[6][16] = {};
    uint8_t unused[4] = {};
    uint16_t midiNotesUsed = 0;
    uint8_t chordNotesUsed = 0, unused2 = 0, locksUsed = 0;
    uint8_t locksRaw[62][64];        // lock rows as on the wire (rows without an assignment carry stale bytes)
    uint16_t midiNotes[400] = {};    // note<<9 | track<<6 | position (MIDI sequencer tracks)
    uint16_t chordNotes[192] = {};   // same layout, chord notes of chord trigs
    uint8_t trailing = 0;            // one byte after the chord notes
    std::vector<uint8_t> tail;       // decoded bytes past the known layout (normally empty)

    // Derived views (rebuilt by parse / finalizeLocks): lock row -> track/param and the canonical lock values
    // (255 = no lock; rows without an assignment are cleared).
    int8_t lockTracks[62];           // -1 unused
    int8_t lockParams[62];           // param index 0..71
    uint8_t locks[62][64];
    void finalizeLocks();            // recompute the derived views from lockPatterns and locksRaw

    int swingPercent() const { return 50 + int(swingAmount > 30 ? 30 : swingAmount); }
    int noteTrigCount(int track) const;
    bool empty() const;              // no note trigs on any track (MIDI sequencer tracks included) within the length
    int midiNote(int i, int& track, int& position) const   // decoded MIDI-track note i
    {
        track = (midiNotes[i] >> 6) & 7; position = midiNotes[i] & 0x3F; return (midiNotes[i] >> 9) & 0x7F;
    }
    int chordNote(int i, int& track, int& position) const
    {
        track = (chordNotes[i] >> 6) & 7; position = chordNotes[i] & 0x3F; return (chordNotes[i] >> 9) & 0x7F;
    }
};

// One sysex message of the file, verbatim, with a link to its decoded form where there is one.
struct Message {
    uint8_t id = 0, version = 0, revision = 0;
    int position = -1;               // slot (kit/pattern/song/global), -1 when the message is too short
    std::vector<uint8_t> raw;        // F0 .. F7
    int kitIndex = -1;               // index into Dump::kits when decoded
    int patternIndex = -1;           // index into Dump::patterns when decoded
    bool damaged = false;            // a kit/pattern that failed its checksum or decode (kept raw only)
    bool knownId() const { return id == kGlobalId || id == kKitId || id == kPatternId || id == kSongId; }
};

struct Dump {
    std::string file;                // basename of the source file
    std::vector<Message> messages;   // every message, in file order
    std::vector<Kit> kits;
    std::vector<Pattern> patterns;
    int numSongs = 0;
    int numGlobals = 0;
    int numUnknown = 0;
    int numDamaged = 0;   // kit/pattern messages that failed checksum or decode (e.g. truncated capture)

    const Kit* kitAt(int position) const { for (const auto& k : kits) if (k.position == position) return &k; return nullptr; }
    const Pattern* patternAt(int position) const { for (const auto& p : patterns) if (p.position == position) return &p; return nullptr; }
};

// Pattern slot naming: 0 -> "A01" ... 127 -> "H16".
std::string patternSlotName(int position);

// Parses a whole .syx file (may contain many messages). Never throws; malformed
// messages are kept raw and counted in numDamaged/numUnknown.
Dump parseDump(const uint8_t* data, size_t size, const std::string& filename);

// Decodes one message's payload into a kit / pattern. Returns false on checksum or decode failure.
bool decodeKit(const uint8_t* msg, size_t n, Kit& kit);
bool decodePattern(const uint8_t* msg, size_t n, Pattern& pat);

// Encoding (MnmEncode.cpp). encodeDump() re-encodes every decoded kit/pattern from its struct (so edits
// take effect) and copies every other message verbatim; the result of an unedited dump is the source file.
std::vector<uint8_t> encodeKit(const Kit& kit);
std::vector<uint8_t> encodePattern(const Pattern& pat);
std::vector<uint8_t> encodeDump(const Dump& dump);

} // namespace mnm::dump
