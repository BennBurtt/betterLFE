#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "../core/Timecode.h"

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace subguard
{

/** Real-time side of the app: picks L, C, R, LFE out of the device input and
    hands them to the message thread through a lock-free FIFO. The audio
    thread never allocates, locks or analyses anything. */
class AudioInput : public juce::AudioIODeviceCallback
{
public:
    AudioInput()  { setChannelIndices ({ { 0, 2, 1, 3 } }); }

    static constexpr int frameSize = 4;      // L, C, R, LFE

    void setChannelIndices (const std::array<int, 4>& indices)
    {
        for (size_t i = 0; i < 4; ++i)
            channelIndex[i].store (indices[i]);
    }

    /** Reads up to maxFrames interleaved frames of frameSize samples. One reader only: the analysis thread. */
    int read (float* dest, int maxFrames)
    {
        const auto scope = fifo.read (juce::jmin (maxFrames, fifo.getNumReady()));

        std::copy_n (buffer.data() + (size_t) scope.startIndex1 * frameSize, (size_t) scope.blockSize1 * frameSize, dest);
        std::copy_n (buffer.data() + (size_t) scope.startIndex2 * frameSize, (size_t) scope.blockSize2 * frameSize,
                     dest + (size_t) scope.blockSize1 * frameSize);

        readCount.fetch_add (scope.blockSize1 + scope.blockSize2);
        return scope.blockSize1 + scope.blockSize2;
    }

    /** Frames handed to the reader so far (since the device started): where the analysis is now. */
    long long framesRead() const    { return readCount.load(); }

    /** LTC: the input channel that carries it (-1: none). Read on the audio thread, published here. */
    void setLtcChannel (int channel)        { ltcChannel.store (channel); }
    int ltcChannelIndex() const             { return ltcChannel.load(); }

    struct LtcAnchor
    {
        bool valid = false;
        long long labelFrames = 0;      // frame count of the frame that starts at `startSample`
        long long startSample = 0;      // in the device stream, counted like framesRead()
        FrameRate rate = FrameRate::fps25;
        bool reverse = false;
    };

    /** The latest LTC frame read, any thread. */
    LtcAnchor ltc() const
    {
        LtcAnchor a;

        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const auto before = ltcSequence.load();

            if ((before & 1u) != 0)
                continue;

            a.valid = ltcValid.load();
            a.labelFrames = ltcFrames.load();
            a.startSample = ltcStart.load();
            a.rate = (FrameRate) ltcRate.load();
            a.reverse = ltcReverse.load();

            if (ltcSequence.load() == before)
                return a;
        }

        return {};
    }

    /** Stage measurement: plays `signal` on one output and records one input in the same callback, so the
        two are aligned to the sample. Message thread, while no measurement runs. */
    bool startMeasurement (std::vector<float> signal, int recordFrames, int outputChannel, int inputChannel)
    {
        if (measureState.load() == 1)
            return false;

        measureSignal = std::move (signal);
        measureRecording.assign ((size_t) juce::jmax (recordFrames, (int) measureSignal.size()), 0.0f);
        measureOut = outputChannel;
        measureIn = inputChannel;
        measurePos = 0;
        measureState.store (1);
        return true;
    }

    void cancelMeasurement()                    { measureState.store (0); }
    bool measurementDone() const                { return measureState.load() == 2; }
    bool measuring() const                      { return measureState.load() == 1; }
    double measurementProgress() const          { return measureRecording.empty() ? 0.0 : (double) measurePos.load() / (double) measureRecording.size(); }

    /** The recording, once done (then the next measurement can start). */
    std::vector<float> takeRecording()
    {
        measureState.store (0);
        return std::move (measureRecording);
    }

    bool isRunning() const          { return running.load(); }
    double sampleRate() const       { return rate.load(); }
    int generation() const          { return startCount.load(); }   // bumps on every (re)start
    float level() const             { return rms.load(); }          // all inputs, last block
    int droppedFrames() const       { return dropped.load(); }

    //==============================================================================
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        rate.store (device->getCurrentSampleRate());
        ltcReader.reset (device->getCurrentSampleRate());
        ltcBase = 0;
        ltcReaderChannel = -2;
        streamCount = 0;
        readCount.store (0);
        ltcValid.store (false);
        ++startCount;
        running.store (true);
    }

    void audioDeviceStopped() override
    {
        running.store (false);
        rms.store (0.0f);
    }

    void audioDeviceIOCallbackWithContext (const float* const* input, int numInputs,
                                           float* const* output, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        for (int c = 0; c < numOutputs; ++c)
            if (output[c] != nullptr)
                juce::FloatVectorOperations::clear (output[c], numSamples);

        // Stage measurement: the sweep out, the microphone in, sample for sample
        if (measureState.load() == 1)
        {
            float* out = measureOut >= 0 && measureOut < numOutputs ? output[measureOut] : nullptr;
            const float* mic = measureIn >= 0 && measureIn < numInputs ? input[measureIn] : nullptr;
            int pos = measurePos.load (std::memory_order_relaxed);
            const int length = (int) measureRecording.size();

            for (int i = 0; i < numSamples && pos < length; ++i, ++pos)
            {
                if (out != nullptr && pos < (int) measureSignal.size())
                    out[i] = measureSignal[(size_t) pos];

                measureRecording[(size_t) pos] = mic != nullptr ? mic[i] : 0.0f;
            }

            measurePos.store (pos);

            if (pos >= length)
                measureState.store (2);
        }

        const float* source[4];

        for (size_t i = 0; i < 4; ++i)
        {
            const int index = channelIndex[i].load (std::memory_order_relaxed);
            source[i] = (index >= 0 && index < numInputs) ? input[index] : nullptr;
        }

        // LTC: read on its channel, sample by sample, its frames published with their place in the stream
        const int ltcIndex = ltcChannel.load (std::memory_order_relaxed);

        if (ltcIndex != ltcReaderChannel)
        {
            ltcReaderChannel = ltcIndex;
            ltcReader.reset (rate.load());
            ltcBase = streamCount;
            ltcValid.store (false);
        }

        if (ltcIndex >= 0 && ltcIndex < numInputs && input[ltcIndex] != nullptr)
            for (int i = 0; i < numSamples; ++i)
                if (ltcReader.push (input[ltcIndex][i]))
                {
                    const auto& f = ltcReader.last();
                    const auto r = ltcReader.rate();
                    ltcSequence.fetch_add (1);
                    ltcFrames.store (Timecode::framesFromLabel (f.h, f.m, f.s, f.f, r) + (f.reverse ? -1 : 1));
                    ltcStart.store (ltcBase + f.endSample);
                    ltcRate.store ((int) r);
                    ltcReverse.store (f.reverse);
                    ltcValid.store (true);
                    ltcSequence.fetch_add (1);
                }

        streamCount += numSamples;
        double energy = 0.0;

        for (int c = 0; c < numInputs; ++c)
            if (input[c] != nullptr && c != ltcIndex)
                for (int i = 0; i < numSamples; ++i)
                    energy += (double) input[c][i] * input[c][i];

        if (numInputs > 0 && numSamples > 0)
            rms.store ((float) std::sqrt (energy / ((double) numInputs * numSamples)), std::memory_order_relaxed);

        const int writable = juce::jmin (numSamples, fifo.getFreeSpace());

        if (writable < numSamples)
            dropped.fetch_add (numSamples - writable, std::memory_order_relaxed);

        const auto scope = fifo.write (writable);
        int pos = 0;

        for (const auto& block : { std::pair<int, int> { scope.startIndex1, scope.blockSize1 },
                                   std::pair<int, int> { scope.startIndex2, scope.blockSize2 } })
        {
            float* dest = buffer.data() + (size_t) block.first * frameSize;

            for (int i = 0; i < block.second; ++i, ++pos, dest += frameSize)
                for (size_t c = 0; c < 4; ++c)
                    dest[c] = source[c] != nullptr ? source[c][pos] : 0.0f;
        }
    }

