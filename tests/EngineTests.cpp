// Accuracy checks of the analysis engine on synthetic signals.
// Plain executable: exits with 0 when every check passes.

#include "../core/AlertTracker.h"
#include "../core/AnalysisRunner.h"
#include "../core/Inertia.h"
#include "../core/LfeEngine.h"
#include "../core/PlaybackSim.h"
#include "../core/RoomSim.h"
#include "../core/RoomPro.h"
#include "../core/TestSignals.h"
#include <chrono>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
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
            std::vector<float> d (length() * numChannels, 0.0f);
            const Signal* src[numChannels] = { &l, &c, &r, &lfe };

            for (int ch = 0; ch < numChannels; ++ch)
                for (size_t i = 0; i < src[ch]->size(); ++i)
                    d[i * numChannels + (size_t) ch] = (*src[ch])[i];

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
            engine.push (data.data() + pos * numChannels, (int) std::min ((size_t) blockSize, total - pos));

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

    bool alerting (const Analysis& a, int pair, size_t i, double threshold = 0.5, double sigmas = AlertTracker::alertSigmas)
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

    // A truly shared signal must clear the chance level as fast as the frequency
    // allows: a few cycles are needed, so the low end takes longer.
    const auto detected = [] (double seconds, double fromHz)
    {
        Scene s;
        const auto n = delayed (noise (5.0, 60), 2.0);
        s.l = n;
        s.lfe = n;
        LfeEngine e (sampleRate, 1.0, 0.0);
        run (e, s, {}, {}, (size_t) ((2.0 + seconds) * sampleRate));
        const auto& a = e.result();
        int hits = 0, count = 0;

        for (size_t i = 0; i < a.grid.size(); ++i)
            if (a.grid[i] >= fromHz && a.grid[i] <= 150.0)
            {
                hits += alerting (a, chL, i);
                ++count;
            }

        return (double) hits / count;
    };

    check (detected (0.5, 50.0) > 0.9, "shared signal above 50 Hz detected within 0.5 s", detected (0.5, 50.0));
    check (detected (1.5, 20.0) > 0.9, "shared signal above 20 Hz detected within 1.5 s", detected (1.5, 20.0));
    check (detected (3.0, 8.0) > 0.9, "shared signal above 8 Hz detected within 3 s", detected (3.0, 8.0));
    std::printf ("  shared signal detected: %.0f %% above 50 Hz at 0.5 s, %.0f %% above 20 Hz at 1.5 s, %.0f %% above 8 Hz at 3 s\n",
                 100.0 * detected (0.5, 50.0), 100.0 * detected (1.5, 20.0), 100.0 * detected (3.0, 8.0));
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
        b.push (data.data() + pos * numChannels, (int) n);
        pos += n;
    }

    b.analyze ({});
    double worst = 0.0;

    for (size_t i = 0; i < a.result().lfeDb.size(); ++i)
        worst = std::max (worst, std::abs (a.result().lfeDb[i] - b.result().lfeDb[i]));

    check (worst < 1e-9, "same result whatever the block size", worst);
}

static void testLowLevelAndLowFrequency()
{
    std::printf ("very low level, 5 Hz, level independence\n");
    const auto n = noise (10.0, 31);

    {
        // The same signal 100 dB down (about -120 dBFS RMS) must read exactly the same
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.l = scaled (n, 1e-5);
        s.lfe = scaled (n, 1e-5);
        run (e, s);
        const auto& a = e.result();
        check (over (a, a.correlation[chL], 6.0, 120.0).lo > 0.97, "-120 dBFS: correlation still +1", over (a, a.correlation[chL], 6.0, 120.0).lo);
        check (over (a, a.gate[chL], 6.0, 120.0).lo > 0.99, "-120 dBFS: not gated out", over (a, a.gate[chL], 6.0, 120.0).lo);
    }
    {
        // Unrelated signals at that level stay quiet
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.l = scaled (noise (10.0, 32), 1e-5);
        s.lfe = scaled (noise (10.0, 33), 1e-5);
        run (e, s);
        const auto& a = e.result();
        int hits = 0, count = 0;

        for (size_t i = 0; i < a.grid.size(); ++i)
            if (a.grid[i] >= 5.0 && a.grid[i] <= 150.0)
            {
                hits += std::abs (a.correlation[chL][i] * a.gate[chL][i]) > std::max (0.5, AlertTracker::alertSigmas * a.sigma[chL][i]);
                ++count;
            }

        check (hits == 0, "-120 dBFS unrelated: no alert", (double) hits / count);
    }
    {
        // LFE 30 dB below the screen channel: the correlation does not care, the interaction does
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.l = n;
        s.lfe = scaled (n, -0.0316);
        run (e, s);
        const auto& a = e.result();
        check (over (a, a.correlation[chL]).hi < -0.97, "LFE 30 dB lower, inverted: correlation -1", over (a, a.correlation[chL]).hi);
        check (std::abs (over (a, a.interaction[chL]).mean + 0.063) < 0.01, "LFE 30 dB lower: interaction -0.06", over (a, a.interaction[chL]).mean);

        // "Out of phase only" and its opposite case
        AlertTracker tracker;
        AlertTracker::Options o;
        o.outOfPhaseOnly = true;

        for (int i = 0; i < 20; ++i)
            tracker.update (a, o, 0.016);

        check (tracker.speakerLevel[0] > 0.9, "out-of-phase-only mode: inverted LFE alerts", tracker.speakerLevel[0]);
    }
    {
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.l = n;
        s.lfe = n;
        run (e, s);
        AlertTracker both, negative;
        AlertTracker::Options o;

        for (int i = 0; i < 20; ++i)
        {
            o.outOfPhaseOnly = false;
            both.update (e.result(), o, 0.016);
            o.outOfPhaseOnly = true;
            negative.update (e.result(), o, 0.016);
        }

        check (both.speakerLevel[0] > 0.9, "default mode: in-phase LFE alerts", both.speakerLevel[0]);
        check (negative.speakerLevel[0] < 1e-9, "out-of-phase-only mode: in-phase LFE is ignored", negative.speakerLevel[0]);
    }
    {
        // Down to 5 Hz
        LfeEngine e (sampleRate, 2.0, 0.0);
        Scene s;
        s.lfe = sine (6.0, -20.0, 8.0);
        run (e, s);
        double at = 0.0;
        const double level = peakDb (e.result().lfeDb, e.result().freqs, &at);
        check (std::abs (level + 20.0) < 0.3 && std::abs (at - 6.0) < 0.2, "6 Hz sine reads -20 dB", level);

        LfeEngine e2 (sampleRate, 2.0, 0.0);
        Scene s2;
        s2.l = n;
        s2.lfe = scaled (n, -1.0);
        run (e2, s2);
        const auto& a = e2.result();
        check (a.grid.front() <= 5.0, "analysis grid starts at 5 Hz or below", a.grid.front());
        check (over (a, a.correlation[chL], 5.0, 20.0).hi < -0.95, "5-20 Hz: inverted LFE reads -1", over (a, a.correlation[chL], 5.0, 20.0).hi);
    }
}

