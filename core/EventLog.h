// The event log: what SubGUARD saw, when, on which channel, stamped with the
// timeline's timecode. Fed by the live input and by the offline file analysis
// alike, so both log the same things the same way. Portable C++17.

#pragma once

#include "AlertTracker.h"
#include "PlaybackSim.h"
#include "Timecode.h"
#include "TransientDetector.h"

#include <array>
#include <string>
#include <vector>

namespace subguard
{

enum class EventKind
{
    phaseAlert,         // the LFE works with or against a screen channel (the Phase Alert, both phases by default)
    lfeTiming,          // the LFE's attacks late, early or inverted against the mix
    lowRobustness,      // the bass robustness score under its threshold
    lfeCarriesBass,     // the LFE well above the screen: the bass rests on it
    screenAntiphase,    // screen channels in opposition of phase: lost when summed
    deliverable,        // a check of the delivered file (LFE above 120 Hz, infra, DC, peak)
    numKinds
};

struct LogEvent
{
    EventKind kind = EventKind::phaseAlert;
    double start = 0.0, end = 0.0;      // seconds of the timeline (real time from 00:00:00:00)
    int channel = -1;                   // 0 L, 1 C, 2 R, 3 LFE, -1 none
    double peak = 0.0;                  // worst value seen, in `unit`
    double hz = 0.0;                    // where in frequency, when it applies
    int count = 1;                      // attacks merged into one event (lfeTiming)
    std::string unit;                   // of `peak`: "0-1", "ms", "/100", "dB"
    std::string detail;

    static const char* kindName (EventKind);
    static const char* channelName (int);
};

/** What one analysis step shows, as the log needs it. */
struct LogObservation
{
    bool hasBass = false;
    std::array<double, 3> alert {};             // 0..1 per screen channel, this step only (AlertTracker::alertNow)
    std::array<double, 3> alertHz {}, alertSign { { 1.0, 1.0, 1.0 } };
    double robustness = 100.0;                  // with nothing to judge, 100
    double lfeBalanceDb = -60.0;                // LFE against the screen, dB
    double antiphaseDb = 0.0;                   // worst band, screen summed against nothing cancelled
    double antiphaseHz = 0.0;
    int antiphasePair = -1;                     // 0 L / R, 1 L / C, 2 C / R
    std::vector<TransientEvent> transients;     // attacks found in this step
};

class EventLog
{
public:
    struct Rules
    {
        double alertOn = 0.25;              // a phase alert opens above this (0..1), closes under half of it
        double robustnessUnder = 50.0;
        double lfeOverScreenDb = 6.0;       // PlaybackSimulator::balanceWarningDb
        double antiphaseUnderDb = -3.0;     // PlaybackSimulator::antiphaseWarningDb, as a loss
        double timingMs = 2.0;              // an attack of the LFE this late or early, or inverted, is logged
        double minMatch = 0.3;              // only attacks the LFE really shares
        double mergeSeconds = 1.0;          // the same thing again within this time continues the event
        double minSeconds = 0.25;           // shorter conditions are not logged (attacks are)
        bool french = false;                // language of the details
    };

    /** What the log needs from one step: the alerts of this step, the score, the attacks found. */
    static LogObservation observation (const AlertTracker&, const Translation&, const std::vector<TransientEvent>&);

    /** A kind's name in the log's language. */
    static const char* kindName (EventKind, bool french);

    Rules rules;

    /** One analysis step at `time` (seconds of the timeline), covering `stepSeconds`. */
    void observe (double time, double stepSeconds, const LogObservation&);

    /** A finding that is not a condition over time (deliverable checks): logged as it is. */
    void add (const LogEvent&);

    /** Closes what is open (end of a pass or of a file). */
    void flush();

    void clear();

    const std::vector<LogEvent>& events() const     { return done; }
    size_t size() const                             { return done.size(); }

    /** Comma-separated values, one event per line, timecode at `rate`. `title` goes in a comment line. */
    std::string toCsv (FrameRate rate, const std::string& title = {}) const;

    /** Markers as an edit decision list (CMX 3600 with marker notes, as DaVinci Resolve reads them). */
    std::string toEdlMarkers (FrameRate rate, const std::string& title) const;

private:
    struct Open
    {
        bool active = false;
        LogEvent event;
        double lastSeen = 0.0;
    };

    void condition (Open&, bool on, bool stillOn, double time, double stepSeconds, double value, double hz,
                    EventKind, int channel, const char* unit, const std::string& detail, bool worseIsLower);
    void close (Open&);

    std::array<Open, 3> alerts;
    Open timing, robustness, lfeBass, antiphase;
    std::vector<LogEvent> done;
};

} // namespace subguard
