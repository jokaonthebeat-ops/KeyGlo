/*
    HeaderComponent - premium logo, preset browser, utility icons, power.

    The logo is the supplied image, cropped to opaque bounds at load and
    aspect-fitted into layout::logo (LOGO_USAGE_GUIDE.md forbids typesetting
    the name). The preset browser, Save, Undo and Redo are live from the
    product milestone: prev/next step the real bank, the field shows the
    current preset with a modified dot, Save writes a user preset.
*/

#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace keyglo
{

class HeaderComponent : public juce::Component,
                        private juce::Timer
{
public:
    explicit HeaderComponent (KeyGloProcessor& p) : processor (p)
    {
        setName ("Header");

        addAndMakeVisible (prevButton);
        addAndMakeVisible (nextButton);
        prevButton.onClick = [this] { processor.getPresets().step (-1); repaint(); };
        nextButton.onClick = [this] { processor.getPresets().step (1); repaint(); };

        for (auto* b : { &saveButton, &settingsButton, &helpButton, &undoButton, &redoButton })
            addAndMakeVisible (*b);

        settingsButton.onClick = [this] { showSettingsMenu(); };
        saveButton.onClick = [this] { savePresetPrompt(); };
        undoButton.onClick = [this]
        {
            processor.getPresets().captureUndoPoint();   // bank any pending drift first
            processor.getUndoManager().undo();
            processor.getPresets().resyncUndoBaseline();
            repaint();
        };
        redoButton.onClick = [this]
        {
            processor.getUndoManager().redo();
            processor.getPresets().resyncUndoBaseline();
            repaint();
        };

        // Slow poll: undo/redo enablement + the modified dot. Also gives the
        // undo history its transaction boundaries - JUCE's recommended
        // pattern is a periodic beginNewTransaction, so knob drags coalesce
        // per interval instead of one step per sample value.
        startTimerHz (4);

        addAndMakeVisible (powerButton);
        powerButton.setCircled (true, tokens::cyan);
        powerButton.setClickingTogglesState (true);
        powerButton.setToggleState (true, juce::dontSendNotification);
        powerAttachment = std::make_unique<juce::ParameterAttachment> (
            *processor.getAPVTS().getParameter (pid::pluginBypass),
            [this] (float v) { powerButton.setToggleState (v < 0.5f, juce::dontSendNotification); });
        powerAttachment->sendInitialUpdate();
        powerButton.onClick = [this]
        {
            if (auto* param = processor.getAPVTS().getParameter (pid::pluginBypass))
                param->setValueNotifyingHost (powerButton.getToggleState() ? 0.0f : 1.0f);
        };

        onSettingsChanged = [] {};
    }

    std::function<void()> onSettingsChanged;

    void resized() override
    {
        // All child bounds in design space, translated into this component.
        const auto off = layout::header.getPosition();
        auto local = [off] (juce::Rectangle<int> r) { return r - off; };

        prevButton.setBounds (local (layout::presetPrev));
        nextButton.setBounds (local (layout::presetNext));

        saveButton.setBounds     (local ({ 1050, 22, 58, 52 }));
        settingsButton.setBounds (local ({ 1120, 22, 60, 52 }));
        helpButton.setBounds     (local ({ 1194, 22, 58, 52 }));
        undoButton.setBounds     (local ({ 1290, 22, 52, 52 }));
        redoButton.setBounds     (local ({ 1346, 22, 52, 52 }));
        powerButton.setBounds    (local ({ 1420, 21, 46, 46 }));
    }

    void paint (juce::Graphics& g) override
    {
        const auto off = layout::header.getPosition();
        auto local = [off] (juce::Rectangle<int> r) { return r - off; };

        // --- premium logo, left-anchored aspect fit ------------------------
        auto logo = Assets::logoHeader (1.0f);
        if (logo.isValid())
        {
            const auto box = local (layout::logo).toFloat();
            const float s = juce::jmin (box.getWidth() / (float) logo.getWidth(),
                                        box.getHeight() / (float) logo.getHeight());
            const float w = logo.getWidth() * s, h = logo.getHeight() * s;
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (logo, { box.getX(), box.getCentreY() - h * 0.5f, w, h },
                         juce::RectanglePlacement::stretchToFit);
        }
        else
        {
            g.setColour (tokens::red.withAlpha (0.6f));
            g.drawRect (local (layout::logo));
        }

        // --- preset name field --------------------------------------------
        const auto nameBox = local (layout::presetName);
        auto field = Assets::buttonSkin ("preset_field_338x52", ButtonState::normal);
        if (field.isValid())
            g.drawImage (field, nameBox.toFloat(), juce::RectanglePlacement::stretchToFit);
        else
        {
            g.setColour (tokens::bg1);
            g.fillRoundedRectangle (nameBox.toFloat(), 8.0f);
        }
        auto& bank = processor.getPresets();
        g.setColour (tokens::white);
        g.setFont (Fonts::make (19.0f, true));
        g.drawText (bank.currentName(), nameBox.reduced (18, 0),
                    juce::Justification::centred);

        // Modified marker: a small cyan dot at the field's right edge when
        // the current parameters have drifted from the loaded preset.
        if (bank.isModified())
        {
            g.setColour (tokens::cyan);
            g.fillEllipse ((float) nameBox.getRight() - 16.0f,
                           (float) nameBox.getCentreY() - 2.5f, 5.0f, 5.0f);
        }

        // "PRESET" caption + page dots below the field. The dots stay the
        // mockup's six factory pages; a user preset lights none of them.
        g.setColour (tokens::muted2);
        g.setFont (Fonts::make (10.0f, true).withExtraKerningFactor (0.22f));
        g.drawText ("PRESET", nameBox.withY (nameBox.getBottom() - 3).withHeight (12),
                    juce::Justification::centred);

        const int dotCount = PresetManager::factoryCount;
        const int dotSpacing = 15;
        int dx = nameBox.getCentreX() - ((dotCount - 1) * dotSpacing) / 2;
        const int dy = nameBox.getBottom() + 12;
        const int litDot = bank.currentIsFactory() ? bank.getCurrentIndex() : -1;
        for (int i = 0; i < dotCount; ++i)
        {
            g.setColour (i == litDot ? tokens::cyan : tokens::muted2.withAlpha (0.55f));
            g.fillEllipse ((float) (dx - 2), (float) (dy - 2), 4.5f, 4.5f);
            dx += dotSpacing;
        }

        // --- dividers between utility groups ------------------------------
        g.setColour (tokens::stroke);
        g.fillRect (local ({ 1272, 30, 1, 36 }));
        g.fillRect (local ({ 1406, 30, 1, 36 }));
    }

private:
    // Chevron nav buttons drawn on the supplied nav skin.
    class NavButton : public SkinButton
    {
    public:
        NavButton (const juce::String& name, bool forwardIn)
            : SkinButton (name, "preset_nav_60x52"), forward (forwardIn) {}

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            SkinButton::paintButton (g, over, down);

            const auto tint = over || down ? tokens::white : tokens::text;
            if (auto* ic = Assets::icon (forward ? "chevron_right" : "chevron_left", tint))
            {
                ic->drawWithin (g, getLocalBounds().withSizeKeepingCentre (16, 16).toFloat(),
                                juce::RectanglePlacement::centred, 1.0f);
                return;
            }

            const auto c = getLocalBounds().getCentre().toFloat();
            juce::Path p;
            const float d = forward ? 1.0f : -1.0f;
            p.startNewSubPath (c.x - 4.0f * d, c.y - 7.0f);
            p.lineTo (c.x + 4.0f * d, c.y);
            p.lineTo (c.x - 4.0f * d, c.y + 7.0f);
            g.setColour (tint);
            g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        }

    private:
        bool forward;
    };

    void savePresetPrompt()
    {
        auto* editor = new juce::AlertWindow ("Save Preset",
                                              "Name this preset:",
                                              juce::MessageBoxIconType::NoIcon);
        auto& bank = processor.getPresets();
        editor->addTextEditor ("name", bank.currentIsFactory() ? juce::String ("My Preset")
                                                               : bank.currentName(), {});
        editor->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        editor->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        editor->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, editor] (int result)
            {
                const auto name = editor->getTextEditorContents ("name").trim();
                std::unique_ptr<juce::AlertWindow> owned (editor);
                if (result == 1 && name.isNotEmpty())
                {
                    processor.getPresets().saveCurrentAs (name);
                    repaint();
                }
            }), false);
    }

    void timerCallback() override
    {
        // Coalesce parameter moves into one undoable step per interval, and
        // keep the undo/redo buttons + modified dot current.
        processor.getPresets().captureUndoPoint();

        auto& um = processor.getUndoManager();
        undoButton.setEnabled (um.canUndo());
        redoButton.setEnabled (um.canRedo());

        const bool modified = processor.getPresets().isModified();
        if (modified != shownModified)
        {
            shownModified = modified;
            repaint();
        }
    }

    void showSettingsMenu()
    {
        juce::PopupMenu m;
        m.setLookAndFeel (&getLookAndFeel());
        m.addItem (1, "Reduce Motion", true, processor.getReduceMotion());
        m.addItem (2, "Low Power Mode (30 fps)", true, processor.getLowPowerMode());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsButton),
                         [safe = juce::Component::SafePointer<HeaderComponent> (this)] (int result)
                         {
                             if (safe == nullptr || result == 0)
                                 return;
                             auto& p = safe->processor;
                             if (result == 1) p.setReduceMotion (! p.getReduceMotion());
                             if (result == 2) p.setLowPowerMode (! p.getLowPowerMode());
                             safe->onSettingsChanged();
                         });
    }

    KeyGloProcessor& processor;

    NavButton prevButton { "Previous Preset", false };
    NavButton nextButton { "Next Preset", true };

    IconButton saveButton     { "Save",     "save",     "Save" };
    IconButton settingsButton { "Settings", "settings", "Settings" };
    IconButton helpButton     { "Help",     "help",     "Help" };
    IconButton undoButton     { "Undo",     "undo",     "Undo" };
    IconButton redoButton     { "Redo",     "redo",     "Redo" };
    IconButton powerButton    { "Power",    "power",    {}, tokens::cyan, tokens::cyan };
    std::unique_ptr<juce::ParameterAttachment> powerAttachment;
    bool shownModified = false;
};

} // namespace keyglo
