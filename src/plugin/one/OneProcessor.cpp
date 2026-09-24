#include "OneProcessor.h"
#include "OneEditor.h"
#include "SharedSettings.h"
#include "Store.h"
#include <cmath>
#include <map>

namespace mnm::plugin::one {

using namespace host;

juce::AudioProcessor::BusesProperties MnmOneProcessor::makeBuses(Variant variant)
{
    // FX: a plain stereo effect; the main input is what the FX machine hears (INP AB on the hardware).
    if (variant == Variant::Fx)
        return BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true).withOutput("Output", juce::AudioChannelSet::stereo(), true);
    const int numTracks = numTracksOf(variant);
    // The side-chain input feeds FX machines (INP AB); off until the host enables it.
    auto b = BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), false);
    if (numTracks == 1) return b.withOutput("Output", juce::AudioChannelSet::stereo(), true);
    // Multitimbral: one stereo bus per track, all live by default, so each hardware track lands on its
    // own mixer channel in the host.
    for (int t = 0; t < numTracks; ++t) b = b.withOutput("Track " + juce::String(t + 1), juce::AudioChannelSet::stereo(), true);
    return b;
}

MnmOneProcessor::MnmOneProcessor(Variant variant)
    : AudioProcessor(makeBuses(variant)),
      apvts(*this, nullptr, "PARAMS", createLayout(variant)),
      m_variant(variant), m_numTracks(numTracksOf(variant))
{
    for (int t = 0; t < m_numTracks; ++t) {
        auto tr = std::make_unique<Track>();
        tr->index = t;
        tr->machine = apvts.getRawParameterValue(machineId(t));
        for (int k = 0; k < 8; ++k) {
            tr->syn[k] = apvts.getRawParameterValue(synId(t, k));
            if (auto* sp = dynamic_cast<SynParam*>(apvts.getParameter(synId(t, k)))) sp->setMachineSource(tr->machine);
        }
        for (int p = 1; p < 4; ++p)
            for (int k = 0; k < 8; ++k) tr->pages[p][k] = apvts.getRawParameterValue(pageId(t, Page(p), k));
        for (int l = 0; l < spec::kNumLfos; ++l)
            for (int k = 0; k < 8; ++k) tr->lfo[l][k] = apvts.getRawParameterValue(lfoId(t, l, k));
        tr->level = apvts.getRawParameterValue(levelId(t));
        tr->mute = apvts.getRawParameterValue(muteId(t));
        tr->lpKeyTrack = apvts.getRawParameterValue(lpKeyTrackId(t));
        tr->hpKeyTrack = apvts.getRawParameterValue(hpKeyTrackId(t));
        if (m_numTracks > 1) {
            tr->input = apvts.getRawParameterValue(inputId(t));
            for (int b = 0; b < 3; ++b) tr->outBus[b] = apvts.getRawParameterValue(outBusId(t, b));
        }
        tr->shadowSlot = defaultMachineSlot(m_variant);
        apvts.addParameterListener(machineId(t), this);
        m_tracks.push_back(std::move(tr));
    }
    m_bpm = apvts.getRawParameterValue(bpmId());
    m_bpmSync = apvts.getRawParameterValue(bpmSyncId());
    m_outputMode = m_numTracks > 1 ? apvts.getRawParameterValue(outputModeId()) : nullptr;
    m_firmwarePath = loadSharedOsPath();   // never a compiled-in path: fresh installs start unconfigured
    loadEngine();
}

MnmOneProcessor::~MnmOneProcessor()
{
    cancelPendingUpdate();
    for (int t = 0; t < m_numTracks; ++t) apvts.removeParameterListener(machineId(t), this);
}

void MnmOneProcessor::loadEngine()
{
    std::unique_ptr<fw::Firmware> fwNew;
    std::vector<std::unique_ptr<MonoVoice>> voices;
    juce::String status;
    if (m_firmwarePath.isEmpty())
        status = "No Monomachine OS file selected";
    else if (!juce::File(m_firmwarePath).existsAsFile())
        status = "OS file not found: " + m_firmwarePath;
    else try {
        fwNew = std::make_unique<fw::Firmware>(fw::loadFirmware(m_firmwarePath.toStdString()));
        for (auto& tr : m_tracks) {
            const int slot = machineSlotOf(*tr);
            tr->currentMachine = machineSupported(slot) ? machineAt(slot) : host::Machine::SAW;   // preview machines: engine idles on SAW
            auto v = std::make_unique<MonoVoice>(*fwNew);
            v->host().setMachine(tr->currentMachine);
            v->host().setOutBuses(uint32_t(outBusMask(*tr)));
            v->host().setKeyTracking(tr->lpKeyTrack->load() >= 0.5f, tr->hpKeyTrack->load() >= 0.5f);
            if (isFxMachine(tr->currentMachine)) { v->host().setRouting(dspInputBits(fxInputOf(*tr))); v->host().noteOn(60); }
            v->warmUp(64);   // every voice runs the same warm-up, so the six frame counters stay in step
            voices.push_back(std::move(v));
        }
        status = "OS" + juce::String(fwNew->version).trim() + " loaded, "
               + (m_numTracks > 1 ? juce::String(m_numTracks) + " DSP engines ready (" : juce::String("DSP engine ready ("))
               + (voices.front()->engine().usingJit() ? "JIT" : "interpreter") + ")";
    } catch (const std::exception& e) {
        status = "Firmware load failed: " + juce::String(e.what());
        fwNew.reset(); voices.clear();
    }
    {
        const juce::ScopedLock sl(m_engineLock);
        m_engineReady = false;
        for (size_t t = 0; t < m_tracks.size(); ++t) {
            m_tracks[t]->voice = fwNew ? std::move(voices[t]) : nullptr;
            m_tracks[t]->heldNotes.clear();
        }
        m_firmware = std::move(fwNew);
        m_status = status;
        m_engineReady = m_firmware != nullptr;
    }
}

void MnmOneProcessor::setFirmwarePath(const juce::String& path, bool persist)
{
    m_firmwarePath = path;
    loadEngine();
    if (persist && m_engineReady) saveSharedOsPath(path);   // a failed load must not poison the other plugins
}

