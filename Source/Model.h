// Rack Memory - datovy model (zavisi jen na juce_core, da se testovat bez GUI)
#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <vector>

namespace rm
{
using juce::String;

constexpr int kSchemaVersion = 3;      // 3: skupiny (Mixing/Mastering), kabely patchbaye, fotky po pristrojich, potvrzeni
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
        String kind;                       // sprite | ring | leds | toggle | glow | vknob | vbuttons | vlever (vektorove, kresli plugin)
        String style;                      // vknob: skirt | small | chicken
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
    String origin = "built-in";    // "built-in" | "library" (stazena knihovna hardwaru)
    String category;               // "mixing" | "mastering" | "" - doporucena skupina pro novy kus
    String kind;                   // compressor | eq | preamp | exciter | transient | patchbay | other
    int slots500 = 0;              // 500 modul: sirka ve slotech lunchboxu (0 = 19" / jine)
    bool hidden = false;           // smazany vlastni profil (ulozena nastaveni zustanou)
    bool isUser() const            { return origin == "user"; }

    // patchbay (formFactor == "patchbay"): body na prednim panelu, 2 rady
    struct Patchbay
    {
        int channels = 0;
        float topY = 0, bottomY = 0, x0 = 0, dx = 0, jackR = 10;   // stred jacku kanalu k: x0 + (k-1)*dx
        juce::StringArray modes;                                   // napr. N, HN, T (prvni = vychozi)
        juce::StringArray modeNames;                               // dlouhe nazvy rezimu
        String modeHow;                                            // jak se rezim meni (prepinac vpredu / otoceni desky)
        String topLabel, bottomLabel;                              // co je horni / dolni rada podle vyrobce
        bool bottomContinues = false;                              // cisla dole pokracuji (25-48), jinak 1-24
        String note;
    } patchbay;
    bool isPatchbay() const        { return formFactor == "patchbay"; }

    const ControlDef* find (const String& controlId) const
    {
        for (auto& c : controls) if (c.id == controlId) return &c;
        return nullptr;
    }
};

const std::vector<DeviceProfile>& profiles();
juce::File hardwareLibraryDir();                  // <knihovna>/Hardware - stazene profily hardwaru (oddelene od pluginu)
int hardwareLibraryVersion();                     // verze nactene stazene knihovny (0 = jen vestavene)
// Slouceni stazene knihovny s vestavenymi profily (bez GUI, testovatelne). Novejsi profil nahradi vestaveny,
// jen kdyz zachova vsechna ID ovladacu (ulozena nastaveni se nesmi ztratit); jinak zustane vestaveny.
std::vector<DeviceProfile> mergeProfiles (const std::vector<DeviceProfile>& builtIn, const juce::var& libraryJson, juce::StringArray* notes = nullptr);
std::vector<DeviceProfile> parseProfiles (const juce::var& json, const String& origin);

// Vlastni pristroje uzivatele: <knihovna>/UserHardware/<id>.json (aktualizace knihovny je neprepisuje)
juce::File userProfilesDir();
juce::var profileToVar (const DeviceProfile&);
const DeviceProfile* saveUserProfile (const DeviceProfile&, String& error);      // novy nebo upraveny vlastni profil
bool hideUserProfile (const String& id);                                        // smazat (soubor do Trash, nastaveni zustanou)
DeviceProfile makeCustomProfile (const String& name, const String& manufacturer, const String& format, const juce::StringArray& controlLines);
const DeviceProfile* findProfile (const String& id);

inline String valueKey (const String& deviceId, const String& controlId)   { return deviceId + ":" + controlId; }

//==============================================================================
// Snapshot = nastaveni celeho racku
//==============================================================================
struct ControlValue
{
    bool known = false;        // false = NEZNAME (nikdy se nedoplnuje odhadem)
    double pos = 0;            // knob 0..1, jinak index polohy
    String source = "manual";  // "manual" | "recognized" (z fotky) | "preset"
    double confidence = -1;
    bool confirmed = true;     // false = precteno z fotky a jeste nezkontrolovano rukou
    bool operator== (const ControlValue& o) const { return known == o.known && (! known || (pos == o.pos && source == o.source && confirmed == o.confirmed)); }
    bool operator!= (const ControlValue& o) const { return ! (*this == o); }
};

// Kabel na prednim panelu patchbaye (dokumentace zapojeni, nic se neprepina).
// Bod = "<patchbayId>:<T|B><kanal>", napr. "samson.spatchplus:T5" (T = horni rada, B = dolni rada).
struct Cable
{
    String from, to, note;      // from = zdroj signalu (vystup), to = cil (vstup)
    bool operator== (const Cable& o) const { return from == o.from && to == o.to && note == o.note; }
};

struct Snapshot
{
    String id;
    String project;            // skladba
    String version;            // "v3" - prideluje se automaticky
    String notes;
    String deviceId = kRackId;
    String name;               // vlastni nazev verze (prejmenovani); prazdne = jen "v3"
    String group;              // skupina (pracovni postup), ke ktere verze patri; prazdne = cely rack (starsi verze)
    juce::StringArray photos;  // nazvy souboru ve slozce Photos (relativni!)
    std::map<String, String> devicePhotos;   // kus -> posledni fotka (nazev v Photos); fotka je jen odkaz
    std::vector<Cable> cables;               // kabely v patchbayi
    juce::var defs;                          // definice ovladacu v dobe ulozeni (profil se pozdeji muze zmenit)
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
    // hodnoty jen danych kusu (pro ukladani po skupinach)
    std::map<String, ControlValue> valuesFor (const juce::StringArray& instances) const;
    int numKnown() const;
    int numKnown (const DeviceProfile&) const;
    String title() const;      // "Track_07 - v3"
};

