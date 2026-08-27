# Recommended JUCE Project Structure

```text
KeyGlo/
├── CMakeLists.txt
├── Assets/
│   └── KeyGlo_UI_Assets_v1.0/
├── Source/
│   ├── PluginProcessor.h/.cpp
│   ├── PluginEditor.h/.cpp
│   ├── UI/
│   │   ├── KeyGloAssetCache.h/.cpp
│   │   ├── HeaderComponent.h/.cpp
│   │   ├── BeatAnalysisPanel.h/.cpp
│   │   ├── KeyWheelHUDComponent.h/.cpp
│   │   ├── ScorePodComponent.h/.cpp
│   │   ├── ArtistRangePanel.h/.cpp
│   │   ├── PitchTrailComponent.h/.cpp
│   │   ├── AutoTuneSetupPanel.h/.cpp
│   │   ├── TransposePreviewPanel.h/.cpp
│   │   ├── SampleTunePanel.h/.cpp
│   │   ├── TunerDialComponent.h/.cpp
│   │   ├── MacroControlStrip.h/.cpp
│   │   └── FooterStatusComponent.h/.cpp
│   ├── Analysis/
│   │   ├── AnalysisCoordinator.h/.cpp
│   │   ├── BeatKeyDetector.h/.cpp
│   │   ├── PitchTracker.h/.cpp
│   │   ├── VocalRangeProfiler.h/.cpp
│   │   ├── HookFitScorer.h/.cpp
│   │   ├── SamplePitchDetector.h/.cpp
│   │   └── TempoDetector.h/.cpp
│   ├── DSP/
│   │   ├── PreviewPitchShifter.h/.cpp
│   │   ├── LoudnessMatchedAB.h/.cpp
│   │   └── Metering.h/.cpp
│   ├── State/
│   │   ├── ParameterIDs.h
│   │   ├── ArtistProfileStore.h/.cpp
│   │   └── PresetManager.h/.cpp
│   └── Utilities/
│       ├── LockFreeAudioFifo.h
│       ├── AnimationClock.h
│       └── AnalysisDisplayModel.h
└── Tests/
    ├── BeatKeyDetectorTests.cpp
    ├── PitchTrackerTests.cpp
    ├── HookFitScorerTests.cpp
    └── UIBoundsTests.cpp
```

Keep UI, analysis, DSP, file storage, and processor responsibilities separate. The editor should not own long-running analysis jobs directly.
