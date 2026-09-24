#include "LibraryUi.h"
#include "MidiExport.h"
#include "SharedSettings.h"
#include <algorithm>

namespace mnm::plugin::one {

using mnm::catalog::Catalog;

namespace {

// Where the Monomodule Library app is installed (see README: Installing): the first candidate that exists,
// else the platform's usual place (which then does not exist either).
juce::File libraryAppFile()
{
    using F = juce::File;
#if JUCE_MAC
    const juce::Array<F> candidates{F("/Applications/Monomodule Library.app"),
                                    F::getSpecialLocation(F::userHomeDirectory).getChildFile("Applications/Monomodule Library.app")};
#elif JUCE_WINDOWS
    // the Windows installer: Program Files, or %LOCALAPPDATA%\Programs for "Install for me only"
    const juce::Array<F> candidates{F::getSpecialLocation(F::globalApplicationsDirectory).getChildFile("Shnolk/Monomodule/Monomodule Library.exe"),
                                    F::getSpecialLocation(F::userApplicationDataDirectory).getParentDirectory()
                                        .getChildFile("Local/Programs/Shnolk/Monomodule/Monomodule Library.exe")};
#else
    const juce::Array<F> candidates{F::getSpecialLocation(F::userHomeDirectory).getChildFile(".local/bin/Monomodule Library"),
                                    F("/usr/local/bin/Monomodule Library"), F("/usr/bin/Monomodule Library")};
#endif
    for (const auto& f : candidates) if (f.exists()) return f;
    return candidates.getFirst();
}

juce::String U(const juce::String& s) { return s.toUpperCase(); }
juce::String U(const std::string& s) { return juce::String(s).toUpperCase(); }

// The text cut to maxW LCD pixels (the faces are proportional).
juce::String fit(const spec::Font& f, juce::String s, int maxW)
{
    while (s.isNotEmpty() && LcdCanvas::textWidth(f, s.toRawUTF8()) > maxW) s = s.dropLastCharacters(1);
    return s;
}

void frame(LcdCanvas& cv, juce::Rectangle<int> r, bool on = true)
{
    cv.fillRect(r.getX(), r.getY(), r.getWidth(), 1, on); cv.fillRect(r.getX(), r.getBottom() - 1, r.getWidth(), 1, on);
    cv.fillRect(r.getX(), r.getY(), 1, r.getHeight(), on); cv.fillRect(r.getRight() - 1, r.getY(), 1, r.getHeight(), on);
}

void dottedFrame(LcdCanvas& cv, juce::Rectangle<int> r)
{
    cv.dotsH(r.getX(), r.getRight() - 1, r.getY()); cv.dotsH(r.getX(), r.getRight() - 1, r.getBottom() - 1);
    cv.dotsV(r.getX(), r.getY(), r.getBottom() - 1); cv.dotsV(r.getRight() - 1, r.getY(), r.getBottom() - 1);
}

// Play triangle (or the stop block while it plays), 5x7, centred in the zone.
void playGlyph(LcdCanvas& cv, juce::Rectangle<int> zone, bool playing, bool on)
{
    const int x = zone.getCentreX() - 2, y = zone.getCentreY() - 3;
    if (playing) { cv.fillRect(x, y + 1, 5, 5, on); return; }
    for (int c = 0; c < 4; ++c) cv.fillRect(x + c, y + c, 1, 7 - 2 * c, on);
}

void pixelIcon(LcdCanvas& cv, const char* const* rows, int n, int x, int y, bool on)
{
    for (int r = 0; r < n; ++r)
        for (int c = 0; rows[r][c]; ++c) if (rows[r][c] == '#') cv.set(x + c, y + r, on);
}

const char* const kIconSave[] = {   // a disk
    "########.", "#.#..#.##", "#.#..#..#", "#.####..#", "#.......#", "#.#####.#", "#.#...#.#", "#.#...#.#", "#########"};
const char* const kIconLibrary[] = {   // four slots
    "####.####", "#..#.#..#", "#..#.#..#", "####.####", ".........", "####.####", "#..#.#..#", "#..#.#..#", "####.####"};

void arrowH(LcdCanvas& cv, int cx, int cy, bool left, bool on)
{
    for (int c = 0; c < 4; ++c) { const int x = left ? cx + 1 - c : cx - 2 + c; const int h = left ? 2 * c + 1 : 7 - 2 * c; cv.fillRect(x, cy - h / 2, 1, h, on); }
}

void caret(LcdCanvas& cv, int x, int y, bool up, bool on)
{
    for (int r = 0; r < 3; ++r) { const int w = up ? 1 + 2 * r : 5 - 2 * r; cv.fillRect(x + (5 - w) / 2, y + r, w, 1, on); }
}

// Text followed by a mark the faces have no glyph for: a 3x3 block (modified, favourite) or a cursor bar.
int textMarked(LcdCanvas& cv, const spec::Font& f, const juce::String& s, int x, int y, bool on, bool block, bool cursor = false)
{
    cv.text(f, s.toRawUTF8(), x, y, on);
    int end = x + LcdCanvas::textWidth(f, s.toRawUTF8());
    if (block) { cv.fillRect(end + 3, y + f.h / 2 - 2, 3, 3, on); end += 6; }
    if (cursor) { cv.fillRect(end + (s.isEmpty() ? 0 : 2), y + f.h - 1, 5, 1, on); end += 7; }
    return end;
}

int slotOfModel(uint8_t model) { const int s = machineSlot(host::Machine(model)); return s < 0 ? 999 : s; }
const spec::Machine* machineOf(uint8_t model) { return spec::machineByIndex(model); }

bool matches(const juce::String& name, const juce::String& query) { return query.isEmpty() || name.containsIgnoreCase(query); }

// Typing edits a short upper-case ASCII string (the faces have no lower case). Returns true when the key was used.
bool typeInto(juce::String& s, const juce::KeyPress& k, int maxLen)
{
    if (k == juce::KeyPress::backspaceKey) { s = s.dropLastCharacters(1); return true; }
    const auto c = k.getTextCharacter();
    if (c >= 32 && c < 127 && !k.getModifiers().isCommandDown()) { if (s.length() < maxLen) s += juce::String::charToString(c).toUpperCase(); return true; }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------------------------- bridge

std::vector<const mnm::catalog::PresetItem*> LibraryBridge::presets(int model, const char* group, bool favourites, bool savedOnly, const juce::String& query) const
{
    std::vector<const mnm::catalog::PresetItem*> out;
    for (const auto& p : cat().presets) {
        if (model >= 0 && p.model != model) continue;
        const auto* m = machineOf(p.model);
        if (!m || machineSlot(host::Machine(p.model)) < 0) continue;
        if (group && juce::String(m->group) != group) continue;
        if (favourites && !favourite(p.id)) continue;
        if (savedOnly && !p.saved) continue;
        if (!matches(juce::String(p.name), query)) continue;
        out.push_back(&p);
    }
    std::stable_sort(out.begin(), out.end(), [](const auto* a, const auto* b) {
        const int sa = slotOfModel(a->model), sb = slotOfModel(b->model);
        return sa != sb ? sa < sb : juce::String(a->name).compareNatural(juce::String(b->name)) < 0; });
    return out;
}

std::vector<const mnm::catalog::KitItem*> LibraryBridge::kits(const juce::String& source, bool favourites, const juce::String& query) const
{
    std::vector<const mnm::catalog::KitItem*> out;
    for (const auto& k : cat().kits) {
        if (favourites && !favourite(k.id)) continue;
        if (!matches(juce::String(k.name), query)) continue;
        if (source == "saved" && !k.saved) continue;
        if (source.isNotEmpty() && source != "saved") {
            bool in = false;
            for (const auto& s : k.sources) in = in || juce::String(s.importId) == source;
            if (!in) continue;
        }
        out.push_back(&k);
    }
    return out;
}

std::vector<const mnm::catalog::PatternItem*> LibraryBridge::patterns(int bank, const juce::String& query) const
{
    std::vector<const mnm::catalog::PatternItem*> out;
    for (const auto& p : cat().patterns) {
        if (bank >= 0 && (p.sources.empty() || p.sources.front().slot / 16 != bank)) continue;
        if (!matches(juce::String(p.name), query)) continue;
        out.push_back(&p);
    }
    return out;
}

bool LibraryBridge::loadPreset(const std::string& id, int t)
{
    const auto* p = cat().preset(id);
    if (!p) return false;
    if (t < 0) t = track();
    juce::String err;
    const bool ok = m_proc.loadPreset(t, Catalog::kitForPreset(*p), 0, juce::String(p->id), U(p->name), err);
    message = ok ? juce::String() : U(err);
    if (onLoaded) onLoaded();
    return ok;
}

bool LibraryBridge::loadKit(const std::string& id)
{
    const auto* k = cat().kit(id);
    if (!k) return false;
    juce::String err;
    const bool ok = m_proc.loadKit(k->kit, juce::String(k->id), U(k->name), err);
    message = ok ? juce::String() : U(err);
    if (onLoaded) onLoaded();
    return ok;
}

bool LibraryBridge::loadPatternKit(const std::string& patternId)
{
    const auto* p = cat().pattern(patternId);
    if (!p || p->kitId.empty()) { message = "THIS PATTERN HAS NO KIT"; return false; }
    return loadKit(p->kitId);
}

void LibraryBridge::step(int dir)
{
    refresh();
    const auto list = presets(int(machineModel()), nullptr, false, false, {});
    if (list.empty()) return;
    const auto cur = m_proc.loadedPreset(track()).id.toStdString();
    int i = -1;
    for (int n = 0; n < int(list.size()); ++n) if (list[size_t(n)]->id == cur) i = n;
    const int n = int(list.size());
    i = i < 0 ? (dir > 0 ? 0 : n - 1) : (i + dir + n) % n;
    loadPreset(list[size_t(i)]->id);
}

void LibraryBridge::preview(const juce::String& kind, const std::string& id)
{
    if (previewing(kind, id)) { m_proc.previewStop(); return; }
    mnm::preview::PreviewOptions opt;
    const auto bpm = double(loadSharedSetting("previewBpm", "120").getFloatValue());
    if (bpm >= 30.0 && bpm <= 300.0) opt.bpm = bpm;
    const auto key = kind + "/" + juce::String(id);
    // the specs are built now (synchronously, from the catalog as it is), so nothing outlives a catalog rebuild
    if (kind == "preset") {
        if (const auto* p = cat().preset(id)) m_proc.previewPlay(key, [p, opt] { return mnm::preview::presetPreview(p->track, p->lpKeyTrack, p->hpKeyTrack, opt); });
    } else if (kind == "pattern") {
        const auto* p = cat().pattern(id);
        const auto* k = p ? cat().kit(p->kitId) : nullptr;
        if (p && k) m_proc.previewPlay(key, [p, k, opt] { return mnm::preview::patternPreview(k->kit, p->pattern, opt); });
        else message = "THIS PATTERN HAS NO KIT";
    } else if (kind == "kit") {
        const auto* k = cat().kit(id);
        if (!k) return;
        const auto best = mnm::preview::choosePreviewPattern(cat(), *k);
        if (const auto* p = best.empty() ? nullptr : cat().pattern(best)) m_proc.previewPlay(key, [p, k, opt] { return mnm::preview::patternPreview(k->kit, p->pattern, opt); });
        else m_proc.previewPlay(key, [k, opt] { return mnm::preview::patternPreview(k->kit, mnm::preview::demoPattern(k->kit), opt); });
    }
    const auto st = m_proc.previewStatus();
    if (st.isNotEmpty()) message = U(st);
}

juce::File LibraryBridge::patternMidiFile(const std::string& patternId)
{
    const auto* p = cat().pattern(patternId);
    if (!p || p->sources.empty()) return {};
    const auto& src = p->sources.front();
    const auto* dump = m_model->state(juce::String(src.importId));
    const auto* pat = dump ? dump->patternAt(src.slot) : nullptr;
    if (!pat) return {};
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MonomoduleDrag");
    dir.createDirectory();
    auto f = dir.getChildFile(juce::File::createLegalFileName(juce::String(p->name)) + ".mid");
    f.deleteFile();
    juce::FileOutputStream os(f);
    if (!os.openedOk() || !mnm::library::buildPatternMidiFile(*dump, *pat).writeTo(os)) return {};
    return f;
}

// ---------------------------------------------------------------------------------------------- strip

void PresetStrip::setPreset(const juce::String& name, bool modified, int track)
{
    if (m_preset == name && m_presetMod == modified && m_track == track) return;
    m_preset = name; m_presetMod = modified; m_track = track; repaint();
}

void PresetStrip::setKit(const juce::String& name, bool modified)
{
    if (m_kit == name && m_kitMod == modified) return;
    m_kit = name; m_kitMod = modified; repaint();
}

void PresetStrip::setOpen(Part menu, bool library)
{
    if (m_menu == menu && m_libOpen == library) return;
    m_menu = menu; m_libOpen = library; repaint();
}

int PresetStrip::preferredWidth(int available) const { return juce::jmin(available, (m_six ? 290 : 190) * kS); }

void PresetStrip::resized()
{
    // joined parts share their edges: each starts on the previous part's last column
    const int w = getWidth() / kS, arrowW = 12, iconW = 15, gap = 4;
    const int fixed = 2 * (arrowW - 1) + 2 * (iconW - 1) + (m_six ? iconW - 1 + gap : 0);
    const int flexible = w - fixed;
    const int kitW = m_six ? juce::jmin(102, flexible / 2) : 0, presetW = flexible - kitW;   // a kit name is ten glyphs
    int x = 0;
    auto take = [&](Part p, int pw) { m_rects[size_t(p)] = {x, 0, pw, kLcdH}; x += pw - 1; };
    for (auto& r : m_rects) r = {};
    if (m_six) { take(Kit, kitW); take(KitSave, iconW); x += 1 + gap; }
    take(Prev, arrowW); take(Preset, presetW); take(Next, arrowW); take(Save, iconW); take(Library, iconW);
}

void PresetStrip::paint(juce::Graphics& g)
{
    LcdCanvas cv(getWidth() / kS, kLcdH);
    auto part = [&](Part p, bool solid) {
        const auto r = m_rects[size_t(p)];
        if (solid) cv.fillRect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), true);
        frame(cv, r);
        if (!solid && m_hover == p) frame(cv, r.reduced(1));
        return r;
    };
    auto selector = [&](Part p, const juce::String& label, const juce::String& name, bool modified) {
        const bool open = m_menu == p;
        const auto r = part(p, open);
        const int lw = LcdCanvas::textWidth(spec::kFontTiny3x5, label.toRawUTF8());
        cv.text(spec::kFontTiny3x5, label.toRawUTF8(), r.getX() + 4, r.getY() + 5, !open);
        const int nx = r.getX() + 4 + lw + 4, maxW = r.getRight() - 10 - nx;
        textMarked(cv, spec::kFontBold8, fit(spec::kFontBold8, name, maxW - (modified ? 7 : 0)), nx, r.getY() + 4, !open, modified);
        caret(cv, r.getRight() - 9, r.getY() + 6, open, !open);
    };
    if (m_six) {
        selector(Kit, "KIT", m_kit, m_kitMod);
        const auto r = part(KitSave, m_kitMod);
        pixelIcon(cv, kIconSave, 9, r.getX() + 3, r.getY() + 3, !m_kitMod);
    }
    auto r = part(Prev, false); arrowH(cv, r.getCentreX(), r.getCentreY(), true, true);
    selector(Preset, m_six ? "T" + juce::String(m_track + 1) : juce::String("PRESET"), m_preset, m_presetMod);
    r = part(Next, false); arrowH(cv, r.getCentreX(), r.getCentreY(), false, true);
    r = part(Save, m_presetMod); pixelIcon(cv, kIconSave, 9, r.getX() + 3, r.getY() + 3, !m_presetMod);
    r = part(Library, m_libOpen); pixelIcon(cv, kIconLibrary, 9, r.getX() + 3, r.getY() + 3, !m_libOpen);
    cv.draw(g, 0, 0, kS);
}

PresetStrip::Part PresetStrip::partAt(juce::Point<int> p) const
{
    const auto lcd = p / kS;
    for (int i = int(m_rects.size()) - 1; i >= 0; --i) if (m_rects[size_t(i)].contains(lcd)) return Part(i);
    return None;
}

void PresetStrip::mouseDown(const juce::MouseEvent& e) { const auto p = partAt(e.getPosition()); if (p != None && onPart) onPart(p); }

void PresetStrip::mouseMove(const juce::MouseEvent& e)
{
    const auto p = partAt(e.getPosition());
    if (p == m_hover) return;
    m_hover = p;
    static const char* const tips[] = {"Kits", "Save this kit to the library", "Previous preset of this machine", "Presets", "Next preset of this machine", "Save this preset to the library", "Library"};
    setTooltip(p == None ? juce::String() : juce::String(tips[int(p)]));
    setMouseCursor(p == None ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    repaint();
}

// ---------------------------------------------------------------------------------------------- dropdown

void LibraryDrop::open(bool kits, juce::Point<int> topLeft, int maxHeight)
{
    m_kits = kits; m_scroll = 0; m_hover = -1;
    m_b.refresh();
    rebuild();
    const int want = (listTop() + kFootH + juce::jmax(3, int(m_rows.size())) * kRowH + 1) * kS;
    setBounds(topLeft.x, topLeft.y, kLcdW * kS, juce::jmin(want, maxHeight));
    // start with the loaded item in view
    for (int i = 0; i < int(m_rows.size()); ++i) if (m_rows[size_t(i)].on) m_scroll = juce::jlimit(0, juce::jmax(0, int(m_rows.size()) - listRows()), i - listRows() / 2);
    setVisible(true);
    toFront(false);
    grabKeyboardFocus();
    repaint();
}

void LibraryDrop::rebuild()
{
    m_rows.clear();
    if (m_kits) {
        const auto cur = m_b.proc().loadedKit().id.toStdString();
        for (const auto* k : m_b.kits({}, false, {})) {
            int tracks = 0;
            for (const auto& pid : k->presetIds) tracks += pid.empty() ? 0 : 1;
            m_rows.push_back({false, k->id, U(k->name), juce::String(tracks) + " TRK  " + (k->saved ? juce::String("SAVED") : U(k->sources.empty() ? std::string() : k->sources.front().importName)), k->id == cur, m_b.favourite(k->id)});
        }
        return;
    }
    const auto cur = m_b.proc().loadedPreset(m_b.track()).id.toStdString();
    const int model = int(m_b.machineModel());
    m_sameCount = int(m_b.presets(model, nullptr, false, false, {}).size());
    m_allCount = int(m_b.presets(-1, nullptr, false, false, {}).size());
    int last = -1;
    for (const auto* p : m_b.presets(m_all ? -1 : model, nullptr, false, false, {})) {
        if (m_all && int(p->model) != last) { last = p->model; const auto* m = machineOf(p->model); Row h; h.header = true; h.name = m ? m->displayName : "?"; m_rows.push_back(h); }
        m_rows.push_back({false, p->id, U(p->name), p->saved ? juce::String("SAVED") : U(p->sources.empty() ? std::string() : p->sources.front().importName), p->id == cur, m_b.favourite(p->id)});
    }
}

int LibraryDrop::rowAt(juce::Point<int> lcd) const
{
    const int y = lcd.y - listTop();
    if (y < 0 || lcd.y >= getHeight() / kS - kFootH) return -1;
    const int i = m_scroll + y / kRowH;
    return i >= 0 && i < int(m_rows.size()) && y / kRowH < listRows() ? i : -1;
}

void LibraryDrop::paint(juce::Graphics& g)
{
    const int w = getWidth() / kS, h = getHeight() / kS;
    LcdCanvas cv(w, h);
    frame(cv, {0, 0, w, h});
    if (!m_kits) {   // scope: this machine | all machines
        const auto* m = machineOf(m_b.machineModel());
        const juce::String a = juce::String(m ? m->displayName : "MACHINE") + " (" + juce::String(m_sameCount) + ")", b = "ALL MACHINES (" + juce::String(m_allCount) + ")";
        const int half = w / 2;
        if (!m_all) cv.fillRect(1, 1, half - 1, kHeadH - 2, true); else cv.fillRect(half, 1, w - half - 1, kHeadH - 2, true);
        cv.textCentred(spec::kFontSmall4x5, a.toRawUTF8(), 0, half, 5, m_all);
        cv.textCentred(spec::kFontSmall4x5, b.toRawUTF8(), half, w - half, 5, !m_all);
        cv.fillRect(0, kHeadH - 1, w, 1, true);
    }
    for (int n = 0; n < listRows(); ++n) {
        const int i = m_scroll + n;
        if (i >= int(m_rows.size())) break;
        const auto& row = m_rows[size_t(i)];
        const int y = listTop() + n * kRowH;
        if (row.header) { cv.text(spec::kFontSmall4x5, row.name.toRawUTF8(), 4, y + 5, true); cv.dotsH(4 + LcdCanvas::textWidth(spec::kFontSmall4x5, row.name.toRawUTF8()) + 3, w - 5, y + 7); continue; }
        if (row.on) cv.fillRect(1, y, w - 2, kRowH, true);
        else if (i == m_hover) dottedFrame(cv, {1, y, w - 2, kRowH});
        const bool ink = !row.on;
        const bool playing = m_b.previewing(m_kits ? "kit" : "preset", row.id);
        playGlyph(cv, {2, y, 11, kRowH}, playing, ink);
        int nx = 15;
        if (playing) { drawLoopGlyph(cv, 16, y + (kRowH - kLoopGlyphH) / 2, m_b.looping(), ink); nx += 13; }   // the loop toggle beside the stop
        const int rw = LcdCanvas::textWidth(spec::kFontTiny3x5, row.right.toRawUTF8());
        textMarked(cv, spec::kFontBold8, fit(spec::kFontBold8, row.name, w - 22 - rw - (nx - 1)), nx, y + 2, ink, row.fav);
        cv.text(spec::kFontTiny3x5, row.right.toRawUTF8(), w - 6 - rw, y + 4, ink);
    }
    if (m_rows.empty()) cv.textCentred(spec::kFontSmall4x5, "NOTHING IN THE LIBRARY YET", 0, w, listTop() + 8, true);
    if (int(m_rows.size()) > listRows()) {   // scroll position
        const int trackH = listRows() * kRowH, barH = juce::jmax(6, trackH * listRows() / int(m_rows.size()));
        cv.fillRect(w - 3, listTop() + (trackH - barH) * m_scroll / juce::jmax(1, int(m_rows.size()) - listRows()), 2, barH, true);
    }
    const int fy = h - kFootH;
    cv.fillRect(0, fy, w, 1, true);
    juce::String hint = m_kits ? "CLICK = LOAD   GLYPH = AUDITION" : m_all ? "A PRESET OF ANOTHER MACHINE CHANGES THE MACHINE" : "CLICK = LOAD   GLYPH = AUDITION";
    if (m_kits) { juce::StringArray locked; for (int t = 0; t < m_b.proc().numTracks(); ++t) if (m_b.proc().trackLocked(t)) locked.add(juce::String(t + 1)); if (!locked.isEmpty()) hint = "LOCKED: " + locked.joinIntoString(" ") + " KEEP THEIR SOUND"; }
    cv.text(spec::kFontTiny3x5, hint.toRawUTF8(), 4, fy + 5, true);
    const int lw = LcdCanvas::textWidth(spec::kFontSmall4x5, "LIBRARY") + 8;
    cv.fillRect(w - lw - 2, fy + 2, lw, kFootH - 4, true);
    cv.text(spec::kFontSmall4x5, "LIBRARY", w - lw + 2, fy + 5, false);
    cv.draw(g, 0, 0, kS);
}

void LibraryDrop::mouseDown(const juce::MouseEvent& e)
{
    const auto lcd = e.getPosition() / kS;
    const int w = getWidth() / kS, h = getHeight() / kS;
    if (!m_kits && lcd.y < kHeadH) { m_all = lcd.x >= w / 2; m_scroll = 0; rebuild(); repaint(); return; }
    if (lcd.y >= h - kFootH) { if (lcd.x > w - 50) { close(); if (onLibrary) onLibrary(); } return; }
    const int i = rowAt(lcd);
    if (i < 0 || m_rows[size_t(i)].header) return;
    const auto id = m_rows[size_t(i)].id;
    if (lcd.x < 14) { m_b.preview(m_kits ? "kit" : "preset", id); repaint(); return; }
    if (lcd.x < 27 && m_b.previewing(m_kits ? "kit" : "preset", id)) { m_b.toggleLoop(); repaint(); return; }
    if (m_kits) m_b.loadKit(id); else m_b.loadPreset(id);
    close();
}

void LibraryDrop::mouseMove(const juce::MouseEvent& e)
{
    const int i = rowAt(e.getPosition() / kS);
    if (i != m_hover) { m_hover = i; repaint(); }
}

void LibraryDrop::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& d)
{
    const int step = d.deltaY > 0 ? -2 : d.deltaY < 0 ? 2 : 0;
    m_scroll = juce::jlimit(0, juce::jmax(0, int(m_rows.size()) - listRows()), m_scroll + step);
    repaint();
}

bool LibraryDrop::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { close(); return true; }
    return false;
}

