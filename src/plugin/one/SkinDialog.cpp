#include "SkinDialog.h"

namespace mnm::plugin::one {

namespace {
void frame(LcdCanvas& cv, juce::Rectangle<int> r, bool on = true)
{
    cv.fillRect(r.getX(), r.getY(), r.getWidth(), 1, on); cv.fillRect(r.getX(), r.getBottom() - 1, r.getWidth(), 1, on);
    cv.fillRect(r.getX(), r.getY(), 1, r.getHeight(), on); cv.fillRect(r.getRight() - 1, r.getY(), 1, r.getHeight(), on);
}
}

void SkinDialog::open()
{
    m_before = skin::current();
    m_text[0] = skin::hexOf(skin::savedCustomInk());
    m_text[1] = skin::hexOf(skin::savedCustomPaper());
    m_field = 0;
    m_error.clear();
    setVisible(true);
    toFront(false);
    grabKeyboardFocus();
    preview();
}

void SkinDialog::preview()
{
    juce::Colour ink, paper;
    const bool ok = skin::parseHex(m_text[0], ink) && skin::parseHex(m_text[1], paper);
    if (ok) { skin::apply(skin::presetSkin(skin::Preset::Custom, ink, paper)); m_error.clear(); }
    if (onChanged) onChanged();
    repaint();
}

void SkinDialog::finish(bool keep)
{
    juce::Colour ink, paper;
    if (keep) {
        if (!skin::parseHex(m_text[0], ink) || !skin::parseHex(m_text[1], paper)) { m_error = "SIX HEX DIGITS PER COLOUR, E.G. 1A2B3C"; repaint(); return; }
        if (ink == paper) { m_error = "INK AND PAPER MUST DIFFER"; repaint(); return; }
        const auto s = skin::presetSkin(skin::Preset::Custom, ink, paper);
        skin::apply(s);
        skin::save(s);
    } else {
        skin::apply(m_before);
    }
    setVisible(false);
    if (onChanged) onChanged();
    if (onDone) onDone();
}

void SkinDialog::paint(juce::Graphics& g)
{
    g.fillAll(lcd::paper.withAlpha(0.8f));
    const auto b = box();
    LcdCanvas cv(kLcdW, kLcdH);
    frame(cv, {0, 0, kLcdW, kLcdH}); frame(cv, {1, 1, kLcdW - 2, kLcdH - 2});
    cv.fillRect(0, 0, kLcdW, 15, true);
    cv.text(spec::kFontBold8, "CUSTOM SKIN", 6, 4, false);

    static const char* labels[2] = {"INK", "PAPER"};
    for (int i = 0; i < 2; ++i) {
        const int y = 24 + i * 20;
        cv.text(spec::kFontSmall4x5, labels[i], 8, y + 5, true);
        m_fields[i] = {40, y, 78, 15};
        frame(cv, m_fields[i]);
        if (i == m_field) frame(cv, m_fields[i].expanded(1));
        cv.text(spec::kFontBold8, m_text[i].toRawUTF8(), m_fields[i].getX() + 5, y + 4, true);
        if (i == m_field) cv.fillRect(m_fields[i].getX() + 5 + LcdCanvas::textWidth(spec::kFontBold8, m_text[i].toRawUTF8()) + (m_text[i].isEmpty() ? 0 : 2), y + 11, 5, 1, true);
    }
    // swatch: a knob cell as the pages draw it, so the pair is judged on the real thing
    const int sx = 132, sy = 22;
    frame(cv, {sx, sy, 100, 40});
    cv.text(spec::kFontTiny3x5, "PREVIEW", sx + 4, sy + 4, true);
    cv.blit(spec::kDialRing, sx + 8, sy + 14); cv.blit(*spec::kDialDot[100], sx + 8 + kDotX, sy + 14 + kDotY);
    cv.text(spec::kFontBold8, "SWAVE", sx + 26, sy + 15, true);
    cv.fillRect(sx + 66, sy + 12, 30, 15, true);
    cv.text(spec::kFontBold8, "SAW", sx + 70, sy + 16, false);

    cv.text(spec::kFontSmall4x5, m_error.isNotEmpty() ? m_error.toRawUTF8() : "TYPE HEX RGB. THE PAGE FOLLOWS AS YOU TYPE.", 8, 66, true);
    m_cancel = {kLcdW - 108, kLcdH - 20, 48, 13};
    m_apply = {kLcdW - 56, kLcdH - 20, 48, 13};
    frame(cv, m_cancel); cv.textCentred(spec::kFontBold8, "CANCEL", m_cancel.getX(), m_cancel.getWidth(), m_cancel.getY() + 3, true);
    cv.fillRect(m_apply.getX(), m_apply.getY(), m_apply.getWidth(), m_apply.getHeight(), true);
    cv.textCentred(spec::kFontBold8, "APPLY", m_apply.getX(), m_apply.getWidth(), m_apply.getY() + 3, false);
    cv.draw(g, b.getX(), b.getY(), kS);
}

void SkinDialog::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto b = box();
    if (!b.contains(e.getPosition())) { finish(false); return; }
    const auto lcd = (e.getPosition() - b.getPosition()) / kS;
    if (m_cancel.contains(lcd)) { finish(false); return; }
    if (m_apply.contains(lcd)) { finish(true); return; }
    for (int i = 0; i < 2; ++i) if (m_fields[i].contains(lcd)) { m_field = i; repaint(); return; }
}

bool SkinDialog::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { finish(false); return true; }
    if (k == juce::KeyPress::returnKey) { finish(true); return true; }
    if (k == juce::KeyPress::tabKey || k == juce::KeyPress::downKey || k == juce::KeyPress::upKey) { m_field = 1 - m_field; repaint(); return true; }
    auto& s = m_text[m_field];
    if (k == juce::KeyPress::backspaceKey) { s = s.dropLastCharacters(1); preview(); return true; }
    const auto c = juce::CharacterFunctions::toUpperCase(k.getTextCharacter());
    if (!k.getModifiers().isCommandDown() && juce::String("0123456789ABCDEF#").containsChar(c) && s.length() < 7) { s += juce::String::charToString(c); preview(); }
    return true;   // modal: nothing reaches the host while the dialog is up
}

} // namespace mnm::plugin::one
