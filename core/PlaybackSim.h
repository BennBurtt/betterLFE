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
//   Stereo         the mix with its LFE left out, as in the Dolby and ITU-R
//                  BS.775 two-channel downmixes. Only the LFE is removed: the
//                  screen channels stay summed as in the reference.
//
// FROM dB TO PERCEPTION
//   The difference between a system and the reference is taken per band, in dB.
//   ISO 226:2023 gives how many dB double the loudness of a low tone around
//   80 phon: 5.0 dB at 25 Hz, 5.5 dB at 40 Hz, 6.4 dB at 70 Hz, 6.9 dB at 100 Hz
//   (10 dB at 1 kHz). Each band is then rated
//
//     penalty = clamp ((|difference| - T) / (doubling - T), 0, 1)
//
//   0 up to the tolerance T, 1 when the band is heard twice as loud or half as
//   loud. T follows the chosen level: 3 dB (Soft), 2 dB (Medium), 1 dB (Strong,
//   about the smallest audible level difference). Bands are combined by their share
//   of the bass.
//
// SCORE
//   Two risks, multiplied:
//
//     interaction = the highest (relevance x penalty) among the cinema and
//                   bass-managed systems. The worst case, not an average: a
//                   mix that cancels on one plausible system is not made safe
//                   by the systems where it survives.
//     dependence  = 1.0 x the penalty of the stereo downmix without LFE, i.e.
//                   of what the bass loses wherever the LFE is missing or
//                   wrongly set. Infra-bass is left out of it: below 25 Hz
//                   it counts for a quarter, from 60 Hz up it counts fully.
//
//     robustness = 100 . (1 - interaction) . (1 - dependence) . (1 - 0.20 level dependence)
//     (the weights are the Tuning defaults below, adjustable in the Dev panel)
//
//   Anchors: screen alone 100; LFE identical to the screen 0 (an inverted
//   subwoofer cancels it); LFE alone about 22 (the room alone decides the
//   bass); 50 when the worst on-time system moves the bass half-way to a
//   doubling or halving of loudness.
//
// LEVEL DEPENDENCE (a warning, and up to 15 % off the score)
//   The same systems with the subwoofer 1, 2, 3 and 6 dB too loud or too
//   quiet. With an LFE unrelated to the screen the bass follows the power
//   sum: that is expected, and so is a bass 1 dB up when all of it is in an
//   LFE played 1 dB up. What is reported is the difference from that
//   expectation, band by band: a cancellation that sets in, or a boost out
//   of one.
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
    enum class Kind { cinema, bassManaged, stereo, other };

    Kind kind = Kind::cinema;
    std::string name;
    double delayMs = 0.0;       // cinema: subwoofer offset
    bool inverted = false;      // cinema: subwoofer polarity
    double crossoverHz = 0.0;   // bass managed
    bool encoderFilter = false; // bass managed: LFE through the encoder low-pass model
    int other = -1;             // other systems: which (PlaybackSimulator::OtherSystem)

    static constexpr int numBands = 4;
    double changeDb = 0.0;                          // bass against the reference, 20-120 Hz (all bands together)
    double minChangeDb = 0.0, maxChangeDb = 0.0;    // the band that changes most down and most up
    std::array<double, numBands> bandRated {};      // other systems: each band's own rating, 0 .. 1 (before its weight)
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
    std::string worstRoom;              // set when a ticked Room Sim room is that worst case instead

    // Level dependence: what happens to the bass when the subwoofer is not at
    // its calibrated level. Against an LFE unrelated to the screen, whose
    // bass simply follows the power sum, how much more (a boost out of a
    // cancellation) or less (a cancellation setting in) the bass moves.
    // Loudness of the reference bass in each rating band, at the chosen
    // listening level (ISO 226:2023): its level in phon, and how many dB
    // double it there. This is what turns a level change into a sensation.
    std::array<double, ChainResult::numBands> bandSplDb {}, bandPhon {}, bandDoublingDb {};

    // Share of listeners who hear the reference bass of each band, 0..1 (see
    // PlaybackSimulator::audibleShare): 1 when audibility is switched off.
    std::array<double, ChainResult::numBands> bandAudible { { 1.0, 1.0, 1.0, 1.0 } };

    // Audibility, point by point: the reference's bass at each grid point of a
    // band (its weight in the band, its frequency, and its level summed over
    // one auditory filter around it, dB SPL). A band's audibility is the share
    // of its power above threshold, each point against the threshold at its
    // own frequency: between 31.5 and 63 Hz the threshold falls by about 22 dB,
    // too much for one value per band.
    struct AudibilityPoint { double weight, hz, levelDb; };
    std::array<std::vector<AudibilityPoint>, ChainResult::numBands> audibilityProfile;

    /** Share of a band's bass heard when its level moves by `shiftDb` from the reference (1 when audibility is off). */
    double audibleAt (int band, double shiftDb, double spreadDb, bool enabled) const;

    double levelDependence = 0.0;       // 0..1, drives the warning and the score: the mean against the "red" value
    double levelMeanDb = 0.0;           // mean of that difference over every system and subwoofer level, in dB
    double levelDeviationDb = 0.0;      // the largest one, signed: negative when the bass falls short
    int levelScenarios = 0;             // how many system x subwoofer-level cases were rated

    // LFE as heard (+10 dB) against the other channels (screen, surrounds and heights), 20-120 Hz
    double lfeBalanceDb = 0.0;          // positive: the LFE is the louder of the two
    double lfeShare = 0.0;              // 0..1, part of the bass energy carried by the LFE

    // Surrounds and heights (summed): their bass against the screen, and their share of all the bass
    bool hasSurround = false;           // they carry bass worth speaking of (over 1 % of the screen's)
    double surroundBalanceDb = -60.0, surroundShare = 0.0;
    double downmixChangeDb = 0.0;       // bass left in the stereo downmix, against the reference

    // LFE (as heard) and screen sum both active in the same band
    struct Overlap
    {
        double screenDb = -200.0, lfeDb = -200.0;   // RMS level in the band, dBFS
        double amount = 0.0;                        // 0..1; 1: equal levels
    };

    std::array<Overlap, 3> overlap;

    // Bass of the screen channels in opposition of phase: what any sum of
    // them loses (subwoofers fed by L + R, mono, a phone), against the same
    // channels with their cancellations left out (in-phase sums kept). Per
    // band, dB (0 or less); the worst band, its centre frequency, and the
    // pair behind it (0: L / R, 1: L / C, 2: C / R, 3: screen / surrounds and heights).
    std::array<double, ChainResult::numBands> antiphaseDb {};
    double antiphaseWorstDb = 0.0, antiphaseWorstHz = 0.0;
    int antiphasePair = -1;

    // Other systems (venues, not cinemas), rated electrically against the mix
    // with its cancellations left out, so that an opposition of phase counts.
    // They count in the score only when ticked in Room Sim.
    std::vector<ChainResult> others;
};

