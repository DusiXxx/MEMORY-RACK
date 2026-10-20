#include "PluginProcessor.h"
#include "PluginEditor.h"

using juce::String;
using juce::var;

namespace
{
// instance pluginu v tomto procesu (Logic): IN a OUT se najdou podle skupiny
juce::SpinLock registryLock;
std::vector<RackMemoryProcessor*>& registry() { static std::vector<RackMemoryProcessor*> r; return r; }
}

RackMemoryProcessor::RackMemoryProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    addParameter (gainParam = new juce::AudioParameterFloat (juce::ParameterID { "gain", 1 }, "Digital gain",
                                                             juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("dB")));
    // Skryty parametr "stav": Logic se o zmenach mimo parametry (knoby racku) jinak nedozvi a projekt
    // by se zavrel bez dotazu na ulozeni. Neautomatizovatelny, zvuk neovlivnuje.
    addParameter (stateParam = new juce::AudioParameterFloat (juce::ParameterID { "state", 1 }, "Rack Memory state",
                                                              juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                                                              juce::AudioParameterFloatAttributes().withAutomatable (false)));
    {
        const juce::SpinLock::ScopedLockType l (registryLock);
        registry().push_back (this);
    }
    instanceId = juce::Uuid().toString();
    working = rm::Snapshot::createNew ({});
    all = library.loadAll (&loadErrors);
    reloadRack();
    loadedByGroup[currentGroup] = {};

    const juce::ScopedLock sl (stateLock);
    stateCache = buildStateJson();
}

RackMemoryProcessor::~RackMemoryProcessor()
{
    stopTimer();
    if (pendingRecovery) writeRecovery();
    {
        const juce::SpinLock::ScopedLockType l (registryLock);
        auto& r = registry();
        r.erase (std::remove (r.begin(), r.end(), this), r.end());
    }
    cancelPendingUpdate();
}

RackMemoryProcessor* RackMemoryProcessor::partner() const
{
    if (role == Role::none) return nullptr;
    const auto want = role == Role::in ? Role::out : Role::in;
    const juce::SpinLock::ScopedLockType l (registryLock);
    for (auto* p : registry())
        if (p != this && p->role == want && p->linkGroup == linkGroup) return p;
    return nullptr;
}

void RackMemoryProcessor::setRole (Role r, int group)
{
    role = r; linkGroup = juce::jlimit (1, 4, group);
    commitState();
}

void RackMemoryProcessor::setGainDb (float db)
{
    gainParam->setValueNotifyingHost (gainParam->convertTo0to1 (juce::jlimit (-24.0f, 24.0f, db)));
}

void RackMemoryProcessor::prepareToPlay (double sampleRate, int)
{
    currentRate = sampleRate;
    meter.prepare (sampleRate, getTotalNumOutputChannels());
    gainSmooth.reset (sampleRate, 0.05);
    gainSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainParam->get()));
}

//==============================================================================
bool RackMemoryProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

// Bez zamku, alokaci, souboru a obrazku v audio vlakne. Gain 0 dB = buffer se nemeni (bitove beze zmeny).
template <typename T>
void RackMemoryProcessor::processAudio (juce::AudioBuffer<T>& buffer)
{
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    gainSmooth.setTargetValue (juce::Decibels::decibelsToGain (gainParam->get(), -100.0f));
    const int n = buffer.getNumSamples(), nch = buffer.getNumChannels();
    if (gainSmooth.isSmoothing())
    {
        for (int i = 0; i < n; ++i)
        {
            const T g = (T) gainSmooth.getNextValue();
            for (int ch = 0; ch < nch; ++ch) buffer.getWritePointer (ch)[i] *= g;
        }
    }
    else if (gainSmooth.getTargetValue() != 1.0f)
        buffer.applyGain ((T) gainSmooth.getTargetValue());

    meter.process (buffer.getArrayOfReadPointers(), nch, n);
}

void RackMemoryProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)    { processAudio (buffer); }
void RackMemoryProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)   { processAudio (buffer); }

juce::AudioProcessorEditor* RackMemoryProcessor::createEditor()
{
    return new RackMemoryEditor (*this);
}

//==============================================================================
void RackMemoryProcessor::reloadLibrary()
{
    loadErrors.clear();
    all = library.loadAll (&loadErrors);
}

const rm::Snapshot* RackMemoryProcessor::findSnapshot (const String& id) const
{
    for (auto& s : all) if (s.id == id) return &s;
    return nullptr;
}

int RackMemoryProcessor::nextVersionNumber (const String& song) const
{
    // cislovani po skladbe a skupine (Mixing v1, v2 ... / Mastering v1, v2 ...); starsi verze bez skupiny patri k prvni ulozene skupine
    int n = 0;
    for (auto& s : all)
        if (s.project == song && (s.group == currentGroup || s.group.isEmpty())) n = juce::jmax (n, rm::versionNumber (s.version));
    return n + 1;
}

