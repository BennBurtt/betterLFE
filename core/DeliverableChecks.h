// Checks of a delivered file, channel by channel, at its own sample rate:
// true peak and sample peak, clipping, DC offset, LFE content above 120 Hz,
// infra-bass under 20 Hz. Fed by the offline file analysis with every channel
// of the file as it is (not the analysis' sums). Portable C++17.
//
// Sources and choices:
//   true peak: 4x oversampling as in ITU-R BS.1770-4 Annex 2 (the interpolation
//     filter here is a windowed sinc of 48 taps, not the standard's table);
//     limit -1 dBTP, EBU R 128;
//   LFE band: 120 Hz (ITU-R BS.775, the LFE channel; Dolby's LFE low-pass);
//   filters: Butterworth sections from the RBJ Audio EQ Cookbook;
//   the other limits (clipping run, DC, LFE above 120 Hz, infra) are choices.

#pragma once

#include "EventLog.h"
#include "OfflineAnalysis.h"

#include <array>
#include <string>
#include <vector>

namespace subguard
{

class DeliverableChecks
{
public:
    struct Limits
    {
        double truePeakDbtp = -1.0;         // EBU R 128
        int clipRun = 3;                    // this many samples in a row at full scale is clipping
        double dcPercent = 0.1;             // mean over the file, % of full scale
        double lfeAboveDb = -30.0;          // LFE above 120 Hz against the LFE below, worst second
        double lfeAboveFloorDbfs = -60.0;   // seconds with a quieter LFE are not judged
        double infraDbfs = -40.0;           // under 20 Hz, RMS over a second
    };

    /** `labels`: the name of each channel (L, C, R, LFE, Ls ...); `start`: timeline seconds of the first sample. */
    DeliverableChecks (double sampleRate, std::vector<std::string> labels, double start, bool french = false);

    Limits limits;

    /** Planar audio, one pointer per channel, in the order of the labels. */
    void push (const float* const* channels, int numFrames);

    /** The findings, and the failures as log events (kind deliverable) added to `events` if given. */
    std::vector<DeliverableFinding> finish (std::vector<LogEvent>* events = nullptr);

    /** Sample and true peak of one channel so far, dBFS / dBTP (for the tests). */
    double samplePeakDb (size_t channel) const;
    double truePeakDb (size_t channel) const;

private:
    struct Section
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double run (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    static Section butterworth (bool highPass, double hz, double q, double rate);

    struct Channel
    {
        std::string label;
        bool isLfe = false;
        double sum = 0.0;
        double peak = 0.0, truePeak = 0.0, truePeakAt = 0.0;
        int run = 0, clips = 0;
        double clipAt = -1.0;
        std::array<double, 12> history {};
        size_t historyPos = 0;
        std::array<Section, 3> infra;           // 5 Hz high-pass, 2nd order; 20 Hz low-pass, 4th order (2 sections)
        std::array<Section, 2> above;           // LFE: 120 Hz high-pass, 4th order
        std::array<Section, 2> below;           // LFE: 120 Hz low-pass, 4th order
        double infraSum = 0.0, aboveSum = 0.0, belowSum = 0.0;
        double infraWorst = 0.0, infraAt = -1.0;
        double aboveWorstDb = -200.0, aboveAt = -1.0;
    };

    void endOfSecond (Channel&);

    double rate, startSeconds;
    bool french;
    std::vector<Channel> channels;
    std::array<std::array<double, 12>, 4> phases {};    // interpolation filter: 48 taps, 4 phases of 12
    long long frames = 0;
    int inSecond = 0, secondLength = 1;
};

} // namespace subguard
