// Writes a set of 5.1 test files for SubGUARD and measures each of them with
// the analysis engine, so that the read-out to expect is known.
//
//   subguard_testfiles <output folder>
//
// Files are 48 kHz, 24-bit WAV, channel order L R C LFE Ls Rs (the "5.1 ITU"
// format of the app). The surrounds are silent.
//
// A second set, in <output folder>/room-sim-pro, isolates one thing at a time
// for Room Sim Pro (bass in one channel, the LFE related or not to C, a quiet
// mix, one layer only), each rated in the three default Room Sim Pro rooms so
// that what the two lenses show is known.

#include "../core/AlertTracker.h"
#include "../core/LfeEngine.h"
#include "../core/PlaybackSim.h"
#include "../core/RoomPro.h"
#include "../core/TestSignals.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace subguard;

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr int sampleRate = 48000;
    constexpr double leadIn = 1.0;          // seconds of silence before the signal

    using Frame = TestFrame;
    using Scene = TestScene;

    void writeWav (const std::string& path, const std::vector<float>& interleaved6)
    {
        FILE* f = std::fopen (path.c_str(), "wb");

        if (f == nullptr)
        {
            std::fprintf (stderr, "cannot write %s\n", path.c_str());
            return;
        }

        const uint32_t channels = 6, bytes = 3, dataSize = (uint32_t) interleaved6.size() * bytes;
        const auto u32 = [f] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
        const auto u16 = [f] (uint16_t v) { std::fwrite (&v, 2, 1, f); };

        std::fwrite ("RIFF", 1, 4, f);  u32 (4 + 8 + 40 + 8 + dataSize);
        std::fwrite ("WAVE", 1, 4, f);
        std::fwrite ("fmt ", 1, 4, f);  u32 (40);
        u16 (0xfffe);                                   // WAVE_FORMAT_EXTENSIBLE
        u16 ((uint16_t) channels);
        u32 ((uint32_t) sampleRate);
        u32 ((uint32_t) sampleRate * channels * bytes);
        u16 ((uint16_t) (channels * bytes));
        u16 (24);
        u16 (22);                                       // extension size
        u16 (24);                                       // valid bits
        u32 (0x3f);                                     // FL FR FC LFE BL BR
        const unsigned char pcmGuid[16] = { 1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71 };
        std::fwrite (pcmGuid, 1, 16, f);
        std::fwrite ("data", 1, 4, f);  u32 (dataSize);

        std::vector<unsigned char> out (interleaved6.size() * bytes);

        for (size_t i = 0; i < interleaved6.size(); ++i)
        {
            const auto v = (int32_t) std::lround (std::clamp ((double) interleaved6[i], -1.0, 1.0) * 8388607.0);
            out[i * 3] = (unsigned char) (v & 0xff);
            out[i * 3 + 1] = (unsigned char) ((v >> 8) & 0xff);
            out[i * 3 + 2] = (unsigned char) ((v >> 16) & 0xff);
        }

        std::fwrite (out.data(), 1, out.size(), f);
        std::fclose (f);
    }

    //==========================================================================
    struct Reading
    {
        bool hasBass = false;
        double robustness = 0, interaction = 0, dependence = 0, balanceDb = 0;
        std::array<double, 3> correlation {};       // L, C, R: mean over 30-100 Hz
        std::array<double, 3> alert {};             // speaker indicator levels
        std::string transients = "none";
        double peak = 0.0;
    };

    Reading measure (const std::vector<float>& interleaved6)
    {
        LfeEngine engine (sampleRate);
        PlaybackSimulator simulator;
        AlertTracker tracker;
        AlertTracker::Options options;
        Settings settings;
        Reading r;

        const size_t frames = interleaved6.size() / 6, block = 800;
        std::vector<float> four (block * 4);
        std::vector<double> delays, attackDelays;
        int mixAttacks = 0, inLfe = 0, inverted = 0;
        double scoreSum = 0, interactionSum = 0, dependenceSum = 0, balanceSum = 0;
        std::array<double, 3> correlationSum {}, alertSum {};
        int averaged = 0;

        for (size_t pos = 0; pos + block <= frames; pos += block)
        {
            for (size_t i = 0; i < block; ++i)
            {
                const float* in = &interleaved6[(pos + i) * 6];
                four[i * 4 + chL] = in[0];
                four[i * 4 + chR] = in[1];
                four[i * 4 + chC] = in[2];
                four[i * 4 + chLFE] = in[3];
                r.peak = std::max ({ r.peak, (double) std::abs (in[0]), (double) std::abs (in[1]),
                                     (double) std::abs (in[2]), (double) std::abs (in[3]) });
            }

            engine.push (four.data(), (int) block);

            if (! engine.analyze (settings))
                continue;

            tracker.update (engine.result(), options, (double) block / sampleRate);

            for (const auto& e : engine.takeTransientEvents())
            {
                ++mixAttacks;

                if (! e.inLfe)
                    continue;

                ++inLfe;

                if (e.match > 0.5)
                {
                    delays.push_back (e.delayMs);
                    inverted += std::abs (e.phase) > 2.0 * pi / 3.0;
                }
                else if (e.lfeAttack)
                    attackDelays.push_back (e.attackDelayMs);
            }

            // read-outs averaged over the last four seconds of the file
            if (pos + (size_t) (4 * sampleRate) >= frames)
            {
                const auto t = simulator.compute (engine.result(), PlaybackSimulator::Level::strong);
                const auto& a = engine.result();
                r.hasBass = r.hasBass || t.hasBass;

                if (t.hasBass)
                {
                    scoreSum += t.robustness;
                    interactionSum += t.interaction;
                    dependenceSum += t.dependence;
                    balanceSum += t.lfeBalanceDb;
                }

                for (size_t c = 0; c < 3; ++c)
                {
                    double sum = 0.0;
                    int n = 0;

                    for (size_t g = 0; g < a.grid.size(); ++g)
                        if (a.grid[g] >= 30.0 && a.grid[g] <= 100.0)
                        {
                            sum += tracker.curves[c][g];
                            ++n;
                        }

                    correlationSum[c] += sum / std::max (n, 1);
                    alertSum[c] += tracker.speakerLevel[c];
                }

                ++averaged;
            }
        }

        const double n = std::max (averaged, 1);
        r.robustness = scoreSum / n;
        r.interaction = interactionSum / n;
        r.dependence = dependenceSum / n;
        r.balanceDb = balanceSum / n;

        for (size_t c = 0; c < 3; ++c)
        {
            r.correlation[c] = correlationSum[c] / n;
            r.alert[c] = alertSum[c] / n;
        }

        if (mixAttacks > 0)
        {
            char buffer[160];
            std::sort (delays.begin(), delays.end());

            std::sort (attackDelays.begin(), attackDelays.end());

            if (delays.empty() && ! attackDelays.empty())
                std::snprintf (buffer, sizeof (buffer), "%d attacks in the mix, %d of them in the LFE: LFE attack about %+.0f ms (unlike sound)",
                               mixAttacks, inLfe, attackDelays[attackDelays.size() / 2]);
            else if (delays.empty())
                std::snprintf (buffer, sizeof (buffer), "%d attacks in the mix, %d of them in the LFE", mixAttacks, inLfe);
            else
                std::snprintf (buffer, sizeof (buffer), "%d attacks in the mix, %d of them in the LFE: LFE %+.1f ms%s", mixAttacks, inLfe,
                               delays[delays.size() / 2], inverted * 2 > (int) delays.size() ? ", inverted" : "");

            r.transients = buffer;
        }

        return r;
    }

    //==========================================================================
    // Room Sim Pro: the final analysis of a file, rated in the default rooms
    Analysis finalAnalysis (const std::vector<float>& interleaved6)
    {
        LfeEngine engine (sampleRate);
        Settings settings;
        const size_t frames = interleaved6.size() / 6, block = 800;
        std::vector<float> four (block * 4);

        for (size_t pos = 0; pos + block <= frames; pos += block)
        {
            for (size_t i = 0; i < block; ++i)
            {
                const float* in = &interleaved6[(pos + i) * 6];
                four[i * 4 + chL] = in[0];
                four[i * 4 + chR] = in[1];
                four[i * 4 + chC] = in[2];
                four[i * 4 + chLFE] = in[3];
            }

            engine.push (four.data(), (int) block);
            engine.analyze (settings);
        }

        return engine.result();
    }

    std::string signedDb (double v)
    {
        char text[32];
        std::snprintf (text, sizeof (text), std::isnan (v) ? "--" : "%+.1f dB", v);
        return text;
    }

    /** What Room Sim Pro shows for a room: the loudness lens and the three causes, in words and numbers. */
    std::string describe (const RoomResult& r)
    {
        const auto range = [] (const std::vector<double>& v, double& lo, double& hi, double& mean)
        {
            lo = 1e9; hi = -1e9; mean = 0.0;
            int n = 0;

            for (double x : v)
                if (! std::isnan (x))
                {
                    lo = std::min (lo, x);
                    hi = std::max (hi, x);
                    mean += x;
                    ++n;
                }

            mean = n > 0 ? mean / n : std::nan ("");
            return n > 0;
        };

        std::string out;
        char line[400];
        double lo, hi, mean;

        // Lens 1: loudness, all / L C R / LFE
        const char* parts[] = { "All", "L C R alone", "LFE alone" };

        for (int p = 0; p < RoomResult::numParts; ++p)
        {
            if (r.approvedPhon[(size_t) p] <= 0.0 || ! range (r.seatPhon[(size_t) p], lo, hi, mean))
            {
                std::snprintf (line, sizeof (line), "      lens 1, %-12s nothing in the mix (mix 0 phon): heads dark grey\n", parts[p]);
            }
            else
            {
                std::snprintf (line, sizeof (line), "      lens 1, %-12s mix %.0f phon; heads %.0f to %.0f phon (mean %.0f)\n", parts[p],
                               r.approvedPhon[(size_t) p], lo, hi, mean);
            }

            out += line;
        }

        // Lens 2: the three causes, mean and extremes over the seats
        const char* causes[] = { "Room", "Speakers", "Mix" };

        for (int c = 0; c < RoomResult::numCauses; ++c)
        {
            if (! range (r.seatCauseDb[(size_t) c], lo, hi, mean))
                std::snprintf (line, sizeof (line), "      lens 2, %-9s --\n", causes[c]);
            else
                std::snprintf (line, sizeof (line), "      lens 2, %-9s seats %s to %s (mean %s)%s\n", causes[c], signedDb (lo).c_str(),
                               signedDb (hi).c_str(), signedDb (mean).c_str(),
                               std::max (std::abs (lo), std::abs (hi)) < 0.5 ? ": all grey" : "");

            out += line;
        }

        // The layered view, lens 1: the mix's own loudness in each layer
        std::snprintf (line, sizeof (line), "      layers (mix): infra %.0f dB above the threshold, very low %.0f phon, low %.0f phon\n",
                       r.approvedLayerPhon[RoomResult::allParts][0], r.approvedLayerPhon[RoomResult::allParts][1],
                       r.approvedLayerPhon[RoomResult::allParts][2]);
        out += line;

        std::snprintf (line, sizeof (line), "      scores: coverage %.0f, placement %.0f, mix %.0f; bass heard: %.0f %% likely\n",
                       std::max (0.0, r.ratingScore[RoomResult::coverageRating]) + 0.0, std::max (0.0, r.ratingScore[RoomResult::speakersRating]) + 0.0,
                       std::max (0.0, r.ratingScore[RoomResult::mixRating]) + 0.0,
                       100.0 * (r.seatHeard.empty() ? 0.0 : *std::max_element (r.seatHeard.begin(), r.seatHeard.end())));
        out += line;
        return out;
    }

}

