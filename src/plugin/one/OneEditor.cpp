#include "OneEditor.h"
#include "ShnolkLogo.h"
#include "ParamDisplay.h"
#include "MachineText.h"
#include "Transfer.h"
#include <cmath>
#include <cstring>

namespace mnm::plugin::one {

using namespace host;

// ---------------------------------------------------------------------------
// KnobCell

KnobCell::KnobCell()
{
    setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(160);
    setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);   // drag in any direction
    setOpaque(false);
}

void KnobCell::mouseMove(const juce::MouseEvent& e)
{
    Slider::mouseMove(e);
    setMouseCursor(m_valueArea.contains(e.getPosition()) ? m_valueCursor : juce::MouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor));
}

void KnobCell::mouseUp(const juce::MouseEvent& e)
{
    Slider::mouseUp(e);
    // a click that did not turn the knob, released on the value row, edits the value
    if (onValueClick && !e.mouseWasDraggedSinceMouseDown() && e.getNumberOfClicks() == 1
        && m_valueArea.contains(e.getMouseDownPosition()) && m_valueArea.contains(e.getPosition()))
        onValueClick();
}

void KnobCell::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (m_valueArea.contains(e.getPosition())) return;   // double-click on the value is editing, not reset
    Slider::mouseDoubleClick(e);
}

// ---------------------------------------------------------------------------
// KnobPage

KnobPage::KnobPage(juce::AudioProcessorValueTreeState& apvts, const char* title) : m_apvts(apvts), m_title(title)
{
    for (int k = 0; k < 8; ++k) {
        auto& c = m_cells[size_t(k)];
        c.onValueChange = [this] { repaint(); };
        c.onValueClick = [this, k] {
            const auto d = m_params[size_t(k)].display;
            if (d == spec::Display::Numeric || d == spec::Display::Bipolar) beginEdit(k);
            else showValueList(k);
        };
        addAndMakeVisible(c);
    }
    m_editor.setJustification(juce::Justification::centred);
    m_editor.setInputRestrictions(4, "0123456789+-");
    m_editor.setSelectAllWhenFocused(true);
    m_editor.setFont(juce::Font(juce::FontOptions(float(4 * kScale))));
    m_editor.setIndents(2, 1);
    m_editor.onReturnKey = [this] { endEdit(true); };
    m_editor.onEscapeKey = [this] { endEdit(false); };
    m_editor.onFocusLost = [this] { endEdit(true); };
    addChildComponent(m_editor);
    setOpaque(true);
}

void KnobPage::beginEdit(int k)
{
    if (m_editing >= 0) endEdit(true);
    const auto& p = m_params[size_t(k)];
    m_editing = k;
    m_editor.setText(valueText(p, int(std::lround(m_cells[size_t(k)].getValue()))), false);
    const int x0 = (k % 4) * kCell, y0 = overhangRows() + kGridY + (k / 4) * kCell;
    m_editor.setBounds(knobValueBox(x0, y0) * kScale);
    m_editor.setVisible(true);
    m_editor.grabKeyboardFocus();
    m_editor.selectAll();
    // clicks that land on components which do not take keyboard focus (the knobs, the page itself)
    // never make the editor lose focus, so watch the whole window for the next press
    if (auto* top = getTopLevelComponent()) { m_listenedTop = top; top->addMouseListener(this, true); }
    repaint();
}

