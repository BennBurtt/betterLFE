#include "RoomSim.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace subguard
{

namespace
{
    using cd = std::complex<double>;
    constexpr double pi = 3.14159265358979323846;

    using Point = std::array<double, 3>;

    // Where things are, for a room of the given size: three screen speakers
    // behind the screen, two subwoofers at the foot of the front wall, twenty
    // seats (5 across, 4 deep) from a third to 85 % of the length, at 1.2 m.
    std::vector<Point> screenSpeakers (const RoomSpec& r)
    {
        return { { 0.6, r.width * 0.15, r.height * 0.4 }, { 0.6, r.width * 0.5, r.height * 0.4 }, { 0.6, r.width * 0.85, r.height * 0.4 } };
    }

    std::vector<Point> subwoofers (const RoomSpec& r)
    {
        return { { 0.4, r.width * 0.3, 0.4 }, { 0.4, r.width * 0.7, 0.4 } };
    }

    std::vector<Point> seats (const RoomSpec& r)
    {
        std::vector<Point> s;

        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 5; ++col)
                s.push_back ({ r.length * (0.35 + 0.5 * row / 3.0), r.width * (0.18 + 0.64 * col / 4.0), 1.2 });

        return s;
    }

    using Mode = RoomSimulator::Mode;
}

std::vector<RoomSimulator::Mode> RoomSimulator::modesOf (const RoomSpec& r, double highestHz)
{
        std::vector<Mode> modes;
        const double fmax = highestHz, c = speedOfSound;
        const int mx = (int) (2.0 * r.length * fmax / c), my = (int) (2.0 * r.width * fmax / c), mz = (int) (2.0 * r.height * fmax / c);
        const double delta = 6.91 / std::max (r.rt60, 0.05);

        for (int nx = 0; nx <= mx; ++nx)
            for (int ny = 0; ny <= my; ++ny)
                for (int nz = 0; nz <= mz; ++nz)
                {
                    const double k2 = std::pow (nx / r.length, 2.0) + std::pow (ny / r.width, 2.0) + std::pow (nz / r.height, 2.0);
                    const double f = 0.5 * c * std::sqrt (k2);

                    if (f > fmax)
                        continue;

                    const double lambda = (nx > 0 ? 0.5 : 1.0) * (ny > 0 ? 0.5 : 1.0) * (nz > 0 ? 0.5 : 1.0);
                    modes.push_back ({ std::pow (2.0 * pi * f, 2.0), delta, 1.0 / lambda, nx, ny, nz });
                }

        return modes;
}

double RoomSimulator::shape (const RoomSpec& r, const Mode& m, const Point& p)
{
    return std::cos (m.nx * pi * p[0] / r.length) * std::cos (m.ny * pi * p[1] / r.width) * std::cos (m.nz * pi * p[2] / r.height);
}

namespace
{
    double limitedDb (double ratio)
    {
        return std::clamp (10.0 * std::log10 (std::max (ratio, 1e-12)), -24.0, 24.0);
    }
}

RoomSimulator::RoomSimulator() : specs (defaultRooms()) {}

std::vector<RoomSpec> RoomSimulator::defaultRooms()
{
    // Representative rooms, not measured ones: sizes and reverberation times
    // typical of each kind. From the worst kind to the best.
    return {
        { "Corridor room, live",             "Corridor",          18.0,  6.0,  5.0, 0.90 },
        { "Small multiplex, live",           "Small multiplex",   14.0,  8.0,  5.0, 0.80 },
        { "Old cinema, near-cubic, live",    "Old cinema",        16.0, 15.0, 12.0, 1.00 },
        { "Narrow 120 seats",                "Narrow 120",        20.0,  9.0,  6.0, 0.70 },
        { "Small screening room",            "Screening room",    10.0,  6.0,  3.5, 0.60 },
        { "Multiplex 150 seats",             "Multiplex 150",     16.0, 11.0,  6.0, 0.60 },
        { "Multiplex 200 seats",             "Multiplex 200",     20.0, 13.0,  7.0, 0.55 },
        { "Multiplex 300 seats",             "Multiplex 300",     24.0, 15.0,  8.0, 0.60 },
        { "Large 400 seats",                 "Large 400",         28.0, 18.0, 10.0, 0.60 },
        { "Mix room (240 m3)",               "Mix room",          10.0,  6.0,  4.0, 0.30 },
        { "Dubbing stage",                   "Dubbing stage",     18.0, 12.0,  7.0, 0.35 },
        { "Premium large format",            "Premium large",     30.0, 20.0, 12.0, 0.50 },
        { "Large 400 seats, treated",        "Large 400 treated", 28.0, 18.0, 10.0, 0.40 },
        { "IMAX",                            "IMAX",              30.0, 22.0, 16.0, 0.60 },
        { "IMAX, treated",                   "IMAX treated",      30.0, 22.0, 16.0, 0.40 },
    };
}

std::vector<bool> RoomSimulator::defaultSelection()
{
    // Multiplex 150, 200 and 300 seats, large 400 seats, premium large format
    std::vector<bool> on (defaultRooms().size(), false);

    for (size_t i : { 5u, 6u, 7u, 8u, 11u })
        on[i] = true;

    return on;
}

