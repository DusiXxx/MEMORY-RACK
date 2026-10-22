#include "Updater.h"
#include "UpdateConfig.h"
#include "Model.h"
#include <juce_cryptography/juce_cryptography.h>
#include "BinaryData.h"
#if JUCE_MAC || JUCE_LINUX
 #include <signal.h>
#endif

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
                                         .withConnectionTimeoutMs (15000).withNumRedirectsToFollow (8)
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

bool defaultDownload (const juce::URL& url, const juce::File& target, const std::function<bool (juce::int64, juce::int64)>& progress, String& error)
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
        if (progress && ! progress (done, total)) { error = U8 ("zrušeno"); return false; }
        if (in->isExhausted()) break;
    }
    out.flush();
    return ! out.getStatus().failed();
}

// spusti instalator jako samostatny proces, ktery prezije zavreni Logicu (nohup, bez vazby na plugin)
bool defaultLaunch (const juce::StringArray& args)
{
   #if JUCE_MAC || JUCE_LINUX
    // args: script mode zip version componentsDir stateDir
    if (args.size() < 6) return false;
    juce::StringArray cmd { "/bin/sh", "-c",
        "nohup /bin/sh \"$0\" \"$1\" \"$2\" \"$3\" \"$4\" \"$5\" </dev/null >>\"$5/install.log\" 2>&1 &" };
    cmd.addArray (args);
    juce::ChildProcess p;
    return p.start (cmd, 0) && p.waitForProcessToFinish (10000) && p.getExitCode() == 0;
   #else
    juce::ignoreUnused (args);
    return false;
   #endif
}

String plistValue (const juce::XmlElement& plist, const String& key)
{
    if (auto* dict = plist.getChildByName ("dict"))
        for (auto* e = dict->getFirstChildElement(); e != nullptr; e = e->getNextElement())
            if (e->hasTagName ("key") && e->getAllSubText() == key)
                if (auto* v = e->getNextElement()) return v->getAllSubText().trim();
    return {};
}

juce::String installerSource()
{
    int size = 0;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        if (String (BinaryData::originalFilenames[i]) == "rackmemory-install.sh")
            if (auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
                return String::fromUTF8 (data, size);
    return {};
}
}

// Architektury v hlavicce Mach-O (fat: 0xcafebabe big endian; thin 64bit: 0xfeedfacf little endian)
juce::StringArray machOArchitectures (const void* data, size_t size)
{
    juce::StringArray r;
    auto* b = static_cast<const juce::uint8*> (data);
    auto be32 = [b] (size_t o) { return (juce::uint32) b[o] << 24 | (juce::uint32) b[o + 1] << 16 | (juce::uint32) b[o + 2] << 8 | b[o + 3]; };
    auto le32 = [b] (size_t o) { return (juce::uint32) b[o + 3] << 24 | (juce::uint32) b[o + 2] << 16 | (juce::uint32) b[o + 1] << 8 | b[o]; };
    auto name = [] (juce::uint32 cpu) { return cpu == 0x01000007u ? String ("x86_64") : cpu == 0x0100000cu ? String ("arm64") : String ("cpu") + String::toHexString ((int) cpu); };
    if (size < 8) return r;
    if (be32 (0) == 0xcafebabeu)
    {
        const auto n = be32 (4);
        for (juce::uint32 i = 0; i < n && 8 + i * 20 + 4 <= size && i < 8; ++i) r.addIfNotAlreadyThere (name (be32 (8 + i * 20)));
    }
    else if (le32 (0) == 0xfeedfacfu) r.add (name (le32 (4)));
    return r;
}

