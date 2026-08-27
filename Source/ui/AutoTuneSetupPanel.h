/*
    AutoTuneSetupPanel - recommended key/scale/mode, allowed-note chips, the
    highlighted keyboard and COPY SCALE. Copy writes a concise text setup
    description to the clipboard (the MIDI scale export joins a later
    milestone). No claim of direct third-party tuner control.
*/

#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace keyglo
{

class AutoTuneSetupPanel : public juce::Component
{
public:
    explicit AutoTuneSetupPanel (KeyGloProcessor& p) : processor (p)
    {
        setName ("AutoTuneSetup");

        for (int i = 0; i < 7; ++i)
        {
            auto* chip = chips.add (new SkinButton ("Note " + juce::String (i),
                                                    "note_chip_36x28"));
            chip->setFontHeight (12.0f);
            chip->setLabelColour (tokens::cyan);
            addAndMakeVisible (chip);
        }

        copyButton.setLabel ("COPY SCALE");
        copyButton.setFontHeight (13.0f);
        copyButton.setIconTint (tokens::text);
        copyButton.setTooltip ("Click: copy the scale setup as text. "
                               "Drag into your DAW: a one-octave MIDI scale file.");
        copyButton.onClick = [this] { copyScaleToClipboard(); };
        copyButton.onDragStart = [this] { dragScaleMidi(); };
        addAndMakeVisible (copyButton);

        refreshNoteChips();
    }

    void update()
    {
        auto snap = processor.getDisplayModel().get();
        if (snap != lastSnap)
        {
            lastSnap = snap;
            refreshNoteChips();
            repaint();
        }
    }

    void resized() override
    {
        const auto off = layout::autotunePanel.getPosition();
        const auto chipRow = layout::autotuneNoteChips - off;

        const int count = chips.size();
        const int gap = count > 1 ? (chipRow.getWidth() - count * 36) / (count - 1) : 0;
        for (int i = 0; i < count; ++i)
            chips[i]->setBounds (chipRow.getX() + i * (36 + gap), chipRow.getY(), 36, 28);

        copyButton.setBounds (layout::copyScale - off);
    }

    void paint (juce::Graphics& g) override
    {
        const auto off = layout::autotunePanel.getPosition();
        auto local = [off] (juce::Rectangle<int> r) { return r - off; };
        auto snap = processor.getDisplayModel().get();

        drawPanelTitle (g, { 15, 15, 260, 26 }, "tuning", "Auto-Tune Setup", tokens::cyan);

        // --- key / scale / mode -------------------------------------------
        // Demo shots show the contract's post-transpose key; production shows
        // the DETECTED key once the engine reports (the transpose-adjusted
        // recommendation arrives with milestone 3's fit scoring).
        const auto shown = shownKeyScale();
        const juce::Rectangle<int> tableArea (13, 60, 287, 78);
        const char* labels[] = { "KEY", "SCALE", "MODE" };
        const juce::String values[] = { shown.first, shown.second,
                                        shown.first == "--" ? juce::String ("--")
                                                            : juce::String ("Modern") };

        const juce::Rectangle<int> valueBox (tableArea.getX() + 112, tableArea.getY() - 2,
                                             92, tableArea.getHeight() + 4);
        g.setColour (tokens::panel3.withAlpha (0.8f));
        g.fillRoundedRectangle (valueBox.toFloat(), 6.0f);
        g.setColour (tokens::stroke);
        g.drawRoundedRectangle (valueBox.toFloat().reduced (0.5f), 6.0f, 1.0f);

        for (int i = 0; i < 3; ++i)
        {
            const int rowY = tableArea.getY() + i * (tableArea.getHeight() / 3);
            const int rowH = tableArea.getHeight() / 3;

            g.setColour (tokens::muted);
            g.setFont (Fonts::rowLabel());
            g.drawText (labels[i], tableArea.getX() + 4, rowY, 90, rowH,
                        juce::Justification::centredLeft);

            g.setColour (tokens::cyan);
            g.setFont (Fonts::make (15.0f, true));
            g.drawText (values[i], valueBox.getX() + 12, rowY, valueBox.getWidth() - 16,
                        rowH, juce::Justification::centredLeft);
        }

        // --- allowed notes -------------------------------------------------
        g.setColour (tokens::muted);
        g.setFont (Fonts::fieldLabel());
        g.drawText ("ALLOWED NOTES",
                    local (layout::autotuneNoteChips).translated (0, -20).withHeight (16),
                    juce::Justification::centredLeft);

        drawKeyboard (g, local (layout::autotuneKeyboard), *snap);
    }

private:
    // (key, scale) currently presented by this panel, "--" when nothing real.
    std::pair<juce::String, juce::String> shownKeyScale() const
    {
        auto snap = processor.getDisplayModel().get();
        if (demoDisplayMode())
            return { snap->newKey, snap->newScale };
        if (snap->hasBeatResult && ! snap->noReliableKey)
            return { snap->key, snap->scale };
        return { "--", "--" };
    }

    void refreshNoteChips()
    {
        auto names = allowedNotes();
        for (int i = 0; i < chips.size(); ++i)
        {
            chips[i]->setLabel (i < names.size() ? names[i] : juce::String());
            chips[i]->setVisible (i < names.size());
        }
        copyButton.setEnabled (! names.isEmpty());
    }

    // Natural major/minor scale of the presented key (contract data: E minor
    // -> E F# G A B C D). Empty when there is no real key yet.
    juce::StringArray allowedNotes() const
    {
        const auto shown = shownKeyScale();
        if (shown.first == "--")
            return {};

        int root = 0;
        for (int i = 0; i < 12; ++i)
            if (shown.first == notes::names[i]) { root = i; break; }

        const bool minor = shown.second.equalsIgnoreCase ("minor");
        const int minorSteps[] = { 0, 2, 3, 5, 7, 8, 10 };
        const int majorSteps[] = { 0, 2, 4, 5, 7, 9, 11 };

        juce::StringArray result;
        for (int i = 0; i < 7; ++i)
            result.add (notes::names[(root + (minor ? minorSteps[i] : majorSteps[i])) % 12]);
        return result;
    }

    std::array<bool, 12> allowedPitchClasses() const
    {
        std::array<bool, 12> pcs {};
        for (const auto& n : allowedNotes())
            for (int i = 0; i < 12; ++i)
                if (n == notes::names[i])
                    pcs[(size_t) i] = true;
        return pcs;
    }

    void drawKeyboard (juce::Graphics& g, juce::Rectangle<int> r,
                       const AnalysisSnapshot&)
    {
        const auto lit = allowedPitchClasses();

        // Two octaves drawn live: 14 white keys, blacks overlaid.
        const int whitePcs[7] = { 0, 2, 4, 5, 7, 9, 11 };
        const float whiteW = (float) r.getWidth() / 14.0f;

        for (int i = 0; i < 14; ++i)
        {
            const int pc = whitePcs[i % 7];
            const juce::Rectangle<float> key ((float) r.getX() + whiteW * (float) i,
                                              (float) r.getY(),
                                              whiteW - 1.0f, (float) r.getHeight());
            g.setColour (lit[(size_t) pc] ? tokens::cyan.interpolatedWith (tokens::white, 0.35f)
                                          : tokens::text.darker (0.05f));
            g.fillRect (key);
            if (lit[(size_t) pc])
            {
                g.setColour (tokens::cyan.withAlpha (0.55f));
                g.fillRect (key.withTrimmedTop (key.getHeight() * 0.55f));
            }
        }

        // Black keys sit between white neighbours (skip E-F and B-C).
        const int blackAfterWhite[5] = { 0, 1, 3, 4, 5 };   // C# D# F# G# A#
        const int blackPcs[5]        = { 1, 3, 6, 8, 10 };
        for (int oct = 0; oct < 2; ++oct)
            for (int b = 0; b < 5; ++b)
            {
                const float x = (float) r.getX()
                              + whiteW * (float) (oct * 7 + blackAfterWhite[b] + 1)
                              - whiteW * 0.32f;
                const juce::Rectangle<float> key (x, (float) r.getY(),
                                                  whiteW * 0.62f, (float) r.getHeight() * 0.62f);
                const bool on = lit[(size_t) blackPcs[b]];
                g.setColour (on ? tokens::cyan2 : tokens::bg0);
                g.fillRect (key);
                if (on)
                {
                    g.setColour (tokens::cyan.withAlpha (0.8f));
                    g.fillRect (key.withTrimmedTop (key.getHeight() * 0.62f));
                }
            }

        g.setColour (tokens::stroke);
        g.drawRect (r, 1);
    }

public:
    // A one-octave ascending scale as a standard MIDI file (the spec's
    // "may also export a one-octave MIDI scale file"), written to the temp
    // folder and dragged into the host. Public + static: the test suite
    // verifies the generated file parses, ascends and stays in scale.
    static juce::File writeScaleMidiFile (const juce::String& keyName,
                                          const juce::String& scaleName,
                                          const juce::StringArray& noteNames)
    {
        if (noteNames.isEmpty())
            return {};

        int rootPc = 0;
        for (int i = 0; i < 12; ++i)
            if (noteNames[0] == notes::names[i]) { rootPc = i; break; }

        // Ascending pitch classes from the root, one octave, top root added.
        std::vector<int> midiNotes;
        int previous = 60 + rootPc;               // root in the C4 octave
        midiNotes.push_back (previous);
        for (int i = 1; i < noteNames.size(); ++i)
        {
            int pc = 0;
            for (int j = 0; j < 12; ++j)
                if (noteNames[i] == notes::names[j]) { pc = j; break; }
            int note = (previous / 12) * 12 + pc;
            while (note <= previous)
                note += 12;
            midiNotes.push_back (note);
            previous = note;
        }
        midiNotes.push_back (midiNotes.front() + 12);

        const int tpq = 960;
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::tempoMetaEvent (500000), 0.0);   // 120 BPM
        double tick = 0.0;
        for (int note : midiNotes)
        {
            track.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), tick);
            track.addEvent (juce::MidiMessage::noteOff (1, note), tick + tpq - 20);
            tick += tpq;
        }
        track.updateMatchedPairs();

        juce::MidiFile midiFile;
        midiFile.setTicksPerQuarterNote (tpq);
        midiFile.addTrack (track);

        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("KeyGlo " + keyName + " " + scaleName + " scale.mid");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        if (! stream.openedOk() || ! midiFile.writeTo (stream))
            return {};
        stream.flush();
        return file;
    }

private:
    void dragScaleMidi()
    {
        const auto shown = shownKeyScale();
        if (shown.first == "--")
            return;

        auto file = writeScaleMidiFile (shown.first, shown.second, allowedNotes());
        if (file.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles (
                { file.getFullPathName() }, false, &copyButton);
    }

    void copyScaleToClipboard()
    {
        const auto shown = shownKeyScale();
        if (shown.first == "--")
            return;

        auto snap = processor.getDisplayModel().get();
        juce::String text = "KeyGlo scale setup: " + shown.first + " " + shown.second
                          + " | Allowed notes: " + allowedNotes().joinIntoString (" ");
        if (snap->hasBeatResult && ! snap->noReliableKey && snap->bpm > 1.0f)
            text += " | Beat: " + snap->key + " " + snap->scale
                  + " @ " + juce::String (juce::roundToInt (snap->bpm)) + " BPM";
        juce::SystemClipboard::copyTextToClipboard (text);
    }

    KeyGloProcessor& processor;
    std::shared_ptr<const AnalysisSnapshot> lastSnap;

    juce::OwnedArray<SkinButton> chips;
    SkinButton copyButton { "Copy Scale", "copy_scale_282x31", {}, "copy" };
};

} // namespace keyglo
