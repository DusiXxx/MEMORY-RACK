#include <functional>
#include <deque>
#include <mutex>
#include "Recognizer.h"
#include <juce_cryptography/juce_cryptography.h>
#include <array>
#include <complex>
#include <mutex>
#include <iostream>

namespace rmcv
{
using juce::String;
using rm::ControlType;
typedef std::complex<double> cd;

namespace
{
const double kPi = 3.14159265358979323846;
const double kMinPrintMatch = 0.45;     // shoda jasu, pri ktere se pristroj bere i bez vyraznych bodu
const int kMinInliers = 12;
const double kMinDetail = 0.35;         // shoda jemneho potisku (pismena) pri prijeti bez vyraznych bodu             // tolik sedicich vyraznych bodu = je to tento pristroj
const double kMaxSkew = 1.12;           // nad tim je fotka moc ze strany

//==============================================================================
struct Plane
{
    int w = 0, h = 0;
    std::vector<float> v;

    float at (int x, int y) const
    {
        x = juce::jlimit (0, w - 1, x); y = juce::jlimit (0, h - 1, y);
        return v[(size_t) y * (size_t) w + (size_t) x];
    }
    float bil (double x, double y) const
    {
        const int x0 = (int) std::floor (x), y0 = (int) std::floor (y);
        const float fx = (float) (x - x0), fy = (float) (y - y0);
        return (at (x0, y0) * (1 - fx) + at (x0 + 1, y0) * fx) * (1 - fy)
             + (at (x0, y0 + 1) * (1 - fx) + at (x0 + 1, y0 + 1) * fx) * fy;
    }
};

struct Photo
{
    Plane r, g, b, gray;
    double scale = 1.0;          // pracovni rozliseni / puvodni fotka
};

Photo toPhoto (const juce::Image& source, int maxSide)
{
    auto img = source.convertedToFormat (juce::Image::ARGB);
    const int longSide = juce::jmax (img.getWidth(), img.getHeight());
    if (longSide > maxSide)
    {
        const double s = (double) maxSide / longSide;
        img = img.rescaled (juce::jmax (1, juce::roundToInt (img.getWidth() * s)), juce::jmax (1, juce::roundToInt (img.getHeight() * s)),
                            juce::Graphics::mediumResamplingQuality);
    }
    Photo p;
    p.scale = (double) img.getWidth() / juce::jmax (1, source.getWidth());
    const int w = img.getWidth(), h = img.getHeight();
    for (auto* pl : { &p.r, &p.g, &p.b, &p.gray }) { pl->w = w; pl->h = h; pl->v.resize ((size_t) w * (size_t) h); }
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const auto c = bd.getPixelColour (x, y);
            const size_t i = (size_t) y * (size_t) w + (size_t) x;
            p.r.v[i] = c.getFloatRed(); p.g.v[i] = c.getFloatGreen(); p.b.v[i] = c.getFloatBlue();
            p.gray.v[i] = 0.299f * p.r.v[i] + 0.587f * p.g.v[i] + 0.114f * p.b.v[i];
        }
    return p;
}

Plane shrink (const Plane& src, int factor)
{
    if (factor <= 1) return src;
    Plane d; d.w = juce::jmax (1, src.w / factor); d.h = juce::jmax (1, src.h / factor);
    d.v.resize ((size_t) d.w * (size_t) d.h);
    for (int y = 0; y < d.h; ++y)
        for (int x = 0; x < d.w; ++x)
        {
            float s = 0;
            for (int yy = 0; yy < factor; ++yy)
                for (int xx = 0; xx < factor; ++xx)
                    s += src.at (x * factor + xx, y * factor + yy);
            d.v[(size_t) y * (size_t) d.w + (size_t) x] = s / (float) (factor * factor);
        }
    return d;
}

//==============================================================================
// Kotvy: "disc" = svetly kruh na tmavem podkladu (stribrny knob),
//        "ring" = svetly prstenec (tecky stupnice) kolem tmaveho stredu (cerny knob na cernem panelu)
struct Kernel
{
    std::vector<std::pair<float, float>> a, b;     // odezva = prumer(a) - prumer(b), souradnice v nasobcich polomeru
};

void addCircle (std::vector<std::pair<float, float>>& pts, float radius, int n)
{
    if (radius <= 0.0f) { pts.push_back ({ 0.0f, 0.0f }); return; }
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) (2.0 * kPi * (i + 0.5) / n);
        pts.push_back ({ radius * std::cos (t), radius * std::sin (t) });
    }
}

Kernel makeKernel (const String& type)
{
    Kernel k;
    if (type == "ring")
    {
        addCircle (k.a, 0.90f, 20); addCircle (k.a, 1.00f, 20); addCircle (k.a, 1.10f, 20);
        addCircle (k.b, 0.0f, 1); addCircle (k.b, 0.25f, 6); addCircle (k.b, 0.50f, 10); addCircle (k.b, 0.70f, 14);
    }
    else
    {
        addCircle (k.a, 0.0f, 1); addCircle (k.a, 0.30f, 6); addCircle (k.a, 0.55f, 10); addCircle (k.a, 0.80f, 14);
        addCircle (k.b, 1.18f, 18); addCircle (k.b, 1.38f, 18);
    }
    return k;
}

// Tvarova odezva nezavisla na kontrastu: rozdil prumeru deleny rozptylem vzorku (bile pozadi,
// cerne sipky, vodoznak apod. jinak prebiji skutecne knoby).
float responseNorm (const Plane& img, double x, double y, double rho, const Kernel& k)
{
    float sa = 0, sb = 0, sq = 0;
    for (auto& p : k.a) { const float v = img.bil (x + p.first * rho, y + p.second * rho); sa += v; sq += v * v; }
    for (auto& p : k.b) { const float v = img.bil (x + p.first * rho, y + p.second * rho); sb += v; sq += v * v; }
    const float n = (float) (k.a.size() + k.b.size());
    const float mean = (sa + sb) / n;
    const float sd = std::sqrt (juce::jmax (0.0f, sq / n - mean * mean));
    const float d = sa / (float) k.a.size() - sb / (float) k.b.size();
    if (d < 0.04f) return 0.0f;                   // skoro zadny kontrast = sum
    return d / (sd + 0.02f) * 0.25f;
}

float response (const Plane& img, double x, double y, double rho, const Kernel& k)
{
    float sa = 0, sb = 0;
    for (auto& p : k.a) sa += img.bil (x + p.first * rho, y + p.second * rho);
    for (auto& p : k.b) sb += img.bil (x + p.first * rho, y + p.second * rho);
    return sa / (float) k.a.size() - sb / (float) k.b.size();
}

struct Peak { double x, y; float r; };

std::vector<Peak> findPeaks (const Plane& img, double rho, const Kernel& k)
{
    const int step = juce::jmax (1, (int) (rho / 4.0));
    const int gw = img.w / step, gh = img.h / step;
    std::vector<float> map ((size_t) gw * (size_t) gh, 0.0f);
    float best = 0;
    for (int gy = 0; gy < gh; ++gy)
        for (int gx = 0; gx < gw; ++gx)
        {
            const float r = responseNorm (img, gx * step, gy * step, rho, k);
            map[(size_t) gy * (size_t) gw + (size_t) gx] = r;
            best = juce::jmax (best, r);
        }

    std::vector<Peak> peaks;
    const float thr = juce::jmax (0.15f, best * 0.45f);
    const int nms = juce::jmax (1, (int) std::ceil (1.5 * rho / step));
    for (int gy = 0; gy < gh; ++gy)
        for (int gx = 0; gx < gw; ++gx)
        {
            const float r = map[(size_t) gy * (size_t) gw + (size_t) gx];
            if (r < thr) continue;
            bool isMax = true;
            for (int yy = juce::jmax (0, gy - nms); yy <= juce::jmin (gh - 1, gy + nms) && isMax; ++yy)
                for (int xx = juce::jmax (0, gx - nms); xx <= juce::jmin (gw - 1, gx + nms); ++xx)
                {
                    const float o = map[(size_t) yy * (size_t) gw + (size_t) xx];
                    if (o > r || (o == r && (yy < gy || (yy == gy && xx < gx)))) { isMax = false; break; }
                }
            if (isMax) peaks.push_back ({ (double) gx * step, (double) gy * step, r });
        }
    std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.r > b.r; });
    if (peaks.size() > 90) peaks.resize (90);
    return peaks;
}

//==============================================================================
struct Anchor { const rm::ControlDef* c; cd p; cd q; bool found = false; float resp = 0; double rho = 0; cd off { 0, 0 }; };

struct Similarity { cd a { 1, 0 }, b { 0, 0 }; cd map (cd p) const { return a * p + b; } };

Similarity fitSimilarity (const std::vector<cd>& p, const std::vector<cd>& q)
{
    cd pm = 0, qm = 0;
    for (size_t i = 0; i < p.size(); ++i) { pm += p[i]; qm += q[i]; }
    pm /= (double) p.size(); qm /= (double) q.size();
    cd num = 0; double den = 0;
    for (size_t i = 0; i < p.size(); ++i) { num += std::conj (p[i] - pm) * (q[i] - qm); den += std::norm (p[i] - pm); }
    Similarity s;
    s.a = den > 1e-9 ? num / den : cd (1, 0);
    s.b = qm - s.a * pm;
    return s;
}

bool solveLinear (std::vector<double>& A, std::vector<double>& b, int n)
{
    for (int col = 0; col < n; ++col)
    {
        int piv = col;
        for (int r = col + 1; r < n; ++r) if (std::abs (A[(size_t) (r * n + col)]) > std::abs (A[(size_t) (piv * n + col)])) piv = r;
        if (std::abs (A[(size_t) (piv * n + col)]) < 1e-12) return false;
        if (piv != col)
        {
            for (int c = 0; c < n; ++c) std::swap (A[(size_t) (col * n + c)], A[(size_t) (piv * n + c)]);
            std::swap (b[(size_t) col], b[(size_t) piv]);
        }
        for (int r = 0; r < n; ++r)
        {
            if (r == col) continue;
            const double f = A[(size_t) (r * n + col)] / A[(size_t) (col * n + col)];
            if (f == 0.0) continue;
            for (int c = col; c < n; ++c) A[(size_t) (r * n + c)] -= f * A[(size_t) (col * n + c)];
            b[(size_t) r] -= f * b[(size_t) col];
        }
    }
    for (int i = 0; i < n; ++i) b[(size_t) i] /= A[(size_t) (i * n + i)];
    return true;
}

// Homografie nejmensimi ctverci (h33 = 1), body predem normalizovane kvuli stabilite.
bool fitHomography (const std::vector<cd>& p, const std::vector<cd>& q, Transform& out)
{
    const int n = (int) p.size();
    if (n < 4) return false;
    auto norm = [n] (const std::vector<cd>& v, cd& mean, double& scale)
    {
        mean = 0; for (auto& z : v) mean += z; mean /= (double) n;
        double d = 0; for (auto& z : v) d += std::abs (z - mean);
        scale = d > 1e-9 ? (double) n / d : 1.0;
    };
    cd pm, qm; double ps, qs;
    norm (p, pm, ps); norm (q, qm, qs);

    std::vector<double> AtA (64, 0.0), Atb (8, 0.0);
    for (int i = 0; i < n; ++i)
    {
        const double x = (p[(size_t) i].real() - pm.real()) * ps, y = (p[(size_t) i].imag() - pm.imag()) * ps;
        const double X = (q[(size_t) i].real() - qm.real()) * qs, Y = (q[(size_t) i].imag() - qm.imag()) * qs;
        const double r1[8] = { x, y, 1, 0, 0, 0, -x * X, -y * X };
        const double r2[8] = { 0, 0, 0, x, y, 1, -x * Y, -y * Y };
        for (int a = 0; a < 8; ++a)
        {
            for (int b = 0; b < 8; ++b) AtA[(size_t) (a * 8 + b)] += r1[a] * r1[b] + r2[a] * r2[b];
            Atb[(size_t) a] += r1[a] * X + r2[a] * Y;
        }
    }
    if (! solveLinear (AtA, Atb, 8)) return false;
    const double H[9] = { Atb[0], Atb[1], Atb[2], Atb[3], Atb[4], Atb[5], Atb[6], Atb[7], 1.0 };

    // denormalizace: q = Tq^-1 * H * Tp * p
    const double Tp[9]  = { ps, 0, -pm.real() * ps, 0, ps, -pm.imag() * ps, 0, 0, 1 };
    const double Tqi[9] = { 1.0 / qs, 0, qm.real(), 0, 1.0 / qs, qm.imag(), 0, 0, 1 };
    double M[9], R[9];
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) { M[r * 3 + c] = 0; for (int k = 0; k < 3; ++k) M[r * 3 + c] += H[r * 3 + k] * Tp[k * 3 + c]; }
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) { R[r * 3 + c] = 0; for (int k = 0; k < 3; ++k) R[r * 3 + c] += Tqi[r * 3 + k] * M[k * 3 + c]; }
    if (std::abs (R[8]) < 1e-12) return false;
    for (int i = 0; i < 9; ++i) out.h[i] = R[i] / R[8];
    return true;
}

Transform fromSimilarity (const Similarity& s)
{
    Transform t;
    t.h[0] = s.a.real(); t.h[1] = -s.a.imag(); t.h[2] = s.b.real();
    t.h[3] = s.a.imag(); t.h[4] =  s.a.real(); t.h[5] = s.b.imag();
    t.h[6] = 0; t.h[7] = 0; t.h[8] = 1;
    return t;
}

double wrapDeg (double a)
{
    while (a > 180.0) a -= 360.0;
    while (a <= -180.0) a += 360.0;
    return a;
}

//==============================================================================
// Cteni jednotlivych ovladacu
//==============================================================================
struct Frame          // lokalni souradnice ovladace: stred na fotce + matice panel -> fotka
{
    double cx = 0, cy = 0, j[4] = { 1, 0, 0, 1 };
    void polar (double radius, double angleDeg, double& x, double& y) const      // uhel: 0 = nahoru, po smeru hodin
    {
        const double a = angleDeg * kPi / 180.0;
        const double px = radius * std::sin (a), py = -radius * std::cos (a);
        x = cx + j[0] * px + j[1] * py;
        y = cy + j[2] * px + j[3] * py;
    }
    double scale() const   { return std::sqrt (std::abs (j[0] * j[3] - j[1] * j[2])); }
};

rm::ControlValue fromAngle (const rm::ControlDef& c, double angleDeg, double confidence)
{
    rm::ControlValue v;
    v.source = "recognized";
    v.confidence = juce::jlimit (0.0, 1.0, confidence);
    const double span = c.angleMax - c.angleMin;
    double rel = (wrapDeg (angleDeg - c.angleMin)) / span;
    if (rel < 0) rel = wrapDeg (angleDeg - c.angleMin + 360.0) / span;
    // ukazatel mimo stupnici o vic nez par stupnu = spatne cteni
    const double slack = 10.0 / span;
    const double a = wrapDeg (angleDeg);
    double pos01;
    const double slackDeg = 16.0;
    if (a >= c.angleMin - slackDeg && a <= c.angleMax + slackDeg) pos01 = juce::jlimit (0.0, 1.0, (a - c.angleMin) / span);
    else { juce::ignoreUnused (rel, slack); v.known = false; return v; }

    v.known = v.confidence >= 0.30;
    if (c.type == ControlType::knob) v.pos = pos01;
    else                             v.pos = (double) juce::jlimit (0, c.numPositions() - 1, (int) std::lround (pos01 * (c.numPositions() - 1)));
    return v;
}

// elysia: celo knobu (svetly kruh) ma na okraji vyfrezovany zarez. Nejdriv se presne najde kruh cela
// (stred knobu na fotce neni stred cela, kdyz se fotka neporidi uplne kolmo), potom zarez v jeho okrajovem pasu.
struct Cap { double cx = 0, cy = 0, r = 0; bool ok = false; };

