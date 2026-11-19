// The Log tab: what SubGUARD saw, stamped with the timeline's timecode (LTC
// read on an input channel, or a session clock from a start timecode), and
// its export as CSV or as markers (EDL).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Widgets.h"
#include "Lang.h"
#include "../core/EventLog.h"

#include <functional>

namespace subguard
{

class LogView : public juce::Component
{
public:
    LogView();

    // Timecode source: -1 the session clock, otherwise the input channel carrying LTC
    std::function<void (int channel)> onTimecodeSource;
    std::function<void (int rate)> onRate;                  // -1: as read from the LTC
    std::function<void (double seconds)> onStart;           // session clock: timecode of its start
    std::function<void()> onClear, onExportCsv, onExportEdl;

    /** The choices as saved, without calling back. */
    void setChoices (int ltcChannel, int rate, double startSeconds, int numInputs);

    /** Each frame: the events, the rate they are shown at, the clock now and where it comes from. */
    void setState (const std::vector<LogEvent>& events, FrameRate rate, const juce::String& clock, const juce::String& source, bool clockOk);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class List : public juce::Component
    {
    public:
        explicit List (LogView& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        int preferredHeight() const;

    private:
        LogView& owner;
    };

    void fillSources (int numInputs);

    List list { *this };
    juce::Viewport viewport;
    juce::Label sourceLabel, rateLabel, startLabel;
    juce::ComboBox sourceBox, rateBox;
    juce::TextEditor startEditor;
    juce::TextButton clearButton, csvButton, edlButton;
    std::vector<LogEvent> shown;
    FrameRate shownRate = FrameRate::fps25;
    juce::String clockText, sourceText;
    bool clockGood = true;
    int inputs = 0;
};

} // namespace subguard
