#include "OfflineAnalysis.h"

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

    std::string escape (const std::string& s)
    {
        std::string out;

        for (char c : s)
        {
            switch (c)
            {
                case '<':   out += "&lt;"; break;
                case '>':   out += "&gt;"; break;
                case '&':   out += "&amp;"; break;
                case '"':   out += "&quot;"; break;
                default:    out += c; break;
            }
        }

        return out;
    }

    std::string duration (double seconds)
    {
        const long long s = (long long) std::llround (std::max (0.0, seconds));
        char text[32];

        if (s >= 3600)
            std::snprintf (text, sizeof (text), "%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
        else
            std::snprintf (text, sizeof (text), "%lld:%02lld", s / 60, s % 60);

        return text;
    }

    const char* scoreClass (double score)
    {
        return score >= 75.0 ? "good" : score >= 50.0 ? "fair" : "bad";
    }
}

ReelReport OfflineAnalyzer::analyse (const std::string& name, double sampleRate, double startSeconds, long long totalFrames,
                                     const Pull& pull, const ReelSettings& settings, const std::function<bool (double)>& progress)
{
    ReelReport r;
    r.name = name;
    r.sampleRate = sampleRate;
    r.startSeconds = startSeconds;

    AnalysisRunner runner (sampleRate, settings.windowSeconds);
    auto engineSettings = settings.engine;
    engineSettings.lfeGainDb = settings.lfeGainDb;
    runner.setSettings (engineSettings);
    runner.setTransientSettings (settings.transients);

    PlaybackSimulator sim;
    sim.tuning = settings.tuning;
    RoomSimulator rooms;
    RoomSimResult roomResult;
    const bool anyRoom = std::find (settings.roomSelection.begin(), settings.roomSelection.end(), true) != settings.roomSelection.end();
    double sinceRooms = 1e9;

    AlertTracker alerts;
    EventLog log;
    log.rules = settings.rules;
    Snapshot snap;

    const double warmup = settings.warmupSeconds >= 0.0 ? settings.warmupSeconds : settings.engine.tauCorrelation;
    const long long evalFrames = std::max (1LL, (long long) std::llround (settings.evaluateSeconds * sampleRate));
    const double evalSeconds = (double) evalFrames / sampleRate;
    constexpr int block = 8192;
    std::vector<float> buffer ((size_t) block * numChannels);
    long long done = 0, sinceEval = 0, sinceProgress = 0;
    std::vector<double> scores;
    double scoreSum = 0.0, shareSum = 0.0;

    const auto evaluate = [&]
    {
        if (! runner.take (snap))
            return;

        alerts.update (snap.analysis, settings.alert, evalSeconds);
        auto t = sim.compute (snap.analysis, settings.level, settings.lfeGainDb);

        if (anyRoom)
        {
            if (sinceRooms >= settings.roomSeconds)
            {
                roomResult = rooms.compute (snap.analysis, t, sim.tuning, settings.level, settings.lfeGainDb);
                sinceRooms = 0.0;
            }

            sinceRooms += evalSeconds;
            RoomSimulator::applyToScore (t, roomResult, settings.roomSelection, sim.tuning.levelDependenceWeight);
        }

        const double now = startSeconds + (double) done / sampleRate;

        if ((double) done / sampleRate < warmup)
            return;

        log.observe (now, evalSeconds, EventLog::observation (alerts, t, snap.events));

        const auto second = (size_t) ((double) done / sampleRate);

        if (r.timeline.size() <= second)
            r.timeline.resize (second + 1, -1.0f);

        if (! t.hasBass)
            return;

        r.bassSeconds += evalSeconds;
        scoreSum += t.robustness * evalSeconds;
        shareSum += t.lfeShare * evalSeconds;
        scores.push_back (t.robustness);
        r.robustnessMin = std::min (r.robustnessMin, t.robustness);
        r.secondsUnder50 += t.robustness < 50.0 ? evalSeconds : 0.0;
        r.secondsUnder75 += t.robustness < 75.0 ? evalSeconds : 0.0;
        r.secondsLfeOverScreen += t.lfeBalanceDb > settings.rules.lfeOverScreenDb ? evalSeconds : 0.0;

        if (t.antiphaseWorstDb < r.antiphaseWorstDb)
        {
            r.antiphaseWorstDb = t.antiphaseWorstDb;
            r.antiphaseWorstAt = now;
        }

        for (size_t c = 0; c < 3; ++c)
            r.alertSeconds[c] += alerts.alertNow[c] >= settings.rules.alertOn ? evalSeconds : 0.0;

        auto& slot = r.timeline[second];
        slot = slot < 0.0f ? (float) t.robustness : std::min (slot, (float) t.robustness);
    };

    for (;;)
    {
        const int got = pull (buffer.data(), block);

        if (got <= 0)
            break;

        const float* p = buffer.data();
        int left = got;

        while (left > 0)
        {
            const int n = (int) std::min ((long long) left, evalFrames - sinceEval);
            runner.feed (p, n);
            p += (size_t) n * numChannels;
            left -= n;
            done += n;
            sinceEval += n;

            if (sinceEval >= evalFrames)
            {
                sinceEval = 0;
                evaluate();
            }
        }

        sinceProgress += got;

        if (progress && sinceProgress >= (long long) sampleRate)
        {
            sinceProgress = 0;

            if (! progress (totalFrames > 0 ? std::min (1.0, (double) done / (double) totalFrames) : 0.0))
            {
                r.cancelled = true;
                break;
            }
        }
    }

    log.flush();
    r.durationSeconds = (double) done / sampleRate;
    r.events = log.events();

    for (const auto& e : r.events)
        r.lfeTimingEvents += e.kind == EventKind::lfeTiming ? 1 : 0;

    if (r.bassSeconds > 0.0)
    {
        r.robustnessMean = scoreSum / r.bassSeconds;
        r.lfeShareMean = shareSum / r.bassSeconds;
        std::sort (scores.begin(), scores.end());
        r.robustnessP10 = scores[(size_t) (0.1 * (double) (scores.size() - 1))];
    }

    // The worst seconds, at least 10 s apart, under 90
    {
        std::vector<size_t> order;

        for (size_t i = 0; i < r.timeline.size(); ++i)
            if (r.timeline[i] >= 0.0f && r.timeline[i] < 90.0f)
                order.push_back (i);

        std::sort (order.begin(), order.end(), [&r] (size_t a, size_t b) { return r.timeline[a] < r.timeline[b]; });

        for (size_t i : order)
        {
            const double at = startSeconds + (double) i;
            const bool near = std::any_of (r.worst.begin(), r.worst.end(), [at] (const ReelReport::Moment& m) { return std::abs (m.at - at) < 10.0; });

            if (! near)
                r.worst.push_back ({ at, (double) r.timeline[i] });

            if (r.worst.size() >= 5)
                break;
        }
    }

    if (progress)
        progress (1.0);

    return r;
}