static void testTransients()
{
    std::printf ("transient detector\n");

    // A kick: pitch sweep 120 -> 45 Hz with a 2 ms attack
    const auto kicks = [] (double seconds, double offset, double gain)
    {
        Signal s ((size_t) (seconds * sampleRate), 0.0f);

        for (size_t i = 0; i < s.size(); ++i)
        {
            const double t = (double) i / sampleRate;

            for (double t0 = 1.0; t0 < seconds - 0.6; t0 += 0.9)
            {
                const double u = t - t0 - offset;

                if (u < 0.0)
                    continue;

                const double phase = 2.0 * pi * (45.0 * u + 75.0 * 0.03 * (1.0 - std::exp (-u / 0.03)));
                s[i] += (float) (gain * 0.5 * std::sin (phase) * std::exp (-u / 0.09) * (1.0 - std::exp (-u / 0.002)));
            }
        }

        return s;
    };

    // Events the app would act on: those whose match clears the default threshold.
    // Weaker ones (chance likeness of two unrelated noise floors) are reported
    // by the detector but never shown.
    const auto collect = [] (const Scene& scene)
    {
        LfeEngine e (sampleRate, 1.0, 0.0);
        std::vector<TransientEvent> all;
        const auto data = scene.interleaved();

        for (size_t pos = 0; pos < scene.length(); pos += blockSize)
        {
            e.push (data.data() + pos * numChannels, (int) std::min ((size_t) blockSize, scene.length() - pos));

            for (const auto& ev : e.takeTransientEvents())
                if (ev.inLfe && ev.match > 0.5)
                    all.push_back (ev);
        }

        return all;
    };

    const auto floorNoise = [] (unsigned seed) { return scaled (noise (6.0, seed), 0.01); };

    struct Case { const char* name; double offset, gain, expectedDelay; bool inverted; };

    for (const Case& c : { Case { "aligned", 0.0, 1.0, 0.0, false },
                           Case { "LFE 10 ms late", 0.010, 1.0, 10.0, false },
                           Case { "LFE 6 ms early", -0.006, 1.0, -6.0, false },
                           Case { "LFE 23.3 ms late", 0.0233, 1.0, 23.3, false },
                           Case { "polarity inverted", 0.0, -1.0, 0.0, true } })
    {
        Scene s;
        s.c = sum (kicks (6.0, 0.0, 1.0), floorNoise (1));
        s.lfe = sum (kicks (6.0, c.offset, c.gain), floorNoise (2));
        const auto events = collect (s);
        check (events.size() == 5, std::string (c.name) + ": one event per kick", (double) events.size());

        double worstDelay = 0.0, worstMatch = 1.0, worstPhase = 0.0;

        for (const auto& ev : events)
        {
            worstDelay = std::max (worstDelay, std::abs (ev.delayMs - c.expectedDelay));
            worstMatch = std::min (worstMatch, ev.match);
            worstPhase = std::max (worstPhase, std::abs (std::abs (ev.phase) - (c.inverted ? pi : 0.0)));
        }

        check (worstDelay < 0.5, std::string (c.name) + ": delay within 0.5 ms", worstDelay);
        check (events.empty() || events[0].delayUncertaintyMs < 1.0, std::string (c.name) + ": stated precision under 1 ms",
               events.empty() ? 0.0 : events[0].delayUncertaintyMs);
        check (worstMatch > 0.95, std::string (c.name) + ": transients recognised as the same", worstMatch);
        check (worstPhase < 0.5, std::string (c.name) + ": polarity", worstPhase);
    }

    {
        // A kick in L and in R in opposite polarity: it cancels in the downmix (L + R),
        // but is still found (in L - R) and lined up with the LFE
        Scene s;
        s.l = sum (kicks (6.0, 0.0, 1.0), floorNoise (3));
        s.r = sum (kicks (6.0, 0.0, -1.0), floorNoise (4));
        s.lfe = sum (kicks (6.0, 0.012, 1.0), floorNoise (5));
        const auto events = collect (s);
        double worst = events.empty() ? 1e9 : 0.0;

        for (const auto& ev : events)
            worst = std::max (worst, std::abs (ev.delayMs - 12.0));

        std::printf ("  kick in L and R in opposite polarity (gone from the downmix): %zu of 5 found, LFE +12 ms read within %.2f ms\n",
                     events.size(), worst);
        check (events.size() == 5 && worst < 0.5, "an attack cancelled in the downmix is still found and timed", (double) events.size());

        // The same with a loud centre above the bass (a voice-like 1 kHz tone): the cancellation
        // is judged below 200 Hz, so the centre does not hide it
        s.c = sine (1000.0, -6.0, 6.0);
        const auto withCentre = collect (s);
        std::printf ("  the same under a loud 1 kHz centre: %zu of 5 found\n", withCentre.size());
        check (withCentre.size() == 5, "a busy centre above the bass does not hide a bass cancelled in L + R", (double) withCentre.size());
    }

    {
        // 10 ms late: the pair would interact fully if aligned, and mostly cancels as it is
        Scene s;
        s.c = kicks (6.0, 0.0, 1.0);
        s.lfe = kicks (6.0, 0.010, 1.0);
        const auto events = collect (s);
        check (! events.empty() && events[0].interactionBest > 0.9, "late kick: full interaction once aligned",
               events.empty() ? 0.0 : events[0].interactionBest);
        check (! events.empty() && events[0].interactionNow < 0.2, "late kick: no longer sums in phase",
               events.empty() ? 0.0 : events[0].interactionNow);
    }
    {
        // The same kicks 84 dB down (peaks around -90 dBFS): found exactly the same
        Scene s;
        s.c = kicks (6.0, 0.0, 6.3e-5);
        s.lfe = kicks (6.0, 0.010, 6.3e-5);
        const auto events = collect (s);
        check (events.size() == 5, "kicks at -90 dBFS: all found", (double) events.size());

        for (const auto& ev : events)
            check (std::abs (ev.delayMs - 10.0) < 0.5 && ev.match > 0.95, "kicks at -90 dBFS: delay and match", ev.delayMs);
    }
    {
        // LFE 30 dB below the screen channel: still matched, whatever the balance
        Scene s;
        s.c = kicks (6.0, 0.0, 1.0);
        s.lfe = kicks (6.0, 0.010, 0.0316);
        const auto events = collect (s);
        check (events.size() == 5 && events[0].match > 0.95, "LFE 30 dB lower: transient still matched",
               events.empty() ? 0.0 : events[0].match);
        check (! events.empty() && events[0].interactionBest < 0.1, "LFE 30 dB lower: level-weighted value is small",
               events.empty() ? 0.0 : events[0].interactionBest);
    }
    {
        // A 28 Hz boom under a louder 60-190 Hz bed, both channels sharing the boom
        Signal booms ((size_t) (8.0 * sampleRate), 0.0f), bed = noise (8.0, 41);

        for (size_t i = 0; i < booms.size(); ++i)
        {
            const double t = (double) i / sampleRate;

            for (double t0 = 1.0; t0 < 7.0; t0 += 1.5)
            {
                const double u = t - t0;

                if (u >= 0.0)
                    booms[i] += (float) (0.5 * std::sin (2.0 * pi * 28.0 * u) * std::exp (-u / 0.25) * (1.0 - std::exp (-u / 0.03)));
            }
        }

        // crude 60 Hz high-pass of the bed, so it sits above the boom
        double lp = 0.0;

        for (auto& v : bed)
        {
            lp += 0.008 * (v - lp);
            v = (float) (4.0 * (v - lp));
        }

        Scene s;
        s.c = sum (booms, bed);
        s.lfe = delayed (booms, 0.008);
        const auto events = collect (s);
        int good = 0;

        for (const auto& ev : events)
            good += std::abs (ev.delayMs - 8.0) < std::max (1.5, 3.0 * ev.delayUncertaintyMs) && ev.match > 0.5;

        check (good >= 4, "28 Hz boom under a louder bed: found with the right delay", good);
    }
    {
        // Narrow-band thump under noise: the stated precision must cover the real error
        Signal thump ((size_t) (6.0 * sampleRate), 0.0f), late (thump.size(), 0.0f);

        for (size_t i = 0; i < thump.size(); ++i)
        {
            const double t = (double) i / sampleRate;

            for (double t0 = 1.0; t0 < 5.4; t0 += 0.9)
                for (int k = 0; k < 2; ++k)
                {
                    const double u = t - t0 - (k == 1 ? 0.010 : 0.0);

                    if (u >= 0.0)
                        (k == 1 ? late : thump)[i] += (float) (0.5 * std::sin (2.0 * pi * 50.0 * u) * std::exp (-u / 0.15)
                                                                 * (1.0 - std::exp (-u / 0.004)));
                }
        }

        Scene s;
        s.c = thump;
        s.lfe = sum (late, scaled (noise (6.0, 21), 12.0));    // about -15 dB of noise in the LFE band
        const auto events = collect (s);
        check (! events.empty(), "narrow-band thump: detected", (double) events.size());

        for (const auto& ev : events)
        {
            check (std::abs (ev.delayMs - 10.0) < 3.0 * ev.delayUncertaintyMs + 0.5, "narrow-band thump: error within the stated precision",
                   std::abs (ev.delayMs - 10.0) / (ev.delayUncertaintyMs + 1e-9));
            check (ev.delayUncertaintyMs > 1.0, "narrow-band thump: precision reported as poorer", ev.delayUncertaintyMs);
        }
    }
    {
        // Music: bass line in L, R and LFE, kick (thump and click) in the centre
        // on the beat, hi-hat in L and R off the beat. 12 s, 24 kicks, 24 hats.
        const auto scene = [] (bool lfeKick, double lfeDelay, double lfeGain, double kickLevel)
        {
            const auto n = (size_t) (12.0 * sampleRate);
            Scene s;
            s.l.assign (n, 0.0f); s.c.assign (n, 0.0f); s.r.assign (n, 0.0f); s.lfe.assign (n, 0.0f);
            std::mt19937 rng (7);
            std::normal_distribution<double> normal (0.0, 1.0);
            double low = 0.0, pad = 0.0;

            const auto thump = [] (double u)
            {
                if (u < 0.0)
                    return 0.0;

                const double phase = 2.0 * pi * (45.0 * u + 75.0 * 0.03 * (1.0 - std::exp (-u / 0.03)));
                return std::sin (phase) * std::exp (-u / 0.12) * (1.0 - std::exp (-u / 0.002));
            };

            for (size_t i = 0; i < n; ++i)
            {
                const double t = (double) i / sampleRate, beat = 0.5;
                const double onBeat = std::fmod (t, beat), offBeat = std::fmod (t + beat / 2.0, beat);
                const double click = onBeat < 0.004 ? normal (rng) * std::exp (-onBeat / 0.0012) : 0.0;
                const double white = normal (rng);
                low += 0.35 * (white - low);
                const double hat = offBeat < 0.05 ? (white - low) * std::exp (-offBeat / 0.012) : 0.0;
                pad += 0.02 * (normal (rng) - pad);
                const double bass = 0.25 * std::sin (2.0 * pi * 55.0 * t) * (0.7 + 0.3 * std::sin (2.0 * pi * 0.5 * t))
                                      + 0.1 * std::sin (2.0 * pi * 110.0 * t);
                s.l[i] = (float) (0.7 * bass + 0.15 * hat + 0.3 * pad);
                s.r[i] = (float) (0.7 * bass - 0.12 * hat + 0.3 * pad);
                s.c[i] = (float) (kickLevel * (0.5 * thump (onBeat) + 0.25 * click));
                s.lfe[i] = (float) (0.3 * 0.316 * std::sin (2.0 * pi * 55.0 * t + 1.0)
                                      + (lfeKick ? lfeGain * 0.316 * 0.5 * kickLevel * thump (std::fmod (t - lfeDelay + beat, beat)) : 0.0));
            }

            return s;
        };

        struct Count { int mix = 0, inLfe = 0, inverted = 0; double median = 0.0, spread = 0.0; };

        const auto run = [] (const Scene& s, double searchMs = 250.0, double release = 50.0)
        {
            LfeEngine e (sampleRate, 1.0, 0.0);
            TransientSettings tuning;
            tuning.searchMs = searchMs;
            tuning.release = release;
            e.setTransientSettings (tuning);
            const auto data = s.interleaved();
            std::vector<double> delays;
            Count c;

            for (size_t pos = 0; pos < s.length(); pos += blockSize)
            {
                e.push (data.data() + pos * numChannels, (int) std::min ((size_t) blockSize, s.length() - pos));

                for (const auto& ev : e.takeTransientEvents())
                {
                    ++c.mix;

                    if (ev.inLfe)
                    {
                        ++c.inLfe;
                        c.inverted += std::abs (ev.phase) > 2.0 * pi / 3.0;
                        delays.push_back (ev.delayMs);
                    }
                }
            }

            if (! delays.empty())
            {
                std::sort (delays.begin(), delays.end());
                c.median = delays[delays.size() / 2];
                c.spread = delays[delays.size() * 9 / 10] - delays[delays.size() / 10];
            }

            return c;
        };

        {
            const auto c = run (scene (true, 0.012, 1.0, 1.0));
            std::printf ("  music, LFE kick 12 ms late: %d attacks in the mix, %d in the LFE, %+.1f ms (10-90 %% spread %.1f ms)\n",
                         c.mix, c.inLfe, c.median, c.spread);
            check (c.mix >= 23 && c.mix <= 52, "music: the kicks of the mix are found", c.mix);

            // The hi-hats sit 20 dB under the click of the kick, a quarter of a second
            // after it: hidden by its tail at the default release, found with a short one
            const auto shortRelease = run (scene (true, 0.012, 1.0, 1.0), 250.0, 10.0);
            std::printf ("  music: %d attacks with release 50 %%, %d with release 10 %%\n", c.mix, shortRelease.mix);
            check (shortRelease.mix >= 40 && shortRelease.inLfe >= 21 && shortRelease.inLfe <= 25,
                   "music: a short release also finds the hi-hats, still not in the LFE", shortRelease.mix);
            check (c.inLfe >= 21 && c.inLfe <= 25, "music: the kicks, and only them, are in the LFE", c.inLfe);
            check (std::abs (c.median - 12.0) < 0.5 && c.spread < 1.5, "music: LFE offset under a bass line", c.median);
        }
        {
            const auto c = run (scene (true, 0.0, -1.0, 1.0));
            check (std::abs (c.median) < 0.5 && c.inverted * 10 >= c.inLfe * 9, "music: inverted LFE kick seen as aligned and inverted", c.median);
        }
        {
            const auto c = run (scene (true, -0.006, 1.0, 1.0));
            check (std::abs (c.median + 6.0) < 0.5, "music: LFE kick 6 ms early", c.median);
        }
        {
            // A weak hit under a loud bass needs a narrow search: with a wide one, most are lost
            const auto c = run (scene (true, 0.012, 1.0, 0.25), 50.0);
            const auto wide = run (scene (true, 0.012, 1.0, 0.25));
            std::printf ("  music, kick 12 dB lower under the same bass: %d of 24 kicks found in the LFE with a 50 ms search (%d with 250 ms), %+.1f ms\n",
                         c.inLfe, wide.inLfe, c.median);
            check (c.inLfe >= 12 && std::abs (c.median - 12.0) < 1.5, "music: quiet kick under a loud bass line", c.inLfe);
        }
        {
            const auto c = run (scene (false, 0.0, 1.0, 1.0));
            check (c.mix >= 23 && c.inLfe <= 1, "music: LFE with the bass line only shares no attack but its start", c.inLfe);
        }
    }
    {
        // A hit with a long, uneven tail (a kick with a cymbal), the LFE 200 ms
        // late: one attack, read as late. Nothing in the tail may be taken for
        // a new attack to which the same LFE hit would then look "early".
        const auto n = (size_t) (9.0 * sampleRate);
        Scene s;
        s.c.assign (n, 0.0f);
        s.lfe.assign (n, 0.0f);
        std::mt19937 rng (3);
        std::normal_distribution<double> normal (0.0, 1.0);

        for (size_t i = 0; i < n; ++i)
        {
            const double t = (double) i / sampleRate, white = normal (rng);

            for (double t0 = 1.0; t0 < 8.0; t0 += 2.0)
            {
                const double u = t - t0, v = u - 0.200;

                if (u >= 0.0)
                {
                    // thump, then a cymbal whose level wobbles by 6 dB as it rings out
                    const double wobble = 1.0 + 0.5 * std::sin (2.0 * pi * 7.0 * u);
                    s.c[i] += (float) (0.4 * std::sin (2.0 * pi * 90.0 * u) * std::exp (-u / 0.08)
                                         + 0.12 * white * wobble * std::exp (-u / 0.5));
                }

                if (v >= 0.0)
                    s.lfe[i] += (float) (0.3 * std::sin (2.0 * pi * 35.0 * v) * std::exp (-v / 0.25) * (1.0 - std::exp (-v / 0.01)));
            }
        }

        LfeEngine e (sampleRate, 1.0, 0.0);
        const auto data = s.interleaved();
        int total = 0, late = 0, early = 0;

        for (size_t pos = 0; pos < s.length(); pos += blockSize)
        {
            e.push (data.data() + pos * numChannels, (int) std::min ((size_t) blockSize, s.length() - pos));

            for (const auto& ev : e.takeTransientEvents())
            {
                ++total;
                late += ev.inLfe && ev.lfeAttack && std::abs (ev.attackDelayMs - 200.0) < 12.0;
                early += ev.inLfe && ((ev.lfeAttack && ev.attackDelayMs < -10.0) || (ev.match >= 0.3 && ev.delayMs < -10.0));
            }
        }

        std::printf ("  kick with a ringing cymbal, LFE 200 ms late: %d attacks, %d read as late, %d as early\n", total, late, early);
        check (late == 4 && early == 0, "ringing tail: the LFE is late, never early", early);
        check (total <= 5, "ringing tail: not taken for new attacks", total);
    }
    {
        // A send that does not look like its source: kick above 60 Hz in the
        // centre, a 30 Hz thud in the LFE, 200 ms late. The waveforms cannot be
        // matched; the attacks can still be timed.
        const auto n = (size_t) (8.0 * sampleRate);
        Scene s;
        s.c.assign (n, 0.0f);
        s.lfe.assign (n, 0.0f);

        for (size_t i = 0; i < n; ++i)
        {
            const double t = (double) i / sampleRate;

            for (double t0 = 1.0; t0 < 7.0; t0 += 1.5)
            {
                const double u = t - t0, v = u - 0.200;

                if (u >= 0.0)
                    s.c[i] += (float) (0.4 * std::sin (2.0 * pi * 95.0 * u) * std::exp (-u / 0.06) * (1.0 - std::exp (-u / 0.001)));

                if (v >= 0.0)
                    s.lfe[i] += (float) (0.3 * std::sin (2.0 * pi * 30.0 * v) * std::exp (-v / 0.2) * (1.0 - std::exp (-v / 0.01)));
            }
        }

        for (double range : { 250.0, 50.0 })
        {
            LfeEngine e (sampleRate, 1.0, 0.0);
            TransientSettings tuning;
            tuning.searchMs = range;
            e.setTransientSettings (tuning);
            const auto data = s.interleaved();
            int timed = 0, total = 0;
            double worst = 0.0, likeness = 0.0;

            for (size_t pos = 0; pos < s.length(); pos += blockSize)
            {
                e.push (data.data() + pos * numChannels, (int) std::min ((size_t) blockSize, s.length() - pos));

                for (const auto& ev : e.takeTransientEvents())
                {
                    ++total;

                    if (ev.lfeAttack)
                    {
                        ++timed;
                        worst = std::max (worst, std::abs (ev.attackDelayMs - 200.0));
                        likeness = std::max (likeness, ev.match);
                    }
                }
            }

            if (range > 200.0)
            {
                std::printf ("  unlike LFE send 200 ms late: %d of %d attacks timed, worst error %.0f ms, likeness %.2f\n", timed, total, worst, likeness);
                check (total == 4 && timed == 4 && worst < 12.0, "unlike LFE send, 200 ms late: timed attack against attack", worst);
            }
            else
                check (timed == 0, "LFE attack outside the search range: not paired", timed);
        }
    }
    {
        // Steady low noise, unrelated in the screen and the LFE: hardly any attack
        Scene s;
        s.c = noise (30.0, 51);
        s.lfe = noise (30.0, 52);
        LfeEngine e (sampleRate, 1.0, 0.0);
        const auto data = s.interleaved();
        int attacks = 0;

        for (size_t pos = 0; pos < s.length(); pos += blockSize)
        {
            e.push (data.data() + pos * numChannels, (int) std::min ((size_t) blockSize, s.length() - pos));
            attacks += (int) e.takeTransientEvents().size();
        }

        std::printf ("  steady noise, 30 s: %d attacks reported (its start is one)\n", attacks);
        check (attacks <= 4, "steady noise: no stream of false attacks", attacks);
    }
    {
        // Kick in the screen channel only, unrelated tone in the LFE: nothing to report
        Scene s;
        s.c = kicks (6.0, 0.0, 1.0);
        s.lfe = sine (33.0, -14.0, 6.0);
        check (collect (s).empty(), "unrelated LFE: no event", (double) collect (s).size());
    }
    {
        // A steady tone has a single attack: its start
        Scene s;
        s.c = delayed (sine (40.0, -10.0, 6.0), 0.5);
        s.lfe = s.c;
        check (collect (s).size() == 1, "steady tone: only its start counts", (double) collect (s).size());
    }
    {
        // Unrelated noise in both channels: no attack, no event
        Scene s;
        s.c = noise (6.0, 11);
        s.lfe = noise (6.0, 12);
        const auto events = collect (s);
        int strong = 0;

        for (const auto& ev : events)
            strong += ev.match > 0.5;

        check (strong == 0, "steady unrelated noise: nothing above the default threshold", strong);
    }
}

