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
    constexpr double minusThreeDb = 0.70794578438413791;     // 10^(-3/20)

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

    const char* surfaceKeys[ProRoom::numSurfaces] = { "front", "back", "left", "right", "floor", "ceiling" };

    // Absorption exponent of a surface: -ln(1 - alpha) (Eyring), alpha for small values; capped at alpha 0.95
    double exponentOf (double alpha)    { return -std::log (1.0 - std::clamp (alpha, 0.0, 0.95)); }
}

//==============================================================================
// Materials. Random-incidence absorption coefficients. 125 Hz: the lowest
// octave of the published tables (Everest & Pohlmann, Master Handbook of
// Acoustics, absorption table). 63 Hz is rarely published: rigid surfaces and
// panels keep their 125 Hz value, porous materials are halved; the values
// marked "estimate" have no table behind them and are orders of magnitude
// (see docs/ALGORITHME.md). Each material says where it can go.
const char* ProMaterial::groupName (int group)
{
    const char* names[] = { "Masonry and stone", "Boards and panels", "Glass", "Floor coverings", "Fabrics",
                            "Acoustic treatment", "Light structures", "Openings" };
    return names[std::clamp (group, 0, (int) numGroups - 1)];
}

const std::vector<ProMaterial>& ProRoom::materialList()
{
    using M = ProMaterial;
    constexpr int walls = M::wall, floors = M::floor, ceilings = M::ceiling;
    static const std::vector<ProMaterial> list {
        { "brick",        "Brick, unglazed",                         0.03,  0.03,  "Everest & Pohlmann",                       M::masonry,   walls },
        { "concrete",     "Concrete, bare or painted",               0.01,  0.01,  "Everest & Pohlmann",                       M::masonry,   walls | floors | ceilings },
        { "block",        "Concrete block, coarse",                  0.18,  0.36,  "Everest & Pohlmann; 63 Hz halved",         M::masonry,   walls },
        { "blockPainted", "Concrete block, painted",                 0.10,  0.10,  "Everest & Pohlmann",                       M::masonry,   walls },
        { "stone",        "Marble, stone or glazed tile",            0.01,  0.01,  "Everest & Pohlmann",                       M::masonry,   walls | floors },
        { "plaster",      "Plaster on brick or concrete",            0.013, 0.013, "Everest & Pohlmann",                       M::masonry,   walls | ceilings },
        { "plasterboard", "Plasterboard on studs",                   0.29,  0.29,  "Everest & Pohlmann; 63 Hz kept",           M::boards,    walls | ceilings },
        { "woodPanel",    "Wood panelling on battens",               0.28,  0.28,  "Everest & Pohlmann; 63 Hz kept",           M::boards,    walls | ceilings },
        { "glassHeavy",   "Glass, large heavy panes",                0.18,  0.18,  "Everest & Pohlmann; 63 Hz kept",           M::glass,     walls | ceilings },
        { "glassWindow",  "Glass, ordinary window",                  0.35,  0.35,  "Everest & Pohlmann; 63 Hz kept",           M::glass,     walls | ceilings },
        { "carpet",       "Carpet on concrete",                      0.02,  0.02,  "Everest & Pohlmann",                       M::floors,    floors },
        { "carpetPad",    "Carpet on foam underlay",                 0.04,  0.08,  "Everest & Pohlmann; 63 Hz halved",         M::floors,    floors },
        { "vinyl",        "Linoleum, vinyl or rubber on concrete",   0.02,  0.02,  "Everest & Pohlmann",                       M::floors,    floors },
        { "woodFloor",    "Wood floor on joists",                    0.15,  0.15,  "Everest & Pohlmann; 63 Hz kept",           M::floors,    floors },
        { "curtain",      "Heavy velour curtain, draped",            0.07,  0.14,  "Everest & Pohlmann; 63 Hz halved",         M::soft,      walls },
        { "bassTraps",    "Bass traps (tuned membrane absorbers)",   0.60,  0.70,  "estimate (typical data)",                  M::treatment, walls },
        { "mineralWool",  "Mineral wool 100 mm",                     0.15,  0.35,  "estimate (typical data)",                  M::treatment, walls | ceilings },
        { "ceilingTile",  "Suspended acoustic ceiling tile",         0.25,  0.50,  "estimate (typical 0.3 to 0.7 at 125 Hz)",  M::treatment, ceilings },
        { "metal",        "Steel sheet cladding or roof",            0.20,  0.15,  "estimate (mass law)",                      M::light,     walls | ceilings },
        { "tent",         "Tent fabric (PVC membrane)",              0.80,  0.70,  "estimate (mass law)",                      M::light,     walls | ceilings },
        { "open",         "Open side (no wall)",                     1.00,  1.00,  "approximation: rigid-box modes, alpha capped at 0.95",                M::open,      walls },
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

    // People on the audience area, absorption per square metre (on top of the floor they stand on)
    double peopleAbsorption (int people, double hz)
    {
        const double t = std::clamp (std::log2 (std::max (hz, 1.0) / 63.0) / std::log2 (125.0 / 63.0), 0.0, 1.0);

        if (people == ProRoom::seatedPeople)        return 0.30 + t * (0.60 - 0.30);     // Beranek, upholstered seats occupied; 63 Hz halved
        if (people == ProRoom::standingPeople)      return 0.10 + t * (0.20 - 0.10);     // estimate
        return 0.0;
    }
}

//==============================================================================
// Rooms
ProRoom ProRoom::fromSpec (const RoomSpec& spec)
{
    ProRoom r;
    r.name = spec.name;
    r.length = spec.length;
    r.width = spec.width;
    r.height = spec.height;
    r.acoustics = 0;
    r.rt60 = spec.rt60;
    r.materials = materialsOf ("plasterboard", "mineralWool", "mineralWool", "mineralWool", "carpet", "plasterboard");
    r.people = seatedPeople;

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

std::vector<ProVenue> ProRoom::venues()
{
    struct Def
    {
        const char* category;
        const char* name;
        int shape;
        double length, width, height;
        std::array<int, numSurfaces> materials;
        double speakerHeight, lrSpread;     // m, share of the width
        int subs;
        int layout;
        double firstRow, lastRow, audienceWidth, aisle;
        int rows, perRow;
        int people;
        double ear;
    };

    const Def list[] = {
        // Exhibitions
        { "Exhibitions", "Small gallery",                    rectangle, 12.0,  8.0,  3.5, materialsOf ("plasterboard", "plasterboard", "plasterboard", "plasterboard", "concrete", "plasterboard"),
          2.2, 0.5, 1, rowsLayout, 0.30, 0.85, 0.60, 0.0, 3, 4, standingPeople, 1.6 },
        { "Exhibitions", "Art gallery, white cube",          rectangle, 20.0, 12.0,  5.0, materialsOf ("plasterboard", "plasterboard", "plasterboard", "glassHeavy", "concrete", "plasterboard"),
          2.2, 0.5, 1, rowsLayout, 0.30, 0.80, 0.60, 0.0, 4, 5, standingPeople, 1.6 },
        { "Exhibitions", "Museum hall, stone",               rectangle, 30.0, 15.0,  9.0, materialsOf ("stone", "woodPanel", "plaster", "glassHeavy", "stone", "plasterboard"),
          2.5, 0.5, 2, rowsLayout, 0.25, 0.85, 0.60, 0.0, 4, 6, standingPeople, 1.6 },
        { "Exhibitions", "Exhibition hall",                  rectangle, 45.0, 30.0, 10.0, materialsOf ("blockPainted", "blockPainted", "blockPainted", "glassHeavy", "concrete", "metal"),
          3.0, 0.5, 2, rowsLayout, 0.20, 0.85, 0.60, 0.0, 5, 6, standingPeople, 1.6 },
        { "Exhibitions", "Convention centre hall",           rectangle, 60.0, 40.0, 14.0, materialsOf ("blockPainted", "blockPainted", "blockPainted", "blockPainted", "carpet", "metal"),
          4.0, 0.5, 4, rowsLayout, 0.15, 0.85, 0.70, 0.0, 6, 8, standingPeople, 1.6 },
        { "Exhibitions", "Oval rotunda",                     oval,      30.0, 20.0, 10.0, materialsOf ("plaster", "plaster", "plaster", "plaster", "stone", "glassHeavy"),
          2.5, 0.4, 2, ringLayout, 0.0, 0.0, 0.0, 0.0, 3, 16, standingPeople, 1.6 },
        { "Exhibitions", "L-shaped gallery",                 lShape,    24.0, 16.0,  5.0, materialsOf ("plasterboard", "plasterboard", "plasterboard", "plasterboard", "woodFloor", "plasterboard"),
          2.2, 0.4, 1, rowsLayout, 0.15, 0.45, 0.80, 0.0, 3, 6, standingPeople, 1.6 },

        // Fashion shows: the audience on both sides of the catwalk
        { "Fashion shows", "Showroom catwalk",               rectangle, 20.0, 10.0,  4.0, materialsOf ("glassWindow", "plasterboard", "plasterboard", "plasterboard", "woodFloor", "plasterboard"),
          2.5, 0.6, 1, sidesLayout, 0.15, 0.90, 0.80, 0.20, 6, 6, seatedPeople, 1.2 },
        { "Fashion shows", "Industrial hall, fashion show",  rectangle, 40.0, 20.0, 10.0, materialsOf ("brick", "brick", "glassWindow", "glassWindow", "concrete", "metal"),
          3.0, 0.6, 2, sidesLayout, 0.20, 0.90, 0.80, 0.20, 6, 8, seatedPeople, 1.2 },
        { "Fashion shows", "Hangar catwalk",                 rectangle, 60.0, 30.0, 15.0, materialsOf ("metal", "metal", "metal", "metal", "concrete", "metal"),
          4.0, 0.6, 4, sidesLayout, 0.15, 0.90, 0.70, 0.15, 8, 8, seatedPeople, 1.2 },
        { "Fashion shows", "Tent catwalk",                   rectangle, 40.0, 15.0,  6.0, materialsOf ("tent", "tent", "tent", "tent", "woodFloor", "tent"),
          3.0, 0.6, 2, sidesLayout, 0.15, 0.90, 0.80, 0.20, 6, 8, seatedPeople, 1.2 },
        { "Fashion shows", "Palace ballroom",                rectangle, 30.0, 15.0,  9.0, materialsOf ("plaster", "plaster", "curtain", "glassWindow", "woodFloor", "plaster"),
          3.0, 0.6, 2, sidesLayout, 0.15, 0.90, 0.80, 0.20, 6, 8, seatedPeople, 1.2 },

        // Shops
        { "Shops", "Small boutique",                         rectangle,  8.0,  5.0,  3.0, materialsOf ("glassWindow", "plasterboard", "woodPanel", "plasterboard", "woodFloor", "plasterboard"),
          2.3, 0.6, 1, rowsLayout, 0.30, 0.80, 0.60, 0.0, 2, 3, standingPeople, 1.6 },
        { "Shops", "Fashion store",                          rectangle, 20.0, 12.0,  4.0, materialsOf ("glassWindow", "plasterboard", "plasterboard", "plasterboard", "stone", "ceilingTile"),
          2.8, 0.6, 1, rowsLayout, 0.25, 0.85, 0.60, 0.0, 3, 5, standingPeople, 1.6 },
        { "Shops", "Flagship store",                         rectangle, 40.0, 25.0,  7.0, materialsOf ("glassHeavy", "plasterboard", "blockPainted", "blockPainted", "stone", "plasterboard"),
          3.5, 0.6, 2, rowsLayout, 0.20, 0.85, 0.60, 0.0, 4, 6, standingPeople, 1.6 },
        { "Shops", "Rounded-corner concept store",           roundedCorners, 25.0, 15.0, 5.0, materialsOf ("plasterboard", "plasterboard", "plasterboard", "plasterboard", "woodFloor", "plasterboard"),
          2.8, 0.6, 1, rowsLayout, 0.25, 0.85, 0.60, 0.0, 3, 5, standingPeople, 1.6 },
        { "Shops", "Shopping mall atrium",                   rectangle, 30.0, 20.0, 15.0, materialsOf ("glassHeavy", "glassHeavy", "plaster", "plaster", "stone", "glassHeavy"),
          4.0, 0.6, 2, rowsLayout, 0.25, 0.85, 0.60, 0.0, 4, 6, standingPeople, 1.6 },

        // Other venues
        { "Other venues", "Tent, marquee",                   rectangle, 30.0, 15.0,  6.0, materialsOf ("tent", "tent", "tent", "tent", "woodFloor", "tent"),
          2.5, 0.6, 2, rowsLayout, 0.30, 0.85, 0.60, 0.0, 4, 5, seatedPeople, 1.2 },
        { "Other venues", "Stone church",                    rectangle, 40.0, 14.0, 15.0, materialsOf ("stone", "woodPanel", "glassHeavy", "glassHeavy", "woodFloor", "plaster"),
          3.0, 0.6, 2, rowsLayout, 0.30, 0.85, 0.60, 0.0, 5, 5, seatedPeople, 1.2 },
        { "Other venues", "Living room",                     rectangle,  6.0,  4.5,  2.6, materialsOf ("plasterboard", "glassWindow", "plasterboard", "plasterboard", "woodFloor", "plaster"),
          1.0, 0.5, 1, rowsLayout, 0.60, 0.60, 0.30, 0.0, 1, 3, noPeople, 1.1 },
        { "Other venues", "Studio control room, treated",    rectangle,  7.0,  5.5,  3.0, materialsOf ("plasterboard", "bassTraps", "mineralWool", "mineralWool", "woodFloor", "mineralWool"),
          1.2, 0.45, 1, rowsLayout, 0.55, 0.55, 0.20, 0.0, 1, 3, noPeople, 1.2 },
    };

    std::vector<ProVenue> rooms;

    for (const auto& v : list)
    {
        ProRoom r = fromSpec ({ v.name, v.name, v.length, v.width, v.height, 0.6 });
        r.shape = v.shape;
        r.acoustics = 1;
        r.materials = v.materials;
        r.people = v.people;
        const double front = std::min (0.6, 0.1 * v.length) / v.length, z = std::min (v.speakerHeight, v.height - 0.3) / v.height;
        r.lcr = { at (front, 0.5 - 0.5 * v.lrSpread, z), at (front, 0.5, z), at (front, 0.5 + 0.5 * v.lrSpread, z) };
        r.numSubs = v.subs;

        if (v.subs == 1)
            r.subs[0] = at (std::min (0.4, 0.1 * v.length) / v.length, 0.5, 0.4 / v.height);

        if (v.shape == oval)
        {
            // Speakers at the front of an oval stand where the floor is
            for (auto& s : r.lcr)
                s.fx = 0.12;

            r.subs[0] = at (0.1, 0.42, 0.4 / v.height);
            r.subs[1] = at (0.1, 0.58, 0.4 / v.height);
        }

        r.audienceLayout = v.layout;
        r.firstRow = v.firstRow;
        r.lastRow = v.lastRow;
        r.audienceWidth = v.audienceWidth;
        r.aisle = v.aisle;
        r.rows = v.rows;
        r.perRow = v.perRow;
        r.earHeight = v.ear;
        r.applyLinks();
        r.rt60 = std::clamp (r.eyringRt60 (63.0), 0.1, 10.0);
        rooms.push_back ({ v.category, r });
    }

    // In the round: two L C R sets back to back in the middle, the audience in rings around them
    {
        ProRoom r = fromSpec ({ "In the round, two L C R back to back", "In the round", 24.0, 24.0, 8.0, 0.6 });
        r.shape = round;
        r.acoustics = 1;
        r.materials = materialsOf ("plasterboard", "plasterboard", "plasterboard", "plasterboard", "concrete", "plasterboard");
        r.people = standingPeople;
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
        r.rt60 = std::clamp (r.eyringRt60 (63.0), 0.1, 10.0);
        rooms.push_back ({ "Other venues", r });
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

    // A seat off the floor (oval, rounded corners, L shape, round) is left out
    const auto add = [&] (double x, double y, double z)
    {
        if (shape == rectangle || isInside (x, y))
            seats.push_back (keptInside ({ x, y, z }));
    };

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
                add (0.5 * length + radius * std::cos (angle), 0.5 * width + radius * std::sin (angle), z);
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
                    add (x, width * (0.5 + (side == 0 ? -share : share)), z);
                }
            }

            continue;
        }

        for (int col = 0; col < nPer && ! full(); ++col)
        {
            const double v = nPer > 1 ? col / (double) (nPer - 1) : 0.5;
            add (x, width * (0.5 - 0.5 * audienceWidth + audienceWidth * v), z);
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

//==============================================================================
// Geometry. The floor lies in x 0..length (from the front wall), y 0..width.
namespace
{
    double cornerRadius (const ProRoom& r)    { return 0.25 * std::min (r.length, r.width); }
}

bool ProRoom::floorInterval (double x, double& y0, double& y1) const
{
    const double L = length, W = width;

    if (x < 0.0 || x > L)
        return false;

    switch (shape)
    {
        case round:
        case oval:
        {
            const double a = 0.5 * L, b = 0.5 * W, u = (x - a) / a;

            if (std::abs (u) >= 1.0)
                return false;

            const double half = b * std::sqrt (1.0 - u * u);
            y0 = b - half;
            y1 = b + half;
            return true;
        }

        case roundedCorners:
        {
            const double rho = cornerRadius (*this);
            const double d = x < rho ? rho - x : (x > L - rho ? x - (L - rho) : 0.0);
            const double inset = d > 0.0 ? rho - std::sqrt (std::max (0.0, rho * rho - d * d)) : 0.0;
            y0 = inset;
            y1 = W - inset;
            return y1 > y0;
        }

        case lShape:
            // The back-right quarter is not part of the room
            y0 = 0.0;
            y1 = x > 0.5 * L ? 0.5 * W : W;
            return true;

        default:
            y0 = 0.0;
            y1 = W;
            return true;
    }
}

bool ProRoom::isInside (double x, double y) const
{
    double y0 = 0.0, y1 = 0.0;
    return floorInterval (x, y0, y1) && y >= y0 && y <= y1;
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
    double y0 = 0.0, y1 = width;

    // Towards the middle of the length until there is floor wide enough
    for (int step = 0; step < 200 && ! (floorInterval (p[0], y0, y1) && y1 - y0 > 0.1); ++step)
        p[0] += (0.5 * length - p[0]) * 0.05;

    p[1] = std::clamp (p[1], y0 + 0.05, y1 - 0.05);
    return p;
}

double ProRoom::floorArea() const
{
    switch (shape)
    {
        case round:             return pi * 0.25 * width * width;
        case oval:              return pi * 0.25 * length * width;
        case roundedCorners:    return length * width - (4.0 - pi) * std::pow (cornerRadius (*this), 2.0);
        case lShape:            return 0.75 * length * width;
        default:                return length * width;
    }
}

double ProRoom::wallArea() const
{
    double perimeter = 2.0 * (length + width);

    if (shape == round)
    {
        perimeter = pi * width;
    }
    else if (shape == oval)
    {
        const double a = 0.5 * length, b = 0.5 * width;     // Ramanujan
        perimeter = pi * (3.0 * (a + b) - std::sqrt ((3.0 * a + b) * (a + 3.0 * b)));
    }
    else if (shape == roundedCorners)
    {
        perimeter -= (8.0 - 2.0 * pi) * cornerRadius (*this);
    }

    return perimeter * height;
}

double ProRoom::volume() const
{
    return floorArea() * height;
}

std::vector<std::array<double, 2>> ProRoom::boundaryPoints (int count) const
{
    // A fine closed outline, then points evenly spaced along it
    std::vector<std::array<double, 2>> outline;
    const double L = length, W = width;

    if (shape == round || shape == oval)
    {
        for (int k = 0; k < 720; ++k)
        {
            const double a = 2.0 * pi * k / 720.0;
            outline.push_back ({ 0.5 * L + 0.5 * L * std::cos (a), 0.5 * W + 0.5 * W * std::sin (a) });
        }
    }
    else if (shape == roundedCorners)
    {
        const double rho = cornerRadius (*this);
        const std::array<std::array<double, 3>, 4> corners { { { L - rho, W - rho, 0.0 }, { rho, W - rho, 0.5 * pi },
                                                               { rho, rho, pi }, { L - rho, rho, 1.5 * pi } } };

        for (const auto& c : corners)
            for (int k = 0; k <= 32; ++k)
            {
                const double a = c[2] + 0.5 * pi * k / 32.0;
                outline.push_back ({ c[0] + rho * std::cos (a), c[1] + rho * std::sin (a) });
            }
    }
    else if (shape == lShape)
    {
        outline = { { 0.0, 0.0 }, { L, 0.0 }, { L, 0.5 * W }, { 0.5 * L, 0.5 * W }, { 0.5 * L, W }, { 0.0, W } };
    }
    else
    {
        outline = { { 0.0, 0.0 }, { L, 0.0 }, { L, W }, { 0.0, W } };
    }

    std::vector<double> cumulative { 0.0 };

    for (size_t i = 0; i < outline.size(); ++i)
    {
        const auto& a = outline[i];
        const auto& b = outline[(i + 1) % outline.size()];
        cumulative.push_back (cumulative.back() + std::hypot (b[0] - a[0], b[1] - a[1]));
    }

    std::vector<std::array<double, 2>> points;
    size_t segment = 0;

    for (int k = 0; k < count; ++k)
    {
        const double s = cumulative.back() * (k + 0.5) / count;

        while (segment + 1 < outline.size() && cumulative[segment + 1] < s)
            ++segment;

        const auto& a = outline[segment];
        const auto& b = outline[(segment + 1) % outline.size()];
        const double t = (s - cumulative[segment]) / std::max (cumulative[segment + 1] - cumulative[segment], 1e-12);
        points.push_back ({ a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]) });
    }

    return points;
}

