#include "Widgets.h"

namespace subguard
{

juce::Font uiFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

namespace
{
    constexpr double lfeCutHz = 120.0;
    constexpr float topDb = 6.0f, bottomDb = -90.0f;

    struct RampStop { double pos; juce::uint8 r, g, b, a; };

    // Amber: LFE in phase with the screen channel. Red: out of phase (cancelling now).
    const RampStop inPhaseRamp[] = {
        { 0.00,   0,   0,  0,   0 }, { 0.10,  60,  45,  0,  30 }, { 0.25, 140, 100,  0,  90 },
        { 0.45, 215, 160,  0, 165 }, { 0.65, 250, 195,  0, 215 }, { 1.00, 255, 225, 30, 245 } };

    const RampStop outOfPhaseRamp[] = {
        { 0.00,   0,  0,  0,   0 }, { 0.10,  60, 15,  0,  30 }, { 0.25, 140, 35,  0,  90 },
        { 0.45, 220, 60,  0, 170 }, { 0.65, 255, 50,  0, 230 }, { 1.00, 255, 20, 40, 255 } };

    juce::Colour rampColour (const RampStop* ramp, double v)
    {
        v = juce::jlimit (0.0, 1.0, v);

        for (int i = 0; i < 5; ++i)
        {
            const auto& a = ramp[i];
            const auto& b = ramp[i + 1];

            if (v <= b.pos)
            {
                const double t = (v - a.pos) / (b.pos - a.pos);
                const auto mix = [t] (juce::uint8 x, juce::uint8 y) { return (juce::uint8) std::lround (x + t * (y - x)); };
                return juce::Colour (mix (a.r, b.r), mix (a.g, b.g), mix (a.b, b.b), mix (a.a, b.a));
            }
        }

        return juce::Colours::transparentBlack;
    }

    // Display weighting of the original app: tilt below 150 Hz plus a bump at 80 Hz
    double displayWeightDb (double hz)
    {
        const double pos = juce::jlimit (0.0, 1.0, std::log (hz / FrequencyAxis::lo)
                                                     / std::log (FrequencyAxis::hi / FrequencyAxis::lo));
        return -12.0 * (1.0 - pos) + 3.0 * std::exp (-0.5 * std::pow ((hz - 80.0) / 25.0, 2.0));
    }

    const int frequencyTicks[] = { 20, 30, 40, 50, 60, 80, 100, 120, 150 };
}

//==============================================================================
SpeakerIndicator::SpeakerIndicator (juce::String labelText)
    : label (std::move (labelText))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SpeakerIndicator::setLevel (double newLevel)
{
    newLevel = juce::jlimit (0.0, 1.0, newLevel);

    if (std::abs (newLevel - level) > 1e-4)
    {
        level = newLevel;
        repaint();
    }
}

void SpeakerIndicator::setActive (bool shouldBeActive)
{
    active = shouldBeActive;
    repaint();
}

void SpeakerIndicator::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isLeftButtonDown())
    {
        setActive (! active);

        if (onToggle)
            onToggle();
    }
}

