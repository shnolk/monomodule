#include "LibraryComponent.h"
#include "ShnolkLogo.h"
#include "ParamDisplay.h"
#include "SharedSettings.h"
#include "Transfer.h"
#include "preview/Preview.h"
#include <algorithm>

namespace mnm::app {

using namespace mnm::dump;
using mnm::library::Store;
using mnm::catalog::Catalog;

namespace {
juce::var tag(const char* kind, const juce::String& id)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("kind", kind); o->setProperty("id", id);
    return juce::var(o);
}
juce::String tagKind(const juce::var& v) { return v.getDynamicObject() ? v["kind"].toString() : juce::String(); }
juce::String tagId(const juce::var& v) { return v.getDynamicObject() ? v["id"].toString() : juce::String(); }
const char* kTabs[5] = {"PATTERNS", "KITS", "SONGS", "GLOBALS", "HISTORY"};
const char* kGroups[8] = {"ALL", "GND", "SID", "SWAVE", "DPRO", "FM+", "VO", "FX"};
juce::Colour kDim() { return lcd::ink.withAlpha(0.55f); }   // a function: the skin can change
int machineSlotOf(uint8_t model) { for (int i = 0; i < spec::kNumMachines; ++i) if (spec::kMachines[i].index == model) return i; return spec::kNumMachines; }
juce::String pad3(int pos) { return juce::String(pos + 1).paddedLeft('0', 3); }
juce::String slotName(const juce::String& kind, int pos) { return kind == "pat" ? "Pattern " + juce::String(patternSlotName(pos)) : "Kit " + pad3(pos); }
juce::String plural(int n, const char* one) { return juce::String(n) + " " + one + (n == 1 ? "" : "s"); }
int usedTracks(const Pattern& p) { int n = 0; for (int t = 0; t < 6; ++t) if (p.noteTrigCount(t) > 0) ++n; return n; }
std::string patternIdIn(const Dump& d, const Pattern& p)
{
    const auto* k = d.kitAt(p.kit);
    return Catalog::patternHash(p, k && !k->isEmptySlot() ? Catalog::kitHash(*k) : std::string());
}
}

LibraryComponent::LibraryComponent(std::unique_ptr<Store> store, bool audio) : m_store(std::move(store))
{
    mnm::plugin::skin::apply(mnm::plugin::skin::load());   // the two colours of every Monomodule window
    m_lnf.applySkin();
    setLookAndFeel(&m_lnf);
    m_skinDialog.onChanged = [this] { skinChanged(); };
    addChildComponent(m_skinDialog);
    setWantsKeyboardFocus(true);
    m_nav.nav = "presets"; m_nav.tab = "patterns"; m_nav.group = "ALL"; m_nav.source = "all";

    // header
    m_backBtn.setGhost(true); m_menu.setGhost(true);
    m_backBtn.onClick = [this] { goBack(); };
    m_import.onClick = [this] { importChooser(); };
    m_menu.onClick = [this] { showMenu(); };
    addChildComponent(m_backBtn); addAndMakeVisible(m_import); addAndMakeVisible(m_menu);
    m_search.setFont(ui::font());
    m_search.setTextToShowWhenEmpty("Search the list", lcd::ink.withAlpha(0.4f));
    m_search.setIndents(8, 4);
    m_search.onTextChange = [this] { rebuildList(); };
    m_search.onEscapeKey = [this] { m_search.clear(); };
    addAndMakeVisible(m_search);

    // rail
    for (auto* vp : {&m_railView, &m_kitGridView, &m_historyView, &m_slotKitViewport, &m_filterView, &m_detail}) { vp->setScrollBarsShown(true, false); vp->setScrollBarThickness(8); addChildComponent(*vp); }
    m_railView.setViewedComponent(&m_rail, false); m_railView.setVisible(true);
    m_rail.onSelect = [this](const juce::String& id) { if (id == "import") importChooser(); else setNav(id); };

    // project pane
    for (int i = 0; i < 5; ++i) {
        auto& t = m_tabs[size_t(i)] = std::make_unique<LcdChip>(kTabs[i], spec::kFontBold8, 2, true);
        t->setRadioGroupId(1);
        t->onClick = [this, i] { m_nav.tab = juce::String(kTabs[i]).toLowerCase(); m_edit.fill.clear(); refresh(); };
        addChildComponent(*t);
    }
    m_editBtn.setGhost(true);
    m_backToCurrent.setGhost(true); m_backToCurrent.setOnDark(true);
    m_editBtn.onClick = [this] { beginEdit(); };
    m_exportBtn.onClick = [this] { if (const auto* p = currentProject()) showExportDialog(m_nav.viewVersion > 0 ? m_nav.viewVersion : p->current()->n); };
    m_saveBtn.onClick = [this] { showSaveDialog(); };
    m_backToCurrent.onClick = [this] { m_nav.viewVersion = 0; refresh(); };
    for (auto* c : {&m_editBtn, &m_exportBtn, &m_saveBtn, &m_backToCurrent}) addChildComponent(*c);
    addChildComponent(m_bankGrid); addChildComponent(m_tray); addChildComponent(m_changes);
    m_kitGridView.setViewedComponent(&m_kitGrid, false);
    m_historyView.setViewedComponent(&m_history, false);
    m_slotKitViewport.setViewedComponent(&m_slotKitView, false);
    m_bankGrid.onBank = [this](int b) { selectBank(b); };
    m_bankGrid.onOpen = [this](int pos) { openSlot("pat", pos); };
    m_bankGrid.onSelect = [this](int pos) { m_selPat = pos; m_edit.fill.clear(); refreshProject(); };
    m_bankGrid.onPlay = [this](int pos) { playSlot("pat", pos); };
    m_bankGrid.onClear = [this](int pos) { clearSlot("pat", pos); };
    m_bankGrid.onFill = [this](int pos) { armFill("pat", pos); };
    m_bankGrid.onDrop = [this](const juce::String& src, int pos, bool copy) { dropOnSlot("pat", src, pos, copy); };
    m_kitGrid.onOpen = [this](int pos) { m_nav.selKit = pos; refreshProject(); };
    m_kitGrid.onSelect = [this](int pos) { m_nav.selKit = pos; m_edit.fill.clear(); refreshProject(); };
    m_kitGrid.onPlay = [this](int pos) { playSlot("kit", pos); };
    m_kitGrid.onClear = [this](int pos) { clearSlot("kit", pos); };
    m_kitGrid.onFill = [this](int pos) { armFill("kit", pos); };
    m_kitGrid.onDrop = [this](const juce::String& src, int pos, bool copy) { dropOnSlot("kit", src, pos, copy); };
    m_tray.onPick = [this](const Card& c) {
        const auto kind = tagKind(c.tag) == "pattern" ? juce::String("pat") : juce::String("kit");
        if (m_edit.fill.startsWith(kind + ":")) placeLibraryItem(kind, tagId(c.tag), m_edit.fill.fromFirstOccurrenceOf(":", false, false).getIntValue());
        else toast("Drag it into a slot, or press + on an empty slot first and then click a library item");
    };
    m_tray.onPlay = [this](const Card& c) { playCatalog(tagKind(c.tag), tagId(c.tag), -1); };
    m_tray.onDragStart = [this](const Card& c, juce::Component* src) { startDragging((tagKind(c.tag) == "pattern" ? "pat:lib:" : "kit:lib:") + tagId(c.tag), src); };
    m_changes.onUndo = [this] { undoEdit(); };
    m_history.onView = [this](int n) { m_nav.viewVersion = n; m_nav.tab = "patterns"; refresh(); };
    m_history.onCompare = [this](int n) { showCompare(n); };
    m_history.onRestore = [this](int n) { confirmRestore(n); };
    m_history.onExport = [this](int n) { showExportDialog(n); };
    m_history.onSave = [this] { showSaveDialog(); };
    m_slotKitView.onLink = [this](const juce::var& v) { onLink(v); };
    m_slotKitView.onPlayKit = [this] { playSlot("kit", m_nav.selKit); };
    m_slotKitView.onPlayTrack = [this](int t) { if (const auto* d = shownState()) if (const auto* k = d->kitAt(m_nav.selKit)) if (const auto* it = m_catalog.kit(Catalog::kitHash(*k))) playCatalog("kit", juce::String(it->id), t); };
    m_slotKitView.onTrack = [this](int t) { if (const auto* d = shownState()) if (const auto* k = d->kitAt(m_nav.selKit)) { const auto id = Catalog::presetHash(k->tracks[t], k->lpKeyTracks(t), k->hpKeyTracks(t)); if (m_catalog.preset(id)) navigateItem("preset", juce::String(id)); } };
    m_slotKitView.patterns().grid.onPlay = [this](const Card& c) { playCatalog(tagKind(c.tag), tagId(c.tag), -1); };

    // browser pane
    m_filterView.setViewedComponent(&m_filters, false);
    m_filters.onSelect = [this](const juce::String& id) {
        if (id.startsWith("group:")) m_nav.group = id.substring(6);
        else if (id.startsWith("source:")) m_nav.source = id.substring(7);
        else if (id == "fav") m_nav.favOnly = !m_nav.favOnly;
        refreshBrowser();
    };
    addChildComponent(m_list);
    m_list.setWantsKeyboardFocus(true);
    m_list.onSelect = [this](int i) { const auto& r = m_list.rows()[size_t(i)]; m_nav.itemKind = tagKind(r.tag); m_nav.itemId = tagId(r.tag); showItem(); };
    m_list.onPlay = [this](int i) { const auto& r = m_list.rows()[size_t(i)]; playCatalog(tagKind(r.tag), tagId(r.tag), -1); };
    m_list.onDragStart = [this](int i) {
        const auto& r = m_list.rows()[size_t(i)];
        const auto kind = tagKind(r.tag);
        if (kind == "preset") dragPreset(tagId(r.tag), &m_list); else if (kind == "kit") dragKit(tagId(r.tag), &m_list); else if (kind == "pattern") dragPatternMidi(tagId(r.tag), -1, &m_list);
    };
    m_kitView.onTrack = [this](int t) { if (const auto* k = m_catalog.kit(m_nav.itemId.toStdString()); k && !k->presetIds[t].empty()) navigateItem("preset", juce::String(k->presetIds[t])); };
    m_kitView.onDragTrack = [this](int t) { if (const auto* k = m_catalog.kit(m_nav.itemId.toStdString()); k && !k->presetIds[t].empty()) dragPreset(juce::String(k->presetIds[t]), &m_kitView); };
    m_kitView.onDragKit = [this] { dragKit(m_nav.itemId, &m_kitView); };
    m_kitView.onLink = [this](const juce::var& v) { onLink(v); };
    m_kitView.onPlayKit = [this] { playCatalog("kit", m_nav.itemId, -1); };
    m_kitView.onPlayTrack = [this](int t) { playCatalog("kit", m_nav.itemId, t); };
    m_presetView.onDrag = [this] { dragPreset(m_nav.itemId, &m_presetView); };
    m_presetView.onLink = [this](const juce::var& v) { onLink(v); };
    m_presetView.onPlay = [this] { playCatalog("preset", m_nav.itemId, -1); };
    m_presetView.onParams = [this] { showParams(); };
    m_presetView.onFavourite = [this] { toggleFavourite(); };
    m_presetView.onTag = [this] { showTagDialog(); };
    m_patternView.onDragTrack = [this](int t) { dragPatternMidi(m_nav.itemId, t, &m_patternView); };
    m_patternView.onDragPattern = [this] { dragPatternMidi(m_nav.itemId, -1, &m_patternView); };
    m_patternView.onPreset = [this](int t) { if (const auto* p = m_catalog.pattern(m_nav.itemId.toStdString()); p && !p->presetIds[t].empty()) navigateItem("preset", juce::String(p->presetIds[t])); };
    m_patternView.onLink = [this](const juce::var& v) { onLink(v); };
    m_patternView.onPlayPattern = [this] { playCatalog("pattern", m_nav.itemId, -1); };
    m_patternView.onPlayTrack = [this](int t) { playCatalog("pattern", m_nav.itemId, t); };
    for (auto* grid : {&m_kitView.patterns().grid, &m_presetView.versions().grid, &m_presetView.kits().grid, &m_presetView.patterns().grid, &m_patternView.kitSection().grid})
        grid->onPlay = [this](const Card& c) { playCatalog(tagKind(c.tag), tagId(c.tag), -1); };

    // dialogs
    addChildComponent(m_modal);
    m_modal.onEscape = [this] { closeDialog(); };
    m_paramsDialog.onClose = [this] { closeDialog(); };

    loadLcdArt(mnm::plugin::loadSharedOsPath());   // the LCD faces come from the OS file; a stand-in face draws until one is chosen
    setSize(kWidth, kHeight);
    if (m_store && audio) {
        m_player = std::make_unique<mnm::library::PreviewPlayer>();
        m_osPath = mnm::plugin::loadSharedOsPath();
        m_player->setFirmwarePath(m_osPath);
        auto opt = m_player->options();
        opt.bpm = juce::jlimit(30.0, 300.0, mnm::plugin::loadSharedSetting("previewBpm", "120").getDoubleValue());
        m_player->setOptions(opt);
        m_player->onChange = [this] { if (!m_player->isPlaying()) m_playingTag.clear(); refreshPlaying(); repaint(); };
        // the loop toggle beside the stop glyph, in every view (LibraryViews.h ui::Transport)
        ui::transport().looping = [this] { return m_player && m_player->loop(); };
        ui::transport().toggleLoop = [this] { if (m_player && m_player->isPlaying()) m_player->setLoop(!m_player->loop()); };
    }
    if (m_store) { reload(); if (!m_projects.empty()) setNav("project:" + m_projects.front().id, false); startTimer(1000); }
    refresh();
}

