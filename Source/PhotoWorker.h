// Jedno pracovni vlakno pro vsechnu praci s fotkami (nacteni, analyza, nahledy).
//
// - nikdy nebezi vic analyz najednou (stary MacBook se neprehriva, Logic ma pocitac pro sebe),
// - vlakno se spusti az s prvni fotkou a mezi ulohami spi (zadne dotazovani),
// - novejsi fotka pro stejny pristroj (stejny klic) zastaralou ulohu zrusi; klic "*" (cely rack) zrusi vse,
// - pri zavreni okna se rozbehla analyza zrusi a vlakno skonci (shutdown).
// Ulohy bezi s nizsi prioritou a NIKDY na audio vlakne.
#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include "Model.h"
#include "Recognizer.h"

namespace rm
{
class PhotoWorker : private juce::Thread
{
public:
    using Task = std::function<void (const std::atomic<bool>& cancelled)>;

    PhotoWorker();
    ~PhotoWorker() override;

    // Uloha se zaradi na konec fronty. Starsi ulohy se stejnym klicem (a pri klici "*" vsechny) se zrusi:
    // jejich priznak "cancelled" se nastavi, rozbehla analyza skonci u nejblizsi kontroly a cekajici
    // uloha jen uklidi (napr. zrusi "busy" u pristroje).
    void submit (const juce::String& key, Task task);
    void cancelAll();
    void shutdown();                         // zrusi vse a pocka na konec vlakna (zavreni okna)

    bool isBusy() const;                     // neco bezi nebo ceka
    int  queued() const;

    std::atomic<int> started { 0 }, finished { 0 }, cancelledJobs { 0 };
    static std::atomic<int> liveThreads;     // kolik pracovnich vlaken prave existuje (vsechny instance pluginu)

private:
    struct Job { juce::String key; Task task; std::shared_ptr<std::atomic<bool>> flag; };
    void run() override;

    mutable juce::CriticalSection lock;
    std::deque<Job> queue;
    Job current;
    bool running = false, stopping = false;
    juce::WaitableEvent wake;
};

// Diagnostika (jen lokalne): kazda analyza fotky zapise jeden radek do ~/Music/Rack Memory/Reports/photo-analysis.log
// - rozmer fotky, otisk, verze cteni, cas nacteni a analyzy, pro kazdy pristroj: nalezen, body, precteno, z pameti.
// Soubor se drzi pod 512 kB (starsi cast se zahodi). Slouzi k porovnani, kdyz fotka na Macu dopadne jinak nez v testu.
void logPhotoJob (const Library&, const juce::File& source, const juce::Image& photo,
                  const std::vector<std::pair<juce::String, const rmcv::Result*>>& results, double loadMs, double analyzeMs);
}
