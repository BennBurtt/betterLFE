#include "MainComponent.h"

#if SUBGUARD_HAS_LOGO
 #include <SubGuardAssets.h>
#endif

namespace subguard
{

namespace
{
    constexpr double preferredSampleRate = 48000.0;
    constexpr double lfeInBandGainDb = 10.0;    // cinema B-chain: LFE plays 10 dB above a screen channel
    constexpr int headerHeight = 170, barHeight = 38;
}

//==============================================================================
LabelledSlider::LabelledSlider (const juce::String& text, double min, double max, double step, double initial)
{
    caption.setText (text, juce::dontSendNotification);
    caption.setFont (uiFont (13.0f));
    valueLabel.setFont (uiFont (13.0f));
    valueLabel.setJustificationType (juce::Justification::centredRight);

    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setRange (min, max, step);
    slider.setValue (initial, juce::dontSendNotification);
    slider.onValueChange = [this]
    {
        refresh();

        if (onChange)
            onChange();
    };

    addAndMakeVisible (caption);
    addAndMakeVisible (valueLabel);
    addAndMakeVisible (slider);
}

void LabelledSlider::refresh()
{
    valueLabel.setText (format ? format (slider.getValue()) : juce::String (slider.getValue()), juce::dontSendNotification);
}

void LabelledSlider::paint (juce::Graphics& g)
{
    if (highlightTo <= highlightFrom)
        return;

    // A lighter stretch behind the track
    const auto track = slider.getBounds().toFloat();
    const float x0 = track.getX() + (float) slider.getPositionOfValue (highlightFrom);
    const float x1 = track.getX() + (float) slider.getPositionOfValue (highlightTo);
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.fillRoundedRectangle (x0, track.getCentreY() - 8.0f, x1 - x0, 16.0f, 4.0f);
}

void LabelledSlider::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (20);
    valueLabel.setBounds (top.removeFromRight (84));
    caption.setBounds (top);
    slider.setBounds (area);
}

//==============================================================================
MainComponent::MainComponent (const LaunchOptions& launchOptions)
    : options (launchOptions)
{
    juce::PropertiesFile::Options storage;
    storage.applicationName = "SubGuard";
    storage.filenameSuffix = ".settings";
    storage.osxLibrarySubFolder = "Application Support";
    properties.setStorageParameters (storage);

   #if SUBGUARD_HAS_LOGO
    header.setLogo (juce::ImageCache::getFromMemory (SubGuardAssets::logo_png, SubGuardAssets::logo_pngSize));
   #endif

    addAndMakeVisible (header);
    addAndMakeVisible (languageFlag);
    addAndMakeVisible (spectrum);
    addAndMakeVisible (transientStrip);
    addAndMakeVisible (correlation);
    addAndMakeVisible (history);
    addChildComponent (roomView);

    roomButton.setClickingTogglesState (true);
    roomButton.setTooltip (tr ("Room Sim: the bass seat by seat in fifteen cinema rooms, from a live corridor room to a treated IMAX."));
    roomButton.onClick = [this] { showMode (roomButton.getToggleState() ? 1 : 0); };
    addAndMakeVisible (roomButton);

    addChildComponent (proView);
    proView.onConfigChanged = [this] { saveState(); };
    proButton.setClickingTogglesState (true);
    proButton.setTooltip (tr ("Room Sim Pro: three rooms of your own, every speaker placed freely, with fill speakers. Its own score, not part of the global one."));
    proButton.onClick = [this] { showMode (proButton.getToggleState() ? 2 : 0); };
    addAndMakeVisible (proButton);

    // Test: the built-in test signals, played as an input
    addChildComponent (testView);
    testButton.setClickingTogglesState (true);
    testButton.setTooltip (tr ("The built-in test signals: choose \"SubGUARD test signals\" as the input device, then click a signal."));
    testButton.onClick = [this] { showMode (testButton.getToggleState() ? 3 : 0); };
    addAndMakeVisible (testButton);
    testView.onPlay = [this] (int index) { analysis.playTest (index); };
    testView.onUseTestSource = [this] { useTestSource (true); };
    testView.onLoadRooms = [this] { proView.loadRooms (ProRooms::defaults()); };

    languageFlag.setTooltip (currentLanguage() == Language::french ? tr ("Interface en français. Cliquer : English.")
                                                                   : tr ("Interface in English. Click: français."));
    languageFlag.onClick = [this] { if (onLanguageToggle) onLanguageToggle(); };
    setWantsKeyboardFocus (true);
    buildCards();

    for (auto* speaker : { &header.left, &header.centre, &header.right })
    {
        speaker->onToggle = [this] { updateSpeakers(); };
        speaker->setTooltip (tr ("Click: solo. Option-click: mute."));
    }

    loadState();
    applyFormat();
    analysis.start (options.demoSignal, preferredSampleRate, windowSeconds());

    if (! options.demoSignal)
    {
        deviceManager.addChangeListener (this);
        deviceManager.addAudioCallback (&input);

        juce::RuntimePermissions::request (juce::RuntimePermissions::recordAudio,
                                           [safe = juce::Component::SafePointer<MainComponent> (this)] (bool granted)
                                           {
                                               if (safe == nullptr)
                                                   return;

                                               if (granted)
                                                   safe->openDevices();
                                               else
                                                   safe->updateStatus (tr ("Audio input permission denied"));
                                           });
    }
    else
    {
        statusLabel.setText (tr ("Demo signal (no audio device)"), juce::dontSendNotification);
    }

    setSize (1200, 1000);

    const juce::StringArray cardNames { "source", "spectrum", "alert", "transients", "chains", "dev" };
    showCard (cardNames.indexOf (options.openCard));

    if (options.openCard == "rooms")
        roomButton.setToggleState (true, juce::sendNotificationSync);

    if (options.openCard == "roompro")
        proButton.setToggleState (true, juce::sendNotificationSync);

    lastTick = juce::Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (fpsBox.getSelectedId());
}

MainComponent::~MainComponent()
{
    stopTimer();
    analysis.stop();
    saveState();
    deviceManager.removeAudioCallback (&input);
    deviceManager.removeChangeListener (this);
    deviceManager.closeAudioDevice();
}

const ChannelFormat& MainComponent::format() const
{
    const auto& formats = channelFormats();
    return formats[(size_t) juce::jlimit (0, (int) formats.size() - 1, formatBox.getSelectedId() - 1)];
}

