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

    // LFE recorded 10 dB down plays as loud as a screen channel
    const double asHeard = std::pow (10.0, -10.0 / 20.0);

    //==========================================================================
    /** Low-frequency noise, about 15-180 Hz, the same sequence for the same seed. */
    class Rumble
    {
    public:
        explicit Rumble (uint32_t seed, double rmsLevel = 0.05) : state (seed * 2654435761u + 12345u), level (rmsLevel)
        {
            // measure the raw level once so that the output has the requested RMS
            double sum = 0.0;
            Rumble* self = this;

            for (int i = 0; i < 96000; ++i)
            {
                const double v = self->raw();
                sum += v * v;
            }

            scale = level / std::sqrt (sum / 96000.0);
            state = seed * 2654435761u + 12345u;
            for (auto& v : lp) v = 0.0;
            for (auto& v : hp) v = 0.0;
        }

        double next()   { return raw() * scale; }

    private:
        double raw()
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            double x = (double) (state & 0xffffff) / (double) 0x800000 - 1.0;

            const double kl = 1.0 - std::exp (-2.0 * pi * 180.0 / sampleRate);
            const double kh = 1.0 - std::exp (-2.0 * pi * 15.0 / sampleRate);

            for (auto& v : lp)  { v += kl * (x - v); x = v; }
            for (auto& v : hp)  { v += kh * (x - v); x -= v; }

            return x;
        }

        uint32_t state;
        double level, scale = 1.0;
        double lp[3] = {}, hp[2] = {};
    };

    // A kick: pitch sweep 120 -> 45 Hz, 2 ms attack
    double kick (double t)
    {
        if (t < 0.0)
            return 0.0;

        const double phase = 2.0 * pi * (45.0 * t + 75.0 * 0.03 * (1.0 - std::exp (-t / 0.03)));
        return std::sin (phase) * std::exp (-t / 0.09) * (1.0 - std::exp (-t / 0.002));
    }

    // A low boom: single frequency, slow attack
    double boom (double t, double hz)
    {
        if (t < 0.0)
            return 0.0;

        return std::sin (2.0 * pi * hz * t) * std::exp (-t / 0.25) * (1.0 - std::exp (-t / 0.03));
    }

    // Time since the last hit, for hits every `period` seconds
    double sinceHit (double t, double period)   { return t < 0.0 ? -1.0 : std::fmod (t, period); }

    //==========================================================================
    struct Frame { double l = 0, r = 0, c = 0, lfe = 0; };

    struct Scene
    {
        std::string file, title, lookFor;
        double seconds;
        std::function<std::function<Frame (double)>()> make;    // fresh generator on each call
    };

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

    /** A second-order low-pass or high-pass (Butterworth, bilinear), for band-limited test signals. */
    struct TestFilter
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;

        static TestFilter make (double hz, bool high)
        {
            TestFilter f;
            const double k = std::tan (pi * hz / sampleRate), q = std::sqrt (0.5), norm = 1.0 / (1.0 + k / q + k * k);
            f.b0 = high ? norm : k * k * norm;
            f.b1 = high ? -2.0 * norm : 2.0 * f.b0;
            f.b2 = f.b0;
            f.a1 = 2.0 * (k * k - 1.0) * norm;
            f.a2 = (1.0 - k / q + k * k) * norm;
            return f;
        }

        double process (double x)
        {
            const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
    };
}

