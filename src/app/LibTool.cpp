// mnm-libtool: the library from the terminal (and the test driver for the store).
//   import <file.syx> [<project id> | pack]   a new project, a new version of a project, or a sound pack (MNM_LIBRARY_DIR overrides the location)
//   list                         projects with their versions, newest first
//   show <project> [kit N | pattern N]   summary of a project's current version, a kit, or a pattern
//   export <project> <out.syx> [vN]      the whole state as sysex (unedited messages verbatim), recorded as a version
//   verify <file.syx>            decode -> encode byte comparison per message, no library involved
//   selftest <file.syx>          the store end to end on a temp library: versions, export, restore, similarity, items, migration
//   render <file.syx> <out.png> [project [kits|history] [bank N] | edit | save | export [step] | presets|kits|patterns [row] | params [row]]   the app window, offscreen
//   track <file.syx> <kit N> <track 1-6> <out.mnmtrack>   the drag file for a kit track (drop it on One/Six)
//   kit <file.syx> <kit N> <out.mnmkit>                   the drag file for a kit (drop it on Six)
//   midi <file.syx> <pattern N> [track 1-6] <out.mid>     a pattern's MIDI (one track, or all with trigs)
//   preview <file.syx> <out.wav> kit N [T] | pattern N [T] | track N T [--bpm B]   the audio preview of a kit (its best
//                                pattern, or the demo pattern), a pattern, or a kit track (T = one track's own output);
//                                the OS from $MNM_OS, the plugins' shared setting, or the build default
//   playtest <file.syx> <kit N> <track 1-6>   plays that track's preset preview through the default output device
//                                (the app's PreviewPlayer: render thread, cache, streaming) and reports its progress
#include <juce_gui_basics/juce_gui_basics.h>
#include "Store.h"
#include "MidiExport.h"
#include "Transfer.h"
#include "LibraryComponent.h"
#include "SharedSettings.h"
#include "PreviewPlayer.h"
#include "preview/KitRenderer.h"
#include "preview/Preview.h"
#include "util/Wav.h"
#include <cstdio>
#include <cstdlib>

using namespace mnm::library;
using namespace mnm::dump;