void SpeakerIndicator::paint (juce::Graphics& g)
{
    g.setOpacity (1.0f);
    g.beginTransparencyLayer (active ? 1.0f : 0.4f);

    const float wAvail = (float) getWidth(), hAvail = (float) getHeight();
    const float aspect = 0.65f;
    float drawW, drawH;

    if (wAvail / hAvail > aspect)   { drawH = hAvail * 0.95f; drawW = drawH * aspect; }
    else                            { drawW = wAvail * 0.95f; drawH = drawW / aspect; }

    const juce::Rectangle<float> r ((wAvail - drawW) / 2.0f, (hAvail - drawH) / 2.0f, drawW, drawH);

    const float cabH = r.getHeight() * 0.60f, cabW = cabH * 0.55f;
    const float cx = r.getCentreX();
    const float cabX = cx - cabW / 2.0f, cabY = r.getY() + r.getHeight() * 0.05f;
    const double shown = active ? level : 0.0;

    const juce::Colour edge (200, 200, 200);

    // Cabinet
    const juce::Rectangle<float> cabinet (cabX, cabY, cabW, cabH);
    g.setColour (juce::Colour (20, 20, 20));
    g.fillRoundedRectangle (cabinet, drawW * 0.08f);
    g.setColour (edge);
    g.drawRoundedRectangle (cabinet, drawW * 0.08f, juce::jmax (1.5f, drawW * 0.04f));

    // Tweeter and woofer
    const auto driver = [&] (float centreY, float radius, juce::Colour fill, float stroke)
    {
        const juce::Rectangle<float> c (cx - radius, centreY - radius, 2.0f * radius, 2.0f * radius);
        g.setColour (fill);
        g.fillEllipse (c);
        g.setColour (edge);
        g.drawEllipse (c, stroke);
    };

    driver (cabY + cabH * 0.25f, cabW * 0.18f, juce::Colour (10, 10, 10), juce::jmax (1.0f, drawW * 0.02f));
    driver (cabY + cabH * 0.68f, cabW * 0.30f, juce::Colour (18, 18, 18), juce::jmax (1.2f, drawW * 0.03f));

    // Alert arc under the cabinet
    const float arcW = cabW * 2.5f, arcH = r.getHeight() * 0.25f;
    const juce::Rectangle<float> arcRect (cx - arcW / 2.0f, cabY + cabH + r.getHeight() * 0.02f, arcW, arcH);

    juce::Path arc;
    arc.addArc (arcRect.getX(), arcRect.getY(), arcRect.getWidth(), arcRect.getHeight(),
                juce::MathConstants<float>::pi * 2.0f / 3.0f, juce::MathConstants<float>::pi * 4.0f / 3.0f, true);

    const float baseThick = juce::jmax (2.0f, r.getHeight() * 0.025f);
    const float thick = baseThick + r.getHeight() * 0.09f * (float) shown;
    const auto rounded = [] (float t) { return juce::PathStrokeType (t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };

    g.setColour (juce::Colour (120, 120, 120).withAlpha (0.31f));
    g.strokePath (arc, rounded (baseThick));

    if (shown > 0.01)
    {
        const auto alert = juce::Colour ((juce::uint8) 255, (juce::uint8) std::lround (255.0 * (1.0 - shown)), (juce::uint8) 0,
                                         (juce::uint8) std::lround (60.0 + 195.0 * shown));
        g.setColour (alert.withMultipliedAlpha (0.5f));
        g.strokePath (arc, rounded (thick * 1.9f));
        g.setColour (alert);
        g.strokePath (arc, rounded (thick));
    }

    // Label
    g.setColour (juce::Colour (235, 235, 235));
    g.setFont (uiFont (juce::jmax (11.0f, cabW * 0.6f), true));
    const float labelTop = cabY + cabH - r.getHeight() * 0.02f;
    g.drawText (label, juce::Rectangle<float> (cx - arcW / 2.0f, labelTop, arcW, r.getHeight() * 0.32f),
                juce::Justification::centred, false);

    g.endTransparencyLayer();
}

//==============================================================================
HeaderBar::HeaderBar()
{
    addAndMakeVisible (left);
    addAndMakeVisible (centre);
    addAndMakeVisible (right);
}

void HeaderBar::setAnimationEnabled (bool shouldAnimate)
{
    animated = shouldAnimate;

    if (! animated)
    {
        level = 0.0;
        repaint();
    }
}

void HeaderBar::setLevel (double target)
{
    if (! animated)
        return;

    const double previous = level;
    level += (target > level ? 0.2 : 0.05) * (target - level);

    if (std::abs (level - previous) > 5e-4)
        repaint();
}

void HeaderBar::resized()
{
    auto area = getLocalBounds().reduced (20);
    const int cell = area.getWidth() / 4;
    area.removeFromLeft (cell);     // logo
    left.setBounds (area.removeFromLeft (cell));
    centre.setBounds (area.removeFromLeft (cell));
    right.setBounds (area);
}

void HeaderBar::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth(), h = bounds.getHeight();

    g.setGradientFill (juce::ColourGradient (juce::Colour (10, 10, 10), 0.0f, 0.0f,
                                             juce::Colour (4, 4, 4), 0.0f, h, false));
    g.fillRect (bounds);

    // "Black hole": a wide radial glow that breathes with the input level
    const double t = animated ? level : 0.0;
    const auto mix = [t] (juce::Colour a, juce::Colour b) { return a.interpolatedWith (b, (float) t).withMultipliedAlpha (0.9f); };

    juce::ColourGradient glow (mix (juce::Colour (5, 12, 18), juce::Colour (10, 20, 25)), 0.0f, 0.0f,
                               juce::Colour (8, 8, 8).withAlpha (0.0f), 0.5f, 0.0f, true);
    glow.addColour (0.35, mix (juce::Colour (9, 34, 48), juce::Colour (15, 50, 55)));
    glow.addColour (0.60, mix (juce::Colour (15, 40, 45), juce::Colour (30, 70, 40)));
    glow.addColour (0.85, mix (juce::Colour (20, 25, 20), juce::Colour (60, 65, 20)));

    const float scale = 3.0f + (float) t * 0.15f;
    juce::FillType fill (glow);
    fill.transform = juce::AffineTransform::scale (w * scale, h * scale).translated (w * 0.5f, h * 0.75f);
    g.setFillType (fill);
    g.fillRect (bounds);

    // Logo, or the name when no logo image was built in
    const auto logoArea = getLocalBounds().reduced (20).removeFromLeft ((getWidth() - 40) / 4).toFloat();

    if (logo.isValid())
    {
        g.setOpacity (1.0f);
        g.drawImage (logo, logoArea, juce::RectanglePlacement::centred);
    }
    else
    {
        g.setColour (juce::Colour (235, 235, 235));
        g.setFont (uiFont (juce::jmax (18.0f, h * 0.2f), true));
        g.drawText ("SubGUARD", logoArea, juce::Justification::centred, false);
    }
}

