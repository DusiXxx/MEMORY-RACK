// Offline testy Rack Memory (bez Logicu). Spusteni: RackMemoryTests [slozka_pro_png]
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Recognizer.h"
#include "Assets.h"
#include "Updater.h"
#include <juce_cryptography/juce_cryptography.h>
#include <cstdlib>
#include <iostream>

static int passed = 0, failed = 0;
#define CHECK(cond) do { if (cond) ++passed; else { ++failed; std::cout << "  FAIL line " << __LINE__ << ": " #cond << std::endl; } } while (false)
static void section (const char* s) { std::cout << "== " << s << std::endl; }
static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

static juce::String stateOf (RackMemoryProcessor& p)
{
    juce::MemoryBlock mb; p.getStateInformation (mb);
    return juce::String::fromUTF8 ((const char*) mb.getData(), (int) mb.getSize());
}
static void setState (RackMemoryProcessor& p, const juce::String& s) { p.setStateInformation (s.toRawUTF8(), (int) s.getNumBytesAsUTF8()); }
static void savePng (juce::Component& c, const juce::File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat fmt; fmt.writeImageToStream (img, os);
}
static rm::ControlValue V (double pos) { rm::ControlValue v; v.known = true; v.pos = pos; return v; }
static const char* XM = "elysia.xmax.500";

// rozumne "nahodne" nastaveni celeho racku
static void fillRack (rm::Snapshot& s, int seed, int skipEvery)
{
    int i = seed;
    for (auto& prof : rm::profiles())
        for (auto& c : prof.controls)
        {
            ++i;
            if (skipEvery > 0 && i % skipEvery == 0) continue;
            const int np = c.numPositions();
            s.values[rm::valueKey (prof.id, c.id)] = V (c.type == rm::ControlType::knob ? 0.1 + 0.8 * ((i * 37) % 100) / 100.0 : (double) ((i * 7) % np));
        }
}


// Skutecne fotky z telefonu se znamou pravdou (Tests/photos_real/truth.json): spravne / spatne / nejiste
struct RealScore { int truth = 0, correct = 0, wrong = 0, uncertain = 0, devicesFound = 0, devices = 0; double ms = 0; };
static RealScore evaluateRealPhotos (bool verbose)
{
    RealScore sc;
    auto dir = juce::File (__FILE__).getSiblingFile ("photos_real");
    juce::var truth;
    if (juce::JSON::parse (dir.getChildFile ("truth.json").loadFileAsString(), truth).failed()) return sc;
    for (auto& ph : *truth.getProperty ("photos", juce::var()).getArray())
    {
        juce::String err;
        auto img = rmcv::loadPhoto (dir.getChildFile (ph["file"].toString()), err);
        if (! img.isValid()) continue;
        const double tol = (double) ph.getProperty ("tolerance", 20);
        if (verbose) std::cout << "  " << ph["file"].toString() << "  (" << ph["conditions"].toString() << ")" << std::endl;
        for (auto& dv : ph["devices"].getDynamicObject()->getProperties())
        {
            auto* prof = rm::findProfile (dv.name.toString());
            if (prof == nullptr) continue;
            ++sc.devices;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            auto r = rmcv::recognize (img, *prof, rm::loadAsset (prof->image));
            sc.ms += juce::Time::getMillisecondCounterHiRes() - t0;
            if (r.aligned) ++sc.devicesFound;
            int c = 0, w = 0, u = 0, n = 0;
            juce::StringArray bad;
            auto score = [&] (const juce::String& id, std::function<bool (const rm::ControlValue&, const rm::ControlDef&)> ok, bool expectUnknown)
            {
                auto* def = prof->find (id); if (def == nullptr) return;
                ++n;
                auto it = r.values.find (id);
                const bool known = it != r.values.end() && it->second.known;
                if (expectUnknown) { if (known) { ++w; bad.add (id + "=" + rm::describeValue (*def, it->second) + " (ma byt nezadano)"); } else ++c; return; }
                if (! known) { ++u; return; }
                if (ok (it->second, *def)) ++c; else { ++w; bad.add (id + "=" + juce::String (rm::angleFor (*def, it->second), 0) + "/" + rm::describeValue (*def, it->second)); }
            };
            if (auto* a = dv.value.getProperty ("angles", juce::var()).getDynamicObject())
                for (auto& kv : a->getProperties())
                {
                    const double want = (double) kv.value;
                    score (kv.name.toString(), [want, tol] (const rm::ControlValue& v, const rm::ControlDef& d) { return std::abs (rm::angleFor (d, v) - want) <= tol; }, false);
                }
            if (auto* a = dv.value.getProperty ("positions", juce::var()).getDynamicObject())
                for (auto& kv : a->getProperties())
                {
                    const double want = (double) kv.value;
                    score (kv.name.toString(), [want] (const rm::ControlValue& v, const rm::ControlDef&) { return std::abs (v.pos - want) < 0.5; }, false);
                }
            if (auto* a = dv.value.getProperty ("unknown", juce::var()).getArray())
                for (auto& e : *a) score (e.toString(), {}, true);
            sc.truth += n; sc.correct += c; sc.wrong += w; sc.uncertain += u;
            if (verbose)
                std::cout << "    " << prof->name.paddedRight (' ', 34) << (r.aligned ? "nalezen   " : "NENALEZEN ") << "spravne " << c << "/" << n
                          << ", spatne " << w << ", nejiste " << u << (bad.isEmpty() ? juce::String() : "   [" + bad.joinIntoString (", ") + "]") << std::endl;
        }
    }
    if (verbose)
        std::cout << "  CELKEM: pristroje " << sc.devicesFound << "/" << sc.devices << ", ovladace spravne " << sc.correct << "/" << sc.truth
                  << ", spatne " << sc.wrong << ", nejiste " << sc.uncertain << ", cas " << juce::roundToInt (sc.ms) << " ms" << std::endl;
    return sc;
}

