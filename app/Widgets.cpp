#include "Widgets.h"

#include <limits>

namespace subguard
{

juce::Font uiFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

namespace
{
    constexpr double lfeCutHz = 120.0;
    constexpr float topDb = 6.0f;
    constexpr double peakFallDbPerSecond = 9.0;

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

    // Optional display tilt of the original app: -12 dB at 20 Hz rising to 0 dB
    // at 150 Hz, plus a 3 dB bump at 80 Hz. Off by default: levels are true dBFS.
    double displayWeightDb (double hz)
    {
        const double pos = juce::jlimit (0.0, 1.0, std::log (hz / 20.0) / std::log (150.0 / 20.0));
        return -12.0 * (1.0 - pos) + 3.0 * std::exp (-0.5 * std::pow ((hz - 80.0) / 25.0, 2.0));
    }

    const int frequencyTicks[] = { 5, 7, 10, 15, 20, 30, 40, 50, 60, 80, 100, 120, 150 };
    const int minorTicks[] = { 6, 8, 9, 12, 25, 35, 45, 70, 90, 110, 130, 140 };
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
    g.setColour (theme::grid.withMultipliedAlpha (0.35f));

    for (int f : minorTicks)
        g.fillRect (xFor (f, area), area.getY(), 1.0f, area.getHeight());

    g.setColour (theme::grid);

    for (int f : frequencyTicks)
        g.fillRect (xFor (f, area), area.getY(), 1.0f, area.getHeight());
}

void FrequencyAxis::drawLabels (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (theme::dimText);
    g.setFont (uiFont (12.0f));

    for (int f : frequencyTicks)
        g.drawText (juce::String (f), juce::Rectangle<float> (xFor (f, area) - 20.0f, area.getBottom() + 4.0f, 40.0f, 16.0f),
                    juce::Justification::centred);
}

//==============================================================================
juce::Rectangle<float> SpectrumPlot::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft ((float) FrequencyAxis::leftMargin)
                                     .withTrimmedRight ((float) FrequencyAxis::rightMargin).withTrimmedTop (30.0f);
}

void SpectrumPlot::update (const Analysis& a, const AlertTracker& alerts, double seconds)
{
    if (peakFacade.size() != a.freqs.size())
    {
        peakFacade.assign (a.freqs.size(), -200.0);
        peakLfe.assign (a.freqs.size(), -200.0);
    }

    freqs = a.freqs;
    facadeDb = a.facadeDb;
    lfeDb = a.lfeDb;
    grid = a.grid;
    heatLevel = alerts.heatLevel;
    heatSign = alerts.heatSign;

    const double fall = peakFallDbPerSecond * seconds;

    for (size_t i = 0; i < freqs.size(); ++i)
    {
        peakFacade[i] = std::max (facadeDb[i], peakFacade[i] - fall);
        peakLfe[i] = std::max (lfeDb[i], peakLfe[i] - fall);
    }

    repaint();
}

void SpectrumPlot::setFloorDb (double db)      { floorDb = db; repaint(); }
void SpectrumPlot::setPeakHold (bool on)       { peakHold = on; repaint(); }
void SpectrumPlot::setTilt (bool on)           { tilt = on; repaint(); }
void SpectrumPlot::setScore (double value)     { score = value; }
void SpectrumPlot::setOverlap (const std::array<double, 3>& amounts)   { overlap = amounts; }

void SpectrumPlot::mouseMove (const juce::MouseEvent& e)
{
    hoverX = plotArea().contains (e.position) ? e.position.x : -1.0f;
    repaint();
}

void SpectrumPlot::mouseExit (const juce::MouseEvent&)
{
    hoverX = -1.0f;
    repaint();
}

