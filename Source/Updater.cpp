#include "Updater.h"
#include "UpdateConfig.h"
#include "Model.h"
#include <juce_cryptography/juce_cryptography.h>

#define U8(x) juce::String::fromUTF8 (x)
#ifndef JucePlugin_VersionString
 #define JucePlugin_VersionString "0.0.0"
#endif

namespace rm
{
using juce::String;

juce::int64 versionValue (const String& v)
{
    auto parts = juce::StringArray::fromTokens (v.trim().trimCharactersAtStart ("vV").retainCharacters ("0123456789."), ".", "");
    juce::int64 r = 0;
    for (int i = 0; i < 3; ++i) r = r * 1000 + (i < parts.size() ? juce::jlimit (0, 999, parts[i].getIntValue()) : 0);
    return r;
}

ReleaseInfo ReleaseInfo::parse (const String& json)
{
    ReleaseInfo r;
    juce::var v;
    if (juce::JSON::parse (json, v).failed() || v.getDynamicObject() == nullptr) { r.error = U8 ("latest.json není platný JSON"); return r; }
    r.version  = v.getProperty ("version", "").toString().trim();
    r.download = v.getProperty ("download", "").toString().trim();
    r.sha256   = v.getProperty ("sha256", "").toString().trim().toLowerCase();
    r.notes    = v.getProperty ("notes", "").toString();
    r.notesUrl = v.getProperty ("notes_url", "").toString();
    r.channel  = v.getProperty ("channel", "stable").toString();
    r.minimumVersion = v.getProperty ("minimum_version", "").toString();
    r.macosMin = v.getProperty ("macos_min", "").toString();
    r.size     = (juce::int64) v.getProperty ("size", 0);
    if (auto* a = v.getProperty ("architectures", juce::var()).getArray()) for (auto& e : *a) r.architectures.add (e.toString());

    if (versionValue (r.version) <= 0)                              r.error = U8 ("v latest.json chybí verze");
    else if (! r.download.startsWithIgnoreCase ("https://"))        r.error = U8 ("v latest.json chybí https odkaz ke stažení");
    else if (r.sha256.length() != 64 || ! r.sha256.containsOnly ("0123456789abcdef")) r.error = U8 ("v latest.json chybí kontrolní součet (sha256)");
    else if (! r.download.startsWithIgnoreCase ("https://github.com/") && ! r.download.startsWithIgnoreCase ("https://objects.githubusercontent.com/"))
        r.error = U8 ("odkaz ke stažení nevede na GitHub – nic se nestahuje");
    else r.valid = true;
    return r;
}

//==============================================================================
namespace
{
std::unique_ptr<juce::InputStream> open (const juce::URL& url, String& error)
{
    if (url.isLocalFile())
    {
        auto f = url.getLocalFile();
        if (! f.existsAsFile()) { error = "soubor nenalezen"; return {}; }
        return f.createInputStream();
    }
    int status = 0;
    auto in = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                         .withConnectionTimeoutMs (10000).withNumRedirectsToFollow (8)
                                         .withExtraHeaders (String ("User-Agent: RackMemory/") + JucePlugin_VersionString + "\r\nCache-Control: no-cache")
                                         .withStatusCode (&status));
    if (in == nullptr)  { error = status >= 400 ? String ("server odpověděl ") + String (status) : String ("offline"); return {}; }
    if (status == 404)  { error = U8 ("na GitHubu zatím není žádné vydání"); return {}; }
    if (status >= 400)  { error = String ("server odpověděl ") + String (status); return {}; }
    return in;
}

bool defaultFetch (const juce::URL& url, String& body, String& error)
{
    auto in = open (url, error);
    if (in == nullptr) return false;
    juce::MemoryOutputStream out;
    out.writeFromInputStream (*in, 1024 * 1024);
    body = out.toString();
    return true;
}

bool defaultDownload (const juce::URL& url, const juce::File& target, const std::function<bool (float)>& progress, String& error)
{
    auto in = open (url, error);
    if (in == nullptr) return false;
    target.deleteFile();
    juce::FileOutputStream out (target);
    if (out.failedToOpen()) { error = U8 ("nejde zapsat ") + target.getFullPathName(); return false; }
    const auto total = in->getTotalLength();
    juce::HeapBlock<char> buf (64 * 1024);
    juce::int64 done = 0;
    for (;;)
    {
        const int n = in->read (buf.get(), 64 * 1024);
        if (n < 0) { error = U8 ("spojení se přerušilo"); return false; }
        if (n == 0) { if (total > 0 && done < total) { error = U8 ("stahování skončilo předčasně"); return false; } break; }
        if (! out.write (buf.get(), (size_t) n)) { error = U8 ("plný disk"); return false; }
        done += n;
        if (progress && total > 0) progress ((float) ((double) done / (double) total));
        if (in->isExhausted()) break;
    }
    out.flush();
    return ! out.getStatus().failed();
}

bool runTool (const juce::StringArray& args, int timeoutMs = 120000)
{
    juce::ChildProcess p;
    return p.start (args, 0) && p.waitForProcessToFinish (timeoutMs) && p.getExitCode() == 0;
}
}

