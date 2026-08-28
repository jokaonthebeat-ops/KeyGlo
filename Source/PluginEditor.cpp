#include "PluginEditor.h"

namespace keyglo
{

// -----------------------------------------------------------------------------
//  DebugOverlay - development aid: component bounds, base coordinates and the
//  current scale. Hidden; toggled with Cmd/Ctrl+Shift+D.
// -----------------------------------------------------------------------------
class KeyGloEditor::DebugOverlay : public juce::Component
{
public:
    DebugOverlay()
    {
        setInterceptsMouseClicks (false, false);
        setVisible (false);
    }

    float scaleToShow = 1.0f;

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colours::magenta.withAlpha (0.10f));
        for (int x = 0; x < Design::width; x += 100)
            g.fillRect (x, 0, 1, Design::height);
        for (int y = 0; y < Design::height; y += 100)
            g.fillRect (0, y, Design::width, 1);

        if (auto* parent = getParentComponent())
            for (auto* sibling : parent->getChildren())
                if (sibling != this && sibling->isVisible())
                    drawBounds (g, *sibling, sibling->getBounds(), 0);

        g.setColour (juce::Colours::magenta);
        g.setFont (Fonts::make (13.0f, false, true));
        g.drawText ("DEBUG  scale " + juce::String (scaleToShow, 3) + "   base 1491x1055",
                    12, Design::height - 26, 400, 18, juce::Justification::centredLeft);
    }

private:
    void drawBounds (juce::Graphics& g, juce::Component& c,
                     juce::Rectangle<int> r, int depth)
    {
        g.setColour (juce::Colours::magenta.withAlpha (depth == 0 ? 0.75f : 0.35f));
        g.drawRect (r, 1);

        if (depth == 0)
        {
            g.setFont (Fonts::make (9.0f));
            g.drawText (c.getName().isNotEmpty() ? c.getName() : c.getTitle(),
                        r.getX() + 3, r.getY() + 1, 220, 11, juce::Justification::centredLeft);
        }

        for (auto* child : c.getChildren())
            if (child->isVisible())
                drawBounds (g, *child,
                            child->getBounds().translated (r.getX(), r.getY()), depth + 1);
    }
};

// -----------------------------------------------------------------------------
//  ContentComponent - the fixed 1491 x 1055 design canvas: shell + panels,
//  plus the animation clock and demo feed that keep the interface alive.
// -----------------------------------------------------------------------------
class KeyGloEditor::ContentComponent : public juce::Component
{
public:
    explicit ContentComponent (KeyGloProcessor& p)
        : processor (p),
          header (p), beat (p, demo), wheel (p, demo), artist (p, demo),
          autotune (p), transpose (p), sample (p, demo), macros (p), footer (p)
    {
        setOpaque (true);

        addAndMakeVisible (header);
        addAndMakeVisible (beat);
        addAndMakeVisible (wheel);
        addAndMakeVisible (artist);
        addAndMakeVisible (autotune);
        addAndMakeVisible (transpose);
        addAndMakeVisible (sample);
        addAndMakeVisible (footer);
        addAndMakeVisible (macros);      // after footer: its values ride the footer well
        addChildComponent (debugOverlay);

        header.onSettingsChanged = [this] { applyPerformanceMode(); };

        clock.onFrame = [this] (double dt) { frame (dt); };
        applyPerformanceMode();

        setSize (Design::width, Design::height);
    }

    ~ContentComponent() override  { clock.stop(); }

    void applyPerformanceMode()
    {
        // Full 60 fps, low-power 30 fps; reduce motion stops the continuous
        // orbit but keeps meters, pitch and tuner feedback moving.
        clock.stop();
        clock.start (processor.getLowPowerMode() ? 30 : 60);
        wheel.setReduceMotion (processor.getReduceMotion());
    }

    void frame (double dt)
    {
        auto snap = processor.getDisplayModel().get();
        demo.tick (dt, *snap);

        beat.update();
        wheel.update (dt);
        artist.update();
        autotune.update();
        transpose.update();
        sample.update (dt);
        macros.update();

        if (++footerDivider >= 12)   // slow-changing text, ~5 Hz is plenty
        {
            footerDivider = 0;
            footer.repaint();
        }
    }