void SpectrumPlot::paint (juce::Graphics& g)
{
    g.fillAll (theme::plot);
    const auto area = plotArea();
    const auto bottomDb = (float) floorDb;

    const auto yFor = [area, bottomDb] (double db)
    {
        return area.getY() + area.getHeight() * (topDb - (float) db) / (topDb - bottomDb);
    };

    const auto shown = [this] (double db, double hz)
    {
        return db + (tilt ? displayWeightDb (hz) : 0.0);
    };

    g.setColour (theme::dimText);
    g.setFont (uiFont (15.0f));
    g.drawText ("SubGUARD Analyzer : 5Hz - 150Hz", getLocalBounds().removeFromTop (28), juce::Justification::centred);

    // Left axis: a line every 12 dB
    g.setFont (uiFont (12.0f));

    for (int db = 0; db > (int) bottomDb; db -= 12)
        g.drawText (juce::String (db), juce::Rectangle<float> (0.0f, yFor (db) - 8.0f, (float) FrequencyAxis::leftMargin - 8.0f, 16.0f),
                    juce::Justification::centredRight);

    {
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, 10.0f, area.getCentreY()));
        g.drawText (tilt ? "Level (dB, tilted)" : "Level (dBFS)", juce::Rectangle<float> (-60.0f, area.getCentreY() - 8.0f, 140.0f, 16.0f),
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

    // Robustness score: large, behind the curves, always there
    {
        const float size = juce::jlimit (60.0f, 220.0f, area.getHeight() * 0.55f);
        const auto colour = score < 0.0 ? juce::Colour (0xff808080)
                                        : juce::Colour (0xffff3a2a).interpolatedWith (juce::Colour (0xff3ddc84), (float) (score / 100.0));
        auto box = area.withSizeKeepingCentre (size * 2.4f, size * 1.25f);

        g.setColour (colour.withAlpha (0.20f));
        g.setFont (uiFont (size, true));
        g.drawText (score < 0.0 ? juce::String ("--") : juce::String (juce::roundToInt (score)),
                    box.removeFromTop (size), juce::Justification::centred, false);
        g.setColour (colour.withAlpha (0.32f));
        g.setFont (uiFont (juce::jmax (11.0f, size * 0.11f), true));
        g.drawText ("ROBUSTNESS", box, juce::Justification::centredTop, false);
    }

    // Grid
    FrequencyAxis::drawVerticalGrid (g, area);

    for (int db = 0; db > (int) bottomDb; db -= 6)
    {
        g.setColour (theme::grid.withMultipliedAlpha (db % 12 == 0 ? 1.0f : 0.35f));
        g.fillRect (area.getX(), yFor (db), area.getWidth(), 1.0f);
    }

    const float xCut = FrequencyAxis::xFor (lfeCutHz, area);
    const float dashes[] = { 4.0f, 4.0f };
    g.setColour (juce::Colour (0xff666666));
    g.drawDashedLine ({ xCut, area.getY(), xCut, area.getBottom() }, dashes, 2, 1.0f);

    // One point per pixel column, keeping the highest bin: no peak is lost
    // where several bins share a pixel.
    const auto buildLine = [&] (const std::vector<double>& db, float& firstX, float& lastX)
    {
        juce::Path line;
        bool started = false;
        int column = std::numeric_limits<int>::min();
        float bestX = 0.0f;
        double best = -1e9;

        const auto flush = [&]
        {
            if (best < -1e8)
                return;

            const float y = yFor (juce::jlimit ((double) bottomDb - 6.0, 12.0, best));

            if (! started)  { line.startNewSubPath (bestX, y); firstX = bestX; started = true; }
            else            { line.lineTo (bestX, y); }

            lastX = bestX;
        };

        for (size_t i = 0; i < freqs.size(); ++i)
        {
            if (freqs[i] < 0.8 * FrequencyAxis::lo || freqs[i] > 1.1 * FrequencyAxis::hi)
                continue;

            const float x = FrequencyAxis::xFor (freqs[i], area);
            const int col = (int) std::floor (x);

            if (col != column)
            {
                flush();
                column = col;
                best = -1e9;
            }

            const double value = shown (db[i], freqs[i]);

            if (value > best)
            {
                best = value;
                bestX = x;
            }
        }

        flush();
        return line;
    };

    const auto drawCurve = [&] (const std::vector<double>& db, const std::vector<double>& peaks,
                                juce::Colour colour, float fillAlpha, float thickness)
    {
        if (freqs.empty())
            return;

        float firstX = area.getX(), lastX = area.getX();
        const auto line = buildLine (db, firstX, lastX);

        juce::Path filled (line);
        filled.lineTo (lastX, area.getBottom() + 2.0f);
        filled.lineTo (firstX, area.getBottom() + 2.0f);
        filled.closeSubPath();

        g.setGradientFill (juce::ColourGradient (colour.withAlpha (fillAlpha), 0.0f, area.getY(),
                                                 colour.withAlpha (fillAlpha * 0.35f), 0.0f, area.getBottom(), false));
        g.fillPath (filled);
        g.setColour (colour);
        g.strokePath (line, juce::PathStrokeType (thickness, juce::PathStrokeType::curved));

        if (peakHold)
        {
            g.setColour (colour.brighter (0.5f).withAlpha (0.65f));
            g.strokePath (buildLine (peaks, firstX, lastX), juce::PathStrokeType (1.0f));
        }
    };

    drawCurve (facadeDb, peakFacade, theme::facade, 0.50f, 2.0f);
    drawCurve (lfeDb, peakLfe, theme::lfe, 0.42f, 1.8f);

    // Overlap marks: a thin line under each band, warming up where the LFE and
    // the screen channels both play at similar levels (60-120 Hz only).
    for (size_t b = 0; b < 3; ++b)
    {
        const float x0 = FrequencyAxis::xFor (Translation::bandEdges[b], area) + 2.0f;
        const float x1 = FrequencyAxis::xFor (Translation::bandEdges[b + 1], area) - 2.0f;
        const float amount = b == 0 ? 0.0f : (float) overlap[b];
        g.setColour (juce::Colour (0xff8a8a8a).interpolatedWith (juce::Colour (0xffffb347), amount).withAlpha (0.35f + 0.55f * amount));
        g.fillRoundedRectangle (x0, area.getBottom() - 6.0f, x1 - x0, 3.0f + 2.0f * amount, 1.5f);
    }

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

    // Legend
    {
        auto legend = juce::Rectangle<float> (area.getX() + 8.0f, area.getY() + 6.0f, 150.0f, 20.0f);
        g.setColour (theme::plot.withAlpha (0.75f));
        g.fillRoundedRectangle (legend, 4.0f);
        g.setFont (uiFont (12.0f, true));

        for (const auto& item : { std::pair<const char*, juce::Colour> { "LCR sum", theme::facade }, { "LFE", theme::lfe } })
        {
            auto cell = legend.removeFromLeft (item.second == theme::facade ? 88.0f : 62.0f);
            g.setColour (item.second);
            g.fillRect (cell.getX() + 8.0f, cell.getCentreY() - 4.0f, 10.0f, 8.0f);
            g.setColour (theme::text);
            g.drawText (item.first, cell.withTrimmedLeft (24.0f), juce::Justification::centredLeft);
        }
    }

    // Cursor read-out: exact frequency and levels under the mouse
    if (hoverX >= 0.0f && ! freqs.empty())
    {
        const double hz = FrequencyAxis::hzFor (hoverX, area);
        const auto nearest = (size_t) std::distance (freqs.begin(), std::min_element (freqs.begin(), freqs.end(),
                                 [hz] (double a, double b) { return std::abs (a - hz) < std::abs (b - hz); }));

        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.fillRect (hoverX, area.getY(), 1.0f, area.getHeight());

        const auto text = juce::String (hz, 1) + " Hz     LCR " + juce::String (shown (facadeDb[nearest], hz), 1)
                            + " dB     LFE " + juce::String (shown (lfeDb[nearest], hz), 1) + " dB";
        const float width = 290.0f;
        auto box = juce::Rectangle<float> (juce::jlimit (area.getX() + 4.0f, area.getRight() - width - 4.0f, hoverX + 10.0f),
                                           area.getY() + 32.0f, width, 22.0f);
        g.setColour (juce::Colour (0xe6101010));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (juce::Colour (0xff666666));
        g.drawRoundedRectangle (box, 4.0f, 1.0f);
        g.setColour (theme::text);
        g.setFont (uiFont (13.0f, true));
        g.drawText (text, box, juce::Justification::centred);
    }
}

