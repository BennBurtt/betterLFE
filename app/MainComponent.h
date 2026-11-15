#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioInput.h"
#include "ChannelFormats.h"
#include "Widgets.h"
#include "../core/Inertia.h"

namespace subguard
{

/** A horizontal slider with its caption and current value above it. */
class LabelledSlider : public juce::Component,
                       public juce::SettableTooltipClient
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
    juce::String openCard;          // --card <name>: start with that panel open (for snapshots)
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
    enum CardId { sourceCard, analysisCard, spectrumCard, responseCard, alertCard, transientsCard, chainsCard, numCards };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void buildCards();
    void showCard (int index);      // -1 closes whichever is open
    void positionCards();
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
    AlertTracker::Options alertOptions() const;
    void applyTransientSettings();
    PlaybackSimulator::Level currentLevel() const;

    LaunchOptions options;
    juce::ApplicationProperties properties;

    juce::AudioDeviceManager deviceManager;
    AudioInput input;
    std::unique_ptr<LfeEngine> engine;
    AlertTracker alerts;
    PlaybackSimulator simulator;
    Settings engineSettings;

    HeaderBar header;
    SpectrumPlot spectrum;
    TransientStrip transientStrip;
    CorrelationPlot correlation;
    HistoryStrip history;

    // One button per panel; a panel opens over the analyzer, one at a time
    std::array<juce::TextButton, numCards> cardButtons;
    std::array<Card, numCards> cards { { Card ("Source & Format"), Card ("Analysis"), Card ("Spectrum"),
                                         Card ("Responsiveness"), Card ("Alert"), Card ("Transients"), Card ("Playback chains") } };
    int openCard = -1;

    juce::Label formatLabel { {}, "Channel Format:" }, deviceLabel { {}, "Input Device:" },
                fpsLabel { {}, "Refresh Rate:" }, windowLabel { {}, "Time / Frequency:" },
                rangeLabel { {}, "Level range:" }, statusLabel;
    juce::ComboBox formatBox, deviceBox, fpsBox, windowBox, rangeBox, alertModeBox;
    std::array<juce::TextButton, 3> levelButtons;       // Soft, Medium, Strong
    juce::ToggleButton lfeGainToggle { "LFE +10 dB (theatre level)" }, animationToggle { "Background animation" },
                       peakHoldToggle { "Peak hold" }, tiltToggle { "Bass tilt (old display)" },
                       levelWeightToggle { "Weigh by level balance" };
    LabelledSlider inertia { "Inertia:", 0, 100, 1, 70 }, integration { "Integration:", 0.5, 5.0, 0.1, 2.0 },
                   decay { "Decay:", 0.1, 4.0, 0.05, 1.0 };
    juce::ToggleButton transientToggle { "Transient check on" };
    LabelledSlider tAttack { "Attack:", 0, 100, 1, 40 }, tHoldProfile { "Hold:", 0, 100, 1, 25 },
                   tRelease { "Release:", 0, 100, 1, 50 }, tSensitivity { "Sensitivity:", 0, 100, 1, 70 },
                   tSearch { "Search range (how far off the LFE may be):", 5, 500, 5, 250 },
                   tMatch { "Likeness needed for a phase reading:", 0, 1, 0.05, 0.3 },
                   tHold { "Display time:", 1, 20, 0.5, 4 };
    juce::TextButton transientDefaultsButton { "Defaults" };
    ChainsView chainsView;
    juce::TextButton resetSessionButton { "Reset session" };

    double lastTick = 0.0, engineRate = 0.0, demoTime = 0.0;
    double historyClock = 0.0;
    int displayedScore = -1;

    // Display inertia of every number and curve shown
    std::array<Inertia, 5> readoutInertia;              // score, interaction, dependence, LFE balance, LFE share
    std::vector<Inertia> chainInertia;                  // two per playback system: dB and penalty
    std::array<std::vector<Inertia>, 3> curveInertia;   // correlation curves
    HistoryStrip::Sample historySample;
    std::array<double, 3> transientFlash { { 0.0, 0.0, 0.0 } };

    // How hard each screen channel plays, for the cones: this frame's peak
    // against the loudest of the last few seconds, so that it shows at any level
    void meterChannels (const float* interleaved, int numFrames);
    std::array<double, 3> framePeak { { 0.0, 0.0, 0.0 } }, recentPeak { { 0.0, 0.0, 0.0 } }, coneDrive { { 0.0, 0.0, 0.0 } };
    int inputGeneration = -1, framesShown = 0;
    bool restoring = false;
    std::vector<float> scratch;
    std::vector<std::array<double, 3>> demoTones;   // frequency, phase, group
};

} // namespace subguard
