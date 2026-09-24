// Plugin entry point for Monomodule FX: the same processor as One, as an audio effect (the FX machines only,
// fed from the plugin's main input).
#include "OneProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new mnm::plugin::one::MnmOneProcessor(mnm::plugin::one::Variant::Fx); }
