#include "TestView.h"

namespace subguard
{

namespace
{
    constexpr int rowHeight = 58, headerHeight = 34;

    const juce::String titleOf (const TestScene& s)       { return juce::String::fromUTF8 ((currentLanguage() == Language::french ? s.titleFr : s.title).c_str()); }
    const juce::String lookForOf (const TestScene& s)     { return juce::String::fromUTF8 ((currentLanguage() == Language::french ? s.lookForFr : s.lookFor).c_str()); }
}

TestView::TestView()
{
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    useButton.setButtonText (tr ("Use the test signals as input"));
    useButton.setTooltip (tr ("Same as choosing \"SubGUARD test signals\" as the input device in Source."));
    useButton.onClick = [this] { if (onUseTestSource) onUseTestSource(); };
    addAndMakeVisible (useButton);

    stopButton.setButtonText (tr ("Stop"));
    stopButton.onClick = [this] { if (onPlay) onPlay (-1); };
    addAndMakeVisible (stopButton);

    roomsButton.setButtonText (tr ("Load the rooms of the measurements"));
    roomsButton.setTooltip (tr ("Room Sim Pro: the three rooms the Room Sim Pro signals were measured in (small screening room, "
                                "multiplex 200 seats, premium large format). Undo with Option-Z."));
    roomsButton.onClick = [this] { if (onLoadRooms) onLoadRooms(); };
    addAndMakeVisible (roomsButton);
}

void TestView::setState (bool testSourceOn, int now, double seconds)
{
    const bool changed = testSourceOn != sourceOn || now != playing;
    sourceOn = testSourceOn;
    playing = now;
    position = seconds;
    useButton.setVisible (! sourceOn);

    if (changed)
        resized();

    repaint();
    list.repaint();
}

void TestView::resized()
{
    auto area = getLocalBounds().reduced (16, 10);
    auto top = area.removeFromTop (64);
    auto buttons = top.removeFromBottom (28);
    roomsButton.setBounds (buttons.removeFromRight (260));
    buttons.removeFromRight (8);
    stopButton.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (8);
    useButton.setBounds (buttons.removeFromRight (240));
    area.removeFromTop (8);
    viewport.setBounds (area);
    list.setSize (area.getWidth() - viewport.getScrollBarThickness(), list.preferredHeight());
}

void TestView::paint (juce::Graphics& g)
{
    g.fillAll (theme::plot);
    auto area = getLocalBounds().reduced (16, 10).removeFromTop (36);
    g.setColour (theme::text);
    g.setFont (uiFont (15.0f, true));
    g.drawText (tr ("Test signals"), area.removeFromTop (18), juce::Justification::centredLeft);
    g.setFont (uiFont (11.5f));
    g.setColour (sourceOn ? juce::Colour (0xff7ed957) : juce::Colour (0xffffb347));
    g.drawText (sourceOn ? tr ("Input: the test signals. Click a signal to play it; it loops. Level Strong, LFE +10 dB checked, as measured.")
                         : tr ("To hear them: Source > input device > \"SubGUARD test signals\" (or the button below)."),
                area, juce::Justification::centredLeft);
}

//==============================================================================
std::vector<TestView::List::Row> TestView::List::layout() const
{
    std::vector<Row> rows;
    const auto& scenes = TestSignals::all();
    int y = 0;

    for (int group = 0; group < 2; ++group)
    {
        y += headerHeight;

        for (size_t i = 0; i < scenes.size(); ++i)
            if (scenes[i].roomSimPro == (group == 1))
            {
                rows.push_back ({ (int) i, juce::Rectangle<int> (0, y, getWidth(), rowHeight - 4) });
                y += rowHeight;
            }
    }

    return rows;
}

int TestView::List::preferredHeight() const
{
    const auto& scenes = TestSignals::all();
    return 2 * headerHeight + (int) scenes.size() * rowHeight + 8;
}

void TestView::List::paint (juce::Graphics& g)
{
    const auto& scenes = TestSignals::all();
    const juce::String headers[] = { tr ("The analysis: score, alerts, transients"),
                                     tr ("Room Sim Pro: one thing at a time (open the Room Sim Pro tab while it plays)") };
    // The two group headers: above the first row, and above the first Room Sim Pro row
    int analysisRows = 0;

    for (const auto& scene : scenes)
        analysisRows += scene.roomSimPro ? 0 : 1;

    g.setColour (theme::text);
    g.setFont (uiFont (13.0f, true));
    g.drawText (headers[0], 0, 0, getWidth(), headerHeight - 6, juce::Justification::bottomLeft);
    g.drawText (headers[1], 0, headerHeight + analysisRows * rowHeight, getWidth(), headerHeight - 6, juce::Justification::bottomLeft);

    for (const auto& row : layout())
    {
        const auto& scene = scenes[(size_t) row.scene];
        const bool isPlaying = row.scene == owner.playing;
        auto r = row.bounds.toFloat();
        g.setColour (isPlaying ? juce::Colour (0xff23384d) : (row.scene == hovered ? juce::Colour (0xff222428) : juce::Colour (0xff1b1c1f)));
        g.fillRoundedRectangle (r, 4.0f);

        if (isPlaying)
        {
            g.setColour (juce::Colour (0xff3a9be0));
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.5f);

            // How far into the pass
            const float share = (float) juce::jlimit (0.0, 1.0, owner.position / scene.seconds);
            g.fillRect (r.getX() + 4.0f, r.getBottom() - 3.0f, (r.getWidth() - 8.0f) * share, 2.0f);
        }

        auto text = row.bounds.reduced (10, 5);
        auto first = text.removeFromTop (18);
        g.setColour (isPlaying ? juce::Colour (0xff7ec8ff) : theme::dimText);
        g.setFont (uiFont (11.0f, true));
        g.drawText (isPlaying ? juce::String::fromUTF8 ("\xe2\x96\xb6 ") + juce::String (scene.file) : juce::String (scene.file),
                    first.removeFromLeft (260), juce::Justification::centredLeft);
        g.setColour (theme::text);
        g.setFont (uiFont (12.5f, true));
        g.drawText (titleOf (scene), first, juce::Justification::centredLeft);
        g.setColour (theme::dimText);
        g.setFont (uiFont (11.5f));
        g.drawFittedText (lookForOf (scene), text, juce::Justification::topLeft, 2, 1.0f);
    }
}

void TestView::List::mouseDown (const juce::MouseEvent& e)
{
    for (const auto& row : layout())
        if (row.bounds.contains (e.getPosition()))
        {
            if (owner.onPlay)
                owner.onPlay (row.scene);

            return;
        }
}

void TestView::List::mouseMove (const juce::MouseEvent& e)
{
    int now = -1;

    for (const auto& row : layout())
        if (row.bounds.contains (e.getPosition()))
            now = row.scene;

    if (now != hovered)
    {
        hovered = now;
        repaint();
    }
}

void TestView::List::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

} // namespace subguard
