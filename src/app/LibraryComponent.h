// Monomodule Library: the main window content.
//   header   logo, title, BACK, search, IMPORT SYSEX, MENU
//   rail     PROJECTS (one Monomachine's memory each, with a version history), LIBRARY (every preset / kit / pattern
//            across the projects' current versions plus what the plugins saved, merged by content), COLLECTIONS
//            (favourites, saved from plugins, tags), DEVICE
//   main     a project: PATTERNS (bank keys A-H, 16 slots), KITS (128 slots + the kit's page), SONGS, GLOBALS, HISTORY;
//            or the browser: filter column, list, the item's page
// A project is read-only until EDIT PROJECT: edit mode collects slot changes (clear, fill, move / swap, copy, library
// items dragged in) as an undoable list, and is left through SAVE only, which adds the next version. Nothing is ever
// overwritten. Every item has a preview glyph (PreviewPlayer). Dialogs are in-window (ModalLayer).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "LibraryViews.h"
#include "ProjectViews.h"
#include "Store.h"
#include "PreviewPlayer.h"
#include "OneLookAndFeel.h"
#include "SkinDialog.h"
#include <map>
#include <memory>

namespace mnm::app {

class LibraryComponent : public juce::Component, public juce::FileDragAndDropTarget, public juce::DragAndDropContainer, private juce::Timer {
public:
    static constexpr int kWidth = 1400, kHeight = 800;
    explicit LibraryComponent(std::unique_ptr<mnm::library::Store> store, bool audio = true);
    ~LibraryComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int, int) override;
    bool keyPressed(const juce::KeyPress&) override;

    // Dev/snapshot (mnm-libtool render): "project [patterns|kits|history] [bank N | slot N]", "edit [kits]", "save",
    // "presets|kits|patterns [row]", "params [row]", "import <file>", "export [step]".
    void show(const juce::String& what);
    void reload();

private:
    struct NavState { juce::String nav, tab, itemKind, itemId, group, source; int bank = 0, selKit = 0, viewVersion = 0; bool favOnly = false; };
    struct EditSession {
        bool active = false;
        juce::String projectId, fill;   // fill = "pat:<pos>" | "kit:<pos>": the armed empty slot
        int baseVersion = 0;
        mnm::dump::Dump state;
        juce::StringArray changes;
        std::set<int> changedPat, changedKit;
        struct Undo { mnm::dump::Dump state; juce::StringArray changes; std::set<int> changedPat, changedKit; };
        std::vector<Undo> undo;
    };

    // data
    void rebuildCatalog();
    const mnm::library::ProjectInfo* project(const juce::String& id) const;
    const mnm::library::ProjectInfo* currentProject() const { return m_nav.nav.startsWith("project:") ? project(m_nav.nav.substring(8)) : nullptr; }
    const mnm::dump::Dump* shownState();                      // the edit session, the viewed version, or the current one
    const mnm::dump::Dump* currentState(const juce::String& projectId);
    // navigation
    void setNav(const juce::String& nav, bool push = true);
    void navigateItem(const juce::String& kind, const juce::String& id, bool push = true);
    void pushBack();
    void goBack();
    void refresh();            // everything from the state: rail, panes, visibility
    void refreshRail();
    void refreshProject();
    void refreshBrowser();
    void rebuildList();
    void showItem();
    void onLink(const juce::var& tag);
    // project
    void selectBank(int b);
    void openSlot(const juce::String& kind, int pos);
    void showSlotKit();
    void beginEdit();
    void pushUndo();
    void noteChange(const juce::String& text);
    void clearSlot(const juce::String& kind, int pos);
    void armFill(const juce::String& kind, int pos);
    void dropOnSlot(const juce::String& kind, const juce::String& source, int pos, bool copy);
    void placeLibraryItem(const juce::String& kind, const juce::String& id, int pos);
    void undoEdit();
    std::vector<Card> trayCards(const juce::String& kind) const;
    // dialogs
    void closeDialog();
    void startImport(const juce::StringArray& paths);
    void nextImport();
    void showSaveDialog();
    void showExportDialog(int version);
    void refreshExportDialog();
    void showCompare(int version);
    void confirmRestore(int version);
    void showParams();
    void showTagDialog();
    void showRenameDialog();
    void showDeleteDialog();
    void info(const juce::String& title, const juce::String& text);
    // cards and rows
    Card patternCard(const mnm::catalog::PatternItem& p) const;
    Card kitCard(const mnm::catalog::KitItem& k, const juce::String& note = {}) const;
    Card presetCard(const mnm::catalog::PresetItem& p, const juce::String& note = {}) const;
    std::vector<LinkList::Row> sourceLinks(const std::vector<mnm::catalog::Source>& sources, bool kits) const;
    juce::String sourceText(const std::vector<mnm::catalog::Source>& sources) const;
    bool passesSource(const std::vector<mnm::catalog::Source>& sources) const;
    // preview
    void playCatalog(const juce::String& kind, const juce::String& id, int stem);
public:
    // UI snapshots (mnm-libtool render ... playing [loop]): shows the views as if the current page's item played,
    // with the loop toggle on or off, without a player.
    void debugShowPlaying(bool loop);
private:
    void playSlot(const juce::String& kind, int pos);
    void playTag(const juce::String& tag, const juce::String& key, const std::function<mnm::preview::PreviewSpec()>& build, int stem);
    void refreshPlaying();
    // misc
    void importChooser();
    void selectOsFile();
    void setPreviewTempo();
    void showMenu();
    void toast(const juce::String& s);
    void timerCallback() override;
    void toggleFavourite();
    // drag sources to the DAW / plugins
    void dragPreset(const juce::String& id, juce::Component* source);
    void dragKit(const juce::String& id, juce::Component* source);
    void dragPatternMidi(const juce::String& id, int track, juce::Component* source);
    static void startFileDrag(const juce::File& f, juce::Component* source);