static void testPlaybackChains()
{
    std::printf ("playback systems, robustness, downmix, overlap\n");
    const PlaybackSimulator sim;
    const double asHeard = std::pow (10.0, -10.0 / 20.0);   // LFE recorded 10 dB down plays as loud as a screen channel
    const auto n = noise (10.0, 71);

    const auto cinema = [] (const Translation& t, double delayMs, bool inverted)
    {
        for (const auto& c : t.chains)
            if (c.kind == ChainResult::Kind::cinema && c.delayMs == delayMs && c.inverted == inverted)
                return c;

        return ChainResult();
    };

    const auto home = [] (const Translation& t, double corner, bool encoder)
    {
        for (const auto& c : t.chains)
            if (c.kind == ChainResult::Kind::bassManaged && c.crossoverHz == corner && c.encoderFilter == encoder)
                return c;

        return ChainResult();
    };

    const auto stereo = [] (const Translation& t) { return t.chains.back(); };

    {
        // Screen alone: nothing depends on the subwoofer, every system gives the reference
        LfeEngine e (sampleRate);   // default engine, i.e. with the display trim
        Scene s;
        s.c = n;
        run (e, s);
        const auto t = sim.compute (e.result());
        double worst = 0.0;

        for (const auto& c : t.chains)
            worst = std::max (worst, std::abs (c.changeDb));

        check (t.chains.size() == 17, "17 systems: 10 cinema, 6 bass managed, stereo", (double) t.chains.size());
        check (cinema (t, 0.0, false).weight == 1.0 && cinema (t, 0.0, true).weight == 1.0, "on time counts fully, either polarity", 0.0);
        check (home (t, 80.0, false).weight == 1.0, "bass managed counts fully", home (t, 80.0, false).weight);
        check (worst < 0.05, "screen alone: every system within 0.05 dB of the reference", worst);
        check (t.robustness > 99.9, "screen alone: robustness 100", t.robustness);
        check (t.lfeShare < 0.01, "screen alone: LFE carries nothing", t.lfeShare);

        // The on-time weight is twice the late one and twice the early one
        check (std::abs (cinema (t, 0.0, false).weight / cinema (t, 2.5, false).weight - 2.0) < 1e-9, "0 ms counts double", 0.0);
        check (std::abs (cinema (t, 2.5, false).weight / cinema (t, -2.5, false).weight - 1.0) < 1e-9, "early and late count alike", 0.0);
    }
    {
        // LFE alone: the cinema systems all agree, a stereo downmix has nothing left
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.lfe = n;
        run (e, s);
        const auto t = sim.compute (e.result());
        check (std::abs (cinema (t, 5.0, true).changeDb) < 0.05, "LFE alone: delay and polarity change nothing", cinema (t, 5.0, true).changeDb);
        check (stereo (t).changeDb < -20.0, "LFE alone: nothing left in stereo", stereo (t).changeDb);
        check (t.lfeShare > 0.99, "LFE alone: it carries all the bass", t.lfeShare);
        // Wide-band bass: the part below 40 Hz counts less and less down to 25 Hz; ceiling 1
        check (t.dependence > 0.90, "LFE alone, 20-120 Hz: near-total dependence", t.dependence);
        check (t.robustness < 10.0, "LFE alone: under 10, the room alone decides the bass", t.robustness);

        double worst = 0.0;

        for (const auto& c : t.chains)
            if (c.kind != ChainResult::Kind::stereo)
                worst = std::max (worst, c.weight * c.penalty);

        check (std::abs (t.interaction - worst) < 1e-12, "interaction = worst relevance x penalty", t.interaction);

        // All the bass in the LFE: it follows the subwoofer level exactly, which is expected
        check (t.levelDependence < 0.02, "LFE alone: no level dependence, the bass moves as expected", t.levelDependence);
        check (std::abs (t.robustness - 100.0 * (1.0 - t.interaction) * (1.0 - t.dependence) * (1.0 - 0.15 * t.levelDependence)) < 1e-9,
               "robustness = 100 (1 - interaction) (1 - dependence) (1 - 0.15 level dependence)", t.robustness);
        std::printf ("  LFE alone: robustness %.0f\n", t.robustness);
    }
    {
        // Unrelated signals, the LFE as loud as the screen channel when heard.
        // The simulator reads the last 0.2 s or so: two noises of equal long-term
        // level differ by up to about 1 dB over that span, hence the tolerances.
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.l = n;
        s.lfe = scaled (noise (10.0, 72), asHeard);
        run (e, s);
        const auto t = sim.compute (e.result());
        check (std::abs (t.lfeBalanceDb) < 1.2, "equal levels: balance 0 dB", t.lfeBalanceDb);
        check (std::abs (cinema (t, 5.0, true).changeDb) < 0.3, "unrelated: delay and polarity change nothing", cinema (t, 5.0, true).changeDb);
        check (std::abs (stereo (t).changeDb + 3.01) < 0.8, "half the bass in the LFE: stereo loses about 3 dB", stereo (t).changeDb);
        check (std::abs (home (t, 80.0, false).changeDb) < 1.0, "unrelated: bass management stays close", home (t, 80.0, false).changeDb);
        std::printf ("  unrelated, equal levels: robustness %.0f, level dependence %.2f (%+.1f dB)\n", t.robustness, t.levelDependence, t.levelDeviationDb);
        check (t.levelDependence < 0.15, "unrelated LFE: the bass follows the subwoofer level as expected", t.levelDependence);
    }
    {
        // The case to avoid: unrelated, but the LFE 6 dB above the screen channel
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.l = n;
        s.lfe = scaled (noise (10.0, 73), 2.0 * asHeard);
        run (e, s);
        const auto t = sim.compute (e.result());
        check (std::abs (t.lfeBalanceDb - 6.02) < 1.2, "LFE 6 dB above the screen", t.lfeBalanceDb);
        check (std::abs (stereo (t).changeDb + 6.99) < 1.0, "LFE 6 dB up: stereo loses about 7 dB", stereo (t).changeDb);
        std::printf ("  unrelated, LFE 6 dB above: robustness %.0f\n", t.robustness);
    }
    {
        // Same signal in L and (as heard) in the LFE
        LfeEngine e (sampleRate);
        Scene s;
        s.l = n;
        s.lfe = scaled (n, asHeard);
        run (e, s);
        const auto t = sim.compute (e.result());
        check (cinema (t, 0.0, true).changeDb < -10.0, "identical: an inverted subwoofer cancels", cinema (t, 0.0, true).changeDb);
        check (cinema (t, 0.0, true).penalty > 0.99, "identical: that system gets the full penalty", cinema (t, 0.0, true).penalty);
        check (std::abs (stereo (t).changeDb + 6.02) < 0.5, "identical: stereo loses 6 dB", stereo (t).changeDb);
        check (t.overlap[1].amount > 0.9 && t.overlap[2].amount > 0.9, "equal levels: full overlap", t.overlap[1].amount);
        check (t.robustness < 1.0, "identical: 0, an inverted subwoofer cancels the bass", t.robustness);
        check (t.worstChain >= 0 && t.chains[(size_t) t.worstChain].inverted, "identical: the worst system has the subwoofer inverted",
               (double) t.worstChain);
        std::printf ("  identical signals: robustness %.0f, level dependence %.2f (%+.1f dB)\n", t.robustness, t.levelDependence, t.levelDeviationDb);
        check (t.levelDependence > 0.5 && t.levelMeanDb > 1.5 && std::abs (t.levelDeviationDb) > 20.0,
               "identical signals: a subwoofer level error swings the bass far beyond what is expected", t.levelMeanDb);

        // Taken as recorded (no +10 dB), the LFE is 10 dB under the screen: nothing cancels any more
        const auto asRecorded = sim.compute (e.result(), PlaybackSimulator::Level::strong, 0.0);
        check (asRecorded.lfeBalanceDb < -9.0 && asRecorded.lfeBalanceDb > -11.0, "LFE gain 0 dB: the LFE is weighed as recorded", asRecorded.lfeBalanceDb);
        check (asRecorded.robustness > t.robustness + 5.0, "LFE gain 0 dB: the score follows", asRecorded.robustness);

        // The choices are adjustable
        PlaybackSimulator tuned;
        tuned.tuning.toleranceDb = { { 3.0, 2.0, 4.0 } };
        tuned.tuning.levelDependenceWeight = 0.0;
        check (tuned.compute (e.result(), PlaybackSimulator::Level::strong, 0.0).robustness > asRecorded.robustness,
               "tuning: a wider tolerance raises the score", tuned.compute (e.result(), PlaybackSimulator::Level::strong, 0.0).robustness);
    }
    {
        // LFE 5 ms late in the mix: a subwoofer 5 ms early realigns it. Against the
        // reference (the mix as it is, average cosine -0.374 over the band) that is +5.0 dB.
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.l = n;
        s.lfe = delayed (scaled (n, asHeard), 0.005);
        run (e, s);
        const auto t = sim.compute (e.result());
        check (std::abs (cinema (t, -5.0, false).changeDb - 5.04) < 0.6, "late LFE realigned by the system: +5.0 dB",
               cinema (t, -5.0, false).changeDb);
        check (std::abs (cinema (t, 0.0, false).changeDb) < 1e-9, "the reference itself reads 0 dB", cinema (t, 0.0, false).changeDb);
        std::printf ("  LFE 5 ms late: robustness %.0f\n", t.robustness);

        // One band, one system, checked by hand: a level difference d in a band
        // costs (|d| - 1) / (D - 1), D being the dB that double loudness there
        const auto& c = cinema (t, -5.0, false);
        double expected = 0.0, total = 0.0;
        (void) total;

        for (int b = 0; b < 4; ++b)
            expected = std::max (expected, std::clamp ((std::abs (c.bandChangeDb[(size_t) b]) - 1.0)
                                                         / (PlaybackSimulator::doublingDb[b] - 1.0), 0.0, 1.0));

        check (c.penalty <= expected + 1e-9, "a system's penalty never exceeds its worst band", c.penalty);
    }
    {
        // Infra-bass carried by the LFE alone is felt in a cinema, not heard in a
        // downmix: the lower the tone, the less its absence counts.
        double previous = -1.0;

        for (double hz : { 20.0, 30.0, 40.0, 50.0, 70.0, 100.0 })
        {
            LfeEngine e (sampleRate, 1.0, 0.0);
            Scene s;
            s.lfe = sine (hz, -20.0, 5.0);
            run (e, s);
            const auto t = sim.compute (e.result());
            check (t.dependence >= previous - 1e-9, "dependence grows with frequency", t.dependence);
            previous = t.dependence;

            if (hz <= 20.0)
                check (t.dependence > 0.25 && t.dependence < 0.35 && t.robustness > 65.0 && t.robustness < 75.0,
                       "20 Hz in the LFE alone: infra-bass counts for 30 %", t.robustness);

            if (hz >= 70.0)
                check (std::abs (t.dependence - PlaybackSimulator::dependenceCeiling) < 0.01, "70 Hz and above: full dependence", t.dependence);

            std::printf ("  LFE alone, %3.0f Hz tone: dependence %.2f, robustness %.0f\n", hz, t.dependence, t.robustness);
        }

        // Audibility point by point: two tones in the same rating band (30-60 Hz), at the same
        // level near threshold, are not heard alike: the threshold is about 15 dB higher at 33 Hz
        {
            double heard[2];
            int i = 0;

            for (double hz : { 33.0, 57.0 })
            {
                LfeEngine e (sampleRate, 1.0, 0.0);
                Scene s;
                s.c = sine (hz, -55.0, 5.0);
                run (e, s);
                heard[i++] = sim.compute (e.result()).bandAudible[1];
            }

            std::printf ("  tone at -55 dBFS, 85 dB(C): heard by %.0f %% at 33 Hz, %.0f %% at 57 Hz (same band)\n", 100.0 * heard[0], 100.0 * heard[1]);
            check (heard[1] > heard[0] + 0.2, "audibility follows the threshold inside a band, not only at its centre", heard[1] - heard[0]);
        }

        // The cancellation risk is unchanged: a 20 Hz tone copied into the centre still scores 0
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.c = sine (25.0, -20.0, 5.0);
        s.lfe = scaled (s.c, asHeard);
        run (e, s);
        check (sim.compute (e.result()).robustness < 1.0, "infra-bass shared with the screen still cancels with an inverted sub",
               sim.compute (e.result()).robustness);
    }
    {
        // The three levels on the same mix (LFE 10 dB below the screen, 60 % copied
        // from it): Soft is the most lenient, Strong the strictest; the anchors hold.
        const auto other = noise (10.0, 76);
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.l = n;
        s.lfe = scaled (sum (scaled (n, 0.6), scaled (other, 0.8)), 0.316 * asHeard);
        run (e, s);
        using Level = PlaybackSimulator::Level;
        const double soft = sim.compute (e.result(), Level::soft).robustness;
        const double medium = sim.compute (e.result(), Level::medium).robustness;
        const double strong = sim.compute (e.result(), Level::strong).robustness;
        check (soft >= medium && medium >= strong, "Soft >= Medium >= Strong", soft - strong);
        check (soft > 90.0, "Soft: a moderately shared, quiet LFE keeps a high score", soft);
        check (strong < medium - 2.0, "Strong is clearly stricter than Medium", medium - strong);
        std::printf ("  LFE 10 dB below, 60 %% shared: Soft %.0f, Medium %.0f, Strong %.0f\n", soft, medium, strong);

        LfeEngine same (sampleRate, 1.0, 0.0);
        Scene copy;
        copy.l = n;
        copy.lfe = scaled (n, asHeard);
        run (same, copy);
        check (sim.compute (same.result(), Level::soft).robustness < 1.0, "identical LFE scores 0 even at Soft",
               sim.compute (same.result(), Level::soft).robustness);
        check (PlaybackSimulator::alertThreshold (Level::soft) == 0.90 && PlaybackSimulator::alertThreshold (Level::medium) == 0.75
                 && PlaybackSimulator::alertThreshold (Level::strong) == 0.50, "alert thresholds 0.90 / 0.75 / 0.50", 0.0);
    }
    {
        // Between the anchors: an LFE that only partly copies the screen, 10 dB
        // below it when heard (so that dependence stays out of the way)
        const auto other = noise (10.0, 74);
        double previous = 101.0;

        for (double shared : { 0.0, 0.3, 0.6, 1.0 })
        {
            LfeEngine e (sampleRate, 1.0, 0.0);
            Scene s;
            s.l = n;
            s.lfe = scaled (sum (scaled (n, shared), scaled (other, std::sqrt (1.0 - shared * shared))), 0.316 * asHeard);
            run (e, s);
            const auto t = sim.compute (e.result());
            check (t.robustness <= previous + 1.0, "the more the LFE copies the screen, the lower the score", t.robustness);
            previous = t.robustness;
            std::printf ("  LFE 10 dB below the screen, %.0f %% shared: robustness %.0f, level dependence %.2f (%+.1f dB)\n",
                         100.0 * shared, t.robustness, t.levelDependence, t.levelDeviationDb);
        }
    }
    {
        // ISO 226:2003. Known points of the standard: the threshold-side values
        // quoted in the documentation, the round trip, and the dB that double
        // loudness around 80 phon.
        check (std::abs (PlaybackSimulator::iso226Phon (40.0, PlaybackSimulator::iso226Spl (40.0, 63.0)) - 63.0) < 1e-6, "ISO 226: SPL -> phon -> SPL", 0.0);
        check (std::abs (PlaybackSimulator::iso226Spl (25.0, 90.0) - PlaybackSimulator::iso226Spl (25.0, 80.0) - 5.0) < 0.1
                 && std::abs (PlaybackSimulator::iso226Spl (100.0, 90.0) - PlaybackSimulator::iso226Spl (100.0, 80.0) - 6.9) < 0.1,
               "ISO 226: 5.0 dB double loudness at 25 Hz, 6.9 dB at 100 Hz, around 80 phon",
               PlaybackSimulator::iso226Spl (25.0, 90.0) - PlaybackSimulator::iso226Spl (25.0, 80.0));
        check (PlaybackSimulator::iso226Spl (25.0, 60.0) > PlaybackSimulator::iso226Spl (100.0, 60.0) + 15.0,
               "ISO 226: a low tone needs far more level for the same loudness",
               PlaybackSimulator::iso226Spl (25.0, 60.0) - PlaybackSimulator::iso226Spl (100.0, 60.0));

        // A 50 Hz tone peaking at -20 dBFS in the centre, i.e. -23 dBFS RMS: 3 dB under
        // the calibration level, 82 dB SPL at the cinema calibration
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.c = sine (50.0, -20.0, 6.0);
        run (e, s);
        PlaybackSimulator loud, quiet;
        quiet.tuning.listeningLevelDbC = 76.0;
        const auto a85 = loud.compute (e.result()), a76 = quiet.compute (e.result());
        std::printf ("  50 Hz at -23 dBFS RMS: %.1f dB SPL and %.0f phon at 85 dB(C); %.1f dB SPL and %.0f phon at 76 dB(C)\n",
                     a85.bandSplDb[1], a85.bandPhon[1], a76.bandSplDb[1], a76.bandPhon[1]);
        check (std::abs (a85.bandSplDb[1] - 82.0) < 0.3, "listening level: a tone 3 dB under the calibration signal reads 3 dB under the level", a85.bandSplDb[1]);
        check (std::abs (a85.bandSplDb[1] - a76.bandSplDb[1] - 9.0) < 0.01 && a85.bandPhon[1] > a76.bandPhon[1] + 10.0,
               "listening level: 9 dB lower is heard much quieter in the bass", a85.bandPhon[1] - a76.bandPhon[1]);
    }
    {
        // ISO 226:2023: at the threshold a tone is at 2.4 phon (the 1 kHz threshold), and Table 1 gives back itself
        check (std::abs (PlaybackSimulator::iso226Phon (40.0, 51.1) - 2.4) < 1e-9 && std::abs (PlaybackSimulator::iso226Spl (63.0, 2.4) - 37.5) < 1e-9,
               "ISO 226:2023: the threshold of hearing is 2.4 phon at every frequency", PlaybackSimulator::iso226Phon (40.0, 51.1));
        check (std::abs (PlaybackSimulator::iso226Phon (50.0, PlaybackSimulator::iso226Spl (50.0, 60.0)) - 60.0) < 1e-9,
               "ISO 226:2023: Formula (2) is the inverse of Formula (1)", PlaybackSimulator::iso226Phon (50.0, PlaybackSimulator::iso226Spl (50.0, 60.0)));

        // Audibility (ISO 226:2023 threshold, as ISO 389-7:2019; listeners spread by 5 dB)
        check (std::abs (PlaybackSimulator::hearingThresholdDb (20.0) - 78.1) < 1e-9
                 && std::abs (PlaybackSimulator::hearingThresholdDb (63.0) - 37.5) < 1e-9, "threshold of hearing: ISO 226 values", 0.0);
        check (std::abs (PlaybackSimulator::audibleShare (40.0, PlaybackSimulator::hearingThresholdDb (40.0), 5.0) - 0.5) < 1e-9,
               "at the threshold, half the listeners hear it", 0.0);
        check (PlaybackSimulator::audibleShare (40.0, PlaybackSimulator::hearingThresholdDb (40.0) + 10.0, 5.0) > 0.97
                 && PlaybackSimulator::audibleShare (40.0, PlaybackSimulator::hearingThresholdDb (40.0) - 10.0, 5.0) < 0.03,
               "two sigma above: nearly all hear it; two below: nearly none", 0.0);

        // The same LFE-identical-to-screen mix, played loud and played very quietly
        const auto identical = [] (double gain)
        {
            const auto bass = scaled (noise (8.0, 95), gain);
            LfeEngine e (sampleRate, 1.0, 0.0);
            Scene s;
            s.c = bass;
            s.lfe = scaled (bass, std::pow (10.0, -10.0 / 20.0));
            run (e, s);
            return e.result();
        };

        PlaybackSimulator sim, off, quietRoom;
        off.tuning.audibility = false;
        quietRoom.tuning.listeningLevelDbC = 70.0;
        const auto loud = identical (1.0), faint = identical (0.0005);     // white noise at -20 and at -86 dBFS
        const double loudScore = sim.compute (loud).robustness, faintScore = sim.compute (faint).robustness;
        std::printf ("  identical LFE: robustness %.0f at -20 dBFS, %.0f at -86 dBFS (inaudible at 85 dB(C)), %.0f with audibility off\n",
                     loudScore, faintScore, off.compute (faint).robustness);
        check (loudScore < 1.0, "identical LFE, audible: 0", loudScore);
        check (faintScore > 99.0, "identical LFE, under the threshold of hearing: nothing heard changes, 100", faintScore);
        check (off.compute (faint).robustness < 1.0, "audibility off: level does not matter", off.compute (faint).robustness);

        // A level that a quieter room no longer hears
        const auto middle = identical (0.02);      // -54 dBFS
        std::printf ("  identical LFE at -54 dBFS: robustness %.0f at 85 dB(C), %.0f at 70 dB(C)\n",
                     sim.compute (middle).robustness, quietRoom.compute (middle).robustness);
        check (quietRoom.compute (middle).robustness > sim.compute (middle).robustness + 20.0,
               "a quieter listening level hears less of the bass", quietRoom.compute (middle).robustness);

        // LFE level dependence: every cinema and bass-managed system, at every subwoofer level
        check (sim.compute (loud).levelScenarios == 16 * PlaybackSimulator::numLevelSteps,
               "level dependence rated for each of the 16 cinema and home systems at each subwoofer level", sim.compute (loud).levelScenarios);
    }
    {
        // The bass in L and R, a quieter centre, and an LFE that is the centre
        // inverted: against the centre alone it cancels, against the whole
        // screen it is small. Correlation says -1; the level-weighted value
        // says how little it matters.
        const auto bass = noise (8.0, 91);
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.l = bass;
        s.r = bass;
        s.c = scaled (bass, 0.4);
        s.lfe = scaled (bass, -0.4);
        run (e, s);
        const auto& a = e.result();
        std::printf ("  LFE = inverted centre under a louder L and R: correlation %.2f, level-weighted %.2f\n",
                     over (a, a.correlation[chC], 30.0, 100.0).hi, over (a, a.interaction[chC], 30.0, 100.0).lo);
        check (over (a, a.correlation[chC], 30.0, 100.0).hi < -0.95, "small opposed LFE: correlation with the centre is -1", over (a, a.correlation[chC], 30.0, 100.0).hi);
        check (over (a, a.interaction[chC], 30.0, 100.0).lo > -0.12, "small opposed LFE: weighed against the whole screen, it counts little",
               over (a, a.interaction[chC], 30.0, 100.0).lo);
    }
    {
        // The same bass in L, C and R, and an unrelated LFE 9 dB below the screen:
        // the LFE carries about a tenth of the bass, and leaving it out costs
        // half a dB. How L, C and R fold into two channels is not its doing.
        const auto bass = noise (10.0, 81);
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.l = bass;
        s.c = bass;
        s.r = bass;
        s.lfe = scaled (noise (10.0, 82), 3.0 * 0.35 * asHeard);
        run (e, s);
        const auto t = sim.compute (e.result());
        std::printf ("  same bass in L, C, R; LFE %+.1f dB against the screen, %.0f %% of the bass: dependence %.2f, robustness %.0f\n",
                     t.lfeBalanceDb, 100.0 * t.lfeShare, t.dependence, t.robustness);
        check (t.lfeShare > 0.07 && t.lfeShare < 0.16, "small LFE under a screen sharing its bass: about a tenth of the bass", t.lfeShare);
        check (t.dependence < 0.02, "small LFE: leaving it out costs nothing audible", t.dependence);
        check (std::abs (t.downmixChangeDb) < 0.8, "small LFE: the bass without it is within 0.8 dB", t.downmixChangeDb);
        check (t.robustness > 95.0, "small unrelated LFE: score near 100", t.robustness);
    }

    {
        // Equal levels, partly shared
        const auto other = noise (10.0, 75);

        for (double shared : { 0.3, 0.6 })
        {
            LfeEngine e (sampleRate, 1.0, 0.0);
            Scene s;
            s.l = n;
            s.lfe = scaled (sum (scaled (n, shared), scaled (other, std::sqrt (1.0 - shared * shared))), asHeard);
            run (e, s);
            const auto loud = sim.compute (e.result());
            std::printf ("  LFE as loud as the screen, %.0f %% shared: robustness %.0f, level dependence %.2f (%+.1f dB)\n",
                         100.0 * shared, loud.robustness, loud.levelDependence, loud.levelDeviationDb);
        }
    }
    {
        // Band levels: a -20 dBFS sine at 75 Hz is -23 dBFS RMS in the 60-90 Hz band
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.l = sine (75.0, -20.0, 6.0);
        s.lfe = sine (40.0, -40.0, 6.0);
        run (e, s);
        const auto t = sim.compute (e.result());
        check (std::abs (t.overlap[1].screenDb + 23.0) < 1.0, "band RMS level of a sine", t.overlap[1].screenDb);
        check (std::abs (t.overlap[0].lfeDb + 33.0) < 1.0, "LFE band level includes its +10 dB", t.overlap[0].lfeDb);
        check (t.overlap[1].amount < 0.01, "no overlap when only one of them plays in the band", t.overlap[1].amount);
    }
    {
        // Whole-session polarity
        LfeEngine opposed (sampleRate, 1.0, 0.0);
        Scene c;
        c.l = n;
        c.lfe = scaled (n, -0.2);
        run (opposed, c);
        check (opposed.globalCorrelation (chL) < -0.95, "session polarity: opposed LFE reads -1", opposed.globalCorrelation (chL));
        check (std::abs (opposed.globalSeconds() - 10.0) < 0.5, "session time", opposed.globalSeconds());
        opposed.resetGlobal();
        check (opposed.globalSeconds() == 0.0, "session reset", opposed.globalSeconds());
    }
}

