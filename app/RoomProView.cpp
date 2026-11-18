#include "RoomProView.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace subguard
{

namespace
{
    double now()    { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    const juce::Colour speakerColour { 0xfff2f2f2 }, subColour { 0xffff9a2e }, fillColour { 0xff46d0ff };
}

//==============================================================================
// 3D view
void Room3DView::setRoom (const ProRoom& r, const std::vector<double>& seatPenalties, const std::vector<double>& seatValues,
                          bool absoluteValues, double mix, const std::vector<double>& fade)
{
    if (room == r && penalties == seatPenalties && changes == seatValues && absolute == absoluteValues && same (mixValue, mix) && fades == fade)
        return;

    room = r;
    penalties = seatPenalties;
    changes = seatValues;
    absolute = absoluteValues;
    mixValue = mix;
    fades = fade;
    repaint();
}

juce::Colour Room3DView::seatColour (double score)
{
    // Many steps over the range where seats actually fall, so that seats a few points apart look different
    struct Stop { double score; juce::uint32 argb; };
    static const Stop stops[] = { { 0.0, 0xff5a1a8a }, { 40.0, 0xffa8227a }, { 55.0, 0xffe02c3c }, { 65.0, 0xfff25a2a },
                                  { 75.0, 0xfff79a2e }, { 82.0, 0xfff5cf35 }, { 88.0, 0xffcde23a }, { 93.0, 0xff86e04a },
                                  { 97.0, 0xff52d33f }, { 100.0, 0xff22c33a } };     // 100: classic green
    score = juce::jlimit (0.0, 100.0, score);

    for (size_t i = 1; i < std::size (stops); ++i)
        if (score <= stops[i].score)
        {
            const double t = (score - stops[i - 1].score) / (stops[i].score - stops[i - 1].score);
            return juce::Colour (stops[i - 1].argb).interpolatedWith (juce::Colour (stops[i].argb), (float) t);
        }

    return juce::Colour (stops[std::size (stops) - 1].argb);
}

juce::Point<float> projectRoomPoint (const ProRoom& room, const ProCamera& camera, int width, int height, double x, double y, double z, float* depth)
{
    // View space: X across, Y up, Z along the room (front wall far away, at -L/2)
    const double X = y - room.width * 0.5, Y = z - room.height * 0.5, Z = x - room.length * 0.5;

    const double cy = std::cos (camera.yaw), sy = std::sin (camera.yaw), cp = std::cos (camera.pitch), sp = std::sin (camera.pitch);
    const double x1 = X * cy + Z * sy, z1 = -X * sy + Z * cy;
    const double y2 = Y * cp - z1 * sp, z2 = Y * sp + z1 * cp;

    const double size = std::max ({ room.length, room.width, room.height, 1.0 });
    const double distance = 1.6 * size * camera.zoom;
    const double d = std::max (0.2, distance - z2);
    const double focal = 0.75 * std::min (width, height) * 1.6;

    if (depth != nullptr)
        *depth = (float) d;

    return { (float) (width * 0.5 + focal * x1 / d), (float) (height * 0.5 - focal * y2 / d) };
}

void Room3DView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1c20));

    const double L = room.length, W = room.width, H = room.height;
    const bool round = room.shape != ProRoom::rectangle;     // any floor but the rectangle: drawn from its outline
    using Corner = std::array<double, 3>;
    struct Face { std::vector<Corner> corners; float alpha; float depth; bool screen; };
    std::vector<Face> faces;

    if (round)
    {
        // Floor, ceiling and the wall in segments along the outline; the front (x near 0) lighter, with the screen when there is one
        const auto outline = room.boundaryPoints (room.shape == ProRoom::lShape ? 6 * 12 : 64);
        const int segments = (int) outline.size();
        const auto at = [&] (int k, double z) { const auto& p = outline[(size_t) (k % segments)]; return Corner { p[0], p[1], z }; };
        Face floor { {}, 0.30f, 0.0f, false }, ceiling { {}, 0.14f, 0.0f, false };

        for (int k = 0; k < segments; ++k)
        {
            floor.corners.push_back (at (k, 0.0));
            ceiling.corners.push_back (at (k, H));
            const auto a0 = at (k, 0.0), a1 = at (k + 1, 0.0);
            const bool front = std::max (a0[0], a1[0]) < 0.12 * L + 0.3;
            faces.push_back ({ { at (k, 0.0), at (k + 1, 0.0), at (k + 1, H), at (k, H) }, front ? 0.26f : 0.12f, 0.0f, false });

            if (front && room.screen)
                faces.push_back ({ { at (k, H * 0.32), at (k + 1, H * 0.32), at (k + 1, H * 0.82), at (k, H * 0.82) }, 0.0f, 0.0f, true });
        }

        faces.push_back (floor);
        faces.push_back (ceiling);
    }
    else
    {
        faces = {
            { { { 0, 0, 0 }, { 0, W, 0 }, { L, W, 0 }, { L, 0, 0 } }, 0.30f, 0.0f, false },     // floor
            { { { 0, 0, H }, { 0, W, H }, { L, W, H }, { L, 0, H } }, 0.14f, 0.0f, false },     // ceiling
            { { { 0, 0, 0 }, { 0, W, 0 }, { 0, W, H }, { 0, 0, H } }, 0.26f, 0.0f, false },     // front wall
            { { { L, 0, 0 }, { L, W, 0 }, { L, W, H }, { L, 0, H } }, 0.05f, 0.0f, false },     // back wall, nearly open
            { { { 0, 0, 0 }, { L, 0, 0 }, { L, 0, H }, { 0, 0, H } }, 0.22f, 0.0f, false },     // left wall
            { { { 0, W, 0 }, { L, W, 0 }, { L, W, H }, { 0, W, H } }, 0.22f, 0.0f, false },     // right wall
        };

        if (room.screen)
            faces.push_back ({ { { 0.02, W * 0.12, H * 0.32 }, { 0.02, W * 0.88, H * 0.32 }, { 0.02, W * 0.88, H * 0.82 }, { 0.02, W * 0.12, H * 0.82 } },
                               0.0f, 0.0f, true });                                               // the screen
    }

    for (auto& f : faces)
    {
        float sum = 0.0f;

        for (const auto& c : f.corners)
        {
            float d = 0.0f;
            project (c[0], c[1], c[2], &d);
            sum += d;
        }

        f.depth = sum / (float) f.corners.size() - (f.screen ? 0.05f : 0.0f);
    }

    std::stable_sort (faces.begin(), faces.end(), [] (const Face& a, const Face& b) { return a.depth > b.depth; });

    for (const auto& f : faces)
    {
        juce::Path p;

        for (size_t i = 0; i < f.corners.size(); ++i)
        {
            const auto pt = project (f.corners[i][0], f.corners[i][1], f.corners[i][2]);

            if (i == 0)     p.startNewSubPath (pt);
            else            p.lineTo (pt);
        }

        p.closeSubPath();
        const auto bounds = p.getBounds();

        if (f.screen)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.42f), bounds.getX(), bounds.getY(),
                                                     juce::Colours::white.withAlpha (0.22f), bounds.getRight(), bounds.getBottom(), false));
            g.fillPath (p);
            continue;
        }

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9a9da6).withAlpha (f.alpha), bounds.getCentreX(), bounds.getY(),
                                                 juce::Colour (0xff5d6068).withAlpha (f.alpha), bounds.getCentreX(), bounds.getBottom(), false));
        g.fillPath (p);

        if (! round || f.corners.size() > 4)
        {
            g.setColour (juce::Colours::black.withAlpha (0.75f));
            g.strokePath (p, juce::PathStrokeType (1.0f));
        }
    }

    // Speakers and seats, the farthest first
    struct Mark { float depth; juce::Point<float> at; juce::Colour colour; juce::String label; bool square; int seat; };
    std::vector<Mark> marks;

    const auto addSpeaker = [&] (const ProSpeaker& s, juce::Colour colour, const juce::String& label, bool mirror)
    {
        if (! s.enabled)
            return;

        float d = 0.0f;
        auto p = room.positionOf (s);

        if (mirror)
            p = { L - p[0], W - p[1], p[2] };

        p = room.keptInside (p);
        const auto at = project (p[0], p[1], p[2], &d);
        marks.push_back ({ d, at, colour, label, true, -1 });
    };

    const char* lcrNames[] = { "L", "C", "R" };

    for (int i = 0; i < 3; ++i)
    {
        addSpeaker (room.lcr[(size_t) i], speakerColour, lcrNames[i], false);

        if (room.dualLCR)
            addSpeaker (room.lcr[(size_t) i], speakerColour.darker (0.25f), juce::String (lcrNames[i]) + "2", true);
    }

    for (int i = 0; i < std::clamp (room.numSubs, 1, ProRoom::maxSubs); ++i)
        addSpeaker (room.subs[(size_t) i], subColour, "S" + juce::String (i + 1), false);

    if (room.fillsOn)
        for (int i = 0; i < std::clamp (room.numFills, 1, ProRoom::maxFills); ++i)
            addSpeaker (room.fills[(size_t) i], fillColour, "F" + juce::String (i + 1), false);

    const auto seats = room.seatPositions();
    const bool rated = penalties.size() == seats.size();
    seatPoints.assign (seats.size(), {});

    for (size_t i = 0; i < seats.size(); ++i)
    {
        float d = 0.0f;
        const auto at = project (seats[i][0], seats[i][1], seats[i][2], &d);
        seatPoints[i] = at;
        // The head's colour: how loud its bass is (on the mix's scale), or a change against the
        // approved mix (grey: as approved; blue: a dip; red: a boost), greyer the less the bass is heard
        const bool hasValue = rated && i < changes.size() && ! std::isnan (changes[i]);
        auto colour = juce::Colour (0xff4a4a4a);

        if (hasValue)
            colour = absolute ? loudnessColour (changes[i])
                              : juce::Colour (0xff8a8a8a).interpolatedWith (bassChangeColour (changes[i]),
                                                                           i < fades.size() ? (float) juce::jlimit (0.0, 1.0, fades[i]) : 1.0f);
        marks.push_back ({ d, at, colour, {}, false, (int) i });
    }

    std::sort (marks.begin(), marks.end(), [] (const Mark& a, const Mark& b) { return a.depth > b.depth; });

    const float focal = 0.75f * (float) std::min (getWidth(), getHeight()) * 1.6f;

    for (const auto& m : marks)
    {
        const float scale = focal / m.depth;

        if (m.square)
        {
            const float s = juce::jlimit (6.0f, 22.0f, (float) (0.7 * scale));
            const float w = s * (m.label.length() > 2 ? 1.5f : 1.0f);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (m.at.x - w * 0.5f + 1.5f, m.at.y - s * 0.5f + 1.5f, w, s, 2.0f);
            g.setColour (m.colour);
            g.fillRoundedRectangle (m.at.x - w * 0.5f, m.at.y - s * 0.5f, w, s, 2.0f);
            g.setColour (juce::Colours::black);
            g.setFont (uiFont (juce::jmax (8.0f, s * 0.55f), true));
            g.drawText (m.label, juce::Rectangle<float> (m.at.x - w, m.at.y - s * 0.5f, 2.0f * w, s), juce::Justification::centred);
        }
        else
        {
            const float r = juce::jlimit (2.0f, 7.0f, (float) (0.28 * scale));
            g.setColour (m.colour.withAlpha (0.16f));
            g.fillEllipse (m.at.x - 1.6f * r, m.at.y - 1.6f * r, 3.2f * r, 3.2f * r);
            g.setColour (m.colour);
            g.fillEllipse (m.at.x - r, m.at.y - r, 2.0f * r, 2.0f * r);

            if (m.seat == hovered)
            {
                g.setColour (juce::Colours::white);
                g.drawEllipse (m.at.x - r - 2.0f, m.at.y - r - 2.0f, 2.0f * r + 4.0f, 2.0f * r + 4.0f, 1.5f);
            }
        }
    }

    // The worst point found between the seats
    if (worstSpot.has_value())
    {
        const auto& w = *worstSpot;
        const auto at = project (w[0], w[1], w[2]);
        const auto colour = juce::Colour (0xffff5ad2);
        g.setColour (colour.withAlpha (0.25f));
        g.fillEllipse (at.x - 11.0f, at.y - 11.0f, 22.0f, 22.0f);
        g.setColour (colour);
        g.drawEllipse (at.x - 7.0f, at.y - 7.0f, 14.0f, 14.0f, 2.0f);
        g.setFont (uiFont (10.5f, true));
        g.drawText (tr ("worst point ") + juce::String (juce::roundToInt (worstSpotScore)) + " %",
                    juce::Rectangle<float> (at.x + 10.0f, at.y - 8.0f, 140.0f, 16.0f), juce::Justification::centredLeft);
    }

    // The heads' colour scale, as in the layered view
    auto legend = getLocalBounds().reduced (10, 8).removeFromBottom (36).removeFromRight (300).toFloat();
    drawScale (g, legend, absolute, mixValue, caption);

    // The head under the mouse: two lines
    juce::String text, second;

    if (hovered >= 0 && hovered < (int) seatPoints.size() && rated)
    {
        const auto h = (size_t) hovered;
        const auto value = [h] (const std::vector<double>& v) { return h < v.size() ? v[h] : std::nan (""); };
        const auto signedDb = [] (double v) { return std::isnan (v) ? juce::String ("--") : (v >= 0.0 ? "+" : "") + juce::String (v, 1) + " dB"; };
        const auto phon = [] (double v) { return std::isnan (v) ? juce::String ("--") : juce::String (juce::roundToInt (v)); };

        text = tr ("Seat ") + juce::String (hovered + 1) + ":  " + juce::String (juce::roundToInt (100.0 * (1.0 - penalties[h]))) + " %   "
             + phon (value (details.phon[RoomResult::allParts])) + tr (" phon (mix ") + phon (details.mixPhon[RoomResult::allParts]) + ")   L C R "
             + phon (value (details.phon[RoomResult::lcrPart])) + "  LFE " + phon (value (details.phon[RoomResult::lfePart]));

        second = tr ("bass ") + signedDb (value (details.partDb[RoomResult::allParts])) + " = " + tr ("room ")
               + signedDb (value (details.causeDb[RoomResult::roomCause])) + "  " + tr ("speakers ")
               + signedDb (value (details.causeDb[RoomResult::speakersCause])) + "  " + tr ("mix ")
               + signedDb (value (details.causeDb[RoomResult::mixCause]));

        if (h < heard.size() && h < felt.size())
            second << "   " << tr ("feels ") << juce::String (juce::roundToInt (felt[h])) << tr (" dB SPL under 40 Hz");
        const auto at = seatPoints[h];
        const float width = 16.0f + juce::jmax (juce::GlyphArrangement::getStringWidth (uiFont (11.5f, true), text),
                                                juce::GlyphArrangement::getStringWidth (uiFont (11.0f), second));
        auto box = juce::Rectangle<float> (at.x + 12.0f, at.y - 44.0f, width, 38.0f);
        box = box.constrainedWithin (getLocalBounds().toFloat().reduced (4.0f));
        g.setColour (juce::Colour (0xee0e0f11));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (seatColour (100.0 * (1.0 - penalties[h])));
        g.drawRoundedRectangle (box, 4.0f, 1.0f);
        auto lines = box.reduced (8.0f, 3.0f);
        g.setColour (juce::Colours::white);
        g.setFont (uiFont (11.5f, true));
        g.drawText (text, lines.removeFromTop (lines.getHeight() / 2.0f), juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xffd0d0d0));
        g.setFont (uiFont (11.0f));
        g.drawText (second, lines, juce::Justification::centredLeft);
    }

    g.setColour (theme::dimText);
    g.setFont (uiFont (10.5f));
    g.drawText (tr ("Drag to turn, wheel to zoom, double-click to reset. Point at a head for its details.  White: L C R   Orange: subwoofers   Blue: fills"),
                getLocalBounds().reduced (8, 4).removeFromTop (14), juce::Justification::centredLeft);
}