class PlaybackSimulator
{
public:
    static constexpr double lfeInBandGainDb = 10.0;     // SMPTE RP 200
    static constexpr double changeLimitDb = 24.0;       // larger differences all count the same
    // How strict the rating is: the level difference a band may show before it
    // starts to count. Strong starts at 1 dB, about the smallest audible
    // change, so a perfect score is exceptional on real material. Medium
    // (2 dB) and Soft (3 dB) let moderate changes pass.
    enum class Level { soft, medium, strong };

    static constexpr double toleranceDb (Level level)
    {
        return level == Level::soft ? 3.0 : (level == Level::medium ? 2.0 : 1.0);
    }

    // Correlation from which the phase alert fires, for the same three levels
    static constexpr double alertThreshold (Level level)
    {
        return level == Level::soft ? 0.90 : (level == Level::medium ? 0.75 : 0.50);
    }
    static constexpr double overlapWindowDb = 10.0;     // levels further apart than this do not overlap
    // Opposition of phase on the screen: a loss beyond this, when the screen is summed, lights its corner
    // (hypothesis: half the power, about the step at which a change of bass level is plainly heard)
    static constexpr double antiphaseWarningDb = 3.0;
    static constexpr double balanceWarningDb = 6.0;     // LFE this far above the screen: the bass then follows each room's subwoofer level

    // What losing the LFE costs in a stereo downmix depends on frequency. Below
    // about 25 Hz a tone is close to the threshold of hearing (78.1 dB SPL at
    // 20 Hz, 68.7 dB at 25 Hz in ISO 226) and no television, soundbar or small
    // loudspeaker reproduces it: it is felt in a cinema, and its absence
    // changes little in the downmix: it counts for a quarter. From about 60 Hz up (threshold 37.5 dB at
    // 63 Hz; a television that reaches 60 Hz is already a good one) it counts
    // fully. In between, the weight rises linearly in octaves.
    static constexpr double infraLoHz = 25.0, infraHiHz = 40.0;
    static constexpr double infraWeight = 0.30;         // what the infra-bass still counts for: felt, and missed, but not heard

    // The level dependence takes a little off the score: at most this share
    static constexpr double levelDependenceWeight = 0.20;

    // Rating bands and, for each, the level change that doubles loudness
    // (ISO 226:2023 around 80 phon, at 25, 40, 70 and 100 Hz).
    static constexpr double bandLimits[ChainResult::numBands + 1] = { 20.0, 30.0, 60.0, 90.0, 120.0 };
    static constexpr double doublingDb[ChainResult::numBands] = { 5.0, 5.5, 6.4, 6.9 };

    // Relevance of each system, following the brief: a cinema subwoofer on time
    // and bass-managed home systems count fully, a late subwoofer counts half,
    // an early one a quarter. Both
    // polarities count alike. How often each fault occurs in practice is not
    // published, so these are engineering assumptions, kept in one place.
    static constexpr double delaySteps[5] = { -5.0, -2.5, 0.0, 2.5, 5.0 };
    static constexpr double delayRelevance[5] = { 0.5, 0.5, 1.0, 0.5, 0.5 };
    static constexpr double homeRelevance = 1.0;