LibraryComponent::~LibraryComponent()
{
    m_player.reset();
    setLookAndFeel(nullptr);
}

// ---------------------------------------------------------------------------
// Data

const mnm::library::ProjectInfo* LibraryComponent::project(const juce::String& id) const
{
    for (const auto& p : m_projects) if (p.id == id) return &p;
    return nullptr;
}

const Dump* LibraryComponent::currentState(const juce::String& projectId)
{
    const auto* p = project(projectId);
    if (!p || !p->current() || !m_store) return nullptr;
    auto it = m_states.find(projectId);
    if (it != m_states.end() && it->second.first == p->current()->n) return &it->second.second;
    Dump d;
    if (!m_store->loadVersion(projectId, p->current()->n, d)) return nullptr;
    auto& slot = m_states[projectId];
    slot = {p->current()->n, std::move(d)};
    return &slot.second;
}

const Dump* LibraryComponent::shownState()
{
    const auto* p = currentProject();
    if (!p) return nullptr;
    if (m_edit.active && m_edit.projectId == p->id) return &m_edit.state;
    if (m_nav.viewVersion > 0 && p->current() && m_nav.viewVersion != p->current()->n) {
        if (m_viewedProject != p->id || m_viewedVersion != m_nav.viewVersion) {
            if (!m_store->loadVersion(p->id, m_nav.viewVersion, m_viewed)) return currentState(p->id);
            m_viewedProject = p->id; m_viewedVersion = m_nav.viewVersion;
        }
        return &m_viewed;
    }
    return currentState(p->id);
}

void LibraryComponent::rebuildCatalog()
{
    std::vector<mnm::catalog::Input> inputs;
    for (const auto& p : m_projects) if (const auto* d = currentState(p.id)) inputs.push_back({p.id.toStdString(), p.name.toStdString(), d});
    std::vector<mnm::catalog::SavedInput> saved;
    for (const auto& s : m_saved) saved.push_back({s.id.toStdString(), s.name.toStdString(), s.parent.toStdString(), s.savedFrom.toStdString(), s.time.formatted("%d %b %Y").toStdString(), s.isKit, s.track, s.kit});
    m_catalog.build(inputs, saved);
}

void LibraryComponent::reload()
{
    if (!m_store) return;
    m_projects = m_store->listProjects();
    for (auto it = m_states.begin(); it != m_states.end();) it = project(it->first) ? std::next(it) : m_states.erase(it);
    m_saved = m_store->listSavedItems();
    m_user = m_store->loadUser();
    m_stamp = m_store->changeStamp();
    rebuildCatalog();
    if (m_nav.nav.startsWith("project:") && !currentProject()) { m_nav.nav = m_projects.empty() ? juce::String("presets") : "project:" + m_projects.front().id; m_nav.viewVersion = 0; }
    int packs = 0; for (const auto& p : m_projects) packs += p.pack ? 1 : 0;
    m_status = plural(int(m_projects.size()) - packs, "project") + " - " + plural(int(m_catalog.presets.size()), "preset") + " - " + plural(int(m_catalog.kits.size()), "kit") + " - " + plural(int(m_catalog.patterns.size()), "pattern");
    refresh();
}

// ---------------------------------------------------------------------------
// Navigation

void LibraryComponent::pushBack() { m_back.push_back(m_nav); if (m_back.size() > 40) m_back.erase(m_back.begin()); }

void LibraryComponent::setNav(const juce::String& nav, bool push)
{
    if (nav == m_nav.nav) return;
    if (push) pushBack();
    m_nav.nav = nav; m_nav.viewVersion = 0; m_nav.itemKind.clear(); m_nav.itemId.clear();
    m_search.setText({}, false);
    refresh();
}

void LibraryComponent::navigateItem(const juce::String& kind, const juce::String& id, bool push)
{
    if (push) pushBack();
    m_nav.nav = kind == "preset" ? "presets" : kind == "kit" ? "kits" : "patterns";
    m_nav.group = "ALL"; m_nav.source = "all"; m_nav.favOnly = false;
    m_nav.itemKind = kind; m_nav.itemId = id;
    m_search.setText({}, false);
    refresh();
}

void LibraryComponent::goBack()
{
    if (m_back.empty()) return;
    m_nav = m_back.back();
    m_back.pop_back();
    refresh();
}

void LibraryComponent::onLink(const juce::var& v)
{
    const auto kind = tagKind(v);
    if (kind == "project") setNav("project:" + tagId(v)); else navigateItem(kind, tagId(v));
}

void LibraryComponent::refresh()
{
    const bool proj = currentProject() != nullptr;
    const bool edit = proj && m_edit.active && m_edit.projectId == currentProject()->id;
    const bool viewing = proj && !edit && m_nav.viewVersion > 0 && currentProject()->current() && m_nav.viewVersion != currentProject()->current()->n;
    m_backBtn.setVisible(!m_back.empty());
    for (int i = 0; i < 5; ++i) { m_tabs[size_t(i)]->setVisible(proj); m_tabs[size_t(i)]->setToggleState(m_nav.tab == juce::String(kTabs[i]).toLowerCase(), juce::dontSendNotification); }
    m_editBtn.setVisible(proj && !edit); m_editBtn.setEnabled(!viewing && proj && !currentProject()->pack);
    m_exportBtn.setVisible(proj && !edit);
    m_saveBtn.setVisible(edit);
    m_backToCurrent.setVisible(viewing);
    m_bankGrid.setVisible(proj && m_nav.tab == "patterns");
    m_kitGridView.setVisible(proj && m_nav.tab == "kits");
    m_slotKitViewport.setVisible(proj && m_nav.tab == "kits" && !edit);
    m_historyView.setVisible(proj && m_nav.tab == "history");
    m_tray.setVisible(edit && (m_nav.tab == "patterns" || m_nav.tab == "kits"));
    m_changes.setVisible(edit && (m_nav.tab == "patterns" || m_nav.tab == "kits"));
    m_filterView.setVisible(!proj); m_list.setVisible(!proj); m_detail.setVisible(!proj);
    m_search.setVisible(!proj);
    refreshRail();
    resized();
    if (proj) refreshProject(); else refreshBrowser();
    refreshPlaying();
    repaint();
}

void LibraryComponent::refreshRail()
{
    std::vector<Rail::Item> items;
    using K = Rail::Item;
    items.push_back({K::Header, {}, "PROJECTS"});
    for (const auto& p : m_projects) {
        const auto* v = p.current();
        const bool editing = m_edit.active && m_edit.projectId == p.id;
        items.push_back({K::Entry, "project:" + p.id, p.name + (editing ? "   (editing)" : ""), (p.pack ? juce::String("sound pack - ") : juce::String()) + (v ? v->label() + " - " + v->kind + " - " + v->time.formatted("%d %b %Y") : juce::String())});
    }
    items.push_back({K::Entry, "import", "+ Import a sysex dump..."});
    items.push_back({K::Header, {}, "LIBRARY"});
    items.push_back({K::Entry, "presets", "Presets", {}, juce::String(m_catalog.presets.size())});
    items.push_back({K::Entry, "kits", "Kits", {}, juce::String(m_catalog.kits.size())});
    items.push_back({K::Entry, "patterns", "Patterns", {}, juce::String(m_catalog.patterns.size())});
    items.push_back({K::Header, {}, "COLLECTIONS"});
    int saved = 0; for (const auto& p : m_catalog.presets) saved += p.saved ? 1 : 0; for (const auto& k : m_catalog.kits) saved += k.saved ? 1 : 0;
    items.push_back({K::Entry, "favourites", "Favourites", {}, juce::String(m_user.favourites.size())});
    items.push_back({K::Entry, "saved", "Saved from plugins", {}, juce::String(saved)});
    for (const auto& t : m_user.allTags()) { K it{K::Entry, "tag:" + t, "# " + t}; it.indent = 14; items.push_back(it); }
    items.push_back({K::Header, {}, "DEVICE"});
    items.push_back({K::Note, {}, "Monomachine", "not connected - MIDI transfer is a later stage"});
    m_rail.set(std::move(items), {m_nav.nav});
    m_rail.setSize(m_railView.getWidth() - m_railView.getScrollBarThickness(), juce::jmax(m_railView.getHeight(), m_rail.preferredHeight()));
}

// ---------------------------------------------------------------------------
// Project view

void LibraryComponent::selectBank(int b)
{
    if (b == m_nav.bank) return;
    m_nav.bank = b;
    if (m_selPat / 16 != b) m_selPat = b * 16;
    refreshProject();
}

void LibraryComponent::refreshProject()
{
    const auto* p = currentProject();
    const auto* d = shownState();
    if (!p || !d) return;
    const bool edit = m_edit.active && m_edit.projectId == p->id;
    std::array<SlotInfo, 128> pats{}, kits{};
    for (const auto& pat : d->patterns) {
        if (pat.position < 0 || pat.position > 127 || pat.empty()) continue;
        auto& s = pats[size_t(pat.position)];
        const auto* k = d->kitAt(pat.kit);
        s.used = true; s.name = k && !k->isEmptySlot() ? juce::String(k->name) : "kit " + pad3(pat.kit); s.tracks = usedTracks(pat); s.steps = pat.patternLength;
    }
    for (const auto& k : d->kits) { if (k.position < 0 || k.position > 127 || k.isEmptySlot()) continue; auto& s = kits[size_t(k.position)]; s.used = true; s.name = juce::String(k.name); }
    if (edit) { for (int pos : m_edit.changedPat) pats[size_t(pos)].changed = true; for (int pos : m_edit.changedKit) kits[size_t(pos)].changed = true; }
    const int fillPat = m_edit.fill.startsWith("pat:") ? m_edit.fill.substring(4).getIntValue() : -1, fillKit = m_edit.fill.startsWith("kit:") ? m_edit.fill.substring(4).getIntValue() : -1;
    m_bankGrid.set(pats, m_nav.bank, edit, m_selPat, fillPat);
    m_kitGrid.set(kits, edit, m_nav.selKit, fillKit);
    m_kitGrid.setSize(m_kitGridView.getWidth() - m_kitGridView.getScrollBarThickness(), m_kitGrid.preferredHeight());
    if (m_nav.tab == "kits" && !edit) showSlotKit();
    if (m_nav.tab == "history") {
        m_history.setSize(m_historyView.getWidth() - m_historyView.getScrollBarThickness(), 100);
        m_history.set(*p, edit, m_edit.changes);
        m_history.setSize(m_history.getWidth(), juce::jmax(m_historyView.getHeight(), m_history.preferredHeight()));
    }
    if (edit) {
        const bool patTab = m_nav.tab != "kits";
        m_tray.set(patTab ? "LIBRARY PATTERNS" : "LIBRARY KITS", trayCards(patTab ? "pat" : "kit"));
        const juce::String want = patTab ? "pat:" : "kit:";
        m_tray.setHint(m_edit.fill.startsWith(want) ? "CLICK ONE FOR " + slotName(patTab ? "pat" : "kit", m_edit.fill.substring(4).getIntValue()).toUpperCase() : juce::String("DRAG INTO A SLOT TO FILL OR REPLACE IT"));
        m_changes.set(m_edit.changes);
    }
    refreshPlaying();
    repaint();
}