juce::StringArray RackMemoryProcessor::instancesOf (const rm::Snapshot& s) const
{
    // verze skupiny: kusy, ktere v ni pri ulozeni byly; starsi verze (bez skupiny): vsechny ulozene hodnoty
    if (s.group.isNotEmpty() && ! s.order.isEmpty()) return s.order;
    juce::StringArray r;
    for (auto& kv : s.values) r.addIfNotAlreadyThere (kv.first.upToFirstOccurrenceOf (":", false, false));
    r.removeString ("levels");
    for (auto& o : s.order) r.addIfNotAlreadyThere (o);
    return r;
}

String RackMemoryProcessor::snapshotTitle (const rm::Snapshot& s) const
{
    String t = s.project.isEmpty() ? String::fromUTF8 ("Bez názvu") : s.project;
    if (s.group.isNotEmpty())
        if (auto* g = studio.findGroup (s.group)) t << "  " << g->name;
    if (s.version.isNotEmpty()) t << "  " << s.version;
    if (s.name.isNotEmpty()) t << "  " << s.name;
    return t;
}

void RackMemoryProcessor::rememberForUndo()
{
    undoWorking = working; undoLoadedId = loadedId; undoDirty = dirty; hasUndo = true;
}

namespace
{
void putLevels (rm::Snapshot& copy, const RackMemoryProcessor& p)
{
    // hlasitost do a z hardwaru patri k nastaveni (klice "levels:in" / "levels:out", hodnota = dB)
    auto put = [&copy] (const char* k, float db) { rm::ControlValue v; v.known = true; v.pos = db; copy.values[rm::valueKey ("levels", k)] = v; };
    const auto* other = p.partner();
    if (p.role == RackMemoryProcessor::Role::out) { put ("out", p.gainDb()); if (other) put ("in", other->gainDb()); }
    else                                          { put ("in", p.gainDb());  if (other) put ("out", other->gainDb()); }
}
}

juce::Result RackMemoryProcessor::save()
{
    reloadLibrary();    // jina instance mohla mezitim ulozit dalsi verzi

    rm::Snapshot copy;
    copy.id = juce::Uuid().toString();
    copy.project = songName.trim();
    copy.group = currentGroup;
    copy.version = "v" + String (nextVersionNumber (copy.project));
    copy.notes = working.notes;
    copy.created = juce::Time::currentTimeMillis();
    copy.order = rack.ids();                         // kusy teto skupiny
    copy.values = working.valuesFor (copy.order);
    copy.cables = working.cables;
    copy.photos = working.photos;
    copy.defs = rm::definitionsFor (copy);
    for (auto& kv : working.devicePhotos) if (copy.order.contains (kv.first)) copy.devicePhotos[kv.first] = kv.second;
    for (auto& kv : working.photoAnalysis) if (copy.order.contains (kv.first)) copy.photoAnalysis[kv.first] = kv.second;
    putLevels (copy, *this);

    auto r = library.save (copy);
    if (r.failed()) return r;      // nastaveni v projektu zustava, nic se neztrati

    working.id = copy.id; working.project = copy.project; working.version = copy.version; working.group = copy.group;
    working.name.clear(); working.created = copy.created; working.modified = copy.modified;
    loadedId = copy.id;
    loadedByGroup[currentGroup] = loadedId;
    dirty = false;
    reloadLibrary();
    markDirty();          // stav projektu (nactena verze) se zmenil
    dirty = false;
    commitState();
    return r;
}

juce::Result RackMemoryProcessor::overwriteLoaded()
{
    auto* old = findSnapshot (loadedId);
    if (old == nullptr) return juce::Result::fail (String::fromUTF8 ("Není načtená žádná verze."));
    auto copy = *old;
    const auto inst = instancesOf (copy);
    for (auto it = copy.values.begin(); it != copy.values.end();)
        if (it->first.startsWith ("levels:") || inst.contains (it->first.upToFirstOccurrenceOf (":", false, false))) it = copy.values.erase (it); else ++it;
    for (auto& kv : working.valuesFor (inst)) copy.values[kv.first] = kv.second;
    copy.notes = working.notes;
    copy.cables = working.cables;
    putLevels (copy, *this);
    auto r = library.save (copy);
    if (r.failed()) return r;
    dirty = false;
    reloadLibrary();
    commitState();
    return r;
}

juce::Result RackMemoryProcessor::duplicateSnapshot (const String& id)
{
    auto* src = findSnapshot (id);
    if (src == nullptr) return juce::Result::fail (String::fromUTF8 ("Verze neexistuje."));
    auto copy = *src;
    const auto keepGroup = currentGroup;
    currentGroup = copy.group;
    copy.version = "v" + String (nextVersionNumber (copy.project));
    currentGroup = keepGroup;
    copy.id = juce::Uuid().toString();
    copy.name = (src->name.isNotEmpty() ? src->name : src->version) + String::fromUTF8 (" – kopie");
    copy.created = juce::Time::currentTimeMillis();
    auto r = library.save (copy);
    reloadLibrary();
    return r;
}

