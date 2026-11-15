// Writes a set of 5.1 test files for SubGUARD and measures each of them with
// the analysis engine, so that the read-out to expect is known.
//
//   subguard_testfiles <output folder>
//
// Files are 48 kHz, 24-bit WAV, channel order L R C LFE Ls Rs (the "5.1 ITU"
// format of the app). The surrounds are silent.

#include "../core/AlertTracker.h"
#include "../core/LfeEngine.h"
#include "../core/PlaybackSim.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
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
        std::array<std::vector<double>, 3> delays;
        std::array<int, 3> inverted {}, counted {};
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
                if (AlertTracker::severity (AlertTracker::transientMeasure (e, options), options.threshold) > 0.0)
                {
                    const auto c = (size_t) e.channel;
                    delays[c].push_back (e.delayMs);
                    inverted[c] += std::abs (e.phase) > 2.0 * pi / 3.0;
                    ++counted[c];
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

        std::string text;
        const char* names[] = { "L", "C", "R" };

        for (size_t c = 0; c < 3; ++c)
        {
            if (delays[c].empty())
                continue;

            std::sort (delays[c].begin(), delays[c].end());
            char buffer[96];
            std::snprintf (buffer, sizeof (buffer), "%s%s: %d hits, LFE %+.1f ms%s", text.empty() ? "" : "; ", names[c], counted[c],
                           delays[c][delays[c].size() / 2], inverted[c] * 2 > counted[c] ? ", inverted" : "");
            text += buffer;
        }

        if (! text.empty())
            r.transients = text;

        return r;
    }
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
          "Transients strip, C: 'Aligned, in phase'.", 0.0, 1.0, 1.0, false);
    hits ("12_transients_lfe_12ms_late", "Kicks in the centre channel, the LFE hits 12 ms late",
          "Transients strip, C: 'LFE late 12.0 ms'.", 0.012, 1.0, 1.0, false);
    hits ("13_transients_lfe_6ms_early", "Kicks in the centre channel, the LFE hits 6 ms early",
          "Transients strip, C: 'LFE early 6.0 ms'.", -0.006, 1.0, 1.0, false);
    hits ("14_transients_inverted", "Kicks in the centre channel, the LFE hits at the same instant with flipped polarity",
          "Transients strip, C: 'Aligned, polarity inverted'.", 0.0, -1.0, 1.0, false);
    hits ("15_transients_very_quiet", "Same as file 12, 60 dB lower (peaks around -68 dBFS)",
          "Transients strip, C: 'LFE late 12.0 ms', exactly as at full level. Set the level range to -120 dB to see the curves.",
          0.012, 1.0, 0.001, false);
    hits ("16_transients_low_boom_under_bed", "28 Hz booms in the centre under a louder 60-180 Hz bed; the LFE boom is 8 ms late",
          "Transients strip, C: 'LFE late' around 8-10 ms with a wide +/-, or 'timing not measurable': a single low frequency is hard to time.",
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
                          "Default settings assumed: level Strong, both polarities, LFE +10 dB box unchecked.\n"
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
    return 0;
}