// ---------------------------------------------------------------------------------------------- panel

static constexpr int kPanelAnimHz = 60;
static constexpr float kPanelAnimSeconds = 0.22f;

LibraryPanel::LibraryPanel(LibraryBridge& b) : m_b(b) { setWantsKeyboardFocus(true); }

void LibraryPanel::setTargetBounds(juce::Rectangle<int> fullyOpen)
{
    if (m_target == fullyOpen) return;
    m_target = fullyOpen;
    applyAnimation();
    if (m_wantOpen) rebuild();
}

void LibraryPanel::open(bool animate)
{
    const bool was = m_wantOpen;
    m_wantOpen = true;
    m_b.refresh();
    rebuild();
    setVisible(true);
    toFront(false);
    grabKeyboardFocus();
    if (!animate) { m_anim = 1.0f; stopTimer(); applyAnimation(); }
    else startTimerHz(kPanelAnimHz);
    if (!was && onOpenChanged) onOpenChanged(true);
    repaint();
}

void LibraryPanel::close(bool animate)
{
    const bool was = m_wantOpen;
    m_wantOpen = false;
    if (!animate) { m_anim = 0.0f; stopTimer(); applyAnimation(); }
    else startTimerHz(kPanelAnimHz);
    if (was && onOpenChanged) onOpenChanged(false);
}

