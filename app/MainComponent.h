#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "AnalysisThread.h"
#include "AudioInput.h"
#include "ChannelFormats.h"
#include "Widgets.h"
#include "RoomProView.h"
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

    /** Marks a stretch of the track in a lighter shade (the values in common use). */
    void setHighlight (double from, double to)  { highlightFrom = from; highlightTo = to; repaint(); }

    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::Label caption, valueLabel;
    juce::Slider slider;
    double highlightFrom = 0.0, highlightTo = 0.0;
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
    enum CardId { sourceCard, spectrumCard, alertCard, transientsCard, chainsCard, devCard, numCards };

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
    double windowSeconds() const;
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
    AnalysisThread analysis { input };     // reads the audio and analyses it, 20 ms at a time
    Snapshot snapshot;                     // the latest state it published
    bool haveData = false;
    int runnerGeneration = -1;
    std::vector<double> shownFacade, shownLfe;     // spectrum as displayed (Inertia applies here only)
    AlertTracker alerts;
    PlaybackSimulator simulator;
    Settings engineSettings;

    HeaderBar header;
    SpectrumPlot spectrum;
    TransientStrip transientStrip;
    CorrelationPlot correlation;
    HistoryStrip history;

    // Room Sim: replaces the analyzer views (the score and the history strip stay)
    RoomSimulator roomSim;
    RoomSimView roomView;
    RoomSimResult roomResult;
    juce::TextButton roomButton { "Room Sim" };
    bool roomMode = false;
    RoomProView proView;
    juce::TextButton proButton { "Room Sim Pro" };
    bool proMode = false;

    // Rooms that count in the score: ticked in Playback chains or in Room Sim
    std::vector<bool> roomSelected = RoomSimulator::defaultSelection();
    std::vector<std::unique_ptr<juce::ToggleButton>> roomTicks;
    juce::Label roomTicksLabel;
    void setRoomSelected (int room, bool on);

    // One button per panel; a panel opens over the analyzer, one at a time
    std::array<juce::TextButton, numCards> cardButtons;
    std::array<Card, numCards> cards { { Card ("Source & Format"), Card ("Spectrum"), Card ("Phase Alert"),
                                         Card ("Transients"), Card ("Playback chains"), Card ("Dev: algorithm settings") } };
    int openCard = -1;

    juce::Label formatLabel { {}, "Channel Format:" }, deviceLabel { {}, "Input Device:" },
                fpsLabel { {}, "Refresh Rate:" }, windowLabel { {}, "Time / Frequency:" },
                rangeLabel { {}, "Level range:" }, statusLabel;
    juce::ComboBox formatBox, deviceBox, fpsBox, windowBox, rangeBox, alertModeBox;
    std::array<juce::TextButton, 3> levelButtons;       // Soft, Medium, Strong
    juce::ToggleButton lfeGainToggle { "LFE +10 dB (theatre level)" }, animationToggle { "Background animation" },
                       peakHoldToggle { "Peak hold" }, tiltToggle { "Bass tilt (old display)" },
                       levelWeightToggle { "Weigh by level balance" };
    LabelledSlider inertia { "Inertia:", 0, 100, 1, 50 }, integration { "Integration:", 0.5, 5.0, 0.1, 2.0 },
                   decay { "Decay:", 0.1, 4.0, 0.05, 1.0 };
    juce::ToggleButton transientToggle { "Transient check on" };
    LabelledSlider tAttack { "Attack:", 0, 100, 1, 40 }, tHoldProfile { "Hold:", 0, 100, 1, 25 },
                   tRelease { "Release:", 0, 100, 1, 50 }, tSensitivity { "Sensitivity:", 0, 100, 1, 70 },
                   tSearch { "Search range (how far off the LFE may be):", 5, 500, 5, 250 },
                   tMatch { "Likeness needed for a phase reading:", 0, 1, 0.05, 0.3 },
                   tHold { "Display time:", 1, 20, 0.5, 4 };
    juce::TextButton transientDefaultsButton { "Defaults" };
    ChainsView chainsView;
    LabelledSlider scoreSpeed { "Score speed:", 0, 1, 0.05, 0 };

    // Dev panel: every choice of the score, adjustable
    LabelledSlider dTolSoft { "Tolerance, Soft:", 0.5, 4.5, 0.1, 3 }, dTolMedium { "Tolerance, Medium:", 0.5, 4.5, 0.1, 2 },
                   dTolStrong { "Tolerance, Strong:", 0.5, 4.5, 0.1, 1 },
                   dLate { "Weight, cinema sub late:", 0, 1, 0.05, 0.5 }, dEarly { "Weight, cinema sub early:", 0, 1, 0.05, 0.5 },
                   dCeiling { "LFE dependence, ceiling:", 0, 1, 0.01, 1.0 }, dInfraWeight { "Infra-bass weight:", 0, 1, 0.05, 0.30 },
                   dInfraLo { "Infra-bass up to:", 10, 40, 1, 25 }, dInfraHi { "Full weight from:", 40, 100, 1, 40 },
                   dLevelWeight { "LFE level dependence, weight in score:", 0, 0.5, 0.01, 0.20 },
                   dLevelRed { "LFE level dependence, red at a mean of:", 0.5, 12, 0.1, 3 };
    juce::Label devLoudnessLabel;       // what the listening level makes of the bass playing now
    juce::TextButton devDefaultsButton { "Defaults" };
    std::vector<LabelledSlider*> devSliders();
    void applyDevSettings();
    juce::TextButton resetButton { "Reset" };

    // Listening level, left of Reset: always shown, a click opens the choice
    juce::TextButton listeningButton;
    double listeningDbC = 85.0;
    void setListeningLevel (double dbC);
    void showListeningMenu();
    double slowScore = -1.0;        // the score after the extra "Score speed" smoothing; negative: not started
    void updateSpeakers();

    double lastTick = 0.0;
    double historyClock = 0.0;
    int displayedScore = -1;

    // Display inertia of every number and curve shown
    std::array<Inertia, 6> readoutInertia;              // score, interaction, dependence, LFE balance, LFE share, level dependence
    std::vector<Inertia> chainInertia;                  // two per playback system: dB and penalty
    std::array<std::vector<Inertia>, 3> curveInertia;   // correlation curves
    HistoryStrip::Sample historySample;
    std::array<double, 3> transientFlash { { 0.0, 0.0, 0.0 } };

    // How hard each screen channel plays, for the cones: this frame's peak
    // against the loudest of the last few seconds, so that it shows at any level
    std::array<double, 3> framePeak { { 0.0, 0.0, 0.0 } }, recentPeak { { 0.0, 0.0, 0.0 } }, coneDrive { { 0.0, 0.0, 0.0 } };
    double bassFramePeak = 0.0, bassRecentPeak = 0.0, bassDrive = 0.0;
    double waveClock = 0.0, waveMin = 0.0, waveMax = 0.0, waveSumSquares = 0.0;
    long long waveCount = 0;
    double anyFramePeak = 0.0;      // loudest sample of the frame over the four channels, for the waveform of the history
    int framesShown = 0;
    const int snapshotFrames = juce::jmax (60, juce::SystemStats::getEnvironmentVariable ("SUBGUARD_SNAPSHOT_FRAMES", "240").getIntValue());
    bool restoring = false;
};

} // namespace subguard
