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

    ProSpeaker at (double fx, double fy, double fz)
    {
        ProSpeaker s;
        s.fx = fx;
        s.fy = fy;
        s.fz = fz;
        return s;
    }

    ProSpeaker mirrored (ProSpeaker s)
    {
        s.fy = 1.0 - s.fy;
        return s;
    }

    Point inside (const ProRoom& r, double x, double y, double z)
    {
        return r.keptInside ({ x, y, z });
    }

    constexpr double lcRcWeight = 1.7079457843841379;     // L + C at -3 dB: 1 + 10^(-3/20), the screen bass taken as one signal

    const char* surfaceKeys[ProRoom::numSurfaces] = { "front", "back", "left", "right", "floor", "ceiling" };
}

//==============================================================================
// Materials. Random-incidence absorption coefficients. 125 Hz: the lowest
// octave of the published tables (Everest & Pohlmann, Master Handbook of
// Acoustics, absorption table; Beranek for seated audiences). 63 Hz is rarely
// published: rigid surfaces and panels keep their 125 Hz value, porous
// materials are halved; the values marked "estimate" have no table behind
// them and are orders of magnitude (see docs/ALGORITHME.md).
const std::vector<ProMaterial>& ProRoom::materialList()
{
    static const std::vector<ProMaterial> list {
        { "concrete",     "Concrete, bare or painted",                 0.01,  0.01,  "Everest & Pohlmann" },
        { "brick",        "Brick, unglazed",                           0.03,  0.03,  "Everest & Pohlmann" },
        { "plaster",      "Plaster on brick or concrete",              0.013, 0.013, "Everest & Pohlmann" },
        { "stone",        "Marble, stone or glazed tile",              0.01,  0.01,  "Everest & Pohlmann" },
        { "blockPainted", "Concrete block, painted",                   0.10,  0.10,  "Everest & Pohlmann" },
        { "block",        "Concrete block, coarse",                    0.18,  0.36,  "Everest & Pohlmann; 63 Hz halved" },
        { "plasterboard", "Plasterboard on studs",                     0.29,  0.29,  "Everest & Pohlmann; 63 Hz kept" },
        { "woodPanel",    "Wood panelling on battens",                 0.28,  0.28,  "Everest & Pohlmann; 63 Hz kept" },
        { "glassHeavy",   "Glass, large heavy panes",                  0.18,  0.18,  "Everest & Pohlmann; 63 Hz kept" },
        { "glassWindow",  "Glass, ordinary window",                    0.35,  0.35,  "Everest & Pohlmann; 63 Hz kept" },
        { "woodFloor",    "Wood floor on joists",                      0.15,  0.15,  "Everest & Pohlmann; 63 Hz kept" },
        { "vinyl",        "Linoleum, vinyl or rubber on concrete",     0.02,  0.02,  "Everest & Pohlmann" },
        { "carpet",       "Carpet on concrete",                        0.02,  0.02,  "Everest & Pohlmann" },
        { "carpetPad",    "Carpet on foam underlay",                   0.04,  0.08,  "Everest & Pohlmann; 63 Hz halved" },
        { "curtain",      "Heavy velour curtain, draped",              0.07,  0.14,  "Everest & Pohlmann; 63 Hz halved" },
        { "ceilingTile",  "Suspended acoustic ceiling tile",           0.25,  0.50,  "estimate (typical 0.3 to 0.7 at 125 Hz)" },
        { "mineralWool",  "Mineral wool 100 mm on the wall",           0.15,  0.35,  "estimate (typical data)" },
        { "bassTraps",    "Bass traps (tuned membrane absorbers)",     0.60,  0.70,  "estimate (typical data)" },
        { "seated",       "Audience in upholstered seats",             0.30,  0.60,  "Beranek; 63 Hz halved" },
        { "standing",     "Standing audience (about 1 per m2)",        0.10,  0.20,  "estimate" },
        { "metal",        "Steel sheet cladding or roof",              0.20,  0.15,  "estimate (mass law)" },
        { "tent",         "Tent fabric (PVC membrane)",                0.80,  0.70,  "estimate (mass law)" },
        { "open",         "Open side (no wall)",                       1.00,  1.00,  "exact: nothing comes back" },
    };

    return list;
}

int ProRoom::materialIndex (const std::string& key)
{
    const auto& list = materialList();

    for (size_t i = 0; i < list.size(); ++i)
        if (key == list[i].key)
            return (int) i;

    return -1;
}

namespace
{
    std::array<int, ProRoom::numSurfaces> materialsOf (const char* front, const char* back, const char* left, const char* right,
                                                        const char* floor, const char* ceiling)
    {
        return { std::max (0, ProRoom::materialIndex (front)), std::max (0, ProRoom::materialIndex (back)),
                 std::max (0, ProRoom::materialIndex (left)), std::max (0, ProRoom::materialIndex (right)),
                 std::max (0, ProRoom::materialIndex (floor)), std::max (0, ProRoom::materialIndex (ceiling)) };
    }
}