void LibraryComponent::showSlotKit()
{
    const auto* d = shownState();
    const auto* k = d ? d->kitAt(m_nav.selKit) : nullptr;
    if (!k || k->isEmptySlot()) { m_slotKitViewport.setVisible(false); return; }
    m_slotKitViewport.setVisible(true);
    std::array<juce::String, 6> names;
    for (int t = 0; t < 6; ++t) if (const auto* pr = m_catalog.preset(Catalog::presetHash(k->tracks[t], k->lpKeyTracks(t), k->hpKeyTracks(t)))) names[size_t(t)] = juce::String(pr->name);
    std::vector<Card> cards;
    for (const auto& pat : d->patterns)
        if (!pat.empty() && pat.kit == k->position) {
            const auto id = patternIdIn(*d, pat);
            Card c;
            c.label = patternSlotName(pat.position); c.name = juce::String(k->name); c.metaLeft = juce::String(usedTracks(pat)) + " tr"; c.metaRight = juce::String(int(pat.patternLength)) + " st";
            if (m_catalog.pattern(id)) { c.tag = tag("pattern", juce::String(id)); c.playKey = "pattern:" + juce::String(id); }
            cards.push_back(c);
        }
    const int w = m_slotKitViewport.getWidth() - m_slotKitViewport.getScrollBarThickness();
    m_slotKitView.setSize((w / kScale) * kScale, 100);
    m_slotKitView.set(*k, names, std::move(cards), {});
    m_slotKitView.setTitle("KIT " + pad3(k->position) + "  " + juce::String(k->name));
    m_slotKitView.setSize(m_slotKitView.getWidth(), juce::jmax(m_slotKitViewport.getHeight(), m_slotKitView.preferredHeight()));
}

void LibraryComponent::openSlot(const juce::String& kind, int pos)
{
    const auto* d = shownState();
    if (!d || kind != "pat") return;
    const auto* p = d->patternAt(pos);
    if (!p) return;
    const auto id = patternIdIn(*d, *p);
    if (m_catalog.pattern(id)) navigateItem("pattern", juce::String(id));
    else toast("This version's " + juce::String(patternSlotName(pos)) + " differs from the current one: RESTORE the version to work with it");
}

// ---- edit mode

void LibraryComponent::beginEdit()
{
    const auto* p = currentProject();
    const auto* d = p ? currentState(p->id) : nullptr;
    if (!p || !d || !p->current()) return;
    m_edit = {};
    m_edit.active = true; m_edit.projectId = p->id; m_edit.baseVersion = p->current()->n; m_edit.state = *d;
    m_nav.viewVersion = 0;
    if (m_nav.tab != "patterns" && m_nav.tab != "kits" && m_nav.tab != "history") m_nav.tab = "patterns";
    refresh();
}

void LibraryComponent::pushUndo()
{
    m_edit.undo.push_back({m_edit.state, m_edit.changes, m_edit.changedPat, m_edit.changedKit});
    if (m_edit.undo.size() > 25) m_edit.undo.erase(m_edit.undo.begin());
}

void LibraryComponent::noteChange(const juce::String& text) { m_edit.changes.add(text); m_edit.fill.clear(); refreshRail(); refreshProject(); }

void LibraryComponent::undoEdit()
{
    if (m_edit.undo.empty()) return;
    auto u = std::move(m_edit.undo.back());
    m_edit.undo.pop_back();
    m_edit.state = std::move(u.state); m_edit.changes = u.changes; m_edit.changedPat = u.changedPat; m_edit.changedKit = u.changedKit;
    m_edit.fill.clear();
    refreshProject();
}

void LibraryComponent::clearSlot(const juce::String& kind, int pos)
{
    if (!m_edit.active) return;
    if (kind == "pat") {
        if (!mnm::project::patternInUse(m_edit.state, pos)) return;
        pushUndo();
        const auto* k = m_edit.state.kitAt(m_edit.state.patternAt(pos)->kit);
        const juce::String was = k && !k->isEmptySlot() ? juce::String(k->name) : juce::String("-");
        mnm::project::clearPattern(m_edit.state, pos);
        m_edit.changedPat.insert(pos);
        noteChange(slotName(kind, pos) + " cleared (was on kit " + was + ")");
    } else {
        if (!mnm::project::kitInUse(m_edit.state, pos)) return;
        pushUndo();
        const juce::String was = m_edit.state.kitAt(pos)->name;
        int users = 0; for (const auto& p : m_edit.state.patterns) if (!p.empty() && p.kit == pos) ++users;
        mnm::project::clearKit(m_edit.state, pos);
        m_edit.changedKit.insert(pos);
        noteChange(slotName(kind, pos) + " cleared, was " + was + (users ? " - " + plural(users, "pattern") + " still point" + (users == 1 ? "s" : "") + " to this slot" : juce::String()));
    }
}

void LibraryComponent::armFill(const juce::String& kind, int pos)
{
    const auto id = kind + ":" + juce::String(pos);
    m_edit.fill = m_edit.fill == id ? juce::String() : id;
    if (kind == "pat") m_selPat = pos; else m_nav.selKit = pos;
    refreshProject();
}

void LibraryComponent::dropOnSlot(const juce::String& kind, const juce::String& source, int pos, bool copy)
{
    if (!m_edit.active) return;
    if (source.startsWith("lib:")) { placeLibraryItem(kind, source.substring(4), pos); return; }
    const int from = source.fromFirstOccurrenceOf(":", false, false).getIntValue();
    if (from == pos) return;
    auto& d = m_edit.state;
    const bool targetUsed = kind == "pat" ? mnm::project::patternInUse(d, pos) : mnm::project::kitInUse(d, pos);
    pushUndo();
    const juce::String to = kind == "pat" ? juce::String(patternSlotName(pos)) : pad3(pos);
    if (kind == "pat") { if (copy) mnm::project::copyPattern(d, from, pos); else mnm::project::swapPatterns(d, from, pos); m_edit.changedPat.insert(pos); if (!copy) m_edit.changedPat.insert(from); m_selPat = pos; }
    else { if (copy) mnm::project::copyKit(d, from, pos); else mnm::project::swapKits(d, from, pos); m_edit.changedKit.insert(pos); if (!copy) m_edit.changedKit.insert(from); m_nav.selKit = pos; }
    noteChange(copy ? slotName(kind, from) + " copied to " + to + (targetUsed ? " (replaced what was there)" : "")
                    : targetUsed ? slotName(kind, from) + " and " + to + " swapped" + (kind == "kit" ? " - the patterns using them follow" : "")
                                 : slotName(kind, from) + " moved to " + to + (kind == "kit" ? " - the patterns using it follow" : ""));
}

void LibraryComponent::placeLibraryItem(const juce::String& kind, const juce::String& id, int pos)
{
    if (!m_edit.active) return;
    auto& d = m_edit.state;
    if (kind == "pat") {
        const auto* item = m_catalog.pattern(id.toStdString());
        if (!item) return;
        const bool had = mnm::project::patternInUse(d, pos);
        pushUndo();
        const auto* kitItem = m_catalog.kit(item->kitId);
        juce::String kitNote;
        if (kitItem) {
            const bool hadKit = [&] { for (int i = 0; i < 128; ++i) if (mnm::project::kitInUse(d, i) && Catalog::kitHash(*d.kitAt(i)) == kitItem->id) return true; return false; }();
            const int slot = mnm::project::putPatternWithKit(d, pos, item->pattern, kitItem->kit);
            if (slot < 0) kitNote = " - no free kit slot for its kit " + juce::String(kitItem->name);
            else if (!hadKit) { m_edit.changedKit.insert(slot); kitNote = " - its kit " + juce::String(kitItem->name) + " placed in kit slot " + pad3(slot); }
            else kitNote = " (kit " + juce::String(kitItem->name) + ", slot " + pad3(slot) + ")";
        } else mnm::project::putPattern(d, pos, item->pattern);
        m_edit.changedPat.insert(pos); m_selPat = pos;
        noteChange(slotName(kind, pos) + (had ? " replaced by " : " filled with ") + juce::String(item->name) + kitNote);
    } else {
        const auto* item = m_catalog.kit(id.toStdString());
        if (!item) return;
        const bool had = mnm::project::kitInUse(d, pos);
        const juce::String was = had ? juce::String(d.kitAt(pos)->name) : juce::String();
        pushUndo();
        Kit k = item->kit;
        if (k.isEmptySlot() || item->saved) {   // a kit saved from Six has no name bytes of its own: write the item's name
            k.name = item->name.substr(0, 10);
            std::memset(k.nameRaw, 0, sizeof(k.nameRaw));
            std::memcpy(k.nameRaw, k.name.data(), std::min<size_t>(10, k.name.size()));
        }
        mnm::project::putKit(d, pos, k);
        m_edit.changedKit.insert(pos); m_nav.selKit = pos;
        noteChange(slotName(kind, pos) + (had ? " replaced by " + juce::String(item->name) + ", was " + was : " filled with " + juce::String(item->name)));
    }
}

std::vector<Card> LibraryComponent::trayCards(const juce::String& kind) const
{
    std::vector<Card> v;
    auto groupOf = [this](const std::vector<mnm::catalog::Source>& sources, bool saved) {
        for (const auto& s : sources) if (juce::String(s.importId) == m_edit.projectId) return 0;
        return saved ? 2 : 1;
    };
    if (kind == "pat") for (const auto& p : m_catalog.patterns) { auto c = patternCard(p); c.group = groupOf(p.sources, false); v.push_back(c); }
    else for (const auto& k : m_catalog.kits) { auto c = kitCard(k); c.group = groupOf(k.sources, k.saved); v.push_back(c); }
    return v;
}

// ---------------------------------------------------------------------------
// Browser

bool LibraryComponent::passesSource(const std::vector<mnm::catalog::Source>& sources) const
{
    if (m_nav.source == "all") return true;
    for (const auto& s : sources) {
        if (m_nav.source == "saved" ? s.saved() : "project:" + juce::String(s.importId) == m_nav.source) return true;
    }
    return false;
}

juce::String LibraryComponent::sourceText(const std::vector<mnm::catalog::Source>& sources) const
{
    if (sources.empty()) return "-";
    const auto& s = sources.front();
    juce::String t = s.saved() ? juce::String(s.importName) : "Project " + juce::String(s.importName);
    if (sources.size() > 1) t += " +" + juce::String(sources.size() - 1);
    return t;
}

Card LibraryComponent::patternCard(const mnm::catalog::PatternItem& p) const
{
    Card c;
    const auto* k = m_catalog.kit(p.kitId);
    c.label = patternSlotName(p.pattern.position); c.name = k ? juce::String(k->name) : juce::String("-");
    c.metaLeft = juce::String(usedTracks(p.pattern)) + " tr"; c.metaRight = juce::String(int(p.pattern.patternLength)) + " st";
    c.tag = tag("pattern", juce::String(p.id)); c.playKey = "pattern:" + juce::String(p.id);
    return c;
}

Card LibraryComponent::kitCard(const mnm::catalog::KitItem& k, const juce::String& note) const
{
    Card c;
    int tracks = 0; for (const auto& t : k.kit.tracks) tracks += t.model ? 1 : 0;
    c.label = k.saved && (k.sources.empty() || k.sources.front().saved()) ? juce::String("SAVED") : pad3(k.sources.empty() ? k.kit.position : k.sources.front().slot);
    c.name = juce::String(k.name); c.metaLeft = note.isNotEmpty() ? note : plural(tracks, "track"); c.metaRight = k.patternIds.empty() ? juce::String() : juce::String(k.patternIds.size()) + "P";
    c.tag = tag("kit", juce::String(k.id)); c.playKey = "kit:" + juce::String(k.id);
    return c;
}

Card LibraryComponent::presetCard(const mnm::catalog::PresetItem& p, const juce::String& note) const
{
    Card c;
    const auto* m = spec::machineByIndex(p.model);
    c.label = m ? juce::String(m->name) : juce::String("?"); c.name = juce::String(p.name); c.metaLeft = note;
    c.tag = tag("preset", juce::String(p.id)); c.playKey = "preset:" + juce::String(p.id);
    return c;
}

std::vector<LinkList::Row> LibraryComponent::sourceLinks(const std::vector<mnm::catalog::Source>& sources, bool kits) const
{
    std::vector<LinkList::Row> rows;
    for (const auto& s : sources) {
        if (s.saved()) { rows.push_back({juce::String(s.importName), {}, {}}); continue; }
        rows.push_back({"Project " + juce::String(s.importName), (kits ? "kit " + pad3(s.slot) : "pattern " + juce::String(patternSlotName(s.slot))) + (s.track >= 0 ? "  T" + juce::String(s.track + 1) : juce::String()),
                        tag("project", juce::String(s.importId))});
    }
    return rows;
}