void MnmOneProcessor::refreshSharedOsPath()
{
    const auto p = loadSharedOsPath();
    if (p.isNotEmpty() && p != m_firmwarePath && juce::File(p).existsAsFile()) setFirmwarePath(p, false);
}

void MnmOneProcessor::clearFirmware()
{
    m_firmwarePath.clear();
    loadEngine();
    saveSharedOsPath({});
}

void MnmOneProcessor::initPreset()
{
    // Forget every machine's remembered SYN values first, so the reset below lands on hardware defaults and
    // the side-effect pass has nothing to restore.
    for (auto& tr : m_tracks) { tr->visited.fill(false); tr->shadowSlot = defaultMachineSlot(m_variant); }
    libraryFromTree({});
    for (auto* p : getParameters())   // the machine parameter precedes its SYN knobs, whose defaults follow it
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p)) {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost(rp->getDefaultValue());
            rp->endChangeGesture();
        }
    updateHostDisplay(ChangeDetails().withParameterInfoChanged(true));
}

void MnmOneProcessor::parameterChanged(const juce::String&, float) { triggerAsyncUpdate(); }

void MnmOneProcessor::syncMachineSideEffects()
{
    bool changed = false;
    for (auto& trp : m_tracks) {
        auto& tr = *trp;
        const int slot = machineSlotOf(tr);
        if (slot == tr.shadowSlot) continue;
        const int old = tr.shadowSlot;
        for (int k = 0; k < 8; ++k) tr.shadow[size_t(old)][size_t(k)] = uint8_t(juce::jlimit(0, 127, int(std::lround(tr.syn[k]->load()))));
        tr.visited[size_t(old)] = true;
        for (int k = 0; k < 8; ++k) {
            const int v = tr.visited[size_t(slot)] ? int(tr.shadow[size_t(slot)][size_t(k)]) : int(spec::kMachines[slot].params[k].defaultRaw);
            if (auto* p = apvts.getParameter(synId(tr.index, k))) p->setValueNotifyingHost(p->convertTo0to1(float(v)));
        }
        const bool wasFx = isFxMachine(machineAt(old)), nowFx = isFxMachine(machineAt(slot));
        if (wasFx != nowFx) {
            // machine assign loads the page defaults; AMP is the one page that differs (FX: envelope held open)
            const uint8_t* amp = nowFx ? spec::kAmpDefaultsFx : spec::kSharedPages[0].defaults;
            for (int k = 0; k < 8; ++k)
                if (auto* p = apvts.getParameter(pageId(tr.index, Page::AMP, k))) p->setValueNotifyingHost(p->convertTo0to1(float(amp[k])));
        }
        tr.shadowSlot = slot;
        changed = true;
    }
    if (changed) updateHostDisplay(ChangeDetails().withParameterInfoChanged(true));   // SYN A-H are named after the machine's knobs
}

bool MnmOneProcessor::applyKitTrack(int t, const mnm::dump::Kit& kit, int kitTrack, juce::String& error, bool soundOnly)
{
    if (t < 0 || t >= m_numTracks || kitTrack < 0 || kitTrack > 5) { error = "No such track"; return false; }
    const auto& kt = kit.tracks[kitTrack];
    const int slot = machineSlot(host::Machine(kt.model));
    if (slot < 0 || !spec::machineByIndex(kt.model)) { error = "Machine " + juce::String(int(kt.model)) + " is not available in this plugin"; return false; }
    if (isEffect() && !spec::kMachines[slot].isFx) { error = juce::String(spec::kMachines[slot].displayName) + " is a synth machine: Monomodule FX plays the FX machines only"; return false; }
    auto set = [this](const juce::String& id, float v) { if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v)); };
    set(machineId(t), float(slot));
    syncMachineSideEffects();   // the machine change settles its SYN memory and AMP defaults first; the kit's values then overwrite them
    for (int k = 0; k < 8; ++k) {
        set(synId(t, k), float(kt.params[k]));
        set(pageId(t, Page::AMP, k), float(kt.params[8 + k]));
        set(pageId(t, Page::FILT, k), float(kt.params[16 + k]));
        set(pageId(t, Page::EFX, k), float(kt.params[24 + k]));
        for (int l = 0; l < spec::kNumLfos; ++l) set(lfoId(t, l, k), float(kt.params[32 + 8 * l + k]));
    }
    if (!soundOnly) set(levelId(t), float(kt.level));
    set(lpKeyTrackId(t), kit.lpKeyTracks(kitTrack) ? 1.0f : 0.0f);
    set(hpKeyTrackId(t), kit.hpKeyTracks(kitTrack) ? 1.0f : 0.0f);
    m_tracks[size_t(t)]->carry = kt;
    if (m_numTracks > 1 && !soundOnly) {
        const int out = kit.outBuses(kitTrack);
        for (int b = 0; b < 3; ++b) set(outBusId(t, b), (out >> b) & 1 ? 1.0f : 0.0f);
        set(inputId(t), float(juce::jlimit(0, 6, kit.fxInput(kitTrack))));
    }
    return true;
}

bool MnmOneProcessor::applyKit(const mnm::dump::Kit& kit, juce::String& error)
{
    if (m_numTracks < 6) { error = "Drop a track here; whole kits go into Monomodule Six"; return false; }
    for (int t = 0; t < 6; ++t)
        if (!m_tracks[size_t(t)]->locked && !applyKitTrack(t, kit, t, error)) return false;
    m_kitCarry = kit;
    return true;
}

// ---- library state ----

std::vector<uint8_t> MnmOneProcessor::soundPrint(int t, bool withKitBytes) const
{
    const auto kt = currentKitTrack(t);
    std::vector<uint8_t> v;
    v.push_back(kt.model);
    v.insert(v.end(), kt.params, kt.params + 56);
    const auto& tr = *m_tracks[size_t(t)];
    v.push_back(tr.lpKeyTrack->load() > 0.5f); v.push_back(tr.hpKeyTrack->load() > 0.5f);
    if (withKitBytes) { v.push_back(kt.level); v.push_back(uint8_t(outBusMask(tr))); v.push_back(uint8_t(fxInputOf(tr))); }
    return v;
}