static std::vector<uint8_t> readAll(const juce::File& f)
{
    juce::MemoryBlock mb;
    f.loadFileAsData(mb);
    return std::vector<uint8_t>(static_cast<const uint8_t*>(mb.getData()), static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
}

static int verify(const juce::File& f)
{
    const auto bytes = readAll(f);
    if (bytes.empty()) { std::printf("cannot read %s\n", f.getFullPathName().toRawUTF8()); return 1; }
    const auto d = parseDump(bytes.data(), bytes.size(), f.getFileNameWithoutExtension().toStdString());
    int decoded = 0, identical = 0;
    for (size_t i = 0; i < d.messages.size(); ++i) {
        const auto& m = d.messages[i];
        std::vector<uint8_t> enc;
        if (m.kitIndex >= 0) enc = encodeKit(d.kits[size_t(m.kitIndex)]);
        else if (m.patternIndex >= 0) enc = encodePattern(d.patterns[size_t(m.patternIndex)]);
        else continue;
        ++decoded;
        if (enc == m.raw) ++identical;
        else std::printf("message %zu (id %02x pos %d): differs (%zu vs %zu bytes)\n", i, m.id, m.position, enc.size(), m.raw.size());
    }
    const bool whole = encodeDump(d) == bytes;
    std::printf("%s: %zu messages, %zu kits, %zu patterns, %d songs, %d globals, %d damaged, %d unknown; %d/%d re-encode identical; whole file %s\n",
                f.getFileName().toRawUTF8(), d.messages.size(), d.kits.size(), d.patterns.size(), d.numSongs, d.numGlobals, d.numDamaged, d.numUnknown,
                identical, decoded, whole ? "IDENTICAL" : "DIFFERS");
    return identical == decoded && whole ? 0 : 1;
}

static void showKit(const Kit& k)
{
    std::printf("kit %d \"%s\"  multimode %d timing %d split %d/%d portamento %d legato %d/%d/%d\n", k.position, k.name.c_str(),
                k.commonMultimode, k.commonTiming, k.splitKey, k.splitRange, k.trigPortamento, k.trigLegatoAmp, k.trigLegatoFilter, k.trigLegatoLFO);
    for (int t = 0; t < 6; ++t) {
        const auto& tr = k.tracks[t];
        std::printf("  T%d %-10s lev %3d out %-5s in %-6s key LP%d HP%d  syn", t + 1, machineName(tr.model).toRawUTF8(), tr.level,
                    outBusName(k.outBuses(t)).toRawUTF8(), fxInputName(k.fxInput(t)), k.lpKeyTracks(t), k.hpKeyTracks(t));
        for (int i = 0; i < 8; ++i) std::printf(" %3d", tr.params[i]);
        std::printf("\n");
    }
}

static void showPattern(const Pattern& p)
{
    std::printf("pattern %s len %d kit %d swing %d%% %s transpose %d locks %d midi notes %d chord notes %d\n", patternSlotName(p.position).c_str(),
                p.patternLength, p.kit, p.swingPercent(), p.doubleTempo ? "2x" : "1x", p.patternTranspose, p.locksUsed, p.midiNotesUsed, p.chordNotesUsed);
    for (int t = 0; t < 6; ++t) {
        std::printf("  T%d ", t + 1);
        for (int j = 0; j < p.patternLength && j < 64; ++j) {
            const bool on = (p.ampTrigs[t] >> j) & 1, off = (p.offTrigs[t] >> j) & 1, tl = (p.triglessTrigs[t] >> j) & 1;
            std::putchar(on ? 'X' : off ? 'o' : tl ? '.' : '-');
        }
        std::printf("\n");
    }
}

static int preview(const juce::StringArray& args)
{
    const auto src = juce::File::getCurrentWorkingDirectory().getChildFile(args[1]);
    const auto bytes = readAll(src);
    if (bytes.empty()) { std::printf("cannot read %s\n", src.getFullPathName().toRawUTF8()); return 1; }
    const auto d = parseDump(bytes.data(), bytes.size(), src.getFileNameWithoutExtension().toStdString());
    const auto out = juce::File::getCurrentWorkingDirectory().getChildFile(args[2]);
    mnm::preview::PreviewOptions opt;
    juce::StringArray rest;
    for (int i = 3; i < args.size(); ++i) {
        if (args[i] == "--bpm" && i + 1 < args.size()) opt.bpm = args[++i].getDoubleValue();
        else rest.add(args[i]);
    }
    if (rest.size() < 2) { std::printf("preview: kit N [T] | pattern N [T] | track N T\n"); return 1; }
    const auto what = rest[0];
    const int n = rest[1].getIntValue();
    int stem = -1;
    mnm::preview::PreviewSpec spec;
    if (what == "kit" || what == "pattern") {
        if (rest.size() > 2) stem = juce::jlimit(1, 6, rest[2].getIntValue()) - 1;
        const Kit* kit = nullptr;
        const Pattern* pat = nullptr;
        Pattern demo;
        if (what == "pattern") {
            pat = d.patternAt(n);
            if (!pat) { std::printf("no pattern %d\n", n); return 1; }
            kit = d.kitAt(pat->kit);
            if (!kit) { std::printf("pattern %d: kit slot %d is empty\n", n, pat->kit); return 1; }
        } else {
            kit = d.kitAt(n);
            if (!kit) { std::printf("no kit %d\n", n); return 1; }
            mnm::catalog::Catalog cat;
            cat.build({{"dump", d.file, &d}});
            const auto kitId = cat.kitIdOf("dump", n);
            const auto* item = cat.kit(kitId);
            const auto best = item ? mnm::preview::choosePreviewPattern(cat, *item) : std::string();
            if (const auto* p = best.empty() ? nullptr : cat.pattern(best)) { pat = &p->pattern; std::printf("kit %d: previewing with pattern %s\n", n, p->name.c_str()); }
            else { demo = mnm::preview::demoPattern(*kit); pat = &demo; std::printf("kit %d: no pattern uses it, previewing with the demo pattern\n", n); }
        }
        spec = mnm::preview::patternPreview(*kit, *pat, opt);
    } else if (what == "track" && rest.size() > 2) {
        const auto* kit = d.kitAt(n);
        if (!kit) { std::printf("no kit %d\n", n); return 1; }
        const int t = juce::jlimit(1, 6, rest[2].getIntValue()) - 1;
        spec = mnm::preview::presetPreview(kit->tracks[t], kit->lpKeyTracks(t), kit->hpKeyTracks(t), opt);
    } else { std::printf("preview: kit N [T] | pattern N [T] | track N T\n"); return 1; }
    juce::String os = juce::String(std::getenv("MNM_OS") ? std::getenv("MNM_OS") : "");
    if (os.isEmpty()) os = mnm::plugin::loadSharedOsPath();
    if (os.isEmpty() || !juce::File(os).existsAsFile()) os = MNM_OS_SYX;
    try {
        const auto fw = mnm::fw::loadFirmware(os.toStdString());
        mnm::preview::KitRenderer r(fw);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        r.loadKit(spec.kit, opt.bpm);
        std::vector<int32_t> pcm;
        pcm.reserve(size_t(spec.frames) * 2 + 32);
        auto to24 = [](float x) { return int32_t(std::lrint(juce::jlimit(-1.0f, 0.99999988f, x) * 8388608.0f)); };
        const bool ok = r.render(spec.events, spec.frames, [&](uint32_t, const mnm::preview::RenderBlock& b) {
            for (int i = 0; i < mnm::preview::RenderBlock::kFrames; ++i) {
                const float l = stem < 0 ? b.mixL[size_t(i)] : b.stemL[size_t(stem)][size_t(i)];
                const float rr = stem < 0 ? b.mixR[size_t(i)] : b.stemR[size_t(stem)][size_t(i)];
                pcm.push_back(to24(l)); pcm.push_back(to24(rr));
            }
        });
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        if (!ok) { std::printf("render failed: %s\n", r.error().c_str()); return 1; }
        if (!mnm::util::writeWav24(out.getFullPathName().toStdString(), pcm, 2, mnm::preview::kSampleRate)) { std::printf("cannot write %s\n", out.getFullPathName().toRawUTF8()); return 1; }
        std::printf("%s: %.2f s (%d loop%s of %.2f s + tail), %zu events, rendered in %.0f ms, OS %s\n", out.getFullPathName().toRawUTF8(),
                    spec.frames / 44100.0, spec.loops, spec.loops == 1 ? "" : "s", spec.loopFrames / 44100.0, spec.events.size(), ms, fw.version.c_str());
        return 0;
    } catch (const std::exception& e) { std::printf("error: %s\n", e.what()); return 1; }
}

// Drives the PreviewPlayer as the app does: play, poll until playback ends, then play again (cached) and stop early.
static int playtest(const juce::StringArray& args)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto src = juce::File::getCurrentWorkingDirectory().getChildFile(args[1]);
    const auto bytes = readAll(src);
    if (bytes.empty()) { std::printf("cannot read %s\n", src.getFullPathName().toRawUTF8()); return 1; }
    const auto d = parseDump(bytes.data(), bytes.size(), src.getFileNameWithoutExtension().toStdString());
    const auto* kit = d.kitAt(args[2].getIntValue());
    if (!kit) { std::printf("no kit %s\n", args[2].toRawUTF8()); return 1; }
    const int t = juce::jlimit(1, 6, args[3].getIntValue()) - 1;
    juce::String os = juce::String(std::getenv("MNM_OS") ? std::getenv("MNM_OS") : "");
    if (os.isEmpty()) os = mnm::plugin::loadSharedOsPath();
    if (os.isEmpty() || !juce::File(os).existsAsFile()) os = MNM_OS_SYX;
    PreviewPlayer player;
    player.setFirmwarePath(os);
    int changes = 0;
    player.onChange = [&] { ++changes; std::printf("  change %d: playing=%d rendering=%d status=\"%s\"\n", changes, player.isPlaying(), player.rendering(), player.status().toRawUTF8()); };
    const auto opt = player.options();
    auto build = [&] { return mnm::preview::presetPreview(kit->tracks[t], kit->lpKeyTracks(t), kit->hpKeyTracks(t), opt); };
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    int phase = 0; double startedAt = 0;
    bool ok = true;
    player.play("test/" + juce::String(t), build, -1);
    std::printf("play: playing=%d\n", player.isPlaying());
    // pump the message loop (AsyncUpdater callbacks) and watch the player as the app's UI would
    while (true) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        const double now = juce::Time::getMillisecondCounterHiRes() - t0;
        const double p = player.progress();
        if (phase == 0) {   // first run: renders + plays
            if (p > 0 && startedAt == 0) { startedAt = now; std::printf("  audio started after %.0f ms\n", now); }
            if (!player.isPlaying()) {
                std::printf("  ended after %.0f ms (2.5 s of audio), status \"%s\"\n", now, player.status().toRawUTF8());
                ok = ok && startedAt > 0 && player.status().isEmpty();
                phase = 1;
                player.play("test/" + juce::String(t), build, -1);   // cached: no render
                std::printf("play again (cached): playing=%d rendering=%d\n", player.isPlaying(), player.rendering());
                ok = ok && player.isPlaying() && !player.rendering();
            } else if (now > 15000) { std::printf("  timeout: still playing\n"); ok = false; break; }
        } else {
            if (p > 0.2) { player.stop(); std::printf("  stopped at %.0f%%: playing=%d\n", 100 * p, player.isPlaying()); ok = ok && !player.isPlaying(); break; }
            if (!player.isPlaying() || now > 30000) { std::printf("  second run ended unexpectedly\n"); ok = false; break; }
        }
    }
    std::printf("PLAYTEST %s\n", ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}