void Room3DView::mouseMove (const juce::MouseEvent& e)
{
    int nearest = -1;
    float best = 12.0f;

    for (size_t i = 0; i < seatPoints.size(); ++i)
    {
        const float d = seatPoints[i].getDistanceFrom (e.position);

        if (d < best)
        {
            best = d;
            nearest = (int) i;
        }
    }

    if (nearest != hovered)
    {
        hovered = nearest;
        repaint();

        if (onHover)
            onHover (hovered);
    }
}

void Room3DView::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();

    if (onHover)
        onHover (-1);
}

namespace
{
    // Turning, zooming and resetting the shared camera, from either view
    void turnCamera (ProCamera& camera, double yawAtDrag, double pitchAtDrag, const juce::MouseEvent& e)
    {
        camera.yaw = yawAtDrag + e.getDistanceFromDragStartX() * 0.008;
        camera.pitch = juce::jlimit (-1.4, 1.4, pitchAtDrag + e.getDistanceFromDragStartY() * 0.008);
    }

    void zoomCamera (ProCamera& camera, const juce::MouseWheelDetails& wheel)
    {
        camera.zoom = juce::jlimit (0.5, 3.0, camera.zoom * (wheel.deltaY > 0 ? 0.92 : 1.08));
    }
}

void Room3DView::mouseDown (const juce::MouseEvent&)
{
    yawAtDrag = camera->yaw;
    pitchAtDrag = camera->pitch;
}

void Room3DView::mouseDrag (const juce::MouseEvent& e)
{
    turnCamera (*camera, yawAtDrag, pitchAtDrag, e);
    repaint();

    if (onCameraChanged)
        onCameraChanged();
}

void Room3DView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    zoomCamera (*camera, wheel);
    repaint();

    if (onCameraChanged)
        onCameraChanged();
}

void Room3DView::mouseDoubleClick (const juce::MouseEvent&)
{
    *camera = ProCamera();
    repaint();

    if (onCameraChanged)
        onCameraChanged();
}

//==============================================================================
// Layered view
juce::Colour bassChangeColour (double db)
{
    // Grey within 0.5 dB, then straight into saturated colours, so that grey never dilutes them;
    // full at 6 dB, about where the loudness of the bass doubles (the full penalty of a seat)
    const double a = std::abs (db);
    const juce::Colour grey (0xff8a8a8a);
    const juce::Colour dip[] = { juce::Colour (0xff35a2ff), juce::Colour (0xff1c3dff), juce::Colour (0xff7a1fff) };      // bright blue, deep blue, violet
    const juce::Colour boost[] = { juce::Colour (0xffffd400), juce::Colour (0xffff6c00), juce::Colour (0xffff1414) };    // yellow, orange, red
    const auto& ramp = db < 0.0 ? dip : boost;

    if (a <= 0.5)   return grey;
    if (a < 1.5)    return grey.interpolatedWith (ramp[0], (float) (a - 0.5));
    if (a < 3.5)    return ramp[0].interpolatedWith (ramp[1], (float) ((a - 1.5) / 2.0));
    if (a < 6.0)    return ramp[1].interpolatedWith (ramp[2], (float) ((a - 3.5) / 2.5));
    return ramp[2];
}

juce::Colour loudnessColour (double phon)
{
    // A heat scale of loudness: about 30 phon is blue, 60 green, 80 orange, 90 and above red to magenta
    struct Stop { double phon; juce::uint32 argb; };
    static const Stop stops[] = { { 0.0, 0xff2b2b30 }, { 15.0, 0xff1b2f8a }, { 30.0, 0xff1f5bff }, { 45.0, 0xff17b8e8 },
                                  { 60.0, 0xff2fd36b }, { 70.0, 0xffd9e021 }, { 80.0, 0xffff8a00 }, { 90.0, 0xffff2a2a },
                                  { 100.0, 0xffff3ad6 } };
    phon = juce::jlimit (0.0, 100.0, phon);

    for (size_t i = 1; i < std::size (stops); ++i)
        if (phon <= stops[i].phon)
            return juce::Colour (stops[i - 1].argb).interpolatedWith (juce::Colour (stops[i].argb),
                                                                     (float) ((phon - stops[i - 1].phon) / (stops[i].phon - stops[i - 1].phon)));

    return juce::Colour (stops[std::size (stops) - 1].argb);
}

void drawScale (juce::Graphics& g, juce::Rectangle<float> area, bool absolute, double mixValue, const juce::String& caption)
{
    // A loudness scale 0..100 phon with the mix's own value marked, or a change scale -9..+9 dB
    auto bar = area.removeFromTop (10.0f);

    for (int x = 0; x < (int) bar.getWidth(); ++x)
    {
        const double t = x / (double) bar.getWidth();
        g.setColour (absolute ? loudnessColour (100.0 * t) : bassChangeColour (-9.0 + 18.0 * t));
        g.fillRect (bar.getX() + (float) x, bar.getY(), 1.0f, bar.getHeight());
    }

    g.setColour (theme::dimText);
    g.setFont (uiFont (10.0f));

    if (absolute)
    {
        for (int tick : { 0, 20, 40, 60, 80, 100 })
            g.drawText (juce::String (tick), juce::Rectangle<float> (bar.getX() + bar.getWidth() * (float) tick / 100.0f - 16.0f,
                                                                    bar.getBottom() + 1.0f, 32.0f, 12.0f), juce::Justification::centred);

        // The mix as approved: a white mark on the scale
        const float x = bar.getX() + bar.getWidth() * (float) juce::jlimit (0.0, 1.0, mixValue / 100.0);
        g.setColour (juce::Colours::white);
        g.fillRect (x - 1.0f, bar.getY() - 3.0f, 2.0f, bar.getHeight() + 6.0f);
        g.setFont (uiFont (9.5f, true));
        g.drawText (tr ("mix ") + juce::String (juce::roundToInt (mixValue)), juce::Rectangle<float> (x - 30.0f, bar.getY() - 14.0f, 60.0f, 11.0f),
                    juce::Justification::centred);
    }
    else
    {
        for (int tick : { -9, -6, -3, 0, 3, 6, 9 })
            g.drawText ((tick > 0 ? "+" : "") + juce::String (tick), juce::Rectangle<float> (bar.getX() + bar.getWidth() * (float) (tick + 9) / 18.0f - 16.0f,
                                                                                               bar.getBottom() + 1.0f, 32.0f, 12.0f),
                        juce::Justification::centred);
    }

    g.setColour (theme::dimText);
    g.setFont (uiFont (10.0f));
    g.drawText (caption, area.withTrimmedTop (4.0f), juce::Justification::centredLeft);
}

