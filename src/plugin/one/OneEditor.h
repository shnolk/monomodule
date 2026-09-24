// Monomodule One / Six editor: header, LEV column, machine block, the six pages (2 rows x 3), and on
// Six a column of six track keys on the right (the hardware's track keys) that selects which track the
// pages, machine block and LEV column edit. Every element is drawn from the hardware's LCD glyphs and
// fonts, black on white.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "OneProcessor.h"
#include "OneLookAndFeel.h"
#include "Lcd.h"
#include "Overlays.h"
#include "MachinePicker.h"
#include "SkinDialog.h"
#include "LibraryUi.h"

namespace mnm::plugin::one {

// Invisible drag surface over one knob cell; the page draws the cell from the slider's value.
// A press anywhere in the cell drags the knob; a clean click (no drag) on the value row fires
// onValueClick, which opens the inline editor or the value list.
class KnobCell : public juce::Slider {
public:
    KnobCell();
    std::function<void()> onValueClick;
    void setValueArea(juce::Rectangle<int> r, juce::MouseCursor valueCursor) { m_valueArea = r; m_valueCursor = valueCursor; }
    void paint(juce::Graphics&) override {}
    void mouseEnter(const juce::MouseEvent& e) override { Slider::mouseEnter(e); notifyPage(); }
    void mouseExit(const juce::MouseEvent& e) override { Slider::mouseExit(e); notifyPage(); }
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void startedDragging() override { notifyPage(); }
    void stoppedDragging() override { notifyPage(); }
private:
    void notifyPage() { if (auto* p = getParentComponent()) p->repaint(); }
    juce::Rectangle<int> m_valueArea;
    juce::MouseCursor m_valueCursor{juce::MouseCursor::IBeamCursor};
};

// One hardware page: inverted title bar over the 2x4 knob grid, drawn at LCD resolution.
class KnobPage : public juce::Component {
public:
    static constexpr int kTitleH = 10, kGridY = kTitleH + 1;               // title bar, a blank row (room for the tie arch), grid
    static constexpr int kLcdW = 4 * kCell + 1, kLcdH = kGridY + 2 * kCell + 1;
    static constexpr int kWidth = kLcdW * kScale, kHeight = kLcdH * kScale;
    // A tabbed page's tabs overhang the title bar by two rows (their rounded tops stand in the gap above
    // the page), so the component extends that far above the page's nominal top edge.
    static constexpr int kTabOverhang = 2;
    int overhangRows() const { return m_tabs.isEmpty() ? 0 : kTabOverhang; }
    int overhangPx() const { return overhangRows() * kScale; }

    explicit KnobPage(juce::AudioProcessorValueTreeState& apvts, const char* title);
    // Tag drawn at the right end of the title bar (e.g. "PREVIEW"); null clears it.
    void setBadge(const char* badge) { m_badge = badge ? badge : ""; repaint(); }
    // Title bar as tabs (LFO2 | LFO3); onTab is called with the new index and must rebind the page.
    void setTabs(const juce::StringArray& names, int initial, std::function<void(int)> onTab);
    int currentTab() const { return m_tab; }
    // Per-cell hooks for the LFO DEST knob, whose names and icon follow the PAGE knob.
    void setValuesSource(int k, std::function<const char* const*()> fn) { m_valuesFn[size_t(k)] = std::move(fn); }
    void setIconSource(int k, std::function<const spec::Bitmap*()> fn) { m_iconFn[size_t(k)] = std::move(fn); }
    int cellValue(int k) const { return int(std::lround(m_cells[size_t(k)].getValue())); }
    // Binds the eight cells to parameters; the descriptors are copied (the SYN page changes with the machine).
    void bind(const spec::Param* params8, const std::function<juce::String(int)>& paramId);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;   // any press outside the inline editor commits it
    void mouseMove(const juce::MouseEvent&) override;   // hand cursor over the inactive tab

private:
    void beginEdit(int k);        // numeric/bipolar: inline text entry, clamped to the displayed range
    void endEdit(bool commit);
    void showValueList(int k);    // list/readout: pick an entry by name
    void refreshDynamic();        // pull DEST-style names from their sources before drawing/listing

