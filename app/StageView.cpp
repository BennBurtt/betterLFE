#include "StageView.h"

namespace subguard
{

namespace
{
    constexpr int controlsHeight = 118;
    constexpr double speedOfSound = 343.0;
}

StageView::StageView()
{
    const auto label = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (uiFont (12.5f));
        l.setColour (juce::Label::textColourId, theme::dimText);
        addAndMakeVisible (l);
    };

    label (deviceLabel, tr ("Output device:"));
    label (screenLabel, tr ("Screen speaker on output:"));
    label (subLabel, tr ("Subwoofer on output:"));
    label (micLabel, tr ("Microphone on input:"));
    label (levelLabel, tr ("Sweep level:"));

    for (auto* box : { &deviceBox, &screenBox, &subBox, &micBox, &levelBox })
        addAndMakeVisible (box);

    for (int db : { -40, -30, -25, -20, -15, -10 })
        levelBox.addItem (juce::String (db) + " dBFS", db + 100);

    levelBox.setSelectedId (80, juce::dontSendNotification);

    measureButton.setButtonText (tr ("Measure"));
    measureButton.setTooltip (tr ("Plays a sweep on the screen speaker, then on the subwoofer, and records the microphone. Start low: "
                                  "the sweep goes from 15 to 600 Hz at the chosen level."));
    measureButton.onClick = [this] { if (onMeasure) onMeasure (choice()); };
    stopButton.setButtonText (tr ("Stop"));
    stopButton.onClick = [this] { if (onStop) onStop(); };
    addAndMakeVisible (measureButton);
    addChildComponent (stopButton);
}

StageView::Choice StageView::choice() const
{
    Choice c;
    c.outputDevice = deviceBox.getText();
    c.screenOutput = screenBox.getSelectedId() - 1;
    c.subOutput = subBox.getSelectedId() - 1;
    c.micInput = micBox.getSelectedId() - 1;
    c.levelDbfs = levelBox.getSelectedId() - 100;
    return c;
}

void StageView::setDevices (const juce::StringArray& outputDevices, const juce::String& current, int numOutputs, int numInputs)
{
    const auto keep = [] (juce::ComboBox& box, int count, int fallback, const juce::String& prefix)
    {
        const int was = box.getSelectedId();
        box.clear (juce::dontSendNotification);

        for (int i = 0; i < count; ++i)
            box.addItem (prefix + juce::String (i + 1), i + 1);

        box.setSelectedId (was >= 1 && was <= count ? was : juce::jmin (count, fallback + 1), juce::dontSendNotification);
    };

    const auto device = deviceBox.getText();
    deviceBox.clear (juce::dontSendNotification);
    deviceBox.addItemList (outputDevices, 1);
    const int index = outputDevices.indexOf (device.isNotEmpty() ? device : current);
    deviceBox.setSelectedItemIndex (juce::jmax (0, index), juce::dontSendNotification);

    keep (screenBox, juce::jmax (numOutputs, 8), 2, tr ("output "));
    keep (subBox, juce::jmax (numOutputs, 8), 3, tr ("output "));
    keep (micBox, juce::jmax (numInputs, 2), 0, tr ("input "));
}

void StageView::setStatus (const juce::String& text, bool isRunning)
{
    status = text;

    if (isRunning != running)
    {
        running = isRunning;
        stopButton.setVisible (running);
        measureButton.setEnabled (! running);
    }

    repaint();
}

void StageView::setResult (const StageAlignment::Result& r)
{
    result = r;
    repaint();
}

void StageView::resized()
{
    auto area = getLocalBounds().reduced (16, 10);
    area.removeFromTop (40);
    auto row = area.removeFromTop (26);
    deviceLabel.setBounds (row.removeFromLeft (110));
    deviceBox.setBounds (row.removeFromLeft (260));
    row.removeFromLeft (16);
    levelLabel.setBounds (row.removeFromLeft (90));
    levelBox.setBounds (row.removeFromLeft (110));
    area.removeFromTop (6);
    row = area.removeFromTop (26);
    screenLabel.setBounds (row.removeFromLeft (170));
    screenBox.setBounds (row.removeFromLeft (110));
    row.removeFromLeft (16);
    subLabel.setBounds (row.removeFromLeft (140));
    subBox.setBounds (row.removeFromLeft (110));
    row.removeFromLeft (16);
    micLabel.setBounds (row.removeFromLeft (140));
    micBox.setBounds (row.removeFromLeft (100));
    row.removeFromLeft (16);
    measureButton.setBounds (row.removeFromLeft (100));
    row.removeFromLeft (8);
    stopButton.setBounds (row.removeFromLeft (70));
}

