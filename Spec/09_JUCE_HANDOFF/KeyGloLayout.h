#pragma once
#include <JuceHeader.h>

namespace keyglo
{
struct Design
{
    static constexpr float width = 1491.0f;
    static constexpr float height = 1055.0f;
};

struct Bounds
{
    static constexpr juce::Rectangle<float> header                   {    2,    2, 1487,   83 };
    static constexpr juce::Rectangle<float> logo                     {   35,   17,  250,   58 };
    static constexpr juce::Rectangle<float> presetBrowser            {  470,   18,  454,   52 };
    static constexpr juce::Rectangle<float> presetPrev               {  470,   18,   60,   52 };
    static constexpr juce::Rectangle<float> presetName               {  530,   18,  338,   52 };
    static constexpr juce::Rectangle<float> presetNext               {  868,   18,   56,   52 };
    static constexpr juce::Rectangle<float> headerUtilities          { 1018,   13,  424,   60 };
    static constexpr juce::Rectangle<float> beatPanel                {   15,   91,  360,  460 };
    static constexpr juce::Rectangle<float> heroPanel                {  383,   91,  641,  460 };
    static constexpr juce::Rectangle<float> artistPanel              { 1033,   91,  443,  460 };
    static constexpr juce::Rectangle<float> keyWheel                 {  435,  103,  525,  438 };
    static constexpr juce::Rectangle<float> keyConfidencePod         {  392,  113,  122,  122 };
    static constexpr juce::Rectangle<float> rangeFitPod              {  906,  138,  116,  116 };
    static constexpr juce::Rectangle<float> hookMatchPod             {  887,  430,  120,  120 };
    static constexpr juce::Rectangle<float> beatRows                 {   23,  133,  343,  165 };
    static constexpr juce::Rectangle<float> beatSpectrum             {   23,  310,  343,   86 };
    static constexpr juce::Rectangle<float> beatDropZone             {   23,  405,  343,   60 };
    static constexpr juce::Rectangle<float> beatNoteMap              {   23,  472,  343,   69 };
    static constexpr juce::Rectangle<float> artistCurrentNote        { 1065,  137,  182,   61 };
    static constexpr juce::Rectangle<float> artistVerticalPiano      { 1075,  207,   58,  276 };
    static constexpr juce::Rectangle<float> artistRangeGraph         { 1136,  207,  292,  276 };
    static constexpr juce::Rectangle<float> artistStartRange         { 1058,  494,  177,   42 };
    static constexpr juce::Rectangle<float> artistSaveProfile        { 1244,  494,  169,   42 };
    static constexpr juce::Rectangle<float> autotunePanel            {   15,  559,  313,  270 };
    static constexpr juce::Rectangle<float> transposePanel           {  329,  559,  695,  270 };
    static constexpr juce::Rectangle<float> samplePanel              { 1033,  559,  443,  270 };
    static constexpr juce::Rectangle<float> transposeButtons         {  350,  596,  655,   49 };
    static constexpr juce::Rectangle<float> transposeResult          {  397,  678,  354,  116 };
    static constexpr juce::Rectangle<float> compareA                 {  786,  681,   72,   91 };
    static constexpr juce::Rectangle<float> compareB                 {  870,  681,   84,   91 };
    static constexpr juce::Rectangle<float> autotuneNoteChips        {   28,  708,  282,   28 };
    static constexpr juce::Rectangle<float> autotuneKeyboard         {   28,  744,  282,   39 };
    static constexpr juce::Rectangle<float> copyScale                {   28,  790,  282,   31 };
    static constexpr juce::Rectangle<float> sampleWaveform           { 1046,  606,  244,   99 };
    static constexpr juce::Rectangle<float> sampleTuner              { 1297,  579,  134,  134 };
    static constexpr juce::Rectangle<float> sampleReadouts           { 1047,  719,  383,   60 };
    static constexpr juce::Rectangle<float> applyTune                { 1046,  790,  219,   31 };
    static constexpr juce::Rectangle<float> sampleSolo               { 1274,  790,  157,   31 };
    static constexpr juce::Rectangle<float> macroPanel               {   15,  835, 1461,  142 };
    static constexpr juce::Rectangle<float> rangeSenseKnob           {   86,  849,  126,  126 };
    static constexpr juce::Rectangle<float> keySenseKnob             {  299,  849,  126,  126 };
    static constexpr juce::Rectangle<float> smoothKnob               {  512,  849,  126,  126 };
    static constexpr juce::Rectangle<float> previewMixKnob           {  724,  849,  126,  126 };
    static constexpr juce::Rectangle<float> fineTuneKnob             {  939,  849,  126,  126 };
    static constexpr juce::Rectangle<float> outputKnob               { 1150,  849,  126,  126 };
    static constexpr juce::Rectangle<float> outputMeter              { 1345,  850,   78,  124 };
    static constexpr juce::Rectangle<float> footer                   {    2,  981, 1487,   72 };
};

inline juce::Rectangle<int> scaledRect (juce::Rectangle<float> designRect,
                                         juce::Rectangle<float> designArea,
                                         float scale)
{
    return juce::Rectangle<float> (designArea.getX() + designRect.getX() * scale,
                                   designArea.getY() + designRect.getY() * scale,
                                   designRect.getWidth() * scale,
                                   designRect.getHeight() * scale).toNearestInt();
}
}
