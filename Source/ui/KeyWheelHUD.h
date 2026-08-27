/*
    KeyWheelHUD - the hero panel: 12-note chroma wheel over the supplied ring
    art, live note nodes, rotating accent arcs, centre readout and the three
    score pods.

    Node angles come from 03_HUD/note_positions.json (C at the top, chromatic
    clockwise, radius 350/1024 of the wheel canvas). All labels, values, glow
    and arcs are live drawing (HUD_RENDERING_NOTES.md) - the base art holds
    only rings, ticks and glass.
*/

#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace keyglo
{

// -----------------------------------------------------------------------------
//  ScorePodComponent - pod artwork + live label/score + progress ring.
//  Scores tween over ~420 ms with ease-out (animation_tokens.json scoreTween).
// -----------------------------------------------------------------------------
class ScorePodComponent : public juce::Component
{
public:
    ScorePodComponent (const juce::String& labelLine1, const juce::String& labelLine2,
                       Accent accentIn)
        : line1 (labelLine1), line2 (labelLine2), accent (accentIn)
    {
        setName (labelLine1 + " " + labelLine2);
        setInterceptsMouseClicks (false, false);
    }

    void setTarget (float score01)
    {
        if (std::abs (score01 - target) > 0.001f)
        {
            tweenFrom = displayed;
            target = score01;
            tweenT = 0.0f;
        }
    }

    // Empty pods show "--" and no ring until their engine milestone reports.
    void setEmpty (bool e)
    {
        if (empty != e) { empty = e; repaint(); }
    }

    void update (double dt)
    {
        if (tweenT < 1.0f)
        {
            tweenT = juce::jmin (1.0f, tweenT + (float) (dt * 1000.0 / 420.0));
            const float e = 1.0f - std::pow (1.0f - tweenT, 3.0f);   // easeOutCubic
            displayed = tweenFrom + (target - tweenFrom) * e;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        auto art = Assets::scorePod (accent);
        if (art.isValid())
        {
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (art, r, juce::RectanglePlacement::centred);
        }
        else
        {
            g.setColour (tokens::panel2);
            g.fillEllipse (r.reduced (4.0f));
        }

        const auto accentColour = accent == Accent::violet ? tokens::violet
                                 : accent == Accent::gold  ? tokens::gold : tokens::cyan;

        // Progress ring, from 12 o'clock clockwise.
        if (! empty)
        {
            juce::Path arc;
            const float radius = r.getWidth() * 0.5f - 7.0f;
            arc.addCentredArc (r.getCentreX(), r.getCentreY(), radius, radius, 0.0f,
                               0.0f, juce::MathConstants<float>::twoPi * displayed, true);
            g.setColour (accentColour.withAlpha (0.9f));
            g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        // Labels stacked in the upper third, score below (approved mockup).
        const int h = getHeight();
        auto labels = getLocalBounds().withTrimmedTop (juce::roundToInt (h * 0.22f))
                                      .withHeight (28);
        g.setColour (tokens::muted.brighter (0.1f));
        g.setFont (Fonts::make (11.0f, false, true).withExtraKerningFactor (0.06f));
        g.drawText (line1, labels.removeFromTop (14), juce::Justification::centred);
        if (line2.isNotEmpty())
            g.drawText (line2, labels, juce::Justification::centred);

        g.setColour (empty ? tokens::muted2 : accentColour);
        g.setFont (Fonts::podScore());
        g.drawText (empty ? "--" : juce::String (juce::roundToInt (displayed * 100.0f)),
                    getLocalBounds().withTrimmedTop (juce::roundToInt (h * 0.48f))
                                    .withHeight (34),
                    juce::Justification::centred);
    }

private:
    juce::String line1, line2;
    Accent accent;
    float target = 0.0f, displayed = 0.0f, tweenFrom = 0.0f, tweenT = 1.0f;
    bool empty = false;
};

// -----------------------------------------------------------------------------
//  KeyWheelHUD
// -----------------------------------------------------------------------------
class KeyWheelHUD : public juce::Component
{
public:
    KeyWheelHUD (KeyGloProcessor& p, DemoFeed& feed)
        : processor (p), demo (feed)
    {
        setName ("KeyWheelHUD");
        addAndMakeVisible (confidencePod);
        addAndMakeVisible (rangePod);
        addAndMakeVisible (hookPod);
    }

    void setReduceMotion (bool b)  { reduceMotion = b; }

    void update (double dt)
    {
        auto snap = processor.getDisplayModel().get();
        const bool demoMode = demoDisplayMode();
        const bool goodKey = demoMode || (snap->hasBeatResult && ! snap->noReliableKey);

        // Orbit speeds (animation_tokens.json): idle 4.5 deg/s, analysing 32,
        // easing over 350 ms; arcs run in opposing directions.
        const float targetSpeed = snap->analyzing ? 32.0f : 4.5f;
        orbitSpeed += (targetSpeed - orbitSpeed) * juce::jmin (1.0f, (float) (dt * 1000.0 / 350.0));

        if (! reduceMotion)
        {
            arcCyan   += (float) dt * orbitSpeed;
            arcViolet -= (float) dt * orbitSpeed * 0.72f;
            arcGold   += (float) dt * orbitSpeed * 0.45f;
        }

        // Node energy: the live input chroma in production, the demo feed in
        // demo shots, with the spec's pulse ballistics (attack 55 ms,
        // release 260 ms) applied per node.
        auto live = processor.getDisplayModel().getLive();
        for (int i = 0; i < 12; ++i)
        {
            float target = demoMode ? demo.chroma[(size_t) i]
                         : live->active ? live->chroma[(size_t) i]
                         : goodKey ? snap->chroma[(size_t) i] * 0.5f : 0.0f;
            auto& e = nodeEnergy[(size_t) i];
            const float ms = target > e ? 55.0f : 260.0f;
            e += (target - e) * juce::jmin (1.0f, (float) (dt * 1000.0 / ms));
        }

        // Key confidence is real from milestone 2; the artist-side pods come
        // alive once milestone 3's vocal engine has a profile and something
        // sung to score. Hook match additionally needs a detected key.
        const bool haveFit = demoMode || snap->hasFitResult;
        confidencePod.setEmpty (! goodKey);
        confidencePod.setTarget (goodKey ? snap->keyConfidence : 0.0f);
        rangePod.setEmpty (! haveFit);
        hookPod.setEmpty (! haveFit || ! goodKey);
        rangePod.setTarget (haveFit ? snap->rangeFit : 0.0f);
        hookPod.setTarget (haveFit && goodKey ? snap->hookMatch : 0.0f);
        confidencePod.update (dt);
        rangePod.update (dt);
        hookPod.update (dt);

        repaint();
    }

    void resized() override
    {
        const auto off = layout::heroPanel.getPosition();
        confidencePod.setBounds (layout::keyConfidencePod - off);
        rangePod.setBounds (layout::rangeFitPod - off);
        hookPod.setBounds (layout::hookMatchPod - off);
    }

    void paint (juce::Graphics& g) override
    {
        const auto off = layout::heroPanel.getPosition();
        auto snap = processor.getDisplayModel().get();

        // Wheel square, aspect-fitted into layout::keyWheel.
        const auto wheelBox = (layout::keyWheel - off).toFloat();
        const float size = juce::jmin (wheelBox.getWidth(), wheelBox.getHeight());
        const juce::Rectangle<float> wheel (wheelBox.getCentreX() - size * 0.5f,
                                            wheelBox.getCentreY() - size * 0.5f, size, size);
        const auto centre = wheel.getCentre();

        // Node ring: measured off the approved mockup's node centres, the
        // nodes orbit at ~0.39 of the wheel size - just inside the art's main
        // bright ring (0.443, measured radially off key_wheel_base_1024) and
        // outside note_positions.json's 350/1024. Angles stay the JSON's
        // exact 30-degree chromatic steps; the mockup's own node placement
        // wobbles by up to 25 degrees because it is a rendered image.
        const float nodeRadius = size * 0.39f;

        // --- base art ------------------------------------------------------
        auto base = Assets::keyWheelBase();
        if (base.isValid())
        {
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (base, wheel, juce::RectanglePlacement::stretchToFit);
        }
        else
        {
            g.setColour (tokens::stroke);
            g.drawEllipse (wheel.reduced (10.0f), 1.5f);
        }

        // --- rotating accent arcs -----------------------------------------
        drawOrbitArc (g, centre, nodeRadius * 1.16f, arcCyan, 145.0f, tokens::cyan, 4.5f);
        drawOrbitArc (g, centre, nodeRadius * 1.21f, arcViolet, 115.0f, tokens::violet, 3.6f);
        drawOrbitArc (g, centre, nodeRadius * 1.25f, arcGold, 38.0f, tokens::gold, 2.8f);

        // --- note nodes ----------------------------------------------------
        const bool demoMode = demoDisplayMode();
        const bool goodKey = demoMode || (snap->hasBeatResult && ! snap->noReliableKey);

        for (int i = 0; i < 12; ++i)
        {
            const float a = juce::degreesToRadians (notes::wheelAngleDegrees[i]);
            const juce::Point<float> pos (centre.x + std::cos (a) * nodeRadius,
                                          centre.y + std::sin (a) * nodeRadius);

            // Without a detected key every node is neutral; the live chroma
            // still pulses them while audio plays.
            const bool isRoot  = goodKey && i == snap->rootNote;
            const bool inScale = goodKey && snap->scaleNotes[(size_t) i];
            const float energy = nodeEnergy[(size_t) i];

            // notePulse: root breathes to 1.11x, scale notes to 1.07x,
            // non-scale nodes stay still but keep a faint rim.
            const float pulse = isRoot  ? 1.0f + 0.11f * energy
                              : inScale ? 1.0f + 0.07f * energy
                              : goodKey ? 1.0f : 1.0f + 0.05f * energy;
            const float d = (isRoot ? 60.0f : inScale ? 46.0f : 38.0f) * pulse;

            auto art = inScale ? Assets::noteNodeActive (Accent::cyan)
                               : Assets::noteNodeInactive();
            const juce::Rectangle<float> box (pos.x - d * 0.5f, pos.y - d * 0.5f, d, d);

            if (isRoot || inScale || (! goodKey && energy > 0.12f))
            {
                // Chroma-driven halo behind the node.
                auto glow = Assets::particleGlow (Accent::cyan);
                if (glow.isValid())
                {
                    const float strength = isRoot ? 0.85f : inScale ? 0.45f : 0.25f;
                    g.setColour (juce::Colours::white.withAlpha (
                        juce::jlimit (0.0f, 1.0f, strength * (0.4f + 0.6f * energy))));
                    g.drawImage (glow, box.expanded (isRoot ? 26.0f : 14.0f),
                                 juce::RectanglePlacement::stretchToFit);
                }
            }

            if (art.isValid())
            {
                g.setColour (juce::Colours::white);
                g.drawImage (art, box, juce::RectanglePlacement::stretchToFit);
            }
            else
            {
                g.setColour (inScale ? tokens::cyan.withAlpha (0.7f) : tokens::stroke);
                g.drawEllipse (box, 1.5f);
            }

            g.setColour (isRoot ? tokens::white
                        : inScale ? tokens::text : tokens::muted2.brighter (0.15f));
            g.setFont (Fonts::make (isRoot ? 19.0f : 15.0f, false, true));
            g.drawText (notes::names[i], box.toNearestInt(), juce::Justification::centred);
        }

        // --- centre readout ------------------------------------------------
        auto glass = Assets::centerGlass();
        if (glass.isValid())
        {
            const float gd = nodeRadius * 1.34f;
            g.setColour (juce::Colours::white);
            g.drawImage (glass, { centre.x - gd * 0.5f, centre.y - gd * 0.5f, gd, gd },
                         juce::RectanglePlacement::stretchToFit);
        }

        juce::Rectangle<int> centreBox ((int) centre.x - 130, (int) centre.y - 66,
                                        260, 150);

        // Key line: detected key, an analysing note, an honest failure, or
        // the fresh-instance hint.
        juce::String keyLine = snap->key.toUpperCase() + " " + snap->scale.toUpperCase();
        float keyHeight = 38.0f;
        if (! demoMode && ! snap->hasBeatResult)
        {
            keyLine = snap->analyzing ? "ANALYZING..." : "DROP A BEAT";
            keyHeight = 27.0f;
        }
        else if (! demoMode && snap->noReliableKey)
        {
            keyLine = "NO RELIABLE KEY";
            keyHeight = 24.0f;
        }

        g.setColour (goodKey || demoMode ? tokens::white : tokens::muted.brighter (0.1f));
        g.setFont (Fonts::centerKey().withHeight (keyHeight));
        g.drawText (keyLine, centreBox.removeFromTop (44), juce::Justification::centred);

        g.setColour (tokens::stroke.brighter (0.15f));
        g.fillRect (centreBox.getCentreX() - 70, centreBox.getY() + 2, 140, 1);

        // Artist Fit and the recommendation are real once a profile exists
        // and there is sung material to score against it.
        const bool haveFit = demoMode || snap->hasFitResult;

        g.setColour (tokens::muted);
        g.setFont (Fonts::make (12.5f, false, true).withExtraKerningFactor (0.09f));
        g.drawText ("ARTIST FIT", centreBox.removeFromTop (20), juce::Justification::centred);

        g.setColour (haveFit ? tokens::white : tokens::muted2);
        g.setFont (Fonts::centerScore().withHeight (44.0f));
        g.drawText (haveFit ? juce::String (juce::roundToInt (snap->artistFit * 100.0f)) : "--",
                    centreBox.removeFromTop (44), juce::Justification::centred);

        g.setColour (tokens::muted);
        g.setFont (Fonts::make (11.5f, false, true).withExtraKerningFactor (0.09f));
        g.drawText ("RECOMMENDED", centreBox.removeFromTop (17), juce::Justification::centred);

        g.setColour (haveFit ? tokens::cyan : tokens::muted2);
        g.setFont (Fonts::make (21.0f, false, true));
        const auto st = snap->recommendedTranspose;
        g.drawText (haveFit ? (st > 0 ? "+" : "") + juce::String (st) + " ST" : "--",
                    centreBox, juce::Justification::centredTop);
    }

private:
    void drawOrbitArc (juce::Graphics& g, juce::Point<float> centre, float radius,
                       float startDeg, float spanDeg, juce::Colour colour, float thickness)
    {
        const float a0 = juce::degreesToRadians (startDeg);
        const float a1 = juce::degreesToRadians (startDeg + spanDeg);

        // addCentredArc measures from 12 o'clock; our angles are from east.
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                           a0 + juce::MathConstants<float>::halfPi,
                           a1 + juce::MathConstants<float>::halfPi, true);
        g.setColour (colour.withAlpha (0.85f));
        g.strokePath (arc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        // Leading-edge glow dot.
        auto glow = Assets::particleGlow (colour == tokens::violet ? Accent::violet
                                          : colour == tokens::gold ? Accent::gold : Accent::cyan);
        if (glow.isValid())
        {
            const juce::Point<float> tip (centre.x + std::cos (a1) * radius,
                                          centre.y + std::sin (a1) * radius);
            g.setColour (juce::Colours::white);
            g.drawImage (glow, { tip.x - 9.0f, tip.y - 9.0f, 18.0f, 18.0f },
                         juce::RectanglePlacement::stretchToFit);
        }
    }

    KeyGloProcessor& processor;
    DemoFeed& demo;

    ScorePodComponent confidencePod { "KEY", "CONFIDENCE", Accent::cyan };
    ScorePodComponent rangePod      { "RANGE", "FIT", Accent::violet };
    ScorePodComponent hookPod       { "HOOK", "MATCH", Accent::gold };

    float arcCyan = -150.0f, arcViolet = -30.0f, arcGold = 55.0f;
    float orbitSpeed = 4.5f;
    bool reduceMotion = false;
    std::array<float, 12> nodeEnergy {};
};

} // namespace keyglo
