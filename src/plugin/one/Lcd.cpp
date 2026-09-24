#include "Lcd.h"
#include <cmath>

namespace mnm::plugin::one {

void LcdCanvas::blit(const spec::Bitmap& b, int x, int y, bool on)
{
    for (int r = 0; r < b.h; ++r)
        for (int c = 0; c < b.w; ++c)
            if (b.lit(c, r)) set(x + c, y + r, on);
}

void LcdCanvas::fillRect(int x, int y, int w, int h, bool on)
{
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) set(x + c, y + r, on);
}

void LcdCanvas::invertRect(int x, int y, int w, int h)
{
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) set(x + c, y + r, !get(x + c, y + r));
}

void LcdCanvas::dotsH(int x0, int x1, int y) { for (int x = x0; x <= x1; x += 2) set(x, y); }
void LcdCanvas::dotsV(int x, int y0, int y1) { for (int y = y0; y <= y1; y += 2) set(x, y); }

int LcdCanvas::textWidth(const spec::Font& f, const char* s)
{
    int w = 0;
    for (const char* c = s; *c; ++c) {
        const auto* g = f.glyph(static_cast<unsigned char>(*c));
        w += (g ? g->w : f.adv) + 1;
    }
    return w > 0 ? w - 1 : 0;
}

void LcdCanvas::text(const spec::Font& f, const char* s, int x, int y, bool on)
{
    for (const char* c = s; *c; ++c) {
        const auto* g = f.glyph(static_cast<unsigned char>(*c));
        if (g) { blit(*g, x, y, on); x += g->w + 1; }
        else x += f.adv + 1;
    }
}

void LcdCanvas::tallDigits(const char* s, int x, int y, bool on)
{
    text(spec::kFontDigitsTop, s, x, y, on);
    text(spec::kFontDigitsBottom, s, x, y + spec::kFontDigitsTop.h, on);
}

void LcdCanvas::draw(juce::Graphics& g, int destX, int destY, int scale) const
{
    juce::Image img(juce::Image::ARGB, m_w, m_h, false);
    {
        juce::Image::BitmapData bd(img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < m_h; ++y)
            for (int x = 0; x < m_w; ++x)
                bd.setPixelColour(x, y, m_px[size_t(y * m_w + x)] ? lcd::ink : lcd::paper);
    }
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    g.drawImageTransformed(img, juce::AffineTransform::scale(float(scale)).translated(float(destX), float(destY)));
}

// ---------------------------------------------------------------------------

bool loadLcdArt(const juce::String& osPath)
{
    return osPath.isNotEmpty() && spec::ensureRomArt(std::filesystem::path(osPath.toStdString()));
}

juce::String valueText(const spec::Param& p, int raw)
{
    raw = juce::jlimit(0, int(p.maxRaw), raw);
    switch (p.display) {
    case spec::Display::List:
    case spec::Display::Readout: return juce::String(spec::valueName(p, raw));
    case spec::Display::Bipolar: { const int b = raw - 64; return b > 0 ? "+" + juce::String(b) : juce::String(b); }
    default: return juce::String(raw);
    }
}

// Rotary switch for an N-position list without its own glyphs: the plain ring with a pointer line from
// the centre to position idx, swept like the dial (first entry lower-left, last lower-right).
static void drawSwitch(LcdCanvas& cv, int x, int y, int idx, int n)
{
    cv.blit(spec::kRingPlain, x, y);
    const double frac = n > 1 ? double(idx) / double(n - 1) : 0.5;
    const double a = juce::degreesToRadians(-150.0 + 300.0 * frac);   // clockwise from 12 o'clock
    const int cx = x + spec::kRingPlain.w / 2, cy = y + spec::kRingPlain.h / 2;
    for (int t = 1; t <= 4; ++t)
        cv.set(cx + int(std::lround(t * std::sin(a))), cy - int(std::lround(t * std::cos(a))));
}

void drawKnobCell(LcdCanvas& cv, int x0, int y0, const spec::Param& p, int raw, bool highlightValue, const spec::Bitmap* iconOverride)
{
    raw = juce::jlimit(0, int(p.maxRaw), raw);
    // dotted top and left edge (the grid's right/bottom edges are the neighbours' left/top)
    cv.dotsH(x0, x0 + kCell, y0);
    cv.dotsV(x0, y0, y0 + kCell - 1);
    if (p.display == spec::Display::Blank) return;   // nothing at all, not even the label

    const auto& font = spec::kFontTiny3x5;
    const int innerX = x0 + 1, innerW = kCell - 1;
    cv.textCentred(font, p.label, innerX, innerW, y0 + kLabelY);

    if (p.display == spec::Display::Numeric || p.display == spec::Display::Bipolar) {
        const int rx = innerX + (innerW - spec::kDialRing.w) / 2, ry = y0 + kContentY + (kContentH - spec::kDialRing.h) / 2;
        cv.blit(spec::kDialRing, rx, ry);
        cv.blit(*spec::kDialDot[raw], rx + kDotX, ry + kDotY);
    } else {
        const int idx = p.display == spec::Display::List ? spec::listIndex(raw, p.valueCount) : raw;
        if (p.icons == spec::Icons::Switch)
            drawSwitch(cv, innerX + (innerW - spec::kRingPlain.w) / 2, y0 + kContentY + (kContentH - spec::kRingPlain.h) / 2, idx, p.valueCount);
        else if (const auto* ic = iconOverride ? iconOverride : spec::icon(p.icons, idx))
            cv.blit(*ic, innerX + (innerW - ic->w) / 2, y0 + kContentY + (kContentH - ic->h) / 2);
        else if (p.display == spec::Display::List)   // no OS file yet: the icon families are not available
            drawSwitch(cv, innerX + (innerW - spec::kRingPlain.w) / 2, y0 + kContentY + (kContentH - spec::kRingPlain.h) / 2, idx, p.valueCount);
    }
    // value row: the hardware prints no number in a dial cell, but the One shows every value here
    const auto box = knobValueBox(x0, y0);
    const juce::String value = valueText(p, raw);
    cv.textCentred(font, value.toRawUTF8(), box.getX(), box.getWidth(), box.getY() + (box.getHeight() - font.h) / 2);
    if (highlightValue) cv.invertRect(box.getX(), box.getY(), box.getWidth(), box.getHeight());
    if (p.tieRight) cv.blit(spec::kGroupTie, x0 + kCell - 3, y0 - 1);   // arch straddling the right divider
}

