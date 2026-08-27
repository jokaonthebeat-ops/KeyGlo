/*
    PresetManager - factory + user presets over the APVTS.

    Presets cover ONLY the creative parameters (range/key sense, smooth,
    preview mix, fine tune, transpose selection, A/B). Output trim, bypass
    and Solo are excluded so loading a preset never jumps the level or the
    monitoring state (the SourceGlo preset-contract rule).

    Modified tracking is snapshot-based: the values captured at load are
    compared against the live parameters - no listener ordering to get
    wrong (the EQGlo trap).

    User presets are small JSON files under
    ~/Library/Application Support/Diamond Loopz/KeyGlo/Presets/User;
    `dirOverride()` sandboxes tests. Names are sanitised to their file form
    so a preset cannot escape the folder.
*/

#pragma once
#include <JuceHeader.h>
#include <map>
#include <vector>

namespace keyglo
{

// A parameter move recorded for undo. APVTS parameter changes made through
// setValueNotifyingHost do NOT reach the tree's UndoManager (only direct
// ValueTree edits do), so the plugin records its own actions: the change has
// already happened by the time it is pushed, hence the first perform() is a
// no-op and later redos re-apply it.
class ParameterChangeAction : public juce::UndoableAction
{
public:
    ParameterChangeAction (juce::AudioProcessorValueTreeState& stateIn,
                           std::map<juce::String, float> beforeIn,
                           std::map<juce::String, float> afterIn)
        : apvts (stateIn), before (std::move (beforeIn)), after (std::move (afterIn)) {}

    bool perform() override
    {
        if (! applied)          // the caller already made this change
        {
            applied = true;
            return true;
        }
        return apply (after);
    }

    bool undo() override        { return apply (before); }
    int getSizeInUnits() override { return (int) (before.size() * sizeof (float) * 2); }

private:
    bool apply (const std::map<juce::String, float>& values)
    {
        for (const auto& [id, value] : values)
            if (auto* param = apvts.getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (value));
        return true;
    }

    juce::AudioProcessorValueTreeState& apvts;
    std::map<juce::String, float> before, after;
    bool applied = false;
};

class PresetManager
{
public:
    struct Preset
    {
        juce::String name;
        std::map<juce::String, float> values;   // param id -> plain value
        bool factory = false;
    };

    static const juce::StringArray& creativeParams()
    {
        static const juce::StringArray ids { "rangeSense", "keySense", "analysisSmooth",
                                             "previewMix", "fineTuneCents",
                                             "transposeSemitones", "previewRecommended" };
        return ids;
    }

    // ------------------------------------------------------------------
    PresetManager (juce::AudioProcessorValueTreeState& stateIn,
                   juce::UndoManager* undoIn)
        : apvts (stateIn), undo (undoIn)
    {
        rebuildList();
        loadSnapshotFrom (presets[0]);   // fresh instance sits on preset 0
        undoSnapshot = currentValues();
    }

    // --- undo capture ------------------------------------------------------
    // Called periodically from the UI: any creative-parameter drift since the
    // last capture becomes one undoable step, so a knob drag coalesces into a
    // single entry instead of one per sample value.
    void captureUndoPoint()
    {
        auto now = currentValues();
        if (undo == nullptr || now == undoSnapshot)
            return;

        undo->beginNewTransaction();
        undo->perform (new ParameterChangeAction (apvts, undoSnapshot, now));
        undoSnapshot = std::move (now);
    }

    // After an undo/redo the live values changed behind our back; re-baseline
    // so the next capture does not record the undo itself as a new step.
    void resyncUndoBaseline()   { undoSnapshot = currentValues(); }

    // --- factory bank ------------------------------------------------------
    // Preset 0 is the neutral fresh-instance state; the rest are voiced
    // starting points per workflow. transpose index 4 = Original.
    static std::vector<Preset> factoryPresets()
    {
        auto make = [] (const char* name, float range, float key, float smooth,
                        float mix, float fine, float transposeIndex, float ab)
        {
            Preset p;
            p.name = name;
            p.factory = true;
            p.values = { { "rangeSense", range }, { "keySense", key },
                         { "analysisSmooth", smooth }, { "previewMix", mix },
                         { "fineTuneCents", fine },
                         { "transposeSemitones", transposeIndex },
                         { "previewRecommended", ab } };
            return p;
        };

        return {
            make ("Male Rap Hook Match",   0.72f, 0.85f, 0.60f, 0.40f, 0.0f, 4.0f, 1.0f),
            make ("Female R&B Range",      0.80f, 0.80f, 0.70f, 0.45f, 0.0f, 4.0f, 1.0f),
            make ("Melodic Rap Fit",       0.75f, 0.90f, 0.55f, 0.50f, 0.0f, 4.0f, 1.0f),
            make ("Soul Hook Builder",     0.85f, 0.75f, 0.75f, 0.45f, 0.0f, 4.0f, 1.0f),
            make ("808 Tune Focus",        0.50f, 0.95f, 0.40f, 0.60f, 0.0f, 4.0f, 1.0f),
            make ("Producer Quick Check",  0.60f, 0.70f, 0.30f, 0.25f, 0.0f, 4.0f, 0.0f),
        };
    }

    static constexpr int factoryCount = 6;

    // --- user preset storage -----------------------------------------------
    static juce::File& dirOverride()
    {
        static juce::File override;
        return override;
    }

