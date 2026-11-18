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
void Room3DView::setRoom (const ProRoom& r, const std::vector<double>& seatPenalties)
{
    if (room == r && penalties == seatPenalties)
        return;

    room = r;
    penalties = seatPenalties;
    repaint();
}

juce::Point<float> Room3DView::project (double x, double y, double z, float* depth) const
{
    // View space: X across, Y up, Z along the room (front wall far away, at -L/2)
    const double X = y - room.width * 0.5, Y = z - room.height * 0.5, Z = x - room.length * 0.5;

    const double cy = std::cos (yaw), sy = std::sin (yaw), cp = std::cos (pitch), sp = std::sin (pitch);
    const double x1 = X * cy + Z * sy, z1 = -X * sy + Z * cy;
    const double y2 = Y * cp - z1 * sp, z2 = Y * sp + z1 * cp;

    const double size = std::max ({ room.length, room.width, room.height, 1.0 });
    const double distance = 1.6 * size * zoom;
    const double d = std::max (0.2, distance - z2);
    const double focal = 0.75 * std::min (getWidth(), getHeight()) * 1.6;

    if (depth != nullptr)
        *depth = (float) d;

    return { (float) (getWidth() * 0.5 + focal * x1 / d), (float) (getHeight() * 0.5 - focal * y2 / d) };
}

