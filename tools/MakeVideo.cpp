// -----------------------------------------------------------------------------
//  Renders the KeyGlo demo film.
//
//    make video                          -> marketing/KeyGlo-demo.mp4
//    make video ARGS="path/to/loop.wav"     drives the analysis with real audio
//                                           and uses the plugin's processed
//                                           output as the soundtrack
//
//  Every frame is the real editor rendering real analysis. The parameter moves,
//  the Analyze presses and the Fix Source engagement are a scripted timeline
//  applied to the actual plugin, so the scores, diagnostics, spectrum and
//  rescue rows on screen are measurements of the signal being processed - not
//  an animation of what they would look like.
//
//  Encoding is AVAssetWriter + VideoToolbox: this machine has no ffmpeg and no
//  Homebrew to install one, but AVFoundation is in the SDK and writes a
//  standard H.264 mp4. (Ported from MasterGlo Pro's proven pipeline.)
//
//  With no audio file the render is SILENT on purpose - a fabricated song under
//  a product video is worse than none. The meters and the analysis still work,
//  because a generated drum bed is pushed through the chain.
// -----------------------------------------------------------------------------

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/Assets.h"
#include "../Source/ui/Theme.h"

#include <juce_audio_formats/juce_audio_formats.h>

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>

#include <array>
#include <cstdio>

using namespace keyglo;

static constexpr int fps = 30;

// Set at startup: 1920x1080 for the landscape film, 1080x1920 for the reel.
static int videoWidth = 1920;
static int videoHeight = 1080;
static bool reelMode = false;

// The editor renders at its native design canvas and is downscaled into the
// frame - crisper than rendering small, and the aspect never has to be guessed.
static constexpr int panelWidth = 1491;
static constexpr int panelHeight = 1055;

/*
    Reel layout. A 1.41:1 panel fitted to 1080 wide is only 764 of 1920 pixels
    tall, so a single centred panel would leave most of the frame empty.
    The panel stays on screen the whole time at the top, and the act's own
    region is blown up underneath - every pixel carries real interface.
*/
namespace reel
{
    inline constexpr float logoY = 96.0f,   logoH = 120.0f;
    inline constexpr float titleY = 236.0f, titleH = 68.0f;
    inline constexpr float panelY = 322.0f, panelW = 1044.0f, panelH = 739.0f;
    inline constexpr float captionY = 1078.0f, captionH = 62.0f;
    inline constexpr float detailY = 1156.0f,  detailH = 764.0f;
}

/*
    Draws a region of the rendered panel into a destination rectangle, scaled
    to COVER it - the crop overflows and is clipped rather than leaving bars.
    `focus` is in the editor's own 1491x1055 canvas units, so callers can name
    regions the way Layout.h does.
*/
static void drawFocus (juce::Graphics& g, const juce::Image& panel,
                       juce::Rectangle<float> focus, juce::Rectangle<float> dest,
                       float alpha)
{
    if (! panel.isValid() || alpha <= 0.01f || focus.isEmpty())
        return;

    const float toRender = (float) panel.getWidth() / 1491.0f;
    auto src = (focus * toRender).getIntersection (panel.getBounds().toFloat());
    if (src.isEmpty())
        return;

    const float scale = juce::jmax (dest.getWidth() / src.getWidth(),
                                    dest.getHeight() / src.getHeight());

    // Draw the WHOLE panel scaled up, positioned so the focus centre lands on
    // the destination centre, and let the clip crop. Drawing the image into a
    // rect the size of the scaled source instead just squeezes the entire
    // panel into the band, which looks like a duplicate of the shot above.
    auto whole = juce::Rectangle<float> ((float) panel.getWidth() * scale,
                                         (float) panel.getHeight() * scale)
                   .withPosition (dest.getCentreX() - src.getCentreX() * scale,
                                  dest.getCentreY() - src.getCentreY() * scale);

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (dest.toNearestInt());
    g.setOpacity (alpha);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (panel, whole, juce::RectanglePlacement::stretchToFit, false);
    g.setOpacity (1.0f);

    g.setColour (tokens::cyan.withAlpha (0.22f * alpha));
    g.drawRect (dest, 1.0f);
}

// --- helpers -----------------------------------------------------------------

struct Segment
{
    double start, end;
    juce::String title;
    juce::String caption;
    std::function<void (KeyGloProcessor&, KeyGloEditor&, double progress)> action;
    // Reel only: the part of the panel the detail band zooms into, in editor
    // canvas units. Empty means no detail band for this act.
    juce::Rectangle<float> focus {};
};

static float smoothstep (float t)
{
    t = juce::jlimit (0.0f, 1.0f, t);
    return t * t * (3.0f - 2.0f * t);
}

static float envelopeFor (double t, double start, double end, double in, double out)
{
    if (t < start || t > end)
        return 0.0f;
    const float rising = (float) juce::jlimit (0.0, 1.0, (t - start) / juce::jmax (1.0e-6, in));
    const float falling = (float) juce::jlimit (0.0, 1.0, (end - t) / juce::jmax (1.0e-6, out));
    return smoothstep (juce::jmin (rising, falling));
}

static void setParam (KeyGloProcessor& p, const char* id, float realValue)
{
    if (auto* param = p.getAPVTS().getParameter (id))
        param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f,
            dynamic_cast<juce::RangedAudioParameter*> (param)->convertTo0to1 (realValue)));
}

static void loadPreset (KeyGloProcessor& p, const juce::String& name)
{
    auto& bank = p.getPresets();
    const auto& all = bank.all();
    for (int i = 0; i < (int) all.size(); ++i)
        if (all[(size_t) i].name == name)
        {
            bank.select (i);
            return;
        }
}

// Runs once when a segment first becomes current, however many frames it spans.
static bool firstFrameOf (const Segment* seg, double progress)
{
    juce::ignoreUnused (seg);
    return progress < (1.0 / fps) / 1.0e-9 && progress <= 0.0001;
}

static void drawTracked (juce::Graphics& g, const juce::String& text,
                         juce::Rectangle<float> area, juce::Font font, float kerning)
{
    g.setFont (font.withExtraKerningFactor (kerning / juce::jmax (1.0f, font.getHeight())));
    g.drawText (text, area, juce::Justification::centred, false);
}

// --- audio -------------------------------------------------------------------

/*
    A kick-forward drum bed so the analysis has something this product is
    actually about: a kick on every beat, an 808 under it, hats between.
    Deliberately not presented as a soundtrack.
*/
class DemoSignal
{
public:
    explicit DemoSignal (double sampleRate) : sr (sampleRate) {}