void LibraryPanel::applyAnimation()
{
    const float eased = 1.0f - (1.0f - m_anim) * (1.0f - m_anim) * (1.0f - m_anim);
    // in whole LCD rows, so the unrolling edge never cuts a pixel
    const int h = (int(std::lround(float(m_target.getHeight()) * eased)) / kS) * kS;
    setBounds(m_target.withHeight(juce::jmax(0, h)));
    if (m_anim <= 0.0f && !m_wantOpen) setVisible(false);
}

void LibraryPanel::timerCallback()
{
    const float step = 1.0f / (kPanelAnimSeconds * float(kPanelAnimHz));
    const bool moving = m_wantOpen ? m_anim < 1.0f : m_anim > 0.0f;
    if (moving) { m_anim = m_wantOpen ? juce::jmin(1.0f, m_anim + step) : juce::jmax(0.0f, m_anim - step); applyAnimation(); return; }
    // open and at rest: follow the library on disk (a save in the app, another plugin instance) and the preview state
    if (!m_wantOpen) { stopTimer(); return; }
    if (getTimerInterval() < 400) startTimer(500);
    if (m_b.refresh() || m_seenRevision != m_b.model().revision()) rebuild();
    repaint();
}

juce::Rectangle<int> LibraryPanel::gridArea() const
{
    const int w = m_target.getWidth() / kS, h = m_target.getHeight() / kS;
    return {kFilterW + 1, kBarH, w - kFilterW - 2, h - kBarH - kFootH};
}

