// Accuracy checks of the analysis engine on synthetic signals.
// Plain executable: exits with 0 when every check passes.

#include "../core/LfeEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace subguard;

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr int sampleRate = 48000;
    constexpr int blockSize = 512;

    int failures = 0, checks = 0;

    void check (bool ok, const std::string& what, double value)
    {
        ++checks;

        if (! ok)
        {
            ++failures;
            std::printf ("  FAILED: %s (value %g)\n", what.c_str(), value);
        }
    }

    using Signal = std::vector<float>;

    Signal sine (double freq, double levelDb, double seconds)
    {
        Signal s ((size_t) (seconds * sampleRate));
        const double amp = std::pow (10.0, levelDb / 20.0);

        for (size_t i = 0; i < s.size(); ++i)
            s[i] = (float) (amp * std::sin (2.0 * pi * freq * (double) i / sampleRate));

        return s;
    }

    Signal noise (double seconds, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::normal_distribution<double> dist (0.0, 0.1);
        Signal s ((size_t) (seconds * sampleRate));

        for (auto& v : s)
            v = (float) dist (rng);

        return s;
    }

    Signal scaled (const Signal& s, double gain)
    {
        Signal r (s);

        for (auto& v : r)
            v = (float) (v * gain);

        return r;
    }

    Signal sum (const Signal& a, const Signal& b)
    {
        Signal r (a);

        for (size_t i = 0; i < r.size(); ++i)
            r[i] += b[i];

        return r;
    }

    Signal delayed (const Signal& s, double seconds)
    {
        Signal r (s.size(), 0.0f);
        const auto n = (size_t) (seconds * sampleRate);
        std::copy (s.begin(), s.end() - (long) n, r.begin() + (long) n);
        return r;
    }

    // Low-frequency noise and the same noise shifted by 90 degrees
    void quadraturePair (double seconds, unsigned seed, Signal& a, Signal& b)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<double> phase (0.0, 2.0 * pi);
        const auto n = (size_t) (seconds * sampleRate);
        std::vector<double> x (n, 0.0), y (n, 0.0);

        for (double f = 12.0; f < 180.0; f += 0.37)
        {
            const double ph = phase (rng), w = 2.0 * pi * f / sampleRate;

            for (size_t i = 0; i < n; ++i)
            {
                x[i] += std::cos (w * (double) i + ph);
                y[i] += std::sin (w * (double) i + ph);
            }
        }

        a.resize (n);
        b.resize (n);

        for (size_t i = 0; i < n; ++i)
        {
            a[i] = (float) (0.005 * x[i]);
            b[i] = (float) (0.005 * y[i]);
        }
    }

    struct Scene
    {
        Signal l, c, r, lfe;

        size_t length() const
        {
            return std::max (std::max (l.size(), c.size()), std::max (r.size(), lfe.size()));
        }

        std::vector<float> interleaved() const
        {
            std::vector<float> d (length() * 4, 0.0f);
            const Signal* src[4] = { &l, &c, &r, &lfe };

            for (int ch = 0; ch < 4; ++ch)
                for (size_t i = 0; i < src[ch]->size(); ++i)
                    d[i * 4 + (size_t) ch] = (*src[ch])[i];

            return d;
        }
    };

    // Streams a scene like the audio callback would; `each` runs after every analysis.
    void run (LfeEngine& engine, const Scene& scene, const Settings& settings = {},
              const std::function<void (double)>& each = {}, size_t maxFrames = 0)
    {
        const auto data = scene.interleaved();
        size_t total = scene.length();

        if (maxFrames > 0)
            total = std::min (total, maxFrames);

        int block = 0;

        for (size_t pos = 0; pos < total; pos += blockSize, ++block)
        {
            engine.push (data.data() + pos * 4, (int) std::min ((size_t) blockSize, total - pos));

            if (block % 2 == 1 && engine.analyze (settings) && each)
                each ((double) pos / sampleRate);
        }
    }

    // Statistics of a per-grid quantity between two frequencies
    struct Range { double lo = 1e9, hi = -1e9, mean = 0; };

    Range over (const Analysis& a, const std::vector<double>& values, double fLo = 25.0, double fHi = 120.0)
    {
        Range r;
        int n = 0;

        for (size_t i = 0; i < a.grid.size(); ++i)
        {
            if (a.grid[i] < fLo || a.grid[i] > fHi)
                continue;

            r.lo = std::min (r.lo, values[i]);
            r.hi = std::max (r.hi, values[i]);
            r.mean += values[i];
            ++n;
        }

        r.mean /= std::max (n, 1);
        return r;
    }

    double peakDb (const std::vector<double>& db, const std::vector<double>& freqs, double* atFreq = nullptr, double maxFreq = 1e9)
    {
        double best = -1e9;

        for (size_t i = 0; i < db.size(); ++i)
        {
            if (freqs[i] <= maxFreq && db[i] > best)
            {
                best = db[i];

                if (atFreq != nullptr)
                    *atFreq = freqs[i];
            }
        }

        return best;
    }

    bool alerting (const Analysis& a, int pair, size_t i, double threshold = 0.33, double sigmas = 3.0)
    {
        return std::abs (a.interaction[(size_t) pair][i] * a.gate[(size_t) pair][i])
                 > std::max (threshold, sigmas * a.sigma[(size_t) pair][i]);
    }
}

