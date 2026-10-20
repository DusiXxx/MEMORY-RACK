#include "Model.h"

namespace rm
{
using juce::var;

static String safeId (const String& s)
{
    return s.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_");
}

//==============================================================================
var Snapshot::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("schema", kSchemaVersion);
    o->setProperty ("id", id);
    o->setProperty ("project", project);
    o->setProperty ("version", version);
    o->setProperty ("deviceId", deviceId);
    o->setProperty ("notes", notes);
    if (name.isNotEmpty())  o->setProperty ("name", name);
    if (group.isNotEmpty()) o->setProperty ("group", group);
    if (! devicePhotos.empty())
    {
        auto* dp = new juce::DynamicObject();
        for (auto& kv : devicePhotos) dp->setProperty (juce::Identifier (kv.first), kv.second);
        o->setProperty ("devicePhotos", var (dp));
    }
    if (! cables.empty())
    {
        juce::Array<var> cb;
        for (auto& c : cables)
        {
            auto* e = new juce::DynamicObject();
            e->setProperty ("from", c.from); e->setProperty ("to", c.to);
            if (c.note.isNotEmpty()) e->setProperty ("note", c.note);
            cb.add (var (e));
        }
        o->setProperty ("cables", cb);
    }
    juce::Array<var> ph;
    for (auto& p : photos) ph.add (p);
    o->setProperty ("photos", ph);
    if (! order.isEmpty())
    {
        juce::Array<var> od;
        for (auto& d : order) od.add (d);
        o->setProperty ("order", od);
    }
    if (defs.getDynamicObject() != nullptr) o->setProperty ("defs", defs);
    o->setProperty ("created", created);
    o->setProperty ("modified", modified);

    auto* vals = new juce::DynamicObject();
    for (auto& kv : values)
    {
        if (! kv.second.known) continue;      // nezname = v souboru proste neni
        auto* v = new juce::DynamicObject();
        v->setProperty ("pos", kv.second.pos);
        if (kv.second.source != "manual")  v->setProperty ("source", kv.second.source);
        if (kv.second.confidence >= 0)     v->setProperty ("confidence", kv.second.confidence);
        if (! kv.second.confirmed)         v->setProperty ("confirmed", false);
        vals->setProperty (juce::Identifier (kv.first), var (v));
    }
    o->setProperty ("values", var (vals));
    return var (o);
}

bool Snapshot::fromVar (const var& v, Snapshot& s, String& error)
{
    auto* o = v.getDynamicObject();
    if (o == nullptr) { error = "soubor neobsahuje snapshot"; return false; }

    const int schema = (int) v.getProperty ("schema", 0);
    if (schema < 1)              { error = "chybi verze formatu"; return false; }
    if (schema > kSchemaVersion) { error = "ulozeno novejsi verzi pluginu (format " + String (schema) + ")"; return false; }

    s = Snapshot();
    s.id = safeId (v.getProperty ("id", "").toString());
    if (s.id.isEmpty()) { error = "chybi ID"; return false; }

    s.project  = v.getProperty ("project", "").toString();
    s.version  = v.getProperty ("version", "").toString();
    s.notes    = v.getProperty ("notes", "").toString();
    s.created  = (juce::int64) v.getProperty ("created", 0);
    s.modified = (juce::int64) v.getProperty ("modified", 0);
    s.name     = v.getProperty ("name", "").toString();
    s.defs     = v.getProperty ("defs", var());
    s.group    = safeId (v.getProperty ("group", "").toString());
    if (auto* dp = v.getProperty ("devicePhotos", var()).getDynamicObject())
        for (auto& kv : dp->getProperties())
            if (auto f = juce::File::createLegalFileName (kv.value.toString()); f.isNotEmpty())
                s.devicePhotos[kv.name.toString()] = f;
    if (auto* cb = v.getProperty ("cables", var()).getArray())
        for (auto& e : *cb)
        {
            Cable c { e.getProperty ("from", "").toString(), e.getProperty ("to", "").toString(), e.getProperty ("note", "").toString() };
            if (c.from.isNotEmpty() && c.to.isNotEmpty() && c.from != c.to) s.cables.push_back (c);
        }

    if (auto* ph = v.getProperty ("photos", var()).getArray())
        for (auto& p : *ph)
            if (auto name = juce::File::createLegalFileName (p.toString()); name.isNotEmpty())
                s.photos.add (name);
    if (auto legacy = juce::File::createLegalFileName (v.getProperty ("photoOriginal", "").toString()); legacy.isNotEmpty())
        s.photos.addIfNotAlreadyThere (legacy);

    if (auto* od = v.getProperty ("order", var()).getArray())
        for (auto& d : *od)
        {
            auto dev = d.toString();
            if (dev == "elysia.xfilter.500") dev = "elysia.xfilter.me";      // viz prevod hodnot nize
            if (dev.isNotEmpty()) s.order.addIfNotAlreadyThere (dev);
        }

    // format 1 ukladal jeden pristroj na snapshot -> prevedeme na klice "pristroj:ovladac"
    const auto fileDevice = v.getProperty ("deviceId", "").toString();
    if (fileDevice.isEmpty()) { error = "chybi pristroj"; return false; }
    const String prefix = fileDevice == kRackId ? String() : fileDevice + ":";
    s.deviceId = kRackId;

    if (auto* vals = v.getProperty ("values", var()).getDynamicObject())
    {
        for (auto& p : vals->getProperties())
        {
            ControlValue cv;
            cv.known      = (bool) p.value.getProperty ("known", true);
            cv.pos        = (double) p.value.getProperty ("pos", 0.0);
            cv.source     = p.value.getProperty ("source", "manual").toString();
            cv.confidence = (double) p.value.getProperty ("confidence", -1.0);
            cv.confirmed  = (bool) p.value.getProperty ("confirmed", true);
            if (! cv.known || ! std::isfinite (cv.pos)) continue;

            auto key = prefix + p.name.toString();
            // do verze 0.4 byl xfilter veden jako 500 modul; polohy knobu jsou stejne (41 kroku) -> patri k ME
            if (key.startsWith ("elysia.xfilter.500:"))
            {
                key = "elysia.xfilter.me:" + key.fromFirstOccurrenceOf (":", false, false);
                if (vals->hasProperty (juce::Identifier (key))) continue;
            }
            const auto dev = key.upToFirstOccurrenceOf (":", false, false);
            const auto ctl = key.fromFirstOccurrenceOf (":", false, false);

            // Hodnota mimo rozsah znameho profilu se NEopravuje odhadem, zahodi se (= neznama).
            if (auto* prof = profileForInstance (dev))
            {
                if (auto* c = prof->find (ctl))
                {
                    if (c->type == ControlType::knob)
                    {
                        if (cv.pos < 0.0 || cv.pos > 1.0) continue;
                    }
                    else
                    {
                        const double r = std::round (cv.pos);
                        if (r < 0 || r >= c->numPositions()) continue;
                        cv.pos = r;
                    }
                }
            }
            s.values[key] = cv;   // nezname pristroje / ovladace (novejsi profil) se zachovaji
        }
    }
    return true;
}

