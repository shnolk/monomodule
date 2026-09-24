// Runs the DSP program from the user's Monomachine OS file (kernel A + synth payload) inside the dsp56300
// emulator. The hardware's DMA/ESSI-driven main loop is replaced by a small "harness" stub that exchanges
// the 52-word track parameter block (in) and the 32-word L/R output block (out) over the host port.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include "firmware/Firmware.h"
#include "Digibank.h"

namespace dsp56k { class DSP; class Memory; class Peripherals56303; class PeripheralsNop; class DefaultMemoryValidator; }

namespace mnm::dsp {

struct RenderStats {
    uint64_t instructions = 0;   // for the last block
    uint64_t totalInstructions = 0;
    uint32_t blocks = 0;
};

class DspEngine {
public:
    static constexpr int kBlockFrames = 16;
    static constexpr int kBlockWords  = 52;
    static constexpr uint32_t kStubInit = 0x0C00, kStubMain = 0x0C20, kStubOut = 0x0C40, kPatchAddr = 0x0143;
    static constexpr uint32_t kInitSentinel = 0x00C0DE;

    explicit DspEngine(const fw::Firmware& fw);
    ~DspEngine();

    // Load records, install stub, run the init sequence. Throws on failure. clearMemory first zeroes every
    // word of X/Y/P RAM so nothing of an earlier run (delay lines, filter states) survives: a re-run then
    // renders the same samples as a fresh engine (offline previews rely on it).
    void reset(bool clearMemory = false);

    // The DigiPRO Digibank for DDRW/DENS (Y:$150000..): written on every reset and immediately when set.
    void setDigibank(std::shared_ptr<const Digibank> bank);
    const Digibank* digibank() const { return m_bank.get(); }

    // External audio input for FX machines: 16 stereo frames (L/R interleaved, 24-bit) placed in the ADC
    // ring half the kernel reads (X:$100..$11F). Call before renderBlock().
    void setInputFrames(const int32_t* lr32);

    // Send one 52-word block and render 16 stereo frames. out = L0 R0 L1 R1 ... (24-bit signed).
    bool renderBlock(const uint32_t* block52, std::array<int32_t, 32>& outLR);

    void setUseJit(bool b) { m_useJit = b; }
    bool usingJit() const { return m_useJit; }
    bool faulted() const { return m_faulted; }
    const std::string& faultReason() const { return m_fault; }
    const RenderStats& stats() const { return m_stats; }

    // Debug access (engine idle between blocks).
    uint32_t peek(fw::Space s, uint32_t addr) const;
    void poke(fw::Space s, uint32_t addr, uint32_t v);
    dsp56k::DSP& dsp() { return *m_dsp; }
    dsp56k::Peripherals56303& periph() { return *m_periphX; }
    // Debug: JIT block size limit (1 = single-step the JIT). Call before reset().
    void setJitMaxInstructionsPerBlock(uint32_t n);
    void sendBlock(const uint32_t* block52);   // low-level: push a block without running

private:
    void loadImage(const fw::DspImage& img);
    void installStub();
    void uploadDigibank();
    bool runUntilTx(size_t words, uint64_t maxInstr);
    void fault(const std::string& why);

    const fw::Firmware& m_fw;
    std::unique_ptr<dsp56k::DefaultMemoryValidator> m_validator;
    std::unique_ptr<dsp56k::Memory> m_mem;
    std::unique_ptr<dsp56k::Peripherals56303> m_periphX;
    std::unique_ptr<dsp56k::PeripheralsNop> m_periphY;
    std::unique_ptr<dsp56k::DSP> m_dsp;
    std::shared_ptr<const Digibank> m_bank;
    bool m_useJit = true;
    bool m_faulted = false;
    std::string m_fault;
    RenderStats m_stats;
};

} // namespace mnm::dsp
