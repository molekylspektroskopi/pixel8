# Changelog

## v3.24.0
- Wallpaper can now rotate between two images at set times of day — a day
  wallpaper and a night wallpaper. Works for uploaded images (upload a
  second one in settings) and for URLs (fill in a second wallpaper URL);
  uploads still take priority over URLs when both are set. Set "Day starts"
  and "Night starts" and the watch switches automatically, checked about
  once an hour. With only one image/URL set, nothing changes

## v3.23.0
- Wallpaper can now be uploaded directly from the settings page instead of
  hosting a URL — pick a PNG on your phone and it's sent straight to the
  watch over Bluetooth using the same chunked transfer as URL wallpapers.
  The file must already be exactly 200×228 px, indexed/palette color, and
  32 KB or smaller (same requirements as the URL flow); the settings page
  validates and rejects anything else rather than silently downscaling it.
  Uploading an image overrides the wallpaper URL while it's set

## v3.22.0
- Removed the battery time-remaining estimate feature (watch/phone settings,
  on-watch rate tracking, persisted history) — the periodic rate-tracking
  writes measurably increased battery drain, and the estimates stayed
  unreliable even after the v3.21.1 accuracy fix. Battery rows are now
  always the pixel-art percentage gauge introduced in v3.21.0

## v3.21.1
- Fixed battery time-remaining estimate being wildly too pessimistic: the
  on-watch rate tracker was resetting its reference point on every battery
  callback, even when the percentage hadn't actually changed. Since battery
  percent only updates in coarse steps, this attributed each real change to
  just the last few minutes instead of the true multi-hour gap, hugely
  overstating the drain rate. Estimates now only advance on an actual
  percent change, so the elapsed time is measured correctly
- Settings page now notes that the estimate takes a day or two (watch) or a
  few hours (phone) to learn your actual drain rate before it stops showing "..."

## v3.21.0
- Battery row: watch and phone can each independently show estimated time
  remaining in place of the bar, derived on-watch from a smoothed %/hour
  rate tracked across battery readings — no reliance on unsupported phone
  battery-time APIs. The percentage keeps its normal bar-mode position;
  only the bar itself swaps for the estimate text (or "..." while a rate
  is still being learned), so switching modes never moves the % around
- Battery bar redesigned: chunky pixel-art gauge (black shell, terminal nub,
  gradient fill), as tall as the phone/watch icon row, replacing the old
  thin flat two-tone strip

## v3.20.2
- Optimization pass: wallpaper receive buffer heap-allocated only during transfers
  (−32 KB static RAM), AppMessage inbox right-sized, webpack source map stripped
  from the pbw (187 → 99 KB), leaner per-minute redraw (4-offset time outline,
  weather parsed once on receipt, cached clock-card measurements), health metrics
  polled only for visible rows

## v3.20.1
- Calendar "rows to show" gained the missing 4-row option

## v3.20.0
- ISO week number on the date (`v.28`), toggleable
- Per-panel 3D card toggles, including a new card behind the clock that hugs the
  text width
- Per-panel content padding (0–24 px)
- Custom "Today"/"Tomorrow" labels (NOW, TMR, Idag, …)
- Selectable font size for Info and Calendar panels (14/18/24 px) with smart row
  fitting — pairs well with CJK language packs
- UTF-8 byte-aware event truncation (CJK text can no longer overflow or split)
- Settings page reorganised as a per-panel accordion; dropped the unused
  pebble-clay dependency

## v3.19.x
- v3.19.4: battery optimizations — health polled on the minute tick instead of
  subscribing to movement events (which fire several times a second while
  walking); phone polled hourly
- v3.19.0: AM/PM indicator (12 h mode), step-goal colour highlight, sleep row

## v3.18.0
- Clock / Info / Calendar became three freely orderable major panels; the bottom
  panel is pinned to the screen edge

## v3.17.0
- Timeline pins gained an "Open Agenda" action; drag-to-reorder panels in settings

## v3.16.0
- Heart rate on the steps row, phone-charging bolt icon

## v3.15.x
- Reliability: phone battery no longer disappears or freezes after reconnect

## v3.14.x
- Replaced Clay with a fully custom HTML settings page (data: URL, no hosting)
- Fixed Android WebView quirks; weather 4-column overflow

## v3.13.x
- Info row ordering (all 24 permutations), background colour picker
- Long tail of wallpaper/reconnect fixes: relative `.list` paths, decode OOM
  crash, dedup keys cleared on reconnect, wallpaper survival across launcher
  visits

## v3.12.0
- Wallpaper system rewrite: images fetched from a URL (or daily-rotating `.list`)
  instead of 18 bundled bitmaps — app 95 % smaller

## v3.11.0
- Configurable weather columns; crash fixes (localtime NULL guard, strchr loop)

## v3.10.0
- 4-column weather row (current + N hours + two days ahead)

## v3.9.0
- Pixel weather icons (sun / cloud / rain / snow / …)

## v3.8.0
- Per-row toggles, four date formats, weather options

## v3.7.0
- Configurable calendar row count, Bluetooth-disconnect indicator

## v3.6.x
- Weather row (Open-Meteo), panel visibility toggles, dual calendar support

## v3.5.x
- Wallpaper picker + rotation whitelist, phone battery persisted across reboots

## v3.4
- First settings page (Clay): calendar URL + colours

## v3.0 – v3.3
- Image background, gradient battery bars, step-goal colour
- Calendar agenda replaced the notification panel; events pushed as Timeline pins
- Battery-optimized companion: conditional GET (ETag/304), localStorage cache,
  change-only sends

## v2
- Landscape background, text battery, health data

## v1.0
- Initial release: time, date, battery, notifications
