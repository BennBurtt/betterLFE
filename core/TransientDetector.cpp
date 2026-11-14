#include "TransientDetector.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace subguard
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    constexpr double highPassHz = 15.0;         // keeps DC and rumble out of the envelopes
    constexpr double fastSeconds = 0.020;       // envelope that follows the attack
    constexpr double slowSeconds = 0.250;       // envelope of the recent level
    constexpr double onsetRatio = 4.0;          // attack: fast envelope 6 dB above the recent level
    constexpr double rearmRatio = 2.0;
    constexpr double floorPower = 1e-7;         // about -70 dBFS: nothing below counts as an attack
    constexpr double refractorySeconds = 0.150;
    constexpr double preSeconds = 0.060;        // analysed segment: from 60 ms before the detection...
    constexpr double segmentSeconds = 0.300;    // ...over 300 ms
    constexpr double bandLoHz = 15.0, bandHiHz = 190.0;
    constexpr double minimumInteraction = 0.15; // weaker pairs are not reported at all
    constexpr int fftSize = 512;

    // In-place radix-2 FFT, forward when inverse is false.
    void fft (std::vector<std::complex<double>>& a, bool inverse)
    {
        const auto n = a.size();

        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;

            for (; j & bit; bit >>= 1)
                j ^= bit;

            j ^= bit;

            if (i < j)
                std::swap (a[i], a[j]);
        }

        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double angle = (inverse ? 2.0 : -2.0) * pi / (double) len;
            const std::complex<double> step (std::cos (angle), std::sin (angle));

            for (size_t start = 0; start < n; start += len)
            {
                std::complex<double> w (1.0, 0.0);

                for (size_t k = 0; k < len / 2; ++k)
                {
                    const auto u = a[start + k], v = a[start + k + len / 2] * w;
                    a[start + k] = u + v;
                    a[start + k + len / 2] = u - v;
                    w *= step;
                }
            }
        }

        if (inverse)
            for (auto& v : a)
                v /= (double) n;
    }
}

//==============================================================================
TransientDetector::TransientDetector (double sampleRate)
    : fs (sampleRate)
{
    preSamples = (int) std::lround (preSeconds * fs);
    segmentLength = std::min ((int) std::lround (segmentSeconds * fs), fftSize);
    maxLag = (int) std::lround (maxLagSeconds * fs);
    refractory = (int) std::lround (refractorySeconds * fs);
    fastCoeff = 1.0 - std::exp (-1.0 / (fs * fastSeconds));
    slowCoeff = 1.0 - std::exp (-1.0 / (fs * slowSeconds));

    // Second-order Butterworth high-pass
    const double w0 = 2.0 * pi * highPassHz / fs;
    const double alpha = std::sin (w0) / std::sqrt (2.0), c = std::cos (w0), a0 = 1.0 + alpha;
    highPass = { (1.0 + c) / 2.0 / a0, -(1.0 + c) / a0, (1.0 + c) / 2.0 / a0, -2.0 * c / a0, (1.0 - alpha) / a0 };

    // Tukey window: flat, with 20 % raised-cosine edges
    window.resize ((size_t) segmentLength);
    const int taper = segmentLength / 5;

    for (int i = 0; i < segmentLength; ++i)
    {
        const int edge = std::min (i, segmentLength - 1 - i);
        window[(size_t) i] = edge < taper ? 0.5 - 0.5 * std::cos (pi * (edge + 0.5) / taper) : 1.0;
    }

    reset();
}

void TransientDetector::reset()
{
    for (auto& r : ring)
        r.assign (historySize, 0.0);

    for (auto& s : filterState)
        s.fill (0.0);

    for (auto& c : channels)
        c = {};

    sampleIndex = 0;
    events.clear();
}

void TransientDetector::setGains (const std::array<double, 3>& screenGains, double lfeGain)
{
    screenGain = screenGains;
    lfeGainLinear = lfeGain;
}

std::vector<TransientEvent> TransientDetector::takeEvents()
{
    std::vector<TransientEvent> result;
    result.swap (events);
    return result;
}

double TransientDetector::history (int channel, long long index) const
{
    return ring[(size_t) channel][(size_t) (index & (historySize - 1))];
}

void TransientDetector::process (const double* frame)
{
    for (int c = 0; c < 4; ++c)
    {
        auto& st = filterState[(size_t) c];
        const double x = frame[c];
        const double y = highPass.b0 * x + st[0];
        st[0] = highPass.b1 * x - highPass.a1 * y + st[1];
        st[1] = highPass.b2 * x - highPass.a2 * y;
        ring[(size_t) c][(size_t) (sampleIndex & (historySize - 1))] = y;
    }

    for (int c = 0; c < 3; ++c)
    {
        auto& ch = channels[(size_t) c];
        const double v = history (c, sampleIndex) * screenGain[(size_t) c];
        const double power = v * v;

        // The recent level is read before this sample updates it
        const double reference = ch.slow + floorPower;
        ch.fast += fastCoeff * (power - ch.fast);
        ch.slow += slowCoeff * (power - ch.slow);
        const double ratio = ch.fast / reference;

        if (ratio < rearmRatio)
            ch.armed = true;

        if (ch.armed && ratio > onsetRatio && sampleIndex - ch.lastOnset > refractory)
        {
            ch.armed = false;
            ch.lastOnset = sampleIndex;
            ch.pending.push_back (sampleIndex);
        }

        // Analyse once the whole segment, and the LFE around it, has arrived
        while (! ch.pending.empty()
               && sampleIndex >= ch.pending.front() - preSamples + segmentLength + maxLag)
        {
            analyse (c, ch.pending.front());
            ch.pending.erase (ch.pending.begin());
        }
    }

    ++sampleIndex;
}

