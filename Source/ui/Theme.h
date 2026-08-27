/*
    Theme.h - colour tokens, font resolution and the design-space layout.

    Every rectangle is in the approved 1491 x 1055 design coordinate system
    (Spec/KeyGlo_UI_Assets_v1.0/08_LAYOUT/layout_1491x1055.json). The editor
    scales one content component uniformly, so nothing here ever needs
    runtime scaling maths.

    NOTE: juce::Rectangle has no constexpr constructor in JUCE 9, so these
    tables are `inline const`, not `constexpr` (same trap as MasterGlo Pro).
*/

#pragma once
#include <JuceHeader.h>

namespace keyglo
{

// Set by the headless tools (make uishot) so display timers keep updating
// without a visible window peer; isShowing() is false headlessly.
inline bool& headlessRefreshMode()
{
    static bool mode = false;
    return mode;
}

// Demo display mode: the beat panel/wheel run from the DemoFeed and the
// contract dataset instead of live analysis - used by uishot for the
// approved-reference overlay and marketing shots. Production instances never
// set this; they show honest "--" fields until the engine has real results.
inline bool& demoDisplayMode()
{
    static bool mode = false;
    return mode;
}

// -----------------------------------------------------------------------------
//  Colour tokens - Spec/.../08_LAYOUT/color_tokens.json, verbatim.
// -----------------------------------------------------------------------------
namespace tokens
{
    inline const juce::Colour bg0        { 0xff03060a };   // background
    inline const juce::Colour bg1        { 0xff070b10 };   // background2
    inline const juce::Colour panel      { 0xff0a1017 };
    inline const juce::Colour panel2     { 0xff0d151d };
    inline const juce::Colour panel3     { 0xff101923 };
    inline const juce::Colour stroke     { 0xff25313c };
    inline const juce::Colour strokeHi   { 0xff344653 };
    inline const juce::Colour text       { 0xffe7ebef };
    inline const juce::Colour muted      { 0xff929ea8 };
    inline const juce::Colour muted2     { 0xff596772 };
    inline const juce::Colour cyan       { 0xff22ddff };
    inline const juce::Colour cyan2      { 0xff00a9e8 };
    inline const juce::Colour violet     { 0xffa75cff };
    inline const juce::Colour purple     { 0xff7c42ff };
    inline const juce::Colour magenta    { 0xffd95bff };
    inline const juce::Colour gold       { 0xfff4c14d };
    inline const juce::Colour gold2      { 0xffd9972d };
    inline const juce::Colour green      { 0xff4ce486 };
    inline const juce::Colour red        { 0xffff5c70 };
    inline const juce::Colour white      { 0xfff5f7fa };
}

// -----------------------------------------------------------------------------
//  Fonts - system lookup only, no bundled files (typography.md).
//  Preferred order: Inter Display, Inter, SF Pro Display, Segoe UI, Arial.
//  Resolved once per process.
// -----------------------------------------------------------------------------
struct Fonts
{
    static const juce::String& family()
    {
        static const juce::String resolved = []
        {
            const juce::StringArray installed = juce::Font::findAllTypefaceNames();
            for (const char* want : { "Inter Display", "Inter", "SF Pro Display",
                                      "Segoe UI", "Helvetica Neue", "Arial" })
                if (installed.contains (juce::String (want)))
                    return juce::String (want);
            return juce::Font (juce::FontOptions{}).getTypefaceName();
        }();
        return resolved;
    }

    // medium ~ weight 500, bold ~ 600-700. System lookup only: a family that
    // ships a real Medium style gets it, otherwise the nearest of plain/bold.
    static juce::Font make (float px, bool medium = false, bool bold = false)
    {
        if (bold)
            return juce::Font (juce::FontOptions (family(), px, juce::Font::bold));

        if (medium)
        {
            juce::Font f (juce::FontOptions (family(), "Medium", px));
            if (f.getTypefacePtr() != nullptr && f.getTypefaceStyle() == "Medium")
                return f;
        }
        return juce::Font (juce::FontOptions (family(), px, juce::Font::plain));
    }

    // Typography roles (typography.md). Sizes are design-space pixels.
    static juce::Font panelTitle()   { return make (18.0f, false, true).withExtraKerningFactor (0.05f); }
    static juce::Font rowLabel()     { return make (13.0f, false, true).withExtraKerningFactor (0.05f); }
    static juce::Font rowValue()     { return make (15.0f, true); }
    static juce::Font small()        { return make (11.0f, true).withExtraKerningFactor (0.04f); }
    static juce::Font fieldLabel()   { return make (12.0f, false, true).withExtraKerningFactor (0.06f); }
    static juce::Font centerKey()    { return make (38.0f, false, true).withExtraKerningFactor (0.01f); }
    static juce::Font centerScore()  { return make (50.0f, false, true); }
    static juce::Font podScore()     { return make (30.0f, false, true); }
    static juce::Font primaryValue() { return make (24.0f, false, true); }
    static juce::Font buttonLabel()  { return make (14.0f, false, true).withExtraKerningFactor (0.03f); }
    static juce::Font footer()       { return make (12.0f, true).withExtraKerningFactor (0.05f); }
};

// -----------------------------------------------------------------------------
//  Layout - primary bounds from layout_1491x1055.json (the declared authority
//  for panel-level bounds), plus internal geometry measured from the approved
//  mockup where the JSON has no entry.
// -----------------------------------------------------------------------------
struct Design
{
    static constexpr int width  = 1491;
    static constexpr int height = 1055;
    static constexpr float aspect = 1491.0f / 1055.0f;
    static constexpr int minWidth  = 1044;   // responsive_rules.md, 70 % scale
    static constexpr int minHeight = 739;
    static constexpr int maxWidth  = 2237;   // 150 %
    static constexpr int maxHeight = 1583;
};

namespace layout
{
    using R = juce::Rectangle<int>;

