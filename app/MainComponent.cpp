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
    constexpr int headerHeight = 170, panelHeight = 178;
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
    valueLabel.setBounds (top.removeFromRight (60));
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
    addAndMakeVisible (panel);
    addAndMakeVisible (spectrum);
    addAndMakeVisible (transientStrip);
    addAndMakeVisible (correlation);
    addAndMakeVisible (settingsButton);
    buildSettingsPanel();

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

    setSize (1000, 900);
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
void MainComponent::buildSettingsPanel()
{
    for (auto* group : { &sourceGroup, &analysisGroup, &responseGroup, &alertGroup })
    {
        group->setColour (juce::GroupComponent::outlineColourId, theme::outline);
        group->setColour (juce::GroupComponent::textColourId, theme::dimText);
        panel.addAndMakeVisible (group);
    }

    for (auto* label : { &formatLabel, &deviceLabel, &fpsLabel, &windowLabel, &statusLabel })
    {
        label->setFont (uiFont (13.0f));
        panel.addAndMakeVisible (label);
    }

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

    for (auto* box : { &formatBox, &deviceBox, &fpsBox, &windowBox })
        panel.addAndMakeVisible (box);

    lfeGainToggle.setTooltip ("Weigh the LFE as it plays in a cinema: 10 dB of in-band gain over a screen channel.");
    animationToggle.setToggleState (true, juce::dontSendNotification);
    animationToggle.onClick = [this] { header.setAnimationEnabled (animationToggle.getToggleState()); };
    panel.addAndMakeVisible (lfeGainToggle);
    panel.addAndMakeVisible (animationToggle);

    inertia.format = [] (double v) { return juce::String ((int) v) + "%"; };
    integration.format = [] (double v) { return juce::String (v, 1) + " s"; };
    threshold.format = [] (double v) { return juce::String::fromUTF8 ("\xc2\xb1") + juce::String (v, 2); };
    decay.format = [] (double v) { return juce::String (v, 2) + " s"; };

    for (auto* slider : { &inertia, &integration, &threshold, &decay })
    {
        slider->refresh();
        panel.addAndMakeVisible (slider);
    }

    settingsButton.setClickingTogglesState (true);
    settingsButton.setToggleState (true, juce::dontSendNotification);
    settingsButton.onClick = [this]
    {
        panelVisible = settingsButton.getToggleState();
        panel.setVisible (panelVisible);
        resized();
    };
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::window);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    header.setBounds (area.removeFromTop (juce::jmax (120, juce::jmin (headerHeight, getHeight() / 5))));

    if (panelVisible)
    {
        panel.setBounds (area.removeFromTop (panelHeight));
        auto row = panel.getLocalBounds().reduced (10, 2);

        const auto place = [&row] (juce::GroupComponent& group, int width)
        {
            group.setBounds (row.removeFromLeft (width));
            row.removeFromLeft (8);
            return group.getBounds().reduced (12, 6).withTrimmedTop (14);
        };

        auto source = place (sourceGroup, juce::jmax (290, row.getWidth() * 32 / 100));
        formatLabel.setBounds (source.removeFromTop (20));
        formatBox.setBounds (source.removeFromTop (26));
        source.removeFromTop (4);
        deviceLabel.setBounds (source.removeFromTop (20));
        deviceBox.setBounds (source.removeFromTop (26));
        statusLabel.setBounds (source.removeFromTop (30));

        auto analysis = place (analysisGroup, 300);
        auto combos = analysis.removeFromTop (46);
        auto fpsArea = combos.removeFromLeft (100);
        fpsLabel.setBounds (fpsArea.removeFromTop (20));
        fpsBox.setBounds (fpsArea.withTrimmedRight (6));
        windowLabel.setBounds (combos.removeFromTop (20));
        windowBox.setBounds (combos);
        analysis.removeFromTop (10);
        lfeGainToggle.setBounds (analysis.removeFromTop (26));
        animationToggle.setBounds (analysis.removeFromTop (26));

        const int sliderWidth = juce::jmax (150, (row.getWidth() - 8) / 2);
        auto response = place (responseGroup, sliderWidth);
        inertia.setBounds (response.removeFromTop (48));
        response.removeFromTop (8);
        integration.setBounds (response.removeFromTop (48));

        auto alert = place (alertGroup, juce::jmax (150, row.getWidth()));
        threshold.setBounds (alert.removeFromTop (48));
        alert.removeFromTop (8);
        decay.setBounds (alert.removeFromTop (48));
    }

    const int correlationHeight = juce::jmax (150, area.getHeight() * 2 / 7);
    correlation.setBounds (area.removeFromBottom (correlationHeight));
    transientStrip.setBounds (area.removeFromBottom (68));
    spectrum.setBounds (area);
    settingsButton.setBounds (area.getX() + 10, area.getY() + 4, 72, 22);
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