static void testInertiaAndAutoReset()
{
    std::printf ("display inertia, automatic reset\n");
    const double frame = 1.0 / 60.0;

    {
        // A value that flickers between 0.1 and 0.9 ten times a second: the read-out settles near 0.5
        Inertia shown;
        double lo = 1e9, hi = -1e9;

        for (int i = 0; i < 600; ++i)
        {
            const double v = shown.update ((i / 6) % 2 == 0 ? 0.1 : 0.9, frame);

            if (i >= 360)
            {
                lo = std::min (lo, v);
                hi = std::max (hi, v);
            }
        }

        check (lo > 0.40 && hi < 0.60, "flicker 0.1 / 0.9: read-out stays near the middle", hi - lo);
        check (hi - lo < 0.08, "flicker 0.1 / 0.9: read-out barely moves", hi - lo);
        std::printf ("  flicker between 0.1 and 0.9: read-out moves by %.3f\n", hi - lo);
    }
    {
        // A real change from 1 to 2: followed quickly
        Inertia shown;

        for (int i = 0; i < 120; ++i)
            shown.update (1.0, frame);

        check (std::abs (shown.value() - 1.0) < 1e-12, "steady input: read-out equals it", shown.value());
        int frames = 0;

        while (shown.update (2.0, frame) < 1.9 && frames < 600)
            ++frames;

        check (frames * frame < 0.8, "step from 1 to 2: 90 % of the way in under 0.8 s", frames * frame);
        std::printf ("  step from 1 to 2: 90 %% reached after %.2f s\n", frames * frame);
    }
    {
        // Slow drift on top of flicker: the read-out follows the drift, not the flicker
        Inertia shown;
        double worst = 0.0;

        for (int i = 0; i < 1200; ++i)
        {
            const double trend = i * frame * 0.05;      // +0.05 per second
            const double v = shown.update (trend + ((i / 3) % 2 == 0 ? -0.3 : 0.3), frame);

            if (i > 300)
                worst = std::max (worst, std::abs (v - trend));
        }

        check (worst < 0.12, "drift under flicker: read-out stays on the drift", worst);
    }
    {
        // The LFE stops: what was measured against it is cleared by itself
        const auto n = noise (9.0, 81);
        Scene s;
        s.l = n;
        s.lfe = Signal (n.begin(), n.begin() + 4 * sampleRate);     // LFE present for 4 s only

        LfeEngine e (sampleRate, 1.0, 0.0);
        const auto data = s.interleaved();
        int resets = 0;
        double before = 0.0, after = 1.0, resetAt = -1.0;

        for (size_t pos = 0; pos + 800 <= s.length(); pos += 800)
        {
            e.push (data.data() + pos * numChannels, 800);
            e.analyze ({});
            const double t = (double) pos / sampleRate;

            if (e.takeAutoReset())
            {
                ++resets;
                resetAt = t;
            }

            const double c = over (e.result(), e.result().correlation[chL]).mean;

            if (t > 3.0 && t < 3.9)
                before = c;

            if (t > 7.5)
                after = std::abs (c);
        }

        check (before > 0.95, "while the LFE plays: correlation +1", before);
        check (resets == 1, "the LFE goes silent: one automatic reset", resets);
        check (resetAt > 4.0 && resetAt < 6.5, "reset within about 2 s of the LFE stopping", resetAt - 4.0);
        check (after < 0.02, "after the reset: nothing left on the correlation curve", after);
        check (e.lfeLevelDb() < -100.0, "LFE level reads as silent", e.lfeLevelDb());
        std::printf ("  automatic reset %.1f s after the LFE stopped\n", resetAt - 4.0);
    }
    {
        // A short LFE passage followed by digital silence: the engine resets, the
        // alert on screen is held for a moment, then fades (it is not wiped)
        const auto n = noise (8.0, 83);
        Scene s;
        s.l = n;
        s.lfe = Signal (n.begin(), n.begin() + sampleRate);      // 1 s of LFE, then silence
        LfeEngine e (sampleRate, 1.0, 0.0);
        AlertTracker tracker;
        AlertTracker::Options o;
        const auto data = s.interleaved();
        double resetAt = -1.0, peak = 0.0, heldAfter = 0.0, late = 1.0;
        const double frame = 800.0 / sampleRate;

        for (size_t pos = 0; pos + 800 <= s.length(); pos += 800)
        {
            e.push (data.data() + pos * numChannels, 800);
            e.analyze ({});
            const double t = (double) pos / sampleRate;

            if (e.takeAutoReset())
                resetAt = t;

            tracker.update (e.result(), o, frame);
            peak = std::max (peak, tracker.speakerLevel[0]);

            if (resetAt > 0.0 && t > resetAt + 0.8 && t < resetAt + 1.0)
                heldAfter = tracker.speakerLevel[0];

            if (resetAt > 0.0 && t > resetAt + 4.5)
                late = tracker.speakerLevel[0];
        }

        std::printf ("  short LFE then silence: alert %.2f at its peak, %.2f 0.9 s after the reset, %.2f 4.5 s after\n", peak, heldAfter, late);
        check (resetAt > 0.0 && peak > 0.9 && heldAfter > 0.5 * peak, "after the automatic reset the alert stays readable for a moment", heldAfter);
        check (late < 0.1, "then it fades", late);
    }
    {
        // A very quiet LFE (-80 dBFS) is not silence: no reset
        const auto n = noise (6.0, 82);
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.l = n;
        s.lfe = scaled (n, 0.001);
        int resets = 0;
        run (e, s, {}, [&] (double) { resets += e.takeAutoReset(); });
        check (resets == 0, "LFE at -80 dBFS: still analysed, no reset", resets);
        check (over (e.result(), e.result().correlation[chL]).lo > 0.95, "LFE at -80 dBFS: correlation +1", 0.0);
    }
}

