#include "TransientDetector.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace subguard
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    constexpr double inputHighPassHz = 8.0;     // keeps DC and infrasonic drift out
    constexpr double floorPower = 1e-11;        // -110 dBFS: only digital silence is ignored
    constexpr double minimumRiseDb = 6.0;       // an attack rises at least this far above the recent level...
    constexpr double roughnessFactor = 5.5;     // ...and this many times the band's usual fluctuation
    constexpr double slowSeconds = 0.30;        // "recent level"
    constexpr double roughnessSeconds = 1.0;
    constexpr double roughnessLagSeconds = 0.040;
    constexpr double freezeSeconds = 0.25;      // an attack does not count as usual fluctuation
    constexpr double refractorySeconds = 0.080;
    constexpr double preSeconds = 0.070;        // analysed segment: from 70 ms before the attack...
    constexpr double segmentSeconds = 0.300;    // ...over 300 ms
    constexpr double bandLoHz = 8.0, bandHiHz = 190.0;
    constexpr int fftSize = 512;
    constexpr int whitenOrder = 8;

    // An attack of the mix is in the LFE when the LFE has one of its own
    // between 30 ms before and 120 ms after it (a low attack builds slowly)...
    constexpr double lfeNearBeforeSeconds = 0.030, lfeNearAfterSeconds = 0.120;

    // ...or when the LFE rises by 6 dB there while matching the mix
    constexpr double lfeRiseRatio = 1.25, lfeRiseMatch = 0.7;

    // Peak detector (mix): the peak of the last 12 ms must double against
    // the highest peak of the 36 ms before (AudioLib asks 1.5, AC-3 2.5 at
    // its coarsest layer), and stand out of the signal's usual variation.
    // Two views: above 600 Hz, where 12 ms holds enough cycles for a peak to
    // mean something, and above 8 kHz, where a hi-hat or a click stands alone.
    constexpr double peakRiseDb = 6.0;
    constexpr double peakRoughnessFactor = 5.0;
    constexpr double peakFloor = 5e-4;          // -66 dBFS: below this the peak detector stays silent
    constexpr double midViewHz = 600.0, highViewHz = 8000.0;
    constexpr double mixRefractorySeconds = 0.080;

    // Full-band analysis of the mix, and LFE analysis in its own range
    const std::vector<std::array<double, 2>> mixBands = { { { 10.0, 40.0 } }, { { 40.0, 150.0 } }, { { 150.0, 600.0 } } };
    const std::vector<std::array<double, 2>> lfeBands = { { { 10.0, 190.0 } }, { { 10.0, 30.0 } }, { { 30.0, 50.0 } },
                                                          { { 50.0, 80.0 } }, { { 80.0, 190.0 } } };

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
OnsetBank::Biquad OnsetBank::highPass (double hz, double sampleRate)
{
    const double w0 = 2.0 * pi * hz / sampleRate;
    const double alpha = std::sin (w0) / std::sqrt (2.0), c = std::cos (w0), a0 = 1.0 + alpha;
    return { (1.0 + c) / 2.0 / a0, -(1.0 + c) / a0, (1.0 + c) / 2.0 / a0, -2.0 * c / a0, (1.0 - alpha) / a0 };
}

OnsetBank::Biquad OnsetBank::lowPass (double hz, double sampleRate)
{
    const double w0 = 2.0 * pi * hz / sampleRate;
    const double alpha = std::sin (w0) / std::sqrt (2.0), c = std::cos (w0), a0 = 1.0 + alpha;
    return { (1.0 - c) / 2.0 / a0, (1.0 - c) / a0, (1.0 - c) / 2.0 / a0, -2.0 * c / a0, (1.0 - alpha) / a0 };
}