void MainComponent::rebuildEngine (double sampleRate)
{
    const double windows[] = { 0.5, 1.0, 2.0 };
    engineRate = sampleRate;
    engine = std::make_unique<LfeEngine> (sampleRate, windows[juce::jlimit (0, 2, windowBox.getSelectedId() - 1)]);
    alerts.reset (engine->result().grid.size());
}

void MainComponent::feedDemoSignal (int numFrames)
{
    scratch.resize ((size_t) numFrames * 4);

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

        float* frame = scratch.data() + (size_t) i * 4;
        frame[chL] = (float) (1.4 * shared);
        frame[chC] = (float) other;
        frame[chR] = (float) (1.4 * flipped);
        frame[chLFE] = (float) lfe;
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
            scratch.resize (4096 * 4);

            while (input.read (scratch.data(), 4096) > 0) {}

            rebuildEngine (input.sampleRate());
            updateStatus();
        }

        scratch.resize (4096 * 4);

        for (int n; (n = input.read (scratch.data(), 4096)) > 0;)
            engine->push (scratch.data(), n);

        level = juce::jlimit (0.0, 1.0, (juce::Decibels::gainToDecibels ((double) input.level(), -200.0) + 60.0) / 60.0);
    }
    else
    {
        // No device: feed silence so the display settles instead of freezing
        scratch.assign ((size_t) std::lround (engineRate * dt) * 4, 0.0f);
        engine->push (scratch.data(), (int) scratch.size() / 4);
    }

    header.setLevel (level);

    engineSettings.lfeGainDb = lfeGainToggle.getToggleState() ? lfeInBandGainDb : 0.0;
    engineSettings.tauSpectrum = 0.010 * std::pow (60.0, inertia.value() / 100.0);    // 10 ms .. 600 ms
    engineSettings.tauCorrelation = integration.value();

    if (engine->analyze (engineSettings))
    {
        alerts.update (engine->result(), threshold.value(), decay.value(), dt);
        spectrum.update (engine->result(), alerts);
        correlation.update (engine->result(), alerts, threshold.value());
    }

    // Transient detector: one verdict per attack found in a screen channel
    for (const auto& event : engine->takeTransientEvents())
    {
        const double thr = juce::jmin (threshold.value(), 0.99);
        const double severity = juce::jmin (1.0, AlertTracker::alertGain * (event.interactionBest - thr) / (1.0 - thr));

        if (severity > 0.0)
        {
            transientStrip.show (event, severity);
            transientFlash[(size_t) event.channel] = juce::jmax (transientFlash[(size_t) event.channel], severity);
        }
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
    threshold.setValue (settings->getDoubleValue ("alertThreshold", 0.5));
    decay.setValue (settings->getDoubleValue ("decay", 1.0));
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
    settings->setValue ("alertThreshold", threshold.value());
    settings->setValue ("decay", decay.value());

    if (auto xml = deviceManager.createStateXml())
        settings->setValue ("audioDevice", xml.get());

    settings->saveIfNeeded();
}

} // namespace subguard
