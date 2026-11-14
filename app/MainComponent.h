#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioInput.h"
#include "ChannelFormats.h"
#include "Widgets.h"

namespace subguard
{

/** A horizontal slider with its caption and current value above it. */
class LabelledSlider : public juce::Component
{
public:
    LabelledSlider (const juce::String& caption, double min, double max, double step, double initial);

    double value() const                { return slider.getValue(); }
    void setValue (double v)            { slider.setValue (v, juce::sendNotificationSync); }

    std::function<juce::String (double)> format;
    std::function<void()> onChange;

    void refresh();
    void resized() override;

private:
    juce::Label caption, valueLabel;
    juce::Slider slider;
};

struct LaunchOptions
{
    bool demoSignal = false;        // --demo: built-in test signal instead of an audio device
    juce::File snapshotFile;        // --snapshot <png>: save the window after 4 s and quit
};

class MainComponent : public juce::Component,
                      private juce::Timer,
                      private juce::ChangeListener
{
public:
    explicit MainComponent (const LaunchOptions&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void buildSettingsPanel();
    void openDevices();
    void applyFormat();
    void selectDevice (const juce::String& name);
    void refreshDeviceList();
    void updateStatus (const juce::String& error = {});
    void rebuildEngine (double sampleRate);
    void feedDemoSignal (int numFrames);
    void loadState();
    void saveState();

    const ChannelFormat& format() const;
    int channelsNeeded() const      { return (int) format().channels.size(); }

    LaunchOptions options;
    juce::ApplicationProperties properties;

    juce::AudioDeviceManager deviceManager;
    AudioInput input;
    std::unique_ptr<LfeEngine> engine;
    AlertTracker alerts;
    Settings engineSettings;

    HeaderBar header;
    SpectrumPlot spectrum;
    CorrelationPlot correlation;

    juce::Component panel;
    juce::GroupComponent sourceGroup { {}, "Source & Format" }, analysisGroup { {}, "Analysis" },
                         responseGroup { {}, "Responsiveness" }, alertGroup { {}, "Phase Alert" };
    juce::Label formatLabel { {}, "Channel Format:" }, deviceLabel { {}, "Input Device:" },
                fpsLabel { {}, "Refresh Rate:" }, windowLabel { {}, "Time / Frequency:" }, statusLabel;
    juce::ComboBox formatBox, deviceBox, fpsBox, windowBox;
    juce::ToggleButton lfeGainToggle { "LFE +10 dB (theatre level)" }, animationToggle { "Background animation" };
    LabelledSlider inertia { "Inertia:", 0, 100, 1, 70 }, integration { "Integration:", 0.5, 5.0, 0.1, 2.0 },
                   threshold { "Threshold:", 0.0, 1.0, 0.01, 0.33 }, decay { "Decay:", 0.1, 4.0, 0.05, 1.0 };
    juce::TextButton settingsButton { "Settings" };

    double lastTick = 0.0, engineRate = 0.0, demoTime = 0.0;
    int inputGeneration = -1, framesShown = 0;
    bool panelVisible = true, restoring = false;
    std::vector<float> scratch;
    std::vector<std::array<double, 3>> demoTones;   // frequency, phase, group
};

} // namespace subguard