int totalControls();

// Otisk definic pristroju pouzitych v nastaveni (verze profilu, ovladace, pocty poloh, stupnice) - uklada se
// s projektem a s verzemi, aby pozdejsi zmena knihovny hardwaru nic tise neprevykladala.
juce::var definitionsFor (const Snapshot&);
juce::StringArray checkDefinitions (const juce::var& defs);    // co se od ulozeni zmenilo / chybi (prazdne = nic)

// Rozdil dvou nastaveni (porovnani verzi): jen ovladace, ktere se lisi
struct Difference { String instance, control; ControlValue a, b; };
std::vector<Difference> compare (const Snapshot& a, const Snapshot& b, const juce::StringArray& instances = {});

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
    String lunchboxModel;            // sasi (napr. "Heritage Audio OST-10 v2.0"), "" = obecny lunchbox
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
    String add (const String& model, const String& wantedId = {});  // novy kus (nebo kus z jine skupiny); vraci jeho ID ("" = nejde)
    bool remove (const String& id);
    bool place500 (const String& id, int slot);                   // presun modulu na slot (ostatni se odsunou, je-li misto)
    void move (const String& id, int newIndex);                   // poradi radku
    String displayName (const String& id) const;                  // "xmax 500" / "xmax 500 (2)"
    void normalise();                                             // nezname modely ven, sloty bez prekryvu
    bool operator== (const RackLayout& o) const;
    bool operator!= (const RackLayout& o) const   { return ! (*this == o); }
};
int versionNumber (const String& version);                   // "v12" -> 12

//==============================================================================
// Studio = skupiny (Mixing, Mastering, vlastni) s vlastnim rackem + patchbaye. Dlouhodobe, spolecne
// pro vsechny projekty: ~/Music/Rack Memory/rack.json. Kus (instance) muze byt ve vic skupinach -
// je to porad ten samy fyzicky pristroj (stejne hodnoty), definice pristroje je jen jedna (profil).
//==============================================================================
struct RackGroup
{
    String id, name;
    RackLayout layout;
};

struct PatchbayConfig
{
    String id;                                  // napr. "samson.spatchplus", druhy kus "...#2"
    String model;                               // profil patchbaye
    std::map<String, String> labels;            // "T5" -> "WA-2A IN" (co je zezadu zapojene)
    std::map<int, String> modes;                // kanal 1..N -> "N" | "HN" | "T" (podle modelu)
    String notes;
};

struct Studio
{
    static constexpr int kSchema = 2;
    std::vector<RackGroup> groups;              // poradi = poradi zalozek
    std::vector<PatchbayConfig> patchbays;

    static Studio makeDefault();
    static Studio fromLegacy (const RackLayout&);       // rack.json schema 1 -> skupina Mastering (+ prazdny Mixing)
    juce::var toVar() const;
    static bool fromVar (const juce::var&, Studio&, String& error);

    RackGroup* findGroup (const String& id);
    const RackGroup* findGroup (const String& id) const;
    String addGroup (const String& name);               // vraci ID
    String duplicateGroup (const String& id, const String& newName);
    bool removeGroup (const String& id);                // posledni skupina zustane
    bool renameGroup (const String& id, const String& name);
    void moveGroup (const String& id, int newIndex);
    juce::StringArray allInstances() const;             // vsechny kusy ve vsech skupinach
    String newInstanceId (const String& model) const;   // unikatni pres vsechny skupiny
    String addPatchbay (const String& model);
    PatchbayConfig* findPatchbay (const String& id);
    void normalise();
    bool operator== (const Studio& o) const;
    bool operator!= (const Studio& o) const   { return ! (*this == o); }
};

//==============================================================================
// Preset pristroje: znovupouzitelne nastaveni jednoho modelu (nezavisle na skladbach)
struct DevicePreset
{
    String id, name, model;
    juce::int64 modified = 0;
    std::map<String, ControlValue> values;      // klic = ID ovladace
    juce::var toVar() const;
    static bool fromVar (const juce::var&, DevicePreset&, String& error);
};

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
    juce::File presetsDir() const       { return root.getChildFile ("Presets"); }
    juce::File recoveryDir() const      { return root.getChildFile ("Recovery"); }
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
    bool loadStudio (Studio&, String& error) const;      // rack.json (i stary format = 1 rack)
    juce::Result saveStudio (const Studio&) const;
    bool hasStudio() const               { return rackFile().existsAsFile() || rackFile().withFileExtension ("json.bak").existsAsFile(); }

    // presety pristroju: <root>/Presets/<model>/<id>.json
    std::vector<DevicePreset> loadPresets (const String& model) const;
    juce::Result savePreset (DevicePreset&) const;
    juce::Result removePreset (const String& model, const String& id) const;

    // Zkopiruje fotku do knihovny (original se nemeni). Vraci nazev souboru, nebo "" + error.
    String importPhoto (const juce::File& source, const String& snapshotId, String& error) const;

private:
    juce::File root;
};

} // namespace rm
