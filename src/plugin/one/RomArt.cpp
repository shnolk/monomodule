#include "RomArt.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <deque>
#include <memory>
#include <stdexcept>
#include <system_error>
#include "firmware/Firmware.h"

namespace mnm::uispec {

namespace {

// Backing storage of one artwork set. Deques and per-font vectors sized once: addresses stay put.
struct Store {
    std::deque<std::vector<uint64_t>> rows;
    std::deque<Bitmap> bitmaps;
    struct FontData { std::vector<Bitmap> glyphs; std::array<int16_t, 128> index; };
    std::deque<FontData> fonts;

    const uint64_t* addRows(std::vector<uint64_t> r) { rows.push_back(std::move(r)); return rows.back().data(); }
    const Bitmap* addBitmap(int w, int h, std::vector<uint64_t> r)
    {
        bitmaps.push_back(Bitmap{uint8_t(w), uint8_t(h), addRows(std::move(r))});
        return &bitmaps.back();
    }
};

// Every set ever made, kept for the life of the process (see the threading note in RomArt.h).
std::vector<std::unique_ptr<Store>>& stores() { static std::vector<std::unique_ptr<Store>> s; return s; }

// ---------------------------------------------------------------------------------------------------
// Stand-in artwork, used while no OS file is present. Drawn for this project: a plain 3x5 caps face, one
// digit per row of the glyph (bit 2 = left column), and generated dials. The other faces are made from it
// by repeating rows and columns, so layouts keep the row heights they have with the hardware's faces.

struct Glyph35 { char code; const char* rows; };
constexpr Glyph35 kStandIn[] = {
    {'A', "25755"}, {'B', "65656"}, {'C', "34443"}, {'D', "65556"}, {'E', "74647"}, {'F', "74644"}, {'G', "34553"},
    {'H', "55755"}, {'I', "72227"}, {'J', "11152"}, {'K', "55655"}, {'L', "44447"}, {'M', "57755"}, {'N', "65555"},
    {'O', "25552"}, {'P', "65644"}, {'Q', "25573"}, {'R', "65655"}, {'S', "34216"}, {'T', "72222"}, {'U', "55557"},
    {'V', "55552"}, {'W', "55775"}, {'X', "55255"}, {'Y', "55222"}, {'Z', "71247"},
    {'0', "75557"}, {'1', "26227"}, {'2', "61247"}, {'3', "61216"}, {'4', "55711"}, {'5', "74616"}, {'6', "34757"},
    {'7', "71222"}, {'8', "75757"}, {'9', "75716"},
    {'+', "02720"}, {'-', "00700"}, {'.', "00002"}, {',', "00024"}, {'/', "11244"}, {':', "02020"}, {'(', "12221"},
    {')', "42224"}, {'%', "51245"}, {'!', "22202"}, {'?', "61202"}, {'\'', "22000"}, {'<', "12421"}, {'>', "42124"},
    {'=', "07070"}, {'_', "00007"}, {'#', "57575"}, {'&', "25253"}, {'*', "52725"}, {'[', "32223"}, {']', "62226"},
};

// Three columns cannot hold a diagonal: the 5-column faces take these letters from their own drawings (bit 4 = left).
struct Glyph55 { char code; uint8_t rows[5]; };
constexpr Glyph55 kStandInWide[] = {
    {'M', {0b10001, 0b11011, 0b10101, 0b10001, 0b10001}},
    {'N', {0b10001, 0b11001, 0b10101, 0b10011, 0b10001}},
    {'W', {0b10001, 0b10001, 0b10101, 0b11011, 0b10001}},
    {'V', {0b10001, 0b10001, 0b10001, 0b01010, 0b00100}},
    {'X', {0b10001, 0b01010, 0b00100, 0b01010, 0b10001}},
    {'Y', {0b10001, 0b01010, 0b00100, 0b00100, 0b00100}},
    {'K', {0b10010, 0b10100, 0b11000, 0b10100, 0b10010}},
};

// rowMap/colMap: source row/column of every output row/column.
Font standInFont(Store& st, std::vector<int> rowMap, std::vector<int> colMap, const char* only = nullptr)
{
    st.fonts.emplace_back();
    auto& fd = st.fonts.back();
    fd.index.fill(-1);
    const int w = int(colMap.size()), h = int(rowMap.size());
    for (const auto& g : kStandIn) {
        if (only && !std::char_traits<char>::find(only, std::char_traits<char>::length(only), g.code)) continue;
        std::vector<uint64_t> rows(size_t(h), 0);
        const Glyph55* wide = nullptr;
        if (w == 5) for (const auto& k : kStandInWide) if (k.code == g.code) wide = &k;
        for (int r = 0; r < h; ++r) {
            const int bits = wide ? wide->rows[rowMap[size_t(r)]] : g.rows[rowMap[size_t(r)]] - '0';
            for (int c = 0; c < w; ++c)
                if ((bits >> (wide ? 4 - c : 2 - colMap[size_t(c)])) & 1) rows[size_t(r)] |= uint64_t(1) << (63 - c);
        }
        fd.index[size_t(g.code)] = int16_t(fd.glyphs.size());
        fd.glyphs.push_back(Bitmap{uint8_t(w), uint8_t(h), st.addRows(std::move(rows))});
    }
    return Font{uint8_t(h), uint8_t(w), fd.glyphs.data(), fd.index.data()};
}

const Bitmap* fromText(Store& st, std::initializer_list<const char*> lines)
{
    std::vector<uint64_t> rows;
    int w = 0;
    for (const char* l : lines) {
        uint64_t v = 0; int x = 0;
        for (; l[x]; ++x) if (l[x] == '#') v |= uint64_t(1) << (63 - x);
        rows.push_back(v); w = x;
    }
    const int h = int(rows.size());
    return st.addBitmap(w, h, std::move(rows));
}

const Bitmap* ring(Store& st, int size, int topPad)   // a circle of `size` pixels across under topPad blank rows
{
    std::vector<uint64_t> rows(size_t(size + topPad), 0);
    const double c = (size - 1) / 2.0;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            if (std::abs(std::hypot(x - c, y - c) - c) < 0.5) rows[size_t(y + topPad)] |= uint64_t(1) << (63 - x);
    return st.addBitmap(size, size + topPad, std::move(rows));
}

void buildStandIn(Art& a)
{
    stores().push_back(std::make_unique<Store>());
    auto& st = *stores().back();
    a = Art{};
    a.tiny3x5 = standInFont(st, {0, 1, 2, 3, 4}, {0, 1, 2});
    a.small4x5 = a.tiny3x5;
    a.square5x5 = standInFont(st, {0, 1, 2, 3, 4}, {0, 0, 1, 2, 2});
    // the 8-row face: outer columns doubled for a heavy stem (smearing a 3-column glyph would close its counters)
    a.bold8 = standInFont(st, {0, 0, 1, 1, 2, 3, 3, 4}, {0, 0, 1, 2, 2});
    a.digitsTop = standInFont(st, {0, 0, 1, 1, 2}, {0, 0, 1, 2, 2}, "%./0123456789");
    a.digitsBottom = standInFont(st, {2, 3, 3, 4, 4}, {0, 0, 1, 2, 2}, "%./0123456789");

    const Bitmap* dial = ring(st, 11, 2);   // 11x13 like the hardware's, with a tick at 12 o'clock
    const_cast<uint64_t*>(dial->rows)[0] |= uint64_t(1) << (63 - 5);
    a.dialRing = *dial;
    a.ringPlain = *ring(st, 11, 0);
    a.groupTie = *fromText(st, {".#####.", "#.....#", "#.....#"});
    for (int v = 0; v < 128; ++v) {   // pointer: two pixels on the spoke, swept -150..+150 degrees
        std::vector<uint64_t> rows(7, 0);
        const double ang = (-150.0 + 300.0 * v / 127.0) * 3.14159265358979323846 / 180.0;
        for (double rad : {3.0, 2.0}) {
            const int x = int(std::lround(3 + rad * std::sin(ang))), y = int(std::lround(3 - rad * std::cos(ang)));
            if (x >= 0 && x < 7 && y >= 0 && y < 7) rows[size_t(y)] |= uint64_t(1) << (63 - x);
        }
        a.dialDot[v] = st.addBitmap(7, 7, std::move(rows));
    }
}

// ---------------------------------------------------------------------------------------------------
// Where OS 1.32B keeps the artwork (addresses in the main OS image, load base 0x200000). Facts about the
// file's layout; the pixels themselves are only ever read from the user's copy.
//
// Icon descriptor, 20 bytes of big-endian u32: w, h, n, ->pixels, ->mask. pixels = w columns of u32, the
// glyph in the high h bits, row r (0 = top) = bit 32-h+r; mask = pixels + 4w.
// Font descriptor, 5 x u32: default advance, rows (<= 8), ->widths[256] u8, ->offsets[256] i16 (negative =
// no glyph), ->columns, one byte per column, row r = bit 8-h+r. Codes >= 128 are symbol slots, not used.

constexpr uint32_t kBase = 0x200000;
constexpr uint32_t kDialRingAt = 0x259BC0, kRingPlainAt = 0x259B54, kGroupTieAt = 0x2626A8;
constexpr uint32_t kDialDotAt = 0x25FC20;                       // 128 descriptors back to back
constexpr uint32_t kLfoDestAt = 0x25B1E0;                       // 8 descriptors back to back
constexpr uint32_t kLfoWaveAt = 0x25AADC, kLfoWaveStride = 0x9C;   // 11, each followed by its columns
constexpr uint32_t kDescSize = 20;
// The LFO wave icons sit in the image in alphabetical order of their names (EXP IEXP IRMP ISAW ISQR ITRI RMP
// RND SAW SQR TRI); this is the image position of each menu entry (TRI ITRI SAW ISAW SQR ISQR EXP IEXP RMP IRMP RND).
constexpr int kLfoWaveImagePos[11] = {10, 5, 8, 3, 9, 4, 0, 1, 6, 2, 7};
constexpr uint32_t kFontBold8At = 0x265734, kFontSmall4x5At = 0x2662EC, kFontTiny3x5At = 0x2666D4,
                   kFontSquare5x5At = 0x266A9C, kFontDigitsTopAt = 0x265F54, kFontDigitsBottomAt = 0x265BBC;
// Pointer tables (u32 per list entry -> icon descriptor), as the machine descriptors reference them.
constexpr uint32_t kToggleTable = 0x25BBE8, kFmRatioTable = 0x25D330, kEnsPitchTable = 0x25F260,
                   kFmDynFrqTable = 0x25EFFC, kSidWaveTable = 0x25EB54, kDproSyncTable = 0x25BD3C,
                   kDproWaveTable = 0x25C0D0, kVoConsTable = 0x25E074, kDdrwWaveTable = 0x263550,
                   kLfoPageTable = 0x25B334;
constexpr uint32_t kGroupLogoTable = 0x2507E2;   // 5 pointers: the splash logos SWAVE SID DPRO FM+ VO
constexpr const char* kGroupLogoNames[5] = {"SWAVE", "SID", "DPRO", "FM+", "VO"};

struct Reader {
    const std::vector<uint8_t>& d;
    Store& st;
    std::vector<std::pair<uint32_t, const Bitmap*>> seen;   // pointer tables share descriptors