double ProRoom::audienceArea() const
{
    if (people == noPeople)
        return 0.0;

    double area = 0.0;

    if (audienceLayout == ringLayout)
    {
        const double half = 0.5 * std::min (length, width);
        const double inner = half * std::clamp (std::min (ringInner, ringOuter), 0.0, 1.0);
        const double outer = half * std::clamp (std::max (ringInner, ringOuter), 0.0, 1.0) + 0.45;
        area = pi * (outer * outer - std::pow (std::max (0.0, inner - 0.45), 2.0));
    }
    else
    {
        const double depth = std::abs (lastRow - firstRow) * length + 0.9;
        double across = audienceWidth * width;

        if (audienceLayout == sidesLayout)
            across -= std::clamp (aisle, 0.0, audienceWidth) * width;

        area = depth * std::max (0.0, across);
    }

    return std::clamp (area, 0.0, floorArea());
}

//==============================================================================
// Speakers and routing
std::vector<ProRoom::Placed> ProRoom::placedSpeakers() const
{
    std::vector<Placed> out;

    const auto add = [&] (const ProSpeaker& s, int group, bool mirror)
    {
        if (! s.enabled)
            return;

        auto p = positionOf (s);

        if (mirror)
            p = { length - p[0], width - p[1], p[2] };

        const double sign = s.inverted ? -1.0 : 1.0;
        out.push_back ({ keptInside (p), group, s.delayMs, sign * std::pow (10.0, s.gainDb / 20.0), sign });
    };

    for (int i = 0; i < 3; ++i)
    {
        add (lcr[(size_t) i], groupL + i, false);

        if (dualLCR)
            add (lcr[(size_t) i], groupL + i, true);
    }

    for (int i = 0; i < std::clamp (numSubs, 1, maxSubs); ++i)
        add (subs[(size_t) i], groupSubs, false);

    if (fillsOn)
        for (int i = 0; i < std::clamp (numFills, 1, maxFills); ++i)
        {
            const auto& f = fills[(size_t) i];
            add (f, f.feed == 1 ? groupFillsLfe : (f.fy <= 0.5 ? groupFillsLeft : groupFillsRight), false);
        }

    return out;
}

