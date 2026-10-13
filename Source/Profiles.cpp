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

std::vector<DeviceProfile> loadProfiles()
{
    std::vector<DeviceProfile> out;
    juce::var root;
    if (juce::JSON::parse (String::fromUTF8 (BinaryData::profiles_json, BinaryData::profiles_jsonSize), root).failed())
        return out;

    if (auto* arr = root.getProperty ("profiles", juce::var()).getArray())
    {
        for (auto& v : *arr)
        {
            DeviceProfile p;
            p.id = str (v, "id"); p.name = str (v, "name"); p.manufacturer = str (v, "manufacturer"); p.formFactor = str (v, "formFactor");
            p.profileVersion = (int) v.getProperty ("profileVersion", 1);
            p.w = num (v, "w", 900); p.h = num (v, "h", 1576);
            p.image = str (v, "image"); p.imageNote = str (v, "imageNote");
            p.layoutVerified = (bool) v.getProperty ("layoutVerified", false);
            p.anchor = str (v, "anchor");
            if (auto* s = v.getProperty ("sources", juce::var()).getArray())        for (auto& e : *s) p.sources.add (e.toString());
            if (auto* q = v.getProperty ("openQuestions", juce::var()).getArray())  for (auto& e : *q) p.openQuestions.add (e.toString());
            if (auto* cs = v.getProperty ("controls", juce::var()).getArray())      for (auto& e : *cs) p.controls.push_back (parseControl (e));
            if (p.id.isNotEmpty() && ! p.controls.empty())
                out.push_back (std::move (p));
        }
    }
    return out;
}
} // namespace

const std::vector<DeviceProfile>& profiles()
{
    static const std::vector<DeviceProfile> all = loadProfiles();
    return all;
}

const DeviceProfile* findProfile (const String& id)
{
    for (auto& p : profiles()) if (p.id == id) return &p;
    return nullptr;
}

} // namespace rm
