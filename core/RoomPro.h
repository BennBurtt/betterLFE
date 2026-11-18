// Room Sim Pro: three rooms configured by the user, outside the standard
// cinema layout. Same modal model and the same seat-by-seat rating as Room
// Sim (RoomSimulator::rate), with every speaker placed freely: L, C, R (one
// set, or two back to back), up to four subwoofers and up to four fill
// speakers ("rappels"), each with its own delay, level and polarity.
// Portable C++17.
//
// Each channel of the mix (L, C, R, LFE) is played by its own speakers: bass
// only in L is heard where the L speaker reaches. A routing in the manner of a
// console panner spreads the channels over the speakers: centre % (C played
// by C or as a phantom centre in L and R), front divergence (each screen
// channel spread over L C R), rear divergence (left and right fills sharing
// their feeds) and front / rear divergence (screen channels sent to the
// fills). Fills play Lc / Rc (L or R + C at -3 dB) or the LFE.
//
// Positions are kept as a share of the room: set the room size first, then
// place the speakers; resizing the room moves them with it. The decay is set
// by hand (RT60) or comes from the materials of the surfaces and the people
// in the room, each mode decaying according to the surfaces it meets
// (Kuttruff). Rectangular rooms and round rooms have exact modes; oval,
// rounded-corner and L-shaped floors are solved numerically (Rayleigh-Ritz on
// the floor plan, the height separable).
//
// The configuration is saved as text, `key = value`, one per line, under a
// version header. EVERY FIELD MUST BE WRITTEN BY toText AND READ BY fromText:
// a test changes every field and checks the round trip, so a field added to
// ProRoom and forgotten in the export fails the tests.

#pragma once

