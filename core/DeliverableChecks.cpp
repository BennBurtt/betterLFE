#include "DeliverableChecks.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace subguard
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    double toDb (double v)      { return 20.0 * std::log10 (std::max (v, 1e-12)); }

    std::string number (double v, int decimals)
    {
        char text[32];
        std::snprintf (text, sizeof (text), "%.*f", decimals, v);
        return text;
    }
}

DeliverableChecks::Section DeliverableChecks::butterworth (bool highPass, double hz, double q, double sr)
{
    // RBJ Audio EQ Cookbook, low-pass and high-pass
    const double w = 2.0 * pi * hz / sr, cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Section s;

    if (highPass)
    {
        s.b0 = (1.0 + cw) / 2.0 / a0;
        s.b1 = -(1.0 + cw) / a0;
        s.b2 = s.b0;
    }
    else
    {
        s.b0 = (1.0 - cw) / 2.0 / a0;
        s.b1 = (1.0 - cw) / a0;
        s.b2 = s.b0;
    }

    s.a1 = -2.0 * cw / a0;
    s.a2 = (1.0 - alpha) / a0;
    return s;
}

DeliverableChecks::DeliverableChecks (double sampleRate, std::vector<std::string> labels, double start, bool fr)
    : rate (sampleRate), startSeconds (start), french (fr)
{
    secondLength = std::max (1, (int) std::lround (sampleRate));

    // 4x interpolation: a 48-tap windowed sinc (Hann), split in four phases of 12, each summing to 1
    for (int p = 0; p < 4; ++p)
    {
        double sum = 0.0;

        for (int j = 0; j < 12; ++j)
        {
            const double k = 4.0 * j + p - 23.5;
            const double x = k / 4.0;
            const double sinc = std::abs (x) < 1e-12 ? 1.0 : std::sin (pi * x) / (pi * x);
            const double hann = 0.5 + 0.5 * std::cos (pi * k / 24.0);
            phases[(size_t) p][(size_t) j] = sinc * hann;
            sum += sinc * hann;
        }

        for (auto& c : phases[(size_t) p])
            c /= sum;
    }

    // Butterworth, 4th order: two sections, Q 0.5412 and 1.3066
    constexpr double q1 = 0.54119610014619701, q2 = 1.3065629648763766;

    for (auto& label : labels)
    {
        Channel c;
        c.label = label;
        c.isLfe = label == "LFE";
        c.infra[0] = butterworth (true, 5.0, std::sqrt (0.5), sampleRate);
        c.infra[1] = butterworth (false, 20.0, q1, sampleRate);
        c.infra[2] = butterworth (false, 20.0, q2, sampleRate);
        c.above[0] = butterworth (true, 120.0, q1, sampleRate);
        c.above[1] = butterworth (true, 120.0, q2, sampleRate);
        c.below[0] = butterworth (false, 120.0, q1, sampleRate);
        c.below[1] = butterworth (false, 120.0, q2, sampleRate);
        channels.push_back (c);
    }
}

void DeliverableChecks::endOfSecond (Channel& c)
{
    const double at = startSeconds + (double) frames / rate - 1.0;
    const double infra = std::sqrt (c.infraSum / secondLength);

    if (infra > c.infraWorst)
    {
        c.infraWorst = infra;
        c.infraAt = at;
    }

    if (c.isLfe)
    {
        const double below = std::sqrt (c.belowSum / secondLength), above = std::sqrt (c.aboveSum / secondLength);

        if (toDb (below) > limits.lfeAboveFloorDbfs || toDb (above) > limits.lfeAboveFloorDbfs)
        {
            const double ratio = toDb (above) - toDb (below);

            if (ratio > c.aboveWorstDb)
            {
                c.aboveWorstDb = ratio;
                c.aboveAt = at;
            }
        }
    }

    c.infraSum = c.aboveSum = c.belowSum = 0.0;
}

void DeliverableChecks::push (const float* const* audio, int numFrames)
{
    constexpr double fullScale = 0.99997;     // -0.0003 dBFS: a 16- or 24-bit sample at full scale

    for (int i = 0; i < numFrames; ++i)
    {
        for (size_t ch = 0; ch < channels.size(); ++ch)
        {
            auto& c = channels[ch];
            const double x = audio[ch] != nullptr ? (double) audio[ch][i] : 0.0;
            const double now = startSeconds + (double) frames / rate;
            c.sum += x;
            c.peak = std::max (c.peak, std::abs (x));

            // Clipping: a run of samples at full scale
            if (std::abs (x) >= fullScale)
            {
                if (++c.run == limits.clipRun)
                {
                    ++c.clips;

                    if (c.clipAt < 0.0)
                        c.clipAt = now;
                }
            }
            else
            {
                c.run = 0;
            }

            // True peak: the four interpolated values between this sample and the ones before
            c.history[c.historyPos] = x;

            for (int p = 0; p < 4; ++p)
            {
                double y = 0.0;

                for (size_t j = 0; j < 12; ++j)
                    y += phases[(size_t) p][j] * c.history[(c.historyPos + 12 - j) % 12];

                if (std::abs (y) > c.truePeak)
                {
                    c.truePeak = std::abs (y);
                    c.truePeakAt = now;
                }
            }

            c.historyPos = (c.historyPos + 1) % 12;

            // Under 20 Hz (from 5 Hz up, so that a DC offset is not taken for infra)
            const double infra = c.infra[2].run (c.infra[1].run (c.infra[0].run (x)));
            c.infraSum += infra * infra;

            if (c.isLfe)
            {
                const double above = c.above[1].run (c.above[0].run (x)), below = c.below[1].run (c.below[0].run (x));
                c.aboveSum += above * above;
                c.belowSum += below * below;
            }
        }

        ++frames;

        if (++inSecond >= secondLength)
        {
            inSecond = 0;

            for (auto& c : channels)
                endOfSecond (c);
        }
    }
}

