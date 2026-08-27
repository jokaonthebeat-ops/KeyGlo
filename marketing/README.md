# KeyGlo marketing kit

Everything here is generated from the real plugin. The screenshots are the
shipping interface (not the approved mockup), and every frame of the video
is the actual editor rendering actual analysis — the key on the wheel is
what the detector returned for the beat fixture, the pitch trail is a sung
phrase tracked note by note, the 808 readouts are real measurements.

Regenerate everything with `make video`, `make reel` and
`make uishot ARGS="..."`.

## Video

| File | Use |
|---|---|
| `KeyGlo-demo.mp4` | 1920×1080, ~92 s. Product page, YouTube, plugin directories. |
| `KeyGlo-demo-web.mp4` | Same film at a web bitrate. Embeds. |
| `KeyGlo-reel.mp4` | 1080×1920, ~42 s. Reels / Shorts / TikTok. |
| `KeyGlo-reel-web.mp4` | Reel at a web bitrate. |

**The films are silent on purpose.** A fabricated song under a product
video is worse than none, and the fixtures are synthesised test signals —
musically correct, but not something anyone wants to listen to. Drop your
own beat under it in your editor, or run
`make video BEAT="path/to/loop.wav"` to drive the analysis with real audio
and use the plugin's own output as the soundtrack.

## Screenshots (`screenshots/`)

| File | What it shows |
|---|---|
| `01-everything-live` | All panels working at once: F# minor beat, hook scored 94, 808 tuned. The hero shot. |
| `02-vocal-fit` | The artist side alive — pitch trail, fit pods, recommendation. |
| `03-honest-empty-state` | A fresh instance: `--` everywhere, "DROP A BEAT". Use this when the copy talks about honesty. |
| `04-scaled-min` | 1044×739, proving clean scaling. |

`*-web.jpg` are 1600 px JPEGs for pages; `social-1280.jpg` for cards.

## Brand (`brand/`)

Wordmark at 2048/1200/600 px wide and the circular mark at 1024/512/256,
all transparent PNG. Source art is the user's own
`Assets/Brand/keyglo_logo_v2_2172x724.png`.

**Do not typeset the name.** The wordmark is artwork; the logo usage guide
in the spec pack forbids reproducing it with a system font.

## Copy

`SALES-PAGE-SCRIPT.md` — the full page deck, a 60-second voiceover cut, four
ad variants, and store listing text. It ends with a short list of claims to
avoid; read that before writing anything new.

## The one rule

Every claim in the copy is something the plugin does today. If a feature
gets cut or changed, the copy changes with it — a demo that promises
something the download doesn't do is the fastest way to lose the people who
tried it.