void Room3DView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1c20));

    const double L = room.length, W = room.width, H = room.height;

    // The six faces, the farthest first
    struct Face { std::array<std::array<double, 3>, 4> corners; float alpha; float depth; };
    std::vector<Face> faces = {
        { { { { 0, 0, 0 }, { 0, W, 0 }, { L, W, 0 }, { L, 0, 0 } } }, 0.30f, 0.0f },     // floor
        { { { { 0, 0, H }, { 0, W, H }, { L, W, H }, { L, 0, H } } }, 0.14f, 0.0f },     // ceiling
        { { { { 0, 0, 0 }, { 0, W, 0 }, { 0, W, H }, { 0, 0, H } } }, 0.26f, 0.0f },     // front (screen) wall
        { { { { L, 0, 0 }, { L, W, 0 }, { L, W, H }, { L, 0, H } } }, 0.05f, 0.0f },     // back wall, nearly open
        { { { { 0, 0, 0 }, { L, 0, 0 }, { L, 0, H }, { 0, 0, H } } }, 0.22f, 0.0f },     // left wall
        { { { { 0, W, 0 }, { L, W, 0 }, { L, W, H }, { 0, W, H } } }, 0.22f, 0.0f },     // right wall
    };

    for (auto& f : faces)
    {
        float sum = 0.0f;

        for (const auto& c : f.corners)
        {
            float d = 0.0f;
            project (c[0], c[1], c[2], &d);
            sum += d;
        }

        f.depth = sum / 4.0f;
    }

    std::sort (faces.begin(), faces.end(), [] (const Face& a, const Face& b) { return a.depth > b.depth; });

    for (const auto& f : faces)
    {
        juce::Path p;

        for (size_t i = 0; i < 4; ++i)
        {
            const auto pt = project (f.corners[i][0], f.corners[i][1], f.corners[i][2]);

            if (i == 0)     p.startNewSubPath (pt);
            else            p.lineTo (pt);
        }

        p.closeSubPath();
        const auto bounds = p.getBounds();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9a9da6).withAlpha (f.alpha), bounds.getCentreX(), bounds.getY(),
                                                 juce::Colour (0xff5d6068).withAlpha (f.alpha), bounds.getCentreX(), bounds.getBottom(), false));
        g.fillPath (p);
        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    // The screen on the front wall
    {
        juce::Path screen;
        const double x = 0.02;
        const std::array<std::array<double, 2>, 4> c { { { W * 0.12, H * 0.32 }, { W * 0.88, H * 0.32 }, { W * 0.88, H * 0.82 }, { W * 0.12, H * 0.82 } } };

        for (size_t i = 0; i < 4; ++i)
        {
            const auto pt = project (x, c[i][0], c[i][1]);

            if (i == 0)     screen.startNewSubPath (pt);
            else            screen.lineTo (pt);
        }

        screen.closeSubPath();
        const auto b = screen.getBounds();
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.42f), b.getX(), b.getY(),
                                                 juce::Colours::white.withAlpha (0.22f), b.getRight(), b.getBottom(), false));
        g.fillPath (screen);
    }

    // Speakers and seats, the farthest first
    struct Mark { float depth; juce::Point<float> at; juce::Colour colour; float size; juce::String label; bool square; };
    std::vector<Mark> marks;

    const auto addSpeaker = [&] (const ProSpeaker& s, juce::Colour colour, const juce::String& label)
    {
        if (! s.enabled)
            return;

        float d = 0.0f;
        const auto at = project (std::clamp (s.x, 0.0, L), std::clamp (s.y, 0.0, W), std::clamp (s.z, 0.0, H), &d);
        marks.push_back ({ d, at, colour, 0.0f, label, true });
    };

    const char* lcrNames[] = { "L", "C", "R" };

    for (int i = 0; i < 3; ++i)
        addSpeaker (room.lcr[(size_t) i], speakerColour, lcrNames[i]);

    for (int i = 0; i < std::clamp (room.numSubs, 1, ProRoom::maxSubs); ++i)
        addSpeaker (room.subs[(size_t) i], subColour, "S" + juce::String (i + 1));

    if (room.fillsOn)
        for (int i = 0; i < std::clamp (room.numFills, 1, ProRoom::maxFills); ++i)
            addSpeaker (room.fills[(size_t) i], fillColour, "F" + juce::String (i + 1));

    const auto seats = room.seatPositions();

    for (size_t i = 0; i < seats.size(); ++i)
    {
        float d = 0.0f;
        const auto at = project (seats[i][0], seats[i][1], seats[i][2], &d);
        const auto colour = i < penalties.size() && penalties.size() == seats.size() ? ChainsView::colourFor (penalties[i]).brighter (0.25f)
                                                                                     : juce::Colour (0xff8a8a8a);
        marks.push_back ({ d, at, colour, 0.0f, {}, false });
    }

    std::sort (marks.begin(), marks.end(), [] (const Mark& a, const Mark& b) { return a.depth > b.depth; });

    const float focal = 0.75f * (float) std::min (getWidth(), getHeight()) * 1.6f;

    for (const auto& m : marks)
    {
        const float scale = focal / m.depth;

        if (m.square)
        {
            const float s = juce::jlimit (6.0f, 22.0f, (float) (0.7 * scale));
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (m.at.x - s * 0.5f + 1.5f, m.at.y - s * 0.5f + 1.5f, s, s, 2.0f);
            g.setColour (m.colour);
            g.fillRoundedRectangle (m.at.x - s * 0.5f, m.at.y - s * 0.5f, s, s, 2.0f);
            g.setColour (juce::Colours::black);
            g.setFont (uiFont (juce::jmax (8.0f, s * 0.55f), true));
            g.drawText (m.label, juce::Rectangle<float> (m.at.x - s, m.at.y - s * 0.5f, 2.0f * s, s), juce::Justification::centred);
        }
        else
        {
            const float r = juce::jlimit (2.0f, 7.0f, (float) (0.28 * scale));
            g.setColour (m.colour.withAlpha (0.25f));
            g.fillEllipse (m.at.x - 2.0f * r, m.at.y - 2.0f * r, 4.0f * r, 4.0f * r);
            g.setColour (m.colour);
            g.fillEllipse (m.at.x - r, m.at.y - r, 2.0f * r, 2.0f * r);
        }
    }

    g.setColour (theme::dimText);
    g.setFont (uiFont (10.5f));
    g.drawText ("Drag to turn, wheel to zoom, double-click to reset.  White: L C R   Orange: subwoofers   Blue: fills",
                getLocalBounds().reduced (8, 4).removeFromBottom (14), juce::Justification::centredLeft);
}