    void render (juce::AudioBuffer<float>& buffer)
    {
        const int n = buffer.getNumSamples();
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);

        const double beat = 60.0 / 92.0;
        const double eighth = beat * 0.5;

        for (int i = 0; i < n; ++i)
        {
            const double t = (double) samplesDone / sr;
            const double intoBeat = std::fmod (t, beat);
            const double intoEighth = std::fmod (t, eighth);

            const double kickEnv = std::exp (-intoBeat * 22.0);
            kickPhase += 2.0 * juce::MathConstants<double>::pi
                           * (54.0 + 80.0 * kickEnv) / sr;
            const float kick = (float) (std::sin (kickPhase) * kickEnv * 0.80);

            const int bar = (int) (t / (beat * 2.0));
            const double subHz = (bar % 2 == 0) ? 51.9 : 69.3;
            subPhase += 2.0 * juce::MathConstants<double>::pi * subHz / sr;
            const float sub = (float) (std::sin (subPhase) * 0.26);

            const double hatEnv = std::exp (-intoEighth * 85.0);
            const float noise = random.nextFloat() * 2.0f - 1.0f;
            hatState += 0.55f * (noise - hatState);
            const float hat = (noise - hatState) * (float) hatEnv * 0.20f;

            airL += 0.02f * ((random.nextFloat() * 2.0f - 1.0f) - airL);
            airR += 0.02f * ((random.nextFloat() * 2.0f - 1.0f) - airR);

            l[i] = juce::jlimit (-1.0f, 1.0f, kick + sub + hat + airL * 1.1f);
            r[i] = juce::jlimit (-1.0f, 1.0f, kick + sub + hat * 0.9f + airR * 1.1f);
            ++samplesDone;
        }
    }

private:
    double sr, kickPhase = 0.0, subPhase = 0.0;
    juce::int64 samplesDone = 0;
    juce::Random random { 0x5061ce };
    float hatState = 0.0f, airL = 0.0f, airR = 0.0f;
};

// --- narration ---------------------------------------------------------------

/*
    Optional voice-over. build/vo/act-N.wav (48 kHz mono, produced by
    tools/make-narration.sh) is mixed in at act N's start, and the music is
    ducked underneath it. Missing files simply mean no narration - the film
    is complete without it.
*/
struct Narration
{
    struct Line { int act; juce::AudioBuffer<float> audio; };
    std::vector<Line> lines;
    double sr = 48000.0;

    void load (const juce::File& dir, double sampleRate)
    {
        sr = sampleRate;
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();

        for (int act = 1; act <= 12; ++act)
        {
            auto file = dir.getChildFile ("act-" + juce::String (act) + ".wav");
            if (! file.existsAsFile())
                continue;
            std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
            if (reader == nullptr || reader->lengthInSamples < 16)
                continue;

            // Resample by linear interpolation if the clip is not at the
            // film's rate - narration is speech, so this is inaudible.
            const double ratio = reader->sampleRate / sr;
            const int outLen = (int) std::llround ((double) reader->lengthInSamples / ratio);
            juce::AudioBuffer<float> raw ((int) reader->numChannels,
                                          (int) reader->lengthInSamples);
            reader->read (&raw, 0, (int) reader->lengthInSamples, 0, true, true);

            Line line;
            line.act = act;
            line.audio.setSize (1, outLen);
            for (int i = 0; i < outLen; ++i)
            {
                const double pos = i * ratio;
                const int i0 = (int) pos;
                const int i1 = juce::jmin (i0 + 1, raw.getNumSamples() - 1);
                const float frac = (float) (pos - i0);
                float v = 0.0f;
                for (int ch = 0; ch < raw.getNumChannels(); ++ch)
                    v += raw.getSample (ch, i0) + frac * (raw.getSample (ch, i1)
                                                            - raw.getSample (ch, i0));
                line.audio.setSample (0, i, v / (float) juce::jmax (1, raw.getNumChannels()));
            }
            lines.push_back (std::move (line));
        }
    }

    bool empty() const  { return lines.empty(); }

    // Mixes into `block` for the film time starting at `t`. Returns the music
    // duck gain (1 = untouched) so the caller can hold the beat back.
    float mixInto (juce::AudioBuffer<float>& block, double t,
                   const std::vector<double>& actStarts)
    {
        float duck = 1.0f;
        const int n = block.getNumSamples();

        for (const auto& line : lines)
        {
            if (line.act - 1 >= (int) actStarts.size())
                continue;
            const double start = actStarts[(size_t) (line.act - 1)] + 0.45;   // let the title land
            const double offset = t - start;
            const int len = line.audio.getNumSamples();
            if (offset < -0.2 || offset * sr > len)
                continue;

            for (int i = 0; i < n; ++i)
            {
                const juce::int64 idx = (juce::int64) std::llround ((offset + i / sr) * sr);
                if (idx < 0 || idx >= len)
                    continue;
                const float v = line.audio.getSample (0, (int) idx) * 1.25f;
                block.addSample (0, i, v);
                if (block.getNumChannels() > 1)
                    block.addSample (1, i, v);
            }
            duck = 0.42f;      // about -7.5 dB under the voice
        }
        return duck;
    }
};

// --- overlay drawing ---------------------------------------------------------

static void drawBackdrop (juce::Graphics& g)
{
    juce::Rectangle<float> full (0.0f, 0.0f, (float) videoWidth, (float) videoHeight);

    juce::ColourGradient bg (juce::Colour (0xff07131a), full.getCentreX(), full.getCentreY(),
                             juce::Colour (0xff02060a), full.getX(), full.getBottom(), true);
    g.setGradientFill (bg);
    g.fillRect (full);

    // The product's own cyan/gold bloom, low in the frame.
    juce::ColourGradient bloomL (tokens::cyan.withAlpha (0.13f), 340.0f, 1080.0f,
                                 juce::Colours::transparentBlack, 340.0f, 360.0f, true);
    g.setGradientFill (bloomL);
    g.fillRect (full);

    juce::ColourGradient bloomR (tokens::gold.withAlpha (0.10f), 1580.0f, 1080.0f,
                                 juce::Colours::transparentBlack, 1580.0f, 400.0f, true);
    g.setGradientFill (bloomR);
    g.fillRect (full);
}

