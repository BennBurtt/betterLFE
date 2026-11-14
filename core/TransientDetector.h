// Transient detector: finds attacks in the screen channels (the reference) and
// measures how the LFE lines up with each of them. Portable C++17.
//
// 1. Onset detection on each screen channel, in several frequency bands so
//    that a 25 Hz boom is found under a louder 100 Hz bed. In each band the
//    log-energy envelope is compared with its recent level (relative
//    difference, Klapuri 1999), against a threshold that adapts to how much
//    that band normally fluctuates (adaptive thresholding, Bello et al. 2005).
//    Working on log energy makes the detection independent of level: a
//    transient at -90 dBFS is found like one at -10 dBFS.
// 2. For each onset, the cross-correlation between the screen channel and
//    the LFE over +/- 50 ms (Knapp & Carter 1976, unweighted generalized
//    cross-correlation). The peak of its envelope gives the time offset of
//    the LFE; the value at the peak tells how alike the two transients are
//    and with which polarity.
// 3. The precision of that offset, from the Cramer-Rao bound for time-delay
//    estimation (Carter 1987): it depends on the bandwidth the two signals
//    share, on how alike they are and on the length of the segment. A
//    narrow-band or poorly matched pair gets a large uncertainty, so that no
//    offset is claimed when none can be measured.
//
// It runs on the decimated signal (~500 Hz), i.e. on the part of the screen
// channels that actually shares a frequency range with the LFE.

#pragma once

#include <array>
#include <vector>

namespace subguard
{

struct TransientEvent
{
    int channel = 0;            // 0 L, 1 C, 2 R: the screen channel that had the attack
    double delayMs = 0.0;       // offset of the LFE against that channel; positive: LFE late
    double delayUncertaintyMs = 0.0;   // 1-sigma precision of delayMs (Cramer-Rao bound)
    bool delayAtLimit = false;  // the best match sits at the edge of the +/- 50 ms search
    double match = 0.0;         // 0..1, likeness of the two transients once re-aligned
    double phase = 0.0;         // radians at the best alignment; 0 same polarity, pi inverted
    double correlationNow = 0.0;    // -1..1, correlation as it is, without re-aligning; level independent
    double interactionBest = 0.0;   // 0..1, like match but weighted by the level balance of the pair
    double interactionNow = 0.0;    // -1..1, like correlationNow, level weighted (sum changes by 10 log10 (1 + s) dB)
    double levelDb = -150.0;    // RMS level of the screen-channel transient, dBFS
};

class TransientDetector
{
public:
    explicit TransientDetector (double sampleRate);

    void reset();

    /** Linear gains applied before comparing levels; a gain of 0 mutes a screen channel. */
    void setGains (const std::array<double, 3>& screenGains, double lfeGain);

    /** Feeds one frame (L, C, R, LFE) of the decimated signal. */
    void process (const double* frame);

    /** Events found since the last call. */
    std::vector<TransientEvent> takeEvents();

    static constexpr double maxLagSeconds = 0.050;

private:
    void analyse (int channel, long long onsetIndex);
    double history (int channel, long long index) const;

    double fs;
    int preSamples, segmentLength, maxLag, refractory, roughnessLag, freezeSamples;
    double slowCoeff, roughnessCoeff;

    struct Biquad { double b0, b1, b2, a1, a2; };
    static Biquad highPass (double hz, double sampleRate);
    static Biquad lowPass (double hz, double sampleRate);

    Biquad inputHighPass {};
    std::array<std::array<double, 2>, 4> filterState {};

    static constexpr int historySize = 1024;
    std::array<std::vector<double>, 4> ring;
    long long sampleIndex = 0;

    static constexpr int numBands = 5;
    static constexpr int logHistory = 64;

    struct Band
    {
        Biquad hp {}, lp {};
        double envelopeCoeff = 0.0;
    };

    std::array<Band, numBands> bands;

    struct BandState
    {
        std::array<double, 4> filter {};
        double envelope = 0.0, level = 0.0, roughness = 0.0;
        std::array<double, logHistory> recent {};
        bool primed = false;
    };

    struct ChannelState
    {
        std::array<BandState, numBands> band;
        bool armed = true;
        int run = 0;
        long long lastOnset = -1000000;
        std::vector<long long> pending;
    };

    std::array<ChannelState, 3> channels;
    std::array<double, 3> screenGain { { 1.0, 1.0, 1.0 } };
    double lfeGainLinear = 1.0;

    std::vector<double> window;
    std::vector<TransientEvent> events;
};

} // namespace subguard