ProRoom ProRoom::fromSpec (const RoomSpec& spec)
{
    ProRoom r;
    r.name = spec.name;
    r.length = spec.length;
    r.width = spec.width;
    r.height = spec.height;
    r.acoustics = 0;
    r.rt60 = spec.rt60;
    r.materials = materialsOf ("plasterboard", "mineralWool", "mineralWool", "mineralWool", "seated", "plasterboard");

    const double front = 0.6 / spec.length, foot = 0.4 / spec.length, low = 0.4 / spec.height;
    r.lcr = { at (front, 0.15, 0.4), at (front, 0.5, 0.4), at (front, 0.85, 0.4) };
    r.linkLR = true;
    r.numSubs = 2;
    r.subs = { at (foot, 0.3, low), at (foot, 0.7, low), at (foot, 0.1, low), at (foot, 0.9, low) };
    r.fillsOn = false;
    r.linkFills = true;
    r.numFills = 2;
    const double side = 0.3 / spec.width;
    r.fills = { at (0.6, side, 0.6), at (0.6, 1.0 - side, 0.6), at (0.85, side, 0.6), at (0.85, 1.0 - side, 0.6) };

    for (auto& f : r.fills)
        f.delayMs = 1000.0 * f.fx * spec.length / RoomSimulator::speedOfSound;     // aligned with the screen at their own distance

    r.firstRow = 0.35;
    r.lastRow = 0.85;
    r.audienceWidth = 0.64;
    r.rows = 4;
    r.perRow = 5;
    r.earHeight = 1.2;
    r.rake = 0.0;
    r.calibration = 0;
    return r;
}

std::vector<ProRoom> ProRoom::venues()
{
    struct Venue
    {
        const char* name;
        double length, width, height;
        std::array<int, numSurfaces> materials;
        double speakerHeight, lrSpread;     // m, share of the width
        int subs;
        double firstRow, lastRow, audienceWidth;
        int rows, perRow;
        double ear;
    };

    const Venue list[] = {
        { "Art gallery, white cube",          20.0, 12.0,  5.0, materialsOf ("plasterboard", "plasterboard", "plasterboard", "glassHeavy", "concrete", "plasterboard"),
          2.2, 0.5, 1, 0.30, 0.80, 0.60, 4, 5, 1.6 },
        { "Industrial hall, fashion show",    40.0, 20.0, 10.0, materialsOf ("brick", "brick", "glassWindow", "glassWindow", "concrete", "metal"),
          3.0, 0.6, 2, 0.20, 0.90, 0.40, 6, 4, 1.2 },
        { "Tent, marquee",                    30.0, 15.0,  6.0, materialsOf ("tent", "tent", "tent", "tent", "woodFloor", "tent"),
          2.5, 0.6, 2, 0.30, 0.85, 0.60, 4, 5, 1.2 },
        { "Stone church",                     40.0, 14.0, 15.0, materialsOf ("stone", "woodPanel", "glassHeavy", "glassHeavy", "woodFloor", "plaster"),
          3.0, 0.6, 2, 0.30, 0.85, 0.60, 5, 5, 1.2 },
        { "Living room",                       6.0,  4.5,  2.6, materialsOf ("plasterboard", "glassWindow", "plasterboard", "plasterboard", "woodFloor", "plaster"),
          1.0, 0.5, 1, 0.60, 0.60, 0.30, 1, 3, 1.1 },
        { "Studio control room, treated",      7.0,  5.5,  3.0, materialsOf ("plasterboard", "bassTraps", "mineralWool", "mineralWool", "woodFloor", "mineralWool"),
          1.2, 0.45, 1, 0.55, 0.55, 0.20, 1, 3, 1.2 },
    };

    std::vector<ProRoom> rooms;

    for (const auto& v : list)
    {
        ProRoom r = fromSpec ({ v.name, v.name, v.length, v.width, v.height, 0.6 });
        r.acoustics = 1;
        r.materials = v.materials;
        r.rt60 = r.eyringRt60 (63.0);
        const double front = std::min (0.6, 0.1 * v.length) / v.length, z = std::min (v.speakerHeight, v.height - 0.3) / v.height;
        r.lcr = { at (front, 0.5 - 0.5 * v.lrSpread, z), at (front, 0.5, z), at (front, 0.5 + 0.5 * v.lrSpread, z) };
        r.numSubs = v.subs;

        if (v.subs == 1)
            r.subs[0] = at (std::min (0.4, 0.1 * v.length) / v.length, 0.5, 0.4 / v.height);

        r.firstRow = v.firstRow;
        r.lastRow = v.lastRow;
        r.audienceWidth = v.audienceWidth;
        r.rows = v.rows;
        r.perRow = v.perRow;
        r.earHeight = v.ear;
        rooms.push_back (r);
    }

    // The fashion show: the audience on both sides of the catwalk
    rooms[1].audienceLayout = sidesLayout;
    rooms[1].aisle = 0.2;
    rooms[1].audienceWidth = 0.8;
    rooms[1].perRow = 8;

    // In the round: two L C R sets back to back in the middle, the audience in rings around them
    {
        ProRoom r = fromSpec ({ "In the round, two L C R back to back", "In the round", 24.0, 24.0, 8.0, 0.6 });
        r.shape = round;
        r.acoustics = 1;
        r.materials = materialsOf ("plasterboard", "plasterboard", "plasterboard", "plasterboard", "concrete", "plasterboard");
        r.dualLCR = true;
        const double z = 2.5 / r.height;
        r.lcr = { at (0.47, 0.42, z), at (0.47, 0.5, z), at (0.47, 0.58, z) };
        r.numSubs = 2;
        r.subs[0] = at (0.5, 0.46, 0.4 / r.height);
        r.subs[1] = at (0.5, 0.54, 0.4 / r.height);
        r.audienceLayout = ringLayout;
        r.ringInner = 0.3;
        r.ringOuter = 0.85;
        r.rows = 4;
        r.perRow = 24;
        r.earHeight = 1.6;
        r.applyLinks();
        r.rt60 = r.eyringRt60 (63.0);
        rooms.push_back (r);
    }

    return rooms;
}

