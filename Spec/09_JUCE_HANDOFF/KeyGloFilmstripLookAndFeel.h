#pragma once
#include <JuceHeader.h>

class KeyGloFilmstripLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KeyGloFilmstripLookAndFeel (juce::Image filmstripImage, int numberOfFrames)
        : filmstrip (std::move (filmstripImage)), frames (numberOfFrames) {}

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float, float, juce::Slider&) override
    {
        if (! filmstrip.isValid() || frames <= 0)
            return;

        const int frameHeight = filmstrip.getHeight() / frames;
        const int frame = juce::jlimit (0, frames - 1,
                                        juce::roundToInt (sliderPos * (frames - 1)));

        g.drawImage (filmstrip,
                     x, y, width, height,
                     0, frame * frameHeight,
                     filmstrip.getWidth(), frameHeight,
                     false);
    }

private:
    juce::Image filmstrip;
    int frames = 128;
};