    // Level dependence. Each cinema and bass-managed system is tried with its
    // subwoofer 1, 2, 3 and 6 dB too loud and too quiet. Too quiet matters as
    // well: with an LFE louder than the screen and against it, turning the
    // subwoofer down brings the two closer to cancelling. A difference of
    // levelRedDb from what an unrelated LFE would give is a full warning; one
    // that only shows 6 dB off is kept faint.
    static constexpr int numLevelSteps = 8;
    static constexpr double levelSteps[numLevelSteps] = { -6.0, -3.0, -2.0, -1.0, 1.0, 2.0, 3.0, 6.0 };
    static constexpr double levelStepWeight[numLevelSteps] = { 0.3, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.3 };
    static constexpr double levelRedDb = 3.0;
    static constexpr double levelBandShare = 0.2;       // bands holding less of the bass than this are not looked at

    // With all the bass in the LFE, spread over 20-120 Hz, the score stops at
    // about 22: bad, since the room alone then decides the bass, but not as bad
    // as a cancellation. (The infra-bass part of it is not counted.)
    static constexpr double dependenceCeiling = 1.0;

    static constexpr double speedOfSound = 343.0;       // m/s: a subwoofer 1.7 m further away is 5 ms late

    /** Everything that is a choice rather than a measurement, adjustable at run time (Dev panel). */
    struct Tuning
    {
        std::array<double, 3> toleranceDb { { 3.0, 2.0, 1.0 } };    // Soft, Medium, Strong
        double lateRelevance = 0.5, earlyRelevance = 0.5;           // cinema subwoofer late / early, against 1 on time
        double dependenceCeiling = PlaybackSimulator::dependenceCeiling;
        double infraLoHz = PlaybackSimulator::infraLoHz, infraHiHz = PlaybackSimulator::infraHiHz;
        double infraWeight = PlaybackSimulator::infraWeight;
        double levelDependenceWeight = PlaybackSimulator::levelDependenceWeight;
        double levelRedDb = PlaybackSimulator::levelRedDb;

        // Room Sim and Room Sim Pro: a room's score is the mean seat score,
        // pulled down by its worst seats: (1 - w) x mean + w x mean of the
        // worst share of seats. A few bad seats in a good room then cost it
        // 10 to 30 points instead of 1 or 2.
        double roomWorstWeight = 0.25, roomWorstShare = 0.10;

        // Room Sim Pro: how much the bass level at each seat against the stage
        // (coverage: holes, back rows losing the bass, boosts) counts next to
        // the interaction: seat penalty = 1 - (1 - interaction) (1 - w x coverage)
        double coverageWeight = 0.5;

        // Room Sim and Room Sim Pro: a dip is heard less than a peak of the same
        // size. A dip of a band counts as a change smaller by this share of the
        // published difference between the two detection thresholds (see
        // RoomSimulator::dipAllowanceDb). 0: dips count as peaks.
        double dipAllowance = 1.0;

        // Listening level: the sound pressure level, in dB(C), that a screen
        // channel gives with the reference signal at -20 dBFS RMS. 85 is the
        // cinema calibration (SMPTE RP 200); dubbing stages and smaller
        // rooms work between 76 and 85.
        double listeningLevelDbC = 85.0;

        // Audibility: bass under the threshold of hearing at the listening
        // level counts for nothing; around the threshold it counts for the
        // share of listeners who hear it.
        bool audibility = true;
        double thresholdSpreadDb = 5.0;     // spread (1 sigma) of individual thresholds at low frequencies
    };

    static constexpr double referenceDbfs = -20.0;      // RMS level of the calibration signal
    static constexpr double bandCentreHz[ChainResult::numBands] = { 25.0, 40.0, 70.0, 100.0 };

    /** ISO 226:2023: sound pressure level of a pure tone at `hz` (20..125) heard at `phon`, and the reverse. */
    static double iso226Spl (double hz, double phon);
    static double iso226Phon (double hz, double splDb);

    /** ISO 226:2023 threshold of hearing (Tf, as in ISO 389-7:2019), free field, binaural, 18-25 years: the median listener. */
    static double hearingThresholdDb (double hz);

    /** Share of listeners for whom a band at `splDb`, centred on `hz`, is above threshold:
        thresholds spread normally around the ISO 226 value with `spreadDb` (1 sigma). */
    static double audibleShare (double hz, double splDb, double spreadDb);

    Tuning tuning;

    /** `lfeGainDb`: how much louder than a screen channel the LFE is played, 10 in a cinema, 0 to take it as recorded. */
    Translation compute (const Analysis&, Level level = Level::strong, double lfeGainDb = lfeInBandGainDb) const;

    /** Other systems: how venues often play a mix (electrical, no room). */
    enum OtherSystem { stereoPaNoLfe, surroundPaWithLfe, smallSpeakers, monoLtRt, headphones, tvSpeakers, laptop, numOtherSystems };
    static const char* otherSystemName (int);           // English, "Other system: ..."
    static const char* otherSystemDescription (int);
    static const char* otherSystemShortName (int);      // for a tick box

private:
    void computeOthers (const Analysis&, Translation&, Level, double lfeGain, const double* approved, double approvedTotal,
                        const double* doubling) const;

public:
};

} // namespace subguard