PackageInfo inspectPackage (const juce::File& zipFile)
{
    PackageInfo p;
    juce::ZipFile zip (zipFile);
    if (zip.getNumEntries() == 0) { p.error = U8 ("balíček není zip nebo je prázdný"); return p; }
    int plistIdx = -1, binIdx = -1;
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto n = zip.getEntry (i)->filename.replaceCharacter ('\\', '/');
        if (n.contains ("../") || n.startsWithChar ('/')) { p.error = U8 ("podezřelá cesta v balíčku"); return p; }
        if (n.startsWith ("__MACOSX/")) continue;
        if (n.endsWith ("Rack Memory.component/Contents/Info.plist") && n.upToFirstOccurrenceOf ("Rack Memory.component", false, false).indexOfChar ('/') < 0)
            plistIdx = i;
        if (n.endsWith ("Rack Memory.component/Contents/MacOS/Rack Memory")) binIdx = i;
    }
    if (plistIdx < 0 || binIdx < 0) { p.error = U8 ("v balíčku není Rack Memory.component"); return p; }
    {
        std::unique_ptr<juce::InputStream> s (zip.createStreamForEntry (plistIdx));
        auto xml = s != nullptr ? juce::XmlDocument::parse (s->readEntireStreamAsString()) : nullptr;
        if (xml == nullptr) { p.error = U8 ("Info.plist v balíčku nejde přečíst"); return p; }
        p.version = plistValue (*xml, "CFBundleShortVersionString");
        p.bundleId = plistValue (*xml, "CFBundleIdentifier");
        p.minSystem = plistValue (*xml, "LSMinimumSystemVersion");
    }
    {
        std::unique_ptr<juce::InputStream> s (zip.createStreamForEntry (binIdx));
        if (s == nullptr) { p.error = U8 ("binárku pluginu nejde přečíst"); return p; }
        juce::HeapBlock<char> head (4096, true);
        const int n = s->read (head.get(), 4096);
        p.architectures = machOArchitectures (head.get(), (size_t) juce::jmax (0, n));
    }
    if (p.version.isEmpty())                        p.error = U8 ("v balíčku chybí verze pluginu");
    else if (p.bundleId != "com.dusix.rackmemory")  p.error = U8 ("balíček není Rack Memory (") + p.bundleId + ")";
    else if (p.architectures.isEmpty())             p.error = U8 ("binárka pluginu není pro macOS");
    else p.ok = true;
    return p;
}

// verze pluginu, ktera je opravdu nainstalovana na disku (Info.plist bundlu)
juce::String installedComponentVersion (const juce::File& dir)
{
    auto plist = dir.getChildFile ("Rack Memory.component/Contents/Info.plist");
    if (! plist.existsAsFile()) return {};
    auto xml = juce::XmlDocument::parse (plist);
    return xml != nullptr ? plistValue (*xml, "CFBundleShortVersionString") : String();
}

//==============================================================================
Updater::Updater()
    : repo (builtInRepo()), installed (JucePlugin_VersionString), fetch (defaultFetch), downloader (defaultDownload), launcher (defaultLaunch)
{
    stateDir = rm::Library().getRoot().getChildFile ("Updates");
    componentsDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Plug-Ins/Components");
    libFolder = rm::hardwareLibraryDir();
    systemVersion = juce::SystemStats::getOperatingSystemName().retainCharacters ("0123456789.");
    arch = currentArchitecture();
    if (repo.isEmpty()) { status.state = State::notConfigured; libStatus.state = LibState::notConfigured; }
    loadStaged();
    refreshInstallResult();
}

Updater::~Updater()   { cancelRequested = true; pool.removeAllJobs (true, 20000); }

String Updater::builtInRepo()                    { return rm::updateconfig::repo; }
String Updater::libraryManifestUrl (const String& r)
{
    return "https://github.com/" + r + "/releases/download/" + rm::updateconfig::libraryTag + "/" + rm::updateconfig::libraryManifest;
}
String Updater::getInstalledVersion() const      { const juce::ScopedLock l (lock); return installed; }

bool Updater::fetchWithRetry (const juce::URL& url, String& body, String& error)
{
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
void Updater::setInstalledVersion (const String& v) { const juce::ScopedLock l (lock); installed = v; }
void Updater::setStateFolder (const juce::File& f) { stateDir = f; loadStaged(); refreshInstallResult(); }
void Updater::setComponentsFolder (const juce::File& f) { componentsDir = f; }
void Updater::setLauncher (Launch l)             { launcher = std::move (l); }

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
    const auto st = getStatus().state;
    if (working.load() > 0 || st == State::readyToInstall || st == State::installScheduled) return;   // stazeno / naplanovano: nic nemenit
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
    const auto inst = getInstalledVersion();
    if (versionValue (info.version) <= versionValue (inst)) { setState (State::upToDate, U8 ("aktuální verze ") + inst + U8 (" (na GitHubu ") + info.version + ")"); return; }
    if (! info.architectures.isEmpty() && ! info.architectures.contains (arch))
    { setState (State::upToDate, U8 ("verze ") + info.version + U8 (" není pro tento Mac (") + arch + ")"); return; }
    if (info.macosMin.isNotEmpty() && systemVersion.isNotEmpty() && versionValue (systemVersion) < versionValue (info.macosMin))
    { setState (State::upToDate, U8 ("verze ") + info.version + U8 (" potřebuje macOS ") + info.macosMin); return; }
    if (info.minimumVersion.isNotEmpty() && versionValue (inst) < versionValue (info.minimumVersion))
    { setState (State::manualOnly, U8 ("verzi ") + info.version + U8 (" je potřeba nainstalovat ručně (zip z GitHubu) – automaticky až od ") + info.minimumVersion); return; }
    setState (State::available, U8 ("nová verze ") + info.version
              + (info.size > 0 ? " (" + juce::File::descriptionOfSizeInBytes (info.size) + ")" : String()));
}