//==============================================================================
void TransientStrip::show (const TransientEvent& e)
{
    // An attack the LFE does not share must not wipe the verdict of one it does
    if (! e.inLfe && event.inLfe && age < 1.0)
        return;

    event = e;
    age = 0.0;
    repaint();
}

void TransientStrip::advance (double seconds)
{
    const bool visible = age < holdSeconds + fadeSeconds;
    age += seconds;

    if (visible)
        repaint();
}

bool TransientStrip::fine (const TransientEvent& e) const
{
    return e.match >= minMatch && e.delayUncertaintyMs <= unmeasurableMs && ! e.delayAtLimit;
}

bool TransientStrip::isFault (const TransientEvent& e) const
{
    if (! e.inLfe)
        return false;

    if (fine (e))
        return std::abs (e.delayMs) > juce::jmax (alignedToleranceMs, 2.0 * e.delayUncertaintyMs)
                 || std::abs (e.phase) > juce::MathConstants<double>::pi / 3.0;

    return e.lfeAttack && std::abs (e.attackDelayMs) > attackToleranceMs;
}

void TransientStrip::paint (juce::Graphics& g)
{
    g.fillAll (theme::plot);
    g.setColour (theme::outline);
    g.fillRect (0, 0, getWidth(), 1);

    g.setColour (theme::dimText);
    g.setFont (uiFont (12.0f));
    g.drawFittedText ("Transients\nmix vs LFE", 4, 0, FrequencyAxis::leftMargin + 30, getHeight(), juce::Justification::centredLeft, 2);

    const auto box = getLocalBounds().toFloat().withTrimmedLeft ((float) FrequencyAxis::leftMargin + 36.0f)
                                               .withTrimmedRight ((float) FrequencyAxis::rightMargin).reduced (6.0f, 6.0f);
    g.setColour (juce::Colour (0xff1b1b1b));
    g.fillRoundedRectangle (box, 5.0f);

    auto inner = box.reduced (12.0f, 4.0f);
    const auto& e = event;
    const double range = e.searchMs > 0.0 ? e.searchMs : 250.0;

    // Delay scale: LFE early on the left, late on the right
    auto scaleRow = inner.removeFromBottom (14.0f);
    g.setColour (juce::Colour (0xff777777));
    g.setFont (uiFont (10.0f));
    g.drawText ("early -" + juce::String ((int) range) + " ms", scaleRow.removeFromLeft (84.0f), juce::Justification::centredLeft);
    g.drawText ("late +" + juce::String ((int) range) + " ms", scaleRow.removeFromRight (84.0f), juce::Justification::centredRight);
    const auto scale = scaleRow;
    const float mid = scale.getCentreX(), y = scale.getCentreY();
    g.setColour (juce::Colour (0xff555555));
    g.fillRect (scale.getX(), y, scale.getWidth(), 1.0f);
    g.fillRect (mid, y - 5.0f, 1.0f, 10.0f);

    if (! active)
    {
        g.setColour (juce::Colour (0xff777777));
        g.setFont (uiFont (12.0f));
        g.drawText ("Transient check off. Solo in your DAW what you want to check, then switch it on in the Transients panel.",
                    inner, juce::Justification::centredLeft);
        return;
    }

    const float alpha = (float) juce::jlimit (0.0, 1.0, 1.0 - (age - holdSeconds) / fadeSeconds);

    if (alpha <= 0.0f)
    {
        g.setColour (juce::Colour (0xff555555));
        g.setFont (uiFont (12.0f));
        g.drawText ("waiting for an attack in the mix", inner, juce::Justification::centredLeft);
        return;
    }

    const auto amber = juce::Colour (0xfff5c400), orange = juce::Colour (0xffff8a00), red = juce::Colour (0xffff3a2a);
    const auto degree = juce::String::fromUTF8 ("\xc2\xb0");
    const auto about = juce::String::fromUTF8 ("\xe2\x89\x88 ");
    juce::String verdict, detail;
    juce::Colour colour = theme::dimText;
    double shown = 0.0;
    bool marker = false;

    if (! e.inLfe)
    {
        verdict = "Attack in the mix: nothing in the LFE within " + juce::String ((int) range) + " ms";
        detail = "Widen the search range in the Transients panel if the LFE may be further off.";
    }
    else if (fine (e))
    {
        // An offset is only claimed when it clearly exceeds what can be measured
        const double phaseDegrees = std::abs (e.phase) * 180.0 / juce::MathConstants<double>::pi;
        const double tolerance = juce::jmax (alignedToleranceMs, 2.0 * e.delayUncertaintyMs);
        const auto plusMinus = juce::String::fromUTF8 (" \xc2\xb1") + juce::String (e.delayUncertaintyMs, 1) + " ms";
        shown = e.delayMs;
        marker = true;

        if (std::abs (e.delayMs) > tolerance)
        {
            verdict = (e.delayMs > 0.0 ? "LFE late " : "LFE early ") + juce::String (std::abs (e.delayMs), 1) + plusMinus;
            colour = orange;
        }
        else if (phaseDegrees > 120.0)  { verdict = "Aligned, polarity inverted";   colour = red; }
        else if (phaseDegrees < 60.0)   { verdict = "Aligned, in phase";            colour = amber; }
        else
        {
            verdict = "Aligned, phase shifted " + juce::String ((int) std::lround (phaseDegrees)) + degree;
            colour = orange;
        }

        const double sumDb = 10.0 * std::log10 (juce::jmax (1e-3, 1.0 + e.interactionNow));
        detail = "likeness " + juce::String (e.match, 2) + "   sum " + (sumDb >= 0.0 ? "+" : "") + juce::String (sumDb, 1) + " dB";
    }
    else if (e.lfeAttack)
    {
        shown = e.attackDelayMs;
        marker = true;

        if (std::abs (e.attackDelayMs) > attackToleranceMs)
        {
            verdict = (e.attackDelayMs > 0.0 ? "LFE attack late " : "LFE attack early ") + about
                        + juce::String ((int) std::lround (std::abs (e.attackDelayMs))) + " ms";
            colour = orange;
        }
        else
        {
            verdict = "LFE attack on time (within " + juce::String ((int) attackToleranceMs) + " ms)";
            colour = amber;
        }

        detail = "timed attack against attack: the LFE does not carry the same sound as the mix (likeness "
                   + juce::String (e.match, 2) + "), no phase reading";
    }
    else
    {
        verdict = "In the LFE, timing not measurable";
        detail = "likeness " + juce::String (e.match, 2);
        colour = amber;
    }

    g.setColour (colour.withMultipliedAlpha (alpha));
    g.setFont (uiFont (15.0f, true));
    g.drawText (verdict, inner.removeFromTop (19.0f), juce::Justification::centredLeft);
    g.setColour (theme::dimText.withMultipliedAlpha (alpha));
    g.setFont (uiFont (11.0f));
    g.drawText (detail, inner.removeFromTop (14.0f), juce::Justification::centredLeft);

    if (marker)
    {
        const float x = mid + (float) juce::jlimit (-1.0, 1.0, shown / range) * scale.getWidth() / 2.0f;
        g.setColour (colour.withMultipliedAlpha (alpha));
        g.fillEllipse (x - 4.5f, y - 4.5f, 9.0f, 9.0f);
    }
}