void TransientDetector::analyse (int channel, long long onsetIndex)
{
    const long long start = onsetIndex - preSamples;

    if (start - maxLag < 0 || start - maxLag < sampleIndex - historySize + 1)
        return;

    const double gx = screenGain[(size_t) channel];

    // Analytic (one-sided, band-limited) version of the windowed screen segment
    std::vector<std::complex<double>> xa (fftSize);

    for (int i = 0; i < segmentLength; ++i)
        xa[(size_t) i] = gx * window[(size_t) i] * history (channel, start + i);

    fft (xa, false);

    std::vector<double> magnitudeX ((size_t) fftSize / 2);

    for (int k = 0; k < fftSize / 2; ++k)
        magnitudeX[(size_t) k] = std::abs (xa[(size_t) k]);

    for (int k = 0; k < fftSize; ++k)
    {
        const double hz = k * fs / fftSize;
        const bool keep = k < fftSize / 2 && hz >= bandLoHz && hz <= bandHiHz;
        xa[(size_t) k] = keep ? xa[(size_t) k] * 2.0 : std::complex<double>();
    }

    fft (xa, true);

    double energyX = 0.0;

    for (int i = 0; i < segmentLength; ++i)
        energyX += 0.5 * std::norm (xa[(size_t) i]);

    if (energyX <= 0.0)
        return;

    // Complex cross-correlation against the LFE for every lag. Its real part
    // is the ordinary correlation; its magnitude is the envelope, whose peak
    // locates the LFE transient in time whatever its phase.
    const int lags = 2 * maxLag + 1;
    std::vector<std::complex<double>> cross ((size_t) lags);
    std::vector<double> energyLfe ((size_t) lags), envelope ((size_t) lags);
    int best = 0;

    for (int li = 0; li < lags; ++li)
    {
        const int lag = li - maxLag;
        std::complex<double> sum;
        double energy = 0.0;

        for (int i = 0; i < segmentLength; ++i)
        {
            const double l = lfeGainLinear * window[(size_t) i] * history (3, start + i + lag);
            sum += std::conj (xa[(size_t) i]) * l;
            energy += l * l;
        }

        cross[(size_t) li] = sum;
        energyLfe[(size_t) li] = energy;
        envelope[(size_t) li] = std::abs (sum) / std::sqrt (energyX * energy + 1e-30);

        if (envelope[(size_t) li] > envelope[(size_t) best])
            best = li;
    }

    const auto interaction = [&] (int li, double correlation)
    {
        const double el = energyLfe[(size_t) li];
        return correlation * 2.0 * std::sqrt (energyX * el) / (energyX + el + 1e-30);
    };

    TransientEvent e;
    e.channel = channel;
    e.match = std::min (1.0, envelope[(size_t) best]);
    e.phase = std::arg (cross[(size_t) best]);
    e.interactionBest = interaction (best, e.match);
    e.interactionNow = std::clamp (interaction (maxLag, cross[(size_t) maxLag].real()
                                                          / std::sqrt (energyX * energyLfe[(size_t) maxLag] + 1e-30)),
                                   -1.0, 1.0);
    e.levelDb = 10.0 * std::log10 (energyX / segmentLength + 1e-15);
    e.delayAtLimit = (best == 0 || best == lags - 1);

    // Sub-sample position of the envelope peak (parabolic interpolation)
    double position = best - maxLag;

    if (! e.delayAtLimit)
    {
        const double a = envelope[(size_t) best - 1], b = envelope[(size_t) best], c = envelope[(size_t) best + 1];
        const double curvature = a - 2.0 * b + c;

        if (curvature < 0.0)
            position += 0.5 * (a - c) / curvature;
    }

    e.delayMs = 1000.0 * position / fs;

    // Precision of the offset. The envelope peak is as sharp as the band the
    // two signals share is wide: sigma = sqrt ((1 - m^2) / (m^2 N)) / (2 pi beta),
    // with beta the RMS bandwidth of the cross-spectrum, m the match and
    // N = 2 B T the number of independent samples in the segment.
    std::vector<std::complex<double>> lfe (fftSize);

    for (int i = 0; i < segmentLength; ++i)
        lfe[(size_t) i] = window[(size_t) i] * history (3, start + i + best - maxLag);

    fft (lfe, false);
    double weightSum = 0.0, centroid = 0.0, spread = 0.0, squares = 0.0;

    for (int pass = 0; pass < 2; ++pass)
    {
        for (int k = 1; k < fftSize / 2; ++k)
        {
            const double hz = k * fs / fftSize;

            if (hz < bandLoHz || hz > bandHiHz)
                continue;

            const double w = magnitudeX[(size_t) k] * std::abs (lfe[(size_t) k]);

            if (pass == 0)  { weightSum += w; centroid += w * hz; squares += w * w; }
            else            { spread += w * (hz - centroid) * (hz - centroid); }
        }

        if (pass == 0)
            centroid /= (weightSum + 1e-30);
    }

    const double rmsBandwidth = std::sqrt (spread / (weightSum + 1e-30));
    const double occupiedBandwidth = weightSum * weightSum / (squares + 1e-30) * fs / fftSize;
    const double independent = std::max (1.0, 2.0 * occupiedBandwidth * segmentLength / fs);
    const double m2 = std::clamp (e.match * e.match, 1e-6, 0.999);
    e.delayUncertaintyMs = 1000.0 * std::sqrt ((1.0 - m2) / (m2 * independent))
                             / (2.0 * pi * std::max (rmsBandwidth, 0.5));

    if (e.interactionBest >= minimumInteraction)
        events.push_back (e);
}

} // namespace subguard