//==============================================================================
static void testFilterDesign()
{
    std::printf ("anti-alias filter\n");

    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        Decimator d (rate);
        const double fo = d.outputRate();
        check (std::abs (d.response (0.0) - 1.0) < 1e-9, "unity gain at DC", d.response (0.0));
        check (20.0 * std::log10 (d.response (150.0)) > -0.5, "flat up to 150 Hz", 20.0 * std::log10 (d.response (150.0)));

        double worst = -1e9;

        for (double f = fo - 150.0; f < rate / 2.0; f *= 1.01)
            worst = std::max (worst, 20.0 * std::log10 (d.response (f) + 1e-300));

        check (worst < -89.9, "stop band 90 dB down", worst);
    }
}

static void testLevels()
{
    std::printf ("level accuracy\n");

    for (double window : { 0.5, 1.0, 2.0 })
    {
        for (double freq : { 20.0, 31.7, 63.3, 100.0, 149.0 })
        {
            LfeEngine e (sampleRate, window, 0.0);
            Scene s;
            s.lfe = sine (freq, -20.0, 4.0);
            run (e, s);

            double at = 0.0;
            const double level = peakDb (e.result().lfeDb, e.result().freqs, &at);
            check (std::abs (level + 20.0) < 0.4, "sine level reads -20 dB", level);
            check (std::abs (at - freq) < 0.3, "sine frequency", at);
        }
    }

    LfeEngine e (sampleRate, 1.0, -3.0);
    Scene s;
    s.l = s.c = s.r = sine (50.0, -20.0, 4.0);
    run (e, s);
    const double level = peakDb (e.result().facadeDb, e.result().freqs);
    check (std::abs (level - (-20.0 + 9.54 - 3.0)) < 0.4, "L+C+R sum with trim", level);
}

static void testAliasing()
{
    std::printf ("aliasing rejection\n");

    for (double freq : { 380.0, 450.0, 620.0, 1000.0, 5000.0 })
    {
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.lfe = sine (freq, 0.0, 3.0);
        run (e, s);
        const double level = peakDb (e.result().lfeDb, e.result().freqs, nullptr, 150.0);
        check (level < -80.0, "full-scale tone above the band stays out", level);
    }
}