void LibraryPanel::rebuild()
{
    m_seenRevision = m_b.model().revision();
    m_items.clear(); m_filters.clear();
    const auto grid = gridArea();
    const int colW = (grid.getWidth() - 6) / kCols;
    int x = 0, y = 2, col = 0;
    auto header = [&](const juce::String& name, int count) {
        if (col) { col = 0; y += kCellH + 1; }
        Item h; h.header = true; h.name = name; h.right = juce::String(count); h.r = {2, y, grid.getWidth() - 8, kHeadRowH};
        m_items.push_back(h); y += kHeadRowH;
    };
    auto cell = [&](const std::string& id, const juce::String& name, const juce::String& right, bool on, bool fav = false) {
        x = 2 + col * colW;
        Item c; c.id = id; c.name = name; c.right = right; c.on = on; c.fav = fav; c.r = {x, y, colW - 1, kCellH};
        m_items.push_back(c);
        if (++col == kCols) { col = 0; y += kCellH + 1; }
    };
    auto filterHead = [&](const juce::String& label) { Filter f; f.header = true; f.label = label; m_filters.push_back(f); };
    auto filter = [&](const juce::String& label, const juce::String& value, int section) { Filter f; f.label = label; f.value = value; f.section = section; m_filters.push_back(f); };

    if (m_tab == Presets) {
        const auto cur = m_b.proc().loadedPreset(m_b.track()).id.toStdString();
        const auto list = m_b.presets(-1, m_group.isEmpty() ? nullptr : m_group.toRawUTF8(), m_favourites, m_savedOnly, m_query);
        std::map<int, int> counts;
        for (const auto* p : list) ++counts[p->model];
        int last = -1;
        for (const auto* p : list) {
            if (int(p->model) != last) { last = p->model; const auto* m = machineOf(p->model); header(m ? m->displayName : "?", counts[last]); }
            cell(p->id, U(p->name), p->saved ? "SAVED" : "", p->id == cur, m_b.favourite(p->id));
        }
        filterHead("MACHINE");
        filter("ALL", "", 0);
        juce::StringArray groups;
        for (int i = 0; i < spec::kNumMachines; ++i) groups.addIfNotAlreadyThere(spec::kMachines[i].group);
        for (const auto& gname : groups) filter(gname, gname, 0);
        filterHead("SHOW");
        filter("FAVOURITES", "fav", 1);
        filter("SAVED", "saved", 1);
    } else if (m_tab == Kits) {
        const auto cur = m_b.proc().loadedKit().id.toStdString();
        for (const auto* k : m_b.kits(m_kitSource, m_favourites, m_query))
            cell(k->id, U(k->name), k->saved ? juce::String("SAVED") : k->patternIds.empty() ? juce::String() : juce::String(int(k->patternIds.size())) + "P", k->id == cur, m_b.favourite(k->id));
        filterHead("SOURCE");
        filter("ALL", "", 2);
        for (const auto& p : m_b.model().projects()) filter(U(p.name), p.id, 2);
        filter("SAVED", "saved", 2);
        filterHead("SHOW");
        filter("FAVOURITES", "fav", 1);
    } else {
        for (const auto* p : m_b.patterns(m_bank, m_query)) {
            const auto* k = p->kitId.empty() ? nullptr : m_b.cat().kit(p->kitId);
            cell(p->id, U(p->name), k ? U(k->name) : juce::String("NO KIT"), false);
        }
        filterHead("BANK");
        filter("ALL", "-1", 3);
        for (int bnk = 0; bnk < 8; ++bnk) filter(juce::String::charToString(juce::juce_wchar('A' + bnk)), juce::String(bnk), 3);
    }
    int fy = kBarH + 3;
    for (auto& f : m_filters) { if (f.header && fy > kBarH + 3) fy += 4; f.r = {2, fy, kFilterW - 4, f.header ? 9 : 11}; fy += f.r.getHeight(); }
    clampScroll();
    repaint();
}