mnm::dump::KitTrack MnmOneProcessor::currentKitTrack(int t) const
{
    const auto& tr = *m_tracks[size_t(t)];
    auto raw = [](const std::atomic<float>* p) { return uint8_t(juce::jlimit(0, 127, int(std::lround(p->load())))); };
    mnm::dump::KitTrack kt = tr.carry;   // ASSIGN matrix, MIDI page, extra bytes
    kt.model = spec::kMachines[machineSlotOf(tr)].index;
    for (int k = 0; k < 8; ++k) {
        kt.params[k] = raw(tr.syn[k]);
        kt.params[8 + k] = raw(tr.pages[int(Page::AMP)][k]);
        kt.params[16 + k] = raw(tr.pages[int(Page::FILT)][k]);
        kt.params[24 + k] = raw(tr.pages[int(Page::EFX)][k]);
        for (int l = 0; l < spec::kNumLfos; ++l) kt.params[32 + 8 * l + k] = raw(tr.lfo[l][k]);
    }
    kt.level = raw(tr.level);
    return kt;
}

mnm::dump::Kit MnmOneProcessor::currentKit() const
{
    // One: a kit with the sound on track 1 (what the library stores for a saved preset). Six: the whole kit.
    mnm::dump::Kit kit = m_kitCarry;
    kit.lpKeyTrack = kit.hpKeyTrack = 0;
    kit.patchBusIn = 0;
    for (int t = 0; t < 6; ++t) {
        if (t >= m_numTracks) { kit.tracks[t] = {}; continue; }
        const auto& tr = *m_tracks[size_t(t)];
        auto kt = currentKitTrack(t);
        if (tr.lpKeyTrack->load() > 0.5f) kit.lpKeyTrack |= uint8_t(1u << t);
        if (tr.hpKeyTrack->load() > 0.5f) kit.hpKeyTrack |= uint8_t(1u << t);
        if (m_numTracks > 1) {
            uint8_t type = uint8_t(outBusMask(tr) & 7);
            if (spec::kMachines[machineSlotOf(tr)].isFx) {
                const int in = int(fxInputOf(tr));
                if (in >= 4) { type |= 0x08; kit.patchBusIn = uint16_t(kit.patchBusIn | ((in - 3) << (2 * t))); }
                else type |= uint8_t(0x10 | (in << 6));
            }
            kt.type = type;
        } else kt.type = 1;   // OUT BUS AB
        kit.tracks[t] = kt;
    }
    return kit;
}

bool MnmOneProcessor::loadPreset(int t, const mnm::dump::Kit& kit, int kitTrack, const juce::String& id, const juce::String& name, juce::String& error)
{
    if (!applyKitTrack(t, kit, kitTrack, error, true)) return false;
    markPresetSaved(t, id, name);
    return true;
}

bool MnmOneProcessor::loadKit(const mnm::dump::Kit& kit, const juce::String& id, const juce::String& name, juce::String& error)
{
    if (!applyKit(kit, error)) return false;
    for (int t = 0; t < m_numTracks; ++t)
        if (!m_tracks[size_t(t)]->locked) { auto& tr = *m_tracks[size_t(t)]; tr.loaded = {}; tr.loadedPrint.clear(); }   // the kit names them now
    markKitSaved(id, name);
    return true;
}

void MnmOneProcessor::markPresetSaved(int t, const juce::String& id, const juce::String& name)
{
    auto& tr = *m_tracks[size_t(t)];
    tr.loaded = {id, name};
    tr.loadedPrint = soundPrint(t, false);
}

void MnmOneProcessor::markKitSaved(const juce::String& id, const juce::String& name)
{
    m_loadedKit = {id, name};
    m_loadedKitPrint.clear();
    for (int t = 0; t < m_numTracks; ++t) { const auto v = soundPrint(t, true); m_loadedKitPrint.insert(m_loadedKitPrint.end(), v.begin(), v.end()); }
}

bool MnmOneProcessor::presetModified(int t) const
{
    const auto& tr = *m_tracks[size_t(t)];
    return tr.loaded.valid() && tr.loadedPrint != soundPrint(t, false);
}

bool MnmOneProcessor::kitModified() const
{
    if (!m_loadedKit.valid()) return false;
    std::vector<uint8_t> now;
    for (int t = 0; t < m_numTracks; ++t) { const auto v = soundPrint(t, true); now.insert(now.end(), v.begin(), v.end()); }
    return now != m_loadedKitPrint;
}

static juce::String toHex(const uint8_t* d, size_t n) { return juce::String::toHexString(d, int(n), 0); }
static std::vector<uint8_t> fromHex(const juce::String& s) { juce::MemoryBlock mb; mb.loadFromHexString(s); const auto* d = static_cast<const uint8_t*>(mb.getData()); return std::vector<uint8_t>(d, d + mb.getSize()); }

juce::ValueTree MnmOneProcessor::libraryToTree() const
{
    juce::ValueTree lib("LIBRARY");
    lib.setProperty("kitId", m_loadedKit.id, nullptr);
    lib.setProperty("kitName", m_loadedKit.name, nullptr);
    lib.setProperty("kitPrint", toHex(m_loadedKitPrint.data(), m_loadedKitPrint.size()), nullptr);
    if (m_loadedKit.valid()) lib.setProperty("kitCarry", juce::JSON::toString(mnm::library::kitToJson(m_kitCarry), true), nullptr);
    for (const auto& trp : m_tracks) {
        const auto& tr = *trp;
        juce::ValueTree n("TRACK");
        n.setProperty("index", tr.index, nullptr);
        n.setProperty("id", tr.loaded.id, nullptr);
        n.setProperty("name", tr.loaded.name, nullptr);
        n.setProperty("print", toHex(tr.loadedPrint.data(), tr.loadedPrint.size()), nullptr);
        n.setProperty("locked", tr.locked, nullptr);
        // the bytes that are not parameters: params[56..71] and the ASSIGN matrix
        std::vector<uint8_t> c(tr.carry.params + 56, tr.carry.params + 72);
        for (int s = 0; s < 6; ++s) for (int d = 0; d < 2; ++d) { c.push_back(tr.carry.destPage[s][d]); c.push_back(tr.carry.destParam[s][d]); c.push_back(uint8_t(tr.carry.destRange[s][d])); }
        n.setProperty("carry", toHex(c.data(), c.size()), nullptr);
        lib.appendChild(n, nullptr);
    }
    return lib;
}

