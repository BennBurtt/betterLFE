#include "Timecode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace subguard
{

namespace
{
    // The sync word, bits 64 to 79 in the order they are sent
    constexpr uint8_t syncWord[16] = { 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1 };

    constexpr long long framesPerDay (FrameRate r)
    {
        return r == FrameRate::fps2997drop ? 2589408LL : 86400LL * (r == FrameRate::fps25 ? 25 : (r == FrameRate::fps24 || r == FrameRate::fps23976) ? 24 : 30);
    }

    int bcd (const std::array<uint8_t, 80>& b, int first, int count)
    {
        int v = 0;

        for (int i = 0; i < count; ++i)
            v |= b[(size_t) (first + i)] << i;

        return v;
    }

    void putBcd (std::array<uint8_t, 80>& b, int first, int count, int value)
    {
        for (int i = 0; i < count; ++i)
            b[(size_t) (first + i)] = (uint8_t) ((value >> i) & 1);
    }
}

//==============================================================================
const char* Timecode::name (FrameRate r)
{
    switch (r)
    {
        case FrameRate::fps23976:       return "23.976";
        case FrameRate::fps24:          return "24";
        case FrameRate::fps25:          return "25";
        case FrameRate::fps2997drop:    return "29.97 DF";
        case FrameRate::fps2997:        return "29.97 ND";
        case FrameRate::fps30:          return "30";
        default:                        return "";
    }
}

int Timecode::nominal (FrameRate r)
{
    return r == FrameRate::fps25 ? 25 : (r == FrameRate::fps24 || r == FrameRate::fps23976) ? 24 : 30;
}

double Timecode::real (FrameRate r)
{
    switch (r)
    {
        case FrameRate::fps23976:       return 24000.0 / 1001.0;
        case FrameRate::fps2997drop:
        case FrameRate::fps2997:        return 30000.0 / 1001.0;
        default:                        return (double) nominal (r);
    }
}

long long Timecode::framesFromLabel (int h, int m, int s, int f, FrameRate r)
{
    const long long fps = nominal (r);
    long long frames = ((long long) h * 3600 + (long long) m * 60 + s) * fps + f;

    if (isDrop (r))
    {
        const long long minutes = (long long) h * 60 + m;
        frames -= 2 * (minutes - minutes / 10);
    }

    return frames;
}

std::string Timecode::label (long long frames, FrameRate r)
{
    const long long day = framesPerDay (r);
    frames %= day;

    if (frames < 0)
        frames += day;

    if (isDrop (r))
    {
        // Every ten minutes hold 17982 frames; two labels are skipped in each minute but the tenth
        const long long tens = frames / 17982, rest = frames % 17982;
        frames += 18 * tens + (rest >= 2 ? 2 * ((rest - 2) / 1798) : 0);
    }

    const long long fps = nominal (r);
    const int f = (int) (frames % fps), s = (int) ((frames / fps) % 60), m = (int) ((frames / (fps * 60)) % 60), h = (int) (frames / (fps * 3600));
    char text[16];
    std::snprintf (text, sizeof (text), "%02d:%02d:%02d%c%02d", h, m, s, isDrop (r) ? ';' : ':', f);
    return text;
}

bool Timecode::parse (const std::string& text, FrameRate r, double& seconds)
{
    int fields[4] = { 0, 0, 0, 0 }, n = 0;
    bool digit = false;

    for (char c : text)
    {
        if (c >= '0' && c <= '9')
        {
            if (n >= 4)
                return false;

            fields[n] = fields[n] * 10 + (c - '0');
            digit = true;
        }
        else if (c == ':' || c == ';' || c == '.')
        {
            if (! digit)
                return false;

            ++n;
            digit = false;
        }
        else if (c != ' ')
        {
            return false;
        }
    }

    if (n != 3 || ! digit || fields[0] > 23 || fields[1] > 59 || fields[2] > 59 || fields[3] >= nominal (r))
        return false;

    seconds = (double) framesFromLabel (fields[0], fields[1], fields[2], fields[3], r) / real (r);
    return true;
}

//==============================================================================
void LtcReader::reset (double sampleRate)
{
    rate_ = sampleRate;
    bitPeriod = sampleRate / (80.0 * 27.0);     // between 24 and 30 frames per second: both are read from it
    count = 0;
    lastTransition = 0;
    previousFrameEnd = -1;
    hysteresis = 0.0f;
    level = 0;
    halfPending = false;
    bits.fill (0);
    filled = 0;
    spacing = 0.0;
    frame = {};
}

bool LtcReader::push (float sample)
{
    const long long now = count++;

    // A slowly decaying peak sets the switching threshold (a fifth of it), so any level reads
    hysteresis = std::max (std::abs (sample), hysteresis * 0.9995f);
    const float threshold = 0.2f * hysteresis;
    int next = level;

    if (hysteresis > 1e-4f)
    {
        if (sample > threshold)
            next = 1;
        else if (sample < -threshold)
            next = -1;
    }

    if (next == level)
        return false;

    const bool known = level != 0;
    level = next;

    if (! known)
    {
        lastTransition = now;
        return false;
    }

    const double interval = (double) (now - lastTransition);
    lastTransition = now;
    const auto framesBefore = frame.endSample;

    if (interval > 1.6 * bitPeriod || interval < 0.3 * bitPeriod)
    {
        // Not LTC, or a gap: start again
        halfPending = false;
        filled = 0;
        return false;
    }

    if (interval > 0.75 * bitPeriod)
    {
        bitPeriod += 0.05 * (interval - bitPeriod);

        if (halfPending)
        {
            halfPending = false;        // out of step: the next sync word puts it right
            filled = 0;
            return false;
        }

        bit (0);
    }
    else
    {
        bitPeriod += 0.05 * (2.0 * interval - bitPeriod);

        if (! halfPending)
        {
            halfPending = true;
            return false;
        }

        halfPending = false;
        bit (1);
    }

    return frame.endSample != framesBefore;
}

void LtcReader::bit (int value)
{
    std::memmove (bits.data(), bits.data() + 1, bits.size() - 1);
    bits[79] = (uint8_t) value;
    filled = std::min (filled + 1, 80);

    if (filled < 80)
        return;

    bool forward = true, backward = true;

    for (int i = 0; i < 16; ++i)
    {
        forward = forward && bits[(size_t) (64 + i)] == syncWord[i];
        backward = backward && bits[(size_t) i] == syncWord[15 - i];
    }

    if (! forward && ! backward)
        return;

    std::array<uint8_t, 80> b = bits;

    if (backward)
        std::reverse (b.begin(), b.end());

    Frame read;
    read.f = bcd (b, 0, 4) + 10 * bcd (b, 8, 2);
    read.dropFlag = b[10] != 0;
    read.s = bcd (b, 16, 4) + 10 * bcd (b, 24, 3);
    read.m = bcd (b, 32, 4) + 10 * bcd (b, 40, 3);
    read.h = bcd (b, 48, 4) + 10 * bcd (b, 56, 2);
    read.reverse = backward;
    read.endSample = lastTransition;

    if (read.f > 29 || read.s > 59 || read.m > 59 || read.h > 23)
        return;

    // Frames that follow each other give the rate
    if (previousFrameEnd >= 0)
    {
        const double d = (double) (read.endSample - previousFrameEnd);

        if (d > rate_ / 32.0 && d < rate_ / 22.0)
            spacing = spacing <= 0.0 ? d : spacing + 0.02 * (d - spacing);
    }

    previousFrameEnd = read.endSample;
    read.framesPerSecond = spacing > 0.0 ? rate_ / spacing : 0.0;
    frame = read;
    filled = 0;
}

FrameRate LtcReader::rate() const
{
    const double fps = frame.framesPerSecond;

    if (frame.dropFlag)
        return FrameRate::fps2997drop;

    if (fps <= 0.0 || std::abs (fps - 25.0) < 0.5)
        return FrameRate::fps25;

    if (fps < 24.5)
        return fps < 23.988 ? FrameRate::fps23976 : FrameRate::fps24;

    return fps < 29.985 ? FrameRate::fps2997 : FrameRate::fps30;
}

//==============================================================================
LtcWriter::LtcWriter (double sampleRate, FrameRate r, long long startFrame, float amplitude)
    : rate_ (sampleRate), fr (r), frameNumber (startFrame), amp (amplitude), level (1.0f),
      bitSamples (sampleRate / (80.0 * Timecode::real (r)))
{
    load();
}

void LtcWriter::load()
{
    // The label of this frame, in BCD, then the sync word
    const auto text = Timecode::label (frameNumber, fr);
    int h = 0, m = 0, s = 0, f = 0;
    std::sscanf (text.c_str(), "%d:%d:%d%*c%d", &h, &m, &s, &f);
    bits.fill (0);
    putBcd (bits, 0, 4, f % 10);
    putBcd (bits, 8, 2, f / 10);
    bits[10] = Timecode::isDrop (fr) ? 1 : 0;
    putBcd (bits, 16, 4, s % 10);
    putBcd (bits, 24, 3, s / 10);
    putBcd (bits, 32, 4, m % 10);
    putBcd (bits, 40, 3, m / 10);
    putBcd (bits, 48, 4, h % 10);
    putBcd (bits, 56, 2, h / 10);

    for (int i = 0; i < 16; ++i)
        bits[(size_t) (64 + i)] = syncWord[i];
}

float LtcWriter::next()
{
    if (position >= bitSamples)
    {
        position -= bitSamples;

        if (++bitIndex >= 80)
        {
            bitIndex = 0;
            ++frameNumber;
            load();
        }

        level = -level;         // a transition at every bit boundary
        midDone = false;
    }

    if (bits[(size_t) bitIndex] != 0 && ! midDone && position >= 0.5 * bitSamples)
    {
        level = -level;         // and one more in the middle of a 1
        midDone = true;
    }

    position += 1.0;
    return amp * level;
}

} // namespace subguard
