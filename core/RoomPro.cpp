#include "RoomPro.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>

namespace subguard
{

namespace
{
    using cd = std::complex<double>;
    using Point = std::array<double, 3>;
    constexpr double pi = 3.14159265358979323846;

    ProSpeaker at (double x, double y, double z)
    {
        ProSpeaker s;
        s.x = x;
        s.y = y;
        s.z = z;
        return s;
    }

    Point inside (const ProRoom& r, double x, double y, double z)
    {
        return { std::clamp (x, 0.05, r.length - 0.05), std::clamp (y, 0.05, r.width - 0.05), std::clamp (z, 0.05, r.height - 0.05) };
    }
}

ProRoom ProRoom::fromSpec (const RoomSpec& spec)
{
    ProRoom r;
    r.name = spec.name;
    r.length = spec.length;
    r.width = spec.width;
    r.height = spec.height;
    r.rt60 = spec.rt60;
    r.lcr = { at (0.6, spec.width * 0.15, spec.height * 0.4), at (0.6, spec.width * 0.5, spec.height * 0.4),
              at (0.6, spec.width * 0.85, spec.height * 0.4) };
    r.numSubs = 2;
    r.subs = { at (0.4, spec.width * 0.3, 0.4), at (0.4, spec.width * 0.7, 0.4), at (0.4, spec.width * 0.1, 0.4), at (0.4, spec.width * 0.9, 0.4) };
    r.fillsOn = false;
    r.numFills = 2;
    r.fills = { at (spec.length * 0.6, 0.3, spec.height * 0.6), at (spec.length * 0.6, spec.width - 0.3, spec.height * 0.6),
                at (spec.length * 0.85, 0.3, spec.height * 0.6), at (spec.length * 0.85, spec.width - 0.3, spec.height * 0.6) };

    for (auto& f : r.fills)
        f.delayMs = 1000.0 * f.x / RoomSimulator::speedOfSound;     // aligned with the screen at their own distance

    r.firstRow = spec.length * 0.35;
    r.lastRow = spec.length * 0.85;
    r.audienceWidth = 0.64;
    r.rows = 4;
    r.perRow = 5;
    r.earHeight = 1.2;
    r.rake = 0.0;
    r.calibration = 0;
    return r;
}

std::vector<Point> ProRoom::seatPositions() const
{
    std::vector<Point> seats;
    const int nRows = std::clamp (rows, 1, 12), nPer = std::clamp (perRow, 1, 12);

    for (int row = 0; row < nRows && (int) seats.size() < ProRoomModel::maxSeats; ++row)
    {
        const double u = nRows > 1 ? row / (double) (nRows - 1) : 0.0;
        const double x = firstRow + (lastRow - firstRow) * u;
        const double z = earHeight + rake * u;

        for (int col = 0; col < nPer && (int) seats.size() < ProRoomModel::maxSeats; ++col)
        {
            const double v = nPer > 1 ? col / (double) (nPer - 1) : 0.5;
            const double y = width * (0.5 - 0.5 * audienceWidth + audienceWidth * v);
            seats.push_back (inside (*this, x, y, z));
        }
    }

    return seats;
}

int ProRoom::referenceSeat() const
{
    // The seat nearest to two thirds of the length, on the centre line
    const auto seats = seatPositions();
    int best = 0;
    double bestDistance = 1e30;

    for (size_t i = 0; i < seats.size(); ++i)
    {
        const double d = std::pow (seats[i][0] - length * 2.0 / 3.0, 2.0) + std::pow (seats[i][1] - width * 0.5, 2.0);

        if (d < bestDistance)
        {
            bestDistance = d;
            best = (int) i;
        }
    }

    return best;
}

bool ProRoom::operator== (const ProRoom& o) const
{
    return name == o.name && counted == o.counted && length == o.length && width == o.width && height == o.height && rt60 == o.rt60
             && lcr == o.lcr && numSubs == o.numSubs && subs == o.subs && fillsOn == o.fillsOn && numFills == o.numFills
             && fills == o.fills && firstRow == o.firstRow && lastRow == o.lastRow && audienceWidth == o.audienceWidth
             && rows == o.rows && perRow == o.perRow && earHeight == o.earHeight && rake == o.rake && calibration == o.calibration;
}

ProRooms ProRooms::defaults()
{
    const auto specs = RoomSimulator::defaultRooms();
    ProRooms p;
    p.rooms[0] = ProRoom::fromSpec (specs[4]);      // small screening room
    p.rooms[1] = ProRoom::fromSpec (specs[6]);      // multiplex 200 seats
    p.rooms[2] = ProRoom::fromSpec (specs[11]);     // premium large format
    return p;
}

//==============================================================================
// Text format: a header line, then `room<i>.<field> = <value>` lines.
namespace
{
    std::string number (double v)
    {
        char text[40];
        std::snprintf (text, sizeof (text), "%.17g", v);
        return text;
    }

