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
        refreshButton.setTooltip ("Re-analyse the last 12 seconds of session audio");
        refreshButton.onClick = [this] { processor.analyseCaptureNow(); };
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

        // Honest empty state: a fresh instance has no readings to show.
        const bool hasResult = snap->hasBeatResult || demoDisplayMode();
        const bool goodKey = hasResult && ! snap->noReliableKey;

        const juce::String keyValue = ! hasResult ? "--"
                                    : snap->noReliableKey ? "NO RELIABLE KEY" : snap->key;
        const juce::String bpmValue = hasResult && snap->bpm > 1.0f
                                        ? juce::String (juce::roundToInt (snap->bpm))
                                            + (snap->bpmSource == "HOST" ? " (host)" : "")
                                        : "--";

        struct Row { const char* icon; const char* label; juce::String value; bool cyanValue; };
        const Row rowData[6] = {
            { "key",        "KEY",          keyValue,                                    false },
            { "wave",       "SCALE",        goodKey ? snap->scale : "--",                false },
            { "tempo",      "BPM",          bpmValue,                                    false },
            { "tuning",     "TUNING",       goodKey ? juce::String (juce::roundToInt (snap->tuningCents)) + " cents" : "--", false },
            { "confidence", "CONFIDENCE",   goodKey ? juce::String (juce::roundToInt (snap->keyConfidence * 100.0f)) + "%" : "--", goodKey },
            { "alternatives", "ALTERNATIVES", goodKey ? snap->altKey + " " + snap->altScale : "--", false },
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

            // Long statuses ("NO RELIABLE KEY") start earlier and smaller so
            // they never truncate in the value column.
            const bool longValue = rowData[i].value.length() > 12;
            g.setColour (rowData[i].cyanValue ? tokens::cyan : tokens::text);
            g.setFont (longValue ? Fonts::make (12.5f, true) : Fonts::rowValue());
            g.drawText (rowData[i].value, content.withTrimmedLeft (longValue ? 118 : 196),
                        juce::Justification::centredLeft);

            if (i == 4)   // confidence bar beside the percentage
                drawConfidence (g, { content.getX() + 250, row.getCentreY() - 3,
                                     content.getRight() - content.getX() - 254, 7 },
                                goodKey ? snap->keyConfidence : 0.0f);

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
        const juce::File file (files[0]);
        droppedName = file.getFileName();
        processor.analyseFileAsync (file);
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

        // Demo shots animate from the DemoFeed; production shows the real
        // input spectrum from the analysis worker's fast lane, with local
        // smoothing + peak hold (spec: 320 ms hold, controlled falloff).
        if (! demoDisplayMode())
        {
            auto live = processor.getDisplayModel().getLive();
            for (int b = 0; b < DemoFeed::spectrumBands; ++b)
            {
                const float target = live->active ? live->spectrum[(size_t) b] : 0.0f;
                auto& shown = liveShown[(size_t) b];
                shown += (target > shown ? 0.55f : 0.18f) * (target - shown);
                auto& peak = livePeak[(size_t) b];
                peak = juce::jmax (peak - 0.012f, shown);
            }
        }

        const auto plot = r.reduced (4, 3).withTrimmedBottom (12);
        const int n = DemoFeed::spectrumBands;
        const float bw = (float) plot.getWidth() / (float) n;

        for (int b = 0; b < n; ++b)
        {
            const float v = demoDisplayMode() ? demo.spectrum[(size_t) b]
                                              : liveShown[(size_t) b];
            const float peak = demoDisplayMode() ? demo.spectrumPeak[(size_t) b]
                                                 : livePeak[(size_t) b];
            const float x = (float) plot.getX() + bw * (float) b;

            // Cyan low/mid moving to violet toward the top of the range
            // (ANIMATION_AND_VISUALIZER_SPEC.md).
            const float blend = (float) b / (float) n;
            const auto colour = tokens::cyan.interpolatedWith (tokens::violet,
                                                               juce::jlimit (0.0f, 1.0f, (blend - 0.45f) * 2.2f));

            const float h = juce::jmin (1.0f, v * 1.12f) * (float) plot.getHeight();
            g.setColour (colour.withAlpha (0.95f));
            g.fillRect (x, (float) plot.getBottom() - h, juce::jmax (1.0f, bw - 0.8f), h);

            const float ph = juce::jmin (1.0f, peak * 1.12f) * (float) plot.getHeight();
            g.setColour (colour.withAlpha (0.55f));
            g.fillRect (x, (float) plot.getBottom() - ph - 1.5f, juce::jmax (1.0f, bw - 0.8f), 1.5f);
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

        // Icon + two text lines, centred as a group (approved mockup).
        auto snap = processor.getDisplayModel().get();
        const bool hasFile = droppedName.isNotEmpty();
        const auto title = hasFile ? droppedName : juce::String ("DRAG & DROP BEAT HERE");
        const auto titleFont = Fonts::make (14.0f, false, true).withExtraKerningFactor (0.03f);
        const int titleW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (titleFont, title));
        const int iconSz = 20, gap = 10;
        const int startX = r.getCentreX() - (iconSz + gap + titleW) / 2;

        if (auto* ic = Assets::icon ("upload", tokens::text))
            ic->drawWithin (g, juce::Rectangle<float> ((float) startX,
                            (float) r.getY() + 10.0f, (float) iconSz, (float) iconSz),
                            juce::RectanglePlacement::centred, 1.0f);

        g.setColour (tokens::text);
        g.setFont (titleFont);
        g.drawText (title, startX + iconSz + gap, r.getY() + 9, titleW + 4, 20,
                    juce::Justification::centredLeft);

        juce::String status = "WAV / MP3 / FLAC";
        if (snap->analyzing)
            status = "ANALYZING...";
        else if (hasFile && snap->hasBeatResult && snap->sourceName == droppedName)
            status = snap->noReliableKey ? "ANALYZED - NO RELIABLE KEY"
                                         : "ANALYZED - " + snap->key.toUpperCase()
                                             + " " + snap->scale.toUpperCase();
        g.setColour (snap->analyzing ? tokens::cyan.withAlpha (0.85f) : tokens::muted2);
        g.setFont (Fonts::make (10.5f, true).withExtraKerningFactor (0.1f));
        g.drawText (status, r.withTrimmedTop (30), juce::Justification::centredTop);
    }

    void drawNoteMap (juce::Graphics& g, juce::Rectangle<int> r, const AnalysisSnapshot& snap)
    {
        auto header = r.removeFromTop (17);
        g.setColour (tokens::muted);
        g.setFont (Fonts::fieldLabel());
        g.drawText ("NOTE MAP", header, juce::Justification::centredLeft);
        g.setColour (tokens::text);
        const bool showKey = (snap.hasBeatResult && ! snap.noReliableKey) || demoDisplayMode();
        g.drawText (showKey ? snap.key.toUpperCase() + " " + snap.scale.toUpperCase() : "--",
                    header, juce::Justification::centredRight);

        r.removeFromTop (3);

        // Demo shots use the pack art (its highlights are baked to the demo
        // scale). Production draws the piano live so the highlights follow
        // the actually detected scale - the art cannot change key.
        if (demoDisplayMode())
        {
            auto piano = Assets::noteMapPiano();
            g.setColour (juce::Colours::white);
            if (piano.isValid())
            {
                g.drawImage (piano, r.toFloat(), juce::RectanglePlacement::stretchToFit);
                return;
            }
        }

        drawLivePiano (g, r, snap);
    }

    // Two octaves, styled after the pack's note-map art: silver whites, dark
    // blacks, detected-scale keys lit cyan (root brightest).
    void drawLivePiano (juce::Graphics& g, juce::Rectangle<int> r,
                        const AnalysisSnapshot& snap)
    {
        const bool lit = (snap.hasBeatResult && ! snap.noReliableKey) || demoDisplayMode();

        g.setColour (tokens::bg0);
        g.fillRect (r);

        const int whitePcs[7] = { 0, 2, 4, 5, 7, 9, 11 };
        const float whiteW = (float) r.getWidth() / 14.0f;

        for (int i = 0; i < 14; ++i)
        {
            const int pc = whitePcs[i % 7];
            const bool on = lit && snap.scaleNotes[(size_t) pc];
            const bool root = on && pc == snap.rootNote;
            const juce::Rectangle<float> key ((float) r.getX() + whiteW * (float) i,
                                              (float) r.getY(),
                                              whiteW - 1.0f, (float) r.getHeight());
            g.setColour (root ? tokens::cyan
                        : on  ? tokens::cyan.interpolatedWith (tokens::white, 0.42f)
                              : tokens::text.darker (0.08f));
            g.fillRect (key);
        }

        const int blackAfterWhite[5] = { 0, 1, 3, 4, 5 };
        const int blackPcs[5]        = { 1, 3, 6, 8, 10 };
        for (int oct = 0; oct < 2; ++oct)
            for (int b = 0; b < 5; ++b)
            {
                const int pc = blackPcs[b];
                const bool on = lit && snap.scaleNotes[(size_t) pc];
                const bool root = on && pc == snap.rootNote;
                const float x = (float) r.getX()
                              + whiteW * (float) (oct * 7 + blackAfterWhite[b] + 1)
                              - whiteW * 0.32f;
                const juce::Rectangle<float> key (x, (float) r.getY(),
                                                  whiteW * 0.62f,
                                                  (float) r.getHeight() * 0.62f);
                g.setColour (root ? tokens::cyan2.brighter (0.2f)
                            : on  ? tokens::cyan2 : tokens::bg0);
                g.fillRect (key);
            }

        g.setColour (tokens::stroke);
        g.drawRect (r, 1);
    }

    KeyGloProcessor& processor;
    DemoFeed& demo;
    IconButton refreshButton { "Refresh Analysis", "redo" };
    int hoveredRow = -1;
    int dropState = 0;
    juce::String droppedName;
    std::array<float, DemoFeed::spectrumBands> liveShown {};
    std::array<float, DemoFeed::spectrumBands> livePeak {};
};

} // namespace keyglo
