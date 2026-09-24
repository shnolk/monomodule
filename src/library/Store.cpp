#include "Store.h"
#include "host/Machines.h"
#include <cstring>

namespace mnm::library {

using namespace mnm::dump;

// ---------------------------------------------------------------------------
// Names

const char* fxInputName(int input)
{
    static const char* names[7] = {"NEIBOR", "INP A", "INP B", "INP AB", "BUS AB", "BUS CD", "BUS EF"};
    return input >= 0 && input < 7 ? names[input] : "?";
}

juce::String outBusName(int mask)
{
    juce::StringArray parts;
    if (mask & 1) parts.add("AB");
    if (mask & 2) parts.add("CD");
    if (mask & 4) parts.add("EF");
    return parts.isEmpty() ? juce::String("-") : parts.joinIntoString("+");
}

const char* assignSourceName(int s)
{
    static const char* names[6] = {"+PB", "-PB", "+MW", "-MW", "VEL", "KEY"};
    return s >= 0 && s < 6 ? names[s] : "?";
}

static juce::String machineNameOf(uint8_t model)
{
    if (const auto* def = host::machineDef(host::Machine(model))) return def->name;
    return "MODEL " + juce::String(int(model));
}

// ---------------------------------------------------------------------------
// JSON helpers

namespace {

juce::var ints(const uint8_t* p, int n) { juce::Array<juce::var> a; for (int i = 0; i < n; ++i) a.add(int(p[i])); return a; }
juce::var intsS(const int8_t* p, int n) { juce::Array<juce::var> a; for (int i = 0; i < n; ++i) a.add(int(p[i])); return a; }
juce::String hex(const uint8_t* p, size_t n) { return juce::String::toHexString(p, int(n), 0); }
juce::String hex64(uint64_t v) { return juce::String::toHexString(juce::int64(v)).paddedLeft('0', 16); }
juce::var hex64s(const uint64_t* p, int n) { juce::Array<juce::var> a; for (int i = 0; i < n; ++i) a.add(hex64(p[i])); return a; }

bool readInts(const juce::var& v, uint8_t* out, int n)
{
    const auto* a = v.getArray();
    if (!a || a->size() != n) return false;
    for (int i = 0; i < n; ++i) out[i] = uint8_t(int((*a)[i]));
    return true;
}
bool readIntsS(const juce::var& v, int8_t* out, int n)
{
    const auto* a = v.getArray();
    if (!a || a->size() != n) return false;
    for (int i = 0; i < n; ++i) out[i] = int8_t(int((*a)[i]));
    return true;
}
bool readHex(const juce::var& v, uint8_t* out, size_t n)
{
    const juce::MemoryBlock mb = [&] { juce::MemoryBlock b; b.loadFromHexString(v.toString()); return b; }();
    if (mb.getSize() != n) return false;
    std::memcpy(out, mb.getData(), n);
    return true;
}
std::vector<uint8_t> readHexVec(const juce::var& v)
{
    juce::MemoryBlock mb;
    mb.loadFromHexString(v.toString());
    return std::vector<uint8_t>(static_cast<const uint8_t*>(mb.getData()), static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
}
bool readHex64s(const juce::var& v, uint64_t* out, int n)
{
    const auto* a = v.getArray();
    if (!a || a->size() != n) return false;
    for (int i = 0; i < n; ++i) out[i] = uint64_t((*a)[i].toString().getHexValue64());
    return true;
}
int geti(const juce::DynamicObject* o, const char* name, int def = 0) { return o->hasProperty(name) ? int(o->getProperty(name)) : def; }

} // namespace

// ---------------------------------------------------------------------------
// Kit <-> JSON

juce::var kitToJson(const Kit& k)
{
    auto* kv = new juce::DynamicObject();
    kv->setProperty("pos", k.position);
    kv->setProperty("name", juce::String(k.name));
    kv->setProperty("nameRaw", hex(k.nameRaw, 11));
    kv->setProperty("version", int(k.version));
    kv->setProperty("revision", int(k.revision));
    kv->setProperty("extendedPosition", k.extendedPosition);
    kv->setProperty("unused461", int(k.unused461));
    juce::Array<juce::var> tracks;
    static const char* pages[9] = {"syn", "amp", "filt", "efx", "lfo1", "lfo2", "lfo3", "midi", "extra"};
    for (int t = 0; t < 6; ++t) {
        const auto& tr = k.tracks[t];
        auto* tv = new juce::DynamicObject();
        tv->setProperty("machine", machineNameOf(tr.model));
        tv->setProperty("model", int(tr.model));
        tv->setProperty("type", int(tr.type));
        tv->setProperty("level", int(tr.level));
        tv->setProperty("outBuses", outBusName(k.outBuses(t)));
        tv->setProperty("fxInput", fxInputName(k.fxInput(t)));
        tv->setProperty("lpKeyTrack", k.lpKeyTracks(t));
        tv->setProperty("hpKeyTrack", k.hpKeyTracks(t));
        for (int p = 0; p < 9; ++p) tv->setProperty(pages[p], ints(tr.params + 8 * p, 8));
        juce::Array<juce::var> assign;
        for (int s = 0; s < 6; ++s) {
            auto* av = new juce::DynamicObject();
            av->setProperty("source", assignSourceName(s));
            av->setProperty("page", juce::Array<juce::var>{int(tr.destPage[s][0]), int(tr.destPage[s][1])});
            av->setProperty("param", juce::Array<juce::var>{int(tr.destParam[s][0]), int(tr.destParam[s][1])});
            av->setProperty("add", juce::Array<juce::var>{int(tr.destRange[s][0]), int(tr.destRange[s][1])});
            assign.add(juce::var(av));
        }
        tv->setProperty("assign", assign);
        tracks.add(juce::var(tv));
    }
    kv->setProperty("tracks", tracks);
    kv->setProperty("patchBusIn", int(k.patchBusIn));
    kv->setProperty("mirrorLR", int(k.mirrorLR));
    kv->setProperty("mirrorUD", int(k.mirrorUD));
    kv->setProperty("lpKeyTrackBits", int(k.lpKeyTrack));
    kv->setProperty("hpKeyTrackBits", int(k.hpKeyTrack));
    kv->setProperty("trigPortamento", int(k.trigPortamento));
    kv->setProperty("trigTracks", ints(k.trigTracks, 6));
    kv->setProperty("trigLegato", juce::Array<juce::var>{int(k.trigLegatoAmp), int(k.trigLegatoFilter), int(k.trigLegatoLFO)});
    kv->setProperty("multimode", int(k.commonMultimode));
    kv->setProperty("timing", int(k.commonTiming));
    kv->setProperty("splitKey", int(k.splitKey));
    kv->setProperty("splitRange", int(k.splitRange));
    if (!k.tail.empty()) kv->setProperty("tail", hex(k.tail.data(), k.tail.size()));
    return juce::var(kv);
}

bool kitFromJson(const juce::var& v, Kit& k)
{
    const auto* kv = v.getDynamicObject();
    if (!kv) return false;
    k = Kit();
    k.position = geti(kv, "pos");
    k.name = kv->getProperty("name").toString().toStdString();
    if (!readHex(kv->getProperty("nameRaw"), k.nameRaw, 11)) {   // older JSON: rebuild the wire name from the string
        std::memset(k.nameRaw, 0, 11);
        std::memcpy(k.nameRaw, k.name.data(), std::min<size_t>(11, k.name.size()));
    }
    k.version = uint8_t(geti(kv, "version", 2));
    k.revision = uint8_t(geti(kv, "revision", 1));
    k.extendedPosition = bool(kv->getProperty("extendedPosition"));
    k.unused461 = uint8_t(geti(kv, "unused461"));
    const auto* tracks = kv->getProperty("tracks").getArray();
    if (!tracks || tracks->size() != 6) return false;
    static const char* pages[9] = {"syn", "amp", "filt", "efx", "lfo1", "lfo2", "lfo3", "midi", "extra"};
    for (int t = 0; t < 6; ++t) {
        const auto* tv = (*tracks)[t].getDynamicObject();
        if (!tv) return false;
        auto& tr = k.tracks[t];
        tr.model = uint8_t(geti(tv, "model"));
        tr.type = uint8_t(geti(tv, "type"));
        tr.level = uint8_t(geti(tv, "level"));
        for (int p = 0; p < 9; ++p)
            if (!readInts(tv->getProperty(pages[p]), tr.params + 8 * p, 8)) return false;
        if (const auto* assign = tv->getProperty("assign").getArray(); assign && assign->size() == 6)
            for (int s = 0; s < 6; ++s) {
                const auto* av = (*assign)[s].getDynamicObject();
                if (!av) return false;
                const auto *pg = av->getProperty("page").getArray(), *pr = av->getProperty("param").getArray(), *ad = av->getProperty("add").getArray();
                if (!pg || !pr || !ad || pg->size() != 2 || pr->size() != 2 || ad->size() != 2) return false;
                for (int slot = 0; slot < 2; ++slot) {
                    tr.destPage[s][slot] = uint8_t(int((*pg)[slot]));
                    tr.destParam[s][slot] = uint8_t(int((*pr)[slot]));
                    tr.destRange[s][slot] = int8_t(int((*ad)[slot]));
                }
            }
    }
    k.patchBusIn = uint16_t(geti(kv, "patchBusIn"));
    k.mirrorLR = uint8_t(geti(kv, "mirrorLR"));
    k.mirrorUD = uint8_t(geti(kv, "mirrorUD"));
    k.lpKeyTrack = uint8_t(geti(kv, "lpKeyTrackBits"));
    k.hpKeyTrack = uint8_t(geti(kv, "hpKeyTrackBits"));
    k.trigPortamento = uint8_t(geti(kv, "trigPortamento"));
    readInts(kv->getProperty("trigTracks"), k.trigTracks, 6);
    if (const auto* leg = kv->getProperty("trigLegato").getArray(); leg && leg->size() == 3) {
        k.trigLegatoAmp = uint8_t(int((*leg)[0]));
        k.trigLegatoFilter = uint8_t(int((*leg)[1]));
        k.trigLegatoLFO = uint8_t(int((*leg)[2]));
    }
    k.commonMultimode = uint8_t(geti(kv, "multimode"));
    k.commonTiming = uint8_t(geti(kv, "timing"));
    k.splitKey = uint8_t(geti(kv, "splitKey"));
    k.splitRange = uint8_t(geti(kv, "splitRange"));
    if (kv->hasProperty("tail")) k.tail = readHexVec(kv->getProperty("tail"));
    return true;
}

// ---------------------------------------------------------------------------
// Pattern <-> JSON

juce::var patternToJson(const Pattern& p)
{
    auto* pv = new juce::DynamicObject();
    pv->setProperty("pos", p.position);
    pv->setProperty("slot", juce::String(patternSlotName(p.position)));
    pv->setProperty("version", int(p.version));
    pv->setProperty("revision", int(p.revision));
    pv->setProperty("len", int(p.patternLength));
    pv->setProperty("doubleTempo", int(p.doubleTempo));
    pv->setProperty("kit", int(p.kit));
    pv->setProperty("transposeAll", int(p.patternTranspose));
    pv->setProperty("swing", int(p.swingAmount));
    auto* trigs = new juce::DynamicObject();
    trigs->setProperty("amp", hex64s(p.ampTrigs, 6));
    trigs->setProperty("filter", hex64s(p.filterTrigs, 6));
    trigs->setProperty("lfo", hex64s(p.lfoTrigs, 6));
    trigs->setProperty("off", hex64s(p.offTrigs, 6));
    trigs->setProperty("midiNoteOn", hex64s(p.midiNoteOnTrigs, 6));
    trigs->setProperty("midiNoteOff", hex64s(p.midiNoteOffTrigs, 6));
    trigs->setProperty("trigless", hex64s(p.triglessTrigs, 6));
    trigs->setProperty("chord", hex64s(p.chordTrigs, 6));
    trigs->setProperty("midiTrigless", hex64s(p.midiTriglessTrigs, 6));
    trigs->setProperty("slide", hex64s(p.slidePatterns, 6));
    trigs->setProperty("swing", hex64s(p.swingPatterns, 6));
    trigs->setProperty("midiSlide", hex64s(p.midiSlidePatterns, 6));
    trigs->setProperty("midiSwing", hex64s(p.midiSwingPatterns, 6));
    pv->setProperty("trigs", juce::var(trigs));
    pv->setProperty("lockPatterns", hex64s(p.lockPatterns, 6));
    juce::Array<juce::var> notes;
    for (int t = 0; t < 6; ++t) notes.add(ints(p.noteNBR[t], 64));
    pv->setProperty("notes", notes);
    pv->setProperty("transpose", intsS(p.transpose, 6));
    pv->setProperty("scale", ints(p.scale, 6));
    pv->setProperty("key", ints(p.key, 6));
    pv->setProperty("midiTranspose", intsS(p.midiTranspose, 6));
    pv->setProperty("midiScale", ints(p.midiScale, 6));
    pv->setProperty("midiKey", ints(p.midiKey, 6));
    auto arp = [&](bool midi) {
        auto* a = new juce::DynamicObject();
        a->setProperty("play", ints(midi ? p.midiArpPlay : p.arpPlay, 6));
        a->setProperty("mode", ints(midi ? p.midiArpMode : p.arpMode, 6));
        a->setProperty("octave", ints(midi ? p.midiArpOctaveRange : p.arpOctaveRange, 6));
        a->setProperty("mult", ints(midi ? p.midiArpMultiplier : p.arpMultiplier, 6));
        if (!midi) a->setProperty("dest", ints(p.arpDestination, 6));
        a->setProperty("length", ints(midi ? p.midiArpLength : p.arpLength, 6));
        juce::Array<juce::var> pats;
        for (int t = 0; t < 6; ++t) pats.add(ints(midi ? p.midiArpPattern[t] : p.arpPattern[t], 16));
        a->setProperty("pattern", pats);
        return juce::var(a);
    };
    pv->setProperty("arp", arp(false));
    pv->setProperty("midiArp", arp(true));
    pv->setProperty("unused", hex(p.unused, 4));
    pv->setProperty("midiNotesUsed", int(p.midiNotesUsed));
    pv->setProperty("chordNotesUsed", int(p.chordNotesUsed));
    pv->setProperty("unused2", int(p.unused2));
    pv->setProperty("locksUsed", int(p.locksUsed));
    // canonical sparse locks, plus the stale wire bytes of unassigned rows so the encoding stays identical
    juce::Array<juce::var> locks;
    auto* stale = new juce::DynamicObject();
    for (int r = 0; r < 62; ++r) {
        if (p.lockTracks[r] < 0) {
            bool differs = false;
            for (int j = 0; j < 64; ++j) differs = differs || p.locksRaw[r][j] != 255;
            if (differs) stale->setProperty(juce::String(r), hex(p.locksRaw[r], 64));
            continue;
        }
        auto* lv = new juce::DynamicObject();
        lv->setProperty("t", int(p.lockTracks[r]));
        lv->setProperty("p", int(p.lockParams[r]));
        auto* steps = new juce::DynamicObject();
        for (int j = 0; j < 64; ++j)
            if (p.locksRaw[r][j] != 255) steps->setProperty(juce::String(j), int(p.locksRaw[r][j]));
        lv->setProperty("steps", juce::var(steps));
        locks.add(juce::var(lv));
    }
    pv->setProperty("locks", locks);
    if (stale->getProperties().size() > 0) pv->setProperty("staleLockRows", juce::var(stale));
    uint8_t mn[800], cn[384];
    for (int i = 0; i < 400; ++i) { mn[2 * i] = uint8_t(p.midiNotes[i] >> 8); mn[2 * i + 1] = uint8_t(p.midiNotes[i]); }
    for (int i = 0; i < 192; ++i) { cn[2 * i] = uint8_t(p.chordNotes[i] >> 8); cn[2 * i + 1] = uint8_t(p.chordNotes[i]); }
    pv->setProperty("midiNotes", hex(mn, 800));
    pv->setProperty("chordNotes", hex(cn, 384));
    pv->setProperty("trailing", int(p.trailing));
    if (!p.tail.empty()) pv->setProperty("tail", hex(p.tail.data(), p.tail.size()));
    return juce::var(pv);
}

bool patternFromJson(const juce::var& v, Pattern& p)
{
    const auto* pv = v.getDynamicObject();
    if (!pv) return false;
    p = Pattern();
    std::memset(p.locksRaw, 255, sizeof(p.locksRaw));
    p.position = geti(pv, "pos");
    p.version = uint8_t(geti(pv, "version", 6));
    p.revision = uint8_t(geti(pv, "revision", 1));
    p.patternLength = uint8_t(geti(pv, "len", 16));
    p.doubleTempo = uint8_t(geti(pv, "doubleTempo"));
    p.kit = uint8_t(geti(pv, "kit"));
    p.patternTranspose = int8_t(geti(pv, "transposeAll"));
    p.swingAmount = uint32_t(geti(pv, "swing"));
    const auto* trigs = pv->getProperty("trigs").getDynamicObject();
    if (!trigs) return false;
    if (!readHex64s(trigs->getProperty("amp"), p.ampTrigs, 6)) return false;
    readHex64s(trigs->getProperty("filter"), p.filterTrigs, 6);
    readHex64s(trigs->getProperty("lfo"), p.lfoTrigs, 6);
    readHex64s(trigs->getProperty("off"), p.offTrigs, 6);
    readHex64s(trigs->getProperty("midiNoteOn"), p.midiNoteOnTrigs, 6);
    readHex64s(trigs->getProperty("midiNoteOff"), p.midiNoteOffTrigs, 6);
    readHex64s(trigs->getProperty("trigless"), p.triglessTrigs, 6);
    readHex64s(trigs->getProperty("chord"), p.chordTrigs, 6);
    readHex64s(trigs->getProperty("midiTrigless"), p.midiTriglessTrigs, 6);
    readHex64s(trigs->getProperty("slide"), p.slidePatterns, 6);
    readHex64s(trigs->getProperty("swing"), p.swingPatterns, 6);
    readHex64s(trigs->getProperty("midiSlide"), p.midiSlidePatterns, 6);
    readHex64s(trigs->getProperty("midiSwing"), p.midiSwingPatterns, 6);
    readHex64s(pv->getProperty("lockPatterns"), p.lockPatterns, 6);
    if (const auto* notes = pv->getProperty("notes").getArray(); notes && notes->size() == 6)
        for (int t = 0; t < 6; ++t)
            if (!readInts((*notes)[t], p.noteNBR[t], 64)) return false;
    readIntsS(pv->getProperty("transpose"), p.transpose, 6);
    readInts(pv->getProperty("scale"), p.scale, 6);
    readInts(pv->getProperty("key"), p.key, 6);
    readIntsS(pv->getProperty("midiTranspose"), p.midiTranspose, 6);
    readInts(pv->getProperty("midiScale"), p.midiScale, 6);
    readInts(pv->getProperty("midiKey"), p.midiKey, 6);
    auto arp = [&](bool midi) {
        const auto* a = pv->getProperty(midi ? "midiArp" : "arp").getDynamicObject();
        if (!a) return;
        readInts(a->getProperty("play"), midi ? p.midiArpPlay : p.arpPlay, 6);
        readInts(a->getProperty("mode"), midi ? p.midiArpMode : p.arpMode, 6);
        readInts(a->getProperty("octave"), midi ? p.midiArpOctaveRange : p.arpOctaveRange, 6);
        readInts(a->getProperty("mult"), midi ? p.midiArpMultiplier : p.arpMultiplier, 6);
        if (!midi) readInts(a->getProperty("dest"), p.arpDestination, 6);
        readInts(a->getProperty("length"), midi ? p.midiArpLength : p.arpLength, 6);
        if (const auto* pats = a->getProperty("pattern").getArray(); pats && pats->size() == 6)
            for (int t = 0; t < 6; ++t) readInts((*pats)[t], midi ? p.midiArpPattern[t] : p.arpPattern[t], 16);
    };
    arp(false); arp(true);
    readHex(pv->getProperty("unused"), p.unused, 4);
    p.midiNotesUsed = uint16_t(geti(pv, "midiNotesUsed"));
    p.chordNotesUsed = uint8_t(geti(pv, "chordNotesUsed"));
    p.unused2 = uint8_t(geti(pv, "unused2"));
    p.locksUsed = uint8_t(geti(pv, "locksUsed"));
    // rows are allocated in lockPatterns order; the JSON lists them in that order
    if (const auto* locks = pv->getProperty("locks").getArray()) {
        int r = 0;
        for (const auto& lvv : *locks) {
            if (r >= 62) break;
            const auto* lv = lvv.getDynamicObject();
            if (!lv) continue;
            if (const auto* steps = lv->getProperty("steps").getDynamicObject())
                for (const auto& prop : steps->getProperties()) {
                    const int j = prop.name.toString().getIntValue();
                    if (j >= 0 && j < 64) p.locksRaw[r][j] = uint8_t(int(prop.value));
                }
            ++r;
        }
    }
    if (const auto* stale = pv->getProperty("staleLockRows").getDynamicObject())
        for (const auto& prop : stale->getProperties()) {
            const int r = prop.name.toString().getIntValue();
            if (r >= 0 && r < 62) readHex(prop.value, p.locksRaw[r], 64);
        }
    uint8_t mn[800] = {}, cn[384] = {};
    readHex(pv->getProperty("midiNotes"), mn, 800);
    readHex(pv->getProperty("chordNotes"), cn, 384);
    for (int i = 0; i < 400; ++i) p.midiNotes[i] = uint16_t((mn[2 * i] << 8) | mn[2 * i + 1]);
    for (int i = 0; i < 192; ++i) p.chordNotes[i] = uint16_t((cn[2 * i] << 8) | cn[2 * i + 1]);
    p.trailing = uint8_t(geti(pv, "trailing"));
    if (pv->hasProperty("tail")) p.tail = readHexVec(pv->getProperty("tail"));
    p.finalizeLocks();
    return true;
}

// ---------------------------------------------------------------------------
// Dump <-> JSON

juce::var dumpToJson(const Dump& d)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("format", 2);
    root->setProperty("name", juce::String(d.file));
    root->setProperty("songs", d.numSongs);
    root->setProperty("globals", d.numGlobals);
    root->setProperty("unknown", d.numUnknown);
    root->setProperty("damaged", d.numDamaged);
    juce::Array<juce::var> messages;
    for (const auto& m : d.messages) {
        auto* mv = new juce::DynamicObject();
        mv->setProperty("id", int(m.id));
        mv->setProperty("version", int(m.version));
        mv->setProperty("revision", int(m.revision));
        mv->setProperty("position", m.position);
        if (m.kitIndex >= 0) mv->setProperty("kit", m.kitIndex);
        else if (m.patternIndex >= 0) mv->setProperty("pattern", m.patternIndex);
        else {
            if (m.damaged) mv->setProperty("damaged", true);
            mv->setProperty("raw", hex(m.raw.data(), m.raw.size()));
        }
        messages.add(juce::var(mv));
    }
    root->setProperty("messages", messages);
    juce::Array<juce::var> kits;
    for (const auto& k : d.kits) kits.add(kitToJson(k));
    root->setProperty("kits", kits);
    juce::Array<juce::var> pats;
    for (const auto& p : d.patterns) pats.add(patternToJson(p));
    root->setProperty("patterns", pats);
    return juce::var(root);
}

bool dumpFromJson(const juce::var& v, Dump& d)
{
    const auto* root = v.getDynamicObject();
    if (!root || int(root->getProperty("format")) != 2) return false;
    d = Dump();
    d.file = root->getProperty("name").toString().toStdString();
    d.numSongs = geti(root, "songs");
    d.numGlobals = geti(root, "globals");
    d.numUnknown = geti(root, "unknown");
    d.numDamaged = geti(root, "damaged");
    if (const auto* kits = root->getProperty("kits").getArray())
        for (const auto& kv : *kits) { Kit k; if (!kitFromJson(kv, k)) return false; d.kits.push_back(std::move(k)); }
    if (const auto* pats = root->getProperty("patterns").getArray())
        for (const auto& pv : *pats) { Pattern p; if (!patternFromJson(pv, p)) return false; d.patterns.push_back(std::move(p)); }
    if (const auto* messages = root->getProperty("messages").getArray())
        for (const auto& mvv : *messages) {
            const auto* mv = mvv.getDynamicObject();
            if (!mv) return false;
            Message m;
            m.id = uint8_t(geti(mv, "id"));
            m.version = uint8_t(geti(mv, "version"));
            m.revision = uint8_t(geti(mv, "revision"));
            m.position = geti(mv, "position", -1);
            m.damaged = bool(mv->getProperty("damaged"));
            if (mv->hasProperty("kit")) { m.kitIndex = geti(mv, "kit"); if (m.kitIndex < 0 || m.kitIndex >= int(d.kits.size())) return false; m.raw = encodeKit(d.kits[size_t(m.kitIndex)]); }
            else if (mv->hasProperty("pattern")) { m.patternIndex = geti(mv, "pattern"); if (m.patternIndex < 0 || m.patternIndex >= int(d.patterns.size())) return false; m.raw = encodePattern(d.patterns[size_t(m.patternIndex)]); }
            else m.raw = readHexVec(mv->getProperty("raw"));
            d.messages.push_back(std::move(m));
        }
    return true;
}

// ---------------------------------------------------------------------------
// Store

juce::String newId()
{
    static const char* digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    juce::String s;
    juce::int64 ms = juce::Time::currentTimeMillis();
    char buf[16];
    int n = 0;
    while (ms > 0 && n < 15) { buf[n++] = digits[ms % 36]; ms /= 36; }
    while (n < 9) buf[n++] = '0';   // fixed width keeps ids sortable as text
    for (int i = n - 1; i >= 0; --i) s += buf[i];
    auto& rng = juce::Random::getSystemRandom();
    for (int i = 0; i < 6; ++i) s += digits[rng.nextInt(36)];
    return s;
}

juce::File Store::defaultRoot()
{
    if (const char* env = std::getenv("MNM_LIBRARY_DIR"); env && *env) return juce::File(juce::String(env));
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    base = base.getChildFile("Application Support");
#endif
    return base.getChildFile("Shnolk").getChildFile("Monomachine").getChildFile("Library");
}

Store::Store(juce::File root) : m_root(std::move(root)) {}

namespace {
int countUsedPatterns(const Dump& d) { int n = 0; for (const auto& p : d.patterns) if (!p.empty()) ++n; return n; }
int countNamedKits(const Dump& d) { int n = 0; for (const auto& k : d.kits) if (!k.isEmptySlot()) ++n; return n; }
juce::var strings(const juce::StringArray& a) { juce::Array<juce::var> v; for (const auto& s : a) v.add(s); return v; }
juce::StringArray readStrings(const juce::var& v) { juce::StringArray a; if (const auto* arr = v.getArray()) for (const auto& x : *arr) a.add(x.toString()); return a; }
bool writeJson(const juce::File& f, const juce::var& v, bool oneLine = false) { f.getParentDirectory().createDirectory(); return f.replaceWithText(juce::JSON::toString(v, oneLine)); }
void fillCounts(VersionInfo& v, const Dump& d)
{
    v.kits = int(d.kits.size()); v.patterns = int(d.patterns.size()); v.usedPatterns = countUsedPatterns(d); v.namedKits = countNamedKits(d);
    v.songs = d.numSongs; v.globals = d.numGlobals; v.damaged = d.numDamaged; v.unknown = d.numUnknown;
}
} // namespace

juce::StringArray UserData::allTags() const
{
    juce::StringArray all;
    for (const auto& [id, t] : tags) for (const auto& x : t) all.addIfNotAlreadyThere(x);
    all.sort(true);
    return all;
}

// ---------------------------------------------------------------------------
// Projects and versions

bool Store::readVersion(const juce::File& dir, VersionInfo& v) const
{
    auto json = juce::JSON::parse(dir.getChildFile("version.json").loadFileAsString());
    auto* o = json.getDynamicObject();
    if (!o) return false;
    v.n = geti(o, "n", dir.getFileName().getIntValue());
    v.kind = o->getProperty("kind").toString();
    v.title = o->getProperty("title").toString(); v.note = o->getProperty("note").toString();
    v.sourceFile = o->getProperty("sourceFile").toString(); v.exportFile = o->getProperty("exportFile").toString();
    v.changes = readStrings(o->getProperty("changes"));
    v.time = juce::Time::fromISO8601(o->getProperty("time").toString());
    v.parent = geti(o, "parent"); v.stateOf = geti(o, "stateOf", v.n);
    v.kits = geti(o, "kits"); v.patterns = geti(o, "patterns"); v.usedPatterns = geti(o, "usedPatterns"); v.namedKits = geti(o, "namedKits");
    v.songs = geti(o, "songs"); v.globals = geti(o, "globals"); v.damaged = geti(o, "damaged"); v.unknown = geti(o, "unknown");
    v.dir = dir;
    return true;
}

bool Store::loadProject(const juce::String& idRef, ProjectInfo& out) const
{
    const juce::String id = idRef;   // a copy: callers pass out.id, and out is reset below
    const auto dir = projectDir(id);
    auto json = juce::JSON::parse(dir.getChildFile("project.json").loadFileAsString());
    auto* o = json.getDynamicObject();
    if (!o) return false;
    out = {};
    out.id = id; out.dir = dir;
    out.name = o->getProperty("name").toString(); out.device = o->getProperty("device").toString();
    out.pack = bool(o->getProperty("pack"));
    auto dirs = dir.getChildFile("versions").findChildFiles(juce::File::findDirectories, false);
    dirs.sort();
    for (const auto& d : dirs) { VersionInfo v; if (readVersion(d, v)) out.versions.push_back(std::move(v)); }
    std::sort(out.versions.begin(), out.versions.end(), [](const auto& a, const auto& b) { return a.n > b.n; });
    return true;
}

std::vector<ProjectInfo> Store::listProjects()
{
    if (!m_migrated) { m_migrated = true; migrateLegacyLayout(); migrateImports(); }
    std::vector<ProjectInfo> v;
    auto dirs = m_root.getChildFile("projects").findChildFiles(juce::File::findDirectories, false);
    dirs.sort();
    for (const auto& d : dirs) { ProjectInfo p; if (loadProject(d.getFileName(), p) && !p.versions.empty()) v.push_back(std::move(p)); }
    std::reverse(v.begin(), v.end());   // ids are time-ordered: newest first
    return v;
}

bool Store::renameProject(const juce::String& id, const juce::String& name)
{
    const auto f = projectDir(id).getChildFile("project.json");
    auto json = juce::JSON::parse(f.loadFileAsString());
    auto* o = json.getDynamicObject();
    if (!o) return false;
    o->setProperty("name", name);
    return f.replaceWithText(juce::JSON::toString(json));
}

bool Store::deleteProject(const juce::String& id)
{
    const auto dir = projectDir(id);
    return id.isNotEmpty() && dir.isDirectory() && dir.deleteRecursively();
}

int Store::nextVersionNumber(const juce::String& projectId) const
{
    int n = 0;
    for (const auto& d : projectDir(projectId).getChildFile("versions").findChildFiles(juce::File::findDirectories, false)) n = std::max(n, d.getFileName().getIntValue());
    return n + 1;
}

juce::Result Store::writeVersion(const juce::String& projectId, VersionInfo& v, const Dump* state)
{
    v.n = nextVersionNumber(projectId);
    v.dir = versionDir(projectId, v.n);
    if (!v.dir.createDirectory()) return juce::Result::fail("Could not create " + v.dir.getFullPathName());
    if (state) {
        v.stateOf = v.n;
        fillCounts(v, *state);
        if (!v.dir.getChildFile("dump.json").replaceWithText(juce::JSON::toString(dumpToJson(*state), false))) return juce::Result::fail("Could not write dump.json");
    }
    if (v.time == juce::Time()) v.time = juce::Time::getCurrentTime();
    auto* o = new juce::DynamicObject();
    o->setProperty("n", v.n); o->setProperty("kind", v.kind); o->setProperty("title", v.title); o->setProperty("note", v.note);
    o->setProperty("sourceFile", v.sourceFile); o->setProperty("exportFile", v.exportFile); o->setProperty("changes", strings(v.changes));
    o->setProperty("time", v.time.toISO8601(true)); o->setProperty("parent", v.parent); o->setProperty("stateOf", v.stateOf);
    o->setProperty("kits", v.kits); o->setProperty("patterns", v.patterns); o->setProperty("usedPatterns", v.usedPatterns); o->setProperty("namedKits", v.namedKits);
    o->setProperty("songs", v.songs); o->setProperty("globals", v.globals); o->setProperty("damaged", v.damaged); o->setProperty("unknown", v.unknown);
    if (!writeJson(v.dir.getChildFile("version.json"), juce::var(o))) return juce::Result::fail("Could not write version.json");
    projectDir(projectId).getChildFile("project.json").setLastModificationTime(juce::Time::getCurrentTime());   // pollers watch this
    return juce::Result::ok();
}

juce::Result Store::importSysexFile(const juce::File& syx, ImportMode mode, const juce::String& projectId, ProjectInfo* out)
{
    juce::MemoryBlock mb;
    if (!syx.loadFileAsData(mb)) return juce::Result::fail("Could not read " + syx.getFileName());
    return importSysexData(mb.getData(), mb.getSize(), syx.getFileNameWithoutExtension(), syx.getFullPathName(), mode, projectId, out);
}

juce::Result Store::importSysexData(const void* data, size_t size, const juce::String& name, const juce::String& sourceFile,
                                    ImportMode mode, const juce::String& projectId, ProjectInfo* out)
{
    auto d = parseDump(static_cast<const uint8_t*>(data), size, name.toStdString());
    if (d.messages.empty() || (d.kits.empty() && d.patterns.empty() && d.numSongs == 0 && d.numGlobals == 0 && d.numDamaged == 0))
        return juce::Result::fail(name + " does not look like a Monomachine sysex dump.");
    juce::String pid = projectId;
    VersionInfo v;
    v.kind = "imported"; v.sourceFile = sourceFile;
    v.title = "Imported " + (sourceFile.isNotEmpty() ? juce::File(sourceFile).getFileName() : name);
    if (mode == ImportMode::NewVersion) {
        ProjectInfo p;
        if (!loadProject(pid, p) || p.versions.empty()) return juce::Result::fail("No such project");
        v.parent = p.versions.front().n;
        Dump prev;
        if (loadVersion(pid, v.parent, prev)) {   // what the unit changed since
            const auto diff = mnm::project::diffDumps(prev, d);
            juce::StringArray ks, ps;
            for (int k : diff.kits) ks.add(juce::String(k + 1).paddedLeft('0', 3));
            for (int pp : diff.patterns) ps.add(juce::String(patternSlotName(pp)));
            if (diff.identical()) v.changes.add("Identical to " + p.versions.front().label());
            if (!ks.isEmpty()) v.changes.add(juce::String(ks.size()) + (ks.size() == 1 ? " kit differs: " : " kits differ: ") + ks.joinIntoString(", "));
            if (!ps.isEmpty()) v.changes.add(juce::String(ps.size()) + (ps.size() == 1 ? " pattern differs: " : " patterns differ: ") + ps.joinIntoString(", "));
        }
    } else {
        pid = newId();
        auto* o = new juce::DynamicObject();
        o->setProperty("name", name); o->setProperty("device", juce::String()); o->setProperty("pack", mode == ImportMode::Pack);
        o->setProperty("createdAt", juce::Time::getCurrentTime().toISO8601(true));
        if (!writeJson(projectDir(pid).getChildFile("project.json"), juce::var(o))) return juce::Result::fail("Could not create the project");
    }
    if (v.changes.isEmpty())
        v.changes.add(juce::String(countNamedKits(d)) + " named kits, " + juce::String(countUsedPatterns(d)) + " used patterns, " + juce::String(d.numSongs) + " songs, " + juce::String(d.numGlobals) + " globals");
    auto r = writeVersion(pid, v, &d);
    if (r.failed()) return r;
    if (!v.dir.getChildFile("original.syx").replaceWithData(data, size)) return juce::Result::fail("Could not archive the original dump");
    if (out) loadProject(pid, *out);
    return juce::Result::ok();
}

std::optional<Store::Similar> Store::findSimilar(const Dump& dump)
{
    std::optional<Similar> best;
    for (const auto& p : listProjects()) {
        if (p.pack) continue;
        Dump cur;
        if (!loadVersion(p.id, p.versions.front().n, cur)) continue;
        auto diff = mnm::project::diffDumps(cur, dump);
        if (diff.similarity() >= 0.5 && (!best || diff.similarity() > best->diff.similarity())) best = Similar{p.id, p.name, p.versions.front().n, std::move(diff)};
    }
    return best;
}

bool Store::loadVersion(const juce::String& projectId, int n, Dump& out) const
{
    VersionInfo v;
    if (!readVersion(versionDir(projectId, n), v)) return false;
    const auto dir = versionDir(projectId, v.stateOf > 0 ? v.stateOf : n);
    if (dumpFromJson(juce::JSON::parse(dir.getChildFile("dump.json").loadFileAsString()), out)) return true;
    juce::MemoryBlock mb;   // fall back to the archived bytes (a damaged dump.json)
    if (!dir.getChildFile("original.syx").loadFileAsData(mb)) return false;
    out = parseDump(static_cast<const uint8_t*>(mb.getData()), mb.getSize(), projectId.toStdString());
    return true;
}

juce::File Store::originalFile(const juce::String& projectId, int n) const { return versionDir(projectId, n).getChildFile("original.syx"); }

juce::Result Store::addVersion(const juce::String& projectId, const Dump& state, const juce::String& kind, const juce::String& title,
                               const juce::StringArray& changes, const juce::String& note, int parent, VersionInfo* out)
{
    if (!projectDir(projectId).isDirectory()) return juce::Result::fail("No such project");
    VersionInfo v;
    v.kind = kind; v.title = title; v.changes = changes; v.note = note; v.parent = parent;
    auto r = writeVersion(projectId, v, &state);
    if (r.wasOk() && out) *out = v;
    return r;
}

juce::Result Store::restoreVersion(const juce::String& projectId, int n, VersionInfo* out)
{
    VersionInfo old;
    if (!readVersion(versionDir(projectId, n), old)) return juce::Result::fail("No such version");
    ProjectInfo p;
    loadProject(projectId, p);
    VersionInfo v = old;
    v.kind = "restored"; v.title = "Restored " + old.label(); v.note.clear(); v.exportFile.clear(); v.sourceFile.clear(); v.time = {};
    v.changes = {"Equal to " + old.label() + ": " + old.title};
    v.parent = p.versions.empty() ? n : p.versions.front().n;
    v.stateOf = old.stateOf > 0 ? old.stateOf : n;
    auto r = writeVersion(projectId, v, nullptr);
    if (r.wasOk() && out) *out = v;
    return r;
}

int Store::lastExportedOrImported(const ProjectInfo& p) const
{
    for (const auto& v : p.versions) if (v.kind == "exported" || v.kind == "imported") return v.n;
    return p.versions.empty() ? 0 : p.versions.back().n;
}

juce::Result Store::exportVersion(const juce::String& projectId, int n, const juce::File& dest, const std::vector<int>* kits,
                                  const std::vector<int>* patterns, VersionInfo* out)
{
    VersionInfo src;
    Dump d;
    if (!readVersion(versionDir(projectId, n), src) || !loadVersion(projectId, n, d)) return juce::Result::fail("No such version");
    const bool whole = kits == nullptr && patterns == nullptr;
    const auto bytes = whole ? encodeDump(d) : mnm::project::encodeSlots(d, kits ? *kits : std::vector<int>{}, patterns ? *patterns : std::vector<int>{});
    if (bytes.empty()) return juce::Result::fail("Nothing to export");
    if (!dest.replaceWithData(bytes.data(), bytes.size())) return juce::Result::fail("Could not write " + dest.getFullPathName());
    ProjectInfo p;
    loadProject(projectId, p);
    VersionInfo v = src;
    v.kind = "exported"; v.title = "Exported to " + dest.getFileName(); v.note.clear(); v.sourceFile.clear(); v.time = {};
    v.exportFile = dest.getFullPathName();
    v.parent = p.versions.empty() ? n : p.versions.front().n;
    v.stateOf = src.stateOf > 0 ? src.stateOf : n;
    v.changes.clear();
    if (whole) v.changes.add("The whole project: every kit, pattern, song and global");
    else {
        juce::StringArray ks, ps;
        if (kits) for (int k : *kits) ks.add(juce::String(k + 1).paddedLeft('0', 3));
        if (patterns) for (int pp : *patterns) ps.add(juce::String(patternSlotName(pp)));
        if (!ks.isEmpty()) v.changes.add("Kits " + ks.joinIntoString(", "));
        if (!ps.isEmpty()) v.changes.add("Patterns " + ps.joinIntoString(", "));
    }
    auto r = writeVersion(projectId, v, nullptr);
    if (r.failed()) return r;
    v.dir.getChildFile("export.syx").replaceWithData(bytes.data(), bytes.size());   // what is on the unit, kept beside the record
    if (out) *out = v;
    return juce::Result::ok();
}

// ---------------------------------------------------------------------------
// Saved items, user data

juce::Result Store::saveItem(const SavedItem& item, juce::String* idOut)
{
    const juce::String id = item.id.isNotEmpty() ? item.id : newId();
    auto* o = new juce::DynamicObject();
    o->setProperty("format", 1); o->setProperty("kind", item.isKit ? "kit" : "preset");
    o->setProperty("name", item.name); o->setProperty("parent", item.parent); o->setProperty("savedFrom", item.savedFrom);
    o->setProperty("tags", strings(item.tags)); o->setProperty("time", (item.time == juce::Time() ? juce::Time::getCurrentTime() : item.time).toISO8601(true));
    o->setProperty("track", item.track); o->setProperty("kit", kitToJson(item.kit));
    const auto f = m_root.getChildFile("items").getChildFile(item.isKit ? "kits" : "presets").getChildFile(id + ".json");
    if (!writeJson(f, juce::var(o))) return juce::Result::fail("Could not write " + f.getFullPathName());
    if (idOut) *idOut = id;
    return juce::Result::ok();
}

std::vector<SavedItem> Store::listSavedItems() const
{
    std::vector<SavedItem> v;
    for (const char* sub : {"presets", "kits"}) {
        auto files = m_root.getChildFile("items").getChildFile(sub).findChildFiles(juce::File::findFiles, false, "*.json");
        files.sort();
        for (const auto& f : files) {
            auto json = juce::JSON::parse(f.loadFileAsString());
            auto* o = json.getDynamicObject();
            SavedItem it;
            if (!o || !kitFromJson(o->getProperty("kit"), it.kit)) continue;
            it.id = f.getFileNameWithoutExtension(); it.isKit = o->getProperty("kind").toString() == "kit";
            it.name = o->getProperty("name").toString(); it.parent = o->getProperty("parent").toString(); it.savedFrom = o->getProperty("savedFrom").toString();
            it.tags = readStrings(o->getProperty("tags")); it.time = juce::Time::fromISO8601(o->getProperty("time").toString());
            it.track = juce::jlimit(0, 5, geti(o, "track"));
            v.push_back(std::move(it));
        }
    }
    std::reverse(v.begin(), v.end());
    return v;
}

bool Store::deleteItem(const juce::String& id)
{
    bool any = false;
    for (const char* sub : {"presets", "kits"}) any = m_root.getChildFile("items").getChildFile(sub).getChildFile(id + ".json").deleteFile() || any;
    return any;
}

UserData Store::loadUser() const
{
    UserData u;
    auto json = juce::JSON::parse(m_root.getChildFile("user.json").loadFileAsString());
    if (auto* o = json.getDynamicObject()) {
        u.favourites = readStrings(o->getProperty("favourites"));
        if (auto* t = o->getProperty("tags").getDynamicObject())
            for (const auto& prop : t->getProperties()) u.tags[prop.name.toString()] = readStrings(prop.value);
    }
    return u;
}

bool Store::saveUser(const UserData& u) const
{
    auto* o = new juce::DynamicObject();
    o->setProperty("favourites", strings(u.favourites));
    auto* t = new juce::DynamicObject();
    for (const auto& [id, tags] : u.tags) if (!tags.isEmpty()) t->setProperty(juce::Identifier(id), strings(tags));
    o->setProperty("tags", juce::var(t));
    return writeJson(m_root.getChildFile("user.json"), juce::var(o));
}

juce::int64 Store::changeStamp() const
{
    juce::int64 stamp = 0;
    auto touch = [&](const juce::File& f) { if (f.exists()) stamp = std::max(stamp, f.getLastModificationTime().toMilliseconds()); };
    touch(m_root.getChildFile("projects")); touch(m_root.getChildFile("user.json"));
    for (const auto& d : m_root.getChildFile("projects").findChildFiles(juce::File::findDirectories, false)) { touch(d.getChildFile("project.json")); touch(d.getChildFile("versions")); }
    for (const char* sub : {"presets", "kits"}) touch(m_root.getChildFile("items").getChildFile(sub));
    return stamp;
}

// ---------------------------------------------------------------------------
// Migrations

// The pre-0.9 library kept <root>/<name>.json (format 1) and <root>/Originals/<name>.syx. Re-import the
// archived originals with the lossless codec and drop the old files.
void Store::migrateLegacyLayout()
{
    const auto originals = m_root.getChildFile("Originals");
    if (!originals.isDirectory()) return;
    for (const auto& syx : originals.findChildFiles(juce::File::findFiles, false, "*.syx")) {
        if (importSysexFile(syx, ImportMode::NewProject, {}).wasOk()) {
            m_root.getChildFile(syx.getFileNameWithoutExtension() + ".json").deleteFile();
            syx.deleteFile();
        }
    }
    if (originals.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0) originals.deleteFile();
}

// 0.9 - 1.0: <root>/imports/<id>/{import.json, original.syx, dump.json}. Each import becomes a project with one
// version. Copy first, check that the copy loads, and only then move the old folder aside (imports-migrated/):
// nothing is deleted.
void Store::migrateImports()
{
    const auto imports = m_root.getChildFile("imports");
    if (!imports.isDirectory()) return;
    auto dirs = imports.findChildFiles(juce::File::findDirectories, false);
    dirs.sort();
    for (const auto& dir : dirs) {
        const juce::String id = dir.getFileName();
        auto info = juce::JSON::parse(dir.getChildFile("import.json").loadFileAsString());
        auto* io = info.getDynamicObject();
        if (!io) continue;
        if (!projectDir(id).isDirectory()) {
            juce::MemoryBlock mb;
            Dump d;
            const bool haveSyx = dir.getChildFile("original.syx").loadFileAsData(mb);
            if (!dumpFromJson(juce::JSON::parse(dir.getChildFile("dump.json").loadFileAsString()), d)) {
                if (!haveSyx) continue;
                d = parseDump(static_cast<const uint8_t*>(mb.getData()), mb.getSize(), io->getProperty("name").toString().toStdString());
            }
            auto* po = new juce::DynamicObject();
            po->setProperty("name", io->getProperty("name")); po->setProperty("device", juce::String()); po->setProperty("pack", false);
            po->setProperty("createdAt", io->getProperty("importedAt"));
            if (!writeJson(projectDir(id).getChildFile("project.json"), juce::var(po))) continue;
            VersionInfo v;
            v.kind = "imported"; v.sourceFile = io->getProperty("sourceFile").toString();
            v.title = "Imported " + (v.sourceFile.isNotEmpty() ? juce::File(v.sourceFile).getFileName() : io->getProperty("name").toString());
            v.time = juce::Time::fromISO8601(io->getProperty("importedAt").toString());
            v.changes.add(juce::String(countNamedKits(d)) + " named kits, " + juce::String(countUsedPatterns(d)) + " used patterns, " + juce::String(d.numSongs) + " songs, " + juce::String(d.numGlobals) + " globals");
            if (writeVersion(id, v, &d).failed()) { projectDir(id).deleteRecursively(); continue; }
            if (haveSyx) v.dir.getChildFile("original.syx").replaceWithData(mb.getData(), mb.getSize());
            Dump check;
            if (!loadVersion(id, v.n, check) || check.kits.size() != d.kits.size() || check.patterns.size() != d.patterns.size()) { projectDir(id).deleteRecursively(); continue; }
        }
        const auto aside = m_root.getChildFile("imports-migrated");
        aside.createDirectory();
        dir.moveFileTo(aside.getChildFile(id));
    }
    if (imports.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0) imports.deleteFile();
}

} // namespace mnm::library