RoomLayerView::RoomLayerView()
{
    // Layer spacing: 1 is the first version (layers 30 % of the room height apart), 3 by default
    spacing.setSliderStyle (juce::Slider::LinearHorizontal);
    spacing.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
    spacing.setRange (0.5, 6.0, 0.1);
    spacing.setValue (3.0, juce::dontSendNotification);
    spacing.setTextValueSuffix (juce::String::fromUTF8 (" \xc3\x97"));
    spacing.setTooltip (tr ("How far apart the three layers are."));
    spacing.onValueChange = [this] { repaint(); };
    spacingLabel.setText (tr ("Layer spacing"), juce::dontSendNotification);
    spacingLabel.setFont (uiFont (10.5f));
    spacingLabel.setColour (juce::Label::textColourId, theme::dimText);
    spacingLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (spacing);
    addAndMakeVisible (spacingLabel);

    // Amplitude: how far a dB moves a layer; 1 is the first version (12 dB = 11 % of the room height), 3 by default
    amplitude.setSliderStyle (juce::Slider::LinearHorizontal);
    amplitude.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
    amplitude.setRange (0.5, 10.0, 0.1);
    amplitude.setValue (3.0, juce::dontSendNotification);
    amplitude.setTextValueSuffix (juce::String::fromUTF8 (" \xc3\x97"));
    amplitude.setTooltip (tr ("How far a boost lifts a layer and a dip lowers it."));
    amplitude.onValueChange = [this] { repaint(); };
    amplitudeLabel.setText (tr ("Amplitude"), juce::dontSendNotification);
    amplitudeLabel.setFont (uiFont (10.5f));
    amplitudeLabel.setColour (juce::Label::textColourId, theme::dimText);
    amplitudeLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (amplitude);
    addAndMakeVisible (amplitudeLabel);
}

void RoomLayerView::resized()
{
    auto area = getLocalBounds().reduced (8, 4);
    area.removeFromTop (16);       // the caption line
    auto row = area.removeFromTop (20);
    spacing.setBounds (row.removeFromRight (150));
    spacingLabel.setBounds (row.removeFromRight (100));
    row.removeFromRight (8);
    amplitude.setBounds (row.removeFromRight (150));
    amplitudeLabel.setBounds (row.removeFromRight (70));
}

void RoomLayerView::setData (const ProRoom& r, const RoomPaths* paths, const RoomResult* result, int lensShown, int shown)
{
    room = r;
    lens = juce::jlimit (0, RoomResult::numLenses - 1, lensShown);
    which = juce::jlimit (0, 2, shown);
    hasData = paths != nullptr && result != nullptr && paths->gridPoints > 0
                && (int) result->gridLayerPhon[(size_t) which][0].size() == paths->gridPoints
                && (int) result->gridCauseLayerDb[(size_t) which][0].size() == paths->gridPoints;

    if (hasData)
    {
        columns = paths->gridColumns;
        rows = paths->gridRows;
        index = paths->gridIndex;
        nodes = paths->gridPositions;
        const bool loudness = lens == RoomResult::coverageLens;
        layers = loudness ? result->gridLayerPhon[(size_t) which] : result->gridCauseLayerDb[(size_t) which];
        seatLayers = loudness ? result->seatLayerPhon[(size_t) which] : result->seatCauseLayerDb[(size_t) which];
        neutral = loudness ? result->approvedLayerPhon[(size_t) which] : std::array<double, RoomResult::numLayers> {};
        fade = result->layerHeard;
        mixValue = result->approvedPhon[(size_t) which];
        seats = r.seatPositions();
    }

    repaint();
}

void RoomLayerView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff121315));

    const juce::String partNames[] = { tr ("all of it"), "L C R", "LFE" };
    const juce::String causeNames[] = { tr ("the room"), tr ("the speakers"), tr ("the mix") };
    const bool loudness = lens == RoomResult::coverageLens;
    const juce::String caption = loudness
                                   ? tr ("Loudness of the bass (phon; infra: dB above the threshold)") + " (" + partNames[which] + ")"
                                   : tr ("What changes the bass against the approved mix: ") + causeNames[which];
    g.setColour (theme::dimText);
    g.setFont (uiFont (10.5f));
    g.drawText (caption + ". " + tr ("Drag to turn, wheel to zoom, double-click to reset."),
                getLocalBounds().reduced (8, 4).removeFromTop (14), juce::Justification::centredLeft);

    // The layers 30 % of the room height apart times the spacing setting
    const double roomHeight = room.height, gap = 0.3 * roomHeight * spacing.getValue();
    const double stackHeight = 0.4 * roomHeight + 2.0 * gap;
    const auto baseOf = [&] (int l) { return 0.2 * roomHeight + gap * l; };

    // Height of a change: 12 dB move a layer by 11 % of the room height at an amplitude of 1, with a soft
    // limit (tanh) so that a large boost or dip cannot throw a layer out of the room
    const double scale = 0.11 * roomHeight / 12.0 * amplitude.getValue();
    const auto lift = [scale] (double db) { return scale * 12.0 * std::tanh (db / 12.0); };

    // Loudness: the height is the gap to the mix's own loudness in that layer, the colour the loudness itself.
    // Causes: the change in dB; both fade to grey and flat as the mix's bass in that layer is less likely heard.
    const auto heightOf = [&] (int l, double v) { return loudness ? lift (v - neutral[(size_t) l]) : lift (v * fade[(size_t) l]); };
    const auto colourOf = [&] (int l, double v)
    {
        return loudness ? loudnessColour (v)
                        : juce::Colour (0xff8a8a8a).interpolatedWith (bassChangeColour (v), (float) juce::jlimit (0.0, 1.0, fade[(size_t) l]));
    };

    const juce::String names[] = { tr ("Infra, under 20 Hz (not heard, felt)"), tr ("Very low, 20-60 Hz"), tr ("Low, 60-120 Hz") };
    const auto outline = room.boundaryPoints (room.shape == ProRoom::rectangle ? 4 : 64);

    // The nearest point of the wall, at floor level: where each layer is pinned
    const auto wallLine = room.boundaryPoints (room.shape == ProRoom::rectangle ? 4 : 128);
    const auto wallPoint = [&wallLine] (double x, double y)
    {
        std::array<double, 2> best { x, y };
        double bestDistance = 1e300;

        for (size_t k = 0; k < wallLine.size(); ++k)
        {
            const auto& a = wallLine[k];
            const auto& b = wallLine[(k + 1) % wallLine.size()];
            const double dx = b[0] - a[0], dy = b[1] - a[1], length2 = dx * dx + dy * dy;
            const double t = length2 > 0.0 ? std::clamp (((x - a[0]) * dx + (y - a[1]) * dy) / length2, 0.0, 1.0) : 0.0;
            const double px = a[0] + t * dx, py = a[1] + t * dy, d = (px - x) * (px - x) + (py - y) * (py - y);

            if (d < bestDistance)
            {
                bestDistance = d;
                best = { px, py };
            }
        }

        return best;
    };

    // The geometry first, in room coordinates; the frame is then fitted around all of it
    using Point3 = std::array<double, 3>;
    struct Quad { std::array<Point3, 4> corners; juce::Colour colour; bool skirt; float depth = 0.0f; };
    std::vector<Quad> quads;
    std::array<bool, RoomResult::numLayers> any {};
    double lowest = 0.0, highest = stackHeight;

    if (hasData)
    {
        constexpr int subdivision = 3;

        for (int l = 0; l < RoomResult::numLayers; ++l)
        {
            const double base = baseOf (l);
            const auto& values = layers[(size_t) l];

            const auto valueAt = [&] (int column, int row, double& db)
            {
                if (column < 0 || row < 0 || column >= columns || row >= rows)
                    return false;

                const int k = index[(size_t) (row * columns + column)];

                if (k < 0 || std::isnan (values[(size_t) k]))
                    return false;

                db = juce::jlimit (loudness ? 0.0 : -24.0, loudness ? 120.0 : 24.0, values[(size_t) k]);
                return true;
            };

            const auto cellExists = [&] (int column, int row)
            {
                double v;
                return valueAt (column, row, v) && valueAt (column + 1, row, v) && valueAt (column + 1, row + 1, v) && valueAt (column, row + 1, v);
            };

            const auto nodeAt = [&] (int column, int row) { return nodes[(size_t) index[(size_t) (row * columns + column)]]; };

            const auto addPoint = [&] (const Point3& p)
            {
                lowest = std::min (lowest, p[2]);
                highest = std::max (highest, p[2]);
            };

            for (int row = 0; row + 1 < rows; ++row)
                for (int column = 0; column + 1 < columns; ++column)
                {
                    if (! cellExists (column, row))
                        continue;

                    any[(size_t) l] = true;
                    double v[4];
                    valueAt (column, row, v[0]);
                    valueAt (column + 1, row, v[1]);
                    valueAt (column + 1, row + 1, v[2]);
                    valueAt (column, row + 1, v[3]);
                    const auto p0 = nodeAt (column, row), p2 = nodeAt (column + 1, row + 1);

                    // Bilinear inside the cell
                    const auto at = [&] (double u, double w, double& db)
                    {
                        db = (1 - u) * (1 - w) * v[0] + u * (1 - w) * v[1] + u * w * v[2] + (1 - u) * w * v[3];
                        return Point3 { p0[0] + u * (p2[0] - p0[0]), p0[1] + w * (p2[1] - p0[1]), base + heightOf (l, db) };
                    };

                    for (int sy = 0; sy < subdivision; ++sy)
                        for (int sx = 0; sx < subdivision; ++sx)
                        {
                            const double u0 = sx / (double) subdivision, u1 = (sx + 1) / (double) subdivision;
                            const double w0 = sy / (double) subdivision, w1 = (sy + 1) / (double) subdivision;
                            double d[4];
                            Quad q { { at (u0, w0, d[0]), at (u1, w0, d[1]), at (u1, w1, d[2]), at (u0, w1, d[3]) },
                                     colourOf (l, 0.25 * (d[0] + d[1] + d[2] + d[3])), false };

                            for (const auto& c : q.corners)
                                addPoint (c);

                            quads.push_back (q);
                        }

                    // Where the mesh ends, a skirt down (or up) to the wall at the layer's neutral height:
                    // the layer stays pinned to the room, whatever it does in the middle
                    const auto skirt = [&] (double ua, double wa, double ub, double wb)
                    {
                        for (int s = 0; s < subdivision; ++s)
                        {
                            const double t0 = s / (double) subdivision, t1 = (s + 1) / (double) subdivision;
                            double da, db;
                            const auto a = at (ua + t0 * (ub - ua), wa + t0 * (wb - wa), da);
                            const auto b = at (ua + t1 * (ub - ua), wa + t1 * (wb - wa), db);
                            const auto wallA = wallPoint (a[0], a[1]), wallB = wallPoint (b[0], b[1]);
                            Quad q { { a, b, Point3 { wallB[0], wallB[1], base }, Point3 { wallA[0], wallA[1], base } },
                                     colourOf (l, 0.5 * (da + db)), true };

                            for (const auto& c : q.corners)
                                addPoint (c);

                            quads.push_back (q);
                        }
                    };

                    if (! cellExists (column, row - 1))     skirt (0.0, 0.0, 1.0, 0.0);
                    if (! cellExists (column, row + 1))     skirt (0.0, 1.0, 1.0, 1.0);
                    if (! cellExists (column - 1, row))     skirt (0.0, 0.0, 0.0, 1.0);
                    if (! cellExists (column + 1, row))     skirt (1.0, 0.0, 1.0, 1.0);
                }
        }
    }

    // The frame: from the lowest to the highest point drawn, so nothing leaves the view
    ProRoom stage = room;
    stage.height = highest - lowest;
    const auto project = [this, stage, lowest] (double x, double y, double z, float* depth = nullptr)
    {
        return projectRoomPoint (stage, camera, getWidth(), getHeight(), x, y, z - lowest, depth);
    };

    // The room as a thin wireframe: its outline at the bottom and the top, and the corners between
    {
        juce::Path frame;

        for (double z : { lowest, highest })
            for (size_t k = 0; k <= outline.size(); ++k)
            {
                const auto& p = outline[k % outline.size()];
                const auto at = project (p[0], p[1], z);

                if (k == 0)     frame.startNewSubPath (at);
                else            frame.lineTo (at);
            }

        const size_t step = room.shape == ProRoom::rectangle ? 1 : outline.size() / 8;

        for (size_t k = 0; k < outline.size(); k += std::max<size_t> (1, step))
        {
            frame.startNewSubPath (project (outline[k][0], outline[k][1], lowest));
            frame.lineTo (project (outline[k][0], outline[k][1], highest));
        }

        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.strokePath (frame, juce::PathStrokeType (0.8f));
    }

    if (! hasData)
    {
        g.setColour (theme::dimText);
        g.setFont (uiFont (12.0f));
        g.drawText (tr ("computing..."), getLocalBounds(), juce::Justification::centred);
        return;
    }

    for (int l = 0; l < RoomResult::numLayers; ++l)
    {
        // The neutral plane, a thin grey outline at the layer's own height: where the mix as approved sits
        juce::Path plane;

        for (size_t k = 0; k <= outline.size(); ++k)
        {
            const auto& p = outline[k % outline.size()];
            const auto at = project (p[0], p[1], baseOf (l));

            if (k == 0)     plane.startNewSubPath (at);
            else            plane.lineTo (at);
        }

        g.setColour (juce::Colour (0xff8c8c8c).withAlpha (0.75f));
        g.strokePath (plane, juce::PathStrokeType (1.0f));

        // The layer's name at its left edge
        const auto label = project (0.0, 0.0, baseOf (l));
        g.setColour (any[(size_t) l] ? juce::Colour (0xffdedede) : theme::dimText);
        g.setFont (uiFont (10.5f, true));
        g.drawText (any[(size_t) l] ? names[l] : names[l] + "  -  " + tr ("nothing in the mix"),
                    juce::Rectangle<float> (label.x - 4.0f, label.y - 16.0f, 260.0f, 14.0f), juce::Justification::centredLeft);
    }

    // The faces, the farthest first; opaque, so that a layer's colour is not washed out by the ones behind
    for (auto& q : quads)
    {
        float sum = 0.0f;

        for (const auto& c : q.corners)
        {
            float d = 0.0f;
            project (c[0], c[1], c[2], &d);
            sum += d;
        }

        q.depth = 0.25f * sum;
    }

    std::sort (quads.begin(), quads.end(), [] (const Quad& x, const Quad& y) { return x.depth > y.depth; });

    for (const auto& q : quads)
    {
        juce::Path path;
        path.startNewSubPath (project (q.corners[0][0], q.corners[0][1], q.corners[0][2]));

        for (int k = 1; k < 4; ++k)
            path.lineTo (project (q.corners[(size_t) k][0], q.corners[(size_t) k][1], q.corners[(size_t) k][2]));

        path.closeSubPath();
        g.setColour (q.skirt ? q.colour.darker (0.45f).withAlpha (0.8f) : q.colour);
        g.fillPath (path);
        g.setColour (q.colour.darker (0.6f).withAlpha (0.4f));
        g.strokePath (path, juce::PathStrokeType (0.5f));
    }

    // The seat pointed at in the 3D view: a line through the layers and its three values
    if (hovered >= 0 && hovered < (int) seats.size())
    {
        const auto& p = seats[(size_t) hovered];
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawLine (juce::Line<float> (project (p[0], p[1], lowest), project (p[0], p[1], highest)), 1.0f);
        juce::String text = tr ("Seat ") + juce::String (hovered + 1) + ":";

        for (int l = 0; l < RoomResult::numLayers; ++l)
        {
            const double v = hovered < (int) seatLayers[(size_t) l].size() ? seatLayers[(size_t) l][(size_t) hovered] : std::nan ("");
            const juce::String short_[] = { "infra", tr ("very low"), tr ("low") };
            text << "   " << short_[l] << " ";

            if (std::isnan (v))
                text << "--";
            else if (loudness)
                text << juce::String (juce::roundToInt (v)) << (l == 0 ? tr (" dB SL") : tr (" phon")) << " (" << tr ("mix ")
                     << juce::String (juce::roundToInt (neutral[(size_t) l])) << ")";
            else
                text << (v >= 0.0 ? "+" : "") << juce::String (v, 1) << " dB";
        }

        g.setColour (juce::Colours::white);
        g.setFont (uiFont (11.0f, true));
        g.drawText (text, getLocalBounds().reduced (8, 4).removeFromBottom (32).removeFromTop (16), juce::Justification::centredLeft);
    }

    // Legend: the loudness scale with the mix marked, or a dip on the left, 0 dB (grey) in the middle, a boost on the right
    drawScale (g, getLocalBounds().reduced (10, 8).removeFromBottom (36).removeFromRight (300).toFloat(), loudness, mixValue,
               loudness ? tr ("Neutral plane: the mix's own loudness") : tr ("Grey and flat where the mix's bass is not heard"));
}

