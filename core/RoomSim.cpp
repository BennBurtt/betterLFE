#include "RoomSim.h"

#include <algorithm>
#include <cmath>
#include <functional>

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

void RoomPaths::accumulate (size_t at, cd screen, cd lfe, double weight)
{
    ss[at] += weight * std::norm (screen);
    ll[at] += weight * std::norm (lfe);
    sl[at] += weight * screen * std::conj (lfe);
}

std::vector<int> RoomSimulator::bandPoints (const std::vector<double>& grid)
{
    std::vector<int> points;

    for (size_t g = 0; g < grid.size(); ++g)
        if (grid[g] >= bandLoHz && grid[g] < bandHiHz)
            points.push_back ((int) g);

    return points;
}

void RoomSimulator::calibrate (RoomPaths& p, const std::vector<double>& grid, const std::vector<int>& calibrationSeats)
{
    // As in a real room: the screen and the subwoofers are each set so that
    // their level averaged over the calibration seats (20-120 Hz) equals the
    // level on the stage. Each seat then sits above or below by what the room
    // does there.
    double screenMean = 0.0, subMean = 0.0, weight = 0.0;
    const size_t n = p.points.size();

    for (int seat = 0; seat < p.seats; ++seat)
    {
        if (! calibrationSeats.empty() && std::find (calibrationSeats.begin(), calibrationSeats.end(), seat) == calibrationSeats.end())
            continue;

        for (size_t i = 0; i < n; ++i)
        {
            const double hz = grid[(size_t) p.points[i]];
            screenMean += hz * p.ss[(size_t) seat * n + i];
            subMean += hz * p.ll[(size_t) seat * n + i];
            weight += hz;
        }
    }

    const double screenScale = weight > 0.0 && screenMean > 0.0 ? std::sqrt (weight / screenMean) : 1.0;
    const double subScale = weight > 0.0 && subMean > 0.0 ? std::sqrt (weight / subMean) : 1.0;

    for (auto& v : p.ss)    v *= screenScale * screenScale;
    for (auto& v : p.ll)    v *= subScale * subScale;
    for (auto& v : p.sl)    v *= screenScale * subScale;
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

        // Source terms once per mode: the mean shape over the screen speakers and over the subwoofers
        std::vector<double> atScreen (modes.size()), atSub (modes.size());

        for (size_t k = 0; k < modes.size(); ++k)
        {
            for (const auto& s : screen)    atScreen[k] += shape (spec, modes[k], s) / (double) screen.size();
            for (const auto& s : subs)      atSub[k] += shape (spec, modes[k], s) / (double) subs.size();
        }

        p.ss.assign (seatPoints.size() * points.size(), 0.0);
        p.ll.assign (seatPoints.size() * points.size(), 0.0);
        p.sl.assign (seatPoints.size() * points.size(), cd());

        for (size_t seat = 0; seat < seatPoints.size(); ++seat)
        {
            std::vector<double> atSeat (modes.size());

            for (size_t k = 0; k < modes.size(); ++k)
                atSeat[k] = shape (spec, modes[k], seatPoints[seat]) * modes[k].scale;

            for (size_t i = 0; i < points.size(); ++i)
            {
                const double w = 2.0 * pi * grid[(size_t) points[i]];
                cd hs, hl;

                for (size_t k = 0; k < modes.size(); ++k)
                {
                    const cd term = atSeat[k] / cd (modes[k].omega2 - w * w, 2.0 * modes[k].damping * w);
                    hs += atScreen[k] * term;
                    hl += atSub[k] * term;
                }

                p.accumulate (seat * points.size() + i, hs, hl);
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

    // The cross term with what chance alone could produce taken out, as in the playback simulation
    std::vector<cd> cross (points.size());

    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto g = (size_t) points[i];
        const double sigma = m.sigma[g];
        const double coherence = std::abs (m.cFLfe[g]) / (std::sqrt (m.pF[g] * m.pLfe[g]) + 1e-60);
        const double full = std::max (std::min (3.0 * sigma, 0.95), 2.0 * sigma + 1e-9);
        cross[i] = m.cFLfe[g] * std::clamp ((coherence - 2.0 * sigma) / (full - 2.0 * sigma), 0.0, 1.0);
    }

    // The mix as approved, per band: as heard (in phase) and added in power
    double approved[numBands] = {}, approvedPower[numBands] = {};

    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto g = (size_t) points[i];
        const int b = bandOf (a.grid[g]);

        if (b < 0)
            continue;

        const double hz = a.grid[g];
        const double power = m.pF[g] + gain * gain * m.pLfe[g];
        approvedPower[b] += hz * power;
        approved[b] += hz * std::max (0.0, power + 2.0 * gain * cross[i].real());
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
        return tuning.audibility ? PlaybackSimulator::audibleShare (PlaybackSimulator::bandCentreHz[b], spl, tuning.thresholdSpreadDb) : 1.0;
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

    for (int seat = 0; seat < room.seats; ++seat)
    {
        double actual[numBands] = {}, inPower[numBands] = {};

        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto g = (size_t) points[i];
            const int b = bandOf (a.grid[g]);

            if (b < 0)
                continue;

            const double hz = a.grid[g];
            const size_t at = (size_t) seat * points.size() + i;
            const double ss = room.ss[at], ll = room.ll[at] * gain * gain;
            const double ssRef = room.ssRef.empty() ? ss : room.ssRef[at];
            const double llRef = room.llRef.empty() ? ll : room.llRef[at] * gain * gain;
            const double power = ss * m.pF[g] + ll * m.pLfe[g];

            // Pro: the whole mix through the screen path is the reference (LCR first)
            inPower[b] += hz * (room.lcrReference ? ssRef * (m.pF[g] + gain * gain * m.pLfe[g])
                                                  : ssRef * m.pF[g] + llRef * m.pLfe[g]);
            actual[b] += hz * std::max (0.0, power + 2.0 * gain * (room.sl[at] * cross[i]).real());
        }

        // What the interaction adds at this seat, against what it added on the
        // stage. Audibility and loudness are taken at the level the bass has
        // at this seat, room peaks and dips included: the louder the
        // listening level, the more of what the room does is heard.
        double weights[numBands] = {}, rated[numBands] = {}, weightTotal = 0.0, heardAtAll = 0.0;
        double totalActual = 0.0, totalPower = 0.0;

        for (int b = 0; b < numBands; ++b)
        {
            totalActual += actual[b];
            totalPower += inPower[b];

            if (inPower[b] <= 0.0 || approvedPower[b] <= 0.0)
                continue;

            const double approvedHere = inPower[b] * approved[b] / approvedPower[b];     // this seat, with the stage's interaction
            const double change = limitedDb ((actual[b] / inPower[b]) / (approved[b] / approvedPower[b] + 1e-30));
            const double spl = splAt (b, std::max (approvedHere, actual[b]));
            const double heard = audible (b, spl);
            const double doubling = doublingAt (b, spl);
            const double allowed = std::min (toleranceDb, doubling - 0.5);

            rated[b] = std::clamp ((std::abs (change) - allowed) / (doubling - allowed), 0.0, 1.0);
            weights[b] = approvedHere * heard;
            weightTotal += weights[b];
            heardAtAll = std::max (heardAtAll, heard);
        }

        double penalty = 0.0;

        for (int b = 0; b < numBands; ++b)
            penalty += weights[b] / (weightTotal + 1e-60) * rated[b];

        r.seatPenalty[(size_t) seat] = std::min (penalty, 1.0) * heardAtAll;
        r.seatChangeDb[(size_t) seat] = totalPower > 0.0 ? limitedDb ((totalActual / totalPower) / (approvedTotal / (approvedPowerTotal + 1e-60) + 1e-30))
                                                         : 0.0;
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
