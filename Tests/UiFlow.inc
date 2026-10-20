// Mereni stejnou cestou jako tlacitko FOTO v editoru (pro porovnani verzi na stejne fotce).
// Pouziti: RackMemoryTests uiflow <fotka> <truth.json> <profil> [<profil> ...]
// Profil "elysia.xfilter.me" se hodnoti podle pravdy "elysia.xfilter.1u" (stejne fyzicke polohy knobu).
#include <thread>
static int runUiFlow (int argc, char** argv)
{
    juce::String err;
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    auto img = rmcv::loadPhoto (cwd.getChildFile (argv[2]), err);
    if (! img.isValid()) { std::cout << "fotka nejde nacist: " << err << std::endl; return 1; }
    juce::var truth;
    juce::JSON::parse (cwd.getChildFile (argv[3]).loadFileAsString(), truth);
    auto devTruth = truth["photos"][0]["devices"];
    const double tol = (double) truth["photos"][0].getProperty ("tolerance", 20);

    std::vector<const rm::DeviceProfile*> profs;
    std::vector<juce::Image> refs;
    for (int i = 4; i < argc; ++i)
        if (auto* p = rm::findProfile (argv[i])) { profs.push_back (p); refs.push_back (rm::loadAsset (p->image).createCopy()); }
        else std::cout << "neznamy profil " << argv[i] << std::endl;

    // stejne jako editor od 0.11: jeden pristroj po druhem v jednom vlakne, vyber podle prekryvu
    std::vector<rmcv::Result> results (profs.size());
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (size_t i = 0; i < profs.size(); ++i) results[i] = rmcv::recognize (img, *profs[i], refs[i]);
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    const auto chosen = rmcv::chooseDevices (results, profs);
    int found = 0, taken = 0, truthN = 0, correct = 0, wrong = 0, unsure = 0;
    for (size_t i = 0; i < profs.size(); ++i)
    {
        auto& r = results[i];
        auto* prof = profs[i];
        const bool take = chosen[i];
        found += r.aligned ? 1 : 0; taken += take ? 1 : 0;
        juce::String tid = prof->id == "elysia.xfilter.me" ? "elysia.xfilter.1u" : prof->id;
        auto t = devTruth.getProperty (juce::Identifier (tid), juce::var());
        int n = 0, c = 0, w = 0, u = 0;
        auto score = [&] (const juce::String& id, double want, bool angle)
        {
            auto* def = prof->find (id); if (def == nullptr) return;
            ++n;
            auto it = r.values.find (id);
            if (! take || it == r.values.end() || ! it->second.known) { ++u; return; }
            const bool ok = angle ? std::abs (rm::angleFor (*def, it->second) - want) <= tol : std::abs (it->second.pos - want) < 0.5;
            ok ? ++c : ++w;
        };
        if (auto* a = t.getProperty ("angles", juce::var()).getDynamicObject())
            for (auto& kv : a->getProperties()) score (kv.name.toString(), (double) kv.value, true);
        if (auto* a = t.getProperty ("positions", juce::var()).getDynamicObject())
            for (auto& kv : a->getProperties()) score (kv.name.toString(), (double) kv.value, false);
        if (auto* a = t.getProperty ("unknown", juce::var()).getArray())
            for (auto& e : *a)
                if (auto* def = prof->find (e.toString()))
                {
                    juce::ignoreUnused (def); ++n;
                    auto it = r.values.find (e.toString());
                    (take && it != r.values.end() && it->second.known) ? ++w : ++c;
                }
        truthN += n; correct += c; wrong += w; unsure += u;
        int known = 0; for (auto& kv : r.values) if (kv.second.known) ++known;
        std::cout << "  " << prof->id.paddedRight (' ', 22) << (r.aligned ? "nalezen " : "NENALEZ ") << (take ? "PREVZAT " : (r.aligned ? "ZAHOZEN " : "        "))
                  << "body " << juce::String (r.keypoints).paddedLeft (' ', 4) << "  precteno " << known << "/" << (int) prof->controls.size()
                  << "  pravda: spravne " << c << "/" << n << " spatne " << w << " nejiste " << u << std::endl;
    }
    std::cout << "  CELKEM nalezeno " << found << "/" << profs.size() << ", prevzato " << taken << ", spravne " << correct << "/" << truthN
              << ", spatne " << wrong << ", nejiste " << unsure << ", cas " << juce::roundToInt (ms) << " ms" << std::endl;
    return 0;
}