void LibraryPanel::clampScroll() { m_scroll = juce::jlimit(0, juce::jmax(0, contentHeight() - gridArea().getHeight()), m_scroll); }

int LibraryPanel::itemAt(juce::Point<int> lcd) const
{
    const auto grid = gridArea();
    if (!grid.contains(lcd)) return -1;
    const auto p = lcd - grid.getPosition() + juce::Point<int>(0, m_scroll);
    for (int i = 0; i < int(m_items.size()); ++i) if (!m_items[size_t(i)].header && m_items[size_t(i)].r.contains(p)) return i;
    return -1;
}

void LibraryPanel::paint(juce::Graphics& g)
{
    const int w = m_target.getWidth() / kS, h = m_target.getHeight() / kS;
    if (w <= 0 || h <= 0) return;
    LcdCanvas cv(w, h);
    frame(cv, {0, 0, w, h});
    // bar: title, tabs (Six), what is typed, CLOSE
    cv.fillRect(0, 0, w, kBarH, true);
    cv.text(spec::kFontBold8, "LIBRARY", 5, 5, false);
    int x = 5 + LcdCanvas::textWidth(spec::kFontBold8, "LIBRARY") + 10;
    m_tabRects = {};
    if (m_b.six()) {
        static const char* const names[] = {"PRESETS", "KITS", "PATTERNS"};
        for (int t = 0; t < 3; ++t) {
            const int tw = LcdCanvas::textWidth(spec::kFontSmall4x5, names[t]) + 10;
            m_tabRects[size_t(t)] = {x, 3, tw, kBarH - 6};
            if (int(m_tab) == t) cv.fillRect(x, 3, tw, kBarH - 6, false); else frame(cv, m_tabRects[size_t(t)], false);
            cv.text(spec::kFontSmall4x5, names[t], x + 5, 6, int(m_tab) == t);
            x += tw + 3;
        }
        x += 8;
    }
    if (m_query.isEmpty()) cv.text(spec::kFontSmall4x5, "TYPE TO FIND", x, 6, false);
    else textMarked(cv, spec::kFontSmall4x5, "FIND: " + m_query, x, 6, false, false, true);
    const int cw = LcdCanvas::textWidth(spec::kFontSmall4x5, "CLOSE") + 10;
    m_closeRect = {w - cw - 3, 3, cw, kBarH - 6};
    frame(cv, m_closeRect, false);
    cv.text(spec::kFontSmall4x5, "CLOSE", m_closeRect.getX() + 5, 6, false);

    // filter column
    cv.dotsV(kFilterW, kBarH, h - kFootH - 1);
    for (const auto& f : m_filters) {
        if (f.header) { cv.text(spec::kFontTiny3x5, f.label.toRawUTF8(), f.r.getX() + 2, f.r.getY() + 2, true); continue; }
        const bool on = f.section == 0 ? m_group == f.value : f.section == 1 ? (f.value == "fav" ? m_favourites : m_savedOnly)
                      : f.section == 2 ? m_kitSource == f.value : m_bank == f.value.getIntValue();
        if (on) cv.fillRect(f.r.getX(), f.r.getY(), f.r.getWidth(), f.r.getHeight(), true);
        cv.text(spec::kFontSmall4x5, fit(spec::kFontSmall4x5, f.label, f.r.getWidth() - 6).toRawUTF8(), f.r.getX() + 3, f.r.getY() + 3, !on);
    }

    // grid
    const auto grid = gridArea();
    const char* kind = m_tab == Presets ? "preset" : m_tab == Kits ? "kit" : "pattern";
    for (int i = 0; i < int(m_items.size()); ++i) {
        const auto& it = m_items[size_t(i)];
        const auto r = it.r.translated(grid.getX(), grid.getY() - m_scroll);
        if (r.getY() < grid.getY() || r.getBottom() > grid.getBottom()) continue;   // whole rows only
        if (it.header) {
            cv.text(spec::kFontBold8, it.name.toRawUTF8(), r.getX() + 1, r.getY() + 4, true);
            const int nx = r.getX() + LcdCanvas::textWidth(spec::kFontBold8, it.name.toRawUTF8()) + 6, cwid = LcdCanvas::textWidth(spec::kFontTiny3x5, it.right.toRawUTF8());
            cv.dotsH(nx, r.getRight() - cwid - 5, r.getY() + 8);
            cv.text(spec::kFontTiny3x5, it.right.toRawUTF8(), r.getRight() - cwid, r.getY() + 6, true);
            continue;
        }
        if (it.on) cv.fillRect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), true); else dottedFrame(cv, r);
        const bool ink = !it.on, hover = i == m_hover, playing = m_b.previewing(kind, it.id);
        if (hover && !it.on) frame(cv, r);
        int nx = r.getX() + 4;
        if (hover || playing) { playGlyph(cv, {r.getX() + 1, r.getY(), 11, r.getHeight()}, playing, ink); nx = r.getX() + 13; }
        if (playing) { drawLoopGlyph(cv, nx + 2, r.getY() + (r.getHeight() - kLoopGlyphH) / 2, m_b.looping(), ink); nx += 13; }   // the loop toggle beside the stop
        const int rw = LcdCanvas::textWidth(spec::kFontTiny3x5, it.right.toRawUTF8());
        textMarked(cv, spec::kFontBold8, fit(spec::kFontBold8, it.name, r.getRight() - nx - rw - 12), nx, r.getY() + 3, ink, it.fav);
        cv.text(spec::kFontTiny3x5, it.right.toRawUTF8(), r.getRight() - rw - 3, r.getY() + 5, ink);
    }
    if (m_items.empty()) cv.textCentred(spec::kFontSmall4x5, m_b.cat().presets.empty() ? "THE LIBRARY IS EMPTY: IMPORT A SYSEX DUMP IN THE LIBRARY APP" : "NOTHING MATCHES", grid.getX(), grid.getWidth(), grid.getY() + 20, true);
    if (contentHeight() > grid.getHeight()) {
        const int barH = juce::jmax(8, grid.getHeight() * grid.getHeight() / contentHeight());
        cv.fillRect(w - 4, grid.getY() + (grid.getHeight() - barH) * m_scroll / juce::jmax(1, contentHeight() - grid.getHeight()), 2, barH, true);
    }

    // footer: what a click does (or the last problem), and the way to the full app
    const int fy = h - kFootH;
    cv.dotsH(1, w - 2, fy);
    juce::String hint = m_b.message;
    if (hint.isEmpty()) {
        if (m_tab == Presets) hint = m_b.six() ? "CLICK = LOAD INTO TRACK " + juce::String(m_b.track() + 1) + "   DRAG ONTO A TRACK KEY FOR ANOTHER TRACK   GLYPH = AUDITION" : juce::String("CLICK = LOAD (THE LIBRARY STAYS OPEN)   GLYPH = AUDITION");
        else if (m_tab == Kits) hint = "CLICK = LOAD THE KIT   LOCKED TRACK KEYS KEEP THEIR SOUND   GLYPH = AUDITION";
        else hint = "NO SEQUENCER HERE: DRAG A PATTERN INTO THE DAW FOR ITS MIDI   CLICK = LOAD ITS KIT";
    }
    cv.text(spec::kFontTiny3x5, fit(spec::kFontTiny3x5, hint, w - 110).toRawUTF8(), 4, fy + 5, true);
    const int aw = LcdCanvas::textWidth(spec::kFontTiny3x5, "OPEN THE LIBRARY APP");
    m_appRect = {w - aw - 8, fy + 1, aw + 6, kFootH - 2};
    cv.text(spec::kFontTiny3x5, "OPEN THE LIBRARY APP", m_appRect.getX() + 3, fy + 5, true);
    cv.fillRect(m_appRect.getX() + 3, fy + 11, aw, 1, true);
    cv.draw(g, 0, 0, kS);
}