//==============================================================================
juce::Rectangle<float> CorrelationPlot::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft ((float) FrequencyAxis::leftMargin)
                                     .withTrimmedRight ((float) FrequencyAxis::rightMargin).withTrimmedBottom (40.0f);
}

void CorrelationPlot::update (const Analysis& a, const AlertTracker& alerts, double thresholdToShow, bool outOfPhaseOnly)
{
    grid = a.grid;
    curves = alerts.curves;
    threshold = thresholdToShow;
    negativeOnly = outOfPhaseOnly;
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

    FrequencyAxis::drawLabels (g, area);
    g.setColour (theme::dimText);
    g.drawText ("Frequency (Hz)", juce::Rectangle<float> (area.getX(), area.getBottom() + 20.0f, area.getWidth(), 18.0f),
                juce::Justification::centred);

    g.setColour (theme::outline);
    g.fillRect (area.getX(), area.getY(), area.getWidth(), 1.0f);

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (area.toNearestInt());

    // The zones that raise an alert
    g.setColour (theme::accent.withAlpha (0.10f));
    g.fillRect (area.getX(), yFor (-threshold), area.getWidth(), yFor (-1.0) - yFor (-threshold));

    if (! negativeOnly)
        g.fillRect (area.getX(), yFor (1.0), area.getWidth(), yFor (threshold) - yFor (1.0));

    FrequencyAxis::drawVerticalGrid (g, area);
    g.setColour (theme::grid);
    g.fillRect (area.getX(), yFor (1.0), area.getWidth(), 1.0f);
    g.fillRect (area.getX(), yFor (-1.0), area.getWidth(), 1.0f);
    g.setColour (juce::Colour (0xff777777));
    g.fillRect (area.getX(), yFor (0.0), area.getWidth(), 1.0f);

    const float dashes[] = { 5.0f, 4.0f };
    g.setColour (theme::accent);
    g.drawDashedLine ({ area.getX(), yFor (-threshold), area.getRight(), yFor (-threshold) }, dashes, 2, 1.0f);

    if (! negativeOnly)
        g.drawDashedLine ({ area.getX(), yFor (threshold), area.getRight(), yFor (threshold) }, dashes, 2, 1.0f);

    const juce::Colour colours[] = { theme::curveL, theme::curveC, theme::curveR };
    const char* names[] = { "L", "C", "R" };

    for (size_t c = 0; c < 3; ++c)
    {
        if (grid.empty())
            break;

        juce::Path line;
        bool started = false;

        for (size_t i = 0; i < grid.size(); ++i)
        {
            const float x = FrequencyAxis::xFor (grid[i], area), y = yFor (curves[c][i]);

            if (! started)  { line.startNewSubPath (x, y); started = true; }
            else            { line.lineTo (x, y); }
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

//==============================================================================
void Card::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xf21a1a1a));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (juce::Colour (0xff5a5a5a));
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
    g.setColour (theme::dimText);
    g.setFont (uiFont (13.0f, true));
    g.drawText (title, getLocalBounds().reduced (14, 8).removeFromTop (18), juce::Justification::centredLeft);
}