//==============================================================================
void MainComponent::buildCards()
{
    for (auto* label : { &formatLabel, &deviceLabel, &fpsLabel, &windowLabel, &rangeLabel, &statusLabel })
        label->setFont (uiFont (13.0f));

    statusLabel.setMinimumHorizontalScale (0.7f);

    int id = 1;

    for (const auto& f : channelFormats())
        formatBox.addItem (f.name, id++);

    formatBox.setSelectedId (1, juce::dontSendNotification);
    formatBox.onChange = [this]
    {
        applyFormat();

        if (! restoring && ! options.demoSignal && ! analysis.isTestSource())
            selectDevice (deviceBox.getText());
    };

    deviceBox.setTextWhenNothingSelected (tr ("No input device"));
    deviceBox.onChange = [this]
    {
        if (restoring || deviceBox.getSelectedId() <= 0)
            return;

        if (deviceBox.getSelectedId() == testDeviceId)
        {
            useTestSource (true);
            return;
        }

        useTestSource (false);
        selectDevice (deviceBox.getText());
    };

    fpsBox.addItem (tr ("90 FPS"), 90);
    fpsBox.addItem (tr ("60 FPS"), 60);
    fpsBox.addItem (tr ("30 FPS"), 30);
    fpsBox.setSelectedId (60, juce::dontSendNotification);
    fpsBox.onChange = [this] { startTimerHz (fpsBox.getSelectedId()); };

    windowBox.addItem (tr (juce::String::fromUTF8 ("Fast (0.5 s \xc2\xb7 4 Hz)")), 1);
    windowBox.addItem (tr (juce::String::fromUTF8 ("Normal (1 s \xc2\xb7 2 Hz)")), 2);
    windowBox.addItem (tr (juce::String::fromUTF8 ("Ultra (2 s \xc2\xb7 1 Hz)")), 3);
    windowBox.setSelectedId (2, juce::dontSendNotification);
    windowBox.onChange = [this] { analysis.setWindow (windowSeconds()); };

    for (int db : { 72, 96, 120, 144 })
        rangeBox.addItem ("0 to -" + juce::String (db) + " dB", db);

    rangeBox.setSelectedId (96, juce::dontSendNotification);
    rangeBox.onChange = [this] { spectrum.setFloorDb (-(double) rangeBox.getSelectedId()); };

    alertModeBox.addItem (tr ("In phase and out of phase"), 1);
    alertModeBox.addItem (tr ("Out of phase only"), 2);
    alertModeBox.setSelectedId (1, juce::dontSendNotification);
    alertModeBox.setTooltip (tr ("Out of phase only: alert just where the LFE works against the screen channel."));


    peakHoldToggle.setToggleState (true, juce::dontSendNotification);
    peakHoldToggle.onClick = [this] { spectrum.setPeakHold (peakHoldToggle.getToggleState()); };
    tiltToggle.setTooltip (tr ("Display tilt of the first version: -12 dB at 20 Hz. Off: levels are true dBFS."));
    tiltToggle.onClick = [this] { spectrum.setTilt (tiltToggle.getToggleState()); };
    levelWeightToggle.setTooltip (tr ("Off: the alert follows the correlation alone, however quiet the LFE is.\n"
                                  "On: an LFE much quieter (or louder) than the screen channel counts less."));


    lfeGainToggle.setTooltip (tr ("On: the LFE counts 10 dB louder than a screen channel, as a cinema plays it. Off: as recorded.\n"
                              "It applies everywhere: spectrum, alerts, playback chains and score."));
    lfeGainToggle.setToggleState (true, juce::dontSendNotification);
    header.setAnimationEnabled (false);       // a still picture: nothing moves in the corner of the eye

    inertia.format = [] (double v) { return juce::String ((int) v) + "%"; };
    integration.format = [] (double v) { return juce::String (v, 1) + " s"; };
    // Level selector, above the spectrum on the right: one choice sets both
    // the phase alert threshold and how strict the robustness score is.
    const juce::String levelNames[] = { tr ("Soft"), tr ("Medium"), tr ("Strong") };
    const juce::String levelTips[] = {
        tr ("Soft: phase alert from a correlation of 0.90; the score tolerates 3 dB of change before counting it."),
        tr ("Medium: phase alert from 0.75; the score tolerates 2 dB."),
        tr ("Strong: phase alert from 0.50; the score counts changes from 1 dB, about the smallest audible one.")
    };

    for (int i = 0; i < 3; ++i)
    {
        auto& button = levelButtons[(size_t) i];
        button.setButtonText (levelNames[i]);
        button.setTooltip (levelTips[i]);
        button.setClickingTogglesState (true);
        button.setRadioGroupId (41);
        button.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < 2 ? juce::Button::ConnectedOnRight : 0));
        button.setToggleState (i == 2, juce::dontSendNotification);
        addAndMakeVisible (button);
    }
    decay.format = [] (double v) { return juce::String (v, 2) + " s"; };

    scoreSpeed.format = [] (double v) { return v <= 0.0 ? juce::String (tr ("0 (fast)")) : juce::String (v, 2); };
    scoreSpeed.setTooltip (tr ("0: the score follows the mix as it does now. Towards 1: more and more inertia, up to about ten seconds."));

    for (auto* slider : { &inertia, &integration, &decay, &scoreSpeed })
        slider->refresh();

    const auto percentAnd = [] (double percent, double ms)
    {
        return juce::String ((int) percent) + " %  (" + (ms >= 1000.0 ? juce::String (ms / 1000.0, 1) + " s" : juce::String ((int) std::lround (ms)) + " ms") + ")";
    };

    tAttack.format = [percentAnd] (double v)        { return percentAnd (v, TransientSettings::attackMs (v)); };
    tHoldProfile.format = [percentAnd] (double v)   { return percentAnd (v, TransientSettings::holdMs (v)); };
    tRelease.format = [percentAnd] (double v)       { return percentAnd (v, TransientSettings::releaseMs (v)); };
    tSensitivity.format = [] (double v)             { return juce::String ((int) v) + " %  (" + juce::String (TransientSettings::riseDb (v), 1) + " dB)"; };
    tSearch.format = [] (double v)      { return juce::String::fromUTF8 ("\xc2\xb1") + juce::String ((int) v) + " ms"; };
    tMatch.format = [] (double v)       { return juce::String (v, 2); };
    tHold.format = [] (double v)        { return juce::String (v, 1) + " s"; };

    transientToggle.setTooltip (tr ("Off by default. Solo in your DAW what you want to check (a kick and its LFE send, for instance), then switch this on."));
    tAttack.setTooltip (tr ("How fast a rise must be to count as a transient: the time over which the peak is watched. "
                        "Low: only sharp hits. High: slower swells count too."));
    tHoldProfile.setTooltip (tr ("After a transient, no new one for this long: a kick, its click and its cymbal stay one hit."));
    tRelease.setTooltip (tr ("How long the tail of a hit hides what follows. Higher: a ringing tail is not taken for new transients. "
                         "Lower: quieter hits soon after a loud one are found."));
    tSensitivity.setTooltip (tr ("How small a rise counts as a transient. Higher: more transients found, and more false ones."));
    tSearch.setTooltip (tr ("The LFE is looked for this far before and after each attack of the mix. Wider finds a very late LFE; "
                        "narrower is more precise on dense music and on weak hits."));
    tMatch.setTooltip (tr ("Above this likeness between mix and LFE, the delay and polarity come from the correlation (precise). "
                       "Below, the two attacks are timed against each other (coarse, no phase)."));
    tHold.setTooltip (tr ("How long a verdict stays on screen."));

    for (auto* slider : { &tAttack, &tHoldProfile, &tRelease, &tSensitivity, &tSearch, &tMatch, &tHold })
    {
        slider->refresh();
        slider->onChange = [this] { applyTransientSettings(); };
    }

    transientToggle.onClick = [this] { applyTransientSettings(); };

    // The built-in demo has a late kick to show: start with the check on
    if (options.demoSignal)
        transientToggle.setToggleState (true, juce::dontSendNotification);

    applyTransientSettings();

    transientDefaultsButton.onClick = [this]
    {
        const TransientSettings d;
        tAttack.setValue (d.attack);
        tHoldProfile.setValue (d.hold);
        tRelease.setValue (d.release);
        tSensitivity.setValue (d.sensitivity);
        tSearch.setValue (d.searchMs);
        tMatch.setValue (d.minMatch);
        tHold.setValue (4.0);
    };



    // Reset: the history strip and the score start afresh
    resetButton.setTooltip (tr ("Clears the history strip and restarts the score."));
    resetButton.onClick = [this]
    {
        history.clear();
        waveClock = waveMin = waveMax = waveSumSquares = 0.0;
        waveCount = 0;
        historySample = {};
        historyClock = 0.0;

        for (auto& filter : readoutInertia)
            filter.reset();

        chainInertia.clear();
        slowScore = -1.0;
        proView.resetScores();
        displayedScore = -1;

        analysis.requestReset();
        shownFacade.clear();
        shownLfe.clear();
    };
    addAndMakeVisible (resetButton);

    listeningButton.setTooltip (tr ("Listening level: the sound pressure a screen channel gives with the calibration signal at -20 dBFS RMS "
                                "(85 dB(C) in a cinema). It sets what is audible and how loud the bass is heard, which the score follows."));
    listeningButton.onClick = [this] { showListeningMenu(); };
    setListeningLevel (listeningDbC);
    addAndMakeVisible (listeningButton);

    // What each card holds, its size and how it lays its content out
    const auto populate = [this] (int cardIndex, int width, int height, std::initializer_list<juce::Component*> children,
                                  std::function<void (juce::Rectangle<int>)> layout)
    {
        auto& card = cards[(size_t) cardIndex];

        for (auto* child : children)
            card.addAndMakeVisible (child);

        card.onLayout = std::move (layout);
        card.setSize (width, height);
        addChildComponent (card);
    };

    populate (sourceCard, 360, 226, { &formatLabel, &formatBox, &deviceLabel, &deviceBox, &statusLabel, &lfeGainToggle },
              [this] (juce::Rectangle<int> r)
              {
                  formatLabel.setBounds (r.removeFromTop (20));
                  formatBox.setBounds (r.removeFromTop (26));
                  r.removeFromTop (6);
                  deviceLabel.setBounds (r.removeFromTop (20));
                  deviceBox.setBounds (r.removeFromTop (26));
                  r.removeFromTop (4);
                  statusLabel.setBounds (r.removeFromTop (30));
                  lfeGainToggle.setBounds (r.removeFromTop (26));
              });

    populate (spectrumCard, 330, 352, { &rangeLabel, &rangeBox, &peakHoldToggle, &tiltToggle, &inertia, &integration,
                                        &fpsLabel, &fpsBox, &windowLabel, &windowBox },
              [this] (juce::Rectangle<int> r)
              {
                  rangeLabel.setBounds (r.removeFromTop (20));
                  rangeBox.setBounds (r.removeFromTop (26));
                  r.removeFromTop (6);
                  peakHoldToggle.setBounds (r.removeFromTop (24));
                  tiltToggle.setBounds (r.removeFromTop (24));
                  r.removeFromTop (6);
                  inertia.setBounds (r.removeFromTop (46));
                  r.removeFromTop (4);
                  integration.setBounds (r.removeFromTop (46));
                  r.removeFromTop (8);
                  auto combos = r.removeFromTop (46);
                  auto fps = combos.removeFromLeft (100);
                  fpsLabel.setBounds (fps.removeFromTop (20));
                  fpsBox.setBounds (fps.withTrimmedRight (6));
                  windowLabel.setBounds (combos.removeFromTop (20));
                  windowBox.setBounds (combos);
                  r.removeFromTop (8);
              });

    populate (alertCard, 320, 210, { &decay, &alertModeBox, &levelWeightToggle, &scoreSpeed },
              [this] (juce::Rectangle<int> r)
              {
                  decay.setBounds (r.removeFromTop (46));
                  r.removeFromTop (8);
                  alertModeBox.setBounds (r.removeFromTop (26));
                  r.removeFromTop (4);
                  levelWeightToggle.setBounds (r.removeFromTop (24));
                  r.removeFromTop (8);
                  scoreSpeed.setBounds (r.removeFromTop (46));
              });

    populate (transientsCard, 380, 452, { &transientToggle, &tAttack, &tHoldProfile, &tRelease, &tSensitivity, &tSearch, &tMatch, &tHold,
                                          &transientDefaultsButton },
              [this] (juce::Rectangle<int> r)
              {
                  transientToggle.setBounds (r.removeFromTop (26));
                  r.removeFromTop (6);

                  for (auto* slider : { &tAttack, &tHoldProfile, &tRelease, &tSensitivity, &tSearch, &tMatch, &tHold })
                  {
                      slider->setBounds (r.removeFromTop (46));
                      r.removeFromTop (4);
                  }

                  transientDefaultsButton.setBounds (r.removeFromTop (24).removeFromRight (90));
              });

    // Rooms of the Room Sim that count in the score
    roomTicksLabel.setText (tr ("Rooms in the score (modal simulation, see the Room Sim tab):"), juce::dontSendNotification);
    roomTicksLabel.setFont (uiFont (12.0f, true));
    roomTicksLabel.setColour (juce::Label::textColourId, theme::dimText);

    {
        const auto specs = RoomSimulator::defaultRooms();

        for (size_t i = 0; i < specs.size(); ++i)
        {
            auto tick = std::make_unique<juce::ToggleButton> (tr (juce::String (specs[i].shortName)));
            tick->setTooltip (tr (juce::String (specs[i].name)) + ": " + juce::String (specs[i].length, 0) + " x " + juce::String (specs[i].width, 0)
                              + " x " + juce::String (specs[i].height, 1) + " m, RT60 " + juce::String (specs[i].rt60, 2) + " s");
            tick->setToggleState (roomSelected[i], juce::dontSendNotification);
            tick->onClick = [this, i] { setRoomSelected ((int) i, roomTicks[i]->getToggleState()); };
            roomTicks.push_back (std::move (tick));
        }
    }

    roomView.onToggle = [this] (int room) { setRoomSelected (room, ! roomSelected[(size_t) room]); };
    roomView.setSelection (roomSelected);

    populate (chainsCard, 600, 456, { &chainsView, &roomTicksLabel },
              [this] (juce::Rectangle<int> r)
              {
                  auto ticks = r.removeFromBottom (3 * 24);
                  roomTicksLabel.setBounds (r.removeFromBottom (22));
                  chainsView.setBounds (r);
                  const int width = ticks.getWidth() / 5;

                  for (size_t i = 0; i < roomTicks.size(); ++i)
                      roomTicks[i]->setBounds (ticks.getX() + width * (int) (i % 5), ticks.getY() + 24 * (int) (i / 5), width, 24);
              });

    for (auto& tick : roomTicks)
        cards[(size_t) chainsCard].addAndMakeVisible (tick.get());

    // Dev panel
    {
        const auto db = [] (double v) { return juce::String (v, 1) + " dB"; };
        const auto plain = [] (double v) { return juce::String (v, 2); };
        const auto hz = [] (double v) { return juce::String ((int) v) + " Hz"; };
        dTolSoft.format = dTolMedium.format = dTolStrong.format = dLevelRed.format = db;
        dLate.format = dEarly.format = dCeiling.format = dInfraWeight.format = dLevelWeight.format = dWorstWeight.format = dCoverage.format = dDips.format = plain;
        dWorstShare.format = [] (double v) { return juce::String (juce::roundToInt (100.0 * v)) + " %"; };
        dInfraLo.format = dInfraHi.format = hz;
        const juce::String sectionTitles[] = { tr ("Tolerances (Soft, Medium, Strong)"), tr ("Cinema subwoofer off its place"), tr ("LFE dependence"),
                                               tr ("Infra-bass"), tr ("Rooms (Room Sim and Room Sim Pro)") };

        for (size_t i = 0; i < devSections.size(); ++i)
        {
            devSections[i].setText (sectionTitles[i], juce::dontSendNotification);
            devSections[i].setFont (uiFont (12.5f, true));
            devSections[i].setColour (juce::Label::textColourId, juce::Colour (0xff7fb8ea));
        }

        dTolSoft.setTooltip (tr ("How many dB a band may change on a playback system before it starts to count, at each level."));
        dCeiling.setTooltip (tr ("How much of the score a bass entirely in the LFE can take away. At 1.00 'LFE alone' scores near 2; about 0.80 puts it near 20."));
        dInfraWeight.setTooltip (tr ("What bass under the 'infra-bass up to' frequency still counts for in the LFE dependence; it rises to 1 at 'full weight from'."));
        dLevelWeight.setTooltip (tr ("The most the LFE level dependence can take off the score."));
        dLevelRed.setTooltip (tr ("Mean difference from the expected bass at which the LFE level dependence box is fully red."));
        dWorstWeight.setTooltip (tr ("Room Sim and Room Sim Pro: room score = (1 - weight) x mean seat score + weight x score of the worst seats. "
                                 "0 is the plain mean."));
        dWorstShare.setTooltip (tr ("Which seats count as the worst ones: this share of the seats, at least one."));
        dCoverage.setTooltip (tr ("Room Sim Pro: how much the bass level at each seat against the approved mix counts (back rows losing the bass, holes, boosts). "
                                  "0: only the interaction, as Room Sim."));
        dDips.setTooltip (tr ("Room Sim and Room Sim Pro: a dip is heard less than a peak of the same size. 1: dips get the published "
                              "difference (Bharitkar, Robinson & Poulain 2016; Toole & Olive), about 2.6 dB more below 63 Hz, "
                              "2.2 dB at 125 Hz; 0: dips count as peaks."));

        for (auto* slider : devSliders())
        {
            slider->refresh();
            slider->onChange = [this] { applyDevSettings(); };
        }

        devDefaultsButton.onClick = [this]
        {
            const PlaybackSimulator::Tuning d;
            dTolSoft.setValue (d.toleranceDb[0]);
            dTolMedium.setValue (d.toleranceDb[1]);
            dTolStrong.setValue (d.toleranceDb[2]);
            dLate.setValue (d.lateRelevance);
            dEarly.setValue (d.earlyRelevance);
            dCeiling.setValue (d.dependenceCeiling);
            dInfraWeight.setValue (d.infraWeight);
            dInfraLo.setValue (d.infraLoHz);
            dInfraHi.setValue (d.infraHiHz);
            dLevelWeight.setValue (d.levelDependenceWeight);
            dLevelRed.setValue (d.levelRedDb);
            dWorstWeight.setValue (d.roomWorstWeight);
            dWorstShare.setValue (d.roomWorstShare);
            dCoverage.setValue (d.coverageWeight);
            dDips.setValue (d.dipAllowance);
        };

        auto& card = cards[(size_t) devCard];

        for (auto* slider : devSliders())
            card.addAndMakeVisible (slider);

        card.addAndMakeVisible (devDefaultsButton);
        card.addAndMakeVisible (devLoudness);

        for (auto& section : devSections)
            card.addAndMakeVisible (section);

        card.onLayout = [this] (juce::Rectangle<int> r)
        {
            // Two columns of titled groups, then the table of the bass playing now
            auto bottom = r.removeFromBottom (100);
            devDefaultsButton.setBounds (bottom.removeFromRight (100).removeFromBottom (26).withTrimmedLeft (10));
            devLoudness.setBounds (bottom.withTrimmedRight (12));
            r.removeFromBottom (8);
            auto left = r.removeFromLeft (r.getWidth() / 2).withTrimmedRight (14);
            auto right = r.withTrimmedLeft (14);

            const auto group = [this] (juce::Rectangle<int>& column, size_t title, std::initializer_list<LabelledSlider*> sliders)
            {
                devSections[title].setBounds (column.removeFromTop (22));

                for (auto* slider : sliders)
                {
                    slider->setBounds (column.removeFromTop (40));
                    column.removeFromTop (2);
                }

                column.removeFromTop (8);
            };

            group (left, 0, { &dTolSoft, &dTolMedium, &dTolStrong });
            group (left, 1, { &dLate, &dEarly });
            group (left, 2, { &dCeiling, &dLevelWeight, &dLevelRed });
            group (right, 3, { &dInfraWeight, &dInfraLo, &dInfraHi });
            group (right, 4, { &dWorstWeight, &dWorstShare, &dCoverage, &dDips });
        };
        card.setSize (760, 590);
        addChildComponent (card);
    }

    const juce::String names[] = { tr ("Source"), tr ("Spectrum"), tr ("Phase Alert"), tr ("Transients"), tr ("Playback chains"), tr ("Dev") };

    for (int i = 0; i < numCards; ++i)
    {
        auto& button = cardButtons[(size_t) i];
        button.setButtonText (names[i]);
        button.setClickingTogglesState (false);
        button.onClick = [this, i] { showCard (openCard == i ? -1 : i); };
        addAndMakeVisible (button);
    }
}