void LibraryPanel::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto lcd = e.getPosition() / kS;
    m_pressed = -1; m_dragging = false;
    m_b.message.clear();
    if (m_closeRect.contains(lcd)) { close(); return; }
    if (m_appRect.contains(lcd)) {
        const auto app = libraryAppFile();
        if (app.exists()) app.startAsProcess(); else { m_b.message = "THE LIBRARY APP IS NOT INSTALLED"; repaint(); }
        return;
    }
    for (int t = 0; t < 3; ++t) if (m_tabRects[size_t(t)].contains(lcd)) { m_query.clear(); setTab(Tab(t)); return; }
    for (const auto& f : m_filters) {
        if (f.header || !f.r.contains(lcd)) continue;
        if (f.section == 0) m_group = f.value;
        else if (f.section == 1) { if (f.value == "fav") m_favourites = !m_favourites; else m_savedOnly = !m_savedOnly; }
        else if (f.section == 2) m_kitSource = f.value;
        else m_bank = f.value.getIntValue();
        m_scroll = 0; rebuild(); return;
    }
    m_pressed = itemAt(lcd);
}

void LibraryPanel::mouseDrag(const juce::MouseEvent& e)
{
    if (m_pressed < 0) return;
    if (!m_dragging) {
        if (e.getDistanceFromDragStart() < 6) return;
        m_dragging = true;
        if (m_tab == Patterns) {   // the DAW takes the pattern's MIDI
            const auto f = m_b.patternMidiFile(m_items[size_t(m_pressed)].id);
            if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles({f.getFullPathName()}, false, this);
            else { m_b.message = "THE MIDI OF THIS PATTERN COULD NOT BE WRITTEN"; repaint(); }
            m_pressed = -1; m_dragging = false;
            return;
        }
        if (m_tab == Presets && m_b.six()) setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }
    if (m_tab == Presets && onPresetDragging) onPresetDragging(e.getScreenPosition());   // the track key under it lights up
}

