// Offline file analysis, app side: finds the reels among the chosen files
// (one multichannel file, or one mono file per channel), reads their timecode
// (BWF time reference) and runs core/OfflineAnalysis on a background thread.

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ChannelFormats.h"
#include "Lang.h"
#include "../core/OfflineAnalysis.h"

#include <atomic>
#include <map>
#include <memory>

namespace subguard
{

struct ReelSource
{
    juce::String name;
    juce::Array<juce::File> files;
    std::array<int, 4> file { { -1, -1, -1, -1 } };       // L, C, R, LFE: which file
    std::array<int, 4> channel { { -1, -1, -1, -1 } };    // and which channel in it
    juce::String channels;                                  // how they were found, for the report

    /** The chosen files as reels. Mono files named ..._L, ..._C, ..._R, ..._LFE (or . - space) are put together. */
    static std::vector<ReelSource> fromFiles (const juce::Array<juce::File>& chosen, const ChannelFormat& format)
    {
        std::vector<ReelSource> reels;
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        // The channel a mono file is named after, if any
        const auto labelOf = [] (const juce::String& stem, juce::String& prefix) -> int
        {
            const int cut = juce::jmax (stem.lastIndexOfChar ('_'), stem.lastIndexOfChar ('.'), stem.lastIndexOfChar ('-'), stem.lastIndexOfChar (' '));

            if (cut <= 0)
                return -1;

            const auto token = stem.substring (cut + 1).trim().toUpperCase();
            prefix = stem.substring (0, cut).trim();
            const char* names[4][3] = { { "L", "LEFT", "" }, { "C", "CENTER", "CENTRE" }, { "R", "RIGHT", "" }, { "LFE", "SW", "SUB" } };

            for (int c = 0; c < 4; ++c)
                for (const char* n : names[c])
                    if (*n != 0 && token == n)
                        return c;

            return -1;
        };

        std::map<juce::String, int> groups;

        for (const auto& f : chosen)
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));

            if (reader == nullptr)
                continue;

            juce::String prefix;
            const int label = reader->numChannels == 1 ? labelOf (f.getFileNameWithoutExtension(), prefix) : -1;

            if (label >= 0)
            {
                // A mono file of a set: joins the reel of its prefix
                auto it = groups.find (prefix.toLowerCase());
                int index;

                if (it == groups.end())
                {
                    index = (int) reels.size();
                    groups[prefix.toLowerCase()] = index;
                    ReelSource r;
                    r.name = prefix;
                    reels.push_back (r);
                }
                else
                {
                    index = it->second;
                }

                auto& r = reels[(size_t) index];
                r.file[(size_t) label] = r.files.size();
                r.channel[(size_t) label] = 0;
                r.files.add (f);
                continue;
            }

            // One file with every channel: the chosen format if it has the same count, else the WAV order (L R C LFE ...)
            ReelSource r;
            r.name = f.getFileNameWithoutExtension();
            r.files.add (f);
            const int n = (int) reader->numChannels;
            std::array<int, 4> map { { -1, -1, -1, -1 } };

            if (n == (int) format.channels.size())
            {
                map = format.analysisIndices();
                r.channels = tr ("channels as in ") + format.name;
            }
            else if (n == 1)
            {
                map = { { -1, 0, -1, -1 } };
                r.channels = tr ("one channel, taken as C");
            }
            else if (n == 2 || n == 3)
            {
                map = { { 0, n == 3 ? 2 : -1, 1, -1 } };
                r.channels = n == 2 ? tr ("two channels, taken as L and R") : tr ("three channels, taken as L, R, C");
            }
            else
            {
                map = { { 0, 2, 1, 3 } };
                r.channels = juce::String (n) + tr (" channels, WAV order (L, R, C, LFE, ...)");
            }

            for (int c = 0; c < 4; ++c)
                if (map[(size_t) c] >= 0 && map[(size_t) c] < n)
                {
                    r.file[(size_t) c] = 0;
                    r.channel[(size_t) c] = map[(size_t) c];
                }

            reels.push_back (r);
        }

        for (auto& r : reels)
            if (r.channels.isEmpty())
            {
                juce::StringArray found;
                const char* names[] = { "L", "C", "R", "LFE" };

                for (int c = 0; c < 4; ++c)
                    if (r.file[(size_t) c] >= 0)
                        found.add (names[c]);

                r.channels = tr ("one file per channel: ") + found.joinIntoString (", ");
            }

        return reels;
    }
};

