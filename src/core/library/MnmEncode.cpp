// Encoder half of the Monomachine sysex codec: RLE + 7-bit packing, the 14-bit checksum and the length
// trailer. Field order mirrors the decoders in MnmDump.cpp exactly, so decode -> encode reproduces a dump
// byte for byte.
#include "MnmDump.h"
#include <cstring>

namespace mnm::dump {

namespace {

class Encoder {
public:
    // RLE stage: runs of equal bytes become <0x80|count><byte>; a lone byte < 0x80 goes plain, a lone
    // byte >= 0x80 needs the marker form (count 1) since bit 7 is the marker.
    void put8(uint8_t b)
    {
        if (m_count > 0 && b == m_byte && m_count < 0x7F) { ++m_count; return; }
        flush();
        m_byte = b;
        m_count = 1;
    }
    void put(const uint8_t* p, size_t n) { for (size_t i = 0; i < n; ++i) put8(p[i]); }
    void put16(uint16_t v) { put8(uint8_t(v >> 8)); put8(uint8_t(v)); }
    void put32(uint32_t v) { put16(uint16_t(v >> 16)); put16(uint16_t(v)); }
    void put64(uint64_t v) { put32(uint32_t(v >> 32)); put32(uint32_t(v)); }
    void put64(const uint64_t* p, size_t n) { for (size_t i = 0; i < n; ++i) put64(p[i]); }

    // Ends the stream: flushes the RLE run and the partial 7-bit group. A group's MSB byte is written before
    // its data on the unit, so data ending on a group boundary is followed by the header of an empty group.
    std::vector<uint8_t> finish()
    {
        flush();
        if (m_groupLen > 0) emitGroup();
        else m_out.push_back(0);
        return std::move(m_out);
    }

private:
    void flush()
    {
        if (m_count == 0) return;
        if (m_byte < 0x80 && m_count == 1) pack7(m_byte);
        else { pack7(uint8_t(0x80 | m_count)); pack7(m_byte); }
        m_count = 0;
    }
    // 7-bit layer: groups of 7 data bytes preceded by a byte holding their bit 7s (first byte -> bit 6).
    void pack7(uint8_t b)
    {
        m_group[m_groupLen] = uint8_t(b & 0x7F);
        if (b & 0x80) m_msb |= uint8_t(1u << (6 - m_groupLen));
        if (++m_groupLen == 7) emitGroup();
    }
    void emitGroup()
    {
        m_out.push_back(m_msb);
        m_out.insert(m_out.end(), m_group, m_group + m_groupLen);
        m_msb = 0; m_groupLen = 0;
    }