void LibraryPanel::mouseUp(const juce::MouseEvent& e)
{
    setMouseCursor(juce::MouseCursor::NormalCursor);
    const int i = m_pressed;
    m_pressed = -1;
    if (i < 0 || i >= int(m_items.size())) return;
    const auto id = m_items[size_t(i)].id;
    if (m_dragging) {
        m_dragging = false;
        if (m_tab == Presets && onPresetDropped) onPresetDropped(id, e.getScreenPosition());
        rebuild();
        return;
    }
    const auto lcd = e.getPosition() / kS;
    if (itemAt(lcd) != i) return;
    const auto r = m_items[size_t(i)].r.translated(gridArea().getX(), gridArea().getY() - m_scroll);
    const juce::String kind = m_tab == Presets ? "preset" : m_tab == Kits ? "kit" : "pattern";
    if (lcd.x < r.getX() + 13) { m_b.preview(kind, id); repaint(); return; }
    if (lcd.x < r.getX() + 26 && m_b.previewing(kind, id)) { m_b.toggleLoop(); repaint(); return; }
    if (m_tab == Presets) m_b.loadPreset(id); else if (m_tab == Kits) m_b.loadKit(id); else m_b.loadPatternKit(id);
    rebuild();
}

void LibraryPanel::mouseMove(const juce::MouseEvent& e)
{
    const auto lcd = e.getPosition() / kS;
    const int i = itemAt(lcd);
    if (i != m_hover) { m_hover = i; repaint(); }
    const bool hand = i >= 0 || m_closeRect.contains(lcd) || m_appRect.contains(lcd);
    setMouseCursor(hand ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void LibraryPanel::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& d)
{
    m_scroll -= int(std::lround(d.deltaY * 120.0f));
    clampScroll();
    repaint();
}

bool LibraryPanel::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (m_query.isNotEmpty()) { m_query.clear(); rebuild(); } else close(); return true; }
    if (k == juce::KeyPress::spaceKey && m_query.isEmpty()) return false;   // the host's transport
    if (typeInto(m_query, k, 16)) { m_scroll = 0; rebuild(); return true; }
    return false;
}

// ---------------------------------------------------------------------------------------------- save dialog

