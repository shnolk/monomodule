// Monomodule One: the machine picker. Fills the six-page area with one column per machine group in
// the hardware's assign-menu order (GND, SID, SWAVE, DPRO, FM+, VO, FX): the group's logo
// (or its name where there is none), a blurb, then the group's machines as rows
// with a one-line description, all in the LCD faces. The current machine's row is inverted;
// a click assigns and closes. The machine block above toggles it; Escape or a click elsewhere cancels.
// Opening unrolls it from the top edge over the pages (and closing rolls it back up).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include "Lcd.h"

namespace mnm::plugin::one {

class MachinePicker : public juce::Component, private juce::Timer {
public:
    MachinePicker();
    void setParameter(juce::RangedAudioParameter& machineParam);
    void setFxOnly();   // Monomodule FX: only the FX group's column, at the width it has among all seven   // the machine parameter of the shown track
    void setTargetBounds(juce::Rectangle<int> fullyOpen);   // the six pages' area, in the parent
    void open(bool animate = true);
    void close(bool animate = true);
    bool isOpen() const { return m_wantOpen; }              // requested state (true while unrolling)
    std::function<void(bool)> onOpenChanged;                // the machine block follows (arrow direction)
    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    struct Column {
        const char* group;
        int firstSlot, count;          // contiguous run in spec::kMachines
        juce::Rectangle<int> bounds, header, blurb;
    };
    void layout();
    int slotAt(juce::Point<int>) const;
    int columnOf(int slot) const;
    void pick(int slot);
    void drawColumn(juce::Graphics&, int column) const;
    void timerCallback() override;
    void applyAnimation();

    std::unique_ptr<juce::ParameterAttachment> m_attach;
    int m_current = 0;                 // slot the parameter holds
    int m_cursor = -1;                 // hovered / keyboard-selected slot
    std::vector<Column> m_columns;
    int m_layoutColumns = 0;   // how many columns the width is divided by (all groups, also in FX-only mode)
    std::vector<juce::Rectangle<int>> m_rows;   // per slot
    juce::Rectangle<int> m_target;     // bounds when fully open; the layout is always computed for these
    bool m_wantOpen = false;
    float m_anim = 0.0f;               // 0 = rolled up, 1 = fully open
};

} // namespace mnm::plugin::one
