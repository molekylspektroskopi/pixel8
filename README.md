# Pixel8

> **Heads-up:** this project is built entirely with [Claude Code](https://claude.com/claude-code)
> by someone who can't code. I describe what I want on my watch; Claude writes the
> code. Read, reuse, and judge the source with that in mind.

A feature-rich watchface for **Pebble Time 2** (emery, 200×228 color), built around
full-screen pixel-art wallpapers with floating 3D info cards.

[**Get it on the Rebble store**](https://apps.rePebble.com/90a5c4131c6444bf9d5def66)

![default layout](screenshots/emery_1_default_week.png)
![clock card](screenshots/emery_2_clock_card.png)
![18px Japanese calendar](screenshots/emery_3_font18_kanji.png)
![cards off](screenshots/emery_4_no_cards.png)

## Features

- **Clock panel** — time (LECO 38), date in 4 formats, optional ISO week number
  (`v.28`), AM/PM in 12h mode. Optional card background that hugs the text width.
- **Info panel** — four sortable, individually toggleable rows:
  - *Battery*: phone + watch, each shown as a chunky pixel-art gauge with a
    color gradient fill, plus a charging bolt icon
  - *Steps*: today's steps, distance, heart rate; row turns green at your step goal
  - *Weather*: current + up to 3 forecast columns (N hours ahead, tomorrow, day+2)
    with pixel icons — powered by [Open-Meteo](https://open-meteo.com/), no API key
  - *Sleep*: last night's sleep as hours and minutes
- **Calendar panel** — next 1–5 events from up to two iCal/ICS feeds (Google
  Calendar, Nextcloud, Apple Calendar, …), with multi-day ranges right-aligned and
  customisable "Today"/"Tomorrow" labels (NOW, TMR, Idag, …). Events also become
  Timeline pins with an *Open Agenda* action.
- **Wallpaper** — a solid colour, a PNG fetched from any URL, or a `.list` file
  that rotates through a different image each day.
- **Layout** — the three panels can be reordered or hidden; the bottom panel is
  anchored to the screen edge. Per-panel card toggle, padding (0–24 px), and font
  size (14/18/24 px) with smart row fitting — larger fonts work great with CJK
  language packs.
- **Extras** — AppGlance shows your next event in the launcher; Timeline Quick
  View is respected (layout reflows around it).

Settings live in a self-contained HTML page (no Clay dependency) organised as a
per-panel accordion.

## Battery-conscious design

The face redraws once a minute and avoids everything expensive:

- No health event subscription — the firmware fires movement updates several times
  a second while walking; instead health is polled on the minute tick, and only
  for rows that are visible.
- The phone is polled once an hour; the companion JS only sends data when it
  actually changed (dedup keys), uses conditional GET (ETag/304) for calendars,
  and caches parsed events in localStorage.
- The wallpaper transfer buffer is heap-allocated only during the ~10 s of a
  transfer; the AppMessage inbox is sized to the actual chunk size.

## Building

Requires the [Pebble SDK](https://developer.rebble.io/) (tested with pebble-tool
5.x / SDK 4.17, emery only).

```bash
pebble build
pebble install --emulator emery    # or --phone <ip>
```

`src/pkjs/settings-html.js` is generated from `src/pkjs/settings.html` by
`tools/build-settings.js` on every build — edit the `.html`, not the generated file.

### Calendar feed (optional, build-time default)

The calendar URL is normally set in the settings page. To bake in a default,
copy the example and fill in your feed (the real file is git-ignored):

```bash
cp src/pkjs/calendar_config.example.js src/pkjs/calendar_config.js
```

### Hosting wallpapers

Wallpapers are fetched by the phone and streamed to the watch, so they must be
small indexed-colour PNGs (≤32 KB):

```bash
magick input.png -resize 200x228! -colors 64 -type Palette wallpaper.png
```

For a daily rotation, host a `file.list` next to the images containing one image
URL (or relative filename) per line, and point the wallpaper URL setting at the
`.list` file.

## Architecture notes

| Piece | Role |
|-------|------|
| `src/c/main.c` | All rendering (single canvas layer), AppMessage handling, persistence |
| `src/pkjs/index.js` | Companion: calendar fetch/parse, weather, phone battery, wallpaper streaming, Timeline pins |
| `src/pkjs/settings.html` | Settings UI, inlined as a `data:` URL (no external hosting) |
| `tools/build-settings.js` | Escapes settings.html into `settings-html.js` at build time |

Calendar events are formatted on the phone and sent as ready-to-draw strings
(UTF-8 byte-aware truncation, so CJK text never overflows the watch buffer).

## Version history

See [CHANGELOG.md](CHANGELOG.md).

The pixel-art wallpapers shown in the screenshots are AI-generated images from
[pxlart.com](https://pxlart.com); they are fetched at runtime and not part of
this repository.

---

*This watchface is vibe coded together with Claude for my own personal liking.*
