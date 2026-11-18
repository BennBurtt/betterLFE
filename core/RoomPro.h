// Room Sim Pro: three rooms configured by the user, outside the standard
// cinema layout. Same modal model and the same seat-by-seat rating as Room
// Sim (RoomSimulator::rate), with every speaker placed freely: L, C, R, up to
// four subwoofers and up to four fill speakers ("rappels"), each with its
// own delay, level and polarity. Portable C++17.
//
// The fill speakers play the screen channels (or the LFE, if set so),
// delayed as set: they join the screen path (or the LFE path) at each seat.
//
// The configuration is saved as text, `key = value`, one per line, under a
// version header. EVERY FIELD MUST BE WRITTEN BY toText AND READ BY fromText:
// a test changes every field and checks the round trip, so a field added to
// ProRoom and forgotten in the export fails the tests.

#pragma once

#include "RoomSim.h"

#include <array>
#include <string>
#include <vector>

namespace subguard
{

struct ProSpeaker
{
    bool enabled = true;
    double x = 0.0, y = 0.0, z = 0.0;   // metres: x from the front (screen) wall, y from the left wall, z from the floor
    double delayMs = 0.0;               // added delay
    double gainDb = 0.0;                // level against its calibrated level
    bool inverted = false;
    int feed = 0;                       // fill speakers only: 0 the screen channels, 1 the LFE

    bool operator== (const ProSpeaker& o) const
    {
        return enabled == o.enabled && x == o.x && y == o.y && z == o.z && delayMs == o.delayMs && gainDb == o.gainDb
                 && inverted == o.inverted && feed == o.feed;
    }
};

struct ProRoom
{
    static constexpr int maxSubs = 4, maxFills = 4;

    std::string name = "Room";
    bool counted = true;                // counts in the Room Sim Pro score

    double length = 20.0, width = 13.0, height = 7.0;   // m
    double rt60 = 0.55;                                 // s, around 63 Hz

    std::array<ProSpeaker, 3> lcr;      // L, C, R
    int numSubs = 2;
    std::array<ProSpeaker, maxSubs> subs;
    bool fillsOn = false;
    int numFills = 2;
    std::array<ProSpeaker, maxFills> fills;

    // Audience
    double firstRow = 7.0, lastRow = 17.0;  // m from the front wall
    double audienceWidth = 0.64;            // share of the room width
    int rows = 4, perRow = 5;
    double earHeight = 1.2;                 // m above the floor at the first row
    double rake = 0.0;                      // m the last row is raised above the first

    int calibration = 0;                    // 0: average of all seats; 1: reference seat (2/3 of the room, centre)

    /** A layout for a standard room: screen speakers behind the screen, subwoofers at its foot, seats from 35 to 85 %. */
    static ProRoom fromSpec (const RoomSpec&);

    std::vector<std::array<double, 3>> seatPositions() const;
    int referenceSeat() const;

    bool operator== (const ProRoom& o) const;
};

struct ProRooms
{
    static constexpr int count = 3;
    std::array<ProRoom, count> rooms;

    static ProRooms defaults();     // a small, a medium and a large room

    static constexpr int formatVersion = 1;
    std::string toText() const;
    static bool fromText (const std::string&, ProRooms& out);    // false: not a Room Sim Pro file; `out` untouched

    bool operator== (const ProRooms& o) const   { return rooms == o.rooms; }
};

class ProRoomModel
{
public:
    static constexpr double highestModeHz = 160.0;
    static constexpr int maxSeats = 96;

    /** Calibrated transfer functions of a configured room on the grid (can take a second for a large room). */
    static RoomPaths prepare (const ProRoom&, const std::vector<double>& grid);
};

} // namespace subguard