// Celo = nejvetsi kruh kolem stredu knobu, jehoz okrajovy pas je cely svetly a stejnomerny
// (az na uzky zarez). Hleda se hrubou silou pres stred a polomer - je to odolnejsi nez hledani hran,
// protoze osvetlena strana sukne byva skoro stejne svetla jako celo.
Cap fitCap (const Plane& g, double cx0, double cy0, double R0)
{
    Cap cap; cap.cx = cx0; cap.cy = cy0; cap.r = 0.74 * R0;
    if (R0 < 8.0) return cap;

    const int NA = 60;
    double sinT[NA], cosT[NA];
    for (int t = 0; t < NA; ++t) { sinT[t] = std::sin (2 * kPi * t / NA); cosT[t] = std::cos (2 * kPi * t / NA); }

    // pas [0.80, 0.97] * r: vraci median a "rozptyl" bez nejhorsich 20 % (zarez, odlesk)
    auto band = [&] (double cx, double cy, double r, float& med, float& spread)
    {
        float v[NA * 2];
        for (int t = 0; t < NA; ++t)
        {
            v[t * 2]     = g.bil (cx + 0.84 * r * sinT[t], cy - 0.84 * r * cosT[t]);
            v[t * 2 + 1] = g.bil (cx + 0.94 * r * sinT[t], cy - 0.94 * r * cosT[t]);
        }
        std::sort (v, v + NA * 2);
        med = v[NA];
        float dev[NA * 2];
        for (int i = 0; i < NA * 2; ++i) dev[i] = std::abs (v[i] - med);
        std::sort (dev, dev + NA * 2);
        float s = 0; const int keep = (NA * 2 * 80) / 100;
        for (int i = 0; i < keep; ++i) s += dev[i];
        spread = s / (float) keep;
    };

    // jas cela kolem stredu
    std::vector<float> core;
    for (int t = 0; t < NA; t += 2) for (double rr : { 0.10, 0.22, 0.34 }) core.push_back (g.bil (cx0 + rr * R0 * sinT[t], cy0 - rr * R0 * cosT[t]));
    std::sort (core.begin(), core.end());
    const float B = core[core.size() / 2];
    if (B < 0.25f) return cap;

    double bestScore = -1e9;
    const double cstep = juce::jmax (0.5, R0 / 24.0), rstep = juce::jmax (0.5, R0 / 40.0);
    for (double dy = -0.30 * R0; dy <= 0.30 * R0; dy += cstep)
        for (double dx = -0.30 * R0; dx <= 0.30 * R0; dx += cstep)
        {
            // nejvetsi polomer, pri kterem je pas jeste svetly a stejnomerny
            double rOk = 0; float spreadOk = 0;
            for (double r = 0.50 * R0; r <= 1.02 * R0; r += rstep)
            {
                float med, spread; band (cx0 + dx, cy0 + dy, r, med, spread);
                if (med > B - 0.10f && spread < 0.030f) { rOk = r; spreadOk = spread; }
                else if (rOk > 0) break;
            }
            if (rOk <= 0) continue;
            const double score = rOk / R0 - 2.0 * spreadOk;
            if (score > bestScore) { bestScore = score; cap.cx = cx0 + dx; cap.cy = cy0 + dy; cap.r = rOk; cap.ok = true; }
        }
    if (! cap.ok) return cap;

    // jemne doladeni stredu a polomeru (presnost uhlu zarezu stoji na presnem stredu)
    const double bx = cap.cx, by = cap.cy, br = cap.r;
    bestScore = -1e9;
    const double fstep = juce::jmax (0.25, cstep / 4.0);
    for (double dy = -cstep; dy <= cstep; dy += fstep)
        for (double dx = -cstep; dx <= cstep; dx += fstep)
        {
            double rOk = 0; float spreadOk = 0;
            for (double r = br - 2 * rstep; r <= br + 3 * rstep; r += juce::jmax (0.25, rstep / 4.0))
            {
                float med, spread; band (bx + dx, by + dy, r, med, spread);
                if (med > B - 0.10f && spread < 0.030f) { rOk = r; spreadOk = spread; }
                else if (rOk > 0) break;
            }
            if (rOk <= 0) continue;
            const double score = rOk / R0 - 2.0 * spreadOk;
            if (score > bestScore) { bestScore = score; cap.cx = bx + dx; cap.cy = by + dy; cap.r = rOk; }
        }
    return cap;
}

// Uhel zareza ve stupnich v souradnicich FOTKY (0 = nahoru, po smeru hodin) + duveryhodnost.
bool findNotch (const Plane& g, const Cap& cap, double& angleOut, double& confOut)
{
    const int N = 360, bands = 3;
    const double f0 = 0.80, f1 = 0.965;
    std::vector<std::vector<float>> P ((size_t) bands, std::vector<float> ((size_t) N, 0.0f));
    for (int b = 0; b < bands; ++b)
    {
        const double ra = f0 + (f1 - f0) * b / bands, rb = f0 + (f1 - f0) * (b + 1) / bands;
        for (int t = 0; t < N; ++t)
        {
            const double a = t * kPi / 180.0;
            float s = 0;
            for (int k = 0; k < 4; ++k)
            {
                const double r = cap.r * (ra + (rb - ra) * (k + 0.5) / 4.0);
                s += g.bil (cap.cx + r * std::sin (a), cap.cy - r * std::cos (a));
            }
            P[(size_t) b][(size_t) t] = s / 4.0f;
        }
    }
    auto get = [N] (const std::vector<float>& v, int i) { return v[(size_t) (((i % N) + N) % N)]; };

    // Zarez = uzky utvar s hranami po obou stranach: mirou je mistni "krivost" jasu po obvodu,
    // ktera musi byt videt ve vsech radialnich pasech (odlesky a stiny jsou siroke a hladke).
    std::vector<float> F ((size_t) N, 0.0f);
    for (int t = 0; t < N; ++t)
    {
        float worst = 1e9f;
        for (int b = 0; b < bands; ++b)
        {
            float lo = 1e9f, hi = -1e9f;
            for (int d = -6; d <= 6; ++d) { const float v = get (P[(size_t) b], t + d); lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); }
            // odecist plynuly prechod (rozdil mezi okraji okna)
            const float trend = std::abs (get (P[(size_t) b], t + 8) - get (P[(size_t) b], t - 8));
            worst = juce::jmin (worst, juce::jmax (0.0f, (hi - lo) - trend));
        }
        F[(size_t) t] = worst;
    }
    std::vector<float> Fs ((size_t) N, 0.0f);
    for (int t = 0; t < N; ++t) { float s = 0; for (int d = -3; d <= 3; ++d) s += get (F, t + d); Fs[(size_t) t] = s / 7.0f; }

    int best = 0;
    for (int t = 1; t < N; ++t) if (Fs[(size_t) t] > Fs[(size_t) best]) best = t;
    float second = 0;
    for (int t = 0; t < N; ++t)
    {
        const int d = std::abs (((t - best + 540) % 360) - 180);
        if (d > 24) second = juce::jmax (second, Fs[(size_t) t]);
    }
    const float top = Fs[(size_t) best];
    double num = 0, den = 0;
    for (int d = -8; d <= 8; ++d) { const double w = juce::jmax (0.0f, get (Fs, best + d) - second); num += w * d; den += w; }
    angleOut = best + (den > 1e-9 ? num / den : 0.0);

    double conf = top > 1e-5f ? (double) ((top - second) / top) : 0.0;
    if (top < 0.03f) conf *= (double) top / 0.03;         // skoro zadny kontrast = nevime
    confOut = juce::jlimit (0.0, 1.0, conf * 1.6);
    return true;
}

rm::ControlValue readNotch (const Photo& ph, const rm::ControlDef& c, const Frame& f)
{
    rm::ControlValue v; v.source = "recognized"; v.confidence = 0;
    const double R0 = c.readR * f.scale();
    const Cap cap = fitCap (ph.gray, f.cx, f.cy, R0);
    if (! cap.ok || cap.r < 6.0) return v;

    double angle = 0, conf = 0;
    findNotch (ph.gray, cap, angle, conf);

    // smer "nahoru" panelu na fotce
    const double upx = -f.j[1], upy = -f.j[3];
    const double upAngle = std::atan2 (upx, -upy) * 180.0 / kPi;
    auto out = fromAngle (c, angle - upAngle, conf);
    // u zarezu radsi prisneji: spatne precteny knob je horsi nez neprecteny; maly knob na fotce = jeste prisneji
    const double minConf = juce::jlimit (0.6, 0.8, 0.6 + (32.0 - cap.r) * 0.02);
    if (out.confidence < minConf) out.known = false;
    return out;
}

// SPL: kratka bila ryska u okraje cerneho knobu. Smer = hlavni osa svetlych bodu (nezavisi na presnem
// stredu - na siroke fotce je cepicka knobu vuci stupnici posunuta paralaxou), strana = kde lezi jejich teziste.
rm::ControlValue readLine (const Photo& ph, const rm::ControlDef& c, const Frame& f)
{
    rm::ControlValue none; none.source = "recognized"; none.confidence = 0;
    struct S { double x, y, v; };
    const double R = c.readR;
    const double step = juce::jmax (0.4, 0.7 / juce::jmax (0.05, f.scale()));
    double ox = 0, oy = 0;                 // odhad stredu cepicky vuci stredu stupnice (v jednotkach panelu)
    double mx = 0, my = 0, sxx = 0, syy = 0, sxy = 0, ax = 0, ay = -1;

    for (int pass = 0; pass < 3; ++pass)
    {
        std::vector<S> pts;
        double mean = 0, sq = 0; int n = 0;
        for (double y = -R; y <= R; y += step)
            for (double x = -R; x <= R; x += step)
            {
                if (x * x + y * y > R * R) continue;
                const double X = x + ox, Y = y + oy;
                const double v = ph.gray.bil (f.cx + f.j[0] * X + f.j[1] * Y, f.cy + f.j[2] * X + f.j[3] * Y);
                pts.push_back ({ x, y, v }); mean += v; sq += v * v; ++n;
            }
        if (n < 80) return none;
        mean /= n;
        const double sd = std::sqrt (juce::jmax (1e-9, sq / n - mean * mean));
        const double thr = mean + 2.5 * sd;

        double sw = 0, peak = 0; mx = my = 0;
        for (auto& p : pts) if (p.v > thr) { const double w = p.v - thr; sw += w; mx += w * p.x; my += w * p.y; peak = juce::jmax (peak, p.v); }
        if (sw < 1e-6 || peak - mean < 0.12) return none;                 // zadna ryska neni videt
        mx /= sw; my /= sw;
        sxx = syy = sxy = 0;
        for (auto& p : pts) if (p.v > thr) { const double w = p.v - thr; sxx += w * (p.x - mx) * (p.x - mx); syy += w * (p.y - my) * (p.y - my); sxy += w * (p.x - mx) * (p.y - my); }
        const double theta = 0.5 * std::atan2 (2.0 * sxy, sxx - syy);
        ax = std::cos (theta); ay = std::sin (theta);
        if (ax * mx + ay * my < 0) { ax = -ax; ay = -ay; }                 // ryska je u okraje: miri od stredu k tezisti

        // ryska zabira vnejsi cast polomeru (stred cca v 0.72 R) -> odtud odhad skutecneho stredu cepicky
        const double nx = ox + mx - 0.72 * R * ax, ny = oy + my - 0.72 * R * ay;
        const double lim = 0.30 * R, len = std::hypot (nx, ny);
        ox = len > lim ? nx * lim / len : nx; oy = len > lim ? ny * lim / len : ny;
    }
    const double tr = sxx + syy, det = sxx * syy - sxy * sxy;
    const double l1 = tr / 2 + std::sqrt (juce::jmax (0.0, tr * tr / 4 - det)), l2 = tr / 2 - std::sqrt (juce::jmax (0.0, tr * tr / 4 - det));
    const double elong = l1 > 1e-9 ? 1.0 - std::sqrt (juce::jmax (0.0, l2 / l1)) : 0.0;    // 1 = tenka cara, 0 = flek
    const double off = std::sqrt (mx * mx + my * my) / R;                  // ryska musi byt mimo stred
    const double angle = std::atan2 (ax, -ay) * 180.0 / kPi;               // uz v souradnicich panelu (vzorkovano pres J)
    const double conf = juce::jlimit (0.0, 1.0, (elong - 0.45) / 0.35) * juce::jlimit (0.0, 1.0, off / 0.3);
    return fromAngle (c, angle, conf);
}


// SPL a podobne: bila ryska na cernem knobu. Knoby vycnivaji z panelu -> na fotce shora / zboku je cepicka
// posunuta proti potisku (paralaxa). Smer rysky (jeji osa) na posunu nezavisi; ktery konec je "venku",
// se pozna podle skutecneho stredu cepicky. Ten se spocita spolecne pro cely pristroj: osy rysek vsech knobu
// prochazeji stredy svych cepicek, ktere jsou posunute o stejny vektor.
struct LineSeg { bool ok = false; double mx = 0, my = 0, ax = 1, ay = 0, lo = 0, hi = 0, elong = 0, R = 1, score = 0; };

std::vector<LineSeg> findLineSegs (const Photo& ph, const rm::ControlDef& c, const Frame& f)
{
    std::vector<LineSeg> cands;
    LineSeg out;
    const double R = c.readR > 0 ? c.readR : c.r;
    out.R = R;
    const double W = 1.45 * R;
    const double step = juce::jmax (0.4, 0.6 / juce::jmax (0.05, f.scale()));
    const int n = (int) std::floor (2 * W / step) + 1;
    if (n < 8 || n > 600) return cands;
    std::vector<float> v ((size_t) (n * n), -1.0f);
    double mean = 0, sq = 0; int cnt = 0;
    for (int iy = 0; iy < n; ++iy)
        for (int ix = 0; ix < n; ++ix)
        {
            const double x = -W + ix * step, y = -W + iy * step;
            if (x * x + y * y > W * W) continue;
            const float g = ph.gray.bil (f.cx + f.j[0] * x + f.j[1] * y, f.cy + f.j[2] * x + f.j[3] * y);
            v[(size_t) (iy * n + ix)] = g; mean += g; sq += g * g; ++cnt;
        }
    if (cnt < 80) return cands;
    mean /= cnt;
    const double sd = std::sqrt (juce::jmax (1e-9, sq / cnt - mean * mean));
    const double thr = juce::jmax (mean + 2.2 * sd, mean + 0.10);
    std::vector<int> comp ((size_t) (n * n), -1);
    double bestScore = 0;
    for (int start = 0; start < n * n; ++start)
    {
        if (v[(size_t) start] < thr || comp[(size_t) start] >= 0) continue;
        std::vector<int> q { start }; comp[(size_t) start] = start;
        for (size_t k = 0; k < q.size(); ++k)
        {
            const int ix = q[k] % n, iy = q[k] / n;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
            {
                const int jx = ix + dx, jy = iy + dy;
                if (jx < 0 || jy < 0 || jx >= n || jy >= n) continue;
                const int jj = jy * n + jx;
                if (comp[(size_t) jj] < 0 && v[(size_t) jj] >= thr) { comp[(size_t) jj] = start; q.push_back (jj); }
            }
        }
        if (q.size() < 4) continue;
        double sw = 0, mx = 0, my = 0;
        for (int id : q) { const double w = v[(size_t) id] - thr + 0.02; sw += w; mx += w * (-W + (id % n) * step); my += w * (-W + (id / n) * step); }
        mx /= sw; my /= sw;
        double sxx = 0, syy = 0, sxy = 0;
        for (int id : q)
        {
            const double w = v[(size_t) id] - thr + 0.02, x = -W + (id % n) * step - mx, y = -W + (id / n) * step - my;
            sxx += w * x * x; syy += w * y * y; sxy += w * x * y;
        }
        const double th = 0.5 * std::atan2 (2.0 * sxy, sxx - syy);
        const double ax = std::cos (th), ay = std::sin (th);
        double lo = 1e9, hi = -1e9;
        for (int id : q) { const double t = (-W + (id % n) * step - mx) * ax + (-W + (id / n) * step - my) * ay; lo = juce::jmin (lo, t); hi = juce::jmax (hi, t); }
        const double tr = sxx + syy, det = sxx * syy - sxy * sxy;
        const double l1 = tr / 2 + std::sqrt (juce::jmax (0.0, tr * tr / 4 - det)), l2 = tr / 2 - std::sqrt (juce::jmax (0.0, tr * tr / 4 - det));
        const double elong = l1 > 1e-9 ? 1.0 - std::sqrt (juce::jmax (0.0, l2 / l1)) : 0.0;
        const double len = hi - lo;
        if (len < 0.28 * R || len > 1.3 * R || elong < 0.55) continue;
        const double score = len * elong;
        cands.push_back ({ true, mx, my, ax, ay, lo, hi, elong, R, score });
    }
    juce::ignoreUnused (bestScore, out);
    std::sort (cands.begin(), cands.end(), [] (const LineSeg& a, const LineSeg& b) { return a.score > b.score; });
    if (cands.size() > 6) cands.resize (6);
    return cands;
}

