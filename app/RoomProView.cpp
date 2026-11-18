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
        const auto p = room.positionOf (s);
        const auto at = project (std::clamp (p[0], 0.0, L), std::clamp (p[1], 0.0, W), std::clamp (p[2], 0.0, H), &d);
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
        *target = raw->getToggleState();
        changed (structure);
    };
    addAndMakeVisible (*button);
    items.back().buttons.push_back (std::move (button));
}

juce::Slider& RoomProEditor::knob (const juce::String& label, double min, double max, double step, const juce::String& suffix,
                                   std::function<double()> get, std::function<void (double)> set, std::function<double()> top)
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
    auto* raw = slider.get();
    slider->onValueChange = [this, raw, set]
    {
        set (raw->getValue());
        changed (false);
    };
    item.refresh = [raw, get, top, min, step]
    {
        if (top)
            raw->setRange (min, juce::jmax (min + step, top()), step);

        raw->setValue (get(), juce::dontSendNotification);
    };

    addAndMakeVisible (*item.label);
    addAndMakeVisible (*slider);
    item.control = std::move (slider);
    items.push_back (std::move (item));
    return *raw;
}

juce::Slider& RoomProEditor::knob (const juce::String& label, double min, double max, double step, const juce::String& suffix, double& value)
{
    double* target = &value;
    return knob (label, min, max, step, suffix, [target] { return *target; }, [target] (double v) { *target = v; });
}

