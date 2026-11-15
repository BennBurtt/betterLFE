#include "LfeEngine.h"

#include <algorithm>
#include <cmath>

namespace subguard
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    constexpr double targetRate = 500.0;     // decimated sample rate, approximately
    constexpr double aliasFreeHz = 150.0;    // band that must stay free of aliasing
    constexpr int fftSize = 4096;            // zero-padded FFT length (0.12 Hz grid: a sine reads within 0.1 dB)
    constexpr double bandLo = 2.5, bandHi = 190.0;    // bins kept for the analysis
    constexpr double gridLo = 4.5, gridHi = 160.0;    // log grid of the outputs
    constexpr int gridPoints = 160;
    constexpr double smoothOctaves = 1.0 / 3.0;
    constexpr double gateLoDb = -210.0, gateHiDb = -195.0;   // only digital silence is gated out
    constexpr double tiny = 1e-60;

    // Layout of one row of the statistics: power of L, C, R, F, LFE, then the
    // real and imaginary parts of X.conj(LFE) for X = L, C, R, F.
    constexpr int statCount = 15;
    constexpr int statPower = 0, statRe = 5, statIm = 9;

    // ...then, for the stereo downmix, the real parts of L.conj(C) and R.conj(C).
    constexpr int statLC = 13, statRC = 14;

    constexpr double globalLoHz = 20.0, globalHiHz = 120.0;

    // Below this level for this long, the LFE is considered absent and what was
    // measured against it is cleared.
    constexpr double lfeSilenceDb = -100.0;
    constexpr double lfeSilenceSeconds = 0.5;

    double dbToGain (double db)   { return std::pow (10.0, db / 20.0); }
}

//==============================================================================
std::vector<Biquad> designCheby2Lowpass (int order, double stopDb, double stopHz, double sampleRate)
{
    using cd = std::complex<double>;

    const double eps = 1.0 / std::sqrt (std::pow (10.0, stopDb / 10.0) - 1.0);
    const double mu = std::asinh (1.0 / eps) / order;
    const double k = 2.0 * sampleRate;                                  // bilinear constant
    const double warped = k * std::tan (pi * stopHz / sampleRate);      // pre-warped edge

    std::vector<Biquad> sections;

    for (int i = 1; i <= order / 2; ++i)
    {
        const double theta = pi * (2.0 * i - 1.0) / (2.0 * order);

        // Analog prototype (one of each conjugate pair), scaled to the stop edge
        const cd zero = cd (0.0, 1.0 / std::cos (theta)) * warped;
        const cd pole = warped / cd (-std::sinh (mu) * std::sin (theta),
                                      std::cosh (mu) * std::cos (theta));

        // Bilinear transform
        const cd zd = (k + zero) / (k - zero);
        const cd pd = (k + pole) / (k - pole);

        Biquad s;
        s.b0 = 1.0;
        s.b1 = -2.0 * zd.real();
        s.b2 = std::norm (zd);
        s.a1 = -2.0 * pd.real();
        s.a2 = std::norm (pd);

        // Unity gain at DC for every section
        const double g = (1.0 + s.a1 + s.a2) / (s.b0 + s.b1 + s.b2);
        s.b0 *= g;
        s.b1 *= g;
        s.b2 *= g;
        sections.push_back (s);
    }

    return sections;
}

//==============================================================================
Decimator::Decimator (double sampleRate)
    : fsIn (sampleRate)
{
    ratio = std::max (1, (int) std::lround (fsIn / targetRate));
    fsOut = fsIn / ratio;

    // Everything above (fsOut - aliasFreeHz) folds back into the useful band,
    // so the stop band (90 dB down) starts there.
    sections = designCheby2Lowpass (10, 90.0, fsOut - aliasFreeHz, fsIn);
    state.resize (sections.size());
    reset();
}

void Decimator::reset()
{
    for (auto& s : state)
        s.fill (0.0);

    phase = 0;
}

