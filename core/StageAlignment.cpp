#include "StageAlignment.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace subguard
{

namespace
{
    using cd = std::complex<double>;
    constexpr double pi = 3.14159265358979323846;

    void fft (std::vector<cd>& a, bool inverse)
    {
        const size_t n = a.size();

        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;

            for (; (j & bit) != 0; bit >>= 1)
                j ^= bit;

            j ^= bit;

            if (i < j)
                std::swap (a[i], a[j]);
        }

        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double angle = 2.0 * pi / (double) len * (inverse ? 1.0 : -1.0);
            const cd w1 (std::cos (angle), std::sin (angle));

            for (size_t i = 0; i < n; i += len)
            {
                cd w (1.0, 0.0);

                for (size_t k = 0; k < len / 2; ++k)
                {
                    const cd u = a[i + k], v = a[i + k + len / 2] * w;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                    w *= w1;
                }
            }
        }

        if (inverse)
            for (auto& x : a)
                x /= (double) n;
    }

    cd transferAt (const std::vector<double>& h, size_t from, size_t length, double hz, double rate)
    {
        // DFT of the windowed stretch at one frequency; the phase is taken from the window's start for both responses
        cd sum;
        const double w = -2.0 * pi * hz / rate;

        for (size_t n = 0; n < length && from + n < h.size(); ++n)
        {
            const double fade = n < length * 3 / 4 ? 1.0 : 0.5 + 0.5 * std::cos (pi * (double) (n - length * 3 / 4) / (double) (length / 4));
            sum += h[from + n] * fade * std::polar (1.0, w * (double) n);
        }

        return sum;
    }

    double db (double power)    { return 10.0 * std::log10 (std::max (power, 1e-30)); }
}

std::vector<float> StageAlignment::makeSweep (const Sweep& s)
{
    // Exponential sine sweep: x(t) = sin (2 pi f1 T / ln (f2 / f1) (exp (t ln (f2 / f1) / T) - 1))
    const auto n = (size_t) (s.seconds * s.sampleRate);
    const double rate = std::log (s.toHz / s.fromHz), amp = std::pow (10.0, s.levelDbfs / 20.0);
    std::vector<float> out (n);

    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / s.sampleRate;
        const double phase = 2.0 * pi * s.fromHz * s.seconds / rate * (std::exp (t * rate / s.seconds) - 1.0);
        const double fadeIn = std::min (1.0, t / 0.05), fadeOut = std::min (1.0, (s.seconds - t) / 0.05);
        out[i] = (float) (amp * fadeIn * fadeOut * std::sin (phase));
    }

    return out;
}

std::vector<double> StageAlignment::impulseResponse (const std::vector<float>& sweep, const std::vector<float>& recorded)
{
    size_t n = 1;

    while (n < recorded.size() + sweep.size())
        n <<= 1;

    std::vector<cd> x (n), y (n);

    for (size_t i = 0; i < sweep.size(); ++i)
        x[i] = sweep[i];

    for (size_t i = 0; i < recorded.size(); ++i)
        y[i] = recorded[i];

    fft (x, false);
    fft (y, false);

    // Y / X, regularised where the sweep has no energy (outside its band)
    double peak = 0.0;

    for (const auto& v : x)
        peak = std::max (peak, std::norm (v));

    const double floor = 1e-4 * peak;

    for (size_t i = 0; i < n; ++i)
        y[i] = y[i] * std::conj (x[i]) / (std::norm (x[i]) + floor);

    fft (y, true);
    std::vector<double> h (recorded.size());

    for (size_t i = 0; i < h.size(); ++i)
        h[i] = y[i].real();

    return h;
}

