// LFE vs screen-channel (L/C/R) analysis engine. Portable C++17, no dependencies.
//
// Only 10-160 Hz matters, so the audio is low-pass filtered and decimated
// (48 kHz -> ~500 Hz) by a streaming filter: every input sample is processed
// exactly once. All spectral work then happens on a few hundred samples per
// channel, which gives sub-Hz frequency resolution at negligible CPU cost.
//
// The phase relation between two channels cannot be read from one FFT frame
// (a single frame always looks 100 % coherent). The engine keeps time-averaged
// auto and cross spectra, smoothed over 1/3 octave, and derives:
//
//   correlation  c  = Re(Sxy) / sqrt(Sxx Syy)      in [-1, +1]
//   interaction  s  = 2 Re(Sxy) / (Sxx + Syy)     in [-1, +1]
//   coherence    g2 = |Sxy|^2 / (Sxx Syy)          in [0, 1]
//   phase        ph = arg(Sxy)
//   sigma           = how far s can stray from 0 by pure chance, given the
//                     bandwidth and the amount of audio averaged so far
//
// s is what the sum X + LFE does compared to a plain power sum: its level
// changes by 10*log10(1 + s) dB. +1: in phase at equal level, -1: full
// cancellation, 0: no interaction (decorrelated, in quadrature, or one of the
// two much quieter than the other).

#pragma once

#include "TransientDetector.h"

#include <array>
#include <complex>
#include <vector>

namespace subguard
{

enum Channel { chL = 0, chC, chR, chLFE, numChannels };

// Index of the L+C+R sum in the per-pair results (after L, C, R).
constexpr int pairFacade = 3;
constexpr int numPairs = 4;

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

/** Chebyshev type II low-pass as second-order sections (even order only). */
std::vector<Biquad> designCheby2Lowpass (int order, double stopDb, double stopHz, double sampleRate);

/** Streaming anti-alias low-pass + decimation; state is kept across blocks. */
class Decimator
{
public:
    explicit Decimator (double sampleRate);

    void reset();
    double outputRate() const   { return fsOut; }
    int factor() const          { return ratio; }

    /** Magnitude response of the anti-alias filter at a frequency in Hz. */
    double response (double hz) const;

    /** Feeds one input frame. Returns true when `out` received a decimated frame. */
    bool process (const float* frame, double* out);

private:
    double fsIn, fsOut;
    int ratio, phase = 0;
    std::vector<Biquad> sections;
    std::vector<std::array<double, 2 * numChannels>> state;
};

struct Settings
{
    std::array<bool, 3> mute { { false, false, false } };   // L, C, R
    double lfeGainDb = 0.0;      // e.g. +10 for the cinema in-band gain
    double tauSpectrum = 0.15;   // averaging of the displayed spectra (s)
    double tauCorrelation = 2.0; // averaging of the correlation statistics (s)
};

struct Analysis
{
    std::vector<double> freqs;       // fine frequency axis of the spectra (Hz)
    std::vector<double> facadeDb;    // L+C+R sum, dBFS (a sine reads its peak level)
    std::vector<double> lfeDb;       // LFE, dBFS
    std::vector<double> grid;        // log frequency axis of what follows (Hz)

    // Indexed [pair][grid point]; pairs are L, C, R, facade, each against the LFE.
    // Smoothed auto and cross spectra of the two groups that playback chains
    // treat differently: F = L+C+R and the LFE, on the grid above and at unity
    // gain (no display trim, no LFE offset). cFLfe is the average of
    // F.conj(LFE). With these, the power of any filtered sum of the two can be
    // worked out without touching the audio again.
    struct Matrix
    {
        std::vector<double> pF, pLfe, pL, pC, pR, reLC, reRC;
        std::vector<std::complex<double>> cFLfe;
    } matrix;

    double windowSeconds = 1.0;     // length of the analysis window

    // correlation is level independent: it reads the same however quiet the LFE
    // is. interaction is the same quantity weighted by the level balance.
    std::array<std::vector<double>, numPairs> correlation, interaction, coherence, phase, gate, sigma;
};

class LfeEngine
{
public:
    explicit LfeEngine (double sampleRate, double windowSeconds = 1.0, double facadeTrimDb = -3.0);

    /** Rebuilds everything that depends on the analysis window length. */
    void configure (double windowSeconds);

    /** Clears all audio and statistics. */
    void reset();

    /** Feeds interleaved audio frames ordered L, C, R, LFE. */
    void push (const float* interleaved, int numFrames);

    /** Updates the averages with the audio pushed since the last call.
        Returns false (result unchanged) when nothing new arrived. */
    bool analyze (const Settings&);

    const Analysis& result() const  { return out; }

    /** Attacks found in the screen channels since the last call, each with the
        way the LFE lines up with it. Independent of analyze(). */
    std::vector<TransientEvent> takeTransientEvents()   { return transients.takeEvents(); }

    /** LFE content above 120 Hz relative to the whole LFE, in dB (about 1 s average). */
    double lfeOutOfBandDb() const;

    /** Broadband RMS level of the LFE, dBFS (about 1 s average). */
    double lfeLevelDb() const;

    /** Energy-weighted correlation of a pair (L, C, R, facade) with the LFE over
        20-120 Hz, accumulated since the last resetGlobal(). */
    double globalCorrelation (int pair) const;
    double globalSeconds() const    { return globalTime; }
    void resetGlobal();

    double decimatedRate() const    { return fs; }
    double windowSeconds() const    { return windowSec; }

private:
    void fft();

    Decimator decimator;
    double fs, facadeGain, windowSec = 1.0;

    struct Section { double b0, b1, b2, a1, a2, z1 = 0.0, z2 = 0.0; };
    std::array<Section, 2> outOfBandFilter {};
    double outOfBandCoeff = 0.0, outOfBandPower = 0.0, lfePower = 0.0;

    std::array<double, numPairs> globalRe {}, globalPx {}, globalPl {};
    double globalTime = 0.0;
    size_t globalBinLo = 0, globalBinHi = 0;
    TransientDetector transients;
    int nWin = 0, stride = 1;

    std::vector<double> window, scale, bandwidth;
    std::vector<int> bandBins;

    struct Kernel { int first = 0; std::vector<double> weights; };
    std::vector<Kernel> kernels;                          // fine bins -> log grid

    std::array<std::vector<double>, numChannels> ring;    // decimated audio
    std::array<std::vector<double>, numChannels> windowed; // last analysed segment
    int ringPos = 0, pending = 0;

    std::vector<double> specPower;                        // [bin * 2]: facade, LFE
    std::vector<double> stats, now;                       // [bin * 13], see .cpp
    std::vector<double> m1, m2;                           // [grid * 4]
    std::vector<double> workRe, workIm, twiddleRe, twiddleIm;
    std::array<std::vector<std::complex<double>>, numChannels> spectrum;
    std::vector<int> bitReverse;

    Analysis out;
};

} // namespace subguard