double axisResidual (const LineSeg& s, double sx, double sy)
{
    return std::abs (-s.ay * (sx - s.mx / s.R) + s.ax * (sy - s.my / s.R));
}

// spolecny posun cepicek (v jednotkach polomeru knobu) z os rysek: n_i . (S - M_i) = 0
bool solveParallax (const std::vector<LineSeg>& segs, double& sx, double& sy)
{
    std::vector<const LineSeg*> ok;
    for (auto& s : segs) if (s.ok) ok.push_back (&s);
    if (ok.size() < 2) return false;
    auto fit = [] (const std::vector<const LineSeg*>& use, double& x, double& y)
    {
        double a = 0, b = 0, c = 0, d = 0, e = 0;
        for (auto* s : use)
        {
            const double nx = -s->ay, ny = s->ax, k = nx * s->mx / s->R + ny * s->my / s->R;
            a += nx * nx; b += nx * ny; c += ny * ny; d += nx * k; e += ny * k;
        }
        const double det = a * c - b * b;
        if (std::abs (det) < 1e-3 * (a + c) * (a + c)) return false;
        x = (c * d - b * e) / det; y = (a * e - b * d) / det;
        return true;
    };
    auto resid = [] (const LineSeg& s, double x, double y) { return std::abs (-s.ay * (x - s.mx / s.R) + s.ax * (y - s.my / s.R)); };
    // robustne: vsechny dvojice, vyhrava ta, se kterou souhlasi nejvic rysek
    int bestIn = 0; double bx = 0, by = 0;
    for (size_t i = 0; i < ok.size(); ++i)
        for (size_t j = i + 1; j < ok.size(); ++j)
        {
            double x, y;
            if (! fit ({ ok[i], ok[j] }, x, y) || std::hypot (x, y) > 0.9) continue;
            int in = 0; for (auto* s : ok) if (resid (*s, x, y) < 0.18) ++in;
            if (in > bestIn) { bestIn = in; bx = x; by = y; }
        }
    if (bestIn < 2) return false;
    std::vector<const LineSeg*> inl;
    for (auto* s : ok) if (resid (*s, bx, by) < 0.18) inl.push_back (s);
    if (inl.size() >= 2 && fit (inl, sx, sy) && std::hypot (sx, sy) <= 0.9) return true;
    sx = bx; sy = by;
    return true;
}

rm::ControlValue lineValue (const rm::ControlDef& c, const LineSeg& s, double sx, double sy)
{
    rm::ControlValue none; none.source = "recognized"; none.confidence = 0;
    if (! s.ok) return none;
    const double R = s.R, cx = sx * R, cy = sy * R;
    // konce rysky
    const double e1x = s.mx + s.ax * s.lo, e1y = s.my + s.ay * s.lo, e2x = s.mx + s.ax * s.hi, e2y = s.my + s.ay * s.hi;
    const double d1 = std::hypot (e1x - cx, e1y - cy), d2 = std::hypot (e2x - cx, e2y - cy);
    const double fx = d2 >= d1 ? e2x : e1x, fy = d2 >= d1 ? e2y : e1y;     // vnejsi konec
    const double nearD = juce::jmin (d1, d2) / R, farD = juce::jmax (d1, d2) / R;
    const double axisRes = std::abs (-s.ay * (cx - s.mx) + s.ax * (cy - s.my)) / R;   // osa rysky musi mirit do stredu cepicky
    const double angle = std::atan2 (fx - cx, -(fy - cy)) * 180.0 / kPi;
    double conf = juce::jlimit (0.0, 1.0, (s.elong - 0.55) / 0.3)
                * juce::jlimit (0.0, 1.0, (farD - nearD - 0.15) / 0.35)
                * juce::jlimit (0.0, 1.0, (0.35 - axisRes) / 0.2);
    return fromAngle (c, angle, conf);
}

struct Hsv { double v, s; };
Hsv sample (const Photo& ph, double x, double y, double radius)
{
    double vs = 0, ss = 0; int n = 0;
    const double step = juce::jmax (1.0, radius / 2.5);
    for (double dy = -radius; dy <= radius; dy += step)
        for (double dx = -radius; dx <= radius; dx += step)
        {
            if (dx * dx + dy * dy > radius * radius) continue;
            const double r = ph.r.bil (x + dx, y + dy), g = ph.g.bil (x + dx, y + dy), b = ph.b.bil (x + dx, y + dy);
            const double mx = juce::jmax (r, g, b), mn = juce::jmin (r, g, b);
            vs += mx; ss += mx > 1e-6 ? (mx - mn) / mx : 0.0; ++n;
        }
    return n > 0 ? Hsv { vs / n, ss / n } : Hsv { 0, 0 };
}

rm::ControlValue onOff (bool lit, double margin)
{
    rm::ControlValue v;
    v.source = "recognized";
    v.confidence = juce::jlimit (0.0, 1.0, margin / 0.18);
    v.known = v.confidence >= 0.30;
    v.pos = lit ? 1.0 : 0.0;
    return v;
}

// LED vedle tlacitka: sviti = jasna A barevna skvrna (bily potisk hned vedle LED je jasny, ale bezbarvy).
rm::ControlValue readLed (const Photo& ph, const rm::ControlDef& c, const Transform& t)
{
    if (c.draw.points.empty()) return {};
    double px, py; t.map (c.draw.points[0].x, c.draw.points[0].y, px, py);
    double j[4]; t.jacobian (c.draw.points[0].x, c.draw.points[0].y, j);
    const double s = std::sqrt (std::abs (j[0] * j[3] - j[1] * j[2]));
    const double r = juce::jmax (1.5, c.draw.ledR * s);

    // v malem okoli (nepresne zarovnani, paralaxa) se hleda nejbarevnejsi jasne misto
    double best = 0;
    for (double dy = -1.5 * r; dy <= 1.5 * r; dy += juce::jmax (1.0, r / 2))
        for (double dx = -1.5 * r; dx <= 1.5 * r; dx += juce::jmax (1.0, r / 2))
        {
            const auto h = sample (ph, px + dx, py + dy, r * 0.9);
            best = juce::jmax (best, h.v * juce::jmin (1.0, h.s * 1.6));
        }
    const double thr = 0.36;
    return onOff (best > thr, std::abs (best - thr) * 1.3);
}

// xmax: svitici barevny krouzek kolem tlacitka
rm::ControlValue readRingGlow (const Photo& ph, const rm::ControlDef& c, const Transform& t)
{
    double px, py; t.map (c.draw.gx, c.draw.gy, px, py);
    double j[4]; t.jacobian (c.draw.gx, c.draw.gy, j);
    const double s = std::sqrt (std::abs (j[0] * j[3] - j[1] * j[2]));
    // tlacitko s krouzkem vycniva z panelu -> stred se hleda v malem okoli
    double bright = 0;
    const double R = c.draw.gr * s;
    for (double oy = -0.8 * R; oy <= 0.8 * R + 1e-9; oy += 0.2 * R)
        for (double ox = -0.8 * R; ox <= 0.8 * R + 1e-9; ox += 0.2 * R)
        {
            std::vector<double> vals;
            for (double k : { 0.80, 0.92, 1.04 })
                for (int i = 0; i < 16; ++i)
                {
                    const double a = 2 * kPi * i / 16;
                    const auto h = sample (ph, px + ox + R * k * std::cos (a), py + oy + R * k * std::sin (a), juce::jmax (1.0, c.draw.gw * s * 0.3));
                    vals.push_back (h.v * juce::jmin (1.0, h.s * 1.6));
                }
            std::sort (vals.begin(), vals.end());
            bright = juce::jmax (bright, vals[vals.size() * 70 / 100]);    // sviti-li, je jasna a barevna aspon tretina krouzku
        }
    if (std::getenv ("RM_DEBUG") != nullptr) std::cout << "  ring " << c.id << " " << bright << " R=" << R << " at " << px << "," << py << std::endl;
    const double thr = 0.45;
    return onOff (bright > thr, std::abs (bright - thr));
}

// SPL: podsvicene tlacitko ON
rm::ControlValue readRectGlow (const Photo& ph, const rm::ControlDef& c, const Transform& t)
{
    double px, py; t.map (c.draw.gx, c.draw.gy, px, py);
    double j[4]; t.jacobian (c.draw.gx, c.draw.gy, j);
    const double s = std::sqrt (std::abs (j[0] * j[3] - j[1] * j[2]));
    const auto h = sample (ph, px, py, juce::jmax (2.0, juce::jmin (c.draw.gw, c.draw.gh) * 0.32 * s));
    const double thr = 0.45;
    return onOff (h.v > thr, std::abs (h.v - thr));
}

// Svitici LED: barva odpovida barve LED z profilu (modra, cervena, zelena...) a je vyrazne vic nez v okoli.
// Teple svetlo v mistnosti barvi bily / stribrny panel do oranzova - proto barva LED, ne jen "sytost".
double ledContrastAt (const Photo& ph, double px, double py, double r, juce::Colour col)
{
    const double H = col.getHue() * 360.0;
    auto score = [&] (double x, double y)
    {
        const double R = ph.r.bil (x, y), G = ph.g.bil (x, y), B = ph.b.bil (x, y);
        const double mx = juce::jmax (R, G, B), mn = juce::jmin (R, G, B);
        if (mx < 1e-6 || mx - mn < 1e-6) return 0.0;
        double h;
        if (mx == R)      h = 60.0 * std::fmod ((G - B) / (mx - mn), 6.0);
        else if (mx == G) h = 60.0 * ((B - R) / (mx - mn) + 2.0);
        else              h = 60.0 * ((R - G) / (mx - mn) + 4.0);
        if (h < 0) h += 360.0;
        double dh = std::abs (h - H); if (dh > 180.0) dh = 360.0 - dh;
        const double match = dh < 45.0 ? std::cos (dh / 45.0 * kPi * 0.5) : 0.0;
        return mx * ((mx - mn) / mx) * match;
    };
    auto disc = [&] (double cx, double cy, double rad)
    {
        // svitici LED mivá prepaleny (bily) stred a barevnou auru -> horni ctvrtina bodu, ne prumer
        std::vector<double> vals;
        const double st = juce::jmax (0.6, rad / 3.0);
        for (double dy = -rad; dy <= rad + 1e-9; dy += st)
            for (double dx = -rad; dx <= rad + 1e-9; dx += st)
                if (dx * dx + dy * dy <= rad * rad) vals.push_back (score (cx + dx, cy + dy));
        if (vals.empty()) return 0.0;
        std::sort (vals.begin(), vals.end());
        return vals[vals.size() * 3 / 4];
    };
    // stred LED v malem okoli (nepresne zarovnani, paralaxa)
    double best = -1, bx = px, by = py;
    for (double dy = -1.2 * r; dy <= 1.2 * r + 1e-9; dy += juce::jmax (0.7, r / 2))
        for (double dx = -1.2 * r; dx <= 1.2 * r + 1e-9; dx += juce::jmax (0.7, r / 2))
        {
            const double v = disc (px + dx, py + dy, r * 1.1);
            if (v > best) { best = v; bx = px + dx; by = py + dy; }
        }
    std::vector<double> ring;
    for (int i = 0; i < 20; ++i)
    {
        const double a = 2 * kPi * i / 20;
        ring.push_back (score (bx + 2.6 * r * std::cos (a), by + 2.6 * r * std::sin (a)));
    }
    std::sort (ring.begin(), ring.end());
    return best - ring[ring.size() / 2];
}

double ledLevel (const Photo& ph, const rm::ControlDef& c, const Transform& t, size_t i)
{
    double px, py; t.map (c.draw.points[i].x, c.draw.points[i].y, px, py);
    double j[4]; t.jacobian (c.draw.points[i].x, c.draw.points[i].y, j);
    const double s = std::sqrt (std::abs (j[0] * j[3] - j[1] * j[2]));
    return ledContrastAt (ph, px, py, juce::jmax (1.2, c.draw.ledR * s), juce::Colour (c.draw.colour));
}

// prah svitivosti spolecny pro cely pristroj (fotky maji ruznou expozici; svitici LED je vyrazne nad ostatnimi)
struct LedThreshold { double thr = 0.08; double max = 0; };
LedThreshold ledThreshold (const std::vector<double>& all)
{
    LedThreshold t;
    for (auto v : all) t.max = juce::jmax (t.max, v);
    t.thr = juce::jmax (0.05, t.max * 0.5);
    return t;
}

rm::ControlValue ledOnOff (double level, const LedThreshold& t)
{
    rm::ControlValue v; v.source = "recognized";
    const double margin = std::abs (level - t.thr) / juce::jmax (0.03, t.thr);
    v.confidence = juce::jlimit (0.0, 1.0, margin);
    v.known = v.confidence >= 0.35;
    v.pos = level > t.thr ? 1.0 : 0.0;
    return v;
}

// WesAudio _RHEA: krouzek LED kolem enkoderu. Nevime jiste, jestli krouzek ukazuje jednu tecku nebo pas
// (manual to neuvadi) -> umi oboje; nic nesviti / cokoli jineho = nejiste (nezadano, ke kontrole).
rm::ControlValue readLedRing (const std::vector<double>& lv, const rm::ControlDef& c, const LedThreshold& t)
{
    rm::ControlValue v; v.source = "recognized";
    const int n = (int) lv.size();
    if (n < 2 || n != c.numPositions()) return v;
    const double mx = *std::max_element (lv.begin(), lv.end());
    if (std::getenv ("RM_DEBUG_LED")) { std::cout << "  ledring-in " << c.id << " devthr " << t.thr << ":"; for (auto x : lv) std::cout << " " << juce::String (x, 3); std::cout << std::endl; }
    if (mx < t.thr || mx < 0.06) return v;                            // nic nesviti (vypnuto / zakryto knobem)
    // Svitici = nad skokem jasu (serazeno od nejjasnejsi). Pevny prah 55 % maxima nestaci: u teckoveho rezimu
    // soused svitici LED chyti cast jeji zare (~50 %) a cteni vypadalo nejiste (regresni sada _RHEA).
    // Zkousi se kazdy skok; plati nejvetsi, ktery dava smysluplny vzor (tecka nebo pas od kraje / od stredu).
    std::vector<double> sorted (lv.begin(), lv.end());
    std::sort (sorted.begin(), sorted.end(), std::greater<double>());
    sorted.push_back (0.0);
    auto patternFor = [&] (double thr, std::vector<int>& lit) -> int
    {
        lit.clear();
        for (int i = 0; i < n; ++i) if (lv[(size_t) i] > thr) lit.push_back (i);
        if (lit.empty()) return -1;
        for (size_t q = 1; q < lit.size(); ++q) if (lit[q] != lit[q - 1] + 1) return -1;
        const int a = lit.front(), b = lit.back();
        if (a == b) return a;
        if (a == 0) return b;
        if (b == n - 1) return a;
        if (c.minLabel.startsWith ("-") && (a == n / 2 || a == n / 2 - 1)) return b;
        if (c.minLabel.startsWith ("-") && (b == n / 2 || b == n / 2 - 1)) return a;
        return -1;
    };
    int pos = -1, bestK = 0; double gap = -1;
    for (int k = 1; k <= n; ++k)
    {
        const double g = (sorted[(size_t) k - 1] - sorted[(size_t) k]) / mx;
        if (g <= gap + 1e-9) continue;
        const double thr = juce::jmax (t.thr * 0.9, 0.5 * (sorted[(size_t) k - 1] + sorted[(size_t) k]));
        std::vector<int> lit;
        const int p = patternFor (thr, lit);
        if (p < 0) continue;
        gap = g; pos = p; bestK = k;
    }
    if (pos < 0) return v;
    // jistota = jak vyrazny je skok mezi svitici a nesvitici (v podilu maxima)
    const double hi = (sorted[(size_t) bestK - 1] - t.thr) / juce::jmax (0.03, t.thr) + 0.5;
    v.confidence = juce::jlimit (0.0, 1.0, juce::jmin ((gap - 0.15) / 0.25, hi));
    v.known = v.confidence >= 0.35;
    v.pos = pos;
    if (std::getenv ("RM_DEBUG_LED"))
    {
        std::cout << "  ledring " << c.id << " devthr " << t.thr << " gap " << gap << " k " << bestK << ":";
        for (auto x : lv) std::cout << " " << juce::String (x, 3);
        std::cout << std::endl;
    }
    return v;
}

