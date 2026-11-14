#include "PlaybackSim.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>

namespace subguard
{

namespace
{
    using cd = std::complex<double>;
    constexpr double pi = 3.14159265358979323846;
    constexpr double sumLoHz = 20.0, sumHiHz = 120.0;
    constexpr double lfeLowPassHz = 120.0;
    constexpr double silencePower = 1e-19;      // mean per-bin power under which there is no bass at all

    // Fourth-order Linkwitz-Riley low-pass (two cascaded Butterworth sections)
    cd lr4LowPass (double hz, double corner)
    {
        const double x = hz / corner;
        const cd section = 1.0 / cd (1.0 - x * x, std::sqrt (2.0) * x);
        return section * section;
    }

    // Low-pass plus high-pass of a Linkwitz-Riley crossover: flat magnitude,
    // phase turning by a full cycle through the crossover. This is what a bass
    // managed screen channel has become once subwoofer and satellite recombine.
    cd lr4AllPass (double hz, double corner)
    {
        const double x = hz / corner;
        return cd (1.0 - x * x, -std::sqrt (2.0) * x) / cd (1.0 - x * x, std::sqrt (2.0) * x);
    }

    // Eighth-order Butterworth low-pass: stand-in for an encoder's LFE filter
    cd encoderLowPass (double hz)
    {
        const cd s (0.0, hz / lfeLowPassHz);
        cd h (1.0, 0.0);

        for (int k = 1; k <= 8; ++k)
            h /= s - std::polar (1.0, pi * (2.0 * k + 7.0) / 16.0);

        return h;
    }

    struct Gains { cd screen, lfe, heights; };

    Gains gainsFor (const ChainResult& chain, double hz)
    {
        const double lfeGain = std::pow (10.0, PlaybackSimulator::lfeInBandGainDb / 20.0);

        if (chain.kind == ChainResult::Kind::cinema)
            return { 1.0, lfeGain * (chain.inverted ? -1.0 : 1.0) * std::polar (1.0, -2.0 * pi * hz * chain.delayMs * 0.001), 0.0 };

        const cd redirected = lr4AllPass (hz, chain.crossoverHz);
        cd lfe = lfeGain * lr4LowPass (hz, lfeLowPassHz);

        if (chain.encoderFilter)
            lfe *= encoderLowPass (hz);

        return { redirected, lfe, chain.kind == ChainResult::Kind::atmosHome ? redirected : cd() };
    }