private:
    static constexpr int capacity = 1 << 16;     // frames; over 300 ms even at 192 kHz

    juce::AbstractFifo fifo { capacity };
    std::vector<float> buffer = std::vector<float> ((size_t) capacity * frameSize);

    std::array<std::atomic<int>, 4> channelIndex;
    std::atomic<double> rate { 48000.0 };
    std::atomic<int> startCount { 0 }, dropped { 0 };
    std::atomic<bool> running { false };
    std::atomic<float> rms { 0.0f };
    std::atomic<long long> readCount { 0 };

    // Stage measurement (buffers written by the message thread only while idle)
    std::vector<float> measureSignal, measureRecording;
    int measureOut = -1, measureIn = -1;
    std::atomic<int> measurePos { 0 }, measureState { 0 };       // 0 idle, 1 running, 2 done

    // LTC, audio thread side, then published under a sequence counter
    LtcReader ltcReader;
    long long ltcBase = 0, streamCount = 0;
    int ltcReaderChannel = -2;
    std::atomic<int> ltcChannel { -1 }, ltcRate { (int) FrameRate::fps25 };
    std::atomic<unsigned> ltcSequence { 0 };
    std::atomic<long long> ltcFrames { 0 }, ltcStart { 0 };
    std::atomic<bool> ltcValid { false }, ltcReverse { false };
};

} // namespace subguard