void MainComponent::showCard (int index)
{
    openCard = index >= 0 && index < numCards ? index : -1;

    for (int i = 0; i < numCards; ++i)
    {
        cards[(size_t) i].setVisible (i == openCard);
        cardButtons[(size_t) i].setToggleState (i == openCard, juce::dontSendNotification);
    }

    positionCards();
}

void MainComponent::positionCards()
{
    // Each card hangs under its button, kept inside the window
    for (int i = 0; i < numCards; ++i)
    {
        auto& card = cards[(size_t) i];
        const auto button = cardButtons[(size_t) i].getBounds();
        const int x = juce::jlimit (8, juce::jmax (8, getWidth() - card.getWidth() - 8), button.getX());
        card.setTopLeftPosition (x, button.getBottom() + 6);
        card.toFront (false);
    }
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    // Option-Z / Option-Shift-Z: undo and redo in Room Sim Pro; Cmd-S / Cmd-O: save and load the rooms, from any view
    const auto c = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) key.getKeyCode());

    if (proMode || (key.getModifiers().isCommandDown() && (c == 's' || c == 'o')))
        return proView.keyPressed (key);

    return false;
}

void LoudnessTable::set (const Translation& t)
{
    hasBass = t.hasBass;

    for (size_t b = 0; b < 4; ++b)
    {
        spl[b] = t.bandSplDb[b];
        phon[b] = t.bandPhon[b];
        doubling[b] = t.bandDoublingDb[b];
    }

    repaint();
}

