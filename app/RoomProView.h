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

/** A room drawn in 3D: walls, screen, speakers, seats coloured by their rating. Drag to turn, wheel to zoom, double-click to reset. */
class Room3DView : public juce::Component
{
public:
    void setRoom (const ProRoom& r, const std::vector<double>& seatPenalties, const std::vector<double>& seatChangeDb = {});

    /** The worst point found between the seats (extra points), drawn when there is one below every seat. */
    void setWorstSpot (std::optional<std::array<double, 3>> spot, double score)
    {
        if (spot != worstSpot || score != worstSpotScore)
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
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Point<float> project (double x, double y, double z, float* depth = nullptr) const;

    ProRoom room;
    std::vector<double> penalties, changes;
    std::optional<std::array<double, 3>> worstSpot;
    double worstSpotScore = 100.0;
    std::vector<juce::Point<float>> seatPoints;     // where each seat was drawn
    int hovered = -1;
    double yaw = 0.0, pitch = 0.32, zoom = 1.0;
    double yawAtDrag = 0.0, pitchAtDrag = 0.0;
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
    Smoothed proSmooth;
    bool hasBass = false, ready = false;

    // Layout
    std::array<juce::Rectangle<float>, ProRooms::count> cards;
    std::array<std::unique_ptr<juce::ToggleButton>, ProRooms::count> countedTicks;
    Room3DView view3d;
    RoomProEditor editor;
    juce::Viewport editorViewport;
    juce::TextButton saveButton { tr ("Save config...") }, loadButton { tr ("Load config...") };
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace subguard
