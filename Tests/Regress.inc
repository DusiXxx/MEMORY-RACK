// Pevna regresni sada: stejne soubory a stejna pravda pro kazdou verzi pluginu.
// Pouziti: RackMemoryTests regress <slozka s *.jpg + *.truth.json> [vystup.json]
// Hodnoceni: knob spravne = do 9 stupnu, stupnovy prepinac = do 1 kroku, prepinac = presne.
static int runRegress (int argc, char** argv)
{
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.truth.json");
    files.sort();
    struct Agg { int scenes = 0, aligned = 0, readable = 0, read = 0, correct = 0, wrong = 0; double ms = 0; };
    std::map<juce::String, Agg> per;
    Agg all;
    juce::Array<juce::var> rows;
    for (auto& tf : files)
    {
        juce::var t;
        if (juce::JSON::parse (tf.loadFileAsString(), t).failed()) continue;
        auto* prof = rm::findProfile (t["profile"].toString());
        if (prof == nullptr) { std::cout << "  (profil chybi: " << t["profile"].toString() << ")" << std::endl; continue; }
        juce::String err;
        auto img = rmcv::loadPhoto (dir.getChildFile (t["file"].toString()), err);
        if (! img.isValid()) continue;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto r = rmcv::recognize (img, *prof, rm::loadAsset (prof->image));
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        auto& a = per[prof->id];
        int readable = 0, read = 0, correct = 0, wrong = 0;
        juce::StringArray bad, unread;
        if (auto* vals = t["values"].getDynamicObject())
            for (auto& kv : vals->getProperties())
            {
                ++readable;
                auto it = r.values.find (kv.name.toString());
                if (it == r.values.end() || ! it->second.known) { unread.add (kv.name.toString()); continue; }
                ++read;
                const double want = (double) kv.value["pos"], got = it->second.pos;
                const auto type = kv.value["type"].toString();
                const bool ok = type == "knob" ? std::abs (got - want) * (double) kv.value["degrees"] <= 9.0
                              : type == "stepped" ? std::abs (got - want) <= 1.0 : got == want;
                if (ok) ++correct; else { ++wrong; bad.add (kv.name.toString()); }
            }
        for (auto* g : { &a, &all })
        {
            ++g->scenes; g->aligned += r.aligned ? 1 : 0; g->readable += readable; g->read += read; g->correct += correct; g->wrong += wrong; g->ms += ms;
        }
        auto* row = new juce::DynamicObject();
        row->setProperty ("file", t["file"]); row->setProperty ("aligned", r.aligned); row->setProperty ("readable", readable);
        row->setProperty ("correct", correct); row->setProperty ("wrong", wrong); row->setProperty ("ms", juce::roundToInt (ms));
        rows.add (juce::var (row));
        if (std::getenv ("RM_VERBOSE") != nullptr)
            std::cout << "  " << t["file"].toString().paddedRight (' ', 40) << (r.aligned ? "ok  " : "NE  ") << correct << "/" << readable
                      << (wrong > 0 ? "  SPATNE: " + bad.joinIntoString (",") : juce::String())
                      << (unread.isEmpty() ? juce::String() : "  neprecteno: " + unread.joinIntoString (",")) << std::endl;
    }
    auto line = [] (const juce::String& name, const Agg& a)
    {
        std::cout << "  " << name.paddedRight (' ', 24) << " zarovnano " << a.aligned << "/" << a.scenes << "  spravne " << a.correct << "/" << a.readable
                  << "  spatne " << a.wrong << "  nejiste " << (a.readable - a.read) << "  cas " << juce::roundToInt (a.ms) << " ms" << std::endl;
    };
    for (auto& kv : per) line (kv.first, kv.second);
    line ("CELKEM", all);
    if (argc > 3)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("rows", rows);
        o->setProperty ("correct", all.correct); o->setProperty ("readable", all.readable); o->setProperty ("wrong", all.wrong);
        o->setProperty ("aligned", all.aligned); o->setProperty ("scenes", all.scenes); o->setProperty ("ms", juce::roundToInt (all.ms));
        auto* pp = new juce::DynamicObject();
        for (auto& kv : per)
        {
            auto* e = new juce::DynamicObject();
            e->setProperty ("correct", kv.second.correct); e->setProperty ("wrong", kv.second.wrong); e->setProperty ("readable", kv.second.readable);
            e->setProperty ("aligned", kv.second.aligned); e->setProperty ("scenes", kv.second.scenes);
            pp->setProperty (juce::Identifier (kv.first), juce::var (e));
        }
        o->setProperty ("profiles", juce::var (pp));
        juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[3])).replaceWithText (juce::JSON::toString (juce::var (o)));
    }
    return 0;
}