static void drawCaption (juce::Graphics& g, const juce::String& text, float alpha,
                         juce::Rectangle<float> band, float fontHeight = 27.0f)
{
    if (text.isEmpty() || alpha <= 0.01f)
        return;

    const float ruleWidth = band.getWidth() * 0.40f * alpha;
    g.setColour (tokens::cyan.withAlpha (0.32f * alpha));
    g.fillRect (band.getCentreX() - ruleWidth * 0.5f, band.getY() - 13.0f, ruleWidth, 1.0f);

    g.setColour (tokens::text.withAlpha (0.94f * alpha));
    drawTracked (g, text, band, Fonts::make (fontHeight), fontHeight * 0.08f);
}

static void drawTitle (juce::Graphics& g, const juce::String& text, float alpha,
                       juce::Rectangle<float> area, float fontHeight, bool withScrim)
{
    if (text.isEmpty() || alpha <= 0.01f)
        return;

    if (withScrim)
    {
        g.setColour (juce::Colours::black.withAlpha (0.66f * alpha));
        g.fillRect (0.0f, area.getY() - 42.0f, (float) videoWidth, area.getHeight() + 84.0f);
    }

    g.setColour (tokens::white.withAlpha (alpha));
    drawTracked (g, text.toUpperCase(), area,
                 Fonts::make (fontHeight, false, true), fontHeight * 0.10f);
}

static void drawLogo (juce::Graphics& g, float alpha, float scale, float centreY)
{
    auto logo = Assets::logoHeader (2.0f);   // the v2 wordmark, opaque-cropped
    if (! logo.isValid() || alpha <= 0.01f)
        return;

    const float aspect = (float) logo.getWidth() / (float) logo.getHeight();
    const float w = (float) videoWidth * (reelMode ? 0.84f : 0.52f) * scale;
    const float h = w / aspect;

    auto target = juce::Rectangle<float> (w, h)
                    .withCentre ({ (float) videoWidth * 0.5f, centreY });

    g.setOpacity (alpha);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (logo, target, juce::RectanglePlacement::centred, false);
    g.setOpacity (1.0f);
}

static void drawMark (juce::Graphics& g, float alpha, float size, juce::Point<float> centre)
{
    auto mark = Assets::premiumMark();
    if (! mark.isValid() || alpha <= 0.01f)
        return;

    g.setOpacity (alpha);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (mark, juce::Rectangle<float> (size, size).withCentre (centre),
                 juce::RectanglePlacement::centred, false);
    g.setOpacity (1.0f);
}

// --- juce::Image -> CVPixelBuffer --------------------------------------------

static bool appendFrame (AVAssetWriterInputPixelBufferAdaptor* adaptor,
                         AVAssetWriterInput* input,
                         const juce::Image& image, CMTime time)
{
    while (! input.readyForMoreMediaData)
        [NSThread sleepForTimeInterval: 0.002];

    CVPixelBufferRef pixelBuffer = nullptr;
    if (CVPixelBufferPoolCreatePixelBuffer (kCFAllocatorDefault, adaptor.pixelBufferPool,
                                            &pixelBuffer) != kCVReturnSuccess)
        return false;

    CVPixelBufferLockBaseAddress (pixelBuffer, 0);
    auto* dest = (juce::uint8*) CVPixelBufferGetBaseAddress (pixelBuffer);
    const size_t destStride = CVPixelBufferGetBytesPerRow (pixelBuffer);

    {
        // juce::Image ARGB is BGRA in memory on little-endian - the same layout
        // as kCVPixelFormatType_32BGRA, so this is a row copy, no conversion.
        juce::Image::BitmapData src (image, juce::Image::BitmapData::readOnly);
        const size_t rowBytes = (size_t) image.getWidth() * 4;
        for (int y = 0; y < image.getHeight(); ++y)
            std::memcpy (dest + (size_t) y * destStride, src.getLinePointer (y), rowBytes);
    }

    CVPixelBufferUnlockBaseAddress (pixelBuffer, 0);

    const bool ok = [adaptor appendPixelBuffer: pixelBuffer withPresentationTime: time];
    CVPixelBufferRelease (pixelBuffer);
    return ok;
}

// --- demo library ------------------------------------------------------------

/*
    The film's fixtures. Every one is synthesised here so the render is
    reproducible on any machine and ships no third-party audio:

      beat.wav    an F# minor loop at 148 BPM - i VI iv VII over an F#1 sub,
                  the material the key detector actually reads
      hook.wav    a sung phrase in that key, harmonic-rich with vibrato, for
                  the pitch tracker and the fit scoring
      808.wav     a G2 808 four cents sharp, so the sample tuner has a real
                  correction to recommend against the detected scale
*/
struct DemoFixtures
{
    juce::File beat, hook, sample;
    std::vector<float> beatAudio, hookAudio;
};

static void writeWav (const juce::File& f, const std::vector<float>& audio, double sr)
{
    f.deleteFile();
    // Scoped: the WAV header is finalised in the writer's destructor, and a
    // file read before that looks empty.
    juce::WavAudioFormat wav;
    auto stream = f.createOutputStream();
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), sr, 1, 24, {}, 0));
    if (writer == nullptr)
        return;
    stream.release();
    juce::AudioBuffer<float> b (1, (int) audio.size());
    for (int i = 0; i < (int) audio.size(); ++i)
        b.setSample (0, i, audio[(size_t) i]);
    writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