void LibraryComponent::refreshBrowser()
{
    const auto& nav = m_nav.nav;
    std::vector<Rail::Item> items;
    using K = Rail::Item;
    std::set<juce::String> sel;
    if (nav == "presets") {
        items.push_back({K::Header, {}, "MACHINE"});
        for (const char* g : kGroups) {
            int n = 0;
            for (const auto& p : m_catalog.presets) { const auto* m = spec::machineByIndex(p.model); if (juce::String(g) == "ALL" || (m && juce::String(m->group) == g)) ++n; }
            K it{K::Entry, "group:" + juce::String(g), g, {}, juce::String(n)};
            it.logoGroup = juce::String(g) == "ALL" ? nullptr : g;
            items.push_back(it);
        }
        sel.insert("group:" + m_nav.group);
    }
    items.push_back({K::Header, {}, "SOURCE"});
    items.push_back({K::Entry, "source:all", "All sources"});
    for (const auto& p : m_projects) items.push_back({K::Entry, "source:project:" + p.id, (p.pack ? "Pack " : "Project ") + p.name});
    if (nav != "patterns") items.push_back({K::Entry, "source:saved", "Saved from plugins"});
    sel.insert("source:" + m_nav.source);
    items.push_back({K::Header, {}, "SHOW"});
    items.push_back({K::Entry, "fav", "Favourites only"});
    if (m_nav.favOnly) sel.insert("fav");
    m_filters.set(std::move(items), sel);
    m_filters.setSize(m_filterView.getWidth() - m_filterView.getScrollBarThickness(), juce::jmax(m_filterView.getHeight(), m_filters.preferredHeight()));
    rebuildList();
}

void LibraryComponent::rebuildList()
{
    if (currentProject()) return;
    const auto& nav = m_nav.nav;
    const juce::String q = m_search.getText().trim();
    const juce::String wantTag = nav.startsWith("tag:") ? nav.substring(4) : juce::String();
    auto pass = [&](const std::string& id, const std::vector<mnm::catalog::Source>& sources, bool saved, std::initializer_list<juce::String> fields) {
        const juce::String jid(id);
        if (nav == "favourites" && !m_user.isFavourite(jid)) return false;
        if (nav == "saved" && !saved) return false;
        if (wantTag.isNotEmpty()) { auto it = m_user.tags.find(jid); if (it == m_user.tags.end() || !it->second.contains(wantTag)) return false; }
        if (m_nav.favOnly && !m_user.isFavourite(jid)) return false;
        if (!passesSource(sources)) return false;
        if (q.isEmpty()) return true;
        for (const auto& f : fields) if (f.containsIgnoreCase(q)) return true;
        return false;
    };
    const bool collection = nav == "favourites" || nav == "saved" || wantTag.isNotEmpty();
    std::vector<LcdList::Row> rows;
    auto heading = [&](const juce::String& text, int n) { LcdList::Row h; h.text = text; h.right = juce::String(n); h.selectable = false; h.tag = tag("header", text); rows.push_back(h); };
    if (nav == "presets" || collection) {
        std::vector<const mnm::catalog::PresetItem*> v;
        for (const auto& p : m_catalog.presets) {
            const auto* m = spec::machineByIndex(p.model);
            if (nav == "presets" && m_nav.group != "ALL" && (!m || m_nav.group != m->group)) continue;
            if (pass(p.id, p.sources, p.saved, {juce::String(p.name), machineDisplayName(p.model)})) v.push_back(&p);
        }
        std::sort(v.begin(), v.end(), [](const auto* a, const auto* b) { const int sa = machineSlotOf(a->model), sb = machineSlotOf(b->model); return sa != sb ? sa < sb : a->saved != b->saved ? a->saved : a->name < b->name; });
        if (collection && !v.empty()) heading("PRESETS", int(v.size()));
        int last = -1;
        for (size_t i = 0; i < v.size(); ++i) {
            const auto* p = v[i];
            if (!collection && int(p->model) != last) { last = p->model; int n = 0; for (size_t j = i; j < v.size() && v[j]->model == p->model; ++j) ++n; heading(machineDisplayName(p->model), n); }
            LcdList::Row r;
            r.text = juce::String(p->name) + (m_user.isFavourite(juce::String(p->id)) ? "  *" : ""); r.right = p->saved ? juce::String("SAVED") : sourceText(p->sources);
            r.indent = 10; r.playable = m_player != nullptr; r.tag = tag("preset", juce::String(p->id));
            rows.push_back(r);
        }
    }
    if (nav == "kits" || collection) {
        std::vector<const mnm::catalog::KitItem*> v;
        for (const auto& k : m_catalog.kits) { juce::String machines; for (const auto& t : k.kit.tracks) machines += machineDisplayName(t.model) + " "; if (pass(k.id, k.sources, k.saved, {juce::String(k.name), machines})) v.push_back(&k); }
        std::sort(v.begin(), v.end(), [](const auto* a, const auto* b) { return a->name != b->name ? a->name < b->name : a->id < b->id; });
        if (collection && !v.empty()) heading("KITS", int(v.size()));
        for (const auto* k : v) {
            LcdList::Row r;
            r.text = juce::String(k->name) + (m_user.isFavourite(juce::String(k->id)) ? "  *" : ""); r.right = juce::String(k->patternIds.size()) + "P - " + (k->saved ? juce::String("SAVED") : sourceText(k->sources));
            r.indent = 10; r.playable = m_player != nullptr; r.tag = tag("kit", juce::String(k->id));
            rows.push_back(r);
        }
    }
    if (nav == "patterns" || (collection && nav != "saved")) {
        std::vector<const mnm::catalog::PatternItem*> v;
        for (const auto& p : m_catalog.patterns) { const auto* k = m_catalog.kit(p.kitId); if (pass(p.id, p.sources, false, {juce::String(p.name), k ? juce::String(k->name) : juce::String()})) v.push_back(&p); }
        std::sort(v.begin(), v.end(), [](const auto* a, const auto* b) { const auto& sa = a->sources.front(); const auto& sb = b->sources.front(); return sa.importName != sb.importName ? sa.importName < sb.importName : sa.slot != sb.slot ? sa.slot < sb.slot : a->id < b->id; });
        if (collection && !v.empty()) heading("PATTERNS", int(v.size()));
        for (const auto* p : v) {
            const auto* k = m_catalog.kit(p->kitId);
            LcdList::Row r;
            r.text = juce::String(p->name) + (m_user.isFavourite(juce::String(p->id)) ? "  *" : ""); r.right = k ? juce::String(k->name) : juce::String("-");
            r.indent = 10; r.playable = m_player != nullptr; r.tag = tag("pattern", juce::String(p->id));
            rows.push_back(r);
        }
    }
    int keep = -1;
    for (int i = 0; i < int(rows.size()); ++i) if (rows[size_t(i)].selectable && tagKind(rows[size_t(i)].tag) == m_nav.itemKind && tagId(rows[size_t(i)].tag) == m_nav.itemId) { keep = i; break; }
    if (keep < 0) for (int i = 0; i < int(rows.size()); ++i) if (rows[size_t(i)].selectable) { keep = i; m_nav.itemKind = tagKind(rows[size_t(i)].tag); m_nav.itemId = tagId(rows[size_t(i)].tag); break; }
    if (keep < 0) { m_nav.itemKind.clear(); m_nav.itemId.clear(); }
    m_list.setRows(std::move(rows), keep);
    if (keep >= 0) m_list.scrollTo(keep);
    showItem();
}

void LibraryComponent::showItem()
{
    const int w = ((m_detail.getWidth() - m_detail.getScrollBarThickness()) / kScale) * kScale;
    juce::Component* c = nullptr;
    int pref = 0;
    const auto id = m_nav.itemId.toStdString();
    if (m_nav.itemKind == "preset") {
        if (const auto* p = m_catalog.preset(id)) {
            std::vector<Card> versions, kits, patterns;
            if (const auto* parent = p->parentId.empty() ? nullptr : m_catalog.preset(p->parentId)) { versions.push_back(presetCard(*p, "this one - " + juce::String(p->savedAt))); versions.push_back(presetCard(*parent, "made from")); }
            for (const auto& child : m_catalog.presets) if (child.parentId == p->id) { if (versions.empty()) versions.push_back(presetCard(*p, "this one")); versions.push_back(presetCard(child, "saved " + juce::String(child.savedAt))); }
            for (const auto& kid : p->kitIds) if (const auto* k = m_catalog.kit(kid)) { int tr = 0; for (int t = 0; t < 6; ++t) if (k->presetIds[t] == p->id) { tr = t; break; } kits.push_back(kitCard(*k, "track " + juce::String(tr + 1))); }
            for (const auto& pid : p->patternIds) if (const auto* pat = m_catalog.pattern(pid)) patterns.push_back(patternCard(*pat));
            auto it = m_user.tags.find(m_nav.itemId);
            m_presetView.setSize(w, 100);
            m_presetView.set(*p, sourceText(p->sources), it == m_user.tags.end() ? juce::StringArray() : it->second, m_user.isFavourite(m_nav.itemId), std::move(versions), std::move(kits), std::move(patterns));
            c = &m_presetView; pref = m_presetView.preferredHeight();
        }
    } else if (m_nav.itemKind == "kit") {
        if (const auto* k = m_catalog.kit(id)) {
            std::array<juce::String, 6> names;
            for (int t = 0; t < 6; ++t) if (const auto* p = m_catalog.preset(k->presetIds[t])) names[size_t(t)] = juce::String(p->name);
            std::vector<Card> cards;
            for (const auto& pid : k->patternIds) if (const auto* pat = m_catalog.pattern(pid)) cards.push_back(patternCard(*pat));
            m_kitView.setSize(w, 100);
            Kit shown = k->kit; shown.name = k->name;
            m_kitView.set(shown, names, std::move(cards), sourceLinks(k->sources, true));
            c = &m_kitView; pref = m_kitView.preferredHeight();
        }
    } else if (m_nav.itemKind == "pattern") {
        if (const auto* p = m_catalog.pattern(id)) {
            const auto* k = m_catalog.kit(p->kitId);
            std::array<PatternTrackBlock::Info, 6> info;
            for (int t = 0; t < 6; ++t) {
                auto& in = info[size_t(t)]; in.track = t;
                if (const auto* pr = m_catalog.preset(p->presetIds[t])) { in.presetId = juce::String(pr->id); in.presetName = juce::String(pr->name); in.machine = machineDisplayName(pr->model); }
                else if (k) in.machine = machineDisplayName(k->kit.tracks[t].model);
            }
            m_patternView.setSize(w, 100);
            m_patternView.set(p->pattern, k ? &k->kit : nullptr, info, k ? std::vector<Card>{kitCard(*k)} : std::vector<Card>{}, sourceLinks(p->sources, false));
            c = &m_patternView; pref = m_patternView.preferredHeight();
        }
    }
    if (m_detail.getViewedComponent() != c) { m_detail.setViewedComponent(c, false); m_detail.setViewPosition(0, 0); }
    if (c) c->setSize(w, juce::jmax(pref, m_detail.getHeight()));
    refreshPlaying();
    repaint();
}

void LibraryComponent::toggleFavourite()
{
    if (!m_store || m_nav.itemId.isEmpty()) return;
    if (m_user.favourites.contains(m_nav.itemId)) m_user.favourites.removeString(m_nav.itemId); else m_user.favourites.add(m_nav.itemId);
    m_store->saveUser(m_user);
    m_stamp = m_store->changeStamp();
    refreshRail(); rebuildList();
}

// ---------------------------------------------------------------------------
// Dialogs

void LibraryComponent::closeDialog() { m_modal.hide(); m_dialog.clear(); if (m_importFile.isNotEmpty()) { m_importFile.clear(); nextImport(); } }

void LibraryComponent::info(const juce::String& title, const juce::String& text)
{
    DialogSpec s; s.title = title; s.width = 640;
    for (const auto& line : juce::StringArray::fromLines(text)) s.rows.push_back({line.startsWith("- ") ? DialogSpec::Row::Bullet : DialogSpec::Row::Text, {}, line.startsWith("- ") ? line.substring(2) : line});
    s.buttons = {{"close", "CLOSE"}};
    m_dialog = "info";
    m_form.onButton = [this](const juce::String&) { closeDialog(); };
    m_form.onOption = nullptr;
    m_form.set(std::move(s));
    m_modal.show(&m_form, m_form.preferredWidth(), m_form.preferredHeight());
}

void LibraryComponent::startImport(const juce::StringArray& paths)
{
    if (!m_store) return;
    if (m_edit.active) { toast("Finish the edit first: SAVE leaves edit mode"); return; }
    for (const auto& p : paths) m_importQueue.add(p);
    if (m_dialog.isEmpty()) nextImport();
}