static void testPhaseRelations()
{
    std::printf ("phase relations\n");
    const auto n = noise (8.0, 1);

    {
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.l = n;
        s.lfe = n;
        run (e, s);
        const auto& a = e.result();
        check (over (a, a.interaction[chL]).lo > 0.97, "identical: interaction +1", over (a, a.interaction[chL]).lo);
        check (over (a, a.coherence[chL]).lo > 0.97, "identical: coherence 1", over (a, a.coherence[chL]).lo);
        check (std::abs (over (a, a.interaction[chC]).hi) < 1e-6, "silent channel: 0", over (a, a.interaction[chC]).hi);
    }
    {
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.l = scaled (n, -1.0);
        s.lfe = n;
        run (e, s);
        const auto& a = e.result();
        check (over (a, a.interaction[chL]).hi < -0.97, "inverted: interaction -1", over (a, a.interaction[chL]).hi);

        std::vector<double> absPhase (a.phase[chL]);
        for (auto& v : absPhase) v = std::abs (v);
        check (over (a, absPhase).lo > pi - 0.05, "inverted: phase 180 deg", over (a, absPhase).lo);
    }
    {
        Scene s;
        quadraturePair (8.0, 2, s.l, s.lfe);
        LfeEngine e (sampleRate, 1.0, 0.0);
        run (e, s);
        const auto& a = e.result();

        std::vector<double> absInter (a.interaction[chL]), absPhase (a.phase[chL]);
        for (auto& v : absInter) v = std::abs (v);
        for (auto& v : absPhase) v = std::abs (std::abs (v) - pi / 2.0);

        check (over (a, absInter).hi < 0.1, "quadrature: no interaction", over (a, absInter).hi);
        check (over (a, a.coherence[chL]).lo > 0.9, "quadrature: still coherent", over (a, a.coherence[chL]).lo);
        check (over (a, absPhase).hi < 0.1, "quadrature: phase 90 deg", over (a, absPhase).hi);
    }
    {
        // 5 ms late: phase must follow -2.pi.f.delay (e.g. 90 deg at 50 Hz)
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.l = delayed (n, 0.005);
        s.lfe = n;
        run (e, s);
        const auto& a = e.result();
        double worst = 0.0;

        for (size_t i = 0; i < a.grid.size(); ++i)
            if (a.grid[i] >= 25.0 && a.grid[i] <= 90.0)
                worst = std::max (worst, std::abs (a.phase[chL][i] + 2.0 * pi * a.grid[i] * 0.005));

        check (worst < 0.06, "delay: phase slope", worst);
    }
}

static void testLevelBalanceAndPartial()
{
    std::printf ("level balance, partial correlation, mute\n");
    {
        const auto n = noise (8.0, 3);
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.l = scaled (n, 0.1);
        s.lfe = n;
        run (e, s);
        const auto& a = e.result();
        const double expected = 2.0 * 0.1 / 1.01;
        const auto r = over (a, a.interaction[chL]);
        check (std::abs (r.lo - expected) < 0.02 && std::abs (r.hi - expected) < 0.02, "20 dB apart: interaction 0.2", r.mean);
        check (over (a, a.coherence[chL]).lo > 0.97, "20 dB apart: still coherent", over (a, a.coherence[chL]).lo);
    }
    {
        const auto a1 = noise (12.0, 4), b1 = noise (12.0, 5);
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.l = sum (a1, b1);
        s.lfe = a1;
        Settings settings;
        settings.tauCorrelation = 4.0;
        run (e, s, settings);
        const auto& a = e.result();
        const double mean = over (a, a.interaction[chL], 40.0, 120.0).mean;
        check (std::abs (mean - 2.0 / 3.0) < 0.08, "half shared: interaction 2/3", mean);
    }
    {
        const auto n = noise (6.0, 6);
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.l = n;
        s.lfe = n;
        Settings settings;
        settings.mute[chL] = true;
        run (e, s, settings);
        const auto& a = e.result();
        check (std::abs (over (a, a.interaction[chL]).hi) < 1e-6, "muted channel: 0", over (a, a.interaction[chL]).hi);
        check (peakDb (a.facadeDb, a.freqs) < -120.0, "muted channel leaves the sum", peakDb (a.facadeDb, a.freqs));
    }
    {
        // +10 dB on the LFE turns an equal-level pair into a 10 dB imbalance
        const auto n = noise (8.0, 7);
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.l = n;
        s.lfe = n;
        Settings settings;
        settings.lfeGainDb = 10.0;
        run (e, s, settings);
        const auto& a = e.result();
        const double g = std::pow (10.0, 0.5), expected = 2.0 * g / (1.0 + g * g);
        check (std::abs (over (a, a.interaction[chL]).mean - expected) < 0.02, "LFE +10 dB weighting", over (a, a.interaction[chL]).mean);
    }
}