void KnobPage::mouseMove(const juce::MouseEvent& e)
{
    bool overTab = false;
    if (e.eventComponent == this && !m_tabs.isEmpty()) {
        const auto lcd = e.getPosition() / kScale;
        for (int t = 0; t < m_tabs.size() && t < int(m_tabRects.size()); ++t)
            if (t != m_tab && m_tabRects[size_t(t)].contains(lcd)) overTab = true;
    }
    setMouseCursor(overTab ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void KnobPage::mouseDown(const juce::MouseEvent& e)
{
    if (m_editing >= 0) {
        auto* c = e.eventComponent;
        if (c == &m_editor || m_editor.isParentOf(c)) return;
        endEdit(true);
    }
    if (e.eventComponent == this && !m_tabs.isEmpty()) {   // a press on the title bar switches tabs
        const auto lcd = e.getPosition() / kScale;
        for (int t = 0; t < m_tabs.size() && t < int(m_tabRects.size()); ++t)
            if (m_tabRects[size_t(t)].contains(lcd) && t != m_tab) {
                m_tab = t;
                if (m_onTab) m_onTab(t);
                repaint();
                return;
            }
    }
}

void KnobPage::endEdit(bool commit)
{
    if (m_editing < 0) return;
    const int k = m_editing;
    m_editing = -1;
    if (m_listenedTop != nullptr) m_listenedTop->removeMouseListener(this);
    m_listenedTop = nullptr;
    m_editor.setVisible(false);
    const juce::String txt = m_editor.getText().trim();
    if (commit && txt.isNotEmpty() && txt.containsAnyOf("0123456789")) {
        const auto& p = m_params[size_t(k)];
        int v = txt.getIntValue();   // "+3" -> 3, "-19" -> -19
        // out-of-range input snaps to the displayed range's limits
        v = p.display == spec::Display::Bipolar ? juce::jlimit(-64, 63, v) + 64 : juce::jlimit(0, int(p.maxRaw), v);
        m_cells[size_t(k)].setValue(double(v), juce::sendNotificationSync);
    }
    repaint();
}

void KnobPage::showValueList(int k)
{
    refreshDynamic();
    const auto& p = m_params[size_t(k)];
    if (p.display != spec::Display::List && p.display != spec::Display::Readout) return;
    const int raw = int(std::lround(m_cells[size_t(k)].getValue()));
    const int current = p.display == spec::Display::List ? spec::listIndex(raw, p.valueCount) : raw;
    juce::PopupMenu m;
    for (int i = 0; i < p.valueCount; ++i) m.addItem(i + 1, p.values[i], true, i == current);
    const bool list = p.display == spec::Display::List;
    const int n = p.valueCount;
    const int x0 = (k % 4) * kCell, y0 = overhangRows() + kGridY + (k / 4) * kCell;
    const auto anchor = (knobValueBox(x0, y0) * kScale) + getScreenPosition();
    m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(anchor).withMinimumWidth(kCell * kScale),
        [this, k, list, n](int r) {
            if (r <= 0) return;
            const int idx = r - 1;
            m_cells[size_t(k)].setValue(double(list ? spec::listRawMid(idx, n) : idx), juce::sendNotificationSync);
        });
}

// A page's cells are repainted when any of its values change, so DEST follows PAGE live.


void KnobPage::bind(const spec::Param* params8, const std::function<juce::String(int)>& paramId)
{
    for (int k = 0; k < 8; ++k) {
        auto& cell = m_cells[size_t(k)];
        m_attach[size_t(k)].reset();
        m_params[size_t(k)] = params8[k];
        const auto d = params8[k].display;
        const bool blank = d == spec::Display::Blank;
        cell.setVisible(!blank);   // a blank knob is dead on the hardware: nothing drawn, nothing to turn
        const bool dial = d == spec::Display::Numeric || d == spec::Display::Bipolar;
        cell.setValueArea(juce::Rectangle<int>(0, (kValueBoxY - 1) * kScale, (kCell - 1) * kScale, kValueBoxH * kScale),
                          dial ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::PointingHandCursor);
        m_attach[size_t(k)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(m_apvts, paramId(k), cell);
        cell.setDoubleClickReturnValue(true, double(params8[k].defaultRaw));
    }
    endEdit(false);
    repaint();
}

void KnobPage::setTabs(const juce::StringArray& names, int initial, std::function<void(int)> onTab)
{
    m_tabs = names;
    m_tab = juce::jlimit(0, juce::jmax(0, names.size() - 1), initial);
    m_onTab = std::move(onTab);
    repaint();
}

void KnobPage::refreshDynamic()
{
    for (int k = 0; k < 8; ++k)
        if (m_valuesFn[size_t(k)]) m_params[size_t(k)].values = m_valuesFn[size_t(k)]();
}

// One tab of the title bar, x..x+w-1 wide: its rounded top stands in the gap above the bar (kTabOverhang
// rows) so both tabs read as tabs. The active tab's face is paper: an ink cap where it stands on paper,
// then the face runs through the bar and out of its bottom into the content, with no edge in between.
// The inactive tab's face is the bar itself: a black bump above the bar, and inside it two paper edge
// lines that stop a row short of the bar's bottom edge, so the tab reads as tucked behind the header.
static void drawTab(LcdCanvas& cv, int x, int w, int barY, bool active)
{
    const int capY = barY - KnobPage::kTabOverhang;
    if (active) {
        for (int c = x + 2; c <= x + w - 3; ++c) cv.set(c, capY, true);          // cap: top line ...
        cv.set(x + 1, capY + 1, true); cv.set(x + w - 2, capY + 1, true);          // ... and rounded corners
        for (int r = barY; r <= barY + KnobPage::kTitleH; ++r)                     // face: through the bar and the blank row
            for (int c = x + 1; c <= x + w - 2; ++c) cv.set(c, r, false);
    } else {
        for (int c = x + 2; c <= x + w - 3; ++c) cv.set(c, capY, true);          // solid cap, same silhouette
        for (int c = x + 1; c <= x + w - 2; ++c) cv.set(c, capY + 1, true);
        for (int r = barY; r <= barY + KnobPage::kTitleH - 2; ++r) { cv.set(x, r, false); cv.set(x + w - 1, r, false); }
    }
}

void KnobPage::paint(juce::Graphics& g)
{
    refreshDynamic();
    const int oy = overhangRows();
    LcdCanvas cv(kLcdW, oy + kLcdH);
    cv.fillRect(0, oy, kLcdW, kTitleH, true);   // the full-width title bar, on every page
    if (m_tabs.isEmpty()) cv.text(spec::kFontBold8, m_title.toRawUTF8(), 2, oy + 1, false);
    else {
        int x = 0;
        for (int t = 0; t < m_tabs.size() && t < int(m_tabRects.size()); ++t) {
            const juce::String name = m_tabs[t].toUpperCase();
            const int tw = LcdCanvas::textWidth(spec::kFontBold8, name.toRawUTF8()) + 6;   // 1 px edge + 2 px padding each side
            m_tabRects[size_t(t)] = {x, 0, tw, oy + kTitleH};
            drawTab(cv, x, tw, oy, t == m_tab);
            cv.text(spec::kFontBold8, name.toRawUTF8(), x + 3, oy + 1, t == m_tab);   // title position; ink on the paper face, paper on the bar
            x += tw + 2;
        }
    }
    if (m_badge.isNotEmpty()) {   // paper box with ink text inside the inverted bar
        const int bw = LcdCanvas::textWidth(spec::kFontBold8, m_badge.toRawUTF8()) + 4;
        cv.fillRect(kLcdW - bw - 1, oy + 1, bw, kTitleH - 2, false);
        cv.text(spec::kFontBold8, m_badge.toRawUTF8(), kLcdW - bw + 1, oy + 1, true);
    }
    for (int k = 0; k < 8; ++k) {
        const auto& p = m_params[size_t(k)];
        const auto& cell = m_cells[size_t(k)];
        const int x0 = (k % 4) * kCell, y0 = oy + kGridY + (k / 4) * kCell;
        const int raw = int(std::lround(cell.getValue()));
        const bool hot = cell.isVisible() && (cell.isMouseOverOrDragging() || m_editing == k);
        drawKnobCell(cv, x0, y0, p, raw, hot, m_iconFn[size_t(k)] ? m_iconFn[size_t(k)]() : nullptr);
    }
    cv.dotsV(kLcdW - 1, oy + kGridY, oy + kLcdH - 1);   // grid right edge
    cv.dotsH(0, kLcdW - 1, oy + kLcdH - 1);             // grid bottom edge
    cv.draw(g, 0, 0);
}

void KnobPage::resized()
{
    const int oy = overhangRows();
    for (int k = 0; k < 8; ++k) {
        const int x0 = (k % 4) * kCell, y0 = oy + kGridY + (k / 4) * kCell;
        m_cells[size_t(k)].setBounds((x0 + 1) * kScale, (y0 + 1) * kScale, (kCell - 1) * kScale, (kCell - 1) * kScale);
    }
    if (m_editing >= 0) {
        const int x0 = (m_editing % 4) * kCell, y0 = oy + kGridY + (m_editing / 4) * kCell;
        m_editor.setBounds(knobValueBox(x0, y0) * kScale);
    }
}

// ---------------------------------------------------------------------------
// MachineBar

MachineBar::MachineBar()
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void MachineBar::setParameter(juce::RangedAudioParameter& param)
{
    m_attach = std::make_unique<juce::ParameterAttachment>(param, [this](float v) {
        m_slot = juce::jlimit(0, spec::kNumMachines - 1, int(std::lround(v)));
        if (onContentChanged) onContentChanged();   // re-layout: the block's width follows the name and logo
        repaint();
    });
    m_attach->sendInitialUpdate();
}

int MachineBar::logoWidthLcd() const
{
    const auto& mc = spec::kMachines[m_slot];
    if (const int w = groupLogoWidth(mc.group, kLogoH * kScale); w > 0) return (w + kScale - 1) / kScale;
    const auto* gt = onetext::groupText(mc.group);
    return LcdCanvas::textWidth(spec::kFontBold8, gt ? gt->title : mc.group);
}

int MachineBar::widthLcd() const
{
    const int nameW = LcdCanvas::textWidth(spec::kFontBold8, spec::kMachines[m_slot].name);
    return kLogoX + logoWidthLcd() + kLogoGap + nameW + kArrowGap + kArrowW + kPadR;
}

void MachineBar::paint(juce::Graphics& g)
{
    const int w = getWidth() / kScale, h = getHeight() / kScale;
    LcdCanvas cv(w, h);
    cv.fillRect(0, 0, w, h, true);
    const auto& mc = spec::kMachines[m_slot];
    const int logoW = logoWidthLcd(), nameX = kLogoX + logoW + kLogoGap;
    const bool hasLogo = spec::groupLogo(mc.group) != nullptr;   // drawn after the canvas, at screen resolution
    if (!hasLogo) {   // GND / FX have no logo, and none has one before an OS file is chosen: print the group name
        const auto* gt = onetext::groupText(mc.group);
        cv.text(spec::kFontBold8, gt ? gt->title : mc.group, kLogoX, (h - spec::kFontBold8.h) / 2, false);
    }
    cv.text(spec::kFontBold8, mc.name, nameX, (h - spec::kFontBold8.h) / 2, false);
    // the picker arrow after the name: down when closed, up while the picker is open
    const int ax = nameX + LcdCanvas::textWidth(spec::kFontBold8, mc.name) + kArrowGap, ay = h / 2 - 1;
    for (int r = 0; r < 3; ++r) {
        const int half = m_open ? r : 2 - r;   // half-width of this row: 0,1,2 up / 2,1,0 down
        for (int c = 2 - half; c <= 2 + half; ++c) cv.set(ax + c, ay + r, false);
    }
    if (!mc.supported) {   // incomplete machine: knobs only, no engine path yet
        static const char* tag = "PREVIEW";
        cv.text(spec::kFontTiny3x5, tag, w - 3 - LcdCanvas::textWidth(spec::kFontTiny3x5, tag), h - spec::kFontTiny3x5.h - 2, false);
    }
    cv.draw(g, 0, 0);
    if (hasLogo) drawGroupLogo(g, mc.group, kLogoX * kScale, (h - kLogoH) * kScale / 2, kLogoH * kScale, lcd::paper);
}

void MachineBar::mouseDown(const juce::MouseEvent&)
{
    if (onOpen) onOpen();
}

// ---------------------------------------------------------------------------
// LevelColumn

LevelColumn::LevelColumn()
{
    setSliderStyle(juce::Slider::LinearVertical);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setSliderSnapsToMousePosition(false);
    setMouseDragSensitivity(200);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);   // vertical drag
    setOpaque(true);
}

void LevelColumn::setMeter(float linear)
{
    const float v = juce::jlimit(0.0f, 1.0f, linear);
    if (std::abs(v - m_meter) < 0.01f) return;
    m_meter = v;
    repaint();
}

void LevelColumn::paint(juce::Graphics& g)
{
    const int w = getWidth() / kScale, h = getHeight() / kScale;
    LcdCanvas cv(w, h);
    cv.textCentred(spec::kFontBold8, "LEV", 0, w, 1);
    // dotted frame, as the hardware's LEV column
    const int fx = 0, fy = 11, fw = w, fh = h - fy;
    cv.dotsH(fx, fx + fw - 1, fy); cv.dotsH(fx, fx + fw - 1, fy + fh - 1);
    cv.dotsV(fx, fy, fy + fh - 1); cv.dotsV(fx + fw - 1, fy, fy + fh - 1);
    const int innerY = fy + 2, innerH = fh - 4;
    const double frac = getMaximum() > getMinimum() ? (getValue() - getMinimum()) / (getMaximum() - getMinimum()) : 0.0;
    const int barW = (fw - 7) / 2;
    const int lvlH = int(std::lround(frac * innerH));
    cv.fillRect(fx + 2, innerY + innerH - lvlH, barW, lvlH, true);                 // level: solid bar
    const int metH = int(std::lround(m_meter * innerH));
    for (int y = innerY + innerH - metH; y < innerY + innerH; y += 2)                // output: dotted bar
        for (int x = fx + 3 + barW; x < fx + fw - 2; ++x) cv.set(x, y);
    cv.draw(g, 0, 0);
}

// ---------------------------------------------------------------------------
// BpmReadout

BpmReadout::BpmReadout()
{
    setSliderStyle(juce::Slider::LinearHorizontal);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setSliderSnapsToMousePosition(false);
    setMouseDragSensitivity(300);
    setOpaque(true);
    m_editor.setJustification(juce::Justification::centredRight);
    m_editor.setInputRestrictions(5, "0123456789.");
    m_editor.setSelectAllWhenFocused(true);
    m_editor.setFont(juce::Font(juce::FontOptions(float(6 * kScale))));
    m_editor.setIndents(2, 1);
    m_editor.onReturnKey = [this] { endEdit(true); };
    m_editor.onEscapeKey = [this] { endEdit(false); };
    m_editor.onFocusLost = [this] { endEdit(true); };
    addChildComponent(m_editor);
    setSynced(true);
}

void BpmReadout::setSynced(bool synced)
{
    m_synced = synced;
    if (synced) endEdit(false);
    setMouseCursor(synced ? juce::MouseCursor::NormalCursor : juce::MouseCursor::LeftRightResizeCursor);
    repaint();
}

void BpmReadout::setHostBpm(float bpm)
{
    if (std::abs(bpm - m_hostBpm) < 0.05f) return;
    m_hostBpm = bpm;
    if (m_synced) repaint();
}

void BpmReadout::paint(juce::Graphics& g)
{
    const int w = getWidth() / kScale, h = getHeight() / kScale;
    LcdCanvas cv(w, h);
    const juce::String s(double(m_synced ? m_hostBpm : float(getValue())), 1);
    const int tw = LcdCanvas::tallDigitsWidth(s.toRawUTF8());
    cv.tallDigits(s.toRawUTF8(), w - tw, (h - 10) / 2);
    cv.draw(g, 0, 0);
}

void BpmReadout::resized() { m_editor.setBounds(getLocalBounds()); }

void BpmReadout::mouseDown(const juce::MouseEvent& e) { if (!m_synced && !m_editing) Slider::mouseDown(e); }
void BpmReadout::mouseDrag(const juce::MouseEvent& e) { if (!m_synced && !m_editing) Slider::mouseDrag(e); }

void BpmReadout::mouseUp(const juce::MouseEvent& e)
{
    if (m_synced || m_editing) return;
    Slider::mouseUp(e);
    // a click that did not drag the value types a new one
    if (!e.mouseWasDraggedSinceMouseDown() && e.getNumberOfClicks() == 1) beginEdit();
}

void BpmReadout::beginEdit()
{
    m_editing = true;
    m_editor.setText(juce::String(getValue(), 1), false);
    m_editor.setVisible(true);
    m_editor.grabKeyboardFocus();
    m_editor.selectAll();
}

void BpmReadout::endEdit(bool commit)
{
    if (!m_editing) return;
    m_editing = false;
    m_editor.setVisible(false);
    const juce::String txt = m_editor.getText().trim();
    if (commit && txt.containsAnyOf("0123456789"))
        setValue(juce::jlimit(getMinimum(), getMaximum(), txt.getDoubleValue()), juce::sendNotificationSync);   // clamped to 30..300
    repaint();
}

// ---------------------------------------------------------------------------
// LcdText / LcdButton

LcdText::LcdText(const spec::Font& font, juce::String text, int scale, bool inverted, juce::Justification just)
    : m_font(font), m_text(std::move(text)), m_scale(scale), m_inverted(inverted), m_just(just) {}

void LcdText::paint(juce::Graphics& g)
{
    const int w = juce::jmax(1, getWidth() / m_scale), h = juce::jmax(1, getHeight() / m_scale);
    LcdCanvas cv(w, h);
    if (m_inverted) cv.fillRect(0, 0, w, h, true);
    const juce::String caps = m_text.toUpperCase();   // the LCD fonts have no lowercase
    const int tw = LcdCanvas::textWidth(m_font, caps.toRawUTF8());
    const int x = m_just.testFlags(juce::Justification::right) ? w - tw - 1
                : m_just.testFlags(juce::Justification::horizontallyCentred) ? (w - tw) / 2 : 1;
    cv.text(m_font, caps.toRawUTF8(), x, (h - m_font.h) / 2, !m_inverted);
    cv.draw(g, 0, 0, m_scale);
}

void LcdButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const int w = getWidth() / kScale, h = getHeight() / kScale;
    LcdCanvas cv(w, h);
    const bool inv = !down;   // solid block normally, outlined while pressed
    if (inv) cv.fillRect(0, 0, w, h, true);
    else { cv.dotsH(0, w - 1, 0); cv.dotsH(0, w - 1, h - 1); cv.dotsV(0, 0, h - 1); cv.dotsV(w - 1, 0, h - 1); }
    cv.textCentred(spec::kFontBold8, getButtonText().toRawUTF8(), 0, w, (h - spec::kFontBold8.h) / 2, !inv);
    cv.draw(g, 0, 0);
    if (highlighted && !down) { g.setColour(lcd::ink.withAlpha(0.15f)); g.fillRect(getLocalBounds()); }
}