static DemoFixtures buildFixtures()
{
    DemoFixtures fx;
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getChildFile ("KeyGloFilm");
    dir.createDirectory();
    fx.beat   = dir.getChildFile ("beat.wav");
    fx.hook   = dir.getChildFile ("hook.wav");
    fx.sample = dir.getChildFile ("808.wav");

    const double sr = 48000.0;

    // --- the beat: F# minor, 148 BPM ------------------------------------
    {
        juce::Random rng (0x4b47);
        // i - i - VI - iv - VII - i. The tonic occupies half the loop and the
        // phrase returns home, the way real music behaves. An earlier version
        // cycled four chords for equal time with no return: musically that has
        // no tonic, and the detector correctly reported B minor at 48 % - an
        // honest reading of ambiguous material, but a poor thing to film. The
        // fixture was wrong, not the engine.
        static const double bassHz[6] = { 46.25, 46.25, 36.71, 61.74, 41.20, 46.25 };
        static const double triads[6][3] = { { 185.0, 220.0, 277.18 },    // F#m
                                             { 185.0, 220.0, 277.18 },    // F#m
                                             { 146.83, 185.0, 220.0 },    // D
                                             { 123.47, 146.83, 185.0 },   // Bm
                                             { 164.81, 207.65, 246.94 },  // E
                                             { 185.0, 220.0, 277.18 } };  // F#m
        const int n = (int) (sr * 12.0);
        fx.beatAudio.assign ((size_t) n, 0.0f);
        double bassPhase = 0.0, chordPhase[3] = { 0, 0, 0 };
        float lp = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr, beatLen = 60.0 / 148.0;
            const int chord = ((int) (t / (beatLen * 4.0))) % 6;
            const float kick = (float) std::exp (-std::fmod (t, beatLen) * 9.0);
            bassPhase += juce::MathConstants<double>::twoPi * (bassHz[chord] + 7.0 * kick) / sr;
            float v = 0.48f * (0.45f + 0.55f * kick) * (float) std::sin (bassPhase);
            for (int c = 0; c < 3; ++c)
            {
                chordPhase[c] += juce::MathConstants<double>::twoPi * triads[chord][c] / sr;
                v += 0.16f * (float) std::sin (chordPhase[c])
                   + 0.05f * (float) std::sin (2.0 * chordPhase[c]);
            }
            lp += 0.12f * ((rng.nextFloat() * 2.0f - 1.0f) - lp);
            fx.beatAudio[(size_t) i] = v + 0.035f * lp;
        }
        writeWav (fx.beat, fx.beatAudio, sr);
    }

    // --- the hook: sung, strictly in F# minor ----------------------------
    {
        const int melody[] = { 61, 62, 64, 66, 64, 62, 61, 59, 61, 64 };
        const double amps[6] = { 1.0, 0.55, 0.32, 0.18, 0.10, 0.06 };
        double phase[6] = { 0, 0, 0, 0, 0, 0 };
        for (int note : melody)
        {
            const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
            const int len = (int) (sr * 0.75);
            for (int i = 0; i < len; ++i)
            {
                const double t = i / sr;
                const double vib = std::pow (2.0, (22.0 * std::sin (juce::MathConstants<double>::twoPi
                                                                      * 5.2 * t)) / 1200.0);
                const float env = (float) (juce::jmin (1.0, t / 0.05)
                                            * juce::jmin (1.0, (0.75 - t) / 0.05));
                float v = 0.0f;
                for (int h = 0; h < 6; ++h)
                {
                    phase[h] += juce::MathConstants<double>::twoPi * f0 * vib * (h + 1) / sr;
                    v += (float) (amps[h] * std::sin (phase[h]));
                }
                fx.hookAudio.push_back (0.22f * env * v);
            }
        }
        writeWav (fx.hook, fx.hookAudio, sr);
    }

    // --- the 808: G2, four cents sharp -----------------------------------
    {
        const double f0 = 440.0 * std::pow (2.0, (43.0 - 69.0 + 0.04) / 12.0);
        const int n = (int) (sr * 1.2);
        std::vector<float> audio ((size_t) n, 0.0f);
        double phase = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            phase += juce::MathConstants<double>::twoPi * (f0 + 18.0 * std::exp (-t / 0.012)) / sr;
            const float env = (float) ((1.0 - std::exp (-t / 0.004)) * std::exp (-t / 0.42));
            audio[(size_t) i] = 0.7f * env * (float) (std::sin (phase) + 0.25 * std::sin (2.0 * phase));
        }
        writeWav (fx.sample, audio, sr);
    }

    return fx;
}

/*
    The film's input signal, cut to the script: whatever the current act is
    about is what the plugin is actually hearing. Loops each fixture so an act
    longer than its source still has signal.
*/
class FilmAudio
{
public:
    FilmAudio (const DemoFixtures& fx, double sampleRate)
        : beat (fx.beatAudio), hook (fx.hookAudio), sr (sampleRate) {}

    // `sing` and `tune` bracket the acts whose input is not the beat.
    void setActBounds (double singStart, double singEnd, double tuneStart, double tuneEnd)
    {
        s0 = singStart; s1 = singEnd; t0 = tuneStart; t1 = tuneEnd;
    }

    void render (juce::AudioBuffer<float>& buffer, double t)
    {
        const std::vector<float>* src = &beat;
        if (t >= s0 && t < s1)       src = &hook;
        else if (t >= t0 && t < t1)  src = nullptr;   // the 808 acts are quiet

        const int n = buffer.getNumSamples();
        buffer.clear();
        if (src == nullptr || src->empty())
            return;

        for (int i = 0; i < n; ++i)
        {
            const float v = (*src)[(size_t) (pos % (juce::int64) src->size())];
            buffer.setSample (0, i, v);
            if (buffer.getNumChannels() > 1)
                buffer.setSample (1, i, v * 0.97f);
            ++pos;
        }
    }

private:
    const std::vector<float>& beat;
    const std::vector<float>& hook;
    double sr;
    double s0 = 1e9, s1 = 1e9, t0 = 1e9, t1 = 1e9;
    juce::int64 pos = 0;
};

