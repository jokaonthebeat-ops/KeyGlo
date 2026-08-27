#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

struct AnalysisDisplaySnapshot
{
    std::string key = "F#";
    std::string scale = "Minor";
    float bpm = 148.0f;
    float tuningCents = -4.0f;
    float keyConfidence = 0.92f;
    float artistFit = 0.89f;
    float rangeFit = 0.85f;
    float hookMatch = 0.91f;
    int recommendedTranspose = -2;
    std::string newKey = "E";
    std::string newScale = "Minor";
    std::array<float, 12> chroma { 0.22f, 0.87f, 0.69f, 0.18f, 0.73f, 0.21f,
                                   1.0f, 0.76f, 0.70f, 0.19f, 0.17f, 0.31f };
    std::vector<float> spectrum;
    std::vector<float> pitchHistoryMidi;
};

class AnalysisDisplayModel
{
public:
    void publish (std::shared_ptr<const AnalysisDisplaySnapshot> next)
    {
        std::atomic_store (&snapshot, std::move (next));
    }

    std::shared_ptr<const AnalysisDisplaySnapshot> get() const
    {
        return std::atomic_load (&snapshot);
    }

private:
    mutable std::shared_ptr<const AnalysisDisplaySnapshot> snapshot;
};
