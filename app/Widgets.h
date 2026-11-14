#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../core/AlertTracker.h"
#include "../core/LfeEngine.h"

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
    bool isActive() const   { return active; }
    void setActive (bool shouldBeActive);

    std::function<void()> onToggle;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String label;
    double level = 0.0;
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
    void setLogo (const juce::Image& image)   { logo = image; }

    SpeakerIndicator left { "L" }, centre { "C" }, right { "R" };

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Image logo;
    double level = 0.0;
    bool animated = true;
};

//==============================================================================
/** Shared log-frequency axis (20 - 150 Hz) of the two plots. */
struct FrequencyAxis
{
    static constexpr double lo = 20.0, hi = 150.0;
    static constexpr int leftMargin = 62, rightMargin = 22;

    static float xFor (double hz, juce::Rectangle<float> area)
    {
        return area.getX() + area.getWidth() * (float) (std::log (hz / lo) / std::log (hi / lo));
    }

    static void drawVerticalGrid (juce::Graphics&, juce::Rectangle<float> area);
};

/** Level spectrum of the L+C+R sum and the LFE, with the alert heat map behind. */
class SpectrumPlot : public juce::Component
{
public:
    void update (const Analysis&, const AlertTracker&);
    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<float> plotArea() const;

    std::vector<double> freqs, facadeDb, lfeDb, grid, heatLevel, heatSign;
};

/** Last attack found in each screen channel and how the LFE lines up with it. */
class TransientStrip : public juce::Component
{
public:
    static constexpr double alignedToleranceMs = 2.5;   // quarter period at 100 Hz
    static constexpr double unmeasurableMs = 8.0;       // precision too poor to say anything about timing
    static constexpr double holdSeconds = 4.0, fadeSeconds = 1.5;

    void show (const TransientEvent&, double severity);
    void advance (double seconds);
    void paint (juce::Graphics&) override;

private:
    struct Cell
    {
        TransientEvent event;
        double severity = 0.0, age = 1e9;
    };

    std::array<Cell, 3> cells;
};

/** Signed LFE / screen-channel interaction per frequency, -1 .. +1. */
class CorrelationPlot : public juce::Component
{
public:
    void update (const Analysis&, const AlertTracker&, double thresholdToShow);
    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<float> plotArea() const;

    std::vector<double> grid;
    std::array<std::vector<double>, 3> curves;
    double threshold = 0.33;
};

} // namespace subguard
