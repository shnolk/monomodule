// Plugin entry point for Monomodule One (kept out of OneProcessor.cpp so the dev tools can link it).
#include "OneProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new mnm::plugin::one::MnmOneProcessor(); }
