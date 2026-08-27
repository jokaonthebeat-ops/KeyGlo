/*
    Assets.h - loads and caches every piece of supplied artwork.

    The pack's art is the product's art (see LOGO_USAGE_GUIDE.md and the
    standing rule: never substitute generated stand-ins). When a file fails to
    load the UI draws an obvious flat fallback and the failure is recorded so
    `make uishot` can report it - a wrong-looking UI is a *load* problem first.

    Two corrections applied at load, both measured off this pack on 2026-08-26:

    * The 128-frame knob strips are rotated +90 degrees from standard JUCE
      rotary orientation (frame 64 points east instead of north) - the same
      defect SourceGlo Pro's pack shipped with. Each frame gets one exact
      90-degree anticlockwise pixel transpose, which also restores the
      top-lit bezel the approved mockup shows.

    * The header logo exports carry transparent margin (155x44 of art in the
      250x58 canvas), so they are cropped to their opaque bounds at load and
      aspect-fitted into the layout box - drawing the raw canvas 1:1 renders
      the wordmark ~20 % small (the MasterGlo / Drum King trap).
*/

#pragma once
#include <JuceHeader.h>

namespace keyglo
{

enum class ButtonState { normal, hover, down, disabled };
enum class Accent { cyan, violet, gold };

struct Assets
{
    // --- diagnostics ------------------------------------------------------
    static int loadFailureCount();
    static juce::String describeFailures();
    static juce::File assetsDirectory();

    // --- base -------------------------------------------------------------
    static juce::Image shell();          // 1491 x 1055
    static juce::Image shell2x();        // 2982 x 2110, for scales > 1

    // --- brand ------------------------------------------------------------
    // Header lockup, cropped to opaque bounds at load. Draw aspect-fitted
    // into layout::logo, anchored left-centre.
    static juce::Image logoHeader (float scale);   // 1x / 2x export
    static juce::Image premiumMark();              // circular emblem, 512

    // --- HUD --------------------------------------------------------------
    static juce::Image keyWheelBase();   // 1024 sq rings/ticks/glass
    static juce::Image centerGlass();    // 640 sq
    static juce::Image scorePod (Accent a);           // 256 sq
    static juce::Image noteNodeActive (Accent a);     // 128 sq (cyan / violet)
    static juce::Image noteNodeInactive();            // 128 sq
    static juce::Image particleGlow (Accent a);       // 128 sq soft dot

    // --- controls ---------------------------------------------------------
    struct Filmstrip
    {
        std::vector<juce::Image> frames;  // sliced at load; never draw the strip
        int frameSize = 0;
        bool isValid() const { return ! frames.empty(); }
        const juce::Image& frameFor (float norm01) const
        {
            const int i = juce::jlimit (0, (int) frames.size() - 1,
                                        juce::roundToInt (norm01 * (float) (frames.size() - 1)));
            return frames[(size_t) i];
        }
    };

    static const Filmstrip& macroKnob (Accent a);  // 160 px frames, cyan/gold
    static const Filmstrip& smallKnob();           // 96 px frames

    // Button skins by base name, e.g. skin("apply_tune_219x31", state) ->
    // Controls/buttons/apply_tune_219x31_down.png. Skins contain no text;
    // labels/icons are drawn live (CONTROL_ASSET_NOTES.md).
    static juce::Image buttonSkin (const juce::String& base, ButtonState state);

    static juce::Image toggle (bool on, Accent a);       // 68x32
    static juce::Image meterTrough();                    // 78x124 stereo trough
    static juce::Image meterSegment (int zone);          // 0 cyan 1 violet 2 gold 3 red, 12x5
    static juce::Image confidenceTrack();                // 180x8
    static juce::Image confidenceFill();                 // 180x8

    // --- cards ------------------------------------------------------------
    static juce::Image analysisRow (int state);          // 0 normal 1 hover 2 selected, 343x32
    static juce::Image dropZone (int state);             // 0 normal 1 hover 2 active, 343x60
    static juce::Image panelHeader();                    // 440x42
    static juce::Image readoutCell (Accent a);           // 124x60
    static juce::Image transposeResult (bool highlight); // 354x116

    // --- visualizers ------------------------------------------------------
    static juce::Image artistRangeGrid();  // 690x330
    static juce::Image noteMapPiano();     // 720x110 horizontal
    static juce::Image verticalPiano();    // 88x440
    static juce::Image spectrumGrid();     // 720x220
    static juce::Image waveformGrid();     // 640x220
    static juce::Image tunerDial();        // 512 sq gold

    // --- icons ------------------------------------------------------------
    // Supplied SVGs, recoloured by rewriting stroke/fill attributes before
    // parsing. Cached per (name, colour). Never parsed during paint.
    static juce::Drawable* icon (const juce::String& name, juce::Colour tint);

private:
    static juce::Image load (const juce::String& relativePath);
};

} // namespace keyglo
