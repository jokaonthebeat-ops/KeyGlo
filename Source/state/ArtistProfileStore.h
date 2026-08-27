/*
    ArtistProfileStore - small JSON profiles under the user's application
    data folder. Analysis settings and note ranges only, never recorded
    audio (JUCE_IMPLEMENTATION_SPEC.md).

    All file I/O happens on the message thread or a worker, never the audio
    thread. dirOverride() lets the tests sandbox the real user directory.
*/

#pragma once
#include <JuceHeader.h>
#include "../analysis/VocalRangeProfiler.h"

namespace keyglo
{

class ArtistProfileStore
{
public:
    static juce::File& dirOverride()
    {
        static juce::File override;
        return override;
    }

    static juce::File directory()
    {
        if (dirOverride() != juce::File())
            return dirOverride();
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("Diamond Loopz").getChildFile ("KeyGlo").getChildFile ("Profiles");
    }

    static juce::File fileFor (const juce::String& name)
    {
        auto safe = juce::File::createLegalFileName (name.trim());
        if (safe.isEmpty())
            safe = "Untitled";
        return directory().getChildFile (safe + ".keygloprofile");
    }

    static bool save (const juce::String& name, const ArtistProfile& p)
    {
        directory().createDirectory();

        juce::DynamicObject::Ptr obj (new juce::DynamicObject());
        obj->setProperty ("name", name);
        obj->setProperty ("version", 1);
        obj->setProperty ("comfortableLowMidi", p.comfortableLowMidi);
        obj->setProperty ("comfortableHighMidi", p.comfortableHighMidi);
        obj->setProperty ("strongLowMidi", p.strongLowMidi);
        obj->setProperty ("strongHighMidi", p.strongHighMidi);
        obj->setProperty ("extendedLowMidi", p.extendedLowMidi);
        obj->setProperty ("extendedHighMidi", p.extendedHighMidi);
        obj->setProperty ("falsettoHighMidi", p.falsettoHighMidi);
        obj->setProperty ("hasFalsetto", p.hasFalsetto);

        return fileFor (name).replaceWithText (juce::JSON::toString (juce::var (obj.get())));
    }

    static bool load (const juce::String& name, ArtistProfile& out)
    {
        auto file = fileFor (name);
        if (! file.existsAsFile())
            return false;

        auto parsed = juce::JSON::parse (file.loadFileAsString());
        if (! parsed.isObject())
            return false;

        auto get = [&parsed] (const char* key, int fallback)
        {
            return (int) parsed.getProperty (key, fallback);
        };

        ArtistProfile p;
        p.comfortableLowMidi  = get ("comfortableLowMidi", p.comfortableLowMidi);
        p.comfortableHighMidi = get ("comfortableHighMidi", p.comfortableHighMidi);
        p.strongLowMidi       = get ("strongLowMidi", p.strongLowMidi);
        p.strongHighMidi      = get ("strongHighMidi", p.strongHighMidi);
        p.extendedLowMidi     = get ("extendedLowMidi", p.extendedLowMidi);
        p.extendedHighMidi    = get ("extendedHighMidi", p.extendedHighMidi);
        p.falsettoHighMidi    = get ("falsettoHighMidi", p.falsettoHighMidi);
        p.hasFalsetto         = (bool) parsed.getProperty ("hasFalsetto", false);
        out = p;
        return true;
    }

    static juce::StringArray listProfiles()
    {
        juce::StringArray names;
        for (const auto& f : directory().findChildFiles (juce::File::findFiles, false,
                                                          "*.keygloprofile"))
            names.add (f.getFileNameWithoutExtension());
        names.sort (true);
        return names;
    }
};

} // namespace keyglo
