// Monomodule Library: standalone app entry point.
#include <juce_gui_extra/juce_gui_extra.h>
#include "LibraryComponent.h"

namespace mnm::app {

class MainWindow : public juce::DocumentWindow {
public:
    MainWindow() : juce::DocumentWindow("Monomodule Library", mnm::plugin::skin::paperColour(), juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new LibraryComponent(std::make_unique<mnm::library::Store>()), true);
        setResizable(false, false);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class LibraryApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Monomodule Library"; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void initialise(const juce::String&) override { m_window = std::make_unique<MainWindow>(); }
    void shutdown() override { m_window.reset(); }
    void systemRequestedQuit() override { quit(); }
private:
    std::unique_ptr<MainWindow> m_window;
};

} // namespace mnm::app

START_JUCE_APPLICATION(mnm::app::LibraryApplication)
