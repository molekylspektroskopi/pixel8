# Pixel8 Roadmap

Progress map and future ideas for the Pixel8 watchface (Pebble Time 2 / emery).

---

## Done

### Core display
- [x] Large time + date, 4 date formats
- [x] Phone + watch battery row with icons and gradient progress bars
- [x] Step count + distance row
- [x] Weather row: current temp + up to 3 forecast columns (now / N-hour / tomorrow / day+2)
- [x] Weather icons for all 4 columns
- [x] Day labels on forecast (none / 2-letter / 3-letter)
- [x] Calendar events: next 1–5 events from iCal/ICS feed
- [x] Multi-day event date range (right-aligned on same row)

### Settings
- [x] Week number toggle on the date row (v.28) (v3.20)
- [x] Per-panel card backgrounds: clock card new, info/calendar cards can be turned off (v3.20)
- [x] Per-panel content padding, X and Y separately (v3.20)
- [x] Custom "Today"/"Tomorrow" labels with suggestions + free text (v3.20)
- [x] Info & Calendar font size: 14/18/24 px, with row-fit clamping and ellipsis truncation (v3.20)
- [x] Toggle battery / steps / weather / calendar rows independently
- [x] Reorderable panel rows (6 permutations of battery / steps / weather)
- [x] Calendar row count (1 / 2 / 3 / 5)
- [x] Forecast column count (0–3)
- [x] Forecast hours ahead (1–24 h)
- [x] Fixed location for weather (skip GPS)
- [x] Custom colors: panel box, text, clock & date
- [x] Background color (when no wallpaper)
- [x] Dual calendar feeds (merged + deduped by UID)

### Wallpaper
- [x] Dynamic wallpaper from URL (PNG, 200×228, max ~32 KB)
- [x] Daily rotation via `.list` file (one URL per line, rotates by day index)
- [x] Relative URLs in .list resolved against list URL directory
- [x] Once-per-day throttle; settings save forces re-fetch
- [x] Chunk-based Bluetooth transfer with retry (26 × 900-byte chunks)

### Stability / reliability
- [x] Reconnect handling: weather + calendar re-sent fresh on every watch connect
- [x] All 4 weather icons cached and re-sent on reconnect
- [x] Wallpaper bitmap swap: destroy old before decode (OOM crash fix)
- [x] Calendar ETag conditional GET (304 when unchanged, saves data)
- [x] Timeline pins (genericPin, not calendarPin — renders on PT2 firmware)
- [x] Stale pin cleanup on each sync
- [x] Weather fixed-gap centering (compact columns)

---

## Known bugs

- ~~**Wallpaper disappears on launcher return**~~ — fixed v3.13.7 (startup timer sends WP_HAVE immediately)
- **Calendar double-save**: After fresh install + settings save, events sometimes don't arrive on first press. Second save works. Root cause unclear.

---

## Ideas / Future work

### Fixes
- [ ] Clear `wpFetchedAt` when app version changes (detect via stored version string in localStorage)
- [ ] Root-cause calendar double-save — maybe send events before wallpaper starts

### Weather
- [ ] Wind speed + direction
- [ ] Precipitation probability (%)
- [ ] UV index
- [ ] Sunrise / sunset times
- [ ] Feels-like temperature
- [ ] Humidity

### Calendar
- [ ] RRULE recurring event expansion (currently skipped)
- [ ] Color-code events by calendar (if multiple feeds)
- [ ] Tap to scroll through more events (beyond 5)

### Wallpaper / appearance
- [ ] Tap to cycle to next wallpaper from .list (skip ahead)
- [ ] Per-platform palette auto-conversion hint in docs
- [ ] Animated wallpaper / subtle background effects (if memory allows)

### Display
- [ ] Moon phase indicator
- [ ] Air quality index (AQI)
- [ ] Second custom text row (user-defined string via settings)
- [ ] Analog clock option (hands over the digital face)
- [ ] Notification peek improvements (longer text, swipe to dismiss)

### Performance / internals
- [ ] Lazy-load wallpaper: only fetch when s_background is NULL (instead of timer-based)
- [ ] Compress weather line: drop separators, use fixed-width fields to save AppMessage bytes
- [ ] Background fetch scheduling aligned to watch REQUEST_STATUS ticks (avoid double-fetch race)

---

## Version history (recent)

| Version | Summary |
|---------|---------|
| v3.22.0 | Removed battery time-remaining estimate (settings + on-watch rate tracking) — cost more battery than it was worth and stayed unreliable; battery rows are always the pixel-art percentage gauge now |
| v3.21.1 | Fixed battery time-remaining estimate overstating drain rate (anchor was resetting on no-op ticks); settings page notes the learning period |
| v3.21.0 | Battery row: optional estimated time remaining (watch/phone independently), redesigned pixel-art battery gauge |
| v3.20.2 | Optimization pass: wallpaper buffer heap-allocated per transfer (−32 KB static RAM), right-sized AppMessage inbox, source map stripped from pbw (187→99 KB), leaner per-minute redraw (4-offset outline, pre-parsed weather, cached clock measurements), health polled only for visible rows |
| v3.20.1 | Calendar rows setting gained the missing 4-row option |
| v3.20.0 | Week number toggle; clock card + per-panel card toggles; per-panel padding; custom Today/Tomorrow labels; info/calendar font size (14/18/24 px) with row-fit clamping; dropped unused pebble-clay |
| v3.13.5 | Fix missing forecast weather icons after reconnect; fix wallpaper re-fetching on menu return |
| v3.13.4 | Fix weather + calendar not showing after reconnect — dedup keys (sentWeather/sentEvents) now cleared on every watch connect |
| v3.13.3 | Crash fix: destroy old wallpaper bitmap before decode to avoid OOM with corrupt non-NULL return |
| v3.13.2 | Wallpaper .list relative URL resolution (403 fix) |
| v3.13.1 | BG_COLOR GColorFromHEX fix; weather columns compact fixed-gap centering |
| v3.13.0 | Panel row ordering; background color setting; weather column inner padding |
| v3.12   | Wallpaper system rewrite — URL/list based, 18 bundled images removed, app 95% smaller |
| v3.11   | Crash fixes: strtok → strchr, localtime null guard |
| v3.3    | Battery row icon layout + progress bars |
| v3.2    | Icon battery row, bottom-anchored calendar card |
| v3.1    | Smaller notification fonts, tap peek popup |
| v3      | Image background, gradient battery, step goal colour |
