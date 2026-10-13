#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>
#include "Model.h"
#include "Levels.h"

// Pamet nastaveni racku. Zvuk prochazi beze zmeny, jen s volitelnou hlasitosti (gain, 0 dB = bitove beze zmeny)
// a merenim urovne. Dve instance (IN pred hardwarem, OUT za nim) se navzajem vidi a ukazuji rozdil.
class RackMemoryProcessor : public juce::AudioProcessor,
                            public juce::ChangeBroadcaster,
                            private juce::AsyncUpdater
{
public:
    RackMemoryProcessor();
    ~RackMemoryProcessor() override;

    //== audio ==================================================================
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                 { return true; }
    const juce::String getName() const override     { return "Rack Memory"; }
    bool acceptsMidi() const override               { return false; }
    bool producesMidi() const override              { return false; }
    double getTailLengthSeconds() const override    { return 0.0; }
    int getNumPrograms() override                   { return 1; }
    int getCurrentProgram() override                { return 0; }
    void setCurrentProgram (int) override           {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //== hlasitost a mereni ======================================================
    enum class Role { none = 0, in = 1, out = 2 };     // in = pred hardwarem (send), out = za nim (return)
    juce::AudioParameterFloat* gainParam = nullptr;    // dB
    rm::LoudnessMeter meter;                           // meri se signal ZA gainem (co jde do HW / dal)
    Role role = Role::none;
    int linkGroup = 1;                                 // vic retezcu v jednom projektu: skupina 1..4
    float gainDb() const                               { return gainParam->get(); }
    void setGainDb (float db);
    RackMemoryProcessor* partner() const;              // druha instance ve stejne skupine (IN <-> OUT), jinak nullptr
    void setRole (Role, int group);

    //== stav (jen z message threadu) ==========================================
    rm::Library library;
    std::vector<rm::Snapshot> all;      // cela knihovna, nejnovejsi prvni
    juce::StringArray loadErrors;       // poskozene soubory (nemazou se)

    rm::Snapshot working;               // prave zobrazene nastaveni racku
    juce::String loadedId;              // ulozeny snapshot, ze ktereho working vychazi ("" = zadny)
    bool dirty = false;                 // zmeneno od posledniho ulozeni / nacteni
    juce::String songName;
    bool showValues = false;
    juce::String instanceId;

    void reloadLibrary();
    const rm::Snapshot* findSnapshot (const juce::String& id) const;
    int nextVersionNumber (const juce::String& song) const;

    // Ulozeni vzdy vytvori novou verzi (v1, v2, ...) - nic se neprepisuje, neni na co se ptat.
    juce::Result save();
    bool load (const juce::String& id);             // predchozi stav jde vratit pres undo()
    bool canUndo() const                            { return hasUndo; }
    void undo();
    juce::Result deleteSnapshot (const juce::String& id);
    void clearAll();                                // vsechno zpet na "nezadano" (jde vratit pres undo)

    // cteni z fotky (stav jen pro tuto relaci, do projektu se neuklada)
    struct DevicePhoto
    {
        juce::Image rectified;          // fotka narovnana na panel (pro kontrolu okem)
        juce::Image original;           // zmensena puvodni fotka (prohlizeni dole)
        juce::String status;            // kratky text do slotu
        bool busy = false, failed = false;
        int read = 0, readable = 0;
    };
    std::map<juce::String, DevicePhoto> devicePhotos;
    bool showPhotos = false;
    // Hodnoty pristroje nahradi tim, co se precetlo z fotky; nejiste ovladace zustanou nezadane. Jde vratit pres undo().
    void applyRecognition (const juce::String& deviceId, const std::map<juce::String, rm::ControlValue>& values);
    void applyRecognition (const std::map<juce::String, std::map<juce::String, rm::ControlValue>>& perDevice);   // vic pristroju, jeden krok zpet

    // Rack uzivatele (dlouhodobe, spolecny pro vsechny projekty): ~/Music/Rack Memory/rack.json
    rm::RackLayout rack;
    void reloadRack();
    juce::Result setRack (const rm::RackLayout&);                // ulozi hned (jine instance si ho nactou)
    juce::StringArray rackOrder() const                          { return rack.ids(); }
    juce::StringArray missingInstances() const;                  // kusy s hodnotami v teto verzi, ktere v racku nejsou

    void markDirty();                               // po kazde zmene working / songName
    void commitState();                             // prepocita stav ukladany do projektu Logicu

    juce::String buildStateJson() const;
    void applyStateJson (const juce::String&);

private:
    void handleAsyncUpdate() override;
    template <typename T> void processAudio (juce::AudioBuffer<T>&);
    juce::SmoothedValue<float> gainSmooth { 1.0f };
    double currentRate = 44100.0;
    void rememberForUndo();

    rm::Snapshot undoWorking;
    juce::String undoLoadedId;
    bool undoDirty = false, hasUndo = false;

    juce::CriticalSection stateLock;
    juce::String stateCache, pendingState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackMemoryProcessor)
};