double Decimator::response (double hz) const
{
    const std::complex<double> z1 = std::polar (1.0, -2.0 * pi * hz / fsIn);
    const auto z2 = z1 * z1;
    double mag = 1.0;

    for (const auto& s : sections)
        mag *= std::abs ((s.b0 + s.b1 * z1 + s.b2 * z2) / (1.0 + s.a1 * z1 + s.a2 * z2));

    return mag;
}

bool Decimator::process (const float* frame, double* outFrame)
{
    double v[numChannels];

    for (int c = 0; c < numChannels; ++c)
        v[c] = std::isfinite (frame[c]) ? (double) frame[c] : 0.0;

    for (size_t i = 0; i < sections.size(); ++i)
    {
        const auto& s = sections[i];
        auto& st = state[i];

        for (int c = 0; c < numChannels; ++c)       // transposed direct form II
        {
            const double x = v[c];
            const double y = s.b0 * x + st[(size_t) (2 * c)];
            st[(size_t) (2 * c)] = s.b1 * x - s.a1 * y + st[(size_t) (2 * c + 1)];
            st[(size_t) (2 * c + 1)] = s.b2 * x - s.a2 * y;
            v[c] = y;
        }
    }

    const bool emit = (phase == 0);
    phase = (phase + 1) % ratio;

    if (emit)
        std::copy (v, v + numChannels, outFrame);

    return emit;
}

//==============================================================================
LfeEngine::LfeEngine (double sampleRate, double windowSeconds, double facadeTrimDb)
    : decimator (sampleRate),
      fs (decimator.outputRate()),
      facadeGain (dbToGain (facadeTrimDb)),
      transients (sampleRate, decimator.factor())
{
    transients.setGains ({ { facadeGain, facadeGain, facadeGain } }, 1.0);

    // FFT tables (radix 2)
    twiddleRe.resize (fftSize / 2);
    twiddleIm.resize (fftSize / 2);

    for (int i = 0; i < fftSize / 2; ++i)
    {
        twiddleRe[(size_t) i] = std::cos (2.0 * pi * i / fftSize);
        twiddleIm[(size_t) i] = -std::sin (2.0 * pi * i / fftSize);
    }

    bitReverse.resize (fftSize);
    int bits = 0;

    while ((1 << bits) < fftSize)
        ++bits;

    for (int i = 0; i < fftSize; ++i)
    {
        int r = 0;

        for (int b = 0; b < bits; ++b)
            if (i & (1 << b))
                r |= 1 << (bits - 1 - b);

        bitReverse[(size_t) i] = r;
    }

    workRe.resize (fftSize);
    workIm.resize (fftSize);
    configure (windowSeconds);
}

