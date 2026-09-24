#include "Overlays.h"
#include "Skin.h"
#include "ParamDisplay.h"
#include "ShnolkLogo.h"

namespace mnm::plugin {

static juce::Colour panelColour(const juce::Component& c)
{
    return c.getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId).brighter(0.08f);
}

// ---------------------------------------------------------------------------
// MissingOsOverlay

MissingOsOverlay::MissingOsOverlay(std::function<void()> onSelect)
    : m_link("elektron.se  -  Elektron_SFX6-60_OS1.32B.zip", juce::URL(kOsDownloadUrl))
{
    m_title.setText("MONOMACHINE OS REQUIRED", juce::dontSendNotification);
    m_title.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
    m_title.setJustificationType(juce::Justification::centred);
    m_body.setText(
        "This plugin is an emulator: software that behaves exactly like a real\n"
        "Elektron Monomachine. To make sound, it needs a copy of the Monomachine's\n"
        "operating system (its \"OS\" file). That file belongs to Elektron, so it is not\n"
        "included here - but it is a free download from Elektron's own website.\n"
        "\n"
        "1.  Click the link below. It downloads a file called Elektron_SFX6-60_OS1.32B.zip\n"
        "2.  Double-click that zip to unpack it\n"
        "3.  Inside is the OS file itself: Elektron_SFX6-60_OS1.32B.syx\n"
        "4.  Click Select OS File below and choose that .syx file\n"
        "\n"
        "You only need to do this once: the Monomodule plugins and app share this setting.",
        juce::dontSendNotification);
    m_body.setJustificationType(juce::Justification::topLeft);
    m_body.setFont(juce::Font(juce::FontOptions(14.0f)));
    m_link.setFont(juce::Font(juce::FontOptions(14.0f)), false, juce::Justification::centred);
    m_error.setJustificationType(juce::Justification::centred);
    m_error.setFont(juce::Font(juce::FontOptions(12.0f)));
    lookAndFeelChanged();
    m_select.onClick = std::move(onSelect);
    addAndMakeVisible(m_title); addAndMakeVisible(m_body); addAndMakeVisible(m_link);
    addAndMakeVisible(m_select); addAndMakeVisible(m_error);
}

void MissingOsOverlay::setStatusMessage(const juce::String& s)
{
    if (m_error.getText() != s) m_error.setText(s, juce::dontSendNotification);
}

void MissingOsOverlay::lookAndFeelChanged() { m_error.setColour(juce::Label::textColourId, skin::inkColour().interpolatedWith(juce::Colours::red, 0.6f)); }

void MissingOsOverlay::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    auto panel = getLocalBounds().withSizeKeepingCentre(620, 466).toFloat();
    g.setColour(panelColour(*this));
    g.fillRoundedRectangle(panel, 8.0f);
    g.setColour(skin::paperColour().withAlpha(0.15f));
    g.drawRoundedRectangle(panel, 8.0f, 1.0f);
    drawShnolkLogo(g, m_logoBounds, getLookAndFeel().findColour(juce::Label::textColourId));
}

void MissingOsOverlay::resized()
{
    auto r = getLocalBounds().withSizeKeepingCentre(620, 466).reduced(30, 22);
    m_logoBounds = r.removeFromTop(52).toFloat().withSizeKeepingCentre(52, 52);
    r.removeFromTop(8);
    m_title.setBounds(r.removeFromTop(30));
    r.removeFromTop(10);
    m_body.setBounds(r.removeFromTop(198));
    r.removeFromTop(6);
    m_link.setBounds(r.removeFromTop(24));
    r.removeFromTop(12);
    m_select.setBounds(r.removeFromTop(30).withSizeKeepingCentre(180, 30));
    r.removeFromTop(8);
    m_error.setBounds(r.removeFromTop(20));
}

// ---------------------------------------------------------------------------
// AboutOverlay

AboutOverlay::AboutOverlay(const juce::String& pluginTitle)
    : m_insta("DM @shnolk on Instagram", juce::URL("https://www.instagram.com/shnolk")),
      m_mail("shnolk@halftone.world", juce::URL("mailto:shnolk@halftone.world"))
{
    m_title.setText(pluginTitle, juce::dontSendNotification);
    m_title.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
    m_title.setJustificationType(juce::Justification::centred);
    m_version.setText(kPluginVersion, juce::dontSendNotification);
    m_version.setJustificationType(juce::Justification::centred);
    lookAndFeelChanged();
    m_body.setText(
        juce::String(kPluginTagline) + ". It runs the sound engine from the "
        "Monomachine OS file you supply, so what you hear is what the hardware sounds like.\n\n"
        + kCredits + "\n\n" + kDisclaimer,
        juce::dontSendNotification);
    m_body.setJustificationType(juce::Justification::centredTop);
    m_body.setFont(juce::Font(juce::FontOptions(14.0f)));
    m_contactLabel.setText("Feedback and bug reports:", juce::dontSendNotification);
    m_contactLabel.setJustificationType(juce::Justification::centred);
    m_insta.setFont(juce::Font(juce::FontOptions(14.0f)), false, juce::Justification::centred);
    m_mail.setFont(juce::Font(juce::FontOptions(14.0f)), false, juce::Justification::centred);
    m_close.onClick = [this] { setVisible(false); };
    addAndMakeVisible(m_title); addAndMakeVisible(m_version); addAndMakeVisible(m_body);
    addAndMakeVisible(m_contactLabel); addAndMakeVisible(m_insta); addAndMakeVisible(m_mail);
    addAndMakeVisible(m_close);
}

void AboutOverlay::lookAndFeelChanged() { m_version.setColour(juce::Label::textColourId, skin::inkColour().withAlpha(0.6f)); }

void AboutOverlay::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId).withAlpha(0.85f));
    auto panel = getLocalBounds().withSizeKeepingCentre(480, 452).toFloat();
    g.setColour(panelColour(*this));
    g.fillRoundedRectangle(panel, 8.0f);
    g.setColour(skin::paperColour().withAlpha(0.15f));
    g.drawRoundedRectangle(panel, 8.0f, 1.0f);
    drawShnolkLogo(g, m_logoBounds, getLookAndFeel().findColour(juce::Label::textColourId));
}

void AboutOverlay::resized()
{
    auto r = getLocalBounds().withSizeKeepingCentre(480, 452).reduced(28, 22);
    m_logoBounds = r.removeFromTop(44).toFloat().withSizeKeepingCentre(44, 44);
    r.removeFromTop(8);
    m_title.setBounds(r.removeFromTop(28));
    m_version.setBounds(r.removeFromTop(20));
    r.removeFromTop(10);
    m_body.setBounds(r.removeFromTop(176));
    r.removeFromTop(8);
    m_contactLabel.setBounds(r.removeFromTop(20));
    m_insta.setBounds(r.removeFromTop(22));
    m_mail.setBounds(r.removeFromTop(22));
    r.removeFromTop(10);
    m_close.setBounds(r.removeFromTop(28).withSizeKeepingCentre(120, 28));
}

} // namespace mnm::plugin
