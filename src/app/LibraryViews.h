// Monomodule Library: the views. Shapes and the top-level elements (title, buttons, tabs, the inverted
// title bars, the track pages drawn as on the unit) come from the LCD glyphs and fonts (src/plugin/one/Lcd.h)
// at the plugins' scale, black on white; everything else is set in the platform's default font for
// readability, a deliberate exception to the plugins' all-LCD rule. Read-only in this stage.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Lcd.h"
#include "library/MnmDump.h"
#include "library/Catalog.h"
#include <array>
#include <functional>
#include <vector>

namespace mnm::app {

using namespace mnm::plugin::one;   // LcdCanvas, spec, kScale, lcd::paper / ink

// Body text in the default sans font, drawn in screen pixels over the LCD canvas.
namespace ui {
constexpr float kTextPx = 14.0f, kSmallPx = 12.0f, kTinyPx = 10.0f;
constexpr int kLineH = 20;   // screen px per line of body text
juce::Font font(bool bold = false, float px = kTextPx);
int width(const juce::String& s, const juce::Font& f = font());
// Left-aligned at x, vertically centred in the band [y, y + h); returns the x after the text.
int text(juce::Graphics& g, const juce::String& s, int x, int y, int h, juce::Colour c = lcd::ink, const juce::Font& f = font());
// Right-aligned so the text ends at `right`; returns the x where it starts.
int textRight(juce::Graphics& g, const juce::String& s, int right, int y, int h, juce::Colour c = lcd::ink, const juce::Font& f = font());
// "LABEL value": the label bold, the value regular; returns the x after the value.
int labelled(juce::Graphics& g, int x, int y, int h, const juce::String& label, const juce::String& value, juce::Colour c = lcd::ink);
juce::String noteName(int midiNote);   // 60 -> "C4"
void dottedFrame(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour c = lcd::ink);
// The preview glyph in any colour, centred in `zone`: a triangle, or a square while that item plays.
void playGlyph(juce::Graphics& g, juce::Rectangle<int> zone, bool playing, juce::Colour c = lcd::ink, int scale = 2);
// The loop toggle beside a stop glyph (drawLoopGlyph at a scale, centred in `zone`).
void loopGlyph(juce::Graphics& g, juce::Rectangle<int> zone, bool active, juce::Colour c = lcd::ink, int scale = 2);

// The preview transport every view shares (the app has one player): whether the playing preview loops, and the
// toggle. A view draws the loop glyph left of the stop glyph of the item that plays and calls toggle() when it
// is clicked. The Library component sets the two; without them no loop glyph is drawn.
struct Transport { std::function<bool()> looping; std::function<void()> toggleLoop; bool shown() const { return looping && toggleLoop; } };
Transport& transport();
// Wrapped body text: its height at a width, and drawing it.
int textHeight(const juce::String& s, const juce::Font& f, int width);
void wrapped(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> area, juce::Colour c = lcd::ink, const juce::Font& f = font());
}

// The play glyph (audio preview): a 4x7 triangle, a square while that item plays. Drawn `on` (ink) or as paper
// on an inverted bar. kPlayZone = the click zone in LCD px from the right edge of a row or bar.
constexpr int kPlayW = 4, kPlayH = 7, kPlayZone = 12;
void drawPlayGlyph(LcdCanvas& cv, int x, int y, bool playing, bool on = true);
// The stop glyph of a playing item with the loop toggle (ui::transport) to its left: the glyphs at their usual
// places, `x` = the play glyph's x. The right inset a row or title bar must keep free for them.
void drawStopAndLoop(LcdCanvas& cv, int x, int y, bool on = true);
inline int playInset(bool playing) { return playing && ui::transport().shown() ? 2 * kPlayZone : kPlayZone; }
// Which zone a click at `xFromRight` LCD px (measured from the right edge) falls in: 1 = play/stop, 2 = loop, 0 = none.
inline int playZoneAt(int xFromRight, bool playing) { return xFromRight < kPlayZone ? 1 : playing && ui::transport().shown() && xFromRight < 2 * kPlayZone ? 2 : 0; }

// An LCD-drawn button in an LCD face at a scale: a solid block when on (or pressed), a dotted frame otherwise.
// Buttons (bold-8 at the plugins' scale), tabs (bold-8 at 2x) and filter chips (small-4x5 at 2x) share it.
class LcdChip : public juce::Button {
public:
    static constexpr int kPadLcd = 4;
    LcdChip(const juce::String& text, const spec::Font& font, int scale, bool toggles);
    void setGhost(bool ghost) { m_ghost = ghost; repaint(); }   // a push button drawn as a dotted frame (secondary action)
    void setOnDark(bool dark) { m_onDark = dark; repaint(); }   // it sits on an inverted bar: paper and ink swap roles
    int preferredWidth() const { return (LcdCanvas::textWidth(m_font, getButtonText().toRawUTF8()) + 2 * kPadLcd) * m_scale; }
    int preferredHeight() const { return (m_font.h + 4) * m_scale; }
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
private:
    const spec::Font& m_font;
    int m_scale;
    bool m_ghost = false, m_onDark = false;
};

// Cards: an item in a dotted frame with its slot or machine in the LCD face, a preview glyph, its name and one
// meta line. The project editor's tray and every reference section ("used in patterns", "in kits", "versions")
// are grids of them. A click hands the card to onClick, the glyph to onPlay, a drag to onDragStart.
struct Card {
    juce::String label, name, metaLeft, metaRight;
    juce::var tag;          // what it is: {kind, id}
    juce::String playKey;   // "" = no preview glyph
    int group = 0;          // the tray's source filter: 0 this project, 1 other projects, 2 saved from plugins
};
class CardGrid : public juce::Component {
public:
    static constexpr int kCardH = 64, kMinW = 112, kGap = 6, kPad = 8;
    std::function<void(const Card&)> onClick, onPlay;
    std::function<void(const Card&, juce::Component*)> onDragStart;
    void set(std::vector<Card> cards) { m_cards = std::move(cards); m_hover = -1; repaint(); }
    const std::vector<Card>& cards() const { return m_cards; }
    void setPlayingKey(const juce::String& key) { if (key != m_playingKey) { m_playingKey = key; repaint(); } }
    int heightFor(int width) const;   // all rows at this width
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { if (m_hover != -1) { m_hover = -1; repaint(); } }
private:
    int columns(int width) const { return juce::jmax(1, (width - 2 * kPad + kGap) / (kMinW + kGap)); }
    juce::Rectangle<int> cardBounds(int i) const;
    int cardAt(juce::Point<int> p) const;
    static juce::Rectangle<int> glyphZone(juce::Rectangle<int> card) { return card.removeFromTop(22).removeFromRight(24); }
    static juce::Rectangle<int> loopZone(juce::Rectangle<int> card) { return glyphZone(card).translated(-24, 0); }   // the loop toggle, left of the stop
    std::vector<Card> m_cards;
    juce::String m_playingKey;
    int m_hover = -1, m_pressed = -1;
    bool m_dragging = false;
};
// A titled section of cards (an inverted bar with the count, then the grid, or a line of text when empty).
class CardSection : public juce::Component {
public:
    static constexpr int kTitleH = 10 * kScale;
    CardSection() { addAndMakeVisible(grid); }
    CardGrid grid;
    void set(const juce::String& title, std::vector<Card> cards, const juce::String& emptyText) { m_title = title; m_empty = emptyText; grid.set(std::move(cards)); repaint(); }
    int heightFor(int width) const { return kTitleH + (grid.cards().empty() ? ui::kLineH + 8 : grid.heightFor(width)); }
    void paint(juce::Graphics&) override;
    void resized() override { grid.setBounds(0, kTitleH, getWidth(), juce::jmax(0, getHeight() - kTitleH)); }
private:
    juce::String m_title, m_empty;
};

// A list of rows: top-level rows and group rows bold, items regular, in the default font over LCD-drawn markers
// and selection bars. Rows are 12 LCD px tall; the list scrolls with the wheel and shows a dotted scrollbar.
// The selected row is inverted. A row with selectable = false and no marker is a group heading.
class LcdList : public juce::Component {
public:
    struct Row {
        juce::String text, right;   // left text, right-aligned small text
        int indent = 0;             // LCD px
        int expand = -1;            // -1 plain, 0 collapsed, 1 expanded (drawn with a marker)
        bool selectable = true;
        bool playable = false;      // draws the play glyph at the right end (audio preview)
        juce::var tag;              // caller data
    };
    static constexpr int kRowH = 12;