// Prepinac / tlacitko s LED (jedna sviti = poloha; zadna = poloha bez LED, napr. OFF)
rm::ControlValue readLedSelect (const std::vector<double>& lv, const rm::ControlDef& c, const LedThreshold& t)
{
    rm::ControlValue v; v.source = "recognized";
    const int n = (int) lv.size();
    if (n < 1 || (int) c.draw.map.size() != c.numPositions()) return v;
    int litCount = 0, litIdx = -1; double margin = 1.0;
    for (int i = 0; i < n; ++i)
    {
        margin = juce::jmin (margin, std::abs (lv[(size_t) i] - t.thr) / juce::jmax (0.03, t.thr));
        if (lv[(size_t) i] > t.thr) { ++litCount; litIdx = i; }
    }
    if (litCount > 1) return v;
    for (int p = 0; p < (int) c.draw.map.size(); ++p)
        if (c.draw.map[(size_t) p] == litIdx) { v.pos = p; v.known = true; }
    v.confidence = juce::jlimit (0.0, 1.0, margin);
    v.known = v.known && v.confidence >= 0.30;              // jako 0.9 (0.35 = regrese na _RHEA)
    if (std::getenv ("RM_DEBUG_LED"))
    {
        std::cout << "  ledsel " << c.id << " devthr " << t.thr << " margin " << margin << ":";
        for (auto x : lv) std::cout << " " << juce::String (x, 3);
        std::cout << std::endl;
    }
    return v;
}

//==============================================================================
// Zarovnani podle potisku panelu (nezavisle na knobech)
//==============================================================================

// zmenseni/zvetseni prumerovanim (s = novy rozmer / stary)
Plane resampleBox (const Plane& src, double s)
{
    Plane d;
    d.w = juce::jmax (1, (int) std::lround (src.w * s));
    d.h = juce::jmax (1, (int) std::lround (src.h * s));
    d.v.resize ((size_t) d.w * (size_t) d.h);
    if (s >= 0.999)
    {
        for (int y = 0; y < d.h; ++y)
            for (int x = 0; x < d.w; ++x)
                d.v[(size_t) y * (size_t) d.w + (size_t) x] = src.bil ((x + 0.5) / s - 0.5, (y + 0.5) / s - 0.5);
        return d;
    }
    // integralni obraz
    std::vector<double> I ((size_t) (src.w + 1) * (size_t) (src.h + 1), 0.0);
    const size_t W1 = (size_t) src.w + 1;
    for (int y = 0; y < src.h; ++y)
    {
        double row = 0;
        for (int x = 0; x < src.w; ++x)
        {
            row += src.v[(size_t) y * (size_t) src.w + (size_t) x];
            I[(size_t) (y + 1) * W1 + (size_t) (x + 1)] = I[(size_t) y * W1 + (size_t) (x + 1)] + row;
        }
    }
    for (int y = 0; y < d.h; ++y)
    {
        const int y0 = juce::jlimit (0, src.h - 1, (int) std::floor (y / s)), y1 = juce::jlimit (y0 + 1, src.h, (int) std::floor ((y + 1) / s));
        for (int x = 0; x < d.w; ++x)
        {
            const int x0 = juce::jlimit (0, src.w - 1, (int) std::floor (x / s)), x1 = juce::jlimit (x0 + 1, src.w, (int) std::floor ((x + 1) / s));
            const double sum = I[(size_t) y1 * W1 + (size_t) x1] - I[(size_t) y0 * W1 + (size_t) x1] - I[(size_t) y1 * W1 + (size_t) x0] + I[(size_t) y0 * W1 + (size_t) x0];
            d.v[(size_t) y * (size_t) d.w + (size_t) x] = (float) (sum / ((x1 - x0) * (y1 - y0)));
        }
    }
    return d;
}

// Referencni panel: jas + maska (1 = potisk, ktery se nemeni; 0 = knoby, tlacitka, LED, okoli panelu)
struct RefPanel
{
    Plane gray, mask;
    int w = 0, h = 0;
};

RefPanel makeRefPanel (const rm::DeviceProfile& prof, const juce::Image& reference)
{
    RefPanel rp;
    rp.w = prof.w; rp.h = prof.h;
    if (! reference.isValid()) return rp;
    auto img = reference.rescaled (prof.w, prof.h, juce::Graphics::highResamplingQuality).convertedToFormat (juce::Image::ARGB);
    for (auto* pl : { &rp.gray, &rp.mask }) { pl->w = prof.w; pl->h = prof.h; pl->v.assign ((size_t) prof.w * (size_t) prof.h, 1.0f); }
    {
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < prof.h; ++y)
            for (int x = 0; x < prof.w; ++x)
            {
                const auto c = bd.getPixelColour (x, y);
                rp.gray.v[(size_t) y * (size_t) prof.w + (size_t) x] = 0.299f * c.getFloatRed() + 0.587f * c.getFloatGreen() + 0.114f * c.getFloatBlue();
            }
    }
    auto hole = [&rp] (double cx, double cy, double r)
    {
        for (int y = juce::jmax (0, (int) (cy - r)); y <= juce::jmin (rp.h - 1, (int) (cy + r) + 1); ++y)
            for (int x = juce::jmax (0, (int) (cx - r)); x <= juce::jmin (rp.w - 1, (int) (cx + r) + 1); ++x)
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) rp.mask.v[(size_t) y * (size_t) rp.w + (size_t) x] = 0.0f;
    };
    auto rect = [&rp] (double cx, double cy, double hw, double hh)
    {
        for (int y = juce::jmax (0, (int) (cy - hh)); y <= juce::jmin (rp.h - 1, (int) (cy + hh) + 1); ++y)
            for (int x = juce::jmax (0, (int) (cx - hw)); x <= juce::jmin (rp.w - 1, (int) (cx + hw) + 1); ++x)
                rp.mask.v[(size_t) y * (size_t) rp.w + (size_t) x] = 0.0f;
    };
    for (auto& c : prof.controls)
    {
        const bool isKnob = c.type == ControlType::knob || c.type == ControlType::stepped || c.type == ControlType::selector;
        hole (c.x, c.y, isKnob ? 1.12 * c.r : 1.25 * c.r);
        for (auto& p : c.draw.points) hole (p.x, p.y, juce::jmax (6.0f, 2.2f * c.draw.ledR));
        if (c.draw.gr > 0)            hole (c.draw.gx, c.draw.gy, 1.35 * c.draw.gr);
        if (c.draw.gw > 0 && c.draw.gh > 0) rect (c.draw.gx, c.draw.gy, 0.65 * c.draw.gw, 0.65 * c.draw.gh);
    }
    return rp;
}

struct Sample { float x, y, v; };      // bod referencniho panelu (px profilu) a jeho normovany jas

// body potisku v mrizce s rozestupem `spacing` (px profilu); hodnoty z panelu rozmazaneho na stejne meritko
std::vector<Sample> refSamples (const RefPanel& rp, double spacing, bool normalise)
{
    std::vector<Sample> out;
    if (rp.gray.v.empty()) return out;
    const double s = juce::jmin (1.0, 1.0 / spacing);
    const Plane g = resampleBox (rp.gray, s), m = resampleBox (rp.mask, s);
    for (int y = 0; y < g.h; ++y)
        for (int x = 0; x < g.w; ++x)
        {
            if (m.v[(size_t) y * (size_t) m.w + (size_t) x] < 0.98f) continue;
            out.push_back ({ (float) ((x + 0.5) / s), (float) ((y + 0.5) / s), g.v[(size_t) y * (size_t) g.w + (size_t) x] });
        }
    if (normalise && ! out.empty())
    {
        double mean = 0, sq = 0;
        for (auto& p : out) mean += p.v;
        mean /= (double) out.size();
        for (auto& p : out) sq += (p.v - mean) * (p.v - mean);
        const double nrm = std::sqrt (juce::jmax (1e-12, sq));
        for (auto& p : out) p.v = (float) ((p.v - mean) / nrm);
    }
    return out;
}

// Jemne doladeni homografie podle potisku (Levenberg-Marquardt, od hrubeho k jemnemu;
// jas fotky se pokazde prepocita linearne na jas reference -> nevadi jine osvetleni).
Transform refinePrint (const Plane& photo, const RefPanel& rp, Transform t0, double& nccOut)
{
    Transform t = t0;
    nccOut = -1;
    if (rp.gray.v.empty()) return t;

    auto scaleOf = [&rp] (const Transform& tr)
    {
        double j[4]; tr.jacobian (rp.w * 0.5, rp.h * 0.5, j);
        return std::sqrt (std::abs (j[0] * j[3] - j[1] * j[2]));
    };
    const double k0 = scaleOf (t0);

    for (double levelPx : { 8.0, 4.0, 2.0, 1.0 })           // rozestup vzorku na fotce (px)
    {
        const double k = scaleOf (t);
        if (k < 1e-6) break;
        const double spacing = juce::jmax (juce::jmax (1.0, std::sqrt ((double) rp.w * rp.h / 40000.0)), levelPx / k);   // px profilu, max ~40k bodu
        const auto pts = refSamples (rp, spacing, false);
        if (pts.size() < 60) continue;
        const double q = juce::jmin (1.0, 1.0 / levelPx);
        const Plane L = resampleBox (photo, q);

        // vyhodnoceni: naklad + (volitelne) normalni rovnice
        struct Eval { double cost = 1e300; int n = 0; double A[64]; double b[8]; };
        auto evaluate = [&] (const Transform& tr, bool withJ, double huberIn, double& huberOut) -> Eval
        {
            Eval e; std::fill (e.A, e.A + 64, 0.0); std::fill (e.b, e.b + 8, 0.0);
            std::vector<double> I (pts.size()), GX (pts.size()), GY (pts.size()), X (pts.size()), Y (pts.size()), W (pts.size());
            std::vector<char> ok (pts.size(), 0);
            double sI = 0, sT = 0, sII = 0, sIT = 0; int n = 0;
            for (size_t i = 0; i < pts.size(); ++i)
            {
                const double x = pts[i].x, y = pts[i].y;
                const double w = tr.h[6] * x + tr.h[7] * y + tr.h[8];
                if (w <= 1e-9) continue;
                const double px = (tr.h[0] * x + tr.h[1] * y + tr.h[2]) / w, py = (tr.h[3] * x + tr.h[4] * y + tr.h[5]) / w;
                const double lx = px * q, ly = py * q;
                if (lx < 1 || ly < 1 || lx > L.w - 2 || ly > L.h - 2) continue;
                const double v = L.bil (lx, ly);
                I[i] = v; X[i] = px; Y[i] = py; W[i] = w; ok[i] = 1;
                if (withJ) { GX[i] = 0.5 * (L.bil (lx + 1, ly) - L.bil (lx - 1, ly)) * q; GY[i] = 0.5 * (L.bil (lx, ly + 1) - L.bil (lx, ly - 1)) * q; }
                sI += v; sT += pts[i].v; sII += v * v; sIT += v * pts[i].v; ++n;
            }
            e.n = n;
            if (n < (int) pts.size() / 2 || n < 40) return e;
            const double varI = sII / n - (sI / n) * (sI / n);
            if (varI < 1e-8) return e;
            const double alpha = (sIT / n - (sI / n) * (sT / n)) / varI;
            const double beta = sT / n - alpha * sI / n;
            if (alpha <= 0) return e;                       // obraceny kontrast = spatna poloha

            std::vector<double> R (pts.size(), 0.0), absr;
            for (size_t i = 0; i < pts.size(); ++i) if (ok[i]) { R[i] = alpha * I[i] + beta - pts[i].v; absr.push_back (std::abs (R[i])); }
            double huber = huberIn;
            if (huber <= 0)
            {
                std::nth_element (absr.begin(), absr.begin() + (long) absr.size() / 2, absr.end());
                huber = 1.5 * juce::jmax (0.01, absr[absr.size() / 2] * 1.4826);
            }
            huberOut = huber;
            double cost = 0;
            for (size_t i = 0; i < pts.size(); ++i)
            {
                if (! ok[i]) { cost += huber * huber; continue; }   // bod mimo fotku = plna pokuta
                const double r = R[i], ar = std::abs (r);
                cost += ar <= huber ? r * r : huber * (2 * ar - huber);
                if (! withJ) continue;
                const double wgt = ar <= huber ? 1.0 : huber / ar;
                const double x = pts[i].x, y = pts[i].y, w = W[i];
                const double gx = alpha * GX[i], gy = alpha * GY[i];
                const double J[8] = { gx * x / w, gx * y / w, gx / w, gy * x / w, gy * y / w, gy / w,
                                      -(gx * X[i] + gy * Y[i]) * x / w, -(gx * X[i] + gy * Y[i]) * y / w };
                for (int a2 = 0; a2 < 8; ++a2)
                {
                    e.b[a2] -= wgt * J[a2] * r;
                    for (int c = 0; c < 8; ++c) e.A[a2 * 8 + c] += wgt * J[a2] * J[c];
                }
            }
            e.cost = cost / (double) pts.size();
            return e;
        };

        double huber = -1, lambda = 1e-2;
        Eval cur = evaluate (t, true, -1, huber);
        if (cur.cost >= 1e299) continue;
        for (int it = 0; it < 30; ++it)
        {
            bool improved = false;
            for (int tries = 0; tries < 6 && ! improved; ++tries)
            {
                std::vector<double> M (cur.A, cur.A + 64), rhs (cur.b, cur.b + 8);
                for (int a2 = 0; a2 < 8; ++a2) M[(size_t) (a2 * 8 + a2)] += lambda * cur.A[a2 * 8 + a2] + 1e-15;
                if (! solveLinear (M, rhs, 8)) { lambda *= 10; continue; }
                Transform nt = t;
                for (int a2 = 0; a2 < 8; ++a2) nt.h[a2] += rhs[(size_t) a2];
                const double k1 = scaleOf (nt);
                if (! (k1 > 0.7 * k0 && k1 < 1.4 * k0)) { lambda *= 10; continue; }
                double hdummy;
                const Eval ne = evaluate (nt, false, huber, hdummy);
                if (ne.cost < cur.cost)
                {
                    // velikost kroku (rohy panelu, px fotky)
                    double move = 0;
                    for (auto pt : { cd (0, 0), cd (rp.w, 0), cd (0, rp.h), cd (rp.w, rp.h) })
                    {
                        double ax, ay, bx, by; t.map (pt.real(), pt.imag(), ax, ay); nt.map (pt.real(), pt.imag(), bx, by);
                        move = juce::jmax (move, std::hypot (bx - ax, by - ay));
                    }
                    t = nt; improved = true;
                    lambda = juce::jmax (1e-7, lambda * 0.3);
                    double h2;
                    cur = evaluate (t, true, huber, h2);
                    if (move < 0.03 * levelPx) { it = 1000; }
                }
                else lambda *= 8;
            }
            if (! improved) break;
        }
    }

    // vysledna shoda (NCC) na jemnych bodech
    {
        const double k = scaleOf (t);
        const auto pts = refSamples (rp, juce::jmax (juce::jmax (1.0, std::sqrt ((double) rp.w * rp.h / 60000.0)), 1.5 / juce::jmax (1e-6, k)), false);
        double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0; int n = 0, total = 0;
        for (auto& p : pts)
        {
            ++total;
            double px, py; t.map (p.x, p.y, px, py);
            if (px < 1 || py < 1 || px > photo.w - 2 || py > photo.h - 2) continue;
            const double v = photo.bil (px, py);
            sa += p.v; sb += v; saa += p.v * p.v; sbb += v * v; sab += p.v * v; ++n;
        }
        if (n > 50 && n > total / 2)
        {
            const double cov = sab / n - (sa / n) * (sb / n), va = saa / n - (sa / n) * (sa / n), vb = sbb / n - (sb / n) * (sb / n);
            nccOut = (va > 1e-9 && vb > 1e-9) ? cov / std::sqrt (va * vb) : 0.0;
            nccOut *= std::sqrt ((double) n / total);         // kdyz je kus panelu mimo fotku, duvera klesa
        }
    }
    return t;
}