void MnmOneProcessor::libraryFromTree(const juce::ValueTree& lib)
{
    m_loadedKit = {}; m_loadedKitPrint.clear(); m_kitCarry = {};
    for (auto& tr : m_tracks) { tr->loaded = {}; tr->loadedPrint.clear(); tr->carry = {}; tr->locked = false; }
    if (!lib.isValid()) return;
    m_loadedKit = {lib.getProperty("kitId").toString(), lib.getProperty("kitName").toString()};
    m_loadedKitPrint = fromHex(lib.getProperty("kitPrint").toString());
    if (lib.hasProperty("kitCarry")) mnm::library::kitFromJson(juce::JSON::parse(lib.getProperty("kitCarry").toString()), m_kitCarry);
    for (const auto& n : lib) {
        const int t = int(n.getProperty("index", -1));
        if (t < 0 || t >= m_numTracks) continue;
        auto& tr = *m_tracks[size_t(t)];
        tr.loaded = {n.getProperty("id").toString(), n.getProperty("name").toString()};
        tr.loadedPrint = fromHex(n.getProperty("print").toString());
        tr.locked = bool(n.getProperty("locked", false));
        const auto c = fromHex(n.getProperty("carry").toString());
        if (c.size() == 16 + 36) {
            std::copy(c.begin(), c.begin() + 16, tr.carry.params + 56);
            size_t i = 16;
            for (int s = 0; s < 6; ++s) for (int d = 0; d < 2; ++d) { tr.carry.destPage[s][d] = c[i++]; tr.carry.destParam[s][d] = c[i++]; tr.carry.destRange[s][d] = int8_t(c[i++]); }
        }
    }
}

// ---- previews ----

void MnmOneProcessor::previewPlay(const juce::String& key, const std::function<mnm::preview::PreviewSpec()>& build)
{
    if (!m_previewRenderer) m_previewRenderer = std::make_unique<mnm::library::PreviewRenderer>();
    m_previewRenderer->setFirmwarePath(m_firmwarePath);
    m_previewRenderer->clearStatus();
    double bpm = double(loadSharedSetting("previewBpm", "120").getFloatValue());
    if (bpm < 30.0 || bpm > 300.0) bpm = 120.0;
    auto audio = m_previewRenderer->request(key, bpm, build);
    if (!audio || !m_previewRenderer->hasFirmwarePath()) { previewStop(); return; }
    m_previewAudio = audio;
    m_previewRenderer->keep(audio);
    m_previewVoice.start(audio, -1);
    m_previewKey = key;
}

void MnmOneProcessor::previewStop()
{
    m_previewVoice.stop();
    m_previewKey.clear();
}

bool MnmOneProcessor::previewPoll()
{
    if (m_previewKey.isEmpty()) return false;
    if (m_previewVoice.consumeFinished() || (m_previewAudio && m_previewAudio->failed.load()) || !m_previewVoice.active()) { previewStop(); return true; }
    return false;
}

juce::String MnmOneProcessor::statusText() const
{
    const juce::ScopedLock sl(m_engineLock);
    juce::String s = m_status;
    for (const auto& tr : m_tracks)
        if (tr->voice && tr->voice->engine().faulted()) {
            s += (m_numTracks > 1 ? " | T" + juce::String(tr->index + 1) : juce::String()) + " DSP FAULT: " + juce::String(tr->voice->engine().faultReason());
            break;
        }
    if (const auto& tr = m_tracks.front(); tr->voice && !tr->voice->engine().faulted()) {
        const auto& st = tr->voice->engine().stats();
        if (st.blocks) s += " | " + juce::String(st.totalInstructions / st.blocks) + " instr/block";
    }
    if (m_needsResample) s += " | host rate " + juce::String(m_hostRate, 0) + " Hz: resampling from 44100 (not 1:1)";
    for (const auto& tr : m_tracks) {
        const int slot = machineSlotOf(*tr);
        if (!machineSupported(slot))
            s += juce::String(" | ") + (m_numTracks > 1 ? "T" + juce::String(tr->index + 1) + " " : juce::String()) + spec::kMachines[slot].displayName + ": PREVIEW ONLY, no sound (machine not implemented yet)";
    }
    return s;
}

void MnmOneProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    m_hostRate = sampleRate;
    m_needsResample = std::abs(sampleRate - 44100.0) > 0.5;
    const int maxIn = int(std::ceil(samplesPerBlock * 44100.0 / sampleRate)) + 32;
    m_sideL.assign(size_t(samplesPerBlock), 0.f); m_sideR.assign(size_t(samplesPerBlock), 0.f);
    for (int b = 0; b < 3; ++b) { m_busL[size_t(b)].assign(size_t(maxIn), 0.f); m_busR[size_t(b)].assign(size_t(maxIn), 0.f); }
    m_outL.assign(size_t(samplesPerBlock), 0.f); m_outR.assign(size_t(samplesPerBlock), 0.f);
    const juce::ScopedLock sl(m_engineLock);
    for (auto& tr : m_tracks) {
        tr->lastFrames = 0;
        tr->engL.assign(size_t(maxIn), 0.f); tr->engR.assign(size_t(maxIn), 0.f);
        tr->inL.assign(size_t(maxIn), 0.f); tr->inR.assign(size_t(maxIn), 0.f);
        tr->hostL.assign(size_t(samplesPerBlock), 0.f); tr->hostR.assign(size_t(samplesPerBlock), 0.f);
        tr->interpL.reset(); tr->interpR.reset(); tr->interpInL.reset(); tr->interpInR.reset();
        if (tr->voice) { tr->voice->warmUp(16); tr->heldNotes.clear(); }
    }
}

bool MnmOneProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto main = layouts.getMainOutputChannelSet();
    if (main != juce::AudioChannelSet::stereo() && main != juce::AudioChannelSet::mono()) return false;
    if (isEffect()) {   // mono or stereo in, never more channels in than out, and the input cannot be switched off
        const auto in = layouts.getMainInputChannelSet();
        return (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono()) && in.size() <= main.size();
    }
    if (layouts.inputBuses.size() > 0) {
        const auto& in = layouts.inputBuses.getReference(0);
        if (!in.isDisabled() && in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()) return false;
    }
    for (int i = 1; i < layouts.outputBuses.size(); ++i) {
        const auto& set = layouts.outputBuses.getReference(i);
        if (set != juce::AudioChannelSet::stereo() && !set.isDisabled()) return false;
    }
    return true;
}

