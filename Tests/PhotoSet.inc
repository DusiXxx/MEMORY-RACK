// Sada skutecnych fotek (Tests/photo_dataset/catalog.json): stejna cesta jako tlacitko FOTO (vsechny pristroje,
// vyber podle prekryvu). Hodnoti: nalezeni ocekavanych pristroju, falesne nalezy, ovladace podle pravdy
// (jen fotky s "use": "test" a pravdou), vysledky podle modelu a typu ovladace.
// Pouziti: RackMemoryTests photoset <catalog.json> <slozka s fotkami> [vystup.json]
static juce::String controlKind (const rm::ControlDef& c)
{
    if (c.read == "ledring") return "LED kruh";
    if (c.read == "led" || c.read == "ledsel" || c.read == "ringglow" || c.read == "rectglow") return "LED / tlacitko";
    if (c.type == rm::ControlType::knob) return c.read == "line" ? "knob (ryska)" : "knob (zarez)";
    if (c.type == rm::ControlType::stepped) return "stupnovy knob";
    return "prepinac";
}

static int runPhotoSet (int argc, char** argv)
{
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    juce::var cat;
    if (juce::JSON::parse (cwd.getChildFile (juce::String (argv[2])).loadFileAsString(), cat).failed()) { std::cout << "katalog nejde nacist" << std::endl; return 1; }
    const auto dir = cwd.getChildFile (juce::String (argv[3]));
    const juce::String only = std::getenv ("RM_ONLY") ? juce::String (std::getenv ("RM_ONLY")) : juce::String();

    std::vector<const rm::DeviceProfile*> profs;
    std::vector<juce::Image> refs;
    for (auto& p : rm::profiles())
    {
        bool readable = false;
        for (auto& c : p.controls) readable = readable || c.read.isNotEmpty();
        if (readable && ! p.hidden) { profs.push_back (&p); refs.push_back (rm::loadAsset (p.image).createCopy()); }
    }
    struct Agg { int photos = 0, expected = 0, found = 0, falsePos = 0, truth = 0, correct = 0, wrong = 0, unknown = 0; double ms = 0; };
    std::map<juce::String, Agg> byModel, byKind, byCondition;
    Agg all;
    juce::Array<juce::var> rows;
    int missing = 0;
    for (auto& ph : *cat["photos"].getArray())
    {
        const auto use = ph["use"].toString();
        if (use != "test") continue;
        const auto id = ph["id"].toString();
        if (only.isNotEmpty() && ! id.contains (only)) continue;
        auto file = dir.getChildFile (ph["file"].toString());
        if (! file.existsAsFile()) { ++missing; continue; }
        juce::String err;
        auto img = rmcv::loadPhoto (file, err);
        if (! img.isValid()) { std::cout << "  " << id << ": nejde nacist" << std::endl; continue; }
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        std::vector<rmcv::Result> res (profs.size());
        for (size_t i = 0; i < profs.size(); ++i) res[i] = rmcv::recognize (img, *profs[i], refs[i]);
        const auto chosen = rmcv::chooseDevices (res, profs);
        rmcv::releaseWorkingMemory();
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;

        juce::StringArray expected;
        for (auto& e : *ph["expected"].getArray()) expected.add (e.toString());
        const auto condition = ph.getProperty ("condition", "puvodni").toString();
        Agg row;
        row.photos = 1; row.ms = ms; row.expected = expected.size();
        juce::StringArray taken, notes;
        for (size_t i = 0; i < profs.size(); ++i)
        {
            if (! chosen[i]) continue;
            taken.add (profs[i]->id);
            // xfilter ME a 1U maji stejne rozlozeni: zamena se pocita jako nalezeni (stupnice ale nesedi -> uvedeno)
            const bool expectedHere = expected.contains (profs[i]->id);
            if (! expectedHere) { ++row.falsePos; notes.add ("navic " + profs[i]->id); }
        }
        auto truth = ph.getProperty ("truth", juce::var());
        const double tol = (double) truth.getProperty ("tolerance", 20);
        for (auto& pid : expected)
        {
            auto& m = byModel[pid];
            m.photos++; m.expected++; m.ms += ms / juce::jmax (1, expected.size());
            size_t k = 0; for (; k < profs.size(); ++k) if (profs[k]->id == pid) break;
            const bool found = k < profs.size() && chosen[k];
            if (found) { ++row.found; ++m.found; } else notes.add ("nenalezen " + pid);
            auto dt = truth["devices"][juce::Identifier (pid)];
            if (dt.isVoid() || k >= profs.size()) continue;
            auto* prof = profs[k];
            auto score = [&] (const juce::String& cid, std::function<bool (const rm::ControlValue&, const rm::ControlDef&)> ok, bool expectUnknown)
            {
                auto* def = prof->find (cid); if (def == nullptr) return;
                auto& kd = byKind[controlKind (*def)];
                ++row.truth; ++m.truth; ++kd.truth;
                rm::ControlValue v;
                if (found) { auto it = res[k].values.find (cid); if (it != res[k].values.end()) v = it->second; }
                if (expectUnknown) { if (v.known) { ++row.wrong; ++m.wrong; ++kd.wrong; notes.add (cid + " ma byt nezadano"); } else { ++row.correct; ++m.correct; ++kd.correct; } return; }
                if (! v.known) { ++row.unknown; ++m.unknown; ++kd.unknown; return; }
                if (ok (v, *def)) { ++row.correct; ++m.correct; ++kd.correct; }
                else { ++row.wrong; ++m.wrong; ++kd.wrong; notes.add (cid + "=" + juce::String (juce::roundToInt (rm::angleFor (*def, v))) + "st."); }
            };
            if (auto* a = dt.getProperty ("angles", juce::var()).getDynamicObject())
                for (auto& kv : a->getProperties())
                {
                    const double want = (double) kv.value;
                    score (kv.name.toString(), [want, tol] (const rm::ControlValue& v, const rm::ControlDef& d)
                    { double dd = std::abs (rm::angleFor (d, v) - want); if (dd > 180) dd = 360 - dd; return dd <= tol; }, false);
                }
            if (auto* a = dt.getProperty ("positions", juce::var()).getDynamicObject())
                for (auto& kv : a->getProperties())
                {
                    const double want = (double) kv.value;
                    score (kv.name.toString(), [want] (const rm::ControlValue& v, const rm::ControlDef&) { return std::abs (v.pos - want) < 0.5; }, false);
                }
            if (auto* a = dt.getProperty ("unknown", juce::var()).getArray())
                for (auto& e : *a) score (e.toString(), {}, true);
        }
        auto& cnd = byCondition[condition];
        for (auto* g : { &all, &cnd })
        {
            g->photos++; g->expected += row.expected; g->found += row.found; g->falsePos += row.falsePos;
            g->truth += row.truth; g->correct += row.correct; g->wrong += row.wrong; g->unknown += row.unknown; g->ms += ms;
        }
        std::cout << "  " << id.paddedRight (' ', 44) << "nalezeno " << row.found << "/" << row.expected << "  navic " << row.falsePos
                  << (row.truth > 0 ? "  ovladace " + juce::String (row.correct) + "/" + juce::String (row.truth) + " spatne " + juce::String (row.wrong) : juce::String())
                  << "  " << juce::roundToInt (ms) << " ms" << (notes.isEmpty() ? juce::String() : "  [" + notes.joinIntoString (", ") + "]") << std::endl;
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", id); o->setProperty ("condition", condition); o->setProperty ("found", row.found); o->setProperty ("expected", row.expected);
        o->setProperty ("falsePositives", row.falsePos); o->setProperty ("truth", row.truth); o->setProperty ("correct", row.correct);
        o->setProperty ("wrong", row.wrong); o->setProperty ("unknown", row.unknown); o->setProperty ("ms", juce::roundToInt (ms));
        o->setProperty ("taken", taken.joinIntoString (","));
        rows.add (juce::var (o));
    }
    auto line = [] (const juce::String& name, const Agg& a)
    {
        std::cout << "  " << name.paddedRight (' ', 26) << " fotek " << a.photos << "  nalezeno " << a.found << "/" << a.expected << "  navic " << a.falsePos
                  << "  ovladace spravne " << a.correct << "/" << a.truth << " spatne " << a.wrong << " nejiste " << a.unknown << std::endl;
    };
    std::cout << "  -- podle modelu" << std::endl;      for (auto& kv : byModel) line (kv.first, kv.second);
    std::cout << "  -- podle typu ovladace" << std::endl; for (auto& kv : byKind) line (kv.first, kv.second);
    std::cout << "  -- podle podminek" << std::endl;    for (auto& kv : byCondition) line (kv.first, kv.second);
    line ("CELKEM", all);
    if (missing > 0) std::cout << "  (chybi " << missing << " souboru - stahni je: Tools/fetch_photo_dataset.py)" << std::endl;
    if (argc > 4)
    {
        auto toVar = [] (const Agg& a)
        {
            auto* e = new juce::DynamicObject();
            e->setProperty ("photos", a.photos); e->setProperty ("expected", a.expected); e->setProperty ("found", a.found); e->setProperty ("falsePositives", a.falsePos);
            e->setProperty ("truth", a.truth); e->setProperty ("correct", a.correct); e->setProperty ("wrong", a.wrong); e->setProperty ("unknown", a.unknown);
            return juce::var (e);
        };
        auto* o = new juce::DynamicObject();
        o->setProperty ("rows", rows); o->setProperty ("all", toVar (all));
        auto* bm = new juce::DynamicObject(); for (auto& kv : byModel) bm->setProperty (juce::Identifier (kv.first), toVar (kv.second));
        auto* bk = new juce::DynamicObject(); for (auto& kv : byKind) bk->setProperty (juce::Identifier (kv.first.replaceCharacters (" ()/", "____")), toVar (kv.second));
        auto* bc = new juce::DynamicObject(); for (auto& kv : byCondition) bc->setProperty (juce::Identifier (kv.first), toVar (kv.second));
        o->setProperty ("byModel", juce::var (bm)); o->setProperty ("byKind", juce::var (bk)); o->setProperty ("byCondition", juce::var (bc));
        cwd.getChildFile (juce::String (argv[4])).replaceWithText (juce::JSON::toString (juce::var (o)));
    }
    return 0;
}
