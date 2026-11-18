// Transient engine: finds the attacks of the mix and tells whether each one is
// also in the LFE, and if so how the LFE lines up with it. Portable C++17.
//
// The reference is the stereo downmix of the screen channels (Lo + Ro, i.e.
// L + R + 1.414 C): what every listener hears, whatever the system. The same
// detectors also run on L - R: an attack in L and R in opposite polarity
// cancels in the downmix but not there (any screen content shows in one of
// the two). Such an attack is analysed against L - R.
//
// 1. ATTACKS OF THE MIX, FULL BAND. The downmix is analysed at the input rate
//    over its whole band. An attack is declared when the
//    log-energy envelope of a low band (10-40, 40-150, 150-600 Hz) rises above its recent level (relative
//    difference, Klapuri 1999) by more than a threshold that adapts to how
//    much that band usually fluctuates (Bello et al. 2005): this catches a
//    boom that has no click. Next to it, the peak level of the signal above
//    600 Hz and above 8 kHz is followed in 1 ms blocks, and an attack is
//    declared when the peak of the last 12 ms jumps above that of the 36 ms
//    before: the sub-block peak and high-frequency energy methods of audio
//    coders (ATSC A/52; Fan, Kuo, Shah & Bosi, AES 155, 2023) and the
//    valley-to-peak test of T. Darwen's AudioLib. Comparing with what
//    immediately precedes, not with a long average, is what finds a hi-hat
//    a quarter of a second after a kick.
//
// 2. IS IT IN THE LFE TOO? The same detector runs on the LFE in its own
//    range. An attack of the mix counts as present in the LFE when the LFE
//    has an attack of its own close to it, or when its energy rises there
//    while matching the mix.
//
// 3. HOW DOES THE LFE LINE UP? The two signals do not have the same
//    bandwidth, so they are first brought to the same one: the downmix is
//    taken through the very filter that limits the LFE analysis (below
//    190 Hz). Comparing two observations of one event only after giving
//    them the same response is the principle of proper image subtraction
//    and of its extension to shifted sources (Zackay et al. 2016; Springer
//    et al. 2024, "Translient"), transposed here from position to time.
//    The same analysis is run on the stretch just before the attack and
//    subtracted (new minus reference, as in image subtraction): what was
//    already sounding, a held bass note for instance, cancels out, and what
//    remains belongs to the attack alone.
//    The cross-correlation (Knapp & Carter 1976) then gives the time
//    offset from the peak of its envelope and the polarity from its phase,
//    and the Cramer-Rao bound (Carter 1987) gives the precision of that
//    offset, so that none is claimed when none can be measured.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <vector>

namespace subguard
{

struct TransientEvent
{
    bool inLfe = false;         // the attack of the mix is also in the LFE; the rest only means something then
    double searchMs = 0.0;      // the search range this event was measured with
    double delayMs = 0.0;       // offset of the LFE against the mix; positive: LFE late
    double delayUncertaintyMs = 0.0;   // 1-sigma precision of delayMs (Cramer-Rao bound)
    bool delayAtLimit = false;  // the best match sits at the edge of the search range
    bool lfeAttack = false;     // the LFE has an attack of its own in the search range: attackDelayMs is valid
    double attackDelayMs = 0.0; // start of the LFE attack against the start of the mix attack; coarse (a few ms), needs no likeness
    double match = 0.0;         // 0..1, likeness of the two transients once re-aligned
    double phase = 0.0;         // radians at the best alignment; 0 same polarity, pi inverted
    double correlationNow = 0.0;    // -1..1, correlation as it is, without re-aligning; level independent
    double interactionBest = 0.0;   // 0..1, like match but weighted by the level balance of the pair
    double interactionNow = 0.0;    // -1..1, like correlationNow, level weighted (sum changes by 10 log10 (1 + s) dB)
    double levelDb = -150.0;    // RMS level of the low part of the mix transient, dBFS
};

/** What the user can tune. */
struct TransientSettings
{
    bool enabled = true;            // off: nothing is analysed and no event comes out
    double searchMs = 250.0;        // the LFE is looked for up to this far before or after the attack of the mix (5..500)

    // Detection profile, the four controls of a transient shaper, in percent.
    double attack = 40.0;           // how fast a rise must be: the peak is watched over 3 ms (0 %) to 100 ms (100 %); 40 %: 12 ms
    double hold = 25.0;             // after an attack, no new one for 30 ms (0 %) to 510 ms (100 %); 25 %: 150 ms
    double release = 50.0;          // the tail of a hit hides what follows: time constant 20 ms (0 %) to 2 s (100 %); 50 %: 200 ms
    double sensitivity = 70.0;      // how small a rise counts: 70 % asks for 6 dB, 100 % for 2.6 dB, 40 % for 14 dB

    static double attackMs (double percent)     { return 3.0 * std::pow (100.0 / 3.0, std::clamp (percent, 0.0, 100.0) / 100.0); }
    static double holdMs (double percent)       { return 30.0 + 4.8 * std::clamp (percent, 0.0, 100.0); }
    static double releaseMs (double percent)    { return 20.0 * std::pow (100.0, std::clamp (percent, 0.0, 100.0) / 100.0); }
    static double riseDb (double percent)       { return 6.0 * std::pow (2.0, (70.0 - std::clamp (percent, 0.0, 100.0)) / 25.0); }
    static double roughnessScale (double percent)   { return std::pow (2.0, (70.0 - std::clamp (percent, 0.0, 100.0)) / 35.0); }

