// Display inertia. Portable C++17.
//
// A read-out should not chase a value that flickers, yet it must follow a
// value that has really changed. The filter looks at how long the input has
// stayed on the same side of what is shown:
//
//   - it keeps crossing back and forth (flicker): slow, the read-out settles
//     on the average and hardly moves;
//   - it stays on one side (a real change): after a short moment the filter
//     speeds up and the read-out catches up quickly.

#pragma once

#include <algorithm>
#include <cmath>

namespace subguard
{

class Inertia
{
public:
    static constexpr double slowSeconds = 1.5;      // time constant while the input flickers
    static constexpr double fastSeconds = 0.12;     // time constant once a change is confirmed
    static constexpr double confirmStart = 0.15;    // seconds on the same side before speeding up...
    static constexpr double confirmEnd = 0.60;      // ...and before running at full speed

    void reset()            { primed = false; steady = 0.0; side = 0; }
    double value() const    { return shown; }

    double update (double input, double seconds)
    {
        if (! primed || ! std::isfinite (shown))
        {
            shown = input;
            primed = true;
            steady = 0.0;
            side = 0;
            return shown;
        }

        const double error = input - shown;
        const int now = error > 0.0 ? 1 : (error < 0.0 ? -1 : 0);

        if (now != 0 && now == side)
            steady += seconds;
        else if (now != 0)
            steady = 0.0;

        if (now != 0)
            side = now;

        const double t = std::clamp ((steady - confirmStart) / (confirmEnd - confirmStart), 0.0, 1.0);
        const double confidence = t * t * (3.0 - 2.0 * t);
        const double tau = slowSeconds * std::pow (fastSeconds / slowSeconds, confidence);

        shown += (1.0 - std::exp (-seconds / tau)) * error;
        return shown;
    }

private:
    double shown = 0.0, steady = 0.0;
    int side = 0;
    bool primed = false;
};

} // namespace subguard