juce::Result RackMemoryProcessor::renameSnapshot (const String& id, const String& name)
{
    auto* src = findSnapshot (id);
    if (src == nullptr) return juce::Result::fail (String::fromUTF8 ("Verze neexistuje."));
    auto copy = *src;
    copy.name = name.trim().substring (0, 80);
    auto r = library.save (copy);
    reloadLibrary();
    if (id == loadedId) working.name = copy.name;
    commitState();
    return r;
}

std::vector<rm::Difference> RackMemoryProcessor::compareWith (const String& id) const
{
    auto* s = findSnapshot (id);
    if (s == nullptr) return {};
    return rm::compare (*s, working, instancesOf (*s));
}

bool RackMemoryProcessor::load (const String& id)
{
    if (auto* s = findSnapshot (id))
    {
        rememberForUndo();
        const auto inst = instancesOf (*s);
        if (s->group.isEmpty())
        {
            // starsi verze = cely rack najednou (jako drive)
            for (auto it = working.values.begin(); it != working.values.end();)
                if (! it->first.startsWith ("levels:")) it = working.values.erase (it); else ++it;
        }
        else
        {
            for (auto it = working.values.begin(); it != working.values.end();)
                if (inst.contains (it->first.upToFirstOccurrenceOf (":", false, false))) it = working.values.erase (it); else ++it;
        }
        for (auto& kv : s->values)
            if (! kv.first.startsWith ("levels:")) working.values[kv.first] = kv.second;
        for (auto& kv : s->devicePhotos) working.devicePhotos[kv.first] = kv.second;
        for (auto& kv : s->photoAnalysis) working.photoAnalysis[kv.first] = kv.second;
        for (auto& ph : s->photos) working.photos.addIfNotAlreadyThere (ph);
        working.notes = s->notes;
        working.id = s->id; working.version = s->version; working.project = s->project; working.group = s->group;
        working.name = s->name; working.created = s->created; working.modified = s->modified;
        loadedId = id;
        loadedByGroup[currentGroup] = id;
        dirty = false;
        // ulozena hlasitost do / z hardwaru
        auto in = s->get ("levels", "in"), out = s->get ("levels", "out");
        auto* other = partner();
        if (role == Role::out) { if (out.known) setGainDb ((float) out.pos); if (other && in.known) other->setGainDb ((float) in.pos); }
        else                   { if (in.known) setGainDb ((float) in.pos);   if (other && out.known) other->setGainDb ((float) out.pos); }
        markDirty();
        dirty = false;
        commitState();
        return true;
    }
    return false;
}

void RackMemoryProcessor::undo()
{
    if (! hasUndo) return;
    std::swap (working, undoWorking);
    std::swap (loadedId, undoLoadedId);
    std::swap (dirty, undoDirty);
    if (loadedId.isNotEmpty() && findSnapshot (loadedId) == nullptr) { loadedId.clear(); dirty = true; }
    loadedByGroup[currentGroup] = loadedId;
    const bool d = dirty;
    markDirty();
    dirty = d;
    commitState();
}

juce::Result RackMemoryProcessor::deleteSnapshot (const String& id)
{
    auto r = library.remove (id);
    if (r.failed()) return r;

    if (loadedId == id)
    {
        // nastaveni zustane na obrazovce, jen uz neni ulozene
        loadedId.clear();
        working.id = juce::Uuid().toString();
        working.version.clear();
        working.name.clear();
        dirty = true;
    }
    for (auto& kv : loadedByGroup) if (kv.second == id) kv.second.clear();
    if (undoLoadedId == id) undoLoadedId.clear();
    reloadLibrary();
    commitState();
    return r;
}

void RackMemoryProcessor::clearAll()
{
    rememberForUndo();
    // jen pristroje aktualni skupiny (ostatni skupiny se nemeni)
    const auto inst = rack.ids(), everywhere = studio.allInstances();
    for (auto it = working.values.begin(); it != working.values.end();)
    {
        const auto dev = it->first.upToFirstOccurrenceOf (":", false, false);
        if (dev != "levels" && (inst.contains (dev) || ! everywhere.contains (dev))) it = working.values.erase (it); else ++it;
    }
    loadedId.clear();
    loadedByGroup[currentGroup].clear();
    markDirty();
}

void RackMemoryProcessor::clearDevice (const String& deviceId)
{
    rememberForUndo();
    for (auto it = working.values.begin(); it != working.values.end();)
        if (it->first.upToFirstOccurrenceOf (":", false, false) == deviceId) it = working.values.erase (it); else ++it;
    markDirty();
}

void RackMemoryProcessor::applyRecognition (const String& deviceId, const std::map<String, rm::ControlValue>& values)
{
    applyRecognition (std::map<String, std::map<String, rm::ControlValue>> { { deviceId, values } });
}

