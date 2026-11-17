// The analysis thread: reads the audio (or makes the demo signal, or silence
// when there is no device) and runs the analysis at its own pace, 20 ms of
// audio at a time. The display never touches the audio: it reads the latest
// state the analysis published (AnalysisRunner::take).

#pragma once

#include "../core/AnalysisRunner.h"
#include "AudioInput.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>

namespace subguard
{

class AnalysisThread : private juce::Thread
{
public:
    explicit AnalysisThread (AudioInput& source)
        : juce::Thread ("SubGuard analysis"), input (source)
    {
        // Demo signal: LFE made of low tones; L shares its 28-50 Hz part in phase,
        // R its 70-110 Hz part with flipped polarity, C is unrelated.
        juce::Random random (42);

        for (double f = 21.0; f < 115.0; f += 1.3)
            demoTones.push_back ({ { f, random.nextDouble() * juce::MathConstants<double>::twoPi, 0.0 } });

        for (double f = 22.0; f < 150.0; f += 1.7)
            demoTones.push_back ({ { f, random.nextDouble() * juce::MathConstants<double>::twoPi, 1.0 } });
    }

    ~AnalysisThread() override     { stop(); }

    void start (bool demo, double sampleRate, double windowSeconds)
    {
        demoMode = demo;
        window = windowSeconds;
        build (sampleRate);
        startThread (juce::Thread::Priority::high);
    }

    void stop()                     { stopThread (2000); }

    /** The runner the display reads from. It changes when the device rate or the window changes. */
    std::shared_ptr<AnalysisRunner> runner()
    {
        std::lock_guard<std::mutex> lock (runnerLock);
        return current;
    }

    /** Bumps every time a new runner is built. */
    int generation() const          { return built.load(); }

    void setWindow (double seconds)
    {
        window = seconds;
        rebuildWanted = true;
    }

    void setSettings (const Settings& s)
    {
        std::lock_guard<std::mutex> lock (runnerLock);
        settings = s;

        if (current != nullptr)
            current->setSettings (s);
    }

    void setTransientSettings (const TransientSettings& t)
    {
        std::lock_guard<std::mutex> lock (runnerLock);
        transientSettings = t;

        if (current != nullptr)
            current->setTransientSettings (t);
    }

    void requestReset()
    {
        if (auto r = runner())
            r->requestReset();
    }

private:
    void build (double sampleRate)
    {
        auto r = std::make_shared<AnalysisRunner> (sampleRate, window.load());
        std::lock_guard<std::mutex> lock (runnerLock);
        r->setSettings (settings);
        r->setTransientSettings (transientSettings);
        current = r;
        rebuildWanted = false;
        ++built;
    }

    void run() override
    {
        std::vector<float> buffer ((size_t) blockFrames * AudioInput::frameSize);
        double last = juce::Time::getMillisecondCounterHiRes() * 0.001;
        double owed = 0.0;      // frames of demo signal or silence due, carried over

        while (! threadShouldExit())
        {
            wait (4);

            const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
            const double elapsed = juce::jlimit (0.0, 0.25, now - last);
            last = now;
            auto r = runner();

            if (! demoMode && input.isRunning())
            {
                // A (re)started device may run at another rate: start from a clean analysis
                if (input.generation() != seenGeneration || std::abs (input.sampleRate() - r->sampleRate()) > 0.5 || rebuildWanted)
                {
                    seenGeneration = input.generation();

                    while (input.read (buffer.data(), blockFrames) > 0) {}

                    build (input.sampleRate());
                    r = runner();
                }

                for (int n; (n = input.read (buffer.data(), blockFrames)) > 0;)
                    r->feed (buffer.data(), n);

                owed = 0.0;
                continue;
            }

            if (rebuildWanted)
            {
                build (r->sampleRate());
                r = runner();
            }

            // Demo signal, or silence so the display settles: made at the pace of the clock
            owed += elapsed * r->sampleRate();
            auto due = (int) owed;
            owed -= due;

            while (due > 0)
            {
                const int n = juce::jmin (due, blockFrames);

                if (demoMode)
                    makeDemo (buffer.data(), n, r->sampleRate());
                else
                    std::fill (buffer.begin(), buffer.begin() + (std::ptrdiff_t) n * AudioInput::frameSize, 0.0f);

                r->feed (buffer.data(), n);
                due -= n;
            }
        }
    }

    void makeDemo (float* out, int numFrames, double rate)
    {
        const double twoPi = juce::MathConstants<double>::twoPi;

        for (int i = 0; i < numFrames; ++i, out += AudioInput::frameSize)
        {
            double lfe = 0.0, shared = 0.0, flipped = 0.0, other = 0.0;
            const double t = demoTime + i / rate;

            for (const auto& tone : demoTones)
            {
                const double v = 0.012 * std::sin (twoPi * tone[0] * t + tone[1]);

                if (tone[2] > 0.5)      { other += v; continue; }

                lfe += v;

                if (tone[0] > 28.0 && tone[0] < 50.0)    shared += v;
                if (tone[0] > 70.0 && tone[0] < 110.0)   flipped -= v;
            }

            // A kick in the centre channel every 1.5 s, repeated 12 ms late in the LFE
            const auto kick = [twoPi] (double u)
            {
                if (u < 0.0)
                    return 0.0;

                const double phase = twoPi * (45.0 * u + 75.0 * 0.03 * (1.0 - std::exp (-u / 0.03)));
                return 0.5 * std::sin (phase) * std::exp (-u / 0.09) * (1.0 - std::exp (-u / 0.002));
            };

            const double sinceKick = std::fmod (t, 1.5);
            other += kick (sinceKick);
            lfe += kick (sinceKick - 0.012);

            out[chL] = (float) (1.4 * shared);
            out[chC] = (float) other;
            out[chR] = (float) (1.4 * flipped);
            out[chLFE] = (float) lfe;
        }

        demoTime += numFrames / rate;
    }

    static constexpr int blockFrames = 4096;

    AudioInput& input;
    bool demoMode = false;
    std::atomic<double> window { 1.0 };
    std::atomic<bool> rebuildWanted { false };
    std::atomic<int> built { 0 };
    int seenGeneration = -1;

    std::mutex runnerLock;
    std::shared_ptr<AnalysisRunner> current;
    Settings settings;
    TransientSettings transientSettings;

    std::vector<std::array<double, 3>> demoTones;
    double demoTime = 0.0;
};

} // namespace subguard
