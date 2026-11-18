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
        return { std::clamp (x, 0.05, r.length - 0.05), std::clamp (y, 0.05, r.width - 0.05), std::clamp (z, 0.05, r.height - 0.05) };
    }

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

    return rooms;
}

std::array<double, 3> ProRoom::positionOf (const ProSpeaker& s) const
{
    return { s.fx * length, s.fy * width, s.fz * height };
}

std::vector<Point> ProRoom::seatPositions() const
{
    std::vector<Point> seats;
    const int nRows = std::clamp (rows, 1, 12), nPer = std::clamp (perRow, 1, 12);

    for (int row = 0; row < nRows && (int) seats.size() < ProRoomModel::maxSeats; ++row)
    {
        const double u = nRows > 1 ? row / (double) (nRows - 1) : 0.0;
        const double x = length * (firstRow + (lastRow - firstRow) * u);
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

void ProRoom::applyLinks()
{
    if (linkLR)
        lcr[2] = mirrored (lcr[0]);

    if (linkFills)
    {
        numFills = numFills <= 2 ? 2 : 4;
        fills[1] = mirrored (fills[0]);
        fills[3] = mirrored (fills[2]);
    }
}

//==============================================================================
// Acoustics
double ProRoom::surfaceArea (int surface) const
{
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

    const double volume = length * width * height;
    return 24.0 * std::log (10.0) * volume / (RoomSimulator::speedOfSound * area * exponentOf (absorbed / area));
}

double ProRoom::rt60At (double hz) const
{
    return acoustics == 1 ? eyringRt60 (hz) : std::clamp (rt60, 0.1, 10.0);
}

double ProRoom::schroederHz() const
{
    return 2000.0 * std::sqrt (rt60At (63.0) / (length * width * height));
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
    return name == o.name && counted == o.counted && length == o.length && width == o.width && height == o.height
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
            << k << "length = " << number (r.length) << "\n"
            << k << "width = " << number (r.width) << "\n"
            << k << "height = " << number (r.height) << "\n"
            << k << "acoustics = " << r.acoustics << "\n"
            << k << "rt60 = " << number (r.rt60) << "\n";

        for (int m = 0; m < ProRoom::numSurfaces; ++m)
            out << k << "material." << surfaceKeys[m] << " = "
                << ProRoom::materialList()[(size_t) std::clamp (r.materials[(size_t) m], 0, (int) ProRoom::materialList().size() - 1)].key << "\n";

        out << k << "linkLR = " << (r.linkLR ? 1 : 0) << "\n";

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
    int version = 1;
    reader.get ("version", version);

    for (int i = 0; i < count; ++i)
    {
        auto& r = result.rooms[(size_t) i];
        const std::string k = "room" + std::to_string (i + 1) + ".";

        reader.get (k + "name", r.name);
        reader.get (k + "counted", r.counted);
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
        r.rt60 = std::clamp (r.rt60, 0.1, 10.0);
        r.applyLinks();
    }

    out = result;
    return true;
}

//==============================================================================
RoomPaths ProRoomModel::prepare (const ProRoom& room, const std::vector<double>& grid)
{
    ProRoom r = room;
    r.applyLinks();

    RoomSpec spec;
    spec.name = r.name;
    spec.length = std::max (r.length, 2.0);
    spec.width = std::max (r.width, 2.0);
    spec.height = std::max (r.height, 2.0);
    spec.rt60 = r.rt60At (63.0);

    RoomPaths paths;
    paths.spec = spec;
    paths.points = RoomSimulator::bandPoints (grid);
    const auto seats = r.seatPositions();
    paths.seats = (int) seats.size();
    auto modes = RoomSimulator::modesOf (spec, highestModeHz);

    for (auto& mode : modes)
        mode.damping = r.modeDamping (mode);

    // Every source with its path (0 screen, 1 LFE), position and complex gain
    struct Source { int path; Point pos; double delay, sign, level; };
    std::vector<Source> sources;

    const auto add = [&] (const ProSpeaker& s, int path)
    {
        if (! s.enabled)
            return;

        const auto p = r.positionOf (s);
        sources.push_back ({ path, inside (r, p[0], p[1], p[2]), s.delayMs * 0.001, s.inverted ? -1.0 : 1.0, std::pow (10.0, s.gainDb / 20.0) });
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
