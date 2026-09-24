// Monomodule Library: the project side of the app and its dialogs.
//   Rail         the left column: PROJECTS, LIBRARY, COLLECTIONS, DEVICE (also used as the browser's filter column)
//   BankGrid     a project's patterns as on the unit: bank keys A-H, 16 slots per bank
//   KitGrid      the 128 kit slots as a compact four-column grid
//   Tray         edit mode: library patterns / kits to drag (or click) into slots
//   ChangesView  edit mode: what this edit session changed so far, with UNDO
//   HistoryView  the version timeline
//   FormDialog, ParamsDialog, ModalLayer   the in-window dialogs (import, export, save, compare, parameters)
// View mode is read-only. In edit mode slots get an X (clear), empty slots a + on hover, slots drag onto each other
// (move / swap, Option = copy) and tray cards drag into slots. Drag descriptions: "pat:slot:<pos>", "pat:lib:<id>",
// "kit:slot:<pos>", "kit:lib:<id>".
#pragma once
#include "LibraryViews.h"
#include "Store.h"
#include <set>

namespace mnm::app {

class Rail : public juce::Component {
public:
    struct Item {
        enum Kind { Header, Entry, Note } kind = Entry;
        juce::String id, text, sub, count;
        int indent = 0;
        const char* logoGroup = nullptr;   // a machine group's logo before the text (the browser's MACHINE filter)
    };
    std::function<void(const juce::String&)> onSelect;
    void set(std::vector<Item> items, std::set<juce::String> selectedIds) { m_items = std::move(items); m_selected = std::move(selectedIds); m_hover = -1; repaint(); }
    int preferredHeight() const;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { if (m_hover != -1) { m_hover = -1; repaint(); } }
private:
    static int rowHeight(const Item& it) { return it.kind == Item::Header ? 30 : it.sub.isNotEmpty() ? 42 : 26; }
    int itemAt(int y) const;
    std::vector<Item> m_items;
    std::set<juce::String> m_selected;
    int m_hover = -1;
};

struct SlotInfo {
    bool used = false, changed = false;
    juce::String name;        // patterns: the kit's name; kits: the kit's name
    int tracks = 0, steps = 0;
};

// Shared by the two slot grids: edit-mode state and the drop-target plumbing.
class SlotGridBase : public juce::Component, public juce::DragAndDropTarget {
public:
    std::function<void(int)> onOpen, onSelect, onPlay, onClear, onFill;
    std::function<void(const juce::String& source, int pos, bool copy)> onDrop;   // source = "slot:<pos>" | "lib:<id>"
    void setPlayingSlot(int pos) { if (pos != m_playing) { m_playing = pos; repaint(); } }
    bool isInterestedInDragSource(const SourceDetails& d) override { return m_edit && d.description.toString().startsWith(m_kind + ":"); }
    void itemDragMove(const SourceDetails& d) override;
    void itemDragExit(const SourceDetails&) override { if (m_over != -1) { m_over = -1; repaint(); } }
    void itemDropped(const SourceDetails& d) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { if (m_hover != -1) { m_hover = -1; repaint(); } }
protected:
    explicit SlotGridBase(juce::String kind) : m_kind(std::move(kind)) {}
    virtual int slotAt(juce::Point<int> p) const = 0;
    virtual juce::Rectangle<int> slotBounds(int pos) const = 0;
    virtual juce::Rectangle<int> glyphZone(int pos) const = 0;   // the play glyph / X / +
    juce::Rectangle<int> loopZone(int pos) const { const auto z = glyphZone(pos); return z.translated(-z.getWidth(), 0); }   // the loop toggle, left of the stop
    virtual juce::Rectangle<int> clearZone(int pos) const = 0;
    virtual void dragHover(juce::Point<int>) {}
    juce::String m_kind;   // "pat" | "kit"
    std::array<SlotInfo, 128> m_slots{};
    bool m_edit = false;
    int m_selected = -1, m_fill = -1, m_hover = -1, m_over = -1, m_pressed = -1, m_playing = -1;
    bool m_dragging = false;
};

class BankGrid : public SlotGridBase {
public:
    BankGrid() : SlotGridBase("pat") {}
    std::function<void(int)> onBank;
    void set(const std::array<SlotInfo, 128>& slots, int bank, bool edit, int selected, int fillTarget);
    int bank() const { return m_bank; }
    int preferredHeight() const { return kBanksH + 2 * (kSlotH + kGap) + 10; }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
private:
    static constexpr int kBanksH = 70, kBankW = 74, kSlotH = 96, kGap = 6, kPad = 10;
    int bankAt(juce::Point<int> p) const;
    int slotAt(juce::Point<int> p) const override;
    juce::Rectangle<int> slotBounds(int pos) const override;
    juce::Rectangle<int> glyphZone(int pos) const override { return slotBounds(pos).removeFromTop(24).removeFromRight(26); }
    juce::Rectangle<int> clearZone(int pos) const override { return glyphZone(pos); }
    void dragHover(juce::Point<int> p) override;
    int m_bank = 0;
};

class KitGrid : public SlotGridBase {
public:
    KitGrid() : SlotGridBase("kit") {}
    static constexpr int kRowH = 24, kCols = 4, kPad = 8;
    void set(const std::array<SlotInfo, 128>& slots, bool edit, int selected, int fillTarget);
    int preferredHeight() const { return 2 * kPad + 32 * kRowH; }
    void paint(juce::Graphics&) override;
private:
    int slotAt(juce::Point<int> p) const override;
    juce::Rectangle<int> slotBounds(int pos) const override;
    juce::Rectangle<int> glyphZone(int pos) const override { auto r = slotBounds(pos); r.removeFromRight(m_edit ? 22 : 0); return r.removeFromRight(22); }
    juce::Rectangle<int> clearZone(int pos) const override { return slotBounds(pos).removeFromRight(22); }
};

class Tray : public juce::Component {
public:
    Tray();
    std::function<void(const Card&)> onPick, onPlay;
    std::function<void(const Card&, juce::Component*)> onDragStart;
    void set(const juce::String& title, std::vector<Card> cards);
    void setHint(const juce::String& hint) { if (hint != m_hint) { m_hint = hint; repaint(); } }
    void setPlayingKey(const juce::String& k) { m_grid.setPlayingKey(k); }
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void refilter();
    juce::String m_title, m_hint;
    std::vector<Card> m_all;
    int m_source = -1;   // -1 all, else Card::group
    std::array<std::unique_ptr<LcdChip>, 4> m_chips;
    juce::TextEditor m_filter;
    juce::Viewport m_viewport;
    CardGrid m_grid;
};

class ChangesView : public juce::Component {
public:
    ChangesView();
    std::function<void()> onUndo;
    void set(const juce::StringArray& changes);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    juce::StringArray m_changes;
    LcdChip m_undo{"UNDO", spec::kFontBold8, 2, false};
};

class HistoryView : public juce::Component {
public:
    std::function<void(int)> onView, onCompare, onRestore, onExport;
    std::function<void()> onSave;
    void set(const mnm::library::ProjectInfo& project, bool editing, const juce::StringArray& pendingChanges);
    int preferredHeight() const { return m_height; }
    void paint(juce::Graphics&) override;
    void resized() override { layoutRows(); }
private:
    struct Row { int n = 0; bool pending = false; juce::String label, kind, title, when; juce::StringArray changes; int y = 0, h = 0; std::vector<std::unique_ptr<LcdChip>> chips; };
    void layoutRows();
    std::vector<std::unique_ptr<Row>> m_rows;
    int m_height = 100;
};

// ---- dialogs
struct DialogSpec {
    struct Row {
        enum Kind { Text, Bold, Dim, Bullet, Check, Option, Field, Ticks, Steps, Gap } kind = Text;
        juce::String id, a, b;                 // Option: id, title, text. Check: a = mark, b = text. Field: id, a = label, b = value. Steps: a = "1 WHAT|2 CHECK|3 SEND", b = current index
        juce::StringArray items, itemIds;      // Ticks
        bool disabled = false;
    };
    juce::String title, sub, selected, link;   // selected = option id; link = a text link at the left of the footer
    std::vector<Row> rows;
    std::vector<std::pair<juce::String, juce::String>> buttons;   // id, label; the last one is the primary
    int width = 720;
};

class TickGrid : public juce::Component {
public:
    void set(const juce::StringArray& labels, const juce::StringArray& ids, const std::set<juce::String>& ticked) { m_labels = labels; m_ids = ids; m_ticked = ticked; repaint(); }
    const std::set<juce::String>& ticked() const { return m_ticked; }
    int preferredHeight() const { return ((m_labels.size() + 3) / 4) * 22 + 4; }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
private:
    juce::StringArray m_labels, m_ids;
    std::set<juce::String> m_ticked;
};

class FormDialog : public juce::Component {
public:
    std::function<void(const juce::String& buttonId)> onButton;   // also "link"
    std::function<void(const juce::String& optionId)> onOption;
    void set(DialogSpec spec, const std::map<juce::String, std::set<juce::String>>& ticks = {});
    const juce::String& selected() const { return m_spec.selected; }
    juce::String field(const juce::String& id) const;
    std::set<juce::String> ticked(const juce::String& id) const;
    int preferredHeight() const { return m_height; }
    int preferredWidth() const { return m_spec.width; }
    void paint(juce::Graphics&) override;
    void resized() override { layout(); }
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
private:
    struct Laid { juce::Rectangle<int> r; };
    void layout();
    DialogSpec m_spec;
    std::vector<juce::Rectangle<int>> m_rects;
    std::map<juce::String, std::unique_ptr<juce::TextEditor>> m_fields;
    std::map<juce::String, std::unique_ptr<juce::Viewport>> m_tickViews;
    std::map<juce::String, std::unique_ptr<TickGrid>> m_tickGrids;
    std::vector<std::unique_ptr<LcdChip>> m_buttons;
    juce::Rectangle<int> m_linkRect;
    int m_height = 200;
};

// The seven knob pages of a preset, one click away from its page.
class ParamsDialog : public juce::Component {
public:
    ParamsDialog();
    std::function<void()> onClose;
    void set(const mnm::dump::Kit& kit, int track, const juce::String& title, const juce::String& right);
    int preferredWidth() const { return 3 * (kPageW + 4) * TrackView::kPageScale + 24; }
    int preferredHeight() const { return m_pages.preferredHeight() + 24 + 40; }
    void paint(juce::Graphics& g) override { g.fillAll(lcd::paper); }
    void resized() override;
private:
    TrackView m_pages;
    LcdChip m_close{"CLOSE", spec::kFontBold8, 2, false};
};

// Covers the window with a pale scrim and centres one dialog in a solid frame with an offset shadow.
class ModalLayer : public juce::Component {
public:
    ModalLayer() { setInterceptsMouseClicks(true, true); setWantsKeyboardFocus(true); }
    std::function<void()> onEscape;
    void show(juce::Component* dialog, int w, int h);
    void hide();
    bool isShowing() const { return m_dialog != nullptr; }
    juce::Component* dialog() const { return m_dialog; }
    void relayout(int w, int h);
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& k) override { if (k == juce::KeyPress::escapeKey && onEscape) { onEscape(); return true; } return false; }
private:
    juce::Component* m_dialog = nullptr;
    int m_w = 0, m_h = 0;
};

} // namespace mnm::app