void Card::resized()
{
    if (onLayout)
        onLayout (getLocalBounds().reduced (14, 10).withTrimmedTop (22));
}

//==============================================================================
juce::Colour ChainsView::colourFor (double penalty)
{
    if (penalty >= 0.5)    return juce::Colour (0xffc0392b);   // heard as clearly louder or quieter
    if (penalty >= 0.15)   return juce::Colour (0xffb9770e);
    return juce::Colour (0xff2e6b4a);
}

void ChainsView::update (const ChainsInfo& newInfo)
{
    info = newInfo;
    repaint();
}

juce::Rectangle<float> ChainsView::gainButton (int index) const
{
    const float left = 150.0f, width = (index == ChainResult::numGains ? 64.0f : 44.0f);
    return { left + 46.0f * (float) index, 0.0f, width, 20.0f };
}

void ChainsView::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i <= ChainResult::numGains; ++i)
        if (gainButton (i).contains (e.position))
            setGainView (i);
}

void ChainsView::paint (juce::Graphics& g)
{
    const auto& t = info.translation;
    auto area = getLocalBounds().toFloat();
    const float rowHeight = 22.0f, labelWidth = 150.0f;

    // Which subwoofer level the table shows
    {
        g.setColour (theme::dimText);
        g.setFont (uiFont (12.0f, true));
        g.drawText ("Subwoofer level", 0, 0, 150, 20, juce::Justification::centredLeft);

        for (int i = 0; i <= ChainResult::numGains; ++i)
        {
            const auto box = gainButton (i).reduced (1.0f);
            const bool on = i == gainView;
            g.setColour (on ? juce::Colour (0xff3a6ea5) : juce::Colour (0xff2a2a2a));
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (on ? juce::Colours::white : theme::dimText);
            g.setFont (uiFont (11.5f, on));
            const double step = i < ChainResult::numGains ? PlaybackSimulator::gainSteps[i] : 0.0;
            g.drawText (i == ChainResult::numGains ? juce::String ("Worst")
                                                   : (step > 0.0 ? "+" : "") + juce::String ((int) step) + " dB",
                        box, juce::Justification::centred);
        }

        area.removeFromTop (28.0f);
    }

    const auto cinema = [&t] (double delayMs, bool inverted) -> const ChainResult*
    {
        for (const auto& c : t.chains)
            if (c.kind == ChainResult::Kind::cinema && std::abs (c.delayMs - delayMs) < 1e-9 && c.inverted == inverted)
                return &c;

        return nullptr;
    };

    const auto home = [&t] (double corner, bool encoder) -> const ChainResult*
    {
        for (const auto& c : t.chains)
            if (c.kind == ChainResult::Kind::bassManaged && std::abs (c.crossoverHz - corner) < 1e-9 && c.encoderFilter == encoder)
                return &c;

        return nullptr;
    };

    const auto stereo = [&t] () -> const ChainResult*
    {
        return ! t.chains.empty() && t.chains.back().kind == ChainResult::Kind::stereo ? &t.chains.back() : nullptr;
    };

    const auto heading = [&] (const juce::String& text)
    {
        g.setColour (theme::dimText);
        g.setFont (uiFont (12.0f, true));
        g.drawText (text, area.removeFromTop (20.0f), juce::Justification::centredLeft);
    };

    const auto cell = [&] (juce::Rectangle<float> box, const ChainResult* chain)
    {
        box = box.reduced (1.5f);

        if (chain == nullptr || ! t.hasBass)
        {
            g.setColour (juce::Colour (0xff2a2a2a));
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (juce::Colour (0xff777777));
            g.setFont (uiFont (12.0f));
            g.drawText (juce::String::fromUTF8 ("\xe2\x80\x94"), box, juce::Justification::centred);
            return;
        }

        const bool worstView = gainView >= ChainResult::numGains;
        const auto v = (size_t) (worstView ? chain->worstGain : gainView);
        const double db = chain->gainChangeDb[v];
        g.setColour (colourFor (chain->gainPenalty[v]));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (juce::Colours::white);
        g.setFont (uiFont (12.5f, true));
        const auto value = db <= -PlaybackSimulator::changeLimitDb + 0.05
                             ? juce::String::fromUTF8 ("\xe2\x89\xa4-") + juce::String ((int) PlaybackSimulator::changeLimitDb)
                             : (db >= 0.0 ? "+" : "") + juce::String (db, 1);

        // In the "Worst" view, a small mark tells which level error it is
        const double step = PlaybackSimulator::gainSteps[v];
        const bool marked = worstView && chain->kind != ChainResult::Kind::stereo && step != 0.0;
        g.drawText (value + " dB", marked ? box.withTrimmedRight (30.0f) : box, juce::Justification::centred);

        if (marked)
        {
            g.setFont (uiFont (9.5f));
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.drawText ((step > 0.0 ? "sub +" : "sub ") + juce::String ((int) step), box.removeFromRight (36.0f), juce::Justification::centred);
        }
    };

    const auto table = [&] (const juce::StringArray& columns, const juce::StringArray& rows,
                            const std::function<const ChainResult* (int row, int column)>& lookup,
                            const std::function<bool (int row, int column)>& exists = {})
    {
        auto header = area.removeFromTop (18.0f);
        header.removeFromLeft (labelWidth);
        const float width = header.getWidth() / (float) columns.size();
        g.setColour (theme::dimText);
        g.setFont (uiFont (11.5f));

        for (const auto& name : columns)
            g.drawText (name, header.removeFromLeft (width), juce::Justification::centred);

        for (int r = 0; r < rows.size(); ++r)
        {
            auto row = area.removeFromTop (rowHeight);
            g.setColour (theme::text);
            g.setFont (uiFont (12.5f));
            g.drawText (rows[r], row.removeFromLeft (labelWidth), juce::Justification::centredLeft);

            for (int c = 0; c < columns.size(); ++c)
            {
                const auto box = row.removeFromLeft (width);

                if (! exists || exists (r, c))
                    cell (box, lookup (r, c));
            }
        }

        area.removeFromTop (8.0f);
    };

    heading ("Bass on each playback system against the mix as approved (cinema, sub on time, in polarity), 20-120 Hz");

    const double delays[] = { -5.0, -2.5, 0.0, 2.5, 5.0 };
    table ({ "-5 ms", "-2.5 ms", "0 ms", "+2.5 ms", "+5 ms" }, { "Cinema, sub in polarity", "Cinema, sub inverted" },
           [&] (int r, int c) { return cinema (delays[c], r == 1); });

    const double corners[] = { 60.0, 80.0, 100.0 };
    table ({ "60 Hz", "80 Hz", "100 Hz", "Stereo, no LFE" }, { "Bass managed", "+ encoder LFE filter" },
           [&] (int r, int c) { return c < 3 ? home (corners[c], r == 1) : stereo(); },
           [] (int r, int c) { return c < 3 || r == 0; });

    // Read-outs
    const auto line = [&] (const juce::String& label, const juce::String& value, bool warn)
    {
        auto row = area.removeFromTop (rowHeight);
        g.setColour (theme::dimText);
        g.setFont (uiFont (12.5f));
        g.drawText (label, row.removeFromLeft (labelWidth + 40.0f), juce::Justification::centredLeft);
        g.setColour (warn ? juce::Colour (0xffffb347) : theme::text);
        g.setFont (uiFont (12.5f, warn));
        g.drawText (value, row, juce::Justification::centredLeft);
    };

    const juce::String none = juce::String::fromUTF8 ("\xe2\x80\x94");
    juce::String worst;

    if (t.hasBass && t.worstChain >= 0 && t.interaction > 0.02)
    {
        const auto& c = t.chains[(size_t) t.worstChain];
        const auto v = (size_t) c.worstGain;
        const double step = PlaybackSimulator::gainSteps[v];
        worst = "     worst: " + juce::String (c.name) + (step != 0.0 ? ", sub " + juce::String (step > 0.0 ? "+" : "") + juce::String ((int) step) + " dB" : juce::String())
                  + " (" + (c.gainChangeDb[v] >= 0.0 ? "+" : "") + juce::String (c.gainChangeDb[v], 1) + " dB)";
    }

    line ("Robustness", t.hasBass ? juce::String (juce::roundToInt (t.robustness)) + " / 100     interaction x LFE dependence" : none,
          t.hasBass && t.robustness < 50.0);

    line ("  from interaction", t.hasBass ? juce::String (juce::roundToInt (100.0 * (1.0 - t.interaction))) + " / 100" + worst : none,
          t.hasBass && t.interaction > 0.5);

    line ("  from LFE dependence", t.hasBass ? juce::String (juce::roundToInt (100.0 * (1.0 - t.dependence)))
                                                 + " / 100     LFE " + (t.lfeBalanceDb >= 0.0 ? "+" : "") + juce::String (t.lfeBalanceDb, 1)
                                                 + " dB against the screen, carries " + juce::String (juce::roundToInt (100.0 * t.lfeShare))
                                                 + " % of the bass"
                                             : none,
          t.hasBass && t.lfeBalanceDb > PlaybackSimulator::balanceWarningDb);

    const auto signedValue = [] (double v) { return (v >= 0.0 ? "+" : "") + juce::String (v, 2); };
    const int seconds = (int) info.sessionSeconds;
    const bool enough = info.sessionSeconds >= polarityMinSeconds;
    const bool opposed = enough && (info.sessionCorrelation[0] < polarityWarning || info.sessionCorrelation[1] < polarityWarning
                                      || info.sessionCorrelation[2] < polarityWarning);
    line ("Session polarity, LFE vs", "L " + signedValue (info.sessionCorrelation[0]) + "   C " + signedValue (info.sessionCorrelation[1])
                                        + "   R " + signedValue (info.sessionCorrelation[2]) + "     over "
                                        + juce::String (seconds / 60) + ":" + juce::String (seconds % 60).paddedLeft ('0', 2),
          opposed);
}

