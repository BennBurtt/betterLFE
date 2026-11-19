// The Stage tab: your own room, measured. A sweep on a screen speaker, then on
// the subwoofer, recorded by a microphone at the mixing seat: how the two add
// up around their crossover, and the delay and polarity that would make the
// sum whole (core/StageAlignment).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Widgets.h"
#include "Lang.h"
#include "../core/StageAlignment.h"

#include <functional>

namespace subguard
{

class StageView : public juce::Component
{
public:
    StageView();

    struct Choice
    {
        juce::String outputDevice;
        int screenOutput = 2, subOutput = 3, micInput = 0;      // channels, from 0
        double levelDbfs = -20.0;
    };

    std::function<void (const Choice&)> onMeasure;
    std::function<void()> onStop;

    /** The devices and channel counts there are to choose from. */
    void setDevices (const juce::StringArray& outputDevices, const juce::String& current, int numOutputs, int numInputs);

    /** What the measurement is doing; `running` false when idle. */
    void setStatus (const juce::String& text, bool running);

    void setResult (const StageAlignment::Result&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Choice choice() const;

    juce::Label deviceLabel, screenLabel, subLabel, micLabel, levelLabel;
    juce::ComboBox deviceBox, screenBox, subBox, micBox, levelBox;
    juce::TextButton measureButton, stopButton;
    juce::String status;
    bool running = false;
    StageAlignment::Result result;
};

} // namespace subguard
