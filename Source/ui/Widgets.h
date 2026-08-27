/*
    Widgets.h - the small controls every panel shares.

    All of them draw the supplied artwork (Assets) and fall back to flat
    token-coloured shapes only when a file failed to load - visibly, so a
    missing asset reads as the load problem it is. Button skins carry no
    text (CONTROL_ASSET_NOTES.md); labels and icons are drawn live.
*/

#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "../Assets.h"

namespace keyglo
{

// -----------------------------------------------------------------------------
//  Hover/press animation helper - 120 ms hover fade + click ripple
//  (animation_tokens.json buttonHover / clickRipple).
// -----------------------------------------------------------------------------
struct HoverGlow
{
    float amount = 0.0f;          // 0..1 hover fade
    float ripple = -1.0f;         // 0..1 active ripple progress, <0 idle
    juce::Point<float> rippleCentre;

    // Returns true while something is still animating.
    bool tick (bool over, double dt)
    {
        const float target = over ? 1.0f : 0.0f;
        const float step = (float) (dt * 1000.0 / 120.0);
        bool moving = false;

        if (std::abs (target - amount) > 0.001f)
        {
            amount = juce::jlimit (0.0f, 1.0f, amount + (target > amount ? step : -step));
            moving = true;
        }
        if (ripple >= 0.0f)
        {
            ripple += (float) (dt * 1000.0 / 260.0);
            if (ripple >= 1.0f) ripple = -1.0f;
            moving = true;
        }
        return moving;
    }

    void startRipple (juce::Point<float> centre) { ripple = 0.0f; rippleCentre = centre; }

    void drawRipple (juce::Graphics& g, juce::Colour accent) const
    {
        if (ripple < 0.0f)
            return;
        const float r = 6.0f + ripple * 42.0f;
        g.setColour (accent.withAlpha ((1.0f - ripple) * 0.35f));
        g.drawEllipse (rippleCentre.x - r, rippleCentre.y - r, r * 2.0f, r * 2.0f, 2.0f);
    }
};

// -----------------------------------------------------------------------------
//  SkinButton - normal/hover/down/disabled from the supplied button art,
//  with an optional live-drawn label and icon.
// -----------------------------------------------------------------------------
class SkinButton : public juce::Button, private juce::Timer
{
public:
    SkinButton (const juce::String& name, const juce::String& skinBaseIn,
                const juce::String& labelIn = {}, const juce::String& iconIn = {})
        : juce::Button (name), skinBase (skinBaseIn), label (labelIn), iconName (iconIn)
    {
        setTitle (name);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setSkinBase (const juce::String& base)  { skinBase = base; repaint(); }
    void setLabel (const juce::String& l)        { label = l; repaint(); }
    void setLabelColour (juce::Colour c)         { labelTint = c; repaint(); }
    void setIconTint (juce::Colour c)            { iconTint = c; repaint(); }
    void setFontHeight (float h)                 { fontHeight = h; repaint(); }
    void setAccent (juce::Colour c)              { accent = c; }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        // keyglo::ButtonState, not the juce::Button member type of the same
        // name that unqualified lookup finds first in a Button subclass.
        const auto state = ! isEnabled()               ? keyglo::ButtonState::disabled
                         : (down || getToggleState()) ? keyglo::ButtonState::down
                         : over                       ? keyglo::ButtonState::hover
                                                      : keyglo::ButtonState::normal;

        auto art = Assets::buttonSkin (skinBase, state);
        const auto r = getLocalBounds().toFloat();

        if (art.isValid())
        {
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (art, r, juce::RectanglePlacement::stretchToFit);
        }
        else
        {
            g.setColour (tokens::panel3);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (tokens::stroke);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        }

        // Soft hover glow on top of the art.
        if (glow.amount > 0.001f)
        {
            g.setColour (accent.withAlpha (glow.amount * 0.10f));
            g.fillRoundedRectangle (r.reduced (1.0f), 6.0f);
        }
        glow.drawRipple (g, accent);

        auto content = getLocalBounds();
        const auto font = Fonts::buttonLabel().withHeight (fontHeight);
        const int textW = label.isNotEmpty()
                            ? (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, label)) : 0;
        const int iconSz = juce::jmin (18, getHeight() - 12);
        const int gap    = (iconName.isNotEmpty() && textW > 0) ? 9 : 0;
        const int total  = textW + gap + (iconName.isNotEmpty() ? iconSz : 0);
        int x = content.getCentreX() - total / 2;

        if (auto* ic = iconName.isNotEmpty() ? Assets::icon (iconName, iconTint) : nullptr)
        {
            ic->drawWithin (g, juce::Rectangle<float> ((float) x,
                            (float) (content.getCentreY() - iconSz / 2),
                            (float) iconSz, (float) iconSz),
                            juce::RectanglePlacement::centred,
                            isEnabled() ? 1.0f : 0.4f);
            x += iconSz + gap;
        }