void RoomLayerView::mouseDown (const juce::MouseEvent&)
{
    yawAtDrag = camera.yaw;
    pitchAtDrag = camera.pitch;
}

void RoomLayerView::mouseDrag (const juce::MouseEvent& e)
{
    turnCamera (camera, yawAtDrag, pitchAtDrag, e);
    repaint();
}

void RoomLayerView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    zoomCamera (camera, wheel);
    repaint();
}

void RoomLayerView::mouseDoubleClick (const juce::MouseEvent&)
{
    camera = ProCamera();
    repaint();
}

//==============================================================================
// Knobs
ProKnobLook::ProKnobLook()
{
    setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xff8cf03c));
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff0b120b));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff2a2d2a));
    setColour (juce::Slider::textBoxHighlightColourId, juce::Colour (0xff3d6b1a));
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0b120b));
    setColour (juce::TextEditor::textColourId, juce::Colour (0xff8cf03c));
    setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff2a2d2a));
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff0b120b));
    setColour (juce::ComboBox::textColourId, juce::Colour (0xff8cf03c));
    setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff2a2d2a));
    setColour (juce::ComboBox::arrowColourId, juce::Colour (0xff8cf03c));
}

void ProKnobLook::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position, float startAngle,
                                    float endAngle, juce::Slider& slider)
{
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 3.0f;
    const auto centre = area.getCentre();
    const float angle = startAngle + position * (endAngle - startAngle);
    const juce::Colour green (0xff7ddc1f);

    // Track, then the value arc (from zero when the range goes both ways)
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff141516));
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    float from = startAngle;

    if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0)
        from = startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle);

    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (green.withAlpha (0.85f));
        g.strokePath (arc, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const auto tip = centre.getPointOnCircumference (radius, angle);
    g.setColour (green);
    g.fillEllipse (tip.x - 2.2f, tip.y - 2.2f, 4.4f, 4.4f);

    // Metal knob and its pointer
    const float body = radius - 4.0f;
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (centre.x - body + 1.0f, centre.y - body + 1.5f, body * 2.0f, body * 2.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffc9ccd1), centre.x - body, centre.y - body,
                                             juce::Colour (0xff55585e), centre.x + body, centre.y + body, false));
    g.fillEllipse (centre.x - body, centre.y - body, body * 2.0f, body * 2.0f);
    g.setColour (juce::Colour (0xff2a2b2e));
    g.drawEllipse (centre.x - body, centre.y - body, body * 2.0f, body * 2.0f, 1.0f);
    g.setColour (juce::Colour (0xff1c1d1f));
    g.drawLine (juce::Line<float> (centre, centre.getPointOnCircumference (body - 1.5f, angle)), 2.4f);
}

juce::Label* ProKnobLook::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (uiFont (11.0f, true));
    label->setJustificationType (juce::Justification::centred);
    return label;
}

//==============================================================================
// Settings editor
namespace
{
    const juce::Colour panelColour { 0xff2b2d31 }, barColour { 0xff1b1c1f }, subBarColour { 0xff383a3f };
    const juce::Colour onGreen { 0xff5fae22 }, polarityOrange { 0xffd9861c };
    const juce::String polaritySign = juce::String (juce::CharPointer_UTF8 ("\xc3\x98"));    // Ø
}

RoomProEditor::RoomProEditor()
{
    setLookAndFeel (&look);
}

RoomProEditor::~RoomProEditor()
{
    items.clear();
    setLookAndFeel (nullptr);
}

void RoomProEditor::changed (bool structure)
{
    if (room != nullptr)
        room->applyLinks();

    refreshAll();

    if (structure)
    {
        if (onStructureChange)
            onStructureChange();
    }
    else if (onChange)
    {
        onChange();
    }
}

void RoomProEditor::refreshAll()
{
    for (auto& item : items)
        if (item.refresh)
            item.refresh();
}

void RoomProEditor::section (const juce::String& title, bool sub)
{
    Item item;
    item.kind = sub ? Item::subsection : Item::section;
    item.title = title;
    items.push_back (std::move (item));
}

void RoomProEditor::barToggle (const juce::String& text, const juce::String& tip, bool& value, bool structure, juce::Colour on)
{
    auto button = std::make_unique<juce::TextButton> (text);
    button->setClickingTogglesState (true);
    button->setToggleState (value, juce::dontSendNotification);
    button->setTooltip (tip);
    button->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a3c41));
    button->setColour (juce::TextButton::buttonOnColourId, on);
    button->setColour (juce::TextButton::textColourOffId, juce::Colour (0xffb8b8b8));
    button->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    auto* raw = button.get();
    bool* target = &value;
    button->onClick = [this, raw, target, structure]
    {
        if (onGestureStart)
            onGestureStart();

        *target = raw->getToggleState();
        changed (structure);
    };
    addAndMakeVisible (*button);
    items.back().buttons.push_back (std::move (button));
}

juce::Slider& RoomProEditor::knob (const juce::String& label, double min, double max, double step, const juce::String& suffix,
                                   std::function<double()> get, std::function<void (double)> set, std::function<double()> top,
                                   std::function<double()> fallback)
{
    Item item;
    item.kind = Item::knob;
    item.label = std::make_unique<juce::Label> (juce::String(), label);
    item.label->setFont (uiFont (10.5f));
    item.label->setJustificationType (juce::Justification::centred);
    item.label->setColour (juce::Label::textColourId, juce::Colour (0xffc8c8c8));
    item.label->setInterceptsMouseClicks (false, false);

    auto slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
    slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobWidth - 6, 16);
    slider->setRange (min, top ? juce::jmax (min + step, top()) : max, step);
    slider->setNumDecimalPlacesToDisplay (step >= 1.0 ? 0 : (step >= 0.1 ? 1 : 2));
    slider->setTextValueSuffix (suffix);
    slider->setValue (get(), juce::dontSendNotification);
    slider->setMouseDragSensitivity (180);
    slider->setScrollWheelEnabled (false);      // the wheel scrolls the column
    slider->setTooltip (tr ("Double-click: back to the default."));

    if (fallback)
        slider->setDoubleClickReturnValue (true, fallback());

    auto* raw = slider.get();
    slider->onValueChange = [this, raw, set]
    {
        if (onGestureStart)
            onGestureStart();

        set (raw->getValue());
        changed (false);
    };
    item.refresh = [raw, get, top, min, step, fallback]
    {
        if (top)
            raw->setRange (min, juce::jmax (min + step, top()), step);

        // The default follows the room: a speaker at half the width goes back to half of the width set now
        if (fallback)
            raw->setDoubleClickReturnValue (true, fallback());

        raw->setValue (get(), juce::dontSendNotification);
    };

    addAndMakeVisible (*item.label);
    addAndMakeVisible (*slider);
    item.control = std::move (slider);
    items.push_back (std::move (item));
    return *raw;
}