void RoomSimulator::applyToScore (Translation& t, const RoomSimResult& rooms, const std::vector<bool>& selected, double levelDependenceWeight)
{
    if (! t.hasBass || ! rooms.hasBass)
        return;

    for (size_t i = 0; i < rooms.rooms.size() && i < selected.size(); ++i)
    {
        if (! selected[i])
            continue;

        const double penalty = std::clamp (1.0 - rooms.rooms[i].roomScore / 100.0, 0.0, 1.0);

        if (penalty > t.interaction)
        {
            t.interaction = penalty;
            t.worstRoom = rooms.rooms[i].spec.name;
        }
    }

    t.robustness = 100.0 * (1.0 - t.interaction) * (1.0 - t.dependence) * (1.0 - levelDependenceWeight * t.levelDependence);
}

cd RoomSimulator::response (const RoomSpec& r, const Point& source, const Point& receiver, double hz)
{
    const double w = 2.0 * pi * hz;
    cd sum;

    for (const auto& m : modesOf (r))
        sum += shape (r, m, source) * shape (r, m, receiver) * m.scale / cd (m.omega2 - w * w, 2.0 * m.damping * w);

    return sum;
}

void RoomPaths::accumulate (ChannelMatrix& m, const std::array<cd, numChannels>& h, double weight)
{
    for (int a = 0; a < numChannels; ++a)
        for (int b = 0; b < numChannels; ++b)
            m[(size_t) (a * numChannels + b)] += weight * h[(size_t) a] * std::conj (h[(size_t) b]);
}

std::vector<int> RoomSimulator::bandPoints (const std::vector<double>& grid, double lowHz)
{
    std::vector<int> points;

    for (size_t g = 0; g < grid.size(); ++g)
        if (grid[g] >= lowHz && grid[g] < bandHiHz)
            points.push_back ((int) g);

    return points;
}

void RoomSimulator::calibrate (RoomPaths& p, const std::vector<double>& grid, const std::vector<int>& calibrationSeats)
{
    // As in a real room: each channel's speakers are set so that their level
    // averaged over the calibration seats (20-120 Hz) equals the level on the
    // stage, one channel at a time. Each seat then sits above or below by what
    // the room does there.
    constexpr int nc = RoomPaths::numChannels;
    double mean[nc] = {}, weight = 0.0;
    const size_t n = p.points.size();

    for (int seat = 0; seat < p.seats; ++seat)
    {
        if (! calibrationSeats.empty() && std::find (calibrationSeats.begin(), calibrationSeats.end(), seat) == calibrationSeats.end())
            continue;

        for (size_t i = 0; i < n; ++i)
        {
            const double hz = grid[(size_t) p.points[i]];
            const auto& m = p.p[(size_t) seat * n + i];

            for (int a = 0; a < nc; ++a)
                mean[a] += hz * m[(size_t) (a * nc + a)].real();

            weight += hz;
        }
    }

    double scale[nc];

    for (int a = 0; a < nc; ++a)
        scale[a] = weight > 0.0 && mean[a] > 0.0 ? std::sqrt (weight / mean[a]) : 1.0;

    for (auto& m : p.p)
        for (int a = 0; a < nc; ++a)
            for (int b = 0; b < nc; ++b)
                m[(size_t) (a * nc + b)] *= scale[a] * scale[b];
}

void RoomSimulator::prepare (const std::vector<double>& grid)
{
    preparedGrid = grid;
    const auto points = bandPoints (grid);
    prepared.clear();

    for (const auto& spec : specs)
    {
        RoomPaths p;
        p.spec = spec;
        p.points = points;
        const auto modes = modesOf (spec);
        const auto screen = screenSpeakers (spec), subs = subwoofers (spec), seatPoints = seats (spec);
        p.seats = (int) seatPoints.size();

        // Source terms once per mode: each screen speaker plays its own channel, the subwoofers the LFE
        std::vector<std::array<double, RoomPaths::numChannels>> atSource (modes.size());

        for (size_t k = 0; k < modes.size(); ++k)
        {
            for (size_t c = 0; c < 3; ++c)
                atSource[k][c] = shape (spec, modes[k], screen[c]);

            atSource[k][3] = 0.0;

            for (const auto& s : subs)
                atSource[k][3] += shape (spec, modes[k], s) / (double) subs.size();
        }

        p.p.assign (seatPoints.size() * points.size(), {});

        for (size_t seat = 0; seat < seatPoints.size(); ++seat)
        {
            std::vector<double> atSeat (modes.size());

            for (size_t k = 0; k < modes.size(); ++k)
                atSeat[k] = shape (spec, modes[k], seatPoints[seat]) * modes[k].scale;

            for (size_t i = 0; i < points.size(); ++i)
            {
                const double w = 2.0 * pi * grid[(size_t) points[i]];
                std::array<cd, RoomPaths::numChannels> h {};

                for (size_t k = 0; k < modes.size(); ++k)
                {
                    const cd term = atSeat[k] / cd (modes[k].omega2 - w * w, 2.0 * modes[k].damping * w);

                    for (size_t c = 0; c < RoomPaths::numChannels; ++c)
                        h[c] += atSource[k][c] * term;
                }

                RoomPaths::accumulate (p.p[seat * points.size() + i], h);
            }
        }

        calibrate (p, grid);
        prepared.push_back (std::move (p));
    }
}