StageAlignment::Result StageAlignment::analyse (const std::vector<double>& screen, const std::vector<double>& sub, double rate)
{
    Result r;

    if (screen.empty() || sub.empty() || rate <= 0.0)
    {
        r.problem = "no recording";
        return r;
    }

    // Arrivals: the first rise to half of each response's peak (lower, the band limits' pre-ringing would be taken for it)
    const auto arrival = [] (const std::vector<double>& h)
    {
        double peak = 0.0;

        for (double v : h)
            peak = std::max (peak, std::abs (v));

        for (size_t i = 0; i < h.size(); ++i)
            if (std::abs (h[i]) >= 0.5 * peak)
                return std::make_pair (i, peak);

        return std::make_pair ((size_t) 0, peak);
    };

    const auto [screenAt, screenPeak] = arrival (screen);
    const auto [subAt, subPeak] = arrival (sub);

    if (screenPeak <= 0.0 || subPeak <= 0.0)
    {
        r.problem = "nothing recorded";
        return r;
    }

    r.screenArrivalMs = 1000.0 * (double) screenAt / rate;
    r.subArrivalMs = 1000.0 * (double) subAt / rate;

    // The same stretch of time for both: from 20 ms before the first arrival, 500 ms
    const size_t from = (size_t) std::max (0.0, (double) std::min (screenAt, subAt) - 0.020 * rate);
    const auto length = (size_t) (0.5 * rate);

    for (double f = 20.0; f <= 200.0; f *= std::pow (2.0, 1.0 / 24.0))
        r.hz.push_back (f);

    std::vector<cd> hs (r.hz.size()), hb (r.hz.size());

    for (size_t k = 0; k < r.hz.size(); ++k)
    {
        hs[k] = transferAt (screen, from, length, r.hz[k], rate);
        hb[k] = transferAt (sub, from, length, r.hz[k], rate);
        r.screenDb.push_back (db (std::norm (hs[k])));
        r.subDb.push_back (db (std::norm (hb[k])));
        r.sumDb.push_back (db (std::norm (hs[k] + hb[k])));
    }

    // Crossover: where the two levels, smoothed over a third of an octave, are closest (40 to 160 Hz)
    {
        double best = 1e9;

        for (size_t k = 0; k < r.hz.size(); ++k)
        {
            if (r.hz[k] < 40.0 || r.hz[k] > 160.0)
                continue;

            double a = 0.0, b = 0.0;
            int m = 0;

            for (size_t j = 0; j < r.hz.size(); ++j)
                if (std::abs (std::log2 (r.hz[j] / r.hz[k])) <= 1.0 / 6.0)
                {
                    a += std::norm (hs[j]);
                    b += std::norm (hb[j]);
                    ++m;
                }

            const double gap = std::abs (db (a / m) - db (b / m));

            if (gap < best)
            {
                best = gap;
                r.crossoverHz = r.hz[k];
            }
        }
    }

    // The sum over an octave each side of it, against a perfect in-phase sum
    const auto inBand = [&r] (size_t k) { return std::abs (std::log2 (r.hz[k] / r.crossoverHz)) <= 1.0; };
    const auto sumFor = [&] (double delayMs, double sign)
    {
        double sum = 0.0, ideal = 0.0;

        for (size_t k = 0; k < r.hz.size(); ++k)
            if (inBand (k))
            {
                const cd moved = sign * hb[k] * std::polar (1.0, -2.0 * pi * r.hz[k] * delayMs * 0.001);
                sum += std::norm (hs[k] + moved);
                ideal += std::pow (std::abs (hs[k]) + std::abs (hb[k]), 2.0);
            }

        return db (sum / (ideal + 1e-60));
    };

    r.sumNowDb = sumFor (0.0, 1.0);
    r.nowInverted = sumFor (0.0, -1.0) > r.sumNowDb;
    r.sumBestDb = -1e9;

    for (double sign : { 1.0, -1.0 })
        for (double d = -15.0; d <= 15.0 + 1e-9; d += 0.05)
        {
            const double v = sumFor (d, sign);

            // Ties go to the smaller change (no inversion, then the shortest delay)
            if (v > r.sumBestDb + 1e-6 || (std::abs (v - r.sumBestDb) <= 1e-6 && std::abs (d) < std::abs (r.bestDelayMs)))
            {
                r.sumBestDb = v;
                r.bestDelayMs = d;
                r.bestInverted = sign < 0.0;
            }
        }

    for (size_t k = 0; k < r.hz.size(); ++k)
    {
        const cd moved = (r.bestInverted ? -1.0 : 1.0) * hb[k] * std::polar (1.0, -2.0 * pi * r.hz[k] * r.bestDelayMs * 0.001);
        r.bestDb.push_back (db (std::norm (hs[k] + moved)));
    }

    r.valid = true;
    return r;
}

} // namespace subguard
