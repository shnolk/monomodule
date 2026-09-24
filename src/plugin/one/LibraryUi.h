// Monomodule One / Six: the library inside the plugin, in the LCD faces like the rest of the editor.
//   LibraryBridge  what the views need from the processor and the shared library model
//   PresetStrip    the joined control strip at the top centre of the header: previous / preset selector / next /
//                  save / library (Six: a kit selector with its own save in front)
//   LibraryDrop    the selector's list: this machine's presets or all machines, a click loads, the glyph auditions
//   LibraryPanel   the library proper: unrolls from under the header over the pages like the machine picker
//                  (Six: the track keys stay visible, they are the drop targets and carry the LOCKs); a filter
//                  column and a four-column grid; a click loads and the panel stays open
//   SaveDialog     saving never overwrites: a new version of the loaded item or a new item, optionally also put
//                  into the project the item came from (as a new version of that project)
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "OneProcessor.h"
#include "Lcd.h"
#include "LibraryModel.h"

namespace mnm::plugin::one {

class LibraryBridge {
public:
    LibraryBridge(MnmOneProcessor& proc, std::function<int()> track) : m_proc(proc), m_track(std::move(track)) {}
    bool refresh(bool force = false) { return m_model->refresh(force); }
    const mnm::catalog::Catalog& cat() const { return m_model->catalog(); }
    mnm::library::LibraryModel& model() { return *m_model; }
    MnmOneProcessor& proc() { return m_proc; }
    int track() const { return m_track(); }
    bool six() const { return m_proc.numTracks() > 1; }
    uint8_t machineModel() const { return uint8_t(machineAt(int(std::lround(m_proc.apvts.getRawParameterValue(machineId(track()))->load())))); }
    bool favourite(const std::string& id) const { return m_model->user().isFavourite(juce::String(id)); }

    // Sorted by the machine menu's order, then by name. model < 0 = every machine; group null = every group.
    std::vector<const mnm::catalog::PresetItem*> presets(int model, const char* group, bool favourites, bool savedOnly, const juce::String& query) const;
    std::vector<const mnm::catalog::KitItem*> kits(const juce::String& source, bool favourites, const juce::String& query) const;   // source: "" all, "saved", or a project id
    std::vector<const mnm::catalog::PatternItem*> patterns(int bank, const juce::String& query) const;                          // bank < 0 = all

    bool loadPreset(const std::string& id, int track = -1);   // -1 = the selected track
    bool loadKit(const std::string& id);
    bool loadPatternKit(const std::string& patternId);
    void step(int dir);                                        // previous / next preset of this machine
    void preview(const juce::String& kind, const std::string& id);   // a second click stops
    bool looping() const { return m_proc.previewLoop(); }
    void toggleLoop() { m_proc.previewSetLoop(!m_proc.previewLoop()); }
    bool previewing(const juce::String& kind, const std::string& id) const { return m_proc.previewKey() == kind + "/" + juce::String(id); }
    juce::File patternMidiFile(const std::string& patternId);  // written to the temp folder, for dragging into the DAW

