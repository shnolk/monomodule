#include "MnmDump.h"
#include <cstdio>
#include <cstring>

namespace mnm::dump {

namespace {

constexpr uint8_t kHeader[6] = {0xF0, 0x00, 0x20, 0x3C, 0x03, 0x00};

// Decoder for the 7-bit packing + RLE layer (see MCL ElektronSysexDecoder / MNMSysexDecoder).
class Decoder {
public:
    Decoder(const uint8_t* p, const uint8_t* end) : m_p(p), m_end(end) {}

    bool ok() const { return !m_fail; }
    // True once every byte of the payload has been consumed (no pending RLE repeats, no input left). The unit
    // writes each group's MSB byte before its data, so a payload whose data ends exactly on a group boundary
    // carries one more 0x00: the header of an empty group.
    bool atEnd() const { return m_repeatCount == 0 && (m_p >= m_end || ((m_cnt7 % 8) == 0 && m_end - m_p == 1)); }

    uint8_t get8()
    {
        while (true) {
            if (m_repeatCount > 0) { --m_repeatCount; return m_repeatByte; }
            const uint8_t b = raw8();
            if (m_fail) return 0;
            if (b & 0x80) {   // RLE marker: repeat count, then the value
                m_repeatCount = uint8_t(b & 0x7F);
                m_repeatByte = raw8();
            } else {
                return b;
            }
        }
    }

    void get(uint8_t* out, size_t n) { for (size_t i = 0; i < n; ++i) out[i] = get8(); }
    uint16_t get16() { uint16_t hi = get8(); return uint16_t((hi << 8) | get8()); }
    uint32_t get32() { uint32_t hi = get16(); return (hi << 16) | get16(); }
    uint64_t get64() { uint64_t hi = get32(); return (hi << 32) | get32(); }
    void get64(uint64_t* out, size_t n) { for (size_t i = 0; i < n; ++i) out[i] = get64(); }
    // Whatever decodes after the known layout (kept so the round trip stays exact if a firmware adds fields).
    std::vector<uint8_t> rest()
    {
        std::vector<uint8_t> v;
        while (!atEnd() && !m_fail) { const uint8_t b = get8(); if (!m_fail) v.push_back(b); }
        return v;
    }

private:
    // 7-bit layer: each group of 8 sysex bytes = 1 MSB byte + 7 data bytes (MSB-first).
    uint8_t raw8()
    {
        if ((m_cnt7 % 8) == 0) {
            m_bits = read();
            ++m_cnt7;
        }
        m_bits = uint8_t(m_bits << 1);
        ++m_cnt7;
        return uint8_t(read() | (m_bits & 0x80));
    }

    uint8_t read()
    {
        if (m_p >= m_end) { m_fail = true; return 0; }
        return *m_p++;
    }