double DeliverableChecks::samplePeakDb (size_t channel) const   { return channel < channels.size() ? toDb (channels[channel].peak) : -240.0; }
double DeliverableChecks::truePeakDb (size_t channel) const     { return channel < channels.size() ? toDb (std::max (channels[channel].truePeak, channels[channel].peak)) : -240.0; }

std::vector<DeliverableFinding> DeliverableChecks::finish (std::vector<LogEvent>* events)
{
    const auto T = [this] (const char* en, const char* fr) { return std::string (french ? fr : en); };
    std::vector<DeliverableFinding> out;

    const auto add = [&] (const std::string& name, const std::string& result, bool pass, double at, int channel, double value, const char* unit)
    {
        out.push_back ({ name, result, pass, at });

        if (! pass && events != nullptr)
        {
            LogEvent e;
            e.kind = EventKind::deliverable;
            e.start = e.end = std::max (at, startSeconds);
            e.channel = channel;
            e.peak = value;
            e.unit = unit;
            e.detail = name + ": " + result;
            events->push_back (e);
        }
    };

    const auto logChannel = [] (const std::string& label)
    {
        return label == "L" ? 0 : label == "C" ? 1 : label == "R" ? 2 : label == "LFE" ? 3 : -1;
    };

    if (channels.empty())
        return out;

    // True peak, every channel
    {
        size_t worst = 0;

        for (size_t i = 0; i < channels.size(); ++i)
            if (truePeakDb (i) > truePeakDb (worst))
                worst = i;

        const double tp = truePeakDb (worst);
        add (T ("True peak (EBU R 128: -1 dBTP at most)", "True peak (EBU R 128 : -1 dBTP au plus)"),
             number (tp, 1) + " dBTP " + T ("on ", "sur ") + channels[worst].label + T (" (sample peak ", " (crête échantillon ")
                 + number (samplePeakDb (worst), 1) + " dBFS)",
             tp <= limits.truePeakDbtp, channels[worst].truePeakAt, logChannel (channels[worst].label), tp, "dBTP");
    }

    // The LFE's own peak
    for (size_t i = 0; i < channels.size(); ++i)
        if (channels[i].isLfe)
        {
            const double tp = truePeakDb (i);
            add (T ("LFE peak", "Crête du LFE"), number (tp, 1) + " dBTP (" + number (samplePeakDb (i), 1) + " dBFS)",
                 tp <= limits.truePeakDbtp, channels[i].truePeakAt, 3, tp, "dBTP");
        }

    // Clipping
    {
        int total = 0;
        size_t worst = 0;

        for (size_t i = 0; i < channels.size(); ++i)
        {
            total += channels[i].clips;

            if (channels[i].clips > channels[worst].clips)
                worst = i;
        }

        add (T ("Clipping (3 samples or more at full scale)", "Écrêtage (3 échantillons ou plus à pleine échelle)"),
             total == 0 ? T ("none", "aucun") : std::to_string (total) + T (" runs, most on ", " passages, surtout sur ") + channels[worst].label,
             total == 0, total == 0 ? -1.0 : channels[worst].clipAt, logChannel (channels[worst].label), (double) total, "");
    }

    // DC offset
    {
        size_t worst = 0;
        const auto dc = [this] (size_t i) { return frames > 0 ? 100.0 * std::abs (channels[i].sum / (double) frames) : 0.0; };

        for (size_t i = 0; i < channels.size(); ++i)
            if (dc (i) > dc (worst))
                worst = i;

        add (T ("DC offset", "Composante continue"), number (dc (worst), 3) + T (" % of full scale on ", " % de la pleine échelle sur ") + channels[worst].label,
             dc (worst) < limits.dcPercent, -1.0, logChannel (channels[worst].label), dc (worst), "%");
    }

    // LFE above 120 Hz
    for (const auto& c : channels)
        if (c.isLfe)
        {
            const bool judged = c.aboveAt >= 0.0;
            add (T ("LFE above 120 Hz", "LFE au-dessus de 120 Hz"),
                 judged ? number (c.aboveWorstDb, 1) + T (" dB against the LFE below 120 Hz (worst second)", " dB face au LFE sous 120 Hz (pire seconde)")
                        : T ("LFE too quiet to judge", "LFE trop faible pour juger"),
                 ! judged || c.aboveWorstDb <= limits.lfeAboveDb, c.aboveAt, 3, c.aboveWorstDb, "dB");
        }

    // Infra-bass
    {
        size_t worst = 0;

        for (size_t i = 0; i < channels.size(); ++i)
            if (channels[i].infraWorst > channels[worst].infraWorst)
                worst = i;

        const double db = toDb (channels[worst].infraWorst);
        add (T ("Infra-bass under 20 Hz (PA subwoofers, headroom)", "Infra sous 20 Hz (subs de façade, réserve)"),
             db < -150.0 ? T ("none", "aucun") : number (db, 1) + T (" dBFS RMS over a second, on ", " dBFS RMS sur une seconde, sur ") + channels[worst].label,
             db <= limits.infraDbfs, channels[worst].infraAt, logChannel (channels[worst].label), db, "dBFS");
    }

    return out;
}

} // namespace subguard