// verze pluginu, ktera je opravdu nainstalovana na disku (Info.plist bundlu)
juce::String installedComponentVersion (const juce::File& componentsDir)
{
    auto plist = componentsDir.getChildFile ("Rack Memory.component/Contents/Info.plist");
    if (! plist.existsAsFile()) return {};
    auto xml = juce::XmlDocument::parse (plist);
    if (xml == nullptr) return {};
    if (auto* dict = xml->getChildByName ("dict"))
        for (auto* e = dict->getFirstChildElement(); e != nullptr; e = e->getNextElement())
            if (e->hasTagName ("key") && e->getAllSubText() == "CFBundleShortVersionString")
                if (auto* v = e->getNextElement()) return v->getAllSubText().trim();
    return {};
}

juce::Result installComponentFromZip (const juce::File& zip, const juce::File& componentsDir)
{
   #if JUCE_MAC
    if (! zip.existsAsFile()) return juce::Result::fail (U8 ("Balíček nenalezen."));
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("RackMemoryUpdate_" + juce::Uuid().toString());
    tmp.createDirectory();
    if (! runTool ({ "/usr/bin/ditto", "-x", "-k", zip.getFullPathName(), tmp.getFullPathName() }))
        return juce::Result::fail (U8 ("Balíček nejde rozbalit."));
    auto found = tmp.findChildFiles (juce::File::findDirectories, true, "Rack Memory.component");
    if (found.isEmpty() || ! found[0].getChildFile ("Contents/Info.plist").existsAsFile())
        return juce::Result::fail (U8 ("V balíčku není Rack Memory.component."));

    componentsDir.createDirectory();
    auto dst = componentsDir.getChildFile ("Rack Memory.component");
    const auto old = tmp.getChildFile ("old.component");
    if (dst.exists() && ! runTool ({ "/bin/mv", dst.getFullPathName(), old.getFullPathName() }))
        return juce::Result::fail (U8 ("Starou verzi nejde nahradit."));
    if (! runTool ({ "/usr/bin/ditto", found[0].getFullPathName(), dst.getFullPathName() })
        || ! dst.getChildFile ("Contents/Info.plist").existsAsFile())
    {
        dst.deleteRecursively();
        runTool ({ "/bin/mv", old.getFullPathName(), dst.getFullPathName() });     // vratit puvodni
        return juce::Result::fail (U8 ("Novou verzi nejde nainstalovat, původní zůstala."));
    }
    runTool ({ "/usr/bin/xattr", "-dr", "com.apple.quarantine", dst.getFullPathName() });
    runTool ({ "/usr/bin/killall", "-9", "AudioComponentRegistrar" }, 5000);      // macOS si nacte novou verzi
    return juce::Result::ok();
   #else
    juce::ignoreUnused (zip, componentsDir);
    return juce::Result::fail ("jen macOS");
   #endif
}