//==============================================================================
// Vyrazne body (zjednodusena varianta SIFT): rozdil gaussianu pres meritka, orientace,
// popis 4x4x8 histogramu gradientu. Slouzi k nalezeni panelu na fotce podle potisku
// (pismena, stupnice, srouby) - funguje i na tmavych panelech, s pozadim, vodoznakem, v racku.
//==============================================================================
struct KeyPt { float x, y, sigma, angle, resp; std::array<float, 128> d; };

void gaussBlur (const Plane& src, Plane& dst, double sigma)
{
    const int r = juce::jmax (1, (int) std::ceil (3.0 * sigma));
    std::vector<float> k ((size_t) (2 * r + 1));
    float sum = 0;
    for (int i = -r; i <= r; ++i) { k[(size_t) (i + r)] = (float) std::exp (-0.5 * i * i / (sigma * sigma)); sum += k[(size_t) (i + r)]; }
    for (auto& x : k) x /= sum;
    Plane tmp; tmp.w = src.w; tmp.h = src.h; tmp.v.resize (src.v.size());
    const int w = src.w, h = src.h;
    for (int y = 0; y < h; ++y)
    {
        const float* row = src.v.data() + (size_t) y * (size_t) w;
        float* out = tmp.v.data() + (size_t) y * (size_t) w;
        for (int x = 0; x < w; ++x)
        {
            float s = 0;
            if (x >= r && x < w - r) { const float* p = row + x - r; for (int i = 0; i <= 2 * r; ++i) s += k[(size_t) i] * p[i]; }
            else for (int i = -r; i <= r; ++i) s += k[(size_t) (i + r)] * row[juce::jlimit (0, w - 1, x + i)];
            out[x] = s;
        }
    }
    dst.w = w; dst.h = h; dst.v.resize (src.v.size());
    for (int y = 0; y < h; ++y)
    {
        float* out = dst.v.data() + (size_t) y * (size_t) w;
        if (y >= r && y < h - r)
        {
            for (int x = 0; x < w; ++x) out[x] = 0;
            for (int i = -r; i <= r; ++i)
            {
                const float kk = k[(size_t) (i + r)];
                const float* p = tmp.v.data() + (size_t) (y + i) * (size_t) w;
                for (int x = 0; x < w; ++x) out[x] += kk * p[x];
            }
        }
        else
            for (int x = 0; x < w; ++x)
            {
                float s = 0;
                for (int i = -r; i <= r; ++i) s += k[(size_t) (i + r)] * tmp.v[(size_t) juce::jlimit (0, h - 1, y + i) * (size_t) w + (size_t) x];
                out[x] = s;
            }
    }
}

std::vector<KeyPt> detectKeypoints (const Plane& image, const Plane* mask, int maxPts, float contrastThr = 0.03f)
{
    const int S = 3;
    const double sigma0 = 1.6;
    const float edgeR = 10.0f;
    std::vector<KeyPt> kps;
    if (image.w < 32 || image.h < 32) return kps;

    Plane base; gaussBlur (image, base, std::sqrt (sigma0 * sigma0 - 0.25));
    const int nOct = juce::jlimit (1, 7, (int) std::floor (std::log2 ((double) juce::jmin (image.w, image.h))) - 3);
    double sig[S + 3];
    sig[0] = sigma0;
    for (int i = 1; i < S + 3; ++i)
    {
        const double prev = sigma0 * std::pow (2.0, (i - 1) / (double) S), tot = sigma0 * std::pow (2.0, i / (double) S);
        sig[i] = std::sqrt (tot * tot - prev * prev);
    }

    for (int o = 0; o < nOct; ++o)
    {
        if (cancelled()) return {};
        std::vector<Plane> G ((size_t) (S + 3)), D ((size_t) (S + 2));
        G[0] = base;
        for (int i = 1; i < S + 3; ++i) gaussBlur (G[(size_t) i - 1], G[(size_t) i], sig[i]);
        const int w = G[0].w, h = G[0].h;
        for (int i = 0; i < S + 2; ++i)
        {
            D[(size_t) i].w = w; D[(size_t) i].h = h; D[(size_t) i].v.resize ((size_t) w * (size_t) h);
            for (size_t p = 0; p < D[(size_t) i].v.size(); ++p) D[(size_t) i].v[p] = G[(size_t) i + 1].v[p] - G[(size_t) i].v[p];
        }
        const double octScale = std::pow (2.0, o);
        const int border = 5;

        for (int i = 1; i <= S; ++i)
        {
            const Plane& d0 = D[(size_t) i - 1]; const Plane& d1 = D[(size_t) i]; const Plane& d2 = D[(size_t) i + 1];
            for (int y = border; y < h - border; ++y)
                for (int x = border; x < w - border; ++x)
                {
                    const float v = d1.v[(size_t) y * (size_t) w + (size_t) x];
                    if (std::abs (v) < 0.5f * contrastThr / S) continue;
                    bool isMax = true, isMin = true;
                    for (int dz = 0; dz < 3 && (isMax || isMin); ++dz)
                    {
                        const Plane& dd = dz == 0 ? d0 : (dz == 1 ? d1 : d2);
                        for (int yy = -1; yy <= 1 && (isMax || isMin); ++yy)
                            for (int xx = -1; xx <= 1; ++xx)
                            {
                                if (dz == 1 && xx == 0 && yy == 0) continue;
                                const float u = dd.v[(size_t) (y + yy) * (size_t) w + (size_t) (x + xx)];
                                if (u >= v) isMax = false;
                                if (u <= v) isMin = false;
                            }
                    }
                    if (! isMax && ! isMin) continue;

                    // subpixelove upresneni (kvadraticka aproximace)
                    int xi = x, yi = y, li = i;
                    double ox = 0, oy = 0, os = 0; float val = v; bool ok = false;
                    for (int iter = 0; iter < 5; ++iter)
                    {
                        const Plane& A = D[(size_t) li - 1]; const Plane& B = D[(size_t) li]; const Plane& C = D[(size_t) li + 1];
                        auto at = [w] (const Plane& p, int X, int Y) { return (double) p.v[(size_t) Y * (size_t) w + (size_t) X]; };
                        const double dx = (at (B, xi + 1, yi) - at (B, xi - 1, yi)) * 0.5;
                        const double dy = (at (B, xi, yi + 1) - at (B, xi, yi - 1)) * 0.5;
                        const double ds = (at (C, xi, yi) - at (A, xi, yi)) * 0.5;
                        const double c = at (B, xi, yi);
                        const double dxx = at (B, xi + 1, yi) + at (B, xi - 1, yi) - 2 * c;
                        const double dyy = at (B, xi, yi + 1) + at (B, xi, yi - 1) - 2 * c;
                        const double dss = at (C, xi, yi) + at (A, xi, yi) - 2 * c;
                        const double dxy = (at (B, xi + 1, yi + 1) - at (B, xi - 1, yi + 1) - at (B, xi + 1, yi - 1) + at (B, xi - 1, yi - 1)) * 0.25;
                        const double dxs = (at (C, xi + 1, yi) - at (C, xi - 1, yi) - at (A, xi + 1, yi) + at (A, xi - 1, yi)) * 0.25;
                        const double dys = (at (C, xi, yi + 1) - at (C, xi, yi - 1) - at (A, xi, yi + 1) + at (A, xi, yi - 1)) * 0.25;
                        std::vector<double> M { dxx, dxy, dxs, dxy, dyy, dys, dxs, dys, dss }, g { -dx, -dy, -ds };
                        if (! solveLinear (M, g, 3)) break;
                        ox = g[0]; oy = g[1]; os = g[2];
                        if (std::abs (ox) < 0.5 && std::abs (oy) < 0.5 && std::abs (os) < 0.5)
                        {
                            val = (float) (c + 0.5 * (dx * ox + dy * oy + ds * os));
                            // hrany (pomer hlavnich krivosti)
                            const double tr = dxx + dyy, det = dxx * dyy - dxy * dxy;
                            ok = det > 0 && tr * tr * edgeR < (edgeR + 1) * (edgeR + 1) * det;
                            break;
                        }
                        xi += (int) std::lround (ox); yi += (int) std::lround (oy); li += (int) std::lround (os);
                        if (li < 1 || li > S || xi < border || yi < border || xi >= w - border || yi >= h - border) break;
                    }
                    if (! ok || std::abs (val) < contrastThr / S) continue;

                    const double px = (xi + ox) * octScale, py = (yi + oy) * octScale;
                    if (mask != nullptr && mask->bil (px, py) < 0.99f) continue;
                    const double sLocal = sigma0 * std::pow (2.0, (li + os) / S);     // v px oktavy
                    const Plane& Gl = G[(size_t) li];

                    // orientace: histogram 36 smeru
                    float hist[36] = {};
                    const double sw = 1.5 * sLocal;
                    const int rad = (int) std::lround (3.0 * sw);
                    for (int yy = -rad; yy <= rad; ++yy)
                        for (int xx = -rad; xx <= rad; ++xx)
                        {
                            const int X = xi + xx, Y = yi + yy;
                            if (X < 1 || Y < 1 || X >= w - 1 || Y >= h - 1) continue;
                            const float gx = Gl.v[(size_t) Y * (size_t) w + (size_t) X + 1] - Gl.v[(size_t) Y * (size_t) w + (size_t) X - 1];
                            const float gy = Gl.v[(size_t) (Y + 1) * (size_t) w + (size_t) X] - Gl.v[(size_t) (Y - 1) * (size_t) w + (size_t) X];
                            const float wgt = (float) std::exp (-(xx * xx + yy * yy) / (2 * sw * sw));
                            const double ang = std::atan2 (gy, gx);
                            int bin = (int) std::lround (36 * (ang + kPi) / (2 * kPi)) % 36;
                            hist[bin] += wgt * std::sqrt (gx * gx + gy * gy);
                        }
                    float sm[36];
                    for (int b = 0; b < 36; ++b) sm[b] = 0.25f * hist[(b + 35) % 36] + 0.5f * hist[b] + 0.25f * hist[(b + 1) % 36];
                    float mx = 0; for (float hv : sm) mx = juce::jmax (mx, hv);
                    if (mx <= 0) continue;
                    for (int b = 0; b < 36; ++b)
                    {
                        const float l = sm[(b + 35) % 36], c = sm[b], r2 = sm[(b + 1) % 36];
                        if (c < 0.8f * mx || c < l || c < r2) continue;
                        const double off = 0.5 * (l - r2) / (l - 2 * c + r2 + 1e-12);
                        const double ang = (b + off) * 2 * kPi / 36 - kPi;

                        // popis 4x4x8
                        KeyPt kp; kp.x = (float) px; kp.y = (float) py; kp.sigma = (float) (sLocal * octScale); kp.angle = (float) ang; kp.resp = std::abs (val);
                        std::fill (kp.d.begin(), kp.d.end(), 0.0f);
                        const double histW = 3.0 * sLocal;
                        const int dr = (int) std::lround (histW * std::sqrt (2.0) * 2.5);
                        const double ca = std::cos (ang), sa = std::sin (ang);
                        for (int yy = -dr; yy <= dr; ++yy)
                            for (int xx = -dr; xx <= dr; ++xx)
                            {
                                const double rx = (ca * xx + sa * yy) / histW, ry = (-sa * xx + ca * yy) / histW;
                                const double bx = rx + 1.5, by = ry + 1.5;
                                if (bx <= -1 || bx >= 4 || by <= -1 || by >= 4) continue;
                                const int X = xi + xx, Y = yi + yy;
                                if (X < 1 || Y < 1 || X >= w - 1 || Y >= h - 1) continue;
                                const float gx = Gl.v[(size_t) Y * (size_t) w + (size_t) X + 1] - Gl.v[(size_t) Y * (size_t) w + (size_t) X - 1];
                                const float gy = Gl.v[(size_t) (Y + 1) * (size_t) w + (size_t) X] - Gl.v[(size_t) (Y - 1) * (size_t) w + (size_t) X];
                                double go = std::atan2 (gy, gx) - ang;
                                while (go < 0) go += 2 * kPi;
                                while (go >= 2 * kPi) go -= 2 * kPi;
                                const double ob = go * 8 / (2 * kPi);
                                const double mag = std::sqrt (gx * gx + gy * gy) * std::exp (-(rx * rx + ry * ry) / 8.0);
                                const int x0 = (int) std::floor (bx), y0 = (int) std::floor (by), o0 = (int) std::floor (ob);
                                const double fx = bx - x0, fy = by - y0, fo = ob - o0;
                                for (int a = 0; a < 2; ++a)
                                {
                                    const int Yb = y0 + a; if (Yb < 0 || Yb > 3) continue;
                                    const double wy = a ? fy : 1 - fy;
                                    for (int b2 = 0; b2 < 2; ++b2)
                                    {
                                        const int Xb = x0 + b2; if (Xb < 0 || Xb > 3) continue;
                                        const double wx = b2 ? fx : 1 - fx;
                                        for (int c2 = 0; c2 < 2; ++c2)
                                        {
                                            const int Ob = (o0 + c2) % 8;
                                            kp.d[(size_t) ((Yb * 4 + Xb) * 8 + Ob)] += (float) (mag * wy * wx * (c2 ? fo : 1 - fo));
                                        }
                                    }
                                }
                            }
                        float n2 = 0; for (float dv : kp.d) n2 += dv * dv;
                        const float cl = 0.2f * std::sqrt (n2);
                        n2 = 0; for (auto& dv : kp.d) { dv = juce::jmin (dv, cl); n2 += dv * dv; }
                        const float inv = n2 > 0 ? 1.0f / std::sqrt (n2) : 0.0f;
                        for (auto& dv : kp.d) dv *= inv;
                        kps.push_back (kp);
                    }
                }
        }
        // dalsi oktava: polovicni rozliseni z G[S]
        const Plane& src = G[(size_t) S];
        Plane nb; nb.w = src.w / 2; nb.h = src.h / 2;
        if (nb.w < 32 || nb.h < 32) break;
        nb.v.resize ((size_t) nb.w * (size_t) nb.h);
        for (int y = 0; y < nb.h; ++y) for (int x = 0; x < nb.w; ++x) nb.v[(size_t) y * (size_t) nb.w + (size_t) x] = src.v[(size_t) (2 * y) * (size_t) src.w + (size_t) (2 * x)];
        base = std::move (nb);
    }
    if ((int) kps.size() > maxPts)
    {
        std::nth_element (kps.begin(), kps.begin() + maxPts, kps.end(), [] (const KeyPt& a, const KeyPt& b) { return a.resp > b.resp; });
        kps.resize ((size_t) maxPts);
    }
    return kps;
}

struct Match { int a, b; float dist; };

std::vector<Match> matchKeypoints (const std::vector<KeyPt>& A, const std::vector<KeyPt>& B, float ratio)
{
    std::vector<Match> out;
    if (B.size() < 2) return out;
    for (size_t i = 0; i < A.size(); ++i)
    {
        float b1 = 1e9f, b2 = 1e9f; int bi = -1;
        const float* da = A[i].d.data();
        for (size_t j = 0; j < B.size(); ++j)
        {
            const float* db = B[j].d.data();
            float s = 0;
            for (int k = 0; k < 128; ++k) { const float t = da[k] - db[k]; s += t * t; }
            if (s < b1) { b2 = b1; b1 = s; bi = (int) j; }
            else if (s < b2) b2 = s;
        }
        if (bi >= 0 && b1 < ratio * ratio * b2) out.push_back ({ (int) i, bi, std::sqrt (b1) });
    }
    return out;
}