static void testRunner()
{
    std::printf ("analysis at the pace of the audio (20 ms steps)\n");

    // Kicks in C, the same 12 ms late in the LFE, over a bed of noise
    Scene s;
    s.c = sum (noise (6.0, 61), sine (45.0, -12.0, 6.0));
    s.lfe = sum (scaled (noise (6.0, 61), 0.5), noise (6.0, 62));
    const auto data = s.interleaved();
    const int frames = (int) s.length();

    struct Result { Snapshot last; long long steps = 0; int reads = 0; };

    // Feed in blocks of `block` frames and read every `readEvery` frames, as a display would
    const auto runWith = [&] (int block, int readEvery)
    {
        AnalysisRunner runner (sampleRate, 1.0);
        Result r;
        int sinceRead = 0;

        for (int pos = 0; pos < frames; pos += block)
        {
            const int n = std::min (block, frames - pos);
            runner.feed (data.data() + (size_t) pos * numChannels, n);
            sinceRead += n;

            if (sinceRead >= readEvery)
            {
                sinceRead = 0;
                Snapshot snap;

                if (runner.take (snap))
                {
                    ++r.reads;
                    r.last = snap;
                }
            }
        }

        Snapshot snap;

        if (runner.take (snap))
            r.last = snap;

        r.steps = r.last.step;
        return r;
    };

    const auto a = runWith (1, 800), b = runWith (517, 800), c = runWith (4096, 96000);

    const long long expected = frames / (int) std::lround (AnalysisRunner::hopSeconds * sampleRate);
    check (a.steps == expected && b.steps == expected && c.steps == expected, "one analysis every 20 ms of audio, whatever the blocks",
           (double) c.steps);

    double worst = 0.0;

    for (size_t i = 0; i < a.last.analysis.facadeDb.size(); ++i)
        worst = std::max ({ worst, std::abs (a.last.analysis.facadeDb[i] - b.last.analysis.facadeDb[i]),
                            std::abs (a.last.analysis.facadeDb[i] - c.last.analysis.facadeDb[i]) });

    for (size_t g = 0; g < a.last.analysis.grid.size(); ++g)
        worst = std::max ({ worst, std::abs (a.last.analysis.correlation[chC][g] - b.last.analysis.correlation[chC][g]),
                            std::abs (a.last.analysis.correlation[chC][g] - c.last.analysis.correlation[chC][g]) });

    check (worst < 1e-9, "same result whatever the block size and however often it is read", worst);

    // A display that stops looking for 2 s loses nothing: the next read sees it all
    check (c.reads >= 1 && c.steps == expected, "a stalled reader misses no step", (double) c.reads);

    // Peaks accumulate between reads
    {
        AnalysisRunner runner (sampleRate, 1.0);
        std::vector<float> click (4 * 2000, 0.0f);
        click[(size_t) 1000 * 4 + chC] = 0.5f;
        runner.feed (click.data(), 2000);
        Snapshot snap;
        check (runner.take (snap) && std::abs (snap.screenPeak[1] - 0.5) < 1e-9, "a peak between two reads is kept", snap.screenPeak[1]);
        check (! runner.take (snap), "nothing new: nothing to read", 0.0);
    }
}

static void testRooms()
{
    std::printf ("room simulation (modal sum, fifteen rooms)\n");

    // The first axial mode of a 10 m long room sits at c / 2L = 17.15 Hz
    {
        const RoomSpec room { "test", "test", 10.0, 6.0, 4.0, 0.5 };
        const std::array<double, 3> front { { 0.1, 3.0, 2.0 } }, back { { 9.9, 3.0, 2.0 } };
        const auto peakBetween = [&] (double lo, double hi)
        {
            double best = lo, value = 0.0;

            for (double f = lo; f <= hi; f += 0.05)
                if (std::abs (RoomSimulator::response (room, front, back, f)) > value)
                {
                    value = std::abs (RoomSimulator::response (room, front, back, f));
                    best = f;
                }

            return best;
        };

        check (std::abs (peakBetween (13.0, 22.0) - RoomSimulator::speedOfSound / 20.0) < 0.5, "first axial mode at c / 2L = 17.15 Hz", peakBetween (13.0, 22.0));
        check (std::abs (peakBetween (30.0, 37.0) - RoomSimulator::speedOfSound / 10.0) < 0.6, "second axial mode at c / L = 34.3 Hz", peakBetween (30.0, 37.0));
    }

    const auto scene = [] (double shared)
    {
        const auto n = noise (8.0, 101), other = noise (8.0, 102);
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.c = n;
        s.lfe = scaled (sum (scaled (n, shared), scaled (other, std::sqrt (1.0 - shared * shared))), std::pow (10.0, -10.0 / 20.0));
        run (e, s);
        return e.result();
    };

    PlaybackSimulator sim;
    RoomSimulator rooms;

    const auto apartAnalysis = scene (0.0), sharedAnalysis = scene (1.0);
    const auto started = std::chrono::steady_clock::now();
    const auto apart = rooms.compute (apartAnalysis, sim.compute (apartAnalysis), sim.tuning, PlaybackSimulator::Level::strong, 10.0);
    const double prepareSeconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();
    const auto again = std::chrono::steady_clock::now();
    const auto shared = rooms.compute (sharedAnalysis, sim.compute (sharedAnalysis), sim.tuning, PlaybackSimulator::Level::strong, 10.0);
    const double updateMs = 1000.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - again).count();
    std::printf ("  fifteen rooms: prepared in %.2f s, then %.2f ms per update\n", prepareSeconds, updateMs);

    check (apart.rooms.size() == 15 && shared.rooms.size() == 15, "fifteen rooms", (double) apart.rooms.size());

    double worstApart = 0.0, lowestApart = 100.0;

    for (const auto& r : apart.rooms)
    {
        lowestApart = std::min (lowestApart, r.roomScore);

        for (double d : r.seatChangeDb)
            worstApart = std::max (worstApart, std::abs (d));
    }

    check (worstApart < 0.5 && lowestApart > 99.0, "decorrelated LFE: the interaction adds nothing at any seat of any room", worstApart);

    for (const auto& r : shared.rooms)
        std::printf ("    %-30s room %5.1f  seats %+5.1f .. %+5.1f dB (median %+5.1f)\n", r.spec.name.c_str(), r.roomScore,
                     *std::min_element (r.seatChangeDb.begin(), r.seatChangeDb.end()), *std::max_element (r.seatChangeDb.begin(), r.seatChangeDb.end()),
                     r.medianDb);

    double meanShared = 0.0;

    for (const auto& r : shared.rooms)
        meanShared += r.roomScore / 15.0;

    int thinner = 0;

    for (const auto& r : shared.rooms)
        thinner += r.medianDb < -0.5;

    check (meanShared < 85.0 && thinner >= 12,
           "LFE identical to the screen and as loud: in most rooms the seats lose the in-phase gain heard on the stage", meanShared);
    check (shared.rooms[0].combinedScore <= sim.compute (sharedAnalysis).robustness + 1e-9, "room result = robustness x room score", 0.0);

    // The listening level: the louder, the more of what the rooms do is heard
    {
        const auto quietMix = [] ()
        {
            const auto n = noise (8.0, 103);
            LfeEngine e (sampleRate, 1.0, 0.0);
            Scene s;
            s.c = scaled (n, 0.03);
            s.lfe = scaled (n, 0.03 * std::pow (10.0, -10.0 / 20.0));
            run (e, s);
            return e.result();
        }();

        double mean[2] = {};
        int index = 0;

        for (double dbC : { 70.0, 90.0 })
        {
            PlaybackSimulator at;
            at.tuning.listeningLevelDbC = dbC;
            RoomSimulator roomsAt;
            const auto r = roomsAt.compute (quietMix, at.compute (quietMix), at.tuning, PlaybackSimulator::Level::strong, 10.0);

            for (const auto& room : r.rooms)
                mean[index] += room.roomScore / (double) r.rooms.size();

            ++index;
        }

        std::printf ("  identical LFE at -50 dBFS: mean room score %.0f at 70 dB(C), %.0f at 90 dB(C)\n", mean[0], mean[1]);
        check (mean[1] < mean[0] - 5.0, "a louder listening level makes more of the rooms' faults audible", mean[1]);
    }

    // Ticked rooms enter the score as further systems: the worst one counts
    {
        const auto selection = RoomSimulator::defaultSelection();
        int ticked = 0;

        for (bool on : selection)
            ticked += on;

        check (ticked == 5 && ! selection.front() && ! selection.back(), "five middle rooms ticked by default", ticked);

        auto withRooms = sim.compute (sharedAnalysis);
        const double before = withRooms.robustness;
        RoomSimulator::applyToScore (withRooms, shared, std::vector<bool> (15, true), sim.tuning.levelDependenceWeight);
        check (withRooms.robustness <= before + 1e-9, "ticked rooms can only lower the score", withRooms.robustness);

        auto apartScore = sim.compute (apartAnalysis);
        const double apartBefore = apartScore.robustness;
        RoomSimulator::applyToScore (apartScore, apart, std::vector<bool> (15, true), sim.tuning.levelDependenceWeight);
        check (std::abs (apartScore.robustness - apartBefore) < 1e-9 && apartScore.worstRoom.empty(),
               "decorrelated LFE: the rooms change nothing", apartScore.robustness);

        // A room whose average seat loses more than every electrical system becomes the worst case
        Translation t;
        t.hasBass = true;
        t.interaction = 0.1;
        RoomSimResult r;
        r.hasBass = true;
        RoomResult bad;
        bad.spec.name = "test room";
        bad.roomScore = 40.0;
        r.rooms.push_back (bad);
        RoomSimulator::applyToScore (t, r, { true }, 0.15);
        check (std::abs (t.interaction - 0.6) < 1e-9 && t.worstRoom == "test room" && std::abs (t.robustness - 40.0) < 1e-9,
               "a ticked room scoring 40 sets the interaction to 0.6", t.robustness);
        RoomSimulator::applyToScore (t, r, { false }, 0.15);
    }
}

