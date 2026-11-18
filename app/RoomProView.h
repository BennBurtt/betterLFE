// Room Sim Pro: three configurable rooms, a rotating 3D view, the settings of
// the selected room, its own score (not part of the global one), and saving
// and loading of the configuration.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Widgets.h"
#include "../core/RoomPro.h"

#include <array>
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
    void setRoom (const ProRoom& r, const std::vector<double>& seatPenalties);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Point<float> project (double x, double y, double z, float* depth = nullptr) const;

    ProRoom room;
    std::vector<double> penalties;
    double yaw = 0.0, pitch = 0.32, zoom = 1.0;
    double yawAtDrag = 0.0, pitchAtDrag = 0.0;
};

/** The settings of one room, in a scrolling list. */
class RoomProEditor : public juce::Component
{
public:
    std::function<void()> onChange;             // any value changed
    std::function<void()> onStructureChange;    // the list of controls must be rebuilt

    void setRoom (ProRoom* r)                   { room = r; rebuild(); }
    void rebuild();
    void resized() override;

    int preferredHeight() const                 { return contentHeight; }

private:
    struct Row
    {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Component> control;
        bool header = false;
    };

    void addHeader (const juce::String&);
    juce::Slider& addSlider (const juce::String& label, double min, double max, double step, const juce::String& suffix,
                             double& value, bool rebuildOnChange = false);
    void addToggle (const juce::String& label, bool& value, bool rebuildOnChange = false);
    void addChoice (const juce::String& label, const juce::StringArray& items, int& value, bool rebuildOnChange = false);
    void addCount (const juce::String& label, int lowest, int highest, int& value, bool rebuildOnChange);   // value shown as is
    void addSpeaker (const juce::String& name, ProSpeaker&, bool isFill);
    void changed (bool structure);

    ProRoom* room = nullptr;
    std::vector<Row> rows;
    std::vector<juce::Slider*> xSliders, ySliders, zSliders;
    int contentHeight = 0;
    int presetChoice = 0;
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
    void update (const Analysis&, const Translation& electrical, const PlaybackSimulator::Tuning&,
                 PlaybackSimulator::Level, double lfeGainDb);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
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
    bool hasBass = false, ready = false;

    // Layout
    std::array<juce::Rectangle<float>, ProRooms::count> cards;
    std::array<std::unique_ptr<juce::ToggleButton>, ProRooms::count> countedTicks;
    Room3DView view3d;
    RoomProEditor editor;
    juce::Viewport editorViewport;
    juce::TextButton saveButton { "Save config..." }, loadButton { "Load config..." };
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace subguard