bool Snapshot::fromJson (const String& text, Snapshot& s, String& error)
{
    var v;
    if (juce::JSON::parse (text, v).failed()) { error = "poskozeny JSON"; return false; }
    return fromVar (v, s, error);
}

Snapshot Snapshot::createNew (const String& project)
{
    Snapshot s;
    s.id = juce::Uuid().toString();
    s.project = project;
    s.created = s.modified = juce::Time::currentTimeMillis();
    return s;
}

std::map<String, ControlValue> Snapshot::valuesFor (const juce::StringArray& instances) const
{
    std::map<String, ControlValue> out;
    for (auto& kv : values)
        if (kv.second.known && instances.contains (kv.first.upToFirstOccurrenceOf (":", false, false))) out[kv.first] = kv.second;
    return out;
}

std::vector<Difference> compare (const Snapshot& a, const Snapshot& b, const juce::StringArray& instances)
{
    std::vector<Difference> out;
    juce::StringArray keys;
    for (auto& kv : a.values) keys.addIfNotAlreadyThere (kv.first);
    for (auto& kv : b.values) keys.addIfNotAlreadyThere (kv.first);
    keys.sort (false);
    for (auto& k : keys)
    {
        const auto inst = k.upToFirstOccurrenceOf (":", false, false);
        if (! instances.isEmpty() && ! instances.contains (inst)) continue;
        if (profileForInstance (inst) == nullptr) continue;
        const auto va = a.get (k), vb = b.get (k);
        if (va.known != vb.known || (va.known && std::abs (va.pos - vb.pos) > 1e-9))
            out.push_back ({ inst, k.fromFirstOccurrenceOf (":", false, false), va, vb });
    }
    return out;
}

int Snapshot::numKnown (const DeviceProfile& p) const
{
    int n = 0;
    for (auto& c : p.controls) if (get (p.id, c.id).known) ++n;
    return n;
}

int Snapshot::numKnown() const
{
    // vsechny kusy vsech modelu (i ty, ktere zrovna nejsou v racku)
    int n = 0;
    for (auto& kv : values)
    {
        if (! kv.second.known) continue;
        const auto dev = kv.first.upToFirstOccurrenceOf (":", false, false);
        if (auto* p = profileForInstance (dev))
            if (p->find (kv.first.fromFirstOccurrenceOf (":", false, false)) != nullptr) ++n;
    }
    return n;
}

String Snapshot::title() const
{
    auto t = project.isEmpty() ? String ("Bez nazvu") : project;
    if (version.isNotEmpty()) t << "  " << version;
    return t;
}

juce::var definitionsFor (const Snapshot& s)
{
    auto* o = new juce::DynamicObject();
    juce::StringArray models;
    for (auto& kv : s.values)
        if (kv.second.known) models.addIfNotAlreadyThere (profileIdOf (kv.first.upToFirstOccurrenceOf (":", false, false)));
    for (auto& m : models)
    {
        auto* p = findProfile (m);
        if (p == nullptr) continue;
        auto* d = new juce::DynamicObject();
        d->setProperty ("name", p->name);
        d->setProperty ("profileVersion", p->profileVersion);
        d->setProperty ("origin", p->origin);
        auto* cs = new juce::DynamicObject();
        for (auto& c : p->controls)
        {
            auto* e = new juce::DynamicObject();
            e->setProperty ("type", (int) c.type);
            e->setProperty ("n", c.numPositions());
            if (! c.scale.empty()) { juce::StringArray l; for (auto& mk : c.scale) l.add (mk.label); e->setProperty ("scale", l.joinIntoString (" ")); }
            cs->setProperty (juce::Identifier (c.id), var (e));
        }
        d->setProperty ("controls", var (cs));
        o->setProperty (juce::Identifier (m), var (d));
    }
    return var (o);
}

juce::StringArray checkDefinitions (const juce::var& defs)
{
    juce::StringArray notes;
    auto* o = defs.getDynamicObject();
    if (o == nullptr) return notes;
    for (auto& kv : o->getProperties())
    {
        const auto model = kv.name.toString();
        const auto name = kv.value.getProperty ("name", model).toString();
        auto* p = findProfile (model);
        if (p == nullptr) { notes.add (name + String::fromUTF8 (": přístroj v knihovně chybí – uložené hodnoty zůstávají, jen se nezobrazí")); continue; }
        const int was = (int) kv.value.getProperty ("profileVersion", 0);
        bool changed = false;
        if (auto* cs = kv.value.getProperty ("controls", var()).getDynamicObject())
            for (auto& ck : cs->getProperties())
            {
                auto* c = p->find (ck.name.toString());
                if (c == nullptr) { notes.add (name + " / " + ck.name.toString() + String::fromUTF8 (": ovladač v novém profilu není – hodnota zůstává uložená")); continue; }
                if ((int) ck.value.getProperty ("n", 0) != c->numPositions() || (int) ck.value.getProperty ("type", 0) != (int) c->type) changed = true;
                juce::StringArray l; for (auto& mk : c->scale) l.add (mk.label);
                if (ck.value.getProperty ("scale", "").toString() != l.joinIntoString (" ")) changed = true;
            }
        if (was != p->profileVersion || changed)
            notes.add (name + String::fromUTF8 (": profil se od uložení změnil (v") + String (was) + String::fromUTF8 (" → v") + String (p->profileVersion)
                       + String::fromUTF8 (") – polohy knobů zůstaly, čísla stupnice zkontroluj"));
    }
    return notes;
}

int totalControls()
{
    int n = 0;
    for (auto& p : profiles()) n += (int) p.controls.size();
    return n;
}