//==============================================================================
void HistoryStrip::push (const Sample& sample)
{
    if (samples.empty())
        samples.resize (columns);

    samples[writePos] = sample;
    writePos = (writePos + 1) % samples.size();
    repaint();
}

void HistoryStrip::paint (juce::Graphics& g)
{
    g.fillAll (theme::plot);
    g.setColour (theme::outline);
    g.fillRect (0, 0, getWidth(), 1);

    auto area = getLocalBounds().toFloat().withTrimmedLeft ((float) FrequencyAxis::leftMargin + 36.0f)
                                          .withTrimmedRight ((float) FrequencyAxis::rightMargin).reduced (0.0f, 5.0f);
    const char* names[] = { "Alert", "Robustness", "LFE-dependent" };
    const int lanes = 3;
    const float lane = area.getHeight() / (float) lanes;

    g.setFont (uiFont (10.5f));

    for (int i = 0; i < lanes; ++i)
    {
        const auto row = juce::Rectangle<float> (area.getX(), area.getY() + lane * (float) i, area.getWidth(), lane).reduced (0.0f, 1.0f);
        g.setColour (theme::dimText);
        g.drawText (names[i], juce::Rectangle<float> (4.0f, row.getY() - 2.0f, area.getX() - 10.0f, row.getHeight() + 4.0f),
                    juce::Justification::centredRight);
        g.setColour (juce::Colour (0xff1c1c1c));
        g.fillRect (row);

        if (samples.empty())
            continue;

        const float width = row.getWidth() / (float) columns;

        for (int c = 0; c < columns; ++c)
        {
            const auto& s = samples[(writePos + (size_t) c) % samples.size()];
            const float x = row.getX() + width * (float) c;

            if (i == 1)
            {
                // Robustness: always coloured while there is bass, green for a
                // sound mix, through orange, to red for a fragile one.
                if (! s.scored)
                    continue;

                const auto red = juce::Colour (0xffe53935), orange = juce::Colour (0xffff9800), green = juce::Colour (0xff43a047);
                const auto colour = s.score < 0.5f ? red.interpolatedWith (orange, s.score / 0.5f)
                                                   : orange.interpolatedWith (green, (s.score - 0.5f) / 0.5f);
                g.setColour (colour);
                g.fillRect (x, row.getY(), width + 0.5f, row.getHeight());
                continue;
            }

            const float value = i == 0 ? s.alert : s.dependence;
            const auto colour = i == 0 ? (s.outOfPhase ? juce::Colour (0xffff3a2a) : juce::Colour (0xfff5c400))
                                       : juce::Colour (0xff7e9cff);

            if (value > 0.02f)
            {
                g.setColour (colour.withAlpha (juce::jlimit (0.0f, 1.0f, 0.15f + 0.85f * value)));
                g.fillRect (x, row.getY(), width + 0.5f, row.getHeight());
            }
        }
    }

    g.setColour (juce::Colour (0xff777777));
    g.setFont (uiFont (10.0f));
    g.drawText ("now", juce::Rectangle<float> (area.getRight() + 1.0f, area.getY(), 22.0f, 12.0f), juce::Justification::centredLeft);
}

} // namespace subguard