static void testRoomPro()
{
    std::printf ("Room Sim Pro (configured rooms, export and import)\n");

    // Export / import: every field changed, written, read back, equal
    {
        ProRooms p = ProRooms::defaults();
        double v = 1.0;
        const auto next = [&v] { v += 0.37; return v; };

        for (auto& r : p.rooms)
        {
            r.name = "Room " + std::to_string ((int) next());
            r.counted = ! r.counted;
            r.length = next(); r.width = next(); r.height = next(); r.rt60 = 1.0 + 0.01 * next();
            r.acoustics = 1 - r.acoustics;

            for (size_t m = 0; m < r.materials.size(); ++m)
                r.materials[m] = (int) (3 + m * 2);

            const auto change = [&] (ProSpeaker& sp)
            {
                sp.enabled = ! sp.enabled;
                sp.fx = next() * 0.01; sp.fy = next() * 0.01; sp.fz = next() * 0.01; sp.delayMs = next(); sp.gainDb = next();
                sp.inverted = ! sp.inverted;
                sp.model = 1 + (int) next() % 5;
                sp.yaw = next(); sp.pitch = -next();
            };

            for (auto& sp : r.lcr)      change (sp);
            for (auto& sp : r.subs)     change (sp);
            for (auto& sp : r.fills)    change (sp);

            r.numSubs = 3; r.fillsOn = ! r.fillsOn; r.numFills = 4;
            r.linkLR = false; r.linkFills = false;      // linked speakers would be mirrored on reading
            r.dualLCR = ! r.dualLCR; r.screen = ! r.screen; r.audienceLayout = 2;
            r.aisle = 0.1 * next(); r.ringInner = 0.01 * next(); r.ringOuter = 0.01 * next();
            r.shape = ProRoom::lShape;
            r.people = 2;
            r.centrePercent = next(); r.frontDivergence = next(); r.focusX = 0.01 * next(); r.focusY = 0.01 * next();
            r.firstRow = next(); r.lastRow = next(); r.audienceWidth = next();
            r.rows = 7; r.perRow = 9; r.earHeight = next(); r.rake = next(); r.calibration = 1;
        }

        ProRooms back;
        const bool read = ProRooms::fromText (p.toText(), back);
        check (read && back == p, "export then import gives back every field of the three rooms", read ? 1.0 : 0.0);
        check (! ProRooms::fromText ("not a room file", back), "a file that is not a Room Sim Pro file is refused", 0.0);

        // The links are written and read too
        p.rooms[1].linkLR = true;
        p.rooms[2].linkFills = true;
        p.rooms[1].applyLinks();
        p.rooms[2].applyLinks();
        const bool readLinks = ProRooms::fromText (p.toText(), back);
        check (readLinks && back == p, "linked speakers survive the export too", readLinks ? 1.0 : 0.0);
    }

    // A version 1 file (positions in metres) is read into shares of the room, layout unchanged
    {
        const std::string v1 = "SubGUARD Room Sim Pro\nversion = 1\nroom1.length = 20\nroom1.width = 10\nroom1.height = 5\n"
                               "room1.L.x = 2\nroom1.L.y = 1\nroom1.L.z = 2.5\nroom1.R.y = 7\nroom1.firstRow = 8\n";
        ProRooms back;
        const bool read = ProRooms::fromText (v1, back);
        const auto& r = back.rooms[0];
        const auto l = r.positionOf (r.lcr[0]);
        check (read && std::abs (l[0] - 2.0) < 1e-9 && std::abs (l[1] - 1.0) < 1e-9 && std::abs (l[2] - 2.5) < 1e-9
                 && std::abs (r.positionOf (r.lcr[2])[1] - 7.0) < 1e-9 && std::abs (r.firstRow - 0.4) < 1e-9 && ! r.linkLR,
               "an older file (metres) is read as shares of the room, layout kept", l[0]);
    }

    // Positions follow the room; linked R mirrors L
    {
        auto r = ProRoom::fromSpec (RoomSimulator::defaultRooms()[6]);
        r.lcr[0].fy = 0.2;
        r.applyLinks();
        const double before = r.positionOf (r.lcr[0])[1];
        r.width *= 2.0;
        check (std::abs (r.positionOf (r.lcr[0])[1] - 2.0 * before) < 1e-9 && std::abs (r.lcr[2].fy - 0.8) < 1e-12,
               "resizing the room moves the speakers with it; linked R mirrors L", r.lcr[2].fy);
    }

    // Materials: decay from the surfaces (Kuttruff), checked against Eyring
    {
        auto r = ProRoom::fromSpec (RoomSimulator::defaultRooms()[6]);
        r.acoustics = 1;
        r.materials.fill (ProRoom::materialIndex ("plasterboard"));
        r.people = ProRoom::noPeople;
        const double eyring = r.eyringRt60 (100.0);
        RoomSpec spec { "test", "test", r.length, r.width, r.height, 1.0 };
        RoomSimulator::Mode oblique {}, axial {};

        for (const auto& m : RoomSimulator::modesOf (spec, 160.0))
        {
            const double hz = std::sqrt (m.omega2) / (2.0 * 3.14159265358979);

            if (m.nx != 0 && m.ny != 0 && m.nz != 0 && hz > 95.0 && hz < 105.0 && oblique.nx == 0)    oblique = m;
            if (m.nx != 0 && m.ny == 0 && m.nz == 0 && hz > 60.0 && axial.nx == 0)                   axial = m;
        }

        const double obliqueRt = 3.0 * std::log (10.0) / r.modeDamping (oblique);
        const double axialRt = 3.0 * std::log (10.0) / r.modeDamping (axial);
        std::printf ("  multiplex 200 in plasterboard: Eyring %.2f s, oblique mode %.2f s, axial mode %.2f s, Schroeder %.0f Hz\n",
                     eyring, obliqueRt, axialRt, r.schroederHz());
        check (std::abs (obliqueRt - eyring) / eyring < 0.02, "an oblique mode decays as Eyring's RT60 (same surfaces)", obliqueRt);
        check (axialRt > obliqueRt * 1.1, "an axial mode decays more slowly than an oblique one", axialRt);

        r.materials.fill (ProRoom::materialIndex ("concrete"));
        r.people = ProRoom::noPeople;
        check (r.eyringRt60 (63.0) > 8.0, "a bare concrete box rings for seconds", r.eyringRt60 (63.0));
        check (ProRoom::materialIndex ("open") >= 0 && ProRoom::materialIndex ("nothing") < 0, "materials are found by their saved name", 0.0);
    }

    // The venues: every one prepared and rated
    {
        bool fine = true;

        for (const auto& venue : ProRoom::venues())
        {
            const auto& v = venue.room;
            std::printf ("  %-14s %-34s RT60 63 Hz %.2f s, 125 Hz %.2f s\n", venue.category.c_str(), v.name.c_str(), v.eyringRt60 (63.0), v.eyringRt60 (125.0));
            fine = fine && v.eyringRt60 (63.0) > 0.05 && v.eyringRt60 (63.0) < 20.0;
        }

        check (fine, "the venue presets have sensible reverberation", 0.0);
    }

    const auto scene = [] (double shared, double lfeDb = -10.0)
    {
        const auto n = noise (8.0, 111), other = noise (8.0, 112);
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.c = n;
        s.lfe = lfeDb < -100.0 ? scaled (n, 0.0)
                               : scaled (sum (scaled (n, shared), scaled (other, std::sqrt (1.0 - shared * shared))), std::pow (10.0, lfeDb / 20.0));
        run (e, s);
        return e.result();
    };

    PlaybackSimulator sim;
    const auto sharedAnalysis = scene (1.0), apartAnalysis = scene (0.0);
    const auto sharedT = sim.compute (sharedAnalysis), apartT = sim.compute (apartAnalysis);

    // The standard multiplex 200, set up in Pro: the same answer as Room Sim
    {
        const auto spec = RoomSimulator::defaultRooms()[6];
        const auto started = std::chrono::steady_clock::now();
        auto paths = ProRoomModel::prepare (ProRoom::fromSpec (spec), sharedAnalysis.grid);
        paths.lcrReference = false;     // as Room Sim: the interaction only
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();
        const auto pro = RoomSimulator::rate (paths, sharedAnalysis, sharedT, sim.tuning, PlaybackSimulator::Level::strong, 10.0);

        RoomSimulator standard;
        const auto std15 = standard.compute (sharedAnalysis, sharedT, sim.tuning, PlaybackSimulator::Level::strong, 10.0);
        std::printf ("  multiplex 200 in Pro: room %.1f (Room Sim %.1f), prepared in %.2f s\n", pro.roomScore, std15.rooms[6].roomScore, seconds);
        check (std::abs (pro.roomScore - std15.rooms[6].roomScore) < 5.0, "the same room set up in Pro agrees with Room Sim", pro.roomScore);
    }

    // A mix with no LFE: L C R is the reference, nothing changes at any seat (Lc / Rc fills, a second L C R set)
    {
        const auto noLfe = scene (0.0, -200.0);
        const auto noLfeT = sim.compute (noLfe);
        auto room = ProRoom::fromSpec (RoomSimulator::defaultRooms()[8]);
        room.fillsOn = true;
        room.numFills = 4;
        room.dualLCR = true;
        auto interactionOnly = sim.tuning;
        interactionOnly.coverageWeight = 0.0;       // coverage (the room's own holes) is tested below
        const auto r = RoomSimulator::rate (ProRoomModel::prepare (room, noLfe.grid), noLfe, noLfeT, interactionOnly,
                                            PlaybackSimulator::Level::strong, 10.0);
        double worst = 0.0;

        for (double d : r.seatChangeDb)
            worst = std::max (worst, std::abs (d));

        check (worst < 0.1 && r.roomScore > 99.0, "no LFE in the mix: L C R is the reference, nothing changes at any seat", worst);

        // The layered view too: every grid point, every layer (infra, very low, low)
        double worstLayer = 0.0;
        size_t values = 0;

        for (const auto& layer : r.gridLayerDb)
            for (double v : layer)
                if (! std::isnan (v))
                {
                    worstLayer = std::max (worstLayer, std::abs (v));
                    ++values;
                }

        check (values > 0 && worstLayer < 0.1, "no LFE: the three layers stay flat over the whole floor", worstLayer);
        check (! r.seatHeardPhon.empty() && r.seatHeardPhon[0] > 20.0 && r.seatFeltDb[0] < r.seatBassDb[0],
               "each head: the bass it hears (phon) and the part under 40 Hz it feels", r.seatHeardPhon.empty() ? 0.0 : r.seatHeardPhon[0]);
    }

    // Where the subwoofers are matters (LFE as loud as the screen, unrelated to it)
    {
        const auto loudApart = scene (0.0, 0.0);
        const auto loudApartT = sim.compute (loudApart);
        auto room = ProRoom::fromSpec (RoomSimulator::defaultRooms()[6]);
        const auto front = RoomSimulator::rate (ProRoomModel::prepare (room, loudApart.grid), loudApart, loudApartT, sim.tuning,
                                                PlaybackSimulator::Level::strong, 10.0);
        room.numSubs = 1;
        room.subs[0].fx = 0.98;
        room.subs[0].fy = 0.02;
        room.subs[0].fz = 0.03;
        const auto corner = RoomSimulator::rate (ProRoomModel::prepare (room, loudApart.grid), loudApart, loudApartT, sim.tuning,
                                                 PlaybackSimulator::Level::strong, 10.0);
        auto spread = [] (const RoomResult& x)
        {
            const auto [lo, hi] = std::minmax_element (x.seatChangeDb.begin(), x.seatChangeDb.end());
            return *hi - *lo;
        };
        auto partSpread = [] (const RoomResult& x, int part)
        {
            const auto& v = x.seatPartDb[(size_t) part];
            const auto [lo, hi] = std::minmax_element (v.begin(), v.end());
            return *hi - *lo;
        };
        std::printf ("  LFE as loud as the screen, unrelated: subwoofers under the screen room %.1f (coverage %.1f, interference %.1f, "
                     "seats spread %.1f dB, LFE alone spread %.1f dB), one in a back corner %.1f (coverage %.1f, interference %.1f, %.1f dB, "
                     "LFE alone %.1f dB)\n",
                     front.roomScore, front.ratingScore[RoomResult::coverageRating], front.ratingScore[RoomResult::mixRating], spread (front),
                     partSpread (front, RoomResult::lfePart), corner.roomScore, corner.ratingScore[RoomResult::coverageRating],
                     corner.ratingScore[RoomResult::mixRating], spread (corner), partSpread (corner, RoomResult::lfePart));
        check (std::abs (front.ratingScore[RoomResult::coverageRating] - corner.ratingScore[RoomResult::coverageRating]) > 3.0
                 && std::abs (partSpread (front, RoomResult::lfePart) - partSpread (corner, RoomResult::lfePart)) > 1.0,
               "moving the subwoofer to a back corner changes the coverage and where the LFE goes", corner.ratingScore[RoomResult::coverageRating]);
        check (spread (corner) < 0.1 && corner.ratingScore[RoomResult::mixRating] > 99.0,
               "an LFE unrelated to L C R: no interference, wherever the subwoofer is", corner.ratingScore[RoomResult::mixRating]);

        double lcrMoved = 0.0, lcrValues = 0.0;

        for (size_t i = 0; i < front.seatPartDb[RoomResult::lcrPart].size(); ++i)
        {
            lcrMoved = std::max (lcrMoved, std::abs (front.seatPartDb[RoomResult::lcrPart][i] - corner.seatPartDb[RoomResult::lcrPart][i]));
            lcrValues += 1.0;
        }

        check (lcrValues > 0.0 && lcrMoved < 1e-6, "moving a subwoofer leaves the L C R part of every seat as it was", lcrMoved);

        double worstGrid = 0.0;

        for (const auto& layer : corner.gridLayerDb)
            for (double v : layer)
                if (! std::isnan (v))
                    worstGrid = std::max (worstGrid, std::abs (v));

        check (worstGrid < 0.1, "an LFE unrelated to L C R: the interference layers stay flat over the whole floor", worstGrid);
    }

    // Each channel is played by its own speakers: bass only in L, an LFE identical to it,
    // the left seats are the ones that change
    {
        const auto n = noise (8.0, 121);
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene left;
        left.l = n;
        left.lfe = scaled (n, std::pow (10.0, -6.0 / 20.0));
        run (e, left);
        const auto onlyLeft = e.result();
        const auto onlyLeftT = sim.compute (onlyLeft);
        auto room = ProRoom::fromSpec (RoomSimulator::defaultRooms()[6]);
        room.numSubs = 1;
        room.subs[0].fy = 0.9;      // the subwoofer on the right, the bass on the left
        const auto rated = RoomSimulator::rate (ProRoomModel::prepare (room, onlyLeft.grid), onlyLeft, onlyLeftT, sim.tuning,
                                                PlaybackSimulator::Level::strong, 10.0);
        const auto seats = room.seatPositions();
        double leftChange = 0.0, rightChange = 0.0;
        int leftCount = 0, rightCount = 0;

        for (size_t i = 0; i < seats.size(); ++i)
            (seats[i][1] < 0.5 * room.width ? leftChange : rightChange) += std::abs (rated.seatChangeDb[i]),
            ++(seats[i][1] < 0.5 * room.width ? leftCount : rightCount);

        leftChange /= std::max (1, leftCount);
        rightChange /= std::max (1, rightCount);
        std::printf ("  bass only in L, LFE the same, subwoofer on the right: mean change %.1f dB on the left seats, %.1f dB on the right\n",
                     leftChange, rightChange);
        check (std::abs (leftChange - rightChange) > 0.3, "bass in L alone: the two sides of the room hear it differently", leftChange - rightChange);
    }

    // Routing in the manner of a panner
    {
        ProRoom r;
        auto plain = r.routing();
        check (plain[0][ProRoom::groupL] == 1.0 && plain[1][ProRoom::groupC] == 1.0 && plain[1][ProRoom::groupL] == 0.0 && plain[3][ProRoom::groupSubs] == 1.0,
               "routing at 100: each channel on its own speakers", plain[1][ProRoom::groupC]);
        r.centrePercent = 0.0;
        auto phantom = r.routing();
        check (std::abs (phantom[1][ProRoom::groupL] - std::sqrt (0.5)) < 1e-12 && phantom[1][ProRoom::groupC] == 0.0,
               "centre 0 %: C as a phantom centre, -3 dB in L and in R", phantom[1][ProRoom::groupL]);
        r.centrePercent = 100.0;
        r.frontDivergence = 0.0;
        auto spread = r.routing();
        check (std::abs (spread[0][ProRoom::groupR] - std::sqrt (1.0 / 3.0)) < 1e-12, "front divergence 0: each screen channel on L, C and R alike",
               spread[0][ProRoom::groupR]);
        r.frontDivergence = 100.0;
        r.fillsOn = true;
        r.numFills = 2;
        r.fills[0].fy = 0.1;
        r.fills[1].fy = 0.9;
        auto fills = r.routing();
        check (same (fills[3][ProRoom::groupFillsLfe], 1.0) && same (fills[0][ProRoom::groupFillsLfe], 0.0)
                 && same (fills[1][ProRoom::groupFillsLfe], 0.0) && same (fills[2][ProRoom::groupFillsLfe], 0.0),
               "fills play the LFE only, never L, C or R", fills[0][ProRoom::groupFillsLfe]);
    }

    // People absorb: the same room is drier with an audience
    {
        auto r = ProRoom::venues()[0].room;
        r.people = ProRoom::noPeople;
        const double empty = r.eyringRt60 (100.0);
        r.people = ProRoom::seatedPeople;
        const double full = r.eyringRt60 (100.0);
        std::printf ("  small gallery: RT60 at 100 Hz %.2f s empty, %.2f s with a seated audience\n", empty, full);
        check (full < empty, "an audience shortens the reverberation", full);

        bool sensible = true;

        for (const auto& m : ProRoom::materialList())
            if (std::string (m.key) == "open")
                sensible = sensible && m.surfaces == ProMaterial::wall;

        check (sensible && ProRoom::materialIndex ("seated") < 0 && ProRoom::materialIndex ("standing") < 0,
               "no open side on a floor or a ceiling, no audience among the materials", 0.0);
    }

    // Numerical floor modes: an oval with equal sides is a circle, whose modes are known
    {
        ProRoom oval;
        oval.shape = ProRoom::oval;
        oval.length = oval.width = 10.0;
        oval.height = 4.0;
        const auto started = std::chrono::steady_clock::now();
        const auto floor = ProRoomModel::floorModes (oval, 2.0 * 3.14159265358979 * 160.0 / 343.0);
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();
        double first = 0.0;

        for (const auto& [k, mode] : floor)
            if (k > 0.05) { first = k * 343.0 / (2.0 * 3.14159265358979); break; }

        std::printf ("  oval floor 10 x 10 m (a circle), numerical: %zu floor modes in %.2f s, first at %.2f Hz (exact 20.10)\n",
                     floor.size(), seconds, first);
        check (! floor.empty() && floor.front().first < 0.05 && std::abs (first - 20.10) / 20.10 < 0.015,
               "numerical floor modes: a circle's first mode within 1.5 %", first);

        for (int shape : { ProRoom::oval, ProRoom::roundedCorners, ProRoom::lShape })
        {
            auto r = ProRoom::fromSpec (RoomSimulator::defaultRooms()[6]);
            r.shape = shape;
            const auto t0 = std::chrono::steady_clock::now();
            const auto modes = ProRoomModel::modesOf (r);
            const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            double lowest = 1e9;

            for (const auto& m : modes)
                if (m.hz > 1.0)
                    lowest = std::min (lowest, m.hz);

            const char* names[] = { "", "", "oval", "rounded corners", "L shape" };
            std::printf ("  20 x 13 x 7 m, %-15s: %zu modes in %.2f s, first %.1f Hz\n", names[shape], modes.size(), s, lowest);
            check (modes.size() > 50 && lowest > 5.0 && lowest < 15.0, "a numerical floor gives modes in the expected range", lowest);
        }
    }

    // Extra points around the riskiest seats, the 10th percentile and the worst point found
    {
        const auto loudApart = scene (0.0, 0.0);
        const auto loudApartT = sim.compute (loudApart);
        auto room = ProRoom::fromSpec (RoomSimulator::defaultRooms()[6]);
        room.numSubs = 1;
        room.subs[0].fx = 0.98;
        room.subs[0].fy = 0.02;
        const auto paths = ProRoomModel::prepare (room, loudApart.grid);
        const auto rated = RoomSimulator::rate (paths, loudApart, loudApartT, sim.tuning, PlaybackSimulator::Level::strong, 10.0);
        double worstSeat = 100.0;

        for (double p : rated.seatPenalty)
            worstSeat = std::min (worstSeat, 100.0 * (1.0 - p));

        std::printf ("  %d seats + %d extra points: room %.1f, P10 %.1f, worst seat %.1f, worst point found %.1f%s\n", paths.seats, paths.probes,
                     rated.roomScore, rated.p10Score, worstSeat, rated.worstPointScore, rated.worstProbe >= 0 ? " (between the seats)" : "");
        check (paths.probes > 0 && (int) rated.probePenalty.size() == paths.probes && rated.worstPointScore <= worstSeat + 1e-9
                 && rated.p10Score >= worstSeat - 1e-9,
               "extra points are rated; the worst point found is at most the worst seat; P10 between", rated.worstPointScore);
    }

    // Numerical floors miss no mode: their count follows Weyl's law (Neumann:
    // N(k) = A k^2 / 4 pi + P k / 4 pi) for every shape and three proportions
    {
        double worst = 0.0;

        for (int shape : { ProRoom::oval, ProRoom::roundedCorners, ProRoom::lShape })
            for (double aspect : { 1.0, 1.6, 3.0 })
            {
                ProRoom r;
                r.shape = shape;
                r.width = 12.0;
                r.length = 12.0 * aspect;
                r.height = 5.0;
                const double kmax = 2.0 * 3.14159265358979 * 120.0 / 343.0;
                const auto modes = ProRoomModel::floorModes (r, kmax);
                const double area = r.floorArea(), perimeter = r.wallArea() / r.height;
                const double weyl = area * kmax * kmax / (4.0 * 3.14159265358979) + perimeter * kmax / (4.0 * 3.14159265358979);
                int found = 0;

                for (const auto& m : modes)
                    found += m.first <= kmax ? 1 : 0;

                worst = std::max (worst, std::abs (found - weyl) / weyl);
            }

        std::printf ("  numerical floors (oval, rounded corners, L; proportions 1, 1.6, 3): mode count within %.1f %% of Weyl's law\n", 100.0 * worst);
        check (worst < 0.05, "numerical floors: no mode missing (Weyl's law)", worst);
    }

    // Coverage: LFE fills half way down a catwalk room bring the bass to the back rows
    {
        const auto mix = scene (0.3, -10.0);
        const auto mixT = sim.compute (mix);
        auto room = ProRoom::venues()[7].room;      // showroom catwalk, 20 x 10 x 4 m
        const auto rows = [&] (const ProRoom& rm, const RoomResult& res)
        {
            // Mean bass level against the stage, front half and back half of the audience
            const auto seats = rm.seatPositions();
            double front = 0.0, back = 0.0;
            int nf = 0, nb = 0;

            for (size_t i = 0; i < seats.size() && i < res.seatLevelDb.size(); ++i)
                (seats[i][0] < 0.5 * rm.length ? (++nf, front) : (++nb, back)) += res.seatLevelDb[i];

            return std::pair<double, double> { front / std::max (1, nf), back / std::max (1, nb) };
        };

        const auto without = RoomSimulator::rate (ProRoomModel::prepare (room, mix.grid), mix, mixT, sim.tuning, PlaybackSimulator::Level::medium, 10.0);
        room.fillsOn = true;
        room.numFills = 2;
        room.fills[0].fx = 0.55;
        room.fills[0].fy = 0.03;
        room.fills[0].delayMs = 1000.0 * room.fills[0].fx * room.length / RoomSimulator::speedOfSound;
        room.applyLinks();
        const auto with = RoomSimulator::rate (ProRoomModel::prepare (room, mix.grid), mix, mixT, sim.tuning, PlaybackSimulator::Level::medium, 10.0);
        const auto a = rows (ProRoom::venues()[7].room, without), b = rows (room, with);
        std::printf ("  catwalk, bass level against the stage: front %+.1f / back %+.1f dB; with LFE fills half way %+.1f / %+.1f dB; room %.1f -> %.1f\n",
                     a.first, a.second, b.first, b.second, without.roomScore, with.roomScore);
        check (! with.seatLevelDb.empty() && std::abs (b.first - a.first) + std::abs (b.second - a.second) > 0.3,
               "coverage: the bass level at the seats follows the fills, distance and phase included", b.second - a.second);
    }

    // Room score: the worst seats weigh more
    {
        std::vector<double> penalties (20, 0.0);
        penalties[3] = penalties[9] = penalties[14] = 0.7;
        const double score = RoomSimulator::roomScoreOf (penalties, sim.tuning);
        std::printf ("  17 good seats and 3 bad ones (30 %%): room score %.1f (plain mean 89.5)\n", score);
        check (std::abs (score - 74.6) < 0.2, "three bad seats out of twenty pull the room down to about 75", score);
    }

    // Round room: Bessel modes
    {
        check (std::abs (ProRoomModel::besselJ (0, 2.404825557695773)) < 1e-9, "J0 is zero at 2.4048", ProRoomModel::besselJ (0, 2.404825557695773));
        check (std::abs (ProRoomModel::besselDerivativeZero (1, 1) - 1.841183781) < 1e-6
                 && std::abs (ProRoomModel::besselDerivativeZero (2, 1) - 3.054236928) < 1e-6
                 && std::abs (ProRoomModel::besselDerivativeZero (0, 1) - 3.831705970) < 1e-6,
               "zeros of J'm: 1.8412, 3.0542, 3.8317", ProRoomModel::besselDerivativeZero (1, 1));

        ProRoom round = ProRoom::fromSpec (RoomSimulator::defaultRooms()[6]);
        round.shape = ProRoom::round;
        round.width = 10.0;
        round.applyLinks();
        double lowest = 1e9;

        for (const auto& m : ProRoomModel::modesOf (round, 160.0))
            if (m.hz > 0.0)
                lowest = std::min (lowest, m.hz);

        std::printf ("  round room 10 m across: first mode %.2f Hz (c x 1.8412 / (2 pi x 5) = 20.10)\n", lowest);
        check (std::abs (lowest - 343.0 * 1.841183781 / (2.0 * 3.14159265358979 * 5.0)) < 0.01, "first mode of a round room", lowest);

        double outside = 0.0;

        for (const auto& seat : round.seatPositions())
            outside = std::max (outside, std::hypot (seat[0] - 5.0, seat[1] - 5.0) - 5.0);

        check (outside <= 0.0, "every seat of a round room is inside it", outside);
    }

    // Audience layouts
    {
        auto r = ProRoom::fromSpec (RoomSimulator::defaultRooms()[6]);
        r.audienceLayout = ProRoom::sidesLayout;
        r.aisle = 0.3;
        r.perRow = 8;
        double nearest = 1e9;

        for (const auto& seat : r.seatPositions())
            nearest = std::min (nearest, std::abs (seat[1] - 0.5 * r.width));

        check (nearest >= 0.15 * r.width - 1e-9, "two blocks: no seat in the aisle", nearest);

        r.audienceLayout = ProRoom::ringLayout;
        r.ringInner = 0.3;
        r.ringOuter = 0.8;
        double lo = 1e9, hi = 0.0;

        for (const auto& seat : r.seatPositions())
        {
            const double d = std::hypot (seat[0] - 0.5 * r.length, seat[1] - 0.5 * r.width);
            lo = std::min (lo, d);
            hi = std::max (hi, d);
        }

        const double half = 0.5 * std::min (r.length, r.width);
        check (lo >= 0.3 * half - 1e-6 && hi <= 0.8 * half + 1e-6, "rings: every seat between the inner and outer radius", hi);
    }

    // A subwoofer set 6 dB too loud is not calibrated away
    {
        auto room = ProRoom::fromSpec (RoomSimulator::defaultRooms()[6]);
        const auto even = RoomSimulator::rate (ProRoomModel::prepare (room, sharedAnalysis.grid), sharedAnalysis, sharedT, sim.tuning,
                                               PlaybackSimulator::Level::strong, 10.0);
        room.subs[0].gainDb = room.subs[1].gainDb = 6.0;
        const auto loud = RoomSimulator::rate (ProRoomModel::prepare (room, sharedAnalysis.grid), sharedAnalysis, sharedT, sim.tuning,
                                               PlaybackSimulator::Level::strong, 10.0);
        double lfeRise = 0.0, lcrRise = 0.0;
        const size_t n = even.seatPartDb[RoomResult::lfePart].size();

        for (size_t i = 0; i < n; ++i)
        {
            lfeRise += (loud.seatPartDb[RoomResult::lfePart][i] - even.seatPartDb[RoomResult::lfePart][i]) / (double) n;
            lcrRise = std::max (lcrRise, std::abs (loud.seatPartDb[RoomResult::lcrPart][i] - even.seatPartDb[RoomResult::lcrPart][i]));
        }

        std::printf ("  subwoofers 6 dB too loud, LFE identical to the screen: room %.1f -> %.1f, coverage %.1f -> %.1f, "
                     "LFE alone %+.2f dB at every seat on average, L C R alone %.2f dB at most\n",
                     even.roomScore, loud.roomScore, even.ratingScore[RoomResult::coverageRating], loud.ratingScore[RoomResult::coverageRating], lfeRise, lcrRise);
        check (n > 0 && std::abs (lfeRise - 6.0) < 0.3 && lcrRise < 1e-6, "a subwoofer level offset stays after calibration, in the LFE part only", lfeRise);
        check (std::abs (loud.ratingScore[RoomResult::coverageRating] - even.ratingScore[RoomResult::coverageRating]) > 2.0,
               "subwoofers set 6 dB off their level change the coverage", loud.ratingScore[RoomResult::coverageRating]);
    }

    // An LFE fill moved: the LFE part changes, the L C R part does not
    {
        auto room = ProRoom::fromSpec (RoomSimulator::defaultRooms()[6]);
        room.fillsOn = true;
        room.numFills = 2;
        room.linkFills = true;

        for (auto& f : room.fills)
        {
            f.fx = 0.5;
            f.fy = 0.03;
            f.fz = 0.5;
        }

        room.applyLinks();
        const auto before = RoomSimulator::rate (ProRoomModel::prepare (room, sharedAnalysis.grid), sharedAnalysis, sharedT, sim.tuning,
                                                 PlaybackSimulator::Level::strong, 10.0);
        room.fills[0].fx = 0.8;
        room.applyLinks();
        const auto after = RoomSimulator::rate (ProRoomModel::prepare (room, sharedAnalysis.grid), sharedAnalysis, sharedT, sim.tuning,
                                                PlaybackSimulator::Level::strong, 10.0);
        double lcrMoved = 0.0, lfeMoved = 0.0;

        for (size_t i = 0; i < before.seatPartDb[RoomResult::lcrPart].size(); ++i)
        {
            lcrMoved = std::max (lcrMoved, std::abs (after.seatPartDb[RoomResult::lcrPart][i] - before.seatPartDb[RoomResult::lcrPart][i]));
            lfeMoved = std::max (lfeMoved, std::abs (after.seatPartDb[RoomResult::lfePart][i] - before.seatPartDb[RoomResult::lfePart][i]));
        }

        std::printf ("  LFE fills moved from half way to 80 %% of the length: the LFE alone changes by up to %.1f dB at a seat, "
                     "L C R alone by %.2g dB\n", lfeMoved, lcrMoved);
        check (lfeMoved > 0.5 && lcrMoved < 1e-6, "moving an LFE fill changes the LFE part, never the L C R part", lcrMoved);
    }

    // Directivity: a cardioid C in the middle of a large, dry room, aimed at the back wall. A point
    // 4 m in front of it against one 4 m behind, against the same with an omnidirectional C.
    {
        ProRoom room = ProRoom::fromSpec ({ "dry hall", "dry hall", 40.0, 30.0, 12.0, 0.4 });
        room.numSubs = 1;
        room.subs[0].enabled = true;
        room.lcr[0].enabled = room.lcr[2].enabled = false;
        room.linkLR = false;
        room.lcr[1].fx = 0.5; room.lcr[1].fy = 0.5; room.lcr[1].fz = 0.25;
        room.lcr[1].yaw = 0.0; room.lcr[1].pitch = 0.0;
        room.audienceLayout = ProRoom::rowsLayout;
        room.firstRow = 0.4; room.lastRow = 0.6; room.audienceWidth = 0.02; room.rows = 2; room.perRow = 1;
        room.earHeight = 3.0;
        const auto seats = room.seatPositions();

        const auto frontToBack = [&] (int model)
        {
            room.lcr[1].model = model;
            const auto paths = ProRoomModel::prepare (room, sharedAnalysis.grid);
            double front = 0.0, back = 0.0;

            for (size_t i = 0; i < paths.points.size(); ++i)
            {
                const double hz = sharedAnalysis.grid[(size_t) paths.points[i]];

                if (hz < 60.0 || hz > 120.0)
                    continue;

                for (int seat = 0; seat < paths.seats; ++seat)
                {
                    const double c = paths.p[(size_t) seat * paths.points.size() + i][5].real();     // C through its speaker
                    (seats[(size_t) seat][0] > 0.5 * room.length ? front : back) += hz * c;
                }
            }

            return 10.0 * std::log10 (front / back);
        };

        const double omni = frontToBack (ProSpeakerModel::indexOf ("omni"));
        const double cardioid = frontToBack (ProSpeakerModel::indexOf ("cardioid"));
        std::printf ("  C in the middle of a dry 40 x 30 x 12 m hall, 60-120 Hz, 4 m in front against 4 m behind: omnidirectional %+.1f dB, "
                     "cardioid %+.1f dB\n", omni, cardioid);
        check (seats.size() == 2 && std::abs (omni) < 3.0 && cardioid > omni + 4.0,
               "a cardioid speaker sends more bass ahead than behind, an omnidirectional one does not", cardioid - omni);

        // The models: omnidirectional below ka = 1, then less so; the JBL 3730 is a cardioid at 160 Hz
        const auto& models = ProSpeakerModel::list();
        const auto& jbl = models[(size_t) ProSpeakerModel::indexOf ("jbl3730")];
        const auto& genelec = models[(size_t) ProSpeakerModel::indexOf ("genelec8361a")];
        check (jbl.omniShare (40.0) > 0.9 && std::abs (jbl.omniShare (160.0) - 0.5) < 1e-9 && genelec.omniShare (60.0) > 0.9
                 && std::abs (genelec.omniShare (343.0 / (3.14159265358979 * 0.357)) - 0.7) < 1e-6,
               "speaker models: omnidirectional in the deep bass, -3 dB at 90 degrees at ka = 1 (width model)", jbl.omniShare (160.0));
    }

    // Why a seat gets what it gets: room, speakers and mix add up to the whole change
    {
        auto room = ProRoom::fromSpec (RoomSimulator::defaultRooms()[6]);
        room.numSubs = 2;
        room.subs[0].fx = 0.05; room.subs[0].fy = 0.3; room.subs[0].fz = 0.05;
        room.subs[1].fx = 0.05; room.subs[1].fy = 0.7; room.subs[1].fz = 0.05;
        const auto rated = RoomSimulator::rate (ProRoomModel::prepare (room, sharedAnalysis.grid), sharedAnalysis, sharedT, sim.tuning,
                                                PlaybackSimulator::Level::strong, 10.0);
        double worstSum = 0.0;
        int agree = 0, compared = 0;

        for (size_t i = 0; i < rated.seatPartDb[RoomResult::allParts].size(); ++i)
        {
            const double sum = rated.seatCauseDb[RoomResult::roomCause][i] + rated.seatCauseDb[RoomResult::speakersCause][i]
                             + rated.seatCauseDb[RoomResult::mixCause][i];
            worstSum = std::max (worstSum, std::abs (sum - rated.seatPartDb[RoomResult::allParts][i]));
            const double db = rated.seatPartDb[RoomResult::allParts][i];
            const double phon = rated.seatPhon[RoomResult::allParts][i] - rated.approvedPhon[RoomResult::allParts];

            if (std::abs (db) > 2.0)
            {
                ++compared;
                agree += (db > 0.0) == (phon > 0.0) ? 1 : 0;
            }
        }

        std::printf ("  room + speakers + mix against the whole change: %.2g dB at most; mix %.0f phon; heads 2 dB or more off: "
                     "%d of %d louder or quieter in phon too\n", worstSum, rated.approvedPhon[RoomResult::allParts], agree, compared);
        check (worstSum < 1e-6, "room, speakers and mix add up exactly to the change of every seat", worstSum);
        check (rated.approvedPhon[RoomResult::allParts] > 20.0 && compared > 0 && agree >= 0.8 * compared,
               "each head's loudness is on the mix's scale: a head that loses bass is quieter than the mix", (double) agree);

        // The second subwoofer inverted: the speakers cause changes, the room cause does not
        room.subs[1].inverted = true;
        const auto inverted = RoomSimulator::rate (ProRoomModel::prepare (room, apartAnalysis.grid), apartAnalysis, apartT, sim.tuning,
                                                   PlaybackSimulator::Level::strong, 10.0);
        room.subs[1].inverted = false;
        const auto upright = RoomSimulator::rate (ProRoomModel::prepare (room, apartAnalysis.grid), apartAnalysis, apartT, sim.tuning,
                                                  PlaybackSimulator::Level::strong, 10.0);
        // The room cause may only move one way everywhere: the subwoofers are calibrated again, as installed,
        // which scales the LFE alone (each subwoofer's own response does not depend on its polarity)
        double roomLow = 1e9, roomHigh = -1e9, speakersMoved = 0.0, mixLargest = 0.0;
        const size_t n = upright.seatCauseDb[RoomResult::roomCause].size();

        for (size_t i = 0; i < n; ++i)
        {
            const double d = inverted.seatCauseDb[RoomResult::roomCause][i] - upright.seatCauseDb[RoomResult::roomCause][i];
            roomLow = std::min (roomLow, d);
            roomHigh = std::max (roomHigh, d);
            speakersMoved += std::abs (inverted.seatCauseDb[RoomResult::speakersCause][i] - upright.seatCauseDb[RoomResult::speakersCause][i]) / (double) n;
            mixLargest = std::max ({ mixLargest, std::abs (inverted.seatCauseDb[RoomResult::mixCause][i]), std::abs (upright.seatCauseDb[RoomResult::mixCause][i]) });
        }

        const double roomMoved = roomHigh - roomLow;
        std::printf ("  second subwoofer inverted, LFE unrelated to L C R: speakers cause moves %.1f dB per seat on average, room cause "
                     "%+.2f to %+.2f dB (calibrated again), mix cause at most %.2g dB; speakers score %.1f -> %.1f\n",
                     speakersMoved, roomLow, roomHigh, mixLargest,
                     upright.ratingScore[RoomResult::speakersRating], inverted.ratingScore[RoomResult::speakersRating]);
        check (n > 0 && speakersMoved > 1.0 && roomLow * roomHigh >= 0.0 && roomMoved < speakersMoved * 4.0 && mixLargest < 0.1,
               "a subwoofer inverted shows in the speakers cause only (LFE unrelated: no mix cause)", speakersMoved);
    }

    // A large configured room: prepared in reasonable time
    {
        auto room = ProRoom::fromSpec (RoomSimulator::defaultRooms()[14]);
        room.rows = 8;
        room.perRow = 12;
        room.fillsOn = true;
        room.numFills = 4;
        room.numSubs = 4;
        const auto started = std::chrono::steady_clock::now();
        const auto paths = ProRoomModel::prepare (room, sharedAnalysis.grid);
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();
        std::printf ("  IMAX, 96 seats, 11 speakers: prepared in %.2f s\n", seconds);
        check (paths.seats == 96 && seconds < 20.0, "the largest Pro room is computed", seconds);
    }
}

