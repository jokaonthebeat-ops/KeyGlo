/*
    AnimationClock - the centralized visual update scheduler
    (09_JUCE_HANDOFF/AnimationClock.h, as supplied). One message-thread timer
    drives every animated component; nothing animates from the audio thread.
*/

#pragma once
#include <JuceHeader.h>

namespace keyglo
{

class AnimationClock : private juce::Timer
{
public:
    std::function<void (double)> onFrame;

    void start (int framesPerSecond = 60)
    {
        lastMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (juce::jlimit (15, 60, framesPerSecond));
    }

    void stop() { stopTimer(); }

private:
    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const double dt = juce::jlimit (0.0, 0.1, (now - lastMs) / 1000.0);
        lastMs = now;
        if (onFrame) onFrame (dt);
    }

    double lastMs = 0.0;
};

} // namespace keyglo