// --- main --------------------------------------------------------------------

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    @autoreleasepool
    {
        // The flag is pulled out first and the positions read from what is
        // left: treating "reel" as positional is how MasterGlo's first reel
        // render silently came out with no soundtrack.
        // Named flags are pulled out before the positions are read. The
        // bitrate needs a name because the positional form breaks the moment
        // there is no beat file: empty arguments are dropped, so
        // `out.mp4 "" 0 5` collapses to `out.mp4 0 5` and "0" is taken as the
        // audio path. That is exactly how the first web renders failed.
        juce::StringArray args;
        double bitrateFlag = 0.0;
        for (int i = 1; i < argc; ++i)
        {
            const juce::String a { argv[i] };
            if (a.equalsIgnoreCase ("reel"))
                reelMode = true;
            else if (a.startsWithIgnoreCase ("bitrate="))
                bitrateFlag = a.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
            else if (a.isNotEmpty())
                args.add (a);
        }

        if (reelMode)
        {
            videoWidth = 1080;
            videoHeight = 1920;
        }

        const juce::String outPath = args.size() > 0 ? args[0]
                                                     : juce::String ("KeyGlo-demo.mp4");
        const juce::File sourceAudio = args.size() > 1 ? juce::File (args[1]) : juce::File();
        const double sourceOffset = args.size() > 2 ? args[2].getDoubleValue() : 0.0;
        const double bitrateMbps  = bitrateFlag > 0.0 ? bitrateFlag
                                  : args.size() > 3 ? args[3].getDoubleValue() : 14.0;

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader;
        if (sourceAudio.existsAsFile())
        {
            reader.reset (formats.createReaderFor (sourceAudio));
            if (reader == nullptr)
                std::printf ("could not read %s - falling back to the generated bed\n",
                             sourceAudio.getFullPathName().toRawUTF8());
        }
        else if (sourceAudio != juce::File())
        {
            std::printf ("no such file: %s\n", sourceAudio.getFullPathName().toRawUTF8());
            return 1;
        }

        const bool haveMusic = reader != nullptr;
        const double sr = haveMusic ? reader->sampleRate : 48000.0;
        const int blockSize = (int) std::llround (sr / fps);
        juce::int64 readPos = haveMusic ? (juce::int64) (sourceOffset * sr) : 0;

        if (haveMusic)
            std::printf ("source: %s\n  %.0f Hz, %d ch, %.1f s\n",
                         sourceAudio.getFileName().toRawUTF8(), sr,
                         (int) reader->numChannels,
                         (double) reader->lengthInSamples / sr);

        const auto fixtures = buildFixtures();

        KeyGloProcessor processor;
        processor.setPlayConfigDetails (2, 2, sr, blockSize);
        processor.prepareToPlay (sr, blockSize);

        // The film's analysis is REAL: the beat is analysed through the same
        // async path a dropped file takes, and the artist profile is the one
        // the range test would build. Waiting here rather than mid-render
        // keeps the timeline honest about what is on screen when.
        //
        // When a real beat is supplied it is what gets analysed - the film
        // must show KeyGlo reading the music the viewer is hearing, not a
        // synthetic stand-in with a different key.
        const juce::File beatForAnalysis = haveMusic ? sourceAudio : fixtures.beat;
        processor.analyseFileAsync (beatForAnalysis);
        for (int i = 0; i < 400; ++i)
        {
            auto snap = processor.getDisplayModel().get();
            if (snap->hasBeatResult && ! snap->analyzing)
                break;
            juce::Thread::sleep (25);
        }
        {
            auto snap = processor.getDisplayModel().get();
            std::printf ("beat analysed: %s %s, %.0f BPM\n",
                         snap->key.toRawUTF8(), snap->scale.toRawUTF8(), snap->bpm);
        }

        // The profile is NOT set here: the film installs it when the
        // profiling act runs, so the artist panel is genuinely empty before
        // then. Loading it up front had the vocal engine scoring the drum bed
        // as if someone were singing it - RANGE FIT 0 on screen, from real
        // code doing exactly what it was asked with material that was never a
        // voice.
        ArtistProfile filmProfile;
        filmProfile.extendedLowMidi = 48; filmProfile.comfortableLowMidi = 52;
        filmProfile.strongLowMidi = 55;   filmProfile.strongHighMidi = 64;
        filmProfile.comfortableHighMidi = 67; filmProfile.extendedHighMidi = 71;
        filmProfile.falsettoHighMidi = 71;

        std::unique_ptr<juce::AudioProcessorEditor> editorHolder (processor.createEditorIfNeeded());
        auto* editor = dynamic_cast<KeyGloEditor*> (editorHolder.get());
        if (editor == nullptr)
        {
            std::printf ("could not create the editor\n");
            return 1;
        }
        editor->setSize (panelWidth, panelHeight);

        // --- the script -------------------------------------------------------
        // --- the script -------------------------------------------------------
        //
        // Nine acts, 92 s. Everything the camera shows is the real plugin
        // reacting to the fixtures: the key on the wheel is what the detector
        // returned for beat.wav, the pitch trail is hook.wav tracked frame by
        // frame, the 808 readouts are what the sample detector measured.
        const std::vector<Segment> timeline =
        {
            // Logo opener.
            { 0.0, 5.0, {}, {},
              [] (KeyGloProcessor& p, KeyGloEditor&, double)
              {
                  loadPreset (p, "Male Rap Hook Match");
              } },

            { 5.0, 11.0, "The hook is right. The key is wrong.",
              "Every take fights the beat and nobody can say why", nullptr },

            { 11.0, 21.0, "Drop the beat",
              "Key, scale, tempo and tuning - read from the audio",
              [&beatForAnalysis] (KeyGloProcessor& p, KeyGloEditor&, double progress)
              {
                  // Re-analysing on screen shows the ANALYZING state resolve
                  // into a real verdict rather than cutting to a finished one.
                  if (progress < 0.02)
                      p.analyseFileAsync (beatForAnalysis);
              } },

            { 21.0, 29.0, "No reliable key beats a wrong one",
              "Sparse or tuneless material says so instead of guessing", nullptr },

            { 29.0, 41.0, "Profile the artist",
              "Four sung prompts map comfortable, strong and extended range",
              [&filmProfile] (KeyGloProcessor& p, KeyGloEditor&, double progress)
              {
                  // Installed here, not at startup: before this act the
                  // artist panel has no profile and honestly shows nothing.
                  if (progress > 0.55)
                      p.getVocalEngine().setProfile (filmProfile);
              } },

            { 41.0, 55.0, "Sing the hook",
              "Live pitch tracking - gaps where there is no voice, never a fake note",
              nullptr },

            { 55.0, 67.0, "How well does it fit?",
              "Scored against that voice and that beat, at all nine transpositions",
              nullptr },

            { 67.0, 79.0, "Tune the 808",
              "The sustain note, the cents it is off, and the fix",
              [&fixtures] (KeyGloProcessor& p, KeyGloEditor&, double progress)
              {
                  if (progress < 0.02)
                      p.analyseSampleAsync (fixtures.sample);
              } },

            { 79.0, 92.0, "Free. Local. No account.",
              "KeyGlo - artist-to-beat key matching", nullptr },
        };

        // A reel earns attention in the first second or loses it: six acts in
        // 42 s, each with the panel region it is talking about blown up in the
        // lower half. Focus rects are in the editor's 1491x1055 canvas.
        const std::vector<Segment> reelTimeline =
        {
            { 0.0, 3.2, {}, {},
              [] (KeyGloProcessor& p, KeyGloEditor&, double)
              {
                  loadPreset (p, "Male Rap Hook Match");
              } },

            { 3.2, 10.5, "What key is this beat?",
              "Drop it in. Read it in seconds.",
              [&beatForAnalysis] (KeyGloProcessor& p, KeyGloEditor&, double progress)
              {
                  if (progress < 0.03)
                      p.analyseFileAsync (beatForAnalysis);
              },
              { 15.0f, 91.0f, 360.0f, 460.0f } },          // beat panel

            { 10.5, 18.0, "Is it in YOUR range?",
              "Map the voice once, use it on every beat",
              [&filmProfile] (KeyGloProcessor& p, KeyGloEditor&, double progress)
              {
                  if (progress > 0.35)
                      p.getVocalEngine().setProfile (filmProfile);
              },
              { 1033.0f, 91.0f, 443.0f, 460.0f } },        // artist panel

            { 18.0, 26.0, "Score the hook",
              "Range fit, hook match, artist fit - measured, not guessed",
              nullptr,
              { 383.0f, 91.0f, 641.0f, 460.0f } },         // the wheel

            { 26.0, 33.5, "Move it to where it sings",
              "Nine transpositions scored. It recommends. You choose.",
              nullptr,
              { 329.0f, 559.0f, 695.0f, 270.0f } },        // transpose panel

            { 33.5, 42.0, "Tune your 808 to the track",
              "Free. Local. No account.",
              [&fixtures] (KeyGloProcessor& p, KeyGloEditor&, double progress)
              {
                  if (progress < 0.03)
                      p.analyseSampleAsync (fixtures.sample);
              },
              { 1033.0f, 559.0f, 443.0f, 270.0f } },       // 808 panel
        };


        const auto& script = reelMode ? reelTimeline : timeline;
        const double duration = script.back().end;
        const int totalFrames = (int) (duration * fps);

        // --- writer ------------------------------------------------------------
        auto toNS = [] (const juce::String& str)
        {
            return [NSString stringWithUTF8String: str.toRawUTF8()];
        };

        const auto finalFile = juce::File::getCurrentWorkingDirectory().getChildFile (outPath);
        const auto videoOnlyFile = finalFile.getSiblingFile (finalFile.getFileNameWithoutExtension()
                                                               + "-videoonly.mp4");
        const auto wavFile = finalFile.getSiblingFile (finalFile.getFileNameWithoutExtension()
                                                          + "-audio.wav");
        const auto m4aFile = finalFile.getSiblingFile (finalFile.getFileNameWithoutExtension()
                                                          + "-audio.m4a");

        auto* url = [NSURL fileURLWithPath: toNS (videoOnlyFile.getFullPathName())];
        [[NSFileManager defaultManager] removeItemAtURL: url error: nil];

        NSError* error = nil;
        AVAssetWriter* writer = [AVAssetWriter assetWriterWithURL: url
                                                         fileType: AVFileTypeMPEG4
                                                            error: &error];
        if (writer == nil)
        {
            std::printf ("could not create the writer: %s\n",
                         error.localizedDescription.UTF8String);
            return 1;
        }

        NSDictionary* settings = @{
            AVVideoCodecKey: AVVideoCodecTypeH264,
            AVVideoWidthKey: @(videoWidth),
            AVVideoHeightKey: @(videoHeight),
            AVVideoCompressionPropertiesKey: @{
                AVVideoAverageBitRateKey: @((int) (bitrateMbps * 1000000.0)),
                AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel,
                AVVideoMaxKeyFrameIntervalKey: @(fps * 2),
            },
        };

        AVAssetWriterInput* input = [AVAssetWriterInput assetWriterInputWithMediaType: AVMediaTypeVideo
                                                                      outputSettings: settings];
        input.expectsMediaDataInRealTime = NO;

        NSDictionary* bufferAttributes = @{
            (NSString*) kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
            (NSString*) kCVPixelBufferWidthKey: @(videoWidth),
            (NSString*) kCVPixelBufferHeightKey: @(videoHeight),
        };
        auto* adaptor = [AVAssetWriterInputPixelBufferAdaptor
                           assetWriterInputPixelBufferAdaptorWithAssetWriterInput: input
                                                      sourcePixelBufferAttributes: bufferAttributes];

        [writer addInput: input];
        [writer startWriting];
        [writer startSessionAtSourceTime: kCMTimeZero];

        // --- render ------------------------------------------------------------
        FilmAudio filmAudio (fixtures, sr);
        // Acts whose input is the sung hook / the 808 rather than the beat.
        if (reelMode) filmAudio.setActBounds (10.5, 26.0, 33.5, 42.0);
        else          filmAudio.setActBounds (29.0, 67.0, 67.0, 79.0);
        juce::AudioBuffer<float> audio (2, blockSize);
        juce::MidiBuffer midi;

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatWriter> audioWriter;

        if (haveMusic)
        {
            wavFile.deleteFile();
            if (auto stream = std::unique_ptr<juce::OutputStream> (wavFile.createOutputStream()))
            {
                const auto options = juce::AudioFormatWriterOptions{}
                                       .withSampleRate (sr)
                                       .withNumChannels (2)
                                       .withBitsPerSample (24);
                audioWriter = wavFormat.createWriterFor (stream, options);
            }
        }

        Narration narration;
        narration.load (juce::File::getCurrentWorkingDirectory()
                          .getSiblingFile ("build").getChildFile ("vo"), sr);
        std::vector<double> actStarts;
        for (const auto& seg : script)
            actStarts.push_back (seg.start);
        if (! narration.empty())
            std::printf ("narration: %d lines\n", (int) narration.lines.size());

        juce::Image frame (juce::Image::ARGB, videoWidth, videoHeight, true);
        juce::Image panel (juce::Image::ARGB, panelWidth, panelHeight, true);

        float duckGain = 1.0f;

        std::printf ("rendering %d frames (%.0f seconds) at %dx%d\n",
                     totalFrames, duration, videoWidth, videoHeight);

        for (int f = 0; f < totalFrames; ++f)
        {
            const double t = (double) f / fps;

            const Segment* current = nullptr;
            for (const auto& seg : script)
                if (t >= seg.start && t < seg.end)
                    current = &seg;

            if (current != nullptr && current->action != nullptr)
                current->action (processor, *editor,
                                 (t - current->start) / (current->end - current->start));

            if (haveMusic)
            {
                if (readPos + blockSize >= reader->lengthInSamples)
                    readPos = 0;
                reader->read (&audio, 0, blockSize, readPos, true, true);
                readPos += blockSize;
            }
            else
            {
                // The plugin's input matches what the act is about, because
                // that is what a viewer is being told is happening: the beat
                // while the beat is discussed, the sung hook (alone, the way a
                // vocal mic feeds it) while the artist acts run, the 808 while
                // it is tuned. Feeding one generic bed throughout would have
                // the pitch trail tracking a kick drum.
                filmAudio.render (audio, t);
            }

            processor.processBlock (audio, midi);

            // Popup menus are asynchronous, and this render has no message
            // loop, so showMenuAsync would never get to build its component
            // and the film would show nothing where a menu should be.
            // MessageManager::runDispatchLoopUntil is compiled out of plugin
            // builds (JUCE_MODAL_LOOPS_PERMITTED=0), so the CFRunLoop that
            // backs the queue on macOS is pumped directly instead.
            CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.004, false);

            // The soundtrack is the PROCESSED output: what the plugin did to
            // the audio the viewer is watching it analyse - with the voice
            // over the top and the music ducked under it.
            if (audioWriter != nullptr)
            {
                juce::AudioBuffer<float> mixed (2, blockSize);
                for (int ch = 0; ch < 2; ++ch)
                    mixed.copyFrom (ch, 0, audio, juce::jmin (ch, audio.getNumChannels() - 1),
                                    0, blockSize);

                if (! narration.empty())
                {
                    juce::AudioBuffer<float> voice (2, blockSize);
                    voice.clear();
                    const float duck = narration.mixInto (voice, t, actStarts);

                    duckGain = duckGain + 0.06f * (duck - duckGain);   // smooth
                    mixed.applyGain (duckGain);
                    for (int ch = 0; ch < 2; ++ch)
                        mixed.addFrom (ch, 0, voice, ch, 0, blockSize);
                }

                audioWriter->writeFromAudioSampleBuffer (mixed, 0, blockSize);
            }
            editor->refreshDisplays();

            // --- compose -------------------------------------------------------
            {
                juce::Graphics g (frame);
                drawBackdrop (g);

                const float panelAlpha = reelMode ? envelopeFor (t, 2.9, 44.4, 0.9, 0.8)
                                                  : envelopeFor (t, 4.6, 111.0, 1.2, 1.0);

                if (panelAlpha > 0.01f)
                {
                    { juce::Graphics pg (panel); editor->paintEntireComponent (pg, true); }

                    const float rise = (1.0f - panelAlpha) * 26.0f;

                    if (reelMode)
                    {
                        auto full = juce::Rectangle<float> (reel::panelW, reel::panelH)
                                      .withCentre ({ (float) videoWidth * 0.5f,
                                                     reel::panelY + reel::panelH * 0.5f + rise });

                        g.setColour (juce::Colours::black.withAlpha (0.5f * panelAlpha));
                        g.fillRoundedRectangle (full.expanded (12.0f), 18.0f);

                        g.setOpacity (panelAlpha);
                        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                        g.drawImage (panel, full, juce::RectanglePlacement::stretchToFit, false);
                        g.setOpacity (1.0f);

                        // ...and the act's own region blown up underneath, so
                        // the lower half is real interface, not backdrop.
                        if (current != nullptr)
                            drawFocus (g, panel, current->focus,
                                       juce::Rectangle<float> (0.0f, reel::detailY,
                                                               (float) videoWidth, reel::detailH),
                                       panelAlpha);

                        // Small wordmark riding the top band once the intro is over.
                        drawLogo (g, 0.9f * panelAlpha, 0.60f, reel::logoY + reel::logoH * 0.5f);
                    }
                    else
                    {
                        // 1244 wide keeps the native 1491x1055 canvas' aspect
                        // and leaves room for the title and caption bands.
                        auto target = juce::Rectangle<float> (1244.0f, 880.0f)
                                        .withCentre ({ (float) videoWidth * 0.5f, 512.0f + rise });

                        g.setColour (juce::Colours::black.withAlpha (0.55f * panelAlpha));
                        g.fillRoundedRectangle (target.expanded (16.0f), 24.0f);

                        g.setOpacity (panelAlpha);
                        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                        g.drawImage (panel, target, juce::RectanglePlacement::centred, false);
                        g.setOpacity (1.0f);
                    }
                }

                // Logo opener: the mark rises alone, then cross-dissolves into
                // the full lockup. The wordmark asset ALREADY contains the
                // mark, so drawing both at once reads as a duplicated logo -
                // they must never overlap.
                const float centreY = (float) videoHeight * 0.5f;

                const float markAlpha = reelMode ? envelopeFor (t, 0.1, 1.9, 0.8, 0.5)
                                                 : envelopeFor (t, 0.15, 2.5, 1.0, 0.55);
                if (markAlpha > 0.01f)
                    drawMark (g, markAlpha * 0.95f,
                              (reelMode ? 300.0f : 270.0f) + 26.0f * markAlpha,
                              { (float) videoWidth * 0.5f, centreY });

                const float introAlpha = reelMode ? envelopeFor (t, 1.7, 3.4, 0.7, 0.6)
                                                  : envelopeFor (t, 2.25, 4.9, 0.85, 0.8);
                if (introAlpha > 0.01f)
                {
                    drawLogo (g, introAlpha, 0.97f + 0.03f * introAlpha, centreY);
                    g.setColour (tokens::cyan.withAlpha (0.85f * introAlpha));
                    drawTracked (g, "PRODUCTION INTELLIGENCE FOR BETTER MIXES",
                                 juce::Rectangle<float> (0.0f, centreY + 128.0f,
                                                         (float) videoWidth, 44.0f),
                                 Fonts::make (reelMode ? 20.0f : 25.0f), 5.0f);
                }

                // Logo closer.
                const float outroAlpha = reelMode ? envelopeFor (t, 43.8, 51.0, 0.9, 1.0)
                                                  : envelopeFor (t, 110.4, 118.0, 1.1, 1.2);
                if (outroAlpha > 0.01f)
                {
                    drawLogo (g, outroAlpha, 1.0f, centreY);

                    g.setColour (tokens::text.withAlpha (0.88f * outroAlpha));
                    // fromUTF8, not a bare literal: juce::String reads the middle
                    // dot's two UTF-8 bytes as Latin-1 and draws "Â·".
                    drawTracked (g, juce::String::fromUTF8 (reelMode ? "VST3  ·  AU  ·  STANDALONE"
                                                    : "VST3  ·  AUDIO UNIT  ·  STANDALONE  ·  MACOS"),
                                 juce::Rectangle<float> (0.0f, centreY + 124.0f,
                                                         (float) videoWidth, 44.0f),
                                 Fonts::make (reelMode ? 21.0f : 24.0f), 4.0f);
                    g.setColour (tokens::gold.withAlpha (0.85f * outroAlpha));
                    drawTracked (g, "DIAMOND LOOPZ",
                                 juce::Rectangle<float> (0.0f, centreY + 192.0f,
                                                         (float) videoWidth, 40.0f),
                                 Fonts::make (reelMode ? 19.0f : 21.0f), 6.0f);
                }

                if (current != nullptr)
                {
                    const float fade = envelopeFor (t, current->start, current->end,
                                                    reelMode ? 0.45 : 0.6,
                                                    reelMode ? 0.45 : 0.6);
                    drawTitle (g, current->title, fade,
                               reelMode ? juce::Rectangle<float> (0.0f, reel::titleY,
                                                                  (float) videoWidth, reel::titleH)
                                        : juce::Rectangle<float> (0.0f, 26.0f,
                                                                  (float) videoWidth, 74.0f),
                               reelMode ? 44.0f : 50.0f, false);
                    drawCaption (g, current->caption, fade,
                                 reelMode ? juce::Rectangle<float> (0.0f, reel::captionY,
                                                                    (float) videoWidth, reel::captionH)
                                          : juce::Rectangle<float> (0.0f, 992.0f,
                                                                    (float) videoWidth, 60.0f),
                                 reelMode ? 23.0f : 27.0f);
                }
            }

            // A still per act, so a render can be reviewed without scrubbing -
            // and a bad overlay is caught here rather than after upload.
            {
                static const std::array<double, 15> filmStills
                    { 1.6, 3.6, 8.0, 13.5, 19.0, 26.0, 33.0, 47.6, 56.0, 65.0,
                      74.0, 84.0, 99.0, 106.0, 114.0 };
                static const std::array<double, 8> reelStills
                    { 1.2, 2.8, 7.0, 14.0, 24.0, 31.0, 40.0, 47.5 };

                std::vector<double> stillTimes;
                if (reelMode) stillTimes.assign (reelStills.begin(), reelStills.end());
                else          stillTimes.assign (filmStills.begin(), filmStills.end());

                for (double mark : stillTimes)
                    if (std::abs (t - mark) < 0.5 / fps)
                    {
                        auto dir = juce::File::getCurrentWorkingDirectory()
                                    .getChildFile (reelMode ? "reel-stills" : "video-stills");
                        dir.createDirectory();
                        auto still = dir.getChildFile ("still-" + juce::String (mark, 1) + "s.png");
                        still.deleteFile();
                        juce::PNGImageFormat png;
                        if (auto out = std::unique_ptr<juce::FileOutputStream> (still.createOutputStream()))
                            png.writeImageToStream (frame, *out);
                    }
            }

            if (! appendFrame (adaptor, input, frame,
                               CMTimeMake ((int64_t) f, (int32_t) fps)))
            {
                std::printf ("frame %d failed to encode\n", f);
                return 1;
            }

            if (f % (fps * 5) == 0)
                std::printf ("  %3d%%  (%.0fs)\n", (f * 100) / totalFrames, t);
        }

        [input markAsFinished];

        __block bool finished = false;
        [writer finishWritingWithCompletionHandler: ^{ finished = true; }];
        while (! finished)
            [NSThread sleepForTimeInterval: 0.05];

        if (writer.status != AVAssetWriterStatusCompleted)
        {
            std::printf ("writer failed: %s\n",
                         writer.error.localizedDescription.UTF8String);
            return 1;
        }

        audioWriter.reset();

        // --- mux ---------------------------------------------------------------
        if (haveMusic && wavFile.existsAsFile())
        {
            std::printf ("\nencoding the soundtrack and muxing...\n");

            // afconvert ships with macOS, so the AAC encode needs no install.
            // Encoding first lets the mux be a passthrough - the video is never
            // recompressed.
            m4aFile.deleteFile();
            juce::ChildProcess convert;
            convert.start (juce::StringArray { "/usr/bin/afconvert", "-f", "m4af",
                                               "-d", "aac", "-b", "256000",
                                               wavFile.getFullPathName(),
                                               m4aFile.getFullPathName() });
            convert.waitForProcessToFinish (180000);

            if (! m4aFile.existsAsFile())
            {
                std::printf ("afconvert failed; leaving the silent video in place\n");
                videoOnlyFile.moveFileTo (finalFile);
            }
            else
            {
                auto* composition = [AVMutableComposition composition];
                auto* videoAsset = [AVURLAsset URLAssetWithURL:
                                      [NSURL fileURLWithPath: toNS (videoOnlyFile.getFullPathName())]
                                                      options: nil];
                auto* audioAsset = [AVURLAsset URLAssetWithURL:
                                      [NSURL fileURLWithPath: toNS (m4aFile.getFullPathName())]
                                                      options: nil];

                auto* videoTrack = [composition addMutableTrackWithMediaType: AVMediaTypeVideo
                                                            preferredTrackID: kCMPersistentTrackID_Invalid];
                auto* audioTrack = [composition addMutableTrackWithMediaType: AVMediaTypeAudio
                                                            preferredTrackID: kCMPersistentTrackID_Invalid];

                NSError* muxError = nil;
                const CMTimeRange range = CMTimeRangeMake (kCMTimeZero, videoAsset.duration);

                [videoTrack insertTimeRange: range
                                    ofTrack: [videoAsset tracksWithMediaType: AVMediaTypeVideo].firstObject
                                     atTime: kCMTimeZero error: &muxError];
                [audioTrack insertTimeRange: range
                                    ofTrack: [audioAsset tracksWithMediaType: AVMediaTypeAudio].firstObject
                                     atTime: kCMTimeZero error: &muxError];

                finalFile.deleteFile();
                auto* session = [[AVAssetExportSession alloc]
                                   initWithAsset: composition
                                      presetName: AVAssetExportPresetPassthrough];
                session.outputURL = [NSURL fileURLWithPath: toNS (finalFile.getFullPathName())];
                session.outputFileType = AVFileTypeMPEG4;

                __block bool muxed = false;
                [session exportAsynchronouslyWithCompletionHandler: ^{ muxed = true; }];
                while (! muxed)
                    [NSThread sleepForTimeInterval: 0.05];

                if (session.status != AVAssetExportSessionStatusCompleted)
                {
                    std::printf ("mux failed: %s\n",
                                 session.error.localizedDescription.UTF8String);
                    videoOnlyFile.moveFileTo (finalFile);
                }
                else
                {
                    videoOnlyFile.deleteFile();
                    wavFile.deleteFile();
                    m4aFile.deleteFile();
                }
            }
        }
        else
        {
            videoOnlyFile.moveFileTo (finalFile);
        }

        std::printf ("\nwrote %s\n  %.1f MB, %.0f seconds, %dx%d @ %d fps, %s\n",
                     finalFile.getFullPathName().toRawUTF8(),
                     finalFile.getSize() / (1024.0 * 1024.0), duration,
                     videoWidth, videoHeight, fps,
                     haveMusic ? "with the processed soundtrack" : "silent");

        editorHolder.reset();
        processor.editorBeingDeleted (nullptr);
    }
    return 0;
}
