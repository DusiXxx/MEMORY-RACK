// Profily pristroju se nacitaji z Assets/profiles.json (generuje Tools/make_profiles.py
// z oficialnich podkladu vyrobcu). ID ovladacu se NIKDY nemeni - jsou klicem v ulozenych snapshotech.
#include "Model.h"
#include "BinaryData.h"

namespace rm
{
namespace
{
juce::uint32 parseColour (const String& s)
{
    auto h = s.trimCharactersAtStart ("#");
    return h.length() == 6 ? (juce::uint32) (0xff000000u | (juce::uint32) h.getHexValue32()) : 0xffffffffu;
}

String str (const juce::var& v, const char* key)              { return v.getProperty (key, "").toString(); }
float  num (const juce::var& v, const char* key, double d = 0) { return (float) (double) v.getProperty (key, d); }

ControlDef parseControl (const juce::var& v)
{
    ControlDef c;
    c.id = str (v, "id"); c.name = str (v, "name"); c.group = str (v, "group");
    const auto type = str (v, "type");
    c.type = type == "stepped" ? ControlType::stepped : type == "selector" ? ControlType::selector
           : type == "button" ? ControlType::button : ControlType::knob;
    c.steps = (int) v.getProperty ("steps", 0);
    if (auto* o = v.getProperty ("options", juce::var()).getArray())
        for (auto& e : *o) c.options.add (e.toString());
    c.minLabel = str (v, "minLabel"); c.maxLabel = str (v, "maxLabel"); c.centreLabel = str (v, "centreLabel");
    c.note = str (v, "note");
    c.verified = (bool) v.getProperty ("verified", true);
    c.optional = (bool) v.getProperty ("optional", false);
    c.x = num (v, "x"); c.y = num (v, "y"); c.r = num (v, "r", 20);
    c.angleMin = num (v, "angleMin", -150); c.angleMax = num (v, "angleMax", 150);
    c.anchorR = num (v, "anchorR"); c.read = str (v, "read"); c.readR = num (v, "readR");
    if (auto* sc = v.getProperty ("scale", juce::var()).getArray())
        for (auto& e : *sc)
        {
            ControlDef::Mark m;
            m.pos01 = (double) e[0];
            m.label = e[1].toString();
            m.numeric = parseNumber (m.label, m.value);
            if (e.size() > 2) { m.value = (double) e[2]; m.numeric = true; }   // napr. OFF = 0, MAX = 10
            c.scale.push_back (m);
        }

    auto d = v.getProperty ("draw", juce::var());
    c.draw.kind = str (d, "kind");
    c.draw.style = str (d, "style");
    c.draw.sprite = str (d, "sprite"); c.draw.spriteR = num (d, "spriteR"); c.draw.refAngle = num (d, "refAngle");
    if (auto* p = d.getProperty ("points", juce::var()).getArray())
        for (auto& e : *p) c.draw.points.push_back ({ (float) (double) e[0], (float) (double) e[1] });
    if (auto* m = d.getProperty ("map", juce::var()).getArray())
        for (auto& e : *m) c.draw.map.push_back ((int) e);
    c.draw.ledR = num (d, "ledR");
    c.draw.colour = parseColour (str (d, "colour"));
    c.draw.w = num (d, "w"); c.draw.h = num (d, "h"); c.draw.axis = str (d, "axis"); c.draw.photoState = (int) d.getProperty ("photoState", 0);
    c.draw.shape = str (d, "shape"); c.draw.gx = num (d, "x"); c.draw.gy = num (d, "y"); c.draw.gr = num (d, "r");
    c.draw.gw = num (d, "w"); c.draw.gh = num (d, "h"); c.draw.litInPhoto = (bool) d.getProperty ("litInPhoto", false);
    return c;
}

int loadedLibraryVersion = 0;
} // namespace

std::vector<DeviceProfile> parseProfiles (const juce::var& root, const String& origin)
{
    std::vector<DeviceProfile> out;
    {
        if (auto* arr = root.getProperty ("profiles", juce::var()).getArray())
        for (auto& v : *arr)
        {
            DeviceProfile p;
            p.origin = origin;
            p.category = str (v, "category");
            p.kind = str (v, "kind");
            p.slots500 = (int) v.getProperty ("slots500", 0);
            if (p.slots500 <= 0 && str (v, "formFactor").startsWith ("500")) p.slots500 = 2;
            auto pb = v.getProperty ("patchbay", juce::var());
            if (pb.getDynamicObject() != nullptr)
            {
                p.patchbay.channels = (int) pb.getProperty ("channels", 0);
                p.patchbay.topY = num (pb, "topY"); p.patchbay.bottomY = num (pb, "bottomY");
                p.patchbay.x0 = num (pb, "x0"); p.patchbay.dx = num (pb, "dx"); p.patchbay.jackR = num (pb, "jackR", 10);
                if (auto* m = pb.getProperty ("modes", juce::var()).getArray())     for (auto& e : *m) p.patchbay.modes.add (e.toString());
                if (auto* m = pb.getProperty ("modeNames", juce::var()).getArray()) for (auto& e : *m) p.patchbay.modeNames.add (e.toString());
                p.patchbay.modeHow = str (pb, "modeHow"); p.patchbay.note = str (pb, "note");
                p.patchbay.topLabel = str (pb, "topLabel"); p.patchbay.bottomLabel = str (pb, "bottomLabel");
                p.patchbay.bottomContinues = (bool) pb.getProperty ("bottomContinues", false);
            }
            p.id = str (v, "id"); p.name = str (v, "name"); p.manufacturer = str (v, "manufacturer"); p.formFactor = str (v, "formFactor");
            p.profileVersion = (int) v.getProperty ("profileVersion", 1);
            p.w = num (v, "w", 900); p.h = num (v, "h", 1576);
            p.image = str (v, "image"); p.imageNote = str (v, "imageNote");
            p.layoutVerified = (bool) v.getProperty ("layoutVerified", false);
            p.anchor = str (v, "anchor");
            if (auto* s = v.getProperty ("sources", juce::var()).getArray())        for (auto& e : *s) p.sources.add (e.toString());
            if (auto* q = v.getProperty ("openQuestions", juce::var()).getArray())  for (auto& e : *q) p.openQuestions.add (e.toString());
            if (auto* cs = v.getProperty ("controls", juce::var()).getArray())      for (auto& e : *cs) p.controls.push_back (parseControl (e));
            if (p.id.isNotEmpty() && (! p.controls.empty() || p.patchbay.channels > 0))
                out.push_back (std::move (p));
        }
    }
    return out;
}

std::vector<DeviceProfile> mergeProfiles (const std::vector<DeviceProfile>& builtIn, const juce::var& lib, juce::StringArray* notes)
{
    auto out = builtIn;
    if (lib.getDynamicObject() == nullptr) return out;
    if ((int) lib.getProperty ("schema", 0) != 1) { if (notes) notes->add ("knihovna hardwaru ma neznamy format"); return out; }
    for (auto& np : parseProfiles (lib, "library"))
    {
        auto it = std::find_if (out.begin(), out.end(), [&] (const DeviceProfile& p) { return p.id == np.id; });
        if (it == out.end()) { out.push_back (np); if (notes) notes->add ("novy: " + np.name); continue; }
        if (np.profileVersion <= it->profileVersion) continue;
        bool keepsIds = true;
        for (auto& c : it->controls) if (np.find (c.id) == nullptr) keepsIds = false;
        if (! keepsIds) { if (notes) notes->add ("neprevzato (chybi ovladace): " + np.name); continue; }
        *it = np;
        if (notes) notes->add ("aktualizovano: " + np.name);
    }
    return out;
}

juce::File hardwareLibraryDir()
{
    auto env = juce::SystemStats::getEnvironmentVariable ("RACKMEMORY_HOME", {});
    auto root = env.isNotEmpty() ? juce::File (env) : juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Rack Memory");
    return root.getChildFile ("Hardware");
}

int hardwareLibraryVersion()   { profiles(); return loadedLibraryVersion; }

namespace
{
std::vector<DeviceProfile> loadProfiles()
{
    juce::var root;
    if (juce::JSON::parse (String::fromUTF8 (BinaryData::profiles_json, BinaryData::profiles_jsonSize), root).failed())
        return {};
    auto out = parseProfiles (root, "built-in");
    // stazena (overena) knihovna hardwaru: novejsi profily / nove pristroje
    auto f = hardwareLibraryDir().getChildFile ("library.json");
    juce::var lib;
    if (f.existsAsFile() && f.getSize() < 8 * 1024 * 1024 && juce::JSON::parse (f.loadFileAsString(), lib).wasOk())
    {
        out = mergeProfiles (out, lib);
        loadedLibraryVersion = (int) lib.getProperty ("libraryVersion", 0);
    }
    return out;
}
}

juce::File userProfilesDir()
{
    return hardwareLibraryDir().getSiblingFile ("UserHardware");     // mimo Hardware/ (to prepisuje aktualizace knihovny)
}

namespace
{
std::vector<DeviceProfile> loadUserProfiles()
{
    std::vector<DeviceProfile> out;
    for (auto& f : userProfilesDir().findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        juce::var v;
        if (f.getSize() > 2 * 1024 * 1024 || juce::JSON::parse (f.loadFileAsString(), v).failed()) continue;
        juce::Array<juce::var> one; one.add (v);
        auto* o = new juce::DynamicObject(); o->setProperty ("profiles", one);
        for (auto& p : parseProfiles (juce::var (o), "user")) out.push_back (p);
    }
    return out;
}

std::vector<DeviceProfile>& storage()
{
    static std::vector<DeviceProfile> all = []
    {
        auto v = loadProfiles();
        for (auto& u : loadUserProfiles())
            if (std::none_of (v.begin(), v.end(), [&] (const DeviceProfile& p) { return p.id == u.id; })) v.push_back (u);
        v.reserve (4000);      // ukazatele na profily se nesmi zneplatnit (vlastni pristroje se pridavaji za behu)
        return v;
    }();
    return all;
}
}

const std::vector<DeviceProfile>& profiles()   { return storage(); }

juce::var controlToVar (const ControlDef& c)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", c.id); o->setProperty ("name", c.name); o->setProperty ("group", c.group);
    o->setProperty ("type", c.type == ControlType::stepped ? "stepped" : c.type == ControlType::selector ? "selector" : c.type == ControlType::button ? "button" : "knob");
    if (c.steps > 0) o->setProperty ("steps", c.steps);
    if (! c.options.isEmpty()) { juce::Array<juce::var> a; for (auto& x : c.options) a.add (x); o->setProperty ("options", a); }
    o->setProperty ("minLabel", c.minLabel); o->setProperty ("maxLabel", c.maxLabel); o->setProperty ("note", c.note);
    o->setProperty ("verified", c.verified);
    o->setProperty ("x", c.x); o->setProperty ("y", c.y); o->setProperty ("r", c.r);
    o->setProperty ("angleMin", c.angleMin); o->setProperty ("angleMax", c.angleMax);
    auto* d = new juce::DynamicObject();
    d->setProperty ("kind", c.draw.kind); d->setProperty ("style", c.draw.style);
    if (! c.draw.points.empty()) { juce::Array<juce::var> a; for (auto& p : c.draw.points) { juce::Array<juce::var> q; q.add (p.x); q.add (p.y); a.add (q); } d->setProperty ("points", a); }
    if (! c.draw.map.empty()) { juce::Array<juce::var> a; for (auto m : c.draw.map) a.add (m); d->setProperty ("map", a); }
    d->setProperty ("w", c.draw.w); d->setProperty ("h", c.draw.h);
    d->setProperty ("colour", "#" + juce::String::toHexString ((int) (c.draw.colour & 0xffffff)).paddedLeft ('0', 6));
    o->setProperty ("draw", juce::var (d));
    return juce::var (o);
}

