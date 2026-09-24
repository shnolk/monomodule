#include "ProjectViews.h"
#include <cmath>

namespace mnm::app {

using namespace mnm::dump;

namespace {
juce::Colour kDim() { return lcd::ink.withAlpha(0.55f); }   // functions: the skin can change
juce::Colour kHatch() { return lcd::ink.withAlpha(0.06f); }
void dashedFrame(juce::Graphics& g, juce::Rectangle<int> r)   // a drop target
{
    g.setColour(lcd::ink.withAlpha(0.08f)); g.fillRect(r);
    g.setColour(lcd::ink);
    for (int x = r.getX(); x < r.getRight(); x += 10) { g.fillRect(x, r.getY(), 6, 2); g.fillRect(x, r.getBottom() - 2, 6, 2); }
    for (int y = r.getY(); y < r.getBottom(); y += 10) { g.fillRect(r.getX(), y, 2, 6); g.fillRect(r.getRight() - 2, y, 2, 6); }
}
void cross(juce::Graphics& g, juce::Rectangle<int> zone, juce::Colour c)   // the X that clears a slot
{
    g.setColour(c);
    const auto r = zone.withSizeKeepingCentre(10, 10).toFloat();
    g.drawLine(r.getX(), r.getY(), r.getRight(), r.getBottom(), 2.0f);
    g.drawLine(r.getX(), r.getBottom(), r.getRight(), r.getY(), 2.0f);
}
void plus(juce::Graphics& g, juce::Rectangle<int> zone, juce::Colour c, int size)
{
    g.setColour(c);
    const auto r = zone.withSizeKeepingCentre(size, size);
    g.fillRect(r.getCentreX() - 1, r.getY(), 2, r.getHeight());
    g.fillRect(r.getX(), r.getCentreY() - 1, r.getWidth(), 2);
}
juce::String pad3(int pos) { return juce::String(pos + 1).paddedLeft('0', 3); }
}

// ---------------------------------------------------------------------------
// Rail

int Rail::preferredHeight() const { int h = 8; for (const auto& it : m_items) h += rowHeight(it); return h + 8; }

int Rail::itemAt(int y) const
{
    int top = 8;
    for (int i = 0; i < int(m_items.size()); ++i) { const int h = rowHeight(m_items[size_t(i)]); if (y >= top && y < top + h) return i; top += h; }
    return -1;
}

void Rail::paint(juce::Graphics& g)
{
    int y = 8;
    for (int i = 0; i < int(m_items.size()); ++i) {
        const auto& it = m_items[size_t(i)];
        const int h = rowHeight(it);
        const juce::Rectangle<int> r(0, y, getWidth(), h);
        if (it.kind == Item::Header) {
            drawLcdText(g, spec::kFontSmall4x5, it.text.toUpperCase().toRawUTF8(), 12, y + 13, 2, lcd::ink);
        } else {
            const bool sel = it.kind == Item::Entry && it.id.isNotEmpty() && m_selected.count(it.id) > 0;
            if (sel) { g.setColour(lcd::ink); g.fillRect(r); }
            else if (i == m_hover && it.kind == Item::Entry) { g.setColour(kHatch()); g.fillRect(r); }
            const auto col = it.kind == Item::Note ? kDim() : sel ? lcd::paper : lcd::ink;
            int x = 12 + it.indent;
            if (it.logoGroup) {
                constexpr int kLogoBand = 18;   // 2 screen pixels per logo pixel; no logo without an OS file, the name follows anyway
                if (const int lw = groupLogoWidth(it.logoGroup, kLogoBand); lw > 0) {
                    drawGroupLogo(g, it.logoGroup, x, y + (26 - kLogoBand) / 2, kLogoBand, col);
                    x += lw + 8;
                }
            }
            ui::text(g, it.text, x, y + (it.sub.isNotEmpty() ? 4 : 0), it.sub.isNotEmpty() ? 20 : h, col, ui::font(false, 13.0f));
            if (it.sub.isNotEmpty()) ui::text(g, it.sub, x, y + 21, 16, col.withMultipliedAlpha(0.7f), ui::font(false, 11.5f));
            if (it.count.isNotEmpty()) ui::textRight(g, it.count, getWidth() - 12, y, it.sub.isNotEmpty() ? 26 : h, col.withMultipliedAlpha(0.7f), ui::font(false, 11.5f));
        }
        y += h;
    }
}

void Rail::mouseDown(const juce::MouseEvent& e)
{
    const int i = itemAt(e.y);
    if (i >= 0 && m_items[size_t(i)].kind == Item::Entry && m_items[size_t(i)].id.isNotEmpty() && onSelect) onSelect(juce::String(m_items[size_t(i)].id));
}

void Rail::mouseMove(const juce::MouseEvent& e)
{
    const int i = itemAt(e.y);
    const bool entry = i >= 0 && m_items[size_t(i)].kind == Item::Entry && m_items[size_t(i)].id.isNotEmpty();
    setMouseCursor(entry ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (i != m_hover) { m_hover = i; repaint(); }
}

// ---------------------------------------------------------------------------
// Slot grids

void SlotGridBase::itemDragMove(const SourceDetails& d)
{
    dragHover(d.localPosition);
    const int pos = slotAt(d.localPosition);
    if (pos != m_over) { m_over = pos; repaint(); }
}

void SlotGridBase::itemDropped(const SourceDetails& d)
{
    const int pos = slotAt(d.localPosition);
    m_over = -1;
    repaint();
    const auto what = d.description.toString().fromFirstOccurrenceOf(":", false, false);   // "slot:<pos>" | "lib:<id>"
    if (pos >= 0 && onDrop) onDrop(what, pos, juce::ModifierKeys::currentModifiers.isAltDown());
}

void SlotGridBase::mouseDown(const juce::MouseEvent& e) { m_pressed = slotAt(e.getPosition()); m_dragging = false; }

void SlotGridBase::mouseUp(const juce::MouseEvent& e)
{
    const int pos = slotAt(e.getPosition());
    if (m_dragging || pos < 0 || pos != m_pressed) return;
    const bool used = m_slots[size_t(pos)].used;
    if (!m_edit) {
        if (!used) return;
        if (glyphZone(pos).contains(e.getPosition())) { if (onPlay) onPlay(pos); }
        else if (pos == m_playing && ui::transport().shown() && loopZone(pos).contains(e.getPosition())) { ui::transport().toggleLoop(); repaint(); }
        else if (onOpen) onOpen(pos);
        return;
    }
    if (!used) { if (onFill) onFill(pos); return; }
    if (clearZone(pos).contains(e.getPosition())) { if (onClear) onClear(pos); }
    else if (glyphZone(pos).contains(e.getPosition()) && glyphZone(pos) != clearZone(pos)) { if (onPlay) onPlay(pos); }
    else if (pos == m_playing && glyphZone(pos) != clearZone(pos) && ui::transport().shown() && loopZone(pos).contains(e.getPosition())) { ui::transport().toggleLoop(); repaint(); }
    else if (onSelect) onSelect(pos);
}

void SlotGridBase::mouseDrag(const juce::MouseEvent& e)
{
    if (!m_edit || m_dragging || m_pressed < 0 || !m_slots[size_t(m_pressed)].used || e.getDistanceFromDragStart() < 8) return;
    m_dragging = true;
    if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor(this))
        c->startDragging(m_kind + ":slot:" + juce::String(m_pressed), this, juce::ScaledImage(createComponentSnapshot(slotBounds(m_pressed))), true);
}

void SlotGridBase::mouseMove(const juce::MouseEvent& e)
{
    const int pos = slotAt(e.getPosition());
    const bool live = pos >= 0 && (m_slots[size_t(pos)].used || m_edit);
    setMouseCursor(live ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (pos != m_hover) { m_hover = pos; repaint(); }
}

// ---- BankGrid

void BankGrid::set(const std::array<SlotInfo, 128>& slots, int bank, bool edit, int selected, int fillTarget)
{
    m_slots = slots; m_bank = juce::jlimit(0, 7, bank); m_edit = edit; m_selected = selected; m_fill = fillTarget;
    repaint();
}

int BankGrid::bankAt(juce::Point<int> p) const
{
    if (p.y < kPad || p.y >= kPad + 54) return -1;
    const int b = (p.x - kPad) / (kBankW + kGap);
    return p.x >= kPad && b >= 0 && b < 8 && (p.x - kPad) % (kBankW + kGap) < kBankW ? b : -1;
}

juce::Rectangle<int> BankGrid::slotBounds(int pos) const
{
    const int i = pos - m_bank * 16;
    if (i < 0 || i > 15) return {};
    const int w = (getWidth() - 2 * kPad - 7 * kGap) / 8;
    return {kPad + (i % 8) * (w + kGap), kBanksH + (i / 8) * (kSlotH + kGap), w, kSlotH};
}

int BankGrid::slotAt(juce::Point<int> p) const
{
    for (int i = 0; i < 16; ++i) if (slotBounds(m_bank * 16 + i).contains(p)) return m_bank * 16 + i;
    return -1;
}

void BankGrid::dragHover(juce::Point<int> p)
{
    const int b = bankAt(p);
    if (b >= 0 && b != m_bank && onBank) onBank(b);   // hovering a bank key with a drag switches bank
}

void BankGrid::mouseDown(const juce::MouseEvent& e)
{
    const int b = bankAt(e.getPosition());
    if (b >= 0) {
        int n = 0; for (int i = 0; i < 16; ++i) n += m_slots[size_t(b * 16 + i)].used ? 1 : 0;
        if ((n > 0 || m_edit) && onBank) onBank(b);   // a bank without patterns is inert in view mode
        m_pressed = -1;
        return;
    }
    SlotGridBase::mouseDown(e);
}

void BankGrid::paint(juce::Graphics& g)
{
    for (int b = 0; b < 8; ++b) {
        int n = 0; for (int i = 0; i < 16; ++i) n += m_slots[size_t(b * 16 + i)].used ? 1 : 0;
        const juce::Rectangle<int> r(kPad + b * (kBankW + kGap), kPad, kBankW, 54);
        const bool sel = b == m_bank, unused = n == 0 && !m_edit;
        const auto col = (sel ? lcd::paper : lcd::ink).withMultipliedAlpha(unused && !sel ? 0.25f : 1.0f);
        if (sel) { g.setColour(lcd::ink); g.fillRect(r); } else ui::dottedFrame(g, r, col);
        const char letter[2] = {char('A' + b), 0};
        drawLcdText(g, spec::kFontBold8, letter, r.getCentreX() - LcdCanvas::textWidth(spec::kFontBold8, letter) * 3 / 2, r.getY() + 8, 3, col);
        g.setColour(col.withMultipliedAlpha(0.75f)); g.setFont(ui::font(false, 11.0f));
        g.drawText(juce::String(n) + "/16", r.withTop(r.getBottom() - 18), juce::Justification::centred, false);
    }
    for (int i = 0; i < 16; ++i) {
        const int pos = m_bank * 16 + i;
        const auto& sl = m_slots[size_t(pos)];
        const auto r = slotBounds(pos);
        const bool sel = m_edit && pos == m_selected && sl.used, hover = pos == m_hover, target = pos == m_fill || pos == m_over;
        if (sel) { g.setColour(lcd::ink); g.fillRect(r); }
        else if (target) dashedFrame(g, r);
        else { if (hover && (sl.used || m_edit)) { g.setColour(kHatch()); g.fillRect(r); } ui::dottedFrame(g, r, lcd::ink.withAlpha(sl.used || m_edit ? 1.0f : 0.35f)); }
        const auto col = (sel ? lcd::paper : lcd::ink).withMultipliedAlpha(sl.used ? 1.0f : m_edit ? 0.55f : 0.35f);
        drawLcdText(g, spec::kFontSmall4x5, patternSlotName(pos).c_str(), r.getX() + 8, r.getY() + 9, 2, col);
        if (sl.changed) { g.setColour(col); g.fillRect(r.getX() + 8 + LcdCanvas::textWidth(spec::kFontSmall4x5, "A00") * 2 + 8, r.getY() + 10, 7, 7); }
        if (sl.used) {
            if (m_edit) cross(g, clearZone(pos), col);
            else { ui::playGlyph(g, glyphZone(pos), pos == m_playing, col); if (pos == m_playing && ui::transport().shown()) ui::loopGlyph(g, loopZone(pos), ui::transport().looping(), col); }
            g.setColour(col); g.setFont(ui::font(false, 12.5f));
            g.drawText(sl.name, r.getX() + 8, r.getY() + 28, r.getWidth() - 14, 18, juce::Justification::centredLeft, true);
            g.setColour(col.withMultipliedAlpha(0.75f)); g.setFont(ui::font(false, 11.0f));
            g.drawText(sl.tracks == 0 ? juce::String("MIDI only") : juce::String(sl.tracks) + (sl.tracks == 1 ? " track" : " tracks"), r.getX() + 8, r.getBottom() - 22, r.getWidth() - 16, 14, juce::Justification::centredLeft, false);
            g.drawText(juce::String(sl.steps) + " steps", r.getX() + 8, r.getBottom() - 22, r.getWidth() - 16, 14, juce::Justification::centredRight, false);
        } else if (m_edit) {
            if (hover || target) plus(g, r, lcd::ink, 22);
        } else {
            g.setColour(col); g.setFont(ui::font(false, 12.5f));
            g.drawText("empty", r.getX() + 8, r.getY() + 28, r.getWidth() - 14, 18, juce::Justification::centredLeft, false);
        }
    }
}

// ---- KitGrid

void KitGrid::set(const std::array<SlotInfo, 128>& slots, bool edit, int selected, int fillTarget)
{
    m_slots = slots; m_edit = edit; m_selected = selected; m_fill = fillTarget;
    repaint();
}

juce::Rectangle<int> KitGrid::slotBounds(int pos) const
{
    const int w = (getWidth() - 2 * kPad) / kCols;
    return {kPad + (pos % kCols) * w, kPad + (pos / kCols) * kRowH, w, kRowH};
}

int KitGrid::slotAt(juce::Point<int> p) const
{
    const int w = (getWidth() - 2 * kPad) / kCols;
    if (p.x < kPad || p.y < kPad || w <= 0) return -1;
    const int col = (p.x - kPad) / w, row = (p.y - kPad) / kRowH, pos = row * kCols + col;
    return col < kCols && pos >= 0 && pos < 128 ? pos : -1;
}

void KitGrid::paint(juce::Graphics& g)
{
    const auto clip = g.getClipBounds();
    for (int pos = 0; pos < 128; ++pos) {
        const auto r = slotBounds(pos);
        if (!r.intersects(clip)) continue;
        const auto& sl = m_slots[size_t(pos)];
        const bool sel = pos == m_selected && sl.used, hover = pos == m_hover, target = pos == m_fill || pos == m_over;
        if (sel) { g.setColour(lcd::ink); g.fillRect(r); }
        else if (target) dashedFrame(g, r);
        else if (hover && (sl.used || m_edit)) { g.setColour(kHatch()); g.fillRect(r); }
        const auto col = (sel ? lcd::paper : lcd::ink).withMultipliedAlpha(sl.used ? 1.0f : 0.3f);
        g.setColour(lcd::ink.withAlpha(0.12f));
        for (int x = r.getX(); x < r.getRight(); x += 4) g.fillRect(x, r.getBottom() - 1, 2, 1);
        g.setColour(col.withMultipliedAlpha(0.65f)); g.setFont(ui::font(false, 11.0f));
        g.drawText(pad3(pos), r.getX() + 6, r.getY(), 26, kRowH, juce::Justification::centredLeft, false);
        g.setColour(col); g.setFont(ui::font(false, 13.0f));
        const int nameW = r.getWidth() - 38 - (hover ? 46 : 8) - (pos == m_playing && ui::transport().shown() ? 22 : 0);
        g.drawText(sl.used ? sl.name : m_edit ? juce::String() : juce::String("-"), r.getX() + 36, r.getY(), nameW, kRowH, juce::Justification::centredLeft, true);
        if (sl.changed) { g.setColour(col); g.fillRect(r.getX() + 36 + juce::jmin(nameW, ui::width(sl.name, ui::font(false, 13.0f)) + 6), r.getCentreY() - 3, 6, 6); }
        if (sl.used && (hover || pos == m_playing)) ui::playGlyph(g, glyphZone(pos), pos == m_playing, col);
        if (sl.used && pos == m_playing && ui::transport().shown()) ui::loopGlyph(g, loopZone(pos), ui::transport().looping(), col);
        if (m_edit && hover) { if (sl.used) cross(g, clearZone(pos), col); else plus(g, clearZone(pos), lcd::ink, 12); }
        else if (m_edit && target && !sl.used) plus(g, clearZone(pos), lcd::ink, 12);
    }
}

// ---------------------------------------------------------------------------
// Tray

Tray::Tray()
{
    static const char* names[4] = {"ALL", "THIS PROJECT", "OTHER PROJECTS", "SAVED"};
    for (int i = 0; i < 4; ++i) {
        auto& c = m_chips[size_t(i)] = std::make_unique<LcdChip>(names[i], spec::kFontSmall4x5, 2, true);
        c->setRadioGroupId(77);
        c->onClick = [this, i] { m_source = i - 1; refilter(); };
        addAndMakeVisible(*c);
    }
    m_chips[0]->setToggleState(true, juce::dontSendNotification);
    m_filter.setFont(ui::font(false, 12.5f));
    m_filter.setTextToShowWhenEmpty("Filter", lcd::ink.withAlpha(0.4f));
    m_filter.setIndents(5, 2);
    m_filter.onTextChange = [this] { refilter(); };
    addAndMakeVisible(m_filter);
    m_viewport.setScrollBarsShown(true, false);
    m_viewport.setScrollBarThickness(8);
    m_viewport.setViewedComponent(&m_grid, false);
    addAndMakeVisible(m_viewport);
    m_grid.onClick = [this](const Card& c) { if (onPick) onPick(c); };
    m_grid.onPlay = [this](const Card& c) { if (onPlay) onPlay(c); };
    m_grid.onDragStart = [this](const Card& c, juce::Component* src) { if (onDragStart) onDragStart(c, src); };
}

void Tray::set(const juce::String& title, std::vector<Card> cards) { m_title = title; m_all = std::move(cards); refilter(); repaint(); }

void Tray::refilter()
{
    const auto q = m_filter.getText().trim();
    std::vector<Card> v;
    for (const auto& c : m_all) {
        if (m_source >= 0 && c.group != m_source) continue;
        if (q.isNotEmpty() && !c.name.containsIgnoreCase(q) && !c.label.containsIgnoreCase(q) && !c.metaLeft.containsIgnoreCase(q)) continue;
        v.push_back(c);
    }
    m_grid.set(std::move(v));
    resized();
}

void Tray::paint(juce::Graphics& g)
{
    g.setColour(lcd::ink); g.fillRect(0, 0, getWidth(), 30);
    drawLcdText(g, spec::kFontBold8, m_title.toUpperCase().toRawUTF8(), 8, 7, 2, lcd::paper);
    const int tx = 8 + LcdCanvas::textWidth(spec::kFontBold8, m_title.toUpperCase().toRawUTF8()) * 2 + 14;
    ui::text(g, m_hint, tx, 0, 30, lcd::paper.withAlpha(0.8f), ui::font(false, 12.0f));
}

void Tray::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop(30);
    auto row = r.removeFromTop(28).reduced(8, 4);
    m_filter.setBounds(row.removeFromRight(170));
    for (auto& c : m_chips) { c->setBounds(row.removeFromLeft(c->preferredWidth()).withSizeKeepingCentre(c->preferredWidth(), c->preferredHeight())); row.removeFromLeft(4); }
    m_viewport.setBounds(r);
    const int w = r.getWidth() - m_viewport.getScrollBarThickness();
    m_grid.setSize(w, juce::jmax(r.getHeight(), m_grid.heightFor(w)));
}

// ---------------------------------------------------------------------------
// ChangesView

ChangesView::ChangesView()
{
    m_undo.setGhost(true); m_undo.setOnDark(true);
    m_undo.onClick = [this] { if (onUndo) onUndo(); };
    addAndMakeVisible(m_undo);
}

void ChangesView::set(const juce::StringArray& changes) { m_changes = changes; m_undo.setEnabled(!changes.isEmpty()); repaint(); }

void ChangesView::resized() { m_undo.setBounds(getWidth() - m_undo.preferredWidth() - 6, (30 - m_undo.preferredHeight()) / 2, m_undo.preferredWidth(), m_undo.preferredHeight()); }

void ChangesView::paint(juce::Graphics& g)
{
    g.setColour(lcd::ink); g.fillRect(0, 0, getWidth(), 30);
    drawLcdText(g, spec::kFontBold8, "CHANGES", 8, 7, 2, lcd::paper);
    ui::text(g, juce::String(m_changes.size()) + " in this edit", 8 + LcdCanvas::textWidth(spec::kFontBold8, "CHANGES") * 2 + 14, 0, 30, lcd::paper.withAlpha(0.8f), ui::font(false, 12.0f));
    int y = 38;
    const int w = getWidth() - 24;
    if (m_changes.isEmpty()) {
        ui::wrapped(g, "Nothing changed yet. X clears a slot, + fills an empty one (then click a library item), drag a slot onto another to move or swap it (hold Option to copy), drag from the library below to fill or replace. SAVE leaves edit mode.",
                    {12, y, w, getHeight() - y}, kDim(), ui::font(false, 12.5f));
        return;
    }
    for (int i = 0; i < m_changes.size(); ++i) {
        const auto text = juce::String(i + 1) + ".  " + m_changes[i];
        const int h = ui::textHeight(text, ui::font(false, 12.5f), w);
        if (y + h > getHeight()) { ui::text(g, "...", 12, y, 16, kDim()); break; }
        ui::wrapped(g, text, {12, y, w, h}, lcd::ink, ui::font(false, 12.5f));
        y += h + 4;
    }
}

// ---------------------------------------------------------------------------
// HistoryView

void HistoryView::set(const mnm::library::ProjectInfo& project, bool editing, const juce::StringArray& pending)
{
    for (auto& r : m_rows) for (auto& c : r->chips) removeChildComponent(c.get());
    m_rows.clear();
    auto chip = [this](Row& row, const char* text, bool ghost, std::function<void()> fn) {
        auto c = std::make_unique<LcdChip>(text, spec::kFontSmall4x5, 2, false);
        c->setGhost(ghost); c->onClick = std::move(fn);
        addAndMakeVisible(*c);
        row.chips.push_back(std::move(c));
    };
    if (editing) {
        auto row = std::make_unique<Row>();
        row->pending = true; row->label = "EDIT"; row->kind = "NOT SAVED";
        row->title = juce::String(pending.size()) + (pending.size() == 1 ? " change" : " changes") + " in edit mode, based on " + (project.current() ? project.current()->label() : juce::String("-"));
        row->changes = pending;
        chip(*row, "SAVE...", false, [this] { if (onSave) onSave(); });
        m_rows.push_back(std::move(row));
    }
    bool first = true;
    for (const auto& v : project.versions) {
        auto row = std::make_unique<Row>();
        row->n = v.n; row->label = v.label().toUpperCase(); row->kind = v.kind.toUpperCase();
        row->title = v.title + (v.note.isNotEmpty() ? "  (" + v.note + ")" : juce::String()) + (first ? "   - the current version" : "");
        row->when = v.time.formatted("%d %b %Y  %H:%M");
        row->changes = v.changes;
        const int n = v.n;
        if (!first) {
            chip(*row, "VIEW", true, [this, n] { if (onView) onView(n); });
            chip(*row, "COMPARE", true, [this, n] { if (onCompare) onCompare(n); });
            chip(*row, "RESTORE", true, [this, n] { if (onRestore) onRestore(n); });
        }
        chip(*row, "EXPORT", true, [this, n] { if (onExport) onExport(n); });
        for (auto& c : row->chips) c->setEnabled(!editing);
        first = false;
        m_rows.push_back(std::move(row));
    }
    layoutRows();
    repaint();
}

void HistoryView::layoutRows()
{
    int y = 50;
    const int textW = juce::jmax(200, getWidth() - 120 - 330);
    for (auto& r : m_rows) {
        int h = 12 + 20 + 16;
        for (const auto& c : r->changes) h += ui::textHeight("- " + c, ui::font(false, 12.5f), textW) + 1;
        h = juce::jmax(h + 12, 70);
        r->y = y; r->h = h;
        int x = getWidth() - 18;
        for (int i = int(r->chips.size()) - 1; i >= 0; --i) { auto& c = r->chips[size_t(i)]; x -= c->preferredWidth(); c->setBounds(x, y + 14, c->preferredWidth(), c->preferredHeight()); x -= 6; }
        y += h;
    }
    m_height = y + 20;
}

void HistoryView::paint(juce::Graphics& g)
{
    ui::wrapped(g, "Every import, saved edit and export is a version. Versions are never changed or deleted: saving an edit adds one, and restoring an old one adds a new version equal to it, so nothing is lost.",
                {18, 12, getWidth() - 36, 36}, kDim(), ui::font(false, 12.5f));
    const int textW = juce::jmax(200, getWidth() - 120 - 330);
    for (const auto& r : m_rows) {
        if (r->pending) { g.setColour(kHatch()); g.fillRect(0, r->y, getWidth(), r->h); }
        drawLcdText(g, spec::kFontBold8, r->label.toRawUTF8(), 18, r->y + 12, 3, lcd::ink);
        g.setFont(ui::font(false, 10.5f)); g.setColour(lcd::ink);
        const int kw = ui::width(r->kind, ui::font(false, 10.5f)) + 12;
        ui::dottedFrame(g, {18, r->y + 42, kw, 17}, lcd::ink);
        g.drawText(r->kind, 18, r->y + 42, kw, 17, juce::Justification::centred, false);
        int y = r->y + 10;
        ui::text(g, r->title, 120, y, 20, lcd::ink, ui::font(true, 13.5f)); y += 20;
        if (r->when.isNotEmpty()) { ui::text(g, r->when, 120, y, 16, kDim(), ui::font(false, 11.5f)); y += 18; }
        for (const auto& c : r->changes) {
            const int h = ui::textHeight("- " + c, ui::font(false, 12.5f), textW);
            ui::wrapped(g, "- " + c, {120, y, textW, h}, lcd::ink, ui::font(false, 12.5f));
            y += h + 1;
        }
        g.setColour(lcd::ink);
        for (int x = 18; x < getWidth() - 18; x += 4) g.fillRect(x, r->y + r->h - 1, 2, 1);
    }
}

// ---------------------------------------------------------------------------
// Dialogs

void TickGrid::paint(juce::Graphics& g)
{
    const int w = getWidth() / 4;
    for (int i = 0; i < m_labels.size(); ++i) {
        const juce::Rectangle<int> r((i % 4) * w, (i / 4) * 22 + 2, w, 20);
        const bool on = m_ticked.count(m_ids[i]) > 0;
        const juce::Rectangle<int> box(r.getX() + 4, r.getY() + 3, 13, 13);
        g.setColour(lcd::ink);
        if (on) g.fillRect(box); else g.drawRect(box, 2);
        if (on) { g.setColour(lcd::paper); g.fillRect(box.reduced(4)); g.setColour(lcd::ink); }
        g.setFont(ui::font(false, 12.0f));
        g.drawText(m_labels[i], r.getX() + 24, r.getY(), w - 28, 20, juce::Justification::centredLeft, true);
    }
}

void TickGrid::mouseDown(const juce::MouseEvent& e)
{
    const int w = getWidth() / 4;
    if (w <= 0) return;
    const int i = ((e.y - 2) / 22) * 4 + e.x / w;
    if (i < 0 || i >= m_ids.size()) return;
    if (!m_ticked.erase(m_ids[i])) m_ticked.insert(m_ids[i]);
    repaint();
}

void FormDialog::set(DialogSpec spec, const std::map<juce::String, std::set<juce::String>>& ticks)
{
    std::map<juce::String, juce::String> keepFields;
    for (auto& [id, ed] : m_fields) keepFields[id] = ed->getText();
    removeAllChildren();
    m_fields.clear(); m_tickViews.clear(); m_tickGrids.clear(); m_buttons.clear();
    m_spec = std::move(spec);
    for (const auto& row : m_spec.rows) {
        if (row.kind == DialogSpec::Row::Field) {
            auto ed = std::make_unique<juce::TextEditor>();
            ed->setFont(ui::font(false, 13.0f)); ed->setIndents(6, 3);
            ed->setText(keepFields.count(row.id) ? keepFields[row.id] : row.b, false);
            ed->setTextToShowWhenEmpty(row.items.isEmpty() ? juce::String() : row.items[0], lcd::ink.withAlpha(0.4f));
            addAndMakeVisible(*ed);
            m_fields[row.id] = std::move(ed);
        } else if (row.kind == DialogSpec::Row::Ticks) {
            auto grid = std::make_unique<TickGrid>();
            auto it = ticks.find(row.id);
            grid->set(row.items, row.itemIds, it == ticks.end() ? std::set<juce::String>{} : it->second);
            auto vp = std::make_unique<juce::Viewport>();
            vp->setScrollBarsShown(true, false); vp->setScrollBarThickness(8);
            vp->setViewedComponent(grid.get(), false);
            addAndMakeVisible(*vp);
            m_tickGrids[row.id] = std::move(grid); m_tickViews[row.id] = std::move(vp);
        }
    }
    for (size_t i = 0; i < m_spec.buttons.size(); ++i) {
        auto c = std::make_unique<LcdChip>(m_spec.buttons[i].second, spec::kFontBold8, 2, false);
        c->setGhost(i + 1 < m_spec.buttons.size());
        const auto id = m_spec.buttons[i].first;
        c->onClick = [this, id] { if (onButton) onButton(id); };
        addAndMakeVisible(*c);
        m_buttons.push_back(std::move(c));
    }
    setSize(m_spec.width, 100);
    layout();
    setSize(m_spec.width, m_height);
    repaint();
}

juce::String FormDialog::field(const juce::String& id) const { auto it = m_fields.find(id); return it == m_fields.end() ? juce::String() : it->second->getText(); }
std::set<juce::String> FormDialog::ticked(const juce::String& id) const { auto it = m_tickGrids.find(id); return it == m_tickGrids.end() ? std::set<juce::String>{} : it->second->ticked(); }

void FormDialog::layout()
{
    using R = DialogSpec::Row;
    const int pad = 16, w = m_spec.width - 2 * pad;
    int y = 30 + 14;
    m_rects.assign(m_spec.rows.size(), {});
    for (size_t i = 0; i < m_spec.rows.size(); ++i) {
        const auto& row = m_spec.rows[i];
        int h = 0;
        switch (row.kind) {
        case R::Text: h = ui::textHeight(row.a, ui::font(false, 13.0f), w); break;
        case R::Bold: h = ui::textHeight(row.a, ui::font(true, 13.0f), w); break;
        case R::Dim: h = ui::textHeight(row.a, ui::font(false, 12.0f), w); break;
        case R::Bullet: h = ui::textHeight(row.a, ui::font(false, 12.5f), w - 22); break;
        case R::Check: h = ui::textHeight(row.b, ui::font(false, 13.0f), w - 34); break;
        case R::Option: h = 10 + 18 + ui::textHeight(row.b, ui::font(false, 12.0f), w - 44) + 8; break;
        case R::Field: h = 28; break;
        case R::Ticks: h = 18 + juce::jmin(154, m_tickGrids[row.id]->preferredHeight() + 2); break;
        case R::Steps: h = 24; break;
        case R::Gap: h = 6; break;
        }
        m_rects[i] = {pad, y, w, h};
        if (row.kind == R::Field) { const int lw = ui::width(row.a, ui::font(true, 13.0f)) + 12; m_fields[row.id]->setBounds(pad + lw, y, w - lw, 26); }
        if (row.kind == R::Ticks) { auto& vp = *m_tickViews[row.id]; vp.setBounds(pad, y + 18, w, h - 18); auto& grid = *m_tickGrids[row.id]; grid.setSize(w - 10, juce::jmax(h - 18, grid.preferredHeight())); }
        y += h + (row.kind == R::Option ? 8 : row.kind == R::Bullet ? 2 : 6);
    }
    y += 6;
    int x = m_spec.width - pad;
    for (int i = int(m_buttons.size()) - 1; i >= 0; --i) { auto& c = m_buttons[size_t(i)]; x -= c->preferredWidth(); c->setBounds(x, y, c->preferredWidth(), c->preferredHeight()); x -= 8; }
    m_linkRect = m_spec.link.isEmpty() ? juce::Rectangle<int>() : juce::Rectangle<int>(pad, y, ui::width(m_spec.link, ui::font(false, 12.5f)) + 4, 24);
    m_height = y + 24 + 14;
}

void FormDialog::paint(juce::Graphics& g)
{
    using R = DialogSpec::Row;
    g.fillAll(lcd::paper);
    g.setColour(lcd::ink); g.fillRect(0, 0, getWidth(), 30);
    drawLcdText(g, spec::kFontBold8, m_spec.title.toUpperCase().toRawUTF8(), 8, 7, 2, lcd::paper);
    ui::text(g, m_spec.sub, 8 + LcdCanvas::textWidth(spec::kFontBold8, m_spec.title.toUpperCase().toRawUTF8()) * 2 + 14, 0, 30, lcd::paper.withAlpha(0.8f), ui::font(false, 12.0f));
    for (size_t i = 0; i < m_spec.rows.size(); ++i) {
        const auto& row = m_spec.rows[i];
        const auto r = m_rects[i];
        const float alpha = row.disabled ? 0.4f : 1.0f;
        switch (row.kind) {
        case R::Text: ui::wrapped(g, row.a, r, lcd::ink, ui::font(false, 13.0f)); break;
        case R::Bold: ui::wrapped(g, row.a, r, lcd::ink, ui::font(true, 13.0f)); break;
        case R::Dim: ui::wrapped(g, row.a, r, kDim(), ui::font(false, 12.0f)); break;
        case R::Bullet: ui::text(g, "-", r.getX() + 8, r.getY(), 16, lcd::ink); ui::wrapped(g, row.a, r.withTrimmedLeft(22), lcd::ink, ui::font(false, 12.5f)); break;
        case R::Check: ui::text(g, row.a, r.getX(), r.getY(), 18, lcd::ink, ui::font(true, 13.0f)); ui::wrapped(g, row.b, r.withTrimmedLeft(34), lcd::ink, ui::font(false, 13.0f)); break;
        case R::Option: {
            const bool on = row.id == m_spec.selected;
            if (on) { g.setColour(lcd::ink); g.drawRect(r, 2); } else ui::dottedFrame(g, r, lcd::ink.withAlpha(alpha));
            const juce::Rectangle<int> radio(r.getX() + 12, r.getY() + 12, 14, 14);
            g.setColour(lcd::ink.withAlpha(alpha)); g.drawRect(radio, 2);
            if (on) g.fillRect(radio.reduced(4));
            ui::text(g, row.a, r.getX() + 38, r.getY() + 8, 18, lcd::ink.withAlpha(alpha), ui::font(true, 13.0f));
            ui::wrapped(g, row.b, {r.getX() + 38, r.getY() + 28, r.getWidth() - 46, r.getHeight() - 30}, kDim().withMultipliedAlpha(alpha), ui::font(false, 12.0f));
            break;
        }
        case R::Field: ui::text(g, row.a, r.getX(), r.getY(), 26, lcd::ink, ui::font(true, 13.0f)); break;
        case R::Ticks: ui::text(g, row.a, r.getX(), r.getY(), 16, lcd::ink, ui::font(true, 12.5f)); break;
        case R::Steps: {
            juce::StringArray steps; steps.addTokens(row.a, "|", "");
            int x = r.getX();
            for (int k = 0; k < steps.size(); ++k) {
                const int sw = ui::width(steps[k], ui::font(false, 11.5f)) + 16;
                const juce::Rectangle<int> sr(x, r.getY(), sw, 20);
                const bool on = k == row.b.getIntValue();
                if (on) { g.setColour(lcd::ink); g.fillRect(sr); } else ui::dottedFrame(g, sr, lcd::ink);
                g.setColour(on ? lcd::paper : lcd::ink); g.setFont(ui::font(false, 11.5f));
                g.drawText(steps[k], sr, juce::Justification::centred, false);
                x += sw + 6;
            }
            break;
        }
        case R::Gap: break;
        }
    }
    if (!m_linkRect.isEmpty()) { ui::text(g, m_spec.link, m_linkRect.getX(), m_linkRect.getY(), 24, kDim(), ui::font(false, 12.5f)); g.setColour(kDim()); g.fillRect(m_linkRect.getX(), m_linkRect.getY() + 20, m_linkRect.getWidth() - 4, 1); }
}

void FormDialog::mouseDown(const juce::MouseEvent& e)
{
    if (m_linkRect.contains(e.getPosition())) { if (onButton) onButton("link"); return; }
    for (size_t i = 0; i < m_spec.rows.size(); ++i) {
        const auto& row = m_spec.rows[i];
        if (row.kind == DialogSpec::Row::Option && !row.disabled && m_rects[i].contains(e.getPosition())) {
            m_spec.selected = row.id;
            repaint();
            if (onOption) onOption(juce::String(row.id));
            return;
        }
    }
}

void FormDialog::mouseMove(const juce::MouseEvent& e)
{
    bool hand = m_linkRect.contains(e.getPosition());
    for (size_t i = 0; i < m_spec.rows.size() && !hand; ++i) hand = m_spec.rows[i].kind == DialogSpec::Row::Option && !m_spec.rows[i].disabled && m_rects[i].contains(e.getPosition());
    setMouseCursor(hand ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

ParamsDialog::ParamsDialog()
{
    addAndMakeVisible(m_pages);
    addAndMakeVisible(m_close);
    m_close.setGhost(true);
    m_close.onClick = [this] { if (onClose) onClose(); };
}

void ParamsDialog::set(const Kit& kit, int track, const juce::String& title, const juce::String& right)
{
    m_pages.set(kit, track);
    m_pages.setTitle(title, right);
}

void ParamsDialog::resized()
{
    m_pages.setBounds(12, 12, getWidth() - 24, m_pages.preferredHeight());
    m_close.setBounds(getWidth() - 12 - m_close.preferredWidth(), getHeight() - 10 - m_close.preferredHeight(), m_close.preferredWidth(), m_close.preferredHeight());
}

void ModalLayer::show(juce::Component* dialog, int w, int h)
{
    if (m_dialog && m_dialog != dialog) removeChildComponent(m_dialog);
    m_dialog = dialog; m_w = w; m_h = h;
    addAndMakeVisible(dialog);
    setVisible(true);
    toFront(true);
    resized();
    repaint();
}

void ModalLayer::hide()
{
    if (m_dialog) removeChildComponent(m_dialog);
    m_dialog = nullptr;
    setVisible(false);
}

void ModalLayer::relayout(int w, int h) { m_w = w; m_h = h; resized(); repaint(); }

void ModalLayer::resized() { if (m_dialog) m_dialog->setBounds(getLocalBounds().withSizeKeepingCentre(m_w, juce::jmin(m_h, getHeight() - 40))); }

void ModalLayer::paint(juce::Graphics& g)
{
    g.fillAll(lcd::paper.withAlpha(0.82f));
    if (!m_dialog) return;
    const auto r = m_dialog->getBounds();
    g.setColour(lcd::ink);
    g.fillRect(r.translated(8, 8));
    g.fillRect(r.expanded(2));
}

} // namespace mnm::app