std::array<std::array<double, ProRoom::numGroups>, 4> ProRoom::routing() const
{
    std::array<std::array<double, numGroups>, 4> r {};
    bool screenFills[2] = { false, false };

    if (fillsOn)
        for (int i = 0; i < std::clamp (numFills, 1, maxFills); ++i)
            if (fills[(size_t) i].enabled && fills[(size_t) i].feed == 0)
                screenFills[fills[(size_t) i].fy <= 0.5 ? 0 : 1] = true;

    const bool anyScreenFill = screenFills[0] || screenFills[1];
    const double c = std::clamp (centrePercent / 100.0, 0.0, 1.0);
    const double front = 1.0 - std::clamp (frontDivergence / 100.0, 0.0, 1.0);
    const double rear = 1.0 - std::clamp (rearDivergence / 100.0, 0.0, 1.0);
    const double toFills = anyScreenFill ? 1.0 - std::clamp (frDivergence / 100.0, 0.0, 1.0) : 0.0;

    for (int ch = 0; ch < 3; ++ch)
    {
        // Powers over L, C, R: own speaker, centre %, then front divergence towards an even spread
        double screen[3] = { ch == 0 ? 1.0 : 0.0, ch == 1 ? c : 0.0, ch == 2 ? 1.0 : 0.0 };

        if (ch == 1)
            screen[0] = screen[2] = 0.5 * (1.0 - c);

        for (double& p : screen)
            p = (1.0 - front) * p + front / 3.0;

        // Fills: Lc / Rc, then rear divergence between the two sides, plus what front / rear divergence sends
        const double side[2] = { ch == 0 ? 1.0 : (ch == 1 ? 0.5 : 0.0), ch == 2 ? 1.0 : (ch == 1 ? 0.5 : 0.0) };
        double fill[2] = { ch == 0 ? 1.0 : (ch == 1 ? minusThreeDb * minusThreeDb : 0.0),
                           ch == 2 ? 1.0 : (ch == 1 ? minusThreeDb * minusThreeDb : 0.0) };
        const double mixed[2] = { (1.0 - 0.5 * rear) * fill[0] + 0.5 * rear * fill[1], 0.5 * rear * fill[0] + (1.0 - 0.5 * rear) * fill[1] };
        fill[0] = mixed[0] + 0.5 * toFills * side[0];
        fill[1] = mixed[1] + 0.5 * toFills * side[1];

        for (int g = 0; g < 3; ++g)
            r[(size_t) ch][(size_t) g] = std::sqrt ((1.0 - 0.5 * toFills) * screen[g]);

        r[(size_t) ch][groupFillsLeft] = screenFills[0] ? std::sqrt (fill[0]) : 0.0;
        r[(size_t) ch][groupFillsRight] = screenFills[1] ? std::sqrt (fill[1]) : 0.0;
    }

    r[3][groupSubs] = 1.0;
    r[3][groupFillsLfe] = 1.0;
    return r;
}

