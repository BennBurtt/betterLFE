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

    // The same type everywhere, and text that shrinks a little rather than being cut
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override    { return uiFont (juce::jmin (13.5f, (float) buttonHeight * 0.55f)); }
    juce::Font getComboBoxFont (juce::ComboBox& box) override                       { return uiFont (juce::jmin (13.5f, (float) box.getHeight() * 0.55f)); }
    juce::Font getPopupMenuFont() override                                          { return uiFont (14.0f); }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down) override
    {
        const float fontSize = juce::jmin (13.0f, (float) button.getHeight() * 0.6f);
        const float tick = fontSize * 1.1f;
        auto area = button.getLocalBounds().toFloat();

        drawTickBox (g, button, 4.0f, (area.getHeight() - tick) * 0.5f, tick, tick, button.getToggleState(), button.isEnabled(), highlighted, down);

        g.setColour (button.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
        g.setFont (uiFont (fontSize));
        const auto text = area.withTrimmedLeft (tick + 10.0f).withTrimmedRight (2.0f);
        const int lines = text.getHeight() >= fontSize * 2.3f ? 2 : 1;
        g.drawFittedText (button.getButtonText(), text.toNearestInt(), juce::Justification::centredLeft, lines, 0.7f);
    }
};

/** The window, with the File and Edit menus: in the menu bar on the Mac, at the top of the window elsewhere. */
class MainWindow : public juce::DocumentWindow,
                   private juce::MenuBarModel
{
public:
    explicit MainWindow (const LaunchOptions& options)
        : DocumentWindow ("SubGUARD", theme::window, DocumentWindow::allButtons), launchOptions (options)
    {
        setUsingNativeTitleBar (true);
        setLanguage (savedLanguage());
        build();
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (this);
       #else
        setMenuBar (this);
       #endif
        setResizable (true, false);
        setResizeLimits (1000, 780, 10000, 10000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void build()
    {
        auto* content = new MainComponent (launchOptions);
        content->onLanguageToggle = [this]
        {
            // Rebuilt in the other language, same size: every label follows
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainWindow> (this)]
            {
                if (safe == nullptr)
                    return;

                const auto size = safe->getContentComponent() != nullptr ? safe->getContentComponent()->getBounds() : juce::Rectangle<int>();
                const auto next = currentLanguage() == Language::french ? Language::english : Language::french;
                safe->clearContentComponent();
                setLanguage (next);
                saveLanguage (next);
                safe->build();

                if (! size.isEmpty())
                    safe->getContentComponent()->setSize (size.getWidth(), size.getHeight());
            });
        };
        setContentOwned (content, true);
        menuItemsChanged();     // the menus follow the language
    }

    ~MainWindow() override
    {
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (nullptr);
       #else
        setMenuBar (nullptr);
       #endif
    }

    // File: save and load the Room Sim Pro rooms, export the event log. Edit: undo and redo the rooms' changes.
    juce::StringArray getMenuBarNames() override
    {
        // Edit only undoes and redoes the Room Sim Pro rooms: not shown while Room Sim Pro is set aside
        return MainComponent::roomProEnabled ? juce::StringArray { tr ("File"), tr ("Edit") } : juce::StringArray { tr ("File") };
    }

    juce::PopupMenu getMenuForIndex (int index, const juce::String&) override
    {
        juce::PopupMenu menu;
        const juce::String command = juce::String::fromUTF8 ("\xe2\x8c\x98"), option = juce::String::fromUTF8 ("\xe2\x8c\xa5"),
                           shift = juce::String::fromUTF8 ("\xe2\x87\xa7");

        const auto item = [&menu] (int id, const juce::String& text, const juce::String& keys)
        {
            juce::PopupMenu::Item i (text);
            i.itemID = id;
            i.shortcutKeyDescription = keys;
            menu.addItem (i);
        };

        if (index == 0)
        {
            if (MainComponent::roomProEnabled)
            {
                item (1, tr ("Save the rooms..."), command + "S");
                item (2, tr ("Load rooms..."), command + "O");
                menu.addSeparator();
            }

            item (5, tr ("Export the event log (CSV)..."), {});
            item (6, tr ("Export markers (EDL)..."), {});
            menu.addSeparator();
            item (7, tr ("Analyse files..."), {});
        }
        else
        {
            item (3, tr ("Undo"), option + "Z");
            item (4, tr ("Redo"), option + shift + "Z");
        }

        return menu;
    }

    void menuItemSelected (int id, int) override
    {
        auto* content = dynamic_cast<MainComponent*> (getContentComponent());

        if (content == nullptr)
            return;

        auto& pro = content->roomPro();

        switch (id)
        {
            case 1:     pro.exportConfig(); break;
            case 2:     pro.importConfig(); break;
            case 3:     pro.undo(); break;
            case 4:     pro.redo(); break;
            case 5:     content->exportLogCsv(); break;
            case 6:     content->exportLogEdl(); break;
            case 7:     content->analyseFiles(); break;
            default:    break;
        }
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplicationBase::quit();
    }

private:
    LaunchOptions launchOptions;
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

        const int card = args.indexOf ("--card");

        if (card >= 0 && card + 1 < args.size())
            options.openCard = args[card + 1];

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