void LoudnessTable::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff222428));
    g.fillRoundedRectangle (area, 5.0f);
    area = area.reduced (10.0f, 6.0f);

    g.setColour (juce::Colour (0xff7fb8ea));
    g.setFont (uiFont (12.5f, true));
    g.drawText (tr ("Bass playing now, at the listening level"), area.removeFromTop (18.0f), juce::Justification::centredLeft);

    // A header row, then one row per band
    const juce::String headers[] = { tr ("Band"), tr ("Level"), tr ("Loudness"), tr ("dB to double it") };
    const char* bands[] = { "20-30 Hz", "30-60 Hz", "60-90 Hz", "90-120 Hz" };
    const float columnWidth = area.getWidth() / 5.0f, rowHeight = (area.getHeight() - 2.0f) / 3.0f;

    const auto cell = [&] (int row, int column, const juce::String& text, bool header)
    {
        g.setColour (header ? theme::dimText : theme::text);
        g.setFont (uiFont (header ? 11.0f : 12.0f, ! header));
        const auto r = juce::Rectangle<float> (area.getX() + columnWidth * (float) column, area.getY() + rowHeight * (float) row,
                                               columnWidth, rowHeight);
        g.drawText (text, r, column == 0 ? juce::Justification::centredLeft : juce::Justification::centred);
    };

    // Transposed: bands across, quantities down, so that four bands fit on one line
    cell (0, 0, headers[0], true);

    for (int b = 0; b < 4; ++b)
        cell (0, b + 1, bands[b], true);

    const juce::String rowNames[] = { headers[1], headers[2] + " / " + headers[3] };

    for (int row = 0; row < 2; ++row)
    {
        cell (row + 1, 0, rowNames[row], true);

        for (int b = 0; b < 4; ++b)
        {
            juce::String text = "--";

            if (hasBass)
                text = row == 0 ? juce::String (juce::roundToInt (spl[(size_t) b])) + " dB SPL"
                                : (phon[(size_t) b] < 20.0 ? tr ("under 20 phon") : juce::String (juce::roundToInt (phon[(size_t) b])) + tr (" phon"))
                                    + " / " + juce::String (doubling[(size_t) b], 1) + " dB";

            cell (row + 1, b + 1, text, false);
        }
    }
}