//==============================================================================
String clockString (double pos01, double sweepDeg)
{
    const double angle = (juce::jlimit (0.0, 1.0, pos01) - 0.5) * sweepDeg;   // 0 = 12 hodin
    int minutes = (int) std::lround (angle * 2.0);                            // 30 stupnu = 60 minut
    minutes = (int) std::lround (minutes / 5.0) * 5;
    minutes = ((minutes % 720) + 720) % 720;
    int h = minutes / 60, m = minutes % 60;
    if (h == 0) h = 12;
    return String (h) + ":" + String (m).paddedLeft ('0', 2);
}

double position01 (const ControlDef& c, const ControlValue& v)
{
    if (c.type == ControlType::knob) return juce::jlimit (0.0, 1.0, v.pos);
    const int n = c.numPositions();
    return n > 1 ? juce::jlimit (0.0, 1.0, v.pos / (double) (n - 1)) : 0.0;
}

double angleFor (const ControlDef& c, const ControlValue& v)
{
    return c.angleMin + position01 (c, v) * (c.angleMax - c.angleMin);
}

String shortValue (const ControlDef& c, const ControlValue& v)
{
    if (! v.known) return "?";
    if (! c.scale.empty()) return entryText (c, v);
    switch (c.type)
    {
        case ControlType::button:   return v.pos >= 0.5 ? "ON" : "OFF";
        case ControlType::selector: return c.options[(int) v.pos];
        case ControlType::stepped:
            if (c.options.size() == c.steps) return c.options[(int) v.pos].upToFirstOccurrenceOf (" (", false, false);
            return String ((int) v.pos + 1);
        case ControlType::knob:
        default:                    return clockString (v.pos, c.angleMax - c.angleMin);
    }
}

String describeValue (const ControlDef& c, const ControlValue& v)
{
    if (! v.known) return "?";

    switch (c.type)
    {
        case ControlType::button:
            return v.pos >= 0.5 ? "ON" : "OFF";

        case ControlType::selector:
            return c.options[(int) v.pos];

        case ControlType::stepped:
        {
            const int i = (int) v.pos;
            if (c.options.size() == c.steps) return c.options[i];
            if (! c.scale.empty()) return entryText (c, v) + "   (krok " + String (i + 1) + "/" + String (c.steps) + ")";
            return String (i + 1) + "/" + String (c.steps);
        }

        case ControlType::knob:
        default:
            if (! c.scale.empty()) return entryText (c, v);
            return clockString (v.pos, c.angleMax - c.angleMin);
    }
}

String formatTime (juce::int64 ms)
{
    if (ms <= 0) return "-";
    return juce::Time (ms).formatted ("%d.%m. %H:%M");
}

int versionNumber (const String& version)
{
    return version.retainCharacters ("0123456789").getIntValue();
}

String makeReport (const Snapshot& s)
{
    String r;
    r << "RACK MEMORY\n===========\n";
    r << "Skladba:   " << s.project << "\n";
    r << "Verze:     " << s.version << "\n";
    r << "Ulozeno:   " << formatTime (s.modified) << "\n";
    r << "Poznamka:  " << s.notes << "\n";
    r << "Fotky:     " << s.photos.joinIntoString (", ") << "\n";

    for (auto& p : profiles())
    {
        r << "\n" << p.name << "\n" << String::repeatedString ("-", p.name.length()) << "\n";
        for (auto& c : p.controls)
            r << "  " << (c.group + " / " + c.name).paddedRight (' ', 34) << describeValue (c, s.get (p.id, c.id)) << "\n";
    }
    r << "\n? = nezadano. Plugin hardware neovlada, jen si pamatuje polohy.\n";
    return r;
}


//==============================================================================
// Rucni zadani cisel
//==============================================================================
bool parseNumber (String t, double& out)
{
    t = t.trim().toLowerCase().replace (",", ".").replace (" ", "").replace (juce::CharPointer_UTF8 ("\xe2\x88\x92"), "-");
    t = t.trimCharactersAtStart ("~").trimCharactersAtStart (juce::CharPointer_UTF8 ("\xe2\x89\x88"));
    t = t.upToFirstOccurrenceOf ("hz", false, true).upToFirstOccurrenceOf ("db", false, true).upToFirstOccurrenceOf ("ms", false, true);
    if (t.isEmpty()) return false;
    double mult = 1.0;
    if (t.endsWithChar ('k')) { mult = 1000.0; t = t.dropLastCharacters (1); }
    else if (t.containsChar ('k'))
    {
        // "1k5" = 1.5k, "2k2" = 2.2k
        const auto a = t.upToFirstOccurrenceOf ("k", false, false), b = t.fromFirstOccurrenceOf ("k", false, false);
        if (! b.containsOnly ("0123456789")) return false;
        t = a + "." + b; mult = 1000.0;
    }
    if (t == "+" || t == "-" || ! t.containsOnly ("+-0123456789.")) return false;
    if (t.lastIndexOfAnyOf ("+-") > 0 || t.indexOf (".") != t.lastIndexOf (".")) return false;
    out = t.getDoubleValue() * mult;
    return std::isfinite (out);
}

namespace
{
String formatLike (double v, const std::vector<ControlDef::Mark>& scale)
{
    // format podle potisku: 1k0 / 16k pro kHz, jinak tolik desetinnych mist jako potisk
    bool kilo = false; int decimals = 0;
    for (auto& m : scale)
    {
        if (m.label.containsIgnoreCase ("k")) kilo = true;
        if (m.label.containsChar ('.')) decimals = juce::jmax (decimals, m.label.fromLastOccurrenceOf (".", false, false).length());
    }
    if (kilo && std::abs (v) >= 1000.0)
    {
        const double k = std::round (v / 100.0) / 10.0;      // na desetiny kHz
        if (k >= 9.95) return String (juce::roundToInt (v / 1000.0)) + "k";
        const int whole = (int) std::floor (k + 1e-9), frac = juce::roundToInt ((k - whole) * 10.0);
        return String (whole) + "k" + String (frac);
    }
    if (std::abs (v) >= 10.0) return String (juce::roundToInt (v));
    return String (v, juce::jmax (1, decimals));
}

// poloha 0..1 -> hodnota na stupnici (mezi cisly potisku linearne); false = mimo / bez cisel
bool valueAt (const ControlDef& c, double pos, double& value, String& exactLabel)
{
    const auto& s = c.scale;
    if (s.size() < 2) return false;
    const double tol = c.type == ControlType::stepped && c.steps > 1 ? 0.45 / (c.steps - 1) : 0.006;
    for (auto& m : s)
        if (std::abs (m.pos01 - pos) <= tol) { exactLabel = m.label; value = m.value; return m.numeric; }
    for (size_t i = 0; i + 1 < s.size(); ++i)
    {
        const auto& a = s[i]; const auto& b = s[i + 1];
        if (pos >= a.pos01 && pos <= b.pos01 && a.numeric && b.numeric && b.pos01 > a.pos01)
        {
            value = a.value + (b.value - a.value) * (pos - a.pos01) / (b.pos01 - a.pos01);
            return true;
        }
    }
    return false;
}
}

