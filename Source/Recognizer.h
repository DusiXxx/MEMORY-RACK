// Cteni nastaveni z fotky pristroje. Vse bezi lokalne, bez site, mimo audio vlakno.
//
// Postup: (1) natocit fotku podle EXIF, (2) najit knoby (kotvy) a zarovnat fotku na profil,
// (3) u kazdeho ovladace precist polohu, (4) cemu se neda verit, zustane NEZADANE.
#pragma once
#include <juce_graphics/juce_graphics.h>
#include "Model.h"

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
    Transform transform;
    std::map<juce::String, rm::ControlValue> values;    // jen ovladace, ktere se cist daji (known = false -> nejiste)
    juce::Image rectified;                 // fotka narovnana do souradnic panelu
    juce::String message;
};

int exifOrientation (const juce::File&);                              // 1..8, 1 = bez otoceni
juce::Image applyOrientation (const juce::Image&, int orientation);
juce::Image loadPhoto (const juce::File&, juce::String& error);       // vcetne EXIF otoceni a prevodu HEIC (macOS)

// reference = obrazek panelu z profilu (pomaha poznat, ze sedi i potisk, nejen knoby)
Result recognize (const juce::Image& photo, const rm::DeviceProfile&, const juce::Image& reference);
juce::String debugNotch (const juce::Image&, double rho);
}
