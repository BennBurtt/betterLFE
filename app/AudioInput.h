#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

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

    void setChannelIndices (const std::array<int, 4>& indices)
    {
        for (size_t i = 0; i < 4; ++i)
            channelIndex[i].store (indices[i]);
    }

    /** Reads up to maxFrames interleaved L, C, R, LFE frames. Message thread only. */
    int read (float* dest, int maxFrames)
    {
        const auto scope = fifo.read (juce::jmin (maxFrames, fifo.getNumReady()));

        std::copy_n (buffer.data() + (size_t) scope.startIndex1 * 4, (size_t) scope.blockSize1 * 4, dest);
        std::copy_n (buffer.data() + (size_t) scope.startIndex2 * 4, (size_t) scope.blockSize2 * 4,
                     dest + (size_t) scope.blockSize1 * 4);

        return scope.blockSize1 + scope.blockSize2;
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

        const float* source[4];

        for (size_t i = 0; i < 4; ++i)
        {
            const int index = channelIndex[i].load (std::memory_order_relaxed);
            source[i] = (index >= 0 && index < numInputs) ? input[index] : nullptr;
        }

        double energy = 0.0;

        for (int c = 0; c < numInputs; ++c)
            if (input[c] != nullptr)
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
            float* dest = buffer.data() + (size_t) block.first * 4;

            for (int i = 0; i < block.second; ++i, ++pos, dest += 4)
                for (size_t c = 0; c < 4; ++c)
                    dest[c] = source[c] != nullptr ? source[c][pos] : 0.0f;
        }
    }

private:
    static constexpr int capacity = 1 << 16;     // frames; over 300 ms even at 192 kHz

    juce::AbstractFifo fifo { capacity };
    std::vector<float> buffer = std::vector<float> ((size_t) capacity * 4);

    std::array<std::atomic<int>, 4> channelIndex;
    std::atomic<double> rate { 48000.0 };
    std::atomic<int> startCount { 0 }, dropped { 0 };
    std::atomic<bool> running { false };
    std::atomic<float> rms { 0.0f };
};

} // namespace subguard
