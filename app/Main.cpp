#include "MainComponent.h"

namespace subguard
{

class DarkLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DarkLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, theme::window);
        setColour (juce::Label::textColourId, theme::text);
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff252525));
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff555555));
        setColour (juce::ComboBox::textColourId, theme::text);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff252525));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff3d6fa5));
        setColour (juce::Slider::thumbColourId, juce::Colour (0xffd0d0d0));
        setColour (juce::Slider::trackColourId, juce::Colour (0xff3d8bd9));
        setColour (juce::Slider::backgroundColourId, juce::Colour (0xff3a3a3a));
        setColour (juce::TextButton::buttonColourId, juce::Colour (0xcc2a2a2a));
        setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xcc505050));
        setColour (juce::TextButton::textColourOffId, theme::dimText);
        setColour (juce::TextButton::textColourOnId, theme::text);
        setColour (juce::ToggleButton::textColourId, theme::text);
        setColour (juce::ToggleButton::tickColourId, theme::text);
        setColour (juce::ToggleButton::tickDisabledColourId, theme::dimText);
    }
};

class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow (const LaunchOptions& options)
        : DocumentWindow ("SubGUARD", theme::window, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent (options), true);
        setResizable (true, false);
        setResizeLimits (950, 720, 10000, 10000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplicationBase::quit();
    }
};

class SubGuardApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override      { return "SubGuard"; }
    const juce::String getApplicationVersion() override   { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override            { return false; }

    void initialise (const juce::String&) override
    {
        const auto args = getCommandLineParameterArray();
        LaunchOptions options;
        options.demoSignal = args.contains ("--demo");

        const int snapshot = args.indexOf ("--snapshot");

        if (snapshot >= 0 && snapshot + 1 < args.size())
            options.snapshotFile = juce::File::getCurrentWorkingDirectory().getChildFile (args[snapshot + 1]);

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        window = std::make_unique<MainWindow> (options);
    }

    void shutdown() override
    {
        window.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    DarkLookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> window;
};

} // namespace subguard

START_JUCE_APPLICATION (subguard::SubGuardApplication)
