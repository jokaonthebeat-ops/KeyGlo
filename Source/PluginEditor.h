/*
    KeyGloEditor - the approved 1491 x 1055 interface.

    One fixed-size content component holds every panel in design coordinates;
    the editor applies a single uniform AffineTransform scale and centres the
    result (JUCE_IMPLEMENTATION_SPEC: never stretch X and Y independently).
    Aspect ratio locked 1491:1055, minimum 1044 x 739, maximum 2237 x 1583.
*/

#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/AnimationClock.h"
#include "ui/HeaderComponent.h"
#include "ui/BeatAnalysisPanel.h"
#include "ui/KeyWheelHUD.h"
#include "ui/ArtistRangePanel.h"
#include "ui/AutoTuneSetupPanel.h"
#include "ui/TransposePreviewPanel.h"
#include "ui/SampleTunePanel.h"
#include "ui/MacroControlStrip.h"
#include "ui/FooterStatusComponent.h"

namespace keyglo
{

class KeyGloEditor : public juce::AudioProcessorEditor
{
public:
    explicit KeyGloEditor (KeyGloProcessor&);
    ~KeyGloEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

    // Drives every display timer once, synchronously - used by make uishot.
    void refreshDisplays();

    // Drives the REAL drag-and-drop path on the beat panel, so the demo film
    // shows the feature working rather than a result appearing from nowhere:
    // hoverBeatDrop() lights the drop zone exactly as a dragged file does,
    // dropBeatFile() performs the drop and starts the analysis.
    void hoverBeatDrop (bool hovering);
    void dropBeatFile (const juce::File& file);

private:
    class ContentComponent;
    class DebugOverlay;

    KeyGloProcessor& processor;
    KeyGloLookAndFeel lookAndFeel;
    std::unique_ptr<ContentComponent> content;
    juce::TooltipWindow tooltips { this, 650 };
    float currentScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyGloEditor)
};

} // namespace keyglo
