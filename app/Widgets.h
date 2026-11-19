#pragma once

#include <deque>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Lang.h"
#include "../core/AlertTracker.h"
#include "../core/LfeEngine.h"
#include "../core/PlaybackSim.h"
#include "../core/RoomSim.h"

namespace subguard
{

namespace theme
{
    const juce::Colour window     { 0xff1e1e1e };
    const juce::Colour plot       { 0xff121212 };
    const juce::Colour text       { 0xffe6e6e6 };
    const juce::Colour dimText    { 0xffaaaaaa };
    const juce::Colour outline    { 0xff444444 };
    const juce::Colour grid       { 0x4dffffff };
    const juce::Colour facade     { 0xff0077ff };
    const juce::Colour lfe        { 0xff00ff00 };
    const juce::Colour accent     { 0xffff5500 };
    const juce::Colour curveL     { 0xff4fc3f7 };
    const juce::Colour curveC     { 0xffe6e6e6 };
    const juce::Colour curveR     { 0xffd90f1e };     // blood red
}

juce::Font uiFont (float height, bool bold = false);

//==============================================================================
/** Clickable speaker: shows the alert level of one screen channel, click to mute. */
class SpeakerIndicator : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    explicit SpeakerIndicator (juce::String labelText);

    /** 0..1: how strongly the alert lamp under the speaker is lit. */
    void setLevel (double newLevel);

    /** 0..1, how hard the channel is playing right now: the cone moves with it. */
    void setDrive (double newDrive);

    /** Click: solo. Option-click: mute. */
    bool isSolo() const     { return solo; }
    bool isMuted() const    { return muted; }

    /** Left out of the analysis (muted, or another speaker is soloed): drawn dimmed. */
    void setDimmed (bool shouldBeDimmed);

    /** Off: the cabinet and cone stay still, whatever plays. */
    void setMoving (bool shouldMove);

    std::function<void()> onToggle;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String label;
    double level = 0.0, drive = 0.0;
    bool solo = false, muted = false, dimmed = false, moving = true;
};

//==============================================================================
/** Top banner: animated background, logo and the three speaker indicators. */
class HeaderBar : public juce::Component
{
public:
    HeaderBar();

    void setLevel (double target);

    /** 0..1, how hard the bass (under 200 Hz) hits right now: the background pulses with it. */
    void setBass (double drive)     { bass = juce::jlimit (0.0, 1.0, drive); }
    void setAnimationEnabled (bool);

    /** Moves the background on; call once per frame. */
    void advance (double seconds);
    void setLogo (const juce::Image& image)   { logo = image; }

    SpeakerIndicator left { "L" }, centre { "C" }, right { "R" };

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Image logo;
    double level = 0.0, phase = 0.0, bass = 0.0;
    bool animated = true;
};

//==============================================================================
/** Shared log-frequency axis (5 - 150 Hz) of the two plots. */
struct FrequencyAxis
{
    static constexpr double lo = 5.0, hi = 150.0;
    static constexpr int leftMargin = 62, rightMargin = 22;

    static float xFor (double hz, juce::Rectangle<float> area)
    {
        return area.getX() + area.getWidth() * (float) (std::log (hz / lo) / std::log (hi / lo));
    }

    static double hzFor (float x, juce::Rectangle<float> area)
    {
        return lo * std::pow (hi / lo, (double) ((x - area.getX()) / area.getWidth()));
    }

    static void drawVerticalGrid (juce::Graphics&, juce::Rectangle<float> area);
    static void drawLabels (juce::Graphics&, juce::Rectangle<float> area);
};

/** Level spectrum of the L+C+R sum and the LFE, with the alert heat map behind. */
class SpectrumPlot : public juce::Component
{
public:
    void update (const Analysis&, const AlertTracker&, double seconds);
    void setFloorDb (double);
    void setPeakHold (bool);
    void setTilt (bool);

    /** Robustness score drawn large behind the curves; negative: nothing to score. */
    void setScore (double score);

    /** LFE / screen overlap per band (30-60, 60-90, 90-120 Hz), 0..1: thin marks at the bottom. */
    void setOverlap (const std::array<double, 3>& amounts);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plotArea() const;

    std::vector<double> freqs, facadeDb, lfeDb, peakFacade, peakLfe, grid, heatLevel, heatSign;
    double floorDb = -96.0, score = -1.0;
    std::array<double, 3> overlap { { 0.0, 0.0, 0.0 } };
    bool peakHold = true, tilt = false;
    float hoverX = -1.0f;
};

/** Last attack found in each screen channel and how the LFE lines up with it. */
class TransientStrip : public juce::Component
{
public:
    static constexpr double alignedToleranceMs = 2.5;   // quarter period at 100 Hz
    static constexpr double attackToleranceMs = 10.0;   // what timing attack against attack can tell apart
    static constexpr double unmeasurableMs = 8.0;       // precision too poor to say anything about timing
    static constexpr double fadeSeconds = 1.5;