//==============================================================================
void FrequencyAxis::drawVerticalGrid (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (theme::grid);

    for (int f : frequencyTicks)
        g.fillRect (xFor (f, area), area.getY(), 1.0f, area.getHeight());
}

//==============================================================================
juce::Rectangle<float> SpectrumPlot::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft ((float) FrequencyAxis::leftMargin)
                                     .withTrimmedRight ((float) FrequencyAxis::rightMargin).withTrimmedTop (30.0f);
}

void SpectrumPlot::update (const Analysis& a, const AlertTracker& alerts)
{
    freqs = a.freqs;
    facadeDb = a.facadeDb;
    lfeDb = a.lfeDb;
    grid = a.grid;
    heatLevel = alerts.heatLevel;
    heatSign = alerts.heatSign;
    repaint();
}

void SpectrumPlot::paint (juce::Graphics& g)
{
    g.fillAll (theme::plot);
    const auto area = plotArea();

    const auto yFor = [area] (double db)
    {
        return area.getY() + area.getHeight() * (topDb - (float) db) / (topDb - bottomDb);
    };

    g.setColour (theme::dimText);
    g.setFont (uiFont (15.0f));
    g.drawText ("SubGUARD Analyzer : 20Hz - 150Hz", getLocalBounds().removeFromTop (28), juce::Justification::centred);

    // Left axis
    g.setFont (uiFont (12.0f));

    for (int db : { 0, -18, -36, -54, -72 })
        g.drawText (juce::String (db), juce::Rectangle<float> (0.0f, yFor (db) - 8.0f, (float) FrequencyAxis::leftMargin - 8.0f, 16.0f),
                    juce::Justification::centredRight);

    {
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, 10.0f, area.getCentreY()));
        g.drawText ("Level (dBFS)", juce::Rectangle<float> (-60.0f, area.getCentreY() - 8.0f, 140.0f, 16.0f),
                    juce::Justification::centred);
    }

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (area.toNearestInt());

    // Heat map: one cell per grid point, behind everything else
    if (grid.size() > 1)
    {
        const double ratio = std::sqrt (grid[1] / grid[0]);

        for (size_t i = 0; i < grid.size(); ++i)
        {
            if (heatLevel[i] < 1e-4)
                continue;

            // Whole-pixel edges shared by neighbours: no seams, no double blending
            const int x0 = juce::roundToInt (FrequencyAxis::xFor (grid[i] / ratio, area));
            const int x1 = juce::roundToInt (FrequencyAxis::xFor (grid[i] * ratio, area));
            g.setColour (rampColour (heatSign[i] < 0.0 ? outOfPhaseRamp : inPhaseRamp, std::sqrt (heatLevel[i])));
            g.fillRect (x0, juce::roundToInt (area.getY()), x1 - x0, juce::roundToInt (area.getHeight()) + 1);
        }
    }

    // Grid
    FrequencyAxis::drawVerticalGrid (g, area);
    g.setColour (theme::grid);

    for (int db : { 0, -18, -36, -54, -72 })
        g.fillRect (area.getX(), yFor (db), area.getWidth(), 1.0f);

    const float xCut = FrequencyAxis::xFor (lfeCutHz, area);
    const float dashes[] = { 4.0f, 4.0f };
    g.setColour (juce::Colour (0xff666666));
    g.drawDashedLine ({ xCut, area.getY(), xCut, area.getBottom() }, dashes, 2, 1.0f);

    // Spectra
    const auto drawCurve = [&] (const std::vector<double>& db, juce::Colour colour, float fillAlpha, float thickness)
    {
        if (freqs.empty())
            return;

        juce::Path line;
        bool started = false;
        float lastX = area.getX(), firstX = area.getX();

        for (size_t i = 0; i < freqs.size(); ++i)
        {
            if (freqs[i] < 10.0 || freqs[i] > 165.0)
                continue;

            const float x = FrequencyAxis::xFor (freqs[i], area);
            const float y = yFor (juce::jlimit (-120.0, 10.0, db[i] + displayWeightDb (freqs[i])));

            if (! started)
            {
                line.startNewSubPath (x, y);
                firstX = x;
                started = true;
            }
            else
            {
                line.lineTo (x, y);
            }

            lastX = x;
        }

        juce::Path filled (line);
        filled.lineTo (lastX, area.getBottom() + 2.0f);
        filled.lineTo (firstX, area.getBottom() + 2.0f);
        filled.closeSubPath();

        g.setColour (colour.withAlpha (fillAlpha));
        g.fillPath (filled);
        g.setColour (colour);
        g.strokePath (line, juce::PathStrokeType (thickness, juce::PathStrokeType::curved));
    };

    drawCurve (facadeDb, theme::facade, 0.43f, 2.0f);
    drawCurve (lfeDb, theme::lfe, 0.39f, 1.5f);

    // Fade out above the LFE cut-off (whole-pixel strips, fully black near the edge)
    const int strips = 24;
    const int fadeStart = juce::roundToInt (xCut), fadeEnd = juce::roundToInt (area.getRight()) + 1;

    for (int i = 0; i < strips; ++i)
    {
        const int x0 = fadeStart + (fadeEnd - fadeStart) * i / strips;
        const int x1 = fadeStart + (fadeEnd - fadeStart) * (i + 1) / strips;
        const float t = (float) (i + 1) / (float) strips;
        g.setColour (juce::Colours::black.withAlpha (juce::jmin (1.0f, std::pow (t / 0.8f, 1.4f))));
        g.fillRect (x0, juce::roundToInt (area.getY()), x1 - x0, juce::roundToInt (area.getHeight()) + 1);
    }
}

