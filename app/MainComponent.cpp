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
    constexpr int frame = AudioInput::frameSize;
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

void LabelledSlider::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (20);
    valueLabel.setBounds (top.removeFromRight (120));
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
    addAndMakeVisible (spectrum);
    addAndMakeVisible (transientStrip);
    addAndMakeVisible (correlation);
    addAndMakeVisible (history);
    buildCards();

    for (auto* speaker : { &header.left, &header.centre, &header.right })
        speaker->onToggle = [this]
        {
            engineSettings.mute = { { ! header.left.isActive(), ! header.centre.isActive(), ! header.right.isActive() } };
        };

    // Demo signal: LFE made of low tones; L shares its 28-50 Hz part in phase,
    // R its 70-110 Hz part with flipped polarity, C is unrelated.
    juce::Random random (42);

    for (double f = 21.0; f < 115.0; f += 1.3)
        demoTones.push_back ({ { f, random.nextDouble() * juce::MathConstants<double>::twoPi, 0.0 } });

    for (double f = 22.0; f < 150.0; f += 1.7)
        demoTones.push_back ({ { f, random.nextDouble() * juce::MathConstants<double>::twoPi, 1.0 } });

    loadState();
    applyFormat();
    rebuildEngine (preferredSampleRate);

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
                                                   safe->updateStatus ("Audio input permission denied");
                                           });
    }
    else
    {
        statusLabel.setText ("Demo signal (no audio device)", juce::dontSendNotification);
    }

    setSize (1200, 1000);

    const juce::StringArray cardNames { "source", "analysis", "spectrum", "response", "alert", "transients", "chains" };
    showCard (cardNames.indexOf (options.openCard));

    lastTick = juce::Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (fpsBox.getSelectedId());
}