int MnmOneProcessor::outBusMask(const Track& tr) const
{
    if (m_numTracks == 1) return int(kRouteOutAB);
    int mask = 0;
    for (int b = 0; b < 3; ++b) if (tr.outBus[b] && tr.outBus[b]->load() >= 0.5f) mask |= 1 << b;
    return mask;
}

host::FxInput MnmOneProcessor::fxInputOf(const Track& tr) const
{
    if (!tr.input) return FxInput::InpAB;   // One: the side-chain
    return FxInput(juce::jlimit(0, 6, int(std::lround(tr.input->load()))));
}

void MnmOneProcessor::applyParametersToHost(Track& tr)
{
    auto& h = tr.voice->host();
    const Machine m = machineAt(machineSlotOf(tr));
    const bool fx = isFxMachine(m);
    if (m != tr.currentMachine) {
        tr.currentMachine = m;
        h.setMachine(m);   // machine index + init/trig bit, like KIT > machine assign
        if (fx) h.noteOn(60);   // FX machines must be trigged to pass audio
        // the SYN/AMP parameter side effects of the change run on the message thread (syncMachineSideEffects)
    }
    // KIT > EDIT routing and KIT > ASSIGN > KEY as word 37 bits; internal sources are mixed host-side and
    // enter through the ADC path (dspInputBits)
    h.setOutBuses(uint32_t(outBusMask(tr)));
    h.setRouting(fx ? dspInputBits(fxInputOf(tr)) : 0u);
    h.setKeyTracking(tr.lpKeyTrack->load() >= 0.5f, tr.hpKeyTrack->load() >= 0.5f);
    for (int k = 0; k < 8; ++k) h.setParam(Page::SYN, k, int(tr.syn[k]->load()));   // raw kit bytes, as stored
    for (int p = 1; p < 4; ++p)
        for (int k = 0; k < 8; ++k) h.setParam(Page(p), k, int(tr.pages[p][k]->load()));
    for (int l = 0; l < spec::kNumLfos; ++l)
        for (int k = 0; k < 8; ++k) h.setLfoParam(l, k, int(tr.lfo[l][k]->load()));   // raw kit bytes; the host computes the LFOs
    h.setLevel(int(tr.level->load()));
    // tick = 24 x BPM (word 42), as the OS derives it from the tempo: the host's transport tempo, or the
    // free BPM value; switching sync off hands the current host tempo over so nothing jumps
    const bool synced = m_bpmSync->load() >= 0.5f;
    if (tr.index == 0) {
        if (m_wasSynced && !synced)
            if (auto* p = apvts.getParameter(bpmId())) p->setValueNotifyingHost(p->convertTo0to1(m_hostBpm.load()));
        m_wasSynced = synced;
    }
    h.setBpm(double(synced ? m_hostBpm.load() : m_bpm->load()));
}

void MnmOneProcessor::pushParamFromCC(Track& tr, Page page, int k, int value)
{
    const auto id = page == Page::SYN ? synId(tr.index, k) : pageId(tr.index, page, k);
    if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(float(value)));
}

void MnmOneProcessor::handleMidi(Track& tr, const juce::MidiMessage& m)
{
    // Six: MIDI channels 1..6 address tracks 1..6 (the hardware's individual track channels); One listens on every channel
    if (m_numTracks > 1 && m.getChannel() != tr.index + 1) return;
    auto& h = tr.voice->host();
    if (m.isNoteOn()) {
        if (tr.mute->load() >= 0.5f) return;   // mute as on the hardware: new trigs are masked, sound in flight rings out
        const int note = m.getNoteNumber();
        tr.heldNotes.push_back(note);   // the same pitch may be held twice (a DAW's overlapping notes): every press counts
        h.noteOn(note);
    } else if (m.isNoteOff()) {
        // releases one press of that pitch (the oldest); the sound changes only when the current pitch is no longer held
        const int note = m.getNoteNumber();
        const bool wasCurrent = !tr.heldNotes.empty() && tr.heldNotes.back() == note;
        const auto it = std::find(tr.heldNotes.begin(), tr.heldNotes.end(), note);
        if (it != tr.heldNotes.end()) tr.heldNotes.erase(it);
        const bool stillHeld = std::find(tr.heldNotes.begin(), tr.heldNotes.end(), note) != tr.heldNotes.end();
        if (wasCurrent && !stillHeld) { if (tr.heldNotes.empty()) h.noteOff(); else h.noteOn(tr.heldNotes.back()); }
    } else if (m.isAllNotesOff() || m.isAllSoundOff()) {
        tr.heldNotes.clear(); h.noteOff();
    } else if (m.isController()) {
        const int cc = m.getControllerNumber(), v = m.getControllerValue();
        if (cc >= 48 && cc <= 55) pushParamFromCC(tr, Page::SYN, cc - 48, v);
        else if (cc >= 56 && cc <= 63) pushParamFromCC(tr, Page::AMP, cc - 56, v);
        else if (cc >= 72 && cc <= 79) pushParamFromCC(tr, Page::FILT, cc - 72, v);
        else if (cc >= 80 && cc <= 87) pushParamFromCC(tr, Page::EFX, cc - 80, v);
        else if (cc == 7) { if (auto* p = apvts.getParameter(levelId(tr.index))) p->setValueNotifyingHost(p->convertTo0to1(float(v))); }
        else {   // LFO pages (hardware CC map): raw kit bytes like every other knob
            const int lfo = cc >= 88 && cc <= 95 ? 0 : cc >= 104 && cc <= 111 ? 1 : cc >= 112 && cc <= 119 ? 2 : -1;
            if (lfo >= 0) {
                const int k = cc - (lfo == 0 ? 88 : lfo == 1 ? 104 : 112);
                if (auto* p = apvts.getParameter(lfoId(tr.index, lfo, k))) p->setValueNotifyingHost(p->convertTo0to1(float(v)));
            }
        }
    }
}