void LcdToggle::paintButton(juce::Graphics& g, bool highlighted, bool)
{
    const int w = getWidth() / kScale, h = getHeight() / kScale;
    LcdCanvas cv(w, h);
    const bool on = getToggleState();
    if (on) cv.fillRect(0, 0, w, h, true);
    else { cv.dotsH(0, w - 1, 0); cv.dotsH(0, w - 1, h - 1); cv.dotsV(0, 0, h - 1); cv.dotsV(w - 1, 0, h - 1); }
    cv.textCentred(spec::kFontBold8, getButtonText().toRawUTF8(), 0, w, (h - spec::kFontBold8.h) / 2, !on);
    cv.draw(g, 0, 0);
    if (highlighted) { g.setColour(lcd::ink.withAlpha(0.15f)); g.fillRect(getLocalBounds()); }
}

// ---------------------------------------------------------------------------
// TrackColumn (Six)

TrackColumn::TrackColumn(juce::AudioProcessorValueTreeState& apvts, int numTracks) : m_apvts(apvts), m_numTracks(numTracks)
{
    setOpaque(true);
    refresh();
}

juce::Rectangle<int> TrackColumn::lockBox(int t) const
{
    return {3, t * kCellH + 17, 9, 8};   // bottom left: "L" key
}

juce::Rectangle<int> TrackColumn::muteBox(int t) const
{
    return {kLcdW - 12, t * kCellH + 17, 9, 8};   // bottom right of the cell, under the name row: "M" key
}