String entryText (const ControlDef& c, const ControlValue& v)
{
    if (! v.known) return "?";
    if (c.type == ControlType::button)   return v.pos >= 0.5 ? "ON" : "OFF";
    if (c.type == ControlType::selector) return c.options[(int) v.pos];
    if (c.type == ControlType::stepped && c.options.size() == c.steps) return c.options[(int) v.pos].upToFirstOccurrenceOf (" (", false, false);

    const double pos = position01 (c, v);
    double value = 0; String label;
    if (valueAt (c, pos, value, label))
        return label.isNotEmpty() ? label : String (juce::CharPointer_UTF8 ("\xe2\x89\x88")) + formatLike (value, c.scale);
    if (label.isNotEmpty()) return label;                                  // napr. OFF / MAX
    if (c.type == ControlType::stepped) return String ((int) v.pos + 1);   // krok 1..N
    return clockString (v.pos, c.angleMax - c.angleMin);
}

bool parseEntry (const ControlDef& c, const String& text, ControlValue& out)
{
    const auto t = text.trim();
    out = ControlValue();
    out.source = "manual";
    if (t.isEmpty() || t == "?") { out.known = false; return true; }
    const auto lower = t.toLowerCase();

    auto setPos01 = [&] (double p01)
    {
        p01 = juce::jlimit (0.0, 1.0, p01);
        out.known = true;
        if (c.type == ControlType::knob) out.pos = p01;
        else out.pos = (double) juce::jlimit (0, juce::jmax (0, c.numPositions() - 1), (int) std::lround (p01 * (c.numPositions() - 1)));
        return true;
    };

    if (c.type == ControlType::button)
    {
        if (lower == "on" || lower == "1" || lower == "zap" || lower == "z" || lower == "x" || lower == "+") { out.known = true; out.pos = 1; return true; }
        if (lower == "off" || lower == "0" || lower == "vyp" || lower == "v" || lower == "-")               { out.known = true; out.pos = 0; return true; }
        return false;
    }
    if (c.options.size() > 0 && (c.type == ControlType::selector || c.options.size() == c.steps))
    {
        for (int i = 0; i < c.options.size(); ++i)
            if (c.options[i].toLowerCase().startsWith (lower) || c.options[i].upToFirstOccurrenceOf (" (", false, false).equalsIgnoreCase (t))
                { out.known = true; out.pos = i; return true; }
        double n;
        if (parseNumber (t, n) && n == std::floor (n) && n >= 1 && n <= c.options.size()) { out.known = true; out.pos = n - 1; return true; }
        return false;
    }

    // "#12" = krok 12 (u knobu s aretaci)
    if (c.type == ControlType::stepped && (t.startsWithChar ('#') || lower.startsWith ("k")))
    {
        double n;
        if (parseNumber (t.substring (1), n) && n >= 1 && n <= c.steps && n == std::floor (n)) { out.known = true; out.pos = n - 1; return true; }
        return false;
    }
    // "2:30" = poloha jako na hodinach
    if (t.containsChar (':'))
    {
        const int h = t.upToFirstOccurrenceOf (":", false, false).getIntValue(), m = t.fromFirstOccurrenceOf (":", false, false).getIntValue();
        if (h < 1 || h > 12 || m < 0 || m > 59) return false;
        double angle = ((h % 12) * 60 + m) * 0.5;            // stupne od 12 hodin
        if (angle > 180.0) angle -= 360.0;
        return setPos01 ((angle - c.angleMin) / (c.angleMax - c.angleMin));
    }

    // text z potisku (OFF, MAX, LOW, 1k0 ...)
    for (auto& m : c.scale)
        if (m.label.equalsIgnoreCase (t)) return setPos01 (m.pos01);

    double n;
    if (! parseNumber (t, n)) return false;
    // cislo na stupnici potisku
    std::vector<const ControlDef::Mark*> nums;
    for (auto& m : c.scale) if (m.numeric) nums.push_back (&m);
    if (nums.size() >= 2)
    {
        for (auto* m : nums) if (std::abs (m->value - n) < 1e-9) return setPos01 (m->pos01);
        for (size_t i = 0; i + 1 < nums.size(); ++i)
        {
            const double a = nums[i]->value, b = nums[i + 1]->value;
            if ((n - a) * (n - b) <= 0 && a != b)
                return setPos01 (nums[i]->pos01 + (nums[i + 1]->pos01 - nums[i]->pos01) * (n - a) / (b - a));
        }
        return false;    // mimo stupnici
    }
    if (c.type == ControlType::stepped)
    {
        if (n >= 1 && n <= c.steps && n == std::floor (n)) { out.known = true; out.pos = n - 1; return true; }
        return false;
    }
    if (n >= 0 && n <= 10) return setPos01 (n / 10.0);      // knob bez cisel: 0 .. 10
    return false;
}

juce::StringArray defaultRack()
{
    juce::StringArray out;
    for (auto* id : { "spl.vitalizer.mk3t", "elysia.xfilter.me", "elysia.xmax.500", "elysia.nvelope.500", "wesaudio.rhea" })
        if (findProfile (id) != nullptr) out.add (id);
    return out;
}


//==============================================================================
// Rack uzivatele
//==============================================================================
String profileIdOf (const String& instanceId)          { return instanceId.upToFirstOccurrenceOf ("#", false, false); }
const DeviceProfile* profileForInstance (const String& id)   { return findProfile (profileIdOf (id)); }

namespace
{
int slotsOfModel (const String& model)
{
    auto* p = findProfile (model);
    return p != nullptr && p->formFactor.startsWith ("500") ? juce::jmax (1, p->slots500) : 0;
}
}

int RackLayout::width500 (const RackItem& it) const   { return slotsOfModel (it.model); }

juce::StringArray RackLayout::ids() const
{
    juce::StringArray r;
    for (auto& it : items) r.add (it.id);
    return r;
}

