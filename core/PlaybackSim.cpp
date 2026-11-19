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
// ISO 226:2023, normal equal-loudness-level contours, Formulas (1) and (2):
//
//   Lf = (10 / af) lg [ (4e-10)^(0.3 - af) (10^(0.03 LN) - 10^0.072) + 10^(af (Tf + LU) / 10) ] - LU
//   LN = (100 / 3) lg [ (10^(af (Lf + LU) / 10) - 10^(af (Tf + LU) / 10)) / (4e-10)^(0.3 - af) + 10^0.072 ]
//
// (0.3: the loudness exponent at 1 kHz; 0.072 = 0.3 x 2.4 dB / 10, the
// threshold at 1 kHz; 4e-10 = (20 uPa / 1 Pa)^2), with Table 1 of the
// standard (af, LU, Tf; Tf as in ISO 389-7:2019). Valid from 20 to 90 phon,
// informative below 20 phon. Between the preferred frequencies the three
// parameters are interpolated in log frequency. The 2023 edition differs
// from the 2003 one by up to 0.6 dB (ISO 226:2023, Note 3).
namespace
{
    struct Iso226Row { double hz, alpha, lu, tf; };

    constexpr Iso226Row iso226Table[] = { { 20.0,  0.635, -31.5, 78.1 }, { 25.0,  0.602, -27.2, 68.7 }, { 31.5, 0.569, -23.1, 59.5 },
                                          { 40.0,  0.537, -19.3, 51.1 }, { 50.0,  0.509, -16.1, 44.0 }, { 63.0, 0.482, -13.1, 37.5 },
                                          { 80.0,  0.456, -10.4, 31.5 }, { 100.0, 0.433, -8.2,  26.5 }, { 125.0, 0.412, -6.3, 22.1 } };

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

    constexpr double iso226Reference = 4e-10;       // (p0 / 1 Pa)^2, p0 = 20 uPa
}

double PlaybackSimulator::iso226Spl (double hz, double phon)
{
    const auto r = iso226At (hz);
    const double inner = std::pow (iso226Reference, 0.3 - r.alpha) * (std::pow (10.0, 0.03 * phon) - std::pow (10.0, 0.072))
                       + std::pow (10.0, r.alpha * (r.tf + r.lu) / 10.0);
    return 10.0 / r.alpha * std::log10 (inner) - r.lu;
}

