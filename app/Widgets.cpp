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

void SpeakerIndicator::setDrive (double newDrive)
{
    newDrive = juce::jlimit (0.0, 1.0, newDrive);

    if (std::abs (newDrive - drive) > 0.01)
    {
        drive = newDrive;
        repaint();
    }
}

void SpeakerIndicator::setDimmed (bool shouldBeDimmed)
{
    if (dimmed != shouldBeDimmed)
    {
        dimmed = shouldBeDimmed;
        repaint();
    }
}

void SpeakerIndicator::setMoving (bool shouldMove)
{
    if (moving != shouldMove)
    {
        moving = shouldMove;
        repaint();
    }
}

void SpeakerIndicator::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isLeftButtonDown())
        return;

    // Click: solo. Option-click: mute.
    if (e.mods.isAltDown())     muted = ! muted;
    else                        solo = ! solo;

    repaint();

    if (onToggle)
        onToggle();
}

void SpeakerIndicator::paint (juce::Graphics& g)
{
    g.setOpacity (1.0f);
    g.beginTransparencyLayer (dimmed ? 0.4f : 1.0f);

    const float wAvail = (float) getWidth(), hAvail = (float) getHeight();
    const float aspect = 0.65f;
    float drawW, drawH;

    if (wAvail / hAvail > aspect)   { drawH = hAvail * 0.95f; drawW = drawH * aspect; }
    else                            { drawW = wAvail * 0.95f; drawH = drawW / aspect; }

    const juce::Rectangle<float> r ((wAvail - drawW) / 2.0f, (hAvail - drawH) / 2.0f, drawW, drawH);

    const bool lit = solo && ! dimmed;                              // soloed: the speaker shines
    const double shown = dimmed ? 0.0 : level;
    const float push = (moving && ! dimmed) ? (float) drive : 0.0f; // the channel playing: the cabinet swells a little, the cone more

    const float cx = r.getCentreX();
    const float cabH = r.getHeight() * 0.60f * (1.0f + 0.035f * push), cabW = cabH * 0.55f;
    const float cabX = cx - cabW / 2.0f, cabY = r.getY() + r.getHeight() * 0.05f - r.getHeight() * 0.012f * push;

    const juce::Colour edge = lit ? juce::Colour (255, 255, 255) : juce::Colour (205, 205, 205);
    const float line = juce::jmax (1.2f, drawW * 0.022f);
    const juce::Rectangle<float> cabinet (cabX, cabY, cabW, cabH);

    if (lit)
    {
        // Halo around a soloed speaker
        for (int ring = 3; ring >= 1; --ring)
        {
            g.setColour (juce::Colour (150, 225, 255).withAlpha (0.10f));
            g.drawRoundedRectangle (cabinet.expanded ((float) ring * line * 1.4f), drawW * 0.07f + (float) ring * line, line * 2.2f);
        }
    }

    // Cabinet
    g.setColour (lit ? juce::Colour (30, 42, 48) : juce::Colour (16, 16, 16));
    g.fillRoundedRectangle (cabinet, drawW * 0.07f);
    g.setColour (edge);
    g.drawRoundedRectangle (cabinet, drawW * 0.07f, lit ? line * 1.3f : line);

    // Tweeter and woofer
    const auto driver = [&] (float centreY, float radius, juce::Colour fill)
    {
        const juce::Rectangle<float> c (cx - radius, centreY - radius, 2.0f * radius, 2.0f * radius);
        g.setColour (fill);
        g.fillEllipse (c);
        g.setColour (edge);
        g.drawEllipse (c, line * 0.85f);
    };

    driver (cabY + cabH * 0.25f, cabW * 0.17f * (1.0f + 0.06f * push), juce::Colour (10, 10, 10));
    driver (cabY + cabH * 0.68f, cabW * 0.29f * (1.0f + 0.16f * push), juce::Colour (18, 18, 18));

    // Dust cap: a dot that grows as the cone moves
    {
        const float cap = cabW * (0.05f + 0.07f * push);
        g.setColour (edge.withAlpha (0.35f + 0.45f * push));
        g.fillEllipse (cx - cap, cabY + cabH * 0.68f - cap, 2.0f * cap, 2.0f * cap);
    }

    // Alert lamp under the cabinet: an arc of fixed size that lights up, like a bulb
    const float arcW = r.getHeight() * 0.60f * 0.55f * 2.1f, arcH = r.getHeight() * 0.22f;
    const juce::Rectangle<float> arcRect (cx - arcW / 2.0f, r.getY() + r.getHeight() * 0.69f, arcW, arcH);

    juce::Path arc;
    arc.addArc (arcRect.getX(), arcRect.getY(), arcRect.getWidth(), arcRect.getHeight(),
                juce::MathConstants<float>::pi * 2.0f / 3.0f, juce::MathConstants<float>::pi * 4.0f / 3.0f, true);

    const float thick = juce::jmax (1.8f, r.getHeight() * 0.018f);
    const auto rounded = [] (float t) { return juce::PathStrokeType (t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };

    g.setColour (juce::Colour (120, 120, 120).withAlpha (0.35f));      // the lamp, off
    g.strokePath (arc, rounded (thick));

    if (shown > 0.01)
    {
        // Yellow to red with the alert; only the light changes, never the size
        const auto alert = juce::Colour ((juce::uint8) 255, (juce::uint8) std::lround (255.0 * (1.0 - shown)), (juce::uint8) 0);
        const float light = (float) juce::jlimit (0.0, 1.0, 0.25 + 0.75 * shown);
        g.setColour (alert.withAlpha (0.16f * light));
        g.strokePath (arc, rounded (thick * 4.0f));
        g.setColour (alert.withAlpha (0.30f * light));
        g.strokePath (arc, rounded (thick * 2.2f));
        g.setColour (alert.withAlpha (light));
        g.strokePath (arc, rounded (thick));
    }

    // Label
    g.setColour (lit ? juce::Colours::white : juce::Colour (235, 235, 235));
    g.setFont (uiFont (juce::jmax (11.0f, r.getHeight() * 0.19f), true));
    g.drawText (label, juce::Rectangle<float> (cx - arcW / 2.0f, r.getY() + r.getHeight() * 0.66f, arcW, r.getHeight() * 0.30f),
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

    // One switch for everything that moves: the background and the speakers.
    // The alert lamps are not part of it.
    for (auto* speaker : { &left, &centre, &right })
        speaker->setMoving (animated);

    repaint();
}

void HeaderBar::setLevel (double target)
{
    level += (target > level ? 0.2 : 0.05) * (target - level);
}

void HeaderBar::advance (double seconds)
{
    if (! animated)
        return;

    // Always moving, slowly: a quarter of the former pace, hardly faster when something plays
    phase += seconds * 0.25 * (1.0 + 0.2 * level);
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

    g.setGradientFill (juce::ColourGradient (juce::Colour (9, 11, 12), 0.0f, 0.0f,
                                             juce::Colour (4, 4, 4), 0.0f, h, false));
    g.fillRect (bounds);

    // Glows in the colours of the app: deep blue, teal, green, olive. Each one
    // wanders sideways on its own path and breathes at its own pace, so the
    // background moves at any level. The bass lifts them: at rest they lie
    // along the bottom edge, and each hit sends them up the banner.
    // With the animation off, the same picture is drawn, still: nothing in
    // it may depend on the signal, or a speaker repainting on its own would
    // show a patch that no longer matches the rest.
    struct Glow { juce::Colour colour; double x, y, sx, sy, px, py, size, breathe; };

    static const Glow glows[] = { { juce::Colour (12, 60, 82),  0.22, 0.30, 0.20, 0.10, 0.21, 0.33, 0.55, 0.37 },
                                  { juce::Colour (18, 84, 88),  0.50, 0.20, 0.30, 0.10, 0.16, 0.27, 0.62, 0.29 },
                                  { juce::Colour (38, 96, 52),  0.74, 0.35, 0.22, 0.12, 0.25, 0.19, 0.50, 0.43 },
                                  { juce::Colour (84, 88, 26),  0.90, 0.25, 0.16, 0.10, 0.13, 0.31, 0.38, 0.23 },
                                  { juce::Colour (10, 40, 70),  0.08, 0.15, 0.12, 0.12, 0.18, 0.24, 0.42, 0.34 } };

    const double t = phase;                                 // frozen when the animation is off
    const float hit = animated ? (float) bass : 0.35f;      // 0..1, the bass right now
    const float loud = animated ? (float) level : 0.3f;
    const float brightness = 0.45f + 0.15f * loud + 0.30f * hit;
    const double twoPi = juce::MathConstants<double>::twoPi;
    int index = 0;

    for (const auto& glow : glows)
    {
        const double offset = 1.7 * index++;
        const float cx = w * (float) (glow.x + glow.sx * std::sin (twoPi * glow.px * 0.35 * t + offset));
        const float wander = (float) (glow.sy * std::cos (twoPi * glow.py * 0.35 * t + 2.0 * offset));
        const float low = h * (1.10f + wander), high = h * ((float) glow.y + wander);     // resting place, and where a full hit takes it
        const float cy = low + (high - low) * hit;
        const float radius = w * (float) (glow.size * (0.50 + 0.18 * std::sin (twoPi * glow.breathe * 0.4 * t + offset)))
                               * (0.80f + 0.35f * hit);

        juce::ColourGradient gradient (glow.colour.withAlpha (brightness), cx, cy,
                                       glow.colour.withAlpha (0.0f), cx + radius, cy, true);
        gradient.addColour (0.45, glow.colour.withAlpha (brightness * 0.45f));
        g.setGradientFill (gradient);
        g.fillRect (bounds);
    }

    // Two slow waves, like the one in the logo: they too rise with the bass
    for (int k = 0; k < 2; ++k)
    {
        juce::Path wave;
        const double speed = (k == 0 ? 0.55 : -0.38), length = (k == 0 ? 2.2 : 3.4);
        const float rest = h * (k == 0 ? 0.90f : 0.82f), top = h * (k == 0 ? 0.42f : 0.30f);
        const float base = rest + (top - rest) * hit, swing = h * (0.06f + 0.03f * loud + 0.16f * hit);

        for (int step = 0; step * 6.0f <= w + 6.0f; ++step)
        {
            const float x = step * 6.0f;
            const double u = x / w;
            const float y = base + swing * (float) (std::sin (twoPi * (length * u + speed * 0.5 * t))
                                                      * (0.6 + 0.4 * std::sin (twoPi * (0.7 * u - 0.11 * t) + k)));

            if (step == 0)  wave.startNewSubPath (x, y);
            else            wave.lineTo (x, y);
        }

        g.setColour ((k == 0 ? juce::Colour (70, 190, 190) : juce::Colour (150, 190, 90)).withAlpha (0.10f + 0.05f * loud + 0.22f * hit));
        g.strokePath (wave, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // A hairline closes the banner
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (0.0f, h - 1.0f, w, 1.0f);

    // Logo, or the name when no logo image was built in
    const auto logoArea = getLocalBounds().reduced (20).removeFromLeft ((getWidth() - 40) / 4).toFloat();

    if (logo.isValid())
    {
        g.setOpacity (1.0f);
        g.drawImage (logo, logoArea.reduced (logoArea.getWidth() * 0.12f, logoArea.getHeight() * 0.04f), juce::RectanglePlacement::centred);
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
    g.drawText (tr ("SubGUARD Analyzer : 5Hz - 150Hz"), getLocalBounds().removeFromTop (28), juce::Justification::centred);

    // Left axis: a line every 12 dB
    g.setFont (uiFont (12.0f));

    for (int db = 0; db > (int) bottomDb; db -= 12)
        g.drawText (juce::String (db), juce::Rectangle<float> (0.0f, yFor (db) - 8.0f, (float) FrequencyAxis::leftMargin - 8.0f, 16.0f),
                    juce::Justification::centredRight);

    {
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, 10.0f, area.getCentreY()));
        g.drawText (tilt ? tr ("Level (dB, tilted)") : tr ("Level (dBFS)"), juce::Rectangle<float> (-60.0f, area.getCentreY() - 8.0f, 140.0f, 16.0f),
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
        g.drawText (tr ("ROBUSTNESS"), box, juce::Justification::centredTop, false);
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

        for (const auto& item : { std::pair<juce::String, juce::Colour> { tr ("LCR sum"), theme::facade }, { "LFE", theme::lfe } })
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

        const auto text = juce::String (hz, 1) + tr (" Hz     LCR ") + juce::String (shown (facadeDb[nearest], hz), 1)
                            + tr (" dB     LFE ") + juce::String (shown (lfeDb[nearest], hz), 1) + " dB";
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
    g.drawFittedText (tr ("Transients\nmix vs LFE"), 4, 0, FrequencyAxis::leftMargin + 30, getHeight(), juce::Justification::centredLeft, 2);

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
    g.drawText (tr ("early -") + juce::String ((int) range) + " ms", scaleRow.removeFromLeft (84.0f), juce::Justification::centredLeft);
    g.drawText (tr ("late +") + juce::String ((int) range) + " ms", scaleRow.removeFromRight (84.0f), juce::Justification::centredRight);
    const auto scale = scaleRow;
    const float mid = scale.getCentreX(), y = scale.getCentreY();
    g.setColour (juce::Colour (0xff555555));
    g.fillRect (scale.getX(), y, scale.getWidth(), 1.0f);
    g.fillRect (mid, y - 5.0f, 1.0f, 10.0f);

    if (! active)
    {
        g.setColour (juce::Colour (0xff777777));
        g.setFont (uiFont (12.0f));
        g.drawText (tr ("Transient check off. Solo in your DAW what you want to check, then switch it on in the Transients panel."),
                    inner, juce::Justification::centredLeft);
        return;
    }

    const float alpha = (float) juce::jlimit (0.0, 1.0, 1.0 - (age - holdSeconds) / fadeSeconds);

    if (alpha <= 0.0f)
    {
        g.setColour (juce::Colour (0xff555555));
        g.setFont (uiFont (12.0f));
        g.drawText (tr ("waiting for an attack in the mix"), inner, juce::Justification::centredLeft);
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
        verdict = tr ("Attack in the mix: nothing in the LFE within ") + juce::String ((int) range) + " ms";
        detail = tr ("Widen the search range in the Transients panel if the LFE may be further off.");
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
            verdict = (e.delayMs > 0.0 ? tr ("LFE late ") : tr ("LFE early ")) + juce::String (std::abs (e.delayMs), 1) + plusMinus;
            colour = orange;
        }
        else if (phaseDegrees > 120.0)  { verdict = tr ("Aligned, polarity inverted");   colour = red; }
        else if (phaseDegrees < 60.0)   { verdict = tr ("Aligned, in phase");            colour = amber; }
        else
        {
            verdict = tr ("Aligned, phase shifted ") + juce::String ((int) std::lround (phaseDegrees)) + degree;
            colour = orange;
        }

        const double sumDb = 10.0 * std::log10 (juce::jmax (1e-3, 1.0 + e.interactionNow));
        // Three levels of link between the two attacks: close in time (below), alike, confirmed
        const bool confirmed = e.match >= 0.7 && e.delayUncertaintyMs <= 2.0;
        detail = (confirmed ? tr ("same sound, confirmed") : tr ("similar sound")) + "   " + tr ("likeness ") + juce::String (e.match, 2)
                   + tr ("   sum ") + (sumDb >= 0.0 ? "+" : "") + juce::String (sumDb, 1) + " dB";
    }
    else if (e.lfeAttack)
    {
        shown = e.attackDelayMs;
        marker = true;

        if (std::abs (e.attackDelayMs) > attackToleranceMs)
        {
            verdict = (e.attackDelayMs > 0.0 ? tr ("LFE attack late ") : tr ("LFE attack early ")) + about
                        + juce::String ((int) std::lround (std::abs (e.attackDelayMs))) + " ms";
            colour = orange.withMultipliedSaturation (0.6f);
        }
        else
        {
            verdict = tr ("LFE attack on time (within ") + juce::String ((int) attackToleranceMs) + " ms)";
            colour = amber;
        }

        // Only close in time: it may be another sound that happens to fall in the search range
        detail = tr ("close in time only, maybe another sound (likeness ") + juce::String (e.match, 2) + tr ("): timing attack against attack, no phase reading");
    }
    else
    {
        verdict = tr ("In the LFE, timing not measurable");
        detail = tr ("likeness ") + juce::String (e.match, 2);
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

    for (const auto& tick : { std::pair<double, juce::String> { 1.0, "+1 in" }, { 0.0, "0" }, { -1.0, tr ("-1 out") } })
        g.drawText (tick.second, juce::Rectangle<float> (0.0f, yFor (tick.first) - 8.0f, (float) FrequencyAxis::leftMargin - 8.0f, 16.0f),
                    juce::Justification::centredRight);

    FrequencyAxis::drawLabels (g, area);
    g.setColour (theme::dimText);
    g.drawText (tr ("Frequency (Hz)"), juce::Rectangle<float> (area.getX(), area.getBottom() + 20.0f, area.getWidth(), 18.0f),
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

        // L and R glow; C stays a plain line between them
        // L shines widest, so that it still shows where R runs over it
        if (c != 1)
        {
            const float wide = c == 0 ? 15.0f : 9.0f, narrow = c == 0 ? 7.0f : 4.5f;
            const auto glowColour = c == 0 ? colours[c].brighter (0.3f) : colours[c];
            g.setColour (glowColour.withAlpha (c == 0 ? 0.16f : 0.10f));
            g.strokePath (line, juce::PathStrokeType (wide, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (glowColour.withAlpha (c == 0 ? 0.34f : 0.22f));
            g.strokePath (line, juce::PathStrokeType (narrow, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setColour (c == 0 ? colours[c].brighter (0.25f) : colours[c]);
        g.strokePath (line, juce::PathStrokeType (c == 1 ? 1.7f : 2.0f, juce::PathStrokeType::curved));
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
    g.setColour (juce::Colour (0xff1b1b1d));      // opaque: what is behind does not show through
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

void ChainsView::paint (juce::Graphics& g)
{
    const auto& t = info.translation;
    auto area = getLocalBounds().toFloat();
    const float rowHeight = 22.0f, cellHeight = 32.0f, labelWidth = 150.0f;

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

        const auto dbText = [] (double db)
        {
            return db <= -PlaybackSimulator::changeLimitDb + 0.05
                     ? juce::String::fromUTF8 ("\xe2\x89\xa4-") + juce::String ((int) PlaybackSimulator::changeLimitDb)
                     : (db >= 0.0 ? "+" : "") + juce::String (db, 1);
        };

        g.setColour (colourFor (chain->penalty));
        g.fillRoundedRectangle (box, 3.0f);

        // The mean over 20-120 Hz, and under it the band that moves most down and most up
        auto top = box.withHeight (box.getHeight() * 0.58f), bottom = box.withTrimmedTop (box.getHeight() * 0.52f);
        g.setColour (juce::Colours::white);
        g.setFont (uiFont (12.5f, true));
        g.drawText (dbText (chain->changeDb) + " dB", top, juce::Justification::centredBottom);
        g.setColour (juce::Colours::white.withAlpha (0.78f));
        g.setFont (uiFont (10.0f));
        g.drawText (dbText (chain->minChangeDb) + juce::String::fromUTF8 (" \xe2\x80\xa6 ") + dbText (chain->maxChangeDb), bottom,
                    juce::Justification::centredTop);
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
            auto row = area.removeFromTop (cellHeight);
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

    heading (tr ("Bass on each playback system against the mix as approved (cinema, sub on time, in polarity), 20-120 Hz. "
                 "In each box: the mean, then the band that moves most down and most up"));

    const double delays[] = { -5.0, -2.5, 0.0, 2.5, 5.0 };
    // The subwoofer nearer or further than it should be: metres first, the time it makes in brackets
    juce::StringArray distances;

    for (double ms : delays)
        distances.add (std::abs (ms) < 1e-9 ? juce::String ("0 m")
                                            : (ms > 0.0 ? "+" : "-") + juce::String (std::abs (ms) * 0.001 * PlaybackSimulator::speedOfSound, 1)
                                                + " m (" + (ms > 0.0 ? "+" : "-") + juce::String (std::abs (ms), 1) + " ms)");

    table (distances, { tr ("Cinema, sub in polarity"), tr ("Cinema, sub inverted") },
           [&] (int r, int c) { return cinema (delays[c], r == 1); });

    const double corners[] = { 60.0, 80.0, 100.0 };
    table ({ "60 Hz", "80 Hz", "100 Hz", tr ("Stereo, no LFE") }, { tr ("Bass managed"), tr ("+ encoder LFE filter") },
           [&] (int r, int c) { return c < 3 ? home (corners[c], r == 1) : stereo(); },
           [] (int r, int c) { return c < 3 || r == 0; });

    // The other systems: not cinema, framed apart; they count only when ticked in Room Sim
    {
        const auto other = [&t] (int which) -> const ChainResult*
        {
            for (const auto& c : t.others)
                if (c.other == which)
                    return &c;

            return nullptr;
        };

        const auto frame = area.withHeight (18.0f + cellHeight).expanded (3.0f, 2.0f);
        g.setColour (otherSystemColour.withAlpha (0.10f));
        g.fillRoundedRectangle (frame, 4.0f);
        g.setColour (otherSystemColour.withAlpha (0.7f));
        g.drawRoundedRectangle (frame, 4.0f, 1.0f);

        juce::StringArray columns;

        for (int i = 0; i < PlaybackSimulator::numOtherSystems; ++i)
            columns.add (tr (PlaybackSimulator::otherSystemShortName (i)));

        table (columns, { tr ("Other system (not cinema)") }, [&] (int, int c) { return other (c); });
    }


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

    if (t.hasBass && ! t.worstRoom.empty() && t.interaction > 0.02)
    {
        worst = tr ("     worst: room ") + tr (juce::String (t.worstRoom)) + tr (" (room score ") + juce::String (juce::roundToInt (100.0 * (1.0 - t.interaction))) + ")";
    }
    else if (t.hasBass && t.worstChain >= 0 && t.interaction > 0.02)
    {
        const auto& c = t.chains[(size_t) t.worstChain];
        worst = tr ("     worst system: ") + tr (juce::String (c.name)) + " (" + (c.changeDb >= 0.0 ? "+" : "") + juce::String (c.changeDb, 1) + " dB)";
    }

    line (tr ("Robustness"), t.hasBass ? juce::String (juce::roundToInt (t.robustness)) + tr (" / 100     interaction x LFE dependence") : none,
          t.hasBass && t.robustness < 50.0);

    line (tr ("  from interaction"), t.hasBass ? juce::String (juce::roundToInt (100.0 * (1.0 - t.interaction))) + " / 100" + worst : none,
          t.hasBass && t.interaction > 0.5);

    line (tr ("  from LFE dependence (audible)"), t.hasBass ? juce::String (juce::roundToInt (100.0 * (1.0 - t.dependence)))
                                                 + tr (" / 100     LFE ") + (t.lfeBalanceDb >= 0.0 ? "+" : "") + juce::String (t.lfeBalanceDb, 1)
                                                 + tr (" dB against the screen, carries ") + juce::String (juce::roundToInt (100.0 * t.lfeShare))
                                                 + tr (" % of the bass")
                                             : none,
          t.hasBass && t.lfeBalanceDb > PlaybackSimulator::balanceWarningDb);

    // Level dependence: one box, the stronger its colour the more a subwoofer
    // set off its level would move the bass beyond what is expected
    {
        auto row = area.removeFromTop (rowHeight + 2.0f);
        g.setColour (theme::dimText);
        g.setFont (uiFont (12.5f));
        g.drawText (tr ("LFE level dependence"), row.removeFromLeft (labelWidth + 40.0f), juce::Justification::centredLeft);

        const double severity = t.hasBass ? juce::jlimit (0.0, 1.0, t.levelDependence) : 0.0;
        const auto quiet = juce::Colour (0xff2a2a2a), lightOrange = juce::Colour (0xffffb347), orange = juce::Colour (0xffff8a00),
                   red = juce::Colour (0xffe0261b);
        juce::Colour fill = quiet;

        if (severity > 0.05)
            fill = severity < 0.3 ? quiet.interpolatedWith (lightOrange, (float) (0.25 + 1.5 * severity))
                 : severity < 0.9 ? lightOrange.interpolatedWith (orange, (float) ((severity - 0.3) / 0.6)).interpolatedWith (red, (float) juce::jmax (0.0, (severity - 0.6) / 0.6))
                                  : red;

        auto box = row.removeFromLeft (210.0f).reduced (1.5f);
        g.setColour (fill);
        g.fillRoundedRectangle (box, 3.0f);

        juce::String text = none, note;

        if (t.hasBass)
        {
            const double dev = t.levelDeviationDb;
            text = tr ("mean ") + juce::String (t.levelMeanDb, 1) + tr (" dB  (max ") + (dev >= 0.0 ? "+" : "") + juce::String (dev, 1) + ")";

            if (severity <= 0.05)
                note = tr ("over all systems and subwoofer levels: the bass moves no more than it should");
            else
                note = dev < 0.0 ? tr ("over all systems and subwoofer levels: cancellation risk when the subwoofer is off its level")
                                 : tr ("over all systems and subwoofer levels: boost risk when the subwoofer is off its level");
        }

        g.setColour (severity > 0.3 ? juce::Colours::white : theme::text);
        g.setFont (uiFont (12.5f, severity > 0.3));
        g.drawText (text, box, juce::Justification::centred);

        // The explanation gets its own line, full width
        auto noteRow = area.removeFromTop (18.0f);
        noteRow.removeFromLeft (labelWidth + 40.0f);
        g.setColour (theme::dimText);
        g.setFont (uiFont (11.5f));
        g.drawFittedText (note, noteRow.toNearestInt(), juce::Justification::centredLeft, 1);
    }

    // Opposition of phase on the screen: its own corner. Cinema does not see it (each channel has its
    // own speaker); any sum does (fold-down, PA, phone, small speakers). It counts in the score only
    // through the other systems, when one is ticked in Room Sim.
    {
        area.removeFromTop (4.0f);
        auto block = area.removeFromTop (44.0f);
        const bool counted = info.otherSystemTicked;
        const double worstDb = t.hasBass ? t.antiphaseWorstDb : 0.0;
        const bool warn = t.hasBass && worstDb < -PlaybackSimulator::antiphaseWarningDb;
        g.setColour (otherSystemColour.withAlpha (0.10f));
        g.fillRoundedRectangle (block, 4.0f);
        g.setColour ((warn ? juce::Colour (0xffffb347) : otherSystemColour).withAlpha (0.7f));
        g.drawRoundedRectangle (block, 4.0f, 1.0f);
        block = block.reduced (6.0f, 3.0f);

        auto titleRow = block.removeFromTop (18.0f);
        g.setColour (warn ? juce::Colour (0xffffb347) : theme::text);
        g.setFont (uiFont (12.5f, true));
        const char* pairs[] = { "L / R", "L / C", "C / R" };
        juce::String value = juce::String::fromUTF8 ("\xe2\x80\x94");

        if (t.hasBass)
            value = worstDb > -0.05 ? tr ("none")
                                    : juce::String (worstDb, 1) + tr (" dB at ") + juce::String (juce::roundToInt (t.antiphaseWorstHz)) + " Hz ("
                                        + juce::String (t.antiphasePair >= 0 ? pairs[t.antiphasePair] : "") + ")";

        g.drawText (tr ("Screen channels in opposition of phase, summed: ") + value, titleRow.removeFromLeft (titleRow.getWidth() - 4.0f * 52.0f),
                    juce::Justification::centredLeft);

        // The four bands
        for (int b = 0; b < ChainResult::numBands; ++b)
        {
            auto box = titleRow.removeFromLeft (52.0f).reduced (1.5f, 0.5f);
            const double db = t.hasBass ? t.antiphaseDb[(size_t) b] : 0.0;
            const float strength = (float) juce::jlimit (0.0, 1.0, -db / 12.0);
            g.setColour (juce::Colour (0xff2a2a2a).interpolatedWith (juce::Colour (0xff3a9be0), strength));
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (strength > 0.3f ? juce::Colours::white : theme::dimText);
            g.setFont (uiFont (10.0f));
            g.drawText (juce::String (juce::roundToInt (PlaybackSimulator::bandCentreHz[b])) + " Hz " + juce::String (juce::roundToInt (db)), box,
                        juce::Justification::centred);
        }

        g.setColour (theme::dimText);
        g.setFont (uiFont (11.0f));
        g.drawFittedText (counted ? tr ("Counted in the score: an other system is ticked in Room Sim. Not seen in cinema, where each channel has its own speaker.")
                                  : tr ("Not counted in the score: tick an other system in Room Sim. Not seen in cinema, where each channel has its own speaker."),
                          block.toNearestInt(), juce::Justification::centredLeft, 1);
    }

    // What this table is, and is not
    area.removeFromTop (6.0f);
    g.setColour (theme::dimText.withAlpha (0.8f));
    g.setFont (uiFont (11.0f));
    g.drawFittedText (tr ("Electrical models: delay, polarity, filters and level, plus the rooms ticked below (modal simulation, Room Sim tab). "
                      "Real rooms are not boxes: a score of 100 does not protect against a given room's own faults."),
                      area.removeFromTop (30.0f).toNearestInt(), juce::Justification::topLeft, 2);
}

//==============================================================================
void RoomSimView::update (const RoomSimResult& r, double robustnessNow, bool scored)
{
    result = r;
    robustness = robustnessNow;
    hasScore = scored && r.hasBass;

    if (shownRoom.size() != r.rooms.size())
    {
        shownRoom.clear();

        for (const auto& room : r.rooms)
            shownRoom.push_back (room.roomScore);
    }
    else
    {
        for (size_t i = 0; i < r.rooms.size(); ++i)
            shownRoom[i] += 0.08 * (r.rooms[i].roomScore - shownRoom[i]);      // display smoothing only
    }

    repaint();
}

void RoomSimView::mouseDown (const juce::MouseEvent& e)
{
    for (size_t i = 0; i < rowAreas.size(); ++i)
        if (rowAreas[i].contains (e.position) && onToggle)
            onToggle ((int) i);
}

void RoomSimView::paint (juce::Graphics& g)
{
    rowAreas.clear();
    g.fillAll (theme::plot);
    auto area = getLocalBounds().toFloat().reduced (16.0f, 10.0f);

    // Header: the bass robustness, and how to read the rooms
    {
        auto top = area.removeFromTop (58.0f);
        auto big = top.removeFromLeft (230.0f);
        g.setColour (theme::dimText);
        g.setFont (uiFont (12.0f));
        g.drawText (tr ("Bass robustness"), big.removeFromTop (16.0f), juce::Justification::centredLeft);
        g.setColour (hasScore ? ChainsView::colourFor (1.0 - robustness / 100.0) : theme::dimText);
        g.setFont (uiFont (34.0f, true));
        g.drawText (hasScore ? juce::String (juce::roundToInt (robustness)) : juce::String ("--"), big, juce::Justification::centredLeft);

        top.removeFromRight (230.0f);      // room for the Soft / Medium / Strong selector
        g.setColour (theme::dimText);
        g.setFont (uiFont (11.5f));
        g.drawFittedText (tr ("Fifteen rooms, twenty seats each (rectangular rooms, modal sum). Each dot is a seat: what the LFE / screen "
                          "interaction does to its bass, against the mix as approved. The room's own colour is left out: a "
                          "decorrelated LFE puts every dot on 0. Ticked rooms count in the score (click a row). "
                          "Result = bass robustness x room score.")
                          + " " + tr ("Below, in purple, the other systems: not cinema, electrical models of venues; their four bands in place of seats, "
                                      "against the same channels with nothing cancelled."),
                          top.toNearestInt(), juce::Justification::centredLeft, 4);
    }

    area.removeFromTop (6.0f);

    // Column titles
    const float nameWidth = 250.0f, scoreWidth = 70.0f, resultWidth = 80.0f;
    {
        auto head = area.removeFromTop (16.0f);
        g.setColour (theme::dimText);
        g.setFont (uiFont (11.0f));
        g.drawText (tr ("Room"), head.removeFromLeft (nameWidth), juce::Justification::centredLeft);
        g.drawText (tr ("Result"), head.removeFromRight (resultWidth), juce::Justification::centred);
        g.drawText (tr ("Room"), head.removeFromRight (scoreWidth), juce::Justification::centred);
        g.drawText ("-12 dB", head.removeFromLeft (60.0f), juce::Justification::centredLeft);
        g.drawText ("+12 dB", head.removeFromRight (60.0f), juce::Justification::centredRight);
        g.drawText (tr ("seats: bass change from the LFE / screen interaction"), head, juce::Justification::centred);
    }

    if (! result.hasBass || result.rooms.empty())
    {
        g.setColour (theme::dimText);
        g.setFont (uiFont (13.0f));
        g.drawText (tr ("Waiting for bass"), area, juce::Justification::centred);
        return;
    }

    const auto numOthers = (size_t) std::count_if (result.rooms.begin(), result.rooms.end(), [] (const RoomResult& r) { return r.otherSystem; });
    const float separator = numOthers > 0 ? 20.0f : 0.0f;
    const float rowHeight = juce::jmin (46.0f, (area.getHeight() - separator) / (float) result.rooms.size());

    for (size_t i = 0; i < result.rooms.size(); ++i)
    {
        const auto& room = result.rooms[i];

        // Where the other systems begin: a line, and what they are
        if (room.otherSystem && (i == 0 || ! result.rooms[i - 1].otherSystem))
        {
            auto line = area.removeFromTop (separator);
            g.setColour (otherSystemColour.withAlpha (0.6f));
            g.fillRect (line.getX(), line.getCentreY(), line.getWidth(), 1.0f);
            auto label = line.withSizeKeepingCentre (juce::jmin (line.getWidth(), 520.0f), line.getHeight());
            g.setColour (theme::plot);
            g.fillRect (label);
            g.setColour (otherSystemColour);
            g.setFont (uiFont (11.5f, true));
            g.drawText (tr ("OTHER SYSTEMS - not cinema: venues, electrical model, screen channels summed"), label, juce::Justification::centred);
        }

        auto row = area.removeFromTop (rowHeight);
        rowAreas.push_back (row);
        const bool counted = i < selected.size() && selected[i];

        if (room.otherSystem)
        {
            g.setColour (otherSystemColour.withAlpha (0.07f));
            g.fillRect (row);
        }
        else if (i % 2 == 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.025f));
            g.fillRect (row);
        }

        // Tick box: does this room count in the score?
        {
            auto box = row.removeFromLeft (24.0f).withSizeKeepingCentre (13.0f, 13.0f);
            g.setColour (counted ? (room.otherSystem ? otherSystemColour.darker (0.3f) : juce::Colour (0xff3a9be0)) : juce::Colour (0xff3a3a3a));
            g.fillRoundedRectangle (box, 3.0f);

            if (counted)
            {
                juce::Path tick;
                tick.startNewSubPath (box.getX() + 3.0f, box.getCentreY());
                tick.lineTo (box.getX() + 5.5f, box.getBottom() - 3.0f);
                tick.lineTo (box.getRight() - 2.5f, box.getY() + 3.0f);
                g.setColour (juce::Colours::white);
                g.strokePath (tick, juce::PathStrokeType (1.8f));
            }
        }

        // Name and size
        auto name = row.removeFromLeft (nameWidth - 24.0f);

        if (room.otherSystem)
        {
            // The short name, and what the system does (in full in the tooltip of its tick box)
            const int which = (int) (i - (result.rooms.size() - numOthers));
            g.setColour (counted ? otherSystemColour : otherSystemColour.withAlpha (0.6f));
            g.setFont (uiFont (12.5f, counted));
            g.drawText (tr ("Other system: ") + tr (PlaybackSimulator::otherSystemShortName (which)), name.removeFromTop (row.getHeight() * 0.55f),
                        juce::Justification::bottomLeft);
            g.setColour (theme::dimText);
            g.setFont (uiFont (10.0f));
            g.drawFittedText (tr (juce::String (room.description)), name.toNearestInt(), juce::Justification::topLeft, 1, 0.8f);
        }
        else
        {
            g.setColour (counted ? theme::text : theme::dimText);
            g.setFont (uiFont (12.5f, counted));
            g.drawText (tr (juce::String (room.spec.name)), name.removeFromTop (row.getHeight() * 0.55f), juce::Justification::bottomLeft);
            g.setColour (theme::dimText);
            g.setFont (uiFont (10.0f));
            g.drawText (juce::String (room.spec.length, 0) + " x " + juce::String (room.spec.width, 0) + " x " + juce::String (room.spec.height, 1)
                          + " m,  RT60 " + juce::String (room.spec.rt60, 2) + " s",
                        name, juce::Justification::topLeft);
        }

        // Result = robustness x room score, and the room score
        const double roomScore = i < shownRoom.size() ? shownRoom[i] : room.roomScore;
        const double combined = robustness * roomScore / 100.0;
        auto resultBox = row.removeFromRight (resultWidth).reduced (6.0f, 3.0f);
        g.setColour (ChainsView::colourFor (1.0 - combined / 100.0));
        g.fillRoundedRectangle (resultBox, 4.0f);
        g.setColour (juce::Colours::white);
        g.setFont (uiFont (15.0f, true));
        g.drawText (hasScore ? juce::String (juce::roundToInt (combined)) : juce::String ("--"), resultBox, juce::Justification::centred);

        auto scoreBox = row.removeFromRight (scoreWidth);
        g.setColour (theme::text);
        g.setFont (uiFont (13.0f));
        g.drawText (juce::String (juce::roundToInt (roomScore)), scoreBox, juce::Justification::centred);

        // Seats on a -12 .. +12 dB scale
        auto scale = row.reduced (6.0f, 0.0f);
        const float mid = scale.getCentreX(), y = scale.getCentreY();
        const auto xFor = [&] (double db) { return mid + (float) juce::jlimit (-1.0, 1.0, db / 12.0) * scale.getWidth() / 2.0f; };

        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRect (scale.getX(), y, scale.getWidth(), 1.0f);

        for (double db : { -6.0, -3.0, 3.0, 6.0 })
            g.fillRect (xFor (db), y - 3.0f, 1.0f, 6.0f);

        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillRect (mid, y - 7.0f, 1.0f, 14.0f);

        for (size_t s = 0; s < room.seatChangeDb.size(); ++s)
        {
            const float x = xFor (room.seatChangeDb[s]);
            g.setColour (ChainsView::colourFor (room.seatPenalty[s]).brighter (0.2f));

            if (room.otherSystem)
            {
                // A band, not a seat: a square, with its frequency above it
                g.fillRect (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
                g.setColour (otherSystemColour.withAlpha (0.8f));
                g.setFont (uiFont (9.0f));
                g.drawText (juce::String (juce::roundToInt (PlaybackSimulator::bandCentreHz[s])), juce::Rectangle<float> (x - 14.0f, y - 17.0f, 28.0f, 10.0f),
                            juce::Justification::centred);
            }
            else
            {
                g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
            }
        }

        // Median seat
        g.setColour (juce::Colours::white);
        g.fillRect (xFor (room.medianDb) - 1.0f, y - 8.0f, 2.0f, 16.0f);
    }
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

void HistoryStrip::pushWave (const WavePoint& point)
{
    if (wave.empty())
        wave.resize ((size_t) columns * wavePointsPerColumn);

    wave[waveWritePos] = point;
    waveWritePos = (waveWritePos + 1) % wave.size();
    waveFilled = std::min (waveFilled + 1, wave.size());
}

void HistoryStrip::clear()
{
    samples.clear();
    writePos = 0;
    wave.clear();
    waveWritePos = waveFilled = 0;
    repaint();
}

void HistoryStrip::setClock (double sinceColumn, double sinceWavePoint, const Sample& current, const WavePoint& currentWave)
{
    columnPhase = sinceColumn;
    wavePhase = sinceWavePoint;
    inProgress = current;
    waveInProgress = currentWave;
    repaint();
}

float HistoryStrip::xForAge (double ageSeconds, juce::Rectangle<float> area)
{
    // Time compressed into the past: the last seconds run fast at the right,
    // the older ones settle slowly towards the left, never quite reaching it.
    const double span = columns * secondsPerColumn;
    const double u = std::log1p (juce::jlimit (0.0, span, ageSeconds) / timeScaleSeconds) / std::log1p (span / timeScaleSeconds);
    return area.getRight() - area.getWidth() * (float) u;
}

void HistoryStrip::paint (juce::Graphics& g)
{
    g.fillAll (theme::plot);
    g.setColour (theme::outline);
    g.fillRect (0, 0, getWidth(), 1);

    auto area = getLocalBounds().toFloat().withTrimmedLeft ((float) FrequencyAxis::leftMargin + 36.0f)
                                          .withTrimmedRight ((float) FrequencyAxis::rightMargin).reduced (0.0f, 5.0f);
    const juce::String names[] = { tr ("Alert"), tr ("Robustness"), tr ("LFE-dependent") };
    const int lanes = 3;
    const float lane = area.getHeight() / (float) lanes;

    // Faint time marks, to read the compressed scale
    g.setFont (uiFont (9.0f));

    for (const auto& mark : { std::make_pair (5.0, "5 s"), std::make_pair (15.0, "15 s"), std::make_pair (30.0, "30 s"),
                              std::make_pair (60.0, "1 min"), std::make_pair (120.0, "2 min") })
    {
        const float x = xForAge (mark.first, area);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRect (x, area.getY(), 1.0f, area.getHeight());
        g.setColour (juce::Colour (0xff5a5a5a));
        g.drawText (mark.second, juce::Rectangle<float> (x - 30.0f, area.getBottom() - 1.0f, 60.0f, 6.0f), juce::Justification::centred);
    }

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

        const size_t count = samples.size();

        for (size_t j = 0; j <= count; ++j)     // j = 0: the column being gathered, then the newest finished one
        {
            const bool live = j == 0;
            const auto& s = live ? inProgress : samples[(writePos + count - j) % count];
            const double age = live ? 0.0 : (double) (j - 1) * secondsPerColumn + columnPhase;
            const float x1 = xForAge (age, area), x0 = xForAge (live ? columnPhase : age + secondsPerColumn, area);

            if (x1 - x0 <= 0.0f)
                continue;

            if (i == 1)
            {
                // Robustness: always coloured while there is bass, green for a
                // sound mix, through orange, to red for a fragile one.
                if (! s.scored)
                    continue;

                const auto red = juce::Colour (0xffff2d2d), orange = juce::Colour (0xffffa31a), green = juce::Colour (0xff2ee86b);
                const auto colour = s.score < 0.5f ? red.interpolatedWith (orange, s.score / 0.5f)
                                                   : orange.interpolatedWith (green, (s.score - 0.5f) / 0.5f);

                // Vivid, with a soft glow spilling over the neighbouring lanes and a bright top edge
                g.setColour (colour.withAlpha (0.10f));
                g.fillRect (x0, row.getY() - 5.0f, x1 - x0 + 0.5f, row.getHeight() + 10.0f);
                g.setColour (colour.withAlpha (0.18f));
                g.fillRect (x0, row.getY() - 2.5f, x1 - x0 + 0.5f, row.getHeight() + 5.0f);
                g.setColour (colour);
                g.fillRect (x0, row.getY(), x1 - x0 + 0.5f, row.getHeight());
                g.setColour (colour.brighter (0.6f).withAlpha (0.7f));
                g.fillRect (x0, row.getY(), x1 - x0 + 0.5f, 1.0f);
                continue;
            }

            const float value = i == 0 ? s.alert : s.dependence;
            const auto colour = i == 0 ? (s.outOfPhase ? juce::Colour (0xffff3a2a) : juce::Colour (0xfff5c400))
                                       : juce::Colour (0xff7e9cff);

            if (value > 0.02f)
            {
                g.setColour (colour.withAlpha (juce::jlimit (0.0f, 1.0f, 0.15f + 0.85f * value)));
                g.fillRect (x0, row.getY(), x1 - x0 + 0.5f, row.getHeight());
            }
        }
    }

    // Over the lanes: what was played, as a DAW draws it, on the same
    // compressed time scale. Signed peaks make the outline, the RMS a core.
    // Linear amplitude, zoomed so the loudest peak in view fills the height
    // (never more than 40 dB of zoom). Kept faint so the alerts read through.
    if (! wave.empty() && waveFilled > 0)
    {
        const size_t count = wave.size();
        float loudest = 0.01f;

        for (const auto& p : wave)
            loudest = juce::jmax (loudest, std::abs (p.lo), std::abs (p.hi));

        const float mid = area.getCentreY(), half = area.getHeight() * 0.48f / loudest;

        // Points from now (k = 0, the one being gathered) back to the first sound
        std::vector<std::pair<float, HistoryStrip::WavePoint>> points;
        points.reserve (waveFilled + 1);

        for (size_t k = 0; k <= waveFilled; ++k)
        {
            auto p = k == 0 ? waveInProgress : wave[(waveWritePos + count - k) % count];

            // The point being gathered may hold only a few milliseconds yet:
            // it starts from the last finished one, so the outline never dips at now
            if (k == 0)
            {
                const auto& last = wave[(waveWritePos + count - 1) % count];
                p.lo = juce::jmin (p.lo, last.lo);
                p.hi = juce::jmax (p.hi, last.hi);
                p.rms = juce::jmax (p.rms, last.rms);
            }

            const double age = k == 0 ? 0.0 : ((double) k - 0.5) * secondsPerWavePoint + wavePhase;
            points.emplace_back (xForAge (age, area), p);
        }

        juce::Path peaks, core;

        for (int pass = 0; pass < 2; ++pass)
        {
            auto& path = pass == 0 ? peaks : core;

            for (size_t i = 0; i < points.size(); ++i)
            {
                const auto& p = points[i].second;
                const float y = mid - half * (pass == 0 ? p.hi : p.rms);

                if (i == 0)     path.startNewSubPath (points[i].first, y);
                else            path.lineTo (points[i].first, y);
            }

            for (size_t i = points.size(); i-- > 0;)
            {
                const auto& p = points[i].second;
                path.lineTo (points[i].first, mid - half * (pass == 0 ? p.lo : -p.rms));
            }

            path.closeSubPath();
        }

        g.setColour (juce::Colours::white.withAlpha (0.40f));
        g.fillPath (peaks);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillPath (core);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (area.getX(), mid - 0.5f, area.getWidth(), 1.0f);       // the zero line
    }

    g.setColour (juce::Colour (0xff777777));
    g.setFont (uiFont (10.0f));
    g.drawText (tr ("now"), juce::Rectangle<float> (area.getRight() + 1.0f, area.getY(), 22.0f, 12.0f), juce::Justification::centredLeft);
}

} // namespace subguard