    double minMatch = 0.3;          // 0..1: likeness under which the LFE is said to carry something else than the mix
};

/** Multi-band attack detector on one signal. */
class OnsetBank
{
public:
    /** `hop`: the decision is taken every `hop` samples of the signal. */
    void prepare (double sampleRate, int hop, const std::vector<std::array<double, 2>>& bandEdgesHz);
    void reset();
    void setThreshold (double riseDb, double roughnessFactor)   { minimumRise = riseDb; factor = roughnessFactor; }
    void setTiming (double holdSeconds, double releaseSeconds);

    /** Feeds one sample. Returns true when an attack has just been confirmed;
        `samplesAgo` then tells how far back it started. */
    bool process (double sample, int& samplesAgo);

private:
    struct Biquad { double b0, b1, b2, a1, a2; };

    struct Band
    {
        Biquad hp {}, lp {};
        double envelopeCoeff = 0.0;
        double state[4] = {};
        double envelope = 0.0, level = 0.0, roughness = 0.0, held = -300.0;
        std::array<double, 64> recent {};
        bool primed = false;
    };

    static Biquad highPass (double hz, double sampleRate);
    static Biquad lowPass (double hz, double sampleRate);

    std::vector<Band> bands;
    int hopSize = 1, hopCounter = 0, roughnessLag = 1, freezeHops = 0, refractoryHops = 0, confirmHops = 2;
    double slowCoeff = 0.0, roughnessCoeff = 0.0, minimumRise = 6.0, factor = 5.5;
    double decisionRate = 1000.0, dropPerHop = 0.05;
    int heldRiseHops = 60;
    long long evaluation = 0, lastOnset = -1000000;
    int run = 0;
    bool armed = true;
};

/** Attack detector on the peak level of a signal, in 1 ms blocks: the peak of
    the last 12 ms against the peak of the 12 ms before. */
class PeakOnset
{
public:
    /** `highPassHz` 0: the signal as it is. Returns false when the sample rate is too low for that filter. */
    bool prepare (double sampleRate, double highPassHz);
    void reset();
    bool process (double sample, int& samplesAgo);
    void setThreshold (double riseDb, double roughnessFactor)   { minimumRise = riseDb; factor = roughnessFactor; }
    void setTiming (double windowSeconds, double holdSeconds, double releaseSeconds);

private:
    double minimumRise = 6.0, factor = 5.0;
    int span = 12;                                      // 1 ms blocks: the window that is watched...
    static constexpr int lookBack = 36, ringSize = 256; // ...and what it is compared with
    double heldPeak = 0.0, heldDecay = 0.995, blockRate = 1000.0;
    struct Biquad { double b0, b1, b2, a1, a2; } filter {};
    bool filtered = false, enabled = false;
    double state[4] = {};
    std::array<double, ringSize> peaks {};
    double blockPeak = 0.0, roughness = 0.0, roughnessCoeff = 0.0;
    int blockSize = 48, blockCounter = 0, freezeBlocks = 0, refractoryBlocks = 0;
    long long block = 0, lastOnset = -1000000;
    bool armed = true;
};

class TransientDetector
{
public:
    TransientDetector (double inputSampleRate, int decimationFactor);

    void reset();

    /** Linear gains applied before comparing levels; a gain of 0 mutes a screen channel. */
    void setGains (const std::array<double, 3>& screenGains, double lfeGain);

    /** Feeds one frame (L, C, R, LFE) at the input rate: full-band attacks of the mix. */
    void processInput (const float* frame);

    /** Feeds one frame (L, C, R, LFE) of the decimated signal: LFE attacks and alignment. */
    void processDecimated (const double* frame);

    /** Events found since the last call, one per attack of the mix. */
    std::vector<TransientEvent> takeEvents();

    void setSettings (const TransientSettings&);
    const TransientSettings& getSettings() const    { return settings; }

private:
    void analyse (long long onsetIndex);
    double history (int which, long long index) const;

    TransientSettings settings;
    double fs;              // decimated rate
    int factor;
    int preSamples, segmentLength, maxLag;

    void mixAttack (long long inputSample);

    OnsetBank mixOnsets, lfeOnsets, sideOnsets;
    PeakOnset mixPeaks, mixHighPeaks, sidePeaks, sideHighPeaks;
    double mixEnergy = 0.0, sideEnergy = 0.0, energyCoefficient = 0.0;
    int mixSource = 0;                              // ring the attack being analysed is read from: 0 downmix, 2 L - R
    long long lastMixAttack = -1000000, mixRefractory = 0;
    long long inputIndex = 0, sampleIndex = 0;      // input and decimated sample counters
    std::deque<long long> pendingMix, recentMix, recentLfe;    // decimated indices

    struct HighPass { double b0, b1, b2, a1, a2; } inputHighPass {};
    std::array<std::array<double, 2>, 3> filterState {};

    static constexpr int historySize = 1024;       // 2 s at the decimated rate
    std::array<std::vector<double>, 3> ring;        // 0: downmix, 1: LFE, 2: L - R (decimated)

    std::array<double, 3> screenGain { { 1.0, 1.0, 1.0 } };
    double lfeGainLinear = 1.0;

    std::vector<double> window;
    std::vector<TransientEvent> events;
};

} // namespace subguard