void RackMemoryProcessor::applyRecognition (const std::map<String, std::map<String, rm::ControlValue>>& perDevice)
{
    rememberForUndo();
    for (auto& dev : perDevice)
    {
        auto& review = needsReview[dev.first];
        review.clear();
        auto& proposals = photoProposals[dev.first];
        proposals.clear();
        const auto* prof = rm::profileForInstance (dev.first);
        for (auto& kv : dev.second)
        {
            const auto key = rm::valueKey (dev.first, kv.first);
            auto pred = predictions.find (dev.first);
            auto* pr = pred != predictions.end() && pred->second.count (kv.first) ? &pred->second[kv.first] : nullptr;
            if (! kv.second.known)
            {
                review.add (kv.first);                   // nejiste: nic se nemeni, rucni hodnota zustava
                if (pr != nullptr) pr->eligible = ! working.get (key).known;
                continue;
            }
            auto v = kv.second;
            v.source = "recognized";
            v.confirmed = false;                         // z fotky: ceka na kontrolu okem
            auto it = working.values.find (key);
            if (it != working.values.end() && it->second.known && it->second.confirmed)
            {
                // rucne nastaveno / potvrzeno: prepsat smi jen uzivatel
                const bool same = rm::sameReading (prof != nullptr ? prof->find (kv.first) : nullptr, it->second.pos, v.pos);
                if (! same) proposals[kv.first] = v;
                if (pr != nullptr) pr->eligible = false;     // rucni hodnota z drivejska neni overeni teto fotky
                continue;
            }
            working.values[key] = v;
            if (pr != nullptr) pr->eligible = true;
        }
        if (proposals.empty()) photoProposals.erase (dev.first);
    }
    markDirty();
}

int RackMemoryProcessor::acceptPhotoProposals (const String& deviceId)
{
    auto it = photoProposals.find (deviceId);
    if (it == photoProposals.end() || it->second.empty()) return 0;
    rememberForUndo();
    int n = 0;
    for (auto& kv : it->second)
    {
        working.values[rm::valueKey (deviceId, kv.first)] = kv.second; ++n;
        if (predictions.count (deviceId) && predictions[deviceId].count (kv.first)) predictions[deviceId][kv.first].eligible = true;
    }
    photoProposals.erase (it);
    markDirty();
    return n;
}

void RackMemoryProcessor::rememberPredictions (const String& deviceId, const String& fingerprint, const String& algorithm, const String& photo,
                                               const std::map<String, rm::ControlValue>& values, const std::map<String, String>& states)
{
    auto& p = predictions[deviceId];
    p.clear();
    const auto* prof = rm::profileForInstance (deviceId);
    for (auto& kv : states)
    {
        Prediction pr;
        pr.fingerprint = fingerprint; pr.algorithm = algorithm; pr.photo = photo; pr.state = kv.second;
        pr.model = prof != nullptr ? prof->id : String();
        auto v = values.find (kv.first);
        if (v != values.end()) pr.value = v->second;
        p[kv.first] = pr;
    }
}

juce::File RackMemoryProcessor::correctionsFile() const
{
    return library.getRoot().getChildFile ("Corrections").getChildFile ("corrections.jsonl");
}

int RackMemoryProcessor::capturePhotoFeedback()
{
    String lines;
    int n = 0;
    for (auto& dev : predictions)
    {
        const auto* prof = rm::profileForInstance (dev.first);
        for (auto& kv : dev.second)
        {
            const auto& pr = kv.second;
            if (pr.fingerprint.isEmpty() || ! pr.eligible) continue;
            const auto cur = working.get (dev.first, kv.first);
            if (! cur.known || ! cur.confirmed) continue;         // jen to, co uzivatel potvrdil nebo nastavil sam
            const bool same = pr.value.known && rm::sameReading (prof != nullptr ? prof->find (kv.first) : nullptr, pr.value.pos, cur.pos);
            const String status = same ? "confirmed" : "corrected";
            const String id = pr.fingerprint + "|" + dev.first + "|" + kv.first + "|" + String (cur.pos, 3) + "|" + status;
            if (captured.contains (id)) continue;
            captured.add (id);
            auto* o = new juce::DynamicObject();
            o->setProperty ("time", juce::Time::getCurrentTime().toISO8601 (true));
            o->setProperty ("photoFingerprint", pr.fingerprint);
            o->setProperty ("photo", pr.photo);
            o->setProperty ("model", pr.model);
            o->setProperty ("instance", dev.first);
            o->setProperty ("control", kv.first);
            o->setProperty ("algorithm", pr.algorithm);
            o->setProperty ("predictedState", pr.state);
            o->setProperty ("predicted", pr.value.known ? juce::var (pr.value.pos) : juce::var());
            if (pr.value.confidence >= 0) o->setProperty ("predictedConfidence", pr.value.confidence);
            o->setProperty ("value", cur.pos);
            o->setProperty ("status", status);              // confirmed = fotka mela pravdu, corrected = uzivatel opravil
            o->setProperty ("verification", "user");        // overeno uzivatelem okem na hardwaru, ne referencni zdroj
            lines << juce::JSON::toString (juce::var (o), true) << "\n";
            ++n;
        }
    }
    if (n > 0)
    {
        auto f = correctionsFile();
        f.getParentDirectory().createDirectory();
        f.appendText (lines, false, false, "\n");
    }
    return n;
}

int RackMemoryProcessor::confirmRecognized (const String& deviceId)
{
    int n = 0;
    for (auto& kv : working.values)
        if (kv.first.upToFirstOccurrenceOf (":", false, false) == deviceId && kv.second.known && ! kv.second.confirmed)
        { kv.second.confirmed = true; ++n; }
    if (n > 0) { markDirty(); capturePhotoFeedback(); }
    return n;
}

