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

    Gains gainsFor (const ChainResult& chain, double hz, double lfeGain)
    {
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

//==============================================================================
// ISO 226:2003, normal equal-loudness-level contours:
//
//   Lp = (10 / af) . log10 (Af) - Lu + 94
//   Af = 4.47e-3 . (10^(0.025 LN) - 1.15) + (0.4 . 10^((Tf + Lu) / 10 - 9))^af
//
// with the standard's table of af, Lu, Tf at the preferred frequencies.
// Between them the three parameters are interpolated in log frequency.
namespace
{
    struct Iso226Row { double hz, alpha, lu, tf; };

    constexpr Iso226Row iso226Table[] = { { 20.0,  0.532, -31.6, 78.5 }, { 25.0,  0.506, -27.2, 68.7 }, { 31.5, 0.480, -23.0, 59.5 },
                                          { 40.0,  0.455, -19.1, 51.1 }, { 50.0,  0.432, -15.9, 44.0 }, { 63.0, 0.409, -13.0, 37.5 },
                                          { 80.0,  0.387, -10.3, 31.5 }, { 100.0, 0.367, -8.1,  26.5 }, { 125.0, 0.349, -6.2, 22.1 } };

    Iso226Row iso226At (double hz)
    {
        const int last = (int) (sizeof (iso226Table) / sizeof (iso226Table[0])) - 1;
        hz = std::clamp (hz, iso226Table[0].hz, iso226Table[last].hz);

        for (int i = 0; i < last; ++i)
        {
            const auto& a = iso226Table[i];
            const auto& b = iso226Table[i + 1];

            if (hz <= b.hz)
            {
                const double u = std::log (hz / a.hz) / std::log (b.hz / a.hz);
                return { hz, a.alpha + (b.alpha - a.alpha) * u, a.lu + (b.lu - a.lu) * u, a.tf + (b.tf - a.tf) * u };
            }
        }

        return iso226Table[last];
    }
}

double PlaybackSimulator::iso226Spl (double hz, double phon)
{
    const auto r = iso226At (hz);
    const double af = 4.47e-3 * (std::pow (10.0, 0.025 * phon) - 1.15) + std::pow (0.4 * std::pow (10.0, (r.tf + r.lu) / 10.0 - 9.0), r.alpha);
    return 10.0 / r.alpha * std::log10 (af) - r.lu + 94.0;
}

double PlaybackSimulator::iso226Phon (double hz, double splDb)
{
    const auto r = iso226At (hz);
    const double af = std::pow (10.0, (splDb + r.lu - 94.0) * r.alpha / 10.0);
    const double inner = (af - std::pow (0.4 * std::pow (10.0, (r.tf + r.lu) / 10.0 - 9.0), r.alpha)) / 4.47e-3 + 1.15;
    return inner > 0.0 ? 40.0 * std::log10 (inner) : -100.0;      // far below the threshold of hearing
}

double PlaybackSimulator::hearingThresholdDb (double hz)
{
    return iso226At (hz).tf;
}

double PlaybackSimulator::audibleShare (double hz, double splDb, double spreadDb)
{
    // Normal distribution of thresholds: the share whose threshold lies under splDb
    const double z = (splDb - hearingThresholdDb (hz)) / std::max (spreadDb, 0.1);
    return 0.5 * std::erfc (-z / std::sqrt (2.0));
}

//==============================================================================
Translation PlaybackSimulator::compute (const Analysis& a, Level level, double lfeGainDb) const
{
    Translation t;
    const double tolerance = tuning.toleranceDb[(size_t) level];

    // A quarter (by default) at 25 Hz and below, 1 from 60 Hz up, linear in octaves between
    const auto stereoRelevance = [this] (double hz)
    {
        const double lo = tuning.infraLoHz, hi = std::max (tuning.infraHiHz, lo * 1.01);
        const double rise = std::clamp (std::log (hz / lo) / std::log (hi / lo), 0.0, 1.0);
        return tuning.infraWeight + (1.0 - tuning.infraWeight) * rise;
    };

    const auto& m = a.matrix;
    const auto n = a.grid.size();

    if (n == 0 || m.pF.size() != n)
        return t;

    // The LFE as it is played: 10 dB up in a cinema (SMPTE RP 200), or as recorded
    const double lfeGain2 = std::pow (10.0, lfeGainDb / 10.0), lfeGain = std::sqrt (lfeGain2);

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
            c.weight = c.delayMs < 0.0 ? tuning.earlyRelevance : (c.delayMs > 0.0 ? tuning.lateRelevance : 1.0);
            char text[96];
            if (c.delayMs < 0.0 || c.delayMs > 0.0)
                std::snprintf (text, sizeof (text), "cinema, sub %+.1f m (%+.1f ms)%s", c.delayMs * 0.001 * speedOfSound, c.delayMs,
                               c.inverted ? ", inverted" : "");
            else
                std::snprintf (text, sizeof (text), "cinema, sub in place%s", c.inverted ? ", inverted" : "");
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
    double reference[numBands] = {}, referenceTotal = 0.0, weightTotal = 0.0, bandWeight[numBands] = {};
    double smallSystems[numBands] = {};     // reference bass weighted by how much a small system would render it
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
        bandWeight[b] += hz;
        smallSystems[b] += hz * power * stereoRelevance (hz);
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

    // From level to loudness. The reference bass of each band has an RMS
    // level in dBFS; the listening level says what sound pressure that is
    // (the calibration signal, -20 dBFS RMS in one screen channel, gives the
    // chosen dB(C)); ISO 226 gives its loudness level in phon, and how many
    // dB take it 10 phon up, i.e. double its loudness. The standard runs
    // from 20 to 90 phon: the level is kept within 20..80 for that step.
    double doubling[numBands];

    // Sound pressure level of a band from its summed power (same units as reference[b])
    const auto splOf = [&] (int b, double bandPower)
    {
        const double meanPower = bandWeight[b] > 0.0 ? bandPower / bandWeight[b] : 0.0;
        const double rms = meanPower * (bandLimits[b + 1] - bandLimits[b]) * a.windowSeconds / 3.0;    // Hann window
        return tuning.listeningLevelDbC + (10.0 * std::log10 (rms + 1e-30) - referenceDbfs);
    };

    // Share of listeners who hear a band at a given level; 1 when audibility is off.
    // Each rating band is narrower than about one auditory filter (ERB, Glasberg
    // & Moore 1990: 27 Hz at 25 Hz, 36 Hz at 100 Hz), so its whole power is
    // compared with the pure-tone threshold at its centre.
    const auto audible = [&] (int b, double splDb)
    {
        return tuning.audibility ? audibleShare (bandCentreHz[b], splDb, tuning.thresholdSpreadDb) : 1.0;
    };

    for (int b = 0; b < numBands; ++b)
    {
        const double spl = splOf (b, reference[b]);
        const double phon = iso226Phon (bandCentreHz[b], spl);
        const double at = std::clamp (phon, 20.0, 80.0);

        doubling[b] = iso226Spl (bandCentreHz[b], at + 10.0) - iso226Spl (bandCentreHz[b], at);
        t.bandSplDb[(size_t) b] = spl;
        t.bandPhon[(size_t) b] = phon;
        t.bandDoublingDb[(size_t) b] = doubling[b];
        t.bandAudible[(size_t) b] = audible (b, spl);
    }

    // What is heard of the reference: each band weighted by the share of
    // listeners who hear it, and how many hear the bass at all (its most
    // audible band). A system is then rated on the bass that is heard.
    double heardReference[numBands] = {}, heardTotal = 0.0, heardAtAll = 0.0;

    for (int b = 0; b < numBands; ++b)
    {
        heardReference[b] = reference[b] * t.bandAudible[(size_t) b];
        heardTotal += heardReference[b];
        heardAtAll = std::max (heardAtAll, t.bandAudible[(size_t) b]);
    }

    // Each system against the reference, band by band
    double worst = -1.0;
    double levelSum = 0.0, levelWeightSum = 0.0, levelMax = 0.0;

    for (size_t k = 0; k < t.chains.size(); ++k)
    {
        auto& chain = t.chains[k];
        const bool isStereo = chain.kind == ChainResult::Kind::stereo;
        double band[numBands] = {}, total = 0.0;

        // For the level dependence: the same bands with the subwoofer off
        // its level, as they are and as an unrelated LFE would give them
        double screenPart[numBands] = {}, lfePart[numBands] = {}, crossPart[numBands] = {};

        for (size_t g = 0; g < n; ++g)
        {
            const double hz = a.grid[g];
            const int b = bandOf (hz);

            if (b < 0)
                continue;

            double power;

            if (isStereo)
            {
                // The mix with its LFE left out, as every two-channel downmix does.
                // Only the LFE is taken away: the screen channels are summed as
                // in the reference, so that how L, C and R fold together (which
                // is not the LFE's doing) does not count as a loss.
                power = m.pF[g];
            }
            else
            {
                const auto gain = gainsFor (chain, hz, lfeGain);
                const double screen = std::norm (gain.screen) * m.pF[g], lfe = std::norm (gain.lfe) * m.pLfe[g];
                const double cross = 2.0 * (gain.screen * std::conj (gain.lfe) * cFL[g]).real();
                power = screen + lfe + cross;
                screenPart[b] += hz * screen;
                lfePart[b] += hz * lfe;
                crossPart[b] += hz * cross;
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

            // What the band is worth: its share of the heard reference bass, or
            // of this system's heard bass when the system has some where the
            // reference had almost none (inaudible bass made audible by a boost)
            const double heardBand = band[b] * audible (b, splOf (b, band[b]));
            const double share = std::max (heardReference[b] / (heardTotal + 1e-60),
                                           heardBand / (std::max (heardTotal, heardBand) + 1e-60));
            // 0 up to the tolerance, 1 at a doubling of loudness (the tolerance stays under it)
            const double allowed = std::min (tolerance, doubling[b] - 0.5);
            const double rated = std::clamp ((std::abs (chain.bandChangeDb[(size_t) b]) - allowed)
                                               / (doubling[b] - allowed), 0.0, 1.0);

            // In a stereo downmix, the very bottom of the spectrum does not
            // count: it is felt in a cinema, not heard on the systems a
            // downmix is played on.
            const double relevance = isStereo && reference[b] > 0.0 ? smallSystems[b] / reference[b] : 1.0;
            chain.penalty += share * rated * relevance;
        }

        // Scaled by how many listeners hear the bass at all
        chain.penalty = std::min (chain.penalty, 1.0) * heardAtAll;

        if (isStereo)
        {
            t.downmixChangeDb = chain.changeDb;
            t.dependence = tuning.dependenceCeiling * chain.penalty;
            continue;
        }

        if (chain.weight * chain.penalty > worst)
        {
            worst = chain.weight * chain.penalty;
            t.worstChain = (int) k;
        }

        // Level dependence of this system: one value per subwoofer level, the
        // largest difference from the expectation over the bands that carry
        // a real part of the bass
        for (int v = 0; v < numLevelSteps; ++v)
        {
            const double gain = std::pow (10.0, levelSteps[v] / 20.0);
            double worstBand = 0.0;

            for (int b = 0; b < numBands; ++b)
            {
                const double unrelated = screenPart[b] + lfePart[b];

                if (unrelated < levelBandShare * (screenTotal + lfeTotal) || lfePart[b] <= 0.0)
                    continue;

                const double now = std::max (unrelated + crossPart[b], 0.0);
                const double moved = std::max (screenPart[b] + gain * gain * lfePart[b] + gain * crossPart[b], 0.0);
                const double heard = audible (b, splOf (b, std::max (now, moved)));
                const double expected = limited ((screenPart[b] + gain * gain * lfePart[b] + 1e-60) / (unrelated + 1e-60));
                const double actual = limited ((std::max (screenPart[b] + gain * gain * lfePart[b] + gain * crossPart[b], 0.0) + 1e-60)
                                                 / (now + 1e-60));

                if (heard * std::abs (actual - expected) > std::abs (worstBand))
                    worstBand = heard * (actual - expected);
            }

            levelSum += levelStepWeight[v] * std::abs (worstBand);
            ++t.levelScenarios;
            levelWeightSum += levelStepWeight[v];

            if (levelStepWeight[v] * std::abs (worstBand) > std::abs (levelMax))
                levelMax = levelStepWeight[v] * worstBand;
        }
    }

    // Mean over every system and subwoofer level (a problem that only shows
    // 6 dB off counts for less), and the largest
    t.levelMeanDb = levelWeightSum > 0.0 ? levelSum / levelWeightSum : 0.0;
    t.levelDeviationDb = levelMax;
    t.levelDependence = std::clamp (t.levelMeanDb / std::max (tuning.levelRedDb, 0.1), 0.0, 1.0);

    // Two separate risks. Interaction: the worst that a playback system does
    // to the bass, since a mix is only as safe as its worst plausible outcome.
    // Dependence: how much of the bass exists only in the LFE.
    t.interaction = std::clamp (worst, 0.0, 1.0);
    t.robustness = 100.0 * (1.0 - t.interaction) * (1.0 - t.dependence) * (1.0 - tuning.levelDependenceWeight * t.levelDependence);

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
