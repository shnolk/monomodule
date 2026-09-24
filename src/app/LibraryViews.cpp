#include "LibraryViews.h"
#include "Store.h"
#include <cmath>
#include <set>

namespace mnm::app {

using namespace mnm::dump;

// ---------------------------------------------------------------------------
// Shared helpers

void drawTitleBar(LcdCanvas& cv, int x0, int y0, int w, const char* text, const char* rightText, int rightInset)
{
    cv.fillRect(x0, y0, w, 10, true);
    cv.text(spec::kFontBold8, text, x0 + 2, y0 + 1, false);
    if (rightText && *rightText) cv.text(spec::kFontBold8, rightText, x0 + w - 2 - rightInset - LcdCanvas::textWidth(spec::kFontBold8, rightText), y0 + 1, false);
}

void drawStopAndLoop(LcdCanvas& cv, int x, int y, bool on)
{
    drawPlayGlyph(cv, x, y, true, on);
    if (ui::transport().shown()) drawLoopGlyph(cv, x - kPlayZone + 1, y, ui::transport().looping(), on);
}

void drawPlayGlyph(LcdCanvas& cv, int x, int y, bool playing, bool on)
{
    if (playing) { cv.fillRect(x, y + 1, kPlayW + 1, kPlayH - 2, on); return; }   // stop: a 5x5 square
    for (int c = 0; c < kPlayW; ++c) cv.fillRect(x + c, y + c, 1, kPlayH - 2 * c, on);   // a triangle pointing right
}

namespace ui {

juce::Font font(bool bold, float px) { return juce::Font(juce::FontOptions(px, bold ? juce::Font::bold : juce::Font::plain)); }

int width(const juce::String& s, const juce::Font& f) { return juce::GlyphArrangement::getStringWidthInt(f, s); }

int text(juce::Graphics& g, const juce::String& s, int x, int y, int h, juce::Colour c, const juce::Font& f)
{
    const int w = width(s, f) + 2;
    g.setColour(c);
    g.setFont(f);
    g.drawText(s, x, y, w, h, juce::Justification::centredLeft, false);
    return x + w;
}

int textRight(juce::Graphics& g, const juce::String& s, int right, int y, int h, juce::Colour c, const juce::Font& f)
{
    const int w = width(s, f) + 2;
    g.setColour(c);
    g.setFont(f);
    g.drawText(s, right - w, y, w, h, juce::Justification::centredRight, false);
    return right - w;
}

int labelled(juce::Graphics& g, int x, int y, int h, const juce::String& label, const juce::String& value, juce::Colour c)
{
    x = text(g, label, x, y, h, c, font(true));
    return text(g, value, x + 4, y, h, c);
}

juce::String noteName(int n)
{
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (n < 0 || n > 127) return "-";
    return juce::String(names[n % 12]) + juce::String(n / 12 - 1);
}

} // namespace ui

juce::String machineDisplayName(uint8_t model)
{
    if (const auto* m = spec::machineByIndex(model)) return m->displayName;
    return "MODEL " + juce::String(int(model));
}

static bool modelIsFx(uint8_t model)
{
    const auto* m = spec::machineByIndex(model);
    return m && m->isFx;
}

const char* kitPageTitle(int pageIndex)
{
    static const char* titles[7] = {"SYNTHESIS", "AMPLIFICATION", "FILTER", "EFFECTS", "LFO1", "LFO2", "LFO3"};
    return pageIndex >= 0 && pageIndex < 7 ? titles[pageIndex] : "";
}

bool kitPageParams(const KitTrack& track, int pageIndex, spec::Param out[8])
{
    if (pageIndex == 0) {
        const auto* m = spec::machineByIndex(track.model);
        if (!m) return false;
        for (int k = 0; k < 8; ++k) out[k] = m->params[k];
        return true;
    }
    if (pageIndex >= 1 && pageIndex <= 3) {
        const auto& pg = spec::kSharedPages[pageIndex - 1];
        for (int k = 0; k < 8; ++k)
            out[k] = {pg.labels[k], ((pg.bipolarMask >> k) & 1) ? spec::Display::Bipolar : spec::Display::Numeric,
                      false, pg.defaults[k], 127, 128, spec::Icons::None, nullptr};
        return true;
    }
    if (pageIndex >= 4 && pageIndex <= 6) {
        const int lfo = pageIndex - 4;
        for (int k = 0; k < 8; ++k) out[k] = spec::kLfoParams[k];
        const int pageIdx = spec::listIndex(track.params[32 + 8 * lfo], 9);
        out[1].values = spec::kLfoDestNames[pageIdx];   // DEST names follow the PAGE knob
        return true;
    }
    return false;
}

juce::String kitParamName(const KitTrack& track, int p)
{
    spec::Param pp[8];
    if (p >= 0 && p < 56 && kitPageParams(track, p / 8, pp)) return juce::String(kitPageTitle(p / 8)).substring(0, 3) + " " + pp[p % 8].label;
    if (p >= 56 && p < 64) return "MIDI " + juce::String(p - 55);   // the kit's MIDI page (the MIDI sequencer track's parameters)
    return "P" + juce::String(p);
}

void drawPage(LcdCanvas& cv, int x0, int y0, const char* title, const spec::Param* params8, const uint8_t* raw8)
{
    drawTitleBar(cv, x0, y0, kPageW, title);
    for (int k = 0; k < 8; ++k) drawKnobCell(cv, x0 + (k % 4) * kCell, y0 + 11 + (k / 4) * kCell, params8[k], raw8[k]);
    cv.dotsV(x0 + kPageW - 1, y0 + 11, y0 + kPageH - 1);
    cv.dotsH(x0, x0 + kPageW - 1, y0 + kPageH - 1);
}

// ---------------------------------------------------------------------------
// LcdChip

LcdChip::LcdChip(const juce::String& text, const spec::Font& font, int scale, bool toggles) : juce::Button(text), m_font(font), m_scale(scale)
{
    setClickingTogglesState(toggles);
}

void LcdChip::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const int w = getWidth() / m_scale, h = getHeight() / m_scale;
    LcdCanvas cv(w, h);
    // emphasised = a solid block (a toggle that is on; a push button, unless it is a ghost), else a dotted frame.
    // On an inverted bar paper and ink swap roles.
    const bool emph = getClickingTogglesState() ? getToggleState() : m_ghost ? down : !down;
    const bool fillInk = m_onDark ? !emph : emph;
    if (fillInk) cv.fillRect(0, 0, w, h, true);
    if (!emph) {
        for (int x = 0; x < w; x += 2) { cv.set(x, 0, !fillInk); cv.set(x, h - 1, !fillInk); }
        for (int y = 0; y < h; y += 2) { cv.set(0, y, !fillInk); cv.set(w - 1, y, !fillInk); }
    }
    cv.textCentred(m_font, getButtonText().toRawUTF8(), 0, w, (h - m_font.h) / 2, !fillInk);
    cv.draw(g, 0, 0, m_scale);
    if (!isEnabled()) { g.setColour((m_onDark ? lcd::ink : lcd::paper).withAlpha(0.7f)); g.fillRect(getLocalBounds()); }
    else if (highlighted && !down) { g.setColour((m_onDark ? lcd::paper : lcd::ink).withAlpha(0.15f)); g.fillRect(getLocalBounds()); }
}

// ---------------------------------------------------------------------------
// CardGrid, CardSection

