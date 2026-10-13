// Mereni urovni (LUFS podle ITU-R BS.1770: K-vazeni, 400ms bloky, integrovana hlasitost s hradly) + spicka.
// Audio vlakno jen zapisuje do atomickych promennych, zadne zamky ani alokace.
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>
#include <cmath>

namespace rm
{
class LoudnessMeter
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate > 0 ? sampleRate : 48000.0;
        channels = juce::jlimit (1, 2, numChannels);
        // 1. stupen: shelf (+4 dB nad cca 1.5 kHz)
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / sr);
            const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            s1 = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                   2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        // 2. stupen: horni propust (RLB)
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / sr);
            const double d = 1.0 + K / Q + K * K;
            s2 = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / d, (1.0 - K / Q + K * K) / d };
        }
        for (auto& z : state) z = {};
        blockLen = juce::jmax (1, (int) std::lround (sr * 0.1));    // 100 ms; okno 400 ms = 4 bloky
        blockPos = 0; blockSum = 0; ring = {}; ringFill = 0;
        peakAcc = 0;
        reset();
    }

    void reset()    { resetRequested.store (true); }

    template <typename T>
    void process (const T* const* data, int numChannels, int numSamples)
    {
        if (resetRequested.exchange (false))
        {
            for (auto& h : hist) h.store (0, std::memory_order_relaxed);
            ringFill = 0; blockPos = 0; blockSum = 0;
            peakHold.store (-100.0f);
        }
        const int nch = juce::jmin (numChannels, channels);
        for (int i = 0; i < numSamples; ++i)
        {
            double e = 0;
            for (int c = 0; c < nch; ++c)
            {
                const double x = (double) data[c][i];
                peakAcc = juce::jmax (peakAcc, std::abs (x));
                const double y = filter (filter (x, s1, state[(size_t) c * 2]), s2, state[(size_t) c * 2 + 1]);
                e += y * y;
            }
            blockSum += e;
            if (++blockPos >= blockLen)
            {
                ring[(size_t) (ringFill % 4)] = blockSum / blockLen;
                ++ringFill;
                blockPos = 0; blockSum = 0;
                if (ringFill >= 4)
                {
                    const double ms = (ring[0] + ring[1] + ring[2] + ring[3]) / 4.0;
                    const float l = toLufs (ms);
                    momentary.store (l, std::memory_order_relaxed);
                    if (l > -70.0f)            // absolutni hradlo
                    {
                        const int bin = juce::jlimit (0, kBins - 1, (int) std::lround ((l + 70.0f) * 10.0f));
                        hist[(size_t) bin].fetch_add (1, std::memory_order_relaxed);
                    }
                }
                const float p = peakAcc > 1e-9 ? (float) (20.0 * std::log10 (peakAcc)) : -100.0f;
                peak.store (p, std::memory_order_relaxed);
                if (p > peakHold.load (std::memory_order_relaxed)) peakHold.store (p, std::memory_order_relaxed);
                peakAcc = 0;
            }
        }
    }

    float momentaryLufs() const   { return momentary.load (std::memory_order_relaxed); }
    float peakDb() const          { return peak.load (std::memory_order_relaxed); }
    float maxPeakDb() const       { return peakHold.load (std::memory_order_relaxed); }

    // integrovana hlasitost od posledniho resetu (absolutni hradlo -70, relativni -10 LU); < -69 = zatim nic
    float integratedLufs (double* seconds = nullptr) const
    {
        std::array<long long, kBins> h {};
        long long n = 0; double sum = 0;
        for (int b = 0; b < kBins; ++b) { h[(size_t) b] = hist[(size_t) b].load (std::memory_order_relaxed); n += h[(size_t) b]; sum += h[(size_t) b] * energyOf (b); }
        if (seconds != nullptr) *seconds = n * 0.1;
        if (n == 0) return -100.0f;
        const float gate = toLufs (sum / (double) n) - 10.0f;
        long long m = 0; double s2sum = 0;
        for (int b = 0; b < kBins; ++b)
            if ((float) b / 10.0f - 70.0f >= gate) { m += h[(size_t) b]; s2sum += h[(size_t) b] * energyOf (b); }
        return m > 0 ? toLufs (s2sum / (double) m) : -100.0f;
    }

private:
    struct Coeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct Z { double z1 = 0, z2 = 0; };
    static double filter (double x, const Coeffs& c, Z& z)
    {
        const double y = c.b0 * x + z.z1;
        z.z1 = c.b1 * x - c.a1 * y + z.z2;
        z.z2 = c.b2 * x - c.a2 * y;
        return y;
    }
    static float toLufs (double meanSquare)    { return meanSquare > 1e-12 ? (float) (-0.691 + 10.0 * std::log10 (meanSquare)) : -100.0f; }
    static double energyOf (int bin)           { return std::pow (10.0, ((bin / 10.0 - 70.0) + 0.691) / 10.0); }

    static constexpr int kBins = 1000;          // -70 .. +30 LUFS po 0.1
    double sr = 48000; int channels = 2;
    Coeffs s1, s2;
    std::array<Z, 4> state {};
    int blockLen = 4800, blockPos = 0; double blockSum = 0;
    std::array<double, 4> ring {}; long long ringFill = 0;
    double peakAcc = 0;
    std::array<std::atomic<long long>, kBins> hist {};
    std::atomic<float> momentary { -100.0f }, peak { -100.0f }, peakHold { -100.0f };
    std::atomic<bool> resetRequested { false };
};
} // namespace rm
