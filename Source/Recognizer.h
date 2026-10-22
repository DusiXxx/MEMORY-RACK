// Cteni nastaveni z fotky pristroje. Vse bezi lokalne, bez site, mimo audio vlakno.
//
// Postup: (1) natocit fotku podle EXIF, (2) najit knoby (kotvy) a zarovnat fotku na profil,
// (3) u kazdeho ovladace precist polohu, (4) cemu se neda verit, zustane NEZADANE.
#pragma once
#include <juce_graphics/juce_graphics.h>
#include "Model.h"
#include <atomic>

namespace rmcv
{
struct Transform            // panel (px obrazku profilu) -> fotka (px)
{
    double h[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    void map (double x, double y, double& ox, double& oy) const;
    void jacobian (double x, double y, double j[4]) const;     // [dX/dx dX/dy; dY/dx dY/dy]
};

struct Result
{
    bool aligned = false;
    int anchorsFound = 0, anchorsTotal = 0;
    double printMatch = 0;                 // shoda potisku fotky s profilem (-1..1)
    double parallax = 0;                   // posun rovina knobu vs. panel (v polomerech knobu) = jak moc je fotka ze strany
    double skew = 1.0;                     // > 1.07 = fotka moc ze strany
    int read = 0, readable = 0;            // kolik ovladacu se podarilo precist / kolik se cist da
    int keypoints = 0;                     // kolik vyraznych bodu potisku sedi (miru jistoty, ze je to tento pristroj)
    Transform transform;                   // panel -> fotka v pracovnim rozliseni
    double photoScale = 1.0;               // pracovni rozliseni / puvodni fotka
    std::map<juce::String, rm::ControlValue> values;    // jen ovladace, ktere se cist daji (known = false -> nejiste)
    // stav kazdeho ovladace: recognized | uncertain | notvisible (mimo fotku) | small (moc maly) | unsupported (z fotky se necte)
    std::map<juce::String, juce::String> states;
    int tooSmall = 0;
    juce::Image rectified;                 // fotka narovnana do souradnic panelu
    juce::String message;
    double exposure = 1.0;                 // jas nejsvetlejsiho potisku (99. percentil); < 0.45 = tmava fotka
    bool cancelled = false;                // analyza zrusena (novejsi fotka / zavreni okna) - vysledek se nepouzije
    bool fromCache = false;                // stejna fotka + profil + verze algoritmu uz byla analyzovana
    juce::String algorithm;                // verze algoritmu, ktera vysledek spocitala
};

// Verze cteni z fotky. Meni se pri kazde zmene algoritmu, ktera muze zmenit vysledek (cache, opravy, regresni test).
extern const char* const kAlgorithmVersion;

// Zruseni rozbehle analyzy: PhotoWorker nastavi priznak pro sve vlakno, recognize ho kontroluje mezi kroky.
struct CancelScope
{
    explicit CancelScope (const std::atomic<bool>* flag);
    ~CancelScope();
    const std::atomic<bool>* previous;
};
bool cancelled();

juce::String fingerprint (const juce::File&);       // MD5 obsahu souboru (stejna fotka = stejny otisk, i pod jinym jmenem)

// Analyza s pameti vysledku: klic = otisk fotky + profil (vcetne definic ovladacu) + verze algoritmu.
// Prazdny otisk = bez cache. Pamet je omezena (nejvys 32 vysledku).
Result recognizeCached (const juce::Image& photo, const juce::String& photoFingerprint, const rm::DeviceProfile&, const juce::Image& reference);
struct CacheStats { int hits = 0, misses = 0, size = 0; };
CacheStats cacheStats();
void clearCache();
// Uvolni pracovni pamet analyzy (kopie fotky, vyrazne body fotky). Vola se po kazde uloze; vysledky v cache zustanou.
void releaseWorkingMemory();

// Ktere z nalezenych pristroju na jedne fotce prevzit: kazdy zarovnany, jen kdyz se dva prekryvaji
// (napr. xfilter ME i 1U v racku "najdou" stejny panel), zustane silnejsi shoda.
std::vector<bool> chooseDevices (const std::vector<Result>&, const std::vector<const rm::DeviceProfile*>&);

int exifOrientation (const juce::File&);                              // 1..8, 1 = bez otoceni
juce::Image applyOrientation (const juce::Image&, int orientation);
juce::Image loadPhoto (const juce::File&, juce::String& error);       // vcetne EXIF otoceni a prevodu HEIC (macOS)

// reference = obrazek panelu z profilu (pomaha poznat, ze sedi i potisk, nejen knoby)
// manualAlignment: panel -> puvodni fotka (rucne oznacene rohy); nullptr = najit automaticky
Result recognize (const juce::Image& photo, const rm::DeviceProfile&, const juce::Image& reference, const Transform* manualAlignment = nullptr);
// rohy panelu na fotce v poradi: vlevo nahore, vpravo nahore, vpravo dole, vlevo dole (px puvodni fotky)
Transform fromCorners (const rm::DeviceProfile&, const juce::Point<float> corners[4]);
juce::String debugNotch (const juce::Image&, double rho);
}