// RANSAC: hypotezy z dvojic shod (podobnost), overeni homografii. Vraci pocet inlieru.
int ransacPanel (const std::vector<KeyPt>& R, const std::vector<KeyPt>& P, const std::vector<Match>& m,
                 const rm::DeviceProfile& prof, int photoW, int photoH, Transform& out, bool debug)
{
    const int n = (int) m.size();
    if (n < 6) return 0;
    double tol = juce::jmax (3.0, 0.004 * std::hypot (photoW, photoH));
    juce::Random rng (12345);
    int bestIn = 0; Similarity bestS;

    auto countIn = [&] (auto mapFn, std::vector<int>* idx)
    {
        int c = 0;
        for (int i = 0; i < n; ++i)
        {
            const auto& a = R[(size_t) m[(size_t) i].a]; const auto& b = P[(size_t) m[(size_t) i].b];
            double qx, qy; mapFn (a.x, a.y, qx, qy);
            if (std::hypot (qx - b.x, qy - b.y) < tol) { ++c; if (idx) idx->push_back (i); }
        }
        return c;
    };

    for (int it = 0; it < 1500; ++it)
    {
        const int i = rng.nextInt (n), j = rng.nextInt (n);
        if (i == j) continue;
        const auto& a1 = R[(size_t) m[(size_t) i].a]; const auto& b1 = P[(size_t) m[(size_t) i].b];
        const auto& a2 = R[(size_t) m[(size_t) j].a]; const auto& b2 = P[(size_t) m[(size_t) j].b];
        const cd pa (a1.x, a1.y), pb (a2.x, a2.y), qa (b1.x, b1.y), qb (b2.x, b2.y);
        if (std::abs (pb - pa) < 0.05 * juce::jmax (prof.w, prof.h)) continue;
        Similarity s; s.a = (qb - qa) / (pb - pa); s.b = qa - s.a * pa;
        const double sc = std::abs (s.a);
        if (sc < 0.05 || sc > 8.0 || std::abs (std::arg (s.a)) > 35.0 * kPi / 180.0) continue;
        // meritko a natoceni musi souhlasit i s velikosti/orientaci bodu
        const double r1 = b1.sigma / a1.sigma, r2 = b2.sigma / a2.sigma;
        if (r1 / sc > 1.8 || sc / r1 > 1.8 || r2 / sc > 1.8 || sc / r2 > 1.8) continue;
        const int c = countIn ([&s] (double x, double y, double& ox, double& oy) { const cd q = s.map (cd (x, y)); ox = q.real(); oy = q.imag(); }, nullptr);
        if (c > bestIn) { bestIn = c; bestS = s; }
    }
    if (bestIn < 4) return 0;

    // zpresneni: homografie na inlierech, tolerance se postupne zuzuje (zachyti i silnejsi perspektivu)
    Transform t = fromSimilarity (bestS);
    std::vector<int> idx;
    const double tol0 = tol;
    tol = tol0 * 2.5;
    countIn ([&t] (double x, double y, double& ox, double& oy) { t.map (x, y, ox, oy); }, &idx);
    for (int pass = 0; pass < 4; ++pass)
    {
        tol = tol0 * (pass == 0 ? 2.0 : (pass == 1 ? 1.4 : 1.0));
        std::vector<cd> PP, QQ;
        for (int i : idx) { const auto& a = R[(size_t) m[(size_t) i].a]; const auto& b = P[(size_t) m[(size_t) i].b]; PP.push_back (cd (a.x, a.y)); QQ.push_back (cd (b.x, b.y)); }
        Transform h;
        if (PP.size() >= 8 && fitHomography (PP, QQ, h))
        {
            const double persp = (std::abs (h.h[6]) + std::abs (h.h[7])) * juce::jmax (prof.w, prof.h);
            if (persp < 0.8) t = h; else t = fromSimilarity (fitSimilarity (PP, QQ));
        }
        else if (PP.size() >= 2) t = fromSimilarity (fitSimilarity (PP, QQ));
        idx.clear();
        countIn ([&t] (double x, double y, double& ox, double& oy) { t.map (x, y, ox, oy); }, &idx);
    }
    out = t;
    // sedici body musi byt rozlozene po panelu (nahodne shody byvaji v hlucku)
    double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
    for (int i : idx) { const auto& a = R[(size_t) m[(size_t) i].a]; x0 = juce::jmin (x0, (double) a.x); x1 = juce::jmax (x1, (double) a.x); y0 = juce::jmin (y0, (double) a.y); y1 = juce::jmax (y1, (double) a.y); }
    const double spreadX = (x1 - x0) / prof.w, spreadY = (y1 - y0) / prof.h;
    if (debug) std::cout << "  body: shod " << n << ", sedi " << idx.size() << " (podobnost " << bestIn << "), rozlozeni " << spreadX << " x " << spreadY << std::endl;
    if (spreadX < 0.35 || spreadY < 0.35) return juce::jmin ((int) idx.size(), 5);
    return (int) idx.size();
}

} // namespace

//==============================================================================
void Transform::map (double x, double y, double& ox, double& oy) const
{
    const double w = h[6] * x + h[7] * y + h[8];
    const double d = std::abs (w) > 1e-12 ? w : 1e-12;
    ox = (h[0] * x + h[1] * y + h[2]) / d;
    oy = (h[3] * x + h[4] * y + h[5]) / d;
}

void Transform::jacobian (double x, double y, double j[4]) const
{
    double x0, y0, x1, y1, x2, y2;
    map (x, y, x0, y0); map (x + 1.0, y, x1, y1); map (x, y + 1.0, x2, y2);
    j[0] = x1 - x0; j[1] = x2 - x0; j[2] = y1 - y0; j[3] = y2 - y0;
}

//==============================================================================
int exifOrientation (const juce::File& file)
{
    juce::FileInputStream in (file);
    if (! in.openedOk()) return 1;
    juce::MemoryBlock mb;
    in.readIntoMemoryBlock (mb, 256 * 1024);
    const auto* d = static_cast<const juce::uint8*> (mb.getData());
    const size_t n = mb.getSize();
    if (n < 12 || d[0] != 0xFF || d[1] != 0xD8) return 1;

    size_t pos = 2;
    while (pos + 4 < n)
    {
        if (d[pos] != 0xFF) return 1;
        const int marker = d[pos + 1];
        const size_t len = ((size_t) d[pos + 2] << 8) | d[pos + 3];
        if (marker == 0xE1 && pos + 10 < n && std::memcmp (d + pos + 4, "Exif\0\0", 6) == 0)
        {
            const size_t tiff = pos + 10;
            if (tiff + 8 > n) return 1;
            const bool le = d[tiff] == 'I';
            auto u16 = [&] (size_t o) -> unsigned { if (o + 2 > n) return 0u; return le ? (unsigned) (d[o] | (d[o + 1] << 8)) : (unsigned) ((d[o] << 8) | d[o + 1]); };
            auto u32 = [&] (size_t o) -> unsigned { if (o + 4 > n) return 0u; return le ? (unsigned) (d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | ((unsigned) d[o + 3] << 24))
                                                                                         : (unsigned) (((unsigned) d[o] << 24) | (d[o + 1] << 16) | (d[o + 2] << 8) | d[o + 3]); };
            const size_t ifd = tiff + u32 (tiff + 4);
            const unsigned count = u16 (ifd);
            for (unsigned i = 0; i < count && i < 200; ++i)
            {
                const size_t e = ifd + 2 + (size_t) i * 12;
                if (e + 12 > n) break;
                if (u16 (e) == 0x0112)
                {
                    const unsigned o = u16 (e + 8);
                    return (o >= 1 && o <= 8) ? (int) o : 1;
                }
            }
            return 1;
        }
        if (marker == 0xDA || len < 2) return 1;
        pos += 2 + len;
    }
    return 1;
}

juce::Image applyOrientation (const juce::Image& src, int o)
{
    if (o <= 1 || o > 8 || ! src.isValid()) return src;
    auto in = src.convertedToFormat (juce::Image::ARGB);
    const int w = in.getWidth(), h = in.getHeight();
    const bool swap = o >= 5;
    juce::Image out (juce::Image::ARGB, swap ? h : w, swap ? w : h, true);
    juce::Image::BitmapData s (in, juce::Image::BitmapData::readOnly), d (out, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            int dx = x, dy = y;
            switch (o)
            {
                case 2: dx = w - 1 - x; dy = y; break;
                case 3: dx = w - 1 - x; dy = h - 1 - y; break;
                case 4: dx = x; dy = h - 1 - y; break;
                case 5: dx = y; dy = x; break;
                case 6: dx = h - 1 - y; dy = x; break;              // otocit o 90 po smeru hodin
                case 7: dx = h - 1 - y; dy = w - 1 - x; break;
                case 8: dx = y; dy = w - 1 - x; break;              // o 90 proti smeru
                default: break;
            }
            d.setPixelColour (dx, dy, s.getPixelColour (x, y));
        }
    return out;
}

juce::Image loadPhoto (const juce::File& file, String& error)
{
    juce::File source = file;
    juce::TemporaryFile temp (".jpg");

    if (! file.hasFileExtension ("jpg;jpeg;png;gif"))
    {
       #if JUCE_MAC
        // HEIC / TIFF: prevod vestavenym nastrojem macOS, vse lokalne
        juce::ChildProcess proc;
        if (proc.start (juce::StringArray { "/usr/bin/sips", "-s", "format", "jpeg", "-s", "formatOptions", "92",
                                            file.getFullPathName(), "--out", temp.getFile().getFullPathName() })
            && proc.waitForProcessToFinish (60000) && temp.getFile().getSize() > 0)
            source = temp.getFile();
        else
       #endif
        { error = String::fromUTF8 ("formát nejde načíst (použij JPG)"); return {}; }
    }

    auto img = juce::ImageFileFormat::loadFrom (source);
    if (! img.isValid()) { error = String::fromUTF8 ("fotku nejde načíst"); return {}; }
    return applyOrientation (img, exifOrientation (source));
}

//==============================================================================
const char* const kAlgorithmVersion = "rmcv-0.11.0";

namespace { thread_local const std::atomic<bool>* tCancel = nullptr; }
CancelScope::CancelScope (const std::atomic<bool>* flag) : previous (tCancel)   { tCancel = flag; }
CancelScope::~CancelScope()                                                      { tCancel = previous; }
bool cancelled()                                                                 { return tCancel != nullptr && tCancel->load(); }

String fingerprint (const juce::File& f)
{
    if (! f.existsAsFile()) return {};
    return juce::MD5 (f).toHexString();
}

//==============================================================================
namespace
{
// Shoda jemneho potisku (pismena, stupnice): korelace "horni propusti" (jas minus rozmazany jas).
// Odlisi pristroje se stejnym sasi a rozmistenim knobu, ale jinym potiskem.
double detailMatch (const Plane& photo, const RefPanel& rp, const Transform& t)
{
    if (rp.gray.v.empty()) return 0;
    double j[4]; t.jacobian (rp.w * 0.5, rp.h * 0.5, j);
    const double k = std::sqrt (std::abs (j[0] * j[3] - j[1] * j[2]));
    if (k < 1e-6) return 0;
    // pracovni meritko: cca 1 px fotky, ale nejvys 2x zmenseny panel
    const double sp = juce::jmax (1.0, 1.0 / k);
    const double sigmaRef = 2.5 * sp;                  // px profilu
    Plane rb; gaussBlur (rp.gray, rb, sigmaRef);
    Plane pb; gaussBlur (photo, pb, sigmaRef * k);
    double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0; int n = 0;
    const double step = juce::jmax (sp, std::sqrt ((double) rp.w * rp.h / 80000.0));
    for (double y = 2; y < rp.h - 2; y += step)
        for (double x = 2; x < rp.w - 2; x += step)
        {
            if (rp.mask.bil (x, y) < 0.99f) continue;
            double px, py; t.map (x, y, px, py);
            if (px < 2 || py < 2 || px > photo.w - 3 || py > photo.h - 3) continue;
            const double a = rp.gray.bil (x, y) - rb.bil (x, y);
            const double b = photo.bil (px, py) - pb.bil (px, py);
            sa += a; sb += b; saa += a * a; sbb += b * b; sab += a * b; ++n;
        }
    if (n < 100) return 0;
    const double cov = sab / n - (sa / n) * (sb / n), va = saa / n - (sa / n) * (sa / n), vb = sbb / n - (sb / n) * (sb / n);
    return (va > 1e-12 && vb > 1e-12) ? cov / std::sqrt (va * vb) : 0.0;
}
}

namespace
{
// body referencniho panelu se pocitaji jen jednou za beh pluginu
// Vyrazne body fotky (sdilene mezi pristroji: cely rack se hleda na jedne fotce pro kazdy pristroj zvlast)
std::function<void()>& releaseKeypointsHook()   { static std::function<void()> f; return f; }

std::shared_ptr<const std::vector<KeyPt>> photoKeypoints (const Photo& ph, double fs, int maxPts)
{
    static std::mutex m;
    static std::map<std::string, std::shared_ptr<const std::vector<KeyPt>>> cache;
    static const bool registered = [] { releaseKeypointsHook() = [] { const std::lock_guard<std::mutex> l (m); cache.clear(); }; return true; }();
    juce::ignoreUnused (registered);
    // otisk fotky: rozmer + par pixelu
    double sig = 0;
    for (int i = 0; i < 64; ++i) sig += ph.gray.v[(size_t) ((i * 7919L) % (long) ph.gray.v.size())] * (i + 1);
    const std::string key = std::to_string (ph.gray.w) + "x" + std::to_string (ph.gray.h) + ":" + std::to_string (sig) + ":" + std::to_string (fs) + ":" + std::to_string (maxPts);
    {
        const std::lock_guard<std::mutex> l (m);
        auto it = cache.find (key);
        if (it != cache.end()) return it->second;
    }
    const Plane small = fs < 1.0 ? resampleBox (ph.gray, fs) : ph.gray;
    auto kps = detectKeypoints (small, nullptr, maxPts, std::getenv ("RM_CPH") ? (float) atof (std::getenv ("RM_CPH")) : 0.03f);
    for (auto& k : kps) { k.x = (float) ((k.x + 0.5) / fs - 0.5); k.y = (float) ((k.y + 0.5) / fs - 0.5); k.sigma = (float) (k.sigma / fs); }
    auto ptr = std::make_shared<const std::vector<KeyPt>> (std::move (kps));
    if (cancelled()) return ptr;                        // neuplny vysledek se nepamatuje
    const std::lock_guard<std::mutex> l (m);
    if (cache.size() > 6) cache.clear();
    cache[key] = ptr;
    return ptr;
}

const std::vector<KeyPt>& referenceKeypoints (const rm::DeviceProfile& prof, const RefPanel& rp)
{
    static std::mutex lock;
    static std::map<juce::String, std::vector<KeyPt>> cache;
    const auto key = prof.id + "/" + juce::String (prof.w) + "x" + juce::String (prof.h);
    {
        const std::lock_guard<std::mutex> g (lock);
        auto it = cache.find (key);
        if (it != cache.end()) return it->second;
    }
    // maska se trochu rozsiri, aby popis bodu nezasahoval do knobu
    Plane m = rp.mask;
    {
        Plane mb; gaussBlur (rp.mask, mb, std::getenv ("RM_MB") ? atof (std::getenv ("RM_MB")) : 3.0);
        for (size_t i = 0; i < m.v.size(); ++i) m.v[i] = mb.v[i] > 0.995f ? 1.0f : 0.0f;
    }
    const double up = std::getenv ("RM_UP") ? atof (std::getenv ("RM_UP")) : (juce::jmin (rp.w, rp.h) < 400 ? 2.0 : 1.0);   // nizky 1U panel: drobny potisk -> jemnejsi meritko
    const float cref = std::getenv ("RM_CREF") ? (float) atof (std::getenv ("RM_CREF")) : 0.03f;
    std::vector<KeyPt> k;
    if (up > 1.01)
    {
        const Plane g2 = resampleBox (rp.gray, up), m2 = resampleBox (m, up);
        k = detectKeypoints (g2, &m2, 6000, cref);
        for (auto& p : k) { p.x = (float) ((p.x + 0.5) / up - 0.5); p.y = (float) ((p.y + 0.5) / up - 0.5); p.sigma = (float) (p.sigma / up); }
    }
    else k = detectKeypoints (rp.gray, &m, 6000, cref);

    const std::lock_guard<std::mutex> g (lock);
    auto it = cache.find (key);
    if (it != cache.end()) return it->second;       // mezitim spocital jiny thread
    if (cancelled()) { static std::vector<KeyPt> none; return none; }   // neuplny vysledek se nepamatuje
    return cache[key] = std::move (k);
}
}