void LanguageFlag::paint (juce::Graphics& g)
{
    // The flag of the language shown: France, or the United Kingdom for English
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    juce::Path clip;
    clip.addRoundedRectangle (r, 2.5f);
    g.saveState();
    g.reduceClipRegion (clip);

    if (currentLanguage() == Language::french)
    {
        const float w = r.getWidth() / 3.0f;
        g.setColour (juce::Colour (0xff0055a4));
        g.fillRect (r.withWidth (w));
        g.setColour (juce::Colours::white);
        g.fillRect (r.withX (r.getX() + w).withWidth (w));
        g.setColour (juce::Colour (0xffef4135));
        g.fillRect (r.withX (r.getX() + 2.0f * w).withWidth (w));
    }
    else
    {
        g.setColour (juce::Colour (0xff012169));
        g.fillRect (r);
        const auto diagonal = [&] (float thickness, juce::Colour colour)
        {
            g.setColour (colour);
            g.drawLine (r.getX(), r.getY(), r.getRight(), r.getBottom(), thickness);
            g.drawLine (r.getX(), r.getBottom(), r.getRight(), r.getY(), thickness);
        };
        diagonal (4.0f, juce::Colours::white);
        diagonal (1.5f, juce::Colour (0xffc8102e));
        g.setColour (juce::Colours::white);
        g.fillRect (r.withSizeKeepingCentre (r.getWidth(), 6.0f));
        g.fillRect (r.withSizeKeepingCentre (6.0f, r.getHeight()));
        g.setColour (juce::Colour (0xffc8102e));
        g.fillRect (r.withSizeKeepingCentre (r.getWidth(), 3.0f));
        g.fillRect (r.withSizeKeepingCentre (3.0f, r.getHeight()));
    }

    g.restoreState();
    g.setColour (juce::Colours::black.withAlpha (isMouseOver() ? 0.2f : 0.6f));
    g.drawRoundedRectangle (r, 2.5f, 1.0f);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::window);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    header.setBounds (area.removeFromTop (juce::jmax (110, juce::jmin (headerHeight, getHeight() / 6))));
    languageFlag.setBounds (header.getX() + 10, header.getY() + 8, 30, 20);
    languageFlag.toFront (false);

    auto bar = area.removeFromTop (barHeight).reduced (10, 6);
    const int widths[numCards] = { 90, 90, 104, 100, 140, 60 };
    resetButton.setBounds (bar.removeFromRight (80));
    bar.removeFromRight (6);
    listeningButton.setBounds (bar.removeFromRight (170));

    for (int i = 0; i < numCards; ++i)
    {
        if (i == chainsCard)
            bar.removeFromLeft (14);

        cardButtons[(size_t) i].setBounds (bar.removeFromLeft (widths[i]));
        bar.removeFromLeft (6);
    }

    bar.removeFromLeft (14);
    roomButton.setBounds (bar.removeFromLeft (100));
    bar.removeFromLeft (6);
    proButton.setBounds (bar.removeFromLeft (120));
    bar.removeFromLeft (6);
    testButton.setBounds (bar.removeFromLeft (70));

    history.setBounds (area.removeFromBottom (48));
    roomView.setBounds (area);
    roomView.setVisible (roomMode);
    proView.setBounds (area);
    proView.setVisible (proMode);
    testView.setBounds (area);
    testView.setVisible (testMode);
    spectrum.setVisible (! roomMode && ! proMode && ! testMode);
    transientStrip.setVisible (! roomMode && ! proMode && ! testMode);
    correlation.setVisible (! roomMode && ! proMode && ! testMode);
    correlation.setBounds (area.removeFromBottom (juce::jmax (150, area.getHeight() * 2 / 7)));
    transientStrip.setBounds (area.removeFromBottom (68));
    spectrum.setBounds (area);

    // Level selector over the title row of the spectrum, on the right
    auto levels = juce::Rectangle<int> (area.getRight() - 22 - 3 * 66, area.getY() + 4, 3 * 66, 22);

    for (auto& button : levelButtons)
        button.setBounds (levels.removeFromLeft (66));

    positionCards();
}

//==============================================================================
void MainComponent::applyFormat()
{
    input.setChannelIndices (format().analysisIndices());
}

void MainComponent::openDevices()
{
    std::unique_ptr<juce::XmlElement> saved;

    if (auto* settings = properties.getUserSettings())
        saved = settings->getXmlValue ("audioDevice");

    deviceManager.initialise (16, 0, saved.get(), true);
    refreshDeviceList();

    // The test signals were the input last time: again
    if (auto* settings = properties.getUserSettings(); settings != nullptr && settings->getBoolValue ("testSource", false))
    {
        useTestSource (true);
        return;
    }

    if (auto* device = deviceManager.getCurrentAudioDevice())
        selectDevice (device->getName());       // enables exactly the channels the format needs
    else
        updateStatus();
}

void MainComponent::refreshDeviceList()
{
    juce::StringArray names;

    if (auto* type = deviceManager.getCurrentDeviceTypeObject())
    {
        type->scanForDevices();
        names = type->getDeviceNames (true);
    }

    const auto current = deviceManager.getAudioDeviceSetup().inputDeviceName;
    deviceBox.clear (juce::dontSendNotification);
    deviceBox.addItemList (names, 1);
    deviceBox.addSeparator();
    deviceBox.addItem (tr ("SubGUARD test signals"), testDeviceId);      // built in: see the Test tab
    deviceBox.setSelectedId (analysis.isTestSource() ? testDeviceId : names.indexOf (current) + 1, juce::dontSendNotification);
}

void MainComponent::selectDevice (const juce::String& name)
{
    if (name.isEmpty())
        return;

    auto setup = deviceManager.getAudioDeviceSetup();
    setup.inputDeviceName = name;
    setup.outputDeviceName = {};
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange (0, channelsNeeded(), true);
    setup.outputChannels.clear();
    setup.sampleRate = preferredSampleRate;     // JUCE picks the closest rate the device offers
    setup.bufferSize = 512;

    updateStatus (deviceManager.setAudioDeviceSetup (setup, true));
}

void MainComponent::useTestSource (bool on)
{
    if (on == analysis.isTestSource())
        return;

    analysis.setTestSource (on);

    if (on)
        deviceManager.closeAudioDevice();       // the signals are made inside the app

    if (auto* settings = properties.getUserSettings())
        settings->setValue ("testSource", on);

    refreshDeviceList();
    updateStatus();
}

void MainComponent::showMode (int mode)
{
    roomMode = mode == 1;
    proMode = mode == 2;
    testMode = mode == 3;
    roomButton.setToggleState (roomMode, juce::dontSendNotification);
    proButton.setToggleState (proMode, juce::dontSendNotification);
    testButton.setToggleState (testMode, juce::dontSendNotification);

    if (proMode)
        proView.grabKeyboardFocus();

    resized();
}

void MainComponent::updateStatus (const juce::String& error)
{
    juce::String text;
    auto colour = theme::text;

    if (analysis.isTestSource() && error.isEmpty())
    {
        statusLabel.setText (tr ("Built-in test signals, 48 kHz (Test tab)"), juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff7ed957));
        return;
    }

    if (error.isNotEmpty())
    {
        text = tr ("Error: ") + error;
        colour = juce::Colour (0xffff6b6b);
    }
    else if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const int active = device->getActiveInputChannels().countNumberOfSetBits();
        text = juce::String (active) + " ch @ " + juce::String ((int) device->getCurrentSampleRate()) + " Hz";

        if (active < channelsNeeded())
        {
            text = tr ("Needs ") + juce::String (channelsNeeded()) + tr (" channels, device gives ") + juce::String (active);
            colour = juce::Colour (0xffffb347);
        }
    }
    else
    {
        text = tr ("No input device open");
        colour = juce::Colour (0xffffb347);
    }

    statusLabel.setText (text, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, colour);
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshDeviceList();
    updateStatus();
}

