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
    std::array<std::vector<double>, numPairs> interaction, coherence, phase, gate, sigma;
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

    double decimatedRate() const    { return fs; }
    double windowSeconds() const    { return windowSec; }

private:
    void fft();

    Decimator decimator;
    double fs, facadeGain, windowSec = 1.0;
    TransientDetector transients;
    int nWin = 0, stride = 1;

    std::vector<double> window, scale, bandwidth;
    std::vector<int> bandBins;

    struct Kernel { int first = 0; std::vector<double> weights; };
    std::vector<Kernel> kernels;                          // fine bins -> log grid

    std::array<std::vector<double>, numChannels> ring;    // decimated audio
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