    void resized() override
    {
        header.setBounds (layout::header);
        beat.setBounds (layout::beatPanel);
        wheel.setBounds (layout::heroPanel);
        artist.setBounds (layout::artistPanel);
        autotune.setBounds (layout::autotunePanel);
        transpose.setBounds (layout::transposePanel);
        sample.setBounds (layout::samplePanel);
        macros.setBounds (layout::macroPanel.withHeight (layout::macroPanel.getHeight() + 28));
        footer.setBounds (layout::footer);
        debugOverlay.setBounds (getLocalBounds());
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (tokens::bg0);

        // The 2x shell keeps edges crisp above 100 %.
        auto shell = displayScale > 1.02f ? Assets::shell2x() : Assets::shell();
        if (! shell.isValid())
            shell = Assets::shell();

        if (shell.isValid())
        {
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (shell, getLocalBounds().toFloat(),
                         juce::RectanglePlacement::stretchToFit);
        }
    }

    KeyGloProcessor& processor;
    DemoFeed demo;
    AnimationClock clock;

    HeaderComponent header;
    BeatAnalysisPanel beat;
    KeyWheelHUD wheel;
    ArtistRangePanel artist;
    AutoTuneSetupPanel autotune;
    TransposePreviewPanel transpose;
    SampleTunePanel sample;
    MacroControlStrip macros;
    FooterStatusComponent footer;
    DebugOverlay debugOverlay;

    float displayScale = 1.0f;
    int footerDivider = 0;
};

// -----------------------------------------------------------------------------
//  Editor
// -----------------------------------------------------------------------------
KeyGloEditor::KeyGloEditor (KeyGloProcessor& p)
    : juce::AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    content = std::make_unique<ContentComponent> (processor);
    addAndMakeVisible (*content);

    setWantsKeyboardFocus (true);

    setResizable (true, true);
    if (auto* constrainer = getConstrainer())
    {
        constrainer->setFixedAspectRatio (Design::aspect);
        constrainer->setSizeLimits (Design::minWidth, Design::minHeight,
                                    Design::maxWidth, Design::maxHeight);
    }

    const float saved = juce::jlimit (0.7f, 1.5f, processor.getSavedUIScale());
    setSize (juce::roundToInt (Design::width * saved),
             juce::roundToInt (Design::height * saved));
}

KeyGloEditor::~KeyGloEditor()
{
    setLookAndFeel (nullptr);
}

void KeyGloEditor::resized()
{
    if (content == nullptr)
        return;

    const auto area = getLocalBounds().toFloat();
    const float scale = juce::jmin (area.getWidth() / (float) Design::width,
                                    area.getHeight() / (float) Design::height);

    // Centre the scaled design when the host supplies extra room.
    const float ox = (area.getWidth()  - Design::width  * scale) * 0.5f;
    const float oy = (area.getHeight() - Design::height * scale) * 0.5f;

    content->setBounds (0, 0, Design::width, Design::height);
    content->setTransform (juce::AffineTransform::scale (scale).translated (ox, oy));

    currentScale = scale;
    content->displayScale = scale;
    content->debugOverlay.scaleToShow = scale;

    processor.setSavedUIScale (scale);
}

void KeyGloEditor::paint (juce::Graphics& g)
{
    g.fillAll (tokens::bg0);   // letterbox behind the centred design
}

bool KeyGloEditor::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == 'D'
         && key.getModifiers().isCommandDown()
         && key.getModifiers().isShiftDown())
    {
        auto& overlay = content->debugOverlay;
        overlay.setVisible (! overlay.isVisible());
        return true;
    }
    return false;
}

void KeyGloEditor::hoverBeatDrop (bool hovering)
{
    // The same calls a real drag makes - no separate "demo" appearance that
    // could drift from what a user actually sees.
    if (hovering)
        content->beat.fileDragEnter ({}, 0, 0);
    else
        content->beat.fileDragExit ({});
}

void KeyGloEditor::dropBeatFile (const juce::File& file)
{
    content->beat.filesDropped ({ file.getFullPathName() }, 0, 0);
}

void KeyGloEditor::refreshDisplays()
{
    headlessRefreshMode() = true;
    content->frame (1.0 / 60.0);
    juce::Timer::callPendingTimersSynchronously();
    content->repaint();
}

} // namespace keyglo