//==============================================================================
// Acoustics
double ProRoom::surfaceArea (int surface) const
{
    if (shape != rectangle)
    {
        if (surface == frontWall)                       return wallArea();
        if (surface == floor || surface == ceiling)     return floorArea();
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
    double alpha = m.alpha63 + t * (m.alpha125 - m.alpha63);

    if (surface == floor && people != noPeople)
    {
        // The audience area takes the people's absorption on top of the floor's
        const double share = audienceArea() / std::max (floorArea(), 1e-9);
        alpha = std::min (1.0, alpha + share * peopleAbsorption (people, hz));
    }

    return alpha;
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
    return name == o.name && counted == o.counted && shape == o.shape && length == o.length && width == o.width && height == o.height
             && acoustics == o.acoustics && rt60 == o.rt60 && materials == o.materials
             && lcr == o.lcr && linkLR == o.linkLR && dualLCR == o.dualLCR && numSubs == o.numSubs && subs == o.subs
             && fillsOn == o.fillsOn && linkFills == o.linkFills && numFills == o.numFills && fills == o.fills
             && centrePercent == o.centrePercent && frontDivergence == o.frontDivergence && rearDivergence == o.rearDivergence
             && frDivergence == o.frDivergence
             && audienceLayout == o.audienceLayout && firstRow == o.firstRow && lastRow == o.lastRow && audienceWidth == o.audienceWidth
             && aisle == o.aisle && ringInner == o.ringInner && ringOuter == o.ringOuter
             && rows == o.rows && perRow == o.perRow && earHeight == o.earHeight && rake == o.rake && people == o.people
             && calibration == o.calibration;
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

        out << k << "centrePercent = " << number (r.centrePercent) << "\n"
            << k << "frontDivergence = " << number (r.frontDivergence) << "\n"
            << k << "rearDivergence = " << number (r.rearDivergence) << "\n"
            << k << "frDivergence = " << number (r.frDivergence) << "\n"
            << k << "audienceLayout = " << r.audienceLayout << "\n"
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
            << k << "people = " << r.people << "\n"
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

            // Version 3 had the audience as floor materials: now people on the floor
            if (key == "seated" || key == "standing")
            {
                r.people = key == "seated" ? ProRoom::seatedPeople : ProRoom::standingPeople;
                key = key == "seated" ? "carpet" : "concrete";
            }

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

        reader.get (k + "centrePercent", r.centrePercent);
        reader.get (k + "frontDivergence", r.frontDivergence);
        reader.get (k + "rearDivergence", r.rearDivergence);
        reader.get (k + "frDivergence", r.frDivergence);
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
        reader.get (k + "people", r.people);
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
        r.shape = std::clamp (r.shape, 0, (int) ProRoom::numShapes - 1);
        r.audienceLayout = std::clamp (r.audienceLayout, 0, 2);
        r.people = std::clamp (r.people, 0, 2);
        r.rt60 = std::clamp (r.rt60, 0.1, 10.0);
        r.applyLinks();
    }

    out = result;
    return true;
}

//==============================================================================
// Bessel functions (round rooms)
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

