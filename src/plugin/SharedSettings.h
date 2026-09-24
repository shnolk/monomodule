#pragma once
#include <cstdlib>
#include <juce_core/juce_core.h>

namespace mnm::plugin {

// One OS-file setting shared by all Monomachine plugins on this machine.
// Note: on macOS userApplicationDataDirectory is ~/Library, so Application Support is appended
// explicitly to land in the conventional location.
// MNM_SETTINGS_DIR overrides the folder (tests and UI snapshots of the unconfigured state), like MNM_LIBRARY_DIR.
inline juce::File sharedSettingsFile()
{
    if (const char* env = std::getenv("MNM_SETTINGS_DIR"); env && *env) return juce::File(juce::String(env)).getChildFile("settings.xml");
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    base = base.getChildFile("Application Support");
#endif
    return base.getChildFile("Shnolk").getChildFile("Monomachine").getChildFile("settings.xml");
}

// One attribute of the shared settings file (the other attributes are kept).
inline juce::String loadSharedSetting(const juce::String& name, const juce::String& fallback = {})
{
    if (auto xml = juce::parseXML(sharedSettingsFile()))
        if (xml->hasTagName("MonomachineSettings")) return xml->getStringAttribute(name, fallback);
    return fallback;
}

inline void saveSharedSetting(const juce::String& name, const juce::String& value)
{
    auto f = sharedSettingsFile();
    f.getParentDirectory().createDirectory();
    auto xml = juce::parseXML(f);
    if (!xml || !xml->hasTagName("MonomachineSettings")) xml = std::make_unique<juce::XmlElement>("MonomachineSettings");
    xml->setAttribute(name, value);
    xml->writeTo(f);
}

inline juce::String loadSharedOsPath() { return loadSharedSetting("osPath"); }
inline void saveSharedOsPath(const juce::String& path) { saveSharedSetting("osPath", path); }

} // namespace mnm::plugin