double PlaybackSimulator::iso226Phon (double hz, double splDb)
{
    const auto r = iso226At (hz);
    const double inner = (std::pow (10.0, r.alpha * (splDb + r.lu) / 10.0) - std::pow (10.0, r.alpha * (r.tf + r.lu) / 10.0))
                           / std::pow (iso226Reference, 0.3 - r.alpha)
                       + std::pow (10.0, 0.072);
    return inner > 0.0 ? 100.0 / 3.0 * std::log10 (inner) : -100.0;      // far below the threshold of hearing
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
double Translation::audibleAt (int band, double shiftDb, double spreadDb, bool enabled) const
{
    if (! enabled)
        return 1.0;

    const auto& profile = audibilityProfile[(size_t) band];
    double heard = 0.0, total = 0.0;

    for (const auto& p : profile)
    {
        heard += p.weight * PlaybackSimulator::audibleShare (p.hz, p.levelDb + shiftDb, spreadDb);
        total += p.weight;
    }

    // No reference bass in the band: the whole band against the threshold at its centre
    if (total <= 0.0)
        return PlaybackSimulator::audibleShare (PlaybackSimulator::bandCentreHz[band], bandSplDb[(size_t) band] + shiftDb, spreadDb);

    return heard / total;
}

const char* PlaybackSimulator::otherSystemName (int which)
{
    switch (which)
    {
        case stereoPaNoLfe:     return "Other system: stereo PA, subwoofers on L + R, no LFE";
        case surroundPaWithLfe: return "Other system: 5.1 on a PA, subwoofers on L + R + LFE";
        case smallSpeakers:     return "Other system: small speakers, no subwoofer";
        default:                return "";
    }
}

const char* PlaybackSimulator::otherSystemShortName (int which)
{
    switch (which)
    {
        case stereoPaNoLfe:     return "Stereo PA";
        case surroundPaWithLfe: return "5.1 PA + LFE";
        case smallSpeakers:     return "Small speakers";
        default:                return "";
    }
}

const char* PlaybackSimulator::otherSystemDescription (int which)
{
    switch (which)
    {
        case stereoPaNoLfe:     return "A stereo fold-down (L + 0.71 C, R + 0.71 C, the LFE left out) on a PA whose subwoofers play L + R, as at a show or in a shop";
        case surroundPaWithLfe: return "The 5.1 on a PA whose processor feeds the subwoofers L + R + C and the LFE (+10 dB), as at an event";
        case smallSpeakers:     return "A stereo fold-down on small speakers that roll off under 60 Hz (12 dB per octave), no subwoofer, no LFE";
        default:                return "";
    }
}

void PlaybackSimulator::computeOthers (const Analysis& a, Translation& t, Level level, double lfeGain,
                                       const double* approved, double approvedTotal, const double* doubling) const
{
    const auto& m = a.matrix;
    const auto n = a.grid.size();

    if (m.cLC.size() != n || m.cLLfe.size() != n)
        return;

    const double tolerance = tuning.toleranceDb[(size_t) level];

    // A cross-spectrum with what chance could produce taken out (as for the LFE)
    const bool ownScreenLevel = m.sigmaScreen.size() == n;
    const auto shrunk = [&] (cd c, double pa, double pb, size_t g, bool screenPair)
    {
        const double sigma = screenPair && ownScreenLevel ? m.sigmaScreen[g] : m.sigma[g];
        const double coherence = std::abs (c) / (std::sqrt (pa * pb) + 1e-60);
        const double full = std::max (std::min (3.0 * sigma, 0.95), 2.0 * sigma + 1e-9);
        return c * std::clamp ((coherence - 2.0 * sigma) / (full - 2.0 * sigma), 0.0, 1.0);
    };

    // Per band: the screen as summed (cancellations in), the same without its
    // cancellations, and each pair's cancellation
    // Channels: L, C, R, the LFE (as heard), S (surrounds and heights, summed)
    constexpr int nc = 5;
    const bool withS = m.pS.size() == n && m.cSL.size() == n && m.cSLfe.size() == n;
    double summed[numBands] = {}, uncancelled[numBands] = {}, pairLoss[numBands][4] = {};
    std::vector<std::array<cd, nc * nc>> spectra (n);   // S_ab = a.conj(b)

    for (size_t g = 0; g < n; ++g)
    {
        const double ps = withS ? m.pS[g] : 0.0;
        const double p[nc] = { m.pL[g], m.pC[g], m.pR[g], lfeGain * lfeGain * m.pLfe[g], ps };
        auto& S = spectra[g];

        for (int i = 0; i < nc; ++i)
            S[(size_t) (i * nc + i)] = p[i];

        const cd lc = shrunk (m.cLC[g], p[0], p[1], g, true), lr = shrunk (m.cLR[g], p[0], p[2], g, true), cr = shrunk (m.cCR[g], p[1], p[2], g, true);
        const cd le = lfeGain * shrunk (m.cLLfe[g], p[0], m.pLfe[g], g, false), ce = lfeGain * shrunk (m.cCLfe[g], p[1], m.pLfe[g], g, false),
                 re = lfeGain * shrunk (m.cRLfe[g], p[2], m.pLfe[g], g, false);
        const cd sl = withS ? shrunk (m.cSL[g], ps, p[0], g, true) : cd(), sc = withS ? shrunk (m.cSC[g], ps, p[1], g, true) : cd(),
                 sr = withS ? shrunk (m.cSR[g], ps, p[2], g, true) : cd(), se = withS ? lfeGain * shrunk (m.cSLfe[g], ps, m.pLfe[g], g, false) : cd();
        struct Cross { cd value; int i, j; };
        const Cross crosses[10] = { { lc, 0, 1 }, { lr, 0, 2 }, { cr, 1, 2 }, { le, 0, 3 }, { ce, 1, 3 }, { re, 2, 3 },
                                    { sl, 4, 0 }, { sc, 4, 1 }, { sr, 4, 2 }, { se, 4, 3 } };

        for (const auto& x : crosses)
        {
            S[(size_t) (x.i * nc + x.j)] = x.value;
            S[(size_t) (x.j * nc + x.i)] = std::conj (x.value);
        }

        const double hz = a.grid[g];
        const int b = bandOf (hz);

        if (b < 0)
            continue;

        // L / R, L / C, C / R, then the screen against the surrounds and heights
        const double diag = p[0] + p[1] + p[2] + ps;
        const double pairs[4] = { 2.0 * lr.real(), 2.0 * lc.real(), 2.0 * cr.real(), 2.0 * (sl + sc + sr).real() };
        const double sPairs[3] = { 2.0 * sl.real(), 2.0 * sc.real(), 2.0 * sr.real() };
        summed[b] += hz * std::max (0.0, diag + pairs[0] + pairs[1] + pairs[2] + pairs[3]);
        uncancelled[b] += hz * (diag + std::max (0.0, pairs[0]) + std::max (0.0, pairs[1]) + std::max (0.0, pairs[2])
                                + std::max (0.0, sPairs[0]) + std::max (0.0, sPairs[1]) + std::max (0.0, sPairs[2]));

        for (int k = 0; k < 3; ++k)
            pairLoss[b][k] += hz * std::min (0.0, pairs[k]);

        pairLoss[b][3] += hz * (std::min (0.0, sPairs[0]) + std::min (0.0, sPairs[1]) + std::min (0.0, sPairs[2]));
    }

    // The opposition of phase, band by band, and where it is worst
    t.antiphaseWorstDb = 0.0;
    t.antiphasePair = -1;

    for (int b = 0; b < numBands; ++b)
    {
        t.antiphaseDb[(size_t) b] = uncancelled[b] > 1e-6 * (approvedTotal + 1e-60) ? std::min (0.0, limited ((summed[b] + 1e-60) / uncancelled[b])) : 0.0;

        if (t.antiphaseDb[(size_t) b] < t.antiphaseWorstDb)
        {
            t.antiphaseWorstDb = t.antiphaseDb[(size_t) b];
            t.antiphaseWorstHz = bandCentreHz[b];
            t.antiphasePair = (int) (std::min_element (pairLoss[b], pairLoss[b] + 4) - pairLoss[b]);
        }
    }

    // The other systems: gains of L, C, R and the LFE at each frequency
    for (int which = 0; which < numOtherSystems; ++which)
    {
        ChainResult chain;
        chain.kind = ChainResult::Kind::other;
        chain.other = which;
        chain.name = otherSystemName (which);
        chain.weight = 1.0;
        const bool withLfe = which == surroundPaWithLfe;
        double band[numBands] = {}, reference[numBands] = {}, total = 0.0, referenceTotal = 0.0;

        for (size_t g = 0; g < n; ++g)
        {
            const double hz = a.grid[g];
            const int b = bandOf (hz);

            if (b < 0)
                continue;

            // The fold-down sums L + 0.707 C and R + 0.707 C (ITU-R BS.775, LFE left out); the subwoofers play their sum
            constexpr double centreIntoEachSide = 0.70710678118654752;     // 1 / sqrt 2, ITU-R BS.775 downmix
            // The surrounds and heights fold in at 0.707 too (Ls into Lo, Rs into Ro), then into the subwoofers
            cd gains[nc] = { 1.0, 2.0 * centreIntoEachSide, 1.0, withLfe ? 1.0 : 0.0, centreIntoEachSide };

            if (which == smallSpeakers)
            {
                // Second-order high-pass at 60 Hz (Butterworth, Q 0.71): a closed box is a second-order
                // high-pass (Small 1972); the 60 Hz corner is a hypothesis for small speakers
                const cd s (0.0, hz / 60.0);
                const cd highPass = s * s / (s * s + std::sqrt (2.0) * s + 1.0);

                for (auto& gain : gains)
                    gain *= highPass;
            }

            // Reference: the same channels as in this system (with or without the LFE), cancellations left out
            const auto& S = spectra[g];
            double power = 0.0, uncancelledHere = 0.0;

            for (int i = 0; i < nc; ++i)
                for (int j = 0; j < nc; ++j)
                {
                    power += (gains[i] * std::conj (gains[j]) * S[(size_t) (i * nc + j)]).real();

                    const bool used = i == 3 || j == 3 ? withLfe : true;

                    if (used)
                        uncancelledHere += i == j ? S[(size_t) (i * nc + i)].real() : std::max (0.0, S[(size_t) (i * nc + j)].real());
                }

            band[b] += hz * std::max (0.0, power);
            reference[b] += hz * uncancelledHere;
            total += hz * std::max (0.0, power);
            referenceTotal += hz * uncancelledHere;
        }

        // Rated as the playback systems: band by band, the tolerance, a doubling of loudness for the full penalty,
        // each band by its share of the heard reference
        chain.changeDb = limited ((total + 1e-60) / (referenceTotal + 1e-60));
        double heardTotal = 0.0, heardBand[numBands] = {};
        bool anyBand = false;

        for (int b = 0; b < numBands; ++b)
        {
            heardBand[b] = reference[b] * t.bandAudible[(size_t) b];
            heardTotal += heardBand[b];
        }

        for (int b = 0; b < numBands; ++b)
        {
            if (reference[b] <= 1e-6 * (referenceTotal + 1e-60))
                continue;

            chain.bandChangeDb[(size_t) b] = limited ((band[b] + 1e-60) / reference[b]);
            const bool first = ! anyBand;
            anyBand = true;
            chain.minChangeDb = first ? chain.bandChangeDb[(size_t) b] : std::min (chain.minChangeDb, chain.bandChangeDb[(size_t) b]);
            chain.maxChangeDb = first ? chain.bandChangeDb[(size_t) b] : std::max (chain.maxChangeDb, chain.bandChangeDb[(size_t) b]);
            const double allowed = std::min (tolerance, doubling[b] - 0.5);
            const double rated = std::clamp ((std::abs (chain.bandChangeDb[(size_t) b]) - allowed) / (doubling[b] - allowed), 0.0, 1.0);
            chain.bandRated[(size_t) b] = rated;
            chain.penalty += heardBand[b] / (heardTotal + 1e-60) * rated;
        }

        double heardAtAll = 0.0;

        for (double v : t.bandAudible)
            heardAtAll = std::max (heardAtAll, v);

        chain.penalty = std::min (chain.penalty, 1.0) * heardAtAll;
        t.others.push_back (chain);
    }

    (void) approved;
}

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
    double screenTotal = 0.0, lfeTotal = 0.0, screenChannels = 0.0;
    std::vector<double> referencePower (n, 0.0);

    // Surrounds and heights (S): from their own speakers in a cinema, so added in power to the
    // reference and to every cinema chain; summed with the screen into the subwoofer once bass managed
    const bool withS = m.pS.size() == n && m.cSL.size() == n && m.cSLfe.size() == n;
    std::vector<double> pS (n, 0.0);
    std::vector<cd> cSF (n), cSFL (n);      // S.conj(F) and S.conj(LFE), chance taken out

    if (withS)
        for (size_t g = 0; g < n; ++g)
        {
            pS[g] = m.pS[g];
            const double sigmaS = m.sigmaScreen.size() == n ? m.sigmaScreen[g] : m.sigma[g];
            const cd sf = m.cSL[g] + m.cSC[g] + m.cSR[g];
            const auto shrink = [] (cd c, double pa, double pb, double sigma)
            {
                const double coherence = std::abs (c) / (std::sqrt (pa * pb) + 1e-60);
                const double full = std::max (std::min (3.0 * sigma, 0.95), 2.0 * sigma + 1e-9);
                return c * std::clamp ((coherence - 2.0 * sigma) / (full - 2.0 * sigma), 0.0, 1.0);
            };
            cSF[g] = shrink (sf, pS[g], m.pF[g], sigmaS);
            cSFL[g] = shrink (m.cSLfe[g], pS[g], m.pLfe[g], m.sigma[g]);
        }

    for (size_t g = 0; g < n; ++g)
        referencePower[g] = std::max (0.0, m.pF[g] + lfeGain2 * m.pLfe[g] + 2.0 * lfeGain * cFL[g].real()) + pS[g];

    double surroundTotal = 0.0;

    for (size_t g = 0; g < n; ++g)
    {
        const double hz = a.grid[g];
        const int b = bandOf (hz);

        if (b < 0)
            continue;

        const double lfe = lfeGain2 * m.pLfe[g];
        const double power = referencePower[g];
        reference[b] += hz * power;
        bandWeight[b] += hz;
        smallSystems[b] += hz * power * stereoRelevance (hz);
        referenceTotal += hz * power;
        weightTotal += hz;
        screenTotal += hz * m.pF[g];
        screenChannels += hz * (m.pL[g] + m.pC[g] + m.pR[g]);
        surroundTotal += hz * pS[g];
        lfeTotal += hz * lfe;
    }

    // Bass is there if any channel carries it: a screen whose channels cancel in their sum
    // (L and R in opposition) still has bass, which any other summing will lose
    t.hasBass = weightTotal > 0.0 && (std::max (screenTotal, screenChannels) + surroundTotal + lfeTotal) / weightTotal > silencePower;

    if (! t.hasBass)
        return t;

    t.lfeShare = lfeTotal / (lfeTotal + screenTotal + surroundTotal + 1e-60);
    t.lfeBalanceDb = std::clamp (10.0 * std::log10 ((lfeTotal + 1e-60) / (screenTotal + surroundTotal + 1e-60)), -60.0, 60.0);
    t.hasSurround = surroundTotal > 0.01 * (screenTotal + 1e-60);
    t.surroundBalanceDb = std::clamp (10.0 * std::log10 ((surroundTotal + 1e-60) / (screenTotal + 1e-60)), -60.0, 60.0);
    t.surroundShare = surroundTotal / (lfeTotal + screenTotal + surroundTotal + 1e-60);

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

    // Audibility point by point. At each grid point, the level of the
    // reference over one auditory filter around it (ERB, Glasberg & Moore
    // 1990: 24.7 (4.37 f / 1000 + 1) Hz, 27 Hz at 25 Hz), against the
    // threshold at that frequency; a band's audibility is the share of its
    // power above threshold. A band at another level (a playback system, a
    // seat) is the same profile moved by the difference.
    for (size_t g = 0; g < n; ++g)
    {
        const double hz = a.grid[g];
        const int b = bandOf (hz);

        if (b < 0)
            continue;

        const double erb = 24.7 * (4.37 * hz / 1000.0 + 1.0);
        double sum = 0.0, weight = 0.0;

        for (size_t k = 0; k < n; ++k)
            if (std::abs (a.grid[k] - hz) <= 0.5 * erb)
            {
                sum += a.grid[k] * referencePower[k];
                weight += a.grid[k];
            }

        const double rms = (weight > 0.0 ? sum / weight : 0.0) * erb * a.windowSeconds / 3.0;     // Hann window
        const double level = tuning.listeningLevelDbC + (10.0 * std::log10 (rms + 1e-30) - referenceDbfs);
        t.audibilityProfile[(size_t) b].push_back ({ hz * referencePower[g], hz, level });
    }

    // Share of listeners who hear a band at a given level; 1 when audibility is off
    const auto audible = [&] (int b, double splDb)
    {
        return t.audibleAt (b, splDb - t.bandSplDb[(size_t) b], tuning.thresholdSpreadDb, tuning.audibility);
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
        t.bandAudible[(size_t) b] = t.audibleAt (b, 0.0, tuning.thresholdSpreadDb, tuning.audibility);
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
                power = m.pF[g] + pS[g];
            }
            else
            {
                const auto gain = gainsFor (chain, hz, lfeGain);
                const bool managed = chain.kind == ChainResult::Kind::bassManaged;

                // Bass managed: the surrounds and heights join the screen in the subwoofer (coherent sum);
                // in a cinema they stay on their own speakers (power sum)
                const double pX = managed ? std::max (0.0, m.pF[g] + pS[g] + 2.0 * cSF[g].real()) : m.pF[g];
                const cd cXL = managed ? cFL[g] + cSFL[g] : cFL[g];
                const double screen = std::norm (gain.screen) * pX + (managed ? 0.0 : pS[g]), lfe = std::norm (gain.lfe) * m.pLfe[g];
                const double cross = 2.0 * (gain.screen * std::conj (gain.lfe) * cXL).real();
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
        bool anyBand = false;

        for (int b = 0; b < numBands; ++b)
        {
            if (reference[b] <= 0.0 && band[b] <= 0.0)
                continue;

            chain.bandChangeDb[(size_t) b] = limited ((band[b] + 1e-60) / (reference[b] + 1e-60));

            // The band that moves most down and most up (bands with no reference bass left out)
            if (reference[b] > 1e-6 * referenceTotal)
            {
                const bool first = ! anyBand;
                anyBand = true;
                chain.minChangeDb = first ? chain.bandChangeDb[(size_t) b] : std::min (chain.minChangeDb, chain.bandChangeDb[(size_t) b]);
                chain.maxChangeDb = first ? chain.bandChangeDb[(size_t) b] : std::max (chain.maxChangeDb, chain.bandChangeDb[(size_t) b]);
            }

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

    computeOthers (a, t, level, lfeGain, reference, referenceTotal, doubling);

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
