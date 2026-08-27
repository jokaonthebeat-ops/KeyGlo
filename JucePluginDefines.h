/*
    KeyGlo - Artist-to-Beat Key Matching

    Plugin identity. Normally the Projucer or JUCE's CMake API generates these
    defines; this project is built by a hand-written Makefile, so they live here
    and are force-included into every translation unit via -include.

    NOTE: editing this file rebuilds every JUCE object.

    Manufacturer code is 'DmLz', matching the rest of the Diamond Loopz
    catalogue - an AU host groups plugins by manufacturer code, so anything
    else would file KeyGlo under a second, empty "Diamond Loopz" entry.

    Plugin code 'KGlo' is unique across the catalogue (Bay2LA, MelodyGlo,
    The Drum King, MasterGlo 'MGPr', EQGlo, VoxGlo, SourceGlo 'SGPr').
    Changeable until release; permanent after it.
*/

#pragma once

#define JucePlugin_Name                     "KeyGlo"
#define JucePlugin_Desc                     "Artist-to-Beat Key Matching"
#define JucePlugin_Manufacturer             "Diamond Loopz"
#define JucePlugin_ManufacturerWebsite      "https://diamondloopz.com"
#define JucePlugin_ManufacturerEmail        ""
#define JucePlugin_ManufacturerCode         0x446d4c7a  // 'DmLz'
#define JucePlugin_PluginCode               0x4b476c6f  // 'KGlo'

#define JucePlugin_IsSynth                  0
#define JucePlugin_WantsMidiInput           0
#define JucePlugin_ProducesMidiOutput       0
#define JucePlugin_IsMidiEffect             0
#define JucePlugin_EditorRequiresKeyboardFocus 0

#define JucePlugin_Version                  0.9.5
#define JucePlugin_VersionString            "0.9.5"
#define JucePlugin_VersionCode              0x905

#define JucePlugin_VSTUniqueID              JucePlugin_PluginCode
#define JucePlugin_VSTCategory              kPlugCategAnalysis
#define JucePlugin_Vst3Category             "Fx|Analyzer|Tools"

#define JucePlugin_AUMainType               kAudioUnitType_Effect
#define JucePlugin_AUSubType                JucePlugin_PluginCode
#define JucePlugin_AUManufacturerCode       JucePlugin_ManufacturerCode
#define JucePlugin_AUExportPrefix           KeyGloAU
#define JucePlugin_AUExportPrefixQuoted     "KeyGloAU"

#define JucePlugin_CFBundleIdentifier       com.diamondloopz.keyglo

#define JucePlugin_VSTNumMidiInputs         0
#define JucePlugin_VSTNumMidiOutputs        0

#define JucePlugin_Enable_ARA               0