// Side-chain input (host rate) -> tr.inL/inR at 44.1 kHz; zeros when the host has no input bus enabled.
void MnmOneProcessor::fillFxInput(Track& tr, int nEngine, double ratio)
{
    if (int(tr.inL.size()) < nEngine) { tr.inL.resize(size_t(nEngine)); tr.inR.resize(size_t(nEngine)); }
    if (m_sideChannels == 0) {
        std::fill_n(tr.inL.data(), nEngine, 0.f); std::fill_n(tr.inR.data(), nEngine, 0.f);
        return;
    }
    if (m_needsResample) {
        tr.interpInL.process(1.0 / ratio, m_sideL.data(), tr.inL.data(), nEngine);
        tr.interpInR.process(1.0 / ratio, m_sideR.data(), tr.inR.data(), nEngine);
    } else {
        std::copy_n(m_sideL.data(), nEngine, tr.inL.data());
        std::copy_n(m_sideR.data(), nEngine, tr.inR.data());
    }
}

// Renders one track into tr.engL/R (44.1 kHz) and mixes it into the OUT BUS buses. Tracks render in index
// order, so a track reading a mix bus or its neighbour sees exactly the tracks before it (the hardware
// mixes "in the same order as their index"); a track that reads and writes the same bus replaces the bus
// content (an insert), as the kernel does.
void MnmOneProcessor::renderTrack(Track& tr, int nEngine, double ratio, const juce::MidiBuffer& midi)
{
    if (int(tr.engL.size()) < nEngine) { tr.engL.resize(size_t(nEngine)); tr.engR.resize(size_t(nEngine)); }
    float* L = tr.engL.data();
    float* R = tr.engR.data();
    tr.lastFrames = nEngine;

    if (!machineSupported(machineSlotOf(tr))) {   // preview machine: knobs on screen, engine untouched, silence
        if (!tr.heldNotes.empty()) { tr.heldNotes.clear(); tr.voice->host().noteOff(); }
        std::fill_n(L, nEngine, 0.f); std::fill_n(R, nEngine, 0.f);
        tr.peak.store(tr.peak.load() * 0.8f);
        return;
    }

    // FX machine input: the previous track's render (NEIBOR), the side-chain (INP A/B/AB) or a mix bus
    const bool fx = isFxMachine(machineAt(machineSlotOf(tr)));
    const FxInput input = fxInputOf(tr);
    const float* srcL = nullptr;
    const float* srcR = nullptr;
    if (fx) {
        if (int(tr.inL.size()) < nEngine) { tr.inL.resize(size_t(nEngine)); tr.inR.resize(size_t(nEngine)); }
        switch (input) {
        case FxInput::Neighbor:
            if (tr.index > 0) { const auto& prev = *m_tracks[size_t(tr.index - 1)]; srcL = prev.engL.data(); srcR = prev.engR.data(); }
            else { std::fill_n(tr.inL.data(), nEngine, 0.f); std::fill_n(tr.inR.data(), nEngine, 0.f); srcL = tr.inL.data(); srcR = tr.inR.data(); }   // track 1: silence
            break;
        case FxInput::BusAB: case FxInput::BusCD: case FxInput::BusEF: {
            const size_t b = size_t(int(input) - int(FxInput::BusAB));
            srcL = m_busL[b].data(); srcR = m_busR[b].data();   // the bus as the tracks before this one left it
            break;
        }
        default:   // INP A / INP B / INP AB from the side-chain; for a mono input the kernel copies that ADC channel to both sides
            fillFxInput(tr, nEngine, ratio);
            srcL = tr.inL.data(); srcR = tr.inR.data();
            break;
        }
    }

    auto ev = midi.begin();
    int pos = 0;
    while (pos < nEngine) {
        if (tr.voice->framesBuffered() == 0) {
            // DSP block boundary: apply MIDI events due so far and refresh parameters (as the hardware does)
            const int hostPos = int(pos / ratio);
            while (ev != midi.end() && (*ev).samplePosition <= hostPos) { handleMidi(tr, (*ev).getMessage()); ++ev; }
            applyParametersToHost(tr);
        }
        const int chunk = std::min(nEngine - pos, tr.voice->framesBuffered() > 0 ? tr.voice->framesBuffered() : dsp::DspEngine::kBlockFrames);
        if (fx) tr.voice->processFx(srcL + pos, srcR + pos, L + pos, R + pos, chunk);
        else tr.voice->process(L + pos, R + pos, chunk);
        pos += chunk;
    }
    while (ev != midi.end()) { handleMidi(tr, (*ev).getMessage()); ++ev; }   // late events take effect at the next boundary

    float pk = tr.peak.load() * 0.8f;   // fall-off so the meter stays readable between UI polls
    for (int i = 0; i < nEngine; ++i) pk = std::max({pk, std::abs(L[i]), std::abs(R[i])});
    tr.peak.store(pk);

    // OUT BUS: add into each selected mix bus; replace the bus this FX track reads (insert)
    const int mask = outBusMask(tr);
    for (int b = 0; b < 3; ++b) {
        if (!((mask >> b) & 1)) continue;
        auto& bl = m_busL[size_t(b)]; auto& br = m_busR[size_t(b)];
        const bool insert = fx && int(input) == int(FxInput::BusAB) + b;
        for (int i = 0; i < nEngine; ++i) {
            if (insert) { bl[size_t(i)] = L[i]; br[size_t(i)] = R[i]; }
            else { bl[size_t(i)] += L[i]; br[size_t(i)] += R[i]; }
        }
    }
}