        if (label.isNotEmpty())
        {
            g.setColour (labelTint.withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));
            g.setFont (font);
            g.drawText (label, x, content.getY(), textW + 4, content.getHeight(),
                        juce::Justification::centredLeft);
        }
    }

    void mouseEnter (const juce::MouseEvent& e) override { juce::Button::mouseEnter (e); startAnim(); }
    void mouseExit  (const juce::MouseEvent& e) override { juce::Button::mouseExit (e);  startAnim(); }
    void mouseDown  (const juce::MouseEvent& e) override
    {
        glow.startRipple (e.position);
        startAnim();
        juce::Button::mouseDown (e);
    }

private:
    void startAnim() { if (! isTimerRunning()) startTimerHz (60); }

    void timerCallback() override
    {
        if (! glow.tick (isMouseOver(), 1.0 / 60.0))
            stopTimer();
        repaint();
    }

    juce::String skinBase, label, iconName;
    juce::Colour labelTint = tokens::text;
    juce::Colour iconTint  = tokens::text;
    juce::Colour accent    = tokens::cyan;
    float fontHeight = 14.0f;
    HoverGlow glow;
};

// -----------------------------------------------------------------------------
//  IconButton - a tinted SVG with hover treatment and an optional caption
//  below (header SAVE / SETTINGS / HELP...), or a circled power button.
// -----------------------------------------------------------------------------
class IconButton : public juce::Button
{
public:
    IconButton (const juce::String& name, const juce::String& iconIn,
                const juce::String& captionIn = {},
                juce::Colour tintIn = tokens::muted, juce::Colour activeIn = tokens::white)
        : juce::Button (name), iconName (iconIn), caption (captionIn),
          tint (tintIn), active (activeIn)
    {
        setTitle (name);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setCircled (bool c, juce::Colour ring)  { circled = c; ringColour = ring; repaint(); }
    void setIconPadding (float p)                { padding = p; repaint(); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const bool lit = over || down || getToggleState();
        auto area = getLocalBounds().toFloat();

        auto iconArea = area;
        if (caption.isNotEmpty())
            iconArea = area.withTrimmedBottom (14.0f);

        if (circled)
        {
            const float d = juce::jmin (iconArea.getWidth(), iconArea.getHeight());
            auto circle = iconArea.withSizeKeepingCentre (d, d).reduced (1.5f);

            if (lit)
            {
                g.setColour (ringColour.withAlpha (0.18f));
                g.fillEllipse (circle.expanded (2.0f));
            }
            g.setColour (ringColour.withAlpha (lit ? 1.0f : 0.75f));
            g.drawEllipse (circle, 1.6f);
        }

        if (auto* ic = Assets::icon (iconName, lit ? active : tint))
            ic->drawWithin (g, iconArea.reduced (padding),
                            juce::RectanglePlacement::centred,
                            isEnabled() ? (down ? 0.8f : 1.0f) : 0.35f);

        if (caption.isNotEmpty())
        {
            g.setColour (lit ? tokens::text : tokens::muted);
            g.setFont (Fonts::make (10.0f, true).withExtraKerningFactor (0.06f));
            g.drawText (caption.toUpperCase(), getLocalBounds().removeFromBottom (13),
                        juce::Justification::centred);
        }
    }

private:
    juce::String iconName, caption;
    juce::Colour tint, active, ringColour = tokens::cyan;
    float padding = 3.0f;
    bool circled = false;
};

// -----------------------------------------------------------------------------
//  FilmstripKnob - the supplied 128-frame strips, sliced and de-rotated at
//  load. Cyan for most macros, gold for Fine Tune.
// -----------------------------------------------------------------------------
class FilmstripKnob : public juce::Slider
{
public:
    FilmstripKnob (const juce::String& name, Accent accentIn)
        : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox),
          accent (accentIn)
    {
        setName (name);
        setTitle (name);
        setMouseDragSensitivity (240);                    // slow enough for fine control
        setVelocityModeParameters (1.0, 1, 0.06, true);   // shift = fine adjust
        setDoubleClickReturnValue (true, 0.0);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& strip = Assets::macroKnob (accent);
        const auto bounds = getLocalBounds().toFloat();
        const float size  = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const juce::Rectangle<float> target (bounds.getCentreX() - size * 0.5f,
                                             bounds.getCentreY() - size * 0.5f, size, size);

        if (strip.isValid())
        {
            const auto norm = (float) valueToProportionOfLength (getValue());
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (strip.frameFor (norm), target, juce::RectanglePlacement::centred);
        }
        else
        {
            g.setColour (tokens::panel3);
            g.fillEllipse (target);
            g.setColour (tokens::stroke);
            g.drawEllipse (target.reduced (1.0f), 1.5f);
        }
    }

private:
    Accent accent;
};

// -----------------------------------------------------------------------------
//  StereoSegmentMeter - the 78x124 trough with live cyan/violet segment
//  columns for L and R. Levels in dBFS; UI applies release ballistics.
// -----------------------------------------------------------------------------
class StereoSegmentMeter : public juce::Component
{
public:
    StereoSegmentMeter() { setInterceptsMouseClicks (false, false); }