int RackMemoryProcessor::numUnconfirmed (const String& deviceId) const
{
    int n = 0;
    for (auto& kv : working.values)
        if (kv.first.upToFirstOccurrenceOf (":", false, false) == deviceId && kv.second.known && ! kv.second.confirmed) ++n;
    return n;
}

void RackMemoryProcessor::unlinkPhoto (const String& deviceId)
{
    // fotka je jen odkaz: nastaveni pristroje i soubor v knihovne (muze ho pouzivat starsi verze) zustavaji
    working.devicePhotos.erase (deviceId);
    working.photoAnalysis.erase (deviceId);
    devicePhotos.erase (deviceId);
    needsReview.erase (deviceId);
    markDirty();
}

juce::Result RackMemoryProcessor::savePreset (const String& deviceId, const String& name)
{
    auto* prof = rm::profileForInstance (deviceId);
    if (prof == nullptr) return juce::Result::fail ("?");
    rm::DevicePreset p;
    p.name = name.trim().isNotEmpty() ? name.trim() : String ("Preset");
    p.model = prof->id;
    for (auto& c : prof->controls)
    {
        auto v = working.get (deviceId, c.id);
        if (v.known) p.values[c.id] = v;
    }
    if (p.values.empty()) return juce::Result::fail (String::fromUTF8 ("Přístroj nemá zadané žádné hodnoty."));
    return library.savePreset (p);
}

void RackMemoryProcessor::applyPreset (const String& deviceId, const rm::DevicePreset& p)
{
    if (rm::profileIdOf (deviceId) != p.model) return;
    rememberForUndo();
    for (auto it = working.values.begin(); it != working.values.end();)
        if (it->first.upToFirstOccurrenceOf (":", false, false) == deviceId) it = working.values.erase (it); else ++it;
    for (auto& kv : p.values)
    {
        auto v = kv.second; v.source = "preset"; v.confirmed = true;
        working.values[rm::valueKey (deviceId, kv.first)] = v;
    }
    markDirty();
}

juce::Result RackMemoryProcessor::moveDeviceToGroup (const String& id, const String& groupId)
{
    auto st = studio;
    auto* from = st.findGroup (currentGroup);
    auto* to = st.findGroup (groupId);
    if (from == nullptr || to == nullptr || groupId == currentGroup) return juce::Result::fail ("?");
    auto* item = from->layout.find (id);
    if (item == nullptr) return juce::Result::fail ("?");
    const auto model = item->model;
    if (to->layout.find (id) == nullptr && to->layout.add (model, id).isEmpty()) return juce::Result::fail (String::fromUTF8 ("Do cílové skupiny ho nejde přidat."));
    from->layout.remove (id);
    return setStudio (st);       // hodnoty kusu se nemeni (klic = ID kusu)
}

juce::Result RackMemoryProcessor::replaceDevice (const String& id, const String& newModel, bool carry, String& newId)
{
    auto* oldProf = rm::profileForInstance (id);
    auto* newProf = rm::findProfile (newModel);
    if (oldProf == nullptr || newProf == nullptr || newProf->isPatchbay()) return juce::Result::fail ("?");
    // 1) puvodni nastaveni skupiny jako verze (da se kdykoli nacist zpet)
    if (working.numKnown (*oldProf) > 0 || ! working.valuesFor ({ id }).empty())
    {
        auto r = save();
        if (r.failed()) return r;
        renameSnapshot (loadedId, String::fromUTF8 ("před nahrazením ") + oldProf->name);
    }
    // 2) v racku misto stareho kusu novy (stejne misto)
    auto lay = rack;
    newId = studio.newInstanceId (newModel);
    for (auto& it : lay.items)
        if (it.id == id) { it.id = newId; it.model = newModel; }
    // 3) polohy ovladacu se stejnym ID a stejnym poctem poloh (napr. xfilter ME -> xfilter 1U)
    if (carry)
        for (auto& c : newProf->controls)
            if (auto* oc = oldProf->find (c.id))
                if (oc->type == c.type && oc->numPositions() == c.numPositions())
                {
                    auto v = working.get (id, c.id);
                    if (v.known) working.values[rm::valueKey (newId, c.id)] = v;
                }
    auto r = setRack (lay);
    markDirty();
    return r;
}

void RackMemoryProcessor::setCables (const std::vector<rm::Cable>& c)
{
    if (c == working.cables) return;
    rememberForUndo();
    working.cables = c;
    markDirty();
}

