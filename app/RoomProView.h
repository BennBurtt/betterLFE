// Room Sim Pro: three configurable rooms, a rotating 3D view, the settings of
// the selected room, its own score (not part of the global one), and saving
// and loading of the configuration.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Widgets.h"
#include "Lang.h"
#include "../core/Inertia.h"
#include "../core/RoomPro.h"

#include <array>
#include <deque>
#include <optional>
#include <functional>
#include <future>
#include <memory>
#include <vector>

namespace subguard
{

/** The camera shared by the 3D view and the layered view: turning one turns the other. */
struct ProCamera
{
    double yaw = 0.0, pitch = 0.32, zoom = 1.0;
};

/** Where a point of the room (metres) falls on a view of the given size. */
juce::Point<float> projectRoomPoint (const ProRoom&, const ProCamera&, int width, int height, double x, double y, double z, float* depth = nullptr);

/** Colour of a bass change against the approved mix (dB), shared by the heads and the layered view:
    grey within 0.5 dB, then blue to violet for a dip, yellow to red for a boost, full from 6 dB. */
juce::Colour bassChangeColour (double db);

/** A room drawn in 3D: walls, screen, speakers, and the heads coloured by the lens shown (the bass level, or the
    interference, against the approved mix). Drag to turn, wheel to zoom, double-click to reset. */
class Room3DView : public juce::Component
{
public:
    /** The room, each seat's score in the lens shown (penalty 0..1) and the change its head is coloured by (dB). */
    void setRoom (const ProRoom& r, const std::vector<double>& seatPenalties, const std::vector<double>& seatChangeDb = {});

    /** For the seat under the mouse: its level against the approved mix (all, L C R alone, LFE alone) and its interference. */
    void setDetails (const std::array<std::vector<double>, RoomResult::numParts>& partDb, const std::vector<double>& interferenceDb)
    {
        if (partDb != parts || interferenceDb != interference)
        {
            parts = partDb;
            interference = interferenceDb;
            repaint();
        }
    }

    /** What the heads' colour says, under the colour scale. */
    void setCaption (const juce::String& text)      { if (text != caption) { caption = text; repaint(); } }

    /** The worst point found between the seats (extra points), drawn when there is one below every seat. */
    void setWorstSpot (std::optional<std::array<double, 3>> spot, double score)
    {
        const bool samePlace = spot.has_value() == worstSpot.has_value()
                                 && (! spot.has_value() || (same ((*spot)[0], (*worstSpot)[0]) && same ((*spot)[1], (*worstSpot)[1])
                                                             && same ((*spot)[2], (*worstSpot)[2])));

        if (! samePlace || ! same (score, worstSpotScore))
        {
            worstSpot = spot;
            worstSpotScore = score;
            repaint();
        }
    }
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** Colour of a seat score (0..100), in many steps over the range where seats fall. */
    static juce::Colour seatColour (double score);

    /** What each head receives: loudness of the bass (phon), level under 40 Hz (dB SPL), shown under the mouse. */
    void setHeads (const std::vector<double>& heardPhon, const std::vector<double>& feltDb)
    {
        if (heardPhon != heard || feltDb != felt)
        {
            heard = heardPhon;
            felt = feltDb;
            repaint();
        }
    }

    void setCamera (std::shared_ptr<ProCamera> c)    { camera = std::move (c); }
    std::function<void()> onCameraChanged;          // the other view repaints
    std::function<void (int)> onHover;              // seat under the mouse, -1 for none

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Point<float> project (double x, double y, double z, float* depth = nullptr) const
    {
        return projectRoomPoint (room, *camera, getWidth(), getHeight(), x, y, z, depth);
    }