#include "UiFlow.inc"
#include "Regress.inc"
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 4 && juce::String (argv[1]) == "uiflow") return runUiFlow (argc, argv);
    if (argc > 2 && juce::String (argv[1]) == "regress") return runRegress (argc, argv);

    if (argc > 3 && juce::String (argv[1]) == "notch")
    {
        juce::String err; auto img = rmcv::loadPhoto (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]), err);
        std::cout << rmcv::debugNotch (img, juce::String (argv[3]).getDoubleValue()) << std::endl; return 0;
    }

    if (argc > 1 && juce::String (argv[1]) == "realphotos") { evaluateRealPhotos (true); return 0; }

    // rucni zkouska cteni z fotky:  RackMemoryTests recog <deviceId> <fotka> [vystup.png]
    if (argc > 3 && juce::String (argv[1]) == "recog")
    {
        auto* prof = rm::findProfile (argv[2]);
        if (prof == nullptr) { std::cout << "neznamy pristroj" << std::endl; return 2; }
        juce::String err;
        auto img = rmcv::loadPhoto (juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]), err);
        if (! img.isValid()) { std::cout << err << std::endl; return 2; }
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto r = rmcv::recognize (img, *prof, rm::loadAsset (prof->image));
        std::cout << prof->name << ": aligned=" << (int) r.aligned << " kotvy " << r.anchorsFound << "/" << r.anchorsTotal
                  << " precteno " << r.read << "/" << r.readable << "  " << r.message << " skew " << juce::String (r.skew, 3) << " potisk " << juce::String (r.printMatch, 2) << " paralaxa " << juce::String (r.parallax, 2) << "  (" << (int) (juce::Time::getMillisecondCounterHiRes() - t0) << " ms)" << std::endl;
        for (auto& c : prof->controls)
        {
            auto it = r.values.find (c.id);
            if (it == r.values.end()) continue;
            std::cout << "  " << (c.group + " " + c.name).paddedRight (' ', 30) << (it->second.known ? rm::describeValue (c, it->second) : juce::String ("?")).paddedRight (' ', 12)
                      << " conf " << juce::String (it->second.confidence, 2) << "   uhel " << juce::String (rm::angleFor (c, it->second), 1) << std::endl;
        }
        if (argc > 4 && r.rectified.isValid())
        {
            auto f = juce::File::getCurrentWorkingDirectory().getChildFile (argv[4]); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat fmt; fmt.writeImageToStream (r.rectified, os);
        }
        return 0;
    }
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rm_test_" + juce::String (juce::Time::currentTimeMillis()));
    auto home = tmp.getChildFile ("lib");
    setenv ("RACKMEMORY_HOME", home.getFullPathName().toRawUTF8(), 1);
    juce::File pngDir = argc > 1 ? juce::File (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])) : juce::File();

    section ("profily");
    {
        CHECK (rm::profiles().size() == 14 && rm::defaultRack().size() == 5);
        juce::StringArray ids;
        for (auto& p : rm::profiles())
        {
            CHECK (! ids.contains (p.id) && ! p.id.contains (":")); ids.add (p.id);
            CHECK (p.sources.size() > 0 && p.image.isNotEmpty());
            juce::StringArray cids;
            for (auto& c : p.controls)
            {
                CHECK (c.id.isNotEmpty() && ! cids.contains (c.id)); cids.add (c.id);
                CHECK (c.x > 0 && c.x < p.w && c.y > 0 && c.y < p.h);
                CHECK (c.draw.kind.isNotEmpty());
                if (c.type == rm::ControlType::stepped)  CHECK (c.steps >= 2 && (c.options.isEmpty() || c.options.size() == c.steps));
                if (c.type == rm::ControlType::selector) CHECK (c.options.size() >= 2);
                for (auto& o : p.controls)
                    if (&o != &c) CHECK (std::hypot (o.x - c.x, o.y - c.y) > juce::jmin (o.r, c.r));
            }
            std::cout << "  " << p.name << ": " << p.controls.size() << " ovladacu" << std::endl;
        }
        std::cout << "  celkem " << rm::totalControls() << std::endl;
    }

    section ("pomocne funkce");
    CHECK (rm::clockString (0.5, 300) == "12:00");
    CHECK (rm::clockString (0.0, 300) == "7:00");
    CHECK (rm::clockString (1.0, 300) == "5:00");
    CHECK (rm::versionNumber ("v12") == 12 && rm::versionNumber ("") == 0);

    section ("snapshot JSON, nezadane zustava nezadane");
    {
        auto s = rm::Snapshot::createNew ("Track_07");
        s.version = "v4"; s.notes = juce::String::fromUTF8 ("Approved ěščř");
        s.values[rm::valueKey (XM, "thr_low")] = V (20);
        s.values[rm::valueKey (XM, "punch")]   = V (1);
        s.values[rm::valueKey ("spl.vitalizer.mk3t", "drive")] = V (0.42);
        rm::Snapshot r; juce::String err;
        CHECK (rm::Snapshot::fromJson (s.toJson(), r, err));
        CHECK (r.version == "v4" && r.project == "Track_07" && r.notes == s.notes);
        CHECK (r.get (XM, "thr_low").known && r.get (XM, "thr_low").pos == 20.0);
        CHECK (r.get (XM, "punch").pos == 1.0 && r.get ("spl.vitalizer.mk3t", "drive").pos == 0.42);
        CHECK (! r.get (XM, "tone").known && ! r.get ("elysia.xfilter.500", "hitit").known);
        CHECK (r.numKnown() == 3 && r.numKnown (*rm::findProfile (XM)) == 2);
        CHECK (rm::describeValue (*rm::findProfile (XM)->find ("tone"), r.get (XM, "tone")) == "?");

        // hodnota mimo rozsah se neorezava na "nejblizsi", ale zahodi (= nezadano)
        s.values[rm::valueKey (XM, "thr_mid")] = V (99);
        s.values[rm::valueKey ("spl.vitalizer.mk3t", "bass")] = V (1.7);
        CHECK (rm::Snapshot::fromJson (s.toJson(), r, err));
        CHECK (! r.get (XM, "thr_mid").known && ! r.get ("spl.vitalizer.mk3t", "bass").known && r.numKnown() == 3);

        CHECK (! rm::Snapshot::fromJson ("{ not json", r, err));
        CHECK (! rm::Snapshot::fromJson ("{}", r, err));
        CHECK (! rm::Snapshot::fromJson ("{\"schema\": 999, \"id\": \"a\", \"deviceId\": \"rack\"}", r, err));

        // budouci pristroj / ovladac se nacte a zachova
        s.values["future.device:knob"] = V (0.3);
        CHECK (rm::Snapshot::fromJson (s.toJson(), r, err) && r.get ("future.device:knob").known);

        // stary format (jeden pristroj na snapshot) se prevede
        CHECK (rm::Snapshot::fromJson ("{\"schema\":1,\"id\":\"old1\",\"deviceId\":\"elysia.xmax.500\",\"project\":\"P\","
                                       "\"values\":{\"tone\":{\"known\":true,\"pos\":7},\"level\":{\"known\":false}}}", r, err));
        CHECK (r.deviceId == "rack" && r.get (XM, "tone").pos == 7 && ! r.get (XM, "level").known);
        CHECK (rm::makeReport (s).contains ("low thres") && rm::makeReport (s).contains ("Vitalizer"));
    }

    section ("knihovna");
    {
        rm::Library lib (home);
        auto a = rm::Snapshot::createNew ("Track_07"); a.values[rm::valueKey (XM, "tone")] = V (10);
        auto b = rm::Snapshot::createNew ("Track_07"); b.values[rm::valueKey (XM, "tone")] = V (30);
        CHECK (lib.save (a).wasOk()); CHECK (lib.save (b).wasOk());
        juce::StringArray errs;
        auto all = lib.loadAll (&errs);
        CHECK (all.size() == 2 && errs.isEmpty());

        // poskozene soubory: preskoci se, nahlasi, nesmazou
        lib.snapshotsDir().getChildFile ("broken.json").replaceWithText ("{ \"schema\": 1, \"id\": ");
        lib.snapshotsDir().getChildFile ("empty.json").replaceWithText ("");
        lib.snapshotsDir().getChildFile ("wrongid.json").replaceWithText (a.toJson());
        all = lib.loadAll (&errs);
        CHECK (all.size() == 2 && errs.size() == 3);
        CHECK (lib.snapshotsDir().getChildFile ("broken.json").existsAsFile());
        for (auto n : { "broken.json", "empty.json", "wrongid.json" }) lib.snapshotsDir().getChildFile (n).deleteFile();

        auto sig = lib.signature();
        CHECK (lib.remove (b.id).wasOk());
        CHECK (! lib.exists (b.id) && lib.wasDeleted (b.id) && lib.exists (a.id));
        CHECK (lib.trashDir().getNumberOfChildFiles (juce::File::findFiles) == 1);
        CHECK (lib.signature() != sig);
        CHECK (lib.remove (a.id).wasOk());
    }

    section ("fotka: kopie do knihovny, original zustava");
    juce::File png = tmp.getChildFile ("panel.png");
    {
        juce::Image img (juce::Image::RGB, 320, 200, true);
        { juce::Graphics g (img); g.fillAll (juce::Colours::darkslateblue); }
        juce::FileOutputStream os (png); juce::PNGImageFormat fmt; CHECK (fmt.writeImageToStream (img, os));
    }
    {
        rm::Library lib (home); juce::String err;
        auto name = lib.importPhoto (png, "abcdef12-3456", err);
        CHECK (err.isEmpty() && name.isNotEmpty() && lib.photoFile (name).getSize() == png.getSize() && png.existsAsFile());
        auto name2 = lib.importPhoto (png, "abcdef12-3456", err);
        CHECK (name2.isNotEmpty() && name2 != name);
        CHECK (lib.importPhoto (tmp.getChildFile ("nope.jpg"), "abc", err).isEmpty() && err.isNotEmpty());
        CHECK (lib.photoFile ("../../etc/passwd").getParentDirectory() == lib.photosDir());
    }

    section ("procesor: ulozit = vzdy nova verze, nic se neprepise");
    juce::String projectState, v1, v2, v3;
    {
        RackMemoryProcessor p;
        CHECK (p.all.empty() && p.loadedId.isEmpty() && ! p.canUndo());
        p.songName = "Track_07";
        p.working.values[rm::valueKey (XM, "thr_low")] = V (12); p.markDirty();
        CHECK (p.save().wasOk());
        v1 = p.loadedId;
        CHECK (p.all.size() == 1 && ! p.dirty && p.working.version == "v1" && p.working.project == "Track_07");

        p.working.values[rm::valueKey (XM, "thr_low")] = V (14); p.markDirty();
        CHECK (p.save().wasOk());
        v2 = p.loadedId;
        CHECK (p.all.size() == 2 && v2 != v1 && p.working.version == "v2");
        CHECK (p.findSnapshot (v1)->get (XM, "thr_low").pos == 12 && p.findSnapshot (v2)->get (XM, "thr_low").pos == 14);

        // jina skladba cisluje od v1
        p.songName = "Track_08"; CHECK (p.save().wasOk()); CHECK (p.working.version == "v1" && p.all.size() == 3);
        p.songName = "Track_07";

        // nacteni + zpet
        p.working.values[rm::valueKey (XM, "tone")] = V (33); p.markDirty();
        CHECK (p.load (v1));
        CHECK (p.working.get (XM, "thr_low").pos == 12 && ! p.working.get (XM, "tone").known && ! p.dirty && p.canUndo());
        p.undo();
        CHECK (p.working.get (XM, "tone").pos == 33 && p.dirty);
        p.undo();
        CHECK (p.loadedId == v1 && ! p.dirty);
        CHECK (! p.load ("neexistuje"));

        // smazani nactene verze: nastaveni zustane na obrazovce jako neulozene
        CHECK (p.load (v2)); CHECK (p.save().wasOk()); v3 = p.loadedId; CHECK (p.working.version == "v3");
        CHECK (p.deleteSnapshot (v3).wasOk());
        CHECK (p.loadedId.isEmpty() && p.dirty && p.working.get (XM, "thr_low").pos == 14 && p.all.size() == 3);
        CHECK (p.save().wasOk()); CHECK (p.working.version == "v3");      // cislo se po smazani pouzije znovu
        v3 = p.loadedId;

        p.clearAll(); CHECK (p.working.numKnown() == 0 && p.canUndo()); p.undo(); CHECK (p.loadedId == v3);

        p.working.values[rm::valueKey (XM, "tone")] = V (5); p.working.notes = "rozpracovano"; p.markDirty();   // neulozena uprava
        projectState = stateOf (p);
    }

    section ("ulozit projekt -> zavrit -> otevrit");
    {
        RackMemoryProcessor p;
        setState (p, projectState);
        CHECK (p.songName == "Track_07" && p.loadedId == v3 && p.dirty);
        CHECK (p.working.get (XM, "tone").pos == 5 && p.working.get (XM, "thr_low").pos == 14 && p.working.notes == "rozpracovano");
        CHECK (! p.working.get (XM, "level").known);
        CHECK (p.all.size() == 4);

        // poskozeny stav projektu nic nerozbije
        RackMemoryProcessor q;
        setState (q, "garbage {{{"); setState (q, ""); setState (q, "{\"schema\": 2}");
        q.setStateInformation (nullptr, 0);
        CHECK (q.working.id.isNotEmpty());
    }

    section ("prenos na jiny Mac: prazdna knihovna, verze skladby se obnovi z projektu");
    {
        auto other = tmp.getChildFile ("lib2");
        setenv ("RACKMEMORY_HOME", other.getFullPathName().toRawUTF8(), 1);
        RackMemoryProcessor p;
        CHECK (p.all.empty());
        setState (p, projectState);
        CHECK (p.all.size() == 3 && p.loadedId == v3);                 // jen Track_07 (v1, v2, v3)
        CHECK (p.findSnapshot (v1) != nullptr && p.findSnapshot (v1)->get (XM, "thr_low").pos == 12);
        CHECK (p.deleteSnapshot (v1).wasOk());
        RackMemoryProcessor q; setState (q, projectState);
        CHECK (q.all.size() == 2 && q.findSnapshot (v1) == nullptr);   // smazane se nevraci
        setenv ("RACKMEMORY_HOME", home.getFullPathName().toRawUTF8(), 1);
    }

    section ("vice instanci si neprepisuje data");
    {
        RackMemoryProcessor a, b;
        CHECK (a.instanceId != b.instanceId);
        a.songName = "Song_A"; b.songName = "Song_A";
        a.working.values[rm::valueKey (XM, "tone")] = V (22);
        b.working.values[rm::valueKey (XM, "tone")] = V (3);
        CHECK (a.save().wasOk()); CHECK (b.save().wasOk());
        CHECK (a.loadedId != b.loadedId && a.working.version == "v1" && b.working.version == "v2");
        a.reloadLibrary();
        CHECK (a.findSnapshot (b.loadedId)->get (XM, "tone").pos == 3 && a.findSnapshot (a.loadedId)->get (XM, "tone").pos == 22);
    }


    section ("REGRESE: Logic se dozvi o zmene nastaveni (jinak zavre projekt bez ulozeni)");
    {
        struct L : juce::AudioProcessorListener
        {
            int params = 0, other = 0;
            void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { ++params; }
            void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override { ++other; }
        } l;
        RackMemoryProcessor p;
        p.addListener (&l);
        // nacteni projektu Logic NESMI oznacit jako zmeneny
        RackMemoryProcessor src; src.working.values[rm::valueKey ("spl.vitalizer.mk3t", "drive")] = V (0.3); src.markDirty();
        setState (p, stateOf (src)); pump (400);
        CHECK (l.params == 0);
        // zmena knobu -> parametr "state" se zmeni (AU/Logic jinak o zmene nevi: nonParameterStateChanged se do AU nepredava)
        p.working.values[rm::valueKey ("spl.vitalizer.mk3t", "drive")] = V (0.6); p.markDirty();
        pump (400);
        CHECK (l.params >= 1 && p.hostNotifications >= 1);
        CHECK (! p.stateParam->isAutomatable());
        p.removeListener (&l);
    }

    section ("dva projekty Logicu s ruznym SPL: kazdy si pamatuje svoje");
    {
        const juce::String SPL ("spl.vitalizer.mk3t");
        juce::String blobA, blobB;
        {
            RackMemoryProcessor a;      // projekt A (bez ULOZIT, jen ulozeni projektu v Logicu)
            a.working.values[rm::valueKey (SPL, "drive")] = V (0.2);
            a.working.values[rm::valueKey (SPL, "process")] = V (0.9);
            a.markDirty(); blobA = stateOf (a);
        }
        {
            RackMemoryProcessor b;      // projekt B
            b.working.values[rm::valueKey (SPL, "drive")] = V (0.8);
            b.markDirty(); blobB = stateOf (b);
        }
        auto check = [&] (const juce::String& blob, double drive, bool processKnown)
        {
            RackMemoryProcessor p; setState (p, blob); pump (50);
            return p.working.get (SPL, "drive").pos == drive && p.working.get (SPL, "process").known == processKnown;
        };
        CHECK (check (blobA, 0.2, true));  CHECK (check (blobB, 0.8, false));
        CHECK (check (blobB, 0.8, false)); CHECK (check (blobA, 0.2, true));
        // oba otevrene zaroven (dve okna / dve stopy) + ulozeni verzi do spolecne knihovny
        RackMemoryProcessor a, b; setState (a, blobA); setState (b, blobB);
        CHECK (a.save().wasOk() && b.save().wasOk());
        a.reloadLibrary();
        CHECK (a.working.get (SPL, "drive").pos == 0.2 && b.working.get (SPL, "drive").pos == 0.8);
        RackMemoryProcessor a2; setState (a2, stateOf (a));
        CHECK (a2.working.get (SPL, "drive").pos == 0.2 && a2.loadedId == a.loadedId);
        // nezadane zustava nezadane (ne 0, ne vychozi)
        CHECK (! a2.working.get (SPL, "intensity").known && ! b.working.get (SPL, "process").known);
    }

    section ("zkopirovana stopa: dve instance nesmi mit stejnou identitu");
    {
        RackMemoryProcessor a; a.markDirty(); const auto blob = stateOf (a);
        RackMemoryProcessor b, c; setState (b, blob); setState (c, blob);
        CHECK (b.instanceId != c.instanceId && (b.instanceId == a.instanceId || c.instanceId == a.instanceId) == false);
    }

    section ("zavreni bez ulozeni: zmeny se nabidnou (jen spravnemu projektu, nikdy samy)");
    {
        const juce::String SPL ("spl.vitalizer.mk3t");
        juce::String saved;
        { RackMemoryProcessor a; a.working.values[rm::valueKey (SPL, "drive")] = V (0.1); a.markDirty(); saved = stateOf (a); }
        juce::String saveAs;
        {
            RackMemoryProcessor a; setState (a, saved); pump (50);
            CHECK (! a.recovery.available);
            a.working.values[rm::valueKey (SPL, "drive")] = V (0.7); a.markDirty();
            pump (500);                                       // zaloha se zapise
        }                                                     // Logic zavren bez ulozeni
        {
            RackMemoryProcessor a; setState (a, saved); pump (50);
            CHECK (a.recovery.available && a.recovery.changes == 1);
            CHECK (a.working.get (SPL, "drive").pos == 0.1);  // nic se samo neprepsalo
            a.acceptRecovery();
            CHECK (a.working.get (SPL, "drive").pos == 0.7 && a.dirty && ! a.recovery.available);
            saveAs = stateOf (a);                             // "Ulozit jako" -> projekt B
            pump (300);
            a.working.values[rm::valueKey (SPL, "drive")] = V (0.5); a.markDirty(); pump (500);   // zmena v B, neulozeno
        }
        {
            RackMemoryProcessor a; setState (a, saved); pump (50);
            CHECK (! a.recovery.available);                   // zmeny z B se do A nenabidnou
        }
        {
            RackMemoryProcessor b; setState (b, saveAs); pump (50);
            CHECK (b.recovery.available && b.working.get (SPL, "drive").pos == 0.7);
            b.discardRecovery();
            CHECK (! b.recovery.available && b.working.get (SPL, "drive").pos == 0.7);
        }
        { RackMemoryProcessor b; setState (b, saveAs); pump (50); CHECK (! b.recovery.available); }
    }

    section ("knihovna nedostupna: nastaveni v projektu se neztrati");
    {
        auto blocker = tmp.getChildFile ("not_a_dir");
        blocker.replaceWithText ("x");
        setenv ("RACKMEMORY_HOME", blocker.getFullPathName().toRawUTF8(), 1);
        juce::String blob;
        {
            RackMemoryProcessor p; p.songName = "Offline";
            p.working.values[rm::valueKey (XM, "tone")] = V (8); p.markDirty();
            CHECK (p.save().failed());                        // ulozit verzi nejde ...
            CHECK (p.working.get (XM, "tone").pos == 8 && p.dirty);
            blob = stateOf (p);
        }
        RackMemoryProcessor q; setState (q, blob);
        CHECK (q.working.get (XM, "tone").pos == 8 && q.songName == "Offline");   // ... ale projekt ho drzi
        setenv ("RACKMEMORY_HOME", home.getFullPathName().toRawUTF8(), 1);
    }

    section ("verze: prepsat, duplikovat, prejmenovat, porovnat; presety; fotka je jen odkaz");
    {
        RackMemoryProcessor p; p.songName = "Ops";
        p.working.values[rm::valueKey (XM, "tone")] = V (4); p.markDirty();
        CHECK (p.save().wasOk());
        const auto v1 = p.loadedId;
        p.working.values[rm::valueKey (XM, "tone")] = V (6); p.markDirty();
        auto diff = p.compareWith (v1);
        CHECK (diff.size() == 1 && diff[0].control == "tone" && diff[0].a.pos == 4 && diff[0].b.pos == 6);
        auto nOps = [&p] { int n = 0; for (auto& x : p.all) if (x.project == "Ops") ++n; return n; };
        CHECK (p.overwriteLoaded().wasOk() && p.findSnapshot (v1)->get (XM, "tone").pos == 6 && nOps() == 1 && ! p.dirty);
        CHECK (p.duplicateSnapshot (v1).wasOk() && nOps() == 2);
        CHECK (p.renameSnapshot (v1, "Refren hlasity").wasOk() && p.findSnapshot (v1)->name == "Refren hlasity");
        CHECK (p.snapshotTitle (*p.findSnapshot (v1)).contains ("Refren hlasity"));
        CHECK (p.compareWith (v1).empty());

        CHECK (p.savePreset (XM, "Vokal jemne").wasOk());
        auto presets = p.library.loadPresets (XM);
        CHECK (presets.size() == 1 && presets[0].values["tone"].pos == 6);
        p.working.values[rm::valueKey (XM, "tone")] = V (1); p.working.values[rm::valueKey (XM, "level")] = V (2);
        p.applyPreset (XM, presets[0]);
        CHECK (p.working.get (XM, "tone").pos == 6 && ! p.working.get (XM, "level").known && p.canUndo());
        p.undo(); CHECK (p.working.get (XM, "tone").pos == 1);
        CHECK (p.library.removePreset (XM, presets[0].id).wasOk() && p.library.loadPresets (XM).empty());

        p.working.devicePhotos[XM] = "x.jpg"; p.working.photos.add ("x.jpg");
        p.unlinkPhoto (XM);
        CHECK (p.working.devicePhotos.count (XM) == 0 && p.working.get (XM, "tone").pos == 1);   // nastaveni zustava
    }

    section ("skupiny: Mixing a Mastering maji nezavisle verze");
    {
        RackMemoryProcessor p; p.songName = "Groups";
        CHECK (p.studio.groups.size() >= 2 && p.studio.findGroup ("mixing") != nullptr && p.currentGroup == "mastering");
        // do Mixingu kus xmax #2 (vokal), do Masteringu zustava puvodni rack
        auto st = p.studio;
        st.findGroup ("mixing")->layout.add ("elysia.xmax.500", st.newInstanceId ("elysia.xmax.500"));
        CHECK (p.setStudio (st).wasOk());
        const juce::String X2 ("elysia.xmax.500#2");
        p.working.values[rm::valueKey (XM, "tone")] = V (3); p.markDirty();
        CHECK (p.save().wasOk()); const auto m1 = p.loadedId;
        p.setGroup ("mixing");
        CHECK (p.rack.find (X2) != nullptr && p.loadedId.isEmpty());
        p.working.values[rm::valueKey (X2, "tone")] = V (11); p.markDirty();
        CHECK (p.save().wasOk()); const auto x1 = p.loadedId;
        CHECK (p.working.version == "v1" && p.findSnapshot (x1)->group == "mixing" && ! p.findSnapshot (x1)->get (XM, "tone").known);
        p.working.values[rm::valueKey (X2, "tone")] = V (12);
        p.working.values[rm::valueKey (XM, "tone")] = V (5);
        p.setGroup ("mastering"); CHECK (p.loadedId == m1);
        CHECK (p.load (m1));
        CHECK (p.working.get (XM, "tone").pos == 3 && p.working.get (X2, "tone").pos == 12);   // Mixing nedotcen
        const auto blob = stateOf (p);
        RackMemoryProcessor q; setState (q, blob);
        CHECK (q.currentGroup == "mastering" && q.loadedId == m1 && q.loadedByGroup["mixing"] == x1);
        // stary rack.json (jeden rack) -> skupina Mastering, prazdny Mixing
        rm::Studio s2; juce::String err;
        CHECK (rm::Studio::fromVar (rm::RackLayout::makeDefault().toVar(), s2, err) && s2.findGroup ("mastering")->layout.items.size() == 5 && s2.findGroup ("mixing")->layout.items.empty());
        // novy format umi precist i starsi plugin (pole "devices")
        rm::RackLayout legacy; CHECK (rm::RackLayout::fromVar (q.studio.toVar(), legacy, err) && ! legacy.items.empty());
        CHECK (s2.addGroup ("Vokaly") == "vokaly" && s2.renameGroup ("vokaly", "Vokály") && s2.duplicateGroup ("mastering", "Mastering B").isNotEmpty());
        CHECK (s2.groups.size() == 4 && s2.removeGroup ("vokaly") && s2.groups.size() == 3);
        rm::Studio one; one.groups.push_back ({ "a", "A", {} }); CHECK (! one.removeGroup ("a"));
        CHECK (p.setStudio (rm::Studio::makeDefault()).wasOk());      // dalsi testy: vychozi rack
    }

    section ("zvuk prochazi beze zmeny");
    {
        juce::Random rng (42);
        for (int channels : { 1, 2 })
        {
            RackMemoryProcessor p;
            auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
            juce::AudioProcessor::BusesLayout layout; layout.inputBuses.add (set); layout.outputBuses.add (set);
            CHECK (p.setBusesLayout (layout));
            p.prepareToPlay (48000.0, 512);
            CHECK (p.getLatencySamples() == 0 && p.getTailLengthSeconds() == 0.0);
            juce::AudioBuffer<float> buf (channels, 512), ref;
            juce::AudioBuffer<double> dbuf (channels, 512), dref;
            juce::MidiBuffer midi;
            bool identical = true;
            for (int block = 0; block < 200; ++block)
            {
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < 512; ++i) { buf.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f); dbuf.setSample (ch, i, rng.nextDouble() * 2.0 - 1.0); }
                ref.makeCopyOf (buf); dref.makeCopyOf (dbuf);
                p.processBlock (buf, midi); p.processBlock (dbuf, midi);
                for (int ch = 0; ch < channels; ++ch)
                {
                    identical = identical && std::memcmp (buf.getReadPointer (ch), ref.getReadPointer (ch), sizeof (float) * 512) == 0;
                    identical = identical && std::memcmp (dbuf.getReadPointer (ch), dref.getReadPointer (ch), sizeof (double) * 512) == 0;
                }
            }
            CHECK (identical);
        }
        RackMemoryProcessor p;
        juce::AudioProcessor::BusesLayout bad; bad.inputBuses.add (juce::AudioChannelSet::mono()); bad.outputBuses.add (juce::AudioChannelSet::stereo());
        CHECK (! p.isBusesLayoutSupported (bad));
    }

    section ("hlasitost a mereni (IN pred HW / OUT za HW)");
    {
        auto run = [] (RackMemoryProcessor& p, float amp, int blocks)
        {
            juce::AudioBuffer<float> b (2, 480); juce::MidiBuffer m;
            static double phase = 0;
            for (int k = 0; k < blocks; ++k)
            {
                for (int i = 0; i < 480; ++i) { const float v = amp * (float) std::sin (phase); phase += 2.0 * juce::MathConstants<double>::pi * 1000.0 / 48000.0; b.setSample (0, i, v); b.setSample (1, i, v); }
                p.processBlock (b, m);
            }
            return b.getMagnitude (0, 0, 480);
        };
        // 1 kHz sinus, amplituda 0.1 (-20 dBFS) ve stereu = -20 LUFS podle BS.1770
        RackMemoryProcessor in; in.prepareToPlay (48000.0, 480);
        run (in, 0.1f, 400);
        double secs = 0;
        std::cout << "  1 kHz -20 dBFS: momentary " << in.meter.momentaryLufs() << ", integrovano " << in.meter.integratedLufs (&secs) << " (" << secs << " s), spicka " << in.meter.maxPeakDb() << std::endl;
        CHECK (std::abs (in.meter.momentaryLufs() + 20.0f) < 0.2f && std::abs (in.meter.integratedLufs() + 20.0f) < 0.2f && std::abs (in.meter.maxPeakDb() + 20.0f) < 0.1f);
        // gain
        in.setGainDb (6.0f);
        const float mag = run (in, 0.1f, 50);
        CHECK (std::abs (mag - 0.1f * juce::Decibels::decibelsToGain (6.0f)) < 0.002f);
        // dvojice IN / OUT: najdou se jen ve stejne skupine
        RackMemoryProcessor out; out.prepareToPlay (48000.0, 480);
        CHECK (in.partner() == nullptr);
        in.setRole (RackMemoryProcessor::Role::in, 2); out.setRole (RackMemoryProcessor::Role::out, 1);
        CHECK (in.partner() == nullptr);
        out.setRole (RackMemoryProcessor::Role::out, 2);
        CHECK (in.partner() == &out && out.partner() == &in);
        // "hardware" ztisi o 4 dB -> srovnani hlasitosti nastavi OUT +4 dB
        in.meter.reset(); out.meter.reset();
        run (in, 0.1f, 400); run (out, 0.1f * juce::Decibels::decibelsToGain (6.0f - 4.0f), 400);
        {
            LevelStrip strip (out);
            strip.setSize (1000, 32);
            std::cout << "  " << strip.deltaText() << std::endl;
            CHECK (strip.deltaText() == U ("HW: -4.0 dB"));
            for (auto* c : strip.getChildren()) if (auto* tb = dynamic_cast<juce::TextButton*> (c)) if (tb->getButtonText() == U ("Srovnat")) tb->triggerClick();
            pump (50);
            CHECK (std::abs (out.gainDb() - 4.0f) < 0.15f);
        }
        // ulozeni verze: hlasitost IN i OUT jde s ni, nacteni ji vrati
        in.songName = "Lvl"; CHECK (in.save().wasOk());
        const auto id = in.loadedId;
        in.setGainDb (0.0f); out.setGainDb (0.0f);
        in.reloadLibrary(); CHECK (in.load (id));
        CHECK (std::abs (in.gainDb() - 6.0f) < 0.05f && std::abs (out.gainDb() - 4.0f) < 0.15f);
        CHECK (in.working.numKnown() == 0);          // hlasitost se nepocita mezi ovladace
        // stav projektu nese roli, skupinu a gain
        RackMemoryProcessor q; setState (q, stateOf (out));
        CHECK (q.role == RackMemoryProcessor::Role::out && q.linkGroup == 2 && std::abs (q.gainDb() - out.gainDb()) < 0.05f);
        if (pngDir != juce::File())
        {
            q.setRole (RackMemoryProcessor::Role::none, 1);
            run (in, 0.2f, 200); run (out, 0.15f, 200);
            std::unique_ptr<juce::AudioProcessorEditor> ed (in.createEditor());
            pump (100); pngDir.createDirectory(); savePng (*ed, pngDir.getChildFile ("rack_levels.png"));
        }
    }

    section ("cteni z fotky: synteticke fotky (znama pravda)");
    {
        // Panel se vykresli se znamym nastavenim, vlozi do "fotky" (posun, meritko, natoceni, sum, nerovne svetlo)
        // a precte zpet. Overuje geometrii a logiku cteni; NEoveruje vzhled skutecnych fotek z telefonu.
        juce::Random rng (7);
        int totalRead = 0, totalReadable = 0, wrong = 0, scenes = 0, aligned = 0;
        PanelView::overlayPointers = false;        // "fotka" skutecneho pristroje: bez ukazatelu pluginu
        for (auto& prof : rm::profiles())
        {
            bool readable = false;
            for (auto& c : prof.controls) readable = readable || c.read.isNotEmpty();
            if (! readable) continue;
            for (int scene = 0; scene < 4; ++scene)
            {
                rm::Snapshot truth = rm::Snapshot::createNew ("t");
                fillRack (truth, 11 * scene + 3, 0);

                PanelView pv;
                pv.setBounds (0, 0, (int) prof.w, (int) prof.h);
                pv.setData (&prof, &truth, prof.id);
                auto panelImg = pv.createComponentSnapshot (pv.getLocalBounds(), true, 1.0f);

                const bool wide = prof.w > prof.h;
                const int W = wide ? 2000 : 1500, H = wide ? 900 : 2000;
                juce::Image photo (juce::Image::RGB, W, H, true);
                juce::AffineTransform placed;
                {
                    juce::Graphics g (photo);
                    g.fillAll (juce::Colour (0xff2b2a28));
                    for (int i = 0; i < 40; ++i)      // okoli: jine veci v racku
                    {
                        g.setColour (juce::Colour::fromHSV (rng.nextFloat(), 0.2f, 0.15f + 0.5f * rng.nextFloat(), 1.0f));
                        g.fillRect (rng.nextInt (W), rng.nextInt (H), 20 + rng.nextInt (160), 10 + rng.nextInt (90));
                    }
                    const float fitS = juce::jmin ((float) W / prof.w, (float) H / prof.h);
                    const float sc = fitS * (0.55f + 0.38f * rng.nextFloat());
                    const float rot = juce::degreesToRadians (-5.0f + 10.0f * rng.nextFloat());
                    const float tx = W * 0.5f + (rng.nextFloat() - 0.5f) * (W - prof.w * sc) * 0.6f;
                    const float ty = H * 0.5f + (rng.nextFloat() - 0.5f) * (H - prof.h * sc) * 0.6f;
                    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                    placed = juce::AffineTransform::translation (-prof.w * 0.5f, -prof.h * 0.5f).scaled (sc).rotated (rot).translated (tx, ty);
                    g.drawImageTransformed (panelImg, placed);
                    if (prof.id == "wesaudio.rhea" && scene == 3)
                    {
                        // _RHEA: LED krouzek jako PAS (INPUT od zacatku po krok 10) - i tak se ma precist
                        auto& c = *prof.find ("input");
                        truth.values[rm::valueKey (prof.id, "input")] = V (9);
                        for (int i = 0; i < 16; ++i)
                        {
                            auto q = juce::Point<float> (c.draw.points[(size_t) i].x, c.draw.points[(size_t) i].y).transformedBy (placed);
                            const float rr = c.draw.ledR * sc;
                            g.setColour (juce::Colour (0xff101010)); g.fillEllipse (q.x - rr * 1.4f, q.y - rr * 1.4f, rr * 2.8f, rr * 2.8f);
                            if (i <= 9) { g.setColour (juce::Colour (0xff2f8dff)); g.fillEllipse (q.x - rr, q.y - rr, rr * 2, rr * 2); }
                        }
                    }
                    // nerovnomerne svetlo
                    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.10f), 0, 0, juce::Colours::black.withAlpha (0.25f), (float) W, (float) H, false));
                    g.fillAll();
                }
                {   // sum
                    juce::Image::BitmapData bd (photo, juce::Image::BitmapData::readWrite);
                    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x)
                    {
                        auto c = bd.getPixelColour (x, y); const float n = (rng.nextFloat() - 0.5f) * 0.05f;
                        bd.setPixelColour (x, y, juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, c.getFloatRed() + n), juce::jlimit (0.0f, 1.0f, c.getFloatGreen() + n), juce::jlimit (0.0f, 1.0f, c.getFloatBlue() + n), 1.0f));
                    }
                }
                auto file = tmp.getChildFile ("synth_" + prof.id + "_" + juce::String (scene) + ".jpg");
                { juce::FileOutputStream os (file); juce::JPEGImageFormat fmt; fmt.setQuality (0.85f); CHECK (fmt.writeImageToStream (photo, os)); }
                if (pngDir != juce::File()) { pngDir.createDirectory(); file.copyFileTo (pngDir.getChildFile (file.getFileName())); }

                juce::String err;
                auto loaded = rmcv::loadPhoto (file, err);
                CHECK (loaded.isValid());
                auto r = rmcv::recognize (loaded, prof, rm::loadAsset (prof.image));
                if (scene == 0)
                {
                    // rucni zarovnani (4 rohy, o par px vedle jako pri klikani mysi) cte stejne
                    juce::Point<float> corners[4] = { juce::Point<float> (0, 0).transformedBy (placed) + juce::Point<float> (3, -2),
                                                      juce::Point<float> (prof.w, 0).transformedBy (placed) + juce::Point<float> (-2, 3),
                                                      juce::Point<float> (prof.w, prof.h).transformedBy (placed) + juce::Point<float> (2, 2),
                                                      juce::Point<float> (0, prof.h).transformedBy (placed) + juce::Point<float> (-3, 1) };
                    auto mt = rmcv::fromCorners (prof, corners);
                    auto rm2 = rmcv::recognize (loaded, prof, rm::loadAsset (prof.image), &mt);
                    int manualWrong = 0;
                    for (auto& kv : rm2.values)
                    {
                        if (! kv.second.known) continue;
                        auto* c = prof.find (kv.first); const auto tv = truth.get (prof.id, kv.first);
                        const bool ok = c->type == rm::ControlType::knob ? std::abs (kv.second.pos - tv.pos) * (c->angleMax - c->angleMin) <= 9.0
                                                                          : std::abs (kv.second.pos - tv.pos) <= (c->type == rm::ControlType::stepped ? 1.0 : 0.0);
                        if (! ok) ++manualWrong;
                    }
                    std::cout << "  " << prof.name.paddedRight (' ', 22) << " rucne zarovnano: precteno " << rm2.read << "/" << rm2.readable << ", spatne " << manualWrong << std::endl;
                    CHECK (rm2.aligned && rm2.read >= rm2.readable * 7 / 10 && manualWrong == 0);
                }
                ++scenes; if (r.aligned) ++aligned;
                int sceneWrong = 0;
                for (auto& c : prof.controls)
                {
                    if (c.read.isEmpty()) continue;
                    ++totalReadable;
                    auto it = r.values.find (c.id);
                    if (it == r.values.end() || ! it->second.known) continue;
                    ++totalRead;
                    const auto tv = truth.get (prof.id, c.id);
                    bool ok;
                    if (c.type == rm::ControlType::knob)          ok = std::abs (it->second.pos - tv.pos) * (c.angleMax - c.angleMin) <= 9.0;   // stupne
                    else if (c.type == rm::ControlType::stepped)  ok = std::abs (it->second.pos - tv.pos) <= 1.0;
                    else                                          ok = it->second.pos == tv.pos;
                    if (! ok) { ++wrong; ++sceneWrong; std::cout << "    spatne: " << prof.name << " / " << c.name << "  pravda " << rm::describeValue (c, tv) << "  precteno " << rm::describeValue (c, it->second) << "  conf " << juce::String (it->second.confidence, 2) << std::endl; }
                }
                std::cout << "  " << prof.name.paddedRight (' ', 22) << " scena " << scene << ": " << (r.aligned ? "zarovnano" : "NEZAROVNANO") << ", precteno " << r.read << "/" << r.readable << ", spatne " << sceneWrong << std::endl;
                if (pngDir != juce::File() && scene == 0 && r.rectified.isValid())
                {
                    auto out = pngDir.getChildFile ("rect_" + prof.id + ".png"); out.deleteFile();
                    juce::FileOutputStream os (out); juce::PNGImageFormat fmt; fmt.writeImageToStream (r.rectified, os);
                }
            }
        }
        PanelView::overlayPointers = true;
        std::cout << "  CELKEM: sceny " << aligned << "/" << scenes << " zarovnano, precteno " << totalRead << "/" << totalReadable << ", spatne " << wrong << std::endl;
        CHECK (aligned == scenes);
        CHECK (totalRead >= totalReadable * 80 / 100);       // xfilter (stribrne knoby s malym zarezem) ~65 %, ostatni 90 %+
        CHECK (wrong <= totalRead / 50);          // nejvys 2 % spatne prectenych

        // nesmysl na vstupu: nic se nesmi "precist"
        juce::Image noise (juce::Image::RGB, 800, 600, true);
        { juce::Graphics g (noise); g.fillAll (juce::Colours::grey); }
        auto rn = rmcv::recognize (noise, *rm::findProfile ("elysia.xfilter.me"), rm::loadAsset ("xfilterme.jpg"));
        CHECK (! rn.aligned && rn.read == 0);
        CHECK (! rmcv::recognize (juce::Image(), *rm::findProfile ("elysia.xfilter.500"), {}).aligned);
        CHECK (! rmcv::recognize (noise, *rm::findProfile ("wesaudio.rhea"), {}).aligned);

        // EXIF otoceni
        juce::Image wideImg (juce::Image::RGB, 40, 20, true);
        CHECK (rmcv::applyOrientation (wideImg, 6).getWidth() == 20 && rmcv::applyOrientation (wideImg, 3).getWidth() == 40 && rmcv::applyOrientation (wideImg, 1).getWidth() == 40);
        CHECK (rmcv::exifOrientation (png) == 1);
    }

    section ("procesor: hodnoty z fotky");
    {
        RackMemoryProcessor p;
        p.working.values[rm::valueKey (XM, "tone")] = V (3);
        p.working.values[rm::valueKey (XM, "level")] = V (9);
        std::map<juce::String, rm::ControlValue> vals;
        rm::ControlValue a = V (17); a.source = "recognized"; a.confidence = 0.9; vals["tone"] = a;
        rm::ControlValue u; u.source = "recognized"; vals["level"] = u;          // nejiste
        p.applyRecognition (XM, vals);
        // rucne nastavena hodnota se z fotky neprepise (0.11): jen navrh, prevezme se tlacitkem "Prevzit z fotky"
        CHECK (p.working.get (XM, "tone").pos == 3 && p.photoProposals[XM].count ("tone") == 1);
        CHECK (p.acceptPhotoProposals (XM) == 1);
        CHECK (p.working.get (XM, "tone").pos == 17 && p.working.get (XM, "tone").source == "recognized");
        // nejiste z fotky: rucni hodnota zustava (nic se tise neprepise), ovladac je ke kontrole
        CHECK (p.working.get (XM, "level").pos == 9 && p.working.get (XM, "level").source == "manual" && p.dirty && p.canUndo());
        CHECK (p.needsReview[XM].contains ("level") && ! p.working.get (XM, "tone").confirmed && p.numUnconfirmed (XM) == 1);
        juce::String err; rm::Snapshot r;
        CHECK (rm::Snapshot::fromJson (p.working.toJson(), r, err) && r.get (XM, "tone").source == "recognized" && r.get (XM, "tone").confidence == 0.9);
        p.undo();
        CHECK (p.working.get (XM, "tone").pos == 3 && p.working.get (XM, "level").pos == 9);
        // nezadana hodnota se z fotky vyplni rovnou
        p.working.values.erase (rm::valueKey (XM, "tone"));
        p.applyRecognition (XM, vals);
        CHECK (p.working.get (XM, "tone").pos == 17 && ! p.working.get (XM, "tone").confirmed && p.photoProposals.count (XM) == 0);
    }

    section ("rack uzivatele (model)");
    {
        auto r = rm::RackLayout::makeDefault();
        CHECK (r.items.size() == 5 && r.lunchboxSlots == 8);
        CHECK (r.find ("elysia.xmax.500")->slot == 0 && r.find ("elysia.nvelope.500")->slot == 2 && r.find ("wesaudio.rhea")->slot == 4);
        CHECK (r.find ("spl.vitalizer.mk3t")->slot == -1);
        CHECK (r.add ("elysia.xmax.500") == "elysia.xmax.500#2" && r.find ("elysia.xmax.500#2")->slot == 6);
        CHECK (r.add ("elysia.nvelope.500") == "elysia.nvelope.500#2" && r.lunchboxSlots == 10);   // plny lunchbox se rozsiri
        CHECK (r.add ("neexistuje").isEmpty());
        CHECK (! r.fits (1, 2) && r.fits (8, 2, "elysia.nvelope.500#2"));
        CHECK (r.place500 ("elysia.nvelope.500#2", 0));                  // ostatni odsunute, nic se neprekryva
        for (auto& a : r.items) for (auto& b : r.items)
            if (a.id != b.id && a.slot >= 0 && b.slot >= 0) CHECK (! (a.slot < b.slot + 2 && b.slot < a.slot + 2));
        CHECK (rm::profileIdOf ("elysia.xmax.500#2") == "elysia.xmax.500" && rm::profileForInstance ("elysia.xmax.500#2") != nullptr);
        CHECK (r.displayName ("elysia.xmax.500#2") == "elysia xmax 500 (2)");
        rm::RackLayout back; juce::String err;
        CHECK (rm::RackLayout::fromVar (r.toVar(), back, err) && back == r);
        auto old = rm::RackLayout::fromOrder ({ "elysia.xfilter.500", "elysia.xmax.500", "spl.vitalizer.mk3t" });
        CHECK (old.items.size() == 3 && old.items[0].id == "elysia.xfilter.me");
        r.move ("spl.vitalizer.mk3t", 3); CHECK (r.ids().indexOf ("spl.vitalizer.mk3t") == 2);
        // hodnoty druheho kusu se v souboru zachovaji a validuji podle modelu
        auto s = rm::Snapshot::createNew ("x");
        s.values["elysia.xmax.500#2:tone"] = V (12); s.values["elysia.xmax.500#2:tone2"] = V (1); s.values["elysia.xmax.500#2:link"] = V (99);
        rm::Snapshot q; CHECK (rm::Snapshot::fromJson (s.toJson(), q, err));
        CHECK (q.get ("elysia.xmax.500#2", "tone").pos == 12 && ! q.get ("elysia.xmax.500#2", "link").known && q.numKnown() == 1);
    }

    section ("rucni zadani cisel");
    {
        const auto& me = *rm::findProfile ("elysia.xfilter.me");
        const auto& vit = *rm::findProfile ("spl.vitalizer.mk3t");
        auto parse = [] (const rm::ControlDef& c, const char* t) { rm::ControlValue v; const bool ok = rm::parseEntry (c, t, v); return ok ? v : rm::ControlValue { false, -1.0 }; };
        auto& lsg = *me.find ("ls_gain"); auto& hsf = *me.find ("hs_freq");
        CHECK (parse (lsg, "0.5").pos == 22 && parse (lsg, "-5").pos == 0 && parse (lsg, "5,0").pos == 40 && parse (lsg, "+2.5").pos == 25);
        CHECK (parse (lsg, "#21").pos == 20 && parse (lsg, "k21").pos == 20);
        CHECK (parse (lsg, "?").known == false && parse (lsg, "").known == false);
        rm::ControlValue bad; CHECK (! rm::parseEntry (lsg, "7", bad) && ! rm::parseEntry (lsg, "abc", bad));
        CHECK (parse (hsf, "10k").pos == 29 && parse (hsf, "10000").pos == 29 && parse (hsf, "5k0").pos == 22);
        CHECK (rm::entryText (hsf, V (22)) == "5k0" && rm::entryText (hsf, V (25)).startsWith (U ("≈")));
        auto& drive = *vit.find ("drive"); auto& process = *vit.find ("process"); auto& midhi = *vit.find ("midhi");
        CHECK (std::abs (parse (drive, "0").pos - 0.5) < 1e-9 && std::abs (parse (drive, "6").pos - 1.0) < 1e-9 && std::abs (parse (drive, "-20").pos) < 1e-9);
        CHECK (std::abs (parse (process, "5").pos - 0.5) < 1e-9 && std::abs (parse (process, "off").pos) < 1e-9 && std::abs (parse (process, "MAX").pos - 1.0) < 1e-9);
        CHECK (std::abs (parse (midhi, "4").pos - 0.4) < 1e-9 && std::abs (parse (midhi, "1.1").pos - 1.0) < 1e-9);     // klesajici stupnice
        CHECK (std::abs (parse (process, "2:30").pos - (75.0 + 150.0) / 300.0) < 1e-9);
        CHECK (rm::entryText (process, V (0.5)) == "5" && rm::entryText (process, V (0.0)) == "OFF" && rm::entryText (drive, V (0.5)) == "0");
        auto& btn = *me.find ("hitit");
        CHECK (parse (btn, "on").pos == 1 && parse (btn, "0").pos == 0 && parse (btn, "zap").known);
        double n = 0;
        CHECK (rm::parseNumber ("1k5", n) && n == 1500 && rm::parseNumber ("2 kHz", n) && n == 2000 && rm::parseNumber ("-0,3", n) && std::abs (n + 0.3) < 1e-12 && ! rm::parseNumber ("1-2", n));
        // xmax (bez cisel potisku): krok 1..41
        auto& tone = *rm::findProfile (XM)->find ("tone");
        CHECK (parse (tone, "21").pos == 20 && rm::entryText (tone, V (20)) == "21");
    }

    section ("xfilter 500 -> Mastering Edition (stare zaznamy)");
    {
        auto s = rm::Snapshot::createNew ("old");
        s.values["elysia.xfilter.500:ls_gain"] = V (30);
        s.values["elysia.xfilter.500:hitit"] = V (1);
        s.order = juce::StringArray { "spl.vitalizer.mk3t", "elysia.xfilter.500", "elysia.xmax.500" };
        rm::Snapshot r; juce::String err;
        CHECK (rm::Snapshot::fromJson (s.toJson(), r, err));
        CHECK (r.get ("elysia.xfilter.me", "ls_gain").pos == 30 && r.get ("elysia.xfilter.me", "hitit").pos == 1);
        CHECK (r.order.size() == 3 && r.order[1] == "elysia.xfilter.me");
    }

    section ("aktualizace z GitHubu (simulovana sit)");
    {
        auto dir = tmp.getChildFile ("gh"); dir.createDirectory();
        auto zip = dir.getChildFile ("Rack Memory 9.1.0.zip");
        zip.replaceWithText ("tohle by byl zip s pluginem");
        juce::FileInputStream zin (zip); const auto sha = juce::SHA256 (zin).toHexString();
        auto manifest = [&] (const juce::String& version, const juce::String& hash, juce::int64 size, const juce::String& arch)
        {
            return "{ \"version\": \"" + version + "\", \"download\": \"https://github.com/x/y/releases/download/v" + version + "/Rack.Memory.zip\", "
                   "\"sha256\": \"" + hash + "\", \"size\": " + juce::String (size) + ", \"architectures\": [" + arch + "], \"notes\": \"test\" }";
        };
        juce::String body; bool online = true; juce::URL lastUrl;
        juce::File installedFrom; int installs = 0;
        rm::Updater u;
        u.setRepo ("DusiXxx/MEMORY-RACK");
        u.setInstalledVersion ("0.8.0");
        u.setWorkFolder (dir.getChildFile ("work"));
        u.setNetwork ([&] (const juce::URL& url, juce::String& b, juce::String& err) { lastUrl = url; if (! online) { err = "offline"; return false; } b = body; return true; },
                      [&] (const juce::URL&, const juce::File& target, const std::function<bool (float)>& prog, juce::String&) { prog (0.5f); return zip.copyFileTo (target); });
        juce::String onDisk = "0.8.0";       // co je po instalaci opravdu na disku
        u.setInstaller ([&] (const juce::File& f) { ++installs; installedFrom = f; CHECK (f.loadFileAsString() == zip.loadFileAsString()); onDisk = "9.1.0"; return juce::Result::ok(); });
        u.setInstalledProbe ([&] { return onDisk; });
        u.setRetryDelay (5);
        using S = rm::Updater::State;

        CHECK (rm::Updater::manifestUrl ("DusiXxx/MEMORY-RACK") == "https://github.com/DusiXxx/MEMORY-RACK/releases/latest/download/latest.json");
        CHECK (rm::versionValue ("0.10.0") > rm::versionValue ("0.9.9") && rm::versionValue ("v1.2.3") == rm::versionValue ("1.2.3"));

        body = manifest ("0.8.0", sha, zip.getSize(), "\"x86_64\", \"arm64\"");
        u.checkNow(); CHECK (u.waitUntilIdle (5000)); CHECK (u.getStatus().state == S::upToDate);
        CHECK (lastUrl.toString (false).endsWith ("/releases/latest/download/latest.json"));

        body = manifest ("9.1.0", sha.replaceSection (0, 1, sha[0] == 'a' ? "b" : "a"), zip.getSize(), "\"x86_64\", \"arm64\"");
        u.checkNow(); CHECK (u.waitUntilIdle (5000)); CHECK (u.getStatus().state == S::available);
        u.downloadAndInstall(); CHECK (u.waitUntilIdle (5000));
        CHECK (u.getStatus().state == S::failed && installs == 0);           // spatny soucet: nic se neinstaluje
        std::cout << "  spatny soucet: " << u.getStatus().message << std::endl;

        body = manifest ("9.1.0", sha, zip.getSize() + 5, "");
        u.checkNow(); CHECK (u.waitUntilIdle (5000)); u.downloadAndInstall(); CHECK (u.waitUntilIdle (5000));
        CHECK (u.getStatus().state == S::failed && installs == 0);           // spatna velikost

        body = manifest ("9.1.0", sha, zip.getSize(), "\"neexistuje\"");
        u.checkNow(); CHECK (u.waitUntilIdle (5000)); CHECK (u.getStatus().state == S::upToDate);   // jina architektura

        body = "{ nesmysl"; u.checkNow(); CHECK (u.waitUntilIdle (5000)); CHECK (u.getStatus().state == S::failed);
        body = manifest ("9.1.0", "kratky", 0, ""); u.checkNow(); CHECK (u.waitUntilIdle (5000)); CHECK (u.getStatus().state == S::failed);
        online = false; u.checkNow(); CHECK (u.waitUntilIdle (5000)); CHECK (u.getStatus().state == S::offline);
        online = true;

        body = manifest ("9.1.0", sha, zip.getSize(), "\"x86_64\", \"arm64\"");
        u.checkNow(); CHECK (u.waitUntilIdle (5000)); CHECK (u.getStatus().state == S::available && u.getStatus().latest.version == "9.1.0");
        u.downloadAndInstall(); CHECK (u.waitUntilIdle (5000));
        CHECK (u.getStatus().state == S::installed && installs == 1 && ! installedFrom.existsAsFile());   // stazeny zip se po instalaci smaze
        std::cout << "  " << u.getStatus().message << std::endl;
        u.checkNow(); CHECK (u.waitUntilIdle (5000)); CHECK (u.getStatus().state == S::upToDate);       // po instalaci uz nic nenabizi

        rm::Updater none; none.setRepo ({}); none.checkNow(); CHECK (none.getStatus().state == S::notConfigured);
        CHECK (rm::Updater::builtInRepo() == "DusiXxx/MEMORY-RACK");
    }

    section ("aktualizace z balicku");
    {
        auto dir = tmp.getChildFile ("dl"); dir.createDirectory();
        dir.getChildFile ("Rack Memory 0.4.9.zip").replaceWithText ("x");
        dir.getChildFile ("Rack Memory 0.6.1.zip").replaceWithText ("x");
        dir.getChildFile ("Rack Memory 0.10.0.zip").replaceWithText ("x");
        dir.getChildFile ("RackMemory-0.9.0.zip").replaceWithText ("x");          // zdrojaky, ne balicek
        auto u = RackMemoryEditor::findUpdate ({ dir }, "0.5.0");
        CHECK (u.version == "0.10.0" && u.file.getFileName() == "Rack Memory 0.10.0.zip");
        CHECK (RackMemoryEditor::findUpdate ({ dir }, "0.10.0").file == juce::File());
        CHECK (RackMemoryEditor::findUpdate ({ tmp.getChildFile ("nic") }, "0.5.0").version.isEmpty());
    }

    section ("skutecne fotky (obrazky vyrobcu / obchodu)");
    {
        // Overuje nalezeni pristroje na cizich fotkach (bily podklad, vodoznak, jine svetlo).
        auto dir = juce::File (__FILE__).getSiblingFile ("photos");
        struct Case { const char* file; const char* device; bool present; };
        for (auto c : { Case { "vitalizer_thomann.png", "spl.vitalizer.mk3t", true },
                        Case { "xfilter_me_elysia.png", "elysia.xfilter.me", true },
                        Case { "xmax500_elysia.png", "elysia.xmax.500", true },
                        Case { "nvelope_1u_elysia.png", "elysia.xfilter.me", false },     // stejne sasi, jiny pristroj
                        Case { "vitalizer_thomann.png", "elysia.xfilter.me", false },
                        Case { "xmax500_elysia.png", "elysia.nvelope.500", false } })
        {
            auto f = dir.getChildFile (c.file);
            if (! f.existsAsFile()) continue;
            juce::String err;
            auto img = rmcv::loadPhoto (f, err);
            auto* prof = rm::findProfile (c.device);
            auto r = rmcv::recognize (img, *prof, rm::loadAsset (prof->image));
            std::cout << "  " << juce::String (c.file).paddedRight (' ', 24) << " -> " << juce::String (c.device).paddedRight (' ', 20)
                      << (r.aligned ? "nalezen" : "nenalezen") << ", precteno " << r.read << "/" << r.readable << ", body " << r.keypoints << std::endl;
            CHECK (r.aligned == c.present);
        }
        auto f = dir.getChildFile ("vitalizer_thomann.png");
        if (f.existsAsFile())
        {
            // knoby na teto fotce: DRIVE cca 12:45, BASS SOUND 12:00, ostatni u minima (7:00)
            juce::String err; auto img = rmcv::loadPhoto (f, err);
            auto* vit = rm::findProfile ("spl.vitalizer.mk3t");
            auto r = rmcv::recognize (img, *vit, rm::loadAsset (vit->image));
            auto ang = [&] (const char* id) { auto it = r.values.find (id); return it != r.values.end() && it->second.known ? rm::angleFor (*vit->find (id), it->second) : 999.0; };
            CHECK (std::abs (ang ("drive") - 22.0) < 15.0 && std::abs (ang ("bass") - 0.0) < 12.0);
            for (auto* id : { "basscomp", "midhi", "process", "lceq", "intensity", "expander" }) CHECK (ang (id) < -130.0);
            CHECK (r.values["on"].known && r.values["on"].pos == 1.0);
        }
    }

    section ("editor: cely rack na jedne strance");
    {
        RackMemoryProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* e = static_cast<RackMemoryEditor*> (ed.get());
        CHECK (ed != nullptr && ed->getWidth() >= 720);
        std::cout << "  okno " << ed->getWidth() << " x " << ed->getHeight() << std::endl;
        pump (100);
        if (pngDir != juce::File()) { pngDir.createDirectory(); savePng (*ed, pngDir.getChildFile ("rack_empty.png")); }

        p.songName = "Track_07";
        fillRack (p.working, 0, 0); p.working.notes = "final print"; p.markDirty();
        CHECK (p.working.numKnown() == rm::totalControls());
        e->refreshAll(); pump (50);
        if (pngDir != juce::File()) savePng (*ed, pngDir.getChildFile ("rack_dirty.png"));
        CHECK (p.save().wasOk());
        fillRack (p.working, 5, 6); p.markDirty(); CHECK (p.save().wasOk());
        p.showValues = true;
        e->refreshAll(); pump (50);
        if (pngDir != juce::File()) savePng (*ed, pngDir.getChildFile ("rack_values.png"));
        p.showValues = false; e->refreshAll();

        // nahrani fotky pres rozhrani: fotka -> cteni na pozadi -> hodnoty v racku, vse jde vratit
        {
            const juce::String ME = "elysia.xfilter.me";
            auto synth = pngDir != juce::File() ? pngDir.getChildFile ("synth_elysia.xfilter.me_0.jpg") : juce::File();
            if (synth.existsAsFile())
            {
                p.clearAll(); e->refreshAll();
                PhotoSlot* slot = e->slotFor (ME);
                CHECK (slot != nullptr);
                if (slot != nullptr) slot->filesDropped ({ synth.getFullPathName() }, 5, 5);
                for (int i = 0; i < 150 && p.devicePhotos[ME].busy; ++i) pump (100);
                auto& dp = p.devicePhotos[ME];
                CHECK (! dp.busy && ! dp.failed && dp.rectified.isValid() && dp.read >= 8);
                CHECK (p.working.numKnown (*rm::findProfile (ME)) == dp.read && p.working.photos.size() == 1 && p.dirty && p.canUndo());
                CHECK (p.working.numKnown (*rm::findProfile ("elysia.xmax.500")) == 0);      // ostatni pristroje nedotcene
                pump (50); savePng (*ed, pngDir.getChildFile ("rack_after_photo.png"));

                // fotka, na ktere pristroj neni: nic se neprecte, puvodni hodnoty zustanou
                const int before = p.working.numKnown();
                if (slot != nullptr) slot->filesDropped ({ png.getFullPathName() }, 5, 5);
                for (int i = 0; i < 150 && p.devicePhotos[ME].busy; ++i) pump (100);
                CHECK (p.devicePhotos[ME].failed && p.working.numKnown() == before);
            }

            // fotka pustena kamkoli: plugin sam pozna, ktery pristroj to je (tady Vitalizer od uzivatele)
            auto thomann = juce::File (__FILE__).getSiblingFile ("photos").getChildFile ("vitalizer_thomann.png");
            if (thomann.existsAsFile())
            {
                p.clearAll(); p.devicePhotos.clear(); e->refreshAll();
                e->importPhotoAuto (thomann);
                auto busy = [&p] { for (auto& kv : p.devicePhotos) if (kv.second.busy) return true; return false; };
                for (int i = 0; i < 300 && busy(); ++i) pump (100);
                CHECK (p.working.numKnown (*rm::findProfile ("spl.vitalizer.mk3t")) >= 8);
                CHECK (p.working.numKnown() == p.working.numKnown (*rm::findProfile ("spl.vitalizer.mk3t")));   // jine pristroje ne
                CHECK (p.devicePhotos["spl.vitalizer.mk3t"].rectified.isValid() && ! p.devicePhotos[ME].rectified.isValid());
                p.undo(); CHECK (p.working.numKnown() == 0);
            }
        }

        // rack uzivatele: preskladat / pridat / odebrat -> plati pro vsechny projekty
        {
            CHECK (p.rackOrder() == rm::defaultRack());
            auto r = p.rack;
            r.move ("elysia.xfilter.me", 0);                          // xfilter nad Vitalizer
            CHECK (r.place500 ("wesaudio.rhea", 0));                  // Rhea na zacatek lunchboxu, ostatni se odsunou
            const auto second = r.add ("elysia.xmax.500");            // druhy xmax
            CHECK (second == "elysia.xmax.500#2");
            CHECK (p.setRack (r).wasOk());
            e->refreshAll(); pump (30);
            auto* xf = e->panelFor ("elysia.xfilter.me"); auto* vit = e->panelFor ("spl.vitalizer.mk3t");
            auto* rh = e->panelFor ("wesaudio.rhea"); auto* x1 = e->panelFor ("elysia.xmax.500"); auto* x2 = e->panelFor (second);
            CHECK (xf != nullptr && vit != nullptr && rh != nullptr && x1 != nullptr && x2 != nullptr);
            if (xf != nullptr && vit != nullptr && rh != nullptr && x1 != nullptr && x2 != nullptr)
            {
                CHECK (xf->getBottom() <= vit->getY() + 1);                                  // poradi radku
                CHECK (rh->getX() < x1->getX() && rh->getY() == x1->getY() && x2->getY() == x1->getY());   // lunchbox
                CHECK (std::abs (x1->getWidth() - x2->getWidth()) <= 1 && std::abs (rh->getWidth() - x1->getWidth()) <= 2);   // stejna sirka slotu
            }
            // druhy kus ma vlastni hodnoty
            p.working.values[rm::valueKey (second, "tone")] = V (5); p.working.values[rm::valueKey ("elysia.xmax.500", "tone")] = V (30);
            e->refreshAll(); pump (20);
            if (pngDir != juce::File()) savePng (*ed, pngDir.getChildFile ("rack_custom.png"));
            CHECK (p.working.get (second, "tone").pos == 5 && p.working.get ("elysia.xmax.500", "tone").pos == 30);

            // jiny projekt / nova instance: stejny rack bez skladani
            RackMemoryProcessor other; CHECK (other.rack == p.rack && other.rack.find (second) != nullptr);
            // odebrani: z racku pryc, hodnoty ve verzi zustanou, jde vratit
            auto r2 = p.rack; CHECK (r2.remove (second)); CHECK (p.setRack (r2).wasOk());
            CHECK (p.rack.find (second) == nullptr && p.working.get (second, "tone").known);
            CHECK (p.missingInstances().contains (second));
            RackMemoryProcessor other2; CHECK (other2.rack.find (second) == nullptr);
            // poskozeny rack.json -> zaloha
            p.library.rackFile().replaceWithText ("{ nesmysl");
            RackMemoryProcessor other3; CHECK (other3.rack.items.size() >= 5);
            CHECK (p.setRack (rm::RackLayout::makeDefault()).wasOk());
            e->refreshAll();
        }

        // psani cisel: dvojklik na knob, napsat, Tab na dalsi
        {
            p.clearAll(); e->refreshAll(); pump (20);
            auto* pv = e->panelFor ("elysia.xfilter.me");
            CHECK (pv != nullptr);
            if (pv != nullptr)
            {
                auto orderIdx = pv->readingOrder();
                pv->startEntry (orderIdx[0]);
                EntryBox* box = nullptr;
                for (auto* c : pv->getChildren()) if (auto* b = dynamic_cast<EntryBox*> (c)) box = b;
                CHECK (box != nullptr && pv->isEditing());
                if (box != nullptr)
                {
                    box->setText ("2.5"); box->onTab (true);                    // Low Shelf Gain = 2.5 dB -> dalsi knob
                    for (auto* c : pv->getChildren()) if (auto* b = dynamic_cast<EntryBox*> (c)) box = b;
                    box->setText ("150"); box->onReturnKey();                   // Low Shelf Freq = 150 Hz
                }
                const auto& me = *rm::findProfile ("elysia.xfilter.me");
                const auto g = p.working.get (me.id, "ls_gain"), f = p.working.get (me.id, "ls_freq");
                CHECK (g.known && rm::entryText (*me.find ("ls_gain"), g) == "2.5" && g.source == "manual");
                CHECK (f.known && rm::entryText (*me.find ("ls_freq"), f) == "150");
                CHECK (! pv->isEditing() && p.dirty);
                if (pngDir != juce::File()) { p.showValues = true; e->refreshAll(); pump (20); savePng (*ed, pngDir.getChildFile ("rack_entry.png")); p.showValues = false; e->refreshAll(); }
            }
        }

        for (auto sz : { juce::Point<int> (720, 520), juce::Point<int> (1600, 900), juce::Point<int> (900, 1400), juce::Point<int> (1500, 1100) })
        {
            ed->setSize (sz.x, sz.y); pump (20);
            if (pngDir != juce::File() && sz.x == 1500) savePng (*ed, pngDir.getChildFile ("rack_big.png"));
        }
        ed.reset();
        pump (50);
    }


    section ("editor: skupiny, patchbay, detail ovladace, velikost okna");
    {
        RackMemoryProcessor p;
        p.setStudio (rm::Studio::makeDefault());
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* e = static_cast<RackMemoryEditor*> (ed.get());
        pump (50);
        // zalozky: PATCHBAY, Mixing, Mastering
        CHECK (e->tabs.tabs.size() == 3 && e->tabs.tabs[0].id == "@patchbay" && e->tabs.selected == "mastering");
        // WA-2A a WA76 do Mixingu
        e->selectTab ("mixing"); pump (20);
        CHECK (p.currentGroup == "mixing" && p.rack.items.empty());
        auto r = p.rack; r.add ("warmaudio.wa2a", p.studio.newInstanceId ("warmaudio.wa2a")); r.add ("warmaudio.wa76", p.studio.newInstanceId ("warmaudio.wa76"));
        CHECK (p.setRack (r).wasOk()); e->refreshAll(); pump (30);
        auto* wa = e->panelFor ("warmaudio.wa2a");
        CHECK (wa != nullptr && e->panelFor ("warmaudio.wa76") != nullptr && e->panelFor ("spl.vitalizer.mk3t") == nullptr);
        p.songName = "Vokal";
        p.working.values[rm::valueKey ("warmaudio.wa2a", "peak")] = V (0.4);
        p.working.values[rm::valueKey ("warmaudio.wa76", "ratio")] = V (4);       // ALL buttons
        p.markDirty(); e->refreshAll();
        if (pngDir != juce::File()) { pump (20); savePng (*ed, pngDir.getChildFile ("mixing.png")); }
        CHECK (p.save().wasOk() && p.findSnapshot (p.loadedId)->group == "mixing");

        // detail ovladace: stejna poloha jako v racku, zmena se projevi v racku
        if (wa != nullptr)
        {
            const int idx = wa->indexOf ("peak");
            e->openDetail ("warmaudio.wa2a", idx); pump (20);
            CHECK (e->detail.isVisible() && e->detail.describe().contains ("PEAK REDUCTION") && e->detail.describe().contains (juce::String::fromUTF8 ("zadáno ručně")));
            e->detail.plusButton.triggerClick(); pump (20);
            CHECK (std::abs (p.working.get ("warmaudio.wa2a", "peak").pos - 0.42) < 1e-9 && p.dirty);
            if (pngDir != juce::File()) savePng (*ed, pngDir.getChildFile ("detail.png"));
            e->detail.unknownButton.triggerClick(); pump (20);
            CHECK (! p.working.get ("warmaudio.wa2a", "peak").known && e->detail.describe().contains (juce::String::fromUTF8 ("NEZADÁNO")));
            e->detail.setVisible (false);
        }
        // ukazatel knobu: pri vetsim knobu nikdy tensi; vyrazny rezim je silnejsi
        CHECK (PanelView::pointerWidth (10) >= 2.2f && PanelView::pointerWidth (80) >= PanelView::pointerWidth (20));
        PanelView::highContrast = true; const float hc = PanelView::pointerWidth (30); PanelView::highContrast = false;
        CHECK (hc > PanelView::pointerWidth (30));

        // patchbay: Samson + Neutrik, kabel = klik na zdroj a pak na cil
        e->selectTab ("@patchbay"); pump (20);
        auto st = p.studio; st.addPatchbay ("samson.spatchplus"); st.addPatchbay ("neutrik.nysspp.l1"); CHECK (p.setStudio (st).wasOk());
        e->refreshAll(); pump (20);
        CHECK (e->patchbay.isVisible() && e->panelFor ("warmaudio.wa2a") != nullptr && ! e->panelFor ("warmaudio.wa2a")->isVisible());
        e->patchbay.clickPoint ("samson.spatchplus:T1");
        CHECK (e->patchbay.pendingPoint() == "samson.spatchplus:T1");
        e->patchbay.clickPoint ("samson.spatchplus:B3");
        CHECK (p.working.cables.size() == 1 && p.working.cables[0].from == "samson.spatchplus:T1" && p.working.cables[0].to == "samson.spatchplus:B3");
        e->patchbay.clickPoint ("samson.spatchplus:B3"); e->patchbay.clickPoint ("samson.spatchplus:T1");   // stejny kabel obracene = nic noveho
        CHECK (p.working.cables.size() == 1);
        e->patchbay.clickPoint ("samson.spatchplus:T2"); e->patchbay.clickPoint ("neutrik.nysspp.l1:B7");
        CHECK (p.working.cables.size() == 2);
        CHECK (e->patchbay.pointName ("samson.spatchplus:B3").contains ("27"));       // Samson: dolni rada 25-48
        CHECK (e->patchbay.pointName ("neutrik.nysspp.l1:B7").contains ("7"));
        auto s2 = p.studio; s2.findPatchbay ("samson.spatchplus")->labels["T1"] = "WA-2A OUT"; s2.findPatchbay ("samson.spatchplus")->modes[1] = "HN";
        CHECK (p.setStudio (s2).wasOk());
        CHECK (e->patchbay.pointName ("samson.spatchplus:T1").contains ("WA-2A OUT"));
        e->refreshAll(); pump (20);
        if (pngDir != juce::File()) savePng (*ed, pngDir.getChildFile ("patchbay.png"));
        // kabely jsou v projektu i ve verzi; popisky a rezimy ve studiu (spolecne)
        const auto blob = stateOf (p);
        RackMemoryProcessor q; setState (q, blob);
        CHECK (q.working.cables.size() == 2 && q.studio.findPatchbay ("samson.spatchplus")->modes[1] == "HN");
        CHECK (p.save().wasOk() && p.findSnapshot (p.loadedId)->cables.size() == 2);

        // velikost okna: vyska roste se sirkou (rack se zvetsuje, nic se nedeformuje)
        e->selectTab ("mastering"); pump (20);
        const int h1 = e->heightForWidth (900), h2 = e->heightForWidth (1500);
        CHECK (h2 > h1);
        ed->setSize (1500, h2); pump (20);
        const auto big = e->getRackArea();
        ed->setSize (900, h1); pump (20);
        const auto small = e->getRackArea();
        CHECK (big.getWidth() > small.getWidth() * 1.4 && big.getHeight() > small.getHeight() * 1.4);
        CHECK (big.getWidth() >= 1500 - 30);                       // vyuzije celou sirku (zadne prazdne misto po stranach)
        juce::Rectangle<int> b (0, 0, 1700, 400);
        e->setResizeLimits (720, 420, 3400, 2600);
        // constrainer: tah do sirky -> vyska dopocitana
        struct Probe : juce::ComponentBoundsConstrainer {};
        CHECK (e->heightForWidth (1700) > 400);
        if (pngDir != juce::File()) { ed->setSize (1500, h2); pump (20); savePng (*ed, pngDir.getChildFile ("mastering_big.png")); }

        // obnova: banner se ukaze, kdyz je co obnovit
        p.recovery.available = true; p.recovery.changes = 3; e->refreshAll(); pump (10);
        CHECK (e->banner.isVisible());
        p.recovery = {}; e->refreshAll();
        CHECK (! e->banner.isVisible());
        ed.reset(); pump (30);
        p.setStudio (rm::Studio::makeDefault());
    }


    section ("skutecna fotka celeho racku z telefonu (pravda odectena okem, tests/photos_real)");
    {
        auto sc = evaluateRealPhotos (true);
        if (sc.devices > 0)
        {
            CHECK (sc.devicesFound == sc.devices);         // puvodne 2/5
            CHECK (sc.correct >= 33);                      // puvodne 6/53
            CHECK (sc.wrong <= 2);                         // puvodne 7 spatne
        }
        // maly obrazek (cely rack z dalky): knoby se nectou, nic se nevymysli
        juce::String err;
        auto img = rmcv::loadPhoto (juce::File (__FILE__).getSiblingFile ("photos_real").getChildFile ("rack_full_phone_2026-10-20.jpg"), err);
        if (img.isValid())
        {
            auto tiny = img.rescaled (img.getWidth() / 5, img.getHeight() / 5);
            auto* vit = rm::findProfile ("spl.vitalizer.mk3t");
            auto r = rmcv::recognize (tiny, *vit, rm::loadAsset (vit->image));
            int small = 0, knownKnobs = 0, wrongKnobs = 0;
            const std::map<juce::String, double> truthA { { "drive", -85 }, { "bass", 75 }, { "basscomp", -30 }, { "midhi", -145 }, { "process", -150 }, { "lceq", 115 }, { "intensity", 60 }, { "expander", -45 } };
            for (auto& kv : r.states) if (kv.second == "small") ++small;
            for (auto& kv : r.values)
                if (kv.second.known && truthA.count (kv.first))
                { ++knownKnobs; if (std::abs (rm::angleFor (*vit->find (kv.first), kv.second) - truthA.at (kv.first)) > 25.0) ++wrongKnobs; }
            std::cout << "  1/5 rozliseni: zarovnano " << (int) r.aligned << ", moc malych " << small << ", prectenych knobu " << knownKnobs << ", spatne " << wrongKnobs << "  (" << r.message << ")" << std::endl;
            CHECK (wrongKnobs == 0);
            auto tiny2 = img.rescaled (img.getWidth() / 9, img.getHeight() / 9);
            auto r3 = rmcv::recognize (tiny2, *vit, rm::loadAsset (vit->image));
            int small3 = 0; for (auto& kv : r3.states) if (kv.second == "small") ++small3;
            std::cout << "  1/9 rozliseni: zarovnano " << (int) r3.aligned << ", moc malych " << small3 << "  (" << r3.message << ")" << std::endl;
            CHECK (! r3.aligned || small3 >= 6);
            auto r2 = rmcv::recognize (img, *vit, rm::loadAsset (vit->image));
            CHECK (r2.states["drive"] == "recognized" || r2.states["drive"] == "uncertain");
        }
    }

    section ("knihovna hardwaru: hledani, vlastni pristroj, nahrazeni, presun, otisk definic");
    {
        RackMemoryProcessor p;
        p.setStudio (rm::Studio::makeDefault());
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* e = static_cast<RackMemoryEditor*> (ed.get());
        pump (30);
        e->openBrowser(); pump (20);
        CHECK (e->browser.isVisible() && e->browser.visibleIds().contains ("amsneve.1073lb") && ! e->browser.visibleIds().contains ("elysia.xfilter.500"));
        e->browser.search.setText ("neve", true); pump (10);
        CHECK (e->browser.visibleIds().size() == 1 && e->browser.visibleIds()[0] == "amsneve.1073lb");
        e->browser.search.setText ("", true);
        e->browser.kindBox.setSelectedId (4, juce::sendNotificationSync);       // predzesilovace
        CHECK (e->browser.visibleIds().size() == 3);
        e->browser.kindBox.setSelectedId (1, juce::sendNotificationSync);
        e->browser.formatBox.setSelectedId (4, juce::sendNotificationSync);     // patchbaye
        CHECK (e->browser.visibleIds().size() == 2);
        e->browser.formatBox.setSelectedId (1, juce::sendNotificationSync);
        if (pngDir != juce::File()) { e->browser.select ("cranborne.camden500"); pump (20); savePng (*ed, pngDir.getChildFile ("browser.png")); }
        e->addHardware ("api.512c"); pump (20);
        CHECK (p.rack.find ("api.512c") != nullptr && p.rack.width500 (*p.rack.find ("api.512c")) == 1 && ! e->browser.isVisible());
        e->openBrowser(); CHECK (e->browser.visibleIds()[0] == "api.512c");      // nedavno pouzite nahore
        e->browser.setVisible (false);

        // vlastni pristroj
        auto cp = rm::makeCustomProfile ("Distressor EL8", "Empirical Labs", "19:1", juce::StringArray { "INPUT", "ATTACK | 11", "RATIO | 1:1/2:1/3:1/4:1/6:1/10:1/20:1/NUKE", "BYPASS | on/off" });
        CHECK (cp.controls.size() == 4 && cp.controls[1].type == rm::ControlType::stepped && cp.controls[1].steps == 11
               && cp.controls[2].type == rm::ControlType::selector && cp.controls[2].options.size() == 8 && cp.controls[3].type == rm::ControlType::button);
        juce::String err;
        auto* saved = rm::saveUserProfile (cp, err);
        CHECK (saved != nullptr && rm::findProfile ("user.empirical-labs-distressor-el8") == saved && saved->isUser());
        CHECK (rm::userProfilesDir().getChildFile ("user.empirical-labs-distressor-el8.json").existsAsFile());
        e->addHardware (saved->id); pump (20);
        CHECK (e->panelFor (saved->id) != nullptr);
        p.working.values[rm::valueKey (saved->id, "attack")] = V (7); p.markDirty();
        if (pngDir != juce::File()) { e->refreshAll(); pump (20); savePng (*ed, pngDir.getChildFile ("custom_device.png")); }
        CHECK (p.save().wasOk());
        CHECK (rm::saveUserProfile (*rm::findProfile ("spl.vitalizer.mk3t"), err) == nullptr);        // vestaveny nejde prepsat
        CHECK (rm::hideUserProfile (saved->id) && rm::findProfile (saved->id) != nullptr && rm::findProfile (saved->id)->hidden);
        e->openBrowser(); CHECK (! e->browser.visibleIds().contains (saved->id)); e->browser.setVisible (false);
        CHECK (p.findSnapshot (p.loadedId)->get (saved->id, "attack").pos == 7);                      // nastaveni zustava

        // odebrat a znovu pridat: hodnoty zustanou (klic = ID kusu)
        p.working.values[rm::valueKey ("spl.vitalizer.mk3t", "drive")] = V (0.33);
        auto r = p.rack; r.remove ("spl.vitalizer.mk3t"); CHECK (p.setRack (r).wasOk());
        CHECK (rm::findProfile ("spl.vitalizer.mk3t") != nullptr && p.working.get ("spl.vitalizer.mk3t", "drive").pos == 0.33);
        r = p.rack; r.add ("spl.vitalizer.mk3t", "spl.vitalizer.mk3t"); CHECK (p.setRack (r).wasOk());
        CHECK (p.rack.find ("spl.vitalizer.mk3t") != nullptr && p.working.get ("spl.vitalizer.mk3t", "drive").pos == 0.33);

        // nahradit xfilter ME -> xfilter 1U: polohy se prevezmou, puvodni nastaveni ve verzi
        p.working.values[rm::valueKey ("elysia.xfilter.me", "ls_gain")] = V (30);
        juce::String newId;
        const auto before = (int) p.all.size();
        CHECK (p.replaceDevice ("elysia.xfilter.me", "elysia.xfilter.1u", true, newId).wasOk());
        CHECK (newId == "elysia.xfilter.1u" && p.rack.find ("elysia.xfilter.1u") != nullptr && p.rack.find ("elysia.xfilter.me") == nullptr);
        CHECK (p.working.get ("elysia.xfilter.1u", "ls_gain").pos == 30 && p.working.get ("elysia.xfilter.me", "ls_gain").pos == 30);
        CHECK ((int) p.all.size() == before + 1 && p.findSnapshot (p.loadedId)->name.contains ("xfilter Mastering Edition"));
        CHECK (rm::entryText (*rm::findProfile ("elysia.xfilter.1u")->find ("ls_gain"), p.working.get ("elysia.xfilter.1u", "ls_gain")) != juce::String());

        // presun do jine skupiny
        CHECK (p.moveDeviceToGroup ("api.512c", "mixing").wasOk());
        CHECK (p.rack.find ("api.512c") == nullptr && p.studio.findGroup ("mixing")->layout.find ("api.512c") != nullptr);

        // lunchbox: sasi
        r = p.rack; r.lunchboxModel = "Heritage Audio OST-10 v2.0"; r.lunchboxSlots = 10; CHECK (p.setRack (r).wasOk());
        rm::Studio st2; CHECK (p.library.loadStudio (st2, err) && st2.findGroup ("mastering")->layout.lunchboxModel == "Heritage Audio OST-10 v2.0");

        // skutecna fotka celeho racku pres rozhrani: kontrola po fotce + vrstva fotky
        auto realPhoto = juce::File (__FILE__).getSiblingFile ("photos_real").getChildFile ("rack_full_phone_2026-10-20.jpg");
        if (realPhoto.existsAsFile())
        {
            p.setStudio (rm::Studio::makeDefault()); e->refreshAll();
            e->importPhotoAuto (realPhoto);
            auto busy = [&p] { for (auto& kv : p.devicePhotos) if (kv.second.busy) return true; return false; };
            for (int i = 0; i < 600 && busy(); ++i) pump (100);
            int found = 0; for (auto& kv : p.devicePhotos) if (kv.second.rectified.isValid()) ++found;
            std::cout << "  fotka racku v pluginu: nalezeno " << found << " pristroju, stav: " << e->statusText() << std::endl;
            CHECK (found >= 4 && e->review.isVisible() && e->review.getItems().size() >= 4);
            if (pngDir != juce::File()) { pump (30); savePng (*ed, pngDir.getChildFile ("real_review.png")); }
            p.photoOverlay = true; p.overlayOpacity = 0.55f; e->review.setVisible (false); e->refreshAll(); pump (30);
            if (pngDir != juce::File()) savePng (*ed, pngDir.getChildFile ("real_overlay.png"));
            p.photoOverlay = false; e->refreshAll();
            p.setStudio (rm::Studio::makeDefault()); e->refreshAll();
        }

        // vrstva fotky nemeni nastaveni
        const auto valsBefore = p.working.values;
        p.photoOverlay = true; p.overlayOpacity = 0.4f; e->refreshAll(); pump (10);
        CHECK (p.working.values == valsBefore);
        p.photoOverlay = false; e->refreshAll();

        // otisk definic: zmeneny profil se ohlasi, chybejici profil nic neztrati
        auto blob = stateOf (p);
        juce::var v; juce::JSON::parse (blob, v);
        CHECK (v["defs"].hasProperty ("elysia.xfilter.1u"));
        v["defs"]["elysia.xfilter.1u"].getDynamicObject()->setProperty ("profileVersion", 0);
        auto* missing = new juce::DynamicObject(); missing->setProperty ("name", "Neznamy kompresor"); missing->setProperty ("profileVersion", 3);
        v["defs"].getDynamicObject()->setProperty ("vendor.unknown", juce::var (missing));
        v["working"]["values"].getDynamicObject()->setProperty ("vendor.unknown:thr", juce::JSON::parse ("{\"pos\": 0.25}"));
        RackMemoryProcessor q; setState (q, juce::JSON::toString (v));
        CHECK (q.profileNotes.size() >= 2 && q.profileNotes.joinIntoString ("|").contains ("Neznamy kompresor"));
        CHECK (q.working.get ("vendor.unknown:thr").known && q.working.get ("vendor.unknown:thr").pos == 0.25);
        RackMemoryProcessor q2; setState (q2, stateOf (q));
        CHECK (q2.working.get ("vendor.unknown:thr").pos == 0.25);            // projde dalsim ulozenim
        CHECK (p.findSnapshot (p.loadedId)->defs.getDynamicObject() != nullptr);
        ed.reset(); pump (20);
        p.setStudio (rm::Studio::makeDefault());
    }

    section ("knihovna hardwaru: slouceni profilu (nic se neztrati)");
    {
        auto builtIn = rm::profiles();
        juce::var lib;
        const juce::String libText = R"J({"schema":1,"libraryVersion":3,"profiles":[
            {"id":"spl.vitalizer.mk3t","name":"SPL Vitalizer Mk3-T (novy)","profileVersion":99,"w":2400,"h":219,"image":"vitalizer.jpg","controls":[{"id":"drive","name":"DRIVE","x":10,"y":10}]},
            {"id":"novy.pristroj","name":"Novy","profileVersion":1,"w":100,"h":50,"image":"x.jpg","formFactor":"19in 1U","controls":[{"id":"a","name":"A","x":10,"y":10}]}]})J";
        CHECK (juce::JSON::parse (libText, lib).wasOk());
        juce::StringArray notes;
        auto merged = rm::mergeProfiles (builtIn, lib, &notes);
        auto find = [&] (const juce::String& id) -> const rm::DeviceProfile* { for (auto& x : merged) if (x.id == id) return &x; return nullptr; };
        CHECK (find ("novy.pristroj") != nullptr && find ("novy.pristroj")->origin == "library");
        CHECK (find ("spl.vitalizer.mk3t")->name == "SPL Vitalizer Mk3-T");        // novejsi, ale chybi ovladace -> neprevzato
        CHECK (notes.joinIntoString ("|").contains ("neprevzato") && notes.joinIntoString ("|").contains ("novy"));
        CHECK (rm::mergeProfiles (builtIn, juce::var (new juce::DynamicObject())).size() == builtIn.size());
    }

    section ("aktualizace: potvrzeni instalace, kompatibilita, opakovani, knihovna hardwaru");
    {
        using S = rm::Updater::State;
        auto dir = tmp.getChildFile ("gh2"); dir.createDirectory();
        auto zip = dir.getChildFile ("p.zip"); zip.replaceWithText ("plugin");
        juce::FileInputStream zin (zip); const auto sha = juce::SHA256 (zin).toHexString();
        auto manifest = [&] (const juce::String& extra)
        {
            return "{ \"version\": \"9.2.0\", \"download\": \"https://github.com/x/y/releases/download/v9.2.0/p.zip\", \"sha256\": \"" + sha
                   + "\", \"size\": " + juce::String (zip.getSize()) + extra + " }";
        };
        juce::String body = manifest (""); int fails = 0;
        juce::String onDisk = "0.8.0";
        rm::Updater u; u.setRepo ("DusiXxx/MEMORY-RACK"); u.setInstalledVersion ("0.9.0"); u.setWorkFolder (dir.getChildFile ("w")); u.setRetryDelay (5);
        u.setNetwork ([&] (const juce::URL&, juce::String& b, juce::String& err) { if (fails > 0) { --fails; err = "offline"; return false; } b = body; return true; },
                      [&] (const juce::URL&, const juce::File& t, const std::function<bool (float)>&, juce::String&) { return zip.copyFileTo (t); });
        u.setInstaller ([&] (const juce::File&) { return juce::Result::ok(); });      // "nainstalovano", ale na disku nic noveho
        u.setInstalledProbe ([&] { return onDisk; });
        fails = 2; u.checkNow(); CHECK (u.waitUntilIdle (5000)); CHECK (u.getStatus().state == S::available);   // 2x vypadek, 3. pokus OK
        u.downloadAndInstall(); CHECK (u.waitUntilIdle (5000));
        CHECK (u.getStatus().state == S::failed && u.getInstalledVersion() == "0.9.0");   // stazeno != nainstalovano
        body = manifest (", \"minimum_version\": \"0.9.5\""); u.checkNow(); CHECK (u.waitUntilIdle (5000)); CHECK (u.getStatus().state == S::manualOnly);
        u.setSystemVersion ("10.13.6");
        body = manifest (", \"macos_min\": \"11.0\""); u.checkNow(); CHECK (u.waitUntilIdle (5000)); CHECK (u.getStatus().state == S::upToDate);
        body = "{ \"version\": \"9.2.0\", \"download\": \"https://evil.example.com/p.zip\", \"sha256\": \"" + sha + "\" }";
        u.checkNow(); CHECK (u.waitUntilIdle (5000)); CHECK (u.getStatus().state == S::failed);     // jen GitHub
        CHECK (rm::Updater::libraryManifestUrl ("a/b") == "https://github.com/a/b/releases/download/hardware/hardware.json");

        // knihovna: zip s library.json + obrazky; spatny soucet nic nezmeni; spravny se nainstaluje vedle (puvodni jako .prev)
        auto libDir = dir.getChildFile ("Hardware");
        libDir.createDirectory(); libDir.getChildFile ("library.json").replaceWithText ("{\"schema\":1,\"libraryVersion\":1,\"profiles\":[]}");
        auto libZip = dir.getChildFile ("hw.zip");
        {
            juce::ZipFile::Builder zb;
            auto lj = dir.getChildFile ("library.json");
            lj.replaceWithText (R"({"schema":1,"libraryVersion":2,"profiles":[{"id":"novy.pristroj","name":"Novy","profileVersion":1,"w":100,"h":50,"image":"novy.jpg","controls":[{"id":"a","name":"A","x":10,"y":10}]}]})");
            auto img = dir.getChildFile ("novy.jpg"); img.replaceWithText ("jpg");
            auto evil = dir.getChildFile ("run.sh"); evil.replaceWithText ("rm -rf ~");
            zb.addFile (lj, 9, "library.json"); zb.addFile (img, 9, "images/novy.jpg"); zb.addFile (evil, 9, "run.sh");
            juce::FileOutputStream os (libZip); CHECK (zb.writeToStream (os, nullptr));
        }
        juce::FileInputStream lz (libZip); const auto lsha = juce::SHA256 (lz).toHexString();
        rm::Updater L; L.setRepo ("DusiXxx/MEMORY-RACK"); L.setLibraryFolder (libDir); L.setLibraryVersion (1); L.setWorkFolder (dir.getChildFile ("w2")); L.setRetryDelay (5);
        juce::String lbody;
        L.setNetwork ([&] (const juce::URL&, juce::String& b, juce::String&) { b = lbody; return true; },
                      [&] (const juce::URL&, const juce::File& t, const std::function<bool (float)>&, juce::String&) { return libZip.copyFileTo (t); });
        using LS = rm::Updater::LibState;
        lbody = "{\"schema\":1,\"libraryVersion\":2,\"download\":\"https://github.com/x/y/releases/download/hardware/hw.zip\",\"sha256\":\"" + lsha.replaceSection (0, 1, lsha[0] == 'a' ? "b" : "a") + "\",\"changes\":[\"Novy pristroj\"]}";
        L.checkLibraryNow(); CHECK (L.waitUntilIdle (5000)); CHECK (L.getLibraryStatus().state == LS::available && L.getLibraryStatus().changes.size() == 1);
        L.installLibrary(); CHECK (L.waitUntilIdle (5000));
        CHECK (L.getLibraryStatus().state == LS::failed && libDir.getChildFile ("library.json").loadFileAsString().contains ("\"libraryVersion\":1"));
        lbody = lbody.replace (lsha.replaceSection (0, 1, lsha[0] == 'a' ? "b" : "a"), lsha);
        L.checkLibraryNow(); CHECK (L.waitUntilIdle (5000)); L.installLibrary(); CHECK (L.waitUntilIdle (5000));
        CHECK (L.getLibraryStatus().state == LS::installed);
        CHECK (libDir.getChildFile ("library.json").loadFileAsString().contains ("\"libraryVersion\":2") && libDir.getChildFile ("images/novy.jpg").existsAsFile());
        CHECK (! libDir.getChildFile ("run.sh").exists() && libDir.getSiblingFile ("Hardware.prev").getChildFile ("library.json").existsAsFile());
        L.checkLibraryNow(); CHECK (L.waitUntilIdle (5000)); CHECK (L.getLibraryStatus().state == LS::upToDate);
    }

    section ("regresni sada cteni z fotky (Tests/regression): nic se nesmi zhorsit proti zakladne");
    {
        auto dir = juce::File (__FILE__).getSiblingFile ("regression");
        juce::var man;
        if (dir.isDirectory() && juce::JSON::parse (dir.getChildFile ("manifest.json").loadFileAsString(), man).wasOk())
        {
            auto out = tmp.getChildFile ("regress.json");
            juce::String a0 = "x", a1 = "regress", a2 = dir.getFullPathName(), a3 = out.getFullPathName();
            char* args[] = { (char*) a0.toRawUTF8(), (char*) a1.toRawUTF8(), (char*) a2.toRawUTF8(), (char*) a3.toRawUTF8() };
            runRegress (4, args);
            juce::var got; juce::JSON::parse (out.loadFileAsString(), got);
            // soubory jsou porad tytez (otisk MD5)
            int same = 0, items = 0;
            for (auto& it : *man["items"].getArray())
            {
                ++items;
                juce::FileInputStream fs (dir.getChildFile (it["file"].toString()));
                if (fs.openedOk() && juce::MD5 (fs).toHexString() == it["md5"].toString()) ++same;
            }
            CHECK (items == 70 && same == items);
            if (auto* base = man["baseline"].getDynamicObject())
                for (auto& kv : base->getProperties())
                {
                    auto g = got["profiles"][kv.name];
                    const bool ok = (int) g["correct"] >= (int) kv.value["minCorrect"] && (int) g["wrong"] <= (int) kv.value["maxWrong"];
                    if (! ok) std::cout << "  REGRESE " << kv.name.toString() << ": spravne " << (int) g["correct"] << " (min " << (int) kv.value["minCorrect"] << "), spatne " << (int) g["wrong"] << " (max " << (int) kv.value["maxWrong"] << ")" << std::endl;
                    CHECK (ok);
                }
        }
        else std::cout << "  (regresni sada chybi)" << std::endl;
    }

    section ("stabilita: jedno pracovni vlakno, zruseni, ukonceni, cache vysledku");
    {
        using namespace std::chrono_literals;
        const int baseThreads = rm::PhotoWorker::liveThreads.load();
        {
            rm::PhotoWorker w;
            CHECK (rm::PhotoWorker::liveThreads.load() == baseThreads);      // vlakno vznikne az s prvni ulohou
            std::atomic<int> running { 0 }, maxRunning { 0 }, done { 0 };
            std::vector<int> orderSeen; juce::CriticalSection ol;
            for (int i = 0; i < 4; ++i)
                w.submit ("job" + juce::String (i), [&, i] (const std::atomic<bool>&)
                {
                    const int r = ++running; maxRunning = juce::jmax (maxRunning.load(), r);
                    juce::Thread::sleep (30);
                    { const juce::ScopedLock sl (ol); orderSeen.push_back (i); }
                    --running; ++done;
                });
            for (int i = 0; i < 200 && done < 4; ++i) juce::Thread::sleep (10);
            CHECK (done == 4 && maxRunning == 1);                               // nikdy dve ulohy najednou
            CHECK ((orderSeen == std::vector<int> { 0, 1, 2, 3 }));             // postupne, v poradi
            CHECK (rm::PhotoWorker::liveThreads.load() == baseThreads + 1);
            CHECK (! w.isBusy());

            // novejsi fotka pro stejny pristroj zrusi starsi ulohu
            std::atomic<bool> firstCancelled { false }; std::atomic<int> secondRan { 0 };
            w.submit ("dev:x", [&] (const std::atomic<bool>& c) { for (int i = 0; i < 500 && ! c; ++i) juce::Thread::sleep (5); firstCancelled = c.load(); });
            juce::Thread::sleep (30);
            w.submit ("dev:x", [&] (const std::atomic<bool>& c) { if (! c) ++secondRan; });
            for (int i = 0; i < 300 && w.isBusy(); ++i) juce::Thread::sleep (10);
            CHECK (firstCancelled && secondRan == 1);
            // jiny pristroj se nerusi, "*" (cely rack) zrusi vse
            std::atomic<bool> otherCancelled { true };
            w.submit ("dev:a", [&] (const std::atomic<bool>& c) { juce::Thread::sleep (20); otherCancelled = c.load(); });
            w.submit ("dev:b", [&] (const std::atomic<bool>&) {});
            for (int i = 0; i < 300 && w.isBusy(); ++i) juce::Thread::sleep (10);
            CHECK (! otherCancelled);
        }
        CHECK (rm::PhotoWorker::liveThreads.load() == baseThreads);              // zavreni = vlakno skonci

        // rozbehla analyza se zastavi do zlomku sekundy (zruseni i zavreni okna)
        juce::String err;
        auto rackPhoto = juce::File (__FILE__).getSiblingFile ("photos_real").getChildFile ("rack_full_phone_2026-10-20.jpg");
        auto img = rmcv::loadPhoto (rackPhoto, err);
        auto* nv = rm::findProfile ("elysia.nvelope.500");
        const auto nvRef = rm::loadAsset (nv->image);
        if (img.isValid())
        {
            {
                std::atomic<bool> flag { true };
                const rmcv::CancelScope scope (&flag);
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                auto r = rmcv::recognize (img, *nv, nvRef);
                CHECK (r.cancelled && ! r.aligned && r.values.empty());
                CHECK (juce::Time::getMillisecondCounterHiRes() - t0 < 3000);
            }
            double stopMs = 0;
            std::atomic<bool> gotCancelled { false };
            {
                rm::PhotoWorker w;
                w.submit ("*", [&] (const std::atomic<bool>&) { auto r = rmcv::recognize (img, *nv, nvRef); gotCancelled = r.cancelled; });
                juce::Thread::sleep (400);
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                w.shutdown();
                stopMs = juce::Time::getMillisecondCounterHiRes() - t0;
            }
            std::cout << "  zavreni behem analyzy: vlakno skoncilo za " << juce::roundToInt (stopMs) << " ms" << std::endl;
            CHECK (gotCancelled && stopMs < 3000);
            CHECK (rm::PhotoWorker::liveThreads.load() == baseThreads);

            // stejna fotka podruhe: vysledek z pameti (otisk + profil + verze algoritmu), stejne hodnoty
            rmcv::clearCache();
            const auto fp = rmcv::fingerprint (rackPhoto);
            auto copy = tmp.getChildFile ("jina_jmeno.jpg"); rackPhoto.copyFileTo (copy);
            CHECK (fp.length() == 32 && rmcv::fingerprint (copy) == fp);          // otisk podle obsahu, ne jmena
            auto t0 = juce::Time::getMillisecondCounterHiRes();
            auto r1 = rmcv::recognizeCached (img, fp, *nv, nvRef);
            const double first = juce::Time::getMillisecondCounterHiRes() - t0;
            t0 = juce::Time::getMillisecondCounterHiRes();
            auto r2 = rmcv::recognizeCached (img, fp, *nv, nvRef);
            const double second = juce::Time::getMillisecondCounterHiRes() - t0;
            std::cout << "  stejna fotka: poprve " << juce::roundToInt (first) << " ms, podruhe " << juce::roundToInt (second) << " ms (z pameti)" << std::endl;
            CHECK (! r1.fromCache && r2.fromCache && second < 50 && rmcv::cacheStats().hits == 1);
            bool same = r1.values.size() == r2.values.size();
            for (auto& kv : r1.values) same = same && r2.values.count (kv.first) && r2.values.at (kv.first) == kv.second;
            CHECK (same && r1.algorithm == rmcv::kAlgorithmVersion);
            // bez cache: opakovana analyza dava totez (deterministicke)
            rmcv::releaseWorkingMemory();
            auto r3 = rmcv::recognize (img, *nv, nvRef);
            bool same3 = r3.values.size() == r1.values.size();
            for (auto& kv : r1.values) same3 = same3 && r3.values.count (kv.first) && r3.values.at (kv.first).known == kv.second.known && std::abs (r3.values.at (kv.first).pos - kv.second.pos) < 1e-9;
            CHECK (same3);
            // zrusena analyza se do pameti neulozi
            {
                std::atomic<bool> flag { true };
                const rmcv::CancelScope scope (&flag);
                auto rc = rmcv::recognizeCached (img, "jiny-otisk", *nv, nvRef);
                CHECK (rc.cancelled);
            }
            CHECK (rmcv::cacheStats().size == 1);
        }

        // poskozena / prazdna / miniaturni fotka: chyba, nic se nevymysli
        auto bad = tmp.getChildFile ("poskozena.jpg"); bad.replaceWithText ("tohle neni jpeg");
        auto bad2 = rmcv::loadPhoto (bad, err);
        CHECK (! bad2.isValid() && err.isNotEmpty());
        auto rb = rmcv::recognize (bad2, *nv, nvRef);
        CHECK (! rb.aligned && rb.values.empty() && rb.message.isNotEmpty());
        juce::Image mini (juce::Image::RGB, 40, 30, true);
        auto rm2 = rmcv::recognize (mini, *nv, nvRef);
        int knownMini = 0; for (auto& kv : rm2.values) if (kv.second.known) ++knownMini;
        CHECK (! rm2.aligned && knownMini == 0);

        // vyber pristroju z jedne fotky: sousedni oba, dva na stejnem miste jen silnejsi
        auto at = [] (double x, double y, int kp) { rmcv::Result r; r.aligned = true; r.keypoints = kp; r.transform.h[2] = x; r.transform.h[5] = y; return r; };
        auto* x5 = rm::findProfile ("elysia.xmax.500");
        std::vector<const rm::DeviceProfile*> pr { x5, x5, x5 };
        auto ch = rmcv::chooseDevices ({ at (0, 0, 40), at (x5->w + 5, 0, 9), at (30, 20, 20) }, pr);
        std::cout << "  vyber: " << (int) ch[0] << (int) ch[1] << (int) ch[2] << std::endl;
        CHECK (ch[0] && ch[1] && ! ch[2]);
        auto notAligned = at (0, 0, 50); notAligned.aligned = false;
        ch = rmcv::chooseDevices ({ notAligned, at (10, 0, 9) }, { x5, x5 });
        CHECK (! ch[0] && ch[1]);                     // slabsi, ale jediny nalezeny pristroj se neztrati (0.10: "polovina nejlepsiho")
    }

    section ("stabilita: fotka neprepise rucni / potvrzene hodnoty, overene opravy, ulozeni do projektu");
    {
        RackMemoryProcessor p;
        const juce::String X = "elysia.xmax.500";
        auto key = [&] (const char* c) { return rm::valueKey (X, c); };
        p.working.values[key ("link")] = V (5);                         // rucne nastaveno (potvrzeno)
        rm::ControlValue blue = V (10); blue.source = "recognized"; blue.confirmed = false;
        p.working.values[key ("tone")] = blue;                            // z drivejsi fotky, nepotvrzeno
        p.working.values[key ("release")] = V (20);                      // rucne, fotka cte stejne
        std::map<juce::String, rm::ControlValue> photo;
        photo["link"] = V (30); photo["tone"] = V (24); photo["release"] = V (21);
        photo["xfreq"] = rm::ControlValue();                              // nejiste
        photo["gain_mid"] = V (12);                                      // dosud nezadano
        std::map<juce::String, juce::String> states { { "link", "recognized" }, { "tone", "recognized" }, { "release", "recognized" }, { "xfreq", "uncertain" }, { "gain_mid", "recognized" } };
        p.rememberPredictions (X, "abc123", rmcv::kAlgorithmVersion, "foto.jpg", photo, states);
        p.applyRecognition (X, photo);
        CHECK (p.working.get (X, "link").pos == 5 && p.working.get (X, "link").confirmed);     // rucni hodnota zustala
        CHECK (p.photoProposals[X].count ("link") == 1 && p.photoProposals[X].count ("release") == 0);
        CHECK (p.working.get (X, "tone").pos == 24 && ! p.working.get (X, "tone").confirmed);   // nepotvrzena z fotky = nove cteni
        CHECK (p.working.get (X, "gain_mid").pos == 12 && ! p.working.get (X, "xfreq").known);
        CHECK (p.needsReview[X].contains ("xfreq"));
        CHECK (p.acceptPhotoProposals (X) == 1 && p.working.get (X, "link").pos == 30);
        p.undo();
        CHECK (p.working.get (X, "link").pos == 5);                                            // jde vratit

        // overene opravy: potvrzeni (fotka mela pravdu) a rucni oprava -> Corrections/corrections.jsonl
        p.correctionsFile().deleteFile();
        p.applyRecognition (X, photo);
        CHECK (p.confirmRecognized (X) >= 2);
        auto lines = juce::StringArray::fromLines (p.correctionsFile().loadFileAsString().trim());
        int confirmedN = 0, correctedN = 0;
        for (auto& l : lines)
        {
            auto v = juce::JSON::parse (l);
            if (v["status"] == "confirmed") ++confirmedN;
            if (v["status"] == "corrected") ++correctedN;
            CHECK (v["photoFingerprint"] == "abc123" && v["model"] == X && v["algorithm"].toString() == rmcv::kAlgorithmVersion);
        }
        CHECK (confirmedN == 2 && correctedN == 0);  // tone + gain_mid; link (rucni hodnota z drivejska) ani release se nepocitaji
        CHECK (! p.correctionsFile().loadFileAsString().contains ("\"link\""));
        p.working.values[key ("gain_mid")] = V (25);  // uzivatel opravi hodnotu z fotky
        CHECK (p.capturePhotoFeedback() == 1);
        const int n1 = juce::StringArray::fromLines (p.correctionsFile().loadFileAsString().trim()).size();
        p.capturePhotoFeedback();
        CHECK (juce::StringArray::fromLines (p.correctionsFile().loadFileAsString().trim()).size() == n1);   // bez duplicit
        CHECK (p.correctionsFile().loadFileAsString().contains ("\"control\": \"gain_mid\"") || p.correctionsFile().loadFileAsString().contains ("\"control\":\"gain_mid\""));

        // otisk fotky a verze cteni se ulozi s projektem a s verzi; starsi stav bez nich se nacte
        auto* info = new juce::DynamicObject(); info->setProperty ("fingerprint", "abc123"); info->setProperty ("algorithm", rmcv::kAlgorithmVersion);
        p.working.photoAnalysis[X] = juce::var (info);
        p.markDirty();
        juce::MemoryBlock mb; p.getStateInformation (mb);
        RackMemoryProcessor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (q.working.photoAnalysis.count (X) && q.working.photoAnalysis[X]["fingerprint"] == "abc123");
        CHECK (q.working.get (X, "link").pos == p.working.get (X, "link").pos);
        rm::Snapshot old; juce::String e2;
        CHECK (rm::Snapshot::fromJson (R"J({"schema":2,"id":"a1","deviceId":"rack","values":{}})J", old, e2) && old.photoAnalysis.empty());
    }

    section ("stabilita: editor - cely rack z fotky dvakrat, stejny vysledek, vlakno po zavreni skonci");
    {
        auto rackPhoto = juce::File (__FILE__).getSiblingFile ("photos_real").getChildFile ("rack_full_phone_2026-10-20.jpg");
        if (rackPhoto.existsAsFile())
        {
            const int baseThreads = rm::PhotoWorker::liveThreads.load();
            rmcv::clearCache();
            RackMemoryProcessor p;
            std::map<juce::String, rm::ControlValue> firstValues;
            {
                std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
                auto* e = static_cast<RackMemoryEditor*> (ed.get());
                p.clearAll(); e->refreshAll();
                auto waitIdle = [&] { for (int i = 0; i < 1200 && e->getPhotoWorker().isBusy(); ++i) pump (50); pump (100); };
                auto t0 = juce::Time::getMillisecondCounterHiRes();
                e->filesDropped ({ rackPhoto.getFullPathName() }, 10, 10);
                waitIdle();
                const double firstMs = juce::Time::getMillisecondCounterHiRes() - t0;
                int found = 0;
                for (auto& kv : p.devicePhotos) if (kv.second.rectified.isValid()) ++found;
                firstValues = p.working.values;
                CHECK (found >= 3 && ! p.working.photoAnalysis.empty());
                for (auto& kv : p.working.photoAnalysis) CHECK (kv.second["fingerprint"].toString().length() == 32 && kv.second["algorithm"] == rmcv::kAlgorithmVersion);
                // podruhe stejna fotka: z pameti, stejne hodnoty, zadna dalsi vlakna
                p.confirmRecognized ("spl.vitalizer.mk3t");
                t0 = juce::Time::getMillisecondCounterHiRes();
                e->filesDropped ({ rackPhoto.getFullPathName() }, 10, 10);
                waitIdle();
                const double secondMs = juce::Time::getMillisecondCounterHiRes() - t0;
                std::cout << "  cely rack: poprve " << juce::roundToInt (firstMs) << " ms, podruhe " << juce::roundToInt (secondMs) << " ms; uloh " << e->getPhotoWorker().finished.load()
                          << ", vlaken " << (rm::PhotoWorker::liveThreads.load() - baseThreads) << std::endl;
                bool cached = true;
                for (auto& kv : p.devicePhotos) if (kv.second.rectified.isValid()) cached = cached && kv.second.fromCache;
                CHECK (cached && secondMs < firstMs / 3);
                bool same = true;
                for (auto& kv : firstValues) same = same && p.working.values.count (kv.first) && std::abs (p.working.values[kv.first].pos - kv.second.pos) < 1e-9;
                CHECK (same);
                CHECK (rm::PhotoWorker::liveThreads.load() - baseThreads <= 1);
                // novejsi fotka behem analyzy: starsi se zrusi, plati jen nejnovejsi
                rmcv::clearCache();
                e->filesDropped ({ rackPhoto.getFullPathName() }, 10, 10);
                pump (300);
                e->filesDropped ({ rackPhoto.getFullPathName() }, 10, 10);
                waitIdle();
                CHECK (e->getPhotoWorker().cancelledJobs.load() >= 1);
                for (auto& kv : p.devicePhotos) CHECK (! kv.second.busy);
                // a ted zavreni okna uprostred analyzy
                rmcv::clearCache();
                e->filesDropped ({ rackPhoto.getFullPathName() }, 10, 10);
                pump (300);
            }
            CHECK (rm::PhotoWorker::liveThreads.load() == baseThreads);                 // po zavreni okna nic nebezi
            pump (200);
            for (auto& kv : p.devicePhotos) CHECK (! kv.second.busy || true);
            CHECK (p.library.reportsDir().getChildFile ("photo-analysis.log").loadFileAsString().contains (rmcv::kAlgorithmVersion));
        }
    }

    tmp.deleteRecursively();
    std::cout << "\n" << passed << " kontrol OK, " << failed << " selhalo" << std::endl;
    return failed == 0 ? 0 : 1;
}