juce::Slider& RoomProEditor::countKnob (const juce::String& label, int min, int max, int& value)
{
    int* target = &value;
    return knob (label, min, max, 1.0, {}, [target] { return (double) *target; }, [target] (double v) { *target = juce::roundToInt (v); });
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

void RoomProEditor::speaker (const juce::String& title, ProSpeaker& s, bool sub, bool isFill, bool* link)
{
    section (title, sub);

    if (link != nullptr)
        barToggle ("Link", "Linked: the right one mirrors the left one across the centre of the room.", *link, true, onGreen);

    barToggle (polaritySign, "Polarity inverted", s.inverted, false, polarityOrange);
    barToggle ("On", "Speaker on", s.enabled, false, onGreen);

    ProSpeaker* sp = &s;
    knob ("From front", 0.0, 1.0, 0.05, " m", [this, sp] { return sp->fx * room->length; },
          [this, sp] (double v) { sp->fx = v / room->length; }, [this] { return room->length; });
    knob ("From left", 0.0, 1.0, 0.05, " m", [this, sp] { return sp->fy * room->width; },
          [this, sp] (double v) { sp->fy = v / room->width; }, [this] { return room->width; });
    knob ("Height", 0.0, 1.0, 0.05, " m", [this, sp] { return sp->fz * room->height; },
          [this, sp] (double v) { sp->fz = v / room->height; }, [this] { return room->height; });
    knob ("Delay", 0.0, isFill ? 300.0 : 50.0, 0.1, " ms", s.delayMs);
    knob ("Level", -20.0, 20.0, 0.5, " dB", s.gainDb);

    if (isFill)
        choice ("Plays", { "L C R (screen channels)", "LFE" }, [sp] { return sp->feed; }, [sp] (int v) { sp->feed = v; }, false);
}

void RoomProEditor::rebuild()
{
    items.clear();
    removeAllChildren();

    if (room == nullptr)
        return;

    auto& r = *room;

    // Room
    section ("Room");
    {
        auto text = std::make_unique<juce::TextEditor>();
        text->setText (juce::String (r.name), false);
        text->setFont (uiFont (12.0f, true));
        auto* raw = text.get();
        text->onTextChange = [this, raw] { room->name = raw->getText().toStdString(); changed (false); };
        wide ("Name", std::move (text));
    }

    {
        auto box = std::make_unique<juce::ComboBox>();
        box->setTextWhenNothingSelected ("Choose a room...");
        box->addSectionHeading ("Cinemas");
        const auto specs = RoomSimulator::defaultRooms();

        for (size_t i = 0; i < specs.size(); ++i)
            box->addItem (specs[i].name, (int) i + 1);

        box->addSectionHeading ("Other venues");
        const auto venues = ProRoom::venues();

        for (size_t i = 0; i < venues.size(); ++i)
            box->addItem (juce::String (venues[i].name), 100 + (int) i);

        auto* raw = box.get();
        box->onChange = [this, raw]
        {
            const int id = raw->getSelectedId();
            const auto cinemas = RoomSimulator::defaultRooms();
            const auto others = ProRoom::venues();
            const bool counted = room->counted;

            if (id >= 1 && id <= (int) cinemas.size())
                *room = ProRoom::fromSpec (cinemas[(size_t) id - 1]);
            else if (id >= 100 && id < 100 + (int) others.size())
                *room = others[(size_t) id - 100];
            else
                return;

            room->counted = counted;
            changed (true);
        };
        wide ("Start from", std::move (box));
    }

    knob ("Length", 4.0, 60.0, 0.1, " m", r.length);
    knob ("Width", 3.0, 40.0, 0.1, " m", r.width);
    knob ("Height", 2.5, 25.0, 0.1, " m", r.height);

    // Acoustics
    section ("Acoustics");
    choice ("Decay", { "RT60 set by hand", "From the materials" }, [this] { return room->acoustics; },
            [this] (int v) { room->acoustics = v; }, true);

    if (r.acoustics == 0)
    {
        knob ("RT60", 0.1, 10.0, 0.01, " s", r.rt60).setSkewFactorFromMidPoint (1.0);
    }
    else
    {
        juce::StringArray names;

        for (const auto& m : ProRoom::materialList())
            names.add (juce::String (m.name) + "  (" + juce::String (m.alpha63, 2) + " / " + juce::String (m.alpha125, 2) + ")");

        const char* surfaces[] = { "Front wall", "Back wall", "Left wall", "Right wall", "Floor", "Ceiling" };

        for (int i = 0; i < ProRoom::numSurfaces; ++i)
            choice (surfaces[i], names, [this, i] { return room->materials[(size_t) i]; },
                    [this, i] (int v) { room->materials[(size_t) i] = juce::jmax (0, v); }, false);
    }

    info ([this]
    {
        const auto& rm = *room;
        juce::String text;

        if (rm.acoustics == 1)
            text << "RT60 (Eyring)  63 Hz: " << juce::String (rm.eyringRt60 (63.0), 2) << " s   125 Hz: "
                 << juce::String (rm.eyringRt60 (125.0), 2) << " s\n";
        else
            text << "Absorption in brackets: 63 Hz / 125 Hz\n";

        text << "Schroeder frequency " << juce::roundToInt (rm.schroederHz()) << " Hz: below it, single modes.";
        return text;
    });

    // Speakers
    if (r.linkLR)
    {
        speaker ("Left / Right", r.lcr[0], false, false, &r.linkLR);
    }
    else
    {
        speaker ("Left", r.lcr[0], false, false, &r.linkLR);
        speaker ("Right", r.lcr[2], false, false, nullptr);
    }

    speaker ("Centre", r.lcr[1], false, false, nullptr);

    section ("Subwoofers");
    choice ("Number", { "1", "2", "3", "4" }, [this] { return room->numSubs - 1; }, [this] (int v) { room->numSubs = v + 1; }, true);

    for (int i = 0; i < juce::jlimit (1, ProRoom::maxSubs, r.numSubs); ++i)
        speaker ("Sub " + juce::String (i + 1), r.subs[(size_t) i], true, false, nullptr);

    section ("Fill speakers");
    barToggle ("Link", "Linked: fills 2 and 4 mirror fills 1 and 3 across the centre of the room.", r.linkFills, true, onGreen);
    barToggle ("On", "Use fill speakers", r.fillsOn, true, onGreen);

    if (r.fillsOn)
    {
        if (r.linkFills)
        {
            choice ("Number", { "1 pair", "2 pairs" }, [this] { return room->numFills <= 2 ? 0 : 1; },
                    [this] (int v) { room->numFills = (v + 1) * 2; }, true);

            for (int pair = 0; pair < (r.numFills <= 2 ? 1 : 2); ++pair)
                speaker ("Fills " + juce::String (pair * 2 + 1) + " / " + juce::String (pair * 2 + 2), r.fills[(size_t) pair * 2],
                         true, true, nullptr);
        }
        else
        {
            choice ("Number", { "1", "2", "3", "4" }, [this] { return room->numFills - 1; }, [this] (int v) { room->numFills = v + 1; }, true);

            for (int i = 0; i < juce::jlimit (1, ProRoom::maxFills, r.numFills); ++i)
                speaker ("Fill " + juce::String (i + 1), r.fills[(size_t) i], true, true, nullptr);
        }
    }

    // Audience
    section ("Audience");
    knob ("First row", 0.0, 1.0, 0.1, " m", [this] { return room->firstRow * room->length; },
          [this] (double v) { room->firstRow = v / room->length; }, [this] { return room->length; });
    knob ("Last row", 0.0, 1.0, 0.1, " m", [this] { return room->lastRow * room->length; },
          [this] (double v) { room->lastRow = v / room->length; }, [this] { return room->length; });
    knob ("Width", 10.0, 100.0, 1.0, " %", [this] { return room->audienceWidth * 100.0; },
          [this] (double v) { room->audienceWidth = v / 100.0; });
    countKnob ("Rows", 1, 12, r.rows);
    countKnob ("Per row", 1, 12, r.perRow);
    knob ("Ear", 0.8, 3.0, 0.05, " m", r.earHeight);
    knob ("Rake", 0.0, 10.0, 0.05, " m", r.rake);

    section ("Calibration");
    choice ("Levels set at", { "All seats (average)", "Reference seat (2/3, centre)" }, [this] { return room->calibration; },
            [this] (int v) { room->calibration = v; }, false);

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
                      + juce::String (room.rt60At (63.0), 2) + " s,  " + juce::String ((int) room.seatPositions().size()) + " seats"
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
    auto settings = area.removeFromRight (RoomProEditor::preferredWidth + editorViewport.getScrollBarThickness() + 2);
    area.removeFromRight (10);
    view3d.setBounds (area);
    editorViewport.setBounds (settings);
    editor.setSize (RoomProEditor::preferredWidth, 100);
    editor.resized();
    editor.setSize (RoomProEditor::preferredWidth, editor.preferredHeight());
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