    ProRoom room;
    std::vector<double> penalties, changes;
    std::optional<std::array<double, 3>> worstSpot;
    double worstSpotScore = 100.0;
    std::vector<juce::Point<float>> seatPoints;     // where each seat was drawn
    std::vector<double> heard, felt;
    std::array<std::vector<double>, RoomResult::numParts> parts;
    std::vector<double> interference;
    juce::String caption;
    int hovered = -1;
    std::shared_ptr<ProCamera> camera = std::make_shared<ProCamera>();
    double yawAtDrag = 0.0, pitchAtDrag = 0.0;
};

/** The three layers of the bass stacked in the room: infra (under 20 Hz), very low (20-60 Hz), low (60-120 Hz).
    Each is a mesh over the floor, pinned to the walls at its neutral height: a boost lifts it, a dip lowers it, its
    colour says how much. It shows the lens chosen: where the bass goes (all of it, L C R or the LFE) or the interference. */
class RoomLayerView : public juce::Component
{
public:
    RoomLayerView();
    void resized() override;

    /** The room and its results (nothing to show when either is null), in a lens, with a part of the bass for the coverage. */
    void setData (const ProRoom&, const RoomPaths*, const RoomResult*, int lens, int part);
    void setHovered (int seat)                      { if (seat != hovered) { hovered = seat; repaint(); } }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    ProRoom room;
    bool hasData = false;
    int lens = RoomResult::coverageLens, part = RoomResult::allParts;
    int columns = 0, rows = 0;
    std::vector<int> index;
    std::vector<std::array<double, 3>> nodes, seats;
    std::array<std::vector<double>, RoomResult::numLayers> layers, seatLayers;
    int hovered = -1;
    ProCamera camera;                               // its own: turned independently of the 3D view
    double yawAtDrag = 0.0, pitchAtDrag = 0.0;
    juce::Slider spacing;                           // how far apart the three layers are (x the room height share)
    juce::Label spacingLabel;
    juce::Slider amplitude;                         // how far a dB moves a layer
    juce::Label amplitudeLabel;
};

/** Knobs in the style of a console panner: dark track, green arc, green-on-black read-out. */
class ProKnobLook : public juce::LookAndFeel_V4
{
public:
    ProKnobLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position, float startAngle, float endAngle,
                           juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

/** The settings of one room: panels of knobs, as on a console panner, in a scrolling column. */
class RoomProEditor : public juce::Component
{
public:
    static constexpr int knobWidth = 56, knobHeight = 76, columns = 5, preferredWidth = columns * knobWidth + 16;

    RoomProEditor();
    ~RoomProEditor() override;

    std::function<void()> onChange;             // any value changed
    std::function<void()> onStructureChange;    // the list of controls must be rebuilt
    std::function<void()> onGestureStart;       // a change is about to be made (for undo)

    void setRoom (ProRoom* r)                   { room = r; rebuild(); }
    void rebuild();
    void resized() override;
    void paint (juce::Graphics&) override;

    int preferredHeight() const                 { return contentHeight; }

private:
    struct Item
    {
        enum Kind { section, subsection, knob, wide, info } kind = knob;
        juce::String title;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Component> control;
        std::vector<std::unique_ptr<juce::TextButton>> buttons;     // on a section bar
        std::function<void()> refresh;                              // brings the control back to the room's values
    };

    void section (const juce::String& title, bool sub = false);
    void barToggle (const juce::String& text, const juce::String& tip, bool& value, bool structure, juce::Colour on);
    juce::Slider& knob (const juce::String& label, double min, double max, double step, const juce::String& suffix,
                        std::function<double()> get, std::function<void (double)> set, std::function<double()> top = {},
                        std::function<double()> fallback = {});     // fallback: the value a double-click goes back to
    juce::Slider& knob (const juce::String& label, double min, double max, double step, const juce::String& suffix, double& value,
                        double fallback);
    juce::Slider& countKnob (const juce::String& label, int min, int max, int& value, int fallback);
    juce::ComboBox& choice (const juce::String& label, const juce::StringArray& entries, std::function<int()> get,
                            std::function<void (int)> set, bool structure);
    void wide (const juce::String& label, std::unique_ptr<juce::Component>);
    void info (std::function<juce::String()> text);
    void speaker (const juce::String& title, ProSpeaker&, const ProSpeaker& reference, bool sub, bool isFill, bool* link);
    void changed (bool structure);
    void refreshAll();

