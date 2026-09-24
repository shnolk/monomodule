// The CUSTOM skin dialog: two hex fields (ink, paper) with a live swatch, drawn like the library's dialogs
// (LCD faces, a framed box over a dimmed page). Typing previews the colours at once; APPLY keeps them (and
// saves them to the shared settings), CANCEL / Escape restores the skin the dialog opened with.
// Shared by the plugin editor and the Library app.
#pragma once
#include <functional>
#include "Lcd.h"

namespace mnm::plugin::one {

class SkinDialog : public juce::Component {
public:
    static constexpr int kS = 2, kLcdW = 240, kLcdH = 96;
    SkinDialog() { setWantsKeyboardFocus(true); }

    // Opens on the saved custom colours (or the colours in use). onChanged fires after every preview / apply /
    // cancel so the owner can re-skin its widgets and repaint; onDone when the dialog closes.
    void open();
    std::function<void()> onChanged, onDone;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    void preview();
    void finish(bool keep);
    juce::Rectangle<int> box() const { return juce::Rectangle<int>(0, 0, kLcdW * kS, kLcdH * kS).withCentre(getLocalBounds().getCentre()); }
    skin::Skin m_before;
    juce::String m_text[2];   // ink, paper
    int m_field = 0;
    juce::String m_error;
    juce::Rectangle<int> m_fields[2], m_cancel, m_apply;   // LCD px inside the box
};

} // namespace mnm::plugin::one