    std::unique_ptr<mnm::library::Store> m_store;
    mnm::plugin::one::OneLookAndFeel m_lnf;
    mnm::plugin::one::SkinDialog m_skinDialog;
    void skinChanged();   // the skin colours changed: re-skin the widgets and the window, repaint
    int m_skinPollCountdown = 0;
    std::vector<mnm::library::ProjectInfo> m_projects;
    std::map<juce::String, std::pair<int, mnm::dump::Dump>> m_states;   // project id -> (version, its state)
    mnm::dump::Dump m_viewed;                                            // HISTORY > VIEW
    int m_viewedVersion = 0;
    juce::String m_viewedProject;
    std::vector<mnm::library::SavedItem> m_saved;
    mnm::library::UserData m_user;
    mnm::catalog::Catalog m_catalog;
    NavState m_nav;
    std::vector<NavState> m_back;
    EditSession m_edit;
    int m_selPat = 0;

    // header
    juce::Rectangle<int> m_logoBounds;
    LcdChip m_backBtn{"BACK", spec::kFontBold8, kScale, false}, m_import{"IMPORT SYSEX", spec::kFontBold8, kScale, false}, m_menu{"MENU", spec::kFontBold8, kScale, false};
    juce::TextEditor m_search;
    // rail
    juce::Viewport m_railView;
    Rail m_rail;
    // project pane
    std::array<std::unique_ptr<LcdChip>, 5> m_tabs;
    LcdChip m_editBtn{"EDIT PROJECT", spec::kFontBold8, 2, false}, m_exportBtn{"EXPORT SYSEX", spec::kFontBold8, 2, false}, m_saveBtn{"SAVE...", spec::kFontBold8, 2, false},
            m_backToCurrent{"BACK TO CURRENT", spec::kFontSmall4x5, 2, false};
    BankGrid m_bankGrid;
    juce::Viewport m_kitGridView, m_historyView, m_slotKitViewport;
    KitGrid m_kitGrid;
    HistoryView m_history;
    KitView m_slotKitView;
    Tray m_tray;
    ChangesView m_changes;
    juce::Rectangle<int> m_mainBounds, m_projectBar, m_tabsRow, m_bodyBounds;
    // browser pane
    juce::Viewport m_filterView, m_detail;
    Rail m_filters;
    LcdList m_list;
    PresetView m_presetView;
    KitView m_kitView;
    PatternView m_patternView;
    // dialogs
    ModalLayer m_modal;
    FormDialog m_form;
    ParamsDialog m_paramsDialog;
    juce::String m_dialog;                      // which dialog the form shows
    juce::StringArray m_importQueue;
    juce::String m_importFile, m_importSimilar;
    struct ExportState { juce::String projectId; int version = 0, step = 0, base = 0; juce::String what = "all", how = "file"; mnm::dump::Dump state; mnm::project::DumpDiff diff; std::set<juce::String> kits, patterns; } m_export;
    int m_dialogVersion = 0;
    // preview
    std::unique_ptr<mnm::library::PreviewPlayer> m_player;
    juce::String m_playingTag, m_osPath, m_status, m_toast;
    bool m_debugPlaying = false;
    int m_toastTicks = 0;
    std::unique_ptr<juce::FileChooser> m_chooser;
    juce::int64 m_stamp = 0;
};

} // namespace mnm::app
