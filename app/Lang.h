// Interface language: English (the text in the code) or French. tr() gives the
// text in the current language; a text missing from the French table stays in
// English. The language is chosen with the flag at the top left; the window
// then rebuilds its content so that every label follows.

#pragma once

#include <juce_core/juce_core.h>

namespace subguard
{

enum class Language { english, french };

Language currentLanguage();
void setLanguage (Language);

/** The text in the current language (the English text is the key). */
juce::String tr (const juce::String& english);

/** The language saved in the settings (read before the window is built). */
Language savedLanguage();
void saveLanguage (Language);

} // namespace subguard