#include "RoomSim.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace subguard
{

/** A surface material: random-incidence absorption coefficient at 63 and 125 Hz. */
struct ProMaterial
{
    enum Group { masonry, boards, glass, floors, soft, treatment, light, open, numGroups };
    enum Surfaces { wall = 1, floor = 2, ceiling = 4 };

    const char* key;            // stable name used in the saved files
    const char* name;           // English; the interface translates it
    double alpha63, alpha125;
    const char* source;         // where the values come from (see docs/ALGORITHME.md)
    int group;
    int surfaces;               // where it can go: walls, floor, ceiling

    static const char* groupName (int group);      // English
};

struct ProSpeaker
{
    bool enabled = true;
    // Position as a share of the room (0..1): x from the front wall, y from the left wall, z from the floor.
    // Shown in metres; resizing the room moves the speaker with it.
    double fx = 0.0, fy = 0.0, fz = 0.0;
    double delayMs = 0.0;               // added delay
    double gainDb = 0.0;                // level against its calibrated level
    bool inverted = false;
    int feed = 0;                       // fill speakers only: 0 Lc / Rc (L or R + C at -3 dB), 1 the LFE

    bool operator== (const ProSpeaker& o) const
    {
        return enabled == o.enabled && fx == o.fx && fy == o.fy && fz == o.fz && delayMs == o.delayMs && gainDb == o.gainDb
                 && inverted == o.inverted && feed == o.feed;
    }
};

struct ProRoom
{
    static constexpr int maxSubs = 4, maxFills = 4;
    enum Surface { frontWall, backWall, leftWall, rightWall, floor, ceiling, numSurfaces };   // other shapes: frontWall is the wall
    enum Shape { rectangle, round, oval, roundedCorners, lShape, numShapes };
    enum Layout { rowsLayout, sidesLayout, ringLayout };
    enum People { noPeople, seatedPeople, standingPeople };

    std::string name = "Room";
    bool counted = true;                // counts in the Room Sim Pro score

    int shape = rectangle;              // round: a cylinder, its diameter is the width (the length follows it)
    double length = 20.0, width = 13.0, height = 7.0;   // m
    int acoustics = 0;                  // 0: RT60 set by hand; 1: from the materials and the people
    double rt60 = 0.55;                 // s, 0.1 to 10
    std::array<int, numSurfaces> materials { 0, 0, 0, 0, 0, 0 };   // index into materialList()

    std::array<ProSpeaker, 3> lcr;      // L, C, R
    bool linkLR = true;                 // R mirrors L across the room's centre line
    bool dualLCR = false;               // a second L C R set, back to back: mirrored through the centre of the room
    int numSubs = 2;
    std::array<ProSpeaker, maxSubs> subs;
    bool fillsOn = false;
    bool linkFills = true;              // fill 2 mirrors fill 1, fill 4 mirrors fill 3
    int numFills = 2;
    std::array<ProSpeaker, maxFills> fills;

    // Routing, in the manner of a console panner (100: each channel on its own speakers)
    double centrePercent = 100.0;       // share of C played by the C speaker, the rest as a phantom centre in L and R
    double frontDivergence = 100.0;     // lower: each screen channel spread over L, C and R
    double rearDivergence = 100.0;      // lower: left and right fills share their feeds
    double frDivergence = 100.0;        // lower: the screen channels also sent to the fills

    // Audience
    int audienceLayout = rowsLayout;        // rows; two blocks with a free aisle in the middle; rings around the centre
    double firstRow = 0.35, lastRow = 0.85; // rows and blocks: share of the length, from the front wall
    double audienceWidth = 0.64;            // rows and blocks: share of the room width
    double aisle = 0.25;                    // blocks: share of the width left free in the middle
    double ringInner = 0.3, ringOuter = 0.85;   // rings: radii as a share of half the smaller side
    int rows = 4, perRow = 5;               // rings: number of rings, seats on the outer ring
    double earHeight = 1.2;                 // m above the floor at the first row
    double rake = 0.0;                      // m the last row is raised above the first
    int people = noPeople;                  // people on the audience area: their absorption is added to the floor's

    int calibration = 0;                    // 0: average of all seats; 1: reference seat (2/3 of the room, centre)

    /** A layout for a standard cinema room: L C R behind the screen, subwoofers at its foot, seats from 35 to 85 %. */
    static ProRoom fromSpec (const RoomSpec&);

    /** Rooms outside the cinema, with their materials, by category (exhibitions, fashion shows, shops, other). */
    static std::vector<struct ProVenue> venues();

    static const std::vector<ProMaterial>& materialList();
    static int materialIndex (const std::string& key);     // -1 when unknown

    std::array<double, 3> positionOf (const ProSpeaker&) const;     // metres
    std::vector<std::array<double, 3>> seatPositions() const;
    int referenceSeat() const;

    /** Mirrors the linked speakers (R from L, fills 2 and 4 from 1 and 3); a round room gets its length from its width. */
    void applyLinks();

    /** Speaker groups: each plays a mix of the channels set by the routing. */
    enum Group { groupL, groupC, groupR, groupSubs, groupFillsLeft, groupFillsRight, groupFillsLfe, numGroups };

    /** Every speaker as placed, with the second L C R set when there is one. gain and nominalWeight: relative amplitude, polarity included. */
    struct Placed { std::array<double, 3> position; int group; double delayMs, gain, nominalWeight; };
    std::vector<Placed> placedSpeakers() const;

    /** Amplitude with which each channel (L, C, R, LFE) feeds each group: the routing (centre %, divergences, fill feeds). */
    std::array<std::array<double, numGroups>, 4> routing() const;

    // Geometry
    bool floorInterval (double x, double& y0, double& y1) const;    // the floor's extent across the room at x (false: none)
    bool isInside (double x, double y) const;
    std::array<double, 3> keptInside (std::array<double, 3> p) const;      // a point moved inside the walls if needed
    double floorArea() const;
    double wallArea() const;                    // the whole wall, all around
    double volume() const;
    std::vector<std::array<double, 2>> boundaryPoints (int count) const;  // evenly spaced along the wall, at floor level
    double audienceArea() const;                // the floor area the audience covers

    // Acoustics
    double surfaceArea (int surface) const;
    double absorption (int surface, double hz) const;       // alpha of a surface (log-interpolated 63 / 125 Hz); the floor with its people
    double eyringRt60 (double hz) const;                    // from the materials
    double rt60At (double hz) const;                        // the set RT60, or the materials' Eyring value
    double schroederHz() const;                             // 2000 * sqrt(T / V)
    /** Amplitude decay constant (1/s) of a mode of a rectangular room: uniform from the RT60, or from the surfaces it meets. */
    double modeDamping (const RoomSimulator::Mode&) const;

    bool operator== (const ProRoom& o) const;
};

struct ProVenue
{
    std::string category;
    ProRoom room;
};

struct ProRooms
{
    static constexpr int count = 3;
    std::array<ProRoom, count> rooms;

    static ProRooms defaults();     // a small, a medium and a large room

    // 2: positions as a share of the room, links, materials; 3: shape, layouts, second L C R;
    // 4: people, routing (centre %, divergences), more shapes
    static constexpr int formatVersion = 4;
    std::string toText() const;
    static bool fromText (const std::string&, ProRooms& out);    // false: not a Room Sim Pro file; `out` untouched

    bool operator== (const ProRooms& o) const   { return rooms == o.rooms; }
};

class ProRoomModel
{
public:
    static constexpr double highestModeHz = 160.0;
    static constexpr int maxSeats = 96;

    /** A floor mode found numerically: coefficients on the cosine basis of the bounding rectangle. */
    struct FloorMode
    {
        double length = 0.0, width = 0.0;
        std::vector<std::array<int, 2>> basis;      // (p, q): cos (p pi x / L) cos (q pi y / W)
        std::vector<double> coefficients;           // normalised: the mean of the square over the floor is 1 / area
        double evaluate (double x, double y) const;
    };

    /** A room mode: rectangular (orders nx, ny, nz), cylindrical (angular order m, radial root, nz) or on a numerical floor. */
    struct Mode
    {
        double hz = 0.0, omega2 = 0.0, damping = 0.0, scale = 1.0;
        int nx = 0, ny = 0, nz = 0;         // rectangle
        int m = 0;                          // cylinder
        double radialK = 0.0;               // cylinder: radial wavenumber, 1/m
        bool sine = false;                  // cylinder: sin (m theta) instead of cos
        std::shared_ptr<const FloorMode> floor;     // other shapes
    };

    static std::vector<Mode> modesOf (const ProRoom&, double highestHz = highestModeHz);
    static double shape (const ProRoom&, const Mode&, const std::array<double, 3>& point);

    /** Floor modes of an oval, rounded-corner or L-shaped floor up to `highestK` (rad/m): wavenumbers and shapes. */
    static std::vector<std::pair<double, std::shared_ptr<const FloorMode>>> floorModes (const ProRoom&, double highestK);

    /** Bessel function of the first kind, and the n-th zero (n from 1) of its derivative. */
    static double besselJ (int m, double x);
    static double besselDerivativeZero (int m, int n);

    /** Calibrated paths of a configured room on the grid (can take a few seconds for a large room). */
    static RoomPaths prepare (const ProRoom&, const std::vector<double>& grid);
};

} // namespace subguard
