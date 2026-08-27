/*
    SampleTunePanel - 808/sample waveform, the gold spring-smoothed tuner
    dial, detected note readouts and Apply Tune / Solo.

    Tuner ballistics per animation_tokens.json: spring 8.5 Hz, damping 0.72,
    gold rim lighting inside +/-5 cents.
*/

#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace keyglo
{

// -----------------------------------------------------------------------------
//  TunerDialComponent
// -----------------------------------------------------------------------------
class TunerDialComponent : public juce::Component
{
public:
    TunerDialComponent()
    {
        setName ("TunerDial");
        setInterceptsMouseClicks (false, false);
    }

    void setCents (float c)   { targetCents = juce::jlimit (-50.0f, 50.0f, c); }
    void setNote (const juce::String& n)  { note = n; }

    void update (double dt)
    {
        // Damped spring toward the target (springHz 8.5, damping 0.72).
        const float w = juce::MathConstants<float>::twoPi * 8.5f;
        const float accel = w * w * (targetCents - shownCents)
                          - 2.0f * 0.72f * w * velocity;
        velocity += accel * (float) dt;
        shownCents += velocity * (float) dt;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const auto centre = r.getCentre();

        auto dial = Assets::tunerDial();
        if (dial.isValid())
        {
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (dial, r, juce::RectanglePlacement::centred);
        }
        else
        {
            g.setColour (tokens::gold.withAlpha (0.6f));
            g.drawEllipse (r.reduced (6.0f), 1.5f);
        }

        const bool inTune = std::abs (shownCents) <= 5.0f;
        const float radius = r.getWidth() * 0.5f;

        // Decorative gold sweep over the dial's upper arc with a glowing tip,
        // as the approved mockup draws it.
        {
            juce::Path sweep;
            const float sr = radius * 0.88f;
            const float a0 = juce::degreesToRadians (-155.0f) + juce::MathConstants<float>::halfPi;
            const float a1 = juce::degreesToRadians (20.0f) + juce::MathConstants<float>::halfPi;
            sweep.addCentredArc (centre.x, centre.y, sr, sr, 0.0f, a0, a1, true);
            g.setColour (tokens::gold.withAlpha (0.85f));
            g.strokePath (sweep, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

            auto glow = Assets::particleGlow (Accent::gold);
            if (glow.isValid())
            {
                const float tx = centre.x + std::cos (juce::degreesToRadians (20.0f)) * sr;
                const float ty = centre.y + std::sin (juce::degreesToRadians (20.0f)) * sr;
                g.setColour (juce::Colours::white);
                g.drawImage (glow, { tx - 9.0f, ty - 9.0f, 18.0f, 18.0f },
                             juce::RectanglePlacement::stretchToFit);
            }
        }

        // Gold rim intensifies when in tune.
        if (inTune)
        {
            g.setColour (tokens::gold.withAlpha (0.35f));
            g.drawEllipse (r.reduced (radius * 0.10f), 2.5f);
        }

        // Needle: +/-50 cents sweeps +/-45 degrees from 12 o'clock.
        {
            const float angle = juce::degreesToRadians (shownCents * 0.9f);
            const float x = std::sin (angle), y = -std::cos (angle);
            const float inner = radius * 0.30f, outer = radius * 0.74f;

            g.setColour (inTune ? tokens::gold : tokens::text);
            g.drawLine (centre.x + x * inner, centre.y + y * inner,
                        centre.x + x * outer, centre.y + y * outer, 2.2f);

            auto glow = Assets::particleGlow (Accent::gold);
            if (glow.isValid())
            {
                g.setColour (juce::Colours::white.withAlpha (inTune ? 1.0f : 0.55f));
                g.drawImage (glow, { centre.x + x * outer - 7.0f,
                                     centre.y + y * outer - 7.0f, 14.0f, 14.0f },
                             juce::RectanglePlacement::stretchToFit);
            }
        }

        // Centre note label. The crossfade-on-change rule matters once real
        // detection runs; the demo note is stable.
        g.setColour (tokens::white);
        g.setFont (Fonts::make (30.0f, false, true));
        g.drawText (note, getLocalBounds(), juce::Justification::centred);
    }

private:
    juce::String note = "G";
    float targetCents = 4.0f, shownCents = 0.0f, velocity = 0.0f;
};

// -----------------------------------------------------------------------------
//  SampleTunePanel
// -----------------------------------------------------------------------------
class SampleTunePanel : public juce::Component,
                        public juce::FileDragAndDropTarget
{
public:
    SampleTunePanel (KeyGloProcessor& p, DemoFeed& feed)
        : processor (p), demo (feed)
    {
        setName ("SampleTune");

        applyButton.setLabel ("APPLY TUNE");
        applyButton.setFontHeight (13.0f);
        applyButton.setLabelColour (tokens::gold);
        applyButton.setIconTint (tokens::gold);
        applyButton.setAccent (tokens::gold);
        applyButton.onClick = [this] { processor.applyTuneAsync(); };
        addAndMakeVisible (applyButton);

        soloButton.setLabel ("SOLO");
        soloButton.setFontHeight (13.0f);
        soloButton.setIconTint (tokens::text);
        soloButton.setClickingTogglesState (true);
        addAndMakeVisible (soloButton);
        soloAttachment = std::make_unique<juce::ButtonParameterAttachment> (
            *processor.getAPVTS().getParameter (pid::sampleSolo), soloButton);

        addAndMakeVisible (tuner);
    }

    void update (double dt)
    {
        auto snap = processor.getDisplayModel().get();
        const bool haveSample = snap->hasSampleResult && ! snap->sampleNoStableNote;

        if (demoDisplayMode())
        {
            tuner.setNote (snap->sampleNote);
            tuner.setCents (demo.tunerCents);
        }
        else if (haveSample)
        {
            tuner.setNote (snap->sampleNote);
            tuner.setCents (snap->sampleDeviationCents);
        }
        else
        {
            tuner.setNote ("--");
            tuner.setCents (0.0f);
        }
        tuner.update (dt);

        applyButton.setEnabled (demoDisplayMode() || haveSample);
        soloButton.setEnabled (demoDisplayMode() || snap->sampleTunedReady);
        repaint();
    }

    // --- sample drop target -------------------------------------------------
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (const auto& f : files)
            if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif")
                 || f.endsWithIgnoreCase (".aiff") || f.endsWithIgnoreCase (".flac")
                 || f.endsWithIgnoreCase (".mp3"))
                return true;
        return false;
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHighlight = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override            { dropHighlight = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dropHighlight = false;
        processor.analyseSampleAsync (juce::File (files[0]));
        repaint();
    }

    void resized() override
    {
        const auto off = layout::samplePanel.getPosition();
        tuner.setBounds (layout::sampleTuner - off);
        applyButton.setBounds (layout::applyTune - off);
        soloButton.setBounds (layout::sampleSolo - off);
    }

    void paint (juce::Graphics& g) override
    {
        const auto off = layout::samplePanel.getPosition();
        auto local = [off] (juce::Rectangle<int> r) { return r - off; };
        auto snap = processor.getDisplayModel().get();

        drawPanelTitle (g, { 15, 15, 280, 26 }, "wave", "808 / Sample Tune", tokens::gold);

        drawWaveform (g, local (layout::sampleWaveform));
        drawReadouts (g, local (layout::sampleReadouts), *snap);
    }

private:
    void drawWaveform (juce::Graphics& g, juce::Rectangle<int> r)
    {
        auto snap = processor.getDisplayModel().get();
        auto grid = Assets::waveformGrid();
        if (grid.isValid())
        {
            g.setColour (juce::Colours::white);
            g.drawImage (grid, r.toFloat(), juce::RectanglePlacement::stretchToFit);
        }
        else
        {
            g.setColour (tokens::bg1);
            g.fillRoundedRectangle (r.toFloat(), 6.0f);
        }

        if (dropHighlight)
        {
            g.setColour (tokens::gold.withAlpha (0.7f));
            g.drawRoundedRectangle (r.toFloat().reduced (1.0f), 6.0f, 1.6f);
        }

        const auto plot = r.reduced (6, 4).withTrimmedBottom (14);
        const float midY = (float) plot.getCentreY();

        if (! demoDisplayMode() && snap->hasSampleResult)
        {
            // Real min/max envelope of the dropped sample.
            const int bins = 240;
            const float bw = (float) plot.getWidth() / (float) bins;
            g.setColour (tokens::gold.withAlpha (0.85f));
            for (int b = 0; b < bins; ++b)
            {
                const float lo = snap->sampleWaveform[(size_t) (b * 2)];
                const float hi = snap->sampleWaveform[(size_t) (b * 2 + 1)];
                const float yTop = midY - hi * (float) plot.getHeight() * 0.48f;
                const float yBot = midY - lo * (float) plot.getHeight() * 0.48f;
                g.fillRect ((float) plot.getX() + bw * (float) b, yTop,
                            juce::jmax (1.0f, bw - 0.4f), juce::jmax (1.0f, yBot - yTop));
            }

            // File name + honest state line inside the well's top edge.
            g.setColour (tokens::muted);
            g.setFont (Fonts::make (9.5f, true));
            g.drawText (snap->sampleFileName, plot.withHeight (12),
                        juce::Justification::topLeft);

            if (snap->samplePitchEnvelope)
            {
                g.setColour (tokens::red.withAlpha (0.9f));
                g.setFont (Fonts::make (9.5f, false, true).withExtraKerningFactor (0.05f));
                g.drawText ("PITCH ENVELOPE: " + snap->sampleStartNote + " > " + snap->sampleNote,
                            plot.withHeight (12), juce::Justification::topRight);
            }
            else if (snap->sampleNoStableNote)
            {
                g.setColour (tokens::red.withAlpha (0.9f));
                g.setFont (Fonts::make (9.5f, false, true).withExtraKerningFactor (0.05f));
                g.drawText ("NO STABLE NOTE", plot.withHeight (12),
                            juce::Justification::topRight);
            }
        }
        else if (! demoDisplayMode())
        {
            g.setColour (tokens::muted2);
            g.setFont (Fonts::make (12.0f, false, true).withExtraKerningFactor (0.06f));
            g.drawText ("DROP 808 / SAMPLE HERE", plot, juce::Justification::centred);
        }
        else
        {
            // Demo shots keep the scripted 808 decay.
            juce::Path wave;
            wave.startNewSubPath ((float) plot.getX(), midY);
            for (int i = 0; i <= 200; ++i)
            {
                const float t = (float) i / 200.0f;
                const float x = (float) plot.getX() + t * (float) plot.getWidth();
                const float env = std::exp (-t * 4.2f);
                const float cycle = std::sin (t * 95.0f + (float) demoPhase * 0.35f)
                                  + 0.35f * std::sin (t * 190.0f);
                wave.lineTo (x, midY - cycle * env * (float) plot.getHeight() * 0.42f);
            }
            g.setColour (tokens::gold.withAlpha (0.9f));
            g.strokePath (wave, juce::PathStrokeType (1.4f));
            g.setColour (tokens::gold.withAlpha (0.25f));
            g.strokePath (wave, juce::PathStrokeType (3.6f));
            demoPhase += 0.15;
        }

        // Frequency captions inside the well's bottom edge.
        static const char* freqs[] = { "20", "50", "100", "200", "500", "1k", "2k" };
        g.setColour (tokens::muted2);
        g.setFont (Fonts::make (9.5f, true));
        const auto axis = r.reduced (8, 0).removeFromBottom (13);
        for (int i = 0; i < 7; ++i)
            g.drawText (freqs[i],
                        axis.getX() + (int) ((float) axis.getWidth() * (float) i / 6.6f),
                        axis.getY(), 26, 11, juce::Justification::left);
    }

    void drawReadouts (juce::Graphics& g, juce::Rectangle<int> r,
                       const AnalysisSnapshot& snap)
    {
        const bool haveSample = demoDisplayMode()
                                  || (snap.hasSampleResult && ! snap.sampleNoStableNote);

        struct Cell { const char* label; juce::String big; juce::String small; };
        const int st = snap.sampleRecommendedSemitones;
        const int ft = juce::roundToInt (snap.sampleFineTuneCents);
        const Cell cells[3] = {
            { "DETECTED SUSTAIN NOTE", haveSample ? snap.sampleNote : "--", "" },
            { "RECOMMENDED", haveSample ? (st > 0 ? "+" : "") + juce::String (st) : "--", "SEMITONE" },
            { "FINE TUNE", haveSample ? (ft > 0 ? "+" : "") + juce::String (ft) : "--", "CENTS" },
        };

        // The first cell is wider in the mockup - its label is the long one.
        const int gap = 6;
        const int widths[3] = { 146, 111, 114 };
        int cellX = r.getX();
        for (int i = 0; i < 3; ++i)
        {
            const juce::Rectangle<int> cell (cellX, r.getY(), widths[i], r.getHeight());
            cellX += widths[i] + gap;
            auto art = Assets::readoutCell (Accent::gold);
            if (art.isValid())
            {
                g.setColour (juce::Colours::white);
                g.drawImage (art, cell.toFloat(), juce::RectanglePlacement::stretchToFit);
            }
            else
            {
                g.setColour (tokens::panel2);
                g.fillRoundedRectangle (cell.toFloat(), 6.0f);
            }

            auto content = cell.reduced (10, 5);
            g.setColour (tokens::muted);
            g.setFont (Fonts::make (9.5f, false, true).withExtraKerningFactor (0.05f));
            g.drawText (cells[i].label, content.removeFromTop (14),
                        juce::Justification::centredLeft);

            g.setColour (tokens::gold);
            const auto bigFont = Fonts::make (24.0f, false, true);
            g.setFont (bigFont);
            const int bigW = cells[i].small.isEmpty()
                ? content.getWidth()
                : juce::jmin (content.getWidth(),
                              6 + (int) std::ceil (juce::GlyphArrangement::getStringWidth (
                                       bigFont, cells[i].big)));
            g.drawText (cells[i].big, content.removeFromLeft (bigW),
                        juce::Justification::centredLeft);

            if (cells[i].small.isNotEmpty())
            {
                g.setColour (tokens::muted);
                g.setFont (Fonts::make (10.0f, false, true).withExtraKerningFactor (0.04f));
                g.drawText (cells[i].small, content, juce::Justification::centredLeft);
            }
        }
    }

    KeyGloProcessor& processor;
    DemoFeed& demo;
    double demoPhase = 0.0;
    bool dropHighlight = false;

    TunerDialComponent tuner;
    SkinButton applyButton { "Apply Tune", "apply_tune_219x31", {}, "tuning" };
    SkinButton soloButton  { "Sample Solo", "solo_157x31", {}, "speaker" };
    std::unique_ptr<juce::ButtonParameterAttachment> soloAttachment;
};

} // namespace keyglo