//==============================================================================
int main (int argc, char** argv)
{
    const std::string folder = argc > 1 ? argv[1] : ".";

    // Shared building blocks. Each scene builds fresh generators so that every
    // file is the same from one run to the next.
    const auto bed = [] (uint32_t seed, double level = 0.05) { return std::make_shared<Rumble> (seed, level); };

    std::vector<Scene> scenes;

    const auto add = [&scenes] (std::string file, std::string title, std::string lookFor, double seconds,
                                std::function<std::function<Frame (double)>()> make)
    {
        scenes.push_back ({ std::move (file), std::move (title), std::move (lookFor), seconds, std::move (make) });
    };

    add ("01_clean_mix", "Clean mix: screen channels carry the bass, LFE unrelated and 10 dB below them",
         "Score near 100. No alert, speakers idle, curves near 0.", 20.0, [bed]
         {
             auto l = bed (1), c = bed (2), r = bed (3), x = bed (4);
             return [=] (double) { return Frame { l->next(), r->next(), c->next(), 0.316 * asHeard * x->next() }; };
         });

    add ("02_lfe_copies_centre", "LFE is a copy of the centre channel, as loud when heard",
         "Score 0. C speaker alerts, amber heat map, C curve at +1.", 20.0, [bed]
         {
             auto l = bed (1), c = bed (2), r = bed (3);
             return [=] (double) { const double v = c->next(); return Frame { 0.3 * l->next(), 0.3 * r->next(), v, asHeard * v }; };
         });

    add ("03_lfe_inverted_copy", "LFE is the centre channel with its polarity flipped",
         "Score 0. C speaker alerts, red heat map, C curve at -1. Still alerts in 'Out of phase only'. Session polarity for C goes negative.",
         20.0, [bed]
         {
             auto l = bed (1), c = bed (2), r = bed (3);
             return [=] (double) { const double v = c->next(); return Frame { 0.3 * l->next(), 0.3 * r->next(), v, -asHeard * v }; };
         });

    add ("04_lfe_copy_5ms_late", "LFE is a copy of the centre channel, 5 ms late",
         "Low score. C curve swings from +1 at the bottom to -1 at 100 Hz (5 ms is half a period there).", 20.0, [bed]
         {
             auto l = bed (1), c = bed (2), r = bed (3);
             auto line = std::make_shared<std::vector<double>> ((size_t) (0.005 * sampleRate), 0.0);
             auto pos = std::make_shared<size_t> (0);
             return [=] (double)
             {
                 const double v = c->next(), late = (*line)[*pos];
                 (*line)[*pos] = v;
                 *pos = (*pos + 1) % line->size();
                 return Frame { 0.3 * l->next(), 0.3 * r->next(), v, asHeard * late };
             };
         });

    add ("05_lfe_only", "All the bass in the LFE, screen channels silent",
         "Score about 20-25: the room alone decides the bass. 'LFE dependence' low, LFE carries 100 %. No phase alert.", 20.0, [bed]
         {
             auto x = bed (4);
             return [=] (double) { return Frame { 0, 0, 0, asHeard * x->next() }; };
         });

    add ("06_lfe_6dB_above_screen", "Unrelated LFE, 6 dB above the screen channels when heard",
         "Score about 20-30. LFE-against-screen line in orange (about +6 dB). No phase alert.", 20.0, [bed]
         {
             auto c = bed (2), x = bed (4);
             return [=] (double) { return Frame { 0, 0, c->next(), 2.0 * asHeard * x->next() }; };
         });

    add ("07_lfe_equal_unrelated", "Unrelated LFE, as loud as the screen channels when heard",
         "Score about 60-70: no interaction, but half the bass rests on the LFE.", 20.0, [bed]
         {
             auto c = bed (2), x = bed (4);
             return [=] (double) { return Frame { 0, 0, c->next(), asHeard * x->next() }; };
         });

    add ("08_lfe_partly_shared", "LFE 10 dB below the centre channel, 60 % copied from it",
         "Score about 60, 'dangerous but passable'. C curve around +0.6, C speaker alerts.", 20.0, [bed]
         {
             auto c = bed (2), x = bed (4);
             return [=] (double) { const double v = c->next(); return Frame { 0, 0, v, 0.316 * asHeard * (0.6 * v + 0.8 * x->next()) }; };
         });

    add ("09_per_channel", "L shares its bass with the LFE in phase, R out of phase, C is unrelated",
         "L speaker alerts in amber, R in red, C stays idle. L curve positive, R curve negative. The score stays high: what L adds to the LFE, R takes away, so the screen as a whole does not interact with it.", 20.0, [bed]
         {
             auto shared = bed (5), opposed = bed (6), c = bed (2);
             return [=] (double)
             {
                 const double a = shared->next(), b = opposed->next();
                 return Frame { a, b, 0.5 * c->next(), 0.5 * asHeard * (a - b) };
             };
         });

    add ("10_overlap_60_120", "Screen and LFE both loud and unrelated",
         "Overlap marks under 60-90 and 90-120 Hz turn amber. No phase alert.", 20.0, [bed]
         {
             auto c = bed (2, 0.1), x = bed (4, 0.1);
             return [=] (double) { return Frame { 0, 0, c->next(), asHeard * x->next() }; };
         });

    // Transients: a hit in the centre channel every 1.5 s, with the LFE variant named in the title
    const auto hits = [&add, bed] (std::string file, std::string title, std::string lookFor, double lfeDelay, double lfeGain,
                                   double level, bool lowBoom)
    {
        add (std::move (file), std::move (title), std::move (lookFor), 20.0, [=]
        {
            auto bedC = bed (2, lowBoom ? 0.04 : 0.0005), bedLfe = bed (4, 0.0005);
            auto high = std::make_shared<double> (0.0);
            return [=] (double t)
            {
                const double s = sinceHit (t, 1.5), late = sinceHit (t - lfeDelay, 1.5);
                const auto shape = [lowBoom] (double u) { return lowBoom ? boom (u, 28.0) : kick (u); };
                double c = 0.4 * shape (s), lfe = 0.4 * asHeard * lfeGain * shape (late);

                // for the low boom, the centre also carries a louder bed above 60 Hz
                double noise = bedC->next();

                if (lowBoom)
                {
                    *high += 0.008 * (noise - *high);
                    noise = 4.0 * (noise - *high);
                }

                return Frame { 0, 0, level * (c + noise), level * (lfe + bedLfe->next()) };
            };
        });
    };

    hits ("11_transients_aligned", "Kicks in the centre channel, the LFE hits at the same instant",
          "Transients strip: 'Aligned, in phase'.", 0.0, 1.0, 1.0, false);
    hits ("12_transients_lfe_12ms_late", "Kicks in the centre channel, the LFE hits 12 ms late",
          "Transients strip: 'LFE late 12.0 ms'.", 0.012, 1.0, 1.0, false);
    hits ("13_transients_lfe_6ms_early", "Kicks in the centre channel, the LFE hits 6 ms early",
          "Transients strip: 'LFE early 6.0 ms'.", -0.006, 1.0, 1.0, false);
    hits ("14_transients_inverted", "Kicks in the centre channel, the LFE hits at the same instant with flipped polarity",
          "Transients strip: 'Aligned, polarity inverted'.", 0.0, -1.0, 1.0, false);
    hits ("15_transients_very_quiet", "Same as file 12, 60 dB lower (peaks around -68 dBFS)",
          "Transients strip: 'LFE late 12.0 ms', exactly as at full level. Set the level range to -120 dB to see the curves.",
          0.012, 1.0, 0.001, false);
    hits ("16_transients_low_boom_under_bed", "28 Hz booms in the centre under a louder 60-180 Hz bed; the LFE boom is 8 ms late",
          "Transients strip: 'LFE late' around 8-10 ms with a wide +/-, or 'timing not measurable': a single low frequency is hard to time.",
          0.008, 1.0, 1.0, true);

    add ("17_level_calibration", "Pure tones: 40 Hz at -20 dBFS in the LFE, 80 Hz at -20 dBFS in the centre, 8 Hz at -30 dBFS in the LFE",
         "Cursor read-out: LFE -20.0 dB at 40 Hz, LCR -23.0 dB at 80 Hz (the screen sum is shown 3 dB down), LFE -30.0 dB at 8 Hz.",
         20.0, []
         {
             return [] (double t)
             {
                 if (t < 0.0)
                     return Frame {};

                 return Frame { 0, 0, 0.1 * std::sin (2.0 * pi * 80.0 * t),
                                0.1 * std::sin (2.0 * pi * 40.0 * t) + 0.0316 * std::sin (2.0 * pi * 8.0 * t) };
             };
         });

    add ("18_silence", "Digital silence", "Score shows '--'. Nothing moves.", 10.0, [] { return [] (double) { return Frame {}; }; });

    // Music-like: bass line in L, R and LFE, kick (thump and click) in the centre, hi-hat off the beat in L and R
    const auto music = [&add] (std::string file, std::string title, std::string lookFor, bool lfeKick, double lfeDelay, double lfeGain)
    {
        add (std::move (file), std::move (title), std::move (lookFor), 30.0, [=]
        {
            auto rng = std::make_shared<std::mt19937> (77u);
            auto low = std::make_shared<double> (0.0);
            return [=] (double t)
            {
                if (t < 0.0)
                    return Frame {};

                std::normal_distribution<double> normal (0.0, 1.0);
                const double beat = 0.5, onBeat = std::fmod (t, beat), offBeat = std::fmod (t + beat / 2.0, beat);
                const double click = onBeat < 0.004 ? normal (*rng) * std::exp (-onBeat / 0.0012) : 0.0;
                const double white = normal (*rng);
                *low += 0.35 * (white - *low);
                const double hat = offBeat < 0.05 ? (white - *low) * std::exp (-offBeat / 0.012) : 0.0;
                const double bass = 0.25 * std::sin (2.0 * pi * 55.0 * t) * (0.7 + 0.3 * std::sin (2.0 * pi * 0.5 * t))
                                      + 0.1 * std::sin (2.0 * pi * 110.0 * t);
                const double lfe = 0.3 * asHeard * std::sin (2.0 * pi * 55.0 * t + 1.0)
                                     + (lfeKick ? 0.5 * asHeard * lfeGain * kick (std::fmod (t - lfeDelay + beat, beat)) : 0.0);
                return Frame { 0.5 * (0.7 * bass + 0.15 * hat), 0.5 * (0.7 * bass - 0.12 * hat), 0.5 * (0.5 * kick (onBeat) + 0.25 * click), 0.5 * lfe };
            };
        });
    };

    music ("19_music_kick_in_lfe_12ms_late", "Bass line, kick and hi-hat; the LFE carries the bass and the kick, 12 ms late",
           "Transients strip: 'LFE late' around 12 ms on every kick. With Release lowered to about 10 %, the hi-hats are found too: 'nothing in the LFE'.",
           true, 0.012, 1.0);
    music ("20_music_kick_in_lfe_inverted", "Same music; the LFE kick is on time with flipped polarity",
           "Transients strip: the kicks are in the LFE: 'Aligned, polarity inverted'.", true, 0.0, -1.0);
    music ("21_music_kick_not_in_lfe", "Same music; the LFE only carries the bass line, no kick",
           "Transients strip: attacks of the mix are counted, none of them in the LFE.", false, 0.0, 1.0);

    add ("22_unlike_lfe_send_200ms_late", "Kick above 60 Hz in the centre; the LFE answers with a 30 Hz thud, 200 ms late",
         "Transients strip: 'LFE attack late' around 200 ms, timed attack against attack. With the search range under 200 ms: 'nothing in the LFE'.",
         20.0, []
         {
             return [] (double t)
             {
                 const double u = sinceHit (t, 1.5), v = sinceHit (t - 0.2, 1.5);
                 const double c = u < 0.0 ? 0.0 : 0.4 * std::sin (2.0 * pi * 95.0 * u) * std::exp (-u / 0.06) * (1.0 - std::exp (-u / 0.001));
                 const double lfe = v < 0.0 ? 0.0 : 0.3 * std::sin (2.0 * pi * 30.0 * v) * std::exp (-v / 0.2) * (1.0 - std::exp (-v / 0.01));
                 return Frame { 0, 0, c, lfe };
             };
         });

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
    // Room Sim Pro: one thing at a time
    std::vector<Scene> pro;

    const auto addPro = [&pro] (std::string file, std::string title, std::string lookFor, std::function<std::function<Frame (double)>()> make)
    {
        pro.push_back ({ std::move (file), std::move (title), std::move (lookFor), 20.0, std::move (make) });
    };

    // A delay line, for an LFE that copies the centre late
    const auto delayed = [] (double seconds)
    {
        auto line = std::make_shared<std::vector<double>> ((size_t) std::max (1.0, seconds * sampleRate), 0.0);
        auto pos = std::make_shared<size_t> (0);
        return [line, pos] (double v)
        {
            const double out = (*line)[*pos];
            (*line)[*pos] = v;
            *pos = (*pos + 1) % line->size();
            return out;
        };
    };

    addPro ("P01_bass_L_only", "Bass in L only (15-180 Hz noise), nothing else",
            "Lens 1, L C R: the heads near the L speaker are the loudest. Lens 1, LFE: nothing (dark grey). "
            "Lens 2, Mix: all grey (no LFE, so no LFE / L C R interference). Lens 2, Speakers: grey (one speaker alone).",
            [bed] { auto l = bed (1); return [=] (double) { return Frame { l->next(), 0, 0, 0 }; }; });
    addPro ("P02_bass_C_only", "Bass in C only", "As P01, the C speaker the loudest place. Compare with P01 and P03: the bass follows its speaker.",
            [bed] { auto c = bed (2); return [=] (double) { return Frame { 0, 0, c->next(), 0 }; }; });
    addPro ("P03_bass_R_only", "Bass in R only", "As P01, mirrored: the heads near R get the most.",
            [bed] { auto r = bed (3); return [=] (double) { return Frame { 0, r->next(), 0, 0 }; }; });
    addPro ("P04_bass_LR_same", "The same bass in L and R (a mono bass as a phantom centre)",
            "Lens 2, Speakers: in the approved mix L and R add exactly in step; in the room they arrive at different times, "
            "so they add less: the heads go blue. Mix: grey (no LFE). This is the venue, not the mix.",
            [bed] { auto m = bed (5); return [=] (double) { const double v = m->next(); return Frame { 0.7 * v, 0.7 * v, 0, 0 }; }; });
    addPro ("P05_bass_LR_unrelated", "Different bass in L and in R (two unrelated noises)",
            "Lens 2, Speakers: grey, unlike P04: two unrelated signals cannot interfere. Lens 1 shows each side louder near its speaker.",
            [bed] { auto l = bed (1), r = bed (3); return [=] (double) { return Frame { 0.7 * l->next(), 0.7 * r->next(), 0, 0 }; }; });
    addPro ("P06_lfe_only", "All the bass in the LFE, the screen channels silent",
            "Lens 1, LFE: where the subwoofers reach. Lens 1, L C R: nothing. Lens 2, Speakers: the two subwoofers against each other. Mix: grey.",
            [bed] { auto x = bed (4); return [=] (double) { return Frame { 0, 0, 0, asHeard * x->next() }; }; });
    addPro ("P07_C_and_lfe_unrelated", "C and LFE as loud, unrelated to each other",
            "Lens 2, Mix: all grey: an LFE that shares nothing with C cannot add to it or cancel it. Lens 1: both parts lit.",
            [bed] { auto c = bed (2), x = bed (4); return [=] (double) { return Frame { 0, 0, c->next(), asHeard * x->next() }; }; });
    addPro ("P08_lfe_copy_of_C", "The LFE is a copy of C, 6 dB below it when heard",
            "Lens 2, Mix: in the approved mix C and the LFE add exactly in step; in the room the subwoofers and C arrive at "
            "different times, so they add less: the heads go blue (a loss caused by the mix). This is what the mixing assistant will work on.",
            [bed] { auto c = bed (2); return [=] (double) { const double v = c->next(); return Frame { 0, 0, v, 0.5 * asHeard * v }; }; });
    addPro ("P09_lfe_copy_of_C_inverted", "The LFE is a copy of C with its polarity flipped, 6 dB below it",
            "Lens 2, Mix: in the approved mix the inverted LFE takes bass away from C; in the room it takes away less: "
            "the heads go red (the room undoes part of the cancellation). The opposite of P08.",
            [bed] { auto c = bed (2); return [=] (double) { const double v = c->next(); return Frame { 0, 0, v, -0.5 * asHeard * v }; }; });
    addPro ("P10_lfe_copy_of_C_5ms_late", "The LFE is a copy of C, 5 ms late",
            "In the approved mix, 5 ms late cancels C around 100 Hz (half a period). In the room the cancellation is partly undone: "
            "lens 2, Mix goes red, and lens 1 shows heads louder than the mix. The layered view: the low layer moves more than the very low one.",
            [bed, delayed] { auto c = bed (2); auto late = delayed (0.005);
                             return [=] (double) { const double v = c->next(); return Frame { 0, 0, v, asHeard * late (v) }; }; });
    addPro ("P11_inaudible_bass", "Bass in C and an unrelated LFE, 60 dB down (about -86 dBFS)",
            "Lens 1: every head dark grey, 0 phon, as the mix itself. Lens 2: grey and flat: nobody hears this bass, so nothing is shown, "
            "and the scores stay at 100.",
            [bed] { auto c = bed (2, 0.00005), x = bed (4, 0.00005); return [=] (double) { return Frame { 0, 0, c->next(), asHeard * x->next() }; }; });
    addPro ("P12_very_low_only", "Bass in C between 20 and 60 Hz only",
            "Layered view: only the middle layer (very low, 20-60 Hz) moves; the low layer says 'nothing in the mix'.",
            [bed] { auto c = bed (2, 0.25); auto f = std::make_shared<std::array<TestFilter, 4>>();
                    for (auto& x : *f) x = TestFilter::make (45.0, false);
                    return [=] (double) { double v = c->next(); for (auto& x : *f) v = x.process (v); return Frame { 0, 0, v, 0 }; }; });
    addPro ("P13_low_only", "Bass in C between 60 and 120 Hz only",
            "Layered view: only the top layer (low, 60-120 Hz) moves.",
            [bed] { auto c = bed (2, 0.12); auto f = std::make_shared<std::array<TestFilter, 5>>();
                    for (size_t i = 0; i < 4; ++i) (*f)[i] = TestFilter::make (75.0, true);
                    (*f)[4] = TestFilter::make (110.0, false);
                    return [=] (double) { double v = c->next(); for (auto& x : *f) v = x.process (v); return Frame { 0, 0, v, 0 }; }; });
    addPro ("P14_infra_only", "A 12 Hz tone in the LFE (-12 dBFS), nothing else",
            "Layered view: only the bottom layer (infra) moves; lens 1 gives it in dB above the threshold of feeling (not in phon).",
            [] { return [] (double t) { return Frame { 0, 0, 0, t < 0.0 ? 0.0 : 0.25 * std::sin (2.0 * pi * 12.0 * t) }; }; });

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