static void testChanceLevel()
{
    std::printf ("unrelated signals and onsets\n");
    long steadyHits = 0, steadyCount = 0, onsetHits = 0, onsetCount = 0;
    double zSum = 0.0;
    long zCount = 0;

    for (unsigned seed = 0; seed < 6; ++seed)
    {
        // 2 s of silence, then two unrelated sounds start together
        Scene s;
        s.l = delayed (noise (10.0, 10 + seed), 2.0);
        s.lfe = delayed (noise (10.0, 100 + seed), 2.0);
        LfeEngine e (sampleRate, 1.0, 0.0);

        run (e, s, {}, [&] (double t)
        {
            if (t < 2.05)
                return;

            const auto& a = e.result();

            for (size_t i = 0; i < a.grid.size(); ++i)
            {
                if (a.grid[i] < 20.0 || a.grid[i] > 150.0)
                    continue;

                const bool hit = alerting (a, chL, i);

                if (t < 4.0) { onsetHits += hit; ++onsetCount; }
                else
                {
                    steadyHits += hit;
                    ++steadyCount;
                    const double z = a.interaction[chL][i] / a.sigma[chL][i];
                    zSum += z * z;
                    ++zCount;
                }
            }
        });
    }

    const double steady = (double) steadyHits / (double) steadyCount;
    const double onset = (double) onsetHits / (double) onsetCount;
    const double zRms = std::sqrt (zSum / (double) zCount);
    check (steady < 0.01, "false alerts, steady state", steady);
    check (onset < 0.03, "false alerts, 2 s after an onset", onset);
    check (zRms > 0.6 && zRms < 1.2, "sigma matches the observed spread", zRms);
    std::printf ("  false alerts: %.2f %% steady, %.2f %% at onsets; spread / sigma = %.2f\n",
                 100.0 * steady, 100.0 * onset, zRms);

    // A truly shared signal must clear the chance level quickly
    Scene s;
    const auto n = delayed (noise (3.0, 60), 2.0);
    s.l = n;
    s.lfe = n;
    LfeEngine e (sampleRate, 1.0, 0.0);
    run (e, s, {}, {}, (size_t) (2.5 * sampleRate));
    const auto& a = e.result();
    int hits = 0, count = 0;

    for (size_t i = 0; i < a.grid.size(); ++i)
        if (a.grid[i] >= 20.0 && a.grid[i] <= 150.0)
        {
            hits += alerting (a, chL, i);
            ++count;
        }

    check (hits > 0.9 * count, "shared signal detected within 0.5 s", (double) hits / count);
}

static void testBlockSizeIndependence()
{
    std::printf ("block size independence\n");
    Scene s;
    s.l = noise (3.0, 7);
    s.r = scaled (s.l, -1.0);
    s.lfe = s.l;
    const auto data = s.interleaved();
    const auto total = s.length();

    LfeEngine a (sampleRate), b (sampleRate);
    a.push (data.data(), (int) total);
    a.analyze ({});

    std::mt19937 rng (0);
    std::uniform_int_distribution<int> sizes (1, 2000);

    for (size_t pos = 0; pos < total;)
    {
        const auto n = std::min ((size_t) sizes (rng), total - pos);
        b.push (data.data() + pos * 4, (int) n);
        pos += n;
    }

    b.analyze ({});
    double worst = 0.0;

    for (size_t i = 0; i < a.result().lfeDb.size(); ++i)
        worst = std::max (worst, std::abs (a.result().lfeDb[i] - b.result().lfeDb[i]));

    check (worst < 1e-9, "same result whatever the block size", worst);
}

int main()
{
    testFilterDesign();
    testLevels();
    testAliasing();
    testPhaseRelations();
    testLevelBalanceAndPartial();
    testChanceLevel();
    testBlockSizeIndependence();

    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
