// Aktualizace Rack Memory z GitHub Releases.
//
// Plugin cte https://github.com/<ucet>/<repozitar>/releases/latest/download/latest.json, porovna verzi,
// na kliknuti stahne zip s pluginem, overi SHA-256 a velikost, rozbali ho a nahradi
// ~/Library/Audio/Plug-Ins/Components/Rack Memory.component (uzivatelska slozka, neni potreba heslo).
// Puvodni verze se odlozi a pri chybe se vrati. Ulozena nastaveni (~/Music/Rack Memory) se nemeni.
// Nic se nedeje na audio vlakne. Nova verze plati po restartu Logicu.
#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>

namespace rm
{
juce::int64 versionValue (const juce::String& v);      // "0.10.2" -> porovnatelne cislo

struct ReleaseInfo
{
    juce::String version, notes, notesUrl, download, sha256;
    juce::int64 size = 0;
    juce::StringArray architectures;
    bool valid = false;
    juce::String error;
    static ReleaseInfo parse (const juce::String& json);
};

class Updater
{
public:
    enum class State { notConfigured, idle, checking, upToDate, available, downloading, installing, installed, failed, offline };
    struct Status
    {
        State state = State::idle;
        juce::String message;
        ReleaseInfo latest;
        float progress = 0;
        juce::Time lastCheck;
    };

    using Fetch    = std::function<bool (const juce::URL&, juce::String& body, juce::String& error)>;
    using Download = std::function<bool (const juce::URL&, const juce::File& target, const std::function<bool (float)>& progress, juce::String& error)>;
    using Install  = std::function<juce::Result (const juce::File& zip)>;

    Updater();
    ~Updater();

    static juce::String builtInRepo();                 // RM_UPDATE_REPO z CMake (napr. "DusiXxx/MEMORY-RACK")
    static juce::String manifestUrl (const juce::String& repo);
    static juce::String currentArchitecture();

    Status getStatus() const;
    juce::uint32 getRevision() const noexcept   { return revision.load(); }

    void checkNow();                    // na pozadi
    void maybeAutoCheck();              // pri otevreni okna: nejvys jednou za 6 hodin
    void downloadAndInstall();          // na pozadi, jen kdyz je co instalovat
    bool waitUntilIdle (int timeoutMs) const;

    // testy / jiny zdroj
    void setRepo (const juce::String& ownerRepo);
    void setNetwork (Fetch, Download);
    void setInstaller (Install);
    void setInstalledVersion (const juce::String&);
    void setWorkFolder (const juce::File&);

private:
    void runCheck();
    void runInstall();
    void setState (State, const juce::String&);

    mutable juce::CriticalSection lock;
    Status status;
    juce::String repo, installed;
    juce::File workFolder;
    Fetch fetch; Download downloader; Install installer;
    std::atomic<int> working { 0 };
    std::atomic<juce::uint32> revision { 0 };
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (1).withThreadName ("Rack Memory update") };
    JUCE_DECLARE_NON_COPYABLE (Updater)
};

// Nahradi nainstalovany plugin pluginem ze zipu (puvodni se pri chybe vrati). macOS.
juce::Result installComponentFromZip (const juce::File& zip, const juce::File& componentsDir);
} // namespace rm