void drawLoopGlyph(LcdCanvas& cv, int x, int y, bool active, bool on)
{
    static const char* rows[7] = {"..####...", ".#....#..", "#......#.", "#...#####", "#....###.", ".#....#..", "..###...."};   // circular arrow, head pointing down into the gap
    if (active) { cv.fillRect(x - 1, y - 1, kLoopGlyphW + 2, kLoopGlyphH + 2, on); on = !on; }
    for (int r = 0; r < kLoopGlyphH; ++r)
        for (int c = 0; c < kLoopGlyphW; ++c)
            if (rows[r][c] == '#') cv.set(x + c, y + r, on);
}

void drawLcdText(juce::Graphics& g, const spec::Font& f, const char* s, int x, int y, int scale, juce::Colour colour)
{
    const int w = LcdCanvas::textWidth(f, s), h = f.h;
    if (w <= 0) return;
    LcdCanvas cv(w, h);
    cv.text(f, s, 0, 0);
    g.setColour(colour);
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c)
            if (cv.get(c, r)) g.fillRect(x + c * scale, y + r * scale, scale, scale);
}

std::vector<juce::String> wrapLcdText(const spec::Font& f, const juce::String& text, int maxWidth)
{
    std::vector<juce::String> lines;
    juce::String line;
    for (const auto& word : juce::StringArray::fromTokens(text, " ", "")) {
        if (word.isEmpty()) continue;
        const juce::String candidate = line.isEmpty() ? word : line + " " + word;
        if (line.isEmpty() || LcdCanvas::textWidth(f, candidate.toRawUTF8()) <= maxWidth) line = candidate;
        else { lines.push_back(line); line = word; }
    }
    if (line.isNotEmpty()) lines.push_back(line);
    return lines;
}

namespace {
struct LogoLayout { const spec::Bitmap* art = nullptr; spec::Bounds lb; int px = 0, textPx = 0, textW = 0, gap = 0; const char* const* words = nullptr; };

LogoLayout layoutGroupLogo(const char* group, int height)
{
    LogoLayout l;
    l.art = spec::groupLogo(group);
    if (!l.art) return l;
    l.lb = spec::litBounds(*l.art);
    if (l.lb.h <= 0) { l.art = nullptr; return l; }
    l.px = juce::jmax(1, int(std::lround(double(height) / double(juce::jmax(9, l.lb.h)))));
    l.words = spec::groupLogoWords(group);
    if (l.words) {
        l.textPx = juce::jmax(1, int(std::lround(l.px * 2.0 / 3.0)));
        l.textW = juce::jmax(LcdCanvas::textWidth(spec::kFontSmall4x5, l.words[0]), LcdCanvas::textWidth(spec::kFontSmall4x5, l.words[1])) * l.textPx;
        l.gap = l.px * 3 / 2;
    }
    return l;
}
}

int groupLogoWidth(const char* group, int height)
{
    const auto l = layoutGroupLogo(group, height);
    return l.art ? l.textW + l.gap + l.lb.w * l.px : 0;
}

void drawGroupLogo(juce::Graphics& g, const char* group, int x, int yTop, int height, juce::Colour colour)
{
    const auto l = layoutGroupLogo(group, height);
    if (!l.art) return;
    if (l.words) {
        const auto& f = spec::kFontSmall4x5;
        const int lineGap = 2, blockH = (2 * f.h + lineGap) * l.textPx, ty = yTop + (height - blockH) / 2;
        drawLcdText(g, f, l.words[0], x, ty, l.textPx, colour);
        drawLcdText(g, f, l.words[1], x, ty + (f.h + lineGap) * l.textPx, l.textPx, colour);
    }
    const int ax = x + l.textW + l.gap, ay = yTop + (height - l.lb.h * l.px) / 2;
    g.setColour(colour);
    for (int r = 0; r < l.lb.h; ++r)
        for (int c = 0; c < l.lb.w; ++c)
            if (l.art->lit(l.lb.x + c, l.lb.y + r)) g.fillRect(ax + c * l.px, ay + r * l.px, l.px, l.px);
}

} // namespace mnm::plugin::one
