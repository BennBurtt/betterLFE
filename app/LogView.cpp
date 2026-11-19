#include "LogView.h"

namespace subguard
{

namespace
{
    constexpr int rowHeight = 22, headerHeight = 22, topHeight = 96;

    juce::Colour colourOf (EventKind k)
    {
        switch (k)
        {
            case EventKind::phaseAlert:         return juce::Colour (0xffe0261b);
            case EventKind::lfeTiming:          return juce::Colour (0xffffb347);
            case EventKind::lowRobustness:      return juce::Colour (0xff3a9be0);
            case EventKind::lfeCarriesBass:     return juce::Colour (0xffff8a00);
            case EventKind::screenAntiphase:    return otherSystemColour;
            case EventKind::deliverable:        return juce::Colour (0xff4dd0e1);
            default:                            return theme::text;
        }
    }
}

LogView::LogView()
{
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    const auto label = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (uiFont (12.5f));
        l.setColour (juce::Label::textColourId, theme::dimText);
        addAndMakeVisible (l);
    };

    label (sourceLabel, tr ("Timecode:"));
    label (rateLabel, tr ("Frame rate:"));
    label (startLabel, tr ("Session clock starts at:"));

    sourceBox.onChange = [this]
    {
        const int id = sourceBox.getSelectedId();
        startEditor.setEnabled (id == 1);

        if (onTimecodeSource)
            onTimecodeSource (id <= 1 ? -1 : id - 2);
    };
    sourceBox.setTooltip (tr ("LTC: the input channel your workstation sends its timecode on (Pro Tools: a sync peripheral or an LTC output "
                              "patched to an input). Without it, a clock that starts at the time below when the log is cleared."));
    addAndMakeVisible (sourceBox);

    rateBox.addItem (tr ("As read from the LTC"), 1);

    for (int r = 0; r < (int) FrameRate::numRates; ++r)
        rateBox.addItem (Timecode::name ((FrameRate) r), r + 2);

    rateBox.setSelectedId (1, juce::dontSendNotification);
    rateBox.onChange = [this] { if (onRate) onRate (rateBox.getSelectedId() <= 1 ? -1 : rateBox.getSelectedId() - 2); };
    addAndMakeVisible (rateBox);

    startEditor.setText ("01:00:00:00", false);
    startEditor.setJustification (juce::Justification::centred);
    startEditor.setFont (uiFont (13.0f));
    startEditor.onReturnKey = startEditor.onFocusLost = [this]
    {
        const int id = rateBox.getSelectedId();
        const auto rate = id <= 1 ? FrameRate::fps25 : (FrameRate) (id - 2);
        double seconds = 0.0;

        if (Timecode::parse (startEditor.getText().toStdString(), rate, seconds))
        {
            startEditor.setColour (juce::TextEditor::textColourId, theme::text);

            if (onStart)
                onStart (seconds);
        }
        else
        {
            startEditor.setColour (juce::TextEditor::textColourId, juce::Colour (0xffff6b6b));
        }

        startEditor.applyColourToAllText (startEditor.findColour (juce::TextEditor::textColourId));
    };
    addAndMakeVisible (startEditor);

    clearButton.setButtonText (tr ("Clear"));
    clearButton.setTooltip (tr ("Empties the log; the session clock starts again at its start time."));
    clearButton.onClick = [this] { if (onClear) onClear(); };
    csvButton.setButtonText (tr ("Export CSV..."));
    csvButton.onClick = [this] { if (onExportCsv) onExportCsv(); };
    edlButton.setButtonText (tr ("Export markers (EDL)..."));
    edlButton.setTooltip (tr ("An edit decision list whose notes are markers, as DaVinci Resolve imports them (Timeline > Import > Timeline Markers from EDL)."));
    edlButton.onClick = [this] { if (onExportEdl) onExportEdl(); };

    filesButton.setButtonText (tr ("Analyse files..."));
    filesButton.setTooltip (tr ("Reels or any mix, as WAV, BWF or AIFF: one multichannel file, or one mono file per channel (..._L, ..._C, ..._R, "
                                "..._LFE). Analysed as the live input is, faster than real time, with a report per reel."));
    filesButton.onClick = [this] { if (onAnalyseFiles) onAnalyseFiles(); };
    cancelButton.setButtonText (tr ("Stop"));
    cancelButton.onClick = [this] { if (onCancelAnalysis) onCancelAnalysis(); };

