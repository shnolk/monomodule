// Monomodule One / Six / FX: parameter layout. One processor serves all three plugins; One is the
// single-track build, Six runs six tracks, FX is One as an audio effect: one track that offers only the FX
// machines and processes the plugin's main input (Variant). The parameter model is the hardware's: per track a machine choice, the
// eight SYN knobs A-H (their meaning follows the machine, as CC 48-55 do), the AMP/FILT/EFX pages,
// the three LFO pages, level and mute. Every knob is an int parameter stored raw 0..127 (the hardware
// kit byte); the UI spec decides how it is named and displayed. Each machine's last SYN values are kept
// in plugin state (not as parameters), so switching machines restores them, as One always did.
// IDs: t<n><name>, n = 1..6. bpm/bpmsync are global.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cmath>
#include "host/Machines.h"
#include "Lcd.h"

namespace mnm::plugin::one { using host::FxInput; using host::kFxInputNames; }

namespace mnm::plugin::one {

constexpr const char* kOneTitle = "MONOMODULE ONE";
constexpr const char* kSixTitle = "MONOMODULE SIX";
constexpr const char* kFxTitle = "MONOMODULE FX";

enum class Variant { One, Six, Fx };
inline int numTracksOf(Variant v) { return v == Variant::Six ? 6 : 1; }
constexpr int kMaxTracks = 6;

// Routing (Six; KIT > EDIT on the hardware): per track the OUT BUS set (AB/CD/EF) and, for FX machines, the
// input source (host::FxInput: NEIBOR, INP A/B/AB = the side-chain, BUS AB/CD/EF). `outputs` decides what the
// plugin's six output buses carry: each track's own output (TRACKS), or the three mix buses AB/CD/EF on
// buses 1-3 as the hardware's 3xSTEREO outputs (BUSES).
enum class OutputMode : int { Tracks = 0, Buses = 1 };
constexpr const char* kOutputModeNames[2] = {"TRACKS", "BUSES"};

inline juce::String trackPrefix(int t) { return "t" + juce::String(t + 1); }   // t = 0-based track
inline juce::String machineId(int t = 0) { return trackPrefix(t) + "machine"; }
inline juce::String synId(int t, int k) { return trackPrefix(t) + "syn" + juce::String(k); }
inline juce::String pageId(int t, host::Page page, int k)
{
    static const char* names[4] = {"syn", "amp", "filt", "efx"};
    return trackPrefix(t) + names[int(page)] + juce::String(k);
}
inline juce::String levelId(int t = 0) { return trackPrefix(t) + "level"; }
inline juce::String muteId(int t) { return trackPrefix(t) + "mute"; }
inline juce::String inputId(int t) { return trackPrefix(t) + "input"; }
inline juce::String outBusId(int t, int bus) { static const char* n[3] = {"outab", "outcd", "outef"}; return trackPrefix(t) + n[bus]; }   // bus 0..2 = AB/CD/EF
inline juce::String lpKeyTrackId(int t) { return trackPrefix(t) + "lpf"; }   // KIT > ASSIGN > KEY: LPF / HPF tracking
inline juce::String hpKeyTrackId(int t) { return trackPrefix(t) + "hpf"; }
inline juce::String outputModeId() { return "outputs"; }
inline juce::String lfoId(int t, int lfo, int k) { return trackPrefix(t) + "lfo" + juce::String(lfo + 1) + "p" + juce::String(k); }   // lfo 0..2
inline juce::String bpmId() { return "bpm"; }
inline juce::String bpmSyncId() { return "bpmsync"; }
// Schema <= 3 (single-track One) kept every machine's SYN knobs as parameters: t1m<machine index>s<k>.
inline juce::String legacySynId(int machineIndex, int k) { return "t1m" + juce::String(machineIndex) + "s" + juce::String(k); }

// Position of a machine in the menu (spec::kMachines), -1 if the plugin does not offer it.
inline int machineSlot(host::Machine m)
{
    for (int i = 0; i < spec::kNumMachines; ++i) if (spec::kMachines[i].index == uint8_t(m)) return i;
    return -1;
}
inline host::Machine machineAt(int slot) { return host::Machine(spec::kMachines[juce::jlimit(0, spec::kNumMachines - 1, slot)].index); }
inline int defaultMachineSlot() { return machineSlot(host::Machine::SAW); }   // SWAVE-SAW, as in One
// The FX machines are the last menu group, so their slots are one contiguous range: the FX plugin's machine
// parameter is an int over that range whose raw value is still the slot, like the choice parameter of One/Six.
inline int firstFxSlot() { for (int i = 0; i < spec::kNumMachines; ++i) if (spec::kMachines[i].isFx) return i; return 0; }
inline int lastFxSlot() { int l = firstFxSlot(); for (int i = l; i < spec::kNumMachines && spec::kMachines[i].isFx; ++i) l = i; return l; }
inline int defaultMachineSlot(Variant v) { return v == Variant::Fx ? machineSlot(host::Machine::REVERB) : defaultMachineSlot(); }
// Every machine has an engine path since v0.7.6; `supported` stays for a machine the DSP path cannot play.
inline bool machineSupported(int slot) { return spec::kMachines[juce::jlimit(0, spec::kNumMachines - 1, slot)].supported; }

// The AMP/FILT/EFX knobs as spec::Param records so the pages share the SYN cell renderer.
inline spec::Param sharedPageParam(host::Page page, int k)
{
    const auto& pg = spec::kSharedPages[int(page) - 1];
    return {pg.labels[k], ((pg.bipolarMask >> k) & 1) ? spec::Display::Bipolar : spec::Display::Numeric,
            false, pg.defaults[k], 127, 128, spec::Icons::None, nullptr};
}

inline juce::AudioParameterIntAttributes specDisplay(const spec::Param& p)
{
    return juce::AudioParameterIntAttributes().withStringFromValueFunction([p](int v, int) { return valueText(p, v); });
}

// One SYN knob (A-H) of a track: a raw 0..127 kit byte whose label, value text and default follow the
// track's machine, read live from the machine parameter (setMachineSource). Hosts that cache parameter
// names are told to re-read them when the machine changes (AudioProcessor::updateHostDisplay).
class SynParam : public juce::AudioParameterInt {
public:
    SynParam(int track, int k, int defaultSlot, const juce::String& namePrefix)
        : juce::AudioParameterInt(juce::ParameterID{synId(track, k), 1}, namePrefix + "SYN " + juce::String::charToString(juce::juce_wchar('A' + k)),
                                  0, 127, int(spec::kMachines[defaultSlot].params[k].defaultRaw)),
          m_k(k), m_slot(defaultSlot), m_prefix(namePrefix) {}

