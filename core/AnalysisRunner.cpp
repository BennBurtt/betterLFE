#include "AnalysisRunner.h"

#include <algorithm>
#include <cmath>

namespace subguard
{

AnalysisRunner::AnalysisRunner (double sampleRate, double windowSeconds)
    : rate (sampleRate), window (windowSeconds),
      hop (std::max (1, (int) std::lround (hopSeconds * sampleRate))),
      engine (sampleRate, windowSeconds)
{
    // Two one-pole low-passes at 200 Hz for the bass meter
    bassCoeff = 1.0 - std::exp (-2.0 * 3.14159265358979323846 * 200.0 / sampleRate);
    published.sampleRate = sampleRate;
}

void AnalysisRunner::setSettings (const Settings& s)
{
    std::lock_guard<std::mutex> lock (settingsLock);
    settings = s;
}

void AnalysisRunner::setTransientSettings (const TransientSettings& t)
{
    std::lock_guard<std::mutex> lock (settingsLock);
    transientSettings = t;
    transientChanged = true;
}

void AnalysisRunner::requestReset()
{
    std::lock_guard<std::mutex> lock (settingsLock);
    resetWanted = true;
}

void AnalysisRunner::meter (const float* in, int numFrames)
{
    const double lfeGain = std::pow (10.0, current.lfeGainDb / 20.0);

    for (int i = 0; i < numFrames; ++i, in += numChannels)
    {
        screenPeak[0] = std::max (screenPeak[0], (double) std::abs (in[chL]));
        screenPeak[1] = std::max (screenPeak[1], (double) std::abs (in[chC]));
        screenPeak[2] = std::max (screenPeak[2], (double) std::abs (in[chR]));
        lfePeak = std::max (lfePeak, (double) std::abs (in[chLFE]));

        const double sum = (double) in[chL] + in[chC] + in[chR] + lfeGain * in[chLFE];
        bassState[0] += bassCoeff * (sum - bassState[0]);
        bassState[1] += bassCoeff * (bassState[0] - bassState[1]);
        bassPeak = std::max (bassPeak, std::abs (bassState[1]));
    }
}

void AnalysisRunner::feed (const float* interleaved, int numFrames)
{
    while (numFrames > 0)
    {
        // Up to the next 20 ms boundary, then analyse
        const int n = std::min (numFrames, hop - sinceStep);
        engine.push (interleaved, n);
        meter (interleaved, n);
        interleaved += (size_t) n * numChannels;
        numFrames -= n;
        sinceStep += n;

        if (sinceStep == hop)
        {
            sinceStep = 0;
            step();
        }
    }
}

void AnalysisRunner::step()
{
    bool reset = false;
    {
        std::lock_guard<std::mutex> lock (settingsLock);
        current = settings;

        if (transientChanged)
        {
            engine.setTransientSettings (transientSettings);
            transientChanged = false;
        }

        reset = resetWanted;
        resetWanted = false;
    }

    if (reset)
    {
        engine.resetCorrelation();
        engine.resetGlobal();
    }

    if (! engine.analyze (current))
        return;

    ++steps;
    auto events = engine.takeTransientEvents();
    const bool autoReset = engine.takeAutoReset();

    std::lock_guard<std::mutex> lock (publishLock);
    published.step = steps;
    published.analysis = engine.result();
    published.autoReset = published.autoReset || autoReset || reset;
    published.globalCorrelation = { { engine.globalCorrelation (0), engine.globalCorrelation (1), engine.globalCorrelation (2) } };
    published.globalSeconds = engine.globalSeconds();
    published.events.insert (published.events.end(), events.begin(), events.end());

    for (size_t c = 0; c < 3; ++c)
        published.screenPeak[c] = std::max (published.screenPeak[c], screenPeak[c]);

    published.lfePeak = std::max (published.lfePeak, lfePeak);
    published.bassPeak = std::max (published.bassPeak, bassPeak);
    published.audioSeconds += hop / rate;

    screenPeak = {};
    lfePeak = bassPeak = 0.0;
}

bool AnalysisRunner::take (Snapshot& out)
{
    std::lock_guard<std::mutex> lock (publishLock);

    if (published.step == lastTaken)
        return false;

    lastTaken = published.step;
    out.step = published.step;
    out.sampleRate = published.sampleRate;
    out.analysis = published.analysis;
    out.autoReset = published.autoReset;
    out.globalCorrelation = published.globalCorrelation;
    out.globalSeconds = published.globalSeconds;
    out.events.clear();
    out.events.swap (published.events);
    out.screenPeak = published.screenPeak;
    out.lfePeak = published.lfePeak;
    out.bassPeak = published.bassPeak;
    out.audioSeconds = published.audioSeconds;

    published.autoReset = false;
    published.screenPeak = {};
    published.lfePeak = published.bassPeak = 0.0;
    published.audioSeconds = 0.0;
    return true;
}

} // namespace subguard
