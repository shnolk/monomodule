// Full-editor overlays shared by the Monomodule plugin editors.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace mnm::plugin {

// Blocking screen shown until a valid Monomachine OS file has been selected.
class MissingOsOverlay : public juce::Component {
public:
    explicit MissingOsOverlay(std::function<void()> onSelect);
    void paint(juce::Graphics&) override;
    void lookAndFeelChanged() override;   // the skin changed: re-tint the labels that carry their own colour
    void resized() override;
    void setStatusMessage(const juce::String& s);
private:
    juce::Label m_title, m_body, m_error;
    juce::HyperlinkButton m_link;
    juce::TextButton m_select{"Select OS File..."};
    juce::Rectangle<float> m_logoBounds;
};

// Plugin info screen (version + contact), opened from the config menu.
class AboutOverlay : public juce::Component {
public:
    explicit AboutOverlay(const juce::String& pluginTitle);
    void paint(juce::Graphics&) override;
    void lookAndFeelChanged() override;
    void resized() override;
private:
    juce::Label m_title, m_version, m_body, m_contactLabel;
    juce::HyperlinkButton m_insta, m_mail;
    juce::TextButton m_close{"Close"};
    juce::Rectangle<float> m_logoBounds;
};

} // namespace mnm::plugin
