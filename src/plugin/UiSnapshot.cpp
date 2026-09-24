// Dev tool: renders the plugin editor offscreen to PNG files for UI review (not installed).
// Usage: mnm-uisnapshot <outDir> [osFile.syx]
//        mnm-uisnapshot --logo <sizePx> <marginPx> <AARRGGBB> <out.png>
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <array>
#include "ShnolkLogo.h"
#include "MnmLookAndFeel.h"
#include "Overlays.h"
#include "one/OneProcessor.h"
#include "one/OneEditor.h"
#include "Transfer.h"
#include "Store.h"
#include "MidiExport.h"

using namespace mnm::plugin;

static void saveImage(const juce::Image& img, const juce::File& out)
{
    juce::PNGImageFormat png;
    juce::FileOutputStream os(out);
    if (os.openedOk()) { os.setPosition(0); os.truncate(); png.writeImageToStream(img, os); }
    std::printf("%s\n", out.getFullPathName().toRawUTF8());
}

static void save(juce::Component& c, const juce::File& out)
{
    saveImage(c.createComponentSnapshot(c.getLocalBounds(), true, 1.0f), out);
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc == 6 && juce::String(argv[1]) == "--logo") {
        const int size = juce::String(argv[2]).getIntValue();
        const int margin = juce::String(argv[3]).getIntValue();
        const juce::Colour col(uint32_t(juce::String(argv[4]).getHexValue64()));
        juce::Image img(juce::Image::ARGB, size + 2 * margin, size + 2 * margin, true);
        juce::Graphics g(img);
        drawShnolkLogo(g, juce::Rectangle<float>(float(margin), float(margin), float(size), float(size)), col);
        saveImage(img, juce::File(juce::String(argv[5])));
        return 0;
    }
    if (argc >= 3 && (juce::String(argv[1]) == "--one" || juce::String(argv[1]) == "--one-picker" || juce::String(argv[1]) == "--fx" || juce::String(argv[1]) == "--fx-picker")) {
        // --one <out.png> [os.syx] [machine display name, e.g. FM+-PAR]: Monomodule One editor
        // --one-picker: the same with the machine picker open; --fx / --fx-picker: Monomodule FX
        const bool fxPlugin = juce::String(argv[1]).startsWith("--fx");
        mnm::plugin::one::MnmOneProcessor proc(fxPlugin ? mnm::plugin::one::Variant::Fx : mnm::plugin::one::Variant::One);
        if (argc > 3) proc.setFirmwarePath(juce::String(argv[3]), false);
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        if (auto* led = dynamic_cast<mnm::plugin::one::OneEditor*>(ed.get())) {
            if (const char* skinDlg = std::getenv("MNM_SNAPSHOT_SKIN_DIALOG"); skinDlg && *skinDlg) led->showSkinDialog();   // renders the CUSTOM skin dialog
            if (argc > 4)
                for (int i = 0; i < mnm::uispec::kNumMachines; ++i)
                    if (juce::String(argv[4]) == mnm::uispec::kMachines[i].displayName)
                        if (auto* p = proc.apvts.getParameter(mnm::plugin::one::machineId()))
                            p->setValueNotifyingHost(p->convertTo0to1(float(i)));
            led->refresh();
            if (juce::String(argv[1]).endsWith("-picker")) led->showMachinePicker();
        }
        save(*ed, juce::File(juce::String(argv[2])));
        ed.reset();
        return 0;
    }
    if (argc == 2 && juce::String(argv[1]) == "--migrate") {
        // --migrate: a session saved by the previous One UI (16-entry machine list, FM ratio as a 24-step
        // choice, every machine's SYN knobs as parameters, no schema property) must restore to the same
        // machine and the same kit bytes, with the other machines' knobs kept as SYN memory (schema 4).
        using namespace mnm::plugin::one;
        MnmOneProcessor proc;
        juce::ValueTree old("PARAMS");
        auto add = [&](const juce::String& id, double v) { juce::ValueTree p("PARAM"); p.setProperty("id", id, nullptr); p.setProperty("value", v, nullptr); old.appendChild(p, nullptr); };
        add("t1machine", 7.0);            // old slot 7 = FM+ PAR
        add("t1m9s0", 11.0);              // 1FRQ choice index 11 = "1/2" (raw 60)
        add("t1m9s2", 15.0);              // 2FRQ index 15 = "1" (raw 80)
        add("t1m9s4", 19.0);              // 3FRQ index 19 = "2" (raw 102)
        add("t1m9s1", 77.0);              // a plain knob stays as it is
        add("t1m4s0", 5.0);               // SAW UNIL, not the current machine: must survive as SYN memory
        add("t1m4s1", 9.0);
        juce::MemoryBlock mb;
        proc.copyXmlToBinary(*old.createXml(), mb);
        proc.setStateInformation(mb.getData(), int(mb.getSize()));
        auto raw = [&](const juce::String& id) { return int(proc.apvts.getRawParameterValue(id)->load()); };
        const int slot = raw(machineId());
        const int r0 = raw(synId(0, 0)), r2 = raw(synId(0, 2)), r4 = raw(synId(0, 4)), r1 = raw(synId(0, 1));
        const bool ok = slot == machineSlot(mnm::host::Machine::FM_PAR)
                     && mnm::uispec::listIndex(r0, 24) == 11 && mnm::uispec::listIndex(r2, 24) == 15 && mnm::uispec::listIndex(r4, 24) == 19 && r1 == 77;
        std::printf("MIGRATE %s: slot=%d (%s) 1FRQ=%d 2FRQ=%d 3FRQ=%d 1ENV=%d\n", ok ? "OK" : "FAIL", slot,
                    mnm::uispec::kMachines[slot].displayName, r0, r2, r4, r1);
        // a schema-4 state must pass through untouched
        juce::MemoryBlock mb2;
        proc.getStateInformation(mb2);
        proc.setStateInformation(mb2.getData(), int(mb2.getSize()));
        const bool ok2 = raw(synId(0, 0)) == r0 && raw(synId(0, 1)) == r1;
        std::printf("RELOAD %s\n", ok2 ? "OK" : "FAIL");
        // switching to SAW restores its remembered knobs; switching back restores FM+ PAR's
        auto setMachine = [&](mnm::host::Machine m) {
            auto* p = proc.apvts.getParameter(machineId());
            p->setValueNotifyingHost(p->convertTo0to1(float(machineSlot(m))));
            proc.syncMachineSideEffects();
        };
        setMachine(mnm::host::Machine::SAW);
        const bool ok3 = raw(synId(0, 0)) == 5 && raw(synId(0, 1)) == 9 && raw(synId(0, 7)) == 64;   // UNIL, UNIW, TUNE default
        std::printf("MEMORY SAW %s: UNIL=%d UNIW=%d TUNE=%d\n", ok3 ? "OK" : "FAIL", raw(synId(0, 0)), raw(synId(0, 1)), raw(synId(0, 7)));
        setMachine(mnm::host::Machine::FM_PAR);
        const bool ok4 = raw(synId(0, 0)) == r0 && raw(synId(0, 1)) == r1;
        std::printf("MEMORY FM+PAR %s: 1FRQ=%d 1ENV=%d\n", ok4 ? "OK" : "FAIL", raw(synId(0, 0)), raw(synId(0, 1)));
        // the memory round-trips through the state: save on FM+ PAR, reload, switch to SAW again
        juce::MemoryBlock mb3;
        proc.getStateInformation(mb3);
        MnmOneProcessor proc2;
        proc2.setStateInformation(mb3.getData(), int(mb3.getSize()));
        auto* p2 = proc2.apvts.getParameter(machineId());
        p2->setValueNotifyingHost(p2->convertTo0to1(float(machineSlot(mnm::host::Machine::SAW))));
        proc2.syncMachineSideEffects();
        const bool ok5 = int(proc2.apvts.getRawParameterValue(synId(0, 0))->load()) == 5 && int(proc2.apvts.getRawParameterValue(synId(0, 1))->load()) == 9;
        std::printf("MEMORY RELOAD %s\n", ok5 ? "OK" : "FAIL");
        // a switch between a synth and an FX machine loads the FX AMP page (DEC/REL held open) and back
        setMachine(mnm::host::Machine::REVERB);
        const bool ok6 = raw(pageId(0, mnm::host::Page::AMP, 2)) == 127 && raw(pageId(0, mnm::host::Page::AMP, 3)) == 127;
        setMachine(mnm::host::Machine::SAW);
        const bool ok7 = raw(pageId(0, mnm::host::Page::AMP, 2)) == 64;
        std::printf("FX AMP DEFAULTS %s\n", ok6 && ok7 ? "OK" : "FAIL");
        return ok && ok2 && ok3 && ok4 && ok5 && ok6 && ok7 ? 0 : 1;
    }
    if (argc == 4 && juce::String(argv[1]) == "--rendertest") {
        // --rendertest <os.syx> <dump.syx>: several kit previews back to back through one KitRenderer, as the app's
        // render thread does across previews (each render resets the DSPs, which rewrites their program memory).
        const auto fwr = mnm::fw::loadFirmware(juce::String(argv[2]).toStdString());
        juce::MemoryBlock mb; juce::File(juce::String(argv[3])).loadFileAsData(mb);
        const auto d = mnm::dump::parseDump(static_cast<const uint8_t*>(mb.getData()), mb.getSize(), "dump");
        std::unique_ptr<mnm::preview::KitRenderer> rp;
        try { rp = std::make_unique<mnm::preview::KitRenderer>(fwr); }   // boots six DSPs: a JIT failure throws here
        catch (const std::exception& e) { std::printf("renderer: %s\n", e.what()); return 1; }
        auto& r = *rp;
        mnm::preview::PreviewOptions opt;
        int n = 0;
        for (const auto& k : d.kits) {
            if (k.isEmptySlot() || n >= 8) continue;
            const auto spec = mnm::preview::patternPreview(k, mnm::preview::demoPattern(k), opt);
            try { r.loadKit(spec.kit, opt.bpm); } catch (const std::exception& e) { std::printf("kit %d %-16s %s\n", n, k.name.c_str(), e.what()); ++n; continue; }
            double sum = 0;
            const bool ok = r.render(spec.events, spec.frames, [&](uint32_t, const mnm::preview::RenderBlock& b) { for (float v : b.mixL) sum += v * v; });
            std::printf("kit %d %-16s %s rms %.4f\n", n, k.name.c_str(), ok ? "ok" : r.error().c_str(), std::sqrt(sum / spec.frames));
            ++n;
        }
        return 0;
    }
    if (argc == 2 && juce::String(argv[1]) == "--looptest") {
        // --looptest: the preview voice's loop wraps at the pattern's last pass, seamlessly, at 44.1 and 48 kHz.
        // A synthetic render: a sine of period 100 frames (so the loop region [1000, 3000) joins itself without
        // a step), 5000 frames. The output must follow the sine through every wrap within the interpolator's error.
        using namespace mnm::library;
        bool ok = true;
        constexpr double kPeriod = 100.0;
        for (double rate : {44100.0, 48000.0}) {
            auto a = std::make_shared<PreviewAudio>();
            a->frames = 5000; a->loopStart = 1000; a->loopEnd = 3000;
            a->mixL.resize(a->frames); a->mixR.resize(a->frames);
            for (uint32_t f = 0; f < a->frames; ++f) a->mixL[f] = a->mixR[f] = float(std::sin(2.0 * juce::MathConstants<double>::pi * f / kPeriod));
            a->ready.store(a->frames); a->done.store(true);
            PreviewVoice v;
            v.start(a, -1);
            v.setLoop(true);
            std::vector<float> out;
            float blkL[256], blkR[256];
            for (int b = 0; b < 60; ++b) { if (!v.process(blkL, blkR, 256, rate, true)) break; out.insert(out.end(), blkL, blkL + 256); }
            const double ratio = 44100.0 / rate;
            // the interpolator has a fixed latency of a couple of source frames: find it on the first pass, then it
            // must hold through every wrap (a gap or a hold at a seam would shift the phase)
            auto errAt = [&](size_t i, double lag) {
                double p = i * ratio - lag;
                if (p >= 3000.0) p = 1000.0 + std::fmod(p - 3000.0, 2000.0);
                return std::abs(out[i] - std::sin(2.0 * juce::MathConstants<double>::pi * p / kPeriod));
            };
            double lag = 0, best = 1e9;
            for (double l = -4.0; l <= 4.0; l += 0.05) { double e = 0; for (size_t i = 64; i < 2000; ++i) e = std::max(e, errAt(i, l)); if (e < best) { best = e; lag = l; } }
            double maxErr = 0; size_t worst = 0;
            for (size_t i = 64; i < out.size(); ++i) { const double err = errAt(i, lag); if (err > maxErr) { maxErr = err; worst = i; } }
            std::printf("  latency %.2f source frames (first-pass error %.4f)\n", lag, best);
            const int wraps = int((out.size() * ratio - 3000.0) / 2000.0) + 1;
            std::printf("%.0f Hz: %zu frames, about %d wraps, max error %.4f at frame %zu\n", rate, out.size(), wraps, maxErr, worst);
            ok = ok && wraps >= 5 && maxErr < 0.03 && !v.consumeFinished();
            v.setLoop(false);   // and it runs out through the tail once the loop is off
            int more = 0; while (v.process(blkL, blkR, 256, rate, true)) ++more;
            ok = ok && v.consumeFinished() && more > 0;
        }
        std::printf(ok ? "OK\n" : "FAILED\n");
        return ok ? 0 : 1;
    }
    if (argc == 3 && juce::String(argv[1]) == "--fxrender") {
        // --fxrender <os.syx>: headless audio check of Monomodule FX (the effect variant of the One processor).
        // Every FX machine must turn the main input into output and stay silent without one; REVERB must ring on
        // after the input stops; a synth preset must be refused.
        using namespace mnm::plugin::one;
        MnmOneProcessor proc(Variant::Fx);
        proc.setFirmwarePath(juce::String(argv[2]), false);
        if (!proc.engineReady()) { std::printf("engine not ready: %s\n", proc.statusText().toRawUTF8()); return 1; }
        proc.prepareToPlay(44100.0, 512);
        juce::AudioBuffer<float> buf(2, 512);
        double phase = 0.0;
        auto render = [&](bool feedInput, int blocks) {
            double sum = 0;
            for (int b = 0; b < blocks; ++b) {
                buf.clear();
                if (feedInput)
                    for (int i = 0; i < 512; ++i) {
                        const float v = 0.5f * float(std::sin(phase));
                        buf.setSample(0, i, v); buf.setSample(1, i, v);
                        phase += 2.0 * juce::MathConstants<double>::pi * 440.0 / 44100.0;
                    }
                juce::MidiBuffer midi;
                proc.processBlock(buf, midi);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 512; ++i) { const double v = buf.getSample(c, i); sum += v * v; }
            }
            return std::sqrt(sum / (blocks * 2.0 * 512.0));
        };
        bool ok = proc.isEffect() && !proc.acceptsMidi() && proc.getBusCount(true) == 1 && proc.getBus(true, 0)->isEnabled();
        auto* mp = proc.apvts.getParameter(machineId());
        ok = ok && mp != nullptr && mp->getNumSteps() == lastFxSlot() - firstFxSlot() + 1;
        std::printf("machine parameter: %d steps, default %s\n", mp ? mp->getNumSteps() : -1, mp ? mp->getCurrentValueAsText().toRawUTF8() : "?");
        for (int slot = firstFxSlot(); slot <= lastFxSlot(); ++slot) {
            mp->setValueNotifyingHost(mp->convertTo0to1(float(slot)));
            proc.syncMachineSideEffects();
            render(false, 40);                              // let the previous machine's tail die
            const double silent = render(false, 20), wet = render(true, 40), tail = render(false, 8);
            const bool good = silent < 1e-4 && wet > 0.01;
            std::printf("%-12s silent %.6f  with input %.4f  tail %.4f  %s\n", mnm::uispec::kMachines[slot].displayName, silent, wet, tail, good ? "ok" : "FAIL");
            ok = ok && good;
            if (machineAt(slot) == mnm::host::Machine::REVERB && tail < 1e-3) { std::printf("REVERB has no tail\n"); ok = false; }
        }
        mnm::dump::Kit kit{};
        kit.tracks[0].model = uint8_t(mnm::host::Machine::SAW);
        juce::String err;
        const bool refused = !proc.applyKitTrack(0, kit, 0, err);
        std::printf("synth preset refused: %s (%s)\n", refused ? "yes" : "NO", err.toRawUTF8());
        std::printf(ok && refused ? "OK\n" : "FAILED\n");
        return ok && refused ? 0 : 1;
    }
    if (argc == 3 && juce::String(argv[1]) == "--unirender") {
        // --unirender <os.syx>: headless audio check of Monomodule One.
        // A note must make sound; an FX machine must pass/effect the side-chain input and stay
        // silent without one.
        mnm::plugin::one::MnmOneProcessor proc;
        proc.setFirmwarePath(juce::String(argv[2]), false);
        if (!proc.engineReady()) { std::printf("engine not ready: %s\n", proc.statusText().toRawUTF8()); return 1; }
        proc.enableAllBuses();   // side-chain on, as a host would when the user picks a source
        proc.prepareToPlay(44100.0, 512);
        juce::AudioBuffer<float> buf(2, 512);
        double phase = 0.0;
        // renders `blocks` host blocks; optional note-on at the start and/or a 440 Hz side-chain tone
        auto render = [&](bool noteOn, bool feedInput, int blocks) {
            double sum = 0;
            for (int b = 0; b < blocks; ++b) {
                buf.clear();
                if (feedInput)
                    for (int i = 0; i < 512; ++i) {
                        const float v = 0.5f * float(std::sin(phase));
                        buf.setSample(0, i, v); buf.setSample(1, i, v);
                        phase += 2.0 * juce::MathConstants<double>::pi * 440.0 / 44100.0;
                    }
                juce::MidiBuffer midi;
                if (b == 0 && noteOn) midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
                proc.processBlock(buf, midi);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 512; ++i) { const double v = buf.getSample(c, i); sum += v * v; }
            }
            return std::sqrt(sum / (blocks * 2.0 * 512.0));
        };
        auto quiesce = [&] {
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
            buf.clear();
            proc.processBlock(buf, midi);
            for (int round = 0; round < 20 && render(false, false, 20) > 1e-6; ++round) {}
        };
        auto setMachine = [&](mnm::host::Machine m) {
            auto* p = proc.apvts.getParameter(mnm::plugin::one::machineId());
            p->setValueNotifyingHost(p->convertTo0to1(float(mnm::plugin::one::machineSlot(m))));
            proc.syncMachineSideEffects();   // SYN memory + FX AMP defaults (an async update in a host)
            render(false, false, 4);   // machine assign + FX trig take effect at the next block boundaries
        };
        // a note or a passed tone reads ~1e-2..1e-1; residual release tails read ~1e-5
        const double kLoud = 1e-4, kQuiet = 1e-3;
        bool ok = true;
        quiesce();   // machine assign trigs at load (as the hardware does); let it decay
        const double note = render(true, false, 40);
        std::printf("SAW note: rms %.5f\n", note);
        ok = ok && note > kLoud;
        for (auto m : {mnm::host::Machine::SID, mnm::host::Machine::VO6, mnm::host::Machine::WAVE, mnm::host::Machine::BBOX, mnm::host::Machine::DDRW, mnm::host::Machine::DENS}) {   // synth paths enabled in v0.7.6
            quiesce();
            setMachine(m);
            quiesce();
            const double n = render(true, false, 40);
            std::printf("%s note: rms %.5f\n", mnm::host::machineDef(m)->name, n);
            ok = ok && n > kLoud;
        }
        quiesce();
        for (auto m : {mnm::host::Machine::THRU, mnm::host::Machine::REVERB}) {
            setMachine(m);
            const double fed = render(false, true, 40);
            quiesce();
            const double unfed = render(false, false, 40);
            std::printf("%s: side-chain tone rms %.5f, no input rms %.6f\n", mnm::host::machineDef(m)->name, fed, unfed);
            ok = ok && fed > kLoud && unfed < kQuiet;
        }
        setMachine(mnm::host::Machine::SAW);
        std::printf("UNIRENDER %s\n", ok ? "OK" : "FAIL");
        return ok ? 0 : 1;
    }
    if (argc >= 3 && juce::String(argv[1]) == "--six") {
        // --six <out.png> [os.syx] [track 1-6] [machine display name]: the Monomodule Six editor with a track selected
        using namespace mnm::plugin::one;
        MnmOneProcessor proc(6);
        if (argc > 3) proc.setFirmwarePath(juce::String(argv[3]), false);
        const int track = argc > 4 ? juce::jlimit(1, 6, juce::String(argv[4]).getIntValue()) - 1 : 0;
        // a few different machines so the column reads as a kit
        const mnm::host::Machine demo[6] = {mnm::host::Machine::SAW, mnm::host::Machine::REVERB, mnm::host::Machine::FM_PAR,
                                            mnm::host::Machine::BBOX, mnm::host::Machine::SID, mnm::host::Machine::GND};
        for (int t = 0; t < 6; ++t)
            if (auto* p = proc.apvts.getParameter(machineId(t))) p->setValueNotifyingHost(p->convertTo0to1(float(machineSlot(demo[t]))));
        if (argc > 5)
            for (int i = 0; i < mnm::uispec::kNumMachines; ++i)
                if (juce::String(argv[5]) == mnm::uispec::kMachines[i].displayName)
                    if (auto* p = proc.apvts.getParameter(machineId(track))) p->setValueNotifyingHost(p->convertTo0to1(float(i)));
        if (auto* p = proc.apvts.getParameter(muteId(3))) p->setValueNotifyingHost(1.0f);   // track 4 muted
        proc.syncMachineSideEffects();
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        if (auto* led = dynamic_cast<OneEditor*>(ed.get())) { led->selectTrack(track); led->refresh(); }
        save(*ed, juce::File(juce::String(argv[2])));
        ed.reset();
        return 0;
    }
    if ((argc == 3 || argc == 4) && juce::String(argv[1]) == "--libtest") {
        // --libtest <dump.syx> [os.syx] (with the OS file also: a preview sounds through processBlock): the plugin's library state (load, locks, modified, save, plugin state, stepping) over a
        // scratch library holding that dump.
        using namespace mnm::plugin::one;
        int fails = 0;
        auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mnm-libtest-" + juce::String(juce::Random::getSystemRandom().nextInt(1 << 30)));
        root.createDirectory();
        #ifdef _WIN32
        _putenv_s("MNM_LIBRARY_DIR", root.getFullPathName().toRawUTF8());
#else
        setenv("MNM_LIBRARY_DIR", root.getFullPathName().toRawUTF8(), 1);
#endif
        {
            mnm::library::Store store;
            const auto r = store.importSysexFile(juce::File(juce::String(argv[2])), mnm::library::ImportMode::NewProject, {}, nullptr);
            if (r.failed()) { std::printf("import failed: %s\n", r.getErrorMessage().toRawUTF8()); return 1; }
        }
        auto check = [&](bool ok, const char* what) { std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; };
        {
            MnmOneProcessor proc(6);
            int track = 0;
            LibraryBridge lib(proc, [&track] { return track; });
            lib.refresh();
            check(!lib.cat().kits.empty(), "library has kits");
            // every kit survives plugin -> kit: sound bytes, level, routing, key tracking, carried ASSIGN bytes
            int same = 0, total = 0;
            for (const auto& k : lib.cat().kits) {
                juce::String err;
                if (!proc.loadKit(k.kit, juce::String(k.id), juce::String(k.name), err)) continue;
                ++total;
                const auto out = proc.currentKit();
                bool eq = out.lpKeyTrack == k.kit.lpKeyTrack && out.hpKeyTrack == k.kit.hpKeyTrack;
                for (int t = 0; t < 6 && eq; ++t) {
                    const auto& a = k.kit.tracks[t]; const auto& b = out.tracks[t];
                    const bool fx = mnm::uispec::machineByIndex(a.model) && mnm::uispec::machineByIndex(a.model)->isFx;
                    eq = a.model == b.model && a.level == b.level && std::equal(a.params, a.params + 72, b.params)
                      && std::memcmp(a.destPage, b.destPage, sizeof(a.destPage)) == 0 && std::memcmp(a.destRange, b.destRange, sizeof(a.destRange)) == 0
                      && (a.type & 7) == (b.type & 7) && (!fx || k.kit.fxInput(t) == out.fxInput(t));
                    if (!eq) std::printf("      kit %s track %d differs (type %02x -> %02x)\n", k.name.c_str(), t + 1, a.type, b.type);
                }
                same += eq ? 1 : 0;
                if (eq && !proc.kitModified()) continue;
                if (proc.kitModified()) { std::printf("      kit %s reads as modified right after loading\n", k.name.c_str()); --same; }
            }
            std::printf("      %d of %d kits round-trip\n", same, total);
            check(same == total && total > 0, "kits round-trip through the plugin unchanged");

            // locks: a locked track keeps its sound
            const auto k0 = lib.cat().kits.front(); const auto k1 = lib.cat().kits[1];   // copies: a save rebuilds the catalog
            lib.loadKit(k0.id);
            proc.setTrackLocked(2, true);
            const auto before = proc.currentKit().tracks[2];
            lib.loadKit(k1.id);
            const auto after = proc.currentKit();
            check(std::equal(before.params, before.params + 56, after.tracks[2].params) && before.model == after.tracks[2].model, "locked track keeps its sound");
            check(after.tracks[0].model == k1.kit.tracks[0].model && std::equal(after.tracks[0].params, after.tracks[0].params + 56, k1.kit.tracks[0].params), "unlocked track takes the kit");
            proc.setTrackLocked(2, false);

            // preset load keeps the track's level and routing, modified follows the knobs
            lib.loadKit(k0.id);
            track = 1;
            const auto lvl = proc.currentKit().tracks[1].level; const auto type = proc.currentKit().tracks[1].type;
            const auto pr = lib.cat().presets[lib.cat().presets.size() / 2];
            check(lib.loadPreset(pr.id), "preset loads");
            check(proc.currentKit().tracks[1].level == lvl && (proc.currentKit().tracks[1].type & 7) == (type & 7), "preset load keeps level and OUT BUS");
            check(proc.loadedPreset(1).id == juce::String(pr.id) && !proc.presetModified(1), "loaded preset is named and unmodified");
            check(proc.kitModified(), "the kit now reads as modified");
            auto* knob = proc.apvts.getParameter(pageId(1, mnm::host::Page::FILT, 0));
            knob->setValueNotifyingHost(knob->getValue() > 0.5f ? 0.2f : 0.8f);
            check(proc.presetModified(1), "a knob makes it modified");

            // save as a new version, also into the project
            const auto slot = lib.model().projectSlotOfPreset(pr.id);
            check(slot.valid(), "the preset has a project slot");
            const int versionsBefore = int(lib.model().projects().front().versions.size());
            juce::String newId;
            const auto r = lib.model().savePreset(proc.currentKit(), 1, "TEST SAVE", juce::String(pr.id), "Monomodule Six", slot, &newId);
            check(r.wasOk(), "save works");
            proc.markPresetSaved(1, newId, "TEST SAVE");
            check(!proc.presetModified(1), "saved = unmodified");
            const auto* saved = lib.cat().preset(newId.toStdString());
            check(saved && saved->saved && saved->parentId == pr.id && juce::String(saved->name) == "TEST SAVE", "the catalog lists it with its parent and name");
            bool grew = false;
            for (const auto& p : lib.model().projects()) if (p.id == slot.projectId) grew = int(p.versions.size()) == versionsBefore + 1 && p.current()->kind == "saved";
            check(grew, "the project got a new 'saved' version");
            const auto* st = lib.model().state(slot.projectId);
            const auto* pk = st ? st->kitAt(slot.kit) : nullptr;
            check(pk && std::equal(pk->tracks[slot.track].params, pk->tracks[slot.track].params + 56, proc.currentKit().tracks[1].params), "the project slot carries the edited sound");

            // plugin state keeps it all
            proc.setTrackLocked(4, true);
            juce::MemoryBlock mb; proc.getStateInformation(mb);
            MnmOneProcessor other(6);
            other.setStateInformation(mb.getData(), int(mb.getSize()));
            check(other.loadedPreset(1).name == "TEST SAVE" && other.loadedPreset(1).id == newId && !other.presetModified(1), "state restores the loaded preset");
            check(other.loadedKit().id == juce::String(k0.id) && other.kitModified(), "state restores the loaded kit");
            check(other.trackLocked(4) && !other.trackLocked(2), "state restores the locks");
            bool carry = true;
            const auto kitA = other.currentKit(), kitB = proc.currentKit();
            for (int t = 0; t < 6; ++t) carry = carry && std::memcmp(kitA.tracks[t].destPage, kitB.tracks[t].destPage, 12) == 0 && std::memcmp(kitA.tracks[t].destRange, kitB.tracks[t].destRange, 12) == 0
                                             && std::equal(kitA.tracks[t].params, kitA.tracks[t].params + 72, kitB.tracks[t].params);
            check(carry, "state restores the carried bytes");

            // stepping walks this machine's presets
            track = 0;
            lib.step(1); const auto a1 = proc.loadedPreset(0).id; lib.step(1); const auto a2 = proc.loadedPreset(0).id; lib.step(-1);
            check(a1.isNotEmpty() && a1 != a2 && proc.loadedPreset(0).id == a1, "previous / next step through the machine's presets");
            if (argc == 4) {   // a preview is mixed into the plugin's main output, with no note played
                MnmOneProcessor one(1);
                one.setFirmwarePath(juce::String(argv[3]), false);
                one.setRateAndBufferSizeDetails(48000.0, 512);
                one.prepareToPlay(48000.0, 512);
                LibraryBridge lb(one, [] { return 0; });
                lb.refresh();
                const auto pid = lb.presets(int(mnm::host::Machine::SAW), nullptr, false, false, {}).front()->id;
                lb.preview("preset", pid);
                check(lb.previewing("preset", pid), "a preview starts");
                juce::AudioBuffer<float> buf(one.getTotalNumOutputChannels() > 2 ? one.getTotalNumOutputChannels() : juce::jmax(2, one.getTotalNumInputChannels()), 512);
                juce::MidiBuffer midi;
                float peak = 0.0f; bool ended = false;
                for (int i = 0; i < 1500 && !ended; ++i) {
                    buf.clear();
                    one.processBlock(buf, midi);
                    peak = juce::jmax(peak, buf.getMagnitude(0, 0, 512));
                    if (peak < 0.001f) juce::Thread::sleep(2);   // the render thread is still loading the OS
                    ended = one.previewPoll();
                }
                std::printf("      preview peak %.3f, ended %d\n", double(peak), int(ended));
                check(peak > 0.01f, "the preview sounds in the plugin's output");
                check(ended && one.previewKey().isEmpty(), "and ends by itself");
            }
            // pattern MIDI for the DAW
            const auto mid = lib.patternMidiFile(lib.cat().patterns.front().id);
            check(mid.existsAsFile() && mid.getSize() > 30, "a pattern gives a MIDI file");
        }
        root.deleteRecursively();
        std::printf(fails ? "LIBTEST FAILED (%d)\n" : "LIBTEST OK\n", fails);
        return fails ? 1 : 0;
    }
    if (argc >= 5 && juce::String(argv[1]) == "--lib") {
        // --lib <one|six> <out.png> <strip|menu|kitmenu|panel|kits|patterns|save|savekit> [os.syx]: the library UI of the
        // editor, over the library in MNM_LIBRARY_DIR. The first preset (Six: first kit) is loaded and a knob moved,
        // so the strip shows a name and the "modified" star.
        using namespace mnm::plugin::one;
        const bool six = juce::String(argv[2]) == "six";
        const juce::String view(argv[4]);
        MnmOneProcessor proc(six ? 6 : 1);
        if (argc > 5) proc.setFirmwarePath(juce::String(argv[5]), false);
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        if (auto* led = dynamic_cast<OneEditor*>(ed.get())) {
            LibraryBridge lib(proc, [led] { return led->selectedTrack(); });
            lib.refresh();
            lib.onLoaded = [led] { led->refresh(); };
            if (six && !lib.cat().kits.empty()) { lib.loadKit(lib.cat().kits.front().id); proc.setTrackLocked(2, true); }
            if (!lib.cat().presets.empty()) lib.loadPreset(lib.cat().presets[lib.cat().presets.size() / 3].id);
            if (auto* p = proc.apvts.getParameter(pageId(0, mnm::host::Page::FILT, 1))) p->setValueNotifyingHost(0.33f);
            std::printf("presets %d kits %d patterns %d  loaded '%s' modified %d\n", int(lib.cat().presets.size()), int(lib.cat().kits.size()), int(lib.cat().patterns.size()),
                        proc.loadedPreset(0).name.toRawUTF8(), int(proc.presetModified(0)));
            led->refresh();
            led->resized();
            if (view == "menu") led->showPresetMenu(false);
            else if (view == "kitmenu") led->showPresetMenu(true);
            else if (view == "panel") led->showLibrary(0);
            else if (view == "kits") led->showLibrary(1);
            else if (view == "patterns") led->showLibrary(2);
            else if (view == "save") led->showSaveDialog(false);
            else if (view == "savekit") led->showSaveDialog(true);
            if (std::getenv("MNM_SNAPSHOT_PREVIEW") && !lib.cat().presets.empty()) {   // a preset playing (looping with =loop): the transport glyphs
                const auto& p = lib.cat().presets[lib.cat().presets.size() / 3];
                lib.preview("preset", p.id);
                proc.previewSetLoop(juce::String(std::getenv("MNM_SNAPSHOT_PREVIEW")) == "loop");
            }
            led->refresh();
        }
        save(*ed, juce::File(juce::String(argv[3])));
        ed.reset();
        return 0;
    }
    if (argc == 4 && juce::String(argv[1]) == "--drop") {
        // --drop <os.syx> <dump.syx>: library drag files applied through the editors' drop path. A kit file fills
        // a Six (machines, pages, levels, routing, key flags); a track file lands on the track it is dropped on
        // in Six and on the one track of One; One refuses kits.
        using namespace mnm::plugin::one;
        using namespace mnm::library;
        juce::MemoryBlock mb;
        if (!juce::File(juce::String(argv[3])).loadFileAsData(mb)) { std::printf("cannot read dump\n"); return 1; }
        const auto d = mnm::dump::parseDump(static_cast<const uint8_t*>(mb.getData()), mb.getSize(), "dump");
        const auto* kit = d.kitAt(4);      // a kit with six SID tracks and distinct SYN values
        const auto* kit0 = d.kitAt(0);     // SUPERWAVES
        if (!kit || !kit0) { std::printf("kits missing\n"); return 1; }
        const auto kitFile = writeKitDragFile(*kit, "kit");
        const auto trackFile = writeTrackDragFile(*kit0, 2, "track");
        bool ok = true;
        auto raw = [](MnmOneProcessor& p, const juce::String& id) { return int(std::lround(p.apvts.getRawParameterValue(id)->load())); };
        auto checkTrack = [&](MnmOneProcessor& p, int t, const mnm::dump::Kit& k, int kt, const char* what) {
            bool good = raw(p, machineId(t)) == machineSlot(mnm::host::Machine(k.tracks[kt].model)) && raw(p, levelId(t)) == k.tracks[kt].level
                     && (raw(p, lpKeyTrackId(t)) >= 1) == k.lpKeyTracks(kt) && (raw(p, hpKeyTrackId(t)) >= 1) == k.hpKeyTracks(kt);
            for (int i = 0; i < 8; ++i) {
                good = good && raw(p, synId(t, i)) == k.tracks[kt].params[i] && raw(p, pageId(t, mnm::host::Page::AMP, i)) == k.tracks[kt].params[8 + i]
                    && raw(p, pageId(t, mnm::host::Page::FILT, i)) == k.tracks[kt].params[16 + i] && raw(p, pageId(t, mnm::host::Page::EFX, i)) == k.tracks[kt].params[24 + i];
                for (int l = 0; l < 3; ++l) good = good && raw(p, lfoId(t, l, i)) == k.tracks[kt].params[32 + 8 * l + i];
            }
            if (p.numTracks() > 1) {
                const int out = (raw(p, outBusId(t, 0)) >= 1 ? 1 : 0) | (raw(p, outBusId(t, 1)) >= 1 ? 2 : 0) | (raw(p, outBusId(t, 2)) >= 1 ? 4 : 0);
                good = good && out == k.outBuses(kt) && raw(p, inputId(t)) == k.fxInput(kt);
            }
            std::printf("%s T%d <- kit %d track %d: %s\n", what, t + 1, k.position + 1, kt + 1, good ? "OK" : "MISMATCH");
            ok = ok && good;
        };
        {
            MnmOneProcessor six(6);
            six.setFirmwarePath(juce::String(argv[2]), false);
            std::unique_ptr<juce::AudioProcessorEditor> ed(six.createEditor());
            auto* led = dynamic_cast<OneEditor*>(ed.get());
            juce::String err;
            ok = ok && led->isInterestedInFileDrag({kitFile.getFullPathName()}) && led->dropFile(kitFile, 0, err);
            for (int t = 0; t < 6; ++t) checkTrack(six, t, *kit, t, "SIX kit drop");
            ok = ok && led->dropFile(trackFile, 4, err) && led->selectedTrack() == 4;
            checkTrack(six, 4, *kit0, 2, "SIX track drop");
            checkTrack(six, 3, *kit, 3, "SIX untouched neighbour");
        }
        {
            MnmOneProcessor one;
            one.setFirmwarePath(juce::String(argv[2]), false);
            std::unique_ptr<juce::AudioProcessorEditor> ed(one.createEditor());
            auto* led = dynamic_cast<OneEditor*>(ed.get());
            juce::String err;
            const bool refusesKit = !led->isInterestedInFileDrag({kitFile.getFullPathName()}) && !led->dropFile(kitFile, 0, err);
            std::printf("ONE refuses a kit: %s (%s)\n", refusesKit ? "OK" : "FAIL", err.toRawUTF8());
            ok = ok && refusesKit && led->dropFile(trackFile, 0, err);
            checkTrack(one, 0, *kit0, 2, "ONE track drop");
        }
        // pattern MIDI: the whole pattern has one MIDI track per Monomachine track with trigs, on channels 1-6
        const auto* pat = d.patternAt(0);
        const auto mf = buildPatternMidiFile(d, *pat);
        int expected = 0;
        for (int t = 0; t < 6; ++t) if (pat->noteTrigCount(t) > 0) ++expected;
        bool midiOk = mf.getNumTracks() == expected;
        for (int i = 0; i < mf.getNumTracks() && midiOk; ++i) {
            int channel = 0;
            for (auto* ev : *mf.getTrack(i)) if (ev->message.isNoteOn()) { channel = ev->message.getChannel(); break; }
            midiOk = midiOk && channel >= 1 && channel <= 6;
        }
        const auto midiFile = writePatternMidiDragFile(d, *pat, -1);
        std::printf("PATTERN MIDI: %d tracks (%d expected), drag file %lld bytes: %s\n", mf.getNumTracks(), expected, (long long) midiFile.getSize(), midiOk && midiFile.getSize() > 0 ? "OK" : "FAIL");
        ok = ok && midiOk && midiFile.getSize() > 0;
        std::printf("DROP %s\n", ok ? "OK" : "FAIL");
        return ok ? 0 : 1;
    }
    if (argc == 3 && juce::String(argv[1]) == "--sixrender") {
        // --sixrender <os.syx>: headless multitimbral check of Monomodule Six. A note on channel 3 sounds
        // on bus 3 only; a REVERB on track 2 fed from track 1 (NEIBOR) sounds on bus 2 when track 1 plays
        // and not otherwise; mute cuts a track's bus while it keeps feeding its neighbour.
        using namespace mnm::plugin::one;
        MnmOneProcessor proc(6);
        proc.setFirmwarePath(juce::String(argv[2]), false);
        if (!proc.engineReady()) { std::printf("engine not ready: %s\n", proc.statusText().toRawUTF8()); return 1; }
        proc.enableAllBuses();
        proc.prepareToPlay(44100.0, 512);
        const int chans = proc.getTotalNumOutputChannels();
        juce::AudioBuffer<float> buf(chans, 512);
        std::array<double, 6> rms{};
        // renders `blocks` host blocks with an optional note-on (channel 1-based) at the start; per-bus rms
        auto render = [&](int noteChannel, int blocks) {
            std::array<double, 6> sum{};
            for (int b = 0; b < blocks; ++b) {
                buf.clear();
                juce::MidiBuffer midi;
                if (b == 0 && noteChannel > 0) midi.addEvent(juce::MidiMessage::noteOn(noteChannel, 60, 1.0f), 0);
                proc.processBlock(buf, midi);
                for (int t = 0; t < 6; ++t)
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < 512; ++i) { const double v = buf.getSample(2 * t + c, i); sum[size_t(t)] += v * v; }
            }
            for (int t = 0; t < 6; ++t) rms[size_t(t)] = std::sqrt(sum[size_t(t)] / (blocks * 2.0 * 512.0));
        };
        auto quiesce = [&] {
            juce::MidiBuffer midi;
            for (int ch = 1; ch <= 6; ++ch) midi.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
            buf.clear();
            proc.processBlock(buf, midi);
            for (int round = 0; round < 20; ++round) {
                render(0, 20);
                if (*std::max_element(rms.begin(), rms.end()) < 1e-6) break;
            }
        };
        auto setParam = [&](const juce::String& id, float v) {
            auto* p = proc.apvts.getParameter(id);
            p->setValueNotifyingHost(p->convertTo0to1(v));
        };
        const double kLoud = 1e-4, kQuiet = 1e-3;
        bool ok = true;
        auto report = [&](const char* what) {
            std::printf("%s:", what);
            for (int t = 0; t < 6; ++t) std::printf(" T%d=%.5f", t + 1, rms[size_t(t)]);
            std::printf("\n");
        };
        quiesce();
        render(3, 40);
        report("note on ch 3");
        for (int t = 0; t < 6; ++t) ok = ok && (t == 2 ? rms[size_t(t)] > kLoud : rms[size_t(t)] < kQuiet);
        quiesce();
        // track 2 = REVERB fed by track 1
        setParam(machineId(1), float(machineSlot(mnm::host::Machine::REVERB)));
        setParam(inputId(1), float(int(FxInput::Neighbor)));
        proc.syncMachineSideEffects();
        render(0, 4);
        quiesce();
        render(1, 40);
        report("note on ch 1, T2 REVERB<-NEIBOR");
        ok = ok && rms[0] > kLoud && rms[1] > kLoud && rms[2] < kQuiet;
        quiesce();
        render(0, 40);
        report("no note");
        ok = ok && rms[1] < kQuiet;
        // mute track 1: as on the hardware, its trigs are masked, so nothing sounds on it or in the reverb after it
        setParam(muteId(0), 1.0f);
        quiesce();
        render(1, 40);
        report("note on ch 1, T1 muted");
        ok = ok && rms[0] < kQuiet && rms[1] < kQuiet;
        setParam(muteId(0), 0.0f);
        // mix buses: T1 to bus CD only, T3 = REVERB reading BUS CD and writing AB (a send), T2 back to SAW on AB.
        // TRACKS outputs: T1 and T3 on their own buses; BUSES outputs: bus 1 (AB) = T2 dry + reverb, bus 2 (CD) = T1 dry
        setParam(machineId(1), float(machineSlot(mnm::host::Machine::SAW)));
        setParam(machineId(2), float(machineSlot(mnm::host::Machine::REVERB)));
        setParam(inputId(2), float(int(FxInput::BusCD)));
        setParam(outBusId(0, 0), 0.0f); setParam(outBusId(0, 1), 1.0f);
        proc.syncMachineSideEffects();
        render(0, 4);
        quiesce();
        render(1, 40);
        report("note on ch 1 -> CD, T3 REVERB<-BUS CD");
        ok = ok && rms[0] > kLoud && rms[2] > kLoud && rms[1] < kQuiet;
        setParam(outputModeId(), float(int(OutputMode::Buses)));
        quiesce();
        render(1, 40);
        report("same, BUSES outputs");
        ok = ok && rms[0] > kLoud && rms[1] > kLoud && rms[2] < kQuiet && rms[3] < 1e-9;   // AB = reverb, CD = T1 dry, EF empty, 4-6 silent
        // an insert: T3 reads and writes AB while T1 plays on AB -> AB carries the reverb output only
        setParam(outputModeId(), float(int(OutputMode::Tracks)));
        setParam(outBusId(0, 0), 1.0f); setParam(outBusId(0, 1), 0.0f);
        setParam(inputId(2), float(int(FxInput::BusAB)));
        setParam(machineId(2), float(machineSlot(mnm::host::Machine::THRU)));   // THRU = the dry sum passed through
        proc.syncMachineSideEffects();
        render(0, 4);
        setParam(outputModeId(), float(int(OutputMode::Buses)));
        quiesce();
        render(1, 40);
        const double abInsert = rms[0];
        setParam(outBusId(2, 0), 0.0f);   // the insert's output off: AB keeps T1 dry as the tracks before T3 left it
        quiesce();
        render(1, 40);
        report("AB with THRU insert on / off");
        ok = ok && abInsert > kLoud && rms[0] > kLoud;
        setParam(outputModeId(), float(int(OutputMode::Tracks)));
        std::printf("SIXRENDER %s\n", ok ? "OK" : "FAIL");
        return ok ? 0 : 1;
    }
    MnmLookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
    if (argc < 2) { std::printf("usage: mnm-uisnapshot <outDir> [osFile.syx]\n"); return 1; }
    const juce::File outDir{juce::String(argv[1])};
    outDir.createDirectory();

    {
        // Rendered standalone: inside an editor the shared-settings poll would immediately
        // reload a locally configured OS file and hide the overlay.
        MissingOsOverlay overlay([] {});
        overlay.setSize(760, 700);
        save(overlay, outDir.getChildFile("editor_missing_os.png"));
    }
    if (argc > 2) {
        mnm::plugin::one::MnmOneProcessor proc(mnm::plugin::one::Variant::Fx);
        proc.setFirmwarePath(juce::String(argv[2]), false);
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        save(*ed, outDir.getChildFile("editor_ready.png"));
    }
    {
        AboutOverlay about(mnm::plugin::one::kFxTitle);
        about.setSize(760, 700);
        save(about, outDir.getChildFile("about.png"));
    }
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    return 0;
}