    for (auto* b : { &clearButton, &csvButton, &edlButton, &filesButton })
        addAndMakeVisible (b);

    addChildComponent (cancelButton);

    fillSources (0);
}

void LogView::fillSources (int numInputs)
{
    const int keep = sourceBox.getSelectedId();
    sourceBox.clear (juce::dontSendNotification);
    sourceBox.addItem (tr ("Session clock (no LTC)"), 1);

    for (int i = 0; i < numInputs; ++i)
        sourceBox.addItem (tr ("LTC on input ") + juce::String (i + 1), i + 2);

    sourceBox.setSelectedId (keep >= 1 && keep <= numInputs + 1 ? keep : 1, juce::dontSendNotification);
    inputs = numInputs;
}

void LogView::setChoices (int ltcChannel, int rate, double startSeconds, int numInputs)
{
    fillSources (juce::jmax (numInputs, ltcChannel + 1));
    sourceBox.setSelectedId (ltcChannel >= 0 ? ltcChannel + 2 : 1, juce::dontSendNotification);
    rateBox.setSelectedId (rate >= 0 ? rate + 2 : 1, juce::dontSendNotification);
    startEditor.setEnabled (ltcChannel < 0);
    startEditor.setText (Timecode::labelOfSeconds (startSeconds, rate >= 0 ? (FrameRate) rate : FrameRate::fps25), false);
}

void LogView::setProgress (bool running, const juce::String& text, double fraction)
{
    if (running != analysing)
    {
        analysing = running;
        cancelButton.setVisible (running);
        filesButton.setEnabled (! running);
    }

    progressText = text;
    progressFraction = fraction;
    repaint();
}

void LogView::setState (const std::vector<LogEvent>& events, FrameRate rate, const juce::String& clock, const juce::String& source, bool clockOk)
{
    const bool grew = events.size() != shown.size();
    shown = events;
    shownRate = rate;
    clockText = clock;
    sourceText = source;
    clockGood = clockOk;

    if (grew)
    {
        resized();
        viewport.setViewPosition (0, juce::jmax (0, list.getHeight() - viewport.getHeight()));     // follow the newest
    }

    repaint();
    list.repaint();
}

void LogView::resized()
{
    auto area = getLocalBounds().reduced (16, 10);
    auto top = area.removeFromTop (topHeight);
    top.removeFromTop (40);
    auto row = top.removeFromTop (26);
    sourceLabel.setBounds (row.removeFromLeft (80));
    sourceBox.setBounds (row.removeFromLeft (200));
    row.removeFromLeft (16);
    rateLabel.setBounds (row.removeFromLeft (84));
    rateBox.setBounds (row.removeFromLeft (170));
    row.removeFromLeft (16);
    startLabel.setBounds (row.removeFromLeft (160));
    startEditor.setBounds (row.removeFromLeft (110));
    top.removeFromTop (4);
    auto buttons = top.removeFromTop (26);
    edlButton.setBounds (buttons.removeFromRight (190));
    buttons.removeFromRight (8);
    csvButton.setBounds (buttons.removeFromRight (120));
    buttons.removeFromRight (8);
    clearButton.setBounds (buttons.removeFromRight (90));
    filesButton.setBounds (buttons.removeFromLeft (160));
    buttons.removeFromLeft (8);
    cancelButton.setBounds (buttons.removeFromLeft (70));
    viewport.setBounds (area);
    list.setSize (area.getWidth() - viewport.getScrollBarThickness(), juce::jmax (area.getHeight(), list.preferredHeight()));
}