//==============================================================================
void RackMemoryProcessor::reloadRack()
{
    rm::Studio s; String err;
    if (! library.loadStudio (s, err))
    {
        // prvni spusteni / starsi verze: vychozi poradi z settings.json (0.5), jinak vychozi rack
        juce::StringArray ids;
        juce::var v;
        auto f = library.getRoot().getChildFile ("settings.json");
        if (f.existsAsFile() && juce::JSON::parse (f.loadFileAsString(), v).wasOk())
            if (auto* a = v.getProperty ("rack", juce::var()).getArray())
                for (auto& e : *a) ids.add (e.toString());
        auto r = ids.isEmpty() ? rm::RackLayout::makeDefault() : rm::RackLayout::fromOrder (ids);
        if (r.items.empty()) r = rm::RackLayout::makeDefault();
        s = rm::Studio::fromLegacy (r);
        library.saveStudio (s);
    }
    studio = s;
    if (studio.findGroup (currentGroup) == nullptr)
    {
        // vychozi skupina: prvni neprazdna (u stavajicich uzivatelu Mastering = puvodni rack)
        currentGroup = studio.groups.front().id;
        for (auto& g : studio.groups) if (! g.layout.items.empty()) { currentGroup = g.id; break; }
    }
    rack = studio.findGroup (currentGroup)->layout;
}

juce::Result RackMemoryProcessor::setStudio (const rm::Studio& s)
{
    auto n = s;
    n.normalise();
    if (n == studio) return juce::Result::ok();
    auto res = library.saveStudio (n);
    if (res.wasOk())
    {
        studio = n;
        if (studio.findGroup (currentGroup) == nullptr) currentGroup = studio.groups.front().id;
        rack = studio.findGroup (currentGroup)->layout;
        commitState (false);
    }
    return res;
}

juce::Result RackMemoryProcessor::setRack (const rm::RackLayout& r)
{
    auto n = r;
    n.normalise();
    if (n == rack) return juce::Result::ok();
    auto s = studio;
    if (auto* g = s.findGroup (currentGroup)) g->layout = n;
    auto res = setStudio (s);
    if (res.wasOk()) rack = n;
    return res;
}

void RackMemoryProcessor::setGroup (const String& groupId)
{
    if (studio.findGroup (groupId) == nullptr || groupId == currentGroup) return;
    loadedByGroup[currentGroup] = loadedId;
    currentGroup = groupId;
    rack = studio.findGroup (groupId)->layout;
    loadedId = loadedByGroup[groupId];
    if (loadedId.isNotEmpty() && findSnapshot (loadedId) == nullptr) loadedId.clear();
    commitState (false);
}

juce::StringArray RackMemoryProcessor::missingInstances() const
{
    // kusy s hodnotami v nactene verzi teto skupiny, ktere v racku skupiny nejsou
    juce::StringArray out;
    const auto* s = findSnapshot (loadedId);
    for (auto& kv : working.values)
    {
        if (! kv.second.known) continue;
        const auto dev = kv.first.upToFirstOccurrenceOf (":", false, false);
        if (s != nullptr && ! instancesOf (*s).contains (dev)) continue;
        if (s == nullptr && studio.allInstances().contains (dev)) continue;      // patri do jine skupiny
        if (rack.find (dev) == nullptr && rm::profileForInstance (dev) != nullptr) out.addIfNotAlreadyThere (dev);
    }
    return out;
}

void RackMemoryProcessor::markDirty()
{
    dirty = true;
    commitState (true);
}

void RackMemoryProcessor::commitState (bool notifyHost)
{
    auto json = buildStateJson();
    {
        const juce::ScopedLock sl (stateLock);
        stateCache = json;
    }
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    if (notifyHost)
    {
        // AU (Logic) "nonParameterStateChanged" nepredava -> zmena skryteho parametru = projekt je zmeneny
        pendingNotify = true;
        pendingRecovery = true;
        if (juce::MessageManager::getInstanceWithoutCreating() != nullptr) startTimer (150);
    }
}

void RackMemoryProcessor::timerCallback()
{
    stopTimer();
    if (pendingNotify)
    {
        pendingNotify = false;
        const float next = stateParam->get() >= 0.5f ? stateParam->get() - 0.37f : stateParam->get() + 0.41f;
        stateParam->beginChangeGesture();
        stateParam->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, next));
        stateParam->endChangeGesture();
        ++hostNotifications;
    }
    if (emitted.exchange (false))
    {
        // Logic si stav vyzvedl (ulozeni projektu): rozpracovane zmeny uz jsou v projektu
        recoveryFile().deleteFile();
    }
    if (pendingRecovery) { pendingRecovery = false; writeRecovery(); }
}

//==============================================================================
// Obnova neulozenych zmen
juce::File RackMemoryProcessor::recoveryFile() const
{
    return library.recoveryDir().getChildFile (instanceId.retainCharacters ("0123456789abcdef") + ".json");
}

void RackMemoryProcessor::writeRecovery()
{
    String base;
    {
        const juce::ScopedLock sl (stateLock);
        base = lastEmittedToken.isNotEmpty() ? lastEmittedToken : loadedToken;
    }
    if (base.isEmpty() || ! dirty) return;           // instance, ktera jeste nikdy nebyla v ulozenem projektu
    auto* o = new juce::DynamicObject();
    o->setProperty ("schema", 1);
    o->setProperty ("instanceId", instanceId);
    o->setProperty ("base", base);
    o->setProperty ("time", juce::Time::currentTimeMillis());
    o->setProperty ("song", songName);
    o->setProperty ("state", buildStateJson());
    if (library.recoveryDir().createDirectory())
        recoveryFile().replaceWithText (juce::JSON::toString (juce::var (o)), false, false, "\n");
}

