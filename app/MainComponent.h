#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "AnalysisThread.h"
#include "AudioInput.h"
#include "ChannelFormats.h"
#include "Widgets.h"
#include "RoomProView.h"
#include "TestView.h"
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

    /** Where the default value comes from: a published source, or a hypothesis (shown as a tag by the caption). */
    void setEvidence (bool sourced, const juce::String& why);

    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::Label caption, valueLabel, evidence;
    juce::Slider slider;
    double highlightFrom = 0.0, highlightTo = 0.0;
};

struct LaunchOptions
{
    bool demoSignal = false;        // --demo: built-in test signal instead of an audio device
    juce::File snapshotFile;        // --snapshot <png>: save the window after 4 s and quit
    juce::String openCard;          // --card <name>: start with that panel open (for snapshots)
};

/** Dev panel: the bass playing now at the listening level, band by band, as a small table. */
class LoudnessTable : public juce::Component
{
public:
    void set (const Translation& t);
    void paint (juce::Graphics&) override;

private:
    bool hasBass = false;
    std::array<double, 4> spl {}, phon {}, doubling {};
};

/** A small flag at the top left: the interface language. A click switches between French and English. */
class LanguageFlag : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void()> onClick;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override     { if (contains (e.getPosition()) && onClick) onClick(); }
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
    bool keyPressed (const juce::KeyPress&) override;

    /** Set by the window: switch the language and rebuild the content in it. */
    std::function<void()> onLanguageToggle;

    /** Room Sim Pro, for the File and Edit menus of the window. */
    RoomProView& roomPro()                      { return proView; }

private:
    LanguageFlag languageFlag;
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

    /** The built-in test signals as the input (the "SubGUARD test signals" device), or back to the audio devices. */
    void useTestSource (bool on);
    static constexpr int testDeviceId = 10000;      // its entry in the input device list
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
    juce::TextButton roomButton { tr ("Room Sim") };
    bool roomMode = false;
    RoomProView proView;
    juce::TextButton proButton { tr ("Room Sim Pro") };
    bool proMode = false;
    TestView testView;
    juce::TextButton testButton { tr ("Test") };
    bool testMode = false;
    void showMode (int mode);       // 0 the analyzer, 1 Room Sim, 2 Room Sim Pro, 3 Test

    // Rooms that count in the score: ticked in Playback chains or in Room Sim
    std::vector<bool> roomSelected = RoomSimulator::defaultSelection();
    std::vector<std::unique_ptr<juce::ToggleButton>> roomTicks;
    juce::Label roomTicksLabel;
    void setRoomSelected (int room, bool on);

    // One button per panel; a panel opens over the analyzer, one at a time
    std::array<juce::TextButton, numCards> cardButtons;
    std::array<Card, numCards> cards { { Card (tr ("Source & Format")), Card (tr ("Spectrum")), Card (tr ("Phase Alert")),
                                         Card (tr ("Transients")), Card (tr ("Playback chains")), Card (tr ("Dev: algorithm settings")) } };
    int openCard = -1;

    juce::Label formatLabel { {}, tr ("Channel Format:") }, deviceLabel { {}, tr ("Input Device:") },
                fpsLabel { {}, tr ("Refresh Rate:") }, windowLabel { {}, tr ("Time / Frequency:") },
                rangeLabel { {}, tr ("Level range:") }, statusLabel;
    juce::ComboBox formatBox, deviceBox, fpsBox, windowBox, rangeBox, alertModeBox;
    std::array<juce::TextButton, 3> levelButtons;       // Soft, Medium, Strong
    juce::ToggleButton lfeGainToggle { tr ("LFE +10 dB (theatre level)") },
                       peakHoldToggle { tr ("Peak hold") }, tiltToggle { tr ("Bass tilt (old display)") },
                       levelWeightToggle { tr ("Weigh by level balance") };
    LabelledSlider inertia { tr ("Inertia:"), 0, 100, 1, 50 }, integration { tr ("Integration:"), 0.5, 5.0, 0.1, 2.0 },
                   decay { tr ("Decay:"), 0.1, 4.0, 0.05, 1.0 };
    juce::ToggleButton transientToggle { tr ("Transient check on") };
    LabelledSlider tAttack { tr ("Attack:"), 0, 100, 1, 40 }, tHoldProfile { tr ("Hold:"), 0, 100, 1, 25 },
                   tRelease { tr ("Release:"), 0, 100, 1, 50 }, tSensitivity { tr ("Sensitivity:"), 0, 100, 1, 70 },
                   tSearch { tr ("Search range (how far off the LFE may be):"), 5, 500, 5, 250 },
                   tMatch { tr ("Likeness needed for a phase reading:"), 0, 1, 0.05, 0.3 },
                   tHold { tr ("Display time:"), 1, 20, 0.5, 4 };
    juce::TextButton transientDefaultsButton { tr ("Defaults") };
    ChainsView chainsView;
    LabelledSlider scoreSpeed { tr ("Score speed:"), 0, 1, 0.05, 0 };

    // Dev panel: every choice of the score, adjustable
    // (grouped under section titles, so the captions stay short)
    LabelledSlider dTolSoft { tr ("Soft:"), 0.5, 4.5, 0.1, 3 }, dTolMedium { tr ("Medium:"), 0.5, 4.5, 0.1, 2 },
                   dTolStrong { tr ("Strong:"), 0.5, 4.5, 0.1, 1 },
                   dLate { tr ("Weight when late:"), 0, 1, 0.05, 0.5 }, dEarly { tr ("Weight when early:"), 0, 1, 0.05, 0.5 },
                   dCeiling { tr ("Ceiling:"), 0, 1, 0.01, 1.0 }, dInfraWeight { tr ("Weight:"), 0, 1, 0.05, 0.30 },
                   dInfraLo { tr ("Up to:"), 10, 40, 1, 25 }, dInfraHi { tr ("Full weight from:"), 40, 100, 1, 40 },
                   dLevelWeight { tr ("Level dependence, weight:"), 0, 0.5, 0.01, 0.20 },
                   dLevelRed { tr ("Level dependence, red at:"), 0.5, 12, 0.1, 3 },
                   dWorstWeight { tr ("Worst seats, weight:"), 0, 1, 0.01, 0.25 },
                   dWorstShare { tr ("Worst seats, share:"), 0.02, 0.5, 0.01, 0.10 },
                   dCoverage { tr ("Coverage (Pro), weight:"), 0, 1, 0.05, 0.5 },
                   dDips { tr ("Dips against peaks, allowance:"), 0, 1, 0.05, 1.0 };
    std::array<juce::Label, 5> devSections;
    LoudnessTable devLoudness;          // what the listening level makes of the bass playing now
    juce::TextButton devDefaultsButton { tr ("Defaults") };
    std::vector<LabelledSlider*> devSliders();
    void applyDevSettings();
    juce::TextButton resetButton { tr ("Reset") };

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