void TrackColumn::setPeak(int t, float linear)
{
    const bool active = linear > 0.02f;
    if (t >= 0 && t < m_numTracks && m_active[size_t(t)] != active) { m_active[size_t(t)] = active; repaint(); }
}

void TrackColumn::refresh()
{
    bool changed = false;
    for (int t = 0; t < m_numTracks; ++t) {
        const int slot = juce::jlimit(0, spec::kNumMachines - 1, int(std::lround(m_apvts.getRawParameterValue(machineId(t))->load())));
        const bool mute = m_apvts.getRawParameterValue(muteId(t))->load() >= 0.5f;
        if (m_slot[size_t(t)] != slot || m_mute[size_t(t)] != mute) { m_slot[size_t(t)] = slot; m_mute[size_t(t)] = mute; changed = true; }
    }
    if (changed) repaint();
}

void TrackColumn::paint(juce::Graphics& g)
{
    const int w = getWidth() / kScale, h = getHeight() / kScale;
    LcdCanvas cv(w, h);
    for (int t = 0; t < m_numTracks; ++t) {
        const int y0 = t * kCellH;
        const bool sel = t == m_selected;
        // the cell: an inverted block for the selected track, a dotted frame otherwise
        if (sel) cv.fillRect(0, y0, kLcdW, kCellH, true);
        else {
            cv.dotsH(0, kLcdW - 1, y0); cv.dotsH(0, kLcdW - 1, y0 + kCellH - 1);
            cv.dotsV(0, y0, y0 + kCellH - 1); cv.dotsV(kLcdW - 1, y0, y0 + kCellH - 1);
        }
        const bool ink = !sel;   // drawing colour inside the cell
        // track number (the key's legend) on rows 3-10, the machine's name on rows 12-16 (up to 7 glyphs wide)
        const juce::String num(t + 1);
        cv.text(spec::kFontBold8, num.toRawUTF8(), 3, y0 + 3, ink);
        cv.text(spec::kFontTiny3x5, spec::kMachines[m_slot[size_t(t)]].name, 3, y0 + 12, ink);
        // activity LED, top right: a 4x4 block while the track outputs signal, its outline otherwise
        const int lx = kLcdW - 7, ly = y0 + 4;
        if (m_active[size_t(t)]) cv.fillRect(lx, ly, 4, 4, ink);
        else {
            for (int c = 0; c < 4; ++c) { cv.set(lx + c, ly, ink); cv.set(lx + c, ly + 3, ink); }
            cv.set(lx, ly + 1, ink); cv.set(lx, ly + 2, ink); cv.set(lx + 3, ly + 1, ink); cv.set(lx + 3, ly + 2, ink);
        }
        // LOCK key, bottom left: solid while the track is locked against kit loads
        const auto lb = lockBox(t);
        if (isLocked && isLocked(t)) {
            cv.fillRect(lb.getX(), lb.getY(), lb.getWidth(), lb.getHeight(), ink);
            cv.text(spec::kFontTiny3x5, "L", lb.getX() + 3, lb.getY() + 2, !ink);
        } else {
            cv.dotsH(lb.getX(), lb.getRight() - 1, lb.getY()); cv.dotsH(lb.getX(), lb.getRight() - 1, lb.getBottom() - 1);
            cv.dotsV(lb.getX(), lb.getY(), lb.getBottom() - 1); cv.dotsV(lb.getRight() - 1, lb.getY(), lb.getBottom() - 1);
            cv.text(spec::kFontTiny3x5, "L", lb.getX() + 3, lb.getY() + 2, ink);
        }
        if (t == m_dropTarget) { cv.invertRect(1, y0 + 1, kLcdW - 2, 1); cv.invertRect(1, y0 + kCellH - 2, kLcdW - 2, 1); cv.invertRect(1, y0 + 2, 1, kCellH - 4); cv.invertRect(kLcdW - 2, y0 + 2, 1, kCellH - 4); }
        // MUTE key, bottom right: solid when muted, outlined otherwise
        const auto mb = muteBox(t);
        if (m_mute[size_t(t)]) {
            cv.fillRect(mb.getX(), mb.getY(), mb.getWidth(), mb.getHeight(), ink);
            cv.text(spec::kFontTiny3x5, "M", mb.getX() + 3, mb.getY() + 2, !ink);
        } else {
            cv.dotsH(mb.getX(), mb.getRight() - 1, mb.getY()); cv.dotsH(mb.getX(), mb.getRight() - 1, mb.getBottom() - 1);
            cv.dotsV(mb.getX(), mb.getY(), mb.getBottom() - 1); cv.dotsV(mb.getRight() - 1, mb.getY(), mb.getBottom() - 1);
            cv.text(spec::kFontTiny3x5, "M", mb.getX() + 3, mb.getY() + 2, ink);
        }
    }
    cv.draw(g, 0, 0);
}