    void setMachineSource(std::atomic<float>* slot) { m_source = slot; }
    int knob() const { return m_k; }
    const spec::Param& param() const
    {
        const int s = m_source ? juce::jlimit(0, spec::kNumMachines - 1, int(std::lround(m_source->load()))) : m_slot;
        return spec::kMachines[s].params[m_k];
    }
    juce::String getName(int maximumStringLength) const override
    {
        const auto& p = param();
        const juce::String name = m_prefix + (p.display == spec::Display::Blank ? "SYN " + juce::String::charToString(juce::juce_wchar('A' + m_k)) : juce::String(p.label));
        return name.substring(0, maximumStringLength);
    }
    juce::String getText(float normalised, int) const override { return valueText(param(), int(std::lround(convertFrom0to1(normalised)))); }
    float getDefaultValue() const override { return convertTo0to1(float(param().defaultRaw)); }

private:
    int m_k, m_slot;
    juce::String m_prefix;
    std::atomic<float>* m_source = nullptr;
};

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout(Variant variant)
{
    const int numTracks = numTracksOf(variant);
    const bool effect = variant == Variant::Fx;
    using namespace host;
    using Group = juce::AudioProcessorParameterGroup;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int t = 0; t < numTracks; ++t) {
        const juce::String prefix = numTracks > 1 ? "T" + juce::String(t + 1) + " " : juce::String();
        std::vector<std::unique_ptr<Group>> groups;
        {
            juce::StringArray names;
            for (const auto& m : spec::kMachines) names.add(m.displayName);
            auto g = std::make_unique<Group>(trackPrefix(t) + "track", prefix + "TRACK", " ");
            if (effect)
                g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{machineId(t), 1}, "Machine", firstFxSlot(), lastFxSlot(), defaultMachineSlot(variant),
                    juce::AudioParameterIntAttributes().withStringFromValueFunction([](int v, int) { return juce::String(spec::kMachines[juce::jlimit(0, spec::kNumMachines - 1, v)].displayName); })
                        .withValueFromStringFunction([](const juce::String& s) { for (int i = 0; i < spec::kNumMachines; ++i) if (s == spec::kMachines[i].displayName) return i; return firstFxSlot(); })));
            else
                g->addChild(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{machineId(t), 1}, prefix + "Machine", names, defaultMachineSlot()));
            g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{levelId(t), 1}, prefix + "LEVEL", 0, 127, 100));
            g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{muteId(t), 1}, prefix + "MUTE", false));
            // KIT > ASSIGN > KEY: the filters track the note pitch unless switched off (the kit default is on)
            g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{lpKeyTrackId(t), 1}, prefix + "LPF KEY TRACK", true));
            g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{hpKeyTrackId(t), 1}, prefix + "HPF KEY TRACK", true));
            if (numTracks > 1) {   // KIT > EDIT routing; track 1 has no neighbour, so its FX input starts on the side-chain
                juce::StringArray inputs;
                for (const char* n : kFxInputNames) inputs.add(n);
                g->addChild(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{inputId(t), 1}, prefix + "FX INPUT", inputs,
                    t == 0 ? int(FxInput::InpAB) : int(FxInput::Neighbor)));
                static const char* busNames[3] = {"OUT BUS AB", "OUT BUS CD", "OUT BUS EF"};
                for (int b = 0; b < 3; ++b)
                    g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{outBusId(t, b), 1}, prefix + busNames[b], b == 0));
            }
            groups.push_back(std::move(g));
        }
        {
            auto g = std::make_unique<Group>(trackPrefix(t) + "syn", prefix + "SYNTHESIS", " ");
            for (int k = 0; k < 8; ++k) g->addChild(std::make_unique<SynParam>(t, k, defaultMachineSlot(variant), prefix));
            groups.push_back(std::move(g));
        }
        for (int pi = 1; pi < 4; ++pi) {
            const auto page = Page(pi);
            const auto& pg = spec::kSharedPages[pi - 1];
            auto g = std::make_unique<Group>(trackPrefix(t) + pg.name, prefix + pg.name, " ");
            for (int k = 0; k < 8; ++k) {
                auto p = sharedPageParam(page, k);
                if (effect && page == Page::AMP) p.defaultRaw = spec::kAmpDefaultsFx[k];   // an FX track holds its envelope open
                g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{pageId(t, page, k), 1},
                    prefix + juce::String(pg.name) + " " + p.label, 0, 127, int(p.defaultRaw), specDisplay(p)));
            }
            groups.push_back(std::move(g));
        }
        for (int lfo = 0; lfo < spec::kNumLfos; ++lfo) {   // LFO pages: raw kit bytes, computed host-side (host/Lfo.h)
            const juce::String name = "LFO" + juce::String(lfo + 1);
            auto g = std::make_unique<Group>(trackPrefix(t) + "lfo" + juce::String(lfo + 1), prefix + name, " ");
            for (int k = 0; k < 8; ++k) {
                const auto& p = spec::kLfoParams[k];
                g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{lfoId(t, lfo, k), 1},
                    prefix + name + " " + p.label, 0, int(p.maxRaw), int(p.defaultRaw), specDisplay(p)));
            }
            groups.push_back(std::move(g));
        }
        if (numTracks > 1) {   // Six: one group per track holding the page groups
            auto track = std::make_unique<Group>(trackPrefix(t), "T" + juce::String(t + 1), " ");
            for (auto& g : groups) track->addChild(std::move(g));
            layout.add(std::move(track));
        } else {
            for (auto& g : groups) layout.add(std::move(g));
        }
    }
    if (numTracks > 1)
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{outputModeId(), 1}, "OUTPUTS",
            juce::StringArray{"Per Track", "Mix Buses AB CD EF"}, int(OutputMode::Tracks)));
    // Tempo: the tick word (24 x BPM) follows the host's transport by default; BPM is the free value used
    // when sync is off (drag or type it in the header).
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{bpmSyncId(), 1}, "BPM sync to host", true));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{bpmId(), 1}, "BPM", 30.0f, 300.0f, 120.0f));
    return layout;
}

} // namespace mnm::plugin::one