    ProKnobLook look;
    ProRoom* room = nullptr;
    std::vector<Item> items;
    std::vector<std::pair<juce::Rectangle<int>, int>> bars;     // section bars laid out, and their kind
    std::vector<juce::Rectangle<int>> panels;
    int contentHeight = 0;
};

class RoomProView : public juce::Component
{
public:
    RoomProView();
    ~RoomProView() override;

    void setRooms (const ProRooms&);
    const ProRooms& getRooms() const            { return config; }
    std::function<void()> onConfigChanged;      // to save it

    /** Each frame while shown. `electrical` is the playback simulation without any room. */
    /** `seconds`: time since the last call; `scoreSpeed`: the Score speed setting (0..1), the same smoothing as the global score. */
    void update (const Analysis&, const Translation& electrical, const PlaybackSimulator::Tuning&,
                 PlaybackSimulator::Level, double lfeGainDb, double seconds, double scoreSpeed);

    /** Restarts the shown scores (the Reset button). */
    void resetScores();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Undo and redo the changes to the rooms (Option-Z, Option-Shift-Z), up to 30 steps. */
    bool undo();
    bool redo();

private:
    static constexpr size_t maxUndo = 30;
    void rememberForUndo();
    void restore (const ProRooms&);
    std::deque<ProRooms> undoStack, redoStack;
    double lastEditAt = 0.0;

    void selectRoom (int index);
    void configChanged();
    void showSelected();            // the selected room's results to the two views, in the lens chosen
    void startPreparing (const std::vector<double>& grid);
    void exportConfig();
    void importConfig();

    ProRooms config;
    int selected = 0;

    // Transfer functions, prepared in the background after every change
    std::array<RoomPaths, ProRooms::count> paths;
    std::vector<double> pathsGrid;
    std::future<std::array<RoomPaths, ProRooms::count>> job;
    std::vector<double> jobGrid;
    bool dirty = true;
    double changedAt = 0.0;

    // Results
    std::array<RoomResult, ProRooms::count> results;
    std::array<double, ProRooms::count> shownRoom { { 100.0, 100.0, 100.0 } };
    double proScore = 100.0, shownPro = -1.0, electrical = 100.0;

    // The same smoothing as the global score: display inertia, then Score speed,
    // then the number only moves once it has really moved (1.5 points)
    struct Smoothed
    {
        Inertia inertia;
        double slow = -1.0, shown = -1.0;
        double update (double value, double seconds, double scoreSpeed);
        void reset()    { inertia.reset(); slow = shown = -1.0; }
    };
    std::array<Smoothed, ProRooms::count> roomSmooth;
    std::array<std::array<Smoothed, RoomResult::numLenses>, ProRooms::count> lensSmooth;
    std::array<std::array<double, RoomResult::numLenses>, ProRooms::count> shownLens {};
    Smoothed proSmooth;

    // The lens: where the bass goes (for the venue: all of it, L C R alone or the LFE alone),
    // or the interference between the LFE and L C R (for the mix)
    int lens = RoomResult::coverageLens, part = RoomResult::allParts;
    std::array<juce::TextButton, RoomResult::numLenses> lensButtons;
    std::array<juce::TextButton, RoomResult::numParts> partButtons;
    juce::Rectangle<int> lensRow;
    bool hasBass = false, ready = false;

    // Layout
    std::array<juce::Rectangle<float>, ProRooms::count> cards;
    std::array<std::unique_ptr<juce::ToggleButton>, ProRooms::count> countedTicks;
    Room3DView view3d;
    RoomLayerView layerView;
    std::shared_ptr<ProCamera> camera = std::make_shared<ProCamera>();
    RoomProEditor editor;
    juce::Viewport editorViewport;
    juce::TextButton settingsButton;
    bool settingsOpen = true;
    juce::TextButton saveButton { tr ("Save config...") }, loadButton { tr ("Load config...") };
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace subguard
