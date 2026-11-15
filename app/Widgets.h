#pragma once

#include <deque>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../core/AlertTracker.h"
#include "../core/LfeEngine.h"
#include "../core/PlaybackSim.h"

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
    const juce::Colour curveR     { 0xfff48fb1 };
}

juce::Font uiFont (float height, bool bold = false);

//==============================================================================
/** Clickable speaker: shows the alert level of one screen channel, click to mute. */
class SpeakerIndicator : public juce::Component
{
public:
    explicit SpeakerIndicator (juce::String labelText);

    void setLevel (double newLevel);

    /** 0..1, how hard the channel is playing right now: the cone moves with it. */
    void setDrive (double newDrive);
    bool isActive() const   { return active; }
    void setActive (bool shouldBeActive);

    std::function<void()> onToggle;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String label;
    double level = 0.0, drive = 0.0;
    bool active = true;
};

//==============================================================================
/** Top banner: animated background, logo and the three speaker indicators. */
class HeaderBar : public juce::Component
{
public:
    HeaderBar();

    void setLevel (double target);
    void setAnimationEnabled (bool);

    /** Moves the background on; call once per frame. */
    void advance (double seconds);
    void setLogo (const juce::Image& image)   { logo = image; }

    SpeakerIndicator left { "L" }, centre { "C" }, right { "R" };

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Image logo;
    double level = 0.0, phase = 0.0;
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
};

/** Table of the simulated playback chains plus the related read-outs. */
class ChainsView : public juce::Component
{
public:
    static constexpr double polarityWarning = -0.3;       // whole-session correlation below this
    static constexpr double polarityMinSeconds = 30.0;

    void update (const ChainsInfo&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    static juce::Colour colourFor (double penalty);

    /** Which subwoofer level the table shows: 0..6 for -3..+3 dB, 7 for the worst of them in each cell. */
    void setGainView (int index)    { gainView = juce::jlimit (0, ChainResult::numGains, index); repaint(); }

private:
    juce::Rectangle<float> gainButton (int index) const;

    ChainsInfo info;
    int gainView = ChainResult::numGains;
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
    };

    static constexpr double secondsPerColumn = 0.5;
    static constexpr int columns = 300;                 // two and a half minutes

    void push (const Sample&);
    void paint (juce::Graphics&) override;

private:
    std::vector<Sample> samples;
    size_t writePos = 0;
};

} // namespace subguard