juce::Slider& RoomProEditor::knob (const juce::String& label, double min, double max, double step, const juce::String& suffix, double& value,
                                   double fallback)
{
    double* target = &value;
    return knob (label, min, max, step, suffix, [target] { return *target; }, [target] (double v) { *target = v; }, {},
                 [fallback] { return fallback; });
}

juce::Slider& RoomProEditor::countKnob (const juce::String& label, int min, int max, int& value, int fallback)
{
    int* target = &value;
    return knob (label, min, max, 1.0, {}, [target] { return (double) *target; }, [target] (double v) { *target = juce::roundToInt (v); }, {},
                 [fallback] { return (double) fallback; });
}

void RoomProEditor::wide (const juce::String& label, std::unique_ptr<juce::Component> control)
{
    Item item;
    item.kind = Item::wide;

    if (label.isNotEmpty())
    {
        item.label = std::make_unique<juce::Label> (juce::String(), label);
        item.label->setFont (uiFont (11.0f));
        item.label->setColour (juce::Label::textColourId, juce::Colour (0xffc8c8c8));
        addAndMakeVisible (*item.label);
    }

    addAndMakeVisible (*control);
    item.control = std::move (control);
    items.push_back (std::move (item));
}

juce::ComboBox& RoomProEditor::choice (const juce::String& label, const juce::StringArray& entries, std::function<int()> get,
                                       std::function<void (int)> set, bool structure)
{
    auto box = std::make_unique<juce::ComboBox>();

    for (int i = 0; i < entries.size(); ++i)
        box->addItem (entries[i], i + 1);

    box->setSelectedId (get() + 1, juce::dontSendNotification);
    auto* raw = box.get();
    box->onChange = [this, raw, set, structure]
    {
        if (onGestureStart)
            onGestureStart();

        set (raw->getSelectedId() - 1);
        changed (structure);
    };
    wide (label, std::move (box));
    items.back().refresh = [raw, get] { raw->setSelectedId (get() + 1, juce::dontSendNotification); };
    return *raw;
}

void RoomProEditor::info (std::function<juce::String()> text)
{
    Item item;
    item.kind = Item::info;
    item.label = std::make_unique<juce::Label>();
    item.label->setFont (uiFont (10.5f));
    item.label->setColour (juce::Label::textColourId, juce::Colour (0xff9fd36a));
    item.label->setJustificationType (juce::Justification::topLeft);
    item.label->setText (text(), juce::dontSendNotification);
    auto* raw = item.label.get();
    item.refresh = [raw, text] { raw->setText (text(), juce::dontSendNotification); };
    addAndMakeVisible (*item.label);
    items.push_back (std::move (item));
}

void RoomProEditor::speaker (const juce::String& title, ProSpeaker& s, const ProSpeaker& reference, bool sub, bool isFill, bool* link)
{
    section (title, sub);

    if (link != nullptr)
        barToggle (tr ("Link"), tr ("Linked: the right one mirrors the left one across the centre of the room."), *link, true, onGreen);

    barToggle (polaritySign, tr ("Polarity inverted"), s.inverted, false, polarityOrange);
    barToggle (tr ("On"), tr ("Speaker on"), s.enabled, false, onGreen);

    // Defaults as a share of the room: they follow its size
    ProSpeaker* sp = &s;
    const ProSpeaker def = reference;
    knob (tr ("From front"), 0.0, 1.0, 0.05, " m", [this, sp] { return sp->fx * room->length; },
          [this, sp] (double v) { sp->fx = v / room->length; }, [this] { return room->length; }, [this, def] { return def.fx * room->length; });
    knob (tr ("From left"), 0.0, 1.0, 0.05, " m", [this, sp] { return sp->fy * room->width; },
          [this, sp] (double v) { sp->fy = v / room->width; }, [this] { return room->width; }, [this, def] { return def.fy * room->width; });
    knob (tr ("Height"), 0.0, 1.0, 0.05, " m", [this, sp] { return sp->fz * room->height; },
          [this, sp] (double v) { sp->fz = v / room->height; }, [this] { return room->height; }, [this, def] { return def.fz * room->height; });

    if (isFill)
        // A fill's default delay: aligned with the screen at its own distance
        knob (tr ("Delay"), 0.0, 300.0, 0.1, " ms", [sp] { return sp->delayMs; }, [sp] (double v) { sp->delayMs = v; }, {},
              [this, sp] { return 1000.0 * sp->fx * room->length / RoomSimulator::speedOfSound; });
    else
        knob (tr ("Delay"), 0.0, 50.0, 0.1, " ms", s.delayMs, 0.0);

    knob (tr ("Level"), -20.0, 20.0, 0.5, " dB", s.gainDb, 0.0);

    if (isFill)
        choice (tr ("Plays"), { tr ("Lc / Rc (L or R + C at -3 dB)"), "LFE" }, [sp] { return sp->feed; }, [sp] (int v) { sp->feed = v; }, false);
}

namespace
{
    // The reference layout the defaults come from (a multiplex of 200 seats), as shares of the room
    const ProRoom& referenceRoom()
    {
        static const ProRoom r = ProRoom::fromSpec (RoomSimulator::defaultRooms()[6]);
        return r;
    }

    juce::String materialLabel (const ProMaterial& m)
    {
        return tr (m.name) + "  (" + juce::String (m.alpha63, 2) + " / " + juce::String (m.alpha125, 2) + ")";
    }
}