    juce::String message;                 // the last load's problem or acknowledgement (the panel's footer shows it)
    std::function<void()> onLoaded;       // the editor rebinds / repaints

private:
    MnmOneProcessor& m_proc;
    std::function<int()> m_track;
    juce::SharedResourcePointer<mnm::library::LibraryModel> m_model;
};

class PresetStrip : public juce::Component, public juce::SettableTooltipClient {
public:
    enum Part { None = -1, Kit, KitSave, Prev, Preset, Next, Save, Library };
    static constexpr int kS = 2, kLcdH = 15;
    explicit PresetStrip(bool six) : m_six(six) {}
    std::function<void(Part)> onPart;
    void setPreset(const juce::String& name, bool modified, int track);
    void setKit(const juce::String& name, bool modified);
    void setOpen(Part menu, bool library);
    juce::Rectangle<int> partBounds(Part p) const { return (m_rects[size_t(p)] * kS); }   // in this component
    int preferredWidth(int available) const;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { if (m_hover != None) { m_hover = None; repaint(); } }
private:
    Part partAt(juce::Point<int> p) const;
    bool m_six;
    juce::String m_preset = "INIT", m_kit = "INIT";
    bool m_presetMod = false, m_kitMod = false, m_libOpen = false;
    int m_track = 0;
    Part m_menu = None, m_hover = None;
    std::array<juce::Rectangle<int>, 7> m_rects{};   // LCD px
};

class LibraryDrop : public juce::Component {
public:
    static constexpr int kS = 2, kRowH = 12, kHeadH = 15, kFootH = 15, kLcdW = 210;
    explicit LibraryDrop(LibraryBridge& b) : m_b(b) { setWantsKeyboardFocus(true); }
    void open(bool kits, juce::Point<int> topLeft, int maxHeight);
    void close() { setVisible(false); if (onClosed) onClosed(); }
    bool kits() const { return m_kits; }
    std::function<void()> onClosed, onLibrary;
    void rebuild();
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { m_hover = -1; repaint(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    struct Row { bool header = false; std::string id; juce::String name, right; bool on = false, fav = false; };
    int listTop() const { return m_kits ? 1 : kHeadH; }
    int listRows() const { return (getHeight() / kS - listTop() - kFootH) / kRowH; }
    int rowAt(juce::Point<int> lcd) const;
    LibraryBridge& m_b;
    bool m_kits = false, m_all = false;
    std::vector<Row> m_rows;
    int m_scroll = 0, m_hover = -1, m_sameCount = 0, m_allCount = 0;
};

class LibraryPanel : public juce::Component, private juce::Timer {
public:
    enum Tab { Presets, Kits, Patterns };
    static constexpr int kS = 2, kBarH = 17, kFootH = 13, kFilterW = 78, kCellH = 13, kHeadRowH = 13, kCols = 4;
    explicit LibraryPanel(LibraryBridge& b);
    void setTargetBounds(juce::Rectangle<int> fullyOpen);
    void open(bool animate = true);
    void close(bool animate = true);
    bool isOpen() const { return m_wantOpen; }
    void setTab(Tab t) { m_tab = t; m_scroll = 0; rebuild(); }
    std::function<void(bool)> onOpenChanged;
    std::function<void(juce::Point<int> screenPos)> onPresetDragging;
    std::function<void(const std::string& presetId, juce::Point<int> screenPos)> onPresetDropped;   // Six: released over a track key
    void rebuild();
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { m_hover = -1; repaint(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    struct Item { bool header = false; std::string id; juce::String name, right; bool on = false, fav = false; juce::Rectangle<int> r; };   // r: LCD px in the grid's content space
    struct Filter { juce::String label, value; int section = 0; bool header = false; juce::Rectangle<int> r; };
    juce::Rectangle<int> gridArea() const;   // LCD px
    int itemAt(juce::Point<int> lcd) const;
    int contentHeight() const { return m_items.empty() ? 0 : m_items.back().r.getBottom() + 2; }
    void clampScroll();
    void timerCallback() override;
    void applyAnimation();
    LibraryBridge& m_b;
    Tab m_tab = Presets;
    juce::String m_group, m_kitSource, m_query;   // "" = all
    int m_bank = -1;
    bool m_favourites = false, m_savedOnly = false;
    std::vector<Item> m_items;
    std::vector<Filter> m_filters;
    std::array<juce::Rectangle<int>, 3> m_tabRects{};
    juce::Rectangle<int> m_closeRect, m_appRect;
    int m_scroll = 0, m_hover = -1, m_pressed = -1;
    bool m_dragging = false;
    juce::Rectangle<int> m_target;
    bool m_wantOpen = false;
    float m_anim = 0.0f;
    int m_seenRevision = -1;
};

class SaveDialog : public juce::Component {
public:
    static constexpr int kS = 2, kLcdW = 300, kLcdH = 140;
    explicit SaveDialog(LibraryBridge& b) : m_b(b) { setWantsKeyboardFocus(true); }
    void open(bool kit);
    std::function<void()> onDone;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    void save();
    juce::Rectangle<int> box() const { return juce::Rectangle<int>(0, 0, kLcdW * kS, kLcdH * kS).withCentre(getLocalBounds().getCentre()); }
    LibraryBridge& m_b;
    bool m_kit = false, m_asVersion = true, m_intoProject = false;
    juce::String m_name, m_error;
    MnmOneProcessor::LoadedRef m_loaded;
    mnm::library::LibraryModel::Slot m_slot;
    juce::Rectangle<int> m_optVersion, m_optNew, m_optProject, m_cancel, m_save;   // LCD px inside the box
};

} // namespace mnm::plugin::one