PlaybackSimulator::Level MainComponent::currentLevel() const
{
    if (levelButtons[0].getToggleState())   return PlaybackSimulator::Level::soft;
    if (levelButtons[1].getToggleState())   return PlaybackSimulator::Level::medium;
    return PlaybackSimulator::Level::strong;
}

AlertTracker::Options MainComponent::alertOptions() const
{
    AlertTracker::Options o;
    o.threshold = PlaybackSimulator::alertThreshold (currentLevel());
    o.decaySeconds = decay.value();
    o.outOfPhaseOnly = alertModeBox.getSelectedId() == 2;
    o.levelWeighted = levelWeightToggle.getToggleState();
    return o;
}

void MainComponent::applyTransientSettings()
{
    TransientSettings t;
    t.enabled = transientToggle.getToggleState();
    t.attack = tAttack.value();
    t.hold = tHoldProfile.value();
    t.release = tRelease.value();
    t.sensitivity = tSensitivity.value();
    t.searchMs = tSearch.value();
    t.minMatch = tMatch.value();

    analysis.setTransientSettings (t);

    transientStrip.setActive (t.enabled);
    transientStrip.setMinMatch (t.minMatch);
    transientStrip.setHoldSeconds (tHold.value());
}

double MainComponent::windowSeconds() const
{
    const double windows[] = { 0.5, 1.0, 2.0 };
    return windows[juce::jlimit (0, 2, windowBox.getSelectedId() - 1)];
}

void MainComponent::setListeningLevel (double dbC)
{
    listeningDbC = juce::jlimit (60.0, 100.0, std::round (dbC));
    listeningButton.setButtonText (tr ("Listening ") + juce::String ((int) listeningDbC) + " dB(C)");
    simulator.tuning.listeningLevelDbC = listeningDbC;
}

void MainComponent::showListeningMenu()
{
    // 76 to 85 dB(C), the levels of dubbing stages, stand out; the rest is dimmed. From 60 (a shop) to 100
    // (a loud venue); above 90 the ISO 226 contours are extrapolated (the standard stops at 90 phon)
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Listening level: a screen channel at -20 dBFS RMS gives"));

    for (int db : { 100, 95, 92, 91, 90, 89, 88, 87, 86, 85, 84, 83, 82, 81, 80, 79, 78, 77, 76, 75, 74, 73, 72, 71, 70, 68, 65, 62, 60 })
    {
        juce::PopupMenu::Item item (juce::String (db) + " dB(C)" + (db == 85 ? tr ("   cinema calibration") : ""));
        item.itemID = db;
        item.isTicked = db == (int) listeningDbC;
        item.colour = (db >= 76 && db <= 85) ? juce::Colours::white : juce::Colour (0xff8a8a8a);
        menu.addItem (item);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&listeningButton),
                        [safe = juce::Component::SafePointer<MainComponent> (this)] (int chosen)
                        {
                            if (safe != nullptr && chosen > 0)
                                safe->setListeningLevel (chosen);
                        });
}

//==============================================================================
std::vector<LabelledSlider*> MainComponent::devSliders()
{
    return { &dTolSoft, &dTolMedium, &dTolStrong, &dLate, &dEarly, &dCeiling,
             &dInfraWeight, &dInfraLo, &dInfraHi, &dLevelWeight, &dLevelRed, &dWorstWeight, &dWorstShare, &dCoverage, &dDips };
}

void MainComponent::applyDevSettings()
{
    auto& t = simulator.tuning;
    t.toleranceDb = { { dTolSoft.value(), dTolMedium.value(), dTolStrong.value() } };
    t.lateRelevance = dLate.value();
    t.earlyRelevance = dEarly.value();
    t.dependenceCeiling = dCeiling.value();
    t.infraWeight = dInfraWeight.value();
    t.infraLoHz = dInfraLo.value();
    t.infraHiHz = dInfraHi.value();
    t.levelDependenceWeight = dLevelWeight.value();
    t.levelRedDb = dLevelRed.value();
    t.roomWorstWeight = dWorstWeight.value();
    t.roomWorstShare = dWorstShare.value();
    t.coverageWeight = dCoverage.value();
    t.dipAllowance = dDips.value();
    t.listeningLevelDbC = listeningDbC;
}

void MainComponent::setRoomSelected (int room, bool on)
{
    if (room < 0 || (size_t) room >= roomSelected.size())
        return;

    roomSelected[(size_t) room] = on;
    roomTicks[(size_t) room]->setToggleState (on, juce::dontSendNotification);
    roomView.setSelection (roomSelected);
}

void MainComponent::updateSpeakers()
{
    // A soloed speaker leaves the others out; a muted one is out whatever happens
    SpeakerIndicator* speakers[] = { &header.left, &header.centre, &header.right };
    const bool anySolo = header.left.isSolo() || header.centre.isSolo() || header.right.isSolo();

    for (size_t c = 0; c < 3; ++c)
    {
        const bool out = speakers[c]->isMuted() || (anySolo && ! speakers[c]->isSolo());
        speakers[c]->setDimmed (out);
        engineSettings.mute[c] = out;
    }
}