void RoomProEditor::rebuild()
{
    items.clear();
    removeAllChildren();

    if (room == nullptr)
        return;

    auto& r = *room;
    const auto& ref = referenceRoom();

    // Room
    section (tr ("Room"));
    barToggle (tr ("Screen"), tr ("A screen drawn on the front wall, for a cinema. Only a mark: it changes nothing in the computation."),
               r.screen, false, onGreen);
    {
        auto text = std::make_unique<juce::TextEditor>();
        text->setText (juce::String (r.name), false);
        text->setFont (uiFont (12.0f, true));
        auto* raw = text.get();
        text->onTextChange = [this, raw] { if (onGestureStart) onGestureStart(); room->name = raw->getText().toStdString(); changed (false); };
        wide (tr ("Name"), std::move (text));
    }

    {
        // Presets: the cinema rooms of Room Sim, then the other venues by category
        auto box = std::make_unique<juce::ComboBox>();
        box->setTextWhenNothingSelected (tr ("Choose a room..."));
        box->addSectionHeading (tr ("Cinemas"));
        const auto specs = RoomSimulator::defaultRooms();

        for (size_t i = 0; i < specs.size(); ++i)
            box->addItem (tr (specs[i].name), (int) i + 1);

        const auto venues = ProRoom::venues();
        std::string category;

        for (size_t i = 0; i < venues.size(); ++i)
        {
            if (venues[i].category != category)
            {
                category = venues[i].category;
                box->addSectionHeading (tr (category));
            }

            box->addItem (tr (venues[i].room.name), 100 + (int) i);
        }

        auto* raw = box.get();
        box->onChange = [this, raw]
        {
            const int id = raw->getSelectedId();
            const auto cinemas = RoomSimulator::defaultRooms();
            const auto others = ProRoom::venues();
            const bool counted = room->counted;

            if (onGestureStart)
                onGestureStart();

            if (id >= 1 && id <= (int) cinemas.size())
                *room = ProRoom::fromSpec (cinemas[(size_t) id - 1]);
            else if (id >= 100 && id < 100 + (int) others.size())
                *room = others[(size_t) id - 100].room;
            else
                return;

            room->name = tr (room->name).toStdString();
            room->counted = counted;
            changed (true);
        };
        wide (tr ("Start from"), std::move (box));
    }

    choice (tr ("Shape"), { tr ("Rectangle"), tr ("Round"), tr ("Oval"), tr ("Rounded corners"), tr ("L shape") },
            [this] { return room->shape; }, [this] (int v) { room->shape = v; }, true);

    if (r.shape == ProRoom::round)
    {
        knob (tr ("Diameter"), 3.0, 40.0, 0.1, " m", r.width, 13.0);
    }
    else
    {
        knob (tr ("Length"), 4.0, 60.0, 0.1, " m", r.length, 20.0);
        knob (tr ("Width"), 3.0, 40.0, 0.1, " m", r.width, 13.0);
    }

    knob (tr ("Height"), 2.5, 25.0, 0.1, " m", r.height, 7.0);

    if (r.shape == ProRoom::oval || r.shape == ProRoom::roundedCorners || r.shape == ProRoom::lShape)
        info ([] { return tr ("Modes of this floor computed numerically (Rayleigh-Ritz): a second or two for a large room."); });

    // Acoustics
    section (tr ("Acoustics"));
    choice (tr ("Decay"), { tr ("RT60 set by hand"), tr ("From the materials") }, [this] { return room->acoustics; },
            [this] (int v) { room->acoustics = v; }, true);

    if (r.acoustics == 0)
    {
        knob (tr ("RT60"), 0.1, 10.0, 0.01, " s", r.rt60, 0.55).setSkewFactorFromMidPoint (1.0);
    }
    else
    {
        const juce::String surfaces[] = { tr ("Front wall"), tr ("Back wall"), tr ("Left wall"), tr ("Right wall"), tr ("Floor"), tr ("Ceiling") };
        const int allowed[] = { ProMaterial::wall, ProMaterial::wall, ProMaterial::wall, ProMaterial::wall, ProMaterial::floor, ProMaterial::ceiling };

        for (int i = 0; i < ProRoom::numSurfaces; ++i)
        {
            if (r.shape != ProRoom::rectangle && i != ProRoom::frontWall && i != ProRoom::floor && i != ProRoom::ceiling)
                continue;

            // Only what can go on this surface, by family, in alphabetical order of the language shown
            auto box = std::make_unique<juce::ComboBox>();
            const auto& list = ProRoom::materialList();

            for (int group = 0; group < ProMaterial::numGroups; ++group)
            {
                std::vector<int> members;

                for (size_t m = 0; m < list.size(); ++m)
                    if (list[m].group == group && (list[m].surfaces & allowed[i]) != 0)
                        members.push_back ((int) m);

                if (members.empty())
                    continue;

                std::sort (members.begin(), members.end(), [&list] (int a, int b) { return tr (list[(size_t) a].name).compareNatural (tr (list[(size_t) b].name)) < 0; });
                box->addSectionHeading (tr (ProMaterial::groupName (group)));

                for (int m : members)
                    box->addItem (materialLabel (list[(size_t) m]), m + 1);
            }

            box->setSelectedId (r.materials[(size_t) i] + 1, juce::dontSendNotification);
            auto* raw = box.get();
            box->onChange = [this, raw, i]
            {
                if (raw->getSelectedId() > 0)
                {
                    if (onGestureStart)
                        onGestureStart();

                    room->materials[(size_t) i] = raw->getSelectedId() - 1;
                    changed (false);
                }
            };
            wide (r.shape != ProRoom::rectangle && i == ProRoom::frontWall ? tr ("Wall") : surfaces[i], std::move (box));
            items.back().refresh = [this, raw, i] { raw->setSelectedId (room->materials[(size_t) i] + 1, juce::dontSendNotification); };
        }
    }

    info ([this]
    {
        const auto& rm = *room;
        juce::String text;

        if (rm.acoustics == 1)
            text << tr ("RT60 (Eyring)") << "  63 Hz: " << juce::String (rm.eyringRt60 (63.0), 2) << " s   125 Hz: "
                 << juce::String (rm.eyringRt60 (125.0), 2) << " s\n";
        else
            text << tr ("Absorption in brackets: 63 Hz / 125 Hz") << "\n";

        text << tr ("Schroeder frequency") << " " << juce::roundToInt (rm.schroederHz()) << " Hz: " << tr ("below it, single modes.");
        return text;
    });

    // Speakers
    section ("L C R");
    choice (tr ("Sets"), { tr ("One set"), tr ("Two sets, back to back") }, [this] { return room->dualLCR ? 1 : 0; },
            [this] (int v) { room->dualLCR = v == 1; }, true);

    if (r.dualLCR)
        info ([] { return tr ("The second set is the first one turned round the centre of the room:\nplace the first set, the second follows."); });

    if (r.linkLR)
    {
        speaker (tr ("Left / Right"), r.lcr[0], ref.lcr[0], true, false, &r.linkLR);
    }
    else
    {
        speaker (tr ("Left"), r.lcr[0], ref.lcr[0], true, false, &r.linkLR);
        speaker (tr ("Right"), r.lcr[2], ref.lcr[2], true, false, nullptr);
    }

    speaker (tr ("Centre"), r.lcr[1], ref.lcr[1], true, false, nullptr);

    // Routing, as on a console panner
    section (tr ("Divergence"));
    knob (tr ("Centre %"), 0.0, 100.0, 1.0, " %", r.centrePercent, 100.0)
        .setTooltip (tr ("Share of C played by the centre speaker; the rest goes to L and R as a phantom centre. Double-click: 100."));
    knob (tr ("Front"), 0.0, 100.0, 1.0, " %", r.frontDivergence, 100.0)
        .setTooltip (tr ("100: each screen channel on its own speaker. Lower: spread over L, C and R. Double-click: 100."));
    knob (tr ("Rear"), 0.0, 100.0, 1.0, " %", r.rearDivergence, 100.0)
        .setTooltip (tr ("100: left fills play Lc, right fills Rc. Lower: the two sides share their feeds. Double-click: 100."));
    knob (tr ("F / R"), 0.0, 100.0, 1.0, " %", r.frDivergence, 100.0)
        .setTooltip (tr ("100: the screen channels stay on the screen speakers. Lower: part of them goes to the fills. Double-click: 100."));

    section (tr ("Subwoofers"));
    choice (tr ("Number"), { "1", "2", "3", "4" }, [this] { return room->numSubs - 1; }, [this] (int v) { room->numSubs = v + 1; }, true);

    for (int i = 0; i < juce::jlimit (1, ProRoom::maxSubs, r.numSubs); ++i)
        speaker (tr ("Sub") + " " + juce::String (i + 1), r.subs[(size_t) i], ref.subs[(size_t) i], true, false, nullptr);

    section (tr ("Fill speakers"));
    barToggle (tr ("Link"), tr ("Linked: fills 2 and 4 mirror fills 1 and 3 across the centre of the room."), r.linkFills, true, onGreen);
    barToggle (tr ("On"), tr ("Use fill speakers"), r.fillsOn, true, onGreen);

    if (r.fillsOn)
    {
        if (r.linkFills)
        {
            choice (tr ("Number"), { tr ("1 pair"), tr ("2 pairs") }, [this] { return room->numFills <= 2 ? 0 : 1; },
                    [this] (int v) { room->numFills = (v + 1) * 2; }, true);

            for (int pair = 0; pair < (r.numFills <= 2 ? 1 : 2); ++pair)
                speaker (tr ("Fills") + " " + juce::String (pair * 2 + 1) + " / " + juce::String (pair * 2 + 2), r.fills[(size_t) pair * 2],
                         ref.fills[(size_t) pair * 2], true, true, nullptr);
        }
        else
        {
            choice (tr ("Number"), { "1", "2", "3", "4" }, [this] { return room->numFills - 1; }, [this] (int v) { room->numFills = v + 1; }, true);

            for (int i = 0; i < juce::jlimit (1, ProRoom::maxFills, r.numFills); ++i)
                speaker (tr ("Fill") + " " + juce::String (i + 1), r.fills[(size_t) i], ref.fills[(size_t) i], true, true, nullptr);
        }
    }

    // Audience
    section (tr ("Audience"));
    choice (tr ("Layout"), { tr ("Rows"), tr ("Two blocks, aisle in the middle"), tr ("Rings around the centre") },
            [this] { return room->audienceLayout; }, [this] (int v) { room->audienceLayout = v; }, true);
    choice (tr ("People"), { tr ("None (empty room)"), tr ("Seated"), tr ("Standing") }, [this] { return room->people; },
            [this] (int v) { room->people = v; }, false);

    if (r.audienceLayout == ProRoom::ringLayout)
    {
        const auto half = [this] { return 0.5 * std::min (room->length, room->width); };
        knob (tr ("Inner"), 0.0, 1.0, 0.1, " m", [this, half] { return room->ringInner * half(); },
              [this, half] (double v) { room->ringInner = v / half(); }, half, [half] { return 0.3 * half(); });
        knob (tr ("Outer"), 0.0, 1.0, 0.1, " m", [this, half] { return room->ringOuter * half(); },
              [this, half] (double v) { room->ringOuter = v / half(); }, half, [half] { return 0.85 * half(); });
        countKnob (tr ("Rings"), 1, 12, r.rows, 4);
        countKnob (tr ("Outer ring"), 4, 24, r.perRow, 16);
    }
    else
    {
        knob (tr ("First row"), 0.0, 1.0, 0.1, " m", [this] { return room->firstRow * room->length; },
              [this] (double v) { room->firstRow = v / room->length; }, [this] { return room->length; },
              [this, ref] { return ref.firstRow * room->length; });
        knob (tr ("Last row"), 0.0, 1.0, 0.1, " m", [this] { return room->lastRow * room->length; },
              [this] (double v) { room->lastRow = v / room->length; }, [this] { return room->length; },
              [this, ref] { return ref.lastRow * room->length; });
        knob (tr ("Width"), 10.0, 100.0, 1.0, " %", [this] { return room->audienceWidth * 100.0; },
              [this] (double v) { room->audienceWidth = v / 100.0; }, {}, [ref] { return ref.audienceWidth * 100.0; });

        if (r.audienceLayout == ProRoom::sidesLayout)
            knob (tr ("Aisle"), 0.0, 90.0, 1.0, " %", [this] { return room->aisle * 100.0; }, [this] (double v) { room->aisle = v / 100.0; },
                  {}, [] { return 25.0; });

        countKnob (tr ("Rows"), 1, 12, r.rows, ref.rows);
        countKnob (tr ("Per row"), 1, 24, r.perRow, ref.perRow);
    }

    knob (tr ("Ear"), 0.8, 3.0, 0.05, " m", r.earHeight, 1.2);
    knob (tr ("Rake"), 0.0, 10.0, 0.05, " m", r.rake, 0.0);

    section (tr ("Calibration"));
    choice (tr ("Levels set at"), { tr ("All seats (average)"), tr ("Reference seat (2/3, centre)") }, [this] { return room->calibration; },
            [this] (int v) { room->calibration = v; }, false)
        .setTooltip (tr ("As with pink noise and a sound level meter: each speaker group is set for the same total energy from 20 to 120 Hz "
                         "(direct and reverberant sound together, which below the Schroeder frequency are the same modes). "
                         "A live room therefore gets lower gains than a dead one."));

    resized();
}

void RoomProEditor::resized()
{
    const int width = getWidth();
    int y = 4, column = 0;
    bars.clear();
    panels.clear();
    int panelTop = -1;

    const auto endRow = [&]
    {
        if (column > 0)
        {
            y += knobHeight;
            column = 0;
        }
    };

    for (auto& item : items)
    {
        if (item.kind == Item::section || item.kind == Item::subsection)
        {
            endRow();
            const bool sub = item.kind == Item::subsection;

            if (! sub)
            {
                if (panelTop >= 0)
                    panels.push_back ({ 2, panelTop, width - 4, y + 4 - panelTop });

                y += panelTop >= 0 ? 12 : 0;
                panelTop = y;
            }
            else
            {
                y += 4;
            }

            const auto bar = juce::Rectangle<int> (sub ? 8 : 2, y, width - (sub ? 16 : 4), sub ? 18 : 22);
            bars.push_back ({ bar, (int) item.kind });
            auto buttons = bar.reduced (3, 2);

            for (auto& b : item.buttons)
            {
                b->setBounds (buttons.removeFromRight (b->getButtonText() == polaritySign ? 24 : 34));
                buttons.removeFromRight (3);
            }

            y += bar.getHeight() + 4;
            continue;
        }

        if (item.kind == Item::knob)
        {
            const int x = 8 + column * knobWidth;
            item.label->setBounds (x, y, knobWidth, 12);
            item.control->setBounds (x, y + 12, knobWidth, knobHeight - 14);

            if (++column == columns)
            {
                y += knobHeight;
                column = 0;
            }

            continue;
        }

        endRow();

        if (item.kind == Item::info)
        {
            item.label->setBounds (8, y, width - 16, 30);
            y += 32;
            continue;
        }

        if (item.label != nullptr)
        {
            item.label->setBounds (8, y, 74, 22);
            item.control->setBounds (84, y, width - 92, 22);
        }
        else
        {
            item.control->setBounds (8, y, width - 16, 22);
        }

        y += 26;
    }

    endRow();

    if (panelTop >= 0)
        panels.push_back ({ 2, panelTop, width - 4, y + 4 - panelTop });

    contentHeight = y + 10;
    repaint();
}

void RoomProEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::plot);

    for (const auto& p : panels)
    {
        g.setColour (panelColour);
        g.fillRoundedRectangle (p.toFloat(), 4.0f);
    }

    size_t b = 0;

    for (const auto& item : items)
    {
        if (item.kind != Item::section && item.kind != Item::subsection)
            continue;

        if (b >= bars.size())
            break;

        const auto bar = bars[b++].first;
        const bool sub = item.kind == Item::subsection;
        g.setColour (sub ? subBarColour : barColour);
        g.fillRoundedRectangle (bar.toFloat(), sub ? 2.0f : 3.0f);
        g.setColour (sub ? juce::Colour (0xffd0d0d0) : juce::Colours::white);
        g.setFont (uiFont (sub ? 11.0f : 12.5f, true));
        g.drawText (item.title, bar.reduced (6, 0), juce::Justification::centredLeft);
    }
}