namespace ui {
void dottedFrame(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour c)
{
    g.setColour(c);
    const int d = 2;   // dot size; every other dot
    for (int x = r.getX(); x < r.getRight() - 1; x += 2 * d) { g.fillRect(x, r.getY(), d, d); g.fillRect(x, r.getBottom() - d, d, d); }
    for (int y = r.getY(); y < r.getBottom() - 1; y += 2 * d) { g.fillRect(r.getX(), y, d, d); g.fillRect(r.getRight() - d, y, d, d); }
}
void playGlyph(juce::Graphics& g, juce::Rectangle<int> zone, bool playing, juce::Colour c, int s)
{
    g.setColour(c);
    const int x = zone.getCentreX() - (kPlayW * s) / 2, y = zone.getCentreY() - (kPlayH * s) / 2;
    if (playing) { g.fillRect(x, y + s, (kPlayW + 1) * s, (kPlayH - 2) * s); return; }
    for (int col = 0; col < kPlayW; ++col) g.fillRect(x + col * s, y + col * s, s, (kPlayH - 2 * col) * s);
}
void loopGlyph(juce::Graphics& g, juce::Rectangle<int> zone, bool active, juce::Colour c, int s)
{
    LcdCanvas cv(kLoopGlyphW + 2, kLoopGlyphH + 2);
    drawLoopGlyph(cv, 1, 1, active, true);
    g.setColour(c);
    const int x = zone.getCentreX() - (kLoopGlyphW + 2) * s / 2, y = zone.getCentreY() - (kLoopGlyphH + 2) * s / 2;
    for (int r = 0; r < cv.height(); ++r)
        for (int col = 0; col < cv.width(); ++col)
            if (cv.get(col, r)) g.fillRect(x + col * s, y + r * s, s, s);
}
Transport& transport() { static Transport t; return t; }

int textHeight(const juce::String& s, const juce::Font& f, int width)
{
    juce::AttributedString as; as.append(s, f); as.setWordWrap(juce::AttributedString::WordWrap::byWord);
    juce::TextLayout tl; tl.createLayout(as, float(width));
    return int(std::ceil(tl.getHeight())) + 2;
}
void wrapped(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> area, juce::Colour c, const juce::Font& f)
{
    juce::AttributedString as; as.append(s, f, c); as.setWordWrap(juce::AttributedString::WordWrap::byWord);
    juce::TextLayout tl; tl.createLayout(as, float(area.getWidth()));
    tl.draw(g, area.toFloat());
}
}

int CardGrid::heightFor(int width) const
{
    if (m_cards.empty()) return 0;
    const int cols = columns(width), rows = (int(m_cards.size()) + cols - 1) / cols;
    return 2 * kPad + rows * kCardH + (rows - 1) * kGap;
}

juce::Rectangle<int> CardGrid::cardBounds(int i) const
{
    const int cols = columns(getWidth());
    const int w = (getWidth() - 2 * kPad - (cols - 1) * kGap) / cols;
    return {kPad + (i % cols) * (w + kGap), kPad + (i / cols) * (kCardH + kGap), w, kCardH};
}

int CardGrid::cardAt(juce::Point<int> p) const
{
    for (int i = 0; i < int(m_cards.size()); ++i) if (cardBounds(i).contains(p)) return i;
    return -1;
}

void CardGrid::paint(juce::Graphics& g)
{
    const auto clip = g.getClipBounds();
    for (int i = 0; i < int(m_cards.size()); ++i) {
        const auto r = cardBounds(i);
        if (!r.intersects(clip)) continue;
        const auto& c = m_cards[size_t(i)];
        if (i == m_hover) { g.setColour(lcd::ink.withAlpha(0.06f)); g.fillRect(r); }
        ui::dottedFrame(g, r, lcd::ink);
        drawLcdText(g, spec::kFontSmall4x5, c.label.toUpperCase().toRawUTF8(), r.getX() + 7, r.getY() + 7, 2, lcd::ink);
        if (c.playKey.isNotEmpty()) {
            const bool playing = c.playKey == m_playingKey;
            ui::playGlyph(g, glyphZone(r), playing, lcd::ink);
            if (playing && ui::transport().shown()) ui::loopGlyph(g, loopZone(r), ui::transport().looping(), lcd::ink);
        }
        g.setColour(lcd::ink);
        g.setFont(ui::font(false, ui::kSmallPx));
        g.drawText(c.name, r.getX() + 7, r.getY() + 22, r.getWidth() - 12, 16, juce::Justification::centredLeft, true);
        g.setColour(lcd::ink.withAlpha(0.65f));
        g.setFont(ui::font(false, ui::kTinyPx + 1));
        g.drawText(c.metaLeft, r.getX() + 7, r.getBottom() - 19, r.getWidth() - 14, 14, juce::Justification::centredLeft, true);
        g.drawText(c.metaRight, r.getX() + 7, r.getBottom() - 19, r.getWidth() - 14, 14, juce::Justification::centredRight, true);
    }
}

void CardGrid::mouseDown(const juce::MouseEvent& e) { m_pressed = cardAt(e.getPosition()); m_dragging = false; }

void CardGrid::mouseUp(const juce::MouseEvent& e)
{
    const int i = cardAt(e.getPosition());
    if (m_dragging || i < 0 || i != m_pressed) return;
    const auto card = m_cards[size_t(i)];   // a copy: the callback may rebuild the grid
    if (card.playKey.isNotEmpty() && glyphZone(cardBounds(i)).contains(e.getPosition())) { if (onPlay) onPlay(card); }
    else if (card.playKey.isNotEmpty() && card.playKey == m_playingKey && ui::transport().shown() && loopZone(cardBounds(i)).contains(e.getPosition())) { ui::transport().toggleLoop(); repaint(); }
    else if (onClick) onClick(card);
}

void CardGrid::mouseDrag(const juce::MouseEvent& e)
{
    if (m_dragging || m_pressed < 0 || e.getDistanceFromDragStart() < 8 || !onDragStart) return;
    m_dragging = true;
    onDragStart(m_cards[size_t(m_pressed)], this);
}

