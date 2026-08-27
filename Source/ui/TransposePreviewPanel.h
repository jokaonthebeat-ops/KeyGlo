/*
    TransposePreviewPanel - the -4..+4 semitone audition row, result card and
    A/B compare. The selection drives the transposeSemitones choice parameter;
    the preview pitch shifter itself is milestone-4 DSP, so the audition is
    parameter-complete but audibly a pass-through for now.
*/

#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace keyglo
{

class TransposePreviewPanel : public juce::Component
{
public:
    explicit TransposePreviewPanel (KeyGloProcessor& p) : processor (p)
    {
        setName ("TransposePreview");

        for (int i = 0; i < 9; ++i)
        {
            const bool original = i == 4;
            auto* b = transposeButtons.add (new SkinButton (
                "Transpose " + labelFor (i),
                original ? "transpose_original_92x48" : "transpose_64x48"));
            b->setLabel (labelFor (i));
            b->setFontHeight (original ? 11.0f : 15.0f);
            b->onClick = [this, i]
            {
                if (auto* param = processor.getAPVTS().getParameter (pid::transposeSemitones))
                    param->setValueNotifyingHost ((float) i / 8.0f);
            };
            addAndMakeVisible (b);
        }

        compareA.setLabel ("A");
        compareA.setFontHeight (26.0f);
        compareB.setLabel ("B");
        compareB.setFontHeight (26.0f);
        compareB.setLabelColour (tokens::cyan);
        addAndMakeVisible (compareA);
        addAndMakeVisible (compareB);

        compareA.onClick = [this] { setRecommended (false); };
        compareB.onClick = [this] { setRecommended (true); };

        selectionParam = processor.getAPVTS().getRawParameterValue (pid::transposeSemitones);
        abParam        = processor.getAPVTS().getRawParameterValue (pid::previewRecommended);
        refreshStates();
    }

    void update()
    {
        const int sel = juce::roundToInt (selectionParam->load());
        const bool b  = abParam->load() > 0.5f;
        if (sel != shownSelection || b != shownB)
            refreshStates();
    }

    void resized() override
    {
        const auto off = layout::transposePanel.getPosition();
        const auto row = layout::transposeButtons - off;

        // 8 x 64 px + ORIGINAL at 92 px, gaps even across the row.
        const int total = 8 * 64 + 92;
        const int gap = (row.getWidth() - total) / 8;
        int x = row.getX();
        for (int i = 0; i < 9; ++i)
        {
            const int w = i == 4 ? 92 : 64;
            transposeButtons[i]->setBounds (x, row.getY(), w, row.getHeight());
            x += w + gap;
        }

        compareA.setBounds (layout::compareA - off);
        compareB.setBounds (layout::compareB - off);
    }

    void paint (juce::Graphics& g) override
    {
        const auto off = layout::transposePanel.getPosition();
        auto local = [off] (juce::Rectangle<int> r) { return r - off; };
        auto snap = processor.getDisplayModel().get();

        g.setColour (tokens::text);
        g.setFont (Fonts::panelTitle());
        g.drawText ("TRANSPOSE PREVIEW", 0, 22, getWidth(), 24, juce::Justification::centred);

        // --- connector stem from the selected button to the result card ----
        const int sel = shownSelection;
        if (auto* b = transposeButtons[sel])
        {
            const auto result = local (layout::transposeResult);
            const int bx = b->getBounds().getCentreX();
            g.setColour (tokens::cyan.withAlpha (0.75f));
            g.fillRect (bx - 1, b->getBottom(), 2,
                        juce::jmax (0, result.getY() - b->getBottom()));
        }

        // --- result card ---------------------------------------------------
        const auto card = local (layout::transposeResult);
        auto art = Assets::transposeResult (true);
        if (art.isValid())
        {
            g.setColour (juce::Colours::white);
            g.drawImage (art, card.toFloat(), juce::RectanglePlacement::stretchToFit);
        }
        else
        {
            g.setColour (tokens::panel2);
            g.fillRoundedRectangle (card.toFloat(), 10.0f);
            g.setColour (tokens::cyan.withAlpha (0.4f));
            g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 10.0f, 1.2f);
        }

        auto content = card.reduced (22, 14);
        auto topRow = content.removeFromTop (content.getHeight() / 2);

        // NEW KEY is real music math once a key is detected: the selection
        // applied to the detected root. ESTIMATED FIT needs milestone 3's
        // artist profile - honest "--" until then.
        const bool demoMode = demoDisplayMode();
        const bool goodKey = demoMode || (snap->hasBeatResult && ! snap->noReliableKey);

        juce::String newKeyText = "--";
        if (demoMode)
            newKeyText = snap->newKey + " " + snap->newScale;
        else if (goodKey)
        {
            static const char* names[12] = { "C", "C#", "D", "D#", "E", "F",
                                             "F#", "G", "G#", "A", "A#", "B" };
            const int newRoot = ((snap->rootNote + (shownSelection - 4)) % 12 + 12) % 12;
            newKeyText = juce::String (names[newRoot]) + " " + snap->scale;
        }

        g.setColour (tokens::muted);
        g.setFont (Fonts::make (14.0f, false, true).withExtraKerningFactor (0.05f));
        g.drawText ("NEW KEY:", topRow.removeFromLeft (98), juce::Justification::centredLeft);
        g.setColour (goodKey ? tokens::cyan : tokens::muted2);
        g.setFont (Fonts::make (24.0f, false, true).withExtraKerningFactor (0.04f));
        g.drawText (newKeyText, topRow.translated (8, 0), juce::Justification::centredLeft);

        g.setColour (tokens::stroke.withAlpha (0.8f));
        g.fillRect (card.getX() + 16, card.getCentreY(), card.getWidth() - 32, 1);

        g.setColour (tokens::muted);
        g.setFont (Fonts::make (14.0f, false, true).withExtraKerningFactor (0.05f));
        g.drawText ("ESTIMATED FIT:", content.removeFromLeft (134),
                    juce::Justification::centredLeft);
        g.setColour (demoMode ? tokens::cyan : tokens::muted2);
        g.setFont (Fonts::make (27.0f, false, true));
        g.drawText (demoMode ? juce::String (juce::roundToInt (snap->estimatedFit * 100.0f)) : "--",
                    content.translated (8, 0), juce::Justification::centredLeft);

        // --- captions under the A/B letters --------------------------------
        const auto a = local (layout::compareA);
        const auto b = local (layout::compareB);
        g.setFont (Fonts::make (9.5f, false, true).withExtraKerningFactor (0.05f));
        g.setColour (shownB ? tokens::muted : tokens::text);
        g.drawText ("ORIGINAL", a.withTrimmedTop (a.getHeight() - 22).translated (0, -6),
                    juce::Justification::centred);
        g.setColour (shownB ? tokens::cyan : tokens::muted);
        g.drawText ("RECOMMENDED", b.withTrimmedTop (b.getHeight() - 22).translated (0, -6),
                    juce::Justification::centred);
    }

private:
    static juce::String labelFor (int index)
    {
        if (index == 4) return "ORIGINAL";
        const int st = index - 4;
        return (st > 0 ? "+" : "") + juce::String (st);
    }

    void setRecommended (bool b)
    {
        if (auto* param = processor.getAPVTS().getParameter (pid::previewRecommended))
            param->setValueNotifyingHost (b ? 1.0f : 0.0f);
    }

    void refreshStates()
    {
        shownSelection = juce::jlimit (0, 8, juce::roundToInt (selectionParam->load()));
        shownB = abParam->load() > 0.5f;

        for (int i = 0; i < 9; ++i)
        {
            const bool selected = i == shownSelection;
            const bool original = i == 4;
            transposeButtons[i]->setSkinBase (
                selected ? (original ? "transpose_original_92x48" : "transpose_selected_64x48")
                         : (original ? "transpose_original_92x48" : "transpose_64x48"));
            transposeButtons[i]->setToggleState (selected, juce::dontSendNotification);
            transposeButtons[i]->setLabelColour (selected ? tokens::cyan : tokens::text);
        }

        compareA.setToggleState (! shownB, juce::dontSendNotification);
        compareB.setToggleState (shownB, juce::dontSendNotification);
        compareB.setSkinBase (shownB ? "compare_selected_84x91" : "compare_72x91");

        repaint();
    }

    KeyGloProcessor& processor;
    std::atomic<float>* selectionParam = nullptr;
    std::atomic<float>* abParam = nullptr;
    int shownSelection = 2;
    bool shownB = true;

    juce::OwnedArray<SkinButton> transposeButtons;
    SkinButton compareA { "Compare Original", "compare_72x91" };
    SkinButton compareB { "Compare Recommended", "compare_selected_84x91" };
};

} // namespace keyglo