//==============================================================================
// The tab
RoomProView::RoomProView()
{
    config = ProRooms::defaults();

    for (int i = 0; i < ProRooms::count; ++i)
    {
        countedTicks[(size_t) i] = std::make_unique<juce::ToggleButton> (tr ("Counts"));
        countedTicks[(size_t) i]->setTooltip (tr ("This room counts in the Room Sim Pro score."));
        countedTicks[(size_t) i]->onClick = [this, i]
        {
            rememberForUndo();
            config.rooms[(size_t) i].counted = countedTicks[(size_t) i]->getToggleState();
            configChanged();
        };
        addAndMakeVisible (*countedTicks[(size_t) i]);
    }

    // The lens selector, with the part of the bass (lens 1) or the cause (lens 2) shown
    {
        const juce::String lensNames[] = { tr ("Who gets bass, how much"), tr ("Why") };
        const juce::String lensTips[] = {
            tr ("How loud the bass is at every head and point (phon, ISO 226), on the same scale as the mix as approved, "
                "the room and the placement of every speaker included. A mix with little bass shows little everywhere."),
            tr ("Why the bass changes against the approved mix, cut in three causes that add up: the room (what each speaker "
                "gives there on its own), the speakers (their interference: placement, delay, polarity) and the mix (the LFE "
                "against L C R). Grey where the mix's bass is not heard.") };
        const juce::String partNames[] = { tr ("All"), "L C R", "LFE" };
        const juce::String partTips[] = {
            tr ("All the bass heard."),
            tr ("L C R alone, through every speaker that plays them (Lc / Rc fills included): a hole here comes from L C R or the fills."),
            tr ("The LFE alone, through the subwoofers and the LFE fills: a hole here comes from them.") };
        const juce::String causeNames[] = { tr ("Room"), tr ("Speakers"), tr ("Mix") };
        const juce::String causeTips[] = {
            tr ("What each speaker gives there on its own: modes, distance, materials. Placing the speakers or the audience changes it."),
            tr ("The interference between speakers: subwoofer against subwoofer, fill against the speaker it repeats, L against R, "
                "two L C R sets. Placement, delay and polarity in the venue change it."),
            tr ("The interference between the LFE and L C R that the mix makes, against the approved mix. The LFE's delay, "
                "polarity, level and low-pass in the mix change it.") };

        const auto style = [] (juce::TextButton& b, int group)
        {
            b.setClickingTogglesState (true);
            b.setRadioGroupId (group);
            b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2c31));
            b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3a7bd5));
            b.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffb8b8b8));
            b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        };

        const auto edges = [] (int i, int count)
        {
            return (i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i + 1 < count ? juce::Button::ConnectedOnRight : 0);
        };

        for (int i = 0; i < RoomResult::numLenses; ++i)
        {
            auto& b = lensButtons[(size_t) i];
            b.setButtonText (lensNames[i]);
            b.setTooltip (lensTips[i]);
            style (b, 7101);
            b.setConnectedEdges (edges (i, RoomResult::numLenses));
            b.setToggleState (i == lens, juce::dontSendNotification);
            b.onClick = [this, i] { if (lensButtons[(size_t) i].getToggleState()) { lens = i; showSelected(); resized(); repaint(); } };
            addAndMakeVisible (b);
        }

        for (int i = 0; i < RoomResult::numParts; ++i)
        {
            auto& b = partButtons[(size_t) i];
            b.setButtonText (partNames[i]);
            b.setTooltip (partTips[i]);
            style (b, 7102);
            b.setConnectedEdges (edges (i, RoomResult::numParts));
            b.setToggleState (i == part, juce::dontSendNotification);
            b.onClick = [this, i] { if (partButtons[(size_t) i].getToggleState()) { part = i; showSelected(); repaint(); } };
            addAndMakeVisible (b);
        }

        for (int i = 0; i < RoomResult::numCauses; ++i)
        {
            auto& b = causeButtons[(size_t) i];
            b.setButtonText (causeNames[i]);
            b.setTooltip (causeTips[i]);
            style (b, 7103);
            b.setConnectedEdges (edges (i, RoomResult::numCauses));
            b.setToggleState (i == cause, juce::dontSendNotification);
            b.onClick = [this, i] { if (causeButtons[(size_t) i].getToggleState()) { cause = i; showSelected(); repaint(); } };
            addAndMakeVisible (b);
        }
    }

    addAndMakeVisible (view3d);
    addAndMakeVisible (layerView);
    view3d.setCamera (camera);
    view3d.onHover = [this] (int seat) { layerView.setHovered (seat); };
    editorViewport.setViewedComponent (&editor, false);
    editorViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (editorViewport);

    editor.onChange = [this] { configChanged(); };
    editor.onGestureStart = [this] { rememberForUndo(); };
    setWantsKeyboardFocus (true);
    editor.onStructureChange = [this]
    {
        configChanged();
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RoomProView> (this)]
        {
            if (safe != nullptr)
            {
                // Rebuilt in place: the column stays where it was
                const auto position = safe->editorViewport.getViewPosition();
                safe->editor.rebuild();
                safe->resized();
                safe->editorViewport.setViewPosition (position);
            }
        });
    };

    saveButton.setTooltip (tr ("Save the three rooms to a file."));
    loadButton.setTooltip (tr ("Load three rooms from a file."));
    settingsButton.onClick = [this] { settingsOpen = ! settingsOpen; resized(); };
    addAndMakeVisible (settingsButton);
    saveButton.onClick = [this] { exportConfig(); };
    loadButton.onClick = [this] { importConfig(); };
    addAndMakeVisible (saveButton);
    addAndMakeVisible (loadButton);

    selectRoom (0);
}

RoomProView::~RoomProView()
{
    if (job.valid())
        job.wait();
}

void RoomProView::setRooms (const ProRooms& rooms)
{
    config = rooms;

    for (int i = 0; i < ProRooms::count; ++i)
        countedTicks[(size_t) i]->setToggleState (config.rooms[(size_t) i].counted, juce::dontSendNotification);

    selectRoom (selected);
    dirty = true;
    changedAt = 0.0;
}

void RoomProView::selectRoom (int index)
{
    selected = juce::jlimit (0, ProRooms::count - 1, index);
    editor.setRoom (&config.rooms[(size_t) selected]);
    view3d.setWorstSpot (std::nullopt, 100.0);
    showSelected();
    resized();
    repaint();
}

int RoomProView::shownRating() const
{
    if (lens == RoomResult::coverageLens || cause == RoomResult::roomCause)
        return RoomResult::coverageRating;

    return cause == RoomResult::speakersCause ? RoomResult::speakersRating : RoomResult::mixRating;
}

void RoomProView::showSelected()
{
    const auto& res = results[(size_t) selected];
    const auto& p = paths[(size_t) selected];
    const auto& room = config.rooms[(size_t) selected];
    const int rating = shownRating();
    const bool loudness = lens == RoomResult::coverageLens;
    const bool rated = ready && hasBass && ! dirty && res.seatRatingPenalty[(size_t) rating].size() == room.seatPositions().size();

    const juce::String headCaptions[] = { tr ("Heads: the change caused by the room, against the approved mix"),
                                          tr ("Heads: the change caused by the speakers, against the approved mix"),
                                          tr ("Heads: the change caused by the mix, against the approved mix") };
    view3d.setCaption (loudness ? tr ("Heads: how loud their bass is (phon)") : headCaptions[cause]);

    if (! rated)
    {
        view3d.setRoom (room, {}, {}, loudness);
        view3d.setWorstSpot (std::nullopt, 100.0);
        layerView.setData (room, nullptr, nullptr, lens, loudness ? part : cause);
        return;
    }

    view3d.setRoom (room, res.seatRatingPenalty[(size_t) rating], loudness ? res.seatPhon[(size_t) part] : res.seatCauseDb[(size_t) cause],
                    loudness, res.approvedPhon[(size_t) part], res.seatHeard);
    Room3DView::Details details;
    details.phon = res.seatPhon;
    details.partDb = res.seatPartDb;
    details.mixPhon = res.approvedPhon;
    details.causeDb = res.seatCauseDb;
    view3d.setDetails (details);
    view3d.setHeads (res.seatHeardPhon, res.seatFeltDb);
    layerView.setData (room, &p, &res, lens, loudness ? part : cause);
    const int worst = res.ratingWorstProbe[(size_t) rating];
    view3d.setWorstSpot (worst >= 0 && worst < (int) p.probePositions.size()
                           ? std::optional<std::array<double, 3>> (p.probePositions[(size_t) worst]) : std::nullopt,
                         res.ratingWorstPointScore[(size_t) rating]);
}

void RoomProView::configChanged()
{
    dirty = true;
    changedAt = now();
    view3d.setRoom (config.rooms[(size_t) selected], {});
    view3d.setWorstSpot (std::nullopt, 100.0);
    view3d.setHeads ({}, {});
    layerView.setData (config.rooms[(size_t) selected], nullptr, nullptr, lens, part);
    repaint();

    if (onConfigChanged)
        onConfigChanged();
}

void RoomProView::startPreparing (const std::vector<double>& grid)
{
    const auto rooms = config;
    jobGrid = grid;
    dirty = false;
    job = std::async (std::launch::async, [rooms, grid]
    {
        std::array<RoomPaths, ProRooms::count> out;

        for (int i = 0; i < ProRooms::count; ++i)
            out[(size_t) i] = ProRoomModel::prepare (rooms.rooms[(size_t) i], grid);

        return out;
    });
}

double RoomProView::Smoothed::update (double value, double seconds, double scoreSpeed)
{
    const double v = inertia.update (value, seconds);

    // Score speed: an extra, plain smoothing on top, up to about ten seconds
    if (scoreSpeed > 0.0 && slow >= 0.0)
        slow += (1.0 - std::exp (-seconds / (10.0 * scoreSpeed * scoreSpeed))) * (v - slow);
    else
        slow = v;

    if (shown < 0.0 || std::abs (slow - shown) >= 1.5)
        shown = (double) juce::roundToInt (slow);

    return shown;
}

void RoomProView::resetScores()
{
    for (auto& r : roomSmooth)
        r.reset();

    for (auto& room : lensSmooth)
        for (auto& r : room)
            r.reset();

    proSmooth.reset();
    shownPro = -1.0;
}

void RoomProView::update (const Analysis& a, const Translation& electricalTranslation, const PlaybackSimulator::Tuning& tuning,
                          PlaybackSimulator::Level level, double lfeGainDb, double seconds, double scoreSpeed)
{
    // Collect a finished preparation, start the next one when the settings have settled
    if (job.valid() && job.wait_for (std::chrono::seconds (0)) == std::future_status::ready)
    {
        paths = job.get();
        pathsGrid = jobGrid;
        ready = true;

        // New rooms: their scores start from what they are, not from the old ones
        resetScores();
    }

    if (! job.valid() && ! a.grid.empty() && (dirty || a.grid != pathsGrid) && now() - changedAt > 0.25)
        startPreparing (a.grid);

    hasBass = electricalTranslation.hasBass;

    if (! hasBass)
        resetScores();      // as the global score: no bass, nothing to hold on to
    electrical = electricalTranslation.robustness;

    if (ready && hasBass && a.grid == pathsGrid)
    {
        RoomSimResult rooms;
        rooms.hasBass = true;
        std::vector<bool> counted;

        for (int i = 0; i < ProRooms::count; ++i)
        {
            results[(size_t) i] = RoomSimulator::rate (paths[(size_t) i], a, electricalTranslation, tuning, level, lfeGainDb);
            rooms.rooms.push_back (results[(size_t) i]);
            counted.push_back (config.rooms[(size_t) i].counted);
            shownRoom[(size_t) i] = roomSmooth[(size_t) i].update (results[(size_t) i].roomScore, seconds, scoreSpeed);

            for (int l = 0; l < RoomResult::numRatings; ++l)
                shownLens[(size_t) i][(size_t) l] = lensSmooth[(size_t) i][(size_t) l].update (results[(size_t) i].ratingScore[(size_t) l], seconds, scoreSpeed);
        }

        auto withRooms = electricalTranslation;
        RoomSimulator::applyToScore (withRooms, rooms, counted, tuning.levelDependenceWeight);
        proScore = withRooms.robustness;
        shownPro = proSmooth.update (proScore, seconds, scoreSpeed);

        if (! dirty)
            showSelected();
    }

    repaint (0, 0, getWidth(), view3d.getY());     // the score and the cards; the 3D view repaints itself
}

