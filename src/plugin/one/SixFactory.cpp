// Plugin entry point for Monomodule Six: the same processor as One, running six tracks (MIDI channels
// 1..6, one stereo output bus per track).
#include "OneProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new mnm::plugin::one::MnmOneProcessor(6); }