RoomSimResult RoomSimulator::compute (const Analysis& a, const Translation& t, const PlaybackSimulator::Tuning& tuning,
                                      PlaybackSimulator::Level level, double lfeGainDb)
{
    RoomSimResult out;

    if (a.grid.empty() || a.matrix.pF.size() != a.grid.size() || ! t.hasBass)
        return out;

    if (a.grid != preparedGrid)
        prepare (a.grid);

    out.hasBass = true;

    for (const auto& room : prepared)
        out.rooms.push_back (rate (room, a, t, tuning, level, lfeGainDb));

    return out;
}

double RoomSimulator::roomScoreOf (const std::vector<double>& seatPenalty, const PlaybackSimulator::Tuning& tuning)
{
    // Mean seat score, pulled down by the worst seats
    if (seatPenalty.empty())
        return 100.0;

    auto sorted = seatPenalty;
    std::sort (sorted.begin(), sorted.end(), std::greater<double>());
    const size_t worst = std::max<size_t> (1, (size_t) std::lround (tuning.roomWorstShare * (double) sorted.size()));
    double mean = 0.0, worstMean = 0.0;

    for (size_t i = 0; i < sorted.size(); ++i)
    {
        mean += sorted[i] / (double) sorted.size();

        if (i < worst)
            worstMean += sorted[i] / (double) worst;
    }

    const double w = std::clamp (tuning.roomWorstWeight, 0.0, 1.0);
    return 100.0 * (1.0 - ((1.0 - w) * mean + w * worstMean));
}