void LibraryComponent::nextImport()
{
    if (m_importQueue.isEmpty()) return;
    m_importFile = m_importQueue[0];
    m_importQueue.remove(0);
    const juce::File f(m_importFile);
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb)) { m_importFile.clear(); info("IMPORT SYSEX", "Could not read " + f.getFileName()); return; }
    const auto d = parseDump(static_cast<const uint8_t*>(mb.getData()), mb.getSize(), f.getFileNameWithoutExtension().toStdString());
    if (d.kits.empty() && d.patterns.empty()) { m_importFile.clear(); info("IMPORT SYSEX", f.getFileName() + " does not look like a Monomachine sysex dump."); return; }
    int named = 0, used = 0; for (const auto& k : d.kits) named += k.isEmptySlot() ? 0 : 1; for (const auto& p : d.patterns) used += p.empty() ? 0 : 1;
    const auto similar = m_store->findSimilar(d);
    m_importSimilar = similar ? similar->projectId : juce::String();
    DialogSpec s;
    s.title = "IMPORT SYSEX"; s.sub = f.getFileName() + " - " + plural(named, "named kit") + ", " + plural(used, "used pattern") + ", " + plural(d.numSongs, "song") + ", " + plural(d.numGlobals, "global");
    using R = DialogSpec::Row;
    if (similar) {
        const auto& df = similar->diff;
        s.rows.push_back({R::Bold, {}, df.identical() ? "This is identical to project " + similar->projectName + " (v" + juce::String(similar->version) + ")."
                                                      : "This looks like a newer state of project " + similar->projectName + ". " + juce::String(df.kitsSame) + " of " + juce::String(df.kitsCompared) + " used kit slots and " + juce::String(df.patternsSame) + " of " + juce::String(df.patternsCompared) + " used pattern slots are identical to its v" + juce::String(similar->version) + "."});
        juce::StringArray ks, ps;
        for (int k : df.kits) ks.add(pad3(k)); for (int p : df.patterns) ps.add(juce::String(patternSlotName(p)));
        if (!df.identical()) s.rows.push_back({R::Dim, {}, "Different on the unit: " + (ks.isEmpty() ? juce::String() : "kits " + ks.joinIntoString(", ") + "  ") + (ps.isEmpty() ? juce::String() : "patterns " + ps.joinIntoString(", "))});
        s.rows.push_back({R::Option, "version", "Add as a new version of " + similar->projectName + " (recommended)", "Its history keeps every earlier version. The dump becomes the project's current state."});
    } else s.rows.push_back({R::Text, {}, "No project in the library resembles this dump."});
    s.rows.push_back({R::Option, "project", "Create a new project", "A separate Monomachine memory with its own history."});
    s.rows.push_back({R::Option, "pack", "Only add its presets, kits and patterns to the library", "A sound pack: a source of items, not a unit's memory."});
    s.rows.push_back({R::Dim, {}, "The file is always archived byte for byte, whatever you choose."});
    s.selected = similar ? "version" : "project";
    s.buttons = {{"cancel", "CANCEL"}, {"import", "IMPORT"}};
    m_dialog = "import";
    m_form.onOption = nullptr;
    m_form.onButton = [this](const juce::String& b) {
        const juce::String file = m_importFile, similarId = m_importSimilar, choice = m_form.selected();
        m_importFile.clear();
        m_modal.hide(); m_dialog.clear();
        if (b == "import") {
            mnm::library::ProjectInfo made;
            const auto mode = choice == "version" ? mnm::library::ImportMode::NewVersion : choice == "pack" ? mnm::library::ImportMode::Pack : mnm::library::ImportMode::NewProject;
            const auto r = m_store->importSysexFile(juce::File(file), mode, similarId, &made);
            if (r.failed()) { info("IMPORT SYSEX", r.getErrorMessage()); return; }
            m_back.clear();
            reload();
            m_nav.nav.clear();
            setNav("project:" + made.id, false);
        }
        nextImport();
    };
    m_form.set(std::move(s));
    m_modal.show(&m_form, m_form.preferredWidth(), m_form.preferredHeight());
}

void LibraryComponent::showSaveDialog()
{
    const auto* p = project(m_edit.projectId);
    if (!m_edit.active || !p) return;
    using R = DialogSpec::Row;
    DialogSpec s;
    s.title = "SAVE PROJECT"; s.sub = p->name + " - leave edit mode"; s.width = 660;
    const juce::String next = "v" + juce::String((p->current() ? p->versions.front().n : 0) + 1);
    if (m_edit.changes.isEmpty()) s.rows.push_back({R::Bold, {}, "Nothing was changed. Confirm to leave edit mode; no version is created."});
    else {
        s.rows.push_back({R::Bold, {}, plural(m_edit.changes.size(), "change") + " will be saved as version " + next + ". v" + juce::String(m_edit.baseVersion) + " stays in the history and can be restored at any time."});
        for (const auto& c : m_edit.changes) s.rows.push_back({R::Bullet, {}, c});
        R note; note.kind = R::Field; note.id = "note"; note.a = "NOTE"; note.items.add("Optional: what this edit was about");
        s.rows.push_back({R::Gap}); s.rows.push_back(note);
        s.link = "Discard these changes instead";
    }
    s.buttons = {{"cancel", "CANCEL"}, {"save", m_edit.changes.isEmpty() ? juce::String("LEAVE EDIT MODE") : "SAVE AS " + next.toUpperCase()}};
    m_dialog = "save";
    m_form.onOption = nullptr;
    m_form.onButton = [this](const juce::String& b) {
        if (b == "cancel") { closeDialog(); return; }
        if (b == "save" && !m_edit.changes.isEmpty()) {
            const auto r = m_store->addVersion(m_edit.projectId, m_edit.state, "saved", "Edited in the Library: " + plural(m_edit.changes.size(), "change"), m_edit.changes, m_form.field("note").trim(), m_edit.baseVersion);
            if (r.failed()) { closeDialog(); info("SAVE PROJECT", r.getErrorMessage()); return; }
        }
        m_edit = {};
        closeDialog();
        reload();
    };
    m_form.set(std::move(s));
    m_modal.show(&m_form, m_form.preferredWidth(), m_form.preferredHeight());
}

void LibraryComponent::showExportDialog(int version)
{
    const auto* p = currentProject();
    if (!p || !m_store) return;
    m_export = {};
    m_export.projectId = p->id; m_export.version = version;
    if (!m_store->loadVersion(p->id, version, m_export.state)) return;
    // what changed since the unit last matched the library: the newest export or import at or before this version
    for (const auto& v : p->versions) if (v.n <= version && (v.kind == "exported" || v.kind == "imported")) { m_export.base = v.n; break; }
    Dump base;
    if (m_export.base > 0 && m_store->loadVersion(p->id, m_export.base, base)) m_export.diff = mnm::project::diffDumps(base, m_export.state);
    m_export.what = m_export.diff.identical() ? "all" : "changed";
    for (int k : m_export.diff.kits) m_export.kits.insert(juce::String(k));
    for (int pp : m_export.diff.patterns) m_export.patterns.insert(juce::String(pp));
    m_dialog = "export";
    refreshExportDialog();
}

void LibraryComponent::refreshExportDialog()
{
    const auto* p = project(m_export.projectId);
    if (!p) return;
    using R = DialogSpec::Row;
    auto& e = m_export;
    DialogSpec s;
    s.title = "EXPORT SYSEX"; s.sub = "Project " + p->name + " - v" + juce::String(e.version);
    s.rows.push_back({R::Steps, {}, "1 WHAT|2 CHECK|3 SEND", juce::String(e.step)});
    std::map<juce::String, std::set<juce::String>> ticks;
    auto selection = [&](std::vector<int>& kits, std::vector<int>& pats) {
        if (e.what == "changed") { kits = e.diff.kits; pats = e.diff.patterns; }
        else if (e.what == "pick") { for (const auto& k : e.kits) kits.push_back(k.getIntValue()); for (const auto& pp : e.patterns) pats.push_back(pp.getIntValue()); }
        std::sort(kits.begin(), kits.end()); std::sort(pats.begin(), pats.end());
    };
    if (e.step == 0) {
        s.rows.push_back({R::Option, "all", "The whole project", "Every kit, pattern, song and global: the unit ends up exactly like this version. Messages that were never edited go out byte for byte as received."});
        R ch{R::Option, "changed", "Only what changed since v" + juce::String(e.base) + " (" + plural(int(e.diff.kits.size()), "kit") + ", " + plural(int(e.diff.patterns.size()), "pattern") + ")", "Smaller and faster. Overwrites only those slots on the unit."};
        ch.disabled = e.diff.identical();
        s.rows.push_back(ch);
        s.rows.push_back({R::Option, "pick", "Selected slots...", "Choose the kits and patterns to send."});
        if (e.what == "pick") {
            R kt; kt.kind = R::Ticks; kt.id = "kits"; kt.a = "KITS";
            for (const auto& k : e.state.kits) if (!k.isEmptySlot()) { kt.items.add(pad3(k.position) + " " + juce::String(k.name)); kt.itemIds.add(juce::String(k.position)); }
            R pt; pt.kind = R::Ticks; pt.id = "patterns"; pt.a = "PATTERNS";
            for (const auto& pat : e.state.patterns) if (!pat.empty()) { const auto* k = e.state.kitAt(pat.kit); pt.items.add(juce::String(patternSlotName(pat.position)) + " " + (k ? juce::String(k->name) : juce::String())); pt.itemIds.add(juce::String(pat.position)); }
            s.rows.push_back(kt); s.rows.push_back(pt);
            ticks["kits"] = e.kits; ticks["patterns"] = e.patterns;
        }
        s.selected = e.what;
        s.buttons = {{"cancel", "CANCEL"}, {"next", "NEXT"}};
    } else if (e.step == 1) {
        std::vector<int> kits, pats;
        selection(kits, pats);
        if (e.what == "all") { for (const auto& k : e.state.kits) if (!k.isEmptySlot()) kits.push_back(k.position); for (const auto& pat : e.state.patterns) if (!pat.empty()) pats.push_back(pat.position); }
        const auto chk = mnm::project::checkExport(e.state, kits, pats, e.what == "all" ? std::vector<int>{} : e.diff.kits);
        auto list = [](const std::vector<int>& v, bool pattern) { juce::StringArray a; for (int x : v) a.add(pattern ? juce::String(patternSlotName(x)) : pad3(x)); return a.joinIntoString(", "); };
        if (chk.patternsWithEmptyKit.empty()) s.rows.push_back({R::Check, {}, "OK", "Every exported pattern's kit slot holds a kit."});
        else s.rows.push_back({R::Check, {}, "!", "These patterns point to an empty kit slot and will play nothing: " + list(chk.patternsWithEmptyKit, true)});
        if (chk.patternsKitNotIncluded.empty()) s.rows.push_back({R::Check, {}, "OK", "Every exported pattern's kit is part of the export, or unchanged since v" + juce::String(e.base) + "."});
        else s.rows.push_back({R::Check, {}, "!", "Their kit changed but is not in this export, so on the unit they will play the old kit: " + list(chk.patternsKitNotIncluded, true)});
        s.rows.push_back({R::Check, {}, "OK", "All messages are written in the unit's own format (the codec re-encodes unedited dumps byte for byte)."});
        if (!chk.kitsNeedingMkII.empty()) s.rows.push_back({R::Check, {}, "!", "Kits using DPRO-DDRW or DPRO-DENS need an MKII unit: " + list(chk.kitsNeedingMkII, false)});
        s.rows.push_back({R::Check, {}, "!", e.what == "all" ? juce::String("The unit's whole memory is overwritten. Its current content is not read first: import a fresh dump before if unsure.")
                                                           : "These slots are overwritten on the unit: " + (kits.empty() ? juce::String() : "kits " + list(kits, false) + "  ") + (pats.empty() ? juce::String() : "patterns " + list(pats, true)) + ". The unit's current content is not read first."});
        s.buttons = {{"cancel", "CANCEL"}, {"back", "BACK"}, {"next", "NEXT"}};
    } else {
        s.rows.push_back({R::Option, "file", "Save a .syx file", "Send it to the unit with C6 or Elektron Transfer."});
        R midi{R::Option, "midi", "Send to the Monomachine over MIDI", "A later stage: needs the MIDI transfer work."}; midi.disabled = true;
        s.rows.push_back(midi);
        s.rows.push_back({R::Dim, {}, "The export is recorded as a new version (EXPORTED) with the file kept beside it, so the history always says what is on the unit."});
        s.selected = "file";
        s.buttons = {{"cancel", "CANCEL"}, {"back", "BACK"}, {"export", "EXPORT"}};
    }
    m_form.onOption = [this](const juce::String& id) { if (m_export.step == 0 && id != m_export.what) { if (m_export.what == "pick") { m_export.kits = m_form.ticked("kits"); m_export.patterns = m_form.ticked("patterns"); } m_export.what = id; refreshExportDialog(); } };
    m_form.onButton = [this](const juce::String& b) {
        auto& ex = m_export;
        if (ex.step == 0 && ex.what == "pick") { ex.kits = m_form.ticked("kits"); ex.patterns = m_form.ticked("patterns"); }
        if (b == "cancel") { closeDialog(); return; }
        if (b == "back") { --ex.step; refreshExportDialog(); return; }
        if (b == "next") { if (ex.step == 0 && ex.what == "pick" && ex.kits.empty() && ex.patterns.empty()) { toast("Tick at least one kit or pattern"); return; } ++ex.step; refreshExportDialog(); return; }
        const auto* pj = project(ex.projectId);
        const juce::String name = (pj ? pj->name : juce::String("project")) + "-v" + juce::String(ex.version) + (ex.what == "all" ? "" : "-partial") + ".syx";
        m_chooser = std::make_unique<juce::FileChooser>("Export sysex", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(name), "*.syx");
        m_chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
            const auto dest = fc.getResult();
            if (dest == juce::File()) return;
            auto& x = m_export;
            std::vector<int> kits, pats;
            if (x.what == "changed") { kits = x.diff.kits; pats = x.diff.patterns; }
            else if (x.what == "pick") { for (const auto& k : x.kits) kits.push_back(k.getIntValue()); for (const auto& pp : x.patterns) pats.push_back(pp.getIntValue()); }
            const auto r = m_store->exportVersion(x.projectId, x.version, dest.withFileExtension("syx"), x.what == "all" ? nullptr : &kits, x.what == "all" ? nullptr : &pats);
            closeDialog();
            if (r.failed()) info("EXPORT SYSEX", r.getErrorMessage()); else { reload(); toast("Exported to " + dest.getFileName() + " and recorded in the history"); }
        });
    };
    m_form.set(std::move(s), ticks);
    m_modal.show(&m_form, m_form.preferredWidth(), m_form.preferredHeight());
}

