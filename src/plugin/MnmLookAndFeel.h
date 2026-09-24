#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace mnm::plugin {

// Palette modelled on the Monomachine SFX-60: dark charcoal steel panel, silver legends,
// and the red glow of the MKII LCD as the accent colour.
namespace theme {
inline const juce::Colour panel{0xff26282a};      // charcoal face plate
inline const juce::Colour well{0xff1a1c1e};       // recessed widget wells / LCD surround
inline const juce::Colour outline{0xff45494d};    // brushed-steel edges
inline const juce::Colour text{0xffd9dbdd};       // silver legends
inline const juce::Colour dimText{0xff8e9296};    // secondary legends
inline const juce::Colour accent{0xffe84a2f};     // red LCD glow
inline const juce::Colour knobTrack{0xff3a3d40};  // unlit part of the knob arc
}

class MnmLookAndFeel : public juce::LookAndFeel_V4 {
public:
    MnmLookAndFeel()
        : juce::LookAndFeel_V4({theme::panel,      // windowBackground
                                theme::well,       // widgetBackground
                                theme::well,       // menuBackground
                                theme::outline,    // outline
                                theme::text,       // defaultText
                                theme::well,       // defaultFill
                                theme::text,       // highlightedText
                                theme::accent,     // highlightedFill
                                theme::text})      // menuText
    {
        setColour(juce::Slider::thumbColourId, theme::accent);
        setColour(juce::Slider::rotarySliderFillColourId, theme::accent);
        setColour(juce::Slider::rotarySliderOutlineColourId, theme::knobTrack);
        setColour(juce::Slider::trackColourId, theme::accent);
        setColour(juce::Slider::backgroundColourId, theme::knobTrack);
        setColour(juce::Slider::textBoxTextColourId, theme::accent);   // value readouts glow like the LCD
        setColour(juce::Slider::textBoxOutlineColourId, theme::knobTrack);
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff33363a));
        setColour(juce::ComboBox::backgroundColourId, theme::well);
        setColour(juce::HyperlinkButton::textColourId, theme::accent);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, theme::accent);
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    }
};

} // namespace mnm::plugin