void OnsetBank::prepare (double sampleRate, int hop, const std::vector<std::array<double, 2>>& bandEdgesHz)
{
    hopSize = std::max (1, hop);
    const double decisionRate = sampleRate / hopSize;

    slowCoeff = 1.0 - std::exp (-1.0 / (decisionRate * slowSeconds));
    roughnessCoeff = 1.0 - std::exp (-1.0 / (decisionRate * roughnessSeconds));
    roughnessLag = std::clamp ((int) std::lround (roughnessLagSeconds * decisionRate), 1, 63);
    freezeHops = (int) std::lround (freezeSeconds * decisionRate);
    refractoryHops = (int) std::lround (refractorySeconds * decisionRate);
    confirmHops = std::max (2, (int) std::lround (0.003 * decisionRate));

    bands.clear();

    for (const auto& edges : bandEdgesHz)
    {
        const double lo = edges[0], hi = std::min (edges[1], 0.45 * sampleRate);

        if (lo >= hi)
            continue;

        Band band;
        band.hp = highPass (lo, sampleRate);
        band.lp = lowPass (hi, sampleRate);

        // Envelope smoothing of about one period of the band centre
        const double tau = std::clamp (1.0 / std::sqrt (lo * hi), 0.0025, 0.060);
        band.envelopeCoeff = 1.0 - std::exp (-1.0 / (sampleRate * tau));
        bands.push_back (band);
    }

    reset();
}

void OnsetBank::reset()
{
    for (auto& band : bands)
    {
        std::fill (std::begin (band.state), std::end (band.state), 0.0);
        band.envelope = band.level = band.roughness = 0.0;
        band.primed = false;
    }

    hopCounter = 0;
    evaluation = 0;
    lastOnset = -1000000;
    run = 0;
    armed = true;
}

bool OnsetBank::process (double sample, int& samplesAgo)
{
    for (auto& band : bands)
    {
        const auto step = [] (const Biquad& f, double* st, double x)
        {
            const double y = f.b0 * x + st[0];
            st[0] = f.b1 * x - f.a1 * y + st[1];
            st[1] = f.b2 * x - f.a2 * y;
            return y;
        };

        const double y = step (band.lp, band.state + 2, step (band.hp, band.state, sample));
        band.envelope += band.envelopeCoeff * (y * y - band.envelope);
    }

    if (++hopCounter < hopSize)
        return false;

    hopCounter = 0;
    const auto slot = (size_t) (evaluation % 64);
    const auto lagged = (size_t) ((evaluation + 64 - roughnessLag) % 64);
    const bool frozen = evaluation - lastOnset <= freezeHops;
    double score = 0.0;

    for (auto& band : bands)
    {
        const double logEnergy = 10.0 * std::log10 (band.envelope + floorPower);

        if (! band.primed)
        {
            band.level = logEnergy;
            band.recent.fill (logEnergy);
            band.primed = true;
        }

        // Rise above the recent level, measured against what this band
        // usually does: a noisy band needs a bigger jump than a steady one.
        const double threshold = std::max (minimumRiseDb, roughnessFactor * band.roughness);
        score = std::max (score, (logEnergy - band.level) / threshold);

        if (! frozen)
            band.roughness += roughnessCoeff * (std::min (std::abs (logEnergy - band.recent[lagged]), threshold) - band.roughness);

        band.level += slowCoeff * (logEnergy - band.level);
        band.recent[slot] = logEnergy;
    }

    if (score < 0.5)
        armed = true;

    run = score > 1.0 ? run + 1 : 0;
    bool found = false;

    if (armed && run >= confirmHops && evaluation - lastOnset > refractoryHops)
    {
        armed = false;
        lastOnset = evaluation;
        samplesAgo = (confirmHops - 1) * hopSize;
        found = true;
    }

    ++evaluation;
    return found;
}

