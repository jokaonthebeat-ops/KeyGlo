#include "Assets.h"

namespace keyglo
{

// -----------------------------------------------------------------------------
//  Failure bookkeeping - one diagnostic per distinct missing asset, then quiet.
// -----------------------------------------------------------------------------
static juce::CriticalSection& failureLock()
{
    static juce::CriticalSection lock;
    return lock;
}

static juce::StringArray& reportedFailures()
{
    static juce::StringArray reported;
    return reported;
}

static void logMissOnce (const juce::String& relativePath)
{
    const juce::ScopedLock sl (failureLock());
    if (reportedFailures().contains (relativePath))
        return;
    reportedFailures().add (relativePath);
    juce::Logger::writeToLog ("KeyGlo: asset failed to load, using flat fallback: "
                                + relativePath);
}

int Assets::loadFailureCount()
{
    const juce::ScopedLock sl (failureLock());
    return reportedFailures().size();
}

juce::String Assets::describeFailures()
{
    const juce::ScopedLock sl (failureLock());
    return reportedFailures().joinIntoString ("\n");
}

// -----------------------------------------------------------------------------
//  Locator - bundle Resources/Assets, Windows exe-relative Assets, or (for the
//  headless tools) an Assets/ folder up the tree from the executable.
// -----------------------------------------------------------------------------
juce::File Assets::assetsDirectory()
{
    auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

    auto resources = exe.getParentDirectory().getParentDirectory().getChildFile ("Resources/Assets");
    if (resources.getChildFile ("Base").isDirectory())
        return resources;

    auto beside = exe.getParentDirectory().getChildFile ("Assets");
    if (beside.getChildFile ("Base").isDirectory())
        return beside;

    auto dir = exe.getParentDirectory();
    for (int i = 0; i < 6; ++i)
    {
        auto candidate = dir.getChildFile ("Assets");
        if (candidate.getChildFile ("Base").isDirectory())
            return candidate;
        dir = dir.getParentDirectory();
    }
    return {};
}

juce::Image Assets::load (const juce::String& relativePath)
{
    auto file = assetsDirectory().getChildFile (relativePath);
    if (file.existsAsFile())
    {
        // One decode per file per process; every instance shares it.
        auto img = juce::ImageCache::getFromFile (file);
        if (img.isValid())
            return img;
    }
    logMissOnce (relativePath);
    return {};
}

// -----------------------------------------------------------------------------
//  Opaque-bounds crop - the standard defence against exports whose artwork
//  floats in a larger transparent canvas.
// -----------------------------------------------------------------------------
static juce::Image trimToOpaqueBounds (juce::Image full)
{
    if (! full.isValid())
        return full;

    juce::Image::BitmapData pixels (full, juce::Image::BitmapData::readOnly);
    int minX = full.getWidth(), minY = full.getHeight(), maxX = -1, maxY = -1;

    for (int y = 0; y < full.getHeight(); ++y)
        for (int x = 0; x < full.getWidth(); ++x)
            if (pixels.getPixelColour (x, y).getAlpha() > 8)
            {
                minX = juce::jmin (minX, x); maxX = juce::jmax (maxX, x);
                minY = juce::jmin (minY, y); maxY = juce::jmax (maxY, y);
            }

    if (maxX < minX || maxY < minY)
        return full;

    const juce::Rectangle<int> art (minX, minY, maxX - minX + 1, maxY - minY + 1);
    if (art == full.getBounds())
        return full;

    return full.getClippedImage (art).createCopy();
}

// -----------------------------------------------------------------------------
//  Base + brand
// -----------------------------------------------------------------------------
juce::Image Assets::shell()
{
    static const juce::Image img = load ("Base/keyglo_shell_1491x1055.png");
    return img;
}

juce::Image Assets::shell2x()
{
    static const juce::Image img = load ("Base/keyglo_shell_2982x2110_2x.png");
    return img;
}

juce::Image Assets::logoHeader (float scale)
{
    // v2 wordmark supplied by the user on 2026-08-26 (chrome + neon, 2049x562
    // opaque art in a 2172x724 transparent canvas). One high-res master drawn
    // at every scale - sharp on Retina, one decode (the MasterGlo rule).
    static const juce::Image v2 = trimToOpaqueBounds (load ("Brand/keyglo_logo_v2_2172x724.png"));
    if (v2.isValid())
        return v2;

    // Pack exports as fallback (155x44 of art in the 250x58 canvas at 1x).
    static const juce::Image x1 = trimToOpaqueBounds (load ("Brand/keyglo_header_logo_250x58.png"));
    static const juce::Image x2 = trimToOpaqueBounds (load ("Brand/keyglo_header_logo_500x116.png"));

    if (scale > 1.01f && x2.isValid()) return x2;
    return x1;
}

juce::Image Assets::premiumMark()
{
    static const juce::Image img = load ("Brand/keyglo_mark_512.png");
    return img;
}

// -----------------------------------------------------------------------------
//  HUD
// -----------------------------------------------------------------------------
juce::Image Assets::keyWheelBase()
{
    // Near-full canvas (968x961 opaque in 1024, margins roughly symmetric) -
    // drawn as the full square so note_positions.json's centre/radius apply.
    static const juce::Image img = load ("HUD/key_wheel_base_1024.png");
    return img;
}

juce::Image Assets::centerGlass()
{
    static const juce::Image img = load ("HUD/key_wheel_center_glass_640.png");
    return img;
}

juce::Image Assets::scorePod (Accent a)
{
    switch (a)
    {
        case Accent::violet: { static const juce::Image v = load ("HUD/score_pod_violet_256.png"); return v; }
        case Accent::gold:   { static const juce::Image g = load ("HUD/score_pod_gold_256.png");   return g; }
        case Accent::cyan:
        default:             { static const juce::Image c = load ("HUD/score_pod_cyan_256.png");   return c; }
    }
}

juce::Image Assets::noteNodeActive (Accent a)
{
    if (a == Accent::violet)
    {
        static const juce::Image v = load ("HUD/note_node_active_violet_128.png");
        return v;
    }
    static const juce::Image c = load ("HUD/note_node_active_cyan_128.png");
    return c;
}

juce::Image Assets::noteNodeInactive()
{
    static const juce::Image img = load ("HUD/note_node_inactive_128.png");
    return img;
}

juce::Image Assets::particleGlow (Accent a)
{
    switch (a)
    {
        case Accent::violet: { static const juce::Image v = load ("Visualizers/particle_glow_violet_128.png"); return v; }
        case Accent::gold:   { static const juce::Image g = load ("Visualizers/particle_glow_gold_128.png");   return g; }
        case Accent::cyan:
        default:             { static const juce::Image c = load ("Visualizers/particle_glow_cyan_128.png");   return c; }
    }
}

// -----------------------------------------------------------------------------
//  Controls
// -----------------------------------------------------------------------------

// Slice a vertical filmstrip into per-frame images at load. Confirmed in
// production (MasterGlo Pro, 2026-08-19): drawing from the tall strip works in
// the standalone and is silently MISSING in a DAW once the strip exceeds the
// renderer's texture limit (these are 160x20480). createCopy(), not
// getClippedImage() alone - a clipped image is only a view onto the oversized
// original.

// The supplied strips are rotated +90 degrees from standard rotary
// orientation: frame 0 points 10:30 instead of 7:30, frame 64 east instead of
// north - measured off the extracted frames on 2026-08-26, identical to the
// SourceGlo pack's defect. One exact 90-degree anticlockwise rotation per
// frame restores the -135..+135 sweep AND puts the bezel specular back at the
// top where the approved mockup has it. Pixel transpose, no resampling.
static juce::Image rotateFrameAnticlockwise (const juce::Image& src)
{
    const int size = src.getWidth();
    juce::Image dst (juce::Image::ARGB, size, size, true);
    juce::Image::BitmapData in  (src, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData out (dst, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            out.setPixelColour (y, size - 1 - x, in.getPixelColour (x, y));

    return dst;
}

static Assets::Filmstrip sliceStrip (juce::Image strip)
{
    Assets::Filmstrip fs;
    if (! strip.isValid())
        return fs;

    // Trust the image, not the filename.
    const int size  = strip.getWidth();
    const int count = strip.getHeight() / juce::jmax (1, size);
    fs.frameSize = size;

    if (count >= 2)
    {
        fs.frames.reserve ((size_t) count);
        for (int f = 0; f < count; ++f)
            fs.frames.push_back (rotateFrameAnticlockwise (
                strip.getClippedImage ({ 0, f * size, size, size }).createCopy()));
    }
    return fs;
}

const Assets::Filmstrip& Assets::macroKnob (Accent a)
{
    if (a == Accent::gold)
    {
        static const Filmstrip gold = []
        {
            auto result = sliceStrip (load ("Controls/knobs/macro_knob_gold_160px_128frames_vertical.png"));
            juce::ImageCache::releaseUnusedImages();   // drop the tall source image
            return result;
        }();
        return gold;
    }

    static const Filmstrip cyan = []
    {
        auto result = sliceStrip (load ("Controls/knobs/macro_knob_cyan_160px_128frames_vertical.png"));
        juce::ImageCache::releaseUnusedImages();
        return result;
    }();
    return cyan;
}

const Assets::Filmstrip& Assets::smallKnob()
{
    static const Filmstrip fs = []
    {
        auto result = sliceStrip (load ("Controls/knobs/small_knob_cyan_96px_128frames_vertical.png"));
        juce::ImageCache::releaseUnusedImages();
        return result;
    }();
    return fs;
}

juce::Image Assets::buttonSkin (const juce::String& base, ButtonState state)
{
    static const char* states[] = { "normal", "hover", "down", "disabled" };
    return load ("Controls/buttons/" + base + "_" + states[(int) state] + ".png");
}

juce::Image Assets::toggle (bool on, Accent a)
{
    if (! on)
        return load ("Controls/toggles/toggle_off_68x32.png");

    static const char* names[] = { "cyan", "violet", "gold" };
    return load (juce::String ("Controls/toggles/toggle_on_") + names[(int) a] + "_68x32.png");
}

juce::Image Assets::meterTrough()
{
    static const juce::Image img = load ("Controls/meters/stereo_meter_trough_78x124.png");
    return img;
}

juce::Image Assets::meterSegment (int zone)
{
    static const char* names[] = { "cyan", "violet", "gold", "red" };
    return load (juce::String ("Controls/meters/meter_segment_")
                   + names[juce::jlimit (0, 3, zone)] + "_12x5.png");
}

juce::Image Assets::confidenceTrack()
{
    static const juce::Image img = load ("Controls/meters/confidence_track_180x8.png");
    return img;
}

juce::Image Assets::confidenceFill()
{
    static const juce::Image img = load ("Controls/meters/confidence_fill_cyan_180x8.png");
    return img;
}

// -----------------------------------------------------------------------------
//  Cards
// -----------------------------------------------------------------------------
juce::Image Assets::analysisRow (int state)
{
    static const char* names[] = { "normal", "hover", "selected" };
    return load (juce::String ("Cards/analysis_row_")
                   + names[juce::jlimit (0, 2, state)] + "_343x32.png");
}

juce::Image Assets::dropZone (int state)
{
    static const char* names[] = { "normal", "hover", "active" };
    return load (juce::String ("Cards/drop_zone_")
                   + names[juce::jlimit (0, 2, state)] + "_343x60.png");
}

juce::Image Assets::panelHeader()
{
    static const juce::Image img = load ("Cards/panel_header_440x42.png");
    return img;
}

juce::Image Assets::readoutCell (Accent a)
{
    switch (a)
    {
        case Accent::violet: { static const juce::Image v = load ("Cards/readout_cell_violet_124x60.png"); return v; }
        case Accent::gold:   { static const juce::Image g = load ("Cards/readout_cell_gold_124x60.png");   return g; }
        case Accent::cyan:
        default:             { static const juce::Image c = load ("Cards/readout_cell_cyan_124x60.png");   return c; }
    }
}

juce::Image Assets::transposeResult (bool highlight)
{
    return load (juce::String ("Cards/transpose_result_")
                   + (highlight ? "highlight" : "normal") + "_354x116.png");
}

// -----------------------------------------------------------------------------
//  Visualizers
// -----------------------------------------------------------------------------
juce::Image Assets::artistRangeGrid()
{
    static const juce::Image img = load ("Visualizers/artist_range_grid_690x330.png");
    return img;
}

juce::Image Assets::noteMapPiano()
{
    static const juce::Image img = load ("Visualizers/horizontal_note_map_720x110.png");
    return img;
}

juce::Image Assets::verticalPiano()
{
    static const juce::Image img = load ("Visualizers/vertical_piano_88x440.png");
    return img;
}

juce::Image Assets::spectrumGrid()
{
    static const juce::Image img = load ("Visualizers/spectrum_grid_720x220.png");
    return img;
}

juce::Image Assets::waveformGrid()
{
    static const juce::Image img = load ("Visualizers/waveform_grid_640x220.png");
    return img;
}

juce::Image Assets::tunerDial()
{
    static const juce::Image img = load ("Visualizers/tuner_dial_gold_512.png");
    return img;
}

// -----------------------------------------------------------------------------
//  Icons - supplied SVGs recoloured by rewriting colour attributes in the SVG
//  text before parsing, then cached per (name, tint).
// -----------------------------------------------------------------------------
juce::Drawable* Assets::icon (const juce::String& name, juce::Colour tint)
{
    struct Key
    {
        juce::String name; juce::uint32 argb;
        bool operator< (const Key& o) const
        {
            if (name != o.name) return name < o.name;
            return argb < o.argb;
        }
    };

    static juce::CriticalSection lock;
    static std::map<Key, std::unique_ptr<juce::Drawable>> cache;

    const Key key { name, tint.getARGB() };
    const juce::ScopedLock sl (lock);

    auto it = cache.find (key);
    if (it != cache.end())
        return it->second.get();

    std::unique_ptr<juce::Drawable> drawable;

    auto file = assetsDirectory().getChildFile ("Icons/" + name + ".svg");
    if (file.existsAsFile())
    {
        auto svg = file.loadFileAsString();
        const auto hex = tint.toDisplayString (false);
        svg = svg.replaceCharacters ("\r", " ");

        // These icons are stroke-drawn (1.8 px, round caps, inner paths carry
        // fill="none"), but the export also stamps the stroke colour as the
        // group FILL, which turns e.g. the help "?" ring into a solid disc -
        // both in the SVGs and in the pack's own PNG exports. Correct the
        // export at load: group fills become none, strokes take the tint.
        for (const char* attr : { "stroke=\"#", "fill=\"#" })
        {
            const bool isFill = attr[0] == 'f';
            const juce::String replacement = isFill ? "none" : hex;
            int pos = 0;
            while ((pos = svg.indexOf (pos, attr)) >= 0)
            {
                const int valueStart = pos + (int) juce::String (attr).length();
                int valueEnd = valueStart;
                while (valueEnd < svg.length() && svg[valueEnd] != '"')
                    ++valueEnd;
                svg = svg.substring (0, valueStart - (isFill ? 1 : 0))
                        + replacement + svg.substring (valueEnd);
                pos = valueStart;
            }
        }

        drawable = juce::Drawable::createFromSVGString (svg);
    }

    if (drawable == nullptr)
        logMissOnce ("Icons/" + name + ".svg");

    auto* raw = drawable.get();
    cache[key] = std::move (drawable);
    return raw;
}

} // namespace keyglo