    const uint8_t* m_p;
    const uint8_t* m_end;
    uint8_t m_bits = 0;
    uint32_t m_cnt7 = 0;
    uint8_t m_repeatCount = 0, m_repeatByte = 0;
    bool m_fail = false;
};

// msg = full message including F0..F7. Checksum: 14-bit sum of bytes [9 .. N-6]
// against the 14-bit value in bytes [N-5],[N-4] (see MCL calculateSysexChecksum).
bool checksumOk(const uint8_t* msg, size_t n)
{
    if (n < 15) return false;
    uint16_t sum = 0;
    for (size_t i = 9; i <= n - 6; ++i) sum = uint16_t(sum + msg[i]);
    sum &= 0x3FFF;
    const uint16_t stored = uint16_t((msg[n - 5] << 7) | msg[n - 4]);
    return sum == stored;
}

} // namespace

bool decodeKit(const uint8_t* msg, size_t n, Kit& kit)
{
    if (n < 15) return false;
    kit.version = msg[7];
    kit.revision = msg[8];
    kit.position = msg[9];
    size_t payload = 10;
    kit.extendedPosition = kit.version == 0x40;
    if (kit.extendedPosition) {   // extended form: extra byte flags position > 127
        if (msg[10]) kit.position += 128;
        payload = 11;
    }
    Decoder d(msg + payload, msg + n - 5);

    d.get(kit.nameRaw, 11);
    kit.name.clear();
    for (int i = 0; i < 11 && kit.nameRaw[i] != 0; ++i) {
        if (kit.nameRaw[i] < 0x20 || kit.nameRaw[i] > 0x7E) break;   // empty slots carry 0xFF garbage
        kit.name.push_back(char(kit.nameRaw[i]));
    }

    uint8_t levels[6];
    uint8_t params[6][72];
    uint8_t models[6], types[6];
    d.get(levels, 6);
    d.get(&params[0][0], 6 * 72);
    d.get(models, 6);
    d.get(types, 6);
    kit.unused461 = d.get8();
    kit.patchBusIn = d.get16();
    kit.mirrorLR = d.get8();
    kit.mirrorUD = d.get8();
    uint8_t destPages[6][6][2], destParams[6][6][2], destRanges[6][6][2];
    d.get(&destPages[0][0][0], sizeof(destPages));
    d.get(&destParams[0][0][0], sizeof(destParams));
    d.get(&destRanges[0][0][0], sizeof(destRanges));
    kit.lpKeyTrack = d.get8();
    kit.hpKeyTrack = d.get8();
    kit.trigPortamento = d.get8();
    d.get(kit.trigTracks, 6);
    kit.trigLegatoAmp = d.get8();
    kit.trigLegatoFilter = d.get8();
    kit.trigLegatoLFO = d.get8();
    kit.commonMultimode = d.get8();
    kit.commonTiming = d.get8();
    kit.splitKey = d.get8();
    kit.splitRange = d.get8();
    if (!d.ok()) return false;
    kit.tail = d.rest();

    for (int t = 0; t < 6; ++t) {
        kit.tracks[t].model = models[t];
        kit.tracks[t].type = types[t];
        kit.tracks[t].level = levels[t];
        std::memcpy(kit.tracks[t].params, params[t], 72);
        for (int s = 0; s < 6; ++s)
            for (int slot = 0; slot < 2; ++slot) {
                kit.tracks[t].destPage[s][slot] = destPages[t][s][slot];
                kit.tracks[t].destParam[s][slot] = destParams[t][s][slot];
                kit.tracks[t].destRange[s][slot] = int8_t(destRanges[t][s][slot]);
            }
    }
    return d.ok();
}

bool decodePattern(const uint8_t* msg, size_t n, Pattern& pat)
{
    if (n < 15) return false;
    pat.version = msg[7];
    pat.revision = msg[8];
    pat.position = msg[9];
    Decoder d(msg + 10, msg + n - 5);

    uint64_t* groups[13] = {pat.ampTrigs, pat.filterTrigs, pat.lfoTrigs, pat.offTrigs, pat.midiNoteOnTrigs, pat.midiNoteOffTrigs,
                            pat.triglessTrigs, pat.chordTrigs, pat.midiTriglessTrigs, pat.slidePatterns, pat.swingPatterns,
                            pat.midiSlidePatterns, pat.midiSwingPatterns};   // declaration order in MCL MNMPattern
    for (auto* g : groups) d.get64(g, 6);

    pat.swingAmount = d.get32();
    d.get64(pat.lockPatterns, 6);

    d.get(&pat.noteNBR[0][0], 6 * 64);
    pat.patternLength = d.get8();
    pat.doubleTempo = d.get8();
    pat.kit = d.get8();
    pat.patternTranspose = int8_t(d.get8());
    d.get(reinterpret_cast<uint8_t*>(pat.transpose), 6);
    d.get(pat.scale, 6);
    d.get(pat.key, 6);
    d.get(reinterpret_cast<uint8_t*>(pat.midiTranspose), 6);
    d.get(pat.midiScale, 6);
    d.get(pat.midiKey, 6);
    d.get(pat.arpPlay, 6);
    d.get(pat.arpMode, 6);
    d.get(pat.arpOctaveRange, 6);
    d.get(pat.arpMultiplier, 6);
    d.get(pat.arpDestination, 6);
    d.get(pat.arpLength, 6);
    d.get(&pat.arpPattern[0][0], 6 * 16);
    d.get(pat.midiArpPlay, 6);
    d.get(pat.midiArpMode, 6);
    d.get(pat.midiArpOctaveRange, 6);
    d.get(pat.midiArpMultiplier, 6);
    d.get(pat.midiArpLength, 6);
    d.get(&pat.midiArpPattern[0][0], 6 * 16);

    d.get(pat.unused, 4);
    pat.midiNotesUsed = d.get16();
    pat.chordNotesUsed = d.get8();
    pat.unused2 = d.get8();
    pat.locksUsed = d.get8();
    d.get(&pat.locksRaw[0][0], 62 * 64);
    for (auto& m : pat.midiNotes) m = d.get16();
    for (auto& c : pat.chordNotes) c = d.get16();
    pat.trailing = d.get8();
    if (!d.ok()) return false;
    pat.tail = d.rest();
    pat.finalizeLocks();
    return d.ok();
}

// Lock row assignment: rows are allocated in track-major, param-minor order of set
// lockPatterns bits (see MCL MNMPattern::fromSysex). The canonical view clears the rows without
// an assignment, which carry stale bytes on the wire.
void Pattern::finalizeLocks()
{
    for (int i = 0; i < 62; ++i) { lockTracks[i] = -1; lockParams[i] = -1; }
    std::memcpy(locks, locksRaw, sizeof(locks));
    int row = 0;
    for (int t = 0; t < 6; ++t)
        for (int p = 0; p < 64; ++p)
            if ((lockPatterns[t] >> p) & 1) {
                if (row < 62) {
                    lockTracks[row] = int8_t(t);
                    lockParams[row] = int8_t(p);
                }
                ++row;
            }
    for (int r = (row < 62 ? row : 62); r < 62; ++r) std::memset(locks[r], 255, 64);
}

int Pattern::noteTrigCount(int track) const
{
    int c = 0;
    for (int j = 0; j < 64 && j < patternLength; ++j)
        if ((ampTrigs[track] >> j) & 1) ++c;
    return c;
}

// A pattern counts as used when any track, the six MIDI sequencer tracks included, has a note trig inside
// the pattern length. Trigless (lock-only) trigs and note-offs alone do not make a pattern used.
bool Pattern::empty() const
{
    const uint64_t len = patternLength >= 64 ? ~0ull : ((1ull << patternLength) - 1);
    for (int t = 0; t < 6; ++t)
        if ((ampTrigs[t] & len) || (midiNoteOnTrigs[t] & len)) return false;
    return true;
}

std::string patternSlotName(int position)
{
    if (position < 0 || position > 127) return "?";
    char buf[4];
    std::snprintf(buf, sizeof(buf), "%c%02d", 'A' + position / 16, position % 16 + 1);
    return buf;
}

Dump parseDump(const uint8_t* data, size_t size, const std::string& filename)
{
    Dump dump;
    dump.file = filename;
    size_t i = 0;
    while (i < size) {
        // find next message
        while (i < size && data[i] != 0xF0) ++i;
        if (i >= size) break;
        size_t end = i + 1;
        while (end < size && data[end] != 0xF7) ++end;
        if (end >= size) break;
        const uint8_t* msg = data + i;
        const size_t n = end - i + 1;
        i = end + 1;

        Message m;
        m.raw.assign(msg, msg + n);
        if (n < 8 || std::memcmp(msg, kHeader, 6) != 0) { ++dump.numUnknown; dump.messages.push_back(std::move(m)); continue; }
        m.id = msg[6];
        m.version = n > 7 ? msg[7] : 0;
        m.revision = n > 8 ? msg[8] : 0;
        m.position = n > 9 ? msg[9] : -1;

        // Checksum is only enforced for the types we decode; songs/globals use a
        // different trailer layout and are kept verbatim.
        switch (m.id) {
        case kKitId: {
            Kit kit;
            if (n >= 15 && checksumOk(msg, n) && decodeKit(msg, n, kit)) {
                m.kitIndex = int(dump.kits.size());
                m.position = kit.position;
                dump.kits.push_back(std::move(kit));
            } else { m.damaged = true; ++dump.numDamaged; }
            break;
        }
        case kPatternId: {
            Pattern pat;
            if (n >= 15 && checksumOk(msg, n) && decodePattern(msg, n, pat)) {
                m.patternIndex = int(dump.patterns.size());
                dump.patterns.push_back(std::move(pat));
            } else { m.damaged = true; ++dump.numDamaged; }
            break;
        }
        case kSongId: ++dump.numSongs; break;
        case kGlobalId: ++dump.numGlobals; break;
        default: ++dump.numUnknown; break;
        }
        dump.messages.push_back(std::move(m));
    }
    return dump;
}

} // namespace mnm::dump
