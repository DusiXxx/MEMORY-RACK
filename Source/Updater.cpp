#include "Updater.h"
#include <juce_cryptography/juce_cryptography.h>

#define U8(x) juce::String::fromUTF8 (x)

#ifndef RM_UPDATE_REPO
 #define RM_UPDATE_REPO ""
#endif
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
    r.size     = (juce::int64) v.getProperty ("size", 0);
    if (auto* a = v.getProperty ("architectures", juce::var()).getArray()) for (auto& e : *a) r.architectures.add (e.toString());

    if (versionValue (r.version) <= 0)                              r.error = U8 ("v latest.json chybí verze");
    else if (! r.download.startsWithIgnoreCase ("https://"))        r.error = U8 ("v latest.json chybí https odkaz ke stažení");
    else if (r.sha256.length() != 64 || ! r.sha256.containsOnly ("0123456789abcdef")) r.error = U8 ("v latest.json chybí kontrolní součet (sha256)");
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
    if (repo.isEmpty()) status.state = State::notConfigured;
}

Updater::~Updater()   { pool.removeAllJobs (true, 20000); }

String Updater::builtInRepo()                    { return RM_UPDATE_REPO; }
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
    if (last.toMilliseconds() == 0 || juce::Time::getCurrentTime() - last > juce::RelativeTime::hours (6)) checkNow();
}

void Updater::runCheck()
{
    String r; { const juce::ScopedLock l (lock); r = repo; status.lastCheck = juce::Time::getCurrentTime(); }
    String body, error;
    if (! fetch (juce::URL (manifestUrl (r)), body, error))
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
    if (info.size > 0 && zip.getSize() != info.size) { zip.deleteFile(); setState (State::failed, U8 ("stažený soubor má špatnou velikost")); return; }
    juce::FileInputStream in (zip);
    const auto sha = in.openedOk() ? juce::SHA256 (in).toHexString().toLowerCase() : String();
    if (sha != info.sha256) { zip.deleteFile(); setState (State::failed, U8 ("kontrolní součet nesedí – nic se neinstalovalo")); return; }

    setState (State::installing, U8 ("instaluji…"));
    const auto r = installer (zip);
    zip.deleteFile();
    if (r.failed()) { setState (State::failed, r.getErrorMessage()); return; }
    { const juce::ScopedLock l (lock); installed = info.version; }
    setState (State::installed, U8 ("verze ") + info.version + U8 (" nainstalovaná – restartuj Logic"));
}
} // namespace rm