//==============================================================================
Updater::Updater()
    : repo (builtInRepo()), installed (JucePlugin_VersionString), fetch (defaultFetch), downloader (defaultDownload)
{
    installer = [] (const juce::File& zip)
    {
        return installComponentFromZip (zip, juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Plug-Ins/Components"));
    };
    workFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("RackMemoryDownloads");
    probe = [] { return installedComponentVersion (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Plug-Ins/Components")); };
    libFolder = rm::hardwareLibraryDir();
    systemVersion = juce::SystemStats::getOperatingSystemName().retainCharacters ("0123456789.");
    if (repo.isEmpty()) { status.state = State::notConfigured; libStatus.state = LibState::notConfigured; }
}

Updater::~Updater()   { pool.removeAllJobs (true, 20000); }

String Updater::builtInRepo()                    { return rm::updateconfig::repo; }
String Updater::libraryManifestUrl (const String& r)
{
    return "https://github.com/" + r + "/releases/download/" + rm::updateconfig::libraryTag + "/" + rm::updateconfig::libraryManifest;
}
void Updater::setInstalledProbe (Probe p)        { probe = std::move (p); }
String Updater::getInstalledVersion() const      { const juce::ScopedLock l (lock); return installed; }

bool Updater::fetchWithRetry (const juce::URL& url, String& body, String& error)
{
    // vypadek site / timeout: zkusi se znovu (chyba serveru 404 apod. se neopakuje)
    for (int attempt = 1; attempt <= rm::updateconfig::networkAttempts; ++attempt)
    {
        error.clear();
        if (fetch (url, body, error)) return true;
        if (! error.startsWith ("offline") || attempt == rm::updateconfig::networkAttempts) return false;
        juce::Thread::sleep (retryDelayMs * attempt);
    }
    return false;
}
String Updater::manifestUrl (const String& r)    { return "https://github.com/" + r + "/releases/latest/download/latest.json"; }
String Updater::currentArchitecture()
{
   #if defined (__aarch64__) || defined (__arm64__)
    return "arm64";
   #else
    return "x86_64";
   #endif
}

Updater::Status Updater::getStatus() const       { const juce::ScopedLock l (lock); return status; }

void Updater::setState (State s, const String& msg)
{
    { const juce::ScopedLock l (lock); status.state = s; status.message = msg; }
    ++revision;
}

void Updater::setRepo (const String& r)          { const juce::ScopedLock l (lock); repo = r; status.state = r.isEmpty() ? State::notConfigured : State::idle; ++revision; }
void Updater::setNetwork (Fetch f, Download d)   { fetch = std::move (f); downloader = std::move (d); }
void Updater::setInstaller (Install i)           { installer = std::move (i); }
void Updater::setInstalledVersion (const String& v) { installed = v; }
void Updater::setWorkFolder (const juce::File& f)  { workFolder = f; }

bool Updater::waitUntilIdle (int timeoutMs) const
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (working.load() > 0)
    {
        if (juce::Time::getMillisecondCounter() > end) return false;
        juce::Thread::sleep (10);
    }
    return true;
}

void Updater::checkNow()
{
    { const juce::ScopedLock l (lock); if (repo.isEmpty()) { status.state = State::notConfigured; return; } }
    if (working.load() > 0) return;
    ++working;
    setState (State::checking, U8 ("hledám aktualizaci…"));
    pool.addJob ([this] { runCheck(); --working; ++revision; });
}

void Updater::maybeAutoCheck()
{
    juce::Time last;
    { const juce::ScopedLock l (lock); last = status.lastCheck; }
    if (last.toMilliseconds() == 0 || juce::Time::getCurrentTime() - last > juce::RelativeTime::hours (rm::updateconfig::checkEveryHours)) checkNow();
}

void Updater::runCheck()
{
    String r; { const juce::ScopedLock l (lock); r = repo; status.lastCheck = juce::Time::getCurrentTime(); }
    String body, error;
    if (! fetchWithRetry (juce::URL (manifestUrl (r)), body, error))
    {
        setState (error.startsWith ("offline") ? State::offline : State::failed,
                  error.startsWith ("offline") ? U8 ("bez internetu – zkusím později") : error);
        return;
    }
    auto info = ReleaseInfo::parse (body);
    if (! info.valid) { setState (State::failed, info.error); return; }
    { const juce::ScopedLock l (lock); status.latest = info; }
    if (versionValue (info.version) <= versionValue (installed)) { setState (State::upToDate, U8 ("aktuální verze ") + installed); return; }
    if (! info.architectures.isEmpty() && ! info.architectures.contains (currentArchitecture()))
    { setState (State::upToDate, U8 ("verze ") + info.version + U8 (" není pro tento Mac")); return; }
    if (info.macosMin.isNotEmpty() && systemVersion.isNotEmpty() && versionValue (systemVersion) < versionValue (info.macosMin))
    { setState (State::upToDate, U8 ("verze ") + info.version + U8 (" potřebuje macOS ") + info.macosMin); return; }
    if (info.minimumVersion.isNotEmpty() && versionValue (installed) < versionValue (info.minimumVersion))
    { setState (State::manualOnly, U8 ("verzi ") + info.version + U8 (" je potřeba nainstalovat ručně (zip z GitHubu) – automaticky až od ") + info.minimumVersion); return; }
    setState (State::available, U8 ("nová verze ") + info.version);
}

void Updater::downloadAndInstall()
{
    if (getStatus().state != State::available || working.load() > 0) return;
    ++working;
    setState (State::downloading, U8 ("stahuji…"));
    pool.addJob ([this] { runInstall(); --working; ++revision; });
}

void Updater::runInstall()
{
    const auto info = getStatus().latest;
    workFolder.createDirectory();
    auto zip = workFolder.getChildFile ("Rack Memory " + info.version + ".zip");
    String error;
    const bool ok = downloader (juce::URL (info.download), zip, [this] (float p)
    {
        { const juce::ScopedLock l (lock); status.progress = p; }
        ++revision;
        return true;
    }, error);
    if (! ok) { zip.deleteFile(); setState (State::failed, U8 ("stažení selhalo: ") + error); return; }

    // overeni: velikost a SHA-256 z latest.json (jinak se nic neinstaluje)
    setState (State::verifying, U8 ("ověřuji stažený soubor…"));
    if (info.size > 0 && zip.getSize() != info.size) { zip.deleteFile(); setState (State::failed, U8 ("stažený soubor má špatnou velikost (stahování přerušené?) – nic se neinstalovalo, zkus znovu")); return; }
    juce::FileInputStream in (zip);
    const auto sha = in.openedOk() ? juce::SHA256 (in).toHexString().toLowerCase() : String();
    if (sha != info.sha256) { zip.deleteFile(); setState (State::failed, U8 ("kontrolní součet nesedí – nic se neinstalovalo")); return; }

    setState (State::installing, U8 ("instaluji…"));
    const auto r = installer (zip);
    zip.deleteFile();
    if (r.failed()) { setState (State::failed, r.getErrorMessage() + U8 (" Nainstalovaná verze zůstala.")); return; }
    // hotovo je az to, co je opravdu na disku (ne jen stazeny soubor)
    const auto onDisk = probe ? probe() : String();
    if (onDisk.isEmpty() || versionValue (onDisk) != versionValue (info.version))
    { setState (State::failed, U8 ("instalaci nejde potvrdit (na disku je verze ") + (onDisk.isEmpty() ? String ("?") : onDisk) + U8 (") – nainstaluj zip ručně")); return; }
    { const juce::ScopedLock l (lock); installed = info.version; }
    setState (State::installed, U8 ("verze ") + info.version + U8 (" nainstalovaná – restartuj Logic"));
}
//==============================================================================
// Knihovna hardwaru: samostatne vydani "hardware" na GitHubu (hardware.json + zip s library.json a obrazky).
// Plugin samotny se nemeni; nove profily plati po restartu Logicu. Ulozena nastaveni se nemeni.
Updater::LibStatus Updater::getLibraryStatus() const    { const juce::ScopedLock l (lock); return libStatus; }
void Updater::setLibraryFolder (const juce::File& f)    { libFolder = f; }

void Updater::setLibState (LibState s, const String& msg)
{
    { const juce::ScopedLock l (lock); libStatus.state = s; libStatus.message = msg; }
    ++revision;
}

void Updater::checkLibraryNow()
{
    { const juce::ScopedLock l (lock); if (repo.isEmpty()) { libStatus.state = LibState::notConfigured; ++revision; return; } }
    if (working.load() > 0) return;
    ++working;
    setLibState (LibState::checking, U8 ("hledám knihovnu hardwaru…"));
    pool.addJob ([this] { runLibraryCheck(); --working; ++revision; });
}

void Updater::runLibraryCheck()
{
    String r; { const juce::ScopedLock l (lock); r = repo; }
    String body, error;
    if (! fetchWithRetry (juce::URL (libraryManifestUrl (r)), body, error))
    {
        setLibState (error.startsWith ("offline") ? LibState::offline : LibState::failed,
                     error.startsWith ("offline") ? U8 ("bez internetu") : U8 ("knihovna hardwaru zatím není vydaná (") + error + ")");
        return;
    }
    juce::var v;
    if (juce::JSON::parse (body, v).failed() || v.getDynamicObject() == nullptr) { setLibState (LibState::failed, U8 ("hardware.json není platný")); return; }
    const int ver = (int) v.getProperty ("libraryVersion", 0);
    const auto dl = v.getProperty ("download", "").toString(), sha = v.getProperty ("sha256", "").toString().toLowerCase();
    const auto minPlugin = v.getProperty ("minimum_plugin", "").toString();
    if (ver <= 0 || (int) v.getProperty ("schema", 0) != 1 || ! dl.startsWithIgnoreCase ("https://github.com/") || sha.length() != 64 || ! sha.containsOnly ("0123456789abcdef"))
    { setLibState (LibState::failed, U8 ("hardware.json je neúplný – nic se nestahuje")); return; }
    const int have = libVersion >= 0 ? libVersion : rm::hardwareLibraryVersion();
    {
        const juce::ScopedLock l (lock);
        libStatus.version = have; libStatus.available = ver; libStatus.download = dl; libStatus.sha256 = sha;
        libStatus.size = (juce::int64) v.getProperty ("size", 0);
        libStatus.changes.clear();
        if (auto* ch = v.getProperty ("changes", juce::var()).getArray()) for (auto& e : *ch) libStatus.changes.add (e.toString());
    }
    if (minPlugin.isNotEmpty() && versionValue (installed) < versionValue (minPlugin))
    { setLibState (LibState::upToDate, U8 ("knihovna v") + String (ver) + U8 (" potřebuje plugin ") + minPlugin + U8 (" – nejdřív aktualizuj plugin")); return; }
    if (ver <= have) { setLibState (LibState::upToDate, U8 ("knihovna hardwaru je aktuální (v") + String (juce::jmax (have, 0)) + ")"); return; }
    setLibState (LibState::available, U8 ("nová knihovna hardwaru v") + String (ver));
}

void Updater::installLibrary()
{
    if (getLibraryStatus().state != LibState::available || working.load() > 0) return;
    ++working;
    setLibState (LibState::downloading, U8 ("stahuji knihovnu…"));
    pool.addJob ([this] { runLibraryInstall(); --working; ++revision; });
}

void Updater::runLibraryInstall()
{
    const auto st = getLibraryStatus();
    workFolder.createDirectory();
    auto zip = workFolder.getChildFile ("hardware-" + String (st.available) + ".zip");
    String error;
    if (! downloader (juce::URL (st.download), zip, [] (float) { return true; }, error))
    { zip.deleteFile(); setLibState (LibState::failed, U8 ("stažení selhalo: ") + error); return; }
    if (st.size > 0 && zip.getSize() != st.size) { zip.deleteFile(); setLibState (LibState::failed, U8 ("špatná velikost – nic se neinstalovalo")); return; }
    {
        juce::FileInputStream in (zip);
        if (! in.openedOk() || juce::SHA256 (in).toHexString().toLowerCase() != st.sha256)
        { zip.deleteFile(); setLibState (LibState::failed, U8 ("kontrolní součet knihovny nesedí – nic se neinstalovalo")); return; }
    }
    // rozbalit do docasne slozky, zkontrolovat, pak teprve vymenit (puvodni zustane jako .prev)
    auto tmp = libFolder.getSiblingFile (libFolder.getFileName() + ".new");
    tmp.deleteRecursively();
    tmp.createDirectory();
    {
        juce::ZipFile zf (zip);
        for (int i = 0; i < zf.getNumEntries(); ++i)
        {
            auto* e = zf.getEntry (i);
            const auto name = e->filename.replaceCharacter ('\\', '/');
            const bool ok = name == "library.json" || (name.startsWith ("images/") && ! name.contains ("..") && name.indexOfChar (1 + name.indexOfChar ('/'), '/') < 0);
            if (! ok || name.endsWithChar ('/')) continue;                       // nic jineho se nerozbaluje (zadne spustitelne soubory)
            if (e->uncompressedSize > 20 * 1024 * 1024) continue;
            if (zf.uncompressEntry (i, tmp, false).failed()) { tmp.deleteRecursively(); zip.deleteFile(); setLibState (LibState::failed, U8 ("knihovnu nejde rozbalit")); return; }
        }
    }
    zip.deleteFile();
    juce::var lib;
    if (juce::JSON::parse (tmp.getChildFile ("library.json").loadFileAsString(), lib).failed() || (int) lib.getProperty ("libraryVersion", 0) != st.available)
    { tmp.deleteRecursively(); setLibState (LibState::failed, U8 ("knihovna je poškozená – nic se nezměnilo")); return; }
    juce::StringArray notes;
    rm::mergeProfiles (rm::profiles(), lib, &notes);
    auto prev = libFolder.getSiblingFile (libFolder.getFileName() + ".prev");
    prev.deleteRecursively();
    if (libFolder.exists() && ! libFolder.moveFileTo (prev)) { tmp.deleteRecursively(); setLibState (LibState::failed, U8 ("starou knihovnu nejde odložit – nic se nezměnilo")); return; }
    if (! tmp.moveFileTo (libFolder)) { prev.moveFileTo (libFolder); setLibState (LibState::failed, U8 ("knihovnu nejde nainstalovat – vrácena původní")); return; }
    { const juce::ScopedLock l (lock); libStatus.changes = notes; libVersion = st.available; }
    setLibState (LibState::installed, U8 ("knihovna hardwaru v") + String (st.available) + U8 (" nainstalovaná – platí po restartu Logicu"));
}
} // namespace rm