void RackMemoryProcessor::acceptRecovery()
{
    if (! recovery.available) return;
    const auto text = recovery.state;
    const auto keepId = instanceId;
    recovery = {};
    rememberForUndo();
    applyStateJson (text);
    instanceId = keepId;
    recovery = {};
    markDirty();                 // Logic: projekt je zmeneny -> ulozit
}

void RackMemoryProcessor::discardRecovery()
{
    recovery = {};
    recoveryFile().deleteFile();
}

//==============================================================================
String RackMemoryProcessor::buildStateJson() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("schema", rm::kSchemaVersion);
    o->setProperty ("instanceId", instanceId);
    o->setProperty ("songName", songName);
    o->setProperty ("loadedId", loadedId);
    o->setProperty ("group", currentGroup);
    {
        auto* lg = new juce::DynamicObject();
        for (auto& kv : loadedByGroup) if (kv.second.isNotEmpty() && kv.first.isNotEmpty()) lg->setProperty (juce::Identifier (kv.first), kv.second);
        o->setProperty ("loadedByGroup", var (lg));
    }
    o->setProperty ("dirty", dirty);
    o->setProperty ("showValues", showValues);
    o->setProperty ("highContrast", highContrast);
    o->setProperty ("fitToWindow", fitToWindow);
    o->setProperty ("photoOverlay", photoOverlay);
    o->setProperty ("overlayOpacity", overlayOpacity);
    o->setProperty ("role", (int) role);
    o->setProperty ("linkGroup", linkGroup);
    o->setProperty ("gain", gainParam->get());
    o->setProperty ("working", working.toVar());
    o->setProperty ("defs", rm::definitionsFor (working));
    o->setProperty ("studio", studio.toVar());        // kopie racku: kdyby na jinem Macu rack.json chybel

    // Kopie verzi teto skladby jde s projektem Logicu -> po presunu na jiny Mac
    // se obnovi do knihovny (fotky ne).
    juce::Array<var> embedded;
    for (auto& s : all)
        if (embedded.size() < 60 && (s.id == loadedId || (songName.isNotEmpty() && s.project == songName)))
            embedded.add (s.toVar());
    o->setProperty ("embedded", embedded);

    return juce::JSON::toString (var (o), true);
}

