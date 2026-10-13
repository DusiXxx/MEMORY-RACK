// Rack Memory - datovy model (zavisi jen na juce_core, da se testovat bez GUI)
#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <vector>

namespace rm
{
using juce::String;

constexpr int kSchemaVersion = 2;
static const char* const kRackId = "rack";     // snapshot = cely rack najednou

//==============================================================================
// Profil pristroje
//==============================================================================
enum class ControlType
{
    knob,      // plynuly knoflik, pozice 0..1
    stepped,   // knoflik s aretaci / otocny prepinac / LED krouzek, pozice = index kroku
    selector,  // prepinac s pojmenovanymi polohami, pozice = index
    button     // tlacitko s LED, pozice 0 = vyp, 1 = zap
};

struct ControlDef
{
    String id;            // stabilni ID, nikdy nemenit (klic v ulozenych snapshotech)
    String name;          // popisek na panelu
    String group;         // sekce / kanal
    ControlType type = ControlType::knob;
    int steps = 0;                // stepped: pocet poloh
    juce::StringArray options;    // selector: nazvy poloh; stepped: volitelne hodnoty kroku
    String minLabel, maxLabel;    // krajni hodnoty podle potisku / manualu (prazdne = nezname)
    String centreLabel;
    String note;
    bool verified = true;         // false = pocet kroku nebo rozsah neni potvrzen
    bool optional = false;
    float x = 0, y = 0;           // stred v pixelech obrazku panelu
    float r = 20.0f;              // polomer ovladace (zasah mysi), px obrazku
    float angleMin = -150.0f, angleMax = 150.0f;   // uhel ukazatele v krajnich polohach (0 = 12 hodin)

    // potisk stupnice: kde na otoceni lezi ktera hodnota (pro rucni zadani cisel)
    struct Mark { double pos01 = 0; String label; double value = 0; bool numeric = false; };
    std::vector<Mark> scale;

    // cteni z fotky
    float anchorR = 0;            // > 0: ovladac slouzi k zarovnani fotky (polomer kotvy v px obrazku)
    String read;                  // notch | line | led | ringglow | rectglow | "" (necte se)
    float readR = 0;

    // jak se ovladac kresli na fotce panelu
    struct Draw
    {
        String kind;                       // sprite | ring | leds | toggle | glow
        String sprite; float spriteR = 0, refAngle = 0;          // sprite: vyrez knobu a uhel, pod kterym je vyfocen
        struct Pt { float x = 0, y = 0; };
        std::vector<Pt> points; float ledR = 0;                  // ring / leds: pozice LED
        std::vector<int> map;                                    // leds: poloha -> index LED (-1 = zadna)
        juce::uint32 colour = 0xffffffff;
        float w = 0, h = 0; String axis; int photoState = 0;     // toggle: vyrez packy a jeji stav na fotce
        String shape; float gx = 0, gy = 0, gr = 0, gw = 0, gh = 0; bool litInPhoto = false;   // glow
    } draw;

    int numPositions() const
    {
        if (type == ControlType::button)   return 2;
        if (type == ControlType::selector) return options.size();
        if (type == ControlType::stepped)  return steps;
        return 0;
    }
};

struct DeviceProfile
{
    String id;              // stabilni ID
    String name;            // presny nazev modelu
    String shortName;
    String manufacturer;
    String formFactor;
    int profileVersion = 1;
    float w = 900, h = 1576;       // rozmer obrazku panelu (px)
    String image;                  // nazev obrazku panelu v Assets
    String imageNote;              // odkud obrazek je
    bool layoutVerified = false;
    String anchor;                 // disc | ring | "" (cteni z fotky neni)
    juce::StringArray sources;
    juce::StringArray openQuestions;
    std::vector<ControlDef> controls;

    const ControlDef* find (const String& controlId) const
    {
        for (auto& c : controls) if (c.id == controlId) return &c;
        return nullptr;
    }
};

const std::vector<DeviceProfile>& profiles();
const DeviceProfile* findProfile (const String& id);

inline String valueKey (const String& deviceId, const String& controlId)   { return deviceId + ":" + controlId; }

//==============================================================================
// Snapshot = nastaveni celeho racku
//==============================================================================
struct ControlValue
{
    bool known = false;        // false = NEZNAME (nikdy se nedoplnuje odhadem)
    double pos = 0;            // knob 0..1, jinak index polohy
    String source = "manual";  // "manual" | "recognized" (pozdejsi faze)
    double confidence = -1;
    bool confirmed = true;
};

struct Snapshot
{
    String id;
    String project;            // skladba
    String version;            // "v3" - prideluje se automaticky
    String notes;
    String deviceId = kRackId;
    juce::StringArray photos;  // nazvy souboru ve slozce Photos (relativni!)
    juce::StringArray order;   // poradi pristroju v racku (= co v racku je); prazdne = vychozi
    juce::int64 created = 0, modified = 0;
    std::map<String, ControlValue> values;    // klic "deviceId:controlId"

    ControlValue get (const String& key) const
    {
        auto it = values.find (key);
        return it == values.end() ? ControlValue() : it->second;
    }
    ControlValue get (const String& deviceId, const String& controlId) const   { return get (valueKey (deviceId, controlId)); }

    juce::var toVar() const;
    String toJson() const            { return juce::JSON::toString (toVar()); }
    static bool fromVar (const juce::var&, Snapshot&, String& error);
    static bool fromJson (const String&, Snapshot&, String& error);