    /** Every attack found in the mix goes through here. */
    void show (const TransientEvent&);
    void advance (double seconds);
    void paint (juce::Graphics&) override;

    /** Likeness from which the correlation reading (fine delay, polarity) is trusted. */
    void setMinMatch (double m)         { minMatch = m; repaint(); }
    void setHoldSeconds (double s)      { holdSeconds = s; }

    /** Off: the strip says so and shows nothing else. */
    void setActive (bool on)            { if (active != on) { active = on; age = 1e9; repaint(); } }

    /** True when the event shows a fault worth lighting the speakers for. */
    bool isFault (const TransientEvent&) const;

private:
    bool fine (const TransientEvent&) const;

    TransientEvent event;
    double age = 1e9, minMatch = 0.3, holdSeconds = 4.0;
    bool active = false;
};

/** Signed LFE / screen-channel interaction per frequency, -1 .. +1. */
class CorrelationPlot : public juce::Component
{
public:
    void update (const Analysis&, const AlertTracker&, double thresholdToShow, bool outOfPhaseOnly);
    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<float> plotArea() const;

    std::vector<double> grid;
    std::array<std::vector<double>, 3> curves;
    double threshold = 0.5;
    bool negativeOnly = false;
};

//==============================================================================
/** A panel that opens over the analyzer when its button is pressed. */
class Card : public juce::Component
{
public:
    explicit Card (juce::String titleText) : title (std::move (titleText)) {}

    /** Lays out the card's children inside the given content area. */
    std::function<void (juce::Rectangle<int>)> onLayout;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {}    // clicks do not fall through to the plot

private:
    juce::String title;
};

/** Everything the playback-chains card shows. */
struct ChainsInfo
{
    Translation translation;
    std::array<double, 3> sessionCorrelation { { 0.0, 0.0, 0.0 } };
    double sessionSeconds = 0.0;
    bool otherSystemTicked = false;     // an other system counts in the score (Room Sim)
};

/** The colour that marks what is not cinema (the other systems). */
const juce::Colour otherSystemColour { 0xffc89bff };

/** Table of the simulated playback chains plus the related read-outs. */
class ChainsView : public juce::Component
{
public:
    static constexpr double polarityWarning = -0.3;       // whole-session correlation below this
    static constexpr double polarityMinSeconds = 30.0;

    void update (const ChainsInfo&);
    void paint (juce::Graphics&) override;

    static juce::Colour colourFor (double penalty);

private:
    ChainsInfo info;
};

/** Room Sim: the fifteen rooms, seat by seat. */
class RoomSimView : public juce::Component
{
public:
    void update (const RoomSimResult&, double robustness, bool hasScore);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Which rooms count in the score; a click on a row toggles it. */
    void setSelection (const std::vector<bool>& on)     { selected = on; repaint(); }
    std::function<void (int room)> onToggle;

private:
    RoomSimResult result;
    std::vector<bool> selected;
    std::vector<juce::Rectangle<float>> rowAreas;
    std::vector<double> shownRoom;      // room scores, smoothed for display
    double robustness = 100.0;
    bool hasScore = false;
};

/** Scrolling record of the last minutes: one thin lane per indicator. */
class HistoryStrip : public juce::Component
{
public:
    struct Sample
    {
        float alert = 0.0f;         // 0..1, strongest alert
        bool outOfPhase = false;    // its sign
        float score = 1.0f;         // 0..1, lowest robustness over the column
        bool scored = false;        // false: nothing to score (silence)
        float dependence = 0.0f;    // 0..1, LFE share of the bass
        float level = 0.0f;         // 0..1, loudest peak over the column (-60 .. 0 dBFS)
    };

    static constexpr double secondsPerColumn = 0.5;
    static constexpr int columns = 300;                 // two and a half minutes

    // Waveform behind the lanes, as in a DAW: signed peaks and RMS, four points per column
    static constexpr int wavePointsPerColumn = 4;
    static constexpr double secondsPerWavePoint = secondsPerColumn / wavePointsPerColumn;
    struct WavePoint { float lo = 0.0f, hi = 0.0f, rms = 0.0f; };

    // The time scale is compressed into the past: the last seconds take most
    // of the width, older ones settle towards the left (logarithmic, scale 15 s).
    static constexpr double timeScaleSeconds = 15.0;

    void push (const Sample&);
    void pushWave (const WavePoint&);

    /** Time since the last column and the last waveform point, so the strip scrolls smoothly. */
    void setClock (double sinceColumn, double sinceWavePoint, const Sample& current, const WavePoint& currentWave);
    void clear();
    void paint (juce::Graphics&) override;

private:
    std::vector<Sample> samples;
    size_t writePos = 0;
    std::vector<WavePoint> wave;
    size_t waveWritePos = 0, waveFilled = 0;
    double columnPhase = 0.0, wavePhase = 0.0;
    Sample inProgress;              // the column being gathered, drawn from now back to columnPhase
    WavePoint waveInProgress;

    static float xForAge (double ageSeconds, juce::Rectangle<float> area);
};

} // namespace subguard