//==============================================================================
std::string OfflineAnalyzer::htmlReport (const std::vector<ReelReport>& reels, FrameRate rate, const std::string& title, bool french)
{
    const auto T = [french] (const char* en, const char* fr) { return std::string (french ? fr : en); };
    const auto tc = [rate] (double s) { return Timecode::labelOfSeconds (s, rate); };

    std::string h;
    h += "<!doctype html><html lang=\"" + T ("en", "fr") + "\"><head><meta charset=\"utf-8\"><title>" + escape (title) + "</title><style>"
         "body{font-family:-apple-system,Helvetica,Arial,sans-serif;color:#1d1d1f;margin:28px;max-width:1100px}"
         "h1{font-size:22px;margin:0 0 4px}h2{font-size:17px;margin:28px 0 6px;border-bottom:1px solid #ccc;padding-bottom:4px}"
         "p.note{color:#555;font-size:12px;margin:2px 0 10px}table{border-collapse:collapse;font-size:12.5px;margin:6px 0 10px}"
         "th,td{border-bottom:1px solid #e3e3e3;padding:4px 8px;text-align:left;vertical-align:top}th{background:#f4f4f6;font-weight:600}"
         "td.n{text-align:right;font-variant-numeric:tabular-nums}.good{color:#1f7a3a;font-weight:600}.fair{color:#a86a00;font-weight:600}"
         ".bad{color:#c4271b;font-weight:600}.pass{color:#1f7a3a}.fail{color:#c4271b;font-weight:600}svg{display:block;margin:4px 0 8px}"
         "@media print{body{margin:10mm}h2{page-break-before:auto}}</style></head><body>";
    h += "<h1>" + escape (title) + "</h1>";
    h += "<p class=\"note\">" + T ("Bass robustness: 100 = the bass holds on every playback system; under 50 it does not. "
                                   "Timecode ", "Robustesse du grave : 100 = le grave tient sur tous les systèmes de lecture ; sous 50, il ne tient pas. "
                                   "Timecode ") + Timecode::name (rate) + ".</p>";

    // Summary, one row a reel
    h += "<table><tr><th>" + T ("Reel", "Bobine") + "</th><th>" + T ("Start", "Début") + "</th><th>" + T ("Length", "Durée") + "</th><th>"
         + T ("Bass", "Grave") + "</th><th>" + T ("Robustness mean", "Robustesse moyenne") + "</th><th>" + T ("10 % worst", "10 % pires")
         + "</th><th>" + T ("Min", "Min") + "</th><th>" + T ("Under 50", "Sous 50") + "</th><th>" + T ("Bass in the LFE", "Grave dans le LFE")
         + "</th><th>" + T ("Phase Alert L / C / R", "Phase Alert L / C / R") + "</th><th>" + T ("LFE timing", "Calage LFE") + "</th><th>"
         + T ("Screen antiphase", "Antiphase écran") + "</th><th>" + T ("Checks", "Contrôles") + "</th></tr>";

    for (const auto& r : reels)
    {
        const int failed = (int) std::count_if (r.deliverables.begin(), r.deliverables.end(), [] (const DeliverableFinding& d) { return ! d.pass; });
        const bool bass = r.bassSeconds > 0.0;
        h += "<tr><td>" + escape (r.name) + (r.cancelled ? T (" (stopped)", " (arrêtée)") : "") + "</td><td>" + tc (r.startSeconds) + "</td><td class=\"n\">"
             + duration (r.durationSeconds) + "</td><td class=\"n\">" + duration (r.bassSeconds) + "</td>";
        h += bass ? "<td class=\"n " + std::string (scoreClass (r.robustnessMean)) + "\">" + number (r.robustnessMean, 0) + "</td><td class=\"n "
                        + scoreClass (r.robustnessP10) + "\">" + number (r.robustnessP10, 0) + "</td><td class=\"n " + scoreClass (r.robustnessMin) + "\">"
                        + number (r.robustnessMin, 0) + "</td><td class=\"n\">" + duration (r.secondsUnder50) + "</td><td class=\"n\">"
                        + number (100.0 * r.lfeShareMean, 0) + " %</td>"
                  : "<td colspan=\"5\">" + T ("no bass", "pas de grave") + "</td>";
        h += "<td class=\"n\">" + duration (r.alertSeconds[0]) + " / " + duration (r.alertSeconds[1]) + " / " + duration (r.alertSeconds[2]) + "</td><td class=\"n\">"
             + std::to_string (r.lfeTimingEvents) + "</td><td class=\"n\">" + (r.antiphaseWorstDb < -0.05 ? number (r.antiphaseWorstDb, 1) + " dB" : "-")
             + "</td><td class=\"" + (failed > 0 ? "fail" : "pass") + "\">" + (r.deliverables.empty() ? "-" : failed > 0 ? std::to_string (failed) + T (" failed", " en défaut") : "OK")
             + "</td></tr>";
    }

    h += "</table>";

    // Each reel
    for (const auto& r : reels)
    {
        h += "<h2>" + escape (r.name) + "</h2><p class=\"note\">" + tc (r.startSeconds) + " - " + tc (r.startSeconds + r.durationSeconds) + " ("
             + duration (r.durationSeconds) + "), " + number (r.sampleRate / 1000.0, 1) + " kHz. " + escape (r.timecodeSource) + ". " + escape (r.channels) + ".</p>";

        // The score over time, one point a second: under 50 in red
        if (! r.timeline.empty())
        {
            const double width = 1000.0, height = 70.0, n = (double) r.timeline.size();
            std::string path, low;
            bool penDown = false;

            for (size_t i = 0; i < r.timeline.size(); ++i)
            {
                const double x = width * (double) i / std::max (1.0, n - 1.0);

                if (r.timeline[i] < 0.0f)
                {
                    penDown = false;
                    continue;
                }

                const double y = height - height * r.timeline[i] / 100.0;
                path += (penDown ? "L" : "M") + number (x, 1) + " " + number (y, 1);
                penDown = true;

                if (r.timeline[i] < 50.0f)
                    low += "<rect x=\"" + number (x, 1) + "\" y=\"0\" width=\"" + number (std::max (1.0, width / n), 1) + "\" height=\"" + number (height, 0)
                         + "\" fill=\"#c4271b\" opacity=\"0.12\"/>";
            }

            h += "<svg viewBox=\"0 0 " + number (width, 0) + " " + number (height, 0) + "\" width=\"100%\" height=\"70\" preserveAspectRatio=\"none\">"
                 "<rect width=\"100%\" height=\"100%\" fill=\"#f7f7f9\"/>" + low
                 + "<line x1=\"0\" x2=\"" + number (width, 0) + "\" y1=\"" + number (height / 2.0, 1) + "\" y2=\"" + number (height / 2.0, 1)
                 + "\" stroke=\"#bbb\" stroke-dasharray=\"4 4\"/><path d=\"" + path + "\" fill=\"none\" stroke=\"#3a6fd8\" stroke-width=\"1.5\"/></svg>";
            h += "<p class=\"note\">" + T ("Bass robustness over the reel, the lowest value of each second (dashed: 50; red: under 50; gaps: no bass).",
                                           "Robustesse du grave le long de la bobine, la plus basse de chaque seconde (tirets : 50 ; rouge : sous 50 ; trous : pas de grave).") + "</p>";
        }

        if (! r.worst.empty())
        {
            h += "<table><tr><th>" + T ("Worst moments", "Pires moments") + "</th><th>" + T ("Robustness", "Robustesse") + "</th></tr>";

            for (const auto& m : r.worst)
                h += "<tr><td>" + tc (m.at) + "</td><td class=\"n " + scoreClass (m.robustness) + "\">" + number (m.robustness, 0) + "</td></tr>";

            h += "</table>";
        }

        if (! r.deliverables.empty())
        {
            h += "<table><tr><th>" + T ("Deliverable check", "Contrôle du livrable") + "</th><th>" + T ("Result", "Résultat") + "</th><th>"
                 + T ("Where", "Où") + "</th></tr>";

            for (const auto& d : r.deliverables)
                h += "<tr><td>" + escape (d.name) + "</td><td class=\"" + (d.pass ? "pass" : "fail") + "\">" + escape (d.result) + "</td><td>"
                     + (d.at >= 0.0 ? tc (d.at) : std::string ("-")) + "</td></tr>";

            h += "</table>";
        }

        h += "<table><tr><th>" + T ("Start", "Début") + "</th><th>" + T ("End", "Fin") + "</th><th>" + T ("What", "Quoi") + "</th><th>"
             + T ("Ch.", "Voie") + "</th><th>" + T ("Detail", "Détail") + "</th></tr>";

        if (r.events.empty())
            h += "<tr><td colspan=\"5\">" + T ("Nothing logged.", "Rien de noté.") + "</td></tr>";

        for (const auto& e : r.events)
            h += "<tr><td>" + tc (e.start) + "</td><td>" + tc (e.end) + "</td><td>" + EventLog::kindName (e.kind, french) + "</td><td>"
                 + LogEvent::channelName (e.channel) + "</td><td>" + escape (e.detail) + "</td></tr>";

        h += "</table>";
    }

    h += "<p class=\"note\">SubGUARD. " + T ("Events are stamped when they are seen: the Phase Alert averages over its Integration time.",
                                              "Les événements sont datés quand ils sont vus : la Phase Alert moyenne sur son temps d'intégration.") + "</p></body></html>";
    return h;
}

} // namespace subguard
