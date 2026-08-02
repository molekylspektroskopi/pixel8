/**
 * Pixel8 watchface companion (PebbleKit JS)
 *
 *  - Sends the phone battery level to the watch (only when it changes).
 *  - Fetches calendar events from an ICS URL and drives both the on-face
 *    next-3 glance (EVENT_0/1/2) and Pebble timeline pins.
 *
 * Battery-conscious design (see Pebble battery best-practices):
 *  - Conditional GET (ETag / If-None-Match): when the calendar is unchanged the
 *    server returns 304 with 0 bytes, so we skip the ~600KB download.
 *  - Parsed events are cached in localStorage; the glance recomputes from cache
 *    with no network at all.
 *  - The network is only hit at most every FETCH_MIN_INTERVAL (else cache).
 *  - AppMessages are only sent when the data actually changes.
 *  - Timeline pins are only re-pushed when the calendar changes (or a 6h safety).
 *  - The watch's hourly status request is the single refresh trigger.
 *
 * The calendar URL lives in calendar_config.js (git-ignored).
 */

var CFG = require('./calendar_config');
var TIMELINE_API = (CFG && CFG.TIMELINE_API) || 'https://timeline-api.rebble.io';

// Settings page (Clay). Calendar URL/colors come from the phone settings,
// falling back to calendar_config.js when unset.
var settingsHtml = require('./settings-html');
// Memoized — settings only change via webviewclosed, which refreshes the cache
var _settingsCache = null;
function claySettings() {
  if (_settingsCache) return _settingsCache;
  try { _settingsCache = JSON.parse(localStorage.getItem('clay-settings') || '{}'); } catch (e) { _settingsCache = {}; }
  return _settingsCache;
}
function calUrl()   { return claySettings().CALENDAR_URL   || ''; }
function calUser()  { return claySettings().CALENDAR_USER  || ''; }
function calPass()  { return claySettings().CALENDAR_PASS  || ''; }
function calUrl2()  { return claySettings().CALENDAR_URL_2  || ''; }
function calUser2() { return claySettings().CALENDAR_USER_2 || ''; }
function calPass2() { return claySettings().CALENDAR_PASS_2 || ''; }

