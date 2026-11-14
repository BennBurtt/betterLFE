// Playback-chain simulation: what the bass of the mix becomes on different
// reproduction systems. Portable C++17, no dependencies.
//
// Every chain is a pair of filters, one applied to F (L+C+R) and one to the
// LFE, before the two are summed. The power of that sum follows from the
// smoothed auto and cross spectra the engine already keeps, so nothing is
// re-filtered:
//
//   P = |Gf|^2 Sff + |Gl|^2 Sll + 2 Re (Gf . conj(Gl) . Sfl)
//
// It is compared with the power sum (the same expression without the cross
// terms), i.e. what the bass would be if the groups did not interact. A result
// several dB below it is a cancellation.
//
// Chains
//   Cinema         LFE 10 dB above a screen channel, summed acoustically with
//                  the screen; subwoofer offset -5..+5 ms, either polarity.
//   Bass managed   Home system: screen channels crossed over to the subwoofer
//                  (4th-order Linkwitz-Riley at 60, 80 or 100 Hz), LFE low-passed
//                  at 120 Hz and added 10 dB up. The sum is electrical.
//   Encoder        Same, with a steep 120 Hz low-pass on the LFE standing in for
//                  the LFE filter of a Dolby Digital Plus encoder (a model: the
//                  actual filter is not public).
//
// Cross terms that chance alone could produce are removed first (faded out
// between 3 and 2 sigma on the coherence), so unrelated signals score as
// perfectly robust instead of slightly fragile.

#pragma once

#include "LfeEngine.h"

#include <array>
#include <string>
#include <vector>

namespace subguard
{

struct ChainResult
{
    enum class Kind { cinema, bassManaged };

    Kind kind = Kind::cinema;
    std::string name;
    double delayMs = 0.0;       // cinema: subwoofer offset
    bool inverted = false;      // cinema: subwoofer polarity
    double crossoverHz = 0.0;   // home
    bool encoderFilter = false; // home: LFE through the encoder low-pass model

    double changeDb = 0.0;                  // bass of this chain against the power sum, 20-120 Hz
    double score = 100.0;                   // 0..100: how close that is to 0 dB
    double weight = 1.0;                    // its share in the robustness score
    std::array<double, 3> bandChangeDb {};  // same for 30-60, 60-90, 90-120 Hz
};

struct Translation
{
    static constexpr double bandEdges[4] = { 30.0, 60.0, 90.0, 120.0 };

    bool hasBass = false;               // false: silence, nothing below is meaningful

    std::vector<ChainResult> chains;
    double worstChangeDb = 0.0;         // deepest loss among the chains
    double spreadDb = 0.0;              // how much the bass level moves between chains
    double robustness = 100.0;          // 0..100: weighted average of the chain scores

    // Stereo downmix of L, C, R without the LFE: bass left, against the full mix
    double downmixChangeDb = 0.0;
    double lfeShare = 0.0;              // 0..1, part of the bass energy carried by the LFE
    double downmixScore = 100.0;        // its score in the robustness average

    // LFE (as heard, +10 dB) and screen sum both active in the same band
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
    static constexpr double lfeInBandGainDb = 10.0;
    static constexpr double lossFloorDb = -24.0;        // deeper cancellations all count the same
    static constexpr double scoreZeroDb = 6.0;          // a chain this far from the power sum scores 0
    static constexpr double nominalWeight = 2.0;        // cinema chains with the subwoofer on time
    static constexpr double earlyWeight = 0.5;          // cinema chains with the subwoofer early
    static constexpr double downmixWeight = 2.0;        // stereo downmix without the LFE
    static constexpr double overlapWindowDb = 10.0;     // levels further apart than this do not overlap

    Translation compute (const Analysis&) const;
};

} // namespace subguard