    void setLevels (float leftDb, float rightDb)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            const float dB = ch == 0 ? leftDb : rightDb;
            const float target = juce::jmap (juce::jlimit (-42.0f, 0.0f, dB),
                                             -42.0f, 0.0f, 0.0f, 1.0f);
            display[ch] = juce::jmax (display[ch] * 0.90f - 0.004f, target);
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        auto trough = Assets::meterTrough();

        if (trough.isValid())
            g.drawImage (trough, r, juce::RectanglePlacement::stretchToFit);
        else
        {
            g.setColour (tokens::bg1);
            g.fillRoundedRectangle (r, 4.0f);
        }

        // Two segment columns inside the trough. Segment art is 12x5; violet
        // takes the top third like the approved mockup's meter.
        const float colW = 12.0f, segH = 5.0f, segGap = 2.0f;
        const auto inner = r.reduced (10.0f, 8.0f);
        const int count = (int) ((inner.getHeight() + segGap) / (segH + segGap));

        for (int ch = 0; ch < 2; ++ch)
        {
            const float x = ch == 0 ? inner.getX() + 4.0f
                                    : inner.getRight() - colW - 4.0f;
            const int lit = juce::roundToInt (display[ch] * (float) count);

            for (int i = 0; i < lit && i < count; ++i)
            {
                const float y = inner.getBottom() - (float) (i + 1) * (segH + segGap) + segGap;
                const juce::Rectangle<float> seg (x, y, colW, segH);
                const float frac = (float) i / (float) count;

                auto art = Assets::meterSegment (frac > 0.95f ? 3 : frac > 0.62f ? 1 : 0);
                if (art.isValid())
                {
                    // drawImage uses the current fill colour's alpha as its
                    // opacity, so reset to opaque first (SourceGlo trap).
                    g.setColour (juce::Colours::white);
                    g.drawImage (art, seg, juce::RectanglePlacement::stretchToFit);
                }
                else
                {
                    g.setColour (frac > 0.95f ? tokens::red
                                              : frac > 0.62f ? tokens::violet : tokens::cyan);
                    g.fillRect (seg);
                }
            }
        }
    }

private:
    float display[2] { 0.0f, 0.0f };
};

// -----------------------------------------------------------------------------
//  ConfidenceBar - supplied 180x8 track + cyan fill.
// -----------------------------------------------------------------------------
class ConfidenceBar : public juce::Component
{
public:
    ConfidenceBar() { setInterceptsMouseClicks (false, false); }

    void setAmount (float a)
    {
        if (std::abs (a - amount) > 0.002f) { amount = a; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        auto track = Assets::confidenceTrack();
        auto fill  = Assets::confidenceFill();

        if (track.isValid())
            g.drawImage (track, r, juce::RectanglePlacement::stretchToFit);
        else
        {
            g.setColour (tokens::panel3);
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        }

        const float w = r.getWidth() * juce::jlimit (0.0f, 1.0f, amount);
        if (w < 2.0f)
            return;

        if (fill.isValid())
        {
            g.saveState();
            g.reduceClipRegion (juce::Rectangle<int> ((int) r.getX(), (int) r.getY(),
                                                      (int) w, (int) r.getHeight()));
            g.setColour (juce::Colours::white);
            g.drawImage (fill, r, juce::RectanglePlacement::stretchToFit);
            g.restoreState();
        }
        else
        {
            g.setColour (tokens::cyan);
            g.fillRoundedRectangle (r.withWidth (w), r.getHeight() * 0.5f);
        }
    }

private:
    float amount = 0.0f;
};

// -----------------------------------------------------------------------------
//  Panel title helper - icon + uppercase title at a panel's top strip.
// -----------------------------------------------------------------------------
inline void drawPanelTitle (juce::Graphics& g, juce::Rectangle<int> strip,
                            const juce::String& iconName, const juce::String& title,
                            juce::Colour iconTint)
{
    auto area = strip;
    if (auto* ic = Assets::icon (iconName, iconTint))
        ic->drawWithin (g, area.removeFromLeft (22).withSizeKeepingCentre (18, 18).toFloat(),
                        juce::RectanglePlacement::centred, 1.0f);
    area.removeFromLeft (10);
    g.setColour (tokens::text);
    g.setFont (Fonts::panelTitle());
    g.drawText (title.toUpperCase(), area, juce::Justification::centredLeft);
}

// -----------------------------------------------------------------------------
//  KeyGloLookAndFeel - popup menus and tooltips in the house style.
// -----------------------------------------------------------------------------
class KeyGloLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KeyGloLookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId, tokens::panel2);
        setColour (juce::PopupMenu::textColourId, tokens::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, tokens::panel3);
        setColour (juce::PopupMenu::highlightedTextColourId, tokens::cyan);
        setColour (juce::TooltipWindow::backgroundColourId, tokens::panel2);
        setColour (juce::TooltipWindow::textColourId, tokens::text);
        setColour (juce::TooltipWindow::outlineColourId, tokens::stroke);
    }

    juce::Font getPopupMenuFont() override  { return Fonts::rowValue(); }
};

} // namespace keyglo