juce::var profileToVar (const DeviceProfile& p)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", p.id); o->setProperty ("name", p.name); o->setProperty ("manufacturer", p.manufacturer);
    o->setProperty ("formFactor", p.formFactor); o->setProperty ("kind", p.kind); o->setProperty ("category", p.category);
    o->setProperty ("profileVersion", p.profileVersion); o->setProperty ("w", p.w); o->setProperty ("h", p.h);
    o->setProperty ("image", p.image); o->setProperty ("imageNote", p.imageNote); o->setProperty ("layoutVerified", p.layoutVerified);
    if (p.slots500 > 0) o->setProperty ("slots500", p.slots500);
    juce::Array<juce::var> src; for (auto& x : p.sources) src.add (x); o->setProperty ("sources", src);
    juce::Array<juce::var> oq; for (auto& x : p.openQuestions) oq.add (x); o->setProperty ("openQuestions", oq);
    juce::Array<juce::var> cs; for (auto& c : p.controls) cs.add (controlToVar (c)); o->setProperty ("controls", cs);
    return juce::var (o);
}

const DeviceProfile* saveUserProfile (const DeviceProfile& prof, String& error)
{
    auto& all = storage();
    for (auto& p : all)
        if (p.id == prof.id && ! p.isUser()) { error = "ID patri vestavenemu pristroji"; return nullptr; }
    auto dir = userProfilesDir();
    if (! dir.createDirectory()) { error = "nelze vytvorit " + dir.getFullPathName(); return nullptr; }
    auto f = dir.getChildFile (prof.id.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") + ".json");
    if (! f.replaceWithText (juce::JSON::toString (profileToVar (prof)), false, false, "\n")) { error = "zapis selhal"; return nullptr; }
    auto copy = prof; copy.origin = "user"; copy.hidden = false;
    for (auto& p : all) if (p.id == prof.id) { p = copy; return &p; }
    if (all.size() + 1 >= all.capacity()) { error = "prilis mnoho profilu"; return nullptr; }
    all.push_back (copy);
    return &all.back();
}

bool hideUserProfile (const String& id)
{
    for (auto& p : storage())
        if (p.id == id && p.isUser())
        {
            auto f = userProfilesDir().getChildFile (id.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") + ".json");
            auto trash = hardwareLibraryDir().getSiblingFile ("Trash");
            trash.createDirectory();
            f.moveFileTo (trash.getChildFile ("profile_" + f.getFileNameWithoutExtension() + "_" + String (juce::Time::currentTimeMillis()) + ".json"));
            p.hidden = true;
            return true;
        }
    return false;
}

// Vlastni pristroj z jednoducheho popisu: radek = ovladac. "NAZEV" = knob, "NAZEV | 11" = 11 poloh,
// "NAZEV | A/B/C" = prepinac, "NAZEV | on/off" = tlacitko. Panel se kresli (bez fotky).
DeviceProfile makeCustomProfile (const String& name, const String& manufacturer, const String& format, const juce::StringArray& lines)
{
    DeviceProfile p;
    p.name = name.trim(); p.manufacturer = manufacturer.trim();
    p.id = "user." + (manufacturer + "-" + name).toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789- ").trim().replaceCharacter (' ', '-');
    p.origin = "user"; p.kind = "other"; p.layoutVerified = false; p.profileVersion = 1;
    p.sources.add ("vlastni definice uzivatele");
    p.openQuestions.add ("Vlastni pristroj: ovladace a stupnice zadal uzivatel, panel je schematicky.");
    const bool is500 = format.startsWith ("500");
    const int units = juce::jmax (1, format.fromFirstOccurrenceOf (":", false, false).getIntValue());
    if (is500) { p.formFactor = "500 series, " + String (units) + " slot"; p.slots500 = units; p.w = 450.0f * units; p.h = 1576; }
    else       { p.formFactor = "19\" rack, " + String (units) + "U"; p.w = 2400; p.h = 221.0f * units; }
    juce::StringArray rows;
    for (auto& l : lines) if (l.trim().isNotEmpty()) rows.add (l.trim());
    const int n = juce::jmax (1, rows.size());
    const int perLine = is500 ? juce::jmax (1, units) : juce::jmax (1, (n + units - 1) / units);
    const int lineCount = (n + perLine - 1) / perLine;
    juce::StringArray used;
    for (int i = 0; i < rows.size(); ++i)
    {
        ControlDef c;
        const auto label = rows[i].upToFirstOccurrenceOf ("|", false, false).trim();
        const auto spec = rows[i].fromFirstOccurrenceOf ("|", false, false).trim();
        c.name = label;
        c.id = label.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789");
        if (c.id.isEmpty()) c.id = "c" + String (i + 1);
        while (used.contains (c.id)) c.id << "x";
        used.add (c.id);
        const int row = i / perLine, col = i % perLine;
        const int inRow = juce::jmin (perLine, n - row * perLine);
        if (is500) { c.x = p.w * (col + 0.5f) / (float) juce::jmax (1, inRow); c.y = 180.0f + (p.h - 400.0f) * (row + 0.5f) / (float) lineCount; }
        else       { c.x = 200.0f + (p.w - 400.0f) * (col + 0.5f) / (float) inRow; c.y = p.h * (row + 0.5f) / (float) lineCount; }
        const float cell = is500 ? juce::jmin (p.w / (float) juce::jmax (1, inRow), (p.h - 400.0f) / (float) lineCount)
                                 : juce::jmin ((p.w - 400.0f) / (float) inRow, p.h / (float) lineCount);
        c.r = juce::jlimit (24.0f, 110.0f, cell * 0.28f);
        c.verified = false;
        if (spec.equalsIgnoreCase ("on/off") || spec.equalsIgnoreCase ("button") || spec.equalsIgnoreCase ("tlacitko"))
        {
            c.type = ControlType::button;
            c.draw.kind = "vbuttons"; c.draw.points.push_back ({ c.x, c.y }); c.draw.w = c.r * 1.6f; c.draw.h = c.r * 1.1f;
            c.draw.map = { -1, 0 }; c.draw.colour = 0xffff3b2a;
        }
        else if (spec.containsChar ('/'))
        {
            c.type = ControlType::selector;
            c.options = juce::StringArray::fromTokens (spec, "/", "");
            c.options.trim(); c.options.removeEmptyStrings();
            c.angleMin = -60; c.angleMax = 60;
            c.draw.kind = "vknob"; c.draw.style = "chicken"; c.draw.colour = 0xff202020;
        }
        else if (spec.getIntValue() >= 2)
        {
            c.type = ControlType::stepped; c.steps = juce::jmin (64, spec.getIntValue());
            c.draw.kind = "vknob"; c.draw.style = "small"; c.draw.colour = 0xff2a2a2a;
        }
        else { c.type = ControlType::knob; c.draw.kind = "vknob"; c.draw.style = "small"; c.draw.colour = 0xff2a2a2a; }
        p.controls.push_back (c);
    }
    return p;
}

const DeviceProfile* findProfile (const String& id)
{
    for (auto& p : profiles()) if (p.id == id) return &p;
    return nullptr;
}

} // namespace rm
