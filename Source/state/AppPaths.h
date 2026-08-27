/*
    AppPaths - the one place KeyGlo decides where its user data lives.

    JUCE's userApplicationDataDirectory is NOT the same shape on every
    platform: on macOS it is ~/Library (so the Apple convention needs
    "Application Support" appended), while on Windows it is already
    AppData/Roaming (where appending "Application Support" would produce a
    nonsense path).

    Getting this wrong is quiet: each store still reads back whatever it
    wrote, so nothing fails - the data just lives somewhere the user was
    never told about. Before this helper existed, KeyGlo's presets and
    artist profiles were split across TWO different folders on macOS, and
    the shipped Read Me described a path that only one of them used.
*/

#pragma once
#include <JuceHeader.h>

namespace keyglo
{

struct AppPaths
{
    // macOS:   ~/Library/Application Support/Diamond Loopz/KeyGlo
    // Windows: %APPDATA%/Diamond Loopz/KeyGlo
    // Linux:   ~/.config/Diamond Loopz/KeyGlo
    static juce::File dataDirectory()
    {
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

       #if JUCE_MAC
        base = base.getChildFile ("Application Support");
       #endif

        return base.getChildFile ("Diamond Loopz").getChildFile ("KeyGlo");
    }

    static juce::File profilesDirectory()   { return dataDirectory().getChildFile ("Profiles"); }
    static juce::File presetsDirectory()    { return dataDirectory().getChildFile ("Presets").getChildFile ("User"); }
};

} // namespace keyglo
