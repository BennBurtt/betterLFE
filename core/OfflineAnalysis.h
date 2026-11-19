// Offline file analysis: a reel (or any file) run through exactly what the live
// input goes through (the engine, the Phase Alert, the playback simulation, the
// event log), faster than real time, then summed up in a report per reel.
// Portable C++17: the app reads the files and hands over the audio.

#pragma once

#include "AlertTracker.h"
#include "AnalysisRunner.h"
#include "EventLog.h"
#include "PlaybackSim.h"
#include "RoomSim.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace subguard
{

struct ReelSettings
{
    Settings engine;
    TransientSettings transients;
    AlertTracker::Options alert;
    PlaybackSimulator::Tuning tuning;
    PlaybackSimulator::Level level = PlaybackSimulator::Level::strong;
    double lfeGainDb = 10.0;
    double windowSeconds = 1.0;             // the engine's time / frequency window
    double evaluateSeconds = 0.1;           // the score and the log are worked out this often
    std::vector<bool> roomSelection;        // Room Sim rows that count in the score (rooms, then other systems)
    double roomSeconds = 1.0;               // Room Sim is worked out this often when a row counts
    double warmupSeconds = -1.0;            // the start, while the averages fill: not logged, not counted (-1: the correlation averaging time)
    EventLog::Rules rules;
};

/** A check of the delivered file, beyond the mix itself (filled by DeliverableChecks). */
struct DeliverableFinding
{
    std::string name, result;
    bool pass = true;
    double at = -1.0;           // timeline seconds of the worst place, -1 when it does not apply
};

struct ReelReport
{
    std::string name;
    double sampleRate = 0.0;
    double startSeconds = 0.0;              // timecode of the first sample, in seconds
    double durationSeconds = 0.0;
    std::string timecodeSource;             // where the start came from (BWF time reference, reel number)
    std::string channels;                   // how the channels were found

    // The bass, over the time it plays
    double bassSeconds = 0.0;
    double robustnessMean = 100.0, robustnessMin = 100.0, robustnessP10 = 100.0;
    double secondsUnder50 = 0.0, secondsUnder75 = 0.0;
    double lfeShareMean = 0.0;              // share of the bass in the LFE, mean over the bass time
    double secondsLfeOverScreen = 0.0;      // LFE more than 6 dB above the screen
    double antiphaseWorstDb = 0.0, antiphaseWorstAt = -1.0;
    std::array<double, 3> alertSeconds {};  // Phase Alert on L, C, R
    int lfeTimingEvents = 0;

    struct Moment { double at = 0.0, robustness = 100.0; };
    std::vector<Moment> worst;              // the lowest scores, at least 10 s apart
    std::vector<float> timeline;            // robustness, one value a second (-1: no bass)

    std::vector<LogEvent> events;
    std::vector<DeliverableFinding> deliverables;
    bool cancelled = false;
};

class OfflineAnalyzer
{
public:
    /** Pulls interleaved L, C, R, LFE frames; returns how many it gave, 0 at the end. */
    using Pull = std::function<int (float* interleaved, int maxFrames)>;

    /** Runs a reel. `progress` gets 0..1 and returns false to stop. */
    static ReelReport analyse (const std::string& name, double sampleRate, double startSeconds, long long totalFrames,
                               const Pull& pull, const ReelSettings& settings, const std::function<bool (double)>& progress = {});

    /** One page for all the reels: a summary table, then each reel with its findings and its events. */
    static std::string htmlReport (const std::vector<ReelReport>& reels, FrameRate rate, const std::string& title, bool french = false);
};

} // namespace subguard
