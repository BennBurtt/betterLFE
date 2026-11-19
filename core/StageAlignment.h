// Your own stage: how the subwoofer and a screen speaker add up at the
// listening position, measured with a microphone.
//
// Each speaker plays an exponential sine sweep (Farina 2000); the microphone
// recording, divided by the sweep in frequency (regularised), gives the
// impulse response from the output to the microphone. Both responses are
// windowed over the same stretch of time, so their timing stays as it is, and
// turned into transfer functions from 20 to 200 Hz. Then:
//   - where the two cross in level (the overlap of screen and subwoofer);
//   - their sum there, as it is, against a perfect in-phase sum;
//   - the delay and polarity of the subwoofer that would sum best around it,
//     searched over +-15 ms;
//   - the arrival of each (first rise to half its peak), for reference.
// All of it relative to the system as measured: "keep the polarity" keeps
// whatever polarity the subwoofer has now.
// Portable C++17.

#pragma once

#include <string>
#include <vector>

namespace subguard
{

class StageAlignment
{
public:
    struct Sweep
    {
        double sampleRate = 48000.0;
        double seconds = 6.0, fromHz = 15.0, toHz = 600.0;
        double levelDbfs = -20.0;           // peak of the sweep
        double tailSeconds = 1.5;           // recorded after the sweep: the room's decay and the latency
    };

    /** The sweep to play (faded in and out). */
    static std::vector<float> makeSweep (const Sweep&);

    /** Impulse response from a recording of the sweep (same rate, recording at least as long as the sweep). */
    static std::vector<double> impulseResponse (const std::vector<float>& sweep, const std::vector<float>& recorded);

    struct Result
    {
        bool valid = false;
        std::string problem;                // why not, when not valid
        double screenArrivalMs = 0.0, subArrivalMs = 0.0;     // first rise of each response, from the start of the recording
        double crossoverHz = 0.0;           // where screen and subwoofer are equally loud
        double sumNowDb = 0.0;              // around the crossover: their sum as it is, against a perfect in-phase sum
        double sumBestDb = 0.0;             // the same with the best delay and polarity
        double bestDelayMs = 0.0;           // to add to the subwoofer (negative: take off, or delay the screen)
        bool bestInverted = false;          // the subwoofer's polarity should be changed from what it is now
        bool nowInverted = false;           // as it is, the subwoofer sums better inverted than not
        std::vector<double> hz, screenDb, subDb, sumDb, bestDb;     // 20 .. 200 Hz, dB relative to the sweep
    };

    /** The two impulse responses (from impulseResponse), at `sampleRate`. */
    static Result analyse (const std::vector<double>& screen, const std::vector<double>& sub, double sampleRate);
};

} // namespace subguard