    double toDb (double ratio)
    {
        return std::max (PlaybackSimulator::lossFloorDb, 10.0 * std::log10 (std::max (ratio, 1e-12)));
    }
}

Translation PlaybackSimulator::compute (const Analysis& a, bool formatHasHeights) const
{
    Translation t;
    t.hasHeights = formatHasHeights;

    const auto& m = a.matrix;
    const auto n = a.grid.size();

    if (n == 0 || m.pF.size() != n)
        return t;

    const double lfeGain2 = std::pow (10.0, lfeInBandGainDb / 10.0);

    // Cross terms with what chance could produce taken out
    std::vector<cd> cFL (n), cFH (n), cHL (n);

    for (size_t g = 0; g < n; ++g)
    {
        // Nothing below 2 sigma (chance level), everything above 4 sigma
        const auto keep = [] (std::complex<double> c, double pa, double pb, double sigma)
        {
            const double coherence = std::abs (c) / (std::sqrt (pa * pb) + 1e-60);
            return c * std::clamp ((coherence - 2.0 * sigma) / (2.0 * sigma + 1e-12), 0.0, 1.0);
        };

        cFL[g] = keep (m.cFLfe[g], m.pF[g], m.pLfe[g], a.sigma[pairFacade][g]);
        cFH[g] = keep (m.cFH[g], m.pF[g], m.pH[g], a.sigmaLfe[g]);
        cHL[g] = keep (m.cHLfe[g], m.pH[g], m.pLfe[g], a.sigmaLfe[g]);
    }

    // The chains
    for (bool inverted : { false, true })
        for (double delay : { -5.0, -2.5, 0.0, 2.5, 5.0 })
        {
            ChainResult c;
            c.kind = ChainResult::Kind::cinema;
            c.delayMs = delay;
            c.inverted = inverted;
            char text[64];
            std::snprintf (text, sizeof (text), "Cinema, sub %+.1f ms%s", delay, inverted ? ", inverted" : "");
            c.name = text;
            t.chains.push_back (c);
        }

    for (bool encoder : { false, true })
        for (double corner : { 60.0, 80.0, 100.0 })
        {
            ChainResult c;
            c.kind = ChainResult::Kind::bassManaged;
            c.crossoverHz = corner;
            c.encoderFilter = encoder;
            c.name = "Bass managed " + std::to_string ((int) corner) + " Hz" + (encoder ? ", encoder filter" : "");
            t.chains.push_back (c);
        }

    if (formatHasHeights)
        for (bool encoder : { false, true })
        {
            ChainResult c;
            c.kind = ChainResult::Kind::atmosHome;
            c.crossoverHz = 80.0;
            c.encoderFilter = encoder;
            c.name = std::string ("Atmos home 80 Hz") + (encoder ? ", encoder filter" : "");
            t.chains.push_back (c);
        }

    // Integrate each chain over frequency. The grid is logarithmic, so each
    // point stands for a width proportional to its frequency.
    double referenceTotal = 0.0, weightTotal = 0.0;

    for (auto& chain : t.chains)
    {
        double sum = 0.0, reference = 0.0, bandSum[3] = {}, bandReference[3] = {};

        for (size_t g = 0; g < n; ++g)
        {
            const double hz = a.grid[g];

            if (hz < sumLoHz || hz > sumHiHz)
                continue;

            const auto gain = gainsFor (chain, hz);
            const double incoherent = std::norm (gain.screen) * m.pF[g] + std::norm (gain.lfe) * m.pLfe[g]
                                        + std::norm (gain.heights) * m.pH[g];
            const double coherent = incoherent
                                      + 2.0 * (gain.screen * std::conj (gain.lfe) * cFL[g]).real()
                                      + 2.0 * (gain.screen * std::conj (gain.heights) * cFH[g]).real()
                                      + 2.0 * (gain.heights * std::conj (gain.lfe) * cHL[g]).real();

            sum += hz * std::max (coherent, 0.0);
            reference += hz * incoherent;

            for (int b = 0; b < 3; ++b)
                if (hz >= Translation::bandEdges[b] && hz < Translation::bandEdges[b + 1])
                {
                    bandSum[b] += hz * std::max (coherent, 0.0);
                    bandReference[b] += hz * incoherent;
                }
        }

        chain.changeDb = reference > 0.0 ? toDb (sum / reference) : 0.0;

        for (size_t b = 0; b < 3; ++b)
            chain.bandChangeDb[b] = bandReference[b] > 0.0 ? toDb (bandSum[b] / bandReference[b]) : 0.0;

        if (&chain == &t.chains.front())
            referenceTotal = reference;
    }

    for (size_t g = 0; g < n; ++g)
        if (a.grid[g] >= sumLoHz && a.grid[g] <= sumHiHz)
            weightTotal += a.grid[g];

    t.hasBass = weightTotal > 0.0 && referenceTotal / weightTotal > silencePower;

    if (! t.hasBass)
        return t;

    // Robustness: how far apart the chains land
    double lowest = 1e9, highest = -1e9;

    for (const auto& chain : t.chains)
    {
        lowest = std::min (lowest, chain.changeDb);
        highest = std::max (highest, chain.changeDb);
    }

    t.worstChangeDb = lowest;
    t.spreadDb = highest - lowest;
    t.robustness = 100.0 * std::clamp (1.0 - t.spreadDb / spreadForZeroDb, 0.0, 1.0);

    // Stereo downmix without the LFE: Lo = L + 0.707 C, Ro = R + 0.707 C
    {
        double downmix = 0.0, lfe = 0.0;

        for (size_t g = 0; g < n; ++g)
        {
            const double hz = a.grid[g];

            if (hz < sumLoHz || hz > sumHiHz)
                continue;

            downmix += hz * std::max (0.0, m.pL[g] + m.pC[g] + m.pR[g] + std::sqrt (2.0) * (m.reLC[g] + m.reRC[g]));
            lfe += hz * lfeGain2 * m.pLfe[g];
        }

        t.lfeShare = lfe / (lfe + downmix + 1e-60);
        t.downmixChangeDb = std::max (-60.0, 10.0 * std::log10 (std::max (1.0 - t.lfeShare, 1e-6)));
    }

    // Overlap: RMS level of each group per band. A mean per-bin power over a
    // band of width W becomes an RMS power of mean . W . window / 3 (Hann window).
    for (size_t b = 0; b < 3; ++b)
    {
        const double lo = Translation::bandEdges[b], hi = Translation::bandEdges[b + 1];
        double screen = 0.0, lfe = 0.0, weight = 0.0;

        for (size_t g = 0; g < n; ++g)
            if (a.grid[g] >= lo && a.grid[g] < hi)
            {
                screen += a.grid[g] * m.pF[g];
                lfe += a.grid[g] * lfeGain2 * m.pLfe[g];
                weight += a.grid[g];
            }

        if (weight <= 0.0)
            continue;

        const double scale = (hi - lo) * a.windowSeconds / 3.0 / weight;
        auto& o = t.overlap[b];
        o.screenDb = 10.0 * std::log10 (screen * scale + 1e-30);
        o.lfeDb = 10.0 * std::log10 (lfe * scale + 1e-30);

        if (screen * scale > 1e-18 && lfe * scale > 1e-18)
            o.amount = std::clamp (1.0 - std::abs (o.screenDb - o.lfeDb) / overlapWindowDb, 0.0, 1.0);
    }

    return t;
}

} // namespace subguard
