#include "Firmware.h"
#include <cstring>
#include <fstream>

namespace mnm::fw {

std::vector<uint8_t> readFile(const std::filesystem::path& p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) throw FirmwareError("cannot open " + p.string());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

FlashImage parseSysex(const std::vector<uint8_t>& syx)
{
    static const uint8_t hdr[] = {0xF0, 0x00, 0x20, 0x3C, 0x03, 0x00, 0x7E};
    FlashImage img;
    bool haveBase = false;
    uint32_t expect = 0;
    size_t i = 0;
    while (i < syx.size()) {
        if (syx[i] != 0xF0) { ++i; continue; }
        size_t j = i;
        while (j < syx.size() && syx[j] != 0xF7) ++j;
        if (j >= syx.size()) break;
        const size_t len = j - i + 1;   // incl. F7
        const uint8_t* m = &syx[i];
        if (len > 9 + 6 && std::memcmp(m, hdr, sizeof(hdr)) == 0) {
            uint32_t addr = 0;
            for (int k = 0; k < 6; ++k) addr = (addr << 4) | (m[9 + k] & 0xF);
            const size_t payloadLen = len - 16;   // between a0 and F7
            if (payloadLen % 3 != 0) throw FirmwareError("sysex data block with bad length");
            if (!haveBase) { img.base = addr; expect = addr; haveBase = true; }
            if (addr != expect) throw FirmwareError("non-contiguous sysex data block");
            for (size_t k = 15; k + 2 < len - 1; k += 3) {
                const uint32_t w = (uint32_t(m[k]) << 14) | (uint32_t(m[k + 1]) << 7) | m[k + 2];
                img.bytes.push_back(uint8_t(w >> 8));
                img.bytes.push_back(uint8_t(w & 0xFF));
            }
            expect += uint32_t(payloadLen / 3 * 2);
        } else {
            img.trailers.emplace_back(m, m + len);
        }
        i = j + 1;
    }
    if (!haveBase) throw FirmwareError("no Monomachine OS data messages found");
    return img;
}

namespace {
struct Bits {
    const uint8_t* d; size_t n, i = 0; uint32_t tag = 0; int left = 0; bool err = false;
    Bits(const uint8_t* data, size_t size) : d(data), n(size) {}
    int bit() {
        if (left == 0) {
            if (i >= n) { err = true; return 0; }
            tag = d[i++]; left = 8;
        }
        --left; return (tag >> left) & 1;
    }
    uint32_t byte() { if (i >= n) { err = true; return 0; } return d[i++]; }
    uint32_t gamma() {
        uint32_t v = 1;
        while (!err) { v = (v << 1) | uint32_t(bit()); if (bit()) return v; }
        return v;
    }
};
}

std::vector<uint8_t> aplibDepack(const uint8_t* stream, size_t size)
{
    constexpr uint32_t kOffsetBias = 767, kFar = 3328;
    Bits b(stream, size);
    std::vector<uint8_t> out;
    out.reserve(size * 3);
    uint32_t last = 1;
    while (!b.err) {
        if (b.bit()) {
            if (b.i >= b.n) break;
            out.push_back(uint8_t(b.byte()));
            continue;
        }
        const uint32_t g = b.gamma();
        uint32_t off;
        if (g == 2) off = last;
        else {
            const uint32_t raw = (g << 8) + b.byte();
            if (raw == kOffsetBias) break;
            off = raw - kOffsetBias; last = off;
        }
        const uint32_t sl = (uint32_t(b.bit()) << 1) | uint32_t(b.bit());
        uint32_t L = sl ? sl : b.gamma() + 2;
        if (off > kFar) ++L;
        if (b.err) break;
        if (off == 0 || off > out.size()) throw FirmwareError("aplib: bad match offset");
        for (uint32_t k = 0; k <= L; ++k) out.push_back(out[out.size() - off]);
    }
    return out;
}

static uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

Container parseContainer(const FlashImage& img)
{
    Container c;
    const auto& d = img.bytes;
    size_t p = 0; int n = 0;
    while (p + 8 <= d.size()) {
        const uint32_t size = be32(&d[p]), chk = be32(&d[p + 4]);
        const bool plausible = size >= 16 && size <= d.size() - p - 8;
        uint32_t sum = 0;
        if (plausible) for (size_t k = 0; k < size; ++k) sum += d[p + 8 + k];
        const bool sumOk = plausible && sum == chk;
        if (!(plausible && (sumOk || n == 0))) {   // community builds patch section 0 in place without fixing its sum
            bool ascii = true;
            for (size_t k = 0; k < 8; ++k) if (d[p + k] < 32 || d[p + k] >= 127) ascii = false;
            if (ascii) { c.version.assign(reinterpret_cast<const char*>(&d[p]), 8); p += 8; continue; }
            break;
        }
        Section s;
        s.index = n++; s.flashAddr = img.base + uint32_t(p); s.streamSize = size; s.streamSum = chk; s.sumOk = sumOk;
        s.data = aplibDepack(&d[p + 8], size);
        c.sections.push_back(std::move(s));
        p += 8 + size;
    }
    return c;
}

DspImage parseDspRecords(const std::vector<uint8_t>& sec)
{
    DspImage img;
    const size_t nw = sec.size() / 3;
    auto w = [&](size_t k) { return uint32_t(sec[3 * k]) | (uint32_t(sec[3 * k + 1]) << 8) | (uint32_t(sec[3 * k + 2]) << 16); };
    size_t p = 0;
    while (p < nw) {
        const uint32_t t = w(p);
        if (t == 3) { if (p + 1 >= nw) break; img.startAddr = w(p + 1); p += 2; continue; }
        if (t > 2 || p + 3 > nw) throw FirmwareError("dsp records: bad record header");
        DspRecord r; r.space = Space(t); r.addr = w(p + 1);
        const uint32_t cnt = w(p + 2);
        if (p + 3 + cnt > nw) throw FirmwareError("dsp records: truncated record");
        r.words.resize(cnt);
        for (uint32_t k = 0; k < cnt; ++k) r.words[k] = w(p + 3 + k);
        img.records.push_back(std::move(r));
        p += 3 + cnt;
    }
    return img;
}

Firmware loadFirmware(const std::filesystem::path& syxPath)
{
    const auto c = parseContainer(parseSysex(readFile(syxPath)));
    if (c.sections.size() < 4) throw FirmwareError("OS file has " + std::to_string(c.sections.size()) + " sections, expected >= 4 (not a Monomachine OS?)");
    for (int k = 1; k <= 3; ++k) if (!c.sections[k].sumOk) throw FirmwareError("section " + std::to_string(k) + " checksum mismatch");
    Firmware f;
    f.kernelA = parseDspRecords(c.sections[1].data);
    f.kernelB = parseDspRecords(c.sections[2].data);
    f.payload = parseDspRecords(c.sections[3].data);
    f.version = c.version;
    f.source = syxPath;
    return f;
}

std::vector<uint8_t> loadMainOs(const std::filesystem::path& syxPath)
{
    auto c = parseContainer(parseSysex(readFile(syxPath)));
    if (c.sections.size() < 4) throw FirmwareError("OS file has " + std::to_string(c.sections.size()) + " sections, expected >= 4 (not a Monomachine OS?)");
    return std::move(c.sections[0].data);
}

} // namespace mnm::fw