int main(int argc, char** argv)
{
    const juce::StringArray args(argv + 1, argc - 1);
    if (args.isEmpty()) {
        std::printf("usage: mnm-libtool import <file.syx> | list | show <id> [kit N|pattern N] | export <id> <out.syx> | verify <file.syx> | selftest <file.syx>\n"
                    "       mnm-libtool render <file.syx> out.png [presets|kits|patterns|sysex [row] | import | kit N | track N T | pattern N]\n"
                    "       mnm-libtool track <file.syx> <kit> <track> out.mnmtrack | kit <file.syx> <kit> out.mnmkit | midi <file.syx> <pattern> [track] out.mid\n"
                    "       mnm-libtool preview <file.syx> out.wav kit N [T] | pattern N [T] | track N T [--bpm B]\n");
        return 1;
    }
    const auto cmd = args[0];
    if (cmd == "preview" && args.size() >= 5) return preview(args);
    if (cmd == "playtest" && args.size() == 4) return playtest(args);
    if (cmd == "render" && args.size() >= 3) {
        // the app window offscreen, on a throw-away library holding this dump as a project (imported twice when
        // "history" is asked for, so the timeline has more than one version)
        juce::ScopedJuceInitialiser_GUI gui;
        const auto src = juce::File::getCurrentWorkingDirectory().getChildFile(args[1]);
        const auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mnm-libtool-render-" + newId());
        juce::StringArray what;
        for (int i = 3; i < args.size(); ++i) what.add(args[i]);
        int rc = 1;
        {
            auto store = std::make_unique<Store>(tmp);
            ProjectInfo made;
            if (store->importSysexFile(src, ImportMode::NewProject, {}, &made).failed()) { std::printf("cannot import %s\n", src.getFullPathName().toRawUTF8()); tmp.deleteRecursively(); return 1; }
            if (what.contains("history")) { Dump d; store->loadVersion(made.id, 1, d); mnm::project::clearPattern(d, 1); store->addVersion(made.id, d, "saved", "Edited in the Library: 1 change", {"Pattern A02 cleared"}, "", 1); store->restoreVersion(made.id, 1); }
            if (what.contains("saved") || what.contains("presets")) { Dump d; store->loadVersion(made.id, 1, d); for (const auto& k : d.kits) if (!k.isEmptySlot()) { SavedItem it; it.name = "WIDE LEAD"; it.savedFrom = "Monomodule One"; it.kit = k; it.kit.tracks[0].params[2] ^= 5; it.track = 0; store->saveItem(it); break; } }
            mnm::app::LibraryComponent comp(std::move(store), false);
            comp.show(what.isEmpty() ? "project" : what.joinIntoString(" "));
            if (what.contains("playing")) comp.debugShowPlaying(what.contains("loop"));   // the transport glyphs of a playing item
            const auto out = juce::File::getCurrentWorkingDirectory().getChildFile(args[2]);
            juce::PNGImageFormat png;
            juce::FileOutputStream os(out);
            if (os.openedOk()) {
                os.setPosition(0); os.truncate();
                png.writeImageToStream(comp.createComponentSnapshot(comp.getLocalBounds(), true, 1.0f), os);
                std::printf("%s\n", out.getFullPathName().toRawUTF8());
                rc = 0;
            }
        }
        tmp.deleteRecursively();
        return rc;
    }
    if ((cmd == "track" && args.size() == 5) || (cmd == "kit" && args.size() == 4) || (cmd == "midi" && args.size() >= 4)) {
        const auto src = juce::File::getCurrentWorkingDirectory().getChildFile(args[1]);
        const auto bytes = readAll(src);
        if (bytes.empty()) { std::printf("cannot read %s\n", src.getFullPathName().toRawUTF8()); return 1; }
        const auto d = parseDump(bytes.data(), bytes.size(), src.getFileNameWithoutExtension().toStdString());
        const auto out = juce::File::getCurrentWorkingDirectory().getChildFile(args[args.size() - 1]);
        if (cmd == "midi") {
            const auto* p = d.patternAt(args[2].getIntValue());
            if (!p) { std::printf("no pattern %s\n", args[2].toRawUTF8()); return 1; }
            const int track = args.size() == 5 ? args[3].getIntValue() - 1 : -1;
            juce::FileOutputStream os(out);
            if (!os.openedOk()) return 1;
            os.setPosition(0); os.truncate();
            if (track >= 0) buildTrackMidiFile(d, *p, track).writeTo(os, 1); else buildPatternMidiFile(d, *p).writeTo(os, 1);
        } else {
            const auto* k = d.kitAt(args[2].getIntValue());
            if (!k) { std::printf("no kit %s\n", args[2].toRawUTF8()); return 1; }
            const auto json = cmd == "track" ? trackToTransferJson(*k, juce::jlimit(0, 5, args[3].getIntValue() - 1), juce::String(k->name))
                                             : kitToTransferJson(*k, juce::String(k->name));
            if (!out.replaceWithText(juce::JSON::toString(json))) return 1;
        }
        std::printf("%s\n", out.getFullPathName().toRawUTF8());
        return 0;
    }
    if (cmd == "verify" && args.size() == 2) return verify(juce::File::getCurrentWorkingDirectory().getChildFile(args[1]));

    if (cmd == "selftest" && args.size() == 2) {
        // the store end to end on a throw-away library: import -> project v1 (lossless), a saved edit -> v2, a whole
        // export -> v3 (byte-identical to the source), restore -> v4, similarity, a partial export, saved items, user
        // data, and the migration of the 0.9 - 1.0 imports/ layout
        const auto src = juce::File::getCurrentWorkingDirectory().getChildFile(args[1]);
        const auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mnm-libtool-" + newId());
        const auto srcBytes = readAll(src);
        int fails = 0;
        auto check = [&](bool ok, const char* what) { if (!ok) { ++fails; std::printf("  FAIL: %s\n", what); } };
        {
            Store store(tmp.getChildFile("a"));
            ProjectInfo pj;
            check(store.importSysexFile(src, ImportMode::NewProject, {}, &pj).wasOk(), "import");
            Dump v1;
            check(store.loadVersion(pj.id, 1, v1), "load v1");
            check(encodeDump(v1) == srcBytes, "v1 re-encodes to the source bytes");
            Dump edited = v1;
            int used = -1; for (int i = 0; i < 128 && used < 0; ++i) if (mnm::project::patternInUse(edited, i)) used = i;
            mnm::project::clearPattern(edited, used);
            check(store.addVersion(pj.id, edited, "saved", "Edited: 1 change", {"cleared a pattern"}, "note", 1).wasOk(), "add v2");
            const auto exp = tmp.getChildFile("whole.syx");
            check(store.exportVersion(pj.id, 1, exp, nullptr, nullptr).wasOk(), "export v1");
            check(readAll(exp) == srcBytes, "the whole export of v1 is the source, byte for byte");
            check(store.restoreVersion(pj.id, 1).wasOk(), "restore v1");
            check(store.loadProject(pj.id, pj) && pj.versions.size() == 4 && pj.current()->kind == "restored" && pj.current()->stateOf == 1, "four versions, the newest restored from v1");
            Dump cur; check(store.loadVersion(pj.id, pj.current()->n, cur) && mnm::project::diffDumps(cur, v1).identical(), "the restored state equals v1");
            Dump v2; check(store.loadVersion(pj.id, 2, v2) && mnm::project::diffDumps(v2, v1).patterns.size() == 1, "v2 differs from v1 in one pattern");
            const auto sim = store.findSimilar(edited);
            check(sim && sim->projectId == pj.id && sim->diff.similarity() > 0.95, "the edited dump is recognised as this project");
            const auto part = tmp.getChildFile("part.syx");
            const std::vector<int> kits{v1.patternAt(used)->kit}, pats{used};
            check(store.exportVersion(pj.id, 1, part, &kits, &pats).wasOk(), "partial export");
            const auto pb = readAll(part); const auto pd = parseDump(pb.data(), pb.size(), "part");
            check(pd.kits.size() == 1 && pd.patterns.size() == 1 && pd.numDamaged == 0, "the partial export holds one kit and one pattern");
            SavedItem item; item.name = "TEST LEAD"; item.savedFrom = "Monomodule One"; item.tags = {"lead"}; item.kit = v1.kits[0]; item.track = 0;
            juce::String itemId;
            check(store.saveItem(item, &itemId).wasOk() && store.listSavedItems().size() == 1 && store.listSavedItems()[0].name == "TEST LEAD", "saved item round trip");
            UserData u; u.favourites.add("abc"); u.tags["abc"] = {"lead", "bright"};
            check(store.saveUser(u) && store.loadUser().isFavourite("abc") && store.loadUser().tags["abc"].size() == 2, "user data round trip");
            std::printf("  project %s: %zu versions, dump.json %lld bytes\n", pj.id.toRawUTF8(), pj.versions.size(), (long long) pj.versions.back().dir.getChildFile("dump.json").getSize());
        }
        {   // migration: fabricate the old layout from the source file
            const auto root = tmp.getChildFile("b");
            const auto old = root.getChildFile("imports").getChildFile("0oldimport00001");
            old.createDirectory();
            old.getChildFile("original.syx").replaceWithData(srcBytes.data(), srcBytes.size());
            const auto d = parseDump(srcBytes.data(), srcBytes.size(), "Old");
            old.getChildFile("dump.json").replaceWithText(juce::JSON::toString(dumpToJson(d), false));
            old.getChildFile("import.json").replaceWithText("{\"name\": \"Old Import\", \"sourceFile\": \"/x/old.syx\", \"importedAt\": \"2026-09-10T09:12:00.000+00:00\", \"kits\": 128, \"patterns\": 128}");
            Store store(root);
            const auto list = store.listProjects();
            check(list.size() == 1 && list[0].name == "Old Import" && list[0].versions.size() == 1 && list[0].versions[0].kind == "imported", "the old import became a project with one version");
            Dump back; check(!list.empty() && store.loadVersion(list[0].id, 1, back) && encodeDump(back) == srcBytes, "the migrated state is lossless");
            check(root.getChildFile("imports-migrated").getChildFile("0oldimport00001").getChildFile("original.syx").existsAsFile(), "the old folder is kept aside, not deleted");
            check(!root.getChildFile("imports").exists(), "imports/ is gone once empty");
        }
        tmp.deleteRecursively();
        std::printf("SELFTEST %s\n", fails == 0 ? "OK" : "FAIL");
        return fails == 0 ? 0 : 1;
    }

    Store store;
    if (cmd == "import" && args.size() >= 2) {
        ProjectInfo p;
        const bool pack = args.size() == 3 && args[2] == "pack";
        const auto r = store.importSysexFile(juce::File::getCurrentWorkingDirectory().getChildFile(args[1]), pack ? ImportMode::Pack : args.size() == 3 ? ImportMode::NewVersion : ImportMode::NewProject, pack ? juce::String() : args.size() == 3 ? args[2] : juce::String(), &p);
        if (!r.wasOk()) { std::printf("%s\n", r.getErrorMessage().toRawUTF8()); return 1; }
        std::printf("%s  %s  %s (%d named kits, %d used patterns, %d songs, %d globals, %d damaged)\n", p.id.toRawUTF8(), p.name.toRawUTF8(), p.current()->label().toRawUTF8(), p.current()->namedKits, p.current()->usedPatterns, p.current()->songs, p.current()->globals, p.current()->damaged);
        return 0;
    }
    if (cmd == "list") {
        for (const auto& p : store.listProjects()) {
            std::printf("%s  %-28s%s\n", p.id.toRawUTF8(), p.name.toRawUTF8(), p.pack ? "  (pack)" : "");
            for (const auto& v : p.versions) std::printf("      %-4s %-9s %s  %s\n", v.label().toRawUTF8(), v.kind.toRawUTF8(), v.time.toString(true, true, false).toRawUTF8(), v.title.toRawUTF8());
        }
        std::printf("saved items: %zu\nlibrary: %s\n", store.listSavedItems().size(), store.root().getFullPathName().toRawUTF8());
        return 0;
    }
    auto versionArg = [&](const ProjectInfo& p, int index) { return args.size() > index && args[index].startsWithChar('v') ? args[index].substring(1).getIntValue() : p.current()->n; };
    if (cmd == "show" && args.size() >= 2) {
        ProjectInfo p; Dump d;
        if (!store.loadProject(args[1], p) || p.versions.empty() || !store.loadVersion(p.id, p.current()->n, d)) { std::printf("no project %s\n", args[1].toRawUTF8()); return 1; }
        if (args.size() == 4 && args[2] == "kit") { if (const auto* k = d.kitAt(args[3].getIntValue())) showKit(*k); else std::printf("no kit %s\n", args[3].toRawUTF8()); return 0; }
        if (args.size() == 4 && args[2] == "pattern") { if (const auto* pt = d.patternAt(args[3].getIntValue())) showPattern(*pt); else std::printf("no pattern %s\n", args[3].toRawUTF8()); return 0; }
        std::printf("%s %s: %zu kits, %zu patterns, %d songs, %d globals, %d damaged\n", p.name.toRawUTF8(), p.current()->label().toRawUTF8(), d.kits.size(), d.patterns.size(), d.numSongs, d.numGlobals, d.numDamaged);
        for (const auto& k : d.kits) if (!k.isEmptySlot()) std::printf("  kit %3d %s\n", k.position, k.name.c_str());
        return 0;
    }
    if (cmd == "export" && args.size() >= 3) {   // export <project> <out.syx> [vN]: the whole state, recorded as a version
        ProjectInfo p;
        if (!store.loadProject(args[1], p) || p.versions.empty()) { std::printf("no project %s\n", args[1].toRawUTF8()); return 1; }
        const auto f = juce::File::getCurrentWorkingDirectory().getChildFile(args[2]);
        const auto r = store.exportVersion(p.id, versionArg(p, 3), f, nullptr, nullptr);
        if (r.failed()) { std::printf("%s\n", r.getErrorMessage().toRawUTF8()); return 1; }
        std::printf("%s: %lld bytes, recorded in the history\n", f.getFullPathName().toRawUTF8(), (long long) f.getSize());
        return 0;
    }
    std::printf("unknown command\n");
    return 1;
}