    [[noreturn]] static void bad(const char* what, uint32_t at)
    {
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s at 0x%06X", what, unsigned(at));
        throw std::runtime_error(buf);
    }
    const uint8_t* at(uint32_t addr, size_t n) const
    {
        if (addr < kBase || size_t(addr - kBase) + n > d.size()) bad("address outside the image", addr);
        return &d[addr - kBase];
    }
    uint32_t u32(uint32_t addr) const { const auto* p = at(addr, 4); return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

    const Bitmap* icon(uint32_t addr, int wantW = 0, int wantH = 0)
    {
        for (const auto& s : seen) if (s.first == addr) return s.second;
        const uint32_t w = u32(addr), h = u32(addr + 4), n = u32(addr + 8), px = u32(addr + 12), mask = u32(addr + 16);
        if (w < 1 || w > 64 || h < 1 || h > 32 || n < 1 || n > 4 || mask != px + 4 * w) bad("not an icon descriptor", addr);
        if ((wantW && int(w) != wantW) || (wantH && int(h) != wantH)) bad("icon of unexpected size", addr);
        std::vector<uint64_t> rows(h, 0);
        for (uint32_t c = 0; c < w; ++c) {
            const uint32_t col = u32(px + 4 * c);
            for (uint32_t r = 0; r < h; ++r)
                if ((col >> (32 - h + r)) & 1) rows[r] |= uint64_t(1) << (63 - c);
        }
        const Bitmap* b = st.addBitmap(int(w), int(h), std::move(rows));
        seen.emplace_back(addr, b);
        return b;
    }
    template <size_t N> void run(const Bitmap* out[N], uint32_t first, uint32_t stride, int w = 0, int h = 0)
    {
        for (size_t i = 0; i < N; ++i) out[i] = icon(first + uint32_t(i) * stride, w, h);
    }
    template <size_t N> void table(const Bitmap* out[N], uint32_t tableAt)
    {
        for (size_t i = 0; i < N; ++i) out[i] = icon(u32(tableAt + 4 * uint32_t(i)));
    }

    Font font(uint32_t addr, int wantH)
    {
        const uint32_t adv = u32(addr), h = u32(addr + 4), widthsAt = u32(addr + 8), offsetsAt = u32(addr + 12), colsAt = u32(addr + 16);
        if (int(h) != wantH || adv < 1 || adv > 16) bad("not the expected font", addr);
        const uint8_t* widths = at(widthsAt, 256);
        const uint8_t* offsets = at(offsetsAt, 512);
        st.fonts.emplace_back();
        auto& fd = st.fonts.back();
        fd.index.fill(-1);
        int have = 0;
        for (int c = 0; c < 128; ++c) {
            const int off = int16_t((offsets[2 * c] << 8) | offsets[2 * c + 1]);
            if (off < 0) continue;
            const uint32_t w = widths[c] ? widths[c] : adv;
            if (w > 16) bad("font glyph too wide", addr);
            const uint8_t* cols = at(colsAt + uint32_t(off), w);
            std::vector<uint64_t> rows(h, 0);
            for (uint32_t x = 0; x < w; ++x)
                for (uint32_t r = 0; r < h; ++r)
                    if ((cols[x] >> (8 - h + r)) & 1) rows[r] |= uint64_t(1) << (63 - x);
            fd.index[size_t(c)] = int16_t(fd.glyphs.size());
            fd.glyphs.push_back(Bitmap{uint8_t(w), uint8_t(h), st.addRows(std::move(rows))});
            ++have;
        }
        if (have < 10) bad("font without glyphs", addr);
        return Font{uint8_t(h), uint8_t(adv), fd.glyphs.data(), fd.index.data()};
    }
};

// The emblem of a lettering-plus-emblem logo: what lies right of the last blank column inside the lit area.
Bounds emblemBounds(const Bitmap& b)
{
    Bounds lb = litBounds(b);
    int cut = lb.x;
    for (int x = lb.x; x < lb.x + lb.w; ++x) {
        bool blank = true;
        for (int y = 0; y < b.h && blank; ++y) blank = !b.lit(x, y);
        if (blank) cut = x + 1;
    }
    int y0 = b.h, y1 = -1;
    for (int y = 0; y < b.h; ++y)
        for (int x = cut; x < lb.x + lb.w; ++x)
            if (b.lit(x, y)) { y0 = std::min(y0, y); y1 = std::max(y1, y); }
    return y1 < 0 ? Bounds{} : Bounds{cut, y0, lb.x + lb.w - cut, y1 - y0 + 1};
}

// A copy of the emblem alone.
const Bitmap* cropEmblem(Store& st, const Bitmap& logo)
{
    const Bounds em = emblemBounds(logo);
    if (em.w <= 0) return nullptr;
    std::vector<uint64_t> rows(size_t(em.h), 0);
    for (int y = 0; y < em.h; ++y)
        for (int x = 0; x < em.w; ++x)
            if (logo.lit(em.x + x, em.y + y)) rows[size_t(y)] |= uint64_t(1) << (63 - x);
    return st.addBitmap(em.w, em.h, std::move(rows));
}

struct Installed { std::string path; std::uintmax_t size = 0; std::filesystem::file_time_type mtime{}; bool valid = false; };
Installed& installed() { static Installed i; return i; }

} // namespace

Art& art()
{
    static Art a = [] { Art x; buildStandIn(x); return x; }();
    return a;
}

bool installRomArt(const std::vector<uint8_t>& mainOs, std::string* error)
{
    auto store = std::make_unique<Store>();
    Art a;
    try {
        Reader rd{mainOs, *store, {}};
        a.bold8 = rd.font(kFontBold8At, 8);
        a.small4x5 = rd.font(kFontSmall4x5At, 5);
        a.tiny3x5 = rd.font(kFontTiny3x5At, 5);
        a.square5x5 = rd.font(kFontSquare5x5At, 5);
        a.digitsTop = rd.font(kFontDigitsTopAt, 5);
        a.digitsBottom = rd.font(kFontDigitsBottomAt, 5);
        a.dialRing = *rd.icon(kDialRingAt, 11, 13);
        a.ringPlain = *rd.icon(kRingPlainAt, 11, 11);
        a.groupTie = *rd.icon(kGroupTieAt, 7, 3);
        rd.run<128>(a.dialDot, kDialDotAt, kDescSize, 7, 7);
        rd.run<8>(a.iconLfoDest, kLfoDestAt, kDescSize, 17, 11);
        const Bitmap* waves[11];
        rd.run<11>(waves, kLfoWaveAt, kLfoWaveStride, 17, 9);
        for (int i = 0; i < 11; ++i) a.iconLfoWave[i] = waves[kLfoWaveImagePos[i]];
        rd.table<2>(a.iconToggle, kToggleTable);
        rd.table<24>(a.iconFmRatio, kFmRatioTable);
        rd.table<33>(a.iconEnsPitch, kEnsPitchTable);
        rd.table<128>(a.iconFmDynFrq, kFmDynFrqTable);
        rd.table<5>(a.iconSidWave, kSidWaveTable);
        rd.table<3>(a.iconDproSync, kDproSyncTable);
        rd.table<32>(a.iconDproWave, kDproWaveTable);
        rd.table<21>(a.iconVoCons, kVoConsTable);
        rd.table<64>(a.iconDdrwWave, kDdrwWaveTable);
        rd.table<9>(a.iconLfoPage, kLfoPageTable);
        try { rd.table<5>(a.groupLogo, kGroupLogoTable); }   // decoration: the UI has a text title for every group
        catch (const std::exception&) { for (auto& l : a.groupLogo) l = nullptr; }
        if (a.groupLogo[0])   // SWAVE: keep the wave, the UI sets the name in type (groupLogoWords)
            if (const Bitmap* c = cropEmblem(*store, *a.groupLogo[0])) a.groupLogo[0] = c;
    } catch (const std::exception& e) {
        if (error) *error = std::string("LCD artwork not found in this OS file (") + e.what() + "); OS 1.32B is the supported version";
        return false;
    }
    a.fromRom = true;
    stores().push_back(std::move(store));
    art() = a;
    return true;
}

const Bitmap* groupLogo(const char* group)
{
    for (int i = 0; i < 5; ++i) if (std::strcmp(group, kGroupLogoNames[i]) == 0) return art().groupLogo[i];
    return nullptr;
}

const char* const* groupLogoWords(const char* group)
{
    static const char* const kSuperWave[2] = {"SUPER", "WAVE"};
    return std::strcmp(group, kGroupLogoNames[0]) == 0 && art().groupLogo[0] ? kSuperWave : nullptr;
}

Bounds litBounds(const Bitmap& b)
{
    int x0 = b.w, y0 = b.h, x1 = -1, y1 = -1;
    for (int y = 0; y < b.h; ++y)
        for (int x = 0; x < b.w; ++x)
            if (b.lit(x, y)) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
    return x1 < 0 ? Bounds{} : Bounds{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

bool ensureRomArt(const std::filesystem::path& osFile, std::string* error)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(osFile, ec);
    if (ec) { if (error) *error = "cannot read " + osFile.string(); return false; }
    const auto mtime = std::filesystem::last_write_time(osFile, ec);
    auto& in = installed();
    if (in.valid && in.path == osFile.string() && in.size == size && in.mtime == mtime) return true;
    try {
        if (!installRomArt(fw::loadMainOs(osFile), error)) return false;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
    in = Installed{osFile.string(), size, mtime, true};
    return true;
}

} // namespace mnm::uispec