    // --- primary bounds: layout_1491x1055.json, verbatim ------------------
    inline const R header           {    2,   2, 1487,  83 };
    inline const R logo             {   35,  17,  250,  58 };
    // Preset cluster measured off the mockup - the JSON sits 22 px left of it.
    inline const R presetPrev       {  492,  18,   60,  52 };   // json: 470
    inline const R presetName       {  552,  18,  338,  52 };   // json: 530
    inline const R presetNext       {  890,  18,   56,  52 };   // json: 868
    inline const R headerUtilities  { 1018,  13,  424,  60 };

    inline const R beatPanel        {   15,  91,  360, 460 };
    inline const R heroPanel        {  383,  91,  641, 460 };
    inline const R artistPanel      { 1033,  91,  443, 460 };

    inline const R keyWheel         {  435, 103,  525, 438 };
    inline const R keyConfidencePod {  392, 113,  122, 122 };
    inline const R rangeFitPod      {  906, 138,  116, 116 };
    inline const R hookMatchPod     {  887, 430,  120, 120 };

    inline const R beatRows         {   23, 133,  343, 165 };
    inline const R beatSpectrum     {   23, 310,  343,  86 };
    inline const R beatDropZone     {   23, 405,  343,  60 };
    inline const R beatNoteMap      {   23, 472,  343,  69 };

    inline const R artistCurrentNote{ 1073, 146,  182,  61 };   // json: 1065,137
    inline const R artistPiano      { 1075, 207,   58, 276 };
    inline const R artistRangeGraph { 1136, 207,  292, 276 };
    inline const R artistStartRange { 1058, 494,  177,  42 };
    inline const R artistSaveProfile{ 1244, 494,  169,  42 };

    inline const R autotunePanel    {   15, 559,  313, 270 };
    inline const R transposePanel   {  329, 559,  695, 270 };
    inline const R samplePanel      { 1033, 559,  443, 270 };

    // Measured off the approved mockup - layout_1491x1055.json drifts 10-20 px
    // from the reference here (the declared visual authority), same as the
    // SourceGlo pack. JSON values kept in comments for the audit trail.
    inline const R transposeButtons {  367, 608,  655,  49 };   // json: 350,596
    // Sits exactly on the shell's well (borders measured off the shell art at
    // {399,680,351,113}); both the JSON (397,678) and the mockup card (414,689)
    // disagree with the chassis they ship with.
    inline const R transposeResult  {  399, 680,  352, 113 };
    inline const R compareA         {  812, 697,   72,  91 };   // json: 786,681
    inline const R compareB         {  896, 697,   84,  91 };   // json: 870,681

    inline const R autotuneNoteChips{   28, 717,  282,  28 };   // json: 708
    inline const R autotuneKeyboard {   28, 752,  282,  44 };   // json: 744,h39
    inline const R copyScale        {   28, 796,  282,  31 };   // json: 790

    inline const R sampleWaveform   { 1046, 606,  244,  99 };
    inline const R sampleTuner      { 1305, 585,  134, 134 };   // json: 1297,579
    inline const R sampleReadouts   { 1047, 727,  383,  60 };   // json: y719
    inline const R applyTune        { 1046, 790,  219,  31 };
    inline const R sampleSolo       { 1274, 790,  157,  31 };

    inline const R macroPanel       {   15, 835, 1461, 142 };
    // Knob centres measured off the mockup's value readouts: x = 150, 374,
    // 592, 810, 1028, 1244 (uniform 218 px pitch), centre y = 924. The JSON's
    // knob row (x 86/299/512/724/939/1150, y 849) sits up to 31 px left and
    // 12 px high of the approved art.
    inline const R rangeSenseKnob   {   87, 861,  126, 126 };
    inline const R keySenseKnob     {  311, 861,  126, 126 };
    inline const R smoothKnob       {  529, 861,  126, 126 };
    inline const R previewMixKnob   {  747, 861,  126, 126 };
    inline const R fineTuneKnob     {  965, 861,  126, 126 };
    inline const R outputKnob       { 1181, 861,  126, 126 };
    inline const R outputMeter      { 1363, 853,   78, 124 };   // json: 1345,850

    inline const R footer           {    2, 981, 1487,  72 };
}

// -----------------------------------------------------------------------------
//  Note names + the wheel angle table (03_HUD/note_positions.json: C at the
//  top, chromatic clockwise, radius 350 in the 1024 canvas).
// -----------------------------------------------------------------------------
namespace notes
{
    inline const char* names[12] = { "C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B" };

    // Degrees, 0 = east, positive clockwise (screen-space y down).
    inline const float wheelAngleDegrees[12] = { -90.0f, -60.0f, -30.0f, 0.0f,
                                                 30.0f, 60.0f, 90.0f, 120.0f,
                                                 150.0f, 180.0f, 210.0f, 240.0f };
    inline constexpr float wheelRadiusNorm = 350.0f / 1024.0f;  // of wheel canvas size

    inline juce::String midiNoteName (int midi)
    {
        return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
    }
}

} // namespace keyglo