void TrackColumn::mouseDown(const juce::MouseEvent& e)
{
    const auto lcd = e.getPosition() / kScale;
    const int t = lcd.y / kCellH;
    if (t < 0 || t >= m_numTracks) return;
    if (lockBox(t).contains(lcd)) { if (onLock) onLock(t); repaint(); return; }
    if (muteBox(t).contains(lcd)) {
        if (auto* p = m_apvts.getParameter(muteId(t))) {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->getValue() >= 0.5f ? 0.0f : 1.0f);
            p->endChangeGesture();
        }
        refresh();
        return;
    }
    if (t != m_selected) { setSelected(t); if (onSelect) onSelect(t); }
}

void TrackColumn::mouseMove(const juce::MouseEvent& e)
{
    const auto lcd = e.getPosition() / kScale;
    const int t = lcd.y / kCellH;
    const bool key = t >= 0 && t < m_numTracks && (t != m_selected || muteBox(t).contains(lcd) || lockBox(t).contains(lcd));
    setMouseCursor(key ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

// ---------------------------------------------------------------------------
// OneEditor

OneEditor::OneEditor(MnmOneProcessor& p)
    : AudioProcessorEditor(p), m_proc((skin::apply(skin::load()), p)),   // the skin first: m_lnf reads it when it is built
      m_footerVersion(spec::kFontSmall4x5, kPluginVersion, 2),
      m_footerAlpha(spec::kFontSmall4x5, "BY SHNOLK", 2, false, juce::Justification::centredRight),
      m_bpmLabel(spec::kFontBold8, "BPM", kScale, false, juce::Justification::centredRight),
      m_syn(p.apvts, "SYNTHESIS"), m_amp(p.apvts, "AMPLIFICATION"), m_filt(p.apvts, "FILTER"), m_efx(p.apvts, "EFFECTS"),
      m_lfo1(p.apvts, "LFO1"), m_lfo23(p.apvts, "LFO2"),
      m_missingOs([this] { openOsChooser(); }), m_about(p.isEffect() ? kFxTitle : p.numTracks() > 1 ? kSixTitle : kOneTitle),
      m_lib(p, [this] { return m_track; }), m_strip(p.numTracks() > 1), m_drop(m_lib), m_libPanel(m_lib), m_saveDialog(m_lib)
{
    setLookAndFeel(&m_lnf);
    m_menuButton.onClick = [this] { showConfigMenu(); };
    addAndMakeVisible(m_menuButton);
    m_status.setFont(juce::Font(juce::FontOptions(12.0f)));
    m_fwPath.setFont(juce::Font(juce::FontOptions(12.0f)));
    addChildComponent(m_status);
    addChildComponent(m_fwPath);
    addAndMakeVisible(m_footerVersion);
    addAndMakeVisible(m_footerAlpha);

    addAndMakeVisible(m_machineBar);
    m_machineBar.onOpen = [this] { if (m_picker.isOpen()) m_picker.close(); else { m_libPanel.close(false); m_drop.setVisible(false); m_picker.open(); } };
    m_machineBar.onContentChanged = [this] { resized(); };
    m_picker.onOpenChanged = [this](bool open) { m_machineBar.setOpen(open); };
    if (m_proc.isEffect()) m_picker.setFxOnly();   // Monomodule FX offers the FX machines only
    addMouseListener(this, true);   // to close the picker on a press anywhere else
    for (int t = 0; t < m_proc.numTracks(); ++t) m_proc.apvts.addParameterListener(machineId(t), this);

    for (auto* pg : {&m_syn, &m_amp, &m_filt, &m_efx, &m_lfo1, &m_lfo23}) addAndMakeVisible(pg);
    m_lfo23.setTabs({"LFO2", "LFO3"}, 0, [this](int tab) { bindLfo(m_lfo23, tab + 1); });
    addAndMakeVisible(m_level);
    if (m_proc.numTracks() > 1) {
        m_trackColumn = std::make_unique<TrackColumn>(m_proc.apvts, m_proc.numTracks());
        m_trackColumn->onSelect = [this](int t) { selectTrack(t); };
        m_trackColumn->isLocked = [this](int t) { return m_proc.trackLocked(t); };
        m_trackColumn->onLock = [this](int t) { m_proc.setTrackLocked(t, !m_proc.trackLocked(t)); m_drop.repaint(); };
        addAndMakeVisible(*m_trackColumn);
    }
    bindTrackPages();

    addAndMakeVisible(m_bpmLabel);
    addAndMakeVisible(m_bpm);
    m_bpmAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(m_proc.apvts, bpmId(), m_bpm);
    addAndMakeVisible(m_bpmSync);
    m_bpmSyncAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(m_proc.apvts, bpmSyncId(), m_bpmSync);
    m_bpmSync.onStateChange = [this] { m_bpm.setSynced(m_bpmSync.getToggleState()); };
    m_bpm.setSynced(m_bpmSync.getToggleState());

    // the library: strip in the header, its list, the panel over the pages, the save dialog over everything
    addAndMakeVisible(m_strip);
    m_strip.onPart = [this](PresetStrip::Part part) {
        switch (part) {
            case PresetStrip::Kit:     if (m_drop.isVisible() && m_drop.kits()) m_drop.close(); else openMenu(true); break;
            case PresetStrip::Preset:  if (m_drop.isVisible() && !m_drop.kits()) m_drop.close(); else openMenu(false); break;
            case PresetStrip::Prev:    m_drop.setVisible(false); m_lib.step(-1); break;
            case PresetStrip::Next:    m_drop.setVisible(false); m_lib.step(1); break;
            case PresetStrip::KitSave: m_drop.setVisible(false); m_saveDialog.open(true); break;
            case PresetStrip::Save:    m_drop.setVisible(false); m_saveDialog.open(false); break;
            case PresetStrip::Library: m_drop.setVisible(false); if (m_libPanel.isOpen()) m_libPanel.close(); else openLibrary(); break;
            case PresetStrip::None:    break;
        }
        updateStrip();
    };
    m_drop.onClosed = [this] { updateStrip(); };
    m_drop.onLibrary = [this] { m_libPanel.setTab(m_drop.kits() ? LibraryPanel::Kits : LibraryPanel::Presets); openLibrary(); };
    m_libPanel.onOpenChanged = [this](bool) { updateStrip(); };
    m_libPanel.onPresetDragging = [this](juce::Point<int> screen) {
        if (m_trackColumn) m_trackColumn->setDropTarget(m_trackColumn->trackAt(m_trackColumn->getLocalPoint(nullptr, screen)));
    };
    m_libPanel.onPresetDropped = [this](const std::string& id, juce::Point<int> screen) {
        if (!m_trackColumn) return;
        m_trackColumn->setDropTarget(-1);
        const int t = m_trackColumn->trackAt(m_trackColumn->getLocalPoint(nullptr, screen));
        if (t >= 0) m_lib.loadPreset(id, t);
    };
    m_lib.onLoaded = [this] { afterLibraryLoad(); };
    m_saveDialog.onDone = [this] { updateStrip(); m_libPanel.rebuild(); };

    addChildComponent(m_about);
    addChildComponent(m_picker);
    addChildComponent(m_libPanel);
    addChildComponent(m_drop);
    addChildComponent(m_saveDialog);
    m_skinDialog.onChanged = [this] { skinChanged(); };
    addChildComponent(m_skinDialog);
    addChildComponent(m_missingOs);
    m_missingOs.setVisible(!m_proc.engineReady());

    const int levW = 19 * kScale, gap = 12;
    const int columnW = m_trackColumn ? gap + TrackColumn::kLcdW * kScale : 0;   // Six: the track keys on the right
    setSize(20 + levW + 8 + 3 * KnobPage::kWidth + 2 * gap + columnW, 16 + 16 + 48 + 6 + 30 + 6 + 2 * KnobPage::kHeight + gap);
    startTimerHz(15);
    timerCallback();
}

OneEditor::~OneEditor()
{
    setLookAndFeel(nullptr);
    for (int t = 0; t < m_proc.numTracks(); ++t) m_proc.apvts.removeParameterListener(machineId(t), this);
}

// Binds the machine block, picker, LEV fader and every page to the selected track's parameters.
void OneEditor::bindTrackPages()
{
    const int t = m_track;
    auto& machineParam = *m_proc.apvts.getParameter(machineId(t));
    m_machineBar.setParameter(machineParam);
    m_picker.setParameter(machineParam);
    auto bindShared = [this, t](KnobPage& page, Page which) {
        spec::Param params[8];
        for (int k = 0; k < 8; ++k) params[k] = sharedPageParam(which, k);
        page.bind(params, [t, which](int k) { return pageId(t, which, k); });
    };
    bindShared(m_amp, Page::AMP);
    bindShared(m_filt, Page::FILT);
    bindShared(m_efx, Page::EFX);
    bindLfo(m_lfo1, 0);
    bindLfo(m_lfo23, m_lfo23.currentTab() + 1);
    m_shownSlot = -1;
    rebuildSynPage();
    m_levelAttach.reset();
    m_levelAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(m_proc.apvts, levelId(t), m_level);
}

void OneEditor::selectTrack(int t)
{
    t = juce::jlimit(0, m_proc.numTracks() - 1, t);
    if (t == m_track) return;
    if (m_picker.isOpen()) m_picker.close(false);
    m_drop.setVisible(false);
    m_track = t;
    if (m_trackColumn) m_trackColumn->setSelected(t);
    bindTrackPages();
    resized();   // the machine block's width follows the track's machine
    updateStrip();
    if (m_libPanel.isOpen()) m_libPanel.rebuild();   // "loaded" follows the track
    repaint();
}

void OneEditor::openMenu(bool kits)
{
    m_picker.close(false);
    const auto r = m_strip.partBounds(kits ? PresetStrip::Kit : PresetStrip::Prev).translated(m_strip.getX(), m_strip.getY());
    const int x = juce::jmin(r.getX(), getWidth() - LibraryDrop::kLcdW * LibraryDrop::kS - 10);
    m_drop.open(kits, {x, m_strip.getBottom() - LibraryDrop::kS}, getHeight() - m_strip.getBottom() - 30);
    updateStrip();
}

void OneEditor::openLibrary(bool animate)
{
    m_picker.close(false);
    m_libPanel.open(animate);
    updateStrip();
}

// A load from the library changed machine and values behind the controls' backs.
void OneEditor::afterLibraryLoad()
{
    m_shownSlot = -1;
    rebuildSynPage();
    if (m_trackColumn) m_trackColumn->refresh();
    resized();
    updateStrip();
    if (m_libPanel.isOpen()) m_libPanel.rebuild();
    repaint();
}

void OneEditor::updateStrip()
{
    const auto preset = m_proc.loadedPreset(m_track);
    m_strip.setPreset(preset.valid() ? preset.name : juce::String("INIT"), m_proc.presetModified(m_track), m_track);
    const auto kit = m_proc.loadedKit();
    m_strip.setKit(kit.valid() ? kit.name : juce::String("INIT"), m_proc.kitModified());
    m_strip.setOpen(m_drop.isVisible() ? (m_drop.kits() ? PresetStrip::Kit : PresetStrip::Preset) : PresetStrip::None, m_libPanel.isOpen());
}

// The LFO page: the eight LFO knobs; DEST's names and icon follow the PAGE knob of the same LFO.
void OneEditor::bindLfo(KnobPage& page, int lfo)
{
    const int t = m_track;
    page.bind(spec::kLfoParams, [t, lfo](int k) { return lfoId(t, lfo, k); });
    auto pageIdx = [&page] { return spec::listIndex(page.cellValue(0), 9); };
    page.setValuesSource(1, [pageIdx] { return spec::kLfoDestNames[pageIdx()]; });   // the DEST icon itself marks knob A-H
}

void OneEditor::rebuildSynPage()
{
    const int slot = juce::jlimit(0, spec::kNumMachines - 1, int(m_proc.apvts.getRawParameterValue(machineId(m_track))->load()));
    if (slot == m_shownSlot) return;
    m_shownSlot = slot;
    const auto& m = spec::kMachines[slot];
    const int t = m_track;
    m_syn.bind(m.params, [t](int k) { return synId(t, k); });   // SYN A-H: the same eight parameters, described by the machine
    m_syn.setBadge(m.supported ? nullptr : "PREVIEW");
}

void OneEditor::parameterChanged(const juce::String&, float) { m_synDirty = true; }

void OneEditor::mouseDown(const juce::MouseEvent& e)
{
    auto* c = e.eventComponent;
    if (m_drop.isVisible() && c != &m_drop && c != &m_strip) m_drop.close();
    if (!m_picker.isOpen()) return;
    if (c == &m_picker || m_picker.isParentOf(c) || c == &m_machineBar) return;
    m_picker.close();
}

void OneEditor::applySkin(const skin::Skin& s, bool save)
{
    skin::apply(s);
    if (save) skin::save(s);
    skinChanged();
}

void OneEditor::skinChanged()
{
    m_lnf.applySkin();
    sendLookAndFeelChange();   // every child re-reads the widget colours
    repaint();
}

void OneEditor::showConfigMenu()
{
    juce::PopupMenu m;
    m.addItem(1, "Init Preset");
    m.addItem(2, "Select OS File...");
    m.addItem(5, "Clear OS File Selection", m_proc.firmwarePath().isNotEmpty());
    m.addSeparator();
    m.addItem(6, "Sync BPM to Host", true, m_bpmSync.getToggleState());
    const juce::String trackName = m_proc.numTracks() > 1 ? "Track " + juce::String(m_track + 1) + " " : juce::String();
    if (!m_proc.isEffect()) {   // KIT > ASSIGN > KEY: the filters follow the note unless switched off (an effect plays no notes)
        juce::PopupMenu key;
        key.addItem(20, "LPF Tracks Key (LPF)", true, m_proc.apvts.getRawParameterValue(lpKeyTrackId(m_track))->load() >= 0.5f);
        key.addItem(21, "HPF Tracks Key (HPF)", true, m_proc.apvts.getRawParameterValue(hpKeyTrackId(m_track))->load() >= 0.5f);
        m.addSubMenu(trackName + "Key Tracking", key);
    }
    if (m_proc.numTracks() > 1) {   // KIT > EDIT routing: where an FX machine takes its input from, which mix buses the track feeds
        juce::PopupMenu input;
        const int cur = int(std::lround(m_proc.apvts.getRawParameterValue(inputId(m_track))->load()));
        static const char* inputText[7] = {"Previous Track (NEIBOR)", "Side-Chain Left (INP A)", "Side-Chain Right (INP B)", "Side-Chain Stereo (INP AB)",
                                           "Mix Bus AB", "Mix Bus CD", "Mix Bus EF"};
        for (int i = 0; i < 7; ++i) input.addItem(10 + i, inputText[i], true, cur == i);
        m.addSubMenu(trackName + "FX Input", input);
        juce::PopupMenu out;
        static const char* busText[3] = {"Bus AB", "Bus CD", "Bus EF"};
        for (int b = 0; b < 3; ++b) out.addItem(30 + b, busText[b], true, m_proc.apvts.getRawParameterValue(outBusId(m_track, b))->load() >= 0.5f);
        m.addSubMenu(trackName + "Out Bus", out);
        juce::PopupMenu outputs;
        const int mode = int(std::lround(m_proc.apvts.getRawParameterValue(outputModeId())->load()));
        outputs.addItem(40, "Per Track (Track 1-6)", true, mode == int(OutputMode::Tracks));
        outputs.addItem(41, "Mix Buses (AB, CD, EF on outputs 1-3)", true, mode == int(OutputMode::Buses));
        m.addSubMenu("Plugin Outputs", outputs);
    }
    {   // the two colours of the UI, shared by every Monomodule window
        juce::PopupMenu skins;
        const auto cur = skin::current().preset;
        for (int i = 0; i < skin::kNumPresets; ++i) {
            const auto name = juce::String(skin::kPresetNames[i]).toLowerCase();
            skins.addItem(50 + i, (i == int(skin::Preset::Custom) ? "Custom..." : name.substring(0, 1).toUpperCase() + name.substring(1)), true, cur == skin::Preset(i));
        }
        m.addSubMenu("Skin", skins);
    }
    m.addItem(3, "Show Engine Status", true, m_showStatus);
    m.addSeparator();
    m.addItem(4, "Plugin Info...");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_menuButton), [this](int r) {
        switch (r) {
        case 1: m_proc.initPreset(); break;
        case 2: openOsChooser(); break;
        case 5: m_proc.clearFirmware(); break;
        case 6: m_bpmSync.setToggleState(!m_bpmSync.getToggleState(), juce::sendNotificationSync); break;
        case 10: case 11: case 12: case 13: case 14: case 15: case 16:
            if (auto* p = m_proc.apvts.getParameter(inputId(m_track))) p->setValueNotifyingHost(p->convertTo0to1(float(r - 10)));
            break;
        case 20: case 21:
            if (auto* p = m_proc.apvts.getParameter(r == 20 ? lpKeyTrackId(m_track) : hpKeyTrackId(m_track))) p->setValueNotifyingHost(p->getValue() >= 0.5f ? 0.0f : 1.0f);
            break;
        case 30: case 31: case 32:
            if (auto* p = m_proc.apvts.getParameter(outBusId(m_track, r - 30))) p->setValueNotifyingHost(p->getValue() >= 0.5f ? 0.0f : 1.0f);
            break;
        case 40: case 41:
            if (auto* p = m_proc.apvts.getParameter(outputModeId())) p->setValueNotifyingHost(p->convertTo0to1(float(r - 40)));
            break;
        case 3:
            m_showStatus = !m_showStatus;
            m_status.setVisible(m_showStatus);
            m_fwPath.setVisible(m_showStatus);
            setSize(getWidth(), getHeight() + (m_showStatus ? 36 : -36));   // status lines get their own space
            break;
        case 4: m_about.setVisible(true); m_about.toFront(false); break;
        case 50: case 51: case 52: applySkin(skin::presetSkin(skin::Preset(r - 50)), true); break;
        case 53: m_skinDialog.open(); break;
        default: break;
        }
    });
}

