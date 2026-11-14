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
    constexpr double lfeLowPassHz = 120.0;
    constexpr double silencePower = 1e-19;      // mean per-bin power under which there is no bass at all
    constexpr int numBands = ChainResult::numBands;

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

    struct Gains { cd screen, lfe; };

    Gains gainsFor (const ChainResult& chain, double hz)
    {
        const double lfeGain = std::pow (10.0, PlaybackSimulator::lfeInBandGainDb / 20.0);

        if (chain.kind == ChainResult::Kind::cinema)
            return { 1.0, lfeGain * (chain.inverted ? -1.0 : 1.0) * std::polar (1.0, -2.0 * pi * hz * chain.delayMs * 0.001) };

        cd lfe = lfeGain * lr4LowPass (hz, lfeLowPassHz);

        if (chain.encoderFilter)
            lfe *= encoderLowPass (hz);

        return { lr4AllPass (hz, chain.crossoverHz), lfe };
    }

    int bandOf (double hz)
    {
        for (int b = 0; b < numBands; ++b)
            if (hz >= PlaybackSimulator::bandLimits[b] && hz < PlaybackSimulator::bandLimits[b + 1])
                return b;

        return -1;
    }

    double limited (double ratio)
    {
        return std::clamp (10.0 * std::log10 (std::max (ratio, 1e-12)),
                           -PlaybackSimulator::changeLimitDb, PlaybackSimulator::changeLimitDb);
    }
}

Translation PlaybackSimulator::compute (const Analysis& a) const
{
    Translation t;

    const auto& m = a.matrix;
    const auto n = a.grid.size();

    if (n == 0 || m.pF.size() != n)
        return t;

    const double lfeGain2 = std::pow (10.0, lfeInBandGainDb / 10.0);

    // Cross term with what chance could produce taken out: nothing below
    // 2 sigma of coherence (chance level), everything above 3 sigma (or above
    // 0.95 when the short average makes 3 sigma unreachable).
    std::vector<cd> cFL (n);

    for (size_t g = 0; g < n; ++g)
    {
        const double sigma = m.sigma[g];
        const double coherence = std::abs (m.cFLfe[g]) / (std::sqrt (m.pF[g] * m.pLfe[g]) + 1e-60);
        const double full = std::max (std::min (3.0 * sigma, 0.95), 2.0 * sigma + 1e-9);
        cFL[g] = m.cFLfe[g] * std::clamp ((coherence - 2.0 * sigma) / (full - 2.0 * sigma), 0.0, 1.0);
    }

    // The systems and their weights
    for (int pol = 0; pol < 2; ++pol)
        for (int d = 0; d < 5; ++d)
        {
            ChainResult c;
            c.kind = ChainResult::Kind::cinema;
            c.delayMs = delaySteps[d];
            c.inverted = pol == 1;
            c.weight = delayRelevance[d];
            char text[96];
            std::snprintf (text, sizeof (text), "cinema, sub %+.1f ms%s", c.delayMs, c.inverted ? ", inverted" : "");
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
            c.weight = homeRelevance;
            c.name = "bass managed " + std::to_string ((int) corner) + " Hz" + (encoder ? ", encoder filter" : "");
            t.chains.push_back (c);
        }

    {
        ChainResult c;
        c.kind = ChainResult::Kind::stereo;
        c.weight = 0.0;     // rated apart, as the dependence on the LFE
        c.name = "stereo downmix without LFE";
        t.chains.push_back (c);
    }

    // Reference: screen plus LFE 10 dB up, on time and in polarity. The grid
    // is logarithmic, so each point stands for a width proportional to its
    // frequency.
    double reference[numBands] = {}, referenceTotal = 0.0, weightTotal = 0.0;
    double screenTotal = 0.0, lfeTotal = 0.0;

    for (size_t g = 0; g < n; ++g)
    {
        const double hz = a.grid[g];
        const int b = bandOf (hz);

        if (b < 0)
            continue;

        const double lfe = lfeGain2 * m.pLfe[g];
        const double power = std::max (0.0, m.pF[g] + lfe + 2.0 * std::sqrt (lfeGain2) * cFL[g].real());
        reference[b] += hz * power;
        referenceTotal += hz * power;
        weightTotal += hz;
        screenTotal += hz * m.pF[g];
        lfeTotal += hz * lfe;
    }

    t.hasBass = weightTotal > 0.0 && (screenTotal + lfeTotal) / weightTotal > silencePower;

    if (! t.hasBass)
        return t;

    t.lfeShare = lfeTotal / (lfeTotal + screenTotal + 1e-60);
    t.lfeBalanceDb = std::clamp (10.0 * std::log10 ((lfeTotal + 1e-60) / (screenTotal + 1e-60)), -60.0, 60.0);

    // Each system against the reference, band by band
    double worst = -1.0;

    for (size_t k = 0; k < t.chains.size(); ++k)
    {
        auto& chain = t.chains[k];
        double band[numBands] = {}, total = 0.0;

        for (size_t g = 0; g < n; ++g)
        {
            const double hz = a.grid[g];
            const int b = bandOf (hz);

            if (b < 0)
                continue;

            double power;

            if (chain.kind == ChainResult::Kind::stereo)
            {
                // Lo = L + 0.707 C, Ro = R + 0.707 C, no LFE
                power = m.pL[g] + m.pC[g] + m.pR[g] + std::sqrt (2.0) * (m.reLC[g] + m.reRC[g]);
            }
            else
            {
                const auto gain = gainsFor (chain, hz);
                power = std::norm (gain.screen) * m.pF[g] + std::norm (gain.lfe) * m.pLfe[g]
                          + 2.0 * (gain.screen * std::conj (gain.lfe) * cFL[g]).real();
            }

            band[b] += hz * std::max (power, 0.0);
            total += hz * std::max (power, 0.0);
        }

        chain.changeDb = limited ((total + 1e-60) / (referenceTotal + 1e-60));
        chain.penalty = 0.0;

        for (int b = 0; b < numBands; ++b)
        {
            if (reference[b] <= 0.0 && band[b] <= 0.0)
                continue;

            chain.bandChangeDb[(size_t) b] = limited ((band[b] + 1e-60) / (reference[b] + 1e-60));

            // What the band is worth: its share of the reference bass, or of
            // this system's bass when the system has some where the reference
            // had almost none.
            const double share = std::max (reference[b] / (referenceTotal + 1e-60),
                                           band[b] / (std::max (total, referenceTotal) + 1e-60));
            const double rated = std::clamp ((std::abs (chain.bandChangeDb[(size_t) b]) - justNoticeableDb)
                                               / (doublingDb[b] - justNoticeableDb), 0.0, 1.0);
            chain.penalty += share * rated;
        }

        chain.penalty = std::min (chain.penalty, 1.0);
        if (chain.kind == ChainResult::Kind::stereo)
        {
            t.downmixChangeDb = chain.changeDb;
            t.dependence = dependenceCeiling * chain.penalty;
        }
        else if (chain.weight * chain.penalty > worst)
        {
            worst = chain.weight * chain.penalty;
            t.worstChain = (int) k;
        }
    }

    // Two separate risks. Interaction: the worst that a playback system does
    // to the bass, since a mix is only as safe as its worst plausible outcome.
    // Dependence: how much of the bass exists only in the LFE.
    t.interaction = std::clamp (worst, 0.0, 1.0);
    t.robustness = 100.0 * (1.0 - t.interaction) * (1.0 - t.dependence);

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