void MainComponent::timerCallback()
{
    const double nowSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double dt = juce::jlimit (0.001, 0.25, nowSeconds - lastTick);
    lastTick = nowSeconds;

    if (testMode)
        testView.setState (analysis.isTestSource(), analysis.playingTest(), analysis.testSeconds());

    // Settings for the analysis thread: they apply from its next 20 ms step.
    // The spectrum is measured with a fixed 150 ms average; the Inertia
    // slider only smooths what is displayed.
    engineSettings.lfeGainDb = lfeGainToggle.getToggleState() ? lfeInBandGainDb : 0.0;
    engineSettings.tauSpectrum = 0.15;
    engineSettings.tauCorrelation = integration.value();
    analysis.setSettings (engineSettings);

    // A new analysis (other device rate, other window): the display starts afresh
    if (analysis.generation() != runnerGeneration)
    {
        runnerGeneration = analysis.generation();
        haveData = false;
        alerts.reset (0);
        shownFacade.clear();
        shownLfe.clear();

        for (auto& channel : curveInertia)
            channel.clear();

        updateStatus();
    }

    // The latest state the analysis published, and what accumulated since
    const auto runner = analysis.runner();
    const bool fresh = runner != nullptr && runner->take (snapshot);
    haveData = haveData || fresh;

    if (fresh)
    {
        framePeak = snapshot.screenPeak;
        anyFramePeak = juce::jmax (snapshot.lfePeak, snapshot.screenPeak[0], snapshot.screenPeak[1], snapshot.screenPeak[2]);
        bassFramePeak = snapshot.bassPeak;
        waveMin = juce::jmin (waveMin, snapshot.waveMin);
        waveMax = juce::jmax (waveMax, snapshot.waveMax);
        waveSumSquares += snapshot.waveSumSquares;
        waveCount += snapshot.waveCount;
    }

    // Waveform of the history strip: one point every 125 ms
    waveClock += dt;

    if (waveClock >= HistoryStrip::secondsPerWavePoint)
    {
        waveClock -= HistoryStrip::secondsPerWavePoint;
        HistoryStrip::WavePoint point;
        point.lo = (float) waveMin;
        point.hi = (float) waveMax;
        point.rms = waveCount > 0 ? (float) std::sqrt (waveSumSquares / (double) waveCount) : 0.0f;
        history.pushWave (point);
        waveMin = waveMax = waveSumSquares = 0.0;
        waveCount = 0;
    }

    const double level = options.demoSignal ? 0.6
                       : input.isRunning() ? juce::jlimit (0.0, 1.0, (juce::Decibels::gainToDecibels ((double) input.level(), -200.0) + 60.0) / 60.0)
                                           : 0.0;
    header.setLevel (level);

    // Waveform of the history strip: the loudest peak of the column, -60 .. 0 dBFS
    historySample.level = juce::jmax (historySample.level,
                                      (float) juce::jlimit (0.0, 1.0, (juce::Decibels::gainToDecibels (anyFramePeak, -200.0) + 60.0) / 60.0));

    // Bass under 200 Hz against the loudest of the last seconds: the background follows it, slowly
    bassRecentPeak = juce::jmax (bassFramePeak, bassRecentPeak * std::exp (-dt / 6.0));
    {
        const double target = bassRecentPeak > 1e-6 ? std::pow (bassFramePeak / bassRecentPeak, 1.5) : 0.0;
        bassDrive += (target > bassDrive ? 1.0 - std::exp (-dt / 0.35) : 1.0 - std::exp (-dt / 1.6)) * (target - bassDrive);
    }

    header.setBass (bassDrive);
    header.advance (dt);

    {
        SpeakerIndicator* cones[] = { &header.left, &header.centre, &header.right };

        for (size_t c = 0; c < 3; ++c)
        {
            recentPeak[c] = juce::jmax (framePeak[c], recentPeak[c] * std::exp (-dt / 3.0));
            const double target = recentPeak[c] > 1e-6 ? framePeak[c] / recentPeak[c] : 0.0;     // under -120 dBFS: still
            coneDrive[c] += (target > coneDrive[c] ? 1.0 - std::exp (-dt / 0.015) : 1.0 - std::exp (-dt / 0.12)) * (target - coneDrive[c]);
            cones[c]->setDrive (coneDrive[c]);
        }
    }

    const auto alerting = alertOptions();

    if (haveData)
    {
        const auto& measured = snapshot.analysis;

        // Nothing in the LFE any more: the engine has cleared what it measured
        // against it (so that the next passage starts clean), but the display
        // is not wiped: alerts are held at their peak for a moment, then fade
        // with the Decay setting, so that a short LFE hit followed by digital
        // silence can still be read.
        if (snapshot.autoReset)
            snapshot.autoReset = false;

        alerts.update (measured, alerting, dt);

        // Spectrum as displayed: the measured one, smoothed by the Inertia slider (10 to 600 ms)
        {
            const double tau = 0.010 * std::pow (60.0, inertia.value() / 100.0);
            const double k = 1.0 - std::exp (-dt / tau);
            const auto smooth = [k] (std::vector<double>& shown, const std::vector<double>& now)
            {
                if (shown.size() != now.size())
                {
                    shown = now;
                    return;
                }

                for (size_t i = 0; i < now.size(); ++i)
                    shown[i] += k * (now[i] - shown[i]);
            };

            smooth (shownFacade, measured.facadeDb);
            smooth (shownLfe, measured.lfeDb);
            auto shown = measured;
            shown.facadeDb = shownFacade;
            shown.lfeDb = shownLfe;
            spectrum.update (shown, alerts, dt);
        }

        // Everything shown as a curve or a number goes through the display
        // inertia: flicker is averaged out, real changes are followed quickly.
        auto shownAlerts = alerts;

        for (size_t c = 0; c < 3; ++c)
        {
            auto& filters = curveInertia[c];
            filters.resize (alerts.curves[c].size());

            for (size_t i = 0; i < filters.size(); ++i)
                shownAlerts.curves[c][i] = filters[i].update (alerts.curves[c][i], dt);
        }

        correlation.update (measured, shownAlerts, alerting.threshold, alerting.outOfPhaseOnly);

        // Playback chains, robustness, LFE dependence, overlap
        ChainsInfo info;
        info.translation = simulator.compute (measured, currentLevel(), lfeGainToggle.getToggleState() ? lfeInBandGainDb : 0.0);

        // Room Sim Pro: its own score, from the electrical translation (before any Room Sim folding)
        if (proMode)
            proView.update (measured, info.translation, simulator.tuning, currentLevel(),
                            lfeGainToggle.getToggleState() ? lfeInBandGainDb : 0.0, dt, scoreSpeed.value());

        // Room Sim: computed when shown or when ticked rooms count in the score
        if (roomMode || std::find (roomSelected.begin(), roomSelected.end(), true) != roomSelected.end())
        {
            roomResult = roomSim.compute (measured, info.translation, simulator.tuning, currentLevel(),
                                          lfeGainToggle.getToggleState() ? lfeInBandGainDb : 0.0);
            RoomSimulator::applyToScore (info.translation, roomResult, roomSelected, simulator.tuning.levelDependenceWeight);
        }

        info.sessionCorrelation = snapshot.globalCorrelation;
        info.sessionSeconds = snapshot.globalSeconds;
        auto& t = info.translation;

        if (! t.hasBass)
        {
            for (auto& filter : readoutInertia)
                filter.reset();

            chainInertia.clear();
            slowScore = -1.0;
            displayedScore = -1;
        }
        else
        {
            t.robustness = readoutInertia[0].update (t.robustness, dt);

            // Score speed: an extra, plain smoothing on top, up to about ten seconds
            if (scoreSpeed.value() > 0.0 && slowScore >= 0.0)
                slowScore += (1.0 - std::exp (-dt / (10.0 * scoreSpeed.value() * scoreSpeed.value()))) * (t.robustness - slowScore);
            else
                slowScore = t.robustness;

            t.robustness = slowScore;
            t.interaction = readoutInertia[1].update (t.interaction, dt);
            t.dependence = readoutInertia[2].update (t.dependence, dt);
            t.lfeBalanceDb = readoutInertia[3].update (t.lfeBalanceDb, dt);
            t.lfeShare = readoutInertia[4].update (t.lfeShare, dt);
            t.levelDependence = readoutInertia[5].update (t.levelDependence, dt);
            chainInertia.resize (t.chains.size() * 2);

            for (size_t k = 0; k < t.chains.size(); ++k)
            {
                t.chains[k].changeDb = chainInertia[k * 2].update (t.chains[k].changeDb, dt);
                t.chains[k].penalty = chainInertia[k * 2 + 1].update (t.chains[k].penalty, dt);
            }

            // The number on screen only changes once it has really moved
            if (displayedScore < 0 || std::abs (t.robustness - displayedScore) >= 1.5)
                displayedScore = juce::roundToInt (t.robustness);
        }

        if (cards[chainsCard].isVisible())
            chainsView.update (info);

        if (cards[devCard].isVisible())
        {
            // What the listening level makes of the bass playing now, band by band
            devLoudness.set (t);
        }

        spectrum.setScore ((double) displayedScore);

        if (roomMode)
            roomView.update (roomResult, (double) displayedScore, t.hasBass && displayedScore >= 0);
        spectrum.setOverlap ({ { t.overlap[0].amount, t.overlap[1].amount, t.overlap[2].amount } });

        // History: the strongest value of each indicator over the column
        double alert = 0.0;
        bool outOfPhase = false;

        for (size_t i = 0; i < alerts.heatLevel.size(); ++i)
            if (alerts.heatLevel[i] > alert)
            {
                alert = alerts.heatLevel[i];
                outOfPhase = alerts.heatSign[i] < 0.0;
            }

        if ((float) alert > historySample.alert)
        {
            historySample.alert = (float) alert;
            historySample.outOfPhase = outOfPhase;
        }

        if (t.hasBass)
        {
            const auto score = (float) (t.robustness / 100.0);
            historySample.score = historySample.scored ? juce::jmin (historySample.score, score) : score;
            historySample.scored = true;
            historySample.dependence = juce::jmax (historySample.dependence,
                                                   (float) juce::jlimit (0.0, 1.0, (t.lfeShare - 0.5) / 0.5));
        }
    }

    historyClock += dt;

    if (historyClock >= HistoryStrip::secondsPerColumn)
    {
        historyClock -= HistoryStrip::secondsPerColumn;
        history.push (historySample);
        historySample = {};
    }

    // Transient engine: one event per attack of the mix
    for (const auto& event : snapshot.events)
    {
        transientStrip.show (event);

        // The reference is the downmix: every screen channel lights up on a fault
        if (transientStrip.isFault (event))
            for (auto& flash : transientFlash)
                flash = 1.0;
    }

    {
        HistoryStrip::WavePoint live;
        live.lo = (float) waveMin;
        live.hi = (float) waveMax;
        history.setClock (historyClock, waveClock, historySample, live);
    }
    snapshot.events.clear();        // each event is shown once
    transientStrip.advance (dt);
    SpeakerIndicator* speakers[] = { &header.left, &header.centre, &header.right };

    for (size_t c = 0; c < 3; ++c)
    {
        transientFlash[c] = juce::jmax (0.0, transientFlash[c] - dt / 0.6);
        speakers[c]->setLevel (juce::jmax (alerts.speakerLevel[c], transientFlash[c]));
    }

    if (options.snapshotFile != juce::File() && ++framesShown == snapshotFrames)
    {
        const auto image = createComponentSnapshot (getLocalBounds());
        options.snapshotFile.deleteFile();
        juce::FileOutputStream stream (options.snapshotFile);

        if (stream.openedOk())
            juce::PNGImageFormat().writeImageToStream (image, stream);

        juce::JUCEApplicationBase::quit();
    }
}