void LibraryComponent::showCompare(int version)
{
    const auto* p = currentProject();
    Dump old;
    const auto* cur = p ? currentState(p->id) : nullptr;
    if (!p || !cur || !m_store->loadVersion(p->id, version, old)) return;
    const auto diff = mnm::project::diffDumps(old, *cur);
    juce::String text = diff.identical() ? "v" + juce::String(version) + " and the current version hold the same kits and patterns."
                                         : "Between v" + juce::String(version) + " and the current version " + p->current()->label() + ":";
    for (int k : diff.kits) { const auto* a = old.kitAt(k); const auto* b = cur->kitAt(k); text += "\n- Kit " + pad3(k) + ": " + (a && !a->isEmptySlot() ? juce::String(a->name) : juce::String("empty")) + " -> " + (b && !b->isEmptySlot() ? juce::String(b->name) : juce::String("empty")); }
    for (int pp : diff.patterns) text += "\n- Pattern " + juce::String(patternSlotName(pp)) + (mnm::project::patternInUse(old, pp) ? (mnm::project::patternInUse(*cur, pp) ? " changed" : " was cleared") : " was added");
    info("COMPARE", text);
}

void LibraryComponent::confirmRestore(int version)
{
    const auto* p = currentProject();
    if (!p) return;
    using R = DialogSpec::Row;
    DialogSpec s; s.title = "RESTORE"; s.sub = p->name + " - v" + juce::String(version); s.width = 600;
    s.rows.push_back({R::Bold, {}, "Add a new version equal to v" + juce::String(version) + "?"});
    s.rows.push_back({R::Text, {}, "It becomes the project's current state. Nothing is deleted: " + p->current()->label() + " and everything before it stay in the history."});
    s.buttons = {{"cancel", "CANCEL"}, {"restore", "RESTORE"}};
    m_dialog = "restore"; m_dialogVersion = version;
    m_form.onOption = nullptr;
    m_form.onButton = [this](const juce::String& b) {
        const auto pid = currentProject() ? currentProject()->id : juce::String();
        closeDialog();
        if (b == "restore" && pid.isNotEmpty()) { const auto r = m_store->restoreVersion(pid, m_dialogVersion); if (r.failed()) info("RESTORE", r.getErrorMessage()); else { m_nav.viewVersion = 0; reload(); } }
    };
    m_form.set(std::move(s));
    m_modal.show(&m_form, m_form.preferredWidth(), m_form.preferredHeight());
}

void LibraryComponent::showParams()
{
    const auto* p = m_catalog.preset(m_nav.itemId.toStdString());
    if (!p) return;
    const auto kit = Catalog::kitForPreset(*p);
    m_paramsDialog.set(kit, 0, "PARAMETERS  " + juce::String(p->name) + "  " + machineDisplayName(p->model), "READ ONLY");
    m_dialog = "params";
    m_modal.show(&m_paramsDialog, m_paramsDialog.preferredWidth(), m_paramsDialog.preferredHeight());
}

void LibraryComponent::showTagDialog()
{
    if (m_nav.itemId.isEmpty()) return;
    using R = DialogSpec::Row;
    DialogSpec s; s.title = "TAGS"; s.width = 560;
    auto it = m_user.tags.find(m_nav.itemId);
    R f; f.kind = R::Field; f.id = "tags"; f.a = "TAGS"; f.b = it == m_user.tags.end() ? juce::String() : it->second.joinIntoString(", "); f.items.add("lead, bright");
    s.rows.push_back(f);
    s.rows.push_back({R::Dim, {}, "Comma separated. Tags become collections in the rail." + (m_user.allTags().isEmpty() ? juce::String() : "  In use: " + m_user.allTags().joinIntoString(", "))});
    s.buttons = {{"cancel", "CANCEL"}, {"ok", "SAVE"}};
    m_dialog = "tags";
    m_form.onOption = nullptr;
    m_form.onButton = [this](const juce::String& b) {
        if (b == "ok") {
            juce::StringArray tags; tags.addTokens(m_form.field("tags"), ",", ""); tags.trim(); tags.removeEmptyStrings(); tags.removeDuplicates(true);
            for (auto& t : tags) t = t.toLowerCase();
            if (tags.isEmpty()) m_user.tags.erase(m_nav.itemId); else m_user.tags[m_nav.itemId] = tags;
            m_store->saveUser(m_user); m_stamp = m_store->changeStamp();
        }
        closeDialog();
        refreshRail(); showItem();
    };
    m_form.set(std::move(s));
    m_modal.show(&m_form, m_form.preferredWidth(), m_form.preferredHeight());
}

void LibraryComponent::showRenameDialog()
{
    const auto* p = currentProject();
    if (!p) return;
    using R = DialogSpec::Row;
    DialogSpec s; s.title = "RENAME PROJECT"; s.width = 520;
    R f; f.kind = R::Field; f.id = "name"; f.a = "NAME"; f.b = p->name;
    s.rows.push_back(f);
    s.buttons = {{"cancel", "CANCEL"}, {"ok", "RENAME"}};
    m_dialog = "rename";
    m_form.onOption = nullptr;
    m_form.onButton = [this](const juce::String& b) {
        const auto pid = currentProject() ? currentProject()->id : juce::String();
        const auto name = m_form.field("name").trim();
        closeDialog();
        if (b == "ok" && pid.isNotEmpty() && name.isNotEmpty()) { m_store->renameProject(pid, name); reload(); }
    };
    m_form.set(std::move(s));
    m_modal.show(&m_form, m_form.preferredWidth(), m_form.preferredHeight());
}

void LibraryComponent::showDeleteDialog()
{
    const auto* p = currentProject();
    if (!p) return;
    using R = DialogSpec::Row;
    DialogSpec s; s.title = "DELETE PROJECT"; s.sub = p->name; s.width = 600;
    s.rows.push_back({R::Bold, {}, "Delete " + p->name + " with all " + plural(int(p->versions.size()), "version") + "?"});
    s.rows.push_back({R::Text, {}, "This removes the project's whole history from the library, the archived original dumps included. It cannot be undone. Sounds saved from the plugins are not affected."});
    s.buttons = {{"cancel", "CANCEL"}, {"delete", "DELETE"}};
    m_dialog = "delete";
    m_form.onOption = nullptr;
    m_form.onButton = [this](const juce::String& b) {
        const auto pid = currentProject() ? currentProject()->id : juce::String();
        closeDialog();
        if (b == "delete" && pid.isNotEmpty()) { if (m_edit.projectId == pid) m_edit = {}; m_store->deleteProject(pid); m_back.clear(); reload(); }
    };
    m_form.set(std::move(s));
    m_modal.show(&m_form, m_form.preferredWidth(), m_form.preferredHeight());
}

// ---------------------------------------------------------------------------
// Audio preview

void LibraryComponent::playTag(const juce::String& t, const juce::String& key, const std::function<mnm::preview::PreviewSpec()>& build, int stem)
{
    if (!m_player) return;
    if (m_player->isPlaying() && t == m_playingTag) { m_player->stop(); return; }
    m_player->play(key, build, stem);
    if (m_player->isPlaying()) m_playingTag = t;
    refreshPlaying();
    repaint();
}

void LibraryComponent::playCatalog(const juce::String& kind, const juce::String& id, int stem)
{
    if (!m_player || id.isEmpty()) return;
    const auto opt = m_player->options();
    const juce::String t = kind + ":" + id + (stem >= 0 ? ":t" + juce::String(stem) : juce::String());
    if (kind == "preset") {
        if (const auto* p = m_catalog.preset(id.toStdString())) playTag(t, "preset/" + id, [p, opt] { return mnm::preview::presetPreview(p->track, p->lpKeyTrack, p->hpKeyTrack, opt); }, -1);
    } else if (kind == "pattern") {
        const auto* p = m_catalog.pattern(id.toStdString());
        const auto* k = p ? m_catalog.kit(p->kitId) : nullptr;
        if (p && k) playTag(t, "pattern/" + id, [p, k, opt] { return mnm::preview::patternPreview(k->kit, p->pattern, opt); }, stem);
    } else if (kind == "kit") {
        const auto* k = m_catalog.kit(id.toStdString());
        if (!k) return;
        const auto best = mnm::preview::choosePreviewPattern(m_catalog, *k);
        if (const auto* p = best.empty() ? nullptr : m_catalog.pattern(best)) playTag(t, "pattern/" + juce::String(best), [p, k, opt] { return mnm::preview::patternPreview(k->kit, p->pattern, opt); }, stem);
        else playTag(t, "kitdemo/" + id, [k, opt] { return mnm::preview::patternPreview(k->kit, mnm::preview::demoPattern(k->kit), opt); }, stem);
    }
}

void LibraryComponent::playSlot(const juce::String& kind, int pos)
{
    const auto* d = shownState();
    if (!m_player || !d) return;
    const auto opt = m_player->options();
    const juce::String t = "slot:" + kind + ":" + juce::String(pos);
    if (kind == "pat") {
        const auto* p = d->patternAt(pos);
        const auto* k = p ? d->kitAt(p->kit) : nullptr;
        if (!p || !k) { toast("This pattern's kit slot is empty"); return; }
        const Pattern pat = *p; const Kit kit = *k;
        playTag(t, "pattern/" + juce::String(patternIdIn(*d, *p)), [pat, kit, opt] { return mnm::preview::patternPreview(kit, pat, opt); }, -1);
    } else {
        const auto* k = d->kitAt(pos);
        if (!k || k->isEmptySlot()) return;
        const Kit kit = *k;
        const Pattern* best = nullptr; long score = -1;   // the pattern of this state that uses the kit with the most tracks
        for (const auto& p : d->patterns) if (!p.empty() && p.kit == pos) { int n = 0; for (int tr = 0; tr < 6; ++tr) n += p.noteTrigCount(tr); const long sc = long(usedTracks(p)) * 100000 + n; if (sc > score) { score = sc; best = &p; } }
        if (best) { const Pattern pat = *best; playTag(t, "pattern/" + juce::String(patternIdIn(*d, *best)), [pat, kit, opt] { return mnm::preview::patternPreview(kit, pat, opt); }, -1); }
        else playTag(t, "kitdemo/" + juce::String(Catalog::kitHash(kit)), [kit, opt] { return mnm::preview::patternPreview(kit, mnm::preview::demoPattern(kit), opt); }, -1);
    }
}