static void testSignals()
{
    std::printf ("built-in test signals\n");
    const auto& scenes = TestSignals::all();
    std::set<std::string> names;
    int translated = 0, pro = 0;

    for (const auto& scene : scenes)
    {
        names.insert (scene.file);
        translated += ! scene.titleFr.empty() && ! scene.lookForFr.empty() ? 1 : 0;
        pro += scene.roomSimPro ? 1 : 0;
    }

    check (scenes.size() == 36 && names.size() == scenes.size() && pro == 14 && translated == 36,
           "36 test signals (14 for Room Sim Pro), each named once, each with its French text", (double) translated);

    // The same signal twice: the same samples (a file and the app's built-in signal are the same sound)
    double worst = 0.0, energy = 0.0;

    for (const auto& scene : scenes)
    {
        auto a = scene.make(), b = scene.make();

        for (int i = 0; i < 4800; ++i)
        {
            const double t = 0.5 + i / (double) TestSignals::sampleRate;
            const auto x = a (t), y = b (t);
            worst = std::max ({ worst, std::abs (x.l - y.l), std::abs (x.c - y.c), std::abs (x.r - y.r), std::abs (x.lfe - y.lfe) });
            energy += x.l * x.l + x.c * x.c + x.r * x.r + x.lfe * x.lfe;
        }
    }

    check (worst < 1e-12 && energy > 0.0, "each test signal is the same every time it starts", worst);
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
    testLowLevelAndLowFrequency();
    testTransients();
    testRunner();
    testRooms();
    testRoomPro();
    testSignals();
    testPlaybackChains();
    testInertiaAndAutoReset();

    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