const RackItem* RackLayout::find (const String& id) const
{
    for (auto& it : items) if (it.id == id) return &it;
    return nullptr;
}

bool RackLayout::fits (int slot, int width, const String& ignoreId) const
{
    if (slot < 0 || slot + width > lunchboxSlots) return false;
    for (auto& it : items)
    {
        const int w = width500 (it);
        if (w == 0 || it.id == ignoreId || it.slot < 0) continue;
        if (slot < it.slot + w && it.slot < slot + width) return false;
    }
    return true;
}

int RackLayout::freeSlot (int width) const
{
    for (int s = 0; s + width <= lunchboxSlots; ++s) if (fits (s, width)) return s;
    return -1;
}

String RackLayout::add (const String& model, const String& wantedId)
{
    auto* prof = findProfile (model);
    if (prof == nullptr || prof->formFactor == "patchbay") return {};
    String id = wantedId.isNotEmpty() && profileIdOf (wantedId) == model ? wantedId : model;
    if (wantedId.isNotEmpty() && find (id) != nullptr) return {};
    for (int n = 2; find (id) != nullptr; ++n) id = model + "#" + String (n);
    RackItem it; it.id = id; it.model = model;
    const int w = slotsOfModel (model);
    if (w > 0)
    {
        it.slot = freeSlot (w);
        if (it.slot < 0)                         // lunchbox je plny: rozsirit
        {
            int end = 0;
            for (auto& o : items) if (width500 (o) > 0) end = juce::jmax (end, o.slot + width500 (o));
            lunchboxSlots = juce::jmax (lunchboxSlots, end) + w;
            it.slot = freeSlot (w);
        }
        // modul jde za posledni 500 modul (lunchbox se v radach nehne)
        int insertAt = (int) items.size();
        for (int i = (int) items.size() - 1; i >= 0; --i) if (width500 (items[(size_t) i]) > 0) { insertAt = i + 1; break; }
        items.insert (items.begin() + insertAt, it);
    }
    else items.push_back (it);
    return id;
}

bool RackLayout::remove (const String& id)
{
    for (auto i = items.begin(); i != items.end(); ++i)
        if (i->id == id) { items.erase (i); return true; }
    return false;
}

bool RackLayout::place500 (const String& id, int slot)
{
    auto* me = find (id);
    if (me == nullptr) return false;
    const int w = width500 (*me);
    if (w == 0 || slot < 0 || slot + w > lunchboxSlots) return false;
    if (fits (slot, w, id)) { const_cast<RackItem*> (me)->slot = slot; return true; }

    // odsunout prekazejici moduly do volnych mist (nejdriv co nejbliz)
    RackLayout trial = *this;
    for (auto& it : trial.items) if (it.id == id) it.slot = slot;
    for (auto& it : trial.items)
    {
        const int ow = trial.width500 (it);
        if (ow == 0 || it.id == id) continue;
        if (! (slot < it.slot + ow && it.slot < slot + w)) continue;     // neprekryva se
        const int old = it.slot;
        it.slot = -1;
        int best = -1;
        for (int d = 1; d < trial.lunchboxSlots && best < 0; ++d)
            for (int cand : { old + d, old - d })
                if (cand >= 0 && trial.fits (cand, ow, it.id)) { best = cand; break; }
        if (best < 0) return false;                                       // neni kam
        it.slot = best;
    }
    *this = trial;
    return true;
}

void RackLayout::move (const String& id, int newIndex)
{
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].id == id)
        {
            auto it = items[i];
            items.erase (items.begin() + (long) i);
            if (newIndex > (int) i) --newIndex;
            items.insert (items.begin() + juce::jlimit (0, (int) items.size(), newIndex), it);
            return;
        }
}

String RackLayout::displayName (const String& id) const
{
    auto* p = profileForInstance (id);
    String name = p != nullptr ? p->name : id;
    if (id.containsChar ('#')) name << " (" << id.fromLastOccurrenceOf ("#", false, false) << ")";
    return name;
}

void RackLayout::normalise()
{
    std::vector<RackItem> keep;
    juce::StringArray seen;
    for (auto& it : items)
    {
        auto* prof = findProfile (it.model);
        if (prof == nullptr || prof->formFactor == "patchbay" || it.id.isEmpty() || seen.contains (it.id)) continue;
        seen.add (it.id);
        keep.push_back (it);
    }
    items = keep;
    lunchboxSlots = juce::jlimit (2, 24, lunchboxSlots);
    // sloty: prekryvy / mimo lunchbox se presunou na volne misto
    for (auto& it : items)
    {
        const int w = width500 (it);
        if (w == 0) { it.slot = -1; continue; }
        const int wanted = it.slot;
        it.slot = -2;                                              // docasne mimo
        if (wanted >= 0 && fits (wanted, w, it.id)) { it.slot = wanted; continue; }
        int s = freeSlot (w);
        if (s < 0) { lunchboxSlots += w; s = freeSlot (w); }
        it.slot = s;
    }
}

RackLayout RackLayout::makeDefault()       { return fromOrder (defaultRack()); }

RackLayout RackLayout::fromOrder (const juce::StringArray& ids)
{
    RackLayout r;
    for (auto& id : ids)
    {
        auto model = profileIdOf (id == "elysia.xfilter.500" ? String ("elysia.xfilter.me") : id);
        if (findProfile (model) == nullptr || r.find (id) != nullptr) continue;
        RackItem it; it.id = id == "elysia.xfilter.500" ? String ("elysia.xfilter.me") : id; it.model = model;
        const int w = slotsOfModel (model);
        it.slot = w > 0 ? r.freeSlot (w) : -1;
        if (w > 0 && it.slot < 0) { r.lunchboxSlots += w; it.slot = r.freeSlot (w); }
        r.items.push_back (it);
    }
    return r;
}

juce::var RackLayout::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("schema", kSchema);
    o->setProperty ("lunchboxSlots", lunchboxSlots);
    if (lunchboxModel.isNotEmpty()) o->setProperty ("lunchboxModel", lunchboxModel);
    juce::Array<juce::var> arr;
    for (auto& it : items)
    {
        auto* e = new juce::DynamicObject();
        e->setProperty ("id", it.id);
        e->setProperty ("model", it.model);
        if (it.slot >= 0) e->setProperty ("slot", it.slot);
        arr.add (juce::var (e));
    }
    o->setProperty ("devices", arr);
    return juce::var (o);
}

