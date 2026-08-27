/*
    ArtistRangePanel - live current note, vertical piano, range zones and the
    scrolling 12-second pitch trail (cyan-to-violet, gaps for unvoiced frames,
    glowing endpoint - ANIMATION_AND_VISUALIZER_SPEC.md).
*/

#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace keyglo
{

class ArtistRangePanel : public juce::Component
{
public:
    ArtistRangePanel (KeyGloProcessor& p, DemoFeed& feed)
        : processor (p), demo (feed)
    {
        setName ("ArtistRange");

        rangeTestButton.setLabel ("START RANGE TEST");
        rangeTestButton.setAccent (tokens::violet);
        rangeTestButton.setFontHeight (13.0f);
        rangeTestButton.setIconTint (tokens::violet);
        rangeTestButton.onClick = [this] { onRangeTestClick(); };
        addAndMakeVisible (rangeTestButton);

        saveProfileButton.setLabel ("SAVE PROFILE");
        saveProfileButton.setFontHeight (13.0f);
        saveProfileButton.setIconTint (tokens::text);
        saveProfileButton.onClick = [this] { onSaveProfileClick(); };
        addAndMakeVisible (saveProfileButton);
    }

    void update()
    {
        auto snap = processor.getDisplayModel().get();
        const auto phase = (RangeTestPhase) snap->rangeTestPhase;
        if (phase != shownPhase)
        {
            shownPhase = phase;
            refreshButtons();
        }
        saveProfileButton.setEnabled (snap->hasProfile);
        repaint();
    }

    void resized() override
    {
        const auto off = layout::artistPanel.getPosition();
        rangeTestButton.setBounds (layout::artistStartRange - off);
        saveProfileButton.setBounds (layout::artistSaveProfile - off);
    }

    void paint (juce::Graphics& g) override
    {
        const auto off = layout::artistPanel.getPosition();
        auto local = [off] (juce::Rectangle<int> r) { return r - off; };
        auto snap = processor.getDisplayModel().get();

        // --- title + LIVE badge -------------------------------------------
        drawPanelTitle (g, { 18, 15, 250, 26 }, "mic", "Artist Range", tokens::violet);
        {
            const juce::Rectangle<int> live (getWidth() - 86, 18, 70, 20);
            g.setColour (tokens::violet);
            g.fillEllipse ((float) live.getX(), (float) live.getCentreY() - 3.0f, 6.0f, 6.0f);
            g.setColour (tokens::text);
            g.setFont (Fonts::make (12.0f, false, true).withExtraKerningFactor (0.12f));
            g.drawText ("LIVE", live.withTrimmedLeft (14), juce::Justification::centredLeft);
        }

        drawCurrentNote (g, local (layout::artistCurrentNote), *snap);

        const auto pianoRect = local (layout::artistPiano);
        const auto graphRect = local (layout::artistRangeGraph);

        drawOctaveLabels (g, pianoRect);
        drawPiano (g, pianoRect);
        drawRangeGraph (g, graphRect, *snap);
        drawRangeTestGuide (g, graphRect, *snap);
    }

    // The guided range test's instruction band, drawn over the graph.
    void drawRangeTestGuide (juce::Graphics& g, juce::Rectangle<int> graph,
                             const AnalysisSnapshot& snap)
    {
        const auto phase = (RangeTestPhase) snap.rangeTestPhase;
        if (phase == RangeTestPhase::idle || phase == RangeTestPhase::done)
            return;

        const juce::Rectangle<int> band (graph.getX() + 12, graph.getY() + 10,
                                         graph.getWidth() - 24, 64);
        g.setColour (tokens::bg0.withAlpha (0.88f));
        g.fillRoundedRectangle (band.toFloat(), 8.0f);
        g.setColour (tokens::violet.withAlpha (0.7f));
        g.drawRoundedRectangle (band.toFloat().reduced (0.5f), 8.0f, 1.2f);

        const char* prompt = "";
        switch (phase)
        {
            case RangeTestPhase::low:      prompt = "SING YOUR LOWEST COMFORTABLE NOTE"; break;
            case RangeTestPhase::centre:   prompt = "SING IN THE MIDDLE OF YOUR VOICE"; break;
            case RangeTestPhase::high:     prompt = "SING YOUR HIGHEST COMFORTABLE NOTE"; break;
            case RangeTestPhase::falsetto: prompt = "OPTIONAL: SING IN FALSETTO"; break;
            default: break;
        }

        auto text = band.reduced (12, 8);
        g.setColour (tokens::violet.brighter (0.3f));
        g.setFont (Fonts::make (11.5f, false, true).withExtraKerningFactor (0.06f));
        g.drawText (juce::String ("STEP ") + juce::String ((int) phase) + " OF 4",
                    text.removeFromTop (14), juce::Justification::centredLeft);

        g.setColour (tokens::text);
        g.setFont (Fonts::make (13.0f, true));
        g.drawText (prompt, text.removeFromTop (18), juce::Justification::centredLeft);

        // Collection progress: a phase wants ~2 s of stable pitch.
        const float progress = juce::jlimit (0.0f, 1.0f, snap.rangeTestFrames / 90.0f);
        const auto bar = text.removeFromTop (6).withTrimmedRight (4);
        g.setColour (tokens::panel3);
        g.fillRoundedRectangle (bar.toFloat(), 3.0f);
        g.setColour (progress >= 1.0f ? tokens::green : tokens::violet);
        g.fillRoundedRectangle (bar.toFloat().withWidth (bar.getWidth() * progress), 3.0f);

        g.setColour (tokens::muted);
        g.setFont (Fonts::make (10.0f, true));
        g.drawText (progress >= 1.0f ? "GOT IT - PRESS NEXT" : "HOLD THE NOTE...",
                    text, juce::Justification::centredLeft);
    }

private:
    // Display MIDI range of the piano/graph column (C2..C6).
    static constexpr float midiLow = 36.0f, midiHigh = 84.0f;

    static float midiToY (float midi, juce::Rectangle<int> r)
    {
        return (float) r.getBottom()
             - ((midi - midiLow) / (midiHigh - midiLow)) * (float) r.getHeight();
    }

    void drawCurrentNote (juce::Graphics& g, juce::Rectangle<int> r,
                          const AnalysisSnapshot& snap)
    {
        g.setColour (tokens::panel2);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (tokens::violet.withAlpha (0.45f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);

        // Small "CURRENT NOTE" pill overlapping the card's top edge.
        {
            const juce::Rectangle<int> pill (r.getX() + 12, r.getY() - 9, 104, 18);
            g.setColour (tokens::panel3);
            g.fillRoundedRectangle (pill.toFloat(), 9.0f);
            g.setColour (tokens::violet.withAlpha (0.6f));
            g.drawRoundedRectangle (pill.toFloat().reduced (0.5f), 9.0f, 1.0f);
            g.setColour (tokens::violet.brighter (0.25f));
            g.setFont (Fonts::make (9.5f, false, true).withExtraKerningFactor (0.08f));
            g.drawText ("CURRENT NOTE", pill, juce::Justification::centred);
        }

        auto content = r.reduced (16, 6).withTrimmedTop (8);

        // Demo shots use the scripted feed; production shows the tracked
        // note, or "--" when nothing is being sung.
        auto live = processor.getDisplayModel().getLive();
        const bool voiced = demoDisplayMode() || live->voiced;
        const juce::String noteText = demoDisplayMode()
                                        ? demo.currentNoteName()
                                        : (live->voiced
                                             ? notes::midiNoteName (juce::roundToInt (live->currentMidi))
                                             : "--");
        const float centsValue = demoDisplayMode() ? demo.vocalCents : live->currentCents;

        g.setColour (voiced ? tokens::violet.brighter (0.15f) : tokens::muted2);
        g.setFont (Fonts::make (30.0f, false, true));
        g.drawText (noteText, content.removeFromLeft (78), juce::Justification::centredLeft);

        const int cents = juce::roundToInt (centsValue);
        g.setColour (voiced ? tokens::white : tokens::muted2);
        g.setFont (Fonts::make (22.0f, false, true));
        g.drawText (voiced ? (cents >= 0 ? "+" : "") + juce::String (cents) : "--",
                    content.withTrimmedBottom (16), juce::Justification::centredLeft);
        g.setColour (tokens::muted);
        g.setFont (Fonts::make (10.0f, false, true).withExtraKerningFactor (0.08f));
        g.drawText ("CENTS", content.withTrimmedTop (content.getHeight() - 15),
                    juce::Justification::topLeft);
    }

    void drawOctaveLabels (juce::Graphics& g, juce::Rectangle<int> piano)
    {
        g.setColour (tokens::muted2);
        g.setFont (Fonts::make (10.5f, true));
        for (int octave = 2; octave <= 6; ++octave)
        {
            const float y = midiToY ((float) (octave * 12 + 12), piano);   // C2=36...
            g.drawText ("C" + juce::String (octave),
                        piano.getX() - 24, (int) y - 6, 20, 12,
                        juce::Justification::centredRight);
        }
    }

    void drawPiano (juce::Graphics& g, juce::Rectangle<int> r)
    {
        auto art = Assets::verticalPiano();
        if (art.isValid())
        {
            g.setColour (juce::Colours::white);
            g.drawImage (art, r.toFloat(), juce::RectanglePlacement::stretchToFit);
        }
        else
        {
            g.setColour (tokens::panel3);
            g.fillRect (r);
        }

        // Violet glow across the strong zone keys plus the live note key.
        auto snap = processor.getDisplayModel().get();
        const auto zones = activeZones (*snap);
        const float yTop = midiToY ((float) zones.strongHighMidi, r);
        const float yBot = midiToY ((float) zones.strongLowMidi, r);
        g.setColour (tokens::violet.withAlpha (0.22f));
        g.fillRect ((float) r.getX(), yTop, (float) r.getWidth(), yBot - yTop);

        auto live = processor.getDisplayModel().getLive();
        if (demoDisplayMode() || live->voiced)
        {
            const float noteY = midiToY (demoDisplayMode() ? demo.vocalMidi : live->currentMidi, r);
            g.setColour (tokens::violet.withAlpha (0.75f));
            g.fillRect ((float) r.getX(), noteY - 2.0f, (float) r.getWidth(), 4.0f);
        }
    }

    // Zone boundaries currently displayed: the artist's real profile when
    // one exists, the contract dataset in demo shots, otherwise the generic
    // placeholder bands the panel ships with.
    ArtistProfile activeZones (const AnalysisSnapshot& snap) const
    {
        if (! demoDisplayMode() && snap.hasProfile)
            return processor.getArtistProfile();

        ArtistProfile p;
        p.comfortableLowMidi = snap.comfortableLowMidi;
        p.comfortableHighMidi = snap.comfortableHighMidi;
        p.strongLowMidi = snap.strongLowMidi;
        p.strongHighMidi = snap.strongHighMidi;
        p.extendedLowMidi = snap.extendedLowMidi;
        p.extendedHighMidi = snap.extendedHighMidi;
        p.falsettoHighMidi = snap.falsettoHighMidi;
        return p;
    }

    void drawRangeGraph (juce::Graphics& g, juce::Rectangle<int> r,
                         const AnalysisSnapshot& snap)
    {
        auto grid = Assets::artistRangeGrid();
        if (grid.isValid())
        {
            g.setColour (juce::Colours::white);
            g.drawImage (grid, r.toFloat(), juce::RectanglePlacement::stretchToFit);
        }

        // --- zone bands ----------------------------------------------------
        // A profile without a falsetto pass has no falsetto band at all -
        // drawing a zero-height one collides its label with EXTENDED RANGE.
        const auto z = activeZones (snap);
        struct Zone { float lo, hi; const char* label; bool strong; };
        std::vector<Zone> zones;
        if (z.hasFalsetto && z.falsettoHighMidi > z.extendedHighMidi)
            zones.push_back ({ (float) z.extendedHighMidi, (float) z.falsettoHighMidi,
                               "FALSETTO", false });
        zones.push_back ({ (float) z.comfortableHighMidi, (float) z.extendedHighMidi,
                           "EXTENDED RANGE", false });
        zones.push_back ({ (float) z.strongLowMidi, (float) z.strongHighMidi,
                           "STRONG ZONE", true });
        zones.push_back ({ (float) z.comfortableLowMidi, (float) z.strongLowMidi,
                           "COMFORTABLE RANGE", false });
        zones.push_back ({ (float) z.extendedLowMidi, (float) z.comfortableLowMidi,
                           "EXTENDED RANGE", false });

        for (const auto& zone : zones)
        {
            const float yTop = midiToY (zone.hi, r), yBot = midiToY (zone.lo, r);
            g.setColour ((zone.strong ? tokens::violet : tokens::muted)
                            .withAlpha (zone.strong ? 0.14f : 0.05f));
            g.fillRect ((float) r.getX(), yTop, (float) r.getWidth(), yBot - yTop);
            g.setColour (tokens::stroke.withAlpha (0.7f));
            g.fillRect ((float) r.getX(), yTop, (float) r.getWidth(), 1.0f);

            // Only label a band tall enough to hold the text.
            if (yBot - yTop >= 15.0f)
            {
                g.setColour (zone.strong ? tokens::violet.brighter (0.2f) : tokens::muted);
                g.setFont (Fonts::make (11.0f, false, true).withExtraKerningFactor (0.07f));
                g.drawText (zone.label, r.getX() + 10, (int) yTop + 4, 160, 13,
                            juce::Justification::centredLeft);
            }
        }

        // --- pitch trail ---------------------------------------------------
        // Demo shots scroll the scripted melody; production draws the real
        // tracked pitch history (already oldest-first, gaps as <= 0).
        auto live = processor.getDisplayModel().getLive();
        const int n = DemoFeed::trailLength;
        juce::Path trail;
        bool penDown = false;
        juce::Point<float> last;

        for (int i = 0; i < n; ++i)
        {
            const int idx = (demo.trailHead + i) % n;   // oldest -> newest
            const float midi = demoDisplayMode() ? demo.trailMidi[(size_t) idx]
                                                 : live->pitchTrail[(size_t) i];
            const float x = (float) r.getX() + ((float) i / (float) (n - 1)) * (float) r.getWidth();

            if (midi <= 0.0f)
            {
                penDown = false;   // unvoiced = gap, never a false zero
                continue;
            }
            const juce::Point<float> pt (x, midiToY (midi, r));
            if (! penDown) { trail.startNewSubPath (pt); penDown = true; }
            else            trail.lineTo (pt);
            last = pt;
        }

        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (r);

            // Colour moves cyan (comfortable) to violet (strong/high) along
            // the vertical position - approximate with a vertical gradient.
            juce::ColourGradient grad (tokens::violet, (float) r.getX(),
                                       midiToY ((float) z.strongHighMidi, r),
                                       tokens::cyan, (float) r.getX(),
                                       midiToY ((float) z.comfortableLowMidi, r), false);
            g.setGradientFill (grad);
            g.strokePath (trail, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

            // Soft under-glow.
            g.setColour (tokens::violet.withAlpha (0.20f));
            g.strokePath (trail, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        // Newest point: glowing endpoint (pitchTrail glowRadius 7 px).
        if (! last.isOrigin())
        {
            auto glow = Assets::particleGlow (Accent::violet);
            if (glow.isValid())
            {
                g.setColour (juce::Colours::white);
                g.drawImage (glow, { last.x - 12.0f, last.y - 12.0f, 24.0f, 24.0f },
                             juce::RectanglePlacement::stretchToFit);
            }
            g.setColour (tokens::white);
            g.fillEllipse (last.x - 2.5f, last.y - 2.5f, 5.0f, 5.0f);
            g.setColour (tokens::violet);
            g.drawEllipse (last.x - 5.5f, last.y - 5.5f, 11.0f, 11.0f, 1.4f);
        }

        // --- axes ----------------------------------------------------------
        g.setFont (Fonts::make (10.0f, true));
        g.setColour (tokens::muted2);
        g.drawText ("dB", r.getRight() + 6, r.getY() - 14, 24, 12, juce::Justification::left);
        const char* dbs[] = { "+12", "+6", "0", "-6", "-12" };
        for (int i = 0; i < 5; ++i)
            g.drawText (dbs[i], r.getRight() + 6,
                        r.getY() + 28 + i * (r.getHeight() - 56) / 4 - 6, 26, 12,
                        juce::Justification::left);

        const char* times[] = { "-12s", "-8s", "-4s", "0s" };
        for (int i = 0; i < 4; ++i)
            g.drawText (times[i],
                        r.getX() + i * (r.getWidth() - 30) / 3, r.getBottom() + 4,
                        30, 12, juce::Justification::centred);
    }

    KeyGloProcessor& processor;
    DemoFeed& demo;

    // --- range test flow --------------------------------------------------
    // One button drives the whole guided test: START -> NEXT x3 -> FINISH.
    // The worker collects stable pitches while each phase is active; the
    // artist controls pacing, so nothing advances on a timer.
    void onRangeTestClick()
    {
        switch (shownPhase)
        {
            case RangeTestPhase::idle:
            case RangeTestPhase::done:
                processor.startRangeTest();
                break;
            case RangeTestPhase::falsetto:
                finishAndReport();
                break;
            default:
                processor.advanceRangeTest();
                break;
        }
    }

    void finishAndReport()
    {
        processor.advanceRangeTest();   // falsetto -> done, builds the profile

        auto snap = processor.getDisplayModel().get();
        if (! snap->hasProfile)
        {
            juce::NativeMessageBox::showAsync (
                juce::MessageBoxOptions()
                    .withIconType (juce::MessageBoxIconType::WarningIcon)
                    .withTitle ("Range Test")
                    .withMessage ("Not enough stable pitch was captured to build a profile.\n\n"
                                  "Sing a sustained note in each step and let the progress bar "
                                  "fill before moving on.")
                    .withButton ("OK"),
                nullptr);
        }
    }

    void onSaveProfileClick()
    {
        if (! processor.hasArtistProfile())
            return;

        auto* editor = new juce::AlertWindow ("Save Artist Profile",
                                              "Name this profile:",
                                              juce::MessageBoxIconType::NoIcon);
        editor->addTextEditor ("name", suggestedProfileName(), {});
        editor->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        editor->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        editor->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, editor] (int result)
            {
                const auto name = editor->getTextEditorContents ("name").trim();
                std::unique_ptr<juce::AlertWindow> owned (editor);
                if (result == 1 && name.isNotEmpty())
                    processor.saveArtistProfile (name);
            }), false);
    }

    juce::String suggestedProfileName() const
    {
        auto snap = processor.getDisplayModel().get();
        return snap->profileName.isNotEmpty() ? snap->profileName : "Artist 1";
    }

    void refreshButtons()
    {
        switch (shownPhase)
        {
            case RangeTestPhase::low:
            case RangeTestPhase::centre:
            case RangeTestPhase::high:
                rangeTestButton.setLabel ("NEXT STEP");
                break;
            case RangeTestPhase::falsetto:
                rangeTestButton.setLabel ("FINISH TEST");
                break;
            case RangeTestPhase::done:
                rangeTestButton.setLabel ("REDO RANGE TEST");
                break;
            default:
                rangeTestButton.setLabel ("START RANGE TEST");
                break;
        }
    }

    RangeTestPhase shownPhase = RangeTestPhase::idle;

    SkinButton rangeTestButton   { "Start Range Test", "range_test_177x42", {}, "mic" };
    SkinButton saveProfileButton { "Save Profile", "profile_169x42", {}, "profile" };
};

} // namespace keyglo
