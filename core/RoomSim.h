// Room Sim: what the LFE / screen interaction does in fifteen cinema rooms,
// seat by seat. Portable C++17.
//
// MODEL
//   Each room is a rectangular box with rigid, uniformly damped walls. The
//   sound pressure at a seat r from a source r0 is the sum of the room's
//   modes (Kuttruff, Room Acoustics, ch. 3):
//
//     H(r, r0, w) = sum_n  psi_n(r) . psi_n(r0) / ( Lambda_n . (w_n^2 - w^2 + 2j . delta . w) )
//
//     psi_n = cos (nx pi x / L) . cos (ny pi y / W) . cos (nz pi z / H)
//     w_n   = pi c . sqrt ((nx/L)^2 + (ny/W)^2 + (nz/H)^2)       (Rayleigh)
//     Lambda_n = product of 1 (index 0) or 1/2 (index > 0)
//     delta = 6.91 / T60                                            (decay constant)
//
//   Modes up to 200 Hz are summed for the 20-120 Hz band. The screen is
//   three sources behind it (L, C, R, the screen sum shared equally), the
//   LFE two subwoofers at the foot of the front wall. Twenty seats fill the
//   seating area at ear height.
//
// WHAT IS MEASURED
//   The room colours the bass of any mix: that is not the mix's doing and is
//   left out. What is kept is what the LFE / screen interaction adds at each
//   seat, against the mix as approved:
//
//     interaction at a seat  = P(seat) / P(seat, LFE and screen added in power)
//     interaction approved   = P(stage) / P(stage, added in power)
//     change (dB)            = 10 log10 (interaction at seat / interaction approved)
//
//   An LFE decorrelated from the screen gives 0 dB at every seat of every
//   room. Each room is calibrated as a real one: screen and subwoofers are
//   each set so that their level averaged over the seats equals the stage's.
//   Each seat is rated like a playback system, with audibility and loudness
//   (ISO 226) taken at the level the bass has at that seat, room peaks and
//   dips included, so the listening level matters; the room score is 100 x (1 - the mean
//   seat penalty), and the room result is bass robustness x room score.
//
// LIMITS
//   Real rooms are not boxes (raked floors, splayed walls, frequency-dependent
//   absorption, several subwoofers placed and delayed by a calibrator). The
//   fifteen rooms are representative examples, not measured rooms.

#pragma once

#include "LfeEngine.h"
#include "PlaybackSim.h"

#include <array>
#include <complex>
#include <string>
#include <vector>

namespace subguard
{

struct RoomSpec
{
    std::string name, shortName;
    double length = 20.0, width = 12.0, height = 7.0;     // m
    double rt60 = 0.5;                                    // s
};

struct RoomResult
{
    static constexpr int numSeats = 20;         // seats of the fifteen standard rooms

    RoomSpec spec;
    std::vector<double> seatChangeDb;           // per seat, 20-120 Hz, interaction against the approved one
    std::vector<double> seatPenalty;            // per seat, 0..1
    double worstDb = 0.0, medianDb = 0.0, bestDb = 0.0;   // seats sorted by change
    double roomScore = 100.0;                             // 0..100
    double combinedScore = 100.0;                         // bass robustness x room score / 100
};

/** Calibrated transfer functions of a room: screen path and LFE path, per seat and grid point. */
struct RoomPaths
{
    RoomSpec spec;
    int seats = 0;
    std::vector<int> points;                                // grid indices inside 20-120 Hz
    std::vector<std::complex<double>> screen, sub;          // [seat * points.size() + i]

    // The room as set up normally (no level offsets), calibrated: what each seat
    // is measured against, so that the room's own colour is left out but a
    // speaker set off its level is not. Empty: the same as screen and sub.
    std::vector<std::complex<double>> screenNominal, subNominal;
};

struct RoomSimResult
{
    bool hasBass = false;
    std::vector<RoomResult> rooms;
};

class RoomSimulator
{
public:
    static constexpr double speedOfSound = 343.0;
    static constexpr double highestModeHz = 200.0;
    static constexpr double bandLoHz = 20.0, bandHiHz = 120.0;

    RoomSimulator();

    /** The fifteen rooms, from the worst kind (a narrow, live corridor) to the best (a treated IMAX). */
    static std::vector<RoomSpec> defaultRooms();

    /** Modal transfer function from a source to a receiver (positions in metres), at `hz`. */
    static std::complex<double> response (const RoomSpec&, const std::array<double, 3>& source,
                                          const std::array<double, 3>& receiver, double hz);

    /** The rooms ticked by default: five of the middle of the range, neither the worst nor the best. */
    static std::vector<bool> defaultSelection();

    /** Folds the ticked rooms into the score, as further playback systems: a room
        counts with the penalty of its average seat (1 - room score / 100), and
        the score keeps the worst case, as for the other systems. */
    static void applyToScore (Translation&, const RoomSimResult&, const std::vector<bool>& selected, double levelDependenceWeight);

    /** Grid indices the rooms are computed on (20-120 Hz). */
    static std::vector<int> bandPoints (const std::vector<double>& grid);

    /** Calibration: scales the screen and LFE paths so that their level, averaged over the given seats
        (all when empty), equals the stage's. */
    static void calibrate (RoomPaths&, const std::vector<double>& grid, const std::vector<int>& seats = {});

    /** Rates one room seat by seat (the interaction against the mix as approved, with audibility
        and loudness at each seat's own level). Shared by Room Sim and Room Sim Pro. */
    static RoomResult rate (const RoomPaths&, const Analysis&, const Translation&, const PlaybackSimulator::Tuning&,
                            PlaybackSimulator::Level, double lfeGainDb);

    /** Mode shape and modal denominators, for other room models. */
    struct Mode { double omega2, damping, scale; int nx, ny, nz; };
    static std::vector<Mode> modesOf (const RoomSpec&, double highestHz = highestModeHz);
    static double shape (const RoomSpec&, const Mode&, const std::array<double, 3>& point);

    /** `translation` is the playback simulation of the same analysis (its band loudness and audibility). */
    RoomSimResult compute (const Analysis&, const Translation& translation, const PlaybackSimulator::Tuning&,
                           PlaybackSimulator::Level, double lfeGainDb);

private:
    void prepare (const std::vector<double>& grid);

    std::vector<RoomSpec> specs;
    std::vector<double> preparedGrid;
    std::vector<RoomPaths> prepared;
};

} // namespace subguard