void StageView::paint (juce::Graphics& g)
{
    g.fillAll (theme::plot);
    auto area = getLocalBounds().reduced (16, 10);
    auto title = area.removeFromTop (40);
    g.setColour (theme::text);
    g.setFont (uiFont (15.0f, true));
    g.drawText (tr ("Your stage: subwoofer and screen"), title.removeFromTop (20), juce::Justification::centredLeft);
    g.setColour (theme::dimText);
    g.setFont (uiFont (11.5f));
    g.drawFittedText (tr ("A measurement microphone at the mixing seat. The two outputs feed the screen speaker (C) and the subwoofer "
                          "(the LFE path) as they are set now, processor included. Nothing here changes the mix: it checks the room."),
                      title.toNearestInt(), juce::Justification::centredLeft, 2);

    area.removeFromTop (controlsHeight - 40);
    auto statusRow = area.removeFromTop (22);
    g.setColour (running ? juce::Colour (0xff3a9be0) : theme::dimText);
    g.setFont (uiFont (12.5f));
    g.drawText (status, statusRow, juce::Justification::centredLeft);

    if (! result.valid)
    {
        if (result.problem.empty())
            return;

        g.setColour (juce::Colour (0xffffb347));
        g.drawText (tr ("No result: ") + juce::String (result.problem), area.removeFromTop (22), juce::Justification::centredLeft);
        return;
    }

    // The verdict, in words
    {
        auto text = area.removeFromTop (70);
        const bool aligned = result.sumNowDb >= result.sumBestDb - 1.0;
        juce::String verdict;

        if (aligned)
            verdict = tr ("Aligned: around ") + juce::String (juce::roundToInt (result.crossoverHz)) + tr (" Hz the sum is within 1 dB of the best it can be (")
                      + juce::String (result.sumNowDb, 1) + " dB).";
        else
            verdict = tr ("Advice: ") + (result.bestInverted ? tr ("invert the subwoofer's polarity and ") : juce::String())
                      + tr ("add ") + (result.bestDelayMs >= 0.0 ? "+" : "") + juce::String (result.bestDelayMs, 2) + " ms ("
                      + juce::String (result.bestDelayMs * 0.001 * speedOfSound, 2) + " m)" + tr (" to the subwoofer's delay")
                      + (result.bestDelayMs < 0.0 ? tr (" (or delay the screen by as much)") : juce::String()) + ". "
                      + tr ("Around ") + juce::String (juce::roundToInt (result.crossoverHz)) + tr (" Hz the sum goes from ")
                      + juce::String (result.sumNowDb, 1) + tr (" dB to ") + juce::String (result.sumBestDb, 1) + tr (" dB against a perfect in-phase sum.");

        g.setColour (aligned ? juce::Colour (0xff7ed957) : juce::Colour (0xffffb347));
        g.setFont (uiFont (14.0f, true));
        g.drawFittedText (verdict, text.removeFromTop (44).toNearestInt(), juce::Justification::topLeft, 2);
        g.setColour (theme::dimText);
        g.setFont (uiFont (11.5f));
        g.drawFittedText (tr ("Arrivals (first rise of each response): screen ") + juce::String (result.screenArrivalMs, 1) + tr (" ms, subwoofer ")
                              + juce::String (result.subArrivalMs, 1) + " ms." + (result.nowInverted ? tr (" As it is, the subwoofer would sum better inverted.") : juce::String())
                              + tr (" Over an octave each side of the crossover; one seat only: check a few."),
                          text.toNearestInt(), juce::Justification::topLeft, 2);
    }

    // The responses, 20 to 200 Hz
    auto plot = area.reduced (0, 6).toFloat();
    g.setColour (juce::Colour (0xff1c1c1c));
    g.fillRoundedRectangle (plot, 4.0f);
    plot = plot.reduced (34.0f, 14.0f);

    double top = -1e9;

    for (const auto* curve : { &result.screenDb, &result.subDb, &result.sumDb, &result.bestDb })
        for (double v : *curve)
            top = std::max (top, v);

    top = std::ceil (top / 5.0) * 5.0 + 3.0;
    const double bottom = top - 40.0;
    const auto xOf = [&plot] (double hz) { return plot.getX() + plot.getWidth() * (float) (std::log (hz / 20.0) / std::log (10.0)); };
    const auto yOf = [&] (double db) { return plot.getY() + plot.getHeight() * (float) juce::jlimit (0.0, 1.0, (top - db) / (top - bottom)); };

    g.setFont (uiFont (10.0f));

    for (double hz : { 20.0, 30.0, 50.0, 80.0, 100.0, 150.0, 200.0 })
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (xOf (hz), plot.getY(), 1.0f, plot.getHeight());
        g.setColour (theme::dimText);
        g.drawText (juce::String ((int) hz), juce::Rectangle<float> (xOf (hz) - 15.0f, plot.getBottom() + 1.0f, 30.0f, 12.0f), juce::Justification::centred);
    }

    for (double db = bottom; db <= top + 1e-9; db += 10.0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (plot.getX(), yOf (db), plot.getWidth(), 1.0f);
        g.setColour (theme::dimText);
        g.drawText (juce::String ((int) std::round (db - top)), juce::Rectangle<float> (plot.getX() - 32.0f, yOf (db) - 6.0f, 28.0f, 12.0f), juce::Justification::centredRight);
    }

    // The crossover band, an octave each side
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (result.crossoverHz / 2.0), plot.getY(), xOf (std::min (200.0, result.crossoverHz * 2.0)), plot.getBottom()));

    const auto draw = [&] (const std::vector<double>& curve, juce::Colour colour, float width, bool dashed)
    {
        juce::Path p;

        for (size_t k = 0; k < curve.size() && k < result.hz.size(); ++k)
        {
            const auto pt = juce::Point<float> (xOf (result.hz[k]), yOf (curve[k]));

            if (k == 0)
                p.startNewSubPath (pt);
            else
                p.lineTo (pt);
        }

        g.setColour (colour);

        if (dashed)
        {
            juce::Path d;
            const float dashes[] = { 6.0f, 4.0f };
            juce::PathStrokeType (width).createDashedStroke (d, p, dashes, 2);
            g.fillPath (d);
        }
        else
        {
            g.strokePath (p, juce::PathStrokeType (width));
        }
    };

    draw (result.screenDb, juce::Colour (0xff3a9be0), 1.5f, false);
    draw (result.subDb, juce::Colour (0xffff8a00), 1.5f, false);
    draw (result.sumDb, juce::Colours::white, 2.2f, false);
    draw (result.bestDb, juce::Colour (0xff7ed957), 1.8f, true);

    // Legend
    auto legend = juce::Rectangle<float> (plot.getRight() - 300.0f, plot.getY() + 4.0f, 296.0f, 14.0f);
    const std::pair<juce::Colour, juce::String> items[] = { { juce::Colour (0xff3a9be0), tr ("screen") }, { juce::Colour (0xffff8a00), tr ("subwoofer") },
                                                            { juce::Colours::white, tr ("sum now") }, { juce::Colour (0xff7ed957), tr ("sum, as advised") } };

    for (const auto& [colour, name] : items)
    {
        auto cell = legend.removeFromLeft (74.0f);
        g.setColour (colour);
        g.fillRect (cell.getX(), cell.getCentreY() - 1.0f, 12.0f, 2.0f);
        g.setColour (theme::dimText);
        g.drawText (name, cell.withTrimmedLeft (16.0f), juce::Justification::centredLeft);
    }
}

} // namespace subguard