MainComponent::~MainComponent()
{
    stopTimer();
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

        if (! restoring && ! options.demoSignal)
            selectDevice (deviceBox.getText());
    };

    deviceBox.setTextWhenNothingSelected ("No input device");
    deviceBox.onChange = [this]
    {
        if (! restoring && deviceBox.getSelectedId() > 0)
            selectDevice (deviceBox.getText());
    };

    fpsBox.addItem ("90 FPS", 90);
    fpsBox.addItem ("60 FPS", 60);
    fpsBox.addItem ("30 FPS", 30);
    fpsBox.setSelectedId (60, juce::dontSendNotification);
    fpsBox.onChange = [this] { startTimerHz (fpsBox.getSelectedId()); };

    windowBox.addItem (juce::String::fromUTF8 ("Fast (0.5 s \xc2\xb7 4 Hz)"), 1);
    windowBox.addItem (juce::String::fromUTF8 ("Normal (1 s \xc2\xb7 2 Hz)"), 2);
    windowBox.addItem (juce::String::fromUTF8 ("Ultra (2 s \xc2\xb7 1 Hz)"), 3);
    windowBox.setSelectedId (2, juce::dontSendNotification);
    windowBox.onChange = [this] { rebuildEngine (engineRate); };

    for (int db : { 72, 96, 120, 144 })
        rangeBox.addItem ("0 to -" + juce::String (db) + " dB", db);

    rangeBox.setSelectedId (96, juce::dontSendNotification);
    rangeBox.onChange = [this] { spectrum.setFloorDb (-(double) rangeBox.getSelectedId()); };

    alertModeBox.addItem ("In phase and out of phase", 1);
    alertModeBox.addItem ("Out of phase only", 2);
    alertModeBox.setSelectedId (1, juce::dontSendNotification);
    alertModeBox.setTooltip ("Out of phase only: alert just where the LFE works against the screen channel.");


    peakHoldToggle.setToggleState (true, juce::dontSendNotification);
    peakHoldToggle.onClick = [this] { spectrum.setPeakHold (peakHoldToggle.getToggleState()); };
    tiltToggle.setTooltip ("Display tilt of the first version: -12 dB at 20 Hz. Off: levels are true dBFS.");
    tiltToggle.onClick = [this] { spectrum.setTilt (tiltToggle.getToggleState()); };
    levelWeightToggle.setTooltip ("Off: the alert follows the correlation alone, however quiet the LFE is.\n"
                                  "On: an LFE much quieter (or louder) than the screen channel counts less.");


    lfeGainToggle.setTooltip ("Weigh the LFE as it plays in a cinema: 10 dB of in-band gain over a screen channel.");
    animationToggle.setToggleState (true, juce::dontSendNotification);
    animationToggle.onClick = [this] { header.setAnimationEnabled (animationToggle.getToggleState()); };

    inertia.format = [] (double v) { return juce::String ((int) v) + "%"; };
    integration.format = [] (double v) { return juce::String (v, 1) + " s"; };
    // Level selector, above the spectrum on the right: one choice sets both
    // the phase alert threshold and how strict the robustness score is.
    const char* levelNames[] = { "Soft", "Medium", "Strong" };
    const char* levelTips[] = {
        "Soft: phase alert from a correlation of 0.90; the score tolerates 3 dB of change before counting it.",
        "Medium: phase alert from 0.75; the score tolerates 2 dB.",
        "Strong: phase alert from 0.50; the score counts changes from 1 dB, about the smallest audible one."
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

    for (auto* slider : { &inertia, &integration, &decay })
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

    transientToggle.setTooltip ("Off by default. Solo in your DAW what you want to check (a kick and its LFE send, for instance), then switch this on.");
    tAttack.setTooltip ("How fast a rise must be to count as a transient: the time over which the peak is watched. "
                        "Low: only sharp hits. High: slower swells count too.");
    tHoldProfile.setTooltip ("After a transient, no new one for this long: a kick, its click and its cymbal stay one hit.");
    tRelease.setTooltip ("How long the tail of a hit hides what follows. Higher: a ringing tail is not taken for new transients. "
                         "Lower: quieter hits soon after a loud one are found.");
    tSensitivity.setTooltip ("How small a rise counts as a transient. Higher: more transients found, and more false ones.");
    tSearch.setTooltip ("The LFE is looked for this far before and after each attack of the mix. Wider finds a very late LFE; "
                        "narrower is more precise on dense music and on weak hits.");
    tMatch.setTooltip ("Above this likeness between mix and LFE, the delay and polarity come from the correlation (precise). "
                       "Below, the two attacks are timed against each other (coarse, no phase).");
    tHold.setTooltip ("How long a verdict stays on screen.");

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



    resetSessionButton.onClick = [this]
    {
        if (engine != nullptr)
            engine->resetGlobal();
    };

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

    populate (sourceCard, 360, 196, { &formatLabel, &formatBox, &deviceLabel, &deviceBox, &statusLabel },
              [this] (juce::Rectangle<int> r)
              {
                  formatLabel.setBounds (r.removeFromTop (20));
                  formatBox.setBounds (r.removeFromTop (26));
                  r.removeFromTop (6);
                  deviceLabel.setBounds (r.removeFromTop (20));
                  deviceBox.setBounds (r.removeFromTop (26));
                  r.removeFromTop (4);
                  statusLabel.setBounds (r.removeFromTop (30));
              });

    populate (analysisCard, 330, 156, { &fpsLabel, &fpsBox, &windowLabel, &windowBox, &lfeGainToggle, &animationToggle },
              [this] (juce::Rectangle<int> r)
              {
                  auto combos = r.removeFromTop (46);
                  auto fps = combos.removeFromLeft (100);
                  fpsLabel.setBounds (fps.removeFromTop (20));
                  fpsBox.setBounds (fps.withTrimmedRight (6));
                  windowLabel.setBounds (combos.removeFromTop (20));
                  windowBox.setBounds (combos);
                  r.removeFromTop (8);
                  lfeGainToggle.setBounds (r.removeFromTop (26));
                  animationToggle.setBounds (r.removeFromTop (26));
              });

    populate (spectrumCard, 250, 156, { &rangeLabel, &rangeBox, &peakHoldToggle, &tiltToggle },
              [this] (juce::Rectangle<int> r)
              {
                  rangeLabel.setBounds (r.removeFromTop (20));
                  rangeBox.setBounds (r.removeFromTop (26));
                  r.removeFromTop (8);
                  peakHoldToggle.setBounds (r.removeFromTop (26));
                  tiltToggle.setBounds (r.removeFromTop (26));
              });

    populate (responseCard, 300, 150, { &inertia, &integration },
              [this] (juce::Rectangle<int> r)
              {
                  inertia.setBounds (r.removeFromTop (46));
                  r.removeFromTop (8);
                  integration.setBounds (r.removeFromTop (46));
              });

    populate (alertCard, 320, 156, { &decay, &alertModeBox, &levelWeightToggle },
              [this] (juce::Rectangle<int> r)
              {
                  decay.setBounds (r.removeFromTop (46));
                  r.removeFromTop (8);
                  alertModeBox.setBounds (r.removeFromTop (26));
                  r.removeFromTop (4);
                  levelWeightToggle.setBounds (r.removeFromTop (24));
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

    populate (chainsCard, 600, 352, { &chainsView, &resetSessionButton },
              [this] (juce::Rectangle<int> r)
              {
                  resetSessionButton.setBounds (r.removeFromBottom (24).removeFromRight (110));
                  chainsView.setBounds (r.withTrimmedBottom (-24));
                  resetSessionButton.toFront (false);
              });

    const char* names[] = { "Source", "Analysis", "Spectrum", "Response", "Alert", "Transients", "Playback chains" };

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

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::window);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    header.setBounds (area.removeFromTop (juce::jmax (110, juce::jmin (headerHeight, getHeight() / 6))));

    auto bar = area.removeFromTop (barHeight).reduced (10, 6);
    const int widths[numCards] = { 90, 90, 90, 90, 80, 100, 140 };

    for (int i = 0; i < numCards; ++i)
    {
        if (i == chainsCard)
            bar.removeFromLeft (14);

        cardButtons[(size_t) i].setBounds (bar.removeFromLeft (widths[i]));
        bar.removeFromLeft (6);
    }

    history.setBounds (area.removeFromBottom (48));
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
    deviceBox.setSelectedId (names.indexOf (current) + 1, juce::dontSendNotification);
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

void MainComponent::updateStatus (const juce::String& error)
{
    juce::String text;
    auto colour = theme::text;

    if (error.isNotEmpty())
    {
        text = "Error: " + error;
        colour = juce::Colour (0xffff6b6b);
    }
    else if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const int active = device->getActiveInputChannels().countNumberOfSetBits();
        text = juce::String (active) + " ch @ " + juce::String ((int) device->getCurrentSampleRate()) + " Hz";

        if (active < channelsNeeded())
        {
            text = "Needs " + juce::String (channelsNeeded()) + " channels, device gives " + juce::String (active);
            colour = juce::Colour (0xffffb347);
        }
    }
    else
    {
        text = "No input device open";
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

    if (engine != nullptr)
        engine->setTransientSettings (t);

    transientStrip.setActive (t.enabled);
    transientStrip.setMinMatch (t.minMatch);
    transientStrip.setHoldSeconds (tHold.value());
}

void MainComponent::rebuildEngine (double sampleRate)
{
    const double windows[] = { 0.5, 1.0, 2.0 };
    engineRate = sampleRate;
    engine = std::make_unique<LfeEngine> (sampleRate, windows[juce::jlimit (0, 2, windowBox.getSelectedId() - 1)]);
    applyTransientSettings();
    alerts.reset (engine->result().grid.size());
}

void MainComponent::feedDemoSignal (int numFrames)
{
    scratch.assign ((size_t) numFrames * frame, 0.0f);

    for (int i = 0; i < numFrames; ++i)
    {
        double lfe = 0.0, shared = 0.0, flipped = 0.0, other = 0.0;
        const double t = demoTime + i / engineRate;

        for (const auto& tone : demoTones)
        {
            const double v = 0.012 * std::sin (juce::MathConstants<double>::twoPi * tone[0] * t + tone[1]);

            if (tone[2] > 0.5)      { other += v; continue; }

            lfe += v;

            if (tone[0] > 28.0 && tone[0] < 50.0)    shared += v;
            if (tone[0] > 70.0 && tone[0] < 110.0)   flipped -= v;
        }

        // A kick in the centre channel every 1.5 s, repeated 12 ms late in the LFE
        const auto kick = [] (double u)
        {
            if (u < 0.0)
                return 0.0;

            const double phase = juce::MathConstants<double>::twoPi * (45.0 * u + 75.0 * 0.03 * (1.0 - std::exp (-u / 0.03)));
            return 0.5 * std::sin (phase) * std::exp (-u / 0.09) * (1.0 - std::exp (-u / 0.002));
        };

        const double sinceKick = std::fmod (t, 1.5);
        other += kick (sinceKick);
        lfe += kick (sinceKick - 0.012);

        float* out = scratch.data() + (size_t) i * frame;
        out[chL] = (float) (1.4 * shared);
        out[chC] = (float) other;
        out[chR] = (float) (1.4 * flipped);
        out[chLFE] = (float) lfe;
    }

    demoTime += numFrames / engineRate;
    engine->push (scratch.data(), numFrames);
}

//==============================================================================
void MainComponent::timerCallback()
{
    const double nowSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double dt = juce::jlimit (0.001, 0.25, nowSeconds - lastTick);
    lastTick = nowSeconds;
    double level = 0.0;

    if (options.demoSignal)
    {
        feedDemoSignal ((int) std::lround (engineRate * dt));
        level = 0.6;
    }
    else if (input.isRunning())
    {
        // A (re)started device may run at another rate: start from a clean engine
        if (input.generation() != inputGeneration || std::abs (input.sampleRate() - engineRate) > 0.5)
        {
            inputGeneration = input.generation();
            scratch.resize ((size_t) 4096 * frame);

            while (input.read (scratch.data(), 4096) > 0) {}

            rebuildEngine (input.sampleRate());
            updateStatus();
        }

        scratch.resize ((size_t) 4096 * frame);

        for (int n; (n = input.read (scratch.data(), 4096)) > 0;)
            engine->push (scratch.data(), n);

        level = juce::jlimit (0.0, 1.0, (juce::Decibels::gainToDecibels ((double) input.level(), -200.0) + 60.0) / 60.0);
    }
    else
    {
        // No device: feed silence so the display settles instead of freezing
        scratch.assign ((size_t) std::lround (engineRate * dt) * frame, 0.0f);
        engine->push (scratch.data(), (int) scratch.size() / frame);
    }

    header.setLevel (level);

    engineSettings.lfeGainDb = lfeGainToggle.getToggleState() ? lfeInBandGainDb : 0.0;
    engineSettings.tauSpectrum = 0.010 * std::pow (60.0, inertia.value() / 100.0);    // 10 ms .. 600 ms
    engineSettings.tauCorrelation = integration.value();

    const auto alerting = alertOptions();

    if (engine->analyze (engineSettings))
    {
        // Nothing in the LFE any more: the engine has cleared what it measured
        // against it, so the display starts from a clean slate too.
        if (engine->takeAutoReset())
        {
            alerts.reset (engine->result().grid.size());

            for (auto& channel : curveInertia)
                channel.clear();
        }

        alerts.update (engine->result(), alerting, dt);
        spectrum.update (engine->result(), alerts, dt);

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

        correlation.update (engine->result(), shownAlerts, alerting.threshold, alerting.outOfPhaseOnly);

        // Playback chains, robustness, LFE dependence, overlap
        ChainsInfo info;
        info.translation = simulator.compute (engine->result(), currentLevel());
        info.sessionCorrelation = { { engine->globalCorrelation (0), engine->globalCorrelation (1), engine->globalCorrelation (2) } };
        info.sessionSeconds = engine->globalSeconds();
        auto& t = info.translation;

        if (! t.hasBass)
        {
            for (auto& filter : readoutInertia)
                filter.reset();

            chainInertia.clear();
            displayedScore = -1;
        }
        else
        {
            t.robustness = readoutInertia[0].update (t.robustness, dt);
            t.interaction = readoutInertia[1].update (t.interaction, dt);
            t.dependence = readoutInertia[2].update (t.dependence, dt);
            t.lfeBalanceDb = readoutInertia[3].update (t.lfeBalanceDb, dt);
            t.lfeShare = readoutInertia[4].update (t.lfeShare, dt);
            const size_t perChain = 2 * ChainResult::numGains;
            chainInertia.resize (t.chains.size() * perChain);

            for (size_t k = 0; k < t.chains.size(); ++k)
            {
                auto& chain = t.chains[k];

                for (size_t v = 0; v < (size_t) ChainResult::numGains; ++v)
                {
                    chain.gainChangeDb[v] = chainInertia[k * perChain + 2 * v].update (chain.gainChangeDb[v], dt);
                    chain.gainPenalty[v] = chainInertia[k * perChain + 2 * v + 1].update (chain.gainPenalty[v], dt);
                }

                chain.changeDb = chain.gainChangeDb[(size_t) ChainResult::numGains / 2];
                chain.penalty = chain.gainPenalty[(size_t) ChainResult::numGains / 2];
            }

            // The number on screen only changes once it has really moved
            if (displayedScore < 0 || std::abs (t.robustness - displayedScore) >= 1.5)
                displayedScore = juce::roundToInt (t.robustness);
        }

        if (cards[chainsCard].isVisible())
            chainsView.update (info);

        spectrum.setScore ((double) displayedScore);
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
        historyClock = 0.0;
        history.push (historySample);
        historySample = {};
    }

    // Transient engine: one event per attack of the mix
    for (const auto& event : engine->takeTransientEvents())
    {
        transientStrip.show (event);

        // The reference is the downmix: every screen channel lights up on a fault
        if (transientStrip.isFault (event))
            for (auto& flash : transientFlash)
                flash = 1.0;
    }

    transientStrip.advance (dt);
    SpeakerIndicator* speakers[] = { &header.left, &header.centre, &header.right };

    for (size_t c = 0; c < 3; ++c)
    {
        transientFlash[c] = juce::jmax (0.0, transientFlash[c] - dt / 0.6);
        speakers[c]->setLevel (juce::jmax (alerts.speakerLevel[c], transientFlash[c]));
    }

    if (options.snapshotFile != juce::File() && ++framesShown == 240)
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
    lfeGainToggle.setToggleState (settings->getBoolValue ("lfeGain", false), juce::dontSendNotification);
    animationToggle.setToggleState (settings->getBoolValue ("animation", true), juce::dontSendNotification);
    header.setAnimationEnabled (animationToggle.getToggleState());
    inertia.setValue (settings->getDoubleValue ("inertia", 70.0));
    integration.setValue (settings->getDoubleValue ("integration", 2.0));
    levelButtons[(size_t) juce::jlimit (0, 2, settings->getIntValue ("alertSensitivity", 3) - 1)].setToggleState (true, juce::dontSendNotification);
    decay.setValue (settings->getDoubleValue ("decay", 1.0));
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
    settings->setValue ("lfeGain", lfeGainToggle.getToggleState());
    settings->setValue ("animation", animationToggle.getToggleState());
    settings->setValue ("inertia", inertia.value());
    settings->setValue ("integration", integration.value());
    settings->setValue ("alertSensitivity", (int) currentLevel() + 1);
    settings->setValue ("decay", decay.value());
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
