#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace mnm::util {
// 24-bit PCM stereo WAV from 24-bit integer samples (exact) — L/R interleaved.
bool writeWav24(const std::string& path, const std::vector<int32_t>& interleaved24, int channels, int sampleRate);
}
