// Parameter value helpers and plugin-wide strings shared by every Monomodule plugin and the Library app.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "host/Machines.h"

namespace mnm::plugin {

// UI strings: ASCII only. juce::String(const char*) decodes literals as Latin-1, so UTF-8
// em-dashes/ellipses render as mojibake.
constexpr const char* kPluginTagline = "A chip-level emulation of the Elektron Monomachine";
constexpr const char* kCredits = "Monomodule by Shnolk. Free and open-source software under the GNU AGPL v3.";
constexpr const char* kDisclaimer = "Not affiliated with or endorsed by Elektron. Monomachine is a trademark of "
                                    "Elektron Music Machines MAQ AB, named here only to say what this software emulates.";
#ifdef JucePlugin_VersionString
constexpr const char* kPluginVersion = "v" JucePlugin_VersionString;
#else
constexpr const char* kPluginVersion = "v0.0.0";
#endif
constexpr const char* kOsDownloadUrl = "https://www.elektron.se/wp-content/uploads/2024/09/Elektron_SFX6-60_OS1.32B.zip";

// Hardware-style value display for the FX plugin's int parameters (storage and CC stay raw 0..127).
inline juce::AudioParameterIntAttributes hwDisplay(bool bipolar)
{
    return juce::AudioParameterIntAttributes().withStringFromValueFunction(
        [bipolar](int v, int) -> juce::String {
            if (bipolar) { const int b = v - 64; return b > 0 ? "+" + juce::String(b) : juce::String(b); }
            return juce::String(v);
        });
}

} // namespace mnm::plugin