void SaveDialog::open(bool kit)
{
    m_kit = kit; m_error.clear();
    m_b.refresh();
    auto& proc = m_b.proc();
    m_loaded = kit ? proc.loadedKit() : proc.loadedPreset(m_b.track());
    m_asVersion = m_loaded.valid();
    const auto* m = spec::machineByIndex(m_b.machineModel());
    m_name = m_loaded.valid() ? m_loaded.name : kit ? juce::String("NEW KIT") : juce::String(m ? m->name : "NEW");
    // the project slot the loaded item came from; a saved item leads there through what it was made from
    m_slot = {};
    std::string id = m_loaded.id.toStdString();
    for (int hop = 0; hop < 8 && !id.empty() && !m_slot.valid(); ++hop) {
        m_slot = kit ? m_b.model().projectSlotOfKit(id) : m_b.model().projectSlotOfPreset(id);
        if (m_slot.valid()) break;
        if (kit) { const auto* k = m_b.cat().kit(id); id = k ? k->parentId : std::string(); }
        else { const auto* p = m_b.cat().preset(id); id = p ? p->parentId : std::string(); }
    }
    m_intoProject = false;
    setVisible(true);
    toFront(false);
    grabKeyboardFocus();
    repaint();
}

void SaveDialog::paint(juce::Graphics& g)
{
    g.fillAll(lcd::paper.withAlpha(0.8f));
    const auto b = box();
    LcdCanvas cv(kLcdW, kLcdH);
    frame(cv, {0, 0, kLcdW, kLcdH}); frame(cv, {1, 1, kLcdW - 2, kLcdH - 2});
    cv.fillRect(0, 0, kLcdW, 15, true);
    cv.text(spec::kFontBold8, m_kit ? "SAVE KIT" : "SAVE PRESET", 6, 4, false);
    cv.text(spec::kFontSmall4x5, "NAME", 8, 25, true);
    const juce::Rectangle<int> field(34, 20, kLcdW - 42, 15);
    frame(cv, field);
    textMarked(cv, spec::kFontBold8, m_name, field.getX() + 4, field.getY() + 4, true, false, true);

    int y = 42;
    auto option = [&](juce::Rectangle<int>& r, bool on, bool square, const juce::String& title, const char* sub) {
        r = {8, y, kLcdW - 16, 20};
        const juce::Rectangle<int> mark(r.getX() + 2, r.getY() + 2, 9, 9);
        if (square) frame(cv, mark); else { frame(cv, mark); cv.set(mark.getX(), mark.getY(), false); cv.set(mark.getRight() - 1, mark.getY(), false); cv.set(mark.getX(), mark.getBottom() - 1, false); cv.set(mark.getRight() - 1, mark.getBottom() - 1, false); }
        if (on) cv.fillRect(mark.getX() + 2, mark.getY() + 2, 5, 5, true);
        cv.text(spec::kFontBold8, fit(spec::kFontBold8, title, r.getWidth() - 18).toRawUTF8(), r.getX() + 16, r.getY() + 2, true);
        cv.text(spec::kFontTiny3x5, sub, r.getX() + 16, r.getY() + 13, true);
        y += 23;
    };
    m_optVersion = {};
    if (m_loaded.valid()) option(m_optVersion, m_asVersion, false, "NEW VERSION OF " + m_loaded.name, "THE ORIGINAL STAYS IN THE LIBRARY - THIS ONE LINKS BACK TO IT");
    option(m_optNew, !m_asVersion, false, m_kit ? "NEW KIT" : "NEW PRESET", "AN INDEPENDENT ITEM WITH ITS OWN NAME");
    m_optProject = {};
    if (m_slot.valid()) {
        const juce::String where = "ALSO PUT INTO " + U(m_slot.projectName) + " KIT " + juce::String(m_slot.kit + 1).paddedLeft('0', 3) + (m_kit ? juce::String() : " TRACK " + juce::String(m_slot.track + 1));
        option(m_optProject, m_intoProject, true, where, "AS A NEW VERSION OF THE PROJECT - READY FOR THE NEXT SYSEX EXPORT");
    }
    if (m_error.isNotEmpty()) cv.text(spec::kFontTiny3x5, fit(spec::kFontTiny3x5, m_error, kLcdW - 120).toRawUTF8(), 8, kLcdH - 14, true);
    m_save = {kLcdW - 48, kLcdH - 19, 40, 13};
    m_cancel = {kLcdW - 48 - 52, kLcdH - 19, 48, 13};
    cv.fillRect(m_save.getX(), m_save.getY(), m_save.getWidth(), m_save.getHeight(), true);
    cv.textCentred(spec::kFontSmall4x5, "SAVE", m_save.getX(), m_save.getWidth(), m_save.getY() + 4, false);
    dottedFrame(cv, m_cancel);
    cv.textCentred(spec::kFontSmall4x5, "CANCEL", m_cancel.getX(), m_cancel.getWidth(), m_cancel.getY() + 4, true);
    g.setColour(lcd::paper);
    g.fillRect(b);
    cv.draw(g, b.getX(), b.getY(), kS);
}

void SaveDialog::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto lcd = (e.getPosition() - box().getPosition()) / kS;
    if (m_optVersion.contains(lcd)) m_asVersion = true;
    else if (m_optNew.contains(lcd)) m_asVersion = false;
    else if (m_optProject.contains(lcd)) m_intoProject = !m_intoProject;
    else if (m_cancel.contains(lcd)) { setVisible(false); if (onDone) onDone(); return; }
    else if (m_save.contains(lcd)) { save(); return; }
    repaint();
}

bool SaveDialog::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { setVisible(false); if (onDone) onDone(); return true; }
    if (k == juce::KeyPress::returnKey) { save(); return true; }
    if (typeInto(m_name, k, m_kit ? 10 : 16)) { repaint(); return true; }
    return true;   // modal: nothing reaches the host while the dialog is up
}

void SaveDialog::save()
{
    const auto name = m_name.trim();
    if (name.isEmpty()) { m_error = "GIVE IT A NAME"; repaint(); return; }
    auto& proc = m_b.proc();
    const juce::String from = m_b.six() ? "Monomodule Six" : "Monomodule One";
    const auto parent = m_asVersion ? m_loaded.id : juce::String();
    const auto into = m_intoProject ? m_slot : mnm::library::LibraryModel::Slot{};
    const auto kit = proc.currentKit();
    juce::String id;
    const auto r = m_kit ? m_b.model().saveKit(kit, name, parent, from, into, &id) : m_b.model().savePreset(kit, m_b.track(), name, parent, from, into, &id);
    if (r.failed()) { m_error = U(r.getErrorMessage()); repaint(); return; }
    if (m_kit) proc.markKitSaved(id, name); else proc.markPresetSaved(m_b.track(), id, name);
    m_b.message = "SAVED " + name + (m_intoProject ? " AND A NEW VERSION OF " + U(m_slot.projectName) : juce::String());
    setVisible(false);
    if (onDone) onDone();
}

} // namespace mnm::plugin::one
