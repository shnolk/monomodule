#include "Wav.h"
#include <fstream>
namespace mnm::util {
static void put32(std::ostream& o, uint32_t v) { char b[4] = {char(v), char(v >> 8), char(v >> 16), char(v >> 24)}; o.write(b, 4); }
static void put16(std::ostream& o, uint16_t v) { char b[2] = {char(v), char(v >> 8)}; o.write(b, 2); }
bool writeWav24(const std::string& path, const std::vector<int32_t>& s, int ch, int sr)
{
    std::ofstream o(path, std::ios::binary);
    if (!o) return false;
    const uint32_t dataBytes = uint32_t(s.size()) * 3;
    o.write("RIFF", 4); put32(o, 36 + dataBytes); o.write("WAVE", 4);
    o.write("fmt ", 4); put32(o, 16); put16(o, 1); put16(o, uint16_t(ch)); put32(o, uint32_t(sr));
    put32(o, uint32_t(sr * ch * 3)); put16(o, uint16_t(ch * 3)); put16(o, 24);
    o.write("data", 4); put32(o, dataBytes);
    for (int32_t v : s) { char b[3] = {char(v), char(v >> 8), char(v >> 16)}; o.write(b, 3); }
    return bool(o);
}
}