std::array<double, 3> ProRoom::positionOf (const ProSpeaker& s) const
{
    return { s.fx * length, s.fy * width, s.fz * height };
}

std::vector<Point> ProRoom::seatPositions() const
{
    std::vector<Point> seats;
    const int nRows = std::clamp (rows, 1, 12), nPer = std::clamp (perRow, 1, 24);
    const auto full = [&seats] { return (int) seats.size() >= ProRoomModel::maxSeats; };

    if (audienceLayout == ringLayout)
    {
        // Rings around the centre of the room; the outer ring has `perRow` seats, the inner ones fewer
        const double half = 0.5 * std::min (length, width);
        const double inner = half * std::clamp (std::min (ringInner, ringOuter), 0.0, 1.0);
        const double outer = half * std::clamp (std::max (ringInner, ringOuter), 0.0, 1.0);

        for (int ring = 0; ring < nRows && ! full(); ++ring)
        {
            const double u = nRows > 1 ? ring / (double) (nRows - 1) : 1.0;
            const double radius = inner + (outer - inner) * u;
            const int count = std::max (4, (int) std::lround (nPer * radius / std::max (outer, 0.01)));
            const double z = earHeight + rake * u;

            for (int k = 0; k < count && ! full(); ++k)
            {
                const double angle = 2.0 * pi * (k + 0.5 * (ring % 2)) / count;
                seats.push_back (inside (*this, 0.5 * length + radius * std::cos (angle), 0.5 * width + radius * std::sin (angle), z));
            }
        }

        return seats;
    }

    for (int row = 0; row < nRows && ! full(); ++row)
    {
        const double u = nRows > 1 ? row / (double) (nRows - 1) : 0.0;
        const double x = length * (firstRow + (lastRow - firstRow) * u);
        const double z = earHeight + rake * u;

        if (audienceLayout == sidesLayout)
        {
            // Two blocks, the aisle in the middle left free (a catwalk, a stage)
            const double gap = std::clamp (aisle, 0.0, std::max (0.0, audienceWidth - 0.02));
            const int left = (nPer + 1) / 2, right = nPer / 2;

            for (int side = 0; side < 2; ++side)
            {
                const int count = side == 0 ? left : right;

                for (int col = 0; col < count && ! full(); ++col)
                {
                    const double v = count > 1 ? col / (double) (count - 1) : 0.5;
                    const double share = 0.5 * gap + (0.5 * audienceWidth - 0.5 * gap) * (side == 0 ? 1.0 - v : v);
                    seats.push_back (inside (*this, x, width * (0.5 + (side == 0 ? -share : share)), z));
                }
            }

            continue;
        }

        for (int col = 0; col < nPer && ! full(); ++col)
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

void ProRoom::applyLinks()
{
    if (shape == round)
        length = width;

    if (linkLR)
        lcr[2] = mirrored (lcr[0]);

    if (linkFills)
    {
        numFills = numFills <= 2 ? 2 : 4;
        fills[1] = mirrored (fills[0]);
        fills[3] = mirrored (fills[2]);
    }
}

double ProRoom::volume() const
{
    return shape == round ? pi * 0.25 * width * width * height : length * width * height;
}

Point ProRoom::keptInside (Point p) const
{
    p[2] = std::clamp (p[2], 0.05, height - 0.05);

    if (shape == round)
    {
        const double radius = 0.5 * width, cx = 0.5 * width, cy = 0.5 * width;
        const double dx = p[0] - cx, dy = p[1] - cy, d = std::sqrt (dx * dx + dy * dy);

        if (d > radius - 0.05)
        {
            const double k = (radius - 0.05) / std::max (d, 1e-9);
            p[0] = cx + dx * k;
            p[1] = cy + dy * k;
        }

        return p;
    }

    p[0] = std::clamp (p[0], 0.05, length - 0.05);
    p[1] = std::clamp (p[1], 0.05, width - 0.05);
    return p;
}

std::vector<ProRoom::Placed> ProRoom::placedSpeakers() const
{
    std::vector<Placed> out;

    const auto add = [&] (const ProSpeaker& s, int path, double weight, bool mirror)
    {
        if (! s.enabled)
            return;

        auto p = positionOf (s);

        if (mirror)
            p = { length - p[0], width - p[1], p[2] };

        out.push_back ({ keptInside (p), path, s.delayMs, (s.inverted ? -1.0 : 1.0) * std::pow (10.0, s.gainDb / 20.0) * weight,
                         (s.inverted ? -1.0 : 1.0) * weight });
    };

    for (const auto& s : lcr)
        add (s, 0, 1.0, false);

    if (dualLCR)
        for (const auto& s : lcr)
            add (s, 0, 1.0, true);

    for (int i = 0; i < std::clamp (numSubs, 1, maxSubs); ++i)
        add (subs[(size_t) i], 1, 1.0, false);

    if (fillsOn)
        for (int i = 0; i < std::clamp (numFills, 1, maxFills); ++i)
        {
            const auto& f = fills[(size_t) i];
            add (f, f.feed == 1 ? 1 : 0, f.feed == 1 ? 1.0 : lcRcWeight, false);
        }

    return out;
}

//==============================================================================
// Acoustics
double ProRoom::surfaceArea (int surface) const
{
    if (shape == round)
    {
        if (surface == frontWall)                       return pi * width * height;
        if (surface == floor || surface == ceiling)     return pi * 0.25 * width * width;
        return 0.0;
    }

    if (surface == frontWall || surface == backWall)    return width * height;
    if (surface == leftWall || surface == rightWall)    return length * height;
    return length * width;
}

double ProRoom::absorption (int surface, double hz) const
{
    const auto& list = materialList();
    const auto& m = list[(size_t) std::clamp (materials[(size_t) surface], 0, (int) list.size() - 1)];
    const double t = std::clamp (std::log2 (std::max (hz, 1.0) / 63.0) / std::log2 (125.0 / 63.0), 0.0, 1.0);
    return m.alpha63 + t * (m.alpha125 - m.alpha63);
}

namespace
{
    // Absorption exponent of a surface: -ln(1 - alpha) (Eyring), alpha for small values; capped at alpha 0.95
    double exponentOf (double alpha)    { return -std::log (1.0 - std::clamp (alpha, 0.0, 0.95)); }
}

double ProRoom::eyringRt60 (double hz) const
{
    // Eyring: T = 24 ln10 V / (c S (-ln(1 - mean alpha)))
    double area = 0.0, absorbed = 0.0;

    for (int i = 0; i < numSurfaces; ++i)
    {
        area += surfaceArea (i);
        absorbed += surfaceArea (i) * std::clamp (absorption (i, hz), 0.0, 0.95);
    }

    return 24.0 * std::log (10.0) * volume() / (RoomSimulator::speedOfSound * area * exponentOf (absorbed / area));
}

double ProRoom::rt60At (double hz) const
{
    return acoustics == 1 ? eyringRt60 (hz) : std::clamp (rt60, 0.1, 10.0);
}

double ProRoom::schroederHz() const
{
    return 2000.0 * std::sqrt (rt60At (63.0) / volume());
}

double ProRoom::modeDamping (const RoomSimulator::Mode& mode) const
{
    if (acoustics != 1)
        return 3.0 * std::log (10.0) / std::clamp (rt60, 0.1, 10.0);     // 6.91 / T60, as Room Sim

    // Kuttruff (Room Acoustics, ch. 3), small wall admittance, alpha = 8 Re(beta):
    //   delta_n = c / (8 V) * sum over surfaces of w * S * alpha(f_n)
    // with w = 1 when the mode has a non-zero order across that surface
    // (it strikes it) and w = 1/2 when it runs along it. Oblique modes give
    // back Sabine's decay; axial modes decay only through their two walls,
    // more slowly. For large absorption alpha is replaced by -ln(1 - alpha).
    const double hz = std::sqrt (std::max (mode.omega2, 0.0)) / (2.0 * pi);
    const int order[numSurfaces] = { mode.nx, mode.nx, mode.ny, mode.ny, mode.nz, mode.nz };
    double sum = 0.0;

    for (int i = 0; i < numSurfaces; ++i)
        sum += (order[i] != 0 ? 1.0 : 0.5) * surfaceArea (i) * exponentOf (absorption (i, hz));

    return RoomSimulator::speedOfSound * sum / (8.0 * length * width * height);
}

bool ProRoom::operator== (const ProRoom& o) const
{
    return name == o.name && counted == o.counted && shape == o.shape && dualLCR == o.dualLCR && audienceLayout == o.audienceLayout
             && aisle == o.aisle && ringInner == o.ringInner && ringOuter == o.ringOuter && length == o.length && width == o.width && height == o.height
             && acoustics == o.acoustics && rt60 == o.rt60 && materials == o.materials
             && lcr == o.lcr && linkLR == o.linkLR && numSubs == o.numSubs && subs == o.subs && fillsOn == o.fillsOn
             && linkFills == o.linkFills && numFills == o.numFills
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
            << key << ".x = " << number (s.fx) << "\n"
            << key << ".y = " << number (s.fy) << "\n"
            << key << ".z = " << number (s.fz) << "\n"
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
            get (key + ".x", s.fx);
            get (key + ".y", s.fy);
            get (key + ".z", s.fz);
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
            << k << "shape = " << r.shape << "\n"
            << k << "length = " << number (r.length) << "\n"
            << k << "width = " << number (r.width) << "\n"
            << k << "height = " << number (r.height) << "\n"
            << k << "acoustics = " << r.acoustics << "\n"
            << k << "rt60 = " << number (r.rt60) << "\n";

        for (int m = 0; m < ProRoom::numSurfaces; ++m)
            out << k << "material." << surfaceKeys[m] << " = "
                << ProRoom::materialList()[(size_t) std::clamp (r.materials[(size_t) m], 0, (int) ProRoom::materialList().size() - 1)].key << "\n";

        out << k << "linkLR = " << (r.linkLR ? 1 : 0) << "\n"
            << k << "dualLCR = " << (r.dualLCR ? 1 : 0) << "\n";

        const char* lcrNames[] = { "L", "C", "R" };

        for (int s = 0; s < 3; ++s)
            writeSpeaker (out, k + lcrNames[s], r.lcr[(size_t) s]);

        out << k << "numSubs = " << r.numSubs << "\n";

        for (int s = 0; s < ProRoom::maxSubs; ++s)
            writeSpeaker (out, k + "sub" + std::to_string (s + 1), r.subs[(size_t) s]);

        out << k << "fillsOn = " << (r.fillsOn ? 1 : 0) << "\n" << k << "linkFills = " << (r.linkFills ? 1 : 0) << "\n"
            << k << "numFills = " << r.numFills << "\n";

        for (int s = 0; s < ProRoom::maxFills; ++s)
            writeSpeaker (out, k + "fill" + std::to_string (s + 1), r.fills[(size_t) s]);

        out << k << "audienceLayout = " << r.audienceLayout << "\n"
            << k << "aisle = " << number (r.aisle) << "\n"
            << k << "ringInner = " << number (r.ringInner) << "\n"
            << k << "ringOuter = " << number (r.ringOuter) << "\n"
            << k << "firstRow = " << number (r.firstRow) << "\n"
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
    int version = 1;
    reader.get ("version", version);

    for (int i = 0; i < count; ++i)
    {
        auto& r = result.rooms[(size_t) i];
        const std::string k = "room" + std::to_string (i + 1) + ".";

        reader.get (k + "name", r.name);
        reader.get (k + "counted", r.counted);
        reader.get (k + "shape", r.shape);
        reader.get (k + "length", r.length);
        reader.get (k + "width", r.width);
        reader.get (k + "height", r.height);
        reader.get (k + "acoustics", r.acoustics);
        reader.get (k + "rt60", r.rt60);

        for (int m = 0; m < ProRoom::numSurfaces; ++m)
        {
            std::string key;
            reader.get (k + "material." + surfaceKeys[m], key);

            if (ProRoom::materialIndex (key) >= 0)
                r.materials[(size_t) m] = ProRoom::materialIndex (key);
        }

        reader.get (k + "linkLR", r.linkLR);
        reader.get (k + "dualLCR", r.dualLCR);

        const char* lcrNames[] = { "L", "C", "R" };

        for (int s = 0; s < 3; ++s)
            reader.get (k + lcrNames[s], r.lcr[(size_t) s]);

        reader.get (k + "numSubs", r.numSubs);

        for (int s = 0; s < ProRoom::maxSubs; ++s)
            reader.get (k + "sub" + std::to_string (s + 1), r.subs[(size_t) s]);

        reader.get (k + "fillsOn", r.fillsOn);
        reader.get (k + "linkFills", r.linkFills);
        reader.get (k + "numFills", r.numFills);

        for (int s = 0; s < ProRoom::maxFills; ++s)
            reader.get (k + "fill" + std::to_string (s + 1), r.fills[(size_t) s]);

        reader.get (k + "audienceLayout", r.audienceLayout);
        reader.get (k + "aisle", r.aisle);
        reader.get (k + "ringInner", r.ringInner);
        reader.get (k + "ringOuter", r.ringOuter);
        reader.get (k + "firstRow", r.firstRow);
        reader.get (k + "lastRow", r.lastRow);
        reader.get (k + "audienceWidth", r.audienceWidth);
        reader.get (k + "rows", r.rows);
        reader.get (k + "perRow", r.perRow);
        reader.get (k + "earHeight", r.earHeight);
        reader.get (k + "rake", r.rake);
        reader.get (k + "calibration", r.calibration);

        if (version < 2)
        {
            // Version 1 kept positions in metres and had no links: convert, keep the layout as it was
            const auto toShare = [&r] (ProSpeaker& sp)
            {
                sp.fx /= std::max (r.length, 1.0);
                sp.fy /= std::max (r.width, 1.0);
                sp.fz /= std::max (r.height, 1.0);
            };

            for (auto& sp : r.lcr)      toShare (sp);
            for (auto& sp : r.subs)     toShare (sp);
            for (auto& sp : r.fills)    toShare (sp);

            r.firstRow /= std::max (r.length, 1.0);
            r.lastRow /= std::max (r.length, 1.0);
            r.linkLR = r.linkFills = false;
            r.acoustics = 0;
        }

        r.numSubs = std::clamp (r.numSubs, 1, ProRoom::maxSubs);
        r.numFills = std::clamp (r.numFills, 1, ProRoom::maxFills);
        r.acoustics = std::clamp (r.acoustics, 0, 1);
        r.shape = std::clamp (r.shape, 0, 1);
        r.audienceLayout = std::clamp (r.audienceLayout, 0, 2);
        r.rt60 = std::clamp (r.rt60, 0.1, 10.0);
        r.applyLinks();
    }

    out = result;
    return true;
}

//==============================================================================
//==============================================================================
// Modes
double ProRoomModel::besselJ (int m, double x)
{
    // J_m(x) = 1/(2 pi) * integral over a period of cos (m t - x sin t): the
    // trapezoidal rule on a periodic integrand converges exponentially once the
    // number of points exceeds the oscillations (Trefethen & Weideman 2014).
    const int n = 2 * (int) (std::abs (x) + std::abs (m)) + 48;
    double sum = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const double t = 2.0 * pi * i / n;
        sum += std::cos (m * t - x * std::sin (t));
    }

    return sum / n;
}

namespace
{
    double besselJDerivative (int m, double x)
    {
        return m == 0 ? -ProRoomModel::besselJ (1, x) : 0.5 * (ProRoomModel::besselJ (m - 1, x) - ProRoomModel::besselJ (m + 1, x));
    }

    // Zeros of J'_m up to `highest` (the zero at 0 of J'_0 left out)
    std::vector<double> derivativeZeros (int m, double highest)
    {
        std::vector<double> zeros;
        const double step = 0.05;
        // The first zero of J'_m lies beyond m: start just before it (near 0, J'_m is too small to trust its sign)
        double x0 = m == 0 ? 1e-3 : std::max (1e-3, m - 0.5), f0 = besselJDerivative (m, x0);

        for (double x1 = x0 + step; x1 <= highest + step; x1 += step)
        {
            const double f1 = besselJDerivative (m, x1);

            if ((f0 < 0.0) != (f1 < 0.0))
            {
                double lo = x0, hi = x1, flo = f0;

                for (int it = 0; it < 50; ++it)
                {
                    const double mid = 0.5 * (lo + hi), fm = besselJDerivative (m, mid);

                    if ((fm < 0.0) == (flo < 0.0))  { lo = mid; flo = fm; }
                    else                             { hi = mid; }
                }

                if (0.5 * (lo + hi) <= highest)
                    zeros.push_back (0.5 * (lo + hi));
            }

            x0 = x1;
            f0 = f1;
        }

        return zeros;
    }
}

double ProRoomModel::besselDerivativeZero (int m, int n)
{
    for (double highest = 10.0; highest < 400.0; highest *= 2.0)
    {
        const auto zeros = derivativeZeros (m, highest);

        if ((int) zeros.size() >= n)
            return zeros[(size_t) n - 1];
    }

    return 0.0;
}

std::vector<ProRoomModel::Mode> ProRoomModel::modesOf (const ProRoom& room, double highestHz)
{
    ProRoom r = room;
    r.applyLinks();
    const double c = RoomSimulator::speedOfSound;
    std::vector<Mode> modes;

    if (r.shape == ProRoom::rectangle)
    {
        const RoomSpec spec { "", "", r.length, r.width, r.height, r.rt60At (63.0) };

        for (const auto& m : RoomSimulator::modesOf (spec, highestHz))
        {
            Mode mode;
            mode.omega2 = m.omega2;
            mode.hz = std::sqrt (m.omega2) / (2.0 * pi);
            mode.damping = r.modeDamping (m);
            mode.scale = m.scale;
            mode.nx = m.nx;
            mode.ny = m.ny;
            mode.nz = m.nz;
            modes.push_back (mode);
        }

        return modes;
    }

    // Cylinder of radius a, rigid walls (Morse & Ingard, Theoretical Acoustics, 9.2):
    //   psi = J_m (k_r r) cos or sin (m theta) cos (nz pi z / H),  k_r = j'_mn / a
    // Each mode's decay from the surfaces it meets, as for the rectangle, the
    // wall weighed by how much of the mode lies against it.
    const double a = 0.5 * r.width, H = r.height, kmax = 2.0 * pi * highestHz / c;
    const double V = r.volume();
    const double wallArea = r.surfaceArea (ProRoom::frontWall), floorArea = r.surfaceArea (ProRoom::floor);

    for (int m = 0; m <= (int) (kmax * a) + 2; ++m)
    {
        auto roots = derivativeZeros (m, kmax * a);

        if (m == 0)
            roots.insert (roots.begin(), 0.0);

        for (double root : roots)
        {
            const double kr = root / a;
            const double radialMean = root == 0.0 ? 1.0 : (1.0 - (double) (m * m) / (root * root)) * std::pow (besselJ (m, root), 2.0);

            for (int nz = 0; ; ++nz)
            {
                const double kz = nz * pi / H, k2 = kr * kr + kz * kz;

                if (k2 > kmax * kmax)
                    break;

                Mode mode;
                mode.hz = c * std::sqrt (k2) / (2.0 * pi);
                mode.omega2 = std::pow (2.0 * pi * mode.hz, 2.0);
                mode.m = m;
                mode.radialK = kr;
                mode.nz = nz;
                mode.scale = 1.0 / (std::max (radialMean, 1e-9) * (m == 0 ? 1.0 : 0.5) * (nz == 0 ? 1.0 : 0.5));

                if (r.acoustics != 1)
                {
                    mode.damping = 3.0 * std::log (10.0) / std::clamp (r.rt60, 0.1, 10.0);
                }
                else
                {
                    // delta = c / (16 V) * sum of (mean of psi^2 on the surface / mean in the volume) * S * alpha'
                    const double wallRatio = root == 0.0 ? 1.0 : std::min (4.0, 1.0 / std::max (0.25, 1.0 - (double) (m * m) / (root * root)));
                    const double flatRatio = nz != 0 ? 2.0 : 1.0;
                    const double sum = wallRatio * wallArea * exponentOf (r.absorption (ProRoom::frontWall, mode.hz))
                                     + flatRatio * floorArea * exponentOf (r.absorption (ProRoom::floor, mode.hz))
                                     + flatRatio * floorArea * exponentOf (r.absorption (ProRoom::ceiling, mode.hz));
                    mode.damping = c * sum / (16.0 * V);
                }

                modes.push_back (mode);

                if (m > 0)
                {
                    mode.sine = true;
                    modes.push_back (mode);
                }
            }
        }
    }

    return modes;
}

double ProRoomModel::shape (const ProRoom& r, const Mode& mode, const Point& p)
{
    if (r.shape == ProRoom::rectangle)
        return std::cos (mode.nx * pi * p[0] / r.length) * std::cos (mode.ny * pi * p[1] / r.width) * std::cos (mode.nz * pi * p[2] / r.height);

    const double dx = p[0] - 0.5 * r.width, dy = p[1] - 0.5 * r.width;
    const double radius = std::sqrt (dx * dx + dy * dy), theta = std::atan2 (dy, dx);
    const double radial = mode.radialK == 0.0 ? 1.0 : besselJ (mode.m, mode.radialK * radius);
    const double angular = mode.m == 0 ? 1.0 : (mode.sine ? std::sin (mode.m * theta) : std::cos (mode.m * theta));
    return radial * angular * std::cos (mode.nz * pi * p[2] / r.height);
}

//==============================================================================
RoomPaths ProRoomModel::prepare (const ProRoom& room, const std::vector<double>& grid)
{
    ProRoom r = room;
    r.applyLinks();

    RoomSpec spec;
    spec.name = r.name;
    spec.length = r.length;
    spec.width = r.width;
    spec.height = r.height;
    spec.rt60 = r.rt60At (63.0);

    RoomPaths paths;
    paths.spec = spec;
    paths.lcrReference = true;
    paths.points = RoomSimulator::bandPoints (grid);
    const auto seats = r.seatPositions();
    paths.seats = (int) seats.size();
    const auto modes = modesOf (r, highestModeHz);
    const auto sources = r.placedSpeakers();

    // Mode shapes at every source and seat, once
    std::vector<std::vector<double>> atSource (sources.size(), std::vector<double> (modes.size()));

    for (size_t s = 0; s < sources.size(); ++s)
        for (size_t k = 0; k < modes.size(); ++k)
            atSource[s][k] = shape (r, modes[k], sources[s].position);

    // Each grid point stands for a band about 2.3 % wide. When the modes are
    // narrower than that (long decay), the response is averaged over several
    // frequencies inside the band instead of read at its centre.
    double narrowest = 1e9;

    for (const auto& m : modes)
        narrowest = std::min (narrowest, m.damping / pi);       // half-power bandwidth, Hz

    const size_t n = paths.points.size();
    paths.ss.assign ((size_t) paths.seats * n, 0.0);
    paths.ll = paths.ss;
    paths.sl.assign ((size_t) paths.seats * n, cd());
    RoomPaths nominal = paths;

    struct Sub { double w, weight; };
    std::vector<std::vector<Sub>> subs (n);

    for (size_t i = 0; i < n; ++i)
    {
        const auto g = (size_t) paths.points[i];
        const double lo = g > 0 ? std::sqrt (grid[g] * grid[g - 1]) : grid[g] / 1.0115;
        const double hi = g + 1 < grid.size() ? std::sqrt (grid[g] * grid[g + 1]) : grid[g] * 1.0115;
        const int count = std::clamp ((int) std::ceil (2.0 * (hi - lo) / std::max (narrowest, 1e-3)), 1, 8);

        for (int j = 0; j < count; ++j)
            subs[i].push_back ({ 2.0 * pi * lo * std::pow (hi / lo, (j + 0.5) / count), 1.0 / count });
    }

    // Mode shapes at every seat (with the mode's scale), once
    const size_t numModes = modes.size();
    std::vector<double> atSeats ((size_t) paths.seats * numModes);

    for (int seat = 0; seat < paths.seats; ++seat)
        for (size_t k = 0; k < numModes; ++k)
            atSeats[(size_t) seat * numModes + k] = shape (r, modes[k], seats[(size_t) seat]) * modes[k].scale;

    // For each frequency, the sources of a path are summed mode by mode first
    // (their delays and levels included), so that each seat then costs four
    // sums over the modes, whatever the number of speakers.
    std::vector<cd> screenAt (numModes), lfeAt (numModes), screenNominalAt (numModes), lfeNominalAt (numModes), term (numModes);

    for (size_t i = 0; i < n; ++i)
    {
        for (const auto& sub : subs[i])
        {
            const double w = sub.w;
            std::fill (screenAt.begin(), screenAt.end(), cd());
            std::fill (lfeAt.begin(), lfeAt.end(), cd());
            std::fill (screenNominalAt.begin(), screenNominalAt.end(), cd());
            std::fill (lfeNominalAt.begin(), lfeNominalAt.end(), cd());

            for (size_t s = 0; s < sources.size(); ++s)
            {
                const cd delay = std::polar (1.0, -w * sources[s].delayMs * 0.001);
                const cd gain = delay * sources[s].gain, nominalGain = delay * sources[s].nominalWeight;
                auto& actual = sources[s].path == 0 ? screenAt : lfeAt;
                auto& nominalSum = sources[s].path == 0 ? screenNominalAt : lfeNominalAt;

                for (size_t k = 0; k < numModes; ++k)
                {
                    actual[k] += atSource[s][k] * gain;
                    nominalSum[k] += atSource[s][k] * nominalGain;
                }
            }

            for (size_t k = 0; k < numModes; ++k)
                term[k] = 1.0 / cd (modes[k].omega2 - w * w, 2.0 * modes[k].damping * w);

            for (int seat = 0; seat < paths.seats; ++seat)
            {
                const double* shapes = &atSeats[(size_t) seat * numModes];
                cd screen, lfe, screenNominal, lfeNominal;

                for (size_t k = 0; k < numModes; ++k)
                {
                    const cd t = shapes[k] * term[k];
                    screen += screenAt[k] * t;
                    lfe += lfeAt[k] * t;
                    screenNominal += screenNominalAt[k] * t;
                    lfeNominal += lfeNominalAt[k] * t;
                }

                const size_t at = (size_t) seat * n + i;
                paths.accumulate (at, screen, lfe, sub.weight);
                nominal.accumulate (at, screenNominal, lfeNominal, sub.weight);
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

    const auto scaleOf = [] (const std::vector<double>& before, const std::vector<double>& after)
    {
        for (size_t i = 0; i < before.size(); ++i)
            if (before[i] > 0.0)
                return after[i] / before[i];

        return 1.0;
    };

    const double screenScale = scaleOf (nominal.ss, calibrated.ss), subScale = scaleOf (nominal.ll, calibrated.ll);

    for (auto& v : paths.ss)    v *= screenScale;
    for (auto& v : paths.ll)    v *= subScale;
    for (auto& v : paths.sl)    v *= std::sqrt (screenScale * subScale);

    paths.ssRef = calibrated.ss;
    paths.llRef = calibrated.ll;
    return paths;
}

} // namespace subguard
