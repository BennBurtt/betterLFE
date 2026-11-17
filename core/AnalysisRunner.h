// Runs the analysis at the pace of the audio, independently of the display.
// Portable C++17.
//
// The engine is fed sample by sample and analysed every 20 ms of audio,
// exactly: where the analysis falls does not depend on how the audio arrives
// (block sizes) nor on how often the display looks. With windows of 0.5, 1
// and 2 s, consecutive frames overlap by 96, 98 and 99 %.
//
// One thread feeds (feed), another reads (take). The reader gets the latest
// complete state, plus what accumulated since its last read: transient
// events, peak levels for the meters. Settings may be changed from any thread;
// they apply from the next 20 ms step.

#pragma once

#include "LfeEngine.h"

#include <array>
#include <mutex>
#include <vector>

namespace subguard
{

struct Snapshot
{
    long long step = 0;                 // analysis steps so far; unchanged since the last read: nothing new
    double sampleRate = 0.0;
    Analysis analysis;                  // as measured, no display smoothing
    bool autoReset = false;             // the engine cleared its measurements since the last read
    std::array<double, 3> globalCorrelation {};
    double globalSeconds = 0.0;

    // Since the last read
    std::vector<TransientEvent> events;
    std::array<double, 3> screenPeak {};    // L, C, R, linear
    double lfePeak = 0.0;
    double bassPeak = 0.0;                  // all four channels under 200 Hz, LFE as played
    double audioSeconds = 0.0;              // audio covered

    // Waveform since the last read: signed extremes over the four channels, and their energy
    double waveMin = 0.0, waveMax = 0.0, waveSumSquares = 0.0;
    long long waveCount = 0;
};

class AnalysisRunner
{
public:
    static constexpr double hopSeconds = 0.020;

    AnalysisRunner (double sampleRate, double windowSeconds);

    double sampleRate() const       { return rate; }
    double windowSeconds() const    { return window; }
    int hopFrames() const           { return hop; }

    // Any thread
    void setSettings (const Settings&);
    void setTransientSettings (const TransientSettings&);
    void requestReset();

    /** Analysis thread: interleaved L, C, R, LFE frames. */
    void feed (const float* interleaved, int numFrames);

    /** Reader: the latest state and what accumulated since the last call.
        Returns false, and leaves `out` alone, when nothing new happened. */
    bool take (Snapshot& out);

private:
    void meter (const float* interleaved, int numFrames);
    void step();

    const double rate, window;
    const int hop;
    LfeEngine engine;
    int sinceStep = 0;
    long long steps = 0;

    // Settings, written by any thread, read at each step
    std::mutex settingsLock;
    Settings settings, current;
    TransientSettings transientSettings;
    bool transientChanged = false, resetWanted = false;

    // Meters, analysis thread only
    double bassState[2] = { 0.0, 0.0 }, bassCoeff = 0.0;
    std::array<double, 3> screenPeak {};
    double lfePeak = 0.0, bassPeak = 0.0;
    double waveMin = 0.0, waveMax = 0.0, waveSumSquares = 0.0;
    long long waveCount = 0;

    // What the reader gets
    std::mutex publishLock;
    Snapshot published;
    long long lastTaken = -1;
};

} // namespace subguard