void Room3DView::mouseDown (const juce::MouseEvent&)
{
    yawAtDrag = yaw;
    pitchAtDrag = pitch;
}

void Room3DView::mouseDrag (const juce::MouseEvent& e)
{
    yaw = yawAtDrag + e.getDistanceFromDragStartX() * 0.008;
    pitch = juce::jlimit (-1.4, 1.4, pitchAtDrag + e.getDistanceFromDragStartY() * 0.008);
    repaint();
}

void Room3DView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    zoom = juce::jlimit (0.5, 3.0, zoom * (wheel.deltaY > 0 ? 0.92 : 1.08));
    repaint();
}

void Room3DView::mouseDoubleClick (const juce::MouseEvent&)
{
    yaw = 0.0;
    pitch = 0.32;
    zoom = 1.0;
    repaint();
}

//==============================================================================
// Settings editor
void RoomProEditor::changed (bool structure)
{
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

void RoomProEditor::addHeader (const juce::String& text)
{
    Row row;
    row.header = true;
    row.label = std::make_unique<juce::Label> (juce::String(), text);
    row.label->setFont (uiFont (13.0f, true));
    row.label->setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (*row.label);
    rows.push_back (std::move (row));
}

juce::Slider& RoomProEditor::addSlider (const juce::String& label, double min, double max, double step, const juce::String& suffix,
                                        double& value, bool rebuildOnChange)
{
    Row row;
    row.label = std::make_unique<juce::Label> (juce::String(), label);
    row.label->setFont (uiFont (12.0f));
    row.label->setColour (juce::Label::textColourId, theme::dimText);

    auto slider = std::make_unique<juce::Slider> (juce::Slider::LinearBar, juce::Slider::TextBoxRight);
    slider->setRange (min, max, step);
    slider->setTextValueSuffix (suffix);
    slider->setValue (value, juce::dontSendNotification);
    auto* raw = slider.get();
    double* target = &value;
    slider->onValueChange = [this, raw, target, rebuildOnChange]
    {
        *target = raw->getValue();
        changed (rebuildOnChange);
    };

    addAndMakeVisible (*row.label);
    addAndMakeVisible (*slider);
    row.control = std::move (slider);
    rows.push_back (std::move (row));
    return *raw;
}

void RoomProEditor::addToggle (const juce::String& label, bool& value, bool rebuildOnChange)
{
    Row row;
    auto toggle = std::make_unique<juce::ToggleButton> (label);
    toggle->setToggleState (value, juce::dontSendNotification);
    auto* raw = toggle.get();
    bool* target = &value;
    toggle->onClick = [this, raw, target, rebuildOnChange]
    {
        *target = raw->getToggleState();
        changed (rebuildOnChange);
    };

    addAndMakeVisible (*toggle);
    row.control = std::move (toggle);
    rows.push_back (std::move (row));
}

void RoomProEditor::addChoice (const juce::String& label, const juce::StringArray& items, int& value, bool rebuildOnChange)
{
    Row row;
    row.label = std::make_unique<juce::Label> (juce::String(), label);
    row.label->setFont (uiFont (12.0f));
    row.label->setColour (juce::Label::textColourId, theme::dimText);

    auto box = std::make_unique<juce::ComboBox>();

    for (int i = 0; i < items.size(); ++i)
        box->addItem (items[i], i + 1);

    box->setSelectedId (value + 1, juce::dontSendNotification);
    auto* raw = box.get();
    int* target = &value;
    box->onChange = [this, raw, target, rebuildOnChange]
    {
        *target = raw->getSelectedId() - 1;
        changed (rebuildOnChange);
    };

    addAndMakeVisible (*row.label);
    addAndMakeVisible (*box);
    row.control = std::move (box);
    rows.push_back (std::move (row));
}

void RoomProEditor::addCount (const juce::String& label, int lowest, int highest, int& value, bool rebuildOnChange)
{
    Row row;
    row.label = std::make_unique<juce::Label> (juce::String(), label);
    row.label->setFont (uiFont (12.0f));
    row.label->setColour (juce::Label::textColourId, theme::dimText);

    auto box = std::make_unique<juce::ComboBox>();

    for (int n = lowest; n <= highest; ++n)
        box->addItem (juce::String (n), n);

    value = std::clamp (value, lowest, highest);
    box->setSelectedId (value, juce::dontSendNotification);
    auto* raw = box.get();
    int* target = &value;
    box->onChange = [this, raw, target, rebuildOnChange]
    {
        *target = raw->getSelectedId();
        changed (rebuildOnChange);
    };

    addAndMakeVisible (*row.label);
    addAndMakeVisible (*box);
    row.control = std::move (box);
    rows.push_back (std::move (row));
}

void RoomProEditor::addSpeaker (const juce::String& name, ProSpeaker& s, bool isFill)
{
    addHeader (name);
    addToggle ("On", s.enabled);
    xSliders.push_back (&addSlider ("Distance from the screen wall", 0.0, room->length, 0.05, " m", s.x));
    ySliders.push_back (&addSlider ("From the left wall", 0.0, room->width, 0.05, " m", s.y));
    zSliders.push_back (&addSlider ("Height", 0.0, room->height, 0.05, " m", s.z));
    addSlider ("Delay", 0.0, isFill ? 300.0 : 50.0, 0.1, " ms", s.delayMs);
    addSlider ("Level", -20.0, 20.0, 0.5, " dB", s.gainDb);
    addToggle ("Polarity inverted", s.inverted);

    if (isFill)
        addChoice ("Plays", { "Screen channels (L, C, R)", "LFE" }, s.feed);
}

void RoomProEditor::rebuild()
{
    rows.clear();
    xSliders.clear();
    ySliders.clear();
    zSliders.clear();
    removeAllChildren();

    if (room == nullptr)
        return;

    auto& r = *room;

    // Room
    addHeader ("Room");
    {
        Row row;
        row.label = std::make_unique<juce::Label> (juce::String(), "Name");
        row.label->setFont (uiFont (12.0f));
        row.label->setColour (juce::Label::textColourId, theme::dimText);
        auto text = std::make_unique<juce::TextEditor>();
        text->setText (juce::String (r.name), false);
        auto* raw = text.get();
        text->onTextChange = [this, raw] { room->name = raw->getText().toStdString(); changed (false); };
        addAndMakeVisible (*row.label);
        addAndMakeVisible (*text);
        row.control = std::move (text);
        rows.push_back (std::move (row));
    }

    {
        juce::StringArray presets { "Keep as it is" };

        for (const auto& spec : RoomSimulator::defaultRooms())
            presets.add ("Start from: " + juce::String (spec.name));

        presetChoice = 0;
        Row row;
        row.label = std::make_unique<juce::Label> (juce::String(), "Preset");
        row.label->setFont (uiFont (12.0f));
        row.label->setColour (juce::Label::textColourId, theme::dimText);
        auto box = std::make_unique<juce::ComboBox>();

        for (int i = 0; i < presets.size(); ++i)
            box->addItem (presets[i], i + 1);

        box->setSelectedId (1, juce::dontSendNotification);
        auto* raw = box.get();
        box->onChange = [this, raw]
        {
            const int choice = raw->getSelectedId() - 2;
            const auto specs = RoomSimulator::defaultRooms();

            if (choice >= 0 && choice < (int) specs.size())
            {
                const bool counted = room->counted;
                *room = ProRoom::fromSpec (specs[(size_t) choice]);
                room->counted = counted;
                changed (true);
            }
        };

        addAndMakeVisible (*row.label);
        addAndMakeVisible (*box);
        row.control = std::move (box);
        rows.push_back (std::move (row));
    }

    const auto resizeRanges = [this]
    {
        for (auto* s : xSliders)    s->setRange (0.0, room->length, 0.05);
        for (auto* s : ySliders)    s->setRange (0.0, room->width, 0.05);
        for (auto* s : zSliders)    s->setRange (0.0, room->height, 0.05);
    };

    for (auto* dimension : { &addSlider ("Length", 4.0, 60.0, 0.1, " m", r.length), &addSlider ("Width", 3.0, 40.0, 0.1, " m", r.width),
                             &addSlider ("Height", 2.5, 25.0, 0.1, " m", r.height) })
    {
        auto previous = dimension->onValueChange;
        dimension->onValueChange = [previous, resizeRanges] { previous(); resizeRanges(); };
    }

    addSlider ("Reverberation (RT60, low frequencies)", 0.1, 2.5, 0.01, " s", r.rt60);

    {
        Row row;
        row.label = std::make_unique<juce::Label> (juce::String(), "Treatment preset");
        row.label->setFont (uiFont (12.0f));
        row.label->setColour (juce::Label::textColourId, theme::dimText);
        auto box = std::make_unique<juce::ComboBox>();
        box->addItem ("Keep as it is", 1);
        box->addItem ("Live room (0.9 s)", 2);
        box->addItem ("Average room (0.55 s)", 3);
        box->addItem ("Treated room (0.4 s)", 4);
        box->addItem ("Dead room (0.25 s)", 5);
        box->setSelectedId (1, juce::dontSendNotification);
        auto* raw = box.get();
        box->onChange = [this, raw]
        {
            const double values[] = { 0.0, 0.0, 0.9, 0.55, 0.4, 0.25 };
            const int id = raw->getSelectedId();

            if (id >= 2 && id <= 5)
            {
                room->rt60 = values[id];
                changed (true);
            }
        };

        addAndMakeVisible (*row.label);
        addAndMakeVisible (*box);
        row.control = std::move (box);
        rows.push_back (std::move (row));
    }

    // Screen speakers
    const char* lcrNames[] = { "Left (L)", "Centre (C)", "Right (R)" };

    for (int i = 0; i < 3; ++i)
        addSpeaker (lcrNames[i], r.lcr[(size_t) i], false);

    // Subwoofers
    addHeader ("Subwoofers");
    addCount ("Number", 1, ProRoom::maxSubs, r.numSubs, true);

    for (int i = 0; i < std::clamp (r.numSubs, 1, ProRoom::maxSubs); ++i)
        addSpeaker ("Subwoofer " + juce::String (i + 1), r.subs[(size_t) i], false);

    // Fill speakers
    addHeader ("Fill speakers (rappels)");
    addToggle ("Use fill speakers", r.fillsOn, true);

    if (r.fillsOn)
    {
        addCount ("Number", 1, ProRoom::maxFills, r.numFills, true);

        for (int i = 0; i < std::clamp (r.numFills, 1, ProRoom::maxFills); ++i)
            addSpeaker ("Fill " + juce::String (i + 1), r.fills[(size_t) i], true);
    }

    // Audience
    addHeader ("Audience");
    addSlider ("First row, from the screen wall", 0.5, 60.0, 0.1, " m", r.firstRow);
    addSlider ("Last row, from the screen wall", 0.5, 60.0, 0.1, " m", r.lastRow);
    addSlider ("Width used", 0.1, 1.0, 0.01, "", r.audienceWidth);

    addCount ("Rows", 1, 12, r.rows, false);
    addCount ("Seats per row", 1, 12, r.perRow, false);
    addSlider ("Ear height (first row)", 0.8, 3.0, 0.05, " m", r.earHeight);
    addSlider ("Rake (last row raised by)", 0.0, 10.0, 0.05, " m", r.rake);

    // Calibration
    addHeader ("Calibration");
    addChoice ("Levels set at", { "Average of all seats", "Reference seat (2/3 of the room, centre)" }, r.calibration);

    resized();
}

void RoomProEditor::resized()
{
    int y = 4;
    const int width = getWidth();

    for (auto& row : rows)
    {
        if (row.header)
        {
            y += 8;
            row.label->setBounds (4, y, width - 8, 22);
            y += 24;
            continue;
        }

        if (row.label != nullptr)
        {
            row.label->setBounds (4, y, width * 45 / 100, 24);
            row.control->setBounds (width * 45 / 100 + 6, y + 1, width - width * 45 / 100 - 10, 22);
        }
        else
        {
            row.control->setBounds (4, y, width - 8, 24);
        }

        y += 27;
    }

    contentHeight = y + 8;
}

//==============================================================================
// The tab
RoomProView::RoomProView()
{
    config = ProRooms::defaults();

    for (int i = 0; i < ProRooms::count; ++i)
    {
        countedTicks[(size_t) i] = std::make_unique<juce::ToggleButton> ("Counts");
        countedTicks[(size_t) i]->setTooltip ("This room counts in the Room Sim Pro score.");
        countedTicks[(size_t) i]->onClick = [this, i]
        {
            config.rooms[(size_t) i].counted = countedTicks[(size_t) i]->getToggleState();
            configChanged();
        };
        addAndMakeVisible (*countedTicks[(size_t) i]);
    }

    addAndMakeVisible (view3d);
    editorViewport.setViewedComponent (&editor, false);
    editorViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (editorViewport);

    editor.onChange = [this] { configChanged(); };
    editor.onStructureChange = [this]
    {
        configChanged();
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RoomProView> (this)]
        {
            if (safe != nullptr)
            {
                safe->editor.rebuild();
                safe->resized();
            }
        });
    };

    saveButton.setTooltip ("Save the three rooms to a file.");
    loadButton.setTooltip ("Load three rooms from a file.");
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
    view3d.setRoom (config.rooms[(size_t) selected], results[(size_t) selected].seatPenalty);
    resized();
    repaint();
}