// Copies the block's result into the plugin's output buses (host rate). TRACKS: bus n = track n's own
// output, silent when the track has no OUT BUS (like turning its outputs off on the hardware); a bus the
// host disabled folds into bus 1. BUSES: buses 1-3 = the mix buses AB/CD/EF (the hardware's 3xSTEREO outs).
void MnmOneProcessor::writeOutputs(juce::AudioBuffer<float>& buffer, int nEngine, double ratio)
{
    const int n = buffer.getNumSamples();
    auto emit = [&](int busIdx, const float* L, const float* R, juce::LagrangeInterpolator& iL, juce::LagrangeInterpolator& iR) {
        if (busIdx >= getBusCount(false) || !getBus(false, busIdx)->isEnabled()) busIdx = 0;
        const float* outL = L;
        const float* outR = R;
        if (m_needsResample) {
            if (int(m_outL.size()) < n) { m_outL.resize(size_t(n)); m_outR.resize(size_t(n)); }
            iL.process(ratio, L, m_outL.data(), n);
            iR.process(ratio, R, m_outR.data(), n);
            outL = m_outL.data(); outR = m_outR.data();
        }
        auto bus = getBusBuffer(buffer, false, busIdx);
        if (bus.getNumChannels() > 0) bus.addFrom(0, 0, outL, n);
        if (bus.getNumChannels() > 1) bus.addFrom(1, 0, outR, n);
    };
    const bool buses = m_outputMode && int(std::lround(m_outputMode->load())) == int(OutputMode::Buses);
    if (buses) {
        for (int b = 0; b < 3; ++b) {
            auto& tr = *m_tracks[size_t(b)];   // borrow the first three tracks' interpolators for the three buses
            emit(b, m_busL[size_t(b)].data(), m_busR[size_t(b)].data(), tr.interpL, tr.interpR);
        }
        return;
    }
    for (auto& trp : m_tracks) {
        auto& tr = *trp;
        if (tr.lastFrames < nEngine || outBusMask(tr) == 0) continue;
        emit(tr.index, tr.engL.data(), tr.engR.data(), tr.interpL, tr.interpR);
    }
}

void MnmOneProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
            if (const auto bpm = pos->getBpm(); bpm && *bpm > 0.0) m_hostBpm.store(float(*bpm));
    {
        // capture the side-chain before the (shared) buffer is cleared for output
        auto in = getBusCount(true) > 0 ? getBusBuffer(buffer, true, 0) : juce::AudioBuffer<float>();
        m_sideChannels = in.getNumChannels();
        if (m_sideChannels > 0) {
            if (int(m_sideL.size()) < n) { m_sideL.resize(size_t(n)); m_sideR.resize(size_t(n)); }
            std::copy_n(in.getReadPointer(0), n, m_sideL.data());
            std::copy_n(in.getReadPointer(m_sideChannels > 1 ? 1 : 0), n, m_sideR.data());
        }
    }
    buffer.clear();
    if (auto main = getBusBuffer(buffer, false, 0); main.getNumChannels() > 0)   // a library preview plays whether or not the engine does
        m_previewVoice.process(main.getWritePointer(0), main.getNumChannels() > 1 ? main.getWritePointer(1) : nullptr, main.getNumSamples(), m_hostRate, false);
    const juce::ScopedTryLock sl(m_engineLock);
    if (!sl.isLocked() || !m_engineReady) { midi.clear(); return; }
    const double ratio = 44100.0 / m_hostRate;     // engine frames per host frame
    const int nEngine = m_needsResample ? int(std::ceil(n * ratio)) : n;
    for (int b = 0; b < 3; ++b) {
        if (int(m_busL[size_t(b)].size()) < nEngine) { m_busL[size_t(b)].resize(size_t(nEngine)); m_busR[size_t(b)].resize(size_t(nEngine)); }
        std::fill_n(m_busL[size_t(b)].data(), nEngine, 0.f); std::fill_n(m_busR[size_t(b)].data(), nEngine, 0.f);
    }
    for (auto& tr : m_tracks)   // in track order: NEIBOR and the mix buses carry the tracks before this one
        if (tr->voice) renderTrack(*tr, nEngine, ratio, midi);
    writeOutputs(buffer, nEngine, ratio);
    midi.clear();
}

juce::AudioProcessorEditor* MnmOneProcessor::createEditor()
{
    loadLcdArt(m_firmwarePath);   // before the editor measures any text
    return new OneEditor(*this);
}

// State schema 4 (2026-09): every knob raw 0..127, machine = slot in uispec::kMachines, per-track ids
// t<n>..., SYN A-H as eight parameters per track with the other machines' values in a SHADOWS child.
//   no schema: the previous One held the machine as an index into a 16-entry list and the FM+ ratio knobs
//              as 24-step choice indices;
//   schema 2:  LFO TRIG was a 0..4 choice; the hardware stores it as a kit byte 0..127 (mode = raw*5>>7);
//   schema 3:  every machine's SYN knobs were parameters (t1m<machine index>s<k>).
//   schema 4:  the Six FX input was a two-entry choice (NEIBOR, INP AB); it is now the sysex order
//              (NEIBOR, INP A, INP B, INP AB, BUS AB, BUS CD, BUS EF), so 1 -> 3.
static constexpr int kStateSchema = 5;

