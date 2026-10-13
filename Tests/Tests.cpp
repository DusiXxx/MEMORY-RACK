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

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    if (argc > 3 && juce::String (argv[1]) == "notch")
    {
        juce::String err; auto img = rmcv::loadPhoto (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]), err);
        std::cout << rmcv::debugNotch (img, juce::String (argv[3]).getDoubleValue()) << std::endl; return 0;
    }

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
        CHECK (rm::profiles().size() == 6 && rm::defaultRack().size() == 5);
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
        for (auto& prof : rm::profiles())
        {
            if (prof.anchor.isEmpty()) continue;
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
                    g.drawImageTransformed (panelImg, juce::AffineTransform::translation (-prof.w * 0.5f, -prof.h * 0.5f).scaled (sc).rotated (rot).translated (tx, ty));
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
        std::cout << "  CELKEM: sceny " << aligned << "/" << scenes << " zarovnano, precteno " << totalRead << "/" << totalReadable << ", spatne " << wrong << std::endl;
        CHECK (aligned == scenes);
        CHECK (totalRead >= totalReadable * 85 / 100);
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
        CHECK (p.working.get (XM, "tone").pos == 17 && p.working.get (XM, "tone").source == "recognized");
        CHECK (! p.working.get (XM, "level").known && p.dirty && p.canUndo());     // nejiste = nezadano, ne stara hodnota
        juce::String err; rm::Snapshot r;
        CHECK (rm::Snapshot::fromJson (p.working.toJson(), r, err) && r.get (XM, "tone").source == "recognized" && r.get (XM, "tone").confidence == 0.9);
        p.undo();
        CHECK (p.working.get (XM, "tone").pos == 3 && p.working.get (XM, "level").pos == 9);
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
        u.setInstaller ([&] (const juce::File& f) { ++installs; installedFrom = f; CHECK (f.loadFileAsString() == zip.loadFileAsString()); return juce::Result::ok(); });
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
                PhotoSlot* slot = nullptr;
                for (auto* c : ed->getChildren()) if (auto* ps = dynamic_cast<PhotoSlot*> (c)) if (ps->profile != nullptr && ps->profile->id == ME) slot = ps;
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

    tmp.deleteRecursively();
    std::cout << "\n" << passed << " kontrol OK, " << failed << " selhalo" << std::endl;
    return failed == 0 ? 0 : 1;
}