//==============================================================================
void MainComponent::loadState()
{
    auto* settings = properties.getUserSettings();

    if (settings == nullptr || options.demoSignal)
        return;

    restoring = true;
    formatBox.setSelectedId (settings->getIntValue ("format", 1), juce::dontSendNotification);
    fpsBox.setSelectedId (settings->getIntValue ("fps", 60), juce::dontSendNotification);
    windowBox.setSelectedId (settings->getIntValue ("window", 2), juce::dontSendNotification);
    lfeGainToggle.setToggleState (settings->getBoolValue ("lfeGainEverywhere", true), juce::dontSendNotification);

    {
        const char* keys[] = { "devTolSoft", "devTolMedium", "devTolStrong", "devLate", "devEarly", "devCeiling",
                               "devInfraWeight", "devInfraLo", "devInfraHi", "devLevelWeight", "devLevelRed",
                               "devRoomWorstWeight", "devRoomWorstShare", "devCoverageWeight", "devDipAllowance" };
        int index = 0;

        for (auto* slider : devSliders())
        {
            const auto* key = keys[index++];

            if (settings->containsKey (key))
                slider->setValue (settings->getDoubleValue (key));
        }

        applyDevSettings();
    }
    inertia.setValue (settings->getDoubleValue ("inertia", 50.0));
    integration.setValue (settings->getDoubleValue ("integration", 2.0));
    levelButtons[(size_t) juce::jlimit (0, 2, settings->getIntValue ("alertSensitivity", 3) - 1)].setToggleState (true, juce::dontSendNotification);
    decay.setValue (settings->getDoubleValue ("decay", 1.0));
    scoreSpeed.setValue (settings->getDoubleValue ("scoreSpeed", 0.0));

    {
        const auto saved = settings->getValue ("roomSelection");

        if (saved.length() == (int) roomSelected.size())
            for (int i = 0; i < saved.length(); ++i)
                setRoomSelected (i, saved[i] == '1');
    }

    {
        ProRooms pro;

        if (ProRooms::fromText (settings->getValue ("roomPro").toStdString(), pro))
            proView.setRooms (pro);
    }
    setListeningLevel (settings->getDoubleValue ("listeningLevel", settings->getDoubleValue ("devListening", 85.0)));
    transientToggle.setToggleState (settings->getBoolValue ("transientCheck", false), juce::dontSendNotification);
    tAttack.setValue (settings->getDoubleValue ("transientAttack", 40.0));
    tHoldProfile.setValue (settings->getDoubleValue ("transientHoldProfile", 25.0));
    tRelease.setValue (settings->getDoubleValue ("transientRelease", 50.0));
    tSensitivity.setValue (settings->getDoubleValue ("transientSensitivity", 70.0));
    tSearch.setValue (settings->getDoubleValue ("transientSearchMs", 250.0));
    tMatch.setValue (settings->getDoubleValue ("transientMinMatch", 0.3));
    tHold.setValue (settings->getDoubleValue ("transientHold", 4.0));
    applyTransientSettings();
    rangeBox.setSelectedId (settings->getIntValue ("levelRange", 96), juce::dontSendNotification);
    alertModeBox.setSelectedId (settings->getIntValue ("alertMode", 1), juce::dontSendNotification);
    peakHoldToggle.setToggleState (settings->getBoolValue ("peakHold", true), juce::dontSendNotification);
    tiltToggle.setToggleState (settings->getBoolValue ("tilt", false), juce::dontSendNotification);
    levelWeightToggle.setToggleState (settings->getBoolValue ("levelWeighted", false), juce::dontSendNotification);
    spectrum.setFloorDb (-(double) rangeBox.getSelectedId());
    spectrum.setPeakHold (peakHoldToggle.getToggleState());
    spectrum.setTilt (tiltToggle.getToggleState());
    restoring = false;
}

void MainComponent::saveState()
{
    auto* settings = properties.getUserSettings();

    if (settings == nullptr || options.demoSignal)
        return;

    settings->setValue ("format", formatBox.getSelectedId());
    settings->setValue ("fps", fpsBox.getSelectedId());
    settings->setValue ("window", windowBox.getSelectedId());
    settings->setValue ("lfeGainEverywhere", lfeGainToggle.getToggleState());

    {
        const char* keys[] = { "devTolSoft", "devTolMedium", "devTolStrong", "devLate", "devEarly", "devCeiling",
                               "devInfraWeight", "devInfraLo", "devInfraHi", "devLevelWeight", "devLevelRed",
                               "devRoomWorstWeight", "devRoomWorstShare", "devCoverageWeight", "devDipAllowance" };
        int index = 0;

        for (auto* slider : devSliders())
            settings->setValue (keys[index++], slider->value());
    }
    settings->setValue ("inertia", inertia.value());
    settings->setValue ("integration", integration.value());
    settings->setValue ("alertSensitivity", (int) currentLevel() + 1);
    settings->setValue ("decay", decay.value());
    settings->setValue ("scoreSpeed", scoreSpeed.value());

    {
        juce::String rooms;

        for (bool on : roomSelected)
            rooms << (on ? "1" : "0");

        settings->setValue ("roomSelection", rooms);
    }
    settings->setValue ("roomPro", juce::String (proView.getRooms().toText()));
    settings->setValue ("listeningLevel", listeningDbC);
    settings->setValue ("transientCheck", transientToggle.getToggleState());
    settings->setValue ("transientAttack", tAttack.value());
    settings->setValue ("transientHoldProfile", tHoldProfile.value());
    settings->setValue ("transientRelease", tRelease.value());
    settings->setValue ("transientSensitivity", tSensitivity.value());
    settings->setValue ("transientSearchMs", tSearch.value());
    settings->setValue ("transientMinMatch", tMatch.value());
    settings->setValue ("transientHold", tHold.value());
    settings->setValue ("levelRange", rangeBox.getSelectedId());
    settings->setValue ("alertMode", alertModeBox.getSelectedId());
    settings->setValue ("peakHold", peakHoldToggle.getToggleState());
    settings->setValue ("tilt", tiltToggle.getToggleState());
    settings->setValue ("levelWeighted", levelWeightToggle.getToggleState());

    if (auto xml = deviceManager.createStateXml())
        settings->setValue ("audioDevice", xml.get());

    settings->saveIfNeeded();
}

} // namespace subguard
