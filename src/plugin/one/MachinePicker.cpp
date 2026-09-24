#include "MachinePicker.h"
#include "MachineText.h"
#include <cstring>
#include <cmath>

namespace mnm::plugin::one {

namespace {

// Geometry in screen pixels. Sized so the FX column (seven machines) fits the two-page height:
// 3 + 52 + 6 + 6*12 + 4 + 7*46 + 3 = 462 <= 2 * KnobPage height + gap.
constexpr int kColGap = 6, kBorder = 3, kHeaderH = 52, kPad = 6;
constexpr int kTextScale = 2, kLineH = 12, kBlurbLines = 6;   // small-4x5 at 2x: 10 px tall, 2 px leading
constexpr int kRowH = 46, kNameScale = 2, kNameY = 6, kDescY = 25;
constexpr int kAnimHz = 60; constexpr float kAnimSeconds = 0.16f;

const spec::Font& textFont() { return spec::kFontSmall4x5; }

void dottedH(juce::Graphics& g, int x0, int x1, int y) { for (int x = x0; x < x1; x += 4) g.fillRect(x, y, 2, 2); }
void dottedV(juce::Graphics& g, int x, int y0, int y1) { for (int y = y0; y < y1; y += 4) g.fillRect(x, y, 2, 2); }

// Wrapped caps text in the small face; returns the number of lines drawn (at most maxLines).
int drawParagraph(juce::Graphics& g, const char* text, juce::Rectangle<int> area, int maxLines, juce::Colour colour)
{
    const auto lines = wrapLcdText(textFont(), juce::String(text).toUpperCase(), area.getWidth() / kTextScale);
    int n = 0;
    for (const auto& line : lines) {
        if (n >= maxLines) break;
        drawLcdText(g, textFont(), line.toRawUTF8(), area.getX(), area.getY() + n * kLineH, kTextScale, colour);
        ++n;
    }
    return n;
}

} // namespace

MachinePicker::MachinePicker()
{
    for (int i = 0; i < spec::kNumMachines; ++i) {   // groups are contiguous in menu order
        if (m_columns.empty() || std::strcmp(m_columns.back().group, spec::kMachines[i].group) != 0)
            m_columns.push_back({spec::kMachines[i].group, i, 0, {}, {}, {}});
        ++m_columns.back().count;
    }
    m_layoutColumns = int(m_columns.size());
    m_rows.resize(size_t(spec::kNumMachines));
    setOpaque(true);
    setWantsKeyboardFocus(true);
}

void MachinePicker::setParameter(juce::RangedAudioParameter& machineParam)
{
    m_attach = std::make_unique<juce::ParameterAttachment>(machineParam,
        [this](float v) { m_current = juce::jlimit(0, spec::kNumMachines - 1, int(std::lround(v))); repaint(); });
    m_attach->sendInitialUpdate();
}

void MachinePicker::setFxOnly()
{
    for (auto it = m_columns.begin(); it != m_columns.end();)
        it = spec::kMachines[it->firstSlot].isFx ? it + 1 : m_columns.erase(it);
    for (auto& r : m_rows) r = {};
    layout();
}

void MachinePicker::setTargetBounds(juce::Rectangle<int> fullyOpen)
{
    m_target = fullyOpen;
    if (int(m_columns.size()) < m_layoutColumns) {   // FX only: the panel is as wide as its columns, not the whole page area
        const int colW = (fullyOpen.getWidth() - (m_layoutColumns - 1) * kColGap) / m_layoutColumns, shown = int(m_columns.size());
        m_target.setWidth(shown * colW + (shown - 1) * kColGap);
    }
    layout();
    applyAnimation();
}

void MachinePicker::open(bool animate)
{
    m_cursor = m_current;
    const bool was = m_wantOpen;
    m_wantOpen = true;
    setVisible(true);
    toFront(false);
    grabKeyboardFocus();
    if (!animate) { m_anim = 1.0f; stopTimer(); applyAnimation(); }
    else if (!isTimerRunning()) startTimerHz(kAnimHz);
    if (!was && onOpenChanged) onOpenChanged(true);
    repaint();
}

void MachinePicker::close(bool animate)
{
    const bool was = m_wantOpen;
    m_wantOpen = false;
    m_cursor = -1;
    if (!animate) { m_anim = 0.0f; stopTimer(); applyAnimation(); }
    else if (!isTimerRunning()) startTimerHz(kAnimHz);
    if (was && onOpenChanged) onOpenChanged(false);
}

// The picker unrolls from the top edge: its bounds grow down from the target's top, the layout is
// fixed for the fully open size, so the pages are covered progressively and the columns are clipped.
void MachinePicker::applyAnimation()
{
    const float eased = 1.0f - (1.0f - m_anim) * (1.0f - m_anim) * (1.0f - m_anim);   // ease-out
    const int h = int(std::lround(m_target.getHeight() * eased));
    setBounds(m_target.withHeight(juce::jmax(0, h)));
    if (m_anim <= 0.0f && !m_wantOpen) setVisible(false);
}

void MachinePicker::timerCallback()
{
    const float step = 1.0f / (kAnimSeconds * float(kAnimHz));
    m_anim = m_wantOpen ? juce::jmin(1.0f, m_anim + step) : juce::jmax(0.0f, m_anim - step);
    applyAnimation();
    if (m_anim <= 0.0f || m_anim >= 1.0f) stopTimer();
}

void MachinePicker::pick(int slot)
{
    if (slot >= 0 && slot < spec::kNumMachines && m_attach) m_attach->setValueAsCompleteGesture(float(slot));
    close();
}

// ---------------------------------------------------------------------------

void MachinePicker::layout()
{
    auto r = m_target.withPosition(0, 0);
    const int n = juce::jmax(1, int(m_columns.size()));   // (FX only: setTargetBounds already made the panel one column wide)
    const int colW = (r.getWidth() - (n - 1) * kColGap) / n;
    int x = r.getX();
    for (auto& col : m_columns) {
        col.bounds = {x, r.getY(), colW, r.getHeight()};
        x += colW + kColGap;
        auto inner = col.bounds.reduced(kBorder);
        col.header = inner.removeFromTop(kHeaderH);
        inner.removeFromTop(6);
        col.blurb = inner.removeFromTop(kBlurbLines * kLineH).reduced(kPad, 0);
        inner.removeFromTop(4);
        for (int i = 0; i < col.count; ++i) m_rows[size_t(col.firstSlot + i)] = inner.removeFromTop(kRowH);
    }
}

int MachinePicker::slotAt(juce::Point<int> p) const
{
    for (int i = 0; i < spec::kNumMachines; ++i) if (m_rows[size_t(i)].contains(p)) return i;
    return -1;
}

int MachinePicker::columnOf(int slot) const
{
    for (int c = 0; c < int(m_columns.size()); ++c) {
        const auto& col = m_columns[size_t(c)];
        if (slot >= col.firstSlot && slot < col.firstSlot + col.count) return c;
    }
    return -1;
}

// ---------------------------------------------------------------------------

void MachinePicker::paint(juce::Graphics& g)
{
    g.fillAll(lcd::paper);
    for (int c = 0; c < int(m_columns.size()); ++c) drawColumn(g, c);
}

void MachinePicker::drawColumn(juce::Graphics& g, int index) const
{
    const auto& col = m_columns[size_t(index)];
    const auto* text = onetext::groupText(col.group);
    g.setColour(lcd::ink);
    g.drawRect(col.bounds, kBorder);

    // header: the group's logo (white on black) or its name in the LCD title face
    g.fillRect(col.header);
    if (const auto box = col.header.reduced(kPad + 4, 8); spec::groupLogo(col.group) != nullptr) {
        drawGroupLogo(g, col.group, box.getCentreX() - groupLogoWidth(col.group, box.getHeight()) / 2, box.getY(), box.getHeight(), lcd::paper);
    } else {
        const char* title = text ? text->title : col.group;
        const int tw = LcdCanvas::textWidth(spec::kFontBold8, title) * kScale;
        drawLcdText(g, spec::kFontBold8, title, col.header.getCentreX() - tw / 2, col.header.getCentreY() - spec::kFontBold8.h * kScale / 2, kScale, lcd::paper);
    }

    // group blurb
    if (text) drawParagraph(g, text->blurb, col.blurb, kBlurbLines, lcd::ink);

    // machine rows
    const int inner0 = col.bounds.getX() + kBorder, inner1 = col.bounds.getRight() - kBorder;
    g.setColour(lcd::ink);
    dottedH(g, inner0, inner1, m_rows[size_t(col.firstSlot)].getY() - 1);
    for (int i = 0; i < col.count; ++i) {
        const int slot = col.firstSlot + i;
        const auto& m = spec::kMachines[slot];
        const auto row = m_rows[size_t(slot)];
        const bool current = slot == m_current, cursor = slot == m_cursor;
        if (current) { g.setColour(lcd::ink); g.fillRect(row); }
        else if (cursor) { g.setColour(lcd::ink.withAlpha(0.10f)); g.fillRect(row); }
        const auto ink = current ? lcd::paper : lcd::ink;
        drawLcdText(g, spec::kFontBold8, m.name, row.getX() + kPad, row.getY() + kNameY, kNameScale, ink);
        if (const char* blurb = onetext::machineBlurb(m.index))
            drawParagraph(g, blurb, {row.getX() + kPad, row.getY() + kDescY, row.getWidth() - 2 * kPad, kLineH}, 1, ink);   // one line (MachineText.h)
        if (!m.supported) {   // no engine path: knobs only (dotted PREVIEW tag, as the machine block shows)
            static const char* tag = "PREVIEW";
            const int tw = LcdCanvas::textWidth(spec::kFontTiny3x5, tag) * 2, bw = tw + 10, bh = spec::kFontTiny3x5.h * 2 + 8;
            const int bx = row.getRight() - kPad - bw, by = row.getY() + kNameY + (spec::kFontBold8.h * kNameScale - bh) / 2;
            g.setColour(ink);
            dottedH(g, bx, bx + bw, by); dottedH(g, bx, bx + bw, by + bh - 2);
            dottedV(g, bx, by, by + bh); dottedV(g, bx + bw - 2, by, by + bh);
            drawLcdText(g, spec::kFontTiny3x5, tag, bx + 5, by + 4, 2, ink);
        }
        g.setColour(lcd::ink);
        dottedH(g, inner0, inner1, row.getBottom() - 1);
    }
}

// ---------------------------------------------------------------------------

void MachinePicker::mouseMove(const juce::MouseEvent& e)
{
    const int slot = slotAt(e.getPosition());
    setMouseCursor(slot >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (slot != m_cursor) { m_cursor = slot; repaint(); }
}

void MachinePicker::mouseExit(const juce::MouseEvent&)
{
    if (m_cursor >= 0) { m_cursor = -1; repaint(); }
}

void MachinePicker::mouseDown(const juce::MouseEvent& e)
{
    const int slot = slotAt(e.getPosition());
    if (slot >= 0) pick(slot);
}

bool MachinePicker::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { close(); return true; }
    if (k == juce::KeyPress::returnKey) { pick(m_cursor >= 0 ? m_cursor : m_current); return true; }
    const bool up = k == juce::KeyPress::upKey, down = k == juce::KeyPress::downKey;
    const bool left = k == juce::KeyPress::leftKey, right = k == juce::KeyPress::rightKey;
    if (!(up || down || left || right)) return false;
    int slot = m_cursor >= 0 ? m_cursor : m_current;
    int c = columnOf(slot);
    int i = slot - m_columns[size_t(c)].firstSlot;
    if (up) i = juce::jmax(0, i - 1);
    if (down) i = juce::jmin(m_columns[size_t(c)].count - 1, i + 1);
    if (left) c = juce::jmax(0, c - 1);
    if (right) c = juce::jmin(int(m_columns.size()) - 1, c + 1);
    i = juce::jmin(i, m_columns[size_t(c)].count - 1);
    m_cursor = m_columns[size_t(c)].firstSlot + i;
    repaint();
    return true;
}

} // namespace mnm::plugin::one
