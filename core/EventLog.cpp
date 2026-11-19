#include "EventLog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace subguard
{

namespace
{
    std::string number (double v, int decimals, bool sign = false)
    {
        char text[32];
        std::snprintf (text, sizeof (text), sign ? "%+.*f" : "%.*f", decimals, v);
        return text;
    }

    std::string csvField (const std::string& s)
    {
        if (s.find_first_of (",\"\n") == std::string::npos)
            return s;

        std::string out = "\"";

        for (char c : s)
        {
            if (c == '"')
                out += '"';

            out += c;
        }

        return out + "\"";
    }

}

const char* LogEvent::kindName (EventKind k)
{
    switch (k)
    {
        case EventKind::phaseAlert:         return "Phase alert";
        case EventKind::lfeTiming:          return "LFE timing";
        case EventKind::lowRobustness:      return "Low robustness";
        case EventKind::lfeCarriesBass:     return "Bass rests on the LFE";
        case EventKind::screenAntiphase:    return "Screen in opposition of phase";
        case EventKind::deliverable:        return "Deliverable check";
        default:                            return "";
    }
}

const char* LogEvent::channelName (int c)
{
    switch (c)
    {
        case 0:     return "L";
        case 1:     return "C";
        case 2:     return "R";
        case 3:     return "LFE";
        default:    return "";
    }
}

const char* EventLog::kindName (EventKind k, bool french)
{
    if (! french)
        return LogEvent::kindName (k);

    switch (k)
    {
        case EventKind::phaseAlert:         return "Alerte de phase";
        case EventKind::lfeTiming:          return "Calage du LFE";
        case EventKind::lowRobustness:      return "Robustesse basse";
        case EventKind::lfeCarriesBass:     return "Le grave repose sur le LFE";
        case EventKind::screenAntiphase:    return "Écran en opposition de phase";
        case EventKind::deliverable:        return "Contrôle du livrable";
        default:                            return "";
    }
}

LogObservation EventLog::observation (const AlertTracker& alerts, const Translation& t, const std::vector<TransientEvent>& events)
{
    LogObservation x;
    x.hasBass = t.hasBass;
    x.alert = alerts.alertNow;
    x.alertHz = alerts.alertHz;
    x.alertSign = alerts.alertSign;
    x.robustness = t.robustness;
    x.lfeBalanceDb = t.lfeBalanceDb;
    x.antiphaseDb = t.antiphaseWorstDb;
    x.antiphaseHz = t.antiphaseWorstHz;
    x.antiphasePair = t.antiphasePair;
    x.transients = events;
    return x;
}

//==============================================================================
void EventLog::condition (Open& o, bool on, bool stillOn, double time, double stepSeconds, double value, double hz,
                          EventKind kind, int channel, const char* unit, const std::string& detail, bool worseIsLower)
{
    const bool active = o.active ? stillOn : on;

    if (active)
    {
        if (! o.active)
        {
            o.active = true;
            o.event = {};
            o.event.kind = kind;
            o.event.channel = channel;
            o.event.start = time - stepSeconds;
            o.event.peak = value;
            o.event.hz = hz;
            o.event.unit = unit;
            o.event.detail = detail;
        }

        const bool worse = worseIsLower ? value < o.event.peak : value > o.event.peak;

        if (worse)
        {
            o.event.peak = value;
            o.event.hz = hz;
            o.event.detail = detail;
        }

        o.event.end = time;
        o.lastSeen = time;
    }
    else if (o.active && time - o.lastSeen > rules.mergeSeconds)
    {
        close (o);
    }
}

void EventLog::close (Open& o)
{
    if (! o.active)
        return;

    o.active = false;

    if (o.event.kind == EventKind::lfeTiming || o.event.end - o.event.start >= rules.minSeconds)
        done.push_back (o.event);
}

void EventLog::observe (double time, double stepSeconds, const LogObservation& x)
{
    // Phase alert, channel by channel: opens above the threshold, holds down to half of it
    for (int c = 0; c < 3; ++c)
    {
        const double a = x.hasBass ? x.alert[(size_t) c] : 0.0;
        const bool inPhase = x.alertSign[(size_t) c] > 0.0;
        const std::string where = number (x.alertHz[(size_t) c], 0) + " Hz";
        const std::string detail = rules.french ? std::string ("LFE ") + (inPhase ? "en phase avec " : "contre ") + LogEvent::channelName (c) + " vers " + where
                                                : std::string ("LFE ") + (inPhase ? "in phase with " : "against ") + LogEvent::channelName (c) + " near " + where;
        condition (alerts[(size_t) c], a >= rules.alertOn, a >= 0.5 * rules.alertOn, time, stepSeconds, a, x.alertHz[(size_t) c],
                   EventKind::phaseAlert, c, "0-1", detail, false);
    }

    // Robustness under its threshold
    condition (robustness, x.hasBass && x.robustness < rules.robustnessUnder, x.hasBass && x.robustness < rules.robustnessUnder + 5.0,
               time, stepSeconds, x.robustness, 0.0, EventKind::lowRobustness, -1, "/100",
               (rules.french ? "robustesse du grave " : "bass robustness ") + number (x.robustness, 0) + " / 100", true);

    // The bass resting on the LFE
    condition (lfeBass, x.hasBass && x.lfeBalanceDb > rules.lfeOverScreenDb, x.hasBass && x.lfeBalanceDb > rules.lfeOverScreenDb - 1.0,
               time, stepSeconds, x.lfeBalanceDb, 0.0, EventKind::lfeCarriesBass, 3, "dB",
               "LFE " + number (x.lfeBalanceDb, 1, true) + (rules.french ? " dB face à l'écran" : " dB against the screen"), false);

    // Screen channels in opposition of phase
    {
        const char* pairs[] = { "L / R", "L / C", "C / R" };
        const std::string pair = x.antiphasePair >= 0 && x.antiphasePair < 3 ? pairs[x.antiphasePair] : "";
        condition (antiphase, x.hasBass && x.antiphaseDb < rules.antiphaseUnderDb, x.hasBass && x.antiphaseDb < rules.antiphaseUnderDb + 1.0,
                   time, stepSeconds, x.antiphaseDb, x.antiphaseHz, EventKind::screenAntiphase, -1, "dB",
                   rules.french ? pair + " perdent " + number (-x.antiphaseDb, 1) + " dB vers " + number (x.antiphaseHz, 0) + " Hz une fois sommés"
                                : pair + " lose " + number (-x.antiphaseDb, 1) + " dB near " + number (x.antiphaseHz, 0) + " Hz when summed", true);
    }

    // Attacks of the LFE off time or inverted: one event for a run of them
    for (const auto& e : x.transients)
    {
        if (! e.inLfe || e.match < rules.minMatch)
            continue;

        const bool inverted = std::cos (e.phase) < 0.0;
        const bool off = std::abs (e.delayMs) >= rules.timingMs;

        if (! inverted && ! off)
            continue;

        const bool fr = rules.french;
        const std::string what = inverted ? (off ? (fr ? "inversées, " : "inverted, ") + number (e.delayMs, 1, true) + " ms" : std::string (fr ? "inversées" : "inverted"))
                                          : (e.delayMs > 0.0 ? (fr ? "en retard " : "late ") : (fr ? "en avance " : "early ")) + number (e.delayMs, 1, true) + " ms";

        if (timing.active && time - timing.lastSeen <= 2.0 * rules.mergeSeconds)
        {
            ++timing.event.count;

            if (std::abs (e.delayMs) > std::abs (timing.event.peak))
                timing.event.peak = e.delayMs;

            timing.event.detail = fr ? "attaques du LFE " + what + " (pire " + number (timing.event.peak, 1, true) + " ms, "
                                         + std::to_string (timing.event.count) + " attaques)"
                                     : "LFE attacks " + what + " (worst " + number (timing.event.peak, 1, true) + " ms, "
                                         + std::to_string (timing.event.count) + " attacks)";
            timing.event.end = time;
            timing.lastSeen = time;
        }
        else
        {
            close (timing);
            timing.active = true;
            timing.event = {};
            timing.event.kind = EventKind::lfeTiming;
            timing.event.channel = 3;
            timing.event.start = timing.event.end = time;
            timing.event.peak = e.delayMs;
            timing.event.unit = "ms";
            timing.event.detail = (fr ? "attaque du LFE " : "LFE attack ") + what;
            timing.lastSeen = time;
        }
    }

    if (timing.active && time - timing.lastSeen > 2.0 * rules.mergeSeconds)
        close (timing);

}

void EventLog::add (const LogEvent& e)
{
    done.push_back (e);
}

void EventLog::flush()
{
    for (auto& o : alerts)
        close (o);

    close (timing);
    close (robustness);
    close (lfeBass);
    close (antiphase);

    std::stable_sort (done.begin(), done.end(), [] (const LogEvent& a, const LogEvent& b) { return a.start < b.start; });
}

void EventLog::clear()
{
    done.clear();

    for (auto& o : alerts)
        o = {};

    timing = robustness = lfeBass = antiphase = {};
}

//==============================================================================
std::string EventLog::toCsv (FrameRate rate, const std::string& title) const
{
    std::string out;

    if (! title.empty())
        out += "# " + title + "\n";

    out += std::string ("# Timecode ") + Timecode::name (rate) + "\n";
    out += "Start,End,Duration (s),Type,Channel,Value,Unit,Hz,Attacks,Detail\n";

    for (const auto& e : done)
    {
        out += Timecode::labelOfSeconds (e.start, rate) + "," + Timecode::labelOfSeconds (e.end, rate) + ","
             + number (e.end - e.start, 2) + "," + csvField (kindName (e.kind, rules.french)) + "," + LogEvent::channelName (e.channel) + ","
             + number (e.peak, e.unit == "0-1" ? 2 : 1) + "," + csvField (e.unit) + "," + (e.hz > 0.0 ? number (e.hz, 0) : std::string()) + ","
             + std::to_string (e.count) + "," + csvField (e.detail) + "\n";
    }

    return out;
}

std::string EventLog::toEdlMarkers (FrameRate rate, const std::string& title) const
{
    std::string out = "TITLE: " + (title.empty() ? std::string ("SubGUARD") : title) + "\n";
    out += std::string ("FCM: ") + (Timecode::isDrop (rate) ? "DROP FRAME" : "NON-DROP FRAME") + "\n\n";
    int index = 1;

    for (const auto& e : done)
    {
        const long long in = (long long) (e.start * Timecode::real (rate) + 1e-6);
        const long long length = std::max (1LL, (long long) ((e.end - e.start) * Timecode::real (rate) + 0.5));
        const auto a = Timecode::label (in, rate), b = Timecode::label (in + 1, rate);
        const char* colour = e.kind == EventKind::phaseAlert ? "ResolveColorRed"
                           : e.kind == EventKind::lfeTiming ? "ResolveColorYellow"
                           : e.kind == EventKind::screenAntiphase ? "ResolveColorPurple"
                           : e.kind == EventKind::deliverable ? "ResolveColorCyan" : "ResolveColorBlue";
        char head[128];
        std::snprintf (head, sizeof (head), "%03d  001      V     C        %s %s %s %s  \n", index++, a.c_str(), b.c_str(), a.c_str(), b.c_str());
        std::string name = std::string (kindName (e.kind, rules.french)) + (e.channel >= 0 ? std::string (" ") + LogEvent::channelName (e.channel) : "")
                         + ": " + e.detail;
        std::replace (name.begin(), name.end(), '|', '/');
        out += head;
        out += std::string (" |C:") + colour + " |M:" + name + " |D:" + std::to_string (length) + "\n\n";
    }

    return out;
}

} // namespace subguard