    std::vector<uint8_t> m_out;
    uint8_t m_group[7] = {};
    uint8_t m_msb = 0;
    int m_groupLen = 0;
    uint8_t m_byte = 0;
    int m_count = 0;
};

// Wraps a packed payload in the message framing: header, position byte(s), payload, checksum, length, F7.
std::vector<uint8_t> frame(uint8_t id, uint8_t version, uint8_t revision, const std::vector<uint8_t>& positionBytes, const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> msg = {0xF0, 0x00, 0x20, 0x3C, 0x03, 0x00, id, version, revision};
    msg.insert(msg.end(), positionBytes.begin(), positionBytes.end());
    msg.insert(msg.end(), payload.begin(), payload.end());
    uint16_t sum = 0;
    for (size_t i = 9; i < msg.size(); ++i) sum = uint16_t(sum + msg[i]);
    sum &= 0x3FFF;
    const uint16_t len = uint16_t(msg.size() - 9 + 4);   // position + payload + the four trailer bytes
    msg.push_back(uint8_t((sum >> 7) & 0x7F));
    msg.push_back(uint8_t(sum & 0x7F));
    msg.push_back(uint8_t((len >> 7) & 0x7F));
    msg.push_back(uint8_t(len & 0x7F));
    msg.push_back(0xF7);
    return msg;
}

} // namespace

std::vector<uint8_t> encodeKit(const Kit& kit)
{
    Encoder e;
    e.put(kit.nameRaw, 11);
    for (int t = 0; t < 6; ++t) e.put8(kit.tracks[t].level);
    for (int t = 0; t < 6; ++t) e.put(kit.tracks[t].params, 72);
    for (int t = 0; t < 6; ++t) e.put8(kit.tracks[t].model);
    for (int t = 0; t < 6; ++t) e.put8(kit.tracks[t].type);
    e.put8(kit.unused461);
    e.put16(kit.patchBusIn);
    e.put8(kit.mirrorLR);
    e.put8(kit.mirrorUD);
    for (int t = 0; t < 6; ++t) for (int s = 0; s < 6; ++s) for (int slot = 0; slot < 2; ++slot) e.put8(kit.tracks[t].destPage[s][slot]);
    for (int t = 0; t < 6; ++t) for (int s = 0; s < 6; ++s) for (int slot = 0; slot < 2; ++slot) e.put8(kit.tracks[t].destParam[s][slot]);
    for (int t = 0; t < 6; ++t) for (int s = 0; s < 6; ++s) for (int slot = 0; slot < 2; ++slot) e.put8(uint8_t(kit.tracks[t].destRange[s][slot]));
    e.put8(kit.lpKeyTrack);
    e.put8(kit.hpKeyTrack);
    e.put8(kit.trigPortamento);
    e.put(kit.trigTracks, 6);
    e.put8(kit.trigLegatoAmp);
    e.put8(kit.trigLegatoFilter);
    e.put8(kit.trigLegatoLFO);
    e.put8(kit.commonMultimode);
    e.put8(kit.commonTiming);
    e.put8(kit.splitKey);
    e.put8(kit.splitRange);
    e.put(kit.tail.data(), kit.tail.size());
    std::vector<uint8_t> pos;
    if (kit.extendedPosition) { pos.push_back(uint8_t(kit.position & 0x7F)); pos.push_back(uint8_t(kit.position >= 128 ? 1 : 0)); }
    else pos.push_back(uint8_t(kit.position & 0x7F));
    return frame(kKitId, kit.version, kit.revision, pos, e.finish());
}

std::vector<uint8_t> encodePattern(const Pattern& pat)
{
    Encoder e;
    const uint64_t* groups[13] = {pat.ampTrigs, pat.filterTrigs, pat.lfoTrigs, pat.offTrigs, pat.midiNoteOnTrigs, pat.midiNoteOffTrigs,
                                  pat.triglessTrigs, pat.chordTrigs, pat.midiTriglessTrigs, pat.slidePatterns, pat.swingPatterns,
                                  pat.midiSlidePatterns, pat.midiSwingPatterns};
    for (auto* g : groups) e.put64(g, 6);
    e.put32(pat.swingAmount);
    e.put64(pat.lockPatterns, 6);
    e.put(&pat.noteNBR[0][0], 6 * 64);
    e.put8(pat.patternLength);
    e.put8(pat.doubleTempo);
    e.put8(pat.kit);
    e.put8(uint8_t(pat.patternTranspose));
    e.put(reinterpret_cast<const uint8_t*>(pat.transpose), 6);
    e.put(pat.scale, 6);
    e.put(pat.key, 6);
    e.put(reinterpret_cast<const uint8_t*>(pat.midiTranspose), 6);
    e.put(pat.midiScale, 6);
    e.put(pat.midiKey, 6);
    e.put(pat.arpPlay, 6);
    e.put(pat.arpMode, 6);
    e.put(pat.arpOctaveRange, 6);
    e.put(pat.arpMultiplier, 6);
    e.put(pat.arpDestination, 6);
    e.put(pat.arpLength, 6);
    e.put(&pat.arpPattern[0][0], 6 * 16);
    e.put(pat.midiArpPlay, 6);
    e.put(pat.midiArpMode, 6);
    e.put(pat.midiArpOctaveRange, 6);
    e.put(pat.midiArpMultiplier, 6);
    e.put(pat.midiArpLength, 6);
    e.put(&pat.midiArpPattern[0][0], 6 * 16);
    e.put(pat.unused, 4);
    e.put16(pat.midiNotesUsed);
    e.put8(pat.chordNotesUsed);
    e.put8(pat.unused2);
    e.put8(pat.locksUsed);
    e.put(&pat.locksRaw[0][0], 62 * 64);
    for (auto m : pat.midiNotes) e.put16(m);
    for (auto c : pat.chordNotes) e.put16(c);
    e.put8(pat.trailing);
    e.put(pat.tail.data(), pat.tail.size());
    return frame(kPatternId, pat.version, pat.revision, {uint8_t(pat.position & 0x7F)}, e.finish());
}

std::vector<uint8_t> encodeDump(const Dump& dump)
{
    std::vector<uint8_t> out;
    for (const auto& m : dump.messages) {
        if (m.kitIndex >= 0 && m.kitIndex < int(dump.kits.size())) {
            const auto v = encodeKit(dump.kits[size_t(m.kitIndex)]);
            out.insert(out.end(), v.begin(), v.end());
        } else if (m.patternIndex >= 0 && m.patternIndex < int(dump.patterns.size())) {
            const auto v = encodePattern(dump.patterns[size_t(m.patternIndex)]);
            out.insert(out.end(), v.begin(), v.end());
        } else {
            out.insert(out.end(), m.raw.begin(), m.raw.end());
        }
    }
    return out;
}

} // namespace mnm::dump