void MnmOneProcessor::migrateLegacyState(juce::ValueTree& state) const
{
    const int schema = state.hasProperty("schema") ? int(state.getProperty("schema")) : 0;
    if (schema >= kStateSchema) return;
    static const host::Machine oldOrder[16] = {Machine::GND, Machine::SIN, Machine::NOIS, Machine::SAW, Machine::PULS, Machine::ENS,
        Machine::FM_STAT, Machine::FM_PAR, Machine::FM_DYN, Machine::THRU, Machine::REVERB, Machine::CHORUS,
        Machine::DYNAMIX, Machine::RINGMOD, Machine::PHASER, Machine::FLANGER};
    for (auto child : state) {
        if (!child.hasType("PARAM")) continue;
        const auto id = child.getProperty("id").toString();
        const int v = int(double(child.getProperty("value")));
        if (schema < 2) {
            if (id == machineId(0)) {
                if (v >= 0 && v < 16) child.setProperty("value", double(machineSlot(oldOrder[v])), nullptr);
            } else if (id == legacySynId(8, 0) || id == legacySynId(8, 4) || id == legacySynId(9, 0) || id == legacySynId(9, 2) || id == legacySynId(9, 4)) {
                child.setProperty("value", double(uispec::listRawMid(juce::jlimit(0, 23, v), 24)), nullptr);
            }
        }
        if (schema < 3 && (id == lfoId(0, 0, 2) || id == lfoId(0, 1, 2) || id == lfoId(0, 2, 2)))   // TRIG 0..4 -> raw centre of the 5-entry list
            child.setProperty("value", double(uispec::listRawMid(juce::jlimit(0, 4, v), 5)), nullptr);
        if (schema == 4)
            for (int t = 0; t < kMaxTracks; ++t)
                if (id == inputId(t) && v == 1) child.setProperty("value", double(int(FxInput::InpAB)), nullptr);
    }
    if (m_numTracks == 1 && schema < 4) {
        // schema 3 -> 4: the current machine's t1m<idx>s<k> become t1syn<k>; every other machine's knobs
        // become SHADOWS entries (only where they differ from the hardware defaults)
        int slot = defaultMachineSlot();
        if (auto mp = state.getChildWithProperty("id", machineId(0)); mp.isValid())
            slot = juce::jlimit(0, spec::kNumMachines - 1, int(double(mp.getProperty("value"))));
        const int currentIndex = spec::kMachines[slot].index;
        std::map<int, std::array<int, 8>> others;   // machine index -> values
        std::vector<juce::ValueTree> remove;
        for (auto child : state) {
            if (!child.hasType("PARAM")) continue;
            const auto id = child.getProperty("id").toString();
            if (!id.startsWith("t1m") || !id.containsChar('s')) continue;
            const int sPos = id.indexOfChar(3, 's');
            const juce::String mStr = id.substring(3, sPos), kStr = id.substring(sPos + 1);
            if (mStr.isEmpty() || kStr.isEmpty() || !mStr.containsOnly("0123456789") || !kStr.containsOnly("0123456789")) continue;
            const int mIndex = mStr.getIntValue(), k = kStr.getIntValue();
            if (k < 0 || k > 7) continue;
            const int v = juce::jlimit(0, 127, int(double(child.getProperty("value"))));
            if (mIndex == currentIndex) { child.setProperty("id", synId(0, k), nullptr); continue; }
            if (!others.count(mIndex)) {
                std::array<int, 8> defaults{};
                if (const auto* m = spec::machineByIndex(mIndex)) for (int j = 0; j < 8; ++j) defaults[size_t(j)] = m->params[j].defaultRaw;
                others[mIndex] = defaults;
            }
            others[mIndex][size_t(k)] = v;
            remove.push_back(child);
        }
        for (auto& c : remove) state.removeChild(c, nullptr);
        juce::ValueTree shadows("SHADOWS");
        for (const auto& [mIndex, values] : others) {
            const auto* m = spec::machineByIndex(mIndex);
            if (!m) continue;
            bool differs = false;
            for (int j = 0; j < 8; ++j) differs = differs || values[size_t(j)] != m->params[j].defaultRaw;
            if (!differs) continue;
            juce::ValueTree s("SHADOW");
            s.setProperty("track", 1, nullptr);
            s.setProperty("machine", mIndex, nullptr);
            juce::StringArray vs;
            for (int j = 0; j < 8; ++j) vs.add(juce::String(values[size_t(j)]));
            s.setProperty("values", vs.joinIntoString(","), nullptr);
            shadows.appendChild(s, nullptr);
        }
        if (auto old = state.getChildWithName("SHADOWS"); old.isValid()) state.removeChild(old, nullptr);
        if (shadows.getNumChildren() > 0) state.appendChild(shadows, nullptr);
    }
    state.setProperty("schema", kStateSchema, nullptr);
}

juce::ValueTree MnmOneProcessor::shadowsToTree() const
{
    juce::ValueTree shadows("SHADOWS");
    for (const auto& tr : m_tracks)
        for (int slot = 0; slot < spec::kNumMachines; ++slot) {
            if (!tr->visited[size_t(slot)] || slot == tr->shadowSlot) continue;   // the current machine's values are the SYN parameters
            juce::ValueTree s("SHADOW");
            s.setProperty("track", tr->index + 1, nullptr);
            s.setProperty("machine", int(spec::kMachines[slot].index), nullptr);
            juce::StringArray vs;
            for (int k = 0; k < 8; ++k) vs.add(juce::String(int(tr->shadow[size_t(slot)][size_t(k)])));
            s.setProperty("values", vs.joinIntoString(","), nullptr);
            shadows.appendChild(s, nullptr);
        }
    return shadows;
}

void MnmOneProcessor::shadowsFromTree(const juce::ValueTree& shadows)
{
    for (auto& tr : m_tracks) tr->visited.fill(false);
    if (!shadows.isValid()) return;
    for (auto s : shadows) {
        if (!s.hasType("SHADOW")) continue;
        const int t = int(s.getProperty("track")) - 1;
        if (t < 0 || t >= m_numTracks) continue;
        int slot = -1;
        for (int i = 0; i < spec::kNumMachines; ++i) if (int(spec::kMachines[i].index) == int(s.getProperty("machine"))) slot = i;
        if (slot < 0) continue;
        juce::StringArray vs;
        vs.addTokens(s.getProperty("values").toString(), ",", "");
        if (vs.size() != 8) continue;
        auto& tr = *m_tracks[size_t(t)];
        for (int k = 0; k < 8; ++k) tr.shadow[size_t(slot)][size_t(k)] = uint8_t(juce::jlimit(0, 127, vs[k].getIntValue()));
        tr.visited[size_t(slot)] = true;
    }
}

void MnmOneProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("schema", kStateSchema, nullptr);
    state.setProperty("tracks", m_numTracks, nullptr);
    state.setProperty("firmwarePath", m_firmwarePath, nullptr);
    if (auto old = state.getChildWithName("SHADOWS"); old.isValid()) state.removeChild(old, nullptr);
    auto shadows = shadowsToTree();
    if (shadows.getNumChildren() > 0) state.appendChild(shadows, nullptr);
    if (auto old = state.getChildWithName("LIBRARY"); old.isValid()) state.removeChild(old, nullptr);
    state.appendChild(libraryToTree(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void MnmOneProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid()) {
            migrateLegacyState(state);
            const auto path = state.getProperty("firmwarePath").toString();
            apvts.replaceState(state);
            // the restored SYN values belong to the restored machines: no swap must follow
            shadowsFromTree(state.getChildWithName("SHADOWS"));
            for (auto& tr : m_tracks) tr->shadowSlot = machineSlotOf(*tr);
            libraryFromTree(state.getChildWithName("LIBRARY"));
            updateHostDisplay(ChangeDetails().withParameterInfoChanged(true));
            if (path.isNotEmpty() && path != m_firmwarePath && juce::File(path).existsAsFile()) setFirmwarePath(path, false);   // session restore must not overwrite the shared setting
        }
    }
}

} // namespace mnm::plugin::one