/** Runs the reels one after the other on its own thread. */
class OfflineJob : public juce::Thread
{
public:
    OfflineJob (std::vector<ReelSource> sourcesToRun, const ReelSettings& settingsToUse)
        : juce::Thread ("SubGUARD offline analysis"), sources (std::move (sourcesToRun)), settings (settingsToUse)
    {
        formats.registerBasicFormats();
    }

    ~OfflineJob() override      { stopThread (10000); }

    std::atomic<int> reel { 0 };
    std::atomic<double> fraction { 0.0 };
    std::atomic<bool> finished { false };
    std::vector<ReelReport> reports;            // read once finished
    int numReels() const                        { return (int) sources.size(); }
    juce::String reelName (int i) const         { return i >= 0 && i < numReels() ? sources[(size_t) i].name : juce::String(); }

    void run() override
    {
        for (size_t i = 0; i < sources.size() && ! threadShouldExit(); ++i)
        {
            reel.store ((int) i);
            fraction.store (0.0);
            reports.push_back (runReel (sources[i], (int) i));
        }

        finished.store (true);
    }

private:
    ReelReport runReel (const ReelSource& src, int index)
    {
        std::vector<std::unique_ptr<juce::AudioFormatReader>> readers;

        for (const auto& f : src.files)
            readers.emplace_back (formats.createReaderFor (f));

        ReelReport failed;
        failed.name = src.name.toStdString();

        if (readers.empty() || std::any_of (readers.begin(), readers.end(), [] (const auto& r) { return r == nullptr; }))
        {
            failed.channels = tr ("could not be read").toStdString();
            return failed;
        }

        const double rate = readers[0]->sampleRate;
        long long length = readers[0]->lengthInSamples;

        for (const auto& r : readers)
            length = juce::jmin (length, (long long) r->lengthInSamples);

        // Timecode of the first sample: the BWF time reference, else the reel number in hours (reel 1 at 01:00:00:00)
        double start = (double) (index + 1) * 3600.0;
        juce::String tcSource = tr ("no time reference in the file: reel ") + juce::String (index + 1) + tr (" put at ") + juce::String (index + 1) + ":00:00:00";
        const auto reference = readers[0]->metadataValues.getValue (juce::WavAudioFormat::bwavTimeReference, {});

        if (reference.isNotEmpty() && rate > 0.0)
        {
            start = (double) reference.getLargeIntValue() / rate;
            tcSource = tr ("timecode from the BWF time reference");
        }
        else
        {
            const auto number = src.name.toLowerCase().fromLastOccurrenceOf ("reel", false, false).trim().getIntValue();
            const auto rNumber = src.name.toLowerCase().fromLastOccurrenceOf ("r", false, false).getIntValue();
            const int n = number > 0 ? number : rNumber;

            if (n > 0 && n < 24)
            {
                start = (double) n * 3600.0;
                tcSource = tr ("no time reference in the file: reel ") + juce::String (n) + tr (" (from its name) put at ") + juce::String (n) + ":00:00:00";
            }
        }

        juce::AudioBuffer<float> block ((int) readers.size() * 8, 8192);
        long long position = 0;

        const OfflineAnalyzer::Pull pull = [&] (float* out, int maxFrames) -> int
        {
            const int n = (int) juce::jmin ((long long) maxFrames, length - position);

            if (n <= 0)
                return 0;

            std::vector<std::vector<float>> channels (readers.size());

            for (size_t k = 0; k < readers.size(); ++k)
            {
                const int numCh = (int) readers[k]->numChannels;
                juce::AudioBuffer<float> b (numCh, n);
                readers[k]->read (&b, 0, n, position, true, true);
                channels[k].resize ((size_t) numCh * (size_t) n);

                for (int ch = 0; ch < numCh; ++ch)
                    std::copy_n (b.getReadPointer (ch), n, channels[k].data() + (size_t) ch * (size_t) n);
            }

            for (int c = 0; c < 4; ++c)
            {
                const int f = src.file[(size_t) c], ch = src.channel[(size_t) c];
                const float* from = f >= 0 && ch >= 0 && ch < (int) readers[(size_t) f]->numChannels ? channels[(size_t) f].data() + (size_t) ch * (size_t) n : nullptr;

                for (int i = 0; i < n; ++i)
                    out[i * 4 + c] = from != nullptr ? from[i] : 0.0f;
            }

            position += n;
            return n;
        };

        auto report = OfflineAnalyzer::analyse (src.name.toStdString(), rate, start, length, pull, settings,
                                                [this] (double f) { fraction.store (f); return ! threadShouldExit(); });
        report.timecodeSource = tcSource.toStdString();
        report.channels = src.channels.toStdString();
        return report;
    }

    std::vector<ReelSource> sources;
    ReelSettings settings;
    juce::AudioFormatManager formats;
};

} // namespace subguard
