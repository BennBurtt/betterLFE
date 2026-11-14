// Playback-system simulation and robustness score. Portable C++17.
//
// Question answered: once it leaves the dubbing stage, how far does the bass of
// this mix move from what was approved, across the ways it will be played?
//
// REFERENCE
//   The mix as heard on a calibrated stage: screen channels plus the LFE
//   10 dB above a screen channel (SMPTE RP 200), subwoofer on time and in
//   polarity.
//
// SYSTEMS
//   Each system is a pair of filters applied to F (L+C+R) and to the LFE before
//   they are summed. The power of the sum follows from the smoothed auto and
//   cross spectra the engine keeps, so nothing is re-filtered:
//
//     P = |Gf|^2 Sff + |Gl|^2 Sll + 2 Re (Gf . conj(Gl) . Sfl)
//
//   Cinema         subwoofer arriving -5 .. +5 ms from the screen, in either
//                  polarity (10 cases).
//   Bass managed   home system: screen channels crossed over to the subwoofer
//                  (4th-order Linkwitz-Riley at 60, 80 or 100 Hz), LFE low-passed
//                  at 120 Hz; with and without a steep 120 Hz low-pass standing
//                  in for an encoder's LFE filter (a model, not Dolby's filter).
//   Stereo         two-channel downmix of L, C, R; the LFE is left out, as in
//                  the Dolby and ITU-R BS.775 downmixes.
//
// FROM dB TO PERCEPTION
//   The difference between a system and the reference is taken per band, in dB.
//   ISO 226:2003 gives how many dB double the loudness of a low tone around
//   80 phon: 5.0 dB at 25 Hz, 5.5 dB at 40 Hz, 6.4 dB at 70 Hz, 6.9 dB at 100 Hz
//   (10 dB at 1 kHz). Each band is then rated
//
//     penalty = clamp ((|difference| - 1 dB) / (doubling - 1 dB), 0, 1)
//
//   0 up to 1 dB (about the just-noticeable level difference), 1 when the band
//   is heard twice as loud or half as loud. Bands are combined by their share
//   of the bass.
//
// SCORE
//   Two risks, multiplied:
//
//     interaction = the highest (relevance x penalty) among the cinema and
//                   bass-managed systems. The worst case, not an average: a
//                   mix that cancels on one plausible system is not made safe
//                   by the systems where it survives.
//     dependence  = 0.72 x the penalty of the stereo downmix without LFE, i.e.
//                   of what the bass loses wherever the LFE is missing or
//                   wrongly set.
//
//     robustness = 100 . (1 - interaction) . (1 - dependence)
//
//   Anchors: screen alone 100; LFE identical to the screen 0 (an inverted
//   subwoofer cancels it); LFE alone about 22 (the room alone decides the
//   bass); 50 when the worst on-time system moves the bass half-way to a
//   doubling or halving of loudness.
//
// Cross terms that chance alone could produce are removed first, so unrelated
// signals are never reported as interacting.

#pragma once

#include "LfeEngine.h"

#include <array>
#include <string>
#include <vector>

namespace subguard
{

struct ChainResult
{
    enum class Kind { cinema, bassManaged, stereo };

    Kind kind = Kind::cinema;
    std::string name;
    double delayMs = 0.0;       // cinema: subwoofer offset
    bool inverted = false;      // cinema: subwoofer polarity
    double crossoverHz = 0.0;   // bass managed
    bool encoderFilter = false; // bass managed: LFE through the encoder low-pass model

    static constexpr int numBands = 4;
    double changeDb = 0.0;                          // bass against the reference, 20-120 Hz
    std::array<double, numBands> bandChangeDb {};   // same per band (20-30, 30-60, 60-90, 90-120 Hz)
    double penalty = 0.0;                           // 0..1, perceptual, see the header comment
    double weight = 0.0;                            // relevance, 0..1: how much this system counts
};

struct Translation
{
    static constexpr double bandEdges[4] = { 30.0, 60.0, 90.0, 120.0 };     // overlap bands

    bool hasBass = false;               // false: silence, nothing below is meaningful

    std::vector<ChainResult> chains;
    double robustness = 100.0;          // 0..100
    double interaction = 0.0;           // 0..1, worst relevance x penalty among cinema and bass-managed systems
    double dependence = 0.0;            // 0..1, how much the bass rests on the LFE
    int worstChain = -1;                // the system behind `interaction`

    // LFE as heard (+10 dB) against the screen channels, 20-120 Hz
    double lfeBalanceDb = 0.0;          // positive: the LFE is the louder of the two
    double lfeShare = 0.0;              // 0..1, part of the bass energy carried by the LFE
    double downmixChangeDb = 0.0;       // bass left in the stereo downmix, against the reference

    // LFE (as heard) and screen sum both active in the same band
    struct Overlap
    {
        double screenDb = -200.0, lfeDb = -200.0;   // RMS level in the band, dBFS
        double amount = 0.0;                        // 0..1; 1: equal levels
    };

    std::array<Overlap, 3> overlap;
};

class PlaybackSimulator
{
public:
    static constexpr double lfeInBandGainDb = 10.0;     // SMPTE RP 200
    static constexpr double changeLimitDb = 24.0;       // larger differences all count the same
    static constexpr double justNoticeableDb = 1.0;     // level difference that starts to count
    static constexpr double overlapWindowDb = 10.0;     // levels further apart than this do not overlap
    static constexpr double balanceWarningDb = 6.0;     // LFE this far above the screen: the bass then follows each room's subwoofer level

    // Rating bands and, for each, the level change that doubles loudness
    // (ISO 226:2003 around 80 phon, at 25, 40, 70 and 100 Hz).
    static constexpr double bandLimits[ChainResult::numBands + 1] = { 20.0, 30.0, 60.0, 90.0, 120.0 };
    static constexpr double doublingDb[ChainResult::numBands] = { 5.0, 5.5, 6.4, 6.9 };

    // Relevance of each system, following the brief: a cinema subwoofer on time
    // and bass-managed home systems count fully, a late subwoofer counts half,
    // an early one a quarter. Both
    // polarities count alike. How often each fault occurs in practice is not
    // published, so these are engineering assumptions, kept in one place.
    static constexpr double delaySteps[5] = { -5.0, -2.5, 0.0, 2.5, 5.0 };
    static constexpr double delayRelevance[5] = { 0.25, 0.25, 1.0, 0.5, 0.5 };
    static constexpr double homeRelevance = 1.0;

    // With all the bass in the LFE the score stops at about 22: bad, since the
    // room alone then decides the bass, but not as bad as a cancellation.
    static constexpr double dependenceCeiling = 0.72;

    Translation compute (const Analysis&) const;
};

} // namespace subguard