//==============================================================================
juce::Rectangle<float> CorrelationPlot::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft ((float) FrequencyAxis::leftMargin)
                                     .withTrimmedRight ((float) FrequencyAxis::rightMargin).withTrimmedBottom (40.0f);
}

void CorrelationPlot::update (const Analysis& a, const AlertTracker& alerts, double thresholdToShow)
{
    grid = a.grid;
    curves = alerts.curves;
    threshold = thresholdToShow;
    repaint();
}

void CorrelationPlot::paint (juce::Graphics& g)
{
    g.fillAll (theme::plot);
    const auto area = plotArea();

    const auto yFor = [area] (double v)
    {
        return area.getCentreY() - (float) v * area.getHeight() / 2.4f;
    };

    // Axes
    g.setColour (theme::dimText);
    g.setFont (uiFont (12.0f));

    for (const auto& tick : { std::pair<double, const char*> { 1.0, "+1 in" }, { 0.0, "0" }, { -1.0, "-1 out" } })
        g.drawText (tick.second, juce::Rectangle<float> (0.0f, yFor (tick.first) - 8.0f, (float) FrequencyAxis::leftMargin - 8.0f, 16.0f),
                    juce::Justification::centredRight);

    for (int f : frequencyTicks)
        g.drawText (juce::String (f), juce::Rectangle<float> (FrequencyAxis::xFor (f, area) - 20.0f, area.getBottom() + 4.0f, 40.0f, 16.0f),
                    juce::Justification::centred);

    g.drawText ("Frequency (Hz)", juce::Rectangle<float> (area.getX(), area.getBottom() + 20.0f, area.getWidth(), 18.0f),
                juce::Justification::centred);

    g.setColour (theme::outline);
    g.fillRect (area.getX(), area.getY(), area.getWidth(), 1.0f);

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (area.toNearestInt());

    FrequencyAxis::drawVerticalGrid (g, area);
    g.setColour (theme::grid);
    g.fillRect (area.getX(), yFor (1.0), area.getWidth(), 1.0f);
    g.fillRect (area.getX(), yFor (-1.0), area.getWidth(), 1.0f);
    g.setColour (juce::Colour (0xff777777));
    g.fillRect (area.getX(), yFor (0.0), area.getWidth(), 1.0f);

    const float dashes[] = { 5.0f, 4.0f };
    g.setColour (theme::accent);

    for (double v : { threshold, -threshold })
        g.drawDashedLine ({ area.getX(), yFor (v), area.getRight(), yFor (v) }, dashes, 2, 1.0f);

    const juce::Colour colours[] = { theme::curveL, theme::curveC, theme::curveR };
    const char* names[] = { "L", "C", "R" };

    for (size_t c = 0; c < 3; ++c)
    {
        if (grid.empty())
            break;

        juce::Path line;

        for (size_t i = 0; i < grid.size(); ++i)
        {
            const float x = FrequencyAxis::xFor (grid[i], area), y = yFor (curves[c][i]);

            if (i == 0)
                line.startNewSubPath (x, y);
            else
                line.lineTo (x, y);
        }

        g.setColour (colours[c]);
        g.strokePath (line, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved));
    }

    // Legend
    auto legend = juce::Rectangle<float> (area.getRight() - 118.0f, area.getY() + 6.0f, 110.0f, 20.0f);
    g.setColour (theme::plot.withAlpha (0.8f));
    g.fillRoundedRectangle (legend, 4.0f);
    g.setFont (uiFont (12.0f, true));

    for (size_t c = 0; c < 3; ++c)
    {
        auto cell = legend.removeFromLeft (legend.getWidth() / (float) (3 - c));
        g.setColour (colours[c]);
        g.fillRect (cell.getX() + 6.0f, cell.getCentreY() - 1.0f, 12.0f, 2.0f);
        g.drawText (names[c], cell.withTrimmedLeft (22.0f), juce::Justification::centredLeft);
    }
}

} // namespace subguard
