#include "MonoVoice.h"
#include <algorithm>
#include <cmath>

namespace mnm {

MonoVoice::MonoVoice(const fw::Firmware& fw) : m_engine(std::make_unique<dsp::DspEngine>(fw))
{
    m_engine->setDigibank(dsp::Digibank::standIn(fw));   // DDRW/DENS waveforms (the unit's +Drive bank is not in the OS file)
}

void MonoVoice::renderBlock()
{
    const auto& b = m_host.nextBlock();
    if (!m_engine->renderBlock(b.w.data(), m_fifo)) m_fifo.fill(0);
    m_pos = 0; m_avail = dsp::DspEngine::kBlockFrames;
}

void MonoVoice::process(float* left, float* right, int n)
{
    constexpr float scale = 1.0f / 8388608.0f;
    for (int i = 0; i < n; ++i) {
        if (m_avail == 0) renderBlock();
        left[i]  = float(m_fifo[2 * m_pos]) * scale;
        right[i] = float(m_fifo[2 * m_pos + 1]) * scale;
        ++m_pos; --m_avail;
    }
}

void MonoVoice::processFx(const float* inL, const float* inR, float* outL, float* outR, int n)
{
    constexpr float scale = 1.0f / 8388608.0f;
    auto toDsp = [](float x) { const float c = std::max(-1.0f, std::min(x, 0.99999988f)); return int32_t(std::lrint(c * 8388608.0f)); };
    for (int i = 0; i < n; ++i) {
        m_in[2 * m_inPos] = toDsp(inL[i]); m_in[2 * m_inPos + 1] = toDsp(inR[i]);
        ++m_inPos;
        if (m_inPos == dsp::DspEngine::kBlockFrames) {   // full input block → render (one block of latency)
            m_engine->setInputFrames(m_in.data());
            renderBlock();
            m_inPos = 0;
        }
        if (m_avail == 0) { outL[i] = 0.f; outR[i] = 0.f; continue; }   // first 16 frames before the first block
        outL[i] = float(m_fifo[2 * m_pos]) * scale;
        outR[i] = float(m_fifo[2 * m_pos + 1]) * scale;
        ++m_pos; --m_avail;
    }
}

void MonoVoice::reset()
{
    m_engine->reset(true);
    m_host = host::HostModel();
    m_fifo.fill(0); m_in.fill(0);
    m_inPos = 0; m_pos = 0; m_avail = 0;
}

void MonoVoice::warmUp(int blocks)
{
    m_host.settle();   // start from settled words (the hardware glides in over ~0.2 s after a kit load)
    for (int k = 0; k < blocks; ++k) renderBlock();
    m_avail = 0; m_pos = 0;
}

} // namespace mnm