void LfeEngine::configure (double windowSeconds)
{
    nWin = std::clamp ((int) std::lround (windowSeconds * fs), 32, fftSize);
    windowSec = nWin / fs;

    window.resize ((size_t) nWin);
    double windowSum = 0.0;

    for (int i = 0; i < nWin; ++i)      // periodic Hann
    {
        window[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * pi * i / nWin);
        windowSum += window[(size_t) i];
    }

    bandBins.clear();
    out.freqs.clear();

    for (int i = 0; i <= fftSize / 2; ++i)
    {
        const double f = i * fs / fftSize;

        if (f >= bandLo && f <= bandHi)
        {
            bandBins.push_back (i);
            out.freqs.push_back (f);
        }
    }

    const auto nb = out.freqs.size();

    // A full-scale sine reads 0 dB; the anti-alias filter droop is undone.
    scale.resize (nb);

    for (size_t i = 0; i < nb; ++i)
        scale[i] = 1.0 / (0.5 * windowSum * decimator.response (out.freqs[i]));

    // Fractional-octave smoothing kernels: fine bins -> log grid. Raised cosine:
    // compact, so a strong tone cannot leak into bands further than one width.
    out.grid.resize (gridPoints);
    bandwidth.resize (gridPoints);
    kernels.assign (gridPoints, {});

    // The zero-padded spectrum is oversampled; products of two spectra carry
    // no more information than one bin every fs / (2 nWin).
    stride = std::max (1, fftSize / (2 * nWin));

    const double resolution = 2.0 * fs / nWin;      // Hann main lobe
    const double octaveWidth = std::pow (2.0, smoothOctaves / 2.0) - std::pow (2.0, -smoothOctaves / 2.0);

    for (int g = 0; g < gridPoints; ++g)
    {
        const double fc = gridLo * std::pow (gridHi / gridLo, g / (double) (gridPoints - 1));
        const double fwhm = std::max (fc * octaveWidth, resolution);

        out.grid[(size_t) g] = fc;
        bandwidth[(size_t) g] = fwhm / 0.75;        // equivalent noise bandwidth

        auto& kernel = kernels[(size_t) g];
        kernel.first = -1;
        double sum = 0.0;

        for (size_t i = 0; i < nb; i += (size_t) stride)
        {
            const double d = (out.freqs[i] - fc) / fwhm;

            if (std::abs (d) >= 1.0)
                continue;

            if (kernel.first < 0)
                kernel.first = (int) i;

            kernel.weights.push_back (0.5 + 0.5 * std::cos (pi * d));
            sum += kernel.weights.back();
        }

        for (auto& w : kernel.weights)
            w /= sum;
    }

    for (auto& s : spectrum)
        s.assign (nb, {});

    for (int p = 0; p < numPairs; ++p)
        for (auto* v : { &out.interaction[(size_t) p], &out.correlation[(size_t) p],
                         &out.coherence[(size_t) p], &out.phase[(size_t) p],
                         &out.gate[(size_t) p], &out.sigma[(size_t) p] })
            v->assign (gridPoints, 0.0);

    {
        auto& m = out.matrix;

        for (auto* v : { &m.pF, &m.pLfe, &m.pL, &m.pC, &m.pR, &m.reLC, &m.reRC })
            v->assign (gridPoints, 0.0);

        m.cFLfe.assign (gridPoints, {});
    }

    out.windowSeconds = windowSec;
    globalBinLo = globalBinHi = 0;

    for (size_t i = 0; i < nb; ++i)
    {
        if (out.freqs[i] < globalLoHz)
            globalBinLo = i + 1;

        if (out.freqs[i] <= globalHiHz)
            globalBinHi = i + 1;
    }

    out.facadeDb.assign (nb, -150.0);
    out.lfeDb.assign (nb, -150.0);

    for (auto& r : ring)
        r.assign ((size_t) nWin, 0.0);

    for (auto& w : windowed)
        w.assign ((size_t) nWin, 0.0);

    ringPos = 0;
    pending = 0;
    specPower.assign (nb * 2, 0.0);
    stats.assign (nb * statCount, 0.0);
    fastStats.assign (nb * statCount, 0.0);
    m1Fast.assign (gridPoints, 0.0);
    m2Fast.assign (gridPoints, 0.0);
    out.matrix.sigma.assign (gridPoints, 1.0);
    now.assign (nb * statCount, 0.0);
    m1.assign (gridPoints * numPairs, 0.0);
    m2.assign (gridPoints * numPairs, 0.0);
}

void LfeEngine::reset()
{
    decimator.reset();
    transients.reset();
    configure (windowSec);
}

void LfeEngine::push (const float* interleaved, int numFrames)
{
    double frame[numChannels];

    for (int i = 0; i < numFrames; ++i)
    {
        transients.processInput (interleaved + (size_t) i * numChannels);

        if (decimator.process (interleaved + (size_t) i * numChannels, frame))
        {
            for (int c = 0; c < numChannels; ++c)
                ring[(size_t) c][(size_t) ringPos] = frame[c];

            ringPos = (ringPos + 1) % nWin;
            ++pending;
            transients.processDecimated (frame);
        }
    }
}