namespace
{
// Pracovni kopie fotky (barevne roviny v pracovnim rozliseni) se pro vic pristroju z jedne fotky pocita jen jednou.
std::mutex photoLock;
// Drzi odkaz na zdrojovy obrazek: dokud zije, jeho data nemohou byt uvolnena a adresa pouzita jinym obrazkem
// (porovnani jen podle adresy by po uvolneni vratilo starou fotku - chyba nalezena testem).
struct PhotoEntry { juce::Image source; int maxSide = 0; std::shared_ptr<const Photo> photo; } lastPhoto;
std::shared_ptr<const Photo> sharedPhoto (const juce::Image& image, int maxSide)
{
    {
        const std::lock_guard<std::mutex> l (photoLock);
        if (lastPhoto.photo != nullptr && lastPhoto.source.isValid() && image.isValid()
            && lastPhoto.source.getPixelData() == image.getPixelData() && lastPhoto.maxSide == maxSide)
            return lastPhoto.photo;
    }
    auto p = std::make_shared<const Photo> (toPhoto (image, maxSide));
    const std::lock_guard<std::mutex> l (photoLock);
    lastPhoto = { image, maxSide, p };
    return p;
}
}

void releaseWorkingMemory()
{
    {
        const std::lock_guard<std::mutex> l (photoLock);
        lastPhoto = {};
    }
    if (releaseKeypointsHook()) releaseKeypointsHook()();
}

Transform fromCorners (const rm::DeviceProfile& prof, const juce::Point<float> corners[4])
{
    std::vector<cd> p { cd (0, 0), cd (prof.w, 0), cd (prof.w, prof.h), cd (0, prof.h) }, q;
    for (int i = 0; i < 4; ++i) q.push_back (cd (corners[i].x, corners[i].y));
    Transform t;
    if (! fitHomography (p, q, t)) return {};
    return t;
}

namespace
{
Result recognizeImpl (const juce::Image& image, const rm::DeviceProfile& prof, const juce::Image& reference, const Transform* manual)
{
    Result res;
    for (auto& c : prof.controls)
    {
        if (c.read.isNotEmpty()) ++res.readable;
        if (c.anchorR > 0) ++res.anchorsTotal;
    }
    const bool debug = std::getenv ("RM_DEBUG") != nullptr;
    if (! image.isValid())                        { res.message = String::fromUTF8 ("fotku nejde načíst"); return res; }
    if (res.readable == 0 || ! reference.isValid()) { res.message = String::fromUTF8 ("čtení z fotky tu zatím není"); return res; }

    const auto photoPtr = sharedPhoto (image, std::getenv ("RM_MAXSIDE") ? atoi (std::getenv ("RM_MAXSIDE")) : 3400);
    const Photo& ph = *photoPtr;
    const RefPanel rp = makeRefPanel (prof, reference);

    // ---- 1) kandidati polohy: (a) podle vyraznych bodu potisku, (b) podle knobu
    std::vector<Transform> hyps;
    int siftInliers = 0;
    if (manual != nullptr)
    {
        // rucni zarovnani (uzivatel oznacil 4 rohy panelu): bez hledani, jen se presne doladi potiskem
        Transform t = *manual;
        if (ph.scale != 1.0) for (int i = 0; i < 6; ++i) t.h[i] *= ph.scale;      // fotka se pro vypocet zmensuje
        hyps.push_back (t);
    }
    else
    {
        const auto& refKps = referenceKeypoints (prof, rp);
        // Vyrazne body fotky: nejdriv v nizsim rozliseni (rychle, staci na fotku jednoho pristroje); kdyz pristroj
        // nevyjde a fotka je vetsi (cely rack z telefonu = maly pristroj na fotce), znovu ve vyssim rozliseni.
        const int longSide = juce::jmax (ph.gray.w, ph.gray.h);
        std::vector<double> sizes { 1600.0 };
        if (std::getenv ("RM_SIFT") != nullptr) sizes = { atof (std::getenv ("RM_SIFT")) };
        else if (longSide > 1900) sizes.push_back (juce::jmin (2800.0, (double) longSide));
        Transform best;
        for (const double size : sizes)
        {
            const double fs = juce::jmin (1.0, size / longSide);
            const auto kps = photoKeypoints (ph, fs, size > 2000 ? 12000 : 5000);
            const auto matches = matchKeypoints (refKps, *kps, 0.8f);
            if (debug) std::cout << "  body: reference " << refKps.size() << ", fotka " << kps->size() << " (" << size << " px)" << std::endl;
            Transform t;
            const int in = ransacPanel (refKps, *kps, matches, prof, ph.gray.w, ph.gray.h, t, debug);
            if (in > siftInliers) { siftInliers = in; best = t; }
            if (siftInliers >= 25 || cancelled()) break;
        }
        if (siftInliers >= 8) hyps.push_back (best);
    }

    std::vector<Anchor> anchors;
    for (auto& c : prof.controls)
        if (c.anchorR > 0) anchors.push_back ({ &c, cd (c.x, c.y), cd (0, 0) });
    const int N = (int) anchors.size();
    const Kernel kernel = makeKernel (prof.anchor.isEmpty() ? String ("disc") : prof.anchor);

    if (manual == nullptr && N >= 4 && prof.anchor.isNotEmpty() && siftInliers < 25)
    {
        const int factor = juce::jmax (1, (int) std::ceil (juce::jmax (ph.gray.w, ph.gray.h) / 600.0));
        const Plane work = shrink (ph.gray, factor);
        const double anchorR = anchors.front().c->anchorR;
        // dvojice kotev pouzite pro hypotezy (co nejdal od sebe)
        struct Pair { int i, j; double d; };
        std::vector<Pair> pairs;
        for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j) if (i != j) pairs.push_back ({ i, j, std::abs (anchors[(size_t) i].p - anchors[(size_t) j].p) });
        std::sort (pairs.begin(), pairs.end(), [] (const Pair& a, const Pair& b) { return a.d > b.d; });
        if (pairs.size() > 10) pairs.resize (10);

        // ---- 1) hruba poloha: zkousi se nekolik meritek (pristroj pres celou fotku az cca tretina fotky)
        const double fit = juce::jmin ((double) work.w / prof.w, (double) work.h / prof.h);
        const double fill = juce::jmax ((double) work.w / prof.w, (double) work.h / prof.h);
        struct Cand { Similarity s; int in; double res; };
        std::vector<Cand> cands;
        int bestIn = 0;
        const int need = juce::jmax (4, (int) std::ceil (0.6 * N));

        for (double sc = juce::jmin (fill, fit * 1.6) * 1.06; sc > fit * 0.26 && ! cancelled(); sc *= 0.86)
        {
            const double rho = anchorR * sc;
            if (rho < 3.0) break;
            const auto peaks = findPeaks (work, rho, kernel);
            if (std::getenv ("RM_DEBUG_PEAKS") != nullptr)
            {
                std::cout << "sc " << sc << " rho " << rho << " peaks " << peaks.size() << ":";
                for (size_t i = 0; i < juce::jmin ((size_t) 14, peaks.size()); ++i) std::cout << " (" << (int) peaks[i].x << "," << (int) peaks[i].y << " " << peaks[i].r << ")";
                std::cout << std::endl;
            }
            if ((int) peaks.size() < 3) continue;

            for (auto& pr : pairs)
            {
                const cd pi = anchors[(size_t) pr.i].p, pj = anchors[(size_t) pr.j].p;
                for (size_t a = 0; a < peaks.size(); ++a)
                    for (size_t b = 0; b < peaks.size(); ++b)
                    {
                        if (a == b) continue;
                        const cd qa (peaks[a].x, peaks[a].y), qb (peaks[b].x, peaks[b].y);
                        const double ratio = std::abs (qb - qa) / (pr.d * sc);
                        if (ratio < 0.84 || ratio > 1.19) continue;
                        Similarity s; s.a = (qb - qa) / (pj - pi); s.b = qa - s.a * pi;
                        if (std::abs (std::arg (s.a)) > 28.0 * kPi / 180.0) continue;

                        int in = 0; double rs = 0;
                        const double tol = 0.55 * anchorR * std::abs (s.a);
                        for (auto& an : anchors)
                        {
                            const cd q = s.map (an.p);
                            double dmin = 1e9;
                            for (auto& pk : peaks) dmin = juce::jmin (dmin, std::abs (cd (pk.x, pk.y) - q));
                            if (dmin < tol) { ++in; rs += dmin / tol; }
                        }
                        bestIn = juce::jmax (bestIn, in);
                        if (in < need) continue;
                        const double meanRes = rs / in;

                        // stejna hypoteza uz v seznamu?
                        bool dup = false;
                        for (auto& c : cands)
                            if (std::abs (c.s.b - s.b) < 0.4 * rho && std::abs (c.s.a - s.a) < 0.04 * std::abs (s.a))
                            {
                                dup = true;
                                if (in > c.in || (in == c.in && meanRes < c.res)) c = { s, in, meanRes };
                                break;
                            }
                        if (! dup) cands.push_back ({ s, in, meanRes });
                    }
            }
        }


        if (! cands.empty())
        {
            std::sort (cands.begin(), cands.end(), [] (const Cand& a, const Cand& b) { return a.in != b.in ? a.in > b.in : a.res < b.res; });
            if (cands.size() > 30) cands.resize (30);
            // Vyber hypotezy: krome knobu musi sedet i potisk panelu (porovnani s obrazkem profilu mimo knoby).
            // Bez toho se rada stejne vzdalenych knobu (SPL) snadno chyti o jeden knob vedle.
            struct RefPoint { double x, y; float v; };
            std::vector<RefPoint> refPts;
            if (reference.isValid())
            {
                const int gw = prof.w >= prof.h ? 420 : juce::jmax (40, (int) (260 * prof.w / prof.h));
                const int gh = prof.w >= prof.h ? juce::jmax (16, (int) (420 * prof.h / prof.w)) : 260;
                auto small = reference.rescaled (gw, gh, juce::Graphics::mediumResamplingQuality).convertedToFormat (juce::Image::ARGB);
                juce::Image::BitmapData bd (small, juce::Image::BitmapData::readOnly);
                for (int y = 0; y < gh; ++y)
                    for (int x = 0; x < gw; ++x)
                    {
                        const double px = (x + 0.5) * prof.w / gw, py = (y + 0.5) * prof.h / gh;
                        bool masked = false;
                        for (auto& c : prof.controls)
                            if ((c.type == ControlType::knob || c.type == ControlType::stepped)
                                && std::hypot (c.x - px, c.y - py) < (prof.anchor == "ring" ? 1.65 * c.anchorR : 1.12 * juce::jmax (c.r, c.anchorR * 0.8f)))   // u rady stejnych knobu nesmi rozhodovat jejich (periodicke) stupnice
                                { masked = true; break; }
                        if (! masked) refPts.push_back ({ px, py, bd.getPixelColour (x, y).getBrightness() });
                    }
            }
            auto printMatch = [&] (const Similarity& s) -> double
            {
                if (refPts.size() < 50) return 0.0;
                double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0; int n = 0;
                for (auto& rp : refPts)
                {
                    const cd q = s.map (cd (rp.x, rp.y));
                    if (q.real() < 0 || q.imag() < 0 || q.real() > work.w - 1 || q.imag() > work.h - 1) { sa += rp.v; saa += rp.v * rp.v; ++n; continue; }   // mimo fotku = cerna
                    const double v = work.bil (q.real(), q.imag());
                    sa += rp.v; sb += v; saa += rp.v * rp.v; sbb += v * v; sab += rp.v * v; ++n;
                }
                const double cov = sab / n - (sa / n) * (sb / n), va = saa / n - (sa / n) * (sa / n), vb = sbb / n - (sb / n) * (sb / n);
                return (va > 1e-9 && vb > 1e-9) ? cov / std::sqrt (va * vb) : 0.0;
            };

            // predbezne poradi hypotez (levne), nejlepsich par se pak zpresni a rozhodne potisk v plnem rozliseni
            struct Scored { Similarity s; double score; int in; };
            std::vector<Scored> order;
            for (auto& c : cands)
            {
                double resp = 0;
                for (auto& an : anchors) { const cd q = c.s.map (an.p); resp += juce::jmax (0.0f, response (work, q.real(), q.imag(), anchorR * std::abs (c.s.a), kernel)); }
                order.push_back ({ c.s, (double) c.in / N + 3.0 * printMatch (c.s) + 2.0 * resp / N - 0.1 * c.res, c.in });
            }
            std::sort (order.begin(), order.end(), [] (const Scored& a, const Scored& b) { return a.score > b.score; });
            if (order.size() > 12) order.resize (12);


            for (size_t i = 0; i < order.size() && i < 4; ++i)
            {
                Similarity sim; sim.a = order[i].s.a * (double) factor; sim.b = order[i].s.b * (double) factor;
                hyps.push_back (fromSimilarity (sim));
            }
        }
    }

    if (cancelled()) return res;
    // ---- 2) doladeni kazdeho kandidata podle potisku, vyhrava nejlepsi shoda
    Transform xp, siftT; double best = -2, siftNcc = -2; bool bestIsSift = false;
    for (size_t hi = 0; hi < hyps.size(); ++hi)
    {
        double ncc = -1;
        Transform t = refinePrint (ph.gray, rp, hyps[hi], ncc);
        if (manual != nullptr)
        {
            // doladeni nesmi rucni rohy odvest jinam (max 4 % uhlopricky panelu)
            double worst = 0;
            for (auto c : { cd (0, 0), cd (prof.w, 0), cd (prof.w, prof.h), cd (0, prof.h) })
            {
                double ax, ay, bx, by; hyps[hi].map (c.real(), c.imag(), ax, ay); t.map (c.real(), c.imag(), bx, by);
                worst = juce::jmax (worst, std::hypot (ax - bx, ay - by));
            }
            double j[4]; hyps[hi].jacobian (prof.w * 0.5, prof.h * 0.5, j);
            if (worst > 0.04 * std::hypot (prof.w, prof.h) * std::sqrt (std::abs (j[0] * j[3] - j[1] * j[2]))) t = hyps[hi];
        }
        if (debug) std::cout << "  kandidat " << hi << " -> potisk " << ncc << std::endl;
        if (ncc > best) { best = ncc; xp = t; bestIsSift = siftInliers >= 8 && hi == 0; }
        if (hi == 0 && siftInliers >= 8 && manual == nullptr) { siftT = t; siftNcc = ncc; }
    }
    // geometricky overena shoda vyraznych bodu ma prednost pred o chlup lepsim jasem jine hypotezy
    if (! bestIsSift && siftInliers >= kMinInliers && siftNcc >= 0.12 && siftNcc >= best - 0.05)
    { xp = siftT; best = siftNcc; bestIsSift = true; }
    res.printMatch = best;
    res.keypoints = siftInliers;
    const double detail = best > -1 ? detailMatch (ph.gray, rp, xp) : 0.0;
    if (debug) std::cout << "  jemny potisk " << detail << std::endl;
    // prijeti: hodne sedicich bodu potisku, nebo dobra shoda jasu I jemneho potisku (pismena)
    const bool accepted = manual != nullptr
                       || (bestIsSift && siftInliers >= kMinInliers && best >= 0.12)
                       || (best >= kMinPrintMatch && detail >= kMinDetail);
    if (! accepted && std::getenv ("RM_FORCE") == nullptr)
    {
        res.message = String::fromUTF8 ("přístroj nenalezen – foť zepředu, celý, ostře");
        return res;
    }

    res.aligned = true;
    res.transform = xp;
    res.photoScale = ph.scale;

