// Two-colour scheme for the stock JUCE widgets the One editor still uses (menus, labels, overlays, file
// chooser), in the skin's ink and paper (Skin.h). The LCD elements paint themselves from the same two colours.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Skin.h"

namespace mnm::plugin::one {

class OneLookAndFeel : public juce::LookAndFeel_V4 {
public:
    OneLookAndFeel() { applySkin(); }

    // Re-reads the skin's colours into every widget colour (call after skin::apply, then repaint the tree).
    void applySkin()
    {
        const juce::Colour ink = skin::inkColour(), paper = skin::paperColour();
        setColourScheme({paper,   // windowBackground
                         paper,   // widgetBackground
                         paper,   // menuBackground
                         ink,     // outline
                         ink,     // defaultText
                         paper,   // defaultFill
                         paper,   // highlightedText
                         ink,     // highlightedFill
                         ink});   // menuText
        setColour(juce::Label::textColourId, ink);
        setColour(juce::TextButton::buttonColourId, paper);
        setColour(juce::TextButton::buttonOnColourId, ink);
        setColour(juce::TextButton::textColourOffId, ink);
        setColour(juce::TextButton::textColourOnId, paper);
        setColour(juce::HyperlinkButton::textColourId, ink);
        setColour(juce::PopupMenu::backgroundColourId, paper);
        setColour(juce::PopupMenu::textColourId, ink);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, ink);
        setColour(juce::PopupMenu::highlightedTextColourId, paper);
        setColour(juce::PopupMenu::headerTextColourId, ink);
        setColour(juce::Slider::thumbColourId, ink);
        setColour(juce::Slider::trackColourId, ink);
        setColour(juce::Slider::backgroundColourId, ink.interpolatedWith(paper, 0.85f));
        setColour(juce::Slider::textBoxTextColourId, ink);
        setColour(juce::Slider::textBoxOutlineColourId, ink);
        setColour(juce::TextEditor::backgroundColourId, paper);
        setColour(juce::TextEditor::textColourId, ink);
        setColour(juce::TextEditor::outlineColourId, ink);
        setColour(juce::TextEditor::focusedOutlineColourId, ink);
        setColour(juce::TextEditor::highlightColourId, ink);
        setColour(juce::TextEditor::highlightedTextColourId, paper);
        setColour(juce::CaretComponent::caretColourId, ink);
        setColour(juce::ComboBox::backgroundColourId, paper);
        setColour(juce::ComboBox::textColourId, ink);
        setColour(juce::ComboBox::outlineColourId, ink);
        setColour(juce::ComboBox::arrowColourId, ink);
        setColour(juce::ScrollBar::thumbColourId, ink.withAlpha(0.5f));
        setColour(juce::TooltipWindow::backgroundColourId, paper);
        setColour(juce::TooltipWindow::textColourId, ink);
        setColour(juce::TooltipWindow::outlineColourId, ink);
        setColour(juce::AlertWindow::backgroundColourId, paper);
        setColour(juce::AlertWindow::textColourId, ink);
        setColour(juce::AlertWindow::outlineColourId, ink);
    }
};

} // namespace mnm::plugin::one