void CardGrid::mouseMove(const juce::MouseEvent& e)
{
    const int i = cardAt(e.getPosition());
    setMouseCursor(i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (i != m_hover) { m_hover = i; repaint(); }
}

void CardSection::paint(juce::Graphics& g)
{
    const int w = getWidth() / kScale;
    LcdCanvas cv(w, 10);
    drawTitleBar(cv, 0, 0, w, m_title.toRawUTF8(), juce::String(int(grid.cards().size())).toRawUTF8());
    cv.draw(g, 0, 0);
    if (grid.cards().empty()) ui::text(g, m_empty, 3 * kScale, kTitleH + 4, ui::kLineH, lcd::ink.withAlpha(0.6f));
}

// ---------------------------------------------------------------------------
// LcdList

void LcdList::setRows(std::vector<Row> rows, int keepSelectedIndex)
{
    m_rows = std::move(rows);
    m_selected = keepSelectedIndex >= 0 && keepSelectedIndex < int(m_rows.size()) ? keepSelectedIndex : -1;
    m_playingRow = -1;
    clampScroll();
    repaint();
}

void LcdList::setPlayingRow(int index)
{
    index = index >= 0 && index < int(m_rows.size()) ? index : -1;
    if (index != m_playingRow) { m_playingRow = index; repaint(); }
}

void LcdList::select(int index, bool notify)
{
    if (index < 0 || index >= int(m_rows.size()) || !m_rows[size_t(index)].selectable) return;
    m_selected = index;
    scrollTo(index);
    repaint();
    if (notify && onSelect) onSelect(index);
}

void LcdList::scrollTo(int index)
{
    if (index < m_scroll) m_scroll = index;
    else if (index >= m_scroll + visibleRows()) m_scroll = index - visibleRows() + 1;
    clampScroll();
}

void LcdList::clampScroll()
{
    m_scroll = juce::jlimit(0, juce::jmax(0, int(m_rows.size()) - visibleRows()), m_scroll);
}

void LcdList::paint(juce::Graphics& g)
{
    const int w = getWidth() / kScale, h = getHeight() / kScale;
    LcdCanvas cv(w, h);
    const int textW = w - 4;   // the scrollbar column
    const int vis = visibleRows();
    for (int i = m_scroll; i < int(m_rows.size()) && i < m_scroll + vis; ++i) {
        const auto& r = m_rows[size_t(i)];
        const int y = (i - m_scroll) * kRowH;
        const bool sel = i == m_selected;
        if (sel) cv.fillRect(0, y, textW, kRowH, true);
        const bool ink = !sel;
        if (r.expand >= 0) {   // marker: a 5x5 box with a plus (collapsed) or minus (expanded)
            const int bx = r.indent + 2, by = y + 3;
            cv.dotsH(bx, bx + 4, by); cv.dotsH(bx, bx + 4, by + 4); cv.set(bx, by + 2, ink); cv.set(bx + 4, by + 2, ink);
            for (int c = bx + 1; c <= bx + 3; ++c) cv.set(c, by + 2, ink);
            if (r.expand == 0) { cv.set(bx + 2, by + 1, ink); cv.set(bx + 2, by + 3, ink); }
        } else if (!r.selectable) {   // group heading: a dotted rule under it
            cv.dotsH(r.indent + 2, textW - 2, y + kRowH - 1);
        }
        if (r.playable && i == m_playingRow) drawStopAndLoop(cv, textW - 2 - kPlayW - 1, y + (kRowH - kPlayH) / 2, ink);
        else if (r.playable) drawPlayGlyph(cv, textW - 2 - kPlayW - 1, y + (kRowH - kPlayH) / 2, false, ink);
    }
    if (int(m_rows.size()) > vis) {   // dotted rail with a solid thumb
        const int rx = w - 2;
        cv.dotsV(rx, 0, h - 1);
        const int thumbH = juce::jmax(6, h * vis / int(m_rows.size()));
        const int thumbY = (h - thumbH) * m_scroll / juce::jmax(1, int(m_rows.size()) - vis);
        cv.fillRect(rx - 1, thumbY, 3, thumbH, true);
    }
    cv.draw(g, 0, 0);
    // the text: top-level and group rows bold, items regular; paper on the selected row's inverted bar
    for (int i = m_scroll; i < int(m_rows.size()) && i < m_scroll + vis; ++i) {
        const auto& r = m_rows[size_t(i)];
        const int y = (i - m_scroll) * kRowH * kScale, h2 = kRowH * kScale;
        const auto colour = i == m_selected ? lcd::paper : lcd::ink;
        const int x = (r.indent + 2 + (r.expand >= 0 ? 8 : 0)) * kScale;
        ui::text(g, r.text, x, y, h2, colour, ui::font(r.indent == 0 || r.expand >= 0 || !r.selectable));
        if (r.right.isNotEmpty()) ui::textRight(g, r.right, (textW - 2 - (r.playable ? playInset(i == m_playingRow) : 0)) * kScale, y, h2, colour, ui::font(false, ui::kSmallPx));
    }
}

void LcdList::mouseDrag(const juce::MouseEvent& e)
{
    if (m_dragging || m_pressedRow < 0 || e.getDistanceFromDragStart() < 8) return;
    m_dragging = true;
    if (onDragStart) onDragStart(m_pressedRow);
}

void LcdList::mouseDown(const juce::MouseEvent& e)
{
    m_dragging = false;
    m_pressedRow = -1;
    const int i = m_scroll + e.y / (kRowH * kScale);
    if (i < 0 || i >= int(m_rows.size())) return;
    m_pressedRow = i;
    const auto& r = m_rows[size_t(i)];
    const int lcdX = e.x / kScale;
    if (r.playable) {
        const int zone = playZoneAt(getWidth() / kScale - 4 - lcdX, i == m_playingRow);
        if (zone == 1) { m_pressedRow = -1; if (onPlay) onPlay(i); return; }
        if (zone == 2) { m_pressedRow = -1; ui::transport().toggleLoop(); repaint(); return; }
    }
    if (r.expand >= 0 && lcdX < r.indent + 10) { if (onToggle) onToggle(i); return; }
    if (r.expand >= 0 && !r.selectable) { if (onToggle) onToggle(i); return; }
    select(i, true);
}

void LcdList::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int i = m_scroll + e.y / (kRowH * kScale);
    if (i >= 0 && i < int(m_rows.size()) && m_rows[size_t(i)].expand >= 0 && onToggle) onToggle(i);
}

void LcdList::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    m_scroll -= int(std::lround(wheel.deltaY * 6.0f));
    clampScroll();
    repaint();
}

bool LcdList::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey) {
        int i = m_selected;
        do { i += k == juce::KeyPress::upKey ? -1 : 1; } while (i >= 0 && i < int(m_rows.size()) && !m_rows[size_t(i)].selectable);
        if (i >= 0 && i < int(m_rows.size())) select(i, true);
        return true;
    }
    if ((k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey) && m_selected >= 0 && m_rows[size_t(m_selected)].expand >= 0 && onToggle) { onToggle(m_selected); return true; }
    if (k == juce::KeyPress::spaceKey && m_selected >= 0 && m_rows[size_t(m_selected)].playable && onPlay) { onPlay(m_selected); return true; }
    return false;
}

// ---------------------------------------------------------------------------
// LinkList

void LinkList::set(const juce::String& title, std::vector<Row> rows, const juce::String& emptyText)
{
    m_title = title; m_rows = std::move(rows); m_empty = emptyText; m_hover = -1;
    repaint();
}

int LinkList::rowAt(juce::Point<int> p) const
{
    const int i = (p.y - kTitleH - kPad) / kRowH;
    return p.y >= kTitleH + kPad && i >= 0 && i < int(m_rows.size()) && p.x < getWidth() ? i : -1;
}

void LinkList::paint(juce::Graphics& g)
{
    const int w = getWidth() / kScale;
    LcdCanvas cv(w, 10);
    drawTitleBar(cv, 0, 0, w, m_title.toRawUTF8());
    cv.draw(g, 0, 0);
    int y = kTitleH + kPad;
    if (m_rows.empty()) { ui::text(g, m_empty, 3 * kScale, y, kRowH, lcd::ink.withAlpha(0.6f)); return; }
    for (int i = 0; i < int(m_rows.size()); ++i, y += kRowH) {
        const auto& r = m_rows[size_t(i)];
        const int x = 3 * kScale;
        const int end = ui::text(g, r.text, x, y, kRowH, lcd::ink, ui::font(false));
        if (i == m_hover) { g.setColour(lcd::ink); g.fillRect(x, y + kRowH - 4, end - x - 2, 1); }
        if (r.right.isNotEmpty()) ui::textRight(g, r.right, getWidth() - 3 * kScale, y, kRowH, lcd::ink, ui::font(false, ui::kSmallPx));
    }
}

