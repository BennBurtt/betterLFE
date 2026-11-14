// Accuracy checks of the analysis engine on synthetic signals.
// Plain executable: exits with 0 when every check passes.

#include "../core/AlertTracker.h"
#include "../core/LfeEngine.h"
#include "../core/PlaybackSim.h"

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
                if (ev.match > 0.5)
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
            check (ev.channel == chC, std::string (c.name) + ": reported on the right channel", ev.channel);
        }

        check (worstDelay < 0.5, std::string (c.name) + ": delay within 0.5 ms", worstDelay);
        check (events.empty() || events[0].delayUncertaintyMs < 1.0, std::string (c.name) + ": stated precision under 1 ms",
               events.empty() ? 0.0 : events[0].delayUncertaintyMs);
        check (worstMatch > 0.95, std::string (c.name) + ": transients recognised as the same", worstMatch);
        check (worstPhase < 0.5, std::string (c.name) + ": polarity", worstPhase);
    }

    {
        // 10 ms late: the pair would interact fully if aligned, and mostly cancels as it is
        Scene s;
        s.c = kicks (6.0, 0.0, 1.0);
        s.lfe = kicks (6.0, 0.010, 1.0);
        const auto events = collect (s);
        check (! events.empty() && events[0].interactionBest > 0.95, "late kick: full interaction once aligned",
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

        // The on-time weight is twice the late one and four times the early one
        check (std::abs (cinema (t, 0.0, false).weight / cinema (t, 2.5, false).weight - 2.0) < 1e-9, "0 ms counts double", 0.0);
        check (std::abs (cinema (t, 2.5, false).weight / cinema (t, -2.5, false).weight - 2.0) < 1e-9, "early counts half", 0.0);
    }
    {
        // LFE alone: the cinema systems all agree, a stereo downmix has nothing left
        LfeEngine e (sampleRate, 1.0, 0.0);
        Scene s;
        s.lfe = n;
        run (e, s);
        const auto t = sim.compute (e.result());
        check (std::abs (cinema (t, 5.0, true).changeDb) < 0.05, "LFE alone: delay and polarity change nothing", cinema (t, 5.0, true).changeDb);
        check (stereo (t).penalty > 0.999 && stereo (t).changeDb < -20.0, "LFE alone: nothing left in stereo", stereo (t).changeDb);
        check (t.lfeShare > 0.99, "LFE alone: it carries all the bass", t.lfeShare);
        check (std::abs (t.dependence - PlaybackSimulator::dependenceCeiling) < 1e-9, "LFE alone: full dependence", t.dependence);
        check (t.robustness > 18.0 && t.robustness < 26.0, "LFE alone: about 20-25, the room alone decides the bass", t.robustness);

        double worst = 0.0;

        for (const auto& c : t.chains)
            if (c.kind != ChainResult::Kind::stereo)
                worst = std::max (worst, c.weight * c.penalty);

        check (std::abs (t.interaction - worst) < 1e-12, "interaction = worst relevance x penalty", t.interaction);
        check (std::abs (t.robustness - 100.0 * (1.0 - t.interaction) * (1.0 - t.dependence)) < 1e-9,
               "robustness = 100 (1 - interaction) (1 - dependence)", t.robustness);
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
        std::printf ("  unrelated, equal levels: robustness %.0f\n", t.robustness);
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
        std::printf ("  identical signals: robustness %.0f\n", t.robustness);
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
            std::printf ("  LFE 10 dB below the screen, %.0f %% shared: robustness %.0f\n", 100.0 * shared, t.robustness);
        }
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
            std::printf ("  LFE as loud as the screen, %.0f %% shared: robustness %.0f\n", 100.0 * shared, sim.compute (e.result()).robustness);
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
    testPlaybackChains();

    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