void Updater::startDownload()
{
    if (getStatus().state != State::available || working.load() > 0) return;
    ++working;
    cancelRequested = false;
    { const juce::ScopedLock l (lock); status.progress = 0; status.bytesDone = 0; status.bytesTotal = status.latest.size; status.bytesPerSecond = 0; }
    setState (State::downloading, U8 ("stahuji…"));
    pool.addJob ([this] { runDownload(); --working; ++revision; });
}

void Updater::cancelDownload()               { cancelRequested = true; }

void Updater::runDownload()
{
    const auto info = getStatus().latest;
    auto staged = stateDir.getChildFile ("staged");
    staged.createDirectory();
    auto part = staged.getChildFile ("download.part");
    String error;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const bool ok = downloader (juce::URL (info.download), part, [this, t0] (juce::int64 done, juce::int64 total)
    {
        const double sec = juce::jmax (0.001, (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);
        {
            const juce::ScopedLock l (lock);
            status.bytesDone = done;
            if (total > 0) status.bytesTotal = total;
            status.progress = status.bytesTotal > 0 ? (float) ((double) done / (double) status.bytesTotal) : 0.0f;
            status.bytesPerSecond = (double) done / sec;
        }
        ++revision;
        return ! cancelRequested.load();
    }, error);
    if (! ok)
    {
        part.deleteFile();
        if (cancelRequested.load()) { setState (State::available, U8 ("stahování zrušeno – nic se nezměnilo")); return; }
        setState (State::failed, U8 ("stažení selhalo: ") + error + U8 (" – nic se nezměnilo"));
        return;
    }
    setState (State::verifying, U8 ("ověřuji stažený soubor…"));
    String ver;
    if (! verifyStaged (part, &info, error, ver)) { part.deleteFile(); setState (State::failed, error + U8 (" – nic se neinstalovalo")); return; }
    auto zip = staged.getChildFile ("RackMemory-" + info.version + ".zip");
    zip.deleteFile();
    if (! part.moveFileTo (zip)) { setState (State::failed, U8 ("stažený soubor nejde uložit")); return; }
    auto* o = new juce::DynamicObject();
    o->setProperty ("version", info.version); o->setProperty ("file", zip.getFileName());
    o->setProperty ("sha256", info.sha256); o->setProperty ("size", zip.getSize()); o->setProperty ("notes", info.notes);
    o->setProperty ("source", info.download); o->setProperty ("verified", juce::Time::getCurrentTime().toISO8601 (true));
    staged.getChildFile ("staged.json").replaceWithText (juce::JSON::toString (juce::var (o)));
    { const juce::ScopedLock l (lock); status.stagedVersion = info.version; }
    setState (State::readyToInstall, U8 ("Staženo a ověřeno – připraveno k instalaci (") + info.version + ")");
}

bool Updater::verifyStaged (const juce::File& zip, const ReleaseInfo* expected, String& error, String& versionOut)
{
    if (expected != nullptr)
    {
        if (expected->size > 0 && zip.getSize() != expected->size) { error = U8 ("stažený soubor má špatnou velikost (přerušené stahování?)"); return false; }
        juce::FileInputStream in (zip);
        const auto sha = in.openedOk() ? juce::SHA256 (in).toHexString().toLowerCase() : String();
        if (sha != expected->sha256) { error = U8 ("kontrolní součet (SHA-256) nesedí"); return false; }
    }
    const auto pkg = inspectPackage (zip);
    if (! pkg.ok) { error = pkg.error; return false; }
    if (expected != nullptr && versionValue (pkg.version) != versionValue (expected->version))
    { error = U8 ("v balíčku je verze ") + pkg.version + U8 (", GitHub hlásí ") + expected->version; return false; }
    if (! pkg.architectures.contains (arch)) { error = U8 ("plugin v balíčku není pro tento Mac (") + arch + U8 (", v balíčku ") + pkg.architectures.joinIntoString (", ") + ")"; return false; }
    if (pkg.minSystem.isNotEmpty() && systemVersion.isNotEmpty() && versionValue (systemVersion) < versionValue (pkg.minSystem))
    { error = U8 ("plugin potřebuje macOS ") + pkg.minSystem; return false; }
    versionOut = pkg.version;
    return true;
}

bool Updater::stageLocalPackage (const juce::File& zip, String& error)
{
    String ver;
    if (! verifyStaged (zip, nullptr, error, ver)) return false;
    if (versionValue (ver) <= versionValue (getInstalledVersion())) { error = U8 ("v balíčku je verze ") + ver + U8 (", nainstalovaná je ") + getInstalledVersion(); return false; }
    auto staged = stateDir.getChildFile ("staged");
    staged.createDirectory();
    auto dst = staged.getChildFile ("RackMemory-" + ver + ".zip");
    if (dst != zip) { dst.deleteFile(); if (! zip.copyFileTo (dst)) { error = U8 ("balíček nejde zkopírovat"); return false; } }
    juce::FileInputStream in (dst);
    auto* o = new juce::DynamicObject();
    o->setProperty ("version", ver); o->setProperty ("file", dst.getFileName()); o->setProperty ("size", dst.getSize());
    o->setProperty ("sha256", in.openedOk() ? juce::SHA256 (in).toHexString().toLowerCase() : String());
    o->setProperty ("source", zip.getFullPathName()); o->setProperty ("verified", juce::Time::getCurrentTime().toISO8601 (true));
    staged.getChildFile ("staged.json").replaceWithText (juce::JSON::toString (juce::var (o)));
    { const juce::ScopedLock l (lock); status.stagedVersion = ver; }
    setState (State::readyToInstall, U8 ("Balíček ") + ver + U8 (" ověřen – připraveno k instalaci"));
    return true;
}

// Po restartu: stazeny a overeny balicek plati dal (overi se znovu: soucet ze staged.json + obsah)
void Updater::loadStaged()
{
    auto staged = stateDir.getChildFile ("staged");
    juce::var v;
    if (juce::JSON::parse (staged.getChildFile ("staged.json").loadFileAsString(), v).failed() || v.getDynamicObject() == nullptr) return;
    auto zip = staged.getChildFile (v["file"].toString());
    const auto ver = v["version"].toString();
    if (! zip.existsAsFile() || versionValue (ver) <= versionValue (getInstalledVersion())) return;
    juce::FileInputStream in (zip);
    if (! in.openedOk() || juce::SHA256 (in).toHexString().toLowerCase() != v["sha256"].toString()) return;
    {
        const juce::ScopedLock l (lock);
        status.stagedVersion = ver;
        status.latest.version = ver; status.latest.notes = v["notes"].toString(); status.latest.size = zip.getSize();
        status.state = State::readyToInstall;
        status.message = U8 ("Staženo a ověřeno – připraveno k instalaci (") + ver + ")";
    }
    ++revision;
}

bool Updater::launchInstaller (const String& mode, const juce::File& zip, const String& version, String& error)
{
    stateDir.createDirectory();
    const auto src = installerSource();
    auto script = installerScript();
    if (src.isEmpty() || ! script.replaceWithText (src, false, false, "\n")) { error = U8 ("instalátor nejde připravit"); return false; }
    script.setExecutePermission (true);
    statusFile().deleteFile();
    stateDir.getChildFile ("cancel").deleteFile();
    // rucni zaloha: dvojklik, kdyby se instalator nespustil sam
    auto cmd = stateDir.getChildFile ("Instalovat aktualizaci Rack Memory.command");
    cmd.replaceWithText ("#!/bin/sh\n/bin/sh \"" + script.getFullPathName() + "\" " + mode + " \"" + zip.getFullPathName() + "\" \"" + version
                         + "\" \"" + componentsDir.getFullPathName() + "\" \"" + stateDir.getFullPathName() + "\"\n", false, false, "\n");
    cmd.setExecutePermission (true);
    if (! launcher || ! launcher ({ script.getFullPathName(), mode, zip.getFullPathName(), version, componentsDir.getFullPathName(), stateDir.getFullPathName() }))
    { error = U8 ("instalátor se nespustil – spusť ručně: ") + cmd.getFullPathName(); return false; }
    return true;
}

bool Updater::scheduleInstall (String& error)
{
    const auto st = getStatus();
    if (st.state != State::readyToInstall) { error = U8 ("není nic připraveno k instalaci"); return false; }
    auto zip = stateDir.getChildFile ("staged").getChildFile ("RackMemory-" + st.stagedVersion + ".zip");
    if (! zip.existsAsFile()) { error = U8 ("stažený balíček zmizel – stáhni znovu"); setState (State::idle, error); return false; }
    if (! launchInstaller ("install", zip, st.stagedVersion, error)) { setState (State::failed, error); return false; }
    setState (State::installScheduled, U8 ("Instalace ") + st.stagedVersion + U8 (" proběhne, jakmile zavřeš Logic"));
    return true;
}

bool Updater::scheduleRollback (String& error)
{
    if (! hasBackup()) { error = U8 ("není žádná záloha předchozí verze"); return false; }
    if (! launchInstaller ("rollback", juce::File(), "-", error)) { setState (State::failed, error); return false; }
    setState (State::installScheduled, U8 ("Předchozí verze se vrátí, jakmile zavřeš Logic"));
    return true;
}

bool Updater::hasBackup() const
{
    const auto last = stateDir.getChildFile ("Backups").getChildFile ("last").loadFileAsString().trim();
    return last.isNotEmpty() && juce::File (last).isDirectory();
}

void Updater::cancelScheduledInstall()
{
    stateDir.getChildFile ("cancel").replaceWithText ("1");
    const auto st = getStatus();
    setState (st.stagedVersion.isNotEmpty() ? State::readyToInstall : State::idle, U8 ("instalace zrušena"));
}

void Updater::acknowledgeResult()
{
    auto f = statusFile();
    if (f.existsAsFile()) { auto done = stateDir.getChildFile ("install-status.last.json"); done.deleteFile(); f.moveFileTo (done); }
}

// Vysledek samostatneho instalatoru. Stav "waiting" po restartu Macu (instalator uz nebezi) = znovu naplanovat.
void Updater::refreshInstallResult()
{
    juce::var v;
    if (juce::JSON::parse (statusFile().loadFileAsString(), v).failed() || v.getDynamicObject() == nullptr) return;
    const auto state = v["state"].toString(), ver = v["version"].toString(), prev = v["previous"].toString(), msg = v["message"].toString();
    { const juce::ScopedLock l (lock); status.resultVersion = ver; status.resultPrevious = prev; }
    const auto running = String (JucePlugin_VersionString);
    if (state == "installed")
    {
        stateDir.getChildFile ("staged").deleteRecursively();
        { const juce::ScopedLock l (lock); status.stagedVersion.clear(); }
        setState (State::installed, versionValue (ver) == versionValue (running)
                                       ? U8 ("Aktualizováno na ") + ver + U8 (" (ověřeno instalátorem)")
                                       : U8 ("Verze ") + ver + U8 (" je nainstalovaná – restartuj Logic (teď běží ") + running + ")");
    }
    else if (state == "rolledBack")
        setState (State::rolledBack, U8 ("Vrácena verze ") + ver + (versionValue (ver) == versionValue (running) ? String() : U8 (" – restartuj Logic")));
    else if (state == "failed")
        setState (State::failed, U8 ("Instalace se nepovedla: ") + msg + U8 (". Nainstalovaná verze zůstala (") + prev + ")");
    else if (state == "waiting" || state == "installing")
    {
        const int pid = (int) v["pid"];
        bool alive = false;
       #if JUCE_MAC || JUCE_LINUX
        alive = pid > 0 && ::kill (pid, 0) == 0;
       #endif
        if (alive) setState (State::installScheduled, U8 ("Instalace ") + ver + U8 (" čeká na zavření Logicu"));
        else if (getStatus().stagedVersion.isNotEmpty())
            setState (State::readyToInstall, U8 ("Instalace ") + ver + U8 (" neproběhla (restart Macu?) – připraveno znovu"));
    }
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
    stateDir.createDirectory();
    auto zip = stateDir.getChildFile ("hardware-" + String (st.available) + ".zip");
    String error;
    if (! downloader (juce::URL (st.download), zip, [] (juce::int64, juce::int64) { return true; }, error))
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
