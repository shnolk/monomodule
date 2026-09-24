// Monomodule One / Six / FX processor. One emulated track per hardware track (each with its own DSP engine,
// all sharing one loaded OS), behind a UI built from the hardware's own LCD glyphs and fonts.
//   One (numTracks = 1): one track, every machine; synths play MIDI notes on any channel, FX machines
//       process the side-chain input.
//   Six (numTracks = 6): multitimbral, MIDI channels 1..6 address tracks 1..6 (the hardware's individual
//       track channels), one stereo output bus per track. FX machines take the previous track's output
//       (NEIBOR) or the side-chain input (INP AB), as KIT > ROUTING offers on the hardware.
//   FX (Variant::Fx): One as an audio effect. One track that offers only the FX machines and processes the
//       plugin's main input; no MIDI. Everything else (pages, LFOs, library, state) is One's.
// Every knob is stored raw 0..127 exactly as the hardware kit byte; only the display differs.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "MonoVoice.h"
#include "OneParams.h"
#include "library/MnmDump.h"
#include "PreviewPlayer.h"

namespace mnm::plugin::one {

class MnmOneProcessor : public juce::AudioProcessor,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::AsyncUpdater {
public:
    explicit MnmOneProcessor(Variant variant);
    explicit MnmOneProcessor(int numTracks = 1) : MnmOneProcessor(numTracks > 1 ? Variant::Six : Variant::One) { jassert(numTracks == 1 || numTracks == 6); }
    ~MnmOneProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return m_variant == Variant::Fx ? "Monomodule FX" : m_numTracks > 1 ? "Monomodule Six" : "Monomodule One"; }
    bool acceptsMidi() const override { return m_variant != Variant::Fx; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    int numTracks() const { return m_numTracks; }
    Variant variant() const { return m_variant; }
    bool isEffect() const { return m_variant == Variant::Fx; }
    float trackPeak(int t) const { return m_tracks[size_t(t)]->peak.load(); }   // linear, pre-mute, decays between UI polls
    float outputPeak() const { return trackPeak(0); }
    float hostBpm() const { return m_hostBpm.load(); }   // last tempo reported by the host transport (120 without one)
    host::Machine currentMachine(int t = 0) const { return m_tracks[size_t(t)]->currentMachine; }

    // Machine-change side effects (message thread): each machine's SYN values live in plugin state, so a
    // machine change saves the outgoing machine's eight values and restores the incoming machine's (or its
    // hardware defaults on first use); switching between a synth and an FX machine loads the AMP page defaults
    // the assign routine loads (FX: envelope held open); hosts are told the parameter names changed.
    // Runs from an async update after any machine parameter change; dev tools without a message loop call it.
    void syncMachineSideEffects();

    // Library transfer (message thread): applies a kit track (machine, SYN/AMP/FILT/EFX/LFO pages, level,
    // key tracking, and on Six the OUT BUS / FX INPUT routing) to track t, or a whole kit to a Six. The
    // ASSIGN matrix and the KIT > TRIG settings are not applied (not modelled yet).
    // soundOnly = a preset load: the track keeps its level and routing (they belong to the kit).
    bool applyKitTrack(int t, const mnm::dump::Kit& kit, int kitTrack, juce::String& error, bool soundOnly = false);
    bool applyKit(const mnm::dump::Kit& kit, juce::String& error);   // locked tracks keep their sound

    // What the library knows about the plugin's sounds (message thread, kept in the plugin state): the preset each
    // track was loaded from and the kit (Six), so the editor can name them, step through the library from them and
    // offer "save as a new version of ...". "Modified" = the sound no longer matches what was loaded. The bytes the
    // plugin does not model (ASSIGN matrix, MIDI page, KIT > TRIG) are carried along, so a sound that goes through
    // the plugin and back into a project loses nothing.
    struct LoadedRef { juce::String id, name; bool valid() const { return id.isNotEmpty() || name.isNotEmpty(); } };
    bool loadPreset(int t, const mnm::dump::Kit& kit, int kitTrack, const juce::String& id, const juce::String& name, juce::String& error);
    bool loadKit(const mnm::dump::Kit& kit, const juce::String& id, const juce::String& name, juce::String& error);
    LoadedRef loadedPreset(int t) const { return m_tracks[size_t(t)]->loaded; }
    LoadedRef loadedKit() const { return m_loadedKit; }
    bool presetModified(int t) const;
    bool kitModified() const;
    void markPresetSaved(int t, const juce::String& id, const juce::String& name);   // after a save: the current sound is that item
    void markKitSaved(const juce::String& id, const juce::String& name);
    bool trackLocked(int t) const { return m_tracks[size_t(t)]->locked; }
    void setTrackLocked(int t, bool locked) { m_tracks[size_t(t)]->locked = locked; }
    mnm::dump::Kit currentKit() const;   // the plugin's sound as a kit (One: track 1, the rest GND... see .cpp)

    // Library previews, mixed into the main output (a plugin has no device of its own). The renderer and its
    // emulated DSPs exist from the first preview on.
    void previewPlay(const juce::String& key, const std::function<mnm::preview::PreviewSpec()>& build);
    void previewStop();
    juce::String previewKey() const { return m_previewKey; }
    void previewSetLoop(bool on) { m_previewVoice.setLoop(on && m_previewKey.isNotEmpty()); }   // off again on stop or another preview
    bool previewLoop() const { return m_previewKey.isNotEmpty() && m_previewVoice.loop(); }
    juce::String previewStatus() const { return m_previewRenderer ? m_previewRenderer->status() : juce::String(); }
    bool previewPoll();   // editor timer: true when the playing state changed

    // Firmware management (message thread), same contract as the other plugins.
    juce::String firmwarePath() const { return m_firmwarePath; }
    void setFirmwarePath(const juce::String& path, bool persist = true);
    void refreshSharedOsPath();
    void clearFirmware();
    void initPreset();
    juce::String statusText() const;
    bool engineReady() const { return m_engineReady.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    struct Track {
        int index = 0;
        std::unique_ptr<MonoVoice> voice;
        std::vector<int> heldNotes;   // note stack (last-note priority)
        host::Machine currentMachine = host::Machine::SAW;   // what the engine plays
        std::atomic<float>* machine = nullptr;
        std::atomic<float>* syn[8] = {};
        std::atomic<float>* pages[4][8] = {};   // [SYN] unused
        std::atomic<float>* lfo[spec::kNumLfos][8] = {};
        std::atomic<float>* level = nullptr;
        std::atomic<float>* mute = nullptr;
        std::atomic<float>* input = nullptr;    // host::FxInput (Six only)
        std::atomic<float>* outBus[3] = {};     // OUT BUS AB/CD/EF (Six only)
        std::atomic<float>* lpKeyTrack = nullptr;
        std::atomic<float>* hpKeyTrack = nullptr;
        int lastFrames = 0;                     // engine frames rendered in the last block (engL/R)
        juce::LagrangeInterpolator interpL, interpR, interpInL, interpInR;
        std::vector<float> engL, engR;     // 44.1 kHz render (also the NEIBOR feed for the next track)
        std::vector<float> hostL, hostR;   // resampled to host rate
        std::vector<float> inL, inR;       // FX machine input at 44.1 kHz
        std::atomic<float> peak{0.0f};
        // Per-machine SYN memory (state, not parameters): the eight values of every machine this track
        // has visited, and the machine slot whose values the SYN parameters currently hold.
        std::array<std::array<uint8_t, 8>, spec::kNumMachines> shadow{};
        std::array<bool, spec::kNumMachines> visited{};
        int shadowSlot = 0;
        // library state (message thread)
        LoadedRef loaded;
        std::vector<uint8_t> loadedPrint;   // soundPrint() at load time
        mnm::dump::KitTrack carry;          // the loaded track: the source of the bytes that are not parameters
        bool locked = false;
    };
    std::vector<uint8_t> soundPrint(int t, bool withKitBytes) const;
    mnm::dump::KitTrack currentKitTrack(int t) const;
    juce::ValueTree libraryToTree() const;
    void libraryFromTree(const juce::ValueTree&);

    static BusesProperties makeBuses(Variant variant);
    void loadEngine();
    void applyParametersToHost(Track&);
    void handleMidi(Track&, const juce::MidiMessage&);
    void pushParamFromCC(Track&, host::Page page, int k, int value);
    void renderTrack(Track&, int nEngine, double ratio, const juce::MidiBuffer&);
    void fillFxInput(Track&, int nEngine, double ratio);
    void writeOutputs(juce::AudioBuffer<float>&, int nEngine, double ratio);
    int outBusMask(const Track& tr) const;
    host::FxInput fxInputOf(const Track& tr) const;
    void parameterChanged(const juce::String& id, float newValue) override;
    void handleAsyncUpdate() override { syncMachineSideEffects(); }
    int machineSlotOf(const Track& tr) const { return juce::jlimit(0, spec::kNumMachines - 1, int(tr.machine->load())); }
    void migrateLegacyState(juce::ValueTree& state) const;
    juce::ValueTree shadowsToTree() const;
    void shadowsFromTree(const juce::ValueTree& shadows);

    const Variant m_variant;
    const int m_numTracks;
    juce::String m_firmwarePath;
    std::unique_ptr<fw::Firmware> m_firmware;   // shared by all track voices
    std::vector<std::unique_ptr<Track>> m_tracks;
    juce::CriticalSection m_engineLock;
    std::atomic<bool> m_engineReady{false};
    juce::String m_status;
    double m_hostRate = 44100.0;
    bool m_needsResample = false;
    std::atomic<float>* m_bpm = nullptr;
    std::atomic<float>* m_bpmSync = nullptr;
    std::atomic<float>* m_outputMode = nullptr;   // Six only
    std::array<std::vector<float>, 3> m_busL, m_busR;   // mix buses AB/CD/EF at 44.1 kHz, rebuilt every block in track order
    std::vector<float> m_outL, m_outR;                  // resampling scratch for the bus outputs
    std::atomic<float> m_hostBpm{120.0f};
    bool m_wasSynced = true;
    std::vector<float> m_sideL, m_sideR;   // side-chain input captured before the buffer is cleared
    int m_sideChannels = 0;
    LoadedRef m_loadedKit;
    std::vector<uint8_t> m_loadedKitPrint;
    mnm::dump::Kit m_kitCarry;
    std::unique_ptr<mnm::library::PreviewRenderer> m_previewRenderer;
    mnm::library::PreviewVoice m_previewVoice;
    std::shared_ptr<mnm::library::PreviewAudio> m_previewAudio;
    juce::String m_previewKey;
};

} // namespace mnm::plugin::one