    juce::AudioProcessorValueTreeState& m_apvts;
    juce::String m_title, m_badge;
    juce::StringArray m_tabs;
    std::array<juce::Rectangle<int>, 4> m_tabRects{};   // LCD px, in the title bar
    int m_tab = 0;
    std::function<void(int)> m_onTab;
    std::array<std::function<const char* const*()>, 8> m_valuesFn;
    std::array<std::function<const spec::Bitmap*()>, 8> m_iconFn;
    std::array<spec::Param, 8> m_params{};
    std::array<KnobCell, 8> m_cells;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 8> m_attach;
    juce::TextEditor m_editor;
    int m_editing = -1;
    juce::Component::SafePointer<juce::Component> m_listenedTop;   // window we listen on while editing
};

// The machine block: an inverted block with the group's logo (its name for
// GND/FX) and the machine name beside it, then the picker arrow; the block hugs its contents. A click
// toggles the machine picker (MachinePicker); the arrow points up while it is open.
class MachineBar : public juce::Component {
public:
    static constexpr int kLcdH = 26, kLogoX = 4, kLogoH = 18, kLogoGap = 6, kArrowGap = 4, kArrowW = 5, kPadR = 4;   // LCD px
    MachineBar();
    void setParameter(juce::RangedAudioParameter& param);   // the machine parameter of the shown track
    std::function<void()> onOpen;
    std::function<void()> onContentChanged;   // the width follows the machine; the parent must re-layout
    void setOpen(bool open) { if (m_open != open) { m_open = open; repaint(); } }
    int preferredWidth() const { return widthLcd() * kScale; }
    int preferredHeight() const { return kLcdH * kScale; }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
private:
    int logoWidthLcd() const;   // the group logo in a kLogoH-row band (drawGroupLogo), or the group's name
    int widthLcd() const;
    std::unique_ptr<juce::ParameterAttachment> m_attach;
    int m_slot = 0;
    bool m_open = false;
};

// Six: the column of track keys. One cell per track, stacked to the height of the two page rows:
// the track number, the machine's name, an activity LED lit by the track's output, a LOCK key and a MUTE key.
// A click selects the track (inverted cell); the mute key toggles the track's mute parameter; a locked track
// keeps its sound when a kit is loaded from the library.
class TrackColumn : public juce::Component {
public:
    static constexpr int kLcdW = 32, kCellH = 26;   // LCD px; 6 x 26 = the two page rows plus their gap
    TrackColumn(juce::AudioProcessorValueTreeState& apvts, int numTracks);
    std::function<void(int)> onSelect;
    std::function<bool(int)> isLocked;
    std::function<void(int)> onLock;      // toggle
    void setDropTarget(int t) { if (m_dropTarget != t) { m_dropTarget = t; repaint(); } }   // a preset dragged over a key
    int trackAt(juce::Point<int> local) const { const int t = local.y / (kCellH * kScale); return getLocalBounds().contains(local) && t >= 0 && t < m_numTracks ? t : -1; }
    void setSelected(int t) { if (m_selected != t) { m_selected = t; repaint(); } }
    int selected() const { return m_selected; }
    void setPeak(int t, float linear);   // from the editor's timer
    void refresh();                      // re-read machines and mutes; repaints on change
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
private:
    juce::Rectangle<int> muteBox(int t) const;   // LCD px
    juce::Rectangle<int> lockBox(int t) const;
    int m_dropTarget = -1;
    juce::AudioProcessorValueTreeState& m_apvts;
    int m_numTracks, m_selected = 0;
    std::array<int, kMaxTracks> m_slot{};
    std::array<bool, kMaxTracks> m_mute{}, m_active{};
};

// LEV: level fader drawn as the LCD level bar (dotted frame, filled to the value) with the output
// meter as a second bar beside it. Vertical drag.
class LevelColumn : public juce::Slider {
public:
    LevelColumn();
    void setMeter(float linear);
    void paint(juce::Graphics&) override;
private:
    float m_meter = 0.0f;
};

// BPM: the tempo in the tall digit face. Synced to the host it is a readout of the transport tempo;
// free, a horizontal drag adjusts it and a clean click types a value (as the numeric knobs do).
class BpmReadout : public juce::Slider {
public:
    BpmReadout();
    void setSynced(bool synced);
    void setHostBpm(float bpm);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
private:
    void beginEdit();
    void endEdit(bool commit);
    juce::TextEditor m_editor;
    bool m_synced = true, m_editing = false;
    float m_hostBpm = 120.0f;
};

// A two-state LCD button (SYNC): solid when on, dotted frame when off.
class LcdToggle : public juce::Button {
public:
    explicit LcdToggle(const juce::String& text) : juce::Button(text) { setClickingTogglesState(true); }
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
};

// Static text in an LCD font (title, footer).
class LcdText : public juce::Component {
public:
    LcdText(const spec::Font& font, juce::String text, int scale, bool inverted = false, juce::Justification just = juce::Justification::centredLeft);
    void paint(juce::Graphics&) override;
    void setText(juce::String s) { if (m_text != s) { m_text = std::move(s); repaint(); } }
private:
    const spec::Font& m_font;
    juce::String m_text;
    int m_scale;
    bool m_inverted;
    juce::Justification m_just;
};

// A push button drawn as inverted LCD text (MENU).
class LcdButton : public juce::Button {
public:
    explicit LcdButton(const juce::String& text) : juce::Button(text) {}
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
};

class OneEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer,
                  private juce::AudioProcessorValueTreeState::Listener {
public:
    explicit OneEditor(MnmOneProcessor&);
    ~OneEditor() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;   // the drop-target frame
    void resized() override;
    // Library drag and drop: a .mnmtrack lands on the selected track (or, on Six, the track key it is dropped
    // on); a .mnmkit fills a Six. Returns false with a message when the file cannot be applied.
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    bool dropFile(const juce::File& file, int track, juce::String& error);
    void mouseDown(const juce::MouseEvent&) override;   // a press outside the picker closes it
    void refresh() { m_proc.syncMachineSideEffects(); timerCallback(); }   // dev/snapshot: apply pending rebinds without the message loop
    void showSkinDialog() { m_skinDialog.open(); }   // (UI snapshots)
    void showMachinePicker() { m_picker.open(false); }   // dev/snapshot: fully open at once
    void showLibrary(int tab = 0) { m_libPanel.setTab(LibraryPanel::Tab(tab)); openLibrary(false); }   // dev/snapshot
    void showPresetMenu(bool kits = false) { openMenu(kits); }
    void showSaveDialog(bool kit = false) { m_saveDialog.open(kit); }
    void selectTrack(int t);   // Six: rebinds every control to track t (0-based)
    int selectedTrack() const { return m_track; }

private:
    void rebuildSynPage();
    void showConfigMenu();
    void applySkin(const skin::Skin& s, bool save);   // makes s the skin (all windows follow through the shared settings)
    void skinChanged();                             // re-skins the widgets and repaints after the skin colours changed
    void openOsChooser();
    void parameterChanged(const juce::String& id, float newValue) override;
    void timerCallback() override;

    MnmOneProcessor& m_proc;
    OneLookAndFeel m_lnf;
    juce::Rectangle<int> m_logoBounds;   // the shnolk logo (vector art)
    LcdText m_footerVersion, m_footerAlpha, m_bpmLabel;
    LcdToggle m_bpmSync{"SYNC"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> m_bpmSyncAttach;
    juce::Label m_status, m_fwPath;
    LcdButton m_menuButton{"MENU"};
    MachineBar m_machineBar;
    BpmReadout m_bpm;
    LevelColumn m_level;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_levelAttach, m_bpmAttach;
    KnobPage m_syn, m_amp, m_filt, m_efx, m_lfo1, m_lfo23;
    std::unique_ptr<TrackColumn> m_trackColumn;   // Six only
    int m_track = 0;   // the track the controls are bound to
    int dropTargetTrack(int x, int y) const;      // the track a drop at (x, y) lands on
    juce::Rectangle<int> dropFrame(const juce::StringArray& files, int x, int y) const;
    bool m_dragOver = false;
    juce::Rectangle<int> m_dropRect;
    void bindLfo(KnobPage& page, int lfo);
    void bindTrackPages();
    std::unique_ptr<juce::FileChooser> m_chooser;
    MissingOsOverlay m_missingOs;
    AboutOverlay m_about;
    MachinePicker m_picker;
    // the library (LibraryUi.h)
    void openMenu(bool kits);
    void openLibrary(bool animate = true);
    void afterLibraryLoad();
    void updateStrip();
    LibraryBridge m_lib;
    PresetStrip m_strip;
    LibraryDrop m_drop;
    LibraryPanel m_libPanel;
    SaveDialog m_saveDialog;
    SkinDialog m_skinDialog;
    int m_shownSlot = -1;   // machine slot currently bound in the SYN page
    std::atomic<bool> m_synDirty{true};
    bool m_showStatus = false;
    int m_sharedPollCountdown = 0;
    juce::String m_artPath;   // the OS path the LCD artwork was last requested for
    int m_skinPollCountdown = 0;
};

} // namespace mnm::plugin::one
