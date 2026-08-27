/*
    FooterStatusComponent - version badge, product line, engine status.

    Documented deviation from the mockup: the reference footer reads
    "ONLINE"; KeyGlo's spec promises a fully local product with no internet
    dependency, so the shipped footer says LOCAL - honest strings over
    mockup sample text (the SourceGlo "Track 07" lesson).
*/

#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace keyglo
{

class FooterStatusComponent : public juce::Component
{
public:
    explicit FooterStatusComponent (KeyGloProcessor& p) : processor (p)
    {
        setName ("Footer");
        setInterceptsMouseClicks (false, true);
    }

    void paint (juce::Graphics& g) override
    {
        // Content sits in the lower half of the footer well; the macro value
        // readouts ride the upper half.
        auto row = getLocalBounds().withTrimmedTop (getHeight() - 46).reduced (26, 8);

        // --- left: KG badge + version -------------------------------------
        {
            auto badge = row.removeFromLeft (34).withSizeKeepingCentre (32, 24);
            g.setColour (tokens::panel3);
            g.fillRoundedRectangle (badge.toFloat(), 5.0f);
            g.setColour (tokens::strokeHi);
            g.drawRoundedRectangle (badge.toFloat().reduced (0.5f), 5.0f, 1.0f);
            g.setColour (tokens::text);
            g.setFont (Fonts::make (12.0f, false, true));
            g.drawText ("KG", badge, juce::Justification::centred);

            row.removeFromLeft (10);
            g.setColour (tokens::muted);
            g.setFont (Fonts::footer());
            g.drawText ("v" JucePlugin_VersionString, row.removeFromLeft (70),
                        juce::Justification::centredLeft);
        }

        // --- centre: product line flanked by accent dots -------------------
        {
            const auto centreRow = getLocalBounds().withTrimmedTop (getHeight() - 46)
                                                   .withHeight (34);
            g.setColour (tokens::text.withAlpha (0.9f));
            g.setFont (Fonts::make (13.0f, false, true).withExtraKerningFactor (0.18f));
            const juce::String title ("ARTIST-TO-BEAT KEY MATCHING");
            g.drawText (title, centreRow, juce::Justification::centred);

            const float w = juce::GlyphArrangement::getStringWidth (
                Fonts::make (13.0f, false, true).withExtraKerningFactor (0.18f), title);
            const float cx = (float) centreRow.getCentreX();
            const float cy = (float) centreRow.getCentreY();
            g.setColour (tokens::cyan);
            g.fillEllipse (cx - w * 0.5f - 26.0f, cy - 3.0f, 6.0f, 6.0f);
            g.setColour (tokens::violet);
            g.fillEllipse (cx + w * 0.5f + 20.0f, cy - 3.0f, 6.0f, 6.0f);
        }

        // --- right: engine status ------------------------------------------
        {
            auto right = getLocalBounds().withTrimmedTop (getHeight() - 46)
                                         .withHeight (34).reduced (26, 0);
            const bool bypassed = processor.getAPVTS().getRawParameterValue (
                                      pid::pluginBypass)->load() > 0.5f;

            g.setFont (Fonts::footer());
            g.setColour (tokens::muted);
            g.drawText ("LOCAL", right.removeFromRight (52), juce::Justification::centredRight);

            g.setColour (tokens::stroke);
            g.fillRect (right.getRight() - 12, right.getCentreY() - 8, 1, 16);
            right.removeFromRight (24);

            g.setColour (bypassed ? tokens::muted2 : tokens::green);
            g.fillEllipse ((float) right.getRight() - 8.0f,
                           (float) right.getCentreY() - 3.0f, 6.0f, 6.0f);
            right.removeFromRight (16);

            g.setColour (tokens::muted);
            const auto label = bypassed ? juce::String ("BYPASSED")
                                        : juce::String ("AI ANALYSIS");
            g.drawText (label, right.removeFromRight (86), juce::Justification::centredRight);

            if (auto* ic = Assets::icon ("confidence", tokens::muted))
                ic->drawWithin (g, right.removeFromRight (18)
                                     .withSizeKeepingCentre (14, 14).toFloat(),
                                juce::RectanglePlacement::centred, 1.0f);
        }
    }

private:
    KeyGloProcessor& processor;
};

} // namespace keyglo