void OneEditor::openOsChooser()
{
    const juce::File current(m_proc.firmwarePath());
    const auto startDir = current.existsAsFile() ? current.getParentDirectory()
                                                 : juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    m_chooser = std::make_unique<juce::FileChooser>("Select Monomachine OS .syx (Elektron_SFX6-60_OS1.32B.syx)", startDir, "*.syx");
    m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (f.existsAsFile()) m_proc.setFirmwarePath(f.getFullPathName());
    });
}

void OneEditor::timerCallback()
{
    if (m_synDirty.exchange(false)) rebuildSynPage();
    if (!m_proc.engineReady() && --m_sharedPollCountdown <= 0) {
        m_proc.refreshSharedOsPath();   // another plugin may have configured the OS file meanwhile
        m_sharedPollCountdown = 30;     // every ~2 s at 15 Hz
    }
    if (--m_skinPollCountdown <= 0) {   // another window chose a skin: follow it
        m_skinPollCountdown = 30;       // every ~2 s at 15 Hz
        if (!m_skinDialog.isVisible()) if (const auto s = skin::load(); s != skin::current()) { skin::apply(s); skinChanged(); }
    }
    if (m_proc.firmwarePath() != m_artPath) {   // OS file chosen or changed: draw with its LCD artwork from now on
        m_artPath = m_proc.firmwarePath();
        if (loadLcdArt(m_artPath)) { resized(); repaint(); }
    }
    const bool ready = m_proc.engineReady();
    if (m_missingOs.isVisible() == ready) m_missingOs.setVisible(!ready);
    const auto st = m_proc.statusText();
    m_missingOs.setStatusMessage(st.startsWith("No Monomachine OS") ? juce::String() : st);
    m_status.setText(st, juce::dontSendNotification);
    m_status.setColour(juce::Label::textColourId, ready ? lcd::ink : lcd::ink.interpolatedWith(juce::Colours::red, 0.6f));
    m_fwPath.setText("OS: " + m_proc.firmwarePath(), juce::dontSendNotification);
    if (ready) m_level.setMeter(m_proc.trackPeak(m_track));
    if (m_trackColumn) {
        m_trackColumn->refresh();
        for (int t = 0; t < m_proc.numTracks(); ++t) m_trackColumn->setPeak(t, ready ? m_proc.trackPeak(t) : 0.0f);
    }
    m_bpm.setHostBpm(m_proc.hostBpm());
    updateStrip();   // "modified" follows the knobs
    if (m_proc.previewPoll()) { m_libPanel.repaint(); m_drop.repaint(); }
}

