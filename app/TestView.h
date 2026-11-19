// The Test tab: the built-in test signals (core/TestSignals.h), each with what
// to look for. Click one to play it; it reaches the analysis as an ordinary
// input once "SubGUARD test signals" is chosen as the input device (Source).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Widgets.h"
#include "Lang.h"
#include "../core/TestSignals.h"

#include <functional>

namespace subguard
{

class TestView : public juce::Component
{
public:
    TestView();

    std::function<void (int)> onPlay;               // a signal clicked (index into TestSignals::all()), -1 to stop
    std::function<void()> onUseTestSource;          // switch the input device to the test signals
    std::function<void()> onLoadRooms;              // the three Room Sim Pro rooms of the measurements

    /** Each frame: whether the test signals are the input, which one plays and how far into its pass. */
    void setState (bool testSourceOn, int playing, double seconds);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class List : public juce::Component
    {
    public:
        explicit List (TestView& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        int preferredHeight() const;

    private:
        struct Row { int scene; juce::Rectangle<int> bounds; };
        std::vector<Row> layout() const;
        TestView& owner;
        int hovered = -1;
    };

    List list { *this };
    juce::Viewport viewport;
    juce::TextButton useButton, stopButton, roomsButton;
    bool sourceOn = false;
    int playing = -1;
    double position = 0.0;
};

} // namespace subguard