void RoomProView::configChanged()
{
    dirty = true;
    changedAt = now();
    view3d.setRoom (config.rooms[(size_t) selected], {});
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

void RoomProView::update (const Analysis& a, const Translation& electricalTranslation, const PlaybackSimulator::Tuning& tuning,
                          PlaybackSimulator::Level level, double lfeGainDb)
{
    // Collect a finished preparation, start the next one when the settings have settled
    if (job.valid() && job.wait_for (std::chrono::seconds (0)) == std::future_status::ready)
    {
        paths = job.get();
        pathsGrid = jobGrid;
        ready = true;
    }

    if (! job.valid() && ! a.grid.empty() && (dirty || a.grid != pathsGrid) && now() - changedAt > 0.25)
        startPreparing (a.grid);

    hasBass = electricalTranslation.hasBass;
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
            shownRoom[(size_t) i] += 0.1 * (results[(size_t) i].roomScore - shownRoom[(size_t) i]);
        }

        auto withRooms = electricalTranslation;
        RoomSimulator::applyToScore (withRooms, rooms, counted, tuning.levelDependenceWeight);
        proScore = withRooms.robustness;
        shownPro = shownPro < 0.0 ? proScore : shownPro + 0.1 * (proScore - shownPro);

        if (! dirty)
            view3d.setRoom (config.rooms[(size_t) selected], results[(size_t) selected].seatPenalty);
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
        g.drawText ("Room Sim Pro score", big.removeFromTop (16.0f), juce::Justification::centredLeft);
        const bool show = hasBass && shownPro >= 0.0;
        g.setColour (show ? ChainsView::colourFor (1.0 - shownPro / 100.0) : theme::dimText);
        g.setFont (uiFont (34.0f, true));
        g.drawText (show ? juce::String (juce::roundToInt (shownPro)) : juce::String ("--"), big, juce::Justification::centredLeft);

        top.removeFromRight (230.0f);       // the Soft / Medium / Strong selector
        top.removeFromRight (230.0f);       // save and load
        g.setColour (theme::dimText);
        g.setFont (uiFont (11.5f));
        g.drawFittedText ("Three rooms of your own, outside the cinema layout. Same rating as Room Sim and the same Dev settings and "
                          "listening level. Ticked rooms count in this score, which is not part of the global one. Electrical robustness "
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
        auto scores = inner.removeFromRight (120.0f);

        g.setColour (theme::text);
        g.setFont (uiFont (13.0f, true));
        g.drawText (juce::String (room.name), inner.removeFromTop (18.0f), juce::Justification::centredLeft);
        g.setColour (theme::dimText);
        g.setFont (uiFont (10.5f));
        g.drawText (juce::String (room.length, 1) + " x " + juce::String (room.width, 1) + " x " + juce::String (room.height, 1) + " m,  RT60 "
                      + juce::String (room.rt60, 2) + " s,  " + juce::String ((int) room.seatPositions().size()) + " seats"
                      + (room.fillsOn ? ",  fills" : ""),
                    inner.removeFromTop (14.0f), juce::Justification::centredLeft);

        // Seats on a -12..+12 dB line
        const auto& res = results[(size_t) i];
        auto line = inner.reduced (0.0f, 4.0f);
        const float mid = line.getCentreX(), y = line.getCentreY();
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (line.getX(), y, line.getWidth(), 1.0f);
        g.fillRect (mid, y - 5.0f, 1.0f, 10.0f);

        if (ready && hasBass && res.seatChangeDb.size() == res.seatPenalty.size())
            for (size_t s = 0; s < res.seatChangeDb.size(); ++s)
            {
                const float x = mid + (float) juce::jlimit (-1.0, 1.0, res.seatChangeDb[s] / 12.0) * line.getWidth() / 2.0f;
                g.setColour (ChainsView::colourFor (res.seatPenalty[s]).brighter (0.2f));
                g.fillEllipse (x - 2.5f, y - 2.5f, 5.0f, 5.0f);
            }

        const bool computing = dirty || job.valid();
        const double combined = electrical * shownRoom[(size_t) i] / 100.0;
        auto resultBox = scores.removeFromRight (56.0f).reduced (2.0f, 6.0f);
        g.setColour (hasBass && ready ? ChainsView::colourFor (1.0 - combined / 100.0) : juce::Colour (0xff2a2a2a));
        g.fillRoundedRectangle (resultBox, 4.0f);
        g.setColour (juce::Colours::white);
        g.setFont (uiFont (15.0f, true));
        g.drawText (hasBass && ready ? juce::String (juce::roundToInt (combined)) : juce::String ("--"), resultBox, juce::Justification::centred);
        g.setColour (theme::text);
        g.setFont (uiFont (11.0f));
        g.drawText (computing ? juce::String ("computing...") : "room " + juce::String (juce::roundToInt (shownRoom[(size_t) i])),
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
    auto cardRow = area.removeFromTop (62);
    const int cardWidth = (cardRow.getWidth() - 2 * 10) / ProRooms::count;

    for (int i = 0; i < ProRooms::count; ++i)
    {
        auto card = cardRow.removeFromLeft (cardWidth);
        cardRow.removeFromLeft (10);
        cards[(size_t) i] = card.toFloat();
        countedTicks[(size_t) i]->setBounds (card.getX() + 8, card.getCentreY() - 12, 72, 24);
    }

    area.removeFromTop (10);
    auto settings = area.removeFromRight (juce::jmax (360, area.getWidth() * 38 / 100));
    area.removeFromRight (10);
    view3d.setBounds (area);
    editorViewport.setBounds (settings);
    editor.setSize (settings.getWidth() - editorViewport.getScrollBarThickness() - 2, 100);
    editor.resized();
    editor.setSize (editor.getWidth(), editor.preferredHeight());
}

void RoomProView::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < ProRooms::count; ++i)
        if (cards[(size_t) i].contains (e.position))
            selectRoom (i);
}

void RoomProView::exportConfig()
{
    chooser = std::make_unique<juce::FileChooser> ("Save the Room Sim Pro rooms",
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
    chooser = std::make_unique<juce::FileChooser> ("Load Room Sim Pro rooms",
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