    void writeSpeaker (std::ostringstream& out, const std::string& key, const ProSpeaker& s)
    {
        out << key << ".enabled = " << (s.enabled ? 1 : 0) << "\n"
            << key << ".x = " << number (s.x) << "\n"
            << key << ".y = " << number (s.y) << "\n"
            << key << ".z = " << number (s.z) << "\n"
            << key << ".delayMs = " << number (s.delayMs) << "\n"
            << key << ".gainDb = " << number (s.gainDb) << "\n"
            << key << ".inverted = " << (s.inverted ? 1 : 0) << "\n"
            << key << ".feed = " << s.feed << "\n";
    }

    struct Reader
    {
        std::map<std::string, std::string> values;

        bool has (const std::string& key) const     { return values.count (key) > 0; }
        void get (const std::string& key, double& v) const      { if (has (key)) v = std::strtod (values.at (key).c_str(), nullptr); }
        void get (const std::string& key, int& v) const         { if (has (key)) v = std::atoi (values.at (key).c_str()); }
        void get (const std::string& key, bool& v) const        { if (has (key)) v = std::atoi (values.at (key).c_str()) != 0; }
        void get (const std::string& key, std::string& v) const { if (has (key)) v = values.at (key); }

        void get (const std::string& key, ProSpeaker& s) const
        {
            get (key + ".enabled", s.enabled);
            get (key + ".x", s.x);
            get (key + ".y", s.y);
            get (key + ".z", s.z);
            get (key + ".delayMs", s.delayMs);
            get (key + ".gainDb", s.gainDb);
            get (key + ".inverted", s.inverted);
            get (key + ".feed", s.feed);
        }
    };
}

std::string ProRooms::toText() const
{
    std::ostringstream out;
    out << "SubGUARD Room Sim Pro\n" << "version = " << formatVersion << "\n";

    for (int i = 0; i < count; ++i)
    {
        const auto& r = rooms[(size_t) i];
        const std::string k = "room" + std::to_string (i + 1) + ".";
        std::string name = r.name;
        std::replace (name.begin(), name.end(), '\n', ' ');

        out << k << "name = " << name << "\n"
            << k << "counted = " << (r.counted ? 1 : 0) << "\n"
            << k << "length = " << number (r.length) << "\n"
            << k << "width = " << number (r.width) << "\n"
            << k << "height = " << number (r.height) << "\n"
            << k << "rt60 = " << number (r.rt60) << "\n";

        const char* lcrNames[] = { "L", "C", "R" };

        for (int s = 0; s < 3; ++s)
            writeSpeaker (out, k + lcrNames[s], r.lcr[(size_t) s]);

        out << k << "numSubs = " << r.numSubs << "\n";

        for (int s = 0; s < ProRoom::maxSubs; ++s)
            writeSpeaker (out, k + "sub" + std::to_string (s + 1), r.subs[(size_t) s]);

        out << k << "fillsOn = " << (r.fillsOn ? 1 : 0) << "\n" << k << "numFills = " << r.numFills << "\n";

        for (int s = 0; s < ProRoom::maxFills; ++s)
            writeSpeaker (out, k + "fill" + std::to_string (s + 1), r.fills[(size_t) s]);

        out << k << "firstRow = " << number (r.firstRow) << "\n"
            << k << "lastRow = " << number (r.lastRow) << "\n"
            << k << "audienceWidth = " << number (r.audienceWidth) << "\n"
            << k << "rows = " << r.rows << "\n"
            << k << "perRow = " << r.perRow << "\n"
            << k << "earHeight = " << number (r.earHeight) << "\n"
            << k << "rake = " << number (r.rake) << "\n"
            << k << "calibration = " << r.calibration << "\n";
    }

    return out.str();
}

bool ProRooms::fromText (const std::string& text, ProRooms& out)
{
    std::istringstream in (text);
    std::string line;

    if (! std::getline (in, line) || line.rfind ("SubGUARD Room Sim Pro", 0) != 0)
        return false;

    Reader reader;

    while (std::getline (in, line))
    {
        const auto eq = line.find (" = ");

        if (eq != std::string::npos)
            reader.values[line.substr (0, eq)] = line.substr (eq + 3);
    }

    // Start from the defaults: a field missing from an older file keeps its default
    ProRooms result = defaults();

    for (int i = 0; i < count; ++i)
    {
        auto& r = result.rooms[(size_t) i];
        const std::string k = "room" + std::to_string (i + 1) + ".";

        reader.get (k + "name", r.name);
        reader.get (k + "counted", r.counted);
        reader.get (k + "length", r.length);
        reader.get (k + "width", r.width);
        reader.get (k + "height", r.height);
        reader.get (k + "rt60", r.rt60);

        const char* lcrNames[] = { "L", "C", "R" };

        for (int s = 0; s < 3; ++s)
            reader.get (k + lcrNames[s], r.lcr[(size_t) s]);

        reader.get (k + "numSubs", r.numSubs);

        for (int s = 0; s < ProRoom::maxSubs; ++s)
            reader.get (k + "sub" + std::to_string (s + 1), r.subs[(size_t) s]);

        reader.get (k + "fillsOn", r.fillsOn);
        reader.get (k + "numFills", r.numFills);

        for (int s = 0; s < ProRoom::maxFills; ++s)
            reader.get (k + "fill" + std::to_string (s + 1), r.fills[(size_t) s]);

        reader.get (k + "firstRow", r.firstRow);
        reader.get (k + "lastRow", r.lastRow);
        reader.get (k + "audienceWidth", r.audienceWidth);
        reader.get (k + "rows", r.rows);
        reader.get (k + "perRow", r.perRow);
        reader.get (k + "earHeight", r.earHeight);
        reader.get (k + "rake", r.rake);
        reader.get (k + "calibration", r.calibration);

        r.numSubs = std::clamp (r.numSubs, 1, ProRoom::maxSubs);
        r.numFills = std::clamp (r.numFills, 1, ProRoom::maxFills);
    }

    out = result;
    return true;
}

//==============================================================================
RoomPaths ProRoomModel::prepare (const ProRoom& r, const std::vector<double>& grid)
{
    RoomSpec spec;
    spec.name = r.name;
    spec.length = std::max (r.length, 2.0);
    spec.width = std::max (r.width, 2.0);
    spec.height = std::max (r.height, 2.0);
    spec.rt60 = std::max (r.rt60, 0.1);

    RoomPaths paths;
    paths.spec = spec;
    paths.points = RoomSimulator::bandPoints (grid);
    const auto seats = r.seatPositions();
    paths.seats = (int) seats.size();
    const auto modes = RoomSimulator::modesOf (spec, highestModeHz);

    // Every source with its path (0 screen, 1 LFE), position and complex gain
    struct Source { int path; Point pos; double delay, sign, level; };
    std::vector<Source> sources;

    const auto add = [&] (const ProSpeaker& s, int path)
    {
        if (! s.enabled)
            return;

        sources.push_back ({ path, inside (r, s.x, s.y, s.z), s.delayMs * 0.001, s.inverted ? -1.0 : 1.0, std::pow (10.0, s.gainDb / 20.0) });
    };

    for (const auto& s : r.lcr)
        add (s, 0);

    for (int i = 0; i < std::clamp (r.numSubs, 1, ProRoom::maxSubs); ++i)
        add (r.subs[(size_t) i], 1);

    if (r.fillsOn)
        for (int i = 0; i < std::clamp (r.numFills, 1, ProRoom::maxFills); ++i)
            add (r.fills[(size_t) i], r.fills[(size_t) i].feed == 1 ? 1 : 0);

    // Mode shapes at every source, once
    std::vector<std::vector<double>> atSource (sources.size(), std::vector<double> (modes.size()));

    for (size_t s = 0; s < sources.size(); ++s)
        for (size_t k = 0; k < modes.size(); ++k)
            atSource[s][k] = RoomSimulator::shape (spec, modes[k], sources[s].pos);

    const size_t n = paths.points.size();
    paths.screen.assign ((size_t) paths.seats * n, cd());
    paths.sub.assign ((size_t) paths.seats * n, cd());

    // The same paths with every speaker at its calibrated level: what the
    // calibration measures. Level offsets set on a speaker stay offsets.
    RoomPaths nominal = paths;
    std::vector<double> atSeat (modes.size());
    std::vector<cd> term (modes.size());

    for (int seat = 0; seat < paths.seats; ++seat)
    {
        for (size_t k = 0; k < modes.size(); ++k)
            atSeat[k] = RoomSimulator::shape (spec, modes[k], seats[(size_t) seat]) * modes[k].scale;

        for (size_t i = 0; i < n; ++i)
        {
            const double w = 2.0 * pi * grid[(size_t) paths.points[i]];

            for (size_t k = 0; k < modes.size(); ++k)
                term[k] = atSeat[k] / cd (modes[k].omega2 - w * w, 2.0 * modes[k].damping * w);

            for (size_t s = 0; s < sources.size(); ++s)
            {
                cd h;

                for (size_t k = 0; k < modes.size(); ++k)
                    h += atSource[s][k] * term[k];

                h *= sources[s].sign * std::polar (1.0, -w * sources[s].delay);
                (sources[s].path == 0 ? nominal.screen : nominal.sub)[(size_t) seat * n + i] += h;
                (sources[s].path == 0 ? paths.screen : paths.sub)[(size_t) seat * n + i] += h * sources[s].level;
            }
        }
    }

    // Calibration on the room as set up normally (every speaker at its level):
    // the average of all seats, or the reference seat. The scale it finds is
    // then applied to the paths with the level offsets.
    std::vector<int> calibrationSeats;

    if (r.calibration == 1)
        calibrationSeats.push_back (r.referenceSeat());

    auto calibrated = nominal;
    RoomSimulator::calibrate (calibrated, grid, calibrationSeats);

    const auto scaleOf = [] (const std::vector<cd>& before, const std::vector<cd>& after)
    {
        for (size_t i = 0; i < before.size(); ++i)
            if (std::abs (before[i]) > 0.0)
                return std::abs (after[i]) / std::abs (before[i]);

        return 1.0;
    };

    const double screenScale = scaleOf (nominal.screen, calibrated.screen), subScale = scaleOf (nominal.sub, calibrated.sub);

    for (auto& h : paths.screen)    h *= screenScale;
    for (auto& h : paths.sub)       h *= subScale;

    paths.screenNominal = calibrated.screen;
    paths.subNominal = calibrated.sub;

    return paths;
}

} // namespace subguard