    static juce::File userDir()
    {
        if (dirOverride() != juce::File())
            return dirOverride();
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("Application Support/Diamond Loopz/KeyGlo/Presets/User");
    }

    static juce::File fileFor (const juce::String& name)
    {
        return userDir().getChildFile (juce::File::createLegalFileName (name)
                                         + ".keyglopreset");
    }

    // --- list --------------------------------------------------------------
    void rebuildList()
    {
        presets = factoryPresets();
        userDir().createDirectory();
        auto files = userDir().findChildFiles (juce::File::findFiles, false,
                                               "*.keyglopreset");
        files.sort();
        for (const auto& f : files)
        {
            auto parsed = juce::JSON::parse (f.loadFileAsString());
            if (auto* obj = parsed.getDynamicObject())
            {
                Preset p;
                p.name = obj->getProperty ("name").toString();
                if (p.name.isEmpty())
                    p.name = f.getFileNameWithoutExtension();
                p.factory = false;
                if (auto* vals = obj->getProperty ("values").getDynamicObject())
                    for (const auto& id : creativeParams())
                        if (vals->hasProperty (id))
                            p.values[id] = (float) (double) vals->getProperty (id);
                if (! p.values.empty())
                    presets.push_back (std::move (p));
            }
        }
    }

    const std::vector<Preset>& all() const   { return presets; }
    int getCurrentIndex() const              { return currentIndex; }
    juce::String currentName() const         { return presets[(size_t) currentIndex].name; }
    bool currentIsFactory() const            { return presets[(size_t) currentIndex].factory; }

    // --- selection ---------------------------------------------------------
    void select (int index)
    {
        currentIndex = juce::jlimit (0, (int) presets.size() - 1, index);
        apply (presets[(size_t) currentIndex]);
    }

    void step (int delta)
    {
        const int n = (int) presets.size();
        select (((currentIndex + delta) % n + n) % n);
    }

    // --- save --------------------------------------------------------------
    bool saveCurrentAs (const juce::String& name)
    {
        if (name.trim().isEmpty())
            return false;

        auto* obj = new juce::DynamicObject();
        obj->setProperty ("name", name.trim());
        obj->setProperty ("version", JucePlugin_VersionString);
        auto* vals = new juce::DynamicObject();
        for (const auto& id : creativeParams())
            if (auto* param = apvts.getParameter (id))
                vals->setProperty (id, param->convertFrom0to1 (param->getValue()));
        obj->setProperty ("values", juce::var (vals));

        userDir().createDirectory();
        const auto file = fileFor (name.trim());
        if (! file.replaceWithText (juce::JSON::toString (juce::var (obj), false)))
            return false;

        rebuildList();
        for (int i = 0; i < (int) presets.size(); ++i)
            if (! presets[(size_t) i].factory && presets[(size_t) i].name == name.trim())
            {
                currentIndex = i;
                break;
            }
        loadSnapshotFromCurrentParams();
        return true;
    }

    // --- modified tracking -------------------------------------------------
    bool isModified() const
    {
        for (const auto& [id, snapValue] : loadedSnapshot)
            if (auto* param = apvts.getParameter (id))
                if (std::abs (param->convertFrom0to1 (param->getValue()) - snapValue) > 1.0e-3f)
                    return true;
        return false;
    }

    // --- session restore ---------------------------------------------------
    // Name-based: the host state stores the preset name; parameters restore
    // through the APVTS as usual, so a modified preset stays modified.
    void restoreByName (const juce::String& name)
    {
        for (int i = 0; i < (int) presets.size(); ++i)
            if (presets[(size_t) i].name == name)
            {
                currentIndex = i;
                loadSnapshotFrom (presets[(size_t) i]);
                return;
            }
        currentIndex = 0;
        loadSnapshotFrom (presets[0]);
    }

private:
    void apply (const Preset& p)
    {
        // Any pending knob drift becomes its own step first, so the preset
        // load is a clean single entry rather than swallowing prior edits.
        captureUndoPoint();

        const auto before = currentValues();
        for (const auto& [id, value] : p.values)
            if (auto* param = apvts.getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (value));

        if (undo != nullptr)
        {
            undo->beginNewTransaction ("Load preset: " + p.name);
            undo->perform (new ParameterChangeAction (apvts, before, currentValues()));
        }

        loadSnapshotFrom (p);
        undoSnapshot = currentValues();
    }

    std::map<juce::String, float> currentValues() const
    {
        std::map<juce::String, float> values;
        for (const auto& id : creativeParams())
            if (auto* param = apvts.getParameter (id))
                values[id] = param->convertFrom0to1 (param->getValue());
        return values;
    }

    void loadSnapshotFrom (const Preset& p)      { loadedSnapshot = p.values; }

    void loadSnapshotFromCurrentParams()
    {
        loadedSnapshot.clear();
        for (const auto& id : creativeParams())
            if (auto* param = apvts.getParameter (id))
                loadedSnapshot[id] = param->convertFrom0to1 (param->getValue());
    }

    juce::AudioProcessorValueTreeState& apvts;
    juce::UndoManager* undo;
    std::vector<Preset> presets;
    std::map<juce::String, float> loadedSnapshot;   // for the modified dot
    std::map<juce::String, float> undoSnapshot;     // for undo capture
    int currentIndex = 0;
};

} // namespace keyglo