void OneEditor::paint(juce::Graphics& g)
{
    g.fillAll(lcd::paper);
    drawShnolkLogo(g, m_logoBounds.toFloat(), lcd::ink);
}

// A pixel-aligned dotted frame, two LCD px wide, around the drop target while a library file hovers.
void OneEditor::paintOverChildren(juce::Graphics& g)
{
    if (!m_dragOver || m_dropRect.isEmpty()) return;
    g.setColour(lcd::ink);
    const auto r = m_dropRect;
    for (int x = r.getX(); x < r.getRight(); x += 2 * kScale) {
        g.fillRect(x, r.getY(), kScale, 2 * kScale);
        g.fillRect(x, r.getBottom() - 2 * kScale, kScale, 2 * kScale);
    }
    for (int y = r.getY(); y < r.getBottom(); y += 2 * kScale) {
        g.fillRect(r.getX(), y, 2 * kScale, kScale);
        g.fillRect(r.getRight() - 2 * kScale, y, 2 * kScale, kScale);
    }
}

// ---------------------------------------------------------------------------
// Library drag and drop

using mnm::library::TransferKind;

bool OneEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
        if (mnm::library::isTransferFile(f, TransferKind::Track) || (m_proc.numTracks() > 1 && mnm::library::isTransferFile(f, TransferKind::Kit))) return true;
    return false;
}