//==============================================================================
// Numerical floor modes (oval, rounded corners, L shape)
//
// Rayleigh-Ritz on the floor plan: the modes of the Neumann problem (rigid
// walls) make  integral grad u . grad v = k^2 integral u v  stationary over the
// floor, with no condition to impose at the wall (it is natural). The trial
// functions are the cosine modes of the bounding rectangle; the integrals are
// taken exactly across the room (each column of the floor is one interval)
// and by the midpoint rule along it. Then K c = k^2 M c, a symmetric-definite
// eigenproblem: Cholesky of M, Householder tridiagonalisation and implicit QL
// (EISPACK tred2 / tql2).
namespace
{
    // EISPACK tred2 / tql2 (as in JAMA): eigenvalues d and eigenvectors (columns of V) of a symmetric matrix held in V
    void symmetricEigen (int n, std::vector<double>& V, std::vector<double>& d)
    {
        std::vector<double> e ((size_t) n, 0.0);
        d.assign ((size_t) n, 0.0);
        const auto v = [&V, n] (int i, int j) -> double& { return V[(size_t) i * (size_t) n + (size_t) j]; };

        for (int j = 0; j < n; ++j)
            d[(size_t) j] = v (n - 1, j);

        for (int i = n - 1; i > 0; --i)
        {
            double scale = 0.0, h = 0.0;

            for (int k = 0; k < i; ++k)
                scale += std::abs (d[(size_t) k]);

            if (scale == 0.0)
            {
                e[(size_t) i] = d[(size_t) i - 1];

                for (int j = 0; j < i; ++j)
                {
                    d[(size_t) j] = v (i - 1, j);
                    v (i, j) = 0.0;
                    v (j, i) = 0.0;
                }
            }
            else
            {
                for (int k = 0; k < i; ++k)
                {
                    d[(size_t) k] /= scale;
                    h += d[(size_t) k] * d[(size_t) k];
                }

                double f = d[(size_t) i - 1];
                double g = std::sqrt (h);

                if (f > 0.0)
                    g = -g;

                e[(size_t) i] = scale * g;
                h -= f * g;
                d[(size_t) i - 1] = f - g;

                for (int j = 0; j < i; ++j)
                    e[(size_t) j] = 0.0;

                for (int j = 0; j < i; ++j)
                {
                    f = d[(size_t) j];
                    v (j, i) = f;
                    g = e[(size_t) j] + v (j, j) * f;

                    for (int k = j + 1; k <= i - 1; ++k)
                    {
                        g += v (k, j) * d[(size_t) k];
                        e[(size_t) k] += v (k, j) * f;
                    }

                    e[(size_t) j] = g;
                }

                f = 0.0;

                for (int j = 0; j < i; ++j)
                {
                    e[(size_t) j] /= h;
                    f += e[(size_t) j] * d[(size_t) j];
                }

                const double hh = f / (h + h);

                for (int j = 0; j < i; ++j)
                    e[(size_t) j] -= hh * d[(size_t) j];

                for (int j = 0; j < i; ++j)
                {
                    f = d[(size_t) j];
                    g = e[(size_t) j];

                    for (int k = j; k <= i - 1; ++k)
                        v (k, j) -= f * e[(size_t) k] + g * d[(size_t) k];

                    d[(size_t) j] = v (i - 1, j);
                    v (i, j) = 0.0;
                }
            }

            d[(size_t) i] = h;
        }

        for (int i = 0; i < n - 1; ++i)
        {
            v (n - 1, i) = v (i, i);
            v (i, i) = 1.0;
            const double h = d[(size_t) i + 1];

            if (h != 0.0)
            {
                for (int k = 0; k <= i; ++k)
                    d[(size_t) k] = v (k, i + 1) / h;

                for (int j = 0; j <= i; ++j)
                {
                    double g = 0.0;

                    for (int k = 0; k <= i; ++k)
                        g += v (k, i + 1) * v (k, j);

                    for (int k = 0; k <= i; ++k)
                        v (k, j) -= g * d[(size_t) k];
                }
            }

            for (int k = 0; k <= i; ++k)
                v (k, i + 1) = 0.0;
        }

        for (int j = 0; j < n; ++j)
        {
            d[(size_t) j] = v (n - 1, j);
            v (n - 1, j) = 0.0;
        }

        v (n - 1, n - 1) = 1.0;
        e[0] = 0.0;

        // tql2, on the transpose so that the rotations run along contiguous rows
        std::vector<double> Z ((size_t) n * (size_t) n);

        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                Z[(size_t) j * (size_t) n + (size_t) i] = v (i, j);

        for (int i = 1; i < n; ++i)
            e[(size_t) i - 1] = e[(size_t) i];

        e[(size_t) n - 1] = 0.0;
        double f = 0.0, tst1 = 0.0;
        const double eps = std::pow (2.0, -52.0);

        for (int l = 0; l < n; ++l)
        {
            tst1 = std::max (tst1, std::abs (d[(size_t) l]) + std::abs (e[(size_t) l]));
            int m = l;

            while (m < n && std::abs (e[(size_t) m]) > eps * tst1)
                ++m;

            if (m >= n)
                m = n - 1;

            if (m > l)
            {
                int iter = 0;

                do
                {
                    ++iter;
                    double g = d[(size_t) l];
                    double p = (d[(size_t) l + 1] - g) / (2.0 * e[(size_t) l]);
                    double r = std::hypot (p, 1.0);

                    if (p < 0.0)
                        r = -r;

                    d[(size_t) l] = e[(size_t) l] / (p + r);
                    d[(size_t) l + 1] = e[(size_t) l] * (p + r);
                    const double dl1 = d[(size_t) l + 1];
                    double h = g - d[(size_t) l];

                    for (int i = l + 2; i < n; ++i)
                        d[(size_t) i] -= h;

                    f += h;
                    p = d[(size_t) m];
                    double c = 1.0, c2 = c, c3 = c;
                    const double el1 = e[(size_t) l + 1];
                    double s = 0.0, s2 = 0.0;

                    for (int i = m - 1; i >= l; --i)
                    {
                        c3 = c2;
                        c2 = c;
                        s2 = s;
                        g = c * e[(size_t) i];
                        h = c * p;
                        r = std::hypot (p, e[(size_t) i]);
                        e[(size_t) i + 1] = s * r;
                        s = e[(size_t) i] / r;
                        c = p / r;
                        p = c * d[(size_t) i] - s * g;
                        d[(size_t) i + 1] = h + s * (c * g + s * d[(size_t) i]);

                        double* zi = &Z[(size_t) i * (size_t) n];
                        double* zi1 = &Z[((size_t) i + 1) * (size_t) n];

                        for (int k = 0; k < n; ++k)
                        {
                            h = zi1[k];
                            zi1[k] = s * zi[k] + c * h;
                            zi[k] = c * zi[k] - s * h;
                        }
                    }

                    p = -s * s2 * c3 * el1 * e[(size_t) l] / dl1;
                    e[(size_t) l] = s * p;
                    d[(size_t) l] = c * p;
                }
                while (std::abs (e[(size_t) l]) > eps * tst1 && iter < 60);
            }

            d[(size_t) l] += f;
            e[(size_t) l] = 0.0;
        }

        // Back to columns
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                v (i, j) = Z[(size_t) j * (size_t) n + (size_t) i];
    }
}

double ProRoomModel::FloorMode::evaluate (double x, double y) const
{
    double sum = 0.0;

    for (size_t i = 0; i < basis.size(); ++i)
        sum += coefficients[i] * std::cos (basis[i][0] * pi * x / length) * std::cos (basis[i][1] * pi * y / width);

    return sum;
}

