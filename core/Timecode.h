// SMPTE timecode: frame rates, labels (drop frame included) and a linear
// timecode (LTC) reader and writer. Portable C++17, no dependencies.
//
// LTC (SMPTE ST 12-1): 80 bits per frame, biphase mark coded: a transition at
// every bit boundary, and one more in the middle of a bit that is 1. Bits 0-63
// carry the time in BCD (least significant bit first) with the user bits, bits
// 64-79 the sync word 0011 1111 1111 1101, which also gives the direction.
// Drop frame (29.97): frame labels 00 and 01 are skipped at the start of every
// minute except minutes 0, 10, 20, 30, 40 and 50.

#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace subguard
{

enum class FrameRate { fps23976, fps24, fps25, fps2997drop, fps2997, fps30, numRates };

struct Timecode
{
    static const char* name (FrameRate);
    static int nominal (FrameRate);             // frames per labelled second: 24, 25 or 30
    static double real (FrameRate);             // frames per second of real time: 24000/1001 ...
    static bool isDrop (FrameRate r)            { return r == FrameRate::fps2997drop; }

    /** Frame count from 00:00:00:00 to a label (drop frame labels as counted by the standard). */
    static long long framesFromLabel (int h, int m, int s, int f, FrameRate);

    /** Label of a frame count, as HH:MM:SS:FF (HH:MM:SS;FF in drop frame). Wraps at 24 hours. */
    static std::string label (long long frames, FrameRate);

    /** Label of a time in seconds of real time from 00:00:00:00. */
    static std::string labelOfSeconds (double seconds, FrameRate r)
    {
        return label ((long long) (seconds * real (r) + 1e-6), r);
    }

    /** Reads a label typed by a person (HH:MM:SS:FF, ';' or '.' for the last separator). Returns false if it is not one. */
    static bool parse (const std::string& text, FrameRate, double& seconds);
};

/** Reads LTC from an audio channel, sample by sample. Real-time safe: no allocation, no lock. */
class LtcReader
{
public:
    explicit LtcReader (double sampleRate = 48000.0)    { reset (sampleRate); }

    void reset (double sampleRate);

    /** Feeds one sample. Returns true when a frame was just read (then see last()). */
    bool push (float sample);

    struct Frame
    {
        int h = 0, m = 0, s = 0, f = 0;
        bool dropFlag = false;
        bool reverse = false;           // read backwards (tape or timeline playing in reverse)
        long long endSample = 0;        // sample (counted from reset) where its last bit ended: the next frame starts here
        double framesPerSecond = 0.0;   // measured from the spacing of frames, 0 until two follow each other
    };

    const Frame& last() const           { return frame; }

    /** The rate the frames are sent at, from the drop flag and the measured spacing; fps25 until known. */
    FrameRate rate() const;

    long long samples() const           { return count; }

private:
    void bit (int value);

    double rate_ = 48000.0, bitPeriod = 0.0;
    long long count = 0, lastTransition = 0, previousFrameEnd = -1;
    float hysteresis = 0.0f;
    int level = 0;                      // -1, 0 (unknown) or +1
    bool halfPending = false;
    std::array<uint8_t, 80> bits {};
    int filled = 0;
    double spacing = 0.0;               // samples between frames, smoothed
    Frame frame;
};

/** Writes LTC (for test signals). */
class LtcWriter
{
public:
    LtcWriter (double sampleRate, FrameRate, long long startFrame, float amplitude = 0.25f);

    float next();

private:
    void load();

    double rate_;
    FrameRate fr;
    long long frameNumber;
    float amp, level;
    std::array<uint8_t, 80> bits {};
    double bitSamples, position = 0.0;
    int bitIndex = 0;
    bool midDone = false;
};

} // namespace subguard