int OneEditor::dropTargetTrack(int x, int y) const
{
    if (m_trackColumn && m_trackColumn->getBounds().contains(x, y)) {
        const int t = (y - m_trackColumn->getY()) / (TrackColumn::kCellH * kScale);
        if (t >= 0 && t < m_proc.numTracks()) return t;
    }
    return m_track;
}

juce::Rectangle<int> OneEditor::dropFrame(const juce::StringArray& files, int x, int y) const
{
    const auto pages = juce::Rectangle<int>(m_syn.getX(), m_syn.getY() + m_syn.overhangPx(), m_lfo1.getRight() - m_syn.getX(), m_lfo23.getBottom() - m_syn.getY() - m_syn.overhangPx());
    bool kit = false;
    for (const auto& f : files) kit = kit || mnm::library::isTransferFile(f, TransferKind::Kit);
    if (kit && m_trackColumn) return pages.getUnion(m_trackColumn->getBounds());
    if (m_trackColumn) {
        const int t = dropTargetTrack(x, y);
        if (t != m_track || m_trackColumn->getBounds().contains(x, y))
            return juce::Rectangle<int>(m_trackColumn->getX(), m_trackColumn->getY() + t * TrackColumn::kCellH * kScale, m_trackColumn->getWidth(), TrackColumn::kCellH * kScale);
    }
    return pages;
}

void OneEditor::fileDragEnter(const juce::StringArray& files, int x, int y) { m_dragOver = true; m_dropRect = dropFrame(files, x, y); repaint(); }
void OneEditor::fileDragMove(const juce::StringArray& files, int x, int y) { const auto r = dropFrame(files, x, y); if (r != m_dropRect) { m_dropRect = r; repaint(); } }
void OneEditor::fileDragExit(const juce::StringArray&) { m_dragOver = false; repaint(); }

void OneEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    m_dragOver = false;
    repaint();
    const int t = dropTargetTrack(x, y);
    juce::String errors;
    for (const auto& f : files) {
        juce::String error;
        if (!dropFile(juce::File(f), t, error) && error.isNotEmpty()) errors += error + "\n";
    }
    if (errors.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Monomachine", errors.trim());
}

bool OneEditor::dropFile(const juce::File& file, int track, juce::String& error)
{
    mnm::library::TransferPayload payload;
    if (!mnm::library::readTransferFile(file, payload)) { error = file.getFileName() + " is not a Monomachine track or kit file"; return false; }
    bool ok = false;
    if (payload.kind == TransferKind::Track) {
        ok = m_proc.applyKitTrack(track, payload.kit, payload.track, error);
        if (ok && track != m_track) selectTrack(track);
    } else if (payload.kind == TransferKind::Kit) {
        ok = m_proc.applyKit(payload.kit, error);
    }
    if (ok) { m_shownSlot = -1; rebuildSynPage(); if (m_trackColumn) m_trackColumn->refresh(); }
    return ok;
}

void OneEditor::resized()
{
    auto r = getLocalBounds().reduced(10, 8);
    auto footer = r.removeFromBottom(16);
    m_footerVersion.setBounds(footer.removeFromLeft(200));
    m_footerAlpha.setBounds(footer.removeFromRight(240));

    const int levW = 19 * kScale, top = r.getY();
    auto header = r.removeFromTop(48);
    // the logo fills the LEV column's width (19 LCD px), top edge level with the machine block
    m_logoBounds = header.removeFromLeft(levW).withY(top).withHeight(18 * kScale);
    header.removeFromLeft(8);
    // the machine block hangs from the top edge beside the logo, down to just above the pages
    m_machineBar.setBounds(header.getX(), top, m_machineBar.preferredWidth(), m_machineBar.preferredHeight());
    auto headerTop = header.removeFromTop(30);
    m_menuButton.setBounds(headerTop.removeFromRight(32 * kScale));
    headerTop.removeFromRight(10);
    m_bpmSync.setBounds(headerTop.removeFromRight(28 * kScale));
    headerTop.removeFromRight(8);
    m_bpm.setBounds(headerTop.removeFromRight(62 * kScale));   // "120.0" in the tall digit face is 59 px
    m_bpmLabel.setBounds(headerTop.removeFromRight(22 * kScale));
    {   // the library strip: centred between the machine block and the tempo, as wide as that leaves
        const int x0 = m_machineBar.getRight() + 12, x1 = m_bpmLabel.getX() - 4;
        const int w = m_strip.preferredWidth(juce::jmax(0, x1 - x0));
        m_strip.setBounds(x0 + (x1 - x0 - w) / 2, top, w, PresetStrip::kLcdH * PresetStrip::kS);
    }
    if (m_showStatus) {   // beside the machine block
        const int statusX = m_machineBar.getRight() + 10;
        m_status.setBounds(r.removeFromTop(18).withTrimmedLeft(statusX - r.getX()));
        m_fwPath.setBounds(r.removeFromTop(18).withTrimmedLeft(statusX - r.getX()));
    }
    r.removeFromTop(6);

    auto body = r;
    auto lev = body.removeFromLeft(levW);
    body.removeFromLeft(8);
    body.removeFromTop(36);   // the machine block's lower part and the gap under it
    if (m_trackColumn) {   // Six: the track keys stand beside the pages, as tall as the two rows
        auto col = body.removeFromRight(TrackColumn::kLcdW * kScale);
        body.removeFromRight(12);
        m_trackColumn->setBounds(col.withHeight(m_proc.numTracks() * TrackColumn::kCellH * kScale));
    }
    // LEV: its label sits under the logo and its frame (11 LCD rows down) starts level with the pages
    lev = lev.withTop(body.getY() - 11 * kScale);
    m_level.setBounds(lev.withHeight((lev.getHeight() / kScale) * kScale));

    const int gap = 12;
    auto place = [](juce::Rectangle<int> r, KnobPage& page) { page.setBounds(r.withTop(r.getY() - page.overhangPx())); };   // tabs stand above the page
    auto placeRow = [&](juce::Rectangle<int> row, KnobPage& a, KnobPage& b, KnobPage& c) {
        place(row.removeFromLeft(KnobPage::kWidth), a); row.removeFromLeft(gap);
        place(row.removeFromLeft(KnobPage::kWidth), b); row.removeFromLeft(gap);
        place(row.removeFromLeft(KnobPage::kWidth), c);
    };
    placeRow(body.removeFromTop(KnobPage::kHeight), m_syn, m_amp, m_lfo1);
    body.removeFromTop(gap);
    placeRow(body.removeFromTop(KnobPage::kHeight), m_filt, m_efx, m_lfo23);

    m_picker.setTargetBounds({m_syn.getX(), m_syn.getY(), m_lfo1.getRight() - m_syn.getX(), m_lfo23.getBottom() - m_syn.getY()});   // the six pages' area
    m_libPanel.setTargetBounds({m_syn.getX(), m_syn.getY(), m_lfo1.getRight() - m_syn.getX(), m_lfo23.getBottom() - m_syn.getY()});   // Six: the track keys stay in view
    m_saveDialog.setBounds(getLocalBounds());
    m_skinDialog.setBounds(getLocalBounds());
    m_missingOs.setBounds(getLocalBounds());
    m_about.setBounds(getLocalBounds());
}

} // namespace mnm::plugin::one