bool RackLayout::fromVar (const juce::var& v, RackLayout& out, String& error)
{
    if (v.getDynamicObject() == nullptr)               { error = "neni to rack"; return false; }
    if ((int) v.getProperty ("schema", 0) > kSchema)   { error = "rack ulozila novejsi verze pluginu"; return false; }
    RackLayout r;
    r.lunchboxSlots = (int) v.getProperty ("lunchboxSlots", 8);
    r.lunchboxModel = v.getProperty ("lunchboxModel", "").toString().substring (0, 60);
    if (auto* arr = v.getProperty ("devices", juce::var()).getArray())
        for (auto& e : *arr)
        {
            RackItem it;
            it.id = e.getProperty ("id", "").toString();
            it.model = e.getProperty ("model", profileIdOf (it.id)).toString();
            it.slot = (int) e.getProperty ("slot", -1);
            r.items.push_back (it);
        }
    r.normalise();
    out = r;
    return true;
}

bool RackLayout::operator== (const RackLayout& o) const
{
    if (lunchboxSlots != o.lunchboxSlots || lunchboxModel != o.lunchboxModel || items.size() != o.items.size()) return false;
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].id != o.items[i].id || items[i].model != o.items[i].model || items[i].slot != o.items[i].slot) return false;
    return true;
}


//==============================================================================
// Studio: skupiny + patchbaye
//==============================================================================
Studio Studio::makeDefault()
{
    return fromLegacy (RackLayout::makeDefault());
}

Studio Studio::fromLegacy (const RackLayout& r)
{
    Studio s;
    s.groups.push_back ({ "mixing", "Mixing", RackLayout() });
    s.groups.push_back ({ "mastering", "Mastering", r });
    s.normalise();
    return s;
}

namespace
{
var layoutItems (const RackLayout& l) { return l.toVar(); }
}

juce::var Studio::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("schema", RackLayout::kSchema);     // starsi pluginy (0.6-0.8) ctou "devices" nize
    o->setProperty ("studioSchema", kSchema);
    juce::Array<var> gs;
    for (auto& g : groups)
    {
        auto* e = new juce::DynamicObject();
        e->setProperty ("id", g.id);
        e->setProperty ("name", g.name);
        e->setProperty ("rack", layoutItems (g.layout));
        gs.add (var (e));
    }
    o->setProperty ("groups", gs);
    juce::Array<var> pbs;
    for (auto& pb : patchbays)
    {
        auto* e = new juce::DynamicObject();
        e->setProperty ("id", pb.id);
        e->setProperty ("model", pb.model);
        if (pb.notes.isNotEmpty()) e->setProperty ("notes", pb.notes);
        auto* lab = new juce::DynamicObject();
        for (auto& kv : pb.labels) if (kv.second.isNotEmpty()) lab->setProperty (juce::Identifier (kv.first), kv.second);
        e->setProperty ("labels", var (lab));
        auto* md = new juce::DynamicObject();
        for (auto& kv : pb.modes) md->setProperty (juce::Identifier (String (kv.first)), kv.second);
        e->setProperty ("modes", var (md));
        pbs.add (var (e));
    }
    o->setProperty ("patchbays", pbs);
    // pro starsi verze pluginu (0.6-0.8): prvni neprazdny rack i ve starem formatu
    for (auto& g : groups)
        if (! g.layout.items.empty())
        {
            o->setProperty ("lunchboxSlots", g.layout.lunchboxSlots);
            o->setProperty ("devices", g.layout.toVar().getProperty ("devices", var()));
            break;
        }
    return var (o);
}

bool Studio::fromVar (const juce::var& v, Studio& out, String& error)
{
    if (v.getDynamicObject() == nullptr) { error = "neni to rack"; return false; }
    if (! v.hasProperty ("groups"))
    {
        RackLayout r;
        if (! RackLayout::fromVar (v, r, error)) return false;
        out = fromLegacy (r);
        return true;
    }
    if ((int) v.getProperty ("studioSchema", 0) > kSchema) { error = "rack ulozila novejsi verze pluginu"; return false; }
    Studio s;
    if (auto* gs = v.getProperty ("groups", var()).getArray())
        for (auto& e : *gs)
        {
            RackGroup g;
            g.id = safeId (e.getProperty ("id", "").toString());
            g.name = e.getProperty ("name", g.id).toString().trim();
            String err;
            RackLayout::fromVar (e.getProperty ("rack", var (new juce::DynamicObject())), g.layout, err);
            s.groups.push_back (g);
        }
    if (auto* pbs = v.getProperty ("patchbays", var()).getArray())
        for (auto& e : *pbs)
        {
            PatchbayConfig pb;
            pb.id = e.getProperty ("id", "").toString();
            pb.model = e.getProperty ("model", profileIdOf (pb.id)).toString();
            pb.notes = e.getProperty ("notes", "").toString();
            if (auto* lab = e.getProperty ("labels", var()).getDynamicObject())
                for (auto& kv : lab->getProperties()) pb.labels[kv.name.toString()] = kv.value.toString();
            if (auto* md = e.getProperty ("modes", var()).getDynamicObject())
                for (auto& kv : md->getProperties()) pb.modes[kv.name.toString().getIntValue()] = kv.value.toString();
            s.patchbays.push_back (pb);
        }
    s.normalise();
    out = s;
    return true;
}

void Studio::normalise()
{
    std::vector<RackGroup> keep;
    juce::StringArray seen;
    for (auto& g : groups)
    {
        auto id = safeId (g.id);
        if (id.isEmpty()) id = "g" + String (juce::Random::getSystemRandom().nextInt (1000000));
        while (seen.contains (id)) id << "x";
        seen.add (id);
        auto c = g; c.id = id;
        if (c.name.isEmpty()) c.name = id;
        c.layout.normalise();
        keep.push_back (c);
    }
    groups = keep;
    if (groups.empty()) groups.push_back ({ "mastering", "Mastering", RackLayout() });

    std::vector<PatchbayConfig> pk;
    juce::StringArray pseen;
    for (auto& pb : patchbays)
    {
        auto* prof = findProfile (pb.model);
        if (prof == nullptr || prof->formFactor != "patchbay" || pb.id.isEmpty() || pseen.contains (pb.id)) continue;
        pseen.add (pb.id);
        pk.push_back (pb);
    }
    patchbays = pk;
}

RackGroup* Studio::findGroup (const String& id)             { for (auto& g : groups) if (g.id == id) return &g; return nullptr; }
const RackGroup* Studio::findGroup (const String& id) const { for (auto& g : groups) if (g.id == id) return &g; return nullptr; }