std::vector<std::pair<double, std::shared_ptr<const ProRoomModel::FloorMode>>> ProRoomModel::floorModes (const ProRoom& room, double highestK)
{
    const double L = room.length, W = room.width;
    constexpr int maxBasis = 700;

    // Trial functions up to 1.5 times the highest wavenumber kept, fewer for a large floor
    double kb = 1.5 * highestK;
    std::vector<std::array<int, 2>> basis;

    for (;;)
    {
        basis.clear();

        for (int p = 0; p * pi / L <= kb; ++p)
            for (int q = 0; q * pi / W <= kb; ++q)
                if (std::pow (p * pi / L, 2.0) + std::pow (q * pi / W, 2.0) <= kb * kb)
                    basis.push_back ({ p, q });

        if ((int) basis.size() <= maxBasis)
            break;

        kb *= 0.95;
    }

    const double keepK = std::min (highestK, kb / 1.3);
    const int n = (int) basis.size();
    int pMax = 0, qMax = 0;

    for (const auto& b : basis)
    {
        pMax = std::max (pMax, b[0]);
        qMax = std::max (qMax, b[1]);
    }

    // Mass and stiffness matrices
    std::vector<double> M ((size_t) n * (size_t) n, 0.0), K ((size_t) n * (size_t) n, 0.0);
    const int columns = 16 * pMax + 64;
    const double hx = L / columns;
    const int nq = qMax + 1;
    std::vector<double> cx ((size_t) pMax + 1), sx ((size_t) pMax + 1), icc ((size_t) (nq * nq)), iss ((size_t) (nq * nq));

    for (int j = 0; j < columns; ++j)
    {
        const double x = (j + 0.5) * hx;
        double y0 = 0.0, y1 = 0.0;

        if (! room.floorInterval (x, y0, y1) || y1 <= y0)
            continue;

        for (int p = 0; p <= pMax; ++p)
        {
            cx[(size_t) p] = std::cos (p * pi * x / L);
            sx[(size_t) p] = std::sin (p * pi * x / L);
        }

        // Across the room, exactly: integrals of cos.cos and sin.sin over [y0, y1]
        const auto F = [y0, y1] (double k) { return std::abs (k) < 1e-12 ? y1 - y0 : (std::sin (k * y1) - std::sin (k * y0)) / k; };

        for (int q = 0; q < nq; ++q)
            for (int r = q; r < nq; ++r)
            {
                const double a = q * pi / W, b = r * pi / W;
                const double minus = F (a - b), plus = F (a + b);
                icc[(size_t) (q * nq + r)] = icc[(size_t) (r * nq + q)] = 0.5 * (minus + plus);
                iss[(size_t) (q * nq + r)] = iss[(size_t) (r * nq + q)] = 0.5 * (minus - plus);
            }

        for (int i = 0; i < n; ++i)
        {
            const int p = basis[(size_t) i][0], q = basis[(size_t) i][1];
            const double kpx = p * pi / L, kqy = q * pi / W;
            double* mRow = &M[(size_t) i * (size_t) n];
            double* kRow = &K[(size_t) i * (size_t) n];

            for (int k = i; k < n; ++k)
            {
                const int p2 = basis[(size_t) k][0], q2 = basis[(size_t) k][1];
                const double cc = cx[(size_t) p] * cx[(size_t) p2];
                const double c = icc[(size_t) (q * nq + q2)];
                mRow[k] += hx * cc * c;
                kRow[k] += hx * (kpx * (p2 * pi / L) * sx[(size_t) p] * sx[(size_t) p2] * c + kqy * (q2 * pi / W) * cc * iss[(size_t) (q * nq + q2)]);
            }
        }
    }

    for (int i = 0; i < n; ++i)
        for (int k = 0; k < i; ++k)
        {
            M[(size_t) i * (size_t) n + (size_t) k] = M[(size_t) k * (size_t) n + (size_t) i];
            K[(size_t) i * (size_t) n + (size_t) k] = K[(size_t) k * (size_t) n + (size_t) i];
        }

    // Cholesky of M (a little jitter: some trial functions nearly vanish on the floor)
    double largest = 0.0;

    for (int i = 0; i < n; ++i)
        largest = std::max (largest, M[(size_t) i * (size_t) n + (size_t) i]);

    const double jitter = 1e-9 * largest;
    std::vector<double> C ((size_t) n * (size_t) n, 0.0);

    for (int i = 0; i < n; ++i)
        for (int k = 0; k <= i; ++k)
        {
            double sum = M[(size_t) i * (size_t) n + (size_t) k] + (i == k ? jitter : 0.0);

            for (int j = 0; j < k; ++j)
                sum -= C[(size_t) i * (size_t) n + (size_t) j] * C[(size_t) k * (size_t) n + (size_t) j];

            C[(size_t) i * (size_t) n + (size_t) k] = i == k ? std::sqrt (std::max (sum, jitter)) : sum / C[(size_t) k * (size_t) n + (size_t) k];
        }

    // A = C^-1 K C^-T, by two forward substitutions on rows
    const auto forward = [&C, n] (std::vector<double>& B)
    {
        for (int i = 0; i < n; ++i)
        {
            double* row = &B[(size_t) i * (size_t) n];

            for (int j = 0; j < i; ++j)
            {
                const double c = C[(size_t) i * (size_t) n + (size_t) j];

                if (c != 0.0)
                {
                    const double* other = &B[(size_t) j * (size_t) n];

                    for (int k = 0; k < n; ++k)
                        row[k] -= c * other[k];
                }
            }

            const double pivot = C[(size_t) i * (size_t) n + (size_t) i];

            for (int k = 0; k < n; ++k)
                row[k] /= pivot;
        }
    };

    std::vector<double> A = K;
    forward (A);                    // C^-1 K

    for (int i = 0; i < n; ++i)     // transpose
        for (int k = 0; k < i; ++k)
            std::swap (A[(size_t) i * (size_t) n + (size_t) k], A[(size_t) k * (size_t) n + (size_t) i]);

    forward (A);                    // C^-1 (C^-1 K)^T = C^-1 K C^-T

    for (int i = 0; i < n; ++i)
        for (int k = 0; k < i; ++k)
        {
            const double mean = 0.5 * (A[(size_t) i * (size_t) n + (size_t) k] + A[(size_t) k * (size_t) n + (size_t) i]);
            A[(size_t) i * (size_t) n + (size_t) k] = A[(size_t) k * (size_t) n + (size_t) i] = mean;
        }

    std::vector<double> eigenvalues;
    symmetricEigen (n, A, eigenvalues);

    // Back to the trial functions: c = C^-T y; keep the real floor modes
    std::vector<std::pair<double, std::shared_ptr<const FloorMode>>> modes;

    for (int j = 0; j < n; ++j)
    {
        const double k2 = std::max (0.0, eigenvalues[(size_t) j]);

        if (std::sqrt (k2) > keepK)
            continue;

        std::vector<double> c ((size_t) n);

        for (int i = 0; i < n; ++i)
            c[(size_t) i] = A[(size_t) i * (size_t) n + (size_t) j];

        for (int i = n - 1; i >= 0; --i)
        {
            double sum = c[(size_t) i];

            for (int k = i + 1; k < n; ++k)
                sum -= C[(size_t) k * (size_t) n + (size_t) i] * c[(size_t) k];

            c[(size_t) i] = sum / C[(size_t) i * (size_t) n + (size_t) i];
        }

        // Its true norm on the floor: a mode carried by the jitter (nearly zero on the floor) is spurious
        double norm = 0.0;

        for (int i = 0; i < n; ++i)
        {
            double row = 0.0;

            for (int k = 0; k < n; ++k)
                row += M[(size_t) i * (size_t) n + (size_t) k] * c[(size_t) k];

            norm += c[(size_t) i] * row;
        }

        if (norm < 0.5)
            continue;

        for (auto& v : c)
            v /= std::sqrt (norm);

        auto mode = std::make_shared<FloorMode>();
        mode->length = L;
        mode->width = W;
        mode->basis = basis;
        mode->coefficients = std::move (c);
        modes.push_back ({ std::sqrt (k2), mode });
    }

    std::sort (modes.begin(), modes.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    return modes;
}

//==============================================================================
// Modes of a configured room
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

    const double H = r.height, kmax = 2.0 * pi * highestHz / c, V = r.volume();
    const double wallArea = r.wallArea(), floorArea = r.floorArea();

    // Decay of a mode: delta = c / (16 V) * sum of (mean of psi^2 on the surface / mean in the volume) * S * alpha'
    const auto damping = [&] (double hz, double wallRatio, int nz)
    {
        if (r.acoustics != 1)
            return 3.0 * std::log (10.0) / std::clamp (r.rt60, 0.1, 10.0);

        const double flatRatio = nz != 0 ? 2.0 : 1.0;
        const double sum = wallRatio * wallArea * exponentOf (r.absorption (ProRoom::frontWall, hz))
                         + flatRatio * floorArea * exponentOf (r.absorption (ProRoom::floor, hz))
                         + flatRatio * floorArea * exponentOf (r.absorption (ProRoom::ceiling, hz));
        return c * sum / (16.0 * V);
    };

    if (r.shape == ProRoom::round)
    {
        // Cylinder of radius a, rigid walls (Morse & Ingard, Theoretical Acoustics, 9.2):
        //   psi = J_m (k_r r) cos or sin (m theta) cos (nz pi z / H),  k_r = j'_mn / a
        const double a = 0.5 * r.width;

        for (int m = 0; m <= (int) (kmax * a) + 2; ++m)
        {
            auto roots = derivativeZeros (m, kmax * a);

            if (m == 0)
                roots.insert (roots.begin(), 0.0);

            for (double root : roots)
            {
                const double kr = root / a;
                const double radialMean = root == 0.0 ? 1.0 : (1.0 - (double) (m * m) / (root * root)) * std::pow (besselJ (m, root), 2.0);
                const double wallRatio = root == 0.0 ? 1.0 : std::min (4.0, 1.0 / std::max (0.25, 1.0 - (double) (m * m) / (root * root)));

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
                    mode.damping = damping (mode.hz, wallRatio, nz);
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

    // Oval, rounded corners, L shape: floor modes found numerically, the height separable
    const auto boundary = r.boundaryPoints (256);

    for (const auto& [k, floor] : floorModes (r, kmax))
    {
        double onWall = 0.0;

        for (const auto& p : boundary)
            onWall += std::pow (floor->evaluate (p[0], p[1]), 2.0) / (double) boundary.size();

        const double wallRatio = std::clamp (floorArea * onWall, 0.0, 4.0);

        for (int nz = 0; ; ++nz)
        {
            const double kz = nz * pi / H, k2 = k * k + kz * kz;

            if (k2 > kmax * kmax)
                break;

            Mode mode;
            mode.hz = c * std::sqrt (k2) / (2.0 * pi);
            mode.omega2 = std::pow (2.0 * pi * mode.hz, 2.0);
            mode.nz = nz;
            mode.floor = floor;
            mode.scale = floorArea * (nz == 0 ? 1.0 : 2.0);
            mode.damping = damping (mode.hz, wallRatio, nz);
            modes.push_back (mode);
        }
    }

    return modes;
}

double ProRoomModel::shape (const ProRoom& r, const Mode& mode, const Point& p)
{
    const double vertical = std::cos (mode.nz * pi * p[2] / r.height);

    if (mode.floor != nullptr)
        return mode.floor->evaluate (p[0], p[1]) * vertical;

    if (r.shape == ProRoom::rectangle)
        return std::cos (mode.nx * pi * p[0] / r.length) * std::cos (mode.ny * pi * p[1] / r.width) * vertical;

    const double dx = p[0] - 0.5 * r.width, dy = p[1] - 0.5 * r.width;
    const double radius = std::sqrt (dx * dx + dy * dy), theta = std::atan2 (dy, dx);
    const double radial = mode.radialK == 0.0 ? 1.0 : besselJ (mode.m, mode.radialK * radius);
    const double angular = mode.m == 0 ? 1.0 : (mode.sine ? std::sin (mode.m * theta) : std::cos (mode.m * theta));
    return radial * angular * vertical;
}

//==============================================================================
RoomPaths ProRoomModel::prepare (const ProRoom& room, const std::vector<double>& grid)
{
    ProRoom r = room;
    r.applyLinks();

    RoomPaths paths;
    paths.spec = { r.name, r.name, r.length, r.width, r.height, r.rt60At (63.0) };
    paths.lcrReference = true;
    paths.points = RoomSimulator::bandPoints (grid, RoomResult::layerEdgesHz[0]);     // from 10 Hz: the infra layer of the layered view
    const auto seats = r.seatPositions();
    paths.seats = (int) seats.size();
    const auto modes = modesOf (r, highestModeHz);
    const auto sources = r.placedSpeakers();
    const size_t numModes = modes.size(), n = paths.points.size();
    constexpr int ng = ProRoom::numGroups, nc = RoomPaths::numChannels;

    // Mode shapes at every source and every seat (with the mode's scale), once
    std::vector<std::vector<double>> atSource (sources.size(), std::vector<double> (numModes));

    for (size_t s = 0; s < sources.size(); ++s)
        for (size_t k = 0; k < numModes; ++k)
            atSource[s][k] = shape (r, modes[k], sources[s].position);

    // Which groups have speakers, and whether a level offset makes the set-up differ from the calibrated one
    bool used[ng] = {}, offset[ng] = {};

    for (const auto& s : sources)
    {
        used[s.group] = true;
        offset[s.group] = offset[s.group] || s.gain != s.nominalWeight;
    }

    // Each grid point stands for a band about 2.3 % wide. When the modes are
    // narrower than that (long decay), the response is averaged over several
    // frequencies inside the band instead of read at its centre.
    double narrowest = 1e9;

    for (const auto& m : modes)
        narrowest = std::min (narrowest, m.damping / pi);       // half-power bandwidth, Hz

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

    // Group responses at a set of points, averaged over each band as products: G_g conj(G_h)
    using GroupMatrix = std::array<cd, ng * ng>;
    const auto computeGroups = [&] (const std::vector<Point>& points, std::vector<GroupMatrix>& actual, std::vector<GroupMatrix>& nominal)
    {
        std::vector<double> atSeats (points.size() * numModes);

        for (int seat = 0; seat < (int) points.size(); ++seat)
            for (size_t k = 0; k < numModes; ++k)
                atSeats[(size_t) seat * numModes + k] = shape (r, modes[k], points[(size_t) seat]) * modes[k].scale;

        // Group responses at every seat, averaged over each band as products: G_g conj(G_h)
        actual.assign (points.size() * n, GroupMatrix {});
        nominal = actual;
        std::vector<std::vector<cd>> groupAt (ng, std::vector<cd> (numModes)), groupNominalAt = groupAt;
        std::vector<cd> term (numModes);

        for (size_t i = 0; i < n; ++i)
        {
            for (const auto& sub : subs[i])
            {
                const double w = sub.w;

                // The speakers of a group summed mode by mode first (delays and levels included):
                // each seat then costs one sum over the modes per group
                for (int g = 0; g < ng; ++g)
                {
                    std::fill (groupAt[(size_t) g].begin(), groupAt[(size_t) g].end(), cd());
                    std::fill (groupNominalAt[(size_t) g].begin(), groupNominalAt[(size_t) g].end(), cd());
                }

                for (size_t s = 0; s < sources.size(); ++s)
                {
                    const cd delay = std::polar (1.0, -w * sources[s].delayMs * 0.001);
                    const cd gain = delay * sources[s].gain, nominalGain = delay * sources[s].nominalWeight;
                    auto& sum = groupAt[(size_t) sources[s].group];
                    auto& nominalSum = groupNominalAt[(size_t) sources[s].group];

                    for (size_t k = 0; k < numModes; ++k)
                    {
                        sum[k] += atSource[s][k] * gain;
                        nominalSum[k] += atSource[s][k] * nominalGain;
                    }
                }

                for (size_t k = 0; k < numModes; ++k)
                    term[k] = 1.0 / cd (modes[k].omega2 - w * w, 2.0 * modes[k].damping * w);

                for (int seat = 0; seat < (int) points.size(); ++seat)
                {
                    const double* shapes = &atSeats[(size_t) seat * numModes];
                    std::array<cd, ng> h {}, hn {};

                    for (int g = 0; g < ng; ++g)
                    {
                        if (! used[g])
                            continue;

                        const cd* a = groupAt[(size_t) g].data();
                        cd sum;

                        for (size_t k = 0; k < numModes; ++k)
                            sum += a[k] * (shapes[k] * term[k]);

                        h[(size_t) g] = sum;

                        if (offset[g])
                        {
                            const cd* b = groupNominalAt[(size_t) g].data();
                            cd nominalSum;

                            for (size_t k = 0; k < numModes; ++k)
                                nominalSum += b[k] * (shapes[k] * term[k]);

                            hn[(size_t) g] = nominalSum;
                        }
                        else
                        {
                            hn[(size_t) g] = sum;
                        }
                    }

                    auto& ma = actual[(size_t) seat * n + i];
                    auto& mn = nominal[(size_t) seat * n + i];

                    for (int g = 0; g < ng; ++g)
                        for (int k = 0; k < ng; ++k)
                        {
                            ma[(size_t) (g * ng + k)] += sub.weight * h[(size_t) g] * std::conj (h[(size_t) k]);
                            mn[(size_t) (g * ng + k)] += sub.weight * hn[(size_t) g] * std::conj (hn[(size_t) k]);
                        }
                }
            }
        }

    };

    std::vector<GroupMatrix> actual, nominal;
    computeGroups (seats, actual, nominal);

    // Calibration on the room as set up normally (every speaker at its level),
    // one speaker group at a time, over all seats or the reference seat:
    // L, C, R and the subwoofers each to the stage level. Fills keep their
    // level against the screen speaker of their side.
    double mean[ng] = {}, weight = 0.0;
    const int reference = r.referenceSeat();

    for (int seat = 0; seat < paths.seats; ++seat)
    {
        if (r.calibration == 1 && seat != reference)
            continue;

        for (size_t i = 0; i < n; ++i)
        {
            const double hz = grid[(size_t) paths.points[i]];

            if (hz < RoomSimulator::bandLoHz)
                continue;       // calibrated over 20-120 Hz, as before; the infra points are only shown

            for (int g = 0; g < ng; ++g)
                mean[g] += hz * nominal[(size_t) seat * n + i][(size_t) (g * ng + g)].real();

            weight += hz;
        }
    }

    double scale[ng];

    for (int g = 0; g < ng; ++g)
        scale[g] = weight > 0.0 && mean[g] > 0.0 ? std::sqrt (weight / mean[g]) : 1.0;

    scale[ProRoom::groupFillsLeft] = scale[ProRoom::groupL];
    scale[ProRoom::groupFillsRight] = scale[ProRoom::groupR];
    scale[ProRoom::groupFillsLfe] = scale[ProRoom::groupSubs];

    // Channel paths: H_a = sum over groups of routing (a, g) x scale (g) x G_g
    const auto routing = r.routing();
    double R[nc][ng];

    for (int a = 0; a < nc; ++a)
        for (int g = 0; g < ng; ++g)
            R[a][g] = used[g] ? routing[(size_t) a][(size_t) g] * scale[g] : 0.0;

    const auto toChannels = [&R] (const GroupMatrix& m)
    {
        RoomPaths::ChannelMatrix out {};

        for (int a = 0; a < nc; ++a)
            for (int b = 0; b < nc; ++b)
            {
                cd sum;

                for (int g = 0; g < ng; ++g)
                {
                    if (R[a][g] == 0.0)
                        continue;

                    for (int k = 0; k < ng; ++k)
                        if (R[b][k] != 0.0)
                            sum += R[a][g] * R[b][k] * m[(size_t) (g * ng + k)];
                }

                out[(size_t) (a * nc + b)] = sum;
            }

        return out;
    };

    paths.p.resize (actual.size());
    paths.pRef.resize (actual.size());

    for (size_t i = 0; i < actual.size(); ++i)
    {
        paths.p[i] = toChannels (actual[i]);
        paths.pRef[i] = toChannels (nominal[i]);
    }

    // Extra points around the riskiest seats: those where the room plays the
    // LFE path most differently from the screen path (independent of the mix,
    // so that they are found once). Eight points 0.6 m around each of the
    // four, rated with the seats but not in the room score: they look for a
    // worse spot between the seats.
    if (paths.seats > 0)
    {
        std::vector<std::pair<double, int>> mismatch;

        for (int seat = 0; seat < paths.seats; ++seat)
        {
            double sum = 0.0, weight = 0.0;

            for (size_t i = 0; i < n; ++i)
            {
                const auto& m = paths.pRef[(size_t) seat * n + i];
                const double screen = (m[0].real() + m[5].real() + m[10].real()) / 3.0, lfe = m[15].real();
                const double hz = grid[(size_t) paths.points[i]];

                if (screen > 0.0 && lfe > 0.0)
                {
                    sum += hz * std::abs (10.0 * std::log10 (lfe / screen));
                    weight += hz;
                }
            }

            mismatch.push_back ({ weight > 0.0 ? sum / weight : 0.0, seat });
        }

        std::sort (mismatch.begin(), mismatch.end(), std::greater<>());
        std::vector<Point> probes;

        for (size_t k = 0; k < std::min<size_t> (4, mismatch.size()); ++k)
        {
            const auto& c = seats[(size_t) mismatch[k].second];

            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                {
                    const double x = c[0] + 0.6 * dx, y = c[1] + 0.6 * dy;

                    if ((dx == 0 && dy == 0) || ! r.isInside (x, y))
                        continue;

                    probes.push_back (r.keptInside ({ x, y, c[2] }));
                }
        }

        if (! probes.empty())
        {
            std::vector<GroupMatrix> probeActual, probeNominal;
            computeGroups (probes, probeActual, probeNominal);

            for (size_t i = 0; i < probeActual.size(); ++i)
            {
                paths.p.push_back (toChannels (probeActual[i]));
                paths.pRef.push_back (toChannels (probeNominal[i]));
            }

            paths.probes = (int) probes.size();
            paths.probePositions = probes;
        }
    }

    // A regular grid over the floor at ear height, for the layered view: 12
    // points along the longer side, in proportion across the other
    {
        const double longer = std::max (r.length, r.width);
        const int columns = std::clamp ((int) std::lround (12.0 * r.length / longer), 4, 12);
        const int rows = std::clamp ((int) std::lround (12.0 * r.width / longer), 4, 12);
        std::vector<Point> nodes;
        paths.gridIndex.assign ((size_t) (columns * rows), -1);

        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < columns; ++column)
            {
                const double x = r.length * (column + 0.5) / columns, y = r.width * (row + 0.5) / rows;

                if (! r.isInside (x, y))
                    continue;

                paths.gridIndex[(size_t) (row * columns + column)] = (int) nodes.size();
                nodes.push_back (r.keptInside ({ x, y, r.earHeight }));
            }

        if (! nodes.empty())
        {
            std::vector<GroupMatrix> gridActual, gridNominal;
            computeGroups (nodes, gridActual, gridNominal);

            for (size_t i = 0; i < gridActual.size(); ++i)
            {
                paths.p.push_back (toChannels (gridActual[i]));
                paths.pRef.push_back (toChannels (gridNominal[i]));
            }
        }

        paths.gridPoints = (int) nodes.size();
        paths.gridColumns = columns;
        paths.gridRows = rows;
        paths.gridPositions = nodes;
    }

    return paths;
}

} // namespace subguard
