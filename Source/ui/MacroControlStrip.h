/*
    MacroControlStrip - the six filmstrip macros in the approved order
    (Range Sense, Key Sense, Smooth, Preview Mix, Fine Tune, Output), the
    stereo segment meter, and the value readouts that ride the footer well's
    top edge exactly as the mockup draws them.

    Cyan strips everywhere except Fine Tune, which is gold
    (CLAUDE_MASTER_BUILD_PROMPT.md macro strip section).
*/

#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace keyglo
{

class MacroControlStrip : public juce::Component
{
public:
    explicit MacroControlStrip (KeyGloProcessor& p) : processor (p)
    {
        setName ("MacroStrip");

        struct Def { const char* pid; const char* label; Accent accent; };
        const Def defs[6] = {
            { pid::rangeSense,     "RANGE SENSE", Accent::cyan },
            { pid::keySense,       "KEY SENSE",   Accent::cyan },
            { pid::analysisSmooth, "SMOOTH",      Accent::cyan },
            { pid::previewMix,     "PREVIEW MIX", Accent::cyan },
            { pid::fineTuneCents,  "FINE TUNE",   Accent::gold },
            { pid::outputGainDb,   "OUTPUT",      Accent::cyan },
        };

        for (int i = 0; i < 6; ++i)
        {
            auto* knob = knobs.add (new FilmstripKnob (defs[i].label, defs[i].accent));
            addAndMakeVisible (knob);
            attachments.add (new juce::AudioProcessorValueTreeState::SliderAttachment (
                processor.getAPVTS(), defs[i].pid, *knob));
            labels[i] = defs[i].label;

            // Double-click returns to the parameter's own default, not zero.
            if (auto* param = processor.getAPVTS().getParameter (defs[i].pid))
                knob->setDoubleClickReturnValue (true,
                    param->convertFrom0to1 (param->getDefaultValue()));
        }

        addAndMakeVisible (meter);
    }

    void update()
    {
        meter.setLevels (processor.getPeakDb (0), processor.getPeakDb (1));

        // Mini input strip at the far left of the well.
        miniLevel = juce::jmax (miniLevel * 0.90f,
                                juce::jmap (juce::jlimit (-42.0f, 0.0f,
                                            juce::jmax (processor.getPeakDb (0),
                                                        processor.getPeakDb (1))),
                                            -42.0f, 0.0f, 0.0f, 1.0f));
        repaint();
    }

    void resized() override
    {
        const auto off = getPosition();   // bounds start at layout::macroPanel minus overshoot
        const juce::Rectangle<int> knobRects[6] = {
            layout::rangeSenseKnob, layout::keySenseKnob, layout::smoothKnob,
            layout::previewMixKnob, layout::fineTuneKnob, layout::outputKnob
        };
        for (int i = 0; i < 6; ++i)
            knobs[i]->setBounds (knobRects[i] - off);

        meter.setBounds (layout::outputMeter - off);
    }

    void paint (juce::Graphics& g) override
    {
        const auto off = getPosition();

        // --- labels above-right of each knob, values riding the footer edge
        for (int i = 0; i < 6; ++i)
        {
            const auto kb = knobs[i]->getBounds();

            g.setColour (tokens::muted.brighter (0.08f));
            g.setFont (Fonts::rowLabel());
            g.drawText (labels[i], kb.getCentreX() + 42, kb.getY() + 18, 130, 16,
                        juce::Justification::centredLeft);

            const juce::Rectangle<int> valueRow (kb.getCentreX() - 70,
                                                 kb.getBottom() - 9, 140, 18);
            const bool gold = i == 4;
            g.setColour (gold ? tokens::gold : tokens::text);
            g.setFont (Fonts::make (15.0f, true));

            if (i == 3)
            {
                // PREVIEW MIX shows DRY <value> WET.
                g.drawText (valueText (i), valueRow, juce::Justification::centred);
                g.setColour (tokens::muted2);
                g.setFont (Fonts::make (9.5f, false, true).withExtraKerningFactor (0.06f));
                g.drawText ("DRY", valueRow.translated (-48, -3), juce::Justification::centred);
                g.drawText ("WET", valueRow.translated (48, -3), juce::Justification::centred);
            }
            else if (gold)
            {
                g.drawText (valueText (i), valueRow.withTrimmedRight (44),
                            juce::Justification::centredRight);
                g.setColour (tokens::muted);
                g.setFont (Fonts::make (10.0f, false, true).withExtraKerningFactor (0.05f));
                g.drawText ("CENTS", valueRow.withTrimmedLeft (100),
                            juce::Justification::centredLeft);
            }
            else
            {
                g.drawText (valueText (i), valueRow, juce::Justification::centred);
            }
        }

        // --- mini level strip on the well's left edge ----------------------
        {
            const juce::Rectangle<int> strip (layout::macroPanel.getX() + 7 - off.x,
                                              layout::macroPanel.getY() + 24 - off.y,
                                              10, layout::macroPanel.getHeight() - 40);
            const int segH = 4, segGap = 2;
            const int count = strip.getHeight() / (segH + segGap);
            const int lit = juce::roundToInt (miniLevel * (float) count);
            for (int s = 0; s < count; ++s)
            {
                const bool on = s < lit;
                g.setColour (on ? tokens::cyan.withAlpha (0.8f)
                                : tokens::stroke.withAlpha (0.5f));
                g.fillRect (strip.getX(),
                            strip.getBottom() - (s + 1) * (segH + segGap) + segGap,
                            strip.getWidth(), segH);
            }
        }

        // --- meter scale + channel captions --------------------------------
        {
            const auto mb = meter.getBounds();
            g.setColour (tokens::muted2);
            g.setFont (Fonts::make (9.0f, true));
            static const char* marks[] = { "0", "-6", "-9", "-12", "-18", "-24", "-36" };
            for (int i = 0; i < 7; ++i)
                g.drawText (marks[i], mb.getRight() + 4,
                            mb.getY() + 4 + i * (mb.getHeight() - 18) / 6, 24, 10,
                            juce::Justification::left);

            g.setFont (Fonts::make (9.5f, true));
            g.drawText ("L", mb.getX() + 8, mb.getBottom() + 3, 14, 11, juce::Justification::centred);
            g.drawText ("R", mb.getRight() - 22, mb.getBottom() + 3, 14, 11, juce::Justification::centred);
            g.drawText ("dB", mb.getRight() + 4, mb.getBottom() + 3, 20, 11, juce::Justification::left);
        }
    }

private:
    juce::String valueText (int i) const
    {
        const double v = knobs[i]->getValue();
        switch (i)
        {
            case 3:  return juce::String (juce::roundToInt (v * 100.0)) + "%";
            case 4:  return (v >= 0.05 ? "+" : "") + juce::String (juce::roundToInt (v));
            case 5:  return juce::String (v, 1) + " dB";
            default: return juce::String (juce::roundToInt (v * 100.0)) + "%";
        }
    }

    KeyGloProcessor& processor;
    juce::OwnedArray<FilmstripKnob> knobs;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> attachments;
    juce::String labels[6];
    StereoSegmentMeter meter;
    float miniLevel = 0.0f;
};

} // namespace keyglo
