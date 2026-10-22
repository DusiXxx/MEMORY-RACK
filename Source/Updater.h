// Aktualizace Rack Memory z GitHub Releases (0.12: bezpecny postup).
//
//  1. Zkontrolovat:  https://github.com/<ucet>/<repo>/releases/latest/download/latest.json -> verze, velikost, poznamky
//  2. Stahnout:      na pozadi, s procenty, rychlosti a moznosti zruseni (do ~/Music/Rack Memory/Updates/staged)
//  3. Overit:        velikost + SHA-256 z latest.json, obsah zipu (Rack Memory.component, verze v Info.plist,
//                    architektura binarky, minimalni macOS)  -> stav "Stazeno - pripraveno k instalaci"
//  4. Instalovat:    az po potvrzeni uzivatelem. Nacteny plugin se NIKDY neprepisuje z bezicího Logicu: plugin jen
//                    spusti samostatny instalator (rackmemory-install.sh), ktery pocka, az Logic plugin uvolni,
//                    zalohuje soucasnou verzi, vymeni ji, overi (auval) a pri chybe vrati zalohu.
//  5. Vysledek:      instalator zapise install-status.json; plugin ho precte pri dalsim otevreni (zadny predstirany uspech).
// Nic z toho nebezi na audio vlakne. Ulozena nastaveni (~/Music/Rack Memory) se nemeni.
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
    juce::String version, notes, notesUrl, download, sha256, channel, minimumVersion, macosMin;
    juce::int64 size = 0;
    juce::StringArray architectures;
    bool valid = false;
    juce::String error;
    static ReleaseInfo parse (const juce::String& json);
};

// Co je v zipu s pluginem (overeno bez rozbaleni)
struct PackageInfo
{
    bool ok = false;
    juce::String error, version, bundleId, minSystem;
    juce::StringArray architectures;
};
PackageInfo inspectPackage (const juce::File& zip);
juce::StringArray machOArchitectures (const void* data, size_t size);     // z hlavicky binarky (fat i thin)

class Updater
{
public:
    enum class State { notConfigured, idle, checking, upToDate, available, downloading, cancelled, verifying,
                       readyToInstall, installScheduled, installed, rolledBack, failed, offline, manualOnly };
    struct Status
    {
        State state = State::idle;
        juce::String message;
        ReleaseInfo latest;
        float progress = 0;
        juce::int64 bytesDone = 0, bytesTotal = 0;
        double bytesPerSecond = 0;
        juce::Time lastCheck;
        juce::String stagedVersion;                 // overeny balicek pripraveny k instalaci
        juce::String resultVersion, resultPrevious; // posledni vysledek instalatoru
    };

    // Stahovani: progress (stazeno, celkem) vraci false = zrusit
    using Fetch    = std::function<bool (const juce::URL&, juce::String& body, juce::String& error)>;
    using Download = std::function<bool (const juce::URL&, const juce::File& target, const std::function<bool (juce::int64, juce::int64)>& progress, juce::String& error)>;
    using Launch   = std::function<bool (const juce::StringArray& args)>;     // spusti instalator (odpojeny proces)

    Updater();
    ~Updater();

    static juce::String builtInRepo();
    static juce::String manifestUrl (const juce::String& repo);
    static juce::String currentArchitecture();

    Status getStatus() const;
    juce::uint32 getRevision() const noexcept   { return revision.load(); }

    void checkNow();
    void maybeAutoCheck();                      // pri otevreni okna: nejvys jednou za 6 hodin
    void startDownload();                       // available -> downloading -> verifying -> readyToInstall
    void cancelDownload();
    bool stageLocalPackage (const juce::File& zip, juce::String& error);    // zip ze Stazenych (bez SHA z GitHubu)
    bool scheduleInstall (juce::String& error); // readyToInstall -> installScheduled (spusti instalator)
    bool scheduleRollback (juce::String& error);// vrati posledni zalohu (taky az po zavreni Logicu)
    void cancelScheduledInstall();
    void refreshInstallResult();                // precte install-status.json
    void acknowledgeResult();                   // vysledek byl ukazan -> priste se neukazuje znovu
    bool hasBackup() const;
    bool waitUntilIdle (int timeoutMs) const;

    juce::File stateFolder() const              { return stateDir; }
    juce::File installerScript() const          { return stateDir.getChildFile ("rackmemory-install.sh"); }
    juce::File statusFile() const               { return stateDir.getChildFile ("install-status.json"); }
    juce::File logFile() const                  { return stateDir.getChildFile ("install.log"); }

    // testy / jiny zdroj
    void setRepo (const juce::String& ownerRepo);
    void setNetwork (Fetch, Download);
    void setInstalledVersion (const juce::String&);
    void setStateFolder (const juce::File&);        // misto ~/Music/Rack Memory/Updates
    void setComponentsFolder (const juce::File&);   // misto ~/Library/Audio/Plug-Ins/Components
    void setLauncher (Launch);
    void setArchitecture (const juce::String& a)    { arch = a; }
    void setRetryDelay (int ms)                     { retryDelayMs = ms; }
    void setSystemVersion (const juce::String& v)   { systemVersion = v; }
    juce::String getInstalledVersion() const;

    //== knihovna hardwaru (oddelene od pluginu) ================================
    enum class LibState { idle, checking, upToDate, available, downloading, installed, failed, offline, notConfigured };
    struct LibStatus { LibState state = LibState::idle; juce::String message; int version = 0, available = 0; juce::StringArray changes;
                       juce::String download, sha256; juce::int64 size = 0; };
    LibStatus getLibraryStatus() const;
    void checkLibraryNow();
    void installLibrary();
    void setLibraryFolder (const juce::File&);
    void setLibraryVersion (int v)               { libVersion = v; }
    static juce::String libraryManifestUrl (const juce::String& repo);
    void setWorkFolder (const juce::File& f)     { setStateFolder (f); }

private:
    void runCheck();
    void runDownload();
    void runLibraryCheck();
    void runLibraryInstall();
    void setLibState (LibState, const juce::String&);
    bool verifyStaged (const juce::File& zip, const ReleaseInfo* expected, juce::String& error, juce::String& versionOut);
    void loadStaged();
    bool launchInstaller (const juce::String& mode, const juce::File& zip, const juce::String& version, juce::String& error);
    LibStatus libStatus;
    juce::File libFolder;
    int libVersion = -1;
    int retryDelayMs = 1500;
    juce::String systemVersion, arch;
    bool fetchWithRetry (const juce::URL&, juce::String& body, juce::String& error);
    void setState (State, const juce::String&);

    mutable juce::CriticalSection lock;
    Status status;
    juce::String repo, installed;
    juce::File stateDir, componentsDir;
    Fetch fetch; Download downloader; Launch launcher;
    std::atomic<bool> cancelRequested { false };
    std::atomic<int> working { 0 };
    std::atomic<juce::uint32> revision { 0 };
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (1).withThreadName ("Rack Memory update") };
    JUCE_DECLARE_NON_COPYABLE (Updater)
};

juce::String installedComponentVersion (const juce::File& componentsDir);
} // namespace rm
