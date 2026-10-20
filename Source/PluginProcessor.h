#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>
#include "Model.h"
#include "Levels.h"

// Pamet nastaveni racku. Zvuk prochazi beze zmeny, jen s volitelnou hlasitosti (gain, 0 dB = bitove beze zmeny)
// a merenim urovne. Dve instance (IN pred hardwarem, OUT za nim) se navzajem vidi a ukazuji rozdil.
class RackMemoryProcessor : public juce::AudioProcessor,
                            public juce::ChangeBroadcaster,
                            private juce::AsyncUpdater,
                            private juce::Timer
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

    rm::Snapshot working;               // nastaveni vsech pristroju v tomto projektu (vsechny skupiny)
    juce::String loadedId;              // verze aktualni skupiny, ze ktere working vychazi ("" = zadna)
    std::map<juce::String, juce::String> loadedByGroup;   // skupina -> nactena verze
    bool dirty = false;                 // zmeneno od posledniho ulozeni / nacteni
    juce::String songName;
    bool showValues = false;
    bool highContrast = false;          // vyrazne ukazatele knobu (nastaveni zobrazeni)
    bool fitToWindow = false;           // cely rack do okna (jinak pres celou sirku + posouvani)
    bool photoOverlay = false;          // fotka pres kresbu panelu
    float overlayOpacity = 0.6f;
    juce::String instanceId;

    void reloadLibrary();
    const rm::Snapshot* findSnapshot (const juce::String& id) const;
    int nextVersionNumber (const juce::String& song) const;

    // Ulozeni vzdy vytvori novou verzi (v1, v2, ...) aktualni skupiny - nic se neprepisuje.
    // Verze obsahuje jen pristroje aktualni skupiny (Mixing a Mastering maji nezavisle verze).
    juce::Result save();
    juce::Result overwriteLoaded();                 // prepsat nactenou verzi (jen na vyslovne prani)
    juce::Result duplicateSnapshot (const juce::String& id);
    juce::Result renameSnapshot (const juce::String& id, const juce::String& name);
    std::vector<rm::Difference> compareWith (const juce::String& id) const;    // working vs. ulozena verze
    bool load (const juce::String& id);             // jen pristroje dane verze; predchozi stav jde vratit pres undo()
    juce::StringArray instancesOf (const rm::Snapshot&) const;
    juce::String snapshotTitle (const rm::Snapshot&) const;
    bool canUndo() const                            { return hasUndo; }
    void undo();
    juce::Result deleteSnapshot (const juce::String& id);
    void clearAll();                                // pristroje skupiny zpet na "nezadano" (jde vratit pres undo)
    void clearDevice (const juce::String& deviceId);

    // cteni z fotky (stav jen pro tuto relaci, do projektu se neuklada)
    struct DevicePhoto
    {
        juce::Image rectified;          // fotka narovnana na panel (pro kontrolu okem)
        juce::Image original;           // zmensena puvodni fotka (prohlizeni dole)
        juce::String status;            // kratky text do slotu
        std::vector<juce::Point<float>> quad;   // kde byl pristroj na fotce nalezen (px nahledu original)
        bool manual = false;            // zarovnano rucne (4 rohy)
        std::map<juce::String, juce::String> states;   // stav ovladacu z posledniho cteni
        bool busy = false, failed = false;
        int read = 0, readable = 0;
    };
    std::map<juce::String, DevicePhoto> devicePhotos;
    bool showPhotos = false;
    // Hodnoty pristroje nahradi tim, co se precetlo z fotky; nejiste ovladace zustanou nezadane. Jde vratit pres undo().
    // Precteno jiste -> nastavi se (z fotky, ceka na kontrolu). Nejiste -> NIC se nemeni (rucni hodnota zustava),
    // ovladac se zaradi ke kontrole. Jde vratit pres undo().
    void applyRecognition (const juce::String& deviceId, const std::map<juce::String, rm::ControlValue>& values);
    void applyRecognition (const std::map<juce::String, std::map<juce::String, rm::ControlValue>>& perDevice);   // vic pristroju, jeden krok zpet
    std::map<juce::String, juce::StringArray> needsReview;      // kus -> ovladace, ktere z fotky precist nesly (jen tato relace)
    int confirmRecognized (const juce::String& deviceId);       // modre (z fotky) -> potvrzene; vraci pocet
    int numUnconfirmed (const juce::String& deviceId) const;
    void unlinkPhoto (const juce::String& deviceId);            // odebrat fotku pristroje - nastaveni zustava

    // presety pristroju (nezavisle na skladbach)
    juce::Result savePreset (const juce::String& deviceId, const juce::String& name);
    void applyPreset (const juce::String& deviceId, const rm::DevicePreset&);

    // Studio uzivatele (dlouhodobe, spolecne pro vsechny projekty): skupiny s racky + patchbaye, ~/Music/Rack Memory/rack.json
    rm::Studio studio;
    juce::String currentGroup;                                   // zobrazena skupina (uklada se do projektu)
    rm::RackLayout rack;                                         // rack aktualni skupiny (kopie ze studia)
    void reloadRack();
    juce::Result setRack (const rm::RackLayout&);                // rack aktualni skupiny; ulozi hned (jine instance si ho nactou)
    juce::Result setStudio (const rm::Studio&);
    void setGroup (const juce::String& groupId);
    juce::StringArray rackOrder() const                          { return rack.ids(); }
    juce::StringArray missingInstances() const;                  // kusy s hodnotami v teto verzi, ktere v racku nejsou

    // kabely patchbaye (v projektu, ukladaji se s verzemi)
    void setCables (const std::vector<rm::Cable>&);

    // presun kusu do jine skupiny (hodnoty zustavaji) / nahrazeni jinym modelem (puvodni nastaveni se ulozi jako verze)
    juce::Result moveDeviceToGroup (const juce::String& instanceId, const juce::String& groupId);
    juce::Result replaceDevice (const juce::String& instanceId, const juce::String& newModel, bool carryPositions, juce::String& newInstanceId);

    // Obnova neulozenych zmen: kdyz se projekt zavrel bez ulozeni, plugin zmeny nabidne (nikdy sam neprepise).
    struct RecoveryOffer { bool available = false; juce::int64 time = 0; juce::String song, state; int changes = 0; };
    RecoveryOffer recovery;
    juce::StringArray profileNotes;     // co se v knihovne hardwaru zmenilo od ulozeni projektu
    void acceptRecovery();
    void discardRecovery();

    void markDirty();                               // po kazde zmene working / songName (Logic se dozvi, ze je co ulozit)
    void commitState (bool notifyHost = true);      // prepocita stav ukladany do projektu Logicu
    juce::AudioParameterFloat* stateParam = nullptr;   // skryty parametr: jeho zmenou se Logic dozvi o zmene nastaveni
    int hostNotifications = 0;                      // pro testy

    juce::String buildStateJson() const;
    void applyStateJson (const juce::String&);

private:
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void writeRecovery();
    juce::File recoveryFile() const;
    bool pendingNotify = false, pendingRecovery = false;
    std::atomic<bool> emitted { false };
    juce::String loadedToken, lastEmittedToken;
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