void LinkList::mouseMove(const juce::MouseEvent& e)
{
    const int i = rowAt(e.getPosition());
    setMouseCursor(i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (i != m_hover) { m_hover = i; repaint(); }
}

void LinkList::mouseExit(const juce::MouseEvent&) { if (m_hover != -1) { m_hover = -1; repaint(); } }

void LinkList::mouseDown(const juce::MouseEvent& e)
{
    const int i = rowAt(e.getPosition());
    if (i >= 0 && onClick) onClick(m_rows[size_t(i)].tag);
}

// ---------------------------------------------------------------------------
// KitView

void KitView::set(const Kit& kit, const std::array<juce::String, 6>& presetNames, std::vector<Card> patterns, std::vector<LinkList::Row> dumps)
{
    m_kit = kit; m_presetNames = presetNames; m_hover = -1;
    if (m_patterns.getParentComponent() == nullptr) {
        addAndMakeVisible(m_patterns);
        addAndMakeVisible(m_dumps);
        m_patterns.grid.onClick = [this](const Card& c) { if (onLink) onLink(c.tag); };
        m_dumps.onClick = [this](const juce::var& t) { if (onLink) onLink(t); };
    }
    m_title.clear();
    m_patterns.set("USED IN PATTERNS", std::move(patterns), "No pattern uses this kit.");
    m_dumps.set("FROM", std::move(dumps));
    resized();
    repaint();
}

int KitView::preferredHeight() const
{
    return settingsBottom() + ui::kLineH + 8 + m_patterns.heightFor(getWidth()) + 8 + m_dumps.preferredHeight();
}

void KitView::resized()
{
    int y = settingsBottom() + ui::kLineH + 8;
    m_patterns.setBounds(0, y, getWidth(), m_patterns.heightFor(getWidth()));
    y += m_patterns.getHeight() + 8;
    m_dumps.setBounds(0, y, getWidth(), m_dumps.preferredHeight());
}

juce::Rectangle<int> KitView::trackRow(int t) const
{
    return juce::Rectangle<int>(0, (12 + t * kRowLcdH) * kScale, getWidth(), kRowLcdH * kScale);
}

void KitView::setPlaying(int stem) { if (stem != m_playing) { m_playing = stem; repaint(); } }

void KitView::paint(juce::Graphics& g)
{
    const int w = getWidth() / kScale, h = kSettingsLcdH;
    LcdCanvas cv(w, h);
    const juce::String title = m_title.isNotEmpty() ? m_title.toUpperCase() : "KIT  " + juce::String(m_kit.name).toUpperCase();
    drawTitleBar(cv, 0, 0, w, title.toRawUTF8(), m_kit.isEmptySlot() ? "EMPTY SLOT" : nullptr, playInset(m_playing == -1));
    if (m_playing == -1) drawStopAndLoop(cv, w - 2 - kPlayW - 2, 1, false); else drawPlayGlyph(cv, w - 2 - kPlayW - 2, 1, false, false);
    for (int t = 0; t < 6; ++t) {
        const int y0 = 12 + t * kRowLcdH;
        const auto& tr = m_kit.tracks[t];
        cv.dotsH(0, w - 1, y0); cv.dotsV(0, y0, y0 + kRowLcdH - 1); cv.dotsV(w - 1, y0, y0 + kRowLcdH - 1);
        if (t == 5) cv.dotsH(0, w - 1, y0 + kRowLcdH - 1);
        if (t == m_hover) for (int yy = y0 + 1; yy < y0 + kRowLcdH - 1; yy += 2) for (int xx = 2; xx < w - 2 - playInset(m_playing == t); xx += 2) cv.set(xx, yy);   // light hatch = hover
        if (m_playing == t) drawStopAndLoop(cv, w - 2 - kPlayW - 2, y0 + (kRowLcdH - kPlayH) / 2, true);
        else drawPlayGlyph(cv, w - 2 - kPlayW - 2, y0 + (kRowLcdH - kPlayH) / 2, false, true);
        // LEV: a dotted frame filled to the level (the value is printed beside it below)
        const int lx = 14, ly = y0 + 14, lw = 40;
        cv.dotsH(lx, lx + lw - 1, ly); cv.dotsH(lx, lx + lw - 1, ly + 6); cv.dotsV(lx, ly, ly + 6); cv.dotsV(lx + lw - 1, ly, ly + 6);
        cv.fillRect(lx + 1, ly + 1, (lw - 2) * tr.level / 127, 5, true);
    }
    cv.draw(g, 0, 0);
    const juce::String ownName = juce::String(m_kit.name);
    for (int t = 0; t < 6; ++t) {
        const int y0 = 12 + t * kRowLcdH;
        const auto& tr = m_kit.tracks[t];
        // top line: track number and machine (the preset's name when it was first seen in another kit), then routing and key tracking
        const int topY = (y0 + 2) * kScale, topH = 8 * kScale;
        ui::text(g, juce::String(t + 1), 3 * kScale, topY, topH, lcd::ink, ui::font(true));
        int x = ui::text(g, machineDisplayName(tr.model), 14 * kScale, topY, topH, lcd::ink, ui::font(true));
        const auto& pn = m_presetNames[size_t(t)];
        if (pn.isNotEmpty() && pn != ownName + " T" + juce::String(t + 1)) ui::text(g, "= " + pn, x + 6, topY, topH, lcd::ink.withAlpha(0.7f), ui::font(false, ui::kSmallPx));
        const bool narrow = getWidth() < 640;
        const int colStep = narrow ? 34 * kScale : 44 * kScale;
        x = (narrow ? 84 : 100) * kScale;
        ui::labelled(g, x, topY, topH, "OUT", mnm::library::outBusName(m_kit.outBuses(t))); x += colStep;
        if (modelIsFx(tr.model)) ui::labelled(g, x, topY, topH, "IN", mnm::library::fxInputName(m_kit.fxInput(t)));
        x += colStep;
        if (x + 90 < getWidth() - kPlayZone * kScale) ui::labelled(g, x, topY, topH, "KEY", juce::String(m_kit.lpKeyTracks(t) ? "LPF " : "--- ") + (m_kit.hpKeyTracks(t) ? "HPF" : "---"));
        // bottom line: the level value beside its bar, then the SYN page values as a quick fingerprint of
        // the sound (as many pairs as fit)
        const int lx = 14, ly = y0 + 14, lw = 40;
        const int botY = (ly - 2) * kScale, botH = 11 * kScale;
        ui::labelled(g, (lx + lw + 4) * kScale, botY, botH, "LEV", juce::String(int(tr.level)));
        juce::String syn;
        if (const auto* m = spec::machineByIndex(tr.model))
            for (int k = 0; k < 8; ++k) {
                if (m->params[k].display == spec::Display::Blank) continue;
                const juce::String pair = juce::String(m->params[k].label) + " " + valueText(m->params[k], tr.params[k]);
                if ((narrow ? 84 : 100) * kScale + ui::width(syn + pair + "  ..") > getWidth() - (kPlayZone + 2) * kScale) { syn += ".."; break; }
                syn += pair + "  ";
            }
        ui::text(g, syn, (narrow ? 84 : 100) * kScale, botY, botH);
    }
    // kit settings: three columns on a wide page, two (one more line) in the narrower panes
    const bool narrowPane = narrowLayout();
    const int x1 = 3 * kScale, x2 = narrowPane ? getWidth() / 2 : 110 * kScale, x3 = 220 * kScale;
    int y = kSettingsLcdH * kScale;
    static const char* multimode[4] = {"ALL TRK", "SPLIT KEY", "SEQ START", "SEQ TRANSPOSE"};
    auto onOff = [](uint8_t v) { return v == 255 ? juce::String("OFF") : juce::String(int(v)); };
    juce::String tt;
    for (int t = 0; t < 6; ++t) tt += (m_kit.trigTracks[t] == 255 ? juce::String("-") : juce::String(int(m_kit.trigTracks[t]) + 1)) + " ";
    const juce::String legato = "AMP " + onOff(m_kit.trigLegatoAmp) + "  FLT " + onOff(m_kit.trigLegatoFilter) + "  LFO " + onOff(m_kit.trigLegatoLFO);
    const juce::String mirror = "LR " + juce::String(int(m_kit.mirrorLR)) + "  UD " + juce::String(int(m_kit.mirrorUD));
    ui::labelled(g, x1, y, ui::kLineH, "MULTI MODE", juce::String(multimode[m_kit.commonMultimode & 3]));
    ui::labelled(g, x2, y, ui::kLineH, "SPLIT KEY", juce::String(int(m_kit.splitKey)) + " track " + juce::String(int(m_kit.splitRange)));
    if (!narrowPane) ui::labelled(g, x3, y, ui::kLineH, "TIMING", juce::String(int(m_kit.commonTiming)));
    y += ui::kLineH;
    ui::labelled(g, x1, y, ui::kLineH, "PORTAMENTO", onOff(m_kit.trigPortamento));
    if (narrowPane) ui::labelled(g, x2, y, ui::kLineH, "TIMING", juce::String(int(m_kit.commonTiming))); else ui::labelled(g, x2, y, ui::kLineH, "LEGATO", legato);
    y += ui::kLineH;
    if (narrowPane) { ui::labelled(g, x1, y, ui::kLineH, "LEGATO", legato); y += ui::kLineH; }
    ui::labelled(g, x1, y, ui::kLineH, "TRIG TRACKS", tt);
    ui::labelled(g, x2, y, ui::kLineH, "JOYSTICK MIRROR", mirror);
    y += ui::kLineH + ui::kLineH / 4;
    ui::wrapped(g, "Click a track for its preset, its glyph to hear it in the kit's preview; drag a track or the title bar into the DAW.", {x1, y, getWidth() - 2 * x1, narrowPane ? 2 * ui::kLineH : ui::kLineH}, lcd::ink.withAlpha(0.6f), ui::font(false, ui::kSmallPx));
}

void KitView::mouseDown(const juce::MouseEvent& e)
{
    m_dragging = false;
    const int fromRight = (getWidth() - e.x) / kScale;
    if (e.y < 10 * kScale) {
        const int zone = playZoneAt(fromRight, m_playing == -1);
        if (zone == 1 && onPlayKit) onPlayKit(); else if (zone == 2) { ui::transport().toggleLoop(); repaint(); }
        return;
    }
    for (int t = 0; t < 6; ++t)
        if (trackRow(t).contains(e.getPosition())) {
            const int zone = playZoneAt(fromRight, m_playing == t);
            if (zone == 1) { if (onPlayTrack) onPlayTrack(t); }
            else if (zone == 2) { ui::transport().toggleLoop(); repaint(); }
            else if (onTrack) onTrack(t);
            return;
        }
}

void KitView::mouseDrag(const juce::MouseEvent& e)
{
    if (m_dragging || e.getDistanceFromDragStart() < 8) return;
    m_dragging = true;
    const auto p = e.getMouseDownPosition();
    for (int t = 0; t < 6; ++t)
        if (trackRow(t).contains(p)) { if (onDragTrack) onDragTrack(t); return; }
    if (p.y < 12 * kScale && onDragKit) onDragKit();
}

void KitView::mouseMove(const juce::MouseEvent& e)
{
    int hover = -1;
    for (int t = 0; t < 6; ++t) if (trackRow(t).contains(e.getPosition())) hover = t;
    const bool titlePlay = e.y < 10 * kScale && e.x >= getWidth() - kPlayZone * kScale;
    setMouseCursor(hover >= 0 || titlePlay ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (hover != m_hover) { m_hover = hover; repaint(); }
}

// ---------------------------------------------------------------------------
// TrackView

void TrackView::set(const Kit& kit, int track) { m_kit = kit; m_track = juce::jlimit(0, 5, track); m_title.clear(); m_right.clear(); repaint(); }

void TrackView::mouseDown(const juce::MouseEvent& e)
{
    m_dragging = false;
    switch (playZone(e.getPosition())) { case 1: if (onPlay) onPlay(); break; case 2: ui::transport().toggleLoop(); repaint(); break; default: break; }
}

void TrackView::mouseMove(const juce::MouseEvent& e)
{
    setMouseCursor(playZone(e.getPosition()) != 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void TrackView::mouseDrag(const juce::MouseEvent& e)
{
    if (m_dragging || e.getDistanceFromDragStart() < 8 || playZone(e.getMouseDownPosition()) != 0) return;
    m_dragging = true;
    if (onDragTrack) onDragTrack();
}

void TrackView::paint(juce::Graphics& g)
{
    const int s = kPageScale;
    const int w = getWidth() / s, h = kLcdH;
    LcdCanvas cv(w, h);
    const auto& tr = m_kit.tracks[m_track];
    const juce::String title = m_title.isNotEmpty() ? m_title
        : "KIT " + juce::String(m_kit.position + 1).paddedLeft('0', 3) + "  T" + juce::String(m_track + 1) + "  " + machineDisplayName(tr.model);
    const juce::String right = m_title.isNotEmpty() ? m_right
        : "LEV " + juce::String(int(tr.level)) + "  OUT " + mnm::library::outBusName(m_kit.outBuses(m_track))
          + (modelIsFx(tr.model) ? "  IN " + juce::String(mnm::library::fxInputName(m_kit.fxInput(m_track))) : juce::String())
          + "  KEY " + (m_kit.lpKeyTracks(m_track) ? "LPF " : "--- ") + (m_kit.hpKeyTracks(m_track) ? "HPF" : "---");
    drawTitleBar(cv, 0, 0, w, title.toUpperCase().toRawUTF8(), right.toUpperCase().toRawUTF8(), playInset(m_playing));
    if (m_playing) drawStopAndLoop(cv, w - 2 - kPlayW - 2, 1, false); else drawPlayGlyph(cv, w - 2 - kPlayW - 2, 1, false, false);
    const int gap = 4;
    std::vector<juce::Point<int>> unknownPages;   // LCD px origins of pages the spec cannot describe
    for (int p = 0; p < 7; ++p) {
        spec::Param params[8];
        const int col = p % 3, row = p / 3;
        const int x0 = col * (kPageW + gap), y0 = 13 + row * (kPageH + gap);
        if (!kitPageParams(tr, p, params)) {
            drawTitleBar(cv, x0, y0, kPageW, kitPageTitle(p));
            unknownPages.push_back({x0, y0});
            continue;
        }
        drawPage(cv, x0, y0, kitPageTitle(p), params, tr.params + 8 * p);
    }
    // ASSIGN: the six sources with their two destinations
    const int ax = 1 * (kPageW + gap), ay = 13 + 2 * (kPageH + gap);
    drawTitleBar(cv, ax, ay, 2 * kPageW + gap, "ASSIGN");
    cv.draw(g, 0, 0, s);
    for (const auto& pt : unknownPages) ui::text(g, "Machine not in the UI spec", (pt.x + 3) * s, (pt.y + 12) * s, ui::kLineH);
    int y = (ay + 12) * s;
    for (int src = 0; src < 6; ++src) {
        juce::String line = juce::String(mnm::library::assignSourceName(src)) + "  ";
        for (int slot = 0; slot < 2; ++slot) {
            const int page = tr.destPage[src][slot], param = tr.destParam[src][slot], add = tr.destRange[src][slot];
            const char* pageName = page >= 0 && page < 9 ? spec::kLfoPageNames[page] : "?";
            juce::String dest;
            if (page == 0) dest = juce::String(spec::kLfoDestNames[0][param & 7]);
            else if (page >= 1 && page <= 7) { spec::Param pp[8]; dest = kitPageParams(tr, page - 1, pp) ? juce::String(pp[param & 7].label) : juce::String(param); }
            else dest = "CC " + juce::String(param);
            line += juce::String(pageName) + " " + dest + " " + (add >= 0 ? "+" : "") + juce::String(add) + (slot == 0 ? "   |   " : "");
        }
        ui::text(g, line, (ax + 3) * s, y, ui::kLineH);
        y += ui::kLineH;
    }
}

// ---------------------------------------------------------------------------
// PresetView

PresetView::PresetView()
{
    for (auto* c : {&m_params, &m_fav, &m_tag}) addAndMakeVisible(*c);
    m_fav.setGhost(true); m_tag.setGhost(true);
    m_params.onClick = [this] { if (onParams) onParams(); };
    m_fav.onClick = [this] { if (onFavourite) onFavourite(); };
    m_tag.onClick = [this] { if (onTag) onTag(); };
    for (auto* sct : {&m_versions, &m_kits, &m_patterns}) {
        addAndMakeVisible(*sct);
        sct->grid.onClick = [this](const Card& c) { if (onLink) onLink(c.tag); };
    }
}

void PresetView::set(const mnm::catalog::PresetItem& preset, const juce::String& source, const juce::StringArray& tags, bool favourite,
                     std::vector<Card> versions, std::vector<Card> kits, std::vector<Card> patterns)
{
    m_preset = preset; m_source = source; m_tags = tags; m_favourite = favourite;
    m_fav.setButtonText(favourite ? "FAVOURITE *" : "FAVOURITE");
    m_hasVersions = !versions.empty();
    m_versions.setVisible(m_hasVersions);
    m_versions.set("VERSIONS", std::move(versions), "");
    m_kits.set("IN KITS", std::move(kits), "Not part of a kit: saved from a plugin.");
    m_patterns.set("USED IN PATTERNS", std::move(patterns), "No pattern uses a kit with this preset.");
    resized();
    repaint();
}

int PresetView::preferredHeight() const
{
    const int w = getWidth();
    return sectionsTop() + (m_hasVersions ? m_versions.heightFor(w) + 8 : 0) + m_kits.heightFor(w) + 8 + m_patterns.heightFor(w) + 8;
}

void PresetView::resized()
{
    int x = 3 * kScale;
    for (auto* c : {&m_params, &m_fav, &m_tag}) { c->setBounds(x, kChipsTop, c->preferredWidth(), c->preferredHeight()); x += c->getWidth() + 8; }
    int y = sectionsTop();
    const int w = getWidth();
    if (m_hasVersions) { m_versions.setBounds(0, y, w, m_versions.heightFor(w)); y += m_versions.getHeight() + 8; }
    m_kits.setBounds(0, y, w, m_kits.heightFor(w)); y += m_kits.getHeight() + 8;
    m_patterns.setBounds(0, y, w, m_patterns.heightFor(w));
}

void PresetView::paint(juce::Graphics& g)
{
    const int w = getWidth() / kScale;
    LcdCanvas cv(w, 10);
    drawTitleBar(cv, 0, 0, w, ("PRESET  " + juce::String(m_preset.name).toUpperCase()).toRawUTF8(), machineDisplayName(m_preset.model).toUpperCase().toRawUTF8(), playInset(m_playing));
    if (m_playing) drawStopAndLoop(cv, w - 2 - kPlayW - 2, 1, false); else drawPlayGlyph(cv, w - 2 - kPlayW - 2, 1, false, false);
    cv.draw(g, 0, 0);
    const auto& t = m_preset.track;
    const int x = 3 * kScale;
    int y = kSummaryTop;
    int xx = ui::labelled(g, x, y, ui::kLineH, "MACHINE", machineDisplayName(m_preset.model));
    xx = ui::labelled(g, xx + 18, y, ui::kLineH, "SOURCE", m_source);
    ui::labelled(g, xx + 18, y, ui::kLineH, "TAGS", m_tags.isEmpty() ? juce::String("none") : m_tags.joinIntoString(", "));
    y += ui::kLineH;
    juce::String syn;   // the SYN page in one line, as the kit page prints it
    if (const auto* m = spec::machineByIndex(m_preset.model))
        for (int k = 0; k < 8; ++k) {
            if (m->params[k].display == spec::Display::Blank) continue;
            const juce::String pair = juce::String(m->params[k].label) + " " + valueText(m->params[k], t.params[k]);
            if (x + 40 + ui::width(syn + pair + "  ..") > getWidth() - 2 * kScale) { syn += ".."; break; }
            syn += pair + "   ";
        }
    ui::labelled(g, x, y, ui::kLineH, "SYN", syn.isEmpty() ? juce::String("-") : syn);
    y += ui::kLineH;
    int lfos = 0;
    for (int l = 0; l < 3; ++l) if (t.params[32 + 8 * l + 7] > 0) ++lfos;   // DPTH > 0
    xx = ui::labelled(g, x, y, ui::kLineH, "LEVEL", juce::String(int(t.level)));
    xx = ui::labelled(g, xx + 18, y, ui::kLineH, "FILTER", "BASE " + juce::String(int(t.params[16])) + "  WDTH " + juce::String(int(t.params[17])));
    xx = ui::labelled(g, xx + 18, y, ui::kLineH, "DELAY SEND", juce::String(int(t.params[28]) - 64));
    ui::labelled(g, xx + 18, y, ui::kLineH, "LFOS IN USE", juce::String(lfos));
    y += ui::kLineH;
    ui::labelled(g, x, y, ui::kLineH, "KEY TRACKING", juce::String(m_preset.lpKeyTrack ? "LPF " : "--- ") + (m_preset.hpKeyTrack ? "HPF" : "---"));
}

void PresetView::mouseDown(const juce::MouseEvent& e) { m_dragging = false; switch (playZone(e.getPosition())) { case 1: if (onPlay) onPlay(); break; case 2: ui::transport().toggleLoop(); repaint(); break; default: break; } }
void PresetView::mouseMove(const juce::MouseEvent& e) { setMouseCursor(playZone(e.getPosition()) != 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); }
void PresetView::mouseDrag(const juce::MouseEvent& e)
{
    if (m_dragging || e.getDistanceFromDragStart() < 8 || playZone(e.getMouseDownPosition()) != 0 || e.getMouseDownPosition().y > 10 * kScale) return;
    m_dragging = true;
    if (onDrag) onDrag();
}

// ---------------------------------------------------------------------------
// PatternTrackBlock

int PatternTrackBlock::noteAt(int j) const
{
    const int t = m_info.track;
    if (!((m_pat.ampTrigs[t] >> j) & 1)) return -1;
    const int note = m_pat.noteNBR[t][j];
    if (note > 127) return -1;   // chord trig
    return juce::jlimit(0, 127, note + m_pat.patternTranspose + (m_pat.scale[t] == 0 ? m_pat.transpose[t] : 0));
}

void PatternTrackBlock::set(const Pattern& pat, const Kit* kit, const Info& info)
{
    m_pat = pat; m_hasKit = kit != nullptr; if (kit) m_kit = *kit; m_info = info;
    m_selectedStep = -1; m_hoverStep = -1;
    std::set<int> pitches;
    for (int j = 0; j < m_pat.patternLength && j < 64; ++j) if (const int n = noteAt(j); n >= 0) pitches.insert(n);
    m_pitches.assign(pitches.begin(), pitches.end());
    m_lanes.clear();
    for (int r = 0; r < 62; ++r)
        if (m_pat.lockTracks[r] == m_info.track) {
            const int p = m_pat.lockParams[r];
            m_lanes.push_back({r, m_hasKit ? kitParamName(m_kit.tracks[m_info.track], p) : "P" + juce::String(p)});
        }
    repaint();
}

int PatternTrackBlock::preferredHeight() const
{
    if (m_pat.noteTrigCount(m_info.track) == 0 && m_lanes.empty()) return kHeaderH + kPadH;   // nothing on this track: the header only
    return canvasTop() + canvasLcdH() * kScale + 2 + kReadoutH + kPadH;
}

int PatternTrackBlock::stepAt(juce::Point<int> p) const
{
    const int y = p.y - canvasTop();
    if (y < 0 || y >= canvasLcdH() * kScale) return -1;
    const int j = (p.x / kScale - kGutterLcd) / stepLcd();
    return p.x / kScale >= kGutterLcd && j >= 0 && j < m_pat.patternLength && j < 64 ? j : -1;
}

void PatternTrackBlock::paint(juce::Graphics& g)
{
    const int t = m_info.track;
    const int len = juce::jlimit(1, 64, int(m_pat.patternLength));
    const int trigs = m_pat.noteTrigCount(t);
    // header
    int x = 3 * kScale;
    x = ui::text(g, "T" + juce::String(t + 1), x, 0, kHeaderH, lcd::ink, ui::font(true)) + 6;
    const juce::String presetText = m_info.presetName.isNotEmpty() ? m_info.presetName : juce::String("-");
    const int linkEnd = ui::text(g, presetText, x, 0, kHeaderH, lcd::ink, ui::font(m_info.presetId.isNotEmpty(), ui::kTextPx));
    m_presetLink = m_info.presetId.isNotEmpty() ? juce::Rectangle<int>(x, 0, linkEnd - x, kHeaderH) : juce::Rectangle<int>();
    if (m_overLink) { g.setColour(lcd::ink); g.fillRect(x, kHeaderH - 4, linkEnd - x - 2, 1); }
    x = linkEnd + 8;
    if (m_info.machine.isNotEmpty()) x = ui::text(g, m_info.machine, x, 0, kHeaderH, lcd::ink.withAlpha(0.7f)) + 8;
    x = ui::text(g, juce::String(trigs) + (trigs == 1 ? " trig" : " trigs"), x, 0, kHeaderH) + 8;
    {   // the play glyph at the right end of the header (its own output of the pattern preview), the loop toggle left of its stop
        const int s = 2;
        ui::playGlyph(g, playZone(), m_playing, lcd::ink, s);
        if (m_playing && ui::transport().shown()) ui::loopGlyph(g, loopZone(), ui::transport().looping(), lcd::ink, s);
    }
    if (!m_lanes.empty()) {   // the locked parameters, as many as fit before the play glyph
        const juce::String label = juce::String(m_lanes.size()) + (m_lanes.size() == 1 ? " lock" : " locks");
        const int avail = getWidth() - kPlayPx * (m_playing && ui::transport().shown() ? 2 : 1) - x - ui::width(label, ui::font(true)) - 6;
        juce::String names;
        for (size_t i = 0; i < m_lanes.size(); ++i) {
            const juce::String next = names + (names.isEmpty() ? "" : ", ") + m_lanes[i].name;
            const bool last = i + 1 == m_lanes.size();
            if (ui::width(last ? next : next + ", ..") > avail && !names.isEmpty()) { names += ", .."; break; }
            names = next;
        }
        x = ui::labelled(g, x, 0, kHeaderH, label, names);
    }
    if (m_pat.transpose[t]) x = ui::labelled(g, x + 8, 0, kHeaderH, "TRANSPOSE", juce::String(int(m_pat.transpose[t])));
    if (m_pat.scale[t]) x = ui::labelled(g, x + 8, 0, kHeaderH, "SCALE", juce::String(int(m_pat.scale[t])));
    if (m_pat.arpMode[t]) ui::labelled(g, x + 8, 0, kHeaderH, "ARP", juce::String(int(m_pat.arpMode[t])));
    if (trigs == 0 && m_lanes.empty()) return;

    // the grid: pitch rows, a trig lane, one lane per lock, drawn at LCD resolution
    const int gx = kGutterLcd, rows = int(m_pitches.size());
    const int ch = canvasLcdH(), cw = gx + 64 * stepLcd() + 1;
    LcdCanvas cv(cw, ch);
    const int trigY = rows * kRowLcd + 1, lanesY = trigY + kRowLcd + 1;
    for (int j = 0; j <= 64; j += 4) cv.dotsV(gx + j * stepLcd() - 1, 0, ch - 1);   // bars
    for (int j = len; j < 64; ++j) for (int y = 1; y < ch; y += 2) cv.set(gx + j * stepLcd() + 1, y);   // beyond the length: a light hatch
    for (int j = 0; j < len; ++j) {
        const int sx = gx + j * stepLcd();
        const bool on = (m_pat.ampTrigs[t] >> j) & 1, off = (m_pat.offTrigs[t] >> j) & 1, tl = (m_pat.triglessTrigs[t] >> j) & 1;
        const bool slide = (m_pat.slidePatterns[t] >> j) & 1;
        if (const int n = noteAt(j); n >= 0) {
            int end = len;   // the note runs to the next trig or note-off on the track
            for (int k = j + 1; k < len; ++k) if (((m_pat.ampTrigs[t] >> k) & 1) || ((m_pat.offTrigs[t] >> k) & 1)) { end = k; break; }
            const int r = int(std::find(m_pitches.begin(), m_pitches.end(), n) - m_pitches.begin());
            const int ry = (rows - 1 - r) * kRowLcd;
            cv.fillRect(sx, ry, (end - j) * stepLcd() - 1, kRowLcd - 1, true);
            if (slide) cv.set(sx, ry + kRowLcd - 1);   // slide: a foot under the note's first step
        }
        // trig lane: solid = note trig (a dot inside for a chord trig), bar = note off, single pixel = trigless
        if (on) { cv.fillRect(sx, trigY, stepLcd() - 1, kRowLcd - 1, true); if (m_pat.noteNBR[t][j] > 127) cv.set(sx, trigY, false); }
        else if (off) cv.dotsH(sx, sx + 1, trigY + 1);
        else if (tl) cv.set(sx + 1, trigY + 1);
        for (size_t i = 0; i < m_lanes.size(); ++i)
            if (m_pat.locks[m_lanes[i].row][j] != 255) cv.fillRect(sx, lanesY + int(i) * kRowLcd, 2, kRowLcd - 1, true);
    }
    cv.dotsH(gx - 1, gx + 64 * stepLcd() - 1, ch - 1);
    if (m_selectedStep >= 0) cv.invertRect(gx + m_selectedStep * stepLcd() - 1, 0, stepLcd(), ch - 1);
    else if (m_hoverStep >= 0) for (int y = 0; y < ch - 1; y += 2) cv.set(gx + m_hoverStep * stepLcd() - 1, y);
    cv.draw(g, 0, canvasTop());
    // gutter labels: the highest and lowest pitch, the trig lane, the lock lanes
    const int gutterRight = (gx - 3) * kScale;
    const juce::Font small = ui::font(false, ui::kTinyPx);
    auto label = [&](const juce::String& s, int lcdRow) { ui::textRight(g, s, gutterRight, canvasTop() + lcdRow * kScale - 1, kRowLcd * kScale + 2, lcd::ink, small); };
    if (rows > 0) label(ui::noteName(m_pitches.back()), 0);
    if (rows > 1) label(ui::noteName(m_pitches.front()), (rows - 1) * kRowLcd);
    label("TRIGS", trigY);
    for (size_t i = 0; i < m_lanes.size(); ++i) label(m_lanes[i].name, lanesY + int(i) * kRowLcd);
    // readout
    const int ry = canvasTop() + ch * kScale + 2;
    if (m_selectedStep < 0) { ui::text(g, "Click a step to read its note and lock values.", gx * kScale, ry, kReadoutH, lcd::ink.withAlpha(0.6f), ui::font(false, ui::kSmallPx)); return; }
    const int j = m_selectedStep;
    juce::String s = "Step " + juce::String(j + 1) + ":  ";
    const bool on = (m_pat.ampTrigs[t] >> j) & 1;
    if (on) s += m_pat.noteNBR[t][j] > 127 ? juce::String("chord") : ui::noteName(noteAt(j)) + " (" + juce::String(int(m_pat.noteNBR[t][j])) + ")";
    else if ((m_pat.offTrigs[t] >> j) & 1) s += "note off";
    else if ((m_pat.triglessTrigs[t] >> j) & 1) s += "trigless";
    else s += "no trig";
    if ((m_pat.slidePatterns[t] >> j) & 1) s += ", slide";
    if ((m_pat.swingPatterns[t] >> j) & 1) s += ", swing";
    for (const auto& l : m_lanes) {
        const int v = m_pat.locks[l.row][j];
        if (v == 255) continue;
        juce::String val = juce::String(v);
        if (m_hasKit) { spec::Param pp[8]; const int p = m_pat.lockParams[l.row]; if (p < 56 && kitPageParams(m_kit.tracks[t], p / 8, pp)) val = valueText(pp[p % 8], v); }
        s += "   " + l.name + " " + val;
    }
    ui::text(g, s, gx * kScale, ry, kReadoutH, lcd::ink, ui::font(false, ui::kSmallPx));
}

void PatternTrackBlock::mouseDown(const juce::MouseEvent& e)
{
    m_dragging = false;
    if (playZone().contains(e.getPosition())) { if (onPlay) onPlay(); return; }
    if (m_playing && ui::transport().shown() && loopZone().contains(e.getPosition())) { ui::transport().toggleLoop(); repaint(); return; }
    if (m_presetLink.contains(e.getPosition())) { if (onPreset) onPreset(); return; }
    const int j = stepAt(e.getPosition());
    if (j >= 0 || m_selectedStep >= 0) { m_selectedStep = j; repaint(); }
}

void PatternTrackBlock::mouseDrag(const juce::MouseEvent& e)
{
    if (m_dragging || e.getDistanceFromDragStart() < 8 || m_presetLink.contains(e.getMouseDownPosition()) || (playZone().contains(e.getMouseDownPosition()) || loopZone().contains(e.getMouseDownPosition()))) return;
    m_dragging = true;
    if (onDrag) onDrag();
}

void PatternTrackBlock::mouseMove(const juce::MouseEvent& e)
{
    const bool link = m_presetLink.contains(e.getPosition()) || playZone().contains(e.getPosition()) || (m_playing && loopZone().contains(e.getPosition()));
    const int j = stepAt(e.getPosition());
    setMouseCursor(link ? juce::MouseCursor::PointingHandCursor : j >= 0 ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::DraggingHandCursor);
    if (link != m_overLink || j != m_hoverStep) { m_overLink = link; m_hoverStep = j; repaint(); }
}

void PatternTrackBlock::mouseExit(const juce::MouseEvent&)
{
    if (m_overLink || m_hoverStep >= 0) { m_overLink = false; m_hoverStep = -1; repaint(); }
}

// ---------------------------------------------------------------------------
// PatternView

PatternView::PatternView()
{
    for (int t = 0; t < 6; ++t) {
        auto& b = m_blocks[size_t(t)];
        addAndMakeVisible(b);
        b.onDrag = [this, t] { if (onDragTrack) onDragTrack(t); };
        b.onPreset = [this, t] { if (onPreset) onPreset(t); };
        b.onPlay = [this, t] { if (onPlayTrack) onPlayTrack(t); };
    }
    addAndMakeVisible(m_kitLink);
    addAndMakeVisible(m_dumps);
    m_kitLink.grid.onClick = [this](const Card& c) { if (onLink) onLink(c.tag); };
    m_dumps.onClick = [this](const juce::var& v) { if (onLink) onLink(v); };
}

void PatternView::set(const Pattern& pat, const Kit* kit, const std::array<PatternTrackBlock::Info, 6>& info, std::vector<Card> kitRow, std::vector<LinkList::Row> dumps)
{
    m_pat = pat;
    m_hasKit = kit != nullptr;
    if (kit) m_kit = *kit;
    for (int t = 0; t < 6; ++t) m_blocks[size_t(t)].set(pat, kit, info[size_t(t)]);
    m_title.clear();
    m_kitLink.set("KIT", std::move(kitRow), "The pattern's kit slot is empty.");
    m_dumps.set("FROM", std::move(dumps));
    resized();
    repaint();
}

void PatternView::setPlaying(int stem)
{
    if (stem == m_playing) return;
    m_playing = stem;
    for (int t = 0; t < 6; ++t) m_blocks[size_t(t)].setPlaying(stem == t);
    repaint();
}

void PatternView::mouseDown(const juce::MouseEvent& e)
{
    m_dragging = false;
    switch (playZone(e.getPosition())) { case 1: if (onPlayPattern) onPlayPattern(); break; case 2: ui::transport().toggleLoop(); repaint(); break; default: break; }
}

void PatternView::mouseMove(const juce::MouseEvent& e)
{
    setMouseCursor(playZone(e.getPosition()) != 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

int PatternView::preferredHeight() const
{
    int h = kBlocksTop;
    for (const auto& b : m_blocks) h += b.preferredHeight() + 4;
    return h + 4 + m_kitLink.heightFor(getWidth()) + 8 + m_dumps.preferredHeight();
}

void PatternView::resized()
{
    int y = kBlocksTop;
    for (auto& b : m_blocks) { b.setBounds(0, y, getWidth(), b.preferredHeight()); y += b.getHeight() + 4; }
    y += 4;
    m_kitLink.setBounds(0, y, getWidth(), m_kitLink.heightFor(getWidth()));
    y += m_kitLink.getHeight() + 8;
    m_dumps.setBounds(0, y, getWidth(), m_dumps.preferredHeight());
}

void PatternView::paint(juce::Graphics& g)
{
    const int w = getWidth() / kScale;
    LcdCanvas cv(w, 10);
    const juce::String title = m_title.isNotEmpty() ? m_title.toUpperCase() : "PATTERN " + juce::String(patternSlotName(m_pat.position));
    const juce::String right = "KIT " + juce::String(int(m_pat.kit) + 1).paddedLeft('0', 3) + (m_hasKit ? "  " + juce::String(m_kit.name).toUpperCase() : juce::String());
    drawTitleBar(cv, 0, 0, w, title.toRawUTF8(), right.toRawUTF8(), playInset(m_playing == -1));
    if (m_playing == -1) drawStopAndLoop(cv, w - 2 - kPlayW - 2, 1, false); else drawPlayGlyph(cv, w - 2 - kPlayW - 2, 1, false, false);
    cv.draw(g, 0, 0);
    // the summary line under the title bar
    const int sy = 12 * kScale, sh = 14 * kScale;
    int x = 3 * kScale;
    auto item = [&](const char* label, const juce::String& value) { x = ui::labelled(g, x, sy, sh, label, value) + 5 * kScale; };
    item("LENGTH", juce::String(int(m_pat.patternLength)));
    item("TEMPO", m_pat.doubleTempo ? "2x" : "1x");
    item("SWING", juce::String(m_pat.swingPercent()) + "%");
    item("TRANSPOSE", juce::String(int(m_pat.patternTranspose)));
    item("LOCKS", juce::String(int(m_pat.locksUsed)));
    item("MIDI NOTES", juce::String(int(m_pat.midiNotesUsed)));
    item("CHORDS", juce::String(int(m_pat.chordNotesUsed)));
    // separators between the blocks
    g.setColour(lcd::ink);
    for (size_t t = 0; t + 1 < m_blocks.size(); ++t) {
        const int y = m_blocks[t].getBottom() + 1;
        for (int px = 0; px < getWidth(); px += 2 * kScale) g.fillRect(px, y, kScale, 1);
    }
}

void PatternView::mouseDrag(const juce::MouseEvent& e)
{
    if (m_dragging || e.getDistanceFromDragStart() < 8 || playZone(e.getMouseDownPosition()) != 0) return;
    m_dragging = true;
    if (e.getMouseDownPosition().y < 12 * kScale && onDragPattern) onDragPattern();
}

} // namespace mnm::app