void LibraryComponent::debugShowPlaying(bool loop)
{
    m_playingTag = m_nav.itemKind == "project" ? "slot:pat:0" : m_nav.itemKind + ":" + m_nav.itemId;
    m_debugPlaying = true;
    ui::transport().looping = [loop] { return loop; };
    ui::transport().toggleLoop = [] {};
    refreshPlaying();
    repaint();
}

void LibraryComponent::refreshPlaying()
{
    const bool playing = m_debugPlaying || (m_player && m_player->isPlaying());
    const juce::String t = playing ? m_playingTag : juce::String();
    int row = -1;
    for (int i = 0; i < int(m_list.rows().size()) && row < 0; ++i) { const auto& tg = m_list.rows()[size_t(i)].tag; if (t.isNotEmpty() && tagKind(tg) + ":" + tagId(tg) == t) row = i; }
    m_list.setPlayingRow(row);
    const juce::String page = m_nav.itemKind + ":" + m_nav.itemId;
    auto stemOf = [&](const juce::String& base) { return t == base ? -1 : t.startsWith(base + ":t") ? t.fromLastOccurrenceOf(":t", false, false).getIntValue() : -2; };
    m_presetView.setPlaying(t == page && m_nav.itemKind == "preset");
    m_kitView.setPlaying(m_nav.itemKind == "kit" ? stemOf(page) : -2);
    m_patternView.setPlaying(m_nav.itemKind == "pattern" ? stemOf(page) : -2);
    m_bankGrid.setPlayingSlot(t.startsWith("slot:pat:") ? t.substring(9).getIntValue() : -1);
    m_kitGrid.setPlayingSlot(t.startsWith("slot:kit:") ? t.substring(9).getIntValue() : -1);
    m_slotKitView.setPlaying(t == "slot:kit:" + juce::String(m_nav.selKit) ? -1 : -2);
    const juce::String cardKey = t.upToFirstOccurrenceOf(":t", false, false);
    for (auto* grid : {&m_kitView.patterns().grid, &m_presetView.versions().grid, &m_presetView.kits().grid, &m_presetView.patterns().grid, &m_patternView.kitSection().grid, &m_slotKitView.patterns().grid}) grid->setPlayingKey(cardKey);
    m_tray.setPlayingKey(cardKey);
}

// ---------------------------------------------------------------------------
// Drag sources

void LibraryComponent::startFileDrag(const juce::File& f, juce::Component* source)
{
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles({f.getFullPathName()}, false, source);
}

void LibraryComponent::dragPreset(const juce::String& id, juce::Component* source)
{
    const auto* p = m_catalog.preset(id.toStdString());
    if (!p) return;
    startFileDrag(mnm::library::writeTrackDragFile(Catalog::kitForPreset(*p), 0, juce::String(p->name) + " " + machineDisplayName(p->model)), source);
}

void LibraryComponent::dragKit(const juce::String& id, juce::Component* source)
{
    const auto* k = m_catalog.kit(id.toStdString());
    if (!k) return;
    Kit kit = k->kit; kit.name = k->name;
    startFileDrag(mnm::library::writeKitDragFile(kit, juce::String(k->name)), source);
}

void LibraryComponent::dragPatternMidi(const juce::String& id, int track, juce::Component* source)
{
    const auto* p = m_catalog.pattern(id.toStdString());
    if (!p || p->sources.empty()) return;
    const auto* d = currentState(juce::String(p->sources.front().importId));
    const Pattern* pat = d ? d->patternAt(p->sources.front().slot) : nullptr;
    if (!pat) return;
    startFileDrag(mnm::library::writePatternMidiDragFile(*d, *pat, track), source);
}

// ---------------------------------------------------------------------------
// Import, menu, misc

void LibraryComponent::importChooser()
{
    m_chooser = std::make_unique<juce::FileChooser>("Import Monomachine sysex dump", juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.syx;*.mid;*.bin");
    m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
        [this](const juce::FileChooser& fc) { juce::StringArray paths; for (const auto& f : fc.getResults()) paths.add(f.getFullPathName()); startImport(paths); });
}

bool LibraryComponent::isInterestedInFileDrag(const juce::StringArray& files) { for (const auto& f : files) if (f.endsWithIgnoreCase(".syx")) return true; return false; }

void LibraryComponent::filesDropped(const juce::StringArray& files, int, int)
{
    juce::StringArray syx;
    for (const auto& f : files) if (f.endsWithIgnoreCase(".syx")) syx.add(f);
    startImport(syx);
}

bool LibraryComponent::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey && m_modal.isShowing()) { closeDialog(); return true; }
    return false;
}

void LibraryComponent::selectOsFile()
{
    m_chooser = std::make_unique<juce::FileChooser>("Select the Monomachine OS file (Elektron_SFX6-60_OS1.32B.syx)", juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.syx");
    m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (!f.existsAsFile()) return;
        m_osPath = f.getFullPathName();
        mnm::plugin::saveSharedOsPath(m_osPath);   // shared with the plugins
        if (m_player) m_player->setFirmwarePath(m_osPath);
        if (loadLcdArt(m_osPath)) resized();
        repaint();
    });
}

void LibraryComponent::setPreviewTempo()
{
    if (!m_player) return;
    using R = DialogSpec::Row;
    DialogSpec s; s.title = "PREVIEW TEMPO"; s.width = 560;
    s.rows.push_back({R::Text, {}, "Patterns carry no tempo on the unit (it is global there); previews play at this BPM, 30 to 300."});
    R f; f.kind = R::Field; f.id = "bpm"; f.a = "BPM"; f.b = juce::String(m_player->options().bpm, 1);
    s.rows.push_back(f);
    s.buttons = {{"cancel", "CANCEL"}, {"ok", "SET"}};
    m_dialog = "tempo";
    m_form.onOption = nullptr;
    m_form.onButton = [this](const juce::String& b) {
        const double bpm = juce::jlimit(30.0, 300.0, m_form.field("bpm").getDoubleValue());
        closeDialog();
        if (b != "ok" || !m_player) return;
        auto opt = m_player->options();
        opt.bpm = bpm;
        m_player->stop(); m_player->setOptions(opt);
        mnm::plugin::saveSharedSetting("previewBpm", juce::String(bpm, 2));
    };
    m_form.set(std::move(s));
    m_modal.show(&m_form, m_form.preferredWidth(), m_form.preferredHeight());
}

void LibraryComponent::showMenu()
{
    juce::PopupMenu m;
    const auto* p = currentProject();
    const bool proj = p != nullptr && !m_edit.active;
    m.addItem(1, "Import Sysex Dump...");
    m.addSeparator();
    m.addItem(2, "Rename Project...", proj);
    m.addItem(3, "Delete Project...", proj);
    m.addItem(4, "Show the Original .syx in Finder", proj && p->versions.back().kind == "imported");
    m.addSeparator();
    m.addItem(7, juce::String("Select Monomachine OS File...") + (m_osPath.isNotEmpty() ? "  (" + juce::File(m_osPath).getFileName() + ")" : juce::String()), m_player != nullptr);
    m.addItem(8, "Preview Tempo..." + (m_player ? "  (" + juce::String(m_player->options().bpm, 1) + " BPM)" : juce::String()), m_player != nullptr);
    m.addItem(9, "Stop Preview", m_player && m_player->isPlaying());
    m.addSeparator();
    m.addItem(5, "Show Library Folder in Finder", m_store != nullptr);
    {   // the two colours of the UI, shared by every Monomodule window
        namespace skin = mnm::plugin::skin;
        juce::PopupMenu skins;
        const auto cur = skin::current().preset;
        for (int i = 0; i < skin::kNumPresets; ++i) {
            const auto name = juce::String(skin::kPresetNames[i]).toLowerCase();
            skins.addItem(50 + i, (i == int(skin::Preset::Custom) ? "Custom..." : name.substring(0, 1).toUpperCase() + name.substring(1)), true, cur == skin::Preset(i));
        }
        m.addSubMenu("Skin", skins);
    }
    m.addItem(6, "About Monomodule Library...");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_menu), [this](int r) {
        const auto* pj = currentProject();
        switch (r) {
        case 1: importChooser(); break;
        case 2: showRenameDialog(); break;
        case 3: showDeleteDialog(); break;
        case 4: if (pj) { for (auto it = pj->versions.rbegin(); it != pj->versions.rend(); ++it) if (it->kind == "imported") { m_store->originalFile(pj->id, it->n).revealToUser(); break; } } break;
        case 5: if (m_store) { m_store->root().createDirectory(); m_store->root().revealToUser(); } break;
        case 6: info("MONOMODULE LIBRARY", juce::String("Monomodule Library ") + mnm::plugin::kPluginVersion + "\n" + mnm::plugin::kPluginTagline
                    + "\n\nKeeps each Monomachine's memory as a project with a version history, decodes sysex dumps losslessly, and shares its presets, kits and patterns with the Monomodule plugins."
                    + "\nAudio previews are rendered on demand with the emulated DSP from the Monomachine OS file."
                    + "\n\n" + mnm::plugin::kCredits + "\n" + mnm::plugin::kDisclaimer
                    + "\n\nContact: @shnolk on Instagram, shnolk@halftone.world"); break;
        case 7: selectOsFile(); break;
        case 8: setPreviewTempo(); break;
        case 9: if (m_player) m_player->stop(); break;
        case 50: case 51: case 52: { const auto s = mnm::plugin::skin::presetSkin(mnm::plugin::skin::Preset(r - 50)); mnm::plugin::skin::apply(s); mnm::plugin::skin::save(s); skinChanged(); break; }
        case 53: m_skinDialog.setBounds(getLocalBounds()); m_skinDialog.open(); break;
        default: break;
        }
    });
}

void LibraryComponent::toast(const juce::String& s) { m_toast = s; m_toastTicks = 5; repaint(); }

void LibraryComponent::skinChanged()
{
    m_lnf.applySkin();
    m_lnf.setColour(juce::ScrollBar::thumbColourId, lcd::ink);
    m_lnf.setColour(juce::ScrollBar::trackColourId, lcd::paper);
    if (auto* w = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent())) w->setBackgroundColour(lcd::paper);
    sendLookAndFeelChange();
    repaint();
}

void LibraryComponent::timerCallback()
{
    if (m_toastTicks > 0 && --m_toastTicks == 0) { m_toast.clear(); repaint(); }
    if (--m_skinPollCountdown <= 0) {   // a plugin chose a skin: follow it
        m_skinPollCountdown = 20;
        if (!m_skinDialog.isVisible()) if (const auto s = mnm::plugin::skin::load(); s != mnm::plugin::skin::current()) { mnm::plugin::skin::apply(s); skinChanged(); }
    }
    if (m_store && !m_edit.active && m_dialog.isEmpty() && m_store->changeStamp() != m_stamp) reload();   // another app or a plugin changed the library
    if (m_player) {   // the OS file chosen in a plugin
        const auto p = mnm::plugin::loadSharedOsPath();
        if (p != m_osPath && (p.isEmpty() || juce::File(p).existsAsFile())) { m_osPath = p; m_player->setFirmwarePath(p); if (loadLcdArt(p)) { resized(); repaint(); } }
    }
}

// ---------------------------------------------------------------------------