    // ---- knoby vycnivaji z panelu: jejich skutecny stred na fotce se dohleda v okoli (paralaxa).
    // Vysoke knoby na fotce shora jsou posunute i o vic nez polomer -> nejdriv hrube ve velkem okoli,
    // pak spolecny posun (median), pak jemne kolem nej (jeden spatny knob nestrhne ostatni).
    Transform xf = xp;
    res.anchorsFound = 0;
    std::map<const rm::ControlDef*, cd> knobCentre;
    auto searchAnchor = [&] (Anchor& an, double ox, double oy, double win)
    {
        double gx, gy; xp.map (an.p.real(), an.p.imag(), gx, gy);
        double j[4]; xp.jacobian (an.p.real(), an.p.imag(), j);
        const double rho = an.c->anchorR * std::sqrt (std::abs (j[0] * j[3] - j[1] * j[2]));
        if (prof.anchor.isEmpty() || rho < 4) return;
        double bx = gx + ox * rho, by = gy + oy * rho; float br = -1e9f;
        for (double step : { juce::jmax (1.0, rho / 10.0), juce::jmax (0.5, rho / 40.0) })
        {
            const double x0 = bx, y0 = by, w = step > rho / 20.0 ? win * rho : step * 5.0;
            for (double dy = -w; dy <= w; dy += step)
                for (double dx = -w; dx <= w; dx += step)
                {
                    const float r = responseNorm (ph.gray, x0 + dx, y0 + dy, rho, kernel);
                    if (r > br) { br = r; bx = x0 + dx; by = y0 + dy; }
                }
        }
        an.q = cd (bx, by); an.resp = br;
        an.found = br > 0.25f;
        an.rho = rho; an.off = cd ((bx - gx) / rho, (by - gy) / rho);
    };
    // Nejdriv hrube ve velkem okoli (vysoke knoby na fotce shora) -> spolecny posun knobu (median). Pak dve presna
    // hledani: kolem polohy z potisku (jako 0.9) a kolem spolecneho posunu; u kazdeho knobu vyhraje silnejsi odezva.
    // (Jen posunute hledani bylo u fotek zepredu horsi: regresni sada xmax / xfilter 500 o 3 / 2 ovladace.)
    double medX = 0, medY = 0; bool haveMedian = false;
    {
        for (auto& an : anchors) searchAnchor (an, 0, 0, 1.15);
        std::vector<double> ox, oy;
        for (auto& an : anchors) if (an.found && an.resp > 0.35f) { ox.push_back (an.off.real()); oy.push_back (an.off.imag()); }
        if (ox.size() >= 3)
        {
            std::nth_element (ox.begin(), ox.begin() + (long) ox.size() / 2, ox.end());
            std::nth_element (oy.begin(), oy.begin() + (long) oy.size() / 2, oy.end());
            medX = ox[ox.size() / 2]; medY = oy[oy.size() / 2];
            haveMedian = true;
        }
    }
    // Fotka zepredu (spolecny posun < 0.2 R): presne hledani kolem polohy z potisku presne jako ve 0.9 (regresni sada:
    // posunute hledani bylo u fotek zepredu horsi - xmax -3, xfilter 500 -2 ovladace). Fotka shora / zboku (vysoke
    // knoby, posun >= 0.2 R): hledani kolem spolecneho posunu (skutecna fotka racku 0.10).
    const bool shifted = haveMedian && std::hypot (medX, medY) >= 0.2;
    for (auto& an : anchors)
    {
        if (shifted) searchAnchor (an, medX, medY, 0.5);
        else         searchAnchor (an, 0, 0, 0.45);
    }
    if (debug) std::cout << "  posun knobu (paralaxa): " << medX << ", " << medY << " R" << std::endl;
    for (auto& an : anchors) if (an.found) { knobCentre[an.c] = an.q; ++res.anchorsFound; }
    {
        double ox = 0, oy = 0; int n = 0;
        for (auto& an : anchors) if (an.found) { double gx, gy; xp.map (an.p.real(), an.p.imag(), gx, gy); ox += an.q.real() - gx; oy += an.q.imag() - gy; ++n; }
        double j[4]; xp.jacobian (prof.w * 0.5, prof.h * 0.5, j);
        const double k = std::sqrt (std::abs (j[0] * j[3] - j[1] * j[2]));
        if (n > 0 && N > 0) res.parallax = std::hypot (ox / n, oy / n) / juce::jmax (1e-6, k * anchors.front().c->anchorR);
    }

    // Fotka moc ze strany: nestejne meritko ve dvou smerech (v rovine panelu)
    {
        double j[4]; xp.jacobian (prof.w * 0.5, prof.h * 0.5, j);
        const double a = j[0] * j[0] + j[2] * j[2], b2 = j[0] * j[1] + j[2] * j[3], d = j[1] * j[1] + j[3] * j[3];
        const double tr = a + d, det = a * d - b2 * b2;
        const double s1 = std::sqrt (juce::jmax (1e-12, tr / 2 + std::sqrt (juce::jmax (0.0, tr * tr / 4 - det))));
        const double s2 = std::sqrt (juce::jmax (1e-12, tr / 2 - std::sqrt (juce::jmax (0.0, tr * tr / 4 - det))));
        res.skew = s1 / s2;
    }
    const bool tooOblique = res.skew > kMaxSkew;

    if (cancelled()) return res;
    // ---- 3) cteni ovladacu
    // LED: nejdriv svitivost vsech LED pristroje, prah je spolecny (expozice fotky)
    std::map<const rm::ControlDef*, std::vector<double>> leds;
    std::vector<double> allLeds;
    for (auto& c : prof.controls)
        if (c.read == "led" || c.read == "ledsel" || c.read == "ledring")
            for (size_t i = 0; i < c.draw.points.size(); ++i) { leds[&c].push_back (ledLevel (ph, c, xp, i)); allLeds.push_back (leds[&c].back()); }
    const auto ledThr = ledThreshold (allLeds);
    // rysky (SPL): kandidati u vsech knobu, spolecny posun cepicek (paralaxa), pak u kazdeho knobu ryska,
    // jejiz osa miri do stredu cepicky (odlesky na sukni a cisla stupnice tim neprojdou)
    std::map<const rm::ControlDef*, LineSeg> segs;
    double plx = 0, ply = 0; bool haveParallax = false;
    {
        std::map<const rm::ControlDef*, std::vector<LineSeg>> cands;
        for (auto& c : prof.controls)
            if (c.read == "line")
            {
                Frame f; xf.map (c.x, c.y, f.cx, f.cy); xf.jacobian (c.x, c.y, f.j);
                cands[&c] = findLineSegs (ph, c, f);
                segs[&c] = cands[&c].empty() ? LineSeg() : cands[&c].front();
            }
        for (int it = 0; it < 3; ++it)
        {
            std::vector<LineSeg> all;
            for (auto& kv : segs) all.push_back (kv.second);
            if (! solveParallax (all, plx, ply)) break;
            haveParallax = true;
            for (auto& kv : cands)
            {
                double bestR = 1e9;
                for (auto& cd2 : kv.second)
                {
                    const double r = axisResidual (cd2, plx, ply) - 0.05 * cd2.score / cd2.R;
                    if (r < bestR) { bestR = r; segs[kv.first] = cd2; }
                }
            }
        }
        if (debug)
        {
            std::cout << "  rysky: posun cepicek " << (haveParallax ? juce::String (plx, 2) + ", " + juce::String (ply, 2) : juce::String ("?")) << " R" << std::endl;
            for (auto& kv : segs) { auto& g = kv.second; std::cout << "  ryska " << kv.first->id << " ok=" << g.ok << " M=" << g.mx / g.R << "," << g.my / g.R << " osa " << g.ax << "," << g.ay << " delka " << (g.hi - g.lo) / g.R << " el " << g.elong << " res " << axisResidual (g, plx, ply) << std::endl; }
        }
    }
    for (auto& c : prof.controls)
    {
        if (c.read.isEmpty()) { res.states[c.id] = "unsupported"; continue; }
        rm::ControlValue v;
        v.source = "recognized";
        if (tooOblique) { res.values[c.id] = v; res.states[c.id] = "uncertain"; continue; }
        {
            // ovladac mimo fotku / prilis maly na cteni (cely rack z dalky): neprecte se, nic se nevymysli
            double px, py; xp.map (c.x, c.y, px, py);
            double j[4]; xp.jacobian (c.x, c.y, j);
            const double rpx = c.r * std::sqrt (std::abs (j[0] * j[3] - j[1] * j[2])) / ph.scale;
            if (px < 0 || py < 0 || px >= ph.gray.w || py >= ph.gray.h) { res.values[c.id] = v; res.states[c.id] = "notvisible"; continue; }
            const bool rotary = c.type == ControlType::knob || c.type == ControlType::stepped;
            if (rotary && c.read != "ledring" && rpx < 9.0) { res.values[c.id] = v; res.states[c.id] = "small"; ++res.tooSmall; continue; }
        }

        if (c.read == "notch" || c.read == "line")
        {
            Frame f;
            xf.map (c.x, c.y, f.cx, f.cy);
            xf.jacobian (c.x, c.y, f.j);
            for (auto& an : anchors)
                if (an.c == &c && an.found && prof.anchor == "disc") { f.cx = an.q.real(); f.cy = an.q.imag(); }   // skutecny stred knobu (paralaxa)
            if (c.read == "notch") v = readNotch (ph, c, f);
            else if (haveParallax)  v = lineValue (c, segs[&c], plx, ply);
            else                    v = readLine (ph, c, f);
        }
        else if (c.read == "led")       v = leds[&c].empty() ? rm::ControlValue() : ledOnOff (leds[&c][0], ledThr);
        else if (c.read == "ringglow")  v = readRingGlow (ph, c, xp);
        else if (c.read == "rectglow")  v = readRectGlow (ph, c, xp);
        else if (c.read == "ledring")   v = readLedRing (leds[&c], c, ledThr);
        else if (c.read == "ledsel")    v = readLedSelect (leds[&c], c, ledThr);

        v.source = "recognized";
        res.values[c.id] = v;
        res.states[c.id] = v.known ? "recognized" : "uncertain";
        if (v.known) ++res.read;
    }

    // ---- 4) fotka narovnana do souradnic panelu (pro kontrolu okem)
    {
        const double k = juce::jmin (1.0, 1000.0 / juce::jmax (prof.w, prof.h));
        const int w = juce::jmax (1, juce::roundToInt (prof.w * k)), h = juce::jmax (1, juce::roundToInt (prof.h * k));
        juce::Image out (juce::Image::RGB, w, h, false);
        juce::Image::BitmapData bd (out, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                double sx, sy; xp.map ((x + 0.5) / k, (y + 0.5) / k, sx, sy);
                if (sx < 0 || sy < 0 || sx > ph.gray.w - 1 || sy > ph.gray.h - 1) { bd.setPixelColour (x, y, juce::Colour (0xff101010)); continue; }
                bd.setPixelColour (x, y, juce::Colour::fromFloatRGBA (ph.r.bil (sx, sy), ph.g.bil (sx, sy), ph.b.bil (sx, sy), 1.0f));
            }
        res.rectified = out;
    }

    res.message = tooOblique ? String::fromUTF8 ("moc ze strany – foť kolmo zepředu")
                : res.tooSmall > 0 && res.read == 0 ? String::fromUTF8 ("na fotce moc malý – vyfoť ho zblízka")
                : String (res.read) + "/" + String (res.readable) + (res.tooSmall > 0 ? String::fromUTF8 (" (část moc malá – vyfoť zblízka)") : String());
    return res;
}
}   // namespace

Result recognize (const juce::Image& image, const rm::DeviceProfile& prof, const juce::Image& reference, const Transform* manual)
{
    auto res = recognizeImpl (image, prof, reference, manual);
    res.algorithm = kAlgorithmVersion;
    if (cancelled())
    {
        Result c;
        c.cancelled = true;
        c.algorithm = kAlgorithmVersion;
        c.message = String::fromUTF8 ("analýza zrušena");
        return c;
    }
    return res;
}

namespace
{
struct CacheEntry { String key; Result result; };
std::mutex cacheLock;
std::deque<CacheEntry> resultCache;          // nejnovejsi na konci
CacheStats stats;
}

Result recognizeCached (const juce::Image& image, const String& photoFingerprint, const rm::DeviceProfile& prof, const juce::Image& reference)
{
    if (photoFingerprint.isEmpty()) return recognize (image, prof, reference);
    // definice ovladacu jsou soucasti klice: zmena profilu (oprava rozmisteni) = nova analyza
    const auto profileHash = String::toHexString (juce::JSON::toString (rm::profileToVar (prof), true).hashCode64());
    const auto key = photoFingerprint + "|" + prof.id + "|" + profileHash + "|" + String (reference.getWidth()) + "x" + String (reference.getHeight()) + "|" + kAlgorithmVersion;
    {
        const std::lock_guard<std::mutex> l (cacheLock);
        for (auto& e : resultCache)
            if (e.key == key) { ++stats.hits; auto r = e.result; r.fromCache = true; return r; }
        ++stats.misses;
    }
    auto r = recognize (image, prof, reference);
    if (r.cancelled) return r;
    const std::lock_guard<std::mutex> l (cacheLock);
    resultCache.push_back ({ key, r });
    while (resultCache.size() > 32) resultCache.pop_front();
    return r;
}

CacheStats cacheStats()
{
    const std::lock_guard<std::mutex> l (cacheLock);
    auto s = stats; s.size = (int) resultCache.size();
    return s;
}

void clearCache()
{
    const std::lock_guard<std::mutex> l (cacheLock);
    resultCache.clear(); stats = {};
}

std::vector<bool> chooseDevices (const std::vector<Result>& results, const std::vector<const rm::DeviceProfile*>& profiles)
{
    // obdelnik panelu na fotce (pracovni rozliseni se u vsech pristroju jedne fotky muze lisit -> prepocet na puvodni px)
    auto box = [&] (size_t i)
    {
        const auto& r = results[i];
        const double w = i < profiles.size() && profiles[i] != nullptr ? profiles[i]->w : 1;
        const double h = i < profiles.size() && profiles[i] != nullptr ? profiles[i]->h : 1;
        double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;      // (Rectangle::getUnion bodu nulove velikosti nefunguje)
        for (auto c : { std::pair<double, double> (0, 0), { w, 0 }, { w, h }, { 0, h } })
        {
            double x, y; r.transform.map (c.first, c.second, x, y);
            x /= r.photoScale; y /= r.photoScale;
            x0 = juce::jmin (x0, x); y0 = juce::jmin (y0, y); x1 = juce::jmax (x1, x); y1 = juce::jmax (y1, y);
        }
        return juce::Rectangle<double> (x0, y0, x1 - x0, y1 - y0);
    };
    std::vector<bool> take (results.size(), false);
    for (size_t i = 0; i < results.size(); ++i) take[i] = results[i].aligned && ! results[i].cancelled;
    for (size_t i = 0; i < results.size(); ++i)
        for (size_t j = i + 1; j < results.size(); ++j)
        {
            if (! take[i] || ! take[j]) continue;
            const auto a = box (i), b = box (j);
            const auto inter = a.getIntersection (b);
            const double ia = inter.getWidth() * inter.getHeight();
            const double smaller = juce::jmin (a.getWidth() * a.getHeight(), b.getWidth() * b.getHeight());
            if (smaller <= 0 || ia / smaller < 0.5) continue;       // sousedni pristroje se neprekryvaji
            const auto& ri = results[i]; const auto& rj = results[j];
            const bool iWins = ri.keypoints != rj.keypoints ? ri.keypoints > rj.keypoints : ri.read >= rj.read;
            (iWins ? take[j] : take[i]) = false;
        }
    return take;
}


// Ladici pomucka: najde svetle knoby dane velikosti a precte u nich zarez (bez profilu).
juce::String debugNotch (const juce::Image& image, double rho)
{
    const Photo ph = toPhoto (image, 4000);
    const Kernel k = makeKernel ("disc");
    auto peaks = findPeaks (ph.gray, rho, k);
    std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.x < b.x; });
    juce::String out;
    for (auto& p : peaks)
    {
        if (p.r < 0.45f) continue;
        const Cap cap = fitCap (ph.gray, p.x, p.y, rho);
        double angle = 0, conf = 0;
        if (cap.ok) findNotch (ph.gray, cap, angle, conf);
        out << juce::String (p.x, 0) << "," << juce::String (p.y, 0) << "  celo " << (cap.ok ? juce::String (cap.cx, 1) + "," + juce::String (cap.cy, 1) + " r " + juce::String (cap.r, 1) : juce::String ("-"))
            << "  uhel " << juce::String (wrapDeg (angle), 1) << "  conf " << juce::String (conf, 2) << "\n";
    }
    return out;
}

} // namespace rmcv
