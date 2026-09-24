#include "Transfer.h"
#include "Store.h"
#include "MidiExport.h"

namespace mnm::library {

using namespace mnm::dump;

static juce::String safeFileName(const juce::String& s)
{
    juce::String out;
    for (auto c : s) out += (juce::CharacterFunctions::isLetterOrDigit(c) || c == '-' || c == '_' || c == ' ') ? juce::String::charToString(c) : juce::String("_");
    return out.trim().isEmpty() ? juce::String("monomachine") : out.trim();
}

juce::var trackToTransferJson(const Kit& kit, int track, const juce::String& name)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("type", "mnmtrack");
    o->setProperty("format", 1);
    o->setProperty("name", name);
    o->setProperty("track", track);
    o->setProperty("kit", kitToJson(kit));
    return juce::var(o);
}

juce::var kitToTransferJson(const Kit& kit, const juce::String& name)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("type", "mnmkit");
    o->setProperty("format", 1);
    o->setProperty("name", name);
    o->setProperty("kit", kitToJson(kit));
    return juce::var(o);
}

bool transferFromJson(const juce::var& json, TransferPayload& out)
{
    const auto* o = json.getDynamicObject();
    if (!o) return false;
    const auto type = o->getProperty("type").toString();
    out = TransferPayload();
    if (type == "mnmtrack") out.kind = TransferKind::Track;
    else if (type == "mnmkit") out.kind = TransferKind::Kit;
    else return false;
    out.name = o->getProperty("name").toString();
    if (!kitFromJson(o->getProperty("kit"), out.kit)) return false;
    if (out.kind == TransferKind::Track) {
        out.track = int(o->getProperty("track"));
        if (out.track < 0 || out.track > 5) return false;
    }
    return true;
}

bool readTransferFile(const juce::File& file, TransferPayload& out)
{
    return file.existsAsFile() && transferFromJson(juce::JSON::parse(file.loadFileAsString()), out);
}

bool isTransferFile(const juce::String& path, TransferKind kind)
{
    if (kind == TransferKind::Track) return path.endsWithIgnoreCase(kTrackFileExtension);
    if (kind == TransferKind::Kit) return path.endsWithIgnoreCase(kKitFileExtension);
    return false;
}

juce::File dragDirectory()
{
    static juce::File dir = [] {
        auto d = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MonomachineLibrary-drag");
        d.deleteRecursively();
        d.createDirectory();
        return d;
    }();
    return dir;
}

static juce::File uniqueDragFile(const juce::String& base, const char* ext)
{
    auto f = dragDirectory().getChildFile(safeFileName(base) + ext);
    return f.existsAsFile() ? f.getNonexistentSibling() : f;
}

juce::File writeTrackDragFile(const Kit& kit, int track, const juce::String& baseName)
{
    const auto f = uniqueDragFile(baseName, kTrackFileExtension);
    f.replaceWithText(juce::JSON::toString(trackToTransferJson(kit, track, baseName)));
    return f;
}

juce::File writeKitDragFile(const Kit& kit, const juce::String& baseName)
{
    const auto f = uniqueDragFile(baseName, kKitFileExtension);
    f.replaceWithText(juce::JSON::toString(kitToTransferJson(kit, baseName)));
    return f;
}

juce::File writePatternMidiDragFile(const Dump& dump, const Pattern& pat, int track)
{
    const juce::String base = track >= 0 ? trackMidiFileName(dump, pat, track).upToLastOccurrenceOf(".mid", false, true)
                                         : juce::String(dump.file) + "-" + juce::String(patternSlotName(pat.position));
    const auto f = uniqueDragFile(base, ".mid");
    juce::FileOutputStream os(f);
    if (!os.openedOk()) return {};
    os.setPosition(0); os.truncate();
    if (track >= 0) buildTrackMidiFile(dump, pat, track).writeTo(os, 1);
    else buildPatternMidiFile(dump, pat).writeTo(os, 1);
    return f;
}

} // namespace mnm::library
