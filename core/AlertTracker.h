// Turns the engine output into what the display shows: per-frequency alert
// levels with attack / decay, the worst channel for the heat map and the
// three speaker indicators. Portable C++17, no dependencies.

#pragma once

#include "LfeEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace subguard
{

class AlertTracker
{
public:
    static constexpr double displayLoHz = 5.0, displayHiHz = 150.0;
    static constexpr double alertGain = 3.5;
    static constexpr double alertSigmas = 2.5;        // an alert must stand this far above chance
    static constexpr double speakerDecaySeconds = 0.10;

    // Signed LFE/screen interaction per channel, zero where nothing plays [L, C, R][grid]
    std::array<std::vector<double>, 3> curves;

    // Heat map: worst channel at each grid point. level 0..1, sign +1 in phase / -1 out of phase
    std::vector<double> heatLevel, heatSign;

    std::array<double, 3> speakerLevel { { 0.0, 0.0, 0.0 } };

    void reset (size_t gridSize)
    {
        for (int c = 0; c < 3; ++c)
        {
            curves[(size_t) c].assign (gridSize, 0.0);
            conflict[(size_t) c].assign (gridSize, 0.0);
            sign[(size_t) c].assign (gridSize, 1.0);
        }

        heatLevel.assign (gridSize, 0.0);
        heatSign.assign (gridSize, 1.0);
        speakerLevel.fill (0.0);
    }

    struct Options
    {
        double threshold = 0.5;
        double decaySeconds = 1.0;
        bool outOfPhaseOnly = false;    // alert only where the LFE works against the screen channel
        bool levelWeighted = false;     // weigh by the LFE / screen level balance (a much quieter LFE counts less)
    };

    /** The value an alert is judged on, for one transient. */
    static double transientMeasure (const TransientEvent& e, const Options& o)
    {
        if (o.outOfPhaseOnly)
            return std::max (0.0, -(o.levelWeighted ? e.interactionNow : e.correlationNow));

        return o.levelWeighted ? e.interactionBest : e.match;
    }

    static double severity (double measure, double threshold)
    {
        const double thr = std::min (threshold, 0.99);
        return std::clamp (alertGain * (measure - thr) / (1.0 - thr), 0.0, 1.0);
    }

    void update (const Analysis& a, const Options& options, double frameSeconds)
    {
        const double threshold = options.threshold, decaySeconds = options.decaySeconds;
        const auto n = a.grid.size();

        if (heatLevel.size() != n)
            reset (n);

        const double decay = std::min (frameSeconds / std::max (decaySeconds, 1e-3), 1.0);
        const double speakerDecay = std::min (frameSeconds / speakerDecaySeconds, 1.0);
        std::fill (heatLevel.begin(), heatLevel.end(), 0.0);

        for (size_t c = 0; c < 3; ++c)
        {
            double strongest = 0.0;

            for (size_t i = 0; i < n; ++i)
            {
                const double s = (options.levelWeighted ? a.interaction[c][i] : a.correlation[c][i]) * a.gate[c][i];
                curves[c][i] = s;

                // Alert above the user threshold, but never on a value that pure
                // chance could produce (little data right after an onset).
                double target = 0.0;

                if (a.grid[i] >= displayLoHz && a.grid[i] <= displayHiHz && ! (options.outOfPhaseOnly && s > 0.0))
                {
                    const double thr = std::clamp (std::max (threshold, alertSigmas * a.sigma[c][i]), 0.0, 0.99);
                    target = std::clamp ((std::abs (s) - thr) / (1.0 - thr), 0.0, 1.0);
                    target = std::min (target * alertGain, 1.0);
                }

                double& level = conflict[c][i];
                level += (target > level ? 0.8 : decay) * (target - level);

                if (target > 0.0)
                    sign[c][i] = s < 0.0 ? -1.0 : 1.0;

                if (level > heatLevel[i])
                {
                    heatLevel[i] = level;
                    heatSign[i] = sign[c][i];
                }

                strongest = std::max (strongest, target);
            }

            double& spk = speakerLevel[c];
            spk += (strongest > spk ? 0.7 : speakerDecay) * (strongest - spk);
        }
    }

private:
    std::array<std::vector<double>, 3> conflict, sign;
};

} // namespace subguard