void LogView::paint (juce::Graphics& g)
{
    g.fillAll (theme::plot);
    auto area = getLocalBounds().reduced (16, 10).removeFromTop (40);
    auto title = area.removeFromTop (20);
    g.setColour (theme::text);
    g.setFont (uiFont (15.0f, true));
    g.drawText (tr ("Event log"), title.removeFromLeft (120), juce::Justification::centredLeft);
    g.setFont (uiFont (17.0f, true));
    g.setColour (clockGood ? theme::text : juce::Colour (0xffffb347));
    g.drawText (clockText, title.removeFromLeft (150), juce::Justification::centredLeft);
    g.setFont (uiFont (11.5f));
    g.setColour (clockGood ? theme::dimText : juce::Colour (0xffffb347));
    g.drawText (sourceText, title, juce::Justification::centredLeft);
    if (analysing)
    {
        // The offline analysis, over the row of buttons
        auto bar = getLocalBounds().reduced (16, 10).withTrimmedTop (topHeight - 26).withHeight (26).withTrimmedLeft (246).withTrimmedRight (420);
        g.setColour (juce::Colour (0xff2a2a2a));
        g.fillRoundedRectangle (bar.toFloat(), 4.0f);
        g.setColour (juce::Colour (0xff3a9be0).withAlpha (0.6f));
        g.fillRoundedRectangle (bar.toFloat().withWidth ((float) bar.getWidth() * (float) juce::jlimit (0.0, 1.0, progressFraction)), 4.0f);
        g.setColour (theme::text);
        g.setFont (uiFont (12.0f));
        g.drawText (progressText, bar.reduced (8, 0), juce::Justification::centredLeft);
    }

    g.setColour (theme::dimText);
    g.drawFittedText (tr ("Stamped when SubGUARD sees it: the Phase Alert averages over its Integration time, so an alert can start up to that "
                          "long after its cause. Attacks of the LFE are stamped to the analysis step (20 ms)."),
                      area.toNearestInt(), juce::Justification::centredLeft, 1);
}

//==============================================================================
int LogView::List::preferredHeight() const
{
    return headerHeight + rowHeight * (int) juce::jmax ((size_t) 1, owner.shown.size()) + 4;
}

void LogView::List::paint (juce::Graphics& g)
{
    const float widths[] = { 92.0f, 92.0f, 200.0f, 44.0f };
    auto area = getLocalBounds().toFloat();
    auto head = area.removeFromTop ((float) headerHeight);
    g.setColour (theme::dimText);
    g.setFont (uiFont (11.5f));
    const juce::String titles[] = { tr ("Start"), tr ("End"), tr ("What"), tr ("Ch."), tr ("Detail") };

    for (int i = 0; i < 5; ++i)
        g.drawText (titles[i], i < 4 ? head.removeFromLeft (widths[i]) : head, juce::Justification::centredLeft);

    if (owner.shown.empty())
    {
        g.drawText (tr ("Nothing logged yet."), area.removeFromTop ((float) rowHeight), juce::Justification::centredLeft);
        return;
    }

    for (size_t i = 0; i < owner.shown.size(); ++i)
    {
        const auto& e = owner.shown[i];
        auto row = area.removeFromTop ((float) rowHeight);

        if (i % 2 == 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.03f));
            g.fillRect (row);
        }

        g.setFont (uiFont (12.5f));
        g.setColour (theme::text);
        g.drawText (Timecode::labelOfSeconds (e.start, owner.shownRate), row.removeFromLeft (widths[0]), juce::Justification::centredLeft);
        g.setColour (theme::dimText);
        g.drawText (Timecode::labelOfSeconds (e.end, owner.shownRate), row.removeFromLeft (widths[1]), juce::Justification::centredLeft);
        auto what = row.removeFromLeft (widths[2]);
        g.setColour (colourOf (e.kind));
        g.fillEllipse (what.getX(), what.getCentreY() - 4.0f, 8.0f, 8.0f);
        what.removeFromLeft (14.0f);
        g.setColour (theme::text);
        g.drawText (tr (LogEvent::kindName (e.kind)), what, juce::Justification::centredLeft);
        g.drawText (LogEvent::channelName (e.channel), row.removeFromLeft (widths[3]), juce::Justification::centredLeft);
        g.setColour (theme::dimText);
        g.drawText (juce::String::fromUTF8 (e.detail.c_str()), row, juce::Justification::centredLeft);
    }
}

} // namespace subguard