void LfeEngine::fft()
{
    double* re = workRe.data();
    double* im = workIm.data();

    for (int i = 0; i < fftSize; ++i)
    {
        const int j = bitReverse[(size_t) i];

        if (j > i)
        {
            std::swap (re[i], re[j]);
            std::swap (im[i], im[j]);
        }
    }

    for (int len = 2; len <= fftSize; len <<= 1)
    {
        const int half = len / 2, step = fftSize / len;

        for (int start = 0; start < fftSize; start += len)
        {
            for (int k = 0; k < half; ++k)
            {
                const double wr = twiddleRe[(size_t) (k * step)], wi = twiddleIm[(size_t) (k * step)];
                const int a = start + k, b = a + half;
                const double tr = re[b] * wr - im[b] * wi;
                const double ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
}

bool LfeEngine::analyze (const Settings& settings)
{
    if (pending == 0)
        return false;

    const double dt = pending / fs;
    pending = 0;

    const auto nb = out.freqs.size();
    const double lfeGain = dbToGain (settings.lfeGainDb);

    transients.setGains ({ { settings.mute[0] ? 0.0 : facadeGain,
                             settings.mute[1] ? 0.0 : facadeGain,
                             settings.mute[2] ? 0.0 : facadeGain } }, lfeGain);

    // Windowed, zero-padded FFT of the last nWin decimated samples
    for (int c = 0; c < numChannels; ++c)
    {
        const auto& r = ring[(size_t) c];
        double mean = 0.0;

        for (double v : r)
            mean += v;

        mean /= nWin;

        for (int i = 0; i < nWin; ++i)
            workRe[(size_t) i] = (r[(size_t) ((ringPos + i) % nWin)] - mean) * window[(size_t) i];

        std::copy (workRe.begin(), workRe.begin() + nWin, windowed[(size_t) c].begin());

        std::fill (workRe.begin() + nWin, workRe.end(), 0.0);
        std::fill (workIm.begin(), workIm.end(), 0.0);
        fft();

        const bool muted = (c != chLFE) && settings.mute[(size_t) c];
        const double gain = muted ? 0.0 : (c == chLFE ? lfeGain : facadeGain);

        for (size_t i = 0; i < nb; ++i)
        {
            const auto bin = (size_t) bandBins[i];
            spectrum[(size_t) c][i] = { workRe[bin] * scale[i] * gain, workIm[bin] * scale[i] * gain };
        }
    }

    // LFE level over the window, and automatic clearing once it has gone
    {
        const auto& r = ring[chLFE];
        double sum = 0.0;

        for (double v : r)
            sum += v * v;

        lfeWindowDb = 10.0 * std::log10 (sum / nWin + 1e-30);
        lfeSilentSeconds = lfeWindowDb < lfeSilenceDb ? lfeSilentSeconds + dt : 0.0;

        if (lfeSilentSeconds >= lfeSilenceSeconds && ! correlationCleared)
        {
            resetCorrelation();
            autoResetPending = true;
        }

        if (lfeWindowDb >= lfeSilenceDb)
            correlationCleared = false;
    }

    // How much of the window actually holds signal for each pair (energy-weighted
    // duration). Right after an onset this is far shorter than the window, and
    // the estimate rests on correspondingly little data.
    double frameSeconds[numPairs];

    for (int p = 0; p < numPairs; ++p)
    {
        double sum = 0.0, squares = 0.0;

        for (int i = 0; i < nWin; ++i)
        {
            double x;

            if (p == pairFacade)
            {
                x = 0.0;

                for (int c = 0; c < 3; ++c)
                    if (! settings.mute[(size_t) c])
                        x += windowed[(size_t) c][(size_t) i];
            }
            else
            {
                x = windowed[(size_t) p][(size_t) i];
            }

            const double e = std::abs (x * windowed[chLFE][(size_t) i]);
            sum += e;
            squares += e * e;
        }

        frameSeconds[p] = sum * sum / (squares + tiny) / fs;
    }

    // Instantaneous statistics, then exponential averaging
    const double tauCorr = std::max (settings.tauCorrelation, 1e-3);
    const double aSpec = 1.0 - std::exp (-dt / std::max (settings.tauSpectrum, 1e-3));
    const double aCorr = 1.0 - std::exp (-dt / tauCorr);
    const double tauFast = std::max (settings.tauSimulation, 1e-3);
    const double aFast = 1.0 - std::exp (-dt / tauFast);

    for (size_t i = 0; i < nb; ++i)
    {
        const auto lfe = spectrum[chLFE][i];
        const std::complex<double> x[numPairs] = {
            spectrum[chL][i], spectrum[chC][i], spectrum[chR][i],
            spectrum[chL][i] + spectrum[chC][i] + spectrum[chR][i]
        };

        double* row = &now[i * statCount];

        for (int p = 0; p < numPairs; ++p)
        {
            row[statPower + p] = std::norm (x[p]);
            row[statRe + p] = x[p].real() * lfe.real() + x[p].imag() * lfe.imag();
            row[statIm + p] = x[p].imag() * lfe.real() - x[p].real() * lfe.imag();
        }

        row[statPower + 4] = std::norm (lfe);

        row[statLC] = (x[0] * std::conj (x[1])).real();
        row[statRC] = (x[2] * std::conj (x[1])).real();

        // Whole-session polarity: no forgetting, weighted by energy and time
        if (i >= globalBinLo && i < globalBinHi)
        {
            for (int p = 0; p < numPairs; ++p)
            {
                globalRe[(size_t) p] += dt * row[statRe + p];
                globalPx[(size_t) p] += dt * row[statPower + p];
                globalPl[(size_t) p] += dt * row[statPower + 4];
            }
        }

        double* avg = &stats[i * statCount];

        for (int j = 0; j < statCount; ++j)
        {
            avg[j] += aCorr * (row[j] - avg[j]);
            fastStats[i * statCount + (size_t) j] += aFast * (row[j] - fastStats[i * statCount + (size_t) j]);
        }

        specPower[i * 2] += aSpec * (row[statPower + pairFacade] - specPower[i * 2]);
        specPower[i * 2 + 1] += aSpec * (row[statPower + 4] - specPower[i * 2 + 1]);

        out.facadeDb[i] = 10.0 * std::log10 (specPower[i * 2] + 1e-24);
        out.lfeDb[i] = 10.0 * std::log10 (specPower[i * 2 + 1] + 1e-24);
    }

    // 1/3-octave smoothing and derived quantities
    const double m2Decay = std::exp (-2.0 * dt / tauCorr);
    const double m2FastDecay = std::exp (-2.0 * dt / tauFast);

    for (size_t g = 0; g < kernels.size(); ++g)
    {
        const auto& kernel = kernels[g];
        double sm[statCount] = {}, fast[statCount] = {}, smNow[5] = {};

        for (size_t k = 0; k < kernel.weights.size(); ++k)
        {
            const double w = kernel.weights[k];
            const size_t bin = (size_t) kernel.first + k * (size_t) stride;
            const double* avg = &stats[bin * statCount];
            const double* quick = &fastStats[bin * statCount];
            const double* row = &now[bin * statCount];

            for (int j = 0; j < statCount; ++j)
            {
                sm[j] += w * avg[j];
                fast[j] += w * quick[j];
            }

            for (int j = 0; j < 5; ++j)
                smNow[j] += w * row[j];
        }

        const double pl = sm[statPower + 4];

        // Group spectra for the playback-chain simulation, back at unity gain.
        // They use their own short average so that the robustness score
        // follows the mix closely, with the uncertainty that goes with it.
        {
            const double gf2 = facadeGain * facadeGain, gl = lfeGain;
            auto& m = out.matrix;
            m.pF[g] = fast[statPower + pairFacade] / gf2;
            m.pLfe[g] = fast[statPower + 4] / (gl * gl);
            m.pL[g] = fast[statPower] / gf2;
            m.pC[g] = fast[statPower + 1] / gf2;
            m.pR[g] = fast[statPower + 2] / gf2;
            m.reLC[g] = fast[statLC] / gf2;
            m.reRC[g] = fast[statRC] / gf2;
            m.cFLfe[g] = std::complex<double> (fast[statRe + pairFacade], fast[statIm + pairFacade]) / (facadeGain * gl);

            const double q = std::sqrt (smNow[pairFacade] * smNow[4]);
            double& a1 = m1Fast[g];
            double& a2 = m2Fast[g];
            a1 += aFast * (q - a1);
            a2 = a2 * m2FastDecay + q * q * dt / (tauFast * tauFast);
            const double dof = bandwidth[g] * (a1 * a1 / (a2 + tiny) + frameSeconds[pairFacade]);
            m.sigma[g] = std::min (1.0, 1.0 / std::sqrt (2.0 * std::max (dof, 0.5)));
        }

        for (int p = 0; p < numPairs; ++p)
        {
            const double px = sm[statPower + p], re = sm[statRe + p], im = sm[statIm + p];

            out.interaction[(size_t) p][g] = std::clamp (2.0 * re / (px + pl + tiny), -1.0, 1.0);
            out.correlation[(size_t) p][g] = std::clamp (re / (std::sqrt (px * pl) + tiny), -1.0, 1.0);
            out.coherence[(size_t) p][g] = std::clamp ((re * re + im * im) / (px * pl + tiny), 0.0, 1.0);
            out.phase[(size_t) p][g] = std::atan2 (im, re);

            const double lowDb = 10.0 * std::log10 (std::min (px, pl) + tiny);
            const double t = std::clamp ((lowDb - gateLoDb) / (gateHiDb - gateLoDb), 0.0, 1.0);
            out.gate[(size_t) p][g] = t * t * (3.0 - 2.0 * t);

            // How much independent data backs the estimate. The energy-weighted
            // averaging time (Kish effective duration) is short right after an
            // onset: a loud new sound outweighs everything averaged before it.
            const double q = std::sqrt (smNow[p] * smNow[4]);
            double& a1 = m1[g * numPairs + (size_t) p];
            double& a2 = m2[g * numPairs + (size_t) p];
            a1 += aCorr * (q - a1);
            a2 = a2 * m2Decay + q * q * dt / (tauCorr * tauCorr);

            const double effectiveSeconds = a1 * a1 / (a2 + tiny);
            const double dof = bandwidth[g] * (effectiveSeconds + frameSeconds[p]);
            out.sigma[(size_t) p][g] = std::min (1.0, 1.0 / std::sqrt (2.0 * std::max (dof, 0.5)));
        }
    }

    if (globalPx[pairFacade] > 0.0 && globalPl[pairFacade] > 0.0)
        globalTime += dt;

    return true;
}

double LfeEngine::globalCorrelation (int pair) const
{
    const auto p = (size_t) pair;
    return std::clamp (globalRe[p] / (std::sqrt (globalPx[p] * globalPl[p]) + tiny), -1.0, 1.0);
}

void LfeEngine::resetCorrelation()
{
    std::fill (stats.begin(), stats.end(), 0.0);
    std::fill (fastStats.begin(), fastStats.end(), 0.0);
    std::fill (m1.begin(), m1.end(), 0.0);
    std::fill (m2.begin(), m2.end(), 0.0);
    std::fill (m1Fast.begin(), m1Fast.end(), 0.0);
    std::fill (m2Fast.begin(), m2Fast.end(), 0.0);
    correlationCleared = true;
}

void LfeEngine::resetGlobal()
{
    globalRe.fill (0.0);
    globalPx.fill (0.0);
    globalPl.fill (0.0);
    globalTime = 0.0;
}

} // namespace subguard