    static Snapshot createNew (const String& project);
    int numKnown() const;
    int numKnown (const DeviceProfile&) const;
    String title() const;      // "Track_07 - v3"
};

int totalControls();

//==============================================================================
// Pomocne funkce
//==============================================================================
String clockString (double pos01, double sweepDeg);         // "2:30"
double position01 (const ControlDef&, const ControlValue&);
double angleFor (const ControlDef&, const ControlValue&);     // uhel ukazatele ve stupnich
String describeValue (const ControlDef&, const ControlValue&);
String shortValue (const ControlDef&, const ControlValue&);
String formatTime (juce::int64 ms);

// Rucni zadani: text, ktery se ukaze v policku (cislo z potisku, krok, "2:30", ON/OFF; "?" = nezadano).
// Hodnota mezi dvema cisly potisku je oznacena "≈" - je to jen orientace, ulozi se poloha.
String entryText (const ControlDef&, const ControlValue&);
// Prevod napsaneho textu na polohu. Vraci false, kdyz se text neda pouzit. Prazdny text / "?" = nezadano.
bool parseEntry (const ControlDef&, const String& text, ControlValue& out);
bool parseNumber (String text, double& out);                 // "2.5", "-0,3", "1k5", "16k", "2 kHz"

// vychozi rack; profily, ktere uz neexistuji, se vynechaji
juce::StringArray defaultRack();

//==============================================================================
// Rack uzivatele (dlouhodobe, mimo skladby): ktere pristroje, kolik kusu, poradi radku a sloty v lunchboxu.
// Kus = instance. Prvni kus modelu ma ID = ID modelu (kompatibilita s ulozenymi hodnotami), dalsi "model#2", "model#3"...
//==============================================================================
String profileIdOf (const String& instanceId);              // "elysia.xmax.500#2" -> "elysia.xmax.500"
const DeviceProfile* profileForInstance (const String& instanceId);

struct RackItem
{
    String id;          // ID kusu (klic hodnot ve snapshotech)
    String model;       // ID profilu
    int slot = -1;      // 500 modul: prvni obsazeny slot lunchboxu; 19" pristroj: -1
};

struct RackLayout
{
    static constexpr int kSchema = 1;
    int lunchboxSlots = 8;
    std::vector<RackItem> items;     // poradi = poradi radku (lunchbox stoji na miste prvniho 500 modulu)

    static RackLayout makeDefault();
    static RackLayout fromOrder (const juce::StringArray& ids);   // prevod ze starsiho "poradi"
    juce::var toVar() const;
    static bool fromVar (const juce::var&, RackLayout&, String& error);

    juce::StringArray ids() const;
    const RackItem* find (const String& id) const;
    int width500 (const RackItem&) const;                         // pocet slotu (0 = 19" pristroj)
    bool fits (int slot, int width, const String& ignoreId = {}) const;
    int freeSlot (int width) const;                               // -1 = neni misto
    String add (const String& model);                             // novy kus; vraci jeho ID ("" = neznamy model)
    bool remove (const String& id);
    bool place500 (const String& id, int slot);                   // presun modulu na slot (ostatni se odsunou, je-li misto)
    void move (const String& id, int newIndex);                   // poradi radku
    String displayName (const String& id) const;                  // "xmax 500" / "xmax 500 (2)"
    void normalise();                                             // nezname modely ven, sloty bez prekryvu
    bool operator== (const RackLayout& o) const;
    bool operator!= (const RackLayout& o) const   { return ! (*this == o); }
};
int versionNumber (const String& version);                   // "v12" -> 12

String makeReport (const Snapshot&);

//==============================================================================
// Knihovna na disku: <root>/Snapshots/<id>.json, <root>/Photos/*, <root>/Trash/*
//==============================================================================
struct RackLayout;

class Library
{
public:
    Library();
    explicit Library (const juce::File& rootFolder) : root (rootFolder) {}

    juce::File getRoot() const          { return root; }
    juce::File snapshotsDir() const     { return root.getChildFile ("Snapshots"); }
    juce::File photosDir() const        { return root.getChildFile ("Photos"); }
    juce::File trashDir() const         { return root.getChildFile ("Trash"); }
    juce::File reportsDir() const       { return root.getChildFile ("Reports"); }
    juce::File snapshotFile (const String& id) const;
    juce::File photoFile (const String& relName) const;

    std::vector<Snapshot> loadAll (juce::StringArray* errors = nullptr) const;
    bool exists (const String& id) const       { return snapshotFile (id).existsAsFile(); }
    bool wasDeleted (const String& id) const;
    juce::Result save (Snapshot&) const;            // nastavi modified
    juce::Result remove (const String& id) const;   // presun do Trash, nic se nemaze natrvalo
    String signature() const;                       // levna detekce zmen z jine instance (verze i rack)

    // rack uzivatele: <root>/rack.json (+ rack.json.bak = predchozi stav)
    juce::File rackFile() const         { return root.getChildFile ("rack.json"); }
    bool loadRack (RackLayout&, String& error) const;    // false = soubor neni / je poskozeny (zkusi se zaloha)
    juce::Result saveRack (const RackLayout&) const;

    // Zkopiruje fotku do knihovny (original se nemeni). Vraci nazev souboru, nebo "" + error.
    String importPhoto (const juce::File& source, const String& snapshotId, String& error) const;

private:
    juce::File root;
};

} // namespace rm