    std::function<void(int)> onSelect;    // row index
    std::function<void(int)> onToggle;    // expandable row clicked on its marker (or double-clicked)
    std::function<void(int)> onDragStart; // a row dragged away (once per gesture)
    std::function<void(int)> onPlay;      // the play glyph of a row clicked (or space on the selected row)
    void mouseDrag(const juce::MouseEvent&) override;
    void setRows(std::vector<Row> rows, int keepSelectedIndex = -1);
    void setPlayingRow(int index);        // -1 none: that row's glyph shows stop
    const std::vector<Row>& rows() const { return m_rows; }
    int selected() const { return m_selected; }
    void select(int index, bool notify);
    void scrollTo(int index);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    int visibleRows() const { return juce::jmax(1, getHeight() / (kRowH * kScale)); }
    void clampScroll();
    std::vector<Row> m_rows;
    int m_selected = -1, m_scroll = 0, m_pressedRow = -1, m_playingRow = -1;
    bool m_dragging = false;
};

// A titled list of links to other catalog items: an inverted title bar, then rows in the default font that
// underline on hover; a click hands the row's tag to onClick.
class LinkList : public juce::Component {
public:
    struct Row { juce::String text, right; juce::var tag; };
    static constexpr int kTitleH = 10 * kScale, kRowH = ui::kLineH, kPad = 4;
    std::function<void(const juce::var&)> onClick;
    void set(const juce::String& title, std::vector<Row> rows, const juce::String& emptyText = "none");
    int preferredHeight() const { return kTitleH + kPad + juce::jmax(1, int(m_rows.size())) * kRowH + kPad; }
    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
private:
    int rowAt(juce::Point<int> p) const;
    juce::String m_title, m_empty;
    std::vector<Row> m_rows;
    int m_hover = -1;
};

// Draws a page (title bar + 2x4 knob cells) at LCD resolution into a canvas at (x0, y0), values from `raw`.
void drawPage(LcdCanvas& cv, int x0, int y0, const char* title, const spec::Param* params8, const uint8_t* raw8);
constexpr int kPageW = 4 * kCell + 1, kPageH = 11 + 2 * kCell + 1;   // as KnobPage::kLcdW/kLcdH

// The eight spec::Param records of a kit track page (SYN follows the machine, LFO DEST follows PAGE).
// pageIndex 0 SYN, 1 AMP, 2 FILT, 3 EFX, 4-6 LFO1-3. Returns false for a machine the spec does not know.
bool kitPageParams(const mnm::dump::KitTrack& track, int pageIndex, spec::Param out[8]);
const char* kitPageTitle(int pageIndex);
// "FILT BASE" for kit parameter index p (0..55) of a track, "P<n>" beyond the pages the spec describes.
juce::String kitParamName(const mnm::dump::KitTrack& track, int p);
juce::String machineDisplayName(uint8_t model);   // "SWAVE-SAW", or "MODEL n"

// The kit page: six track rows (each a link to its preset, with a play glyph for the track's part of the kit's
// preview), the kit settings, then the patterns that use the kit and the dumps it came from. The title bar's
// glyph plays the kit.
class KitView : public juce::Component {
public:
    static constexpr int kRowLcdH = 24;
    std::function<void(int)> onTrack;       // a track row clicked (-> its preset)
    std::function<void(int)> onDragTrack;   // a track row dragged away
    std::function<void()> onDragKit;        // the title bar dragged away
    std::function<void()> onPlayKit;        // the title bar's play glyph
    std::function<void(int)> onPlayTrack;   // a track row's play glyph
    std::function<void(const juce::var&)> onLink;
    void setPlaying(int stem);              // -2 nothing, -1 the kit, 0-5 a track: that glyph shows stop
    // presetNames[t] = the catalog name of track t's preset ("" for GND): shown when it is not this kit's own
    void set(const mnm::dump::Kit& kit, const std::array<juce::String, 6>& presetNames,
             std::vector<Card> patterns, std::vector<LinkList::Row> sources);
    void setTitle(const juce::String& title) { m_title = title; repaint(); }   // instead of "KIT nnn NAME" (a project slot)
    CardSection& patterns() { return m_patterns; }
    int preferredHeight() const;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
private:
    static constexpr int kSettingsLcdH = 12 + 6 * kRowLcdH + 3;   // LCD rows above the settings lines
    bool narrowLayout() const { return getWidth() < 640; }   // the project's kit pane and the browser: settings stack in two columns
    int settingsBottom() const { return kSettingsLcdH * kScale + (narrowLayout() ? 4 : 3) * ui::kLineH + ui::kLineH / 2 + (narrowLayout() ? ui::kLineH : 0); }
    juce::Rectangle<int> trackRow(int t) const;   // screen px
    mnm::dump::Kit m_kit;
    std::array<juce::String, 6> m_presetNames;
    juce::String m_title;
    CardSection m_patterns;
    LinkList m_dumps;
    int m_hover = -1, m_playing = -2;
    bool m_dragging = false;
};

// The seven pages of one kit track drawn as on the unit (scale 2) with the ASSIGN matrix.
class TrackView : public juce::Component {
public:
    static constexpr int kPageScale = 2;
    static constexpr int kLcdH = 13 + 3 * (kPageH + 4);
    std::function<void()> onDragTrack;   // anywhere on the view dragged away
    std::function<void()> onPlay;        // the title bar's play glyph
    void set(const mnm::dump::Kit& kit, int track);
    void setTitle(const juce::String& title, const juce::String& right) { m_title = title; m_right = right; repaint(); }   // instead of the kit/track default
    void setPlaying(bool b) { if (b != m_playing) { m_playing = b; repaint(); } }
    int preferredHeight() const { return kLcdH * kPageScale; }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
private:
    int playZone(juce::Point<int> p) const { return p.y < 10 * kPageScale ? playZoneAt((getWidth() - p.x) / kPageScale, m_playing) : 0; }   // 1 play/stop, 2 loop
    mnm::dump::Kit m_kit;
    int m_track = 0;
    juce::String m_title, m_right;
    bool m_dragging = false, m_playing = false;
};

// The preset page: a summary (machine, source, tags, the SYN values in one line, level, filter, delay send, LFOs in
// use), the actions, then the reference cards. The seven knob pages are one click away (VIEW PARAMETERS opens them in
// a dialog): they were too much as the default view. Dragging the title bar gives the .mnmtrack.
class PresetView : public juce::Component {
public:
    std::function<void()> onDrag, onPlay, onParams, onFavourite, onTag;
    std::function<void(const juce::var&)> onLink;
    PresetView();
    void setPlaying(bool b) { if (b != m_playing) { m_playing = b; repaint(); } }
    void set(const mnm::catalog::PresetItem& preset, const juce::String& source, const juce::StringArray& tags, bool favourite,
             std::vector<Card> versions, std::vector<Card> kits, std::vector<Card> patterns);
    CardSection& versions() { return m_versions; }
    CardSection& kits() { return m_kits; }
    CardSection& patterns() { return m_patterns; }
    int preferredHeight() const;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
private:
    static constexpr int kSummaryTop = 12 * kScale, kChipsTop = kSummaryTop + 4 * ui::kLineH + 6;
    int playZone(juce::Point<int> p) const { return p.y < 10 * kScale ? playZoneAt((getWidth() - p.x) / kScale, m_playing) : 0; }   // 1 play/stop, 2 loop
    int sectionsTop() const { return kChipsTop + 34; }
    mnm::catalog::PresetItem m_preset;
    juce::String m_source;
    juce::StringArray m_tags;
    bool m_favourite = false, m_playing = false, m_dragging = false, m_hasVersions = false;
    LcdChip m_params{"VIEW PARAMETERS", spec::kFontBold8, 2, false}, m_fav{"FAVOURITE", spec::kFontBold8, 2, false}, m_tag{"ADD TAG", spec::kFontBold8, 2, false};
    CardSection m_versions, m_kits, m_patterns;
};

// One track of a pattern: a header (track, preset link, machine, trig and lock counts), a piano roll of the
// track's notes (steps across, its pitches stacked, lengths to the note-off), a trig lane, one lane per
// locked parameter with a mark on every locked step, and a readout of the selected step's note and lock
// values. Dragging the block gives the track's MIDI file.
class PatternTrackBlock : public juce::Component {
public:
    struct Info { int track = 0; juce::String presetId, presetName, machine; };
    static constexpr int kHeaderH = 20, kGutterLcd = 60, kRowLcd = 3, kReadoutH = ui::kLineH, kPadH = 8, kPlayPx = 30;
    // LCD px per step: 3 when the page is wide enough for 64 steps, 2 in the narrower browser pane
    int stepLcd() const { return (getWidth() / kScale - kGutterLcd - 2) / 64 >= 3 ? 3 : 2; }
    std::function<void()> onDrag;
    std::function<void()> onPreset;
    std::function<void()> onPlay;      // the header's play glyph: this track's own output of the pattern preview
    void set(const mnm::dump::Pattern& pat, const mnm::dump::Kit* kit, const Info& info);
    void setPlaying(bool b) { if (b != m_playing) { m_playing = b; repaint(); } }
    void selectStep(int step) { m_selectedStep = step; repaint(); }   // as a click on the step does (dev/snapshot)
    int preferredHeight() const;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
private:
    struct Lane { int row; juce::String name; };
    int noteAt(int step) const;                 // the played note of a note trig at step, or -1
    int canvasTop() const { return kHeaderH + 1; }
    int canvasLcdH() const { return int(m_pitches.size()) * kRowLcd + 1 + kRowLcd + (m_lanes.empty() ? 0 : 1 + int(m_lanes.size()) * kRowLcd) + 1; }
    int stepAt(juce::Point<int> p) const;       // -1 outside the grid
    mnm::dump::Pattern m_pat;
    bool m_hasKit = false;
    mnm::dump::Kit m_kit;
    Info m_info;
    std::vector<int> m_pitches;                 // distinct played notes, ascending
    std::vector<Lane> m_lanes;                  // lock rows of this track
    int m_selectedStep = -1, m_hoverStep = -1;
    juce::Rectangle<int> m_presetLink;
    bool m_overLink = false, m_dragging = false, m_playing = false;
    juce::Rectangle<int> playZone() const { return {getWidth() - kPlayPx, 0, kPlayPx, kHeaderH}; }
    juce::Rectangle<int> loopZone() const { return playZone().translated(-kPlayPx, 0); }   // the loop toggle, left of the stop
};

// The pattern page: summary, one block per track, then the kit and the dumps it came from.
class PatternView : public juce::Component {
public:
    std::function<void(int)> onDragTrack;   // a block dragged away: its MIDI
    std::function<void()> onDragPattern;    // the title bar dragged away: the whole pattern's MIDI
    std::function<void(int)> onPreset;      // a block's preset link
    std::function<void()> onPlayPattern;    // the title bar's play glyph
    std::function<void(int)> onPlayTrack;   // a block's play glyph
    std::function<void(const juce::var&)> onLink;
    static constexpr int kBlocksTop = 26 * kScale;
    PatternView();
    void setPlaying(int stem);              // -2 nothing, -1 the pattern, 0-5 a track
    void set(const mnm::dump::Pattern& pat, const mnm::dump::Kit* kit, const std::array<PatternTrackBlock::Info, 6>& info,
             std::vector<Card> kitCard, std::vector<LinkList::Row> sources);
    void setTitle(const juce::String& title) { m_title = title; repaint(); }
    CardSection& kitSection() { return m_kitLink; }
    void selectStep(int step) { for (auto& b : m_blocks) b.selectStep(step); }   // dev/snapshot
    int preferredHeight() const;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
private:
    int playZone(juce::Point<int> p) const { return p.y < 10 * kScale ? playZoneAt((getWidth() - p.x) / kScale, m_playing == -1) : 0; }   // 1 play/stop, 2 loop
    mnm::dump::Pattern m_pat;
    bool m_hasKit = false;
    mnm::dump::Kit m_kit;
    std::array<PatternTrackBlock, 6> m_blocks;
    juce::String m_title;
    CardSection m_kitLink;
    LinkList m_dumps;
    bool m_dragging = false;
    int m_playing = -2;
};

// Shared drawing helpers.
// 10 rows, inverted, bold-8; rightInset keeps the right text clear of a glyph drawn at the bar's right end.
void drawTitleBar(LcdCanvas& cv, int x0, int y0, int w, const char* text, const char* rightText = nullptr, int rightInset = 0);

} // namespace mnm::app