String Studio::addGroup (const String& name)
{
    RackGroup g;
    g.name = name.trim().isNotEmpty() ? name.trim() : String ("Skupina");
    g.id = safeId (g.name.toLowerCase().replaceCharacter (' ', '-'));
    if (g.id.isEmpty()) g.id = "skupina";
    const auto base = g.id;
    for (int n = 2; findGroup (g.id) != nullptr; ++n) g.id = base + "-" + String (n);
    g.layout.items.clear();
    groups.push_back (g);
    return g.id;
}

String Studio::duplicateGroup (const String& id, const String& newName)
{
    auto* src = findGroup (id);
    if (src == nullptr) return {};
    const auto layout = src->layout;
    const auto nid = addGroup (newName);
    findGroup (nid)->layout = layout;          // stejne kusy (fyzicky stejne pristroje)
    return nid;
}

bool Studio::removeGroup (const String& id)
{
    if (groups.size() <= 1) return false;
    for (auto i = groups.begin(); i != groups.end(); ++i)
        if (i->id == id) { groups.erase (i); return true; }
    return false;
}

bool Studio::renameGroup (const String& id, const String& name)
{
    auto* g = findGroup (id);
    if (g == nullptr || name.trim().isEmpty()) return false;
    g->name = name.trim();
    return true;
}

void Studio::moveGroup (const String& id, int newIndex)
{
    for (size_t i = 0; i < groups.size(); ++i)
        if (groups[i].id == id)
        {
            auto g = groups[i];
            groups.erase (groups.begin() + (long) i);
            groups.insert (groups.begin() + juce::jlimit (0, (int) groups.size(), newIndex), g);
            return;
        }
}

juce::StringArray Studio::allInstances() const
{
    juce::StringArray r;
    for (auto& g : groups) for (auto& it : g.layout.items) r.addIfNotAlreadyThere (it.id);
    return r;
}

String Studio::newInstanceId (const String& model) const
{
    const auto all = allInstances();
    String id = model;
    for (int n = 2; all.contains (id); ++n) id = model + "#" + String (n);
    return id;
}

String Studio::addPatchbay (const String& model)
{
    auto* p = findProfile (model);
    if (p == nullptr || p->formFactor != "patchbay") return {};
    String id = model;
    for (int n = 2; findPatchbay (id) != nullptr; ++n) id = model + "#" + String (n);
    PatchbayConfig pb; pb.id = id; pb.model = model;
    patchbays.push_back (pb);
    return id;
}

PatchbayConfig* Studio::findPatchbay (const String& id)   { for (auto& p : patchbays) if (p.id == id) return &p; return nullptr; }

bool Studio::operator== (const Studio& o) const
{
    if (groups.size() != o.groups.size() || patchbays.size() != o.patchbays.size()) return false;
    for (size_t i = 0; i < groups.size(); ++i)
        if (groups[i].id != o.groups[i].id || groups[i].name != o.groups[i].name || groups[i].layout != o.groups[i].layout) return false;
    for (size_t i = 0; i < patchbays.size(); ++i)
        if (patchbays[i].id != o.patchbays[i].id || patchbays[i].model != o.patchbays[i].model || patchbays[i].labels != o.patchbays[i].labels
            || patchbays[i].modes != o.patchbays[i].modes || patchbays[i].notes != o.patchbays[i].notes) return false;
    return true;
}

//==============================================================================
juce::var DevicePreset::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("schema", 1);
    o->setProperty ("id", id); o->setProperty ("name", name); o->setProperty ("model", model);
    o->setProperty ("modified", modified);
    auto* vals = new juce::DynamicObject();
    for (auto& kv : values) if (kv.second.known) vals->setProperty (juce::Identifier (kv.first), kv.second.pos);
    o->setProperty ("values", var (vals));
    return var (o);
}

bool DevicePreset::fromVar (const juce::var& v, DevicePreset& p, String& error)
{
    if (v.getDynamicObject() == nullptr || (int) v.getProperty ("schema", 0) != 1) { error = "neni to preset"; return false; }
    p = DevicePreset();
    p.id = safeId (v.getProperty ("id", "").toString());
    p.name = v.getProperty ("name", "").toString();
    p.model = v.getProperty ("model", "").toString();
    p.modified = (juce::int64) v.getProperty ("modified", 0);
    auto* prof = findProfile (p.model);
    if (p.id.isEmpty() || prof == nullptr) { error = "neznamy pristroj"; return false; }
    if (auto* vals = v.getProperty ("values", var()).getDynamicObject())
        for (auto& kv : vals->getProperties())
        {
            auto* c = prof->find (kv.name.toString());
            const double pos = (double) kv.value;
            if (c == nullptr || ! std::isfinite (pos)) continue;
            if (c->type == ControlType::knob ? (pos < 0 || pos > 1) : (pos < 0 || pos >= c->numPositions())) continue;
            ControlValue cv; cv.known = true; cv.pos = c->type == ControlType::knob ? pos : std::round (pos); cv.source = "preset";
            p.values[c->id] = cv;
        }
    return true;
}

//==============================================================================
Library::Library()
{
    auto env = juce::SystemStats::getEnvironmentVariable ("RACKMEMORY_HOME", {});
    root = env.isNotEmpty() ? juce::File (env)
                            : juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Rack Memory");
}

juce::File Library::snapshotFile (const String& id) const
{
    return snapshotsDir().getChildFile (safeId (id) + ".json");
}

juce::File Library::photoFile (const String& relName) const
{
    if (relName.isEmpty()) return {};
    return photosDir().getChildFile (juce::File::createLegalFileName (relName));
}

std::vector<Snapshot> Library::loadAll (juce::StringArray* errors) const
{
    std::vector<Snapshot> out;
    for (auto& f : snapshotsDir().findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        Snapshot s; String err;
        if (f.getSize() > 4 * 1024 * 1024)
            err = "soubor je podezrele velky";
        else if (Snapshot::fromJson (f.loadFileAsString(), s, err))
        {
            if (s.id != f.getFileNameWithoutExtension()) err = "ID nesouhlasi s nazvem souboru";
            else { out.push_back (std::move (s)); continue; }
        }
        if (errors != nullptr) errors->add (f.getFileName() + ": " + err);
    }
    std::sort (out.begin(), out.end(), [] (const Snapshot& a, const Snapshot& b) { return a.modified > b.modified; });
    return out;
}