//==============================================================================
bool PeakOnset::prepare (double sampleRate, double highPassHz)
{
    blockSize = std::max (1, (int) std::lround (sampleRate / 1000.0));
    const double blockRate = sampleRate / blockSize;
    roughnessCoeff = 1.0 - std::exp (-1.0 / (blockRate * roughnessSeconds));
    freezeBlocks = (int) std::lround (freezeSeconds * blockRate);
    refractoryBlocks = (int) std::lround (refractorySeconds * blockRate);
    filtered = highPassHz > 0.0;
    enabled = ! filtered || highPassHz < 0.4 * sampleRate;

    if (filtered && enabled)
    {
        const double w0 = 2.0 * pi * highPassHz / sampleRate;
        const double alpha = std::sin (w0) / std::sqrt (2.0), c = std::cos (w0), a0 = 1.0 + alpha;
        filter = { (1.0 + c) / 2.0 / a0, -(1.0 + c) / a0, (1.0 + c) / 2.0 / a0, -2.0 * c / a0, (1.0 - alpha) / a0 };
    }

    reset();
    return enabled;
}

void PeakOnset::reset()
{
    std::fill (std::begin (state), std::end (state), 0.0);
    peaks.fill (0.0);
    blockPeak = roughness = 0.0;
    blockCounter = 0;
    block = 0;
    lastOnset = -1000000;
    armed = true;
}

bool PeakOnset::process (double x, int& samplesAgo)
{
    if (! enabled)
        return false;

    if (filtered)
        for (int stage = 0; stage < 2; ++stage)     // twice: 24 dB per octave, the bass must not leak in
        {
            double* st = state + 2 * stage;
            const double y = filter.b0 * x + st[0];
            st[0] = filter.b1 * x - filter.a1 * y + st[1];
            st[1] = filter.b2 * x - filter.a2 * y;
            x = y;
        }

    blockPeak = std::max (blockPeak, std::abs (x));

    if (++blockCounter < blockSize)
        return false;

    blockCounter = 0;
    peaks[(size_t) (block & 63)] = blockPeak;
    blockPeak = 0.0;

    double current = 0.0, previous = 0.0;

    for (int i = 0; i < span; ++i)
        current = std::max (current, peaks[(size_t) ((block - i) & 63)]);

    for (int i = span; i < span + lookBack; ++i)
        previous = std::max (previous, peaks[(size_t) ((block - i) & 63)]);

    const double rise = 20.0 * std::log10 ((current + 0.1 * peakFloor) / (previous + 0.1 * peakFloor));
    const double threshold = std::max (peakRiseDb, peakRoughnessFactor * roughness);

    // Usual variation: the upward half only (a sound dying away is not
    // roughness), counted twice to stand for both halves
    if (block - lastOnset > freezeBlocks)
        roughness += roughnessCoeff * (std::min (2.0 * std::max (rise, 0.0), threshold) - roughness);

    if (rise < 0.5 * threshold)
        armed = true;

    bool found = false;

    if (armed && rise > threshold && current > peakFloor && block - lastOnset > refractoryBlocks)
    {
        // The windows slide by 1 ms, so the block that has just come in is
        // the one that made the peak jump: the attack starts in it.
        armed = false;
        lastOnset = block;
        samplesAgo = blockSize;
        found = true;
    }

    ++block;
    return found;
}