void RackMemoryProcessor::applyStateJson (const String& text)
{
    var v;
    if (juce::JSON::parse (text, v).failed() || v.getDynamicObject() == nullptr)
        return;   // poskozeny stav: nechame vychozi, nic nepada

    if ((int) v.getProperty ("schema", 0) > rm::kSchemaVersion)
        return;

    auto id = v.getProperty ("instanceId", "").toString();
    bool duplicate = false;
    if (id.isNotEmpty())
    {
        // zkopirovana stopa / plugin: dve instance se stejnym ID by si michaly zalohy -> nova identita
        const juce::SpinLock::ScopedLockType l (registryLock);
        for (auto* p : registry()) if (p != this && p->instanceId == id) duplicate = true;
    }
    if (id.isNotEmpty() && ! duplicate) instanceId = id;
    else if (duplicate) instanceId = juce::Uuid().toString();
    songName   = v.getProperty ("songName", v.getProperty ("projectName", "")).toString();
    loadedId   = v.getProperty ("loadedId", "").toString();
    dirty      = (bool) v.getProperty ("dirty", false);
    showValues = (bool) v.getProperty ("showValues", false);
    highContrast = (bool) v.getProperty ("highContrast", false);
    fitToWindow = (bool) v.getProperty ("fitToWindow", false);
    photoOverlay = (bool) v.getProperty ("photoOverlay", false);
    overlayOpacity = juce::jlimit (0.05f, 1.0f, (float) (double) v.getProperty ("overlayOpacity", 0.6));
    role       = (Role) juce::jlimit (0, 2, (int) v.getProperty ("role", 0));
    linkGroup  = juce::jlimit (1, 4, (int) v.getProperty ("linkGroup", 1));
    // gain z projektu: bez hlaseni hostiteli (nacteni projektu ho nesmi oznacit jako zmeneny)
    if (v.hasProperty ("gain")) static_cast<juce::AudioProcessorParameter*> (gainParam)->setValue (gainParam->convertTo0to1 (juce::jlimit (-24.0f, 24.0f, (float) (double) v.getProperty ("gain", 0.0))));
    hasUndo = false;
    {
        const juce::ScopedLock sl (stateLock);
        loadedToken = v.getProperty ("token", "").toString();
        lastEmittedToken.clear();
    }

    // rack: plati spolecny rack.json; kdyz na tomto Macu chybi, prevezme se kopie z projektu
    if (! library.hasStudio())
    {
        rm::Studio s; String err;
        if (v.hasProperty ("studio") && rm::Studio::fromVar (v.getProperty ("studio", var()), s, err))
            library.saveStudio (s);
    }
    const auto wantGroup = v.getProperty ("group", "").toString();
    if (wantGroup.isNotEmpty()) currentGroup = wantGroup;
    reloadRack();
    loadedByGroup.clear();
    if (auto* lg = v.getProperty ("loadedByGroup", var()).getDynamicObject())
        for (auto& kv : lg->getProperties()) loadedByGroup[kv.name.toString()] = kv.value.toString();
    if (! v.hasProperty ("group")) loadedByGroup[currentGroup] = loadedId;   // projekt ze starsi verze

    // obnova snapshotu, ktere v knihovne na tomto Macu chybi (a nebyly tu smazany)
    if (auto* arr = v.getProperty ("embedded", var()).getArray())
    {
        for (auto& e : *arr)
        {
            rm::Snapshot s; String err;
            if (rm::Snapshot::fromVar (e, s, err) && ! library.exists (s.id) && ! library.wasDeleted (s.id))
            {
                const auto keepModified = s.modified;
                if (library.save (s).wasOk() && keepModified > 0)
                {
                    s.modified = keepModified;
                    library.snapshotFile (s.id).replaceWithText (s.toJson(), false, false, "\n");
                }
            }
        }
    }

    reloadLibrary();

    rm::Snapshot w; String err;
    if (rm::Snapshot::fromVar (v.getProperty ("working", var()), w, err))
        working = w;
    else if (auto* s = findSnapshot (loadedId))
        { working = *s; dirty = false; }

    if (loadedId.isNotEmpty() && findSnapshot (loadedId) == nullptr)
    {
        // ulozeny zaznam uz neexistuje -> hodnoty nezahazujeme, vedeme je jako neulozene
        loadedId.clear();
        dirty = true;
    }
    for (auto& kv : loadedByGroup) if (kv.second.isNotEmpty() && findSnapshot (kv.second) == nullptr) kv.second.clear();
    loadedByGroup[currentGroup] = loadedId;

    // knihovna hardwaru se od ulozeni projektu zmenila? (hodnoty se nikdy tise neprevykladaji)
    profileNotes = rm::checkDefinitions (v.getProperty ("defs", var()));

    // neulozene zmeny z minula (projekt se zavrel bez ulozeni)? Nabidnout, nikdy samo neprepsat.
    recovery = {};
    if (! duplicate && loadedToken.isNotEmpty())
    {
        var r;
        auto f = recoveryFile();
        if (f.existsAsFile() && f.getSize() < 64 * 1024 * 1024 && juce::JSON::parse (f.loadFileAsString(), r).wasOk()
            && r.getProperty ("base", "").toString() == loadedToken && r.getProperty ("instanceId", "").toString() == instanceId)
        {
            var rs; rm::Snapshot rw; String e2;
            const auto stateText = r.getProperty ("state", "").toString();
            if (juce::JSON::parse (stateText, rs).wasOk() && rm::Snapshot::fromVar (rs.getProperty ("working", var()), rw, e2))
            {
                const auto diffs = rm::compare (working, rw);
                const bool other = diffs.size() > 0 || rw.cables != working.cables
                                   || rs.getProperty ("songName", "").toString() != songName;
                if (other)
                {
                    recovery.available = true;
                    recovery.time = (juce::int64) r.getProperty ("time", 0);
                    recovery.song = r.getProperty ("song", "").toString();
                    recovery.state = stateText;
                    recovery.changes = (int) diffs.size();
                }
                else f.deleteFile();
            }
        }
    }

    commitState (false);
    sendChangeMessage();
}

void RackMemoryProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    String s;
    bool pending = false;
    {
        const juce::ScopedLock sl (stateLock);
        pending = pendingState.isNotEmpty();
        s = pending ? pendingState : stateCache;
    }
    // gain je parametr (muze ho menit automatizace kdykoli) -> do stavu az pri ukladani.
    // token: identita prave ulozeneho stavu (obnova neulozenych zmen se nabidne jen ke spravnemu projektu)
    juce::var v;
    if (! pending && juce::JSON::parse (s, v).wasOk())
        if (auto* o = v.getDynamicObject())
        {
            const auto token = juce::Uuid().toString();
            o->setProperty ("gain", gainParam->get());
            o->setProperty ("token", token);
            s = juce::JSON::toString (v, true);
            {
                const juce::ScopedLock sl (stateLock);
                lastEmittedToken = token;
            }
            emitted = true;
        }
    dest.append (s.toRawUTF8(), s.getNumBytesAsUTF8());
}

void RackMemoryProcessor::setStateInformation (const void* data, int size)
{
    if (data == nullptr || size <= 0 || size > 64 * 1024 * 1024) return;
    auto text = String::fromUTF8 (static_cast<const char*> (data), size);

    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
        && juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        applyStateJson (text);
        return;
    }

    {
        const juce::ScopedLock sl (stateLock);
        pendingState = text;
    }
    triggerAsyncUpdate();
}

void RackMemoryProcessor::handleAsyncUpdate()
{
    String text;
    {
        const juce::ScopedLock sl (stateLock);
        text.swapWith (pendingState);
    }
    if (text.isNotEmpty())
        applyStateJson (text);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RackMemoryProcessor();
}