RoomResult RoomSimulator::rate (const RoomPaths& room, const Analysis& a, const Translation& t, const PlaybackSimulator::Tuning& tuning,
                                PlaybackSimulator::Level level, double lfeGainDb)
{
    RoomResult r;
    r.spec = room.spec;
    const auto& m = a.matrix;
    const auto& points = room.points;
    const double toleranceDb = tuning.toleranceDb[(size_t) level];
    const double gain = std::pow (10.0, lfeGainDb / 20.0);
    constexpr int numBands = ChainResult::numBands;

    if (room.seats <= 0 || a.grid.empty() || m.pF.size() != a.grid.size())
        return r;

    const auto bandOf = [] (double hz)
    {
        for (int b = 0; b < numBands; ++b)
            if (hz >= PlaybackSimulator::bandLimits[b] && hz < PlaybackSimulator::bandLimits[b + 1])
                return b;

        return -1;
    };

    if (m.cLC.size() != a.grid.size() || room.p.size() != (size_t) (room.seats + room.probes + room.gridPoints) * points.size())
        return r;

    // The mix's cross-spectral matrix (L, C, R, LFE) at each band point, the
    // LFE crosses with what chance alone could produce taken out, as in the
    // playback simulation
    constexpr int nc = RoomPaths::numChannels;
    std::vector<RoomPaths::ChannelMatrix> mix (points.size());

    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto g = (size_t) points[i];
        const double sigma = m.sigma[g];
        const double full = std::max (std::min (3.0 * sigma, 0.95), 2.0 * sigma + 1e-9);
        const double powers[3] = { m.pL[g], m.pC[g], m.pR[g] };
        const cd withLfe[3] = { m.cLLfe[g], m.cCLfe[g], m.cRLfe[g] };
        auto& x = mix[i];

        x[0] = powers[0];
        x[5] = powers[1];
        x[10] = powers[2];
        x[15] = gain * gain * m.pLfe[g];
        x[1] = m.cLC[g];    x[4] = std::conj (m.cLC[g]);
        x[2] = m.cLR[g];    x[8] = std::conj (m.cLR[g]);
        x[6] = m.cCR[g];    x[9] = std::conj (m.cCR[g]);

        for (int c = 0; c < 3; ++c)
        {
            const double coherence = std::abs (withLfe[c]) / (std::sqrt (powers[c] * m.pLfe[g]) + 1e-60);
            const cd cross = gain * withLfe[c] * std::clamp ((coherence - 2.0 * sigma) / (full - 2.0 * sigma), 0.0, 1.0);
            x[(size_t) (c * nc + 3)] = cross;
            x[(size_t) (3 * nc + c)] = std::conj (cross);
        }
    }

    // Power of the mix through a set of paths: sum over a, b of P_ab x S_ab (screen channels only, or all four)
    const auto played = [] (const RoomPaths::ChannelMatrix& paths, const RoomPaths::ChannelMatrix& x, bool screenOnly)
    {
        const int n = screenOnly ? 3 : nc;
        double sum = 0.0;

        for (int a = 0; a < n; ++a)
            for (int b = 0; b < n; ++b)
                sum += (paths[(size_t) (a * nc + b)] * x[(size_t) (a * nc + b)]).real();

        return sum;
    };

    RoomPaths::ChannelMatrix flat;
    flat.fill (cd (1.0, 0.0));

    // The mix as approved, per band: as heard (all crosses), with the LFE added in
    // power (no cross between the LFE and L C R), and its two parts on their own
    // (L C R with their own crosses, the LFE)
    double approved[numBands] = {}, approvedPower[numBands] = {}, approvedLcr[numBands] = {}, approvedLfe[numBands] = {};
    constexpr int numLayers = RoomResult::numLayers;
    double approvedLayer[numLayers] = {}, approvedLayerPower[numLayers] = {}, approvedLcrLayer[numLayers] = {}, approvedLfeLayer[numLayers] = {};

    const auto layerOf = [] (double hz)
    {
        for (int l = 0; l < numLayers; ++l)
            if (hz >= RoomResult::layerEdgesHz[l] && hz < RoomResult::layerEdgesHz[l + 1])
                return l;

        return -1;
    };

    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto g = (size_t) points[i];
        const int b = bandOf (a.grid[g]);
        const int layer = layerOf (a.grid[g]);
        const double hz = a.grid[g];
        const double lcr = played (flat, mix[i], true), lfe = mix[i][15].real();
        const double all = std::max (0.0, played (flat, mix[i], false));

        if (layer >= 0)
        {
            approvedLayerPower[layer] += hz * (lcr + lfe);
            approvedLayer[layer] += hz * all;
            approvedLcrLayer[layer] += hz * lcr;
            approvedLfeLayer[layer] += hz * lfe;
        }

        if (b < 0)
            continue;

        approvedPower[b] += hz * (lcr + lfe);
        approved[b] += hz * all;
        approvedLcr[b] += hz * lcr;
        approvedLfe[b] += hz * lfe;
    }

    double approvedTotal = 0.0, approvedPowerTotal = 0.0;

    for (int b = 0; b < numBands; ++b)
    {
        approvedTotal += approved[b];
        approvedPowerTotal += approvedPower[b];
    }

    // Sound pressure level at a seat of a band whose power (same units as approved[b]) is `power`
    const auto splAt = [&] (int b, double power)
    {
        return t.bandSplDb[(size_t) b] + 10.0 * std::log10 ((power + 1e-60) / (approved[b] + 1e-60));
    };

    const auto audible = [&] (int b, double spl)
    {
        return t.audibleAt (b, spl - t.bandSplDb[(size_t) b], tuning.thresholdSpreadDb, tuning.audibility);
    };

    // dB that double the loudness of a band at the level it has at that seat (ISO 226)
    const auto doublingAt = [] (int b, double spl)
    {
        const double hz = PlaybackSimulator::bandCentreHz[b];
        const double phon = std::clamp (PlaybackSimulator::iso226Phon (hz, spl), 20.0, 80.0);
        return PlaybackSimulator::iso226Spl (hz, phon + 10.0) - PlaybackSimulator::iso226Spl (hz, phon);
    };

    r.seatChangeDb.assign ((size_t) room.seats, 0.0);
    r.seatPenalty.assign ((size_t) room.seats, 0.0);
    r.probeChangeDb.assign ((size_t) room.probes, 0.0);
    r.probePenalty.assign ((size_t) room.probes, 0.0);
    const double nothing = std::numeric_limits<double>::quiet_NaN();
    constexpr int numParts = RoomResult::numParts, numCauses = RoomResult::numCauses, numRatings = RoomResult::numRatings;

    for (auto& v : r.seatLayerDb)   v.assign ((size_t) room.seats, nothing);
    for (auto& v : r.gridLayerDb)   v.assign ((size_t) room.gridPoints, nothing);

    for (auto& cause : r.seatCauseLayerDb)      for (auto& v : cause) v.assign ((size_t) room.seats, nothing);
    for (auto& cause : r.gridCauseLayerDb)      for (auto& v : cause) v.assign ((size_t) room.gridPoints, nothing);
    for (auto& v : r.seatCauseDb)               v.assign ((size_t) room.seats, nothing);
    for (auto& v : r.seatPartDb)                v.assign ((size_t) room.seats, nothing);
    for (auto& v : r.seatPhon)                  v.assign ((size_t) room.seats, 0.0);
    for (auto& part : r.seatLayerPhon)          for (auto& v : part) v.assign ((size_t) room.seats, 0.0);
    for (auto& part : r.gridLayerPhon)          for (auto& v : part) v.assign ((size_t) room.gridPoints, 0.0);
    for (auto& v : r.seatRatingPenalty)         v.assign ((size_t) room.seats, 0.0);
    for (auto& v : r.probeRatingPenalty)        v.assign ((size_t) room.probes, 0.0);
    r.seatHeard.assign ((size_t) room.seats, 0.0);

    r.seatBassDb.assign ((size_t) room.seats, 0.0);
    r.seatLevelDb.clear();
    r.seatHeardPhon.assign ((size_t) room.seats, 0.0);
    r.seatFeltDb.assign ((size_t) room.seats, 0.0);

    // Sound pressure level of any power in the same units (the grid being
    // logarithmic, hz-weighted sums are proportional to the power over the band)
    int levelBand = -1;

    for (int b = 0; b < numBands; ++b)
        if (approved[b] > 0.0 && (levelBand < 0 || approved[b] > approved[levelBand]))
            levelBand = b;

    const auto splOfPower = [&] (double power)
    {
        return levelBand < 0 ? -100.0 : t.bandSplDb[(size_t) levelBand] + 10.0 * std::log10 ((power + 1e-60) / (approved[levelBand] + 1e-60));
    };

    // Loudness level (ISO 226:2023), 0 under the threshold of hearing
    const auto phonOf = [] (int b, double spl)
    {
        const double hz = PlaybackSimulator::bandCentreHz[b];
        return spl <= PlaybackSimulator::hearingThresholdDb (hz) ? 0.0 : std::max (0.0, PlaybackSimulator::iso226Phon (hz, spl));
    };

    const auto layerOfBand = [&] (int b) { return layerOf (PlaybackSimulator::bandCentreHz[b]); };

    // The infra of the mix: where it sits, and the threshold there (Moller & Pedersen 2004:
    // 97 dB at 10 Hz, rising 11.9 dB per octave towards lower frequencies)
    double infraThreshold = 97.0;
    {
        double centre = 0.0, weight = 0.0;

        for (size_t i = 0; i < points.size(); ++i)
        {
            const double hz = a.grid[(size_t) points[i]];

            if (hz < RoomResult::layerEdgesHz[1])
            {
                const double power = std::max (0.0, played (flat, mix[i], false));
                centre += hz * power * std::log (hz);
                weight += hz * power;
            }
        }

        const double hz = weight > 0.0 ? std::exp (centre / weight) : 14.0;
        infraThreshold = 97.0 - 11.9 * std::log2 (hz / 10.0);
    }

    const auto infraSensation = [&] (double power) { return std::max (0.0, splOfPower (power) - infraThreshold); };

    // Each seat (then the extra points, then the floor grid). The power heard is
    // split in three exact parts (sum over a, b of P_ab x S_ab):
    //   L C R: the three channels through every speaker that plays them, their
    //          crosses with each other included;
    //   LFE:   the LFE through every speaker that plays it;
    //   the cross between the LFE and L C R: 2 Re sum_c P_c,LFE S_c,LFE, zero
    //          when the mix's LFE shares nothing with L C R.
    // With every speaker alone added in power (A; at the stage, one speaker per
    // channel: A0 = sum_a S_aa), the level against the approved mix T / T0 is
    // cut in three causes whose product is exactly T / T0:
    //   room     = A / A0
    //   speakers = [(LCR + LFE) / A] / [(LCR0 + LFE0) / A0]
    //   mix      = [T / (LCR + LFE)] / [T0 / (LCR0 + LFE0)]
    const double partFloor = 1e-6 * (approvedPowerTotal + 1e-60);
    const bool hasAlone = room.pAlone.size() == room.p.size();

    double approvedAlone[numBands] = {}, approvedAloneLayer[numLayers] = {};

    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto g = (size_t) points[i];
        const double hz = a.grid[g], alone = mix[i][0].real() + mix[i][5].real() + mix[i][10].real() + mix[i][15].real();
        const int b = bandOf (hz), layer = layerOf (hz);

        if (layer >= 0)     approvedAloneLayer[layer] += hz * alone;
        if (b >= 0)         approvedAlone[b] += hz * alone;
    }

    // The mix as approved on the loudness scale, and how likely it is heard at all, layer by layer
    {
        const double* parts[numParts] = { approved, approvedLcr, approvedLfe };
        const double* layerParts[numParts] = { approvedLayer, approvedLcrLayer, approvedLfeLayer };

        for (int part = 0; part < numParts; ++part)
        {
            for (int b = 0; b < numBands; ++b)
            {
                if (parts[part][b] <= partFloor)
                    continue;

                const double phon = phonOf (b, splAt (b, parts[part][b]));
                r.approvedPhon[(size_t) part] = std::max (r.approvedPhon[(size_t) part], phon);

                if (layerOfBand (b) >= 0)
                    r.approvedLayerPhon[(size_t) part][(size_t) layerOfBand (b)] = std::max (r.approvedLayerPhon[(size_t) part][(size_t) layerOfBand (b)], phon);
            }

            if (layerParts[part][0] > partFloor)
                r.approvedLayerPhon[(size_t) part][0] = infraSensation (layerParts[part][0]);
        }

        for (int b = 0; b < numBands; ++b)
            if (approved[b] > partFloor && layerOfBand (b) >= 0)
                r.layerHeard[(size_t) layerOfBand (b)] = std::max (r.layerHeard[(size_t) layerOfBand (b)], audible (b, t.bandSplDb[(size_t) b]));

        if (room.lcrReference && approvedLayer[0] > partFloor)
            r.layerHeard[0] = tuning.audibility ? 0.5 * std::erfc (-((splOfPower (approvedLayer[0]) - infraThreshold)
                                                                      / std::max (tuning.thresholdSpreadDb, 0.1)) / std::sqrt (2.0))
                                                : 1.0;
    }

    const auto causesOf = [] (double heard, double lcrLfe, double alone, double approvedHeard, double approvedLcrLfe, double approvedAloneHere,
                              double* out)
    {
        out[RoomResult::roomCause] = limitedDb ((alone / (approvedAloneHere + 1e-60)));
        out[RoomResult::speakersCause] = limitedDb ((lcrLfe / (alone + 1e-60)) / (approvedLcrLfe / (approvedAloneHere + 1e-60) + 1e-30));
        out[RoomResult::mixCause] = limitedDb ((heard / (lcrLfe + 1e-60)) / (approvedHeard / (approvedLcrLfe + 1e-60) + 1e-30));
    };

    for (int seat = 0; seat < room.seats + room.probes + room.gridPoints; ++seat)
    {
        double actual[numBands] = {}, inPower[numBands] = {}, lcrBand[numBands] = {}, lfeBand[numBands] = {}, aloneBand[numBands] = {};
        double actualLayer[numLayers] = {}, inPowerLayer[numLayers] = {}, lcrLayer[numLayers] = {}, lfeLayer[numLayers] = {};
        double aloneLayer[numLayers] = {}, felt = 0.0;

        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto g = (size_t) points[i];
            const int b = bandOf (a.grid[g]);
            const int layer = layerOf (a.grid[g]);

            if (b < 0 && layer < 0)
                continue;

            const double hz = a.grid[g];
            const size_t at = (size_t) seat * points.size() + i;
            const auto& paths = room.p[at];
            const auto& x = mix[i];
            const double lcr = played (paths, x, true);
            const double lfe = paths[15].real() * x[15].real();
            const double here = std::max (0.0, played (paths, x, false));
            double alone = lcr + lfe;

            if (hasAlone)
            {
                const auto& q = room.pAlone[at];
                alone = std::max (0.0, played (q, x, true) + q[15].real() * x[15].real());
            }

            if (layer >= 0)
            {
                inPowerLayer[layer] += hz * (lcr + lfe);
                actualLayer[layer] += hz * here;
                lcrLayer[layer] += hz * lcr;
                lfeLayer[layer] += hz * lfe;
                aloneLayer[layer] += hz * alone;
            }

            if (hz < 40.0)
                felt += hz * here;

            if (b < 0)
                continue;

            inPower[b] += hz * (lcr + lfe);
            actual[b] += hz * here;
            lcrBand[b] += hz * lcr;
            lfeBand[b] += hz * lfe;
            aloneBand[b] += hz * alone;
        }

        // Each layer: the three causes (the mix one is the interference shown before), and the loudness of each part
        double causeDb[numCauses][numLayers], layerPhon[numParts][numLayers] = {};

        for (int l = 0; l < numLayers; ++l)
        {
            const bool inMix = approvedLayerPower[l] > partFloor && inPowerLayer[l] > 0.0 && aloneLayer[l] > 0.0;
            double c[numCauses] = { nothing, nothing, nothing };

            if (inMix)
                causesOf (actualLayer[l], inPowerLayer[l], aloneLayer[l], approvedLayer[l], approvedLayerPower[l], approvedAloneLayer[l], c);

            for (int k = 0; k < numCauses; ++k)
                causeDb[k][l] = c[k];
        }

        {
            const double* parts[numParts] = { actual, lcrBand, lfeBand };
            const double* approvedParts[numParts] = { approved, approvedLcr, approvedLfe };
            const double* layerParts[numParts] = { actualLayer, lcrLayer, lfeLayer };
            const double* approvedLayerParts[numParts] = { approvedLayer, approvedLcrLayer, approvedLfeLayer };

            for (int part = 0; part < numParts; ++part)
            {
                for (int b = 0; b < numBands; ++b)
                    if (approvedParts[part][b] > partFloor && layerOfBand (b) >= 0)
                        layerPhon[part][layerOfBand (b)] = std::max (layerPhon[part][layerOfBand (b)], phonOf (b, splAt (b, std::max (0.0, parts[part][b]))));

                if (approvedLayerParts[part][0] > partFloor)
                    layerPhon[part][0] = infraSensation (std::max (0.0, layerParts[part][0]));
            }
        }

        if (seat >= room.seats + room.probes)
        {
            const auto k = (size_t) (seat - room.seats - room.probes);

            for (int l = 0; l < numLayers; ++l)
            {
                r.gridLayerDb[(size_t) l][k] = causeDb[RoomResult::mixCause][l];

                for (int c = 0; c < numCauses; ++c)
                    r.gridCauseLayerDb[(size_t) c][(size_t) l][k] = causeDb[c][l];

                for (int part = 0; part < numParts; ++part)
                    r.gridLayerPhon[(size_t) part][(size_t) l][k] = layerPhon[part][l];
            }

            continue;
        }

        // Room Sim Pro: the infra (under 20 Hz) is rated too, with the threshold
        // proposed by Moller & Pedersen (2004) below 20 Hz. The dB that double the
        // loudness are taken from ISO 226 at 20 Hz, the lowest standardised: infra
        // is more compressed still, so this counts an infra change at least as
        // much as one at 20 Hz.
        double infraWeight = 0.0, infraHeard = 0.0, infraRated[numRatings] = {};

        if (room.lcrReference && approvedLayerPower[0] > partFloor && inPowerLayer[0] > 0.0 && aloneLayer[0] > 0.0)
        {
            const double approvedHere = inPowerLayer[0] * approvedLayer[0] / approvedLayerPower[0];
            const double spl = splOfPower (std::max (approvedHere, actualLayer[0]));
            infraHeard = tuning.audibility ? 0.5 * std::erfc (-((spl - infraThreshold) / std::max (tuning.thresholdSpreadDb, 0.1)) / std::sqrt (2.0)) : 1.0;

            const double doubling = PlaybackSimulator::iso226Spl (20.0, 30.0) - PlaybackSimulator::iso226Spl (20.0, 20.0);
            const double allowed = std::min (toleranceDb, doubling - 0.5);
            const auto rateDb = [&] (double db) { return std::clamp ((std::abs (db) - allowed) / (doubling - allowed), 0.0, 1.0); };
            infraRated[RoomResult::coverageRating] = rateDb (limitedDb (actualLayer[0] / (approvedLayer[0] + 1e-60)));
            infraRated[RoomResult::speakersRating] = rateDb (causeDb[RoomResult::speakersCause][0]);
            infraRated[RoomResult::mixRating] = rateDb (causeDb[RoomResult::mixCause][0]);
            infraWeight = approvedHere * infraHeard;
        }

        // Each band, against the approved mix: the whole level (coverage), the
        // interference between speakers and the one the mix makes; all judged the
        // same way, audibility and loudness at the level the bass has at this
        // seat, room peaks and dips included.
        double weights[numBands] = {}, rated[numRatings][numBands] = {}, weightTotal = 0.0, heardAtAll = 0.0;
        double totalActual = 0.0, totalPower = 0.0, totalLcr = 0.0, totalLfe = 0.0, totalAlone = 0.0;
        double approvedLcrTotal = 0.0, approvedLfeTotal = 0.0, approvedAloneTotal = 0.0;

        for (int b = 0; b < numBands; ++b)
        {
            totalActual += actual[b];
            totalPower += inPower[b];
            totalLcr += lcrBand[b];
            totalLfe += lfeBand[b];
            totalAlone += aloneBand[b];
            approvedLcrTotal += approvedLcr[b];
            approvedLfeTotal += approvedLfe[b];
            approvedAloneTotal += approvedAlone[b];

            if (inPower[b] <= 0.0 || approvedPower[b] <= 0.0 || aloneBand[b] <= 0.0)
                continue;

            double c[numCauses];
            causesOf (actual[b], inPower[b], aloneBand[b], approved[b], approvedPower[b], approvedAlone[b], c);
            const double approvedHere = inPower[b] * approved[b] / approvedPower[b];     // this seat, with the approved mix's interference
            const double spl = splAt (b, std::max (approvedHere, actual[b]));
            const double heard = audible (b, spl);
            const double doubling = doublingAt (b, spl);
            const double allowed = std::min (toleranceDb, doubling - 0.5);
            const auto rateDb = [&] (double db) { return std::clamp ((std::abs (db) - allowed) / (doubling - allowed), 0.0, 1.0); };

            rated[RoomResult::coverageRating][b] = rateDb (limitedDb (actual[b] / (approved[b] + 1e-60)));
            rated[RoomResult::speakersRating][b] = rateDb (c[RoomResult::speakersCause]);
            rated[RoomResult::mixRating][b] = rateDb (c[RoomResult::mixCause]);
            weights[b] = approvedHere * heard;
            weightTotal += weights[b];
            heardAtAll = std::max (heardAtAll, heard);
        }

        double penalties[numRatings] = {};
        weightTotal += infraWeight;
        heardAtAll = std::max (heardAtAll, infraHeard);

        for (int k = 0; k < numRatings; ++k)
        {
            for (int b = 0; b < numBands; ++b)
                penalties[k] += weights[b] / (weightTotal + 1e-60) * rated[k][b];

            penalties[k] = std::min (1.0, penalties[k] + infraWeight / (weightTotal + 1e-60) * infraRated[k]);
        }

        // Room Sim: the interference only. Room Sim Pro: the interference and the coverage, as two risks
        const double interference = penalties[RoomResult::mixRating], coverage = penalties[RoomResult::coverageRating];
        const double penalty = room.lcrReference ? 1.0 - (1.0 - interference) * (1.0 - std::clamp (tuning.coverageWeight, 0.0, 1.0) * coverage)
                                                 : interference;
        const double seatPenalty = std::min (penalty, 1.0) * heardAtAll;
        const double seatChange = totalPower > 0.0 ? limitedDb ((totalActual / totalPower) / (approvedTotal / (approvedPowerTotal + 1e-60) + 1e-30))
                                                   : 0.0;

        if (seat < room.seats)
        {
            const auto k = (size_t) seat;
            r.seatPenalty[k] = seatPenalty;
            r.seatChangeDb[k] = seatChange;
            r.seatHeard[k] = heardAtAll;

            for (int q = 0; q < numRatings; ++q)
                r.seatRatingPenalty[(size_t) q][k] = penalties[q] * heardAtAll;

            for (int l = 0; l < numLayers; ++l)
            {
                r.seatLayerDb[(size_t) l][k] = causeDb[RoomResult::mixCause][l];

                for (int c = 0; c < numCauses; ++c)
                    r.seatCauseLayerDb[(size_t) c][(size_t) l][k] = causeDb[c][l];

                for (int part = 0; part < numParts; ++part)
                    r.seatLayerPhon[(size_t) part][(size_t) l][k] = layerPhon[part][l];
            }

            if (totalPower > 0.0 && totalAlone > 0.0)
            {
                double c[numCauses];
                causesOf (totalActual, totalPower, totalAlone, approvedTotal, approvedPowerTotal, approvedAloneTotal, c);

                for (int q = 0; q < numCauses; ++q)
                    r.seatCauseDb[(size_t) q][k] = c[q];
            }

            // What reaches this head: level, loudness of its loudest band (for each part), level under 40 Hz
            const double* parts[numParts] = { actual, lcrBand, lfeBand };
            const double* approvedParts[numParts] = { approved, approvedLcr, approvedLfe };

            for (int part = 0; part < numParts; ++part)
                for (int b = 0; b < numBands; ++b)
                    if (approvedParts[part][b] > partFloor)
                        r.seatPhon[(size_t) part][k] = std::max (r.seatPhon[(size_t) part][k], phonOf (b, splAt (b, std::max (0.0, parts[part][b]))));

            double phon = -100.0;

            for (int b = 0; b < numBands; ++b)
                if (actual[b] > 0.0 && approved[b] > 0.0)
                    phon = std::max (phon, PlaybackSimulator::iso226Phon (PlaybackSimulator::bandCentreHz[b], splAt (b, actual[b])));

            r.seatBassDb[k] = splOfPower (totalActual);
            r.seatLevelDb.push_back (limitedDb (totalActual / (approvedTotal + 1e-60)));
            r.seatPartDb[RoomResult::allParts][k] = r.seatLevelDb.back();
            r.seatPartDb[RoomResult::lcrPart][k] = approvedLcrTotal > partFloor ? limitedDb (totalLcr / approvedLcrTotal) : nothing;
            r.seatPartDb[RoomResult::lfePart][k] = approvedLfeTotal > partFloor ? limitedDb (totalLfe / approvedLfeTotal) : nothing;
            r.seatHeardPhon[k] = phon;
            r.seatFeltDb[k] = splOfPower (felt);
        }
        else
        {
            const auto k = (size_t) (seat - room.seats);
            r.probePenalty[k] = seatPenalty;
            r.probeChangeDb[k] = seatChange;

            for (int q = 0; q < numRatings; ++q)
                r.probeRatingPenalty[(size_t) q][k] = penalties[q] * heardAtAll;
        }
    }

    if (r.seatChangeDb.empty())
        return r;

    // The score 10 % of the seats fall below, and the worst point found (extra points included)
    const auto worstOf = [] (const std::vector<double>& seatPenalties, const std::vector<double>& probePenalties,
                             double& p10, double& worstPoint, int& worstProbe)
    {
        auto sortedPenalties = seatPenalties;
        std::sort (sortedPenalties.begin(), sortedPenalties.end(), std::greater<double>());
        const size_t at = std::min (sortedPenalties.size() - 1, (size_t) std::floor (0.1 * (double) sortedPenalties.size()));
        p10 = 100.0 * (1.0 - sortedPenalties[at]);
        worstPoint = 100.0 * (1.0 - sortedPenalties.front());
        worstProbe = -1;

        for (size_t k = 0; k < probePenalties.size(); ++k)
            if (100.0 * (1.0 - probePenalties[k]) < worstPoint)
            {
                worstPoint = 100.0 * (1.0 - probePenalties[k]);
                worstProbe = (int) k;
            }
    };

    worstOf (r.seatPenalty, r.probePenalty, r.p10Score, r.worstPointScore, r.worstProbe);

    for (int q = 0; q < numRatings; ++q)
    {
        worstOf (r.seatRatingPenalty[(size_t) q], r.probeRatingPenalty[(size_t) q], r.ratingP10[(size_t) q],
                 r.ratingWorstPointScore[(size_t) q], r.ratingWorstProbe[(size_t) q]);
        r.ratingScore[(size_t) q] = roomScoreOf (r.seatRatingPenalty[(size_t) q], tuning);
    }

    auto sorted = r.seatChangeDb;
    std::sort (sorted.begin(), sorted.end());
    r.worstDb = std::abs (sorted.front()) > std::abs (sorted.back()) ? sorted.front() : sorted.back();
    r.medianDb = sorted.size() % 2 == 1 ? sorted[sorted.size() / 2] : 0.5 * (sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]);
    r.bestDb = *std::min_element (sorted.begin(), sorted.end(), [] (double x, double y) { return std::abs (x) < std::abs (y); });
    r.roomScore = roomScoreOf (r.seatPenalty, tuning);
    r.combinedScore = t.robustness * r.roomScore / 100.0;
    return r;
}

} // namespace subguard