// Coerce a Clay value to a number (colors must reach the watch as int32).
function toInt(v) {
  if (typeof v === 'number') return v & 0xFFFFFF;
  if (typeof v === 'string') { var n = parseInt(v.replace(/^#|^0x/i, ''), 16); return isNaN(n) ? 0 : n; }
  return 0;
}
var DAY = 24 * 60 * 60 * 1000;
var PIN_PAST_MS = 30 * DAY;            // pin/cache events back this far
var PIN_FUTURE_MS = 90 * DAY;          // pin events up to this far ahead
var CACHE_FUTURE_MS = 180 * DAY;       // cache (for glance) up to this far ahead
var FETCH_MIN_INTERVAL_MS = 2 * 60 * 60 * 1000;  // min time between network checks
var PIN_PUSH_INTERVAL_MS = 6 * 60 * 60 * 1000;   // safety re-sync of pins

function lsGet(k) { try { return localStorage.getItem(k); } catch (e) { return null; } }
function lsSet(k, v) { try { localStorage.setItem(k, v); } catch (e) {} }

// ---------------------------------------------------------------------------
// Weather — Open-Meteo (free, no API key)
// ---------------------------------------------------------------------------
var WEATHER_INTERVAL_MS = 30 * 60 * 1000;

function wmoToStr(code) {
  if (code === 0)   return 'Clear';
  if (code <= 2)    return 'P.Cloudy';
  if (code <= 3)    return 'Overcast';
  if (code <= 48)   return 'Fog';
  if (code <= 55)   return 'Drizzle';
  if (code <= 65)   return 'Rain';
  if (code <= 77)   return 'Snow';
  if (code <= 82)   return 'Showers';
  if (code <= 86)   return 'Snow Showers';
  return 'Storm';
}

function wmoToIcon(code) {
  if (code === 0)   return 0; // clear/sunny
  if (code <= 2)    return 1; // partly cloudy
  if (code === 3)   return 2; // overcast
  if (code <= 48)   return 8; // fog/rime
  if (code <= 57)   return 3; // drizzle
  if (code <= 64)   return 3; // light-moderate rain
  if (code <= 67)   return 4; // heavy/freezing rain
  if (code <= 73)   return 5; // light-moderate snow
  if (code <= 77)   return 6; // heavy snow/ice
  if (code <= 82)   return 4; // rain showers
  if (code <= 86)   return 6; // snow showers
  if (code === 95)  return 8; // thunderstorm
  return 8;                   // hail/unknown
}

function doWeatherFetch(lat, lon) {
  if (typeof lat !== 'number' || isNaN(lat) || typeof lon !== 'number' || isNaN(lon)) return;
  var s = claySettings();
  var hours = parseInt(s.WEATHER_HOURS || '4', 10);
  if (isNaN(hours) || hours < 1 || hours > 24) hours = 4;

  var url = 'https://api.open-meteo.com/v1/forecast' +
            '?latitude=' + lat.toFixed(4) + '&longitude=' + lon.toFixed(4) +
            '&current=temperature_2m,weather_code' +
            '&hourly=temperature_2m,weather_code&forecast_hours=25' +
            '&daily=temperature_2m_max,temperature_2m_min,weather_code' +
            '&timezone=auto&forecast_days=3';
  var xhr = new XMLHttpRequest();
  xhr.open('GET', url, true);
  xhr.onload = function () {
    if (xhr.status !== 200) { console.log('Pixel8: weather HTTP ' + xhr.status); return; }
    try {
      var d = JSON.parse(xhr.responseText);
      var nowT = Math.round(d.current.temperature_2m);
      var nowIcon = wmoToIcon(d.current.weather_code);

      // N-hour forecast from hourly data
      var foreT = nowT, foreIcon = nowIcon;
      if (d.hourly) {
        var td = new Date(Date.now() + hours * 3600000);
        var targetStr = td.getFullYear() + '-' + pad2(td.getMonth() + 1) + '-' +
                        pad2(td.getDate()) + 'T' + pad2(td.getHours());
        var htimes = d.hourly.time, htemps = d.hourly.temperature_2m, hcodes = d.hourly.weather_code;
        for (var i = 0; i < htimes.length; i++) {
          if (htimes[i].slice(0, 13) === targetStr) {
            foreT = Math.round(htemps[i]);
            if (hcodes) foreIcon = wmoToIcon(hcodes[i]);
            break;
          }
        }
      }

      // Day+1 and day+2 from daily max and min
      var day1T = nowT, day1Icon = nowIcon, day2T = nowT, day2Icon = nowIcon;
      var day1Min = '', day2Min = '';
      if (d.daily && d.daily.temperature_2m_max && d.daily.temperature_2m_max.length >= 3) {
        day1T = Math.round(d.daily.temperature_2m_max[1]);
        day2T = Math.round(d.daily.temperature_2m_max[2]);
        if (d.daily.weather_code) {
          day1Icon = wmoToIcon(d.daily.weather_code[1]);
          day2Icon = wmoToIcon(d.daily.weather_code[2]);
        }
        if (d.daily.temperature_2m_min && d.daily.temperature_2m_min.length >= 3) {
          day1Min = Math.round(d.daily.temperature_2m_min[1]);
          day2Min = Math.round(d.daily.temperature_2m_min[2]);
        }
      }

      // Pipe-delimited: curT|foreT|hours|day1Max|day2Max|day1Min|day2Min
      // day1Min/day2Min are omitted if unavailable (backwards compatible)
      var line = nowT + '|' + foreT + '|' + hours + '|' + day1T + '|' + day2T +
                 (day1Min !== '' ? '|' + day1Min + '|' + day2Min : '');
      lsSet('weatherLine', line);
      lsSet('weatherAt', '' + Date.now());
      var sentIcon = parseInt(lsGet('sentIcon') || '-1', 10);
      if (line !== lsGet('sentWeather') || nowIcon !== sentIcon) {
        lsSet('sentWeather', line);
        lsSet('sentIcon',  '' + nowIcon);
        lsSet('sentIconF', '' + foreIcon);
        lsSet('sentIcon1', '' + day1Icon);
        lsSet('sentIcon2', '' + day2Icon);
        console.log('Pixel8: weather ' + line + ' icon=' + nowIcon);
        Pebble.sendAppMessage({
          'WEATHER_LINE':   line,
          'WEATHER_ICON':   nowIcon,
          'WEATHER_ICON_F': foreIcon,
          'WEATHER_ICON_1': day1Icon,
          'WEATHER_ICON_2': day2Icon
        }, function() {
          console.log('Pixel8: weather sent ok');
        }, function(err) {
          console.log('Pixel8: weather send failed: ' + JSON.stringify(err));
        });
      }
    } catch (ex) {
      console.log('Pixel8: weather parse error: ' + ex);
    }
  };
  xhr.onerror = function () { console.log('Pixel8: weather fetch failed'); };
  xhr.send();
}

function fetchWeather() {
  var s = claySettings();
  if (s.WEATHER_FIXED) {
    var lat = parseFloat(s.WEATHER_LAT || '');
    var lon = parseFloat(s.WEATHER_LON || '');
    if (!isNaN(lat) && !isNaN(lon)) { doWeatherFetch(lat, lon); return; }
  }
  navigator.geolocation.getCurrentPosition(
    function (pos) { doWeatherFetch(pos.coords.latitude, pos.coords.longitude); },
    function (err) { console.log('Pixel8: geolocation error: ' + err.message); },
    { timeout: 15000, maximumAge: 300000 }
  );
}

function refreshWeather(force) {
  // Always re-send cached data immediately — restores the row after reinstall/reconnect
  // without waiting for the ~20s geolocation + XHR cycle
  var cached = lsGet('weatherLine');
  if (cached && cached !== lsGet('sentWeather')) {
    var cachedIcon  = parseInt(lsGet('sentIcon')  || '8', 10);
    var cachedIconF = parseInt(lsGet('sentIconF') || '8', 10);
    var cachedIcon1 = parseInt(lsGet('sentIcon1') || '8', 10);
    var cachedIcon2 = parseInt(lsGet('sentIcon2') || '8', 10);
    console.log('Pixel8: re-sending cached weather');
    lsSet('sentWeather', cached);
    Pebble.sendAppMessage({
      'WEATHER_LINE':   cached,
      'WEATHER_ICON':   cachedIcon,
      'WEATHER_ICON_F': cachedIconF,
      'WEATHER_ICON_1': cachedIcon1,
      'WEATHER_ICON_2': cachedIcon2
    });
  }
  var last = parseInt(lsGet('weatherAt') || '0', 10);
  if (force || (Date.now() - last) > WEATHER_INTERVAL_MS) {
    fetchWeather();
  }
}

// ---------------------------------------------------------------------------
// Wallpaper — fetched as PNG chunks; one refresh per day
// ---------------------------------------------------------------------------
var WP_CHUNK = 1800;  // bytes per AppMessage chunk (inbox opened at max size)
var wpInFlight = false;  // prevents concurrent wallpaper transfers

function sendWpChunk(buf, offset, total, retries) {
  if (retries === undefined) retries = 0;
  if (offset >= total) {
    Pebble.sendAppMessage({ 'WP_DONE': 1 },
      function() { wpInFlight = false; console.log('Pixel8: wallpaper transfer done (' + total + 'B)'); },
      function() { wpInFlight = false; console.log('Pixel8: WP_DONE failed'); });
    return;
  }
  var end = Math.min(offset + WP_CHUNK, total);
  // Pebble.sendAppMessage requires a plain Array, not a TypedArray
  var view = new Uint8Array(buf, offset, end - offset);
  var chunk = [];
  for (var i = 0; i < view.length; i++) chunk[i] = view[i];
  Pebble.sendAppMessage({ 'WP_DATA': chunk },
    function() { sendWpChunk(buf, end, total, 0); },
    function() {
      if (retries < 4) {
        console.log('Pixel8: WP chunk retry ' + (retries + 1) + ' at offset ' + offset);
        setTimeout(function() { sendWpChunk(buf, offset, total, retries + 1); }, 600);
      } else {
        console.log('Pixel8: WP chunk failed after 4 retries at offset ' + offset);
      }
    });
}

function sendWallpaper(arrayBuffer) {
  if (!arrayBuffer) { wpInFlight = false; console.log('Pixel8: wallpaper response is null (arraybuffer not supported?)'); return; }
  var total = arrayBuffer.byteLength;
  // Check PNG magic bytes: must start with 0x89 P N G
  var hdr = new Uint8Array(arrayBuffer, 0, Math.min(4, total));
  if (total < 4 || hdr[0] !== 0x89 || hdr[1] !== 0x50 || hdr[2] !== 0x4E || hdr[3] !== 0x47) {
    wpInFlight = false;
    console.log('Pixel8: wallpaper is not a PNG (header: 0x' +
      hdr[0].toString(16) + hdr[1].toString(16) + ') — must be a 200x228 PNG');
    return;
  }
  if (total > 32768) {
    wpInFlight = false;
    console.log('Pixel8: wallpaper too large (' + total + 'B, max 32768) — re-export as indexed-color PNG');
    return;
  }
  console.log('Pixel8: sending wallpaper ' + total + 'B in ~' + Math.ceil(total / WP_CHUNK) + ' chunks');
  function sendTotal(retries) {
    Pebble.sendAppMessage({ 'WP_TOTAL': total },
      function() { sendWpChunk(arrayBuffer, 0, total, 0); },
      function() {
        if (retries < 3) {
          console.log('Pixel8: WP_TOTAL retry ' + (retries + 1));
          setTimeout(function() { sendTotal(retries + 1); }, 1000);
        } else {
          console.log('Pixel8: WP_TOTAL failed after 3 retries');
        }
      });
  }
  sendTotal(0);
}

function fetchWallpaperImage(url) {
  var xhr = new XMLHttpRequest();
  xhr.open('GET', url, true);
  xhr.responseType = 'arraybuffer';
  xhr.onload = function() {
    if (xhr.status !== 200) { console.log('Pixel8: wallpaper HTTP ' + xhr.status); return; }
    sendWallpaper(xhr.response);
  };
  xhr.onerror = function() { wpInFlight = false; console.log('Pixel8: wallpaper fetch failed'); };
  xhr.send();
}

function fetchWallpaperList(listUrl) {
  var xhr = new XMLHttpRequest();
  xhr.open('GET', listUrl, true);
  xhr.onload = function() {
    if (xhr.status !== 200) { console.log('Pixel8: wallpaper list HTTP ' + xhr.status); return; }
    var lines = xhr.responseText.split('\n')
      .map(function(l) { return l.trim(); })
      .filter(function(l) { return l.length > 0 && l[0] !== '#'; });
    if (!lines.length) { console.log('Pixel8: wallpaper list empty'); return; }
    var day = Math.floor(Date.now() / 86400000);
    var picked = lines[day % lines.length];
    // Resolve relative paths against the list URL's directory
    var url = picked.match(/^https?:\/\//) ? picked
              : listUrl.replace(/\/[^\/]*$/, '/') + picked;
    console.log('Pixel8: wallpaper pick [' + (day % lines.length) + '] ' + url);
    fetchWallpaperImage(url);
  };
  xhr.onerror = function() { wpInFlight = false; console.log('Pixel8: wallpaper list fetch failed'); };
  xhr.send();
}

function refreshWallpaper(force) {
  if (wpInFlight) { console.log('Pixel8: wallpaper already in flight, skipping'); return; }
  var s = claySettings();
  var url = (s.WALLPAPER_URL || '').trim();
  if (!url) return;
  var last = parseInt(lsGet('wpFetchedAt') || '0', 10);
  if (!force && (Date.now() - last) < 86400000) return;   // once per day
  lsSet('wpFetchedAt', '' + Date.now());
  wpInFlight = true;
  if (/\.list$/i.test(url)) {
    fetchWallpaperList(url);
  } else {
    fetchWallpaperImage(url);
  }
}

// ---------------------------------------------------------------------------
// Phone battery — pushed on levelchange, polled on REQUEST_STATUS fallback
// ---------------------------------------------------------------------------
var s_bat_mgr = null;

function emitBattery(pct, charging) {
  // Encode charging state: 100+pct when charging, plain pct when not.
  var val = charging ? (100 + pct) : pct;
  if (('' + val) === lsGet('sentBattery')) return;
  Pebble.sendAppMessage({ 'PHONE_BATTERY': val });
  lsSet('sentBattery', '' + val);
  console.log('Pixel8: battery ' + pct + '% charging=' + charging);
}

function attachBatteryManager(mgr) {
  s_bat_mgr = mgr;
  mgr.addEventListener('levelchange', function () {
    emitBattery(Math.round(mgr.level * 100), mgr.charging);
  });
  emitBattery(Math.round(mgr.level * 100), mgr.charging);
}

function sendPhoneBattery() {
  try {
    if (s_bat_mgr) {
      emitBattery(Math.round(s_bat_mgr.level * 100), s_bat_mgr.charging);
    } else if (typeof navigator !== 'undefined' && navigator.getBattery) {
      navigator.getBattery().then(attachBatteryManager,
        function () { console.log('Pixel8: getBattery() rejected'); });
    } else if (typeof navigator !== 'undefined' && navigator.battery &&
               typeof navigator.battery.level === 'number') {
      attachBatteryManager(navigator.battery);
    }
  } catch (e) {
    console.log('Pixel8: battery error: ' + e);
  }
}

// ---------------------------------------------------------------------------
// Base64 encode (HTTP Basic auth)
// ---------------------------------------------------------------------------
function base64(str) {
  if (typeof btoa === 'function') return btoa(str);
  var chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  var out = '', i = 0;
  while (i < str.length) {
    var c1 = str.charCodeAt(i++), c2 = str.charCodeAt(i++), c3 = str.charCodeAt(i++);
    out += chars.charAt(c1 >> 2) + chars.charAt(((c1 & 3) << 4) | (c2 >> 4)) +
           chars.charAt(isNaN(c2) ? 64 : (((c2 & 15) << 2) | (c3 >> 6))) +
           chars.charAt(isNaN(c3) ? 64 : (c3 & 63));
  }
  return out;
}

// ---------------------------------------------------------------------------
// ICS parsing
// ---------------------------------------------------------------------------
function unfold(text) { return text.replace(/\r?\n[ \t]/g, ''); }
function unescapeText(s) {
  return s.replace(/\\n/gi, ' ').replace(/\\,/g, ',')
          .replace(/\\;/g, ';').replace(/\\\\/g, '\\').trim();
}
function parseICSDate(value, params) {
  var allDay = /VALUE=DATE(?!-TIME)/i.test(params);
  var m = value.match(/(\d{4})(\d{2})(\d{2})(?:T(\d{2})(\d{2})(\d{2})(Z)?)?/);
  if (!m) return null;
  var y = +m[1], mo = +m[2] - 1, d = +m[3], H = +(m[4] || 0), Mi = +(m[5] || 0), S = +(m[6] || 0);
  var date = (m[7] === 'Z') ? new Date(Date.UTC(y, mo, d, H, Mi, S)) : new Date(y, mo, d, H, Mi, S);
  return { date: date, allDay: allDay };
}
function parseEvents(ics) {
  var lines = unfold(ics).split(/\r?\n/);
  var events = [], cur = null;
  for (var i = 0; i < lines.length; i++) {
    var line = lines[i];
    if (line === 'BEGIN:VEVENT') { cur = {}; continue; }
    if (line === 'END:VEVENT') { if (cur && cur.start) events.push(cur); cur = null; continue; }
    if (!cur) continue;
    var idx = line.indexOf(':');
    if (idx < 0) continue;
    var key = line.substring(0, idx), val = line.substring(idx + 1);
    var name = key.split(';')[0].toUpperCase(), params = key.substring(name.length);
    if (name === 'DTSTART') { var ps = parseICSDate(val, params); if (ps) { cur.start = ps.date; cur.allDay = ps.allDay; } }
    else if (name === 'DTEND') { var pe = parseICSDate(val, params); if (pe) cur.end = pe.date; }
    else if (name === 'SUMMARY') cur.summary = unescapeText(val);
    else if (name === 'LOCATION') cur.location = unescapeText(val);
    else if (name === 'UID') cur.uid = val.trim();
  }
  return events;
}

// ---------------------------------------------------------------------------
// Event cache (slim, per-calendar prefix in localStorage)
// ---------------------------------------------------------------------------
function cacheEventsTo(pfx, events) {
  var lo = Date.now() - PIN_PAST_MS, hi = Date.now() + CACHE_FUTURE_MS;
  var slim = [];
  for (var i = 0; i < events.length; i++) {
    var e = events[i], t = e.start.getTime();
    if (t < lo || t > hi) continue;
    slim.push([t, e.end ? e.end.getTime() : 0, e.allDay ? 1 : 0,
               e.summary || '', e.uid || '', e.location || '']);
  }
  lsSet(pfx + 'Events', JSON.stringify(slim));
}
function loadEventsFrom(pfx) {
  var arr;
  try { arr = JSON.parse(lsGet(pfx + 'Events') || '[]'); } catch (e) { arr = []; }
  return arr.map(function (a) {
    return { start: new Date(a[0]), end: a[1] ? new Date(a[1]) : null,
             allDay: !!a[2], summary: a[3], uid: a[4], location: a[5] };
  });
}
// Merge two event arrays, deduplicate by UID, sort by start time
function mergeEvents(a, b) {
  var seen = {}, merged = [];
  a.concat(b).forEach(function (e) {
    var key = e.uid || (e.start.getTime() + ':' + (e.summary || ''));
    if (!seen[key]) { seen[key] = true; merged.push(e); }
  });
  merged.sort(function (x, y) { return x.start - y.start; });
  return merged;
}

// ---------------------------------------------------------------------------
// Watch-face glance (next 3) — only sent on change
// ---------------------------------------------------------------------------
var MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
function pad2(n) { return n < 10 ? '0' + n : '' + n; }
function sameDay(a, b) {
  return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate();
}
function eventEnd(e) {
  if (e.end) return e.end.getTime();
  return e.allDay ? (e.start.getTime() + DAY) : e.start.getTime();
}
// User-customizable "Today"/"Tomorrow" labels (e.g. NOW / TMR), max 10 chars
function calLabel(key, dflt) {
  var v = claySettings()[key];
  return (typeof v === 'string' && v.trim()) ? v.trim().substring(0, 10) : dflt;
}
function formatEvent(ev) {
  var now = new Date(), nowMs = now.getTime();
  var ongoing = ev.start.getTime() <= nowMs && eventEnd(ev) > nowMs;
  var tomorrow = new Date(now); tomorrow.setDate(now.getDate() + 1);
  var d = ev.start, when;
  if (ongoing || sameDay(d, now)) when = calLabel('LABEL_TODAY', 'Today');
  else if (sameDay(d, tomorrow)) when = calLabel('LABEL_TOMORROW', 'Tomorrow');
  else when = d.getDate() + ' ' + MONTHS[d.getMonth()];
  var time = (ev.allDay || ongoing) ? '' : pad2(d.getHours()) + ':' + pad2(d.getMinutes());
  var label = when + (time ? ' ' + time : '');
  // Truncate by UTF-8 BYTES (watch buffer is 64 bytes incl. optional "\trange");
  // CJK is 3 bytes/char, so a char-based cut could overflow / split a glyph.
  return utf8Truncate(label + '  ' + (ev.summary || '(no title)'), 48);
}
// Cut a string so its UTF-8 encoding fits maxBytes, never splitting a char (ES5-safe)
function utf8Truncate(s, maxBytes) {
  var bytes = 0, out = '';
  for (var i = 0; i < s.length; i++) {
    var c = s.charCodeAt(i), pair = '';
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.length) {   // surrogate pair (4 bytes)
      pair = s.charAt(i) + s.charAt(i + 1);
      if (bytes + 4 > maxBytes) break;
      bytes += 4; out += pair; i++;
      continue;
    }
    var n = c < 0x80 ? 1 : c < 0x800 ? 2 : 3;
    if (bytes + n > maxBytes) break;
    bytes += n;
    out += s.charAt(i);
  }
  return out;
}
function dayKey(d) { return d.getFullYear() * 10000 + d.getMonth() * 100 + d.getDate(); }
function rangeStr(ev) {
  if (!ev.end) return '';
  var endD = ev.allDay ? new Date(ev.end.getTime() - DAY) : ev.end;
  if (dayKey(ev.start) >= dayKey(endD)) return '';
  return ev.start.getDate() + '-' + endD.getDate() + ' ' + MONTHS[endD.getMonth()];
}
function sendGlance(events) {
  var nowMs = Date.now() - 60000;
  var up = events.filter(function (e) { return eventEnd(e) > nowMs; });
  up.sort(function (a, b) { return a.start - b.start; });
  var top = up.slice(0, 5).map(function (ev) {
    var left = formatEvent(ev), range = rangeStr(ev);
    return range ? (left + '\t' + range) : left;
  });
  var nextStart = up.length > 0 ? Math.floor(up[0].start.getTime() / 1000) : 0;
  var sig = JSON.stringify(top) + ':' + nextStart;
  if (sig === lsGet('sentEvents')) return;        // unchanged -> no Bluetooth
  lsSet('sentEvents', sig);
  console.log('Pixel8: glance changed -> sending ' + top.length);
  Pebble.sendAppMessage({
    'EVENT_0': top[0] || '', 'EVENT_1': top[1] || '', 'EVENT_2': top[2] || '',
    'EVENT_3': top[3] || '', 'EVENT_4': top[4] || '',
    'EVENT_0_START': nextStart
  });
}

// ---------------------------------------------------------------------------
// Timeline pins
// ---------------------------------------------------------------------------
function pinId(ev) {
  var base = ev.uid || (ev.start.toISOString() + '-' + (ev.summary || ''));
  return ('cev-' + base.replace(/[^a-zA-Z0-9]/g, '-')).substring(0, 63);
}
function toISO(d) { return d.toISOString().replace(/\.\d{3}Z$/, 'Z'); }
function buildPin(ev) {
  var body = ev.location || (ev.allDay ? 'All day' : pad2(ev.start.getHours()) + ':' + pad2(ev.start.getMinutes()));
  var pin = { id: pinId(ev), time: toISO(ev.start),
    layout: { type: 'genericPin', title: (ev.summary || '(no title)'), body: body,
              tinyIcon: 'system://images/TIMELINE_CALENDAR' },
    actions: [{ title: 'Open Agenda', type: 'openWatchApp', launchCode: 1 }] };
  if (ev.end) { var dur = Math.round((ev.end.getTime() - ev.start.getTime()) / 60000);
    if (dur > 0 && dur <= 1440 * 14) pin.duration = dur; }
  return pin;
}
function timelineRequest(method, token, id, body, cb) {
  var xhr = new XMLHttpRequest();
  xhr.open(method, TIMELINE_API + '/v1/user/pins/' + id, true);
  xhr.setRequestHeader('X-User-Token', token);
  if (body) xhr.setRequestHeader('Content-Type', 'application/json');
  xhr.onload = function () { cb(null, xhr.status, xhr.responseText); };
  xhr.onerror = function () { cb('neterr', 0); };
  xhr.send(body || null);
}
function pushTimeline(events) {
  var lo = Date.now() - PIN_PAST_MS, hi = Date.now() + PIN_FUTURE_MS;
  var pins = events.filter(function (e) { var t = e.start.getTime(); return t >= lo && t <= hi; }).map(buildPin);
  var currentIds = pins.map(function (p) { return p.id; });
  var prevIds = []; try { prevIds = JSON.parse(lsGet('pinIds') || '[]'); } catch (e) {}
  var stale = prevIds.filter(function (id) { return currentIds.indexOf(id) < 0; });

  if (typeof Pebble.insertTimelinePin === 'function') {
    // Native path — works offline, no token round-trip needed
    console.log('Pixel8 timeline: native; ' + pins.length + ' pins, ' + stale.length + ' to remove');
    pins.forEach(function (p) { Pebble.insertTimelinePin(JSON.stringify(p)); });
    stale.forEach(function (id) { Pebble.deleteTimelinePin(id); });
    lsSet('pinIds', JSON.stringify(currentIds));
    lsSet('lastPinPush', '' + Date.now());
  } else {
    // XHR fallback for classic Rebble app
    Pebble.getTimelineToken(function (token) {
      console.log('Pixel8 timeline: XHR; ' + pins.length + ' pins, ' + stale.length + ' to remove');
      var ok = 0, err = 0, i = 0, firstErr = '';
      function pushNext() {
        if (i >= pins.length) return delNext(0);
        timelineRequest('PUT', token, pins[i].id, JSON.stringify(pins[i]), function (e, status, resp) {
          if (e || status >= 300) { err++; if (!firstErr) firstErr = (status || e) + ' ' + (resp || '').substring(0, 60); } else ok++;
          i++; setTimeout(pushNext, 150);
        });
      }
      function delNext(j) {
        if (j >= stale.length) {
          lsSet('pinIds', JSON.stringify(currentIds));
          lsSet('lastPinPush', '' + Date.now());
          console.log('Pixel8 timeline: ' + ok + ' ok, ' + err + ' err' + (firstErr ? ' (' + firstErr + ')' : '') + ', ' + stale.length + ' removed');
          return;
        }
        timelineRequest('DELETE', token, stale[j], null, function () { setTimeout(function () { delNext(j + 1); }, 150); });
      }
      pushNext();
    }, function (err) { console.log('Pixel8 timeline: token failed: ' + JSON.stringify(err)); });
  }
}
function maybePushTimeline(events, force) {
  var last = parseInt(lsGet('lastPinPush') || '0', 10);
  if (force || (Date.now() - last) > PIN_PUSH_INTERVAL_MS) pushTimeline(events);
}

// ---------------------------------------------------------------------------
// Fetch (conditional) + dispatch — supports two calendar URLs
// ---------------------------------------------------------------------------

// Fetch one calendar; cb(events, changed) when done (may use cache)
function fetchOne(url, user, pass, pfx, force, cb) {
  var now = Date.now();
  var lastFetch = parseInt(lsGet(pfx + 'Fetched') || '0', 10);
  if (!force && (now - lastFetch) < FETCH_MIN_INTERVAL_MS) {
    cb(loadEventsFrom(pfx), false);
    return;
  }
  var xhr = new XMLHttpRequest();
  xhr.open('GET', url, true);
  var etag = lsGet(pfx + 'Etag');
  if (etag) xhr.setRequestHeader('If-None-Match', etag);
  if (user) xhr.setRequestHeader('Authorization', 'Basic ' + base64(user + ':' + pass));
  xhr.onload = function () {
    if (xhr.status === 304) {
      lsSet(pfx + 'Fetched', '' + now);
      console.log('Pixel8: ' + pfx + ' 304 (unchanged)');
      cb(loadEventsFrom(pfx), false);
      return;
    }
    if (xhr.status < 200 || xhr.status >= 300) {
      console.log('Pixel8: ' + pfx + ' HTTP ' + xhr.status);
      cb(loadEventsFrom(pfx), false);
      return;
    }
    try {
      var events = parseEvents(xhr.responseText);
      cacheEventsTo(pfx, events);
      var et = xhr.getResponseHeader && xhr.getResponseHeader('ETag');
      if (et) lsSet(pfx + 'Etag', et);
      lsSet(pfx + 'Fetched', '' + now);
      console.log('Pixel8: ' + pfx + ' 200 (' + Math.round(xhr.responseText.length / 1024) + 'KB, ' + events.length + ' events)');
      cb(loadEventsFrom(pfx), true);
    } catch (err) {
      console.log('Pixel8: ' + pfx + ' parse error: ' + err);
      cb(loadEventsFrom(pfx), false);
    }
  };
  xhr.onerror = function () {
    console.log('Pixel8: ' + pfx + ' fetch failed');
    cb(loadEventsFrom(pfx), false);
  };
  xhr.send();
}

function fetchCalendars(force) {
  var url1 = calUrl(), url2 = calUrl2();
  if (!url1 && !url2) {
    var sig = '["No calendar configured"]';
    if (sig !== lsGet('sentEvents')) {
      lsSet('sentEvents', sig);
      Pebble.sendAppMessage({ 'EVENT_0': 'No calendar configured', 'EVENT_1': '', 'EVENT_2': '' });
    }
    return;
  }
  var results = {}, pending = 0, anyChanged = false;
  function onDone() {
    var all = mergeEvents(results[1] || [], results[2] || []);
    sendGlance(all);
    maybePushTimeline(all, anyChanged || !!force);
  }
  if (url1) {
    pending++;
    fetchOne(url1, calUser(), calPass(), 'cal1', force, function (evts, changed) {
      results[1] = evts; anyChanged = anyChanged || changed;
      if (--pending === 0) onDone();
    });
  }
  if (url2) {
    pending++;
    fetchOne(url2, calUser2(), calPass2(), 'cal2', force, function (evts, changed) {
      results[2] = evts; anyChanged = anyChanged || changed;
      if (--pending === 0) onDone();
    });
  }
}

function refreshAll(force) {
  sendPhoneBattery();
  fetchCalendars(force);
  refreshWeather(force);
  refreshWallpaper(false);  // wallpaper uses its own once-per-day throttle; settings reset wpFetchedAt=0
}

Pebble.addEventListener('ready', function () {
  console.log('Pixel8 companion ready');
  // Watch just connected — its RAM state is fresh. Clear dedup keys so
  // cached data is always re-sent on reconnect, even if values unchanged.
  lsSet('sentWeather', '');
  lsSet('sentEvents', '');
  lsSet('sentBattery', '');
  refreshAll(true);
});

// The watch sends REQUEST_STATUS hourly — single refresh trigger.
// WP_HAVE=0 means the watch lost its bitmap (e.g. after returning from launcher);
// reset the throttle so refreshWallpaper sees wpFetchedAt=0 and re-fetches.
Pebble.addEventListener('appmessage', function (e) {
  if (e.payload['REQUEST_STATUS']) {
    if (e.payload['WP_HAVE'] === 0) lsSet('wpFetchedAt', '0');
    refreshAll(false);
  }
});

Pebble.addEventListener('showConfiguration', function () {
  var s = claySettings();
  var html = settingsHtml.replace('/*__CONFIG__*/', 'var INITIAL_CONFIG = ' + JSON.stringify(s) + ';');
  Pebble.openURL('data:text/html,' + encodeURIComponent(html));
});
Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) { console.log('Pixel8 settings: webviewclosed with no response'); return; }
  var data;
  try { data = JSON.parse(decodeURIComponent(e.response)); } catch (err) {
    console.log('Pixel8 settings: parse error ' + err); return;
  }
  localStorage.setItem('clay-settings', JSON.stringify(data));
  _settingsCache = data;
  var s = data;
  var payload = {
    'BOX_COLOR':   toInt(s.BOX_COLOR),
    'TEXT_COLOR':  toInt(s.TEXT_COLOR),
    'CLOCK_COLOR': toInt(s.CLOCK_COLOR)
  };

  // Display toggles — undefined means never opened settings, treat as default (show)
  var SHOW_KEYS = ['SHOW_BATTERY', 'SHOW_STEPS', 'SHOW_WEATHER', 'SHOW_CALENDAR'];
  SHOW_KEYS.forEach(function (k) { payload[k] = (s[k] !== false) ? 1 : 0; });

  payload['BATSTYLE_WATCH'] = (s.BATSTYLE_WATCH === 'true') ? 1 : 0;
  payload['BATSTYLE_PHONE'] = (s.BATSTYLE_PHONE === 'true') ? 1 : 0;

  var calRows = parseInt(s.CALENDAR_ROWS || '3', 10);
  payload['CALENDAR_ROWS'] = (calRows >= 1 && calRows <= 5) ? calRows : 3;

  var dateFormat = parseInt(s.DATE_FORMAT || '0', 10);
  payload['DATE_FORMAT'] = (dateFormat >= 0 && dateFormat <= 3) ? dateFormat : 0;

  var wxCols = parseInt(s.WEATHER_COLS || '3', 10);
  payload['WEATHER_COLS'] = (wxCols >= 0 && wxCols <= 3) ? wxCols : 3;



  var panelOrder = parseInt(s.PANEL_ORDER || '0', 10);
  payload['PANEL_ORDER'] = (panelOrder >= 0 && panelOrder <= 5) ? panelOrder : 0;

  var layoutOrder = parseInt(s.LAYOUT_ORDER || '0', 10);
  payload['LAYOUT_ORDER'] = (layoutOrder >= 0 && layoutOrder <= 5) ? layoutOrder : 0;

  payload['SHOW_CLOCK'] = (s.SHOW_CLOCK !== false) ? 1 : 0;
  payload['SHOW_SLEEP'] = (s.SHOW_SLEEP === true)  ? 1 : 0;
  var stepGoal = parseInt(s.STEP_GOAL || '0', 10);
  payload['STEP_GOAL'] = (stepGoal >= 0) ? stepGoal : 0;

  payload['BG_COLOR'] = toInt(s.BG_COLOR);

  payload['SHOW_WEEK'] = (s.SHOW_WEEK === true) ? 1 : 0;

  // Panel cards: clock defaults OFF (pre-3.20 look), info/cal default ON
  payload['CLOCK_BG'] = (s.CLOCK_BG === true)  ? 1 : 0;
  payload['INFO_BG']  = (s.INFO_BG !== false)  ? 1 : 0;
  payload['CAL_BG']   = (s.CAL_BG !== false)   ? 1 : 0;

  // Per-panel padding (px, 0-24); defaults match the pre-3.20 layout
  function padVal(v, dflt) {
    var n = parseInt(v, 10);
    return (!isNaN(n) && n >= 0 && n <= 24) ? n : dflt;
  }
  payload['CLOCK_PAD_X'] = padVal(s.CLOCK_PAD_X, 4);
  payload['CLOCK_PAD_Y'] = padVal(s.CLOCK_PAD_Y, 2);
  payload['INFO_PAD_X']  = padVal(s.INFO_PAD_X,  8);
  payload['INFO_PAD_Y']  = padVal(s.INFO_PAD_Y,  6);
  payload['CAL_PAD_X']   = padVal(s.CAL_PAD_X,   8);
  payload['CAL_PAD_Y']   = padVal(s.CAL_PAD_Y,   1);

  // Font sizes: only 14 / 18 / 24 exist as system Gothic sizes
  function fontVal(v) {
    var n = parseInt(v, 10);
    return (n === 18 || n === 24) ? n : 14;
  }
  payload['INFO_FONT_PX'] = fontVal(s.INFO_FONT_PX);
  payload['CAL_FONT_PX']  = fontVal(s.CAL_FONT_PX);

  console.log('Pixel8 settings saved: ' + JSON.stringify(payload) +
              ' url=' + (s.CALENDAR_URL ? 'set' : 'empty'));
  // URL may have changed -> force full re-fetch; clear dedup keys so data
  // is always re-sent to the watch regardless of whether it changed.
  lsSet('cal1Etag', ''); lsSet('cal1Fetched', '0');
  lsSet('cal2Etag', ''); lsSet('cal2Fetched', '0');
  lsSet('sentEvents', '');
  lsSet('sentWeather', '');
  lsSet('wpFetchedAt', '0');  // force wallpaper re-fetch on next refreshAll
  Pebble.sendAppMessage(payload,
    function () {
      console.log('Pixel8 settings: colors sent ok');
      refreshAll(true);   // calendar/battery fetch AFTER colors are acknowledged
    },
    function (err) {
      console.log('Pixel8 settings: color send FAILED ' + JSON.stringify(err));
      refreshAll(true);   // still refresh even if colors failed
    });
});
