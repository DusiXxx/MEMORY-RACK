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
    addParameter (gainParam = new juce::AudioParameterFloat (juce::ParameterID { "gain", 1 }, "Gain",
                                                             juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("dB")));
    {
        const juce::SpinLock::ScopedLockType l (registryLock);
        registry().push_back (this);
    }
    instanceId = juce::Uuid().toString();
    working = rm::Snapshot::createNew ({});
    all = library.loadAll (&loadErrors);
    reloadRack();

    const juce::ScopedLock sl (stateLock);
    stateCache = buildStateJson();
}

RackMemoryProcessor::~RackMemoryProcessor()
{
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
    int n = 0;
    for (auto& s : all)
        if (s.project == song) n = juce::jmax (n, rm::versionNumber (s.version));
    return n + 1;
}

void RackMemoryProcessor::rememberForUndo()
{
    undoWorking = working; undoLoadedId = loadedId; undoDirty = dirty; hasUndo = true;
}

juce::Result RackMemoryProcessor::save()
{
    reloadLibrary();    // jina instance mohla mezitim ulozit dalsi verzi

    auto copy = working;
    copy.id = juce::Uuid().toString();
    copy.project = songName.trim();
    copy.version = "v" + String (nextVersionNumber (copy.project));
    copy.created = juce::Time::currentTimeMillis();
    copy.order = rack.ids();          // jen pro informaci: s jakym rackem verze vznikla (rack sam je spolecny)
    // hlasitost do a z hardwaru patri k nastaveni (klice "levels:in" / "levels:out", hodnota = dB)
    {
        auto put = [&copy] (const char* k, float db) { rm::ControlValue v; v.known = true; v.pos = db; copy.values[rm::valueKey ("levels", k)] = v; };
        const auto* other = partner();
        if (role == Role::out) { put ("out", gainDb()); if (other) put ("in", other->gainDb()); }
        else                   { put ("in", gainDb());  if (other) put ("out", other->gainDb()); }
    }

    auto r = library.save (copy);
    if (r.failed()) return r;

    working = copy;
    loadedId = copy.id;
    dirty = false;
    reloadLibrary();
    commitState();
    return r;
}

bool RackMemoryProcessor::load (const String& id)
{
    if (auto* s = findSnapshot (id))
    {
        rememberForUndo();
        working = *s;
        loadedId = id;
        dirty = false;
        // ulozena hlasitost do / z hardwaru
        auto in = working.get ("levels", "in"), out = working.get ("levels", "out");
        auto* other = partner();
        if (role == Role::out) { if (out.known) setGainDb ((float) out.pos); if (other && in.known) other->setGainDb ((float) in.pos); }
        else                   { if (in.known) setGainDb ((float) in.pos);   if (other && out.known) other->setGainDb ((float) out.pos); }
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
        dirty = true;
    }
    if (undoLoadedId == id) undoLoadedId.clear();
    reloadLibrary();
    commitState();
    return r;
}

void RackMemoryProcessor::clearAll()
{
    rememberForUndo();
    working = rm::Snapshot::createNew (songName);
    loadedId.clear();
    dirty = false;
    commitState();
}

void RackMemoryProcessor::applyRecognition (const String& deviceId, const std::map<String, rm::ControlValue>& values)
{
    applyRecognition (std::map<String, std::map<String, rm::ControlValue>> { { deviceId, values } });
}

void RackMemoryProcessor::applyRecognition (const std::map<String, std::map<String, rm::ControlValue>>& perDevice)
{
    rememberForUndo();
    for (auto& dev : perDevice)
        for (auto& kv : dev.second)
        {
            const auto key = rm::valueKey (dev.first, kv.first);
            if (kv.second.known) working.values[key] = kv.second;
            else                 working.values.erase (key);       // nejiste = nezadano, nikdy odhad
        }
    markDirty();
}

//==============================================================================
void RackMemoryProcessor::reloadRack()
{
    rm::RackLayout r; String err;
    if (library.loadRack (r, err)) { rack = r; return; }

    // prvni spusteni / starsi verze: vychozi poradi z settings.json (0.5), jinak vychozi rack
    juce::StringArray ids;
    juce::var v;
    auto f = library.getRoot().getChildFile ("settings.json");
    if (f.existsAsFile() && juce::JSON::parse (f.loadFileAsString(), v).wasOk())
        if (auto* a = v.getProperty ("rack", juce::var()).getArray())
            for (auto& e : *a) ids.add (e.toString());
    rack = ids.isEmpty() ? rm::RackLayout::makeDefault() : rm::RackLayout::fromOrder (ids);
    if (rack.items.empty()) rack = rm::RackLayout::makeDefault();
    library.saveRack (rack);
}

juce::Result RackMemoryProcessor::setRack (const rm::RackLayout& r)
{
    auto n = r;
    n.normalise();
    if (n == rack) return juce::Result::ok();
    auto res = library.saveRack (n);
    if (res.wasOk()) rack = n;
    return res;
}

juce::StringArray RackMemoryProcessor::missingInstances() const
{
    juce::StringArray out;
    for (auto& kv : working.values)
    {
        if (! kv.second.known) continue;
        const auto dev = kv.first.upToFirstOccurrenceOf (":", false, false);
        if (rack.find (dev) == nullptr && rm::profileForInstance (dev) != nullptr) out.addIfNotAlreadyThere (dev);
    }
    return out;
}

void RackMemoryProcessor::markDirty()
{
    dirty = true;
    commitState();
}

void RackMemoryProcessor::commitState()
{
    auto json = buildStateJson();
    {
        const juce::ScopedLock sl (stateLock);
        stateCache = json;
    }
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

//==============================================================================
String RackMemoryProcessor::buildStateJson() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("schema", rm::kSchemaVersion);
    o->setProperty ("instanceId", instanceId);
    o->setProperty ("songName", songName);
    o->setProperty ("loadedId", loadedId);
    o->setProperty ("dirty", dirty);
    o->setProperty ("showValues", showValues);
    o->setProperty ("role", (int) role);
    o->setProperty ("linkGroup", linkGroup);
    o->setProperty ("gain", gainParam->get());
    o->setProperty ("working", working.toVar());

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
    if (id.isNotEmpty()) instanceId = id;
    songName   = v.getProperty ("songName", v.getProperty ("projectName", "")).toString();
    loadedId   = v.getProperty ("loadedId", "").toString();
    dirty      = (bool) v.getProperty ("dirty", false);
    showValues = (bool) v.getProperty ("showValues", false);
    role       = (Role) juce::jlimit (0, 2, (int) v.getProperty ("role", 0));
    linkGroup  = juce::jlimit (1, 4, (int) v.getProperty ("linkGroup", 1));
    if (v.hasProperty ("gain")) setGainDb ((float) (double) v.getProperty ("gain", 0.0));
    hasUndo = false;

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

    commitState();
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
    // gain je parametr (muze ho menit automatizace kdykoli) -> do stavu az pri ukladani
    juce::var v;
    if (! pending && juce::JSON::parse (s, v).wasOk())
        if (auto* o = v.getDynamicObject())
        {
            o->setProperty ("gain", gainParam->get());
            s = juce::JSON::toString (v, true);
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