bool Library::wasDeleted (const String& id) const
{
    return trashDir().getNumberOfChildFiles (juce::File::findFiles, safeId (id) + "*.json") > 0;
}

juce::Result Library::save (Snapshot& s) const
{
    if (safeId (s.id).isEmpty())
        return juce::Result::fail ("Snapshot nema ID.");
    if (! snapshotsDir().createDirectory())
        return juce::Result::fail ("Nelze vytvorit slozku " + snapshotsDir().getFullPathName());

    s.modified = juce::Time::currentTimeMillis();
    if (s.created <= 0) s.created = s.modified;

    // replaceWithText zapisuje pres docasny soubor -> pri padu nezustane napul zapsany snapshot
    if (! snapshotFile (s.id).replaceWithText (s.toJson(), false, false, "\n"))
        return juce::Result::fail ("Zapis selhal: " + snapshotFile (s.id).getFullPathName());
    return juce::Result::ok();
}

bool Library::loadRack (RackLayout& out, String& error) const
{
    for (auto f : { rackFile(), rackFile().withFileExtension ("json.bak") })
    {
        if (! f.existsAsFile()) continue;
        var v;
        if (juce::JSON::parse (f.loadFileAsString(), v).wasOk() && RackLayout::fromVar (v, out, error))
            return true;
        error = "rack.json je poskozeny";
    }
    return false;
}

juce::Result Library::saveRack (const RackLayout& r) const
{
    if (! root.createDirectory()) return juce::Result::fail ("Nelze vytvorit slozku " + root.getFullPathName());
    auto f = rackFile();
    if (f.existsAsFile()) f.copyFileTo (f.withFileExtension ("json.bak"));
    if (! f.replaceWithText (juce::JSON::toString (r.toVar()), false, false, "\n"))
        return juce::Result::fail ("Zapis selhal: " + f.getFullPathName());
    return juce::Result::ok();
}


bool Library::loadStudio (Studio& out, String& error) const
{
    for (auto f : { rackFile(), rackFile().withFileExtension ("json.bak") })
    {
        if (! f.existsAsFile()) continue;
        var v;
        if (juce::JSON::parse (f.loadFileAsString(), v).wasOk() && Studio::fromVar (v, out, error))
            return true;
        error = "rack.json je poskozeny";
    }
    return false;
}

juce::Result Library::saveStudio (const Studio& s) const
{
    if (! root.createDirectory()) return juce::Result::fail ("Nelze vytvorit slozku " + root.getFullPathName());
    auto f = rackFile();
    if (f.existsAsFile()) f.copyFileTo (f.withFileExtension ("json.bak"));
    if (! f.replaceWithText (juce::JSON::toString (s.toVar()), false, false, "\n"))
        return juce::Result::fail ("Zapis selhal: " + f.getFullPathName());
    return juce::Result::ok();
}

std::vector<DevicePreset> Library::loadPresets (const String& model) const
{
    std::vector<DevicePreset> out;
    for (auto& f : presetsDir().getChildFile (safeId (model.replaceCharacter ('.', '_'))).findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        var v; DevicePreset p; String err;
        if (f.getSize() < 1024 * 1024 && juce::JSON::parse (f.loadFileAsString(), v).wasOk() && DevicePreset::fromVar (v, p, err) && p.model == model)
            out.push_back (p);
    }
    std::sort (out.begin(), out.end(), [] (const DevicePreset& a, const DevicePreset& b) { return a.name.compareNatural (b.name) < 0; });
    return out;
}

juce::Result Library::savePreset (DevicePreset& p) const
{
    if (p.id.isEmpty()) p.id = juce::Uuid().toString();
    auto dir = presetsDir().getChildFile (safeId (p.model.replaceCharacter ('.', '_')));
    if (! dir.createDirectory()) return juce::Result::fail ("Nelze vytvorit slozku " + dir.getFullPathName());
    p.modified = juce::Time::currentTimeMillis();
    if (! dir.getChildFile (safeId (p.id) + ".json").replaceWithText (juce::JSON::toString (p.toVar()), false, false, "\n"))
        return juce::Result::fail ("Zapis presetu selhal.");
    return juce::Result::ok();
}

juce::Result Library::removePreset (const String& model, const String& id) const
{
    auto f = presetsDir().getChildFile (safeId (model.replaceCharacter ('.', '_'))).getChildFile (safeId (id) + ".json");
    if (! f.existsAsFile()) return juce::Result::ok();
    if (! trashDir().createDirectory()) return juce::Result::fail ("Nelze vytvorit slozku Trash.");
    return f.moveFileTo (trashDir().getChildFile ("preset_" + safeId (id) + "_" + String (juce::Time::currentTimeMillis()) + ".json"))
               ? juce::Result::ok() : juce::Result::fail ("Presun do Trash selhal.");
}

juce::Result Library::remove (const String& id) const
{
    auto f = snapshotFile (id);
    if (! f.existsAsFile()) return juce::Result::ok();
    if (! trashDir().createDirectory())
        return juce::Result::fail ("Nelze vytvorit slozku Trash.");
    auto target = trashDir().getChildFile (safeId (id) + "_" + String (juce::Time::currentTimeMillis()) + ".json");
    if (! f.moveFileTo (target))
        return juce::Result::fail ("Presun do Trash selhal.");
    return juce::Result::ok();
}

String Library::signature() const
{
    juce::int64 latest = 0; int n = 0;
    for (auto& f : snapshotsDir().findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        ++n;
        latest = juce::jmax (latest, f.getLastModificationTime().toMilliseconds());
    }
    return String (n) + ":" + String (latest) + ":" + String (rackFile().getLastModificationTime().toMilliseconds());
}

String Library::importPhoto (const juce::File& source, const String& snapshotId, String& error) const
{
    if (! source.existsAsFile())                 { error = "Soubor neexistuje."; return {}; }
    if (source.getSize() > 200 * 1024 * 1024)    { error = "Soubor je prilis velky (max 200 MB)."; return {}; }
    if (! photosDir().createDirectory())         { error = "Nelze vytvorit slozku Photos."; return {}; }

    const auto ext = source.getFileExtension().toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789.");
    auto target = photosDir().getNonexistentChildFile (safeId (snapshotId).substring (0, 8) + "_" + String (juce::Time::currentTimeMillis()), ext, false);
    if (! source.copyFileTo (target))            { error = "Kopirovani fotky selhalo."; return {}; }
    return target.getFileName();
}

} // namespace rm