void LibraryComponent::paint(juce::Graphics& g)
{
    g.fillAll(lcd::paper);
    mnm::plugin::drawShnolkLogo(g, m_logoBounds.toFloat(), lcd::ink);
    drawLcdText(g, spec::kFontBold8, "MONOMODULE LIBRARY", m_logoBounds.getRight() + 10, m_logoBounds.getY() + 2, kScale, lcd::ink);
    drawLcdText(g, spec::kFontSmall4x5, juce::String(mnm::plugin::kPluginVersion).toUpperCase().toRawUTF8(), m_logoBounds.getRight() + 10, m_logoBounds.getY() + 2 + 10 * kScale + 4, 2, lcd::ink);
    g.setColour(lcd::ink);
    for (int x = 0; x < getWidth(); x += 4) { g.fillRect(x, 60, 2, 1); g.fillRect(x, getHeight() - 24, 2, 1); }
    for (int y = 60; y < getHeight() - 24; y += 4) g.fillRect(m_railView.getRight(), y, 1, 2);
    // status line
    ui::text(g, m_status, 12, getHeight() - 24, 24, kDim(), ui::font(false, 11.5f));
    juce::String right = m_toast;
    if (right.isEmpty() && m_player) { right = m_player->status(); if (right.isEmpty() && m_player->isPlaying()) right = m_player->rendering() ? "RENDERING + PLAYING" : "PLAYING"; }
    if (right.isEmpty() && m_store) right = m_store->root().getFullPathName();
    ui::textRight(g, right, getWidth() - 12, getHeight() - 24, 24, m_toast.isNotEmpty() ? lcd::ink : kDim(), ui::font(false, 11.5f));

    const auto* p = currentProject();
    g.setColour(lcd::ink); g.fillRect(m_projectBar);
    const int by = m_projectBar.getY();
    if (p) {
        const bool edit = m_edit.active && m_edit.projectId == p->id;
        const juce::String title = (p->pack ? "PACK  " : "PROJECT  ") + p->name.toUpperCase();
        drawLcdText(g, spec::kFontBold8, title.toRawUTF8(), m_projectBar.getX() + 8, by + 7, 2, lcd::paper);
        const int tx = m_projectBar.getX() + 8 + LcdCanvas::textWidth(spec::kFontBold8, title.toRawUTF8()) * 2 + 14;
        if (const auto* d = shownState()) { int kits = 0, pats = 0; for (const auto& k : d->kits) kits += k.isEmptySlot() ? 0 : 1; for (const auto& pt : d->patterns) pats += pt.empty() ? 0 : 1;
            ui::text(g, plural(kits, "kit") + " - " + plural(pats, "pattern"), tx, by, 30, lcd::paper.withAlpha(0.8f), ui::font(false, 12.0f)); }
        const auto* cur = p->current();
        const bool viewing = !edit && m_nav.viewVersion > 0 && cur && m_nav.viewVersion != cur->n;
        const juce::String rightText = edit ? "EDIT MODE - BASED ON V" + juce::String(m_edit.baseVersion) + " - " + plural(m_edit.changes.size(), "CHANGE").toUpperCase()
                                     : viewing ? "VIEWING V" + juce::String(m_nav.viewVersion) + " (READ ONLY)"
                                     : cur ? cur->label().toUpperCase() + " - " + cur->kind.toUpperCase() + " " + cur->time.formatted("%d %b %Y %H:%M").toUpperCase() : juce::String();
        ui::textRight(g, rightText, (viewing ? m_backToCurrent.getX() - 10 : m_projectBar.getRight() - 10), by, 30, lcd::paper.withAlpha(0.85f), ui::font(false, 12.0f));
        if (edit) { g.setColour(lcd::ink.withAlpha(0.07f)); for (int x = m_tabsRow.getX() - m_tabsRow.getHeight(); x < m_tabsRow.getRight(); x += 12) { juce::Path h; h.addQuadrilateral(float(x), float(m_tabsRow.getBottom()), float(x + 6), float(m_tabsRow.getBottom()), float(x + 6 + m_tabsRow.getHeight()), float(m_tabsRow.getY()), float(x + m_tabsRow.getHeight()), float(m_tabsRow.getY())); g.fillPath(h); }
                    ui::textRight(g, "Leave edit mode with SAVE", m_saveBtn.getX() - 12, m_tabsRow.getY(), m_tabsRow.getHeight(), kDim(), ui::font(false, 12.0f)); }
        g.setColour(lcd::ink);
        for (int x = m_tabsRow.getX(); x < m_tabsRow.getRight(); x += 4) g.fillRect(x, m_tabsRow.getBottom() - 1, 2, 1);
        if (m_nav.tab == "songs" || m_nav.tab == "globals")
            ui::wrapped(g, juce::String(m_nav.tab == "songs" ? "Songs" : "Globals") + " are kept exactly as received and travel with every whole-project export. A readable view comes once their format is decoded.", m_bodyBounds.reduced(14, 12), kDim());
        else if (m_nav.tab == "patterns" && !edit)
            ui::wrapped(g, "The unit's layout: 8 banks of 16 patterns. Each slot shows its kit, how many tracks it uses and its length. Click a pattern to open its page, the glyph to hear it. EDIT PROJECT turns the slots into an editor.",
                        {m_bodyBounds.getX() + 12, m_bankGrid.getBottom() + 8, m_bodyBounds.getWidth() - 24, 40}, kDim(), ui::font(false, 12.0f));
        else if (m_nav.tab == "kits") { g.setColour(lcd::ink); for (int y = m_bodyBounds.getY(); y < m_bodyBounds.getBottom(); y += 4) g.fillRect(m_kitGridView.getRight() + 5, y, 1, 2);
            if (!edit && !m_slotKitViewport.isVisible()) ui::text(g, "Empty kit slot.", m_kitGridView.getRight() + 20, m_bodyBounds.getY() + 10, 20, kDim()); }
        if (edit && (m_nav.tab == "patterns" || m_nav.tab == "kits")) { g.setColour(lcd::ink); for (int y = m_bodyBounds.getY(); y < m_bodyBounds.getBottom(); y += 4) g.fillRect(m_changes.getX() - 6, y, 1, 2); }
    } else {
        const auto& nav = m_nav.nav;
        const juce::String name = nav == "presets" ? "PRESETS" : nav == "kits" ? "KITS" : nav == "patterns" ? "PATTERNS" : nav == "favourites" ? "FAVOURITES" : nav == "saved" ? "SAVED FROM PLUGINS" : "# " + nav.substring(4).toUpperCase();
        const juce::String title = "LIBRARY  " + name;
        drawLcdText(g, spec::kFontBold8, title.toRawUTF8(), m_projectBar.getX() + 8, by + 7, 2, lcd::paper);
        int n = 0; for (const auto& r : m_list.rows()) n += r.selectable ? 1 : 0;
        ui::text(g, plural(n, "item") + ", identical items merged across projects", m_projectBar.getX() + 8 + LcdCanvas::textWidth(spec::kFontBold8, title.toRawUTF8()) * 2 + 14, by, 30, lcd::paper.withAlpha(0.8f), ui::font(false, 12.0f));
        g.setColour(lcd::ink);
        for (int y = m_bodyBounds.getY(); y < m_bodyBounds.getBottom(); y += 4) { g.fillRect(m_filterView.getRight() + 2, y, 1, 2); g.fillRect(m_list.getRight() + 5, y, 1, 2); }
        if (m_projects.empty() && m_saved.empty()) ui::text(g, "Import a Monomachine sysex dump (.syx) to start your library, or drop one on this window.", m_detail.getX() + 8, m_detail.getY() + 6, ui::kLineH);
    }
}

void LibraryComponent::resized()
{
    auto r = getLocalBounds();
    m_modal.setBounds(r);
    m_skinDialog.setBounds(getLocalBounds());
    auto header = r.removeFromTop(60).reduced(14, 0);
    m_logoBounds = header.removeFromLeft(19 * kScale).withSizeKeepingCentre(19 * kScale, 18 * kScale);
    for (auto* b : {&m_menu, &m_import}) { b->setBounds(header.removeFromRight(b->preferredWidth()).withSizeKeepingCentre(b->preferredWidth(), b->preferredHeight())); header.removeFromRight(10); }
    m_search.setBounds(header.removeFromRight(300).withSizeKeepingCentre(300, 28));
    header.removeFromRight(10);
    m_backBtn.setBounds(header.removeFromRight(m_backBtn.preferredWidth()).withSizeKeepingCentre(m_backBtn.preferredWidth(), m_backBtn.preferredHeight()));
    r.removeFromBottom(24);
    r.removeFromTop(1);
    m_railView.setBounds(r.removeFromLeft(252));
    m_rail.setSize(m_railView.getWidth() - m_railView.getScrollBarThickness(), juce::jmax(m_railView.getHeight(), m_rail.preferredHeight()));
    r.removeFromLeft(1);
    m_mainBounds = r;
    m_projectBar = r.removeFromTop(30);
    const bool proj = currentProject() != nullptr;
    const bool edit = proj && m_edit.active && m_edit.projectId == currentProject()->id;
    if (proj) {
        m_backToCurrent.setBounds(m_projectBar.getRight() - m_backToCurrent.preferredWidth() - 8, m_projectBar.getY() + (30 - m_backToCurrent.preferredHeight()) / 2, m_backToCurrent.preferredWidth(), m_backToCurrent.preferredHeight());
        m_tabsRow = r.removeFromTop(42);
        auto tabs = m_tabsRow.reduced(10, 0);
        for (auto& t : m_tabs) { t->setBounds(tabs.removeFromLeft(t->preferredWidth()).withSizeKeepingCentre(t->preferredWidth(), t->preferredHeight())); tabs.removeFromLeft(6); }
        for (auto* b : {&m_exportBtn, &m_editBtn}) { b->setBounds(tabs.removeFromRight(b->preferredWidth()).withSizeKeepingCentre(b->preferredWidth(), b->preferredHeight())); tabs.removeFromRight(6); }
        m_saveBtn.setBounds(m_tabsRow.reduced(10, 0).removeFromRight(m_saveBtn.preferredWidth()).withSizeKeepingCentre(m_saveBtn.preferredWidth(), m_saveBtn.preferredHeight()));
        m_bodyBounds = r;
        auto body = r;
        if (edit && (m_nav.tab == "patterns" || m_nav.tab == "kits")) { m_changes.setBounds(body.removeFromRight(330)); body.removeFromRight(7); }
        if (m_nav.tab == "patterns") {
            m_bankGrid.setBounds(body.removeFromTop(m_bankGrid.preferredHeight()));
            if (edit) m_tray.setBounds(body);
        } else if (m_nav.tab == "kits") {
            if (edit) { m_tray.setBounds(body.removeFromBottom(260)); m_kitGridView.setBounds(body); }
            else { m_kitGridView.setBounds(body.removeFromLeft(660)); body.removeFromLeft(12); m_slotKitViewport.setBounds(body.withWidth((body.getWidth() / kScale) * kScale)); }
            m_kitGrid.setSize(m_kitGridView.getWidth() - m_kitGridView.getScrollBarThickness(), m_kitGrid.preferredHeight());
        } else if (m_nav.tab == "history") m_historyView.setBounds(body);
    } else {
        m_bodyBounds = r;
        auto body = r;
        m_filterView.setBounds(body.removeFromLeft(190)); body.removeFromLeft(6);
        auto listCol = body.removeFromLeft(380);
        m_list.setBounds(listCol.withHeight((listCol.getHeight() / (LcdList::kRowH * kScale)) * LcdList::kRowH * kScale));
        body.removeFromLeft(12);
        m_detail.setBounds(body.withWidth((body.getWidth() / kScale) * kScale));
    }
}

// ---------------------------------------------------------------------------
// Dev / snapshot

void LibraryComponent::show(const juce::String& what)
{
    juce::StringArray parts; parts.addTokens(what, " ", "");
    const juce::String w = parts[0];
    const int n = parts.size() > 1 ? parts[parts.size() - 1].getIntValue() : 0;
    if (w == "project" || w == "edit" || w == "save" || w == "export" || w == "compare") {
        if (m_projects.empty()) return;
        m_nav.nav = "project:" + m_projects.front().id;
        const juce::String tab = parts.size() > 1 && !parts[1].containsOnly("0123456789") ? parts[1] : juce::String("patterns");
        m_nav.tab = tab == "kits" || tab == "history" || tab == "songs" ? tab : juce::String("patterns");
        if (parts.contains("bank")) { m_nav.bank = juce::jlimit(0, 7, n); m_selPat = m_nav.bank * 16; }
        if (parts.contains("slot")) m_nav.selKit = n;
        refresh();
        if (w == "edit" || w == "save") {
            beginEdit();
            for (int i = 0; i < 128; ++i) if (mnm::project::patternInUse(m_edit.state, i)) { clearSlot("pat", i); break; }
            int a = -1, b = -1; for (int i = 0; i < 128; ++i) if (mnm::project::patternInUse(m_edit.state, i)) { if (a < 0) a = i; else if (b < 0) b = i; }
            if (a >= 0 && b >= 0) dropOnSlot("pat", "slot:" + juce::String(a), b, false);
            if (!m_catalog.patterns.empty()) { for (int i = 0; i < 128; ++i) if (!mnm::project::patternInUse(m_edit.state, i)) { armFill("pat", i); break; } }
            if (w == "save") showSaveDialog();
        }
        if (w == "export") { showExportDialog(m_projects.front().current()->n); m_export.step = juce::jlimit(0, 2, n); refreshExportDialog(); }
        return;
    }
    if (w == "import" && parts.size() > 1) { startImport({what.fromFirstOccurrenceOf(" ", false, false)}); return; }
    m_nav.nav = w == "kits" || w == "patterns" ? w : juce::String("presets");
    refresh();
    int seen = -1;
    for (int i = 0; i < int(m_list.rows().size()); ++i) if (m_list.rows()[size_t(i)].selectable && ++seen == n) { m_list.select(i, true); break; }
    if (w == "params") showParams();
}

} // namespace mnm::app