void RoomProView::paint (juce::Graphics& g)
{
    g.fillAll (theme::plot);
    auto area = getLocalBounds().toFloat().reduced (16.0f, 10.0f);

    // Header: the Room Sim Pro score
    {
        auto top = area.removeFromTop (58.0f);
        auto big = top.removeFromLeft (230.0f);
        g.setColour (theme::dimText);
        g.setFont (uiFont (12.0f));
        g.drawText (tr ("Room Sim Pro score"), big.removeFromTop (16.0f), juce::Justification::centredLeft);
        const bool show = hasBass && shownPro >= 0.0;
        g.setColour (show ? ChainsView::colourFor (1.0 - shownPro / 100.0) : theme::dimText);
        g.setFont (uiFont (34.0f, true));
        g.drawText (show ? juce::String (juce::roundToInt (shownPro)) : juce::String ("--"), big, juce::Justification::centredLeft);

        top.removeFromRight (230.0f);       // the Soft / Medium / Strong selector
        top.removeFromRight (230.0f);       // save and load
        g.setColour (theme::dimText);
        g.setFont (uiFont (11.5f));
        g.drawFittedText (tr ("Three rooms of your own, outside the cinema layout. Same rating as Room Sim and the same Dev settings and "
                          "listening level. Ticked rooms count in this score, which is not part of the global one. Electrical robustness ")
                          + juce::String (juce::roundToInt (electrical)) + ".",
                          top.toNearestInt(), juce::Justification::centredLeft, 3);
    }

    // The three room cards (their layout is set in resized)
    for (int i = 0; i < ProRooms::count; ++i)
    {
        const auto& card = cards[(size_t) i];
        const auto& room = config.rooms[(size_t) i];
        const bool isSelected = i == selected;

        g.setColour (isSelected ? juce::Colour (0xff2a3440) : juce::Colour (0xff1c1c1c));
        g.fillRoundedRectangle (card, 5.0f);

        if (isSelected)
        {
            g.setColour (juce::Colour (0xff3a9be0));
            g.drawRoundedRectangle (card.reduced (0.5f), 5.0f, 1.5f);
        }

        auto inner = card.reduced (10.0f, 6.0f);
        inner.removeFromLeft (78.0f);       // the "Counts" tick
        auto scores = inner.removeFromRight (150.0f);
        inner.removeFromRight (8.0f);

        g.setColour (theme::text);
        g.setFont (uiFont (13.0f, true));
        g.drawText (juce::String (room.name), inner.removeFromTop (18.0f), juce::Justification::centredLeft);
        g.setColour (theme::dimText);
        g.setFont (uiFont (10.5f));
        g.drawText ((room.shape == ProRoom::round ? tr ("Round, ") + juce::String (room.width, 1) + tr (" m across, ") + juce::String (room.height, 1) + tr (" m high")
                                                  : juce::String (room.length, 1) + " x " + juce::String (room.width, 1) + " x " + juce::String (room.height, 1) + " m")
                      + ",  RT60 "
                      + juce::String (room.rt60At (63.0), 2) + " s,  " + juce::String ((int) room.seatPositions().size()) + tr (" seats")
                      + (room.fillsOn ? tr (",  fills") : ""),
                    inner.removeFromTop (14.0f), juce::Justification::centredLeft);

        // Seats on a -12..+12 line, in the lens shown (phon against the mix's, or dB of the cause shown),
        // coloured by their score in it
        const auto& res = results[(size_t) i];
        const int rating = shownRating();
        const bool loudness = lens == RoomResult::coverageLens;
        auto values = loudness ? res.seatPhon[(size_t) part] : res.seatCauseDb[(size_t) cause];

        if (loudness)
            for (auto& v : values)
                v -= res.approvedPhon[(size_t) part];

        const auto& penalties = res.seatRatingPenalty[(size_t) rating];
        auto line = inner.reduced (0.0f, 4.0f);
        const float mid = line.getCentreX(), y = line.getCentreY();
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (line.getX(), y, line.getWidth(), 1.0f);
        g.fillRect (mid, y - 5.0f, 1.0f, 10.0f);

        if (ready && hasBass && values.size() == penalties.size())
            for (size_t s = 0; s < values.size(); ++s)
                if (! std::isnan (values[s]))
                {
                    const float x = mid + (float) juce::jlimit (-1.0, 1.0, values[s] / 12.0) * line.getWidth() / 2.0f;
                    g.setColour (Room3DView::seatColour (100.0 * (1.0 - penalties[s])));
                    g.fillEllipse (x - 2.5f, y - 2.5f, 5.0f, 5.0f);
                }

        // The three scores, the one that goes with what is shown in a coloured box; under them its
        // 10th percentile and the worst point found
        const bool computing = dirty || job.valid();
        const bool show = ready && hasBass && ! computing;
        const juce::String names[] = { tr ("Coverage"), tr ("Placement"), tr ("Mix") };
        const float h = scores.getHeight() / 4.0f;

        for (int l = 0; l < RoomResult::numRatings; ++l)
        {
            auto row = scores.removeFromTop (h).reduced (0.0f, 1.0f);
            const double v = shownLens[(size_t) i][(size_t) l];
            const auto number = show ? juce::String (juce::roundToInt (v)) : juce::String (computing ? "..." : "--");

            if (l == rating)
            {
                g.setColour (show ? ChainsView::colourFor (1.0 - v / 100.0) : juce::Colour (0xff2a2a2a));
                g.fillRoundedRectangle (row, 3.0f);
                g.setColour (juce::Colours::white);
                g.setFont (uiFont (11.5f, true));
            }
            else
            {
                g.setColour (theme::dimText);
                g.setFont (uiFont (11.0f));
            }

            g.drawText (names[l], row.reduced (6.0f, 0.0f), juce::Justification::centredLeft);
            g.drawText (number, row.reduced (6.0f, 0.0f), juce::Justification::centredRight);
        }

        g.setFont (uiFont (9.5f));
        const int worst = res.ratingWorstProbe[(size_t) rating];
        g.setColour (show && worst >= 0 ? juce::Colour (0xffff5ad2) : theme::dimText);
        g.drawText (show ? "P10 " + juce::String (juce::roundToInt (res.ratingP10[(size_t) rating])) + "   " + tr ("worst ")
                             + juce::String (juce::roundToInt (res.ratingWorstPointScore[(size_t) rating]))
                         : juce::String (computing ? tr ("computing...") : juce::String()),
                    scores, juce::Justification::centred);
    }
}

void RoomProView::resized()
{
    auto area = getLocalBounds().reduced (16, 10);
    auto top = area.removeFromTop (58);
    top.removeFromRight (230);
    auto buttons = top.removeFromRight (230).withSizeKeepingCentre (230, 26);
    saveButton.setBounds (buttons.removeFromLeft (112));
    buttons.removeFromLeft (6);
    loadButton.setBounds (buttons);

    area.removeFromTop (6);
    auto cardRow = area.removeFromTop (72);
    const int cardWidth = (cardRow.getWidth() - 2 * 10) / ProRooms::count;

    for (int i = 0; i < ProRooms::count; ++i)
    {
        auto card = cardRow.removeFromLeft (cardWidth);
        cardRow.removeFromLeft (10);
        cards[(size_t) i] = card.toFloat();
        countedTicks[(size_t) i]->setBounds (card.getX() + 8, card.getCentreY() - 12, 72, 24);
    }

    area.removeFromTop (8);

    // The lens selector, over the two views
    lensRow = area.removeFromTop (24);
    {
        auto row = lensRow;
        for (auto& b : lensButtons)
            b.setBounds (row.removeFromLeft (190));

        row.removeFromLeft (16);

        for (auto& b : partButtons)
        {
            b.setBounds (row.removeFromLeft (64));
            b.setVisible (lens == RoomResult::coverageLens);
        }

        row = lensRow.withTrimmedLeft (2 * 190 + 16);

        for (auto& b : causeButtons)
        {
            b.setBounds (row.removeFromLeft (84));
            b.setVisible (lens == RoomResult::causesLens);
        }
    }
    area.removeFromTop (6);

    // The settings column folds away: a narrow strip with its button is left
    auto strip = area.removeFromRight (settingsOpen ? RoomProEditor::preferredWidth + editorViewport.getScrollBarThickness() + 2 : 26);
    area.removeFromRight (10);
    settingsButton.setButtonText (settingsOpen ? tr ("Settings") + juce::String::fromUTF8 ("  \xe2\x96\xb8")
                                               : juce::String::fromUTF8 ("\xe2\x97\x82"));
    settingsButton.setTooltip (settingsOpen ? tr ("Fold the settings away") : tr ("Show the room and speaker settings"));
    auto settings = strip;
    settingsButton.setBounds (settings.removeFromTop (24));
    settings.removeFromTop (4);
    editorViewport.setVisible (settingsOpen);
    // The 3D view and the layered view side by side, the same size
    auto right = area.removeFromRight ((area.getWidth() - 10) / 2);
    area.removeFromRight (10);
    view3d.setBounds (area);
    layerView.setBounds (right);
    editorViewport.setBounds (settings);
    editor.setSize (RoomProEditor::preferredWidth, juce::jmax (100, editor.getHeight()));     // never shrunk first: that would scroll to the top
    editor.resized();
    editor.setSize (RoomProEditor::preferredWidth, editor.preferredHeight());
}

void RoomProView::rememberForUndo()
{
    // One step per gesture: changes less than 0.6 s apart (a knob being turned) make a single step
    const double t = now();

    if (t - lastEditAt > 0.6)
    {
        undoStack.push_back (config);

        while (undoStack.size() > maxUndo)
            undoStack.pop_front();

        redoStack.clear();
    }

    lastEditAt = t;
}

void RoomProView::restore (const ProRooms& state)
{
    const auto position = editorViewport.getViewPosition();
    config = state;

    for (int i = 0; i < ProRooms::count; ++i)
        countedTicks[(size_t) i]->setToggleState (config.rooms[(size_t) i].counted, juce::dontSendNotification);

    editor.setRoom (&config.rooms[(size_t) selected]);
    resized();
    editorViewport.setViewPosition (position);
    lastEditAt = 0.0;
    configChanged();
}

bool RoomProView::undo()
{
    if (undoStack.empty())
        return false;

    redoStack.push_back (config);
    const auto previous = undoStack.back();
    undoStack.pop_back();
    restore (previous);
    return true;
}

bool RoomProView::redo()
{
    if (redoStack.empty())
        return false;

    undoStack.push_back (config);
    const auto next = redoStack.back();
    redoStack.pop_back();
    restore (next);
    return true;
}

bool RoomProView::keyPressed (const juce::KeyPress& key)
{
    // Option-Z: undo; Option-Shift-Z: redo (Cmd works too)
    const auto mods = key.getModifiers();

    if ((mods.isAltDown() || mods.isCommandDown()) && juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) key.getKeyCode()) == 'z')
    {
        if (mods.isShiftDown())     redo();
        else                        undo();

        return true;
    }

    return false;
}

void RoomProView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    for (int i = 0; i < ProRooms::count; ++i)
        if (cards[(size_t) i].contains (e.position))
            selectRoom (i);
}

void RoomProView::exportConfig()
{
    chooser = std::make_unique<juce::FileChooser> (tr ("Save the Room Sim Pro rooms"),
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Rooms.subguardrooms"),
                                                   "*.subguardrooms");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();

                              if (file != juce::File())
                                  file.withFileExtension ("subguardrooms").replaceWithText (juce::String (config.toText()));
                          });
}

void RoomProView::importConfig()
{
    chooser = std::make_unique<juce::FileChooser> (tr ("Load Room Sim Pro rooms"),
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.subguardrooms");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              ProRooms loaded;

                              if (file.existsAsFile() && ProRooms::fromText (file.loadFileAsString().toStdString(), loaded))
                              {
                                  setRooms (loaded);

                                  if (onConfigChanged)
                                      onConfigChanged();
                              }
                          });
}

} // namespace subguard