//==============================================================================
TransientDetector::TransientDetector (double inputSampleRate, int decimationFactor)
    : fs (inputSampleRate / std::max (1, decimationFactor)),
      factor (std::max (1, decimationFactor))
{
    preSamples = (int) std::lround (preSeconds * fs);
    segmentLength = std::min ((int) std::lround (segmentSeconds * fs), fftSize);
    maxLag = (int) std::lround (maxLagSeconds * fs);

    // Decisions every millisecond on the mix, every decimated sample on the LFE
    mixOnsets.prepare (inputSampleRate, std::max (1, (int) std::lround (inputSampleRate / 1000.0)), mixBands);
    lfeOnsets.prepare (fs, 1, lfeBands);
    mixPeaks.prepare (inputSampleRate, midViewHz);
    mixHighPeaks.prepare (inputSampleRate, highViewHz);
    mixRefractory = (long long) std::lround (mixRefractorySeconds * inputSampleRate);

    // Second-order Butterworth high-pass for the stored signals
    const double w0 = 2.0 * pi * inputHighPassHz / fs;
    const double alpha = std::sin (w0) / std::sqrt (2.0), c = std::cos (w0), a0 = 1.0 + alpha;
    inputHighPass = { (1.0 + c) / 2.0 / a0, -(1.0 + c) / a0, (1.0 + c) / 2.0 / a0, -2.0 * c / a0, (1.0 - alpha) / a0 };

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

    for (auto& st : filterState)
        st.fill (0.0);

    mixOnsets.reset();
    lfeOnsets.reset();
    mixPeaks.reset();
    mixHighPeaks.reset();
    lastMixAttack = -1000000;
    pendingMix.clear();
    recentLfe.clear();
    inputIndex = 0;
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

double TransientDetector::history (int which, long long index) const
{
    return ring[(size_t) which][(size_t) (index & (historySize - 1))];
}

void TransientDetector::mixAttack (long long inputSample)
{
    // The detectors see the same attack one after the other: the first one wins
    if (inputSample - lastMixAttack <= mixRefractory)
        return;

    lastMixAttack = inputSample;
    pendingMix.push_back (inputSample / factor);
}

void TransientDetector::processInput (const float* frame)
{
    // Stereo downmix summed to mono: Lo + Ro = L + R + 1.414 C
    double mix = screenGain[0] * frame[0] + std::sqrt (2.0) * screenGain[1] * frame[1] + screenGain[2] * frame[2];

    if (! std::isfinite (mix))
        mix = 0.0;

    int samplesAgo = 0;

    if (mixHighPeaks.process (mix, samplesAgo))     mixAttack (inputIndex - samplesAgo);
    if (mixPeaks.process (mix, samplesAgo))         mixAttack (inputIndex - samplesAgo);
    if (mixOnsets.process (mix, samplesAgo))        mixAttack (inputIndex - samplesAgo);

    ++inputIndex;
}

void TransientDetector::processDecimated (const double* frame)
{
    const double mix = screenGain[0] * frame[0] + std::sqrt (2.0) * screenGain[1] * frame[1] + screenGain[2] * frame[2];
    const double input[2] = { mix, frame[3] };

    for (int c = 0; c < 2; ++c)
    {
        auto& st = filterState[(size_t) c];
        const double y = inputHighPass.b0 * input[c] + st[0];
        st[0] = inputHighPass.b1 * input[c] - inputHighPass.a1 * y + st[1];
        st[1] = inputHighPass.b2 * input[c] - inputHighPass.a2 * y;
        ring[(size_t) c][(size_t) (sampleIndex & (historySize - 1))] = y;
    }

    int samplesAgo = 0;

    if (lfeOnsets.process (frame[3], samplesAgo))
        recentLfe.push_back (sampleIndex - samplesAgo);

    while (! recentLfe.empty() && recentLfe.front() < sampleIndex - historySize)
        recentLfe.pop_front();

    // Analyse an attack of the mix once its whole segment, and the LFE around it, has arrived
    while (! pendingMix.empty() && sampleIndex >= pendingMix.front() - preSamples + segmentLength + maxLag)
    {
        analyse (pendingMix.front());
        pendingMix.pop_front();
    }

    ++sampleIndex;
}

void TransientDetector::analyse (long long onsetIndex)
{
    const long long start = onsetIndex - preSamples;        // segment holding the attack
    const long long reference = start - segmentLength;      // same length, just before it

    const long long base = reference - maxLag - whitenOrder;      // first sample needed

    if (start - maxLag < 0 || base < sampleIndex - historySize + 1)
        return;

    // Both signals through one and the same whitening filter, fitted to what
    // was sounding just before the attack (linear prediction on the reference
    // stretch of the mix and of the LFE together). A held bass note is thus
    // taken out of both before they are compared, and since the filter is the
    // same for both, it shifts neither against the other. This is the
    // prefiltering of the generalised cross-correlation (Knapp & Carter 1976).
    const int total = (int) (start + segmentLength + maxLag - base) + 1;
    std::vector<double> mixW ((size_t) total), lfeW ((size_t) total);
    {
        double r[whitenOrder + 1] = {};

        for (int which = 0; which < 2; ++which)
        {
            double own[whitenOrder + 1] = {};

            for (int k = 0; k <= whitenOrder; ++k)
                for (int i = 0; i + k < segmentLength; ++i)
                    own[k] += window[(size_t) i] * history (which, reference + i) * window[(size_t) (i + k)] * history (which, reference + i + k);

            if (own[0] > segmentLength * floorPower)
                for (int k = 0; k <= whitenOrder; ++k)
                    r[k] += own[k] / own[0];
        }

        // Levinson-Durbin; the small load on r[0] limits how deep a notch can get
        double a[whitenOrder + 1] = { 1.0 }, error = r[0] * 1.003 + 1e-9;

        for (int m = 1; m <= whitenOrder && r[0] > 0.0; ++m)
        {
            double acc = r[m];

            for (int k = 1; k < m; ++k)
                acc += a[k] * r[m - k];

            const double reflection = -acc / error;
            double next[whitenOrder + 1];

            for (int k = 1; k < m; ++k)
                next[k] = a[k] + reflection * a[m - k];

            for (int k = 1; k < m; ++k)
                a[k] = next[k];

            a[m] = reflection;
            error *= 1.0 - reflection * reflection;

            if (error <= 0.0)
                break;
        }

        for (int i = whitenOrder; i < total; ++i)
        {
            double m = 0.0, l = 0.0;

            for (int k = 0; k <= whitenOrder; ++k)
            {
                m += a[k] * history (0, base + i - k);
                l += a[k] * history (1, base + i - k);
            }

            mixW[(size_t) i] = m;
            lfeW[(size_t) i] = lfeGainLinear * l;
        }
    }

    const auto mixAt = [&] (long long index) { return mixW[(size_t) (index - base)]; };
    const auto lfeAt = [&] (long long index) { return lfeW[(size_t) (index - base)]; };

    // Analytic (one-sided, band-limited) version of a windowed segment of the mix
    const auto analytic = [&] (long long from, std::vector<double>* magnitude)
    {
        std::vector<std::complex<double>> a (fftSize);

        for (int i = 0; i < segmentLength; ++i)
            a[(size_t) i] = window[(size_t) i] * mixAt (from + i);

        fft (a, false);

        if (magnitude != nullptr)
            for (int k = 0; k < fftSize / 2; ++k)
                (*magnitude)[(size_t) k] = std::abs (a[(size_t) k]);

        for (int k = 0; k < fftSize; ++k)
        {
            const double hz = k * fs / fftSize;
            const bool keep = k < fftSize / 2 && hz >= bandLoHz && hz <= bandHiHz;
            a[(size_t) k] = keep ? a[(size_t) k] * 2.0 : std::complex<double>();
        }

        fft (a, true);
        return a;
    };

    std::vector<double> magnitudeX ((size_t) fftSize / 2);
    const auto xNew = analytic (start, &magnitudeX), xRef = analytic (reference, nullptr);

    double energyNew = 0.0, energyRef = 0.0;

    for (int i = 0; i < segmentLength; ++i)
    {
        energyNew += 0.5 * std::norm (xNew[(size_t) i]);
        energyRef += 0.5 * std::norm (xRef[(size_t) i]);
    }

    if (energyNew <= 0.0)
        return;

    // What the attack brought to the low end of the mix: new minus reference.
    // No low end of its own (a hi-hat): nothing to line the LFE up with.
    const bool hasLow = energyNew - energyRef > 0.25 * energyNew;
    const double energyX = std::max (energyNew - energyRef, 0.25 * energyNew);

    // Complex cross-correlation against the LFE for every lag, new minus
    // reference. Its real part is the ordinary correlation; its magnitude is
    // the envelope, whose peak locates the LFE transient in time whatever
    // its phase.
    const int lags = 2 * maxLag + 1;
    std::vector<std::complex<double>> cross ((size_t) lags);
    std::vector<double> energyLfe ((size_t) lags), lfeNew ((size_t) lags), lfeRef ((size_t) lags), envelope ((size_t) lags);
    int best = 0;

    for (int li = 0; li < lags; ++li)
    {
        const int lag = li - maxLag;
        std::complex<double> sumNew, sumRef;
        double eNew = 0.0, eRef = 0.0;

        for (int i = 0; i < segmentLength; ++i)
        {
            const double w = window[(size_t) i];
            const double a = w * lfeAt (start + i + lag), b = w * lfeAt (reference + i + lag);
            sumNew += std::conj (xNew[(size_t) i]) * a;
            sumRef += std::conj (xRef[(size_t) i]) * b;
            eNew += a * a;
            eRef += b * b;
        }

        cross[(size_t) li] = sumNew - sumRef;
        lfeNew[(size_t) li] = eNew;
        lfeRef[(size_t) li] = eRef;
        energyLfe[(size_t) li] = std::max (eNew - eRef, 0.25 * eNew);

        // Divided by the LFE energy at that lag, or a long ringing boom would
        // pull the peak towards wherever the window holds more of it
        envelope[(size_t) li] = std::abs (cross[(size_t) li]) / std::sqrt (eNew + 1e-60);

        if (envelope[(size_t) li] > envelope[(size_t) best])
            best = li;
    }

    const auto normalised = [&] (int li, double value)
    {
        return value / std::sqrt (energyX * energyLfe[(size_t) li] + 1e-60);  // value: a raw cross term
    };

    const auto interaction = [&] (int li, double correlation)
    {
        const double el = energyLfe[(size_t) li];
        return correlation * 2.0 * std::sqrt (energyX * el) / (energyX + el + 1e-60);
    };

    TransientEvent e;
    e.match = hasLow ? std::min (1.0, normalised (best, std::abs (cross[(size_t) best]))) : 0.0;
    e.phase = std::arg (cross[(size_t) best]);
    e.interactionBest = interaction (best, e.match);
    e.correlationNow = hasLow ? std::clamp (normalised (maxLag, cross[(size_t) maxLag].real()), -1.0, 1.0) : 0.0;
    e.interactionNow = std::clamp (interaction (maxLag, e.correlationNow), -1.0, 1.0);

    {
        // Level of the attack itself, on the signal as it is
        double now = 0.0, before = 0.0;

        for (int i = 0; i < segmentLength; ++i)
        {
            const double a = window[(size_t) i] * history (0, start + i), b = window[(size_t) i] * history (0, reference + i);
            now += a * a;
            before += b * b;
        }

        e.levelDb = 10.0 * std::log10 (std::max (now - before, 0.25 * now) / segmentLength + 1e-30);
    }

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
        lfe[(size_t) i] = window[(size_t) i] * lfeAt (start + i + best - maxLag);

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

    // The likeness that counts here is the one actually present in the
    // segment, with everything else that sounds in it: that is what blurs
    // the peak.
    const double present = std::abs (cross[(size_t) best]) / std::sqrt (energyNew * lfeNew[(size_t) best] + 1e-60);
    const double m2 = hasLow ? std::clamp (present * present, 1e-6, 0.999) : 1e-6;
    e.delayUncertaintyMs = 1000.0 * std::sqrt ((1.0 - m2) / (m2 * independent))
                             / (2.0 * pi * std::max (rmsBandwidth, 0.5));

    // Is this attack of the mix in the LFE as well? Either the LFE has an
    // attack of its own next to it, or its energy rises there (by 1 dB at
    // least) with a shape that matches the mix.
    bool near = false;
    const auto before = (long long) std::lround (lfeNearBeforeSeconds * fs), after = (long long) std::lround (lfeNearAfterSeconds * fs);

    for (const auto index : recentLfe)
        near = near || (index >= onsetIndex - before && index <= onsetIndex + after);

    const bool rises = lfeNew[(size_t) best] > lfeRiseRatio * lfeRef[(size_t) best]
                         && lfeRef[(size_t) best] >= 0.0;
    e.inLfe = near || (rises && e.match >= lfeRiseMatch);
    events.push_back (e);
}

} // namespace subguard