//==============================================================================
int main (int argc, char** argv)
{
    const std::string folder = argc > 1 ? argv[1] : ".";

    // The signals, shared with the app (core/TestSignals.cpp)
    std::vector<Scene> scenes, pro;

    for (const auto& scene : TestSignals::all())
        (scene.roomSimPro ? pro : scenes).push_back (scene);

    //==========================================================================
    const std::string notes = folder + "/README.txt";
    FILE* readme = std::fopen (notes.c_str(), "w");

    if (readme == nullptr)
    {
        std::fprintf (stderr, "cannot write into %s (does the folder exist?)\n", folder.c_str());
        return 1;
    }

    std::fprintf (readme, "SubGUARD test files\n===================\n\n"
                          "48 kHz, 24-bit, 5.1 in the order L R C LFE Ls Rs: choose the format \"5.1 ITU\" in the app.\n"
                          "Each file starts with one second of silence. Loop a file for a steadier read-out.\n"
                          "Default settings assumed: transient check switched on (Transients panel), level Strong, both polarities, LFE +10 dB box checked.\n"
                          "\"Measured\" is what the analysis engine itself reads at the end of the file.\n\n");

    std::printf ("%-36s %6s %6s %6s %7s   %-17s %s\n", "file", "score", "inter", "depen", "LFE dB", "corr L / C / R", "transients");

    for (const auto& scene : scenes)
    {
        const auto frames = (size_t) ((scene.seconds + leadIn) * sampleRate);
        std::vector<float> audio (frames * 6, 0.0f);
        auto generate = scene.make();

        for (size_t i = (size_t) (leadIn * sampleRate); i < frames; ++i)
        {
            const auto f = generate ((double) i / sampleRate - leadIn);
            audio[i * 6] = (float) f.l;
            audio[i * 6 + 1] = (float) f.r;
            audio[i * 6 + 2] = (float) f.c;
            audio[i * 6 + 3] = (float) f.lfe;
        }

        writeWav (folder + "/" + scene.file + ".wav", audio);
        const auto m = measure (audio);

        char score[16] = "--", detail[160] = "";

        if (m.hasBass)
        {
            std::snprintf (score, sizeof (score), "%.0f", m.robustness);
            std::snprintf (detail, sizeof (detail), "interaction %.0f / 100, LFE dependence %.0f / 100, LFE %+.1f dB against the screen",
                           100.0 * (1.0 - m.interaction), 100.0 * (1.0 - m.dependence), m.balanceDb);
        }

        std::printf ("%-36s %6s %6.0f %6.0f %+7.1f   %+.2f %+.2f %+.2f  %s\n", scene.file.c_str(), score,
                     100.0 * (1.0 - m.interaction), 100.0 * (1.0 - m.dependence), m.balanceDb,
                     m.correlation[0], m.correlation[1], m.correlation[2], m.transients.c_str());

        std::fprintf (readme, "%s.wav\n  %s\n  Look for: %s\n  Measured: score %s%s%s\n            correlation L %+.2f, C %+.2f, R %+.2f; speaker alerts L %.0f %%, C %.0f %%, R %.0f %%\n"
                              "            transients: %s\n            peak level %.1f dBFS\n\n",
                      scene.file.c_str(), scene.title.c_str(), scene.lookFor.c_str(), score, m.hasBass ? "; " : "", detail,
                      m.correlation[0], m.correlation[1], m.correlation[2],
                      100.0 * m.alert[0], 100.0 * m.alert[1], 100.0 * m.alert[2], m.transients.c_str(),
                      20.0 * std::log10 (m.peak + 1e-12));
    }

    std::fclose (readme);
    std::printf ("\n%zu files and README.txt written to %s\n", scenes.size(), folder.c_str());

    //==========================================================================
    const std::string proFolder = folder + "/room-sim-pro";
    std::filesystem::create_directories (proFolder);
    FILE* proReadme = std::fopen ((proFolder + "/README.txt").c_str(), "w");

    if (proReadme == nullptr)
    {
        std::fprintf (stderr, "cannot write into %s\n", proFolder.c_str());
        return 1;
    }

    std::fprintf (proReadme, "SubGUARD - Room Sim Pro test files\n==================================\n\n"
                             "48 kHz, 24-bit, 5.1 (L R C LFE Ls Rs): format \"5.1 ITU\" in the app. Loop each file.\n"
                             "Each file isolates one thing. Load default_rooms.subguardrooms (File > Load rooms) to get the three rooms measured below.\n"
                             "Lens 1 = \"Who gets bass, how much\" (filter All / L C R / LFE). Lens 2 = \"Why\" (filter Room / Speakers / Mix).\n"
                             "\"Measured\" is what the engine itself computes at the end of the file, level Strong, LFE +10 dB.\n\n");

    // The three rooms the measurements are made in: load them with File > Load rooms
    const auto rooms = ProRooms::defaults();

    if (FILE* config = std::fopen ((proFolder + "/default_rooms.subguardrooms").c_str(), "w"))
    {
        const auto text = rooms.toText();
        std::fwrite (text.data(), 1, text.size(), config);
        std::fclose (config);
    }

    std::array<RoomPaths, ProRooms::count> paths;
    std::vector<double> pathsGrid;
    PlaybackSimulator simulator;

    for (const auto& scene : pro)
    {
        const auto frames = (size_t) ((scene.seconds + leadIn) * sampleRate);
        std::vector<float> audio (frames * 6, 0.0f);
        auto generate = scene.make();

        for (size_t i = (size_t) (leadIn * sampleRate); i < frames; ++i)
        {
            const auto f = generate ((double) i / sampleRate - leadIn);
            audio[i * 6] = (float) f.l;
            audio[i * 6 + 1] = (float) f.r;
            audio[i * 6 + 2] = (float) f.c;
            audio[i * 6 + 3] = (float) f.lfe;
        }

        writeWav (proFolder + "/" + scene.file + ".wav", audio);
        const auto a = finalAnalysis (audio);
        const auto t = simulator.compute (a, PlaybackSimulator::Level::strong);

        if (a.grid != pathsGrid)
        {
            for (int k = 0; k < ProRooms::count; ++k)
                paths[(size_t) k] = ProRoomModel::prepare (rooms.rooms[(size_t) k], a.grid);

            pathsGrid = a.grid;
        }

        std::fprintf (proReadme, "%s.wav\n  %s\n  Look for: %s\n  Measured:\n", scene.file.c_str(), scene.title.c_str(), scene.lookFor.c_str());
        std::printf ("%s\n", scene.file.c_str());

        for (int k = 0; k < ProRooms::count; ++k)
        {
            const auto res = RoomSimulator::rate (paths[(size_t) k], a, t, simulator.tuning, PlaybackSimulator::Level::strong, 10.0);
            const auto text = describe (res);
            std::fprintf (proReadme, "    room %d, %s:\n%s", k + 1, rooms.rooms[(size_t) k].name.c_str(), text.c_str());

            if (k == 1)
                std::printf ("%s", text.c_str());
        }

        std::fprintf (proReadme, "\n");
    }

    std::fclose (proReadme);
    std::printf ("\n%zu Room Sim Pro files and README.txt written to %s\n", pro.size(), proFolder.c_str());
    return 0;
}
