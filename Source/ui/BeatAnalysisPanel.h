/*
    BeatAnalysisPanel - key/scale/BPM/tuning rows, live spectrum, drag & drop
    zone and the scale note map. Values come from the display snapshot; the
    spectrum animates from the demo feed until the real engine lands.
*/

#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace keyglo
{

class BeatAnalysisPanel : public juce::Component,
                          public juce::FileDragAndDropTarget
{
public:
    BeatAnalysisPanel (KeyGloProcessor& p, DemoFeed& feed)
        : processor (p), demo (feed)
    {
        setName ("BeatAnalysis");
        addAndMakeVisible (refreshButton);
        refreshButton.setIconPadding (5.0f);
    }

    void update()
    {
        repaint();   // spectrum + confidence move every frame
    }

    void resized() override
    {
        const auto off = layout::beatPanel.getPosition();
        refreshButton.setBounds (juce::Rectangle<int> (layout::beatPanel.getRight() - 36,
                                                       layout::beatPanel.getY() + 14, 24, 24) - off);
    }

    void paint (juce::Graphics& g) override
    {
        const auto off = layout::beatPanel.getPosition();
        auto local = [off] (juce::Rectangle<int> r) { return r - off; };
        auto snap = processor.getDisplayModel().get();

        drawPanelTitle (g, { 15, 15, 250, 26 }, "wave", "Beat Analysis", tokens::cyan);

        // --- analysis rows -------------------------------------------------
        const auto rows = local (layout::beatRows);
        const float rowH = (float) rows.getHeight() / 6.0f;

        struct Row { const char* icon; const char* label; juce::String value; bool cyanValue; };
        const Row rowData[6] = {
            { "key",        "KEY",          snap->key,                                   false },
            { "wave",       "SCALE",        snap->scale,                                 false },
            { "tempo",      "BPM",          juce::String (juce::roundToInt (snap->bpm)), false },
            { "tuning",     "TUNING",       juce::String (juce::roundToInt (snap->tuningCents)) + " cents", false },
            { "confidence", "CONFIDENCE",   juce::String (juce::roundToInt (snap->keyConfidence * 100.0f)) + "%", true },
            { "profile",    "ALTERNATIVES", snap->altKey + " " + snap->altScale,         false },
        };

        for (int i = 0; i < 6; ++i)
        {
            const juce::Rectangle<int> row (rows.getX(), rows.getY() + (int) (rowH * (float) i),
                                            rows.getWidth(), (int) rowH);

            auto rowArt = Assets::analysisRow (i == hoveredRow ? 1 : 0);
            if (rowArt.isValid())
            {
                g.setColour (juce::Colours::white);
                g.drawImage (rowArt, row.toFloat().reduced (0.0f, 1.0f),
                             juce::RectanglePlacement::stretchToFit);
            }

            auto content = row.reduced (10, 0);
            if (auto* ic = Assets::icon (rowData[i].icon, tokens::muted))
                ic->drawWithin (g, content.removeFromLeft (18).withSizeKeepingCentre (15, 15).toFloat(),
                                juce::RectanglePlacement::centred, 1.0f);
            content.removeFromLeft (9);

            g.setColour (tokens::muted);
            g.setFont (Fonts::rowLabel());
            g.drawText (rowData[i].label, content, juce::Justification::centredLeft);

            g.setColour (rowData[i].cyanValue ? tokens::cyan : tokens::text);
            g.setFont (Fonts::rowValue());
            g.drawText (rowData[i].value, content.withTrimmedLeft (196),
                        juce::Justification::centredLeft);

            if (i == 4)   // confidence bar beside the percentage
                drawConfidence (g, { content.getX() + 250, row.getCentreY() - 3,
                                     content.getRight() - content.getX() - 254, 7 },
                                snap->keyConfidence);

            if (i == 5)   // alternatives chevron
            {
                juce::Path p;
                const float cx = (float) content.getRight() - 8.0f;
                const float cy = (float) row.getCentreY();
                p.startNewSubPath (cx - 3.0f, cy - 5.0f);
                p.lineTo (cx + 2.0f, cy);
                p.lineTo (cx - 3.0f, cy + 5.0f);
                g.setColour (tokens::muted);
                g.strokePath (p, juce::PathStrokeType (1.8f));
            }

            if (i < 5)
            {
                g.setColour (tokens::stroke.withAlpha (0.5f));
                g.fillRect (row.getX() + 4, row.getBottom() - 1, row.getWidth() - 8, 1);
            }
        }

        drawSpectrum (g, local (layout::beatSpectrum));
        drawDropZone (g, local (layout::beatDropZone));
        drawNoteMap  (g, local (layout::beatNoteMap), *snap);
    }

    // --- drag & drop (accepts audio; analysis lands in milestone 2) --------
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (const auto& f : files)
            if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif")
                 || f.endsWithIgnoreCase (".aiff") || f.endsWithIgnoreCase (".flac")
                 || f.endsWithIgnoreCase (".mp3"))
                return true;
        return false;
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { dropState = 2; repaint(); }
    void fileDragExit (const juce::StringArray&) override            { dropState = 0; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dropState = 0;
        droppedName = juce::File (files[0]).getFileName();
        repaint();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto rows = layout::beatRows - layout::beatPanel.getPosition();
        int hit = -1;
        if (rows.contains (e.getPosition()))
            hit = juce::jlimit (0, 5, (e.y - rows.getY()) / (rows.getHeight() / 6));
        if (hit != hoveredRow) { hoveredRow = hit; repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { hoveredRow = -1; repaint(); }

private:
    void drawConfidence (juce::Graphics& g, juce::Rectangle<int> r, float amount)
    {
        auto track = Assets::confidenceTrack();
        auto fill  = Assets::confidenceFill();
        g.setColour (juce::Colours::white);
        if (track.isValid())
            g.drawImage (track, r.toFloat(), juce::RectanglePlacement::stretchToFit);
        const int w = juce::roundToInt ((float) r.getWidth() * amount);
        if (fill.isValid() && w > 2)
        {
            g.saveState();
            g.reduceClipRegion (r.withWidth (w));
            g.drawImage (fill, r.toFloat(), juce::RectanglePlacement::stretchToFit);
            g.restoreState();
        }
        else if (w > 2)
        {
            g.setColour (tokens::cyan);
            g.fillRoundedRectangle (r.withWidth (w).toFloat(), 3.0f);
        }
    }

    void drawSpectrum (juce::Graphics& g, juce::Rectangle<int> r)
    {
        auto grid = Assets::spectrumGrid();
        if (grid.isValid())
        {
            g.setColour (juce::Colours::white);
            g.drawImage (grid, r.toFloat(), juce::RectanglePlacement::stretchToFit);
        }

        const auto plot = r.reduced (4, 3).withTrimmedBottom (12);
        const int n = DemoFeed::spectrumBands;
        const float bw = (float) plot.getWidth() / (float) n;

        for (int b = 0; b < n; ++b)
        {
            const float v = demo.spectrum[(size_t) b];
            const float peak = demo.spectrumPeak[(size_t) b];
            const float x = (float) plot.getX() + bw * (float) b;

            // Cyan low/mid moving to violet toward the top of the range
            // (ANIMATION_AND_VISUALIZER_SPEC.md).
            const float blend = (float) b / (float) n;
            const auto colour = tokens::cyan.interpolatedWith (tokens::violet,
                                                               juce::jlimit (0.0f, 1.0f, (blend - 0.45f) * 2.2f));

            const float h = v * (float) plot.getHeight();
            g.setColour (colour.withAlpha (0.85f));
            g.fillRect (x, (float) plot.getBottom() - h, juce::jmax (1.0f, bw - 1.2f), h);

            const float ph = peak * (float) plot.getHeight();
            g.setColour (colour.withAlpha (0.5f));
            g.fillRect (x, (float) plot.getBottom() - ph - 1.5f, juce::jmax (1.0f, bw - 1.2f), 1.5f);
        }

        // Frequency captions along the bottom, inside the well.
        static const char* freqs[] = { "20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k" };
        g.setColour (tokens::muted2);
        g.setFont (Fonts::make (9.5f, true));
        const auto axis = r.reduced (6, 0).removeFromBottom (13);
        for (int i = 0; i < 10; ++i)
            g.drawText (freqs[i],
                        axis.getX() + (int) ((float) axis.getWidth() * (float) i / 9.5f), axis.getY(),
                        26, 11, juce::Justification::left);
    }

    void drawDropZone (juce::Graphics& g, juce::Rectangle<int> r)
    {
        auto art = Assets::dropZone (dropState);
        g.setColour (juce::Colours::white);
        if (art.isValid())
            g.drawImage (art, r.toFloat(), juce::RectanglePlacement::stretchToFit);
        else
        {
            g.setColour (tokens::panel2);
            g.fillRoundedRectangle (r.toFloat(), 8.0f);
            g.setColour (tokens::stroke);
            g.drawRoundedRectangle (r.toFloat().reduced (1.0f), 8.0f, 1.0f);
        }

        auto content = r.reduced (14, 8);
        if (auto* ic = Assets::icon ("upload", tokens::text))
            ic->drawWithin (g, content.removeFromLeft (26).withSizeKeepingCentre (20, 20).toFloat(),
                            juce::RectanglePlacement::centred, 1.0f);
        content.removeFromLeft (10);

        const bool hasFile = droppedName.isNotEmpty();
        g.setColour (tokens::text);
        g.setFont (Fonts::make (14.0f, false, true).withExtraKerningFactor (0.03f));
        g.drawText (hasFile ? droppedName : "DRAG & DROP BEAT HERE",
                    content.withTrimmedBottom (16), juce::Justification::bottomLeft);
        g.setColour (tokens::muted2);
        g.setFont (Fonts::make (10.5f, true).withExtraKerningFactor (0.1f));
        g.drawText (hasFile ? "QUEUED FOR ANALYSIS" : "WAV / MP3 / FLAC",
                    content.withTrimmedTop (content.getHeight() - 14),
                    juce::Justification::topLeft);
    }

    void drawNoteMap (juce::Graphics& g, juce::Rectangle<int> r, const AnalysisSnapshot& snap)
    {
        auto header = r.removeFromTop (17);
        g.setColour (tokens::muted);
        g.setFont (Fonts::fieldLabel());
        g.drawText ("NOTE MAP", header, juce::Justification::centredLeft);
        g.setColour (tokens::text);
        g.drawText (snap.key.toUpperCase() + " " + snap.scale.toUpperCase(),
                    header, juce::Justification::centredRight);

        r.removeFromTop (3);
        auto piano = Assets::noteMapPiano();
        g.setColour (juce::Colours::white);
        if (piano.isValid())
            g.drawImage (piano, r.toFloat(), juce::RectanglePlacement::stretchToFit);
        else
        {
            g.setColour (tokens::panel3);
            g.fillRect (r);
        }

        // Live scale highlight over two octaves, root brightest.
        const int semis = 24;
        const float w = (float) r.getWidth() / (float) semis;
        for (int s = 0; s < semis; ++s)
        {
            const int pc = s % 12;
            if (! snap.scaleNotes[(size_t) pc])
                continue;
            const bool root = pc == snap.rootNote;
            g.setColour (tokens::cyan.withAlpha (root ? 0.5f : 0.28f));
            g.fillRect ((float) r.getX() + w * (float) s, (float) r.getY(),
                        juce::jmax (1.0f, w - 1.0f), (float) r.getHeight());
        }
    }

    KeyGloProcessor& processor;
    DemoFeed& demo;
    IconButton refreshButton { "Refresh Analysis", "redo" };
    int hoveredRow = -1;
    int dropState = 0;
    juce::String droppedName;
};

} // namespace keyglo
