#include <pebble.h>

// ============================================================================
// LAYOUT — emery 200x228
//
// Forest image covers full screen.
// Notification card floats over wallpaper with rounded corners and 3D bevel.
// Battery/steps drawn over forest with shadow for readability.
// ============================================================================

#define NOTIF_ROW_H    16   // px per notification row

// Clock panel — date/time drawn at offsets within the panel
#define Y_DATE          0   // date flush with clock panel content top
#define Y_TIME         18   // overlaps empty descender space of date rect; visually tight
#define CLOCK_CONTENT  56   // 18 + 38px LECO_38 cap; pad_y*2 added on top (default 2 -> 60)

// Info panel rows
#define ROW_STRIDE     17   // px between content-row baselines
#define PANEL_GAP       4   // vertical gap between major panels

// Default colors (overridable from the settings page / Clay)
#define DEFAULT_TEXT_COLOR   GColorFromRGB(255, 85, 0)  // orange #ff531a
#define DEFAULT_BOX_COLOR    GColorBlack
#define DEFAULT_CLOCK_COLOR  GColorWhite

#define CARD_X          8   // card horizontal margin from screen edge
#define CARD_R          8   // card corner radius

// Persistent storage keys (must stay stable across versions)
#define PERSIST_EVENT_0        1
#define PERSIST_EVENT_1        2
#define PERSIST_EVENT_2        3
#define PERSIST_BOX_COLOR         8
#define PERSIST_TEXT_COLOR        9
#define PERSIST_CLOCK_COLOR      10
// 11, 12 unused (formerly wallpaper override / mask)
#define PERSIST_PHONE_BATTERY    13   // last known phone battery %
#define PERSIST_SHOW_BATTERY     14
#define PERSIST_SHOW_STEPS       15
#define PERSIST_SHOW_WEATHER     16
#define PERSIST_SHOW_CALENDAR    17
#define PERSIST_WEATHER          18   // last known weather string
#define PERSIST_EVENT_3          19
#define PERSIST_EVENT_4          20
#define PERSIST_CALENDAR_ROWS    21
#define PERSIST_DATE_FORMAT      22
#define PERSIST_WEATHER_ICON     23
#define PERSIST_WEATHER_ICON_F   24
#define PERSIST_WEATHER_ICON_1   25
#define PERSIST_WEATHER_ICON_2   26
#define PERSIST_WEATHER_COLS     27
#define PERSIST_PANEL_ORDER      29
#define PERSIST_BG_COLOR         30
#define PERSIST_EVENT_0_START    31
#define PERSIST_LAYOUT_ORDER     32
#define PERSIST_SHOW_CLOCK       33
#define PERSIST_SHOW_SLEEP       34
#define PERSIST_STEP_GOAL        35
#define PERSIST_SHOW_WEEK        36
#define PERSIST_CLOCK_BG         37
#define PERSIST_INFO_BG          38
#define PERSIST_CAL_BG           39
#define PERSIST_CLOCK_PAD_X      40
#define PERSIST_CLOCK_PAD_Y      41
#define PERSIST_INFO_PAD_X       42
#define PERSIST_INFO_PAD_Y       43
#define PERSIST_CAL_PAD_X        44
#define PERSIST_CAL_PAD_Y        45
#define PERSIST_INFO_FONT_PX     46
#define PERSIST_CAL_FONT_PX      47
#define PERSIST_BATSTYLE_WATCH   48   // 0=percentage bar, 1=estimated time remaining
#define PERSIST_BATSTYLE_PHONE   49
#define PERSIST_WBAT_HIST_PCT    50   // battery-life rate tracking (see update_battery_rate)
#define PERSIST_WBAT_HIST_TIME   51
#define PERSIST_WBAT_RATE        52
#define PERSIST_PBAT_HIST_PCT    53
#define PERSIST_PBAT_HIST_TIME   54
#define PERSIST_PBAT_RATE        55

#define safe_copy(dst, src) do { strncpy((dst), (src), sizeof(dst) - 1); (dst)[sizeof(dst) - 1] = '\0'; } while(0)

// ============================================================================
// GLOBAL STATE
// ============================================================================

static Window   *s_main_window;
static Layer    *s_canvas_layer;
static GBitmap  *s_background;
static GBitmap  *s_phone_bmp;
static GBitmap  *s_watch_bmp;
static GBitmap  *s_charge_bmp;
static GBitmap  *s_heart_bmp;

static int  s_watch_battery   = 100;
static bool s_watch_charging  = false;
static int  s_phone_battery   = -1;
static bool s_phone_charging  = false;

// Battery-life estimation: display style (percentage bar vs. "Nd Nh") and the
// rate-tracking state used to derive it. See update_battery_rate().
static bool s_batstyle_watch  = false;
static bool s_batstyle_phone  = false;
static int    s_wbat_hist_pct  = -1;
static time_t s_wbat_hist_time = 0;
static int32_t s_wbat_rate     = 0;   // milli-percent/hour, signed (+ = charging, - = discharging)
static int    s_pbat_hist_pct  = -1;
static time_t s_pbat_hist_time = 0;
static int32_t s_pbat_rate     = 0;

// User-configurable colors (set in load_persist, updated by the settings page)
static GColor s_box_color;
static GColor s_text_color;
static GColor s_clock_color;

// Display panel toggles (default all visible)
static bool s_show_battery  = true;
static bool s_show_steps    = true;
static bool s_show_weather  = true;
static bool s_show_calendar = true;

static int  s_calendar_rows   = 3;    // configurable (1, 2, 3, 5)
static int  s_date_format     = 0;    // 0=Sat 21 Jun  1=21/06/26  2=06/21/26  3=ISO
static bool s_phone_connected = true; // greyed out when BT drops

// Weather data sent from companion JS, cached across reboots
static char s_weather_line[48];
// Parsed render state — recomputed only when s_weather_line changes
static bool s_wx_cols_mode = false;                       // line is "cur|fore|h|d1|d2..."
static char s_wx_ct[8], s_wx_ft[8], s_wx_d1[8], s_wx_d2[8];  // temp strings "12°"
static int  s_weather_icon   = -1;  // current condition icon (0-8), -1=unknown
static int  s_weather_icon_f = -1;  // N-hour forecast icon
static int  s_weather_icon_1 = -1;  // day+1 icon
static int  s_weather_icon_2 = -1;  // day+2 icon
static int  s_weather_cols       = 3;  // 0-3 extra forecast columns
static int  s_panel_order        = 0;  // 0-23: permutation of battery/steps/weather/sleep rows
static int  s_layout_order       = 0;  // 0-5: permutation of clock/info/calendar panels
static bool s_show_clock         = true;
static bool s_show_sleep         = false;
static int  s_step_goal          = 0;  // 0 = disabled
static bool s_show_week          = false;  // append ISO week " v.27" to date

// Panel style: card background per panel + content padding from card border.
// Defaults match the pre-3.20 hardcoded layout.
static bool s_clock_bg    = false;  // clock was always card-less before 3.20
static bool s_info_bg     = true;
static bool s_cal_bg      = true;
static int  s_clock_pad_x = 4,  s_clock_pad_y = 2;
static int  s_info_pad_x  = 8,  s_info_pad_y  = 6;
static int  s_cal_pad_x   = 8,  s_cal_pad_y   = 1;

// Font size (px) for info + calendar panel text: 14 / 18 / 24
static int  s_info_font_px = 14;
static int  s_cal_font_px  = 14;

// Background color used when no wallpaper image is loaded
static GColor s_bg_color;

static GBitmap *s_wx_bmp[9];
static const uint32_t WX_IDS[9] = {
    RESOURCE_ID_WX_SUNNY,  RESOURCE_ID_WX_PARTLY, RESOURCE_ID_WX_CLOUDY,
    RESOURCE_ID_WX_RAIN_L, RESOURCE_ID_WX_RAIN_H, RESOURCE_ID_WX_SNOW_L,
    RESOURCE_ID_WX_SNOW_H, RESOURCE_ID_WX_MIX,    RESOURCE_ID_WX_OTHER,
};

// Wallpaper — loaded from network PNG chunks sent by the companion JS.
// The receive buffer only exists during a transfer (~10s once a day), so it
// is heap-allocated on WP_TOTAL and freed after decode instead of costing
// 32KB of static RAM for the app's whole life.
#define WP_BUF_MAX 32768
static uint8_t *s_wp_buf   = NULL;
static uint32_t s_wp_total = 0;
static uint32_t s_wp_pos   = 0;
static AppTimer *s_wp_timeout_timer = NULL;

static void wp_buf_release(void) {
    if (s_wp_buf) { free(s_wp_buf); s_wp_buf = NULL; }
    s_wp_total = 0;
    s_wp_pos   = 0;
}

// Up to 5 calendar events, pre-formatted by the companion JS. Each is
// "left" or "left\tright" — left is drawn at the panel edge, the optional
// right part (a multi-day date range) is drawn right-aligned at the border.
static char s_event_text[5][64];

static const uint32_t PERSIST_EVENTS[5] = {
    PERSIST_EVENT_0, PERSIST_EVENT_1, PERSIST_EVENT_2, PERSIST_EVENT_3, PERSIST_EVENT_4
};

static HealthValue s_steps      = 0;
static HealthValue s_distance   = 0;
static HealthValue s_heart_rate = 0;
static HealthValue s_sleep      = 0;


// ============================================================================
// COLOR HELPERS
// ============================================================================

// Battery level -> color gradient: 0% red, 50% yellow, 100% green.
static GColor battery_color(int percent) {
    if (percent < 0) return GColorLightGray;
    if (percent > 100) percent = 100;
    int r, g;
    if (percent <= 50) {
        r = 255;
        g = (percent * 255) / 50;
    } else {
        r = ((100 - percent) * 255) / 50;
        g = 255;
    }
    return GColorFromRGB(r, g, 0);
}

// ============================================================================
// BATTERY LIFE ESTIMATION
//
// Neither the watch nor the phone exposes a real "time remaining" figure, so
// we derive one from the % readings we already receive: track (percent,
// timestamp) samples at least 10 minutes apart and keep a smoothed %/hour
// rate. Direction flips (charger plugged/unplugged) reset the rate to the
// fresh instant reading instead of blending, so the estimate doesn't linger
// on stale sign after a state change.
// ============================================================================

static void update_battery_rate(int new_percent, int *hist_pct, time_t *hist_time,
                                 int32_t *rate, uint32_t pct_key, uint32_t time_key, uint32_t rate_key) {
    time_t now = time(NULL);
    if (*hist_pct >= 0 && *hist_time > 0) {
        time_t dt = now - *hist_time;
        if (dt >= 600) {
            int dp = new_percent - *hist_pct;
            int32_t instant = (int32_t)(((int64_t)dp * 1000 * 3600) / dt);  // milli-%/hour
            bool same_dir = (*rate == 0) || (instant == 0) || ((*rate > 0) == (instant > 0));
            *rate = same_dir ? (int32_t)(((int64_t)*rate * 7 + (int64_t)instant * 3) / 10) : instant;
            *hist_pct  = new_percent;
            *hist_time = now;
            persist_write_int(rate_key, *rate);
            persist_write_int(pct_key,  *hist_pct);
            persist_write_int(time_key, (int32_t)*hist_time);
        }
    } else {
        *hist_pct  = new_percent;
        *hist_time = now;
        persist_write_int(pct_key,  *hist_pct);
        persist_write_int(time_key, (int32_t)*hist_time);
    }
}

// Formats remaining (or time-to-full) as "Nd Nh" / "Nh" / "Nm". Falls back to
// "..." until enough history has accumulated to have a rate at all.
static void format_battery_time(char *buf, size_t bufsz, int percent, bool charging, int32_t rate) {
    if (percent < 0) { snprintf(buf, bufsz, "---"); return; }
    if (rate == 0)   { snprintf(buf, bufsz, "...");  return; }
    int32_t abs_rate = rate < 0 ? -rate : rate;
    int pct_left = charging ? (100 - percent) : percent;
    if (pct_left <= 0) { snprintf(buf, bufsz, charging ? "Full" : "0h"); return; }
    int64_t total_min = ((int64_t)pct_left * 1000 * 60) / abs_rate;
    if (total_min < 60) {
        snprintf(buf, bufsz, "%dm", (int)total_min);
    } else if (total_min < 60 * 24) {
        snprintf(buf, bufsz, "%dh", (int)(total_min / 60));
    } else {
        snprintf(buf, bufsz, "%dd%dh", (int)(total_min / (60 * 24)), (int)((total_min / 60) % 24));
    }
}

// ============================================================================
// ICON DRAWING — small pixel phone and watch icons
// ============================================================================

// Draw a white-on-transparent icon bitmap at (x, y); returns its width.
static int draw_icon_bmp(GContext *ctx, GBitmap *bmp, int x, int y) {
    if (!bmp) return 0;
    GRect b = gbitmap_get_bounds(bmp);
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, bmp, GRect(x, y, b.size.w, b.size.h));
    return b.size.w;
}

// Chunky pixel-art battery gauge: black shell + terminal nub, dark-gray empty
// track, proportional colored fill. Replaces the old flat two-tone bar.
static void draw_battery_gauge(GContext *ctx, int x, int y, int w, int h,
                                int percent, GColor fill_color) {
    if (w < 8) return;
    int nub_w   = 2;
    int body_w  = w - nub_w - 1;
    if (body_w < 6) { nub_w = 0; body_w = w; }
    int r = (h >= 6) ? 2 : 1;

    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(x, y, body_w, h), r, GCornersAll);
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_rect(ctx, GRect(x + 1, y + 1, body_w - 2, h - 2), r > 0 ? r - 1 : 0, GCornersAll);

    if (nub_w > 0) {
        int nub_h = h > 4 ? h - 4 : h;
        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_rect(ctx, GRect(x + body_w, y + (h - nub_h) / 2, nub_w + 1, nub_h), 1, GCornersAll);
    }

    if (percent > 0) {
        int fw = ((body_w - 2) * percent) / 100;
        if (fw > 0) {
            graphics_context_set_fill_color(ctx, fill_color);
            graphics_fill_rect(ctx, GRect(x + 1, y + 1, fw, h - 2), r > 0 ? r - 1 : 0, GCornersAll);
        }
    }
}

// ============================================================================
// TEXT DRAWING HELPERS
// ============================================================================

static void draw_shadowed(GContext *ctx, const char *text, GFont font,
                           GRect box, GTextOverflowMode overflow,
                           GTextAlignment align, GColor color) {
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, text, font,
        GRect(box.origin.x + 1, box.origin.y + 1, box.size.w, box.size.h),
        overflow, align, NULL);
    graphics_context_set_text_color(ctx, color);
    graphics_draw_text(ctx, text, font, box, overflow, align, NULL);
}

static void draw_outlined(GContext *ctx, const char *text, GFont font,
                           GRect box, GTextOverflowMode overflow,
                           GTextAlignment align, GColor color) {
    // 4 diagonal offsets look near-identical to a full 8-neighbour outline
    // at half the render cost — this is the hottest draw on the face (LECO 38)
    graphics_context_set_text_color(ctx, GColorBlack);
    for (int dx = -1; dx <= 1; dx += 2) {
        for (int dy = -1; dy <= 1; dy += 2) {
            graphics_draw_text(ctx, text, font,
                GRect(box.origin.x + dx, box.origin.y + dy,
                      box.size.w, box.size.h),
                overflow, align, NULL);
        }
    }
    graphics_context_set_text_color(ctx, color);
    graphics_draw_text(ctx, text, font, box, overflow, align, NULL);
}

// Floating card with drop shadow and 3D bevel (dark gray, reused for all panels)
static void draw_card(GContext *ctx, int x, int y, int w, int h, int r) {
    // Drop shadow
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(x + 2, y + 2, w, h), r, GCornersAll);
    // Solid card (user-configurable color)
    graphics_context_set_fill_color(ctx, s_box_color);
    graphics_fill_rect(ctx, GRect(x, y, w, h), r, GCornersAll);

    // Top-left highlight — DarkGray edge for raised look
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_line(ctx, GPoint(x + r, y), GPoint(x + w - r, y));
    graphics_draw_line(ctx, GPoint(x, y + r), GPoint(x, y + h - r));
}

static void unobstructed_did_change(void *ctx) {
    if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}

// Parse s_weather_line into the cached column temp strings. Called when the
// line changes (inbox / persist load) so the per-minute redraw doesn't re-parse.
static void parse_weather_line(void) {
    s_wx_cols_mode = (strchr(s_weather_line, '|') != NULL);
    if (!s_wx_cols_mode) return;
    char wx_tmp[48];
    safe_copy(wx_tmp, s_weather_line);
    int wx_vals[5] = {0, 0, 4, 0, 0};
    char *wp = wx_tmp;
    for (int wi = 0; wi < 5 && *wp; wi++) {
        wx_vals[wi] = atoi(wp);
        wp = strchr(wp, '|');
        if (!wp) break;
        wp++;
    }
    snprintf(s_wx_ct, sizeof(s_wx_ct), "%d\xc2\xb0", wx_vals[0]);
    snprintf(s_wx_ft, sizeof(s_wx_ft), "%d\xc2\xb0", wx_vals[1]);
    snprintf(s_wx_d1, sizeof(s_wx_d1), "%d\xc2\xb0", wx_vals[3]);
    snprintf(s_wx_d2, sizeof(s_wx_d2), "%d\xc2\xb0", wx_vals[4]);
}

// ============================================================================
// FONT SIZE HELPERS — px setting -> system Gothic font + row metrics
// ============================================================================

static GFont px_font(int px) {
    if (px == 24) return fonts_get_system_font(FONT_KEY_GOTHIC_24);
    if (px == 18) return fonts_get_system_font(FONT_KEY_GOTHIC_18);
    return fonts_get_system_font(FONT_KEY_GOTHIC_14);
}

// Row advance (baseline stride) per font px; 17 == pre-3.20 ROW_STRIDE
static int px_stride(int px) {
    if (px == 24) return 28;
    if (px == 18) return 22;
    return 17;
}

// ============================================================================
// CANVAS UPDATE
// ============================================================================

static void canvas_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    const int W = bounds.size.w;

    // --- background ---
    if (s_background) {
        graphics_draw_bitmap_in_rect(ctx, s_background, bounds);
    } else {
        graphics_context_set_fill_color(ctx, s_bg_color);
        graphics_fill_rect(ctx, bounds, 0, GCornerNone);
    }

    // --- time & date ---
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (!t) return;

    static char date_buf[28];
    static const char *s_date_fmts[] = { "%a %d %b", "%d/%m/%y", "%m/%d/%y", "%Y-%m-%d" };
    strftime(date_buf, sizeof(date_buf), s_date_fmts[s_date_format < 4 ? s_date_format : 0], t);
    for (char *p = date_buf; *p; p++) {
        if (*p >= 'a' && *p <= 'z') *p -= 32;
    }
    // ISO week appended AFTER the uppercase pass so the "v" stays lowercase
    if (s_show_week) {
        size_t dlen = strlen(date_buf);
        strftime(date_buf + dlen, sizeof(date_buf) - dlen, " v.%V", t);
    }

    static char time_buf[8];
    strftime(time_buf, sizeof(time_buf),
             clock_is_24h_style() ? "%H:%M" : "%I:%M", t);

    // --- major panel layout: compute heights and Y positions ---
    bool wx     = s_show_weather && s_weather_line[0];
    bool vis[4] = { s_show_battery, s_show_steps, wx, s_show_sleep };
    int  n_vis  = (vis[0]?1:0) + (vis[1]?1:0) + (vis[2]?1:0) + (vis[3]?1:0);

    int info_stride = px_stride(s_info_font_px);
    int cal_row_h   = px_stride(s_cal_font_px) - 1;   // 16 at default 14px

    int h_clock = s_show_clock ? (s_clock_pad_y * 2 + CLOCK_CONTENT) : 0;
    int h_info  = (n_vis > 0)  ? (s_info_pad_y * 2 + n_vis * info_stride - 1) : 0;
    int h_cal   = s_show_calendar ? (s_cal_pad_y * 2 + s_calendar_rows * cal_row_h) : 0;

    GRect unobs = layer_get_unobstructed_bounds(layer);
    int screen_bottom = unobs.origin.y + unobs.size.h;

    // Clamp DRAWN calendar rows so the whole stack fits the unobstructed
    // screen (large fonts / padding can overflow) — never draw a partial row.
    int cal_rows_fit = s_calendar_rows;
    if (s_show_calendar && h_cal > 0) {
        int avail = (screen_bottom - CARD_X) - CARD_X
                    - h_clock - (h_clock > 0 ? PANEL_GAP : 0)
                    - h_info  - (h_info  > 0 ? PANEL_GAP : 0)
                    - s_cal_pad_y * 2;
        int fit = (cal_row_h > 0) ? avail / cal_row_h : 1;
        if (fit < 1) fit = 1;
        if (fit < cal_rows_fit) cal_rows_fit = fit;
        h_cal = s_cal_pad_y * 2 + cal_rows_fit * cal_row_h;
    }

    static const int8_t LAYOUT_PERMS[6][3] = {
        {0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}
    };
    int y_clock = -1, y_info = -1, y_cal = -1;
    {
        int lo = (s_layout_order >= 0 && s_layout_order < 6) ? s_layout_order : 0;

        // Collect visible panels in display order
        int vp[3]; int nvp = 0;
        for (int i = 0; i < 3; i++) {
            int p = LAYOUT_PERMS[lo][i];
            int h = (p == 0) ? h_clock : (p == 1) ? h_info : h_cal;
            if (h > 0) vp[nvp++] = p;
        }

        // A card-less clock panel starts closer to screen top than card panels
        int y = (nvp > 0 && vp[0] == 0 && !s_clock_bg) ? PANEL_GAP : CARD_X;
        for (int i = 0; i < nvp; i++) {
            int p = vp[i];
            int h = (p == 0) ? h_clock : (p == 1) ? h_info : h_cal;
            int py = (i == nvp - 1 && nvp > 1) ? (screen_bottom - h - CARD_X) : y;
            if      (p == 0) y_clock = py;
            else if (p == 1) y_info  = py;
            else             y_cal   = py;
            if (i < nvp - 1) y += h + PANEL_GAP;
        }
    }

    // --- clock panel ---
    if (s_show_clock && y_clock >= 0) {
        int cpx = CARD_X + s_clock_pad_x;       // default 8+4 == old MARGIN_X
        int ctw = W - cpx * 2;                  // text box width (full when card off)
        if (s_clock_bg) {
            // Card hugs the text: width = widest of date/time + pad_x each side.
            // Measured widths are cached — the date changes daily, the time
            // width only when digit glyph widths differ.
            static char msr_date[28] = "", msr_time[8] = "";
            static int16_t msr_dw = 0, msr_tw = 0;
            if (strcmp(msr_date, date_buf) != 0) {
                msr_dw = graphics_text_layout_get_content_size(date_buf,
                    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                    GRect(0, 0, W, 24), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter).w;
                safe_copy(msr_date, date_buf);
            }
            if (strcmp(msr_time, time_buf) != 0) {
                msr_tw = graphics_text_layout_get_content_size(time_buf,
                    fonts_get_system_font(FONT_KEY_LECO_38_BOLD_NUMBERS),
                    GRect(0, 0, W, 52), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter).w;
                safe_copy(msr_time, time_buf);
            }
            int content_w = (msr_dw > msr_tw ? msr_dw : msr_tw) + 2;  // +2: measure slack
            int card_w = content_w + s_clock_pad_x * 2;
            if (card_w > W - CARD_X * 2) card_w = W - CARD_X * 2;
            int card_x = (W - card_w) / 2;
            draw_card(ctx, card_x, y_clock, card_w, h_clock, CARD_R);
            cpx = card_x + s_clock_pad_x;
            ctw = card_w - s_clock_pad_x * 2;
        }
        int cy  = y_clock + s_clock_pad_y;
        draw_shadowed(ctx, date_buf,
            fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
            GRect(cpx, cy + Y_DATE, ctw, 20),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
            s_clock_color);
        if (!clock_is_24h_style()) {
            static char ampm_buf[4];
            strftime(ampm_buf, sizeof(ampm_buf), "%p", t);
            draw_shadowed(ctx, ampm_buf,
                fonts_get_system_font(FONT_KEY_GOTHIC_14),
                GRect(cpx, cy + Y_DATE + 3, ctw, 16),
                GTextOverflowModeTrailingEllipsis, GTextAlignmentRight,
                s_clock_color);
        }
        draw_outlined(ctx, time_buf,
            fonts_get_system_font(FONT_KEY_LECO_38_BOLD_NUMBERS),
            GRect(cpx, cy + Y_TIME, ctw, 52),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
            s_clock_color);
    }

    // --- info panel (battery / steps / weather in user-chosen order) ---
    {
        // All 24 permutations of rows 0=battery 1=steps 2=weather 3=sleep
        static const int8_t ROW_ORDER[24][4] = {
            {0,1,2,3},{0,1,3,2},{0,2,1,3},{0,2,3,1},{0,3,1,2},{0,3,2,1},
            {1,0,2,3},{1,0,3,2},{1,2,0,3},{1,2,3,0},{1,3,0,2},{1,3,2,0},
            {2,0,1,3},{2,0,3,1},{2,1,0,3},{2,1,3,0},{2,3,0,1},{2,3,1,0},
            {3,0,1,2},{3,0,2,1},{3,1,0,2},{3,1,2,0},{3,2,0,1},{3,2,1,0},
        };

        if (n_vis > 0 && y_info >= 0) {
        if (s_info_bg) draw_card(ctx, CARD_X, y_info, W - CARD_X * 2, h_info, CARD_R);

        GFont info_font = px_font(s_info_font_px);
        int   row_h = info_stride - 1;              // visible row height
        int   vsh   = (info_stride - ROW_STRIDE) / 2;  // extra v-centering for bars/icons
        int   lx    = CARD_X + s_info_pad_x;        // content left edge (default 16)
        int   ord = (s_panel_order >= 0 && s_panel_order < 24) ? s_panel_order : 0;
        int   ry  = y_info + s_info_pad_y;

        static char steps_str[16], dist_str[24], hr_str[8];
        {
            int km   = (int)(s_distance / 1000);
            int km10 = (int)((s_distance % 1000) / 100);
            snprintf(steps_str, sizeof(steps_str), "%lu stp", (unsigned long)s_steps);
            if (s_heart_rate > 0) {
                snprintf(dist_str, sizeof(dist_str), " | %d.%dkm | ", km, km10);
                snprintf(hr_str,   sizeof(hr_str),   "%d", (int)s_heart_rate);
            } else {
                snprintf(dist_str, sizeof(dist_str), " | %d.%dkm", km, km10);
                hr_str[0] = '\0';
            }
        }

        GColor steps_color = (s_step_goal > 0 && s_steps >= (HealthValue)s_step_goal)
                             ? GColorGreen : s_text_color;

        for (int ri = 0; ri < 4; ri++) {
            int row = ROW_ORDER[ord][ri];
            if (!vis[row]) continue;

            if (row == 0) {
            // ── Battery row ──────────────────────────────────────────────
            GColor p_color   = battery_color(s_phone_battery);
            GColor w_color   = battery_color(s_watch_battery);
            GColor phone_txt = s_phone_connected ? s_text_color : GColorLightGray;
            GColor phone_bar = s_phone_connected ? p_color      : GColorLightGray;

            // The percentage keeps its bar-mode position always; only the
            // bar itself is swapped for the time-remaining text (or "..."
            // while a rate is still being learned) in the same slot.
            bool p_no_bar = s_batstyle_phone;
            bool w_no_bar = s_batstyle_watch;

            static char pbat_str[12];
            if (s_phone_battery < 0) strncpy(pbat_str, "---", sizeof(pbat_str));
            else snprintf(pbat_str, sizeof(pbat_str), "%d%%", s_phone_battery);
            static char wbat_str[8];
            snprintf(wbat_str, sizeof(wbat_str), "%d%%", s_watch_battery);

            static char ptime_str[16];
            if (s_pbat_rate != 0) format_battery_time(ptime_str, sizeof(ptime_str), s_phone_battery, s_phone_charging, s_pbat_rate);
            else strncpy(ptime_str, "...", sizeof(ptime_str));
            static char wtime_str[16];
            if (s_wbat_rate != 0) format_battery_time(wtime_str, sizeof(wtime_str), s_watch_battery, s_watch_charging, s_wbat_rate);
            else strncpy(wtime_str, "...", sizeof(wtime_str));

            GFont bat_font = info_font;
            int cx     = W / 2;
            int txt_y  = ry - 4;    // larger fonts' internal top padding ≈ vsh
            int icon_y = ry - 1 + vsh;
            const int BAT_GAP = 4;
            const int OUT = lx;
            int pw = s_phone_bmp ? gbitmap_get_bounds(s_phone_bmp).size.w : 8;
            int ph = s_phone_bmp ? gbitmap_get_bounds(s_phone_bmp).size.h : 12;
            int ww = s_watch_bmp ? gbitmap_get_bounds(s_watch_bmp).size.w : 12;
            int wh = s_watch_bmp ? gbitmap_get_bounds(s_watch_bmp).size.h : 12;
            // Gauge is as tall as the icon row itself, not a thin sliver.
            int bar_h = (ph > wh) ? ph : wh;
            int bar_y = icon_y;

            GSize psz = graphics_text_layout_get_content_size(pbat_str, bat_font,
                GRect(0, 0, 80, 20), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
            GSize wsz = graphics_text_layout_get_content_size(wbat_str, bat_font,
                GRect(0, 0, 80, 20), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);

            {
                int icon_x = cx - BAT_GAP - pw;
                int bx     = OUT + psz.w + BAT_GAP;
                int bw     = (icon_x - BAT_GAP) - bx;
                draw_shadowed(ctx, pbat_str, bat_font,
                    GRect(OUT, txt_y, psz.w + 2, info_stride - 3),
                    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, phone_txt);
                if (s_phone_charging) draw_icon_bmp(ctx, s_charge_bmp, OUT + psz.w + 2, icon_y + 1);
                if (s_phone_battery >= 0 && bw > 0) {
                    if (p_no_bar) {
                        draw_shadowed(ctx, ptime_str, bat_font,
                            GRect(bx, txt_y, bw, info_stride - 3),
                            GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, phone_txt);
                    } else {
                        draw_battery_gauge(ctx, bx, bar_y, bw, bar_h, s_phone_battery, phone_bar);
                    }
                }
                draw_icon_bmp(ctx, s_phone_bmp, icon_x, icon_y);
            }
            {
                int icon_x = cx + BAT_GAP;
                int txt_l  = (W - OUT) - wsz.w;
                int bx     = icon_x + ww + BAT_GAP;
                int bw     = (txt_l - BAT_GAP) - bx;
                draw_icon_bmp(ctx, s_watch_bmp, icon_x, icon_y);
                if (bw > 0) {
                    if (w_no_bar) {
                        draw_shadowed(ctx, wtime_str, bat_font,
                            GRect(bx, txt_y, bw, info_stride - 3),
                            GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, s_text_color);
                    } else {
                        draw_battery_gauge(ctx, bx, bar_y, bw, bar_h, s_watch_battery, w_color);
                    }
                }
                draw_shadowed(ctx, wbat_str, bat_font,
                    GRect(txt_l, txt_y, wsz.w + 2, info_stride - 3),
                    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, s_text_color);
            }
            } else if (row == 1) {
            // ── Steps row ──
            {
                GRect mb = GRect(0, 0, W, info_stride);
                GSize w1 = graphics_text_layout_get_content_size(
                    steps_str, info_font, mb,
                    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
                GSize w2 = graphics_text_layout_get_content_size(
                    dist_str, info_font, mb,
                    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
                int hw = (s_heart_rate > 0 && s_heart_bmp) ? gbitmap_get_bounds(s_heart_bmp).size.w : 0;
                GSize w3 = {0, 0};
                if (s_heart_rate > 0)
                    w3 = graphics_text_layout_get_content_size(
                        hr_str, info_font, mb,
                        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
                int total_w = w1.w + w2.w + (s_heart_rate > 0 ? hw + 1 + w3.w : 0);
                int sx = (W - total_w) / 2;
                if (sx < lx) sx = lx;
                int iy = ry + 5 + vsh;
                draw_shadowed(ctx, steps_str, info_font,
                    GRect(sx, ry, w1.w + 2, row_h),
                    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, steps_color);
                sx += w1.w;
                draw_shadowed(ctx, dist_str, info_font,
                    GRect(sx, ry, w2.w + 2, row_h),
                    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, steps_color);
                sx += w2.w;
                if (s_heart_rate > 0) {
                    if (s_heart_bmp) draw_icon_bmp(ctx, s_heart_bmp, sx, iy);
                    sx += hw + 1;
                    draw_shadowed(ctx, hr_str, info_font,
                        GRect(sx, ry, w3.w + 4, row_h),
                        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, steps_color);
                }
            }
            } else if (row == 2) {
            // ── Weather row ── (temp strings pre-parsed in parse_weather_line)
            if (s_wx_cols_mode) {
                int total_cols = s_weather_cols + 1;
                if (total_cols > 4) total_cols = 4;
                if (total_cols < 1) total_cols = 1;

                const int ICON_W = 15;
                const int TEMP_W = (s_info_font_px == 24) ? 38
                                 : (s_info_font_px == 18) ? 29 : 22;
                const int col_w  = ICON_W + TEMP_W;
                const int col_gap = 5;

                // Drop trailing forecast columns that no longer fit (large fonts)
                while (total_cols > 1 &&
                       total_cols * col_w + (total_cols - 1) * col_gap > W - CARD_X * 2 - 4) {
                    total_cols--;
                }

                int group_w = col_w + (total_cols > 1 ? (total_cols - 1) * (col_w + col_gap) : 0);
                int avail = W - CARD_X * 2;
                int wcx_start = CARD_X + (avail - group_w) / 2;
                if (wcx_start < CARD_X + 2) wcx_start = CARD_X + 2;

                int wcx[4];
                wcx[0] = wcx_start;
                for (int c = 1; c < total_cols; c++) wcx[c] = wcx[c-1] + col_w + col_gap;

                GBitmap *ic0 = (s_weather_icon   >= 0 && s_weather_icon   < 9) ? s_wx_bmp[s_weather_icon]   : NULL;
                GBitmap *ic1 = (s_weather_icon_f >= 0 && s_weather_icon_f < 9) ? s_wx_bmp[s_weather_icon_f] : NULL;
                GBitmap *ic2 = (s_weather_icon_1 >= 0 && s_weather_icon_1 < 9) ? s_wx_bmp[s_weather_icon_1] : NULL;
                GBitmap *ic3 = (s_weather_icon_2 >= 0 && s_weather_icon_2 < 9) ? s_wx_bmp[s_weather_icon_2] : NULL;
                int iy = ry + 1 + vsh;

                if (ic0) draw_icon_bmp(ctx, ic0, wcx[0], iy);
                draw_shadowed(ctx, s_wx_ct, info_font,
                    GRect(wcx[0] + ICON_W, ry, TEMP_W, row_h),
                    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, s_text_color);
                if (total_cols >= 2) {
                    if (ic1) draw_icon_bmp(ctx, ic1, wcx[1], iy);
                    draw_shadowed(ctx, s_wx_ft, info_font,
                        GRect(wcx[1] + ICON_W, ry, TEMP_W, row_h),
                        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, s_text_color);
                }
                if (total_cols >= 3) {
                    if (ic2) draw_icon_bmp(ctx, ic2, wcx[2], iy);
                    draw_shadowed(ctx, s_wx_d1, info_font,
                        GRect(wcx[2] + ICON_W, ry, TEMP_W, row_h),
                        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, s_text_color);
                }
                if (total_cols >= 4) {
                    if (ic3) draw_icon_bmp(ctx, ic3, wcx[3], iy);
                    draw_shadowed(ctx, s_wx_d2, info_font,
                        GRect(wcx[3] + ICON_W, ry, TEMP_W, row_h),
                        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, s_text_color);
                }
            } else {
                draw_shadowed(ctx, s_weather_line, info_font,
                    GRect(lx, ry, W - lx * 2, row_h),
                    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, s_text_color);
            }
            } else {
            // ── Sleep row ──
            {
                int sleep_h = (int)(s_sleep / 3600);
                int sleep_m = (int)((s_sleep % 3600) / 60);
                static char sleep_str[20];
                snprintf(sleep_str, sizeof(sleep_str), "%dh %dm slp", sleep_h, sleep_m);
                draw_shadowed(ctx, sleep_str, info_font,
                    GRect(lx, ry, W - lx * 2, row_h),
                    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, s_text_color);
            }
            } // end row switch

            ry += info_stride;
        } // end for ri
        } // end if n_vis > 0 && y_info >= 0
    } // end info panel block

    // --- agenda card (next calendar events) ---
    if (s_show_calendar && y_cal >= 0) {
        if (s_cal_bg) draw_card(ctx, CARD_X, y_cal, W - CARD_X * 2, h_cal, CARD_R);

        GFont evt_font = px_font(s_cal_font_px);
        int clx = CARD_X + s_cal_pad_x;             // content left edge (default 16)
        int cw  = W - clx * 2;                      // content width  (default 168)
        int row = 0;
        for (int i = 0; i < cal_rows_fit && i < 5; i++) {
            if (s_event_text[i][0] == '\0') continue;
            int ry = y_cal + s_cal_pad_y + row * cal_row_h;
            row++;

            char left[64];
            safe_copy(left, s_event_text[i]);
            char *right = strchr(left, '\t');
            if (right) { *right = '\0'; right++; }

            // Shadowed like the info rows — stays readable when the card is off
            draw_shadowed(ctx, left, evt_font,
                GRect(clx, ry, (right ? cw * 62 / 100 : cw), cal_row_h),
                GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, s_text_color);
            if (right) {
                draw_shadowed(ctx, right, evt_font,
                    GRect(clx, ry, cw, cal_row_h),
                    GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, s_text_color);
            }
        }
    }
}

// ============================================================================
// SERVICE CALLBACKS
// ============================================================================

static void battery_callback(BatteryChargeState state) {
    s_watch_battery  = state.charge_percent;
    s_watch_charging = state.is_charging;
    update_battery_rate(s_watch_battery, &s_wbat_hist_pct, &s_wbat_hist_time, &s_wbat_rate,
                         PERSIST_WBAT_HIST_PCT, PERSIST_WBAT_HIST_TIME, PERSIST_WBAT_RATE);
    APP_LOG(APP_LOG_LEVEL_INFO, "watch battery: %d%% charging=%s",
            state.charge_percent, state.is_charging ? "true" : "false");
    if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}

static void send_status_request(void) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
        dict_write_uint8(iter, MESSAGE_KEY_REQUEST_STATUS, 1);
        dict_write_uint8(iter, MESSAGE_KEY_WP_HAVE, s_background ? 1 : 0);
        app_message_outbox_send();
        APP_LOG(APP_LOG_LEVEL_INFO, "status request -> phone (wp_have=%d)", s_background ? 1 : 0);
    } else {
        APP_LOG(APP_LOG_LEVEL_WARNING, "status request: outbox busy, skipped");
    }
}

static void load_health(void);  // defined below; polled once a minute from the tick

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    // Refresh health from the system once a minute rather than on every
    // movement event — the firmware fires HealthEventMovementUpdate several
    // times a second while walking, which would re-blit the whole face.
    load_health();
    layer_mark_dirty(s_canvas_layer);
    if (tick_time->tm_min == 0) send_status_request();  // poll phone hourly
}

// ============================================================================
// HEALTH
// ============================================================================

static void load_health(void) {
    // Only poll the metrics that are actually on screen — this runs every
    // minute, and each query walks the health service.
    if (s_show_steps) {
        s_steps    = health_service_sum_today(HealthMetricStepCount);
        s_distance = health_service_sum_today(HealthMetricWalkedDistanceMeters);
        HealthValue hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
        s_heart_rate = (hr > 0) ? hr : 0;
    }
    if (s_show_sleep) {
        s_sleep = health_service_sum_today(HealthMetricSleepSeconds);
    }
}

// ============================================================================
// APPMESSAGE
// ============================================================================

static void wp_timeout_cb(void *ctx) {
    s_wp_timeout_timer = NULL;
    if (s_wp_total > 0) {
        APP_LOG(APP_LOG_LEVEL_WARNING, "WP: transfer timeout — resetting");
        wp_buf_release();
    }
}

static void connection_handler(bool connected) {
    s_phone_connected = connected;
    if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
    Tuple *pbt = dict_find(iterator, MESSAGE_KEY_PHONE_BATTERY);
    if (pbt) {
        int val = (int)pbt->value->int32;
        persist_write_int(PERSIST_PHONE_BATTERY, val);
        if (val > 100) { s_phone_charging = true;  s_phone_battery = val - 100; }
        else           { s_phone_charging = false; s_phone_battery = val; }
        update_battery_rate(s_phone_battery, &s_pbat_hist_pct, &s_pbat_hist_time, &s_pbat_rate,
                             PERSIST_PBAT_HIST_PCT, PERSIST_PBAT_HIST_TIME, PERSIST_PBAT_RATE);
    }

    // Calendar events (pre-formatted strings from the companion JS)
    Tuple *ev[5] = {
        dict_find(iterator, MESSAGE_KEY_EVENT_0),
        dict_find(iterator, MESSAGE_KEY_EVENT_1),
        dict_find(iterator, MESSAGE_KEY_EVENT_2),
        dict_find(iterator, MESSAGE_KEY_EVENT_3),
        dict_find(iterator, MESSAGE_KEY_EVENT_4),
    };
    for (int i = 0; i < 5; i++) {
        if (ev[i]) {
            safe_copy(s_event_text[i], ev[i]->value->cstring);
            persist_write_string(PERSIST_EVENTS[i], s_event_text[i]);
        }
    }

    // Colors from the settings page (persisted as GColor.argb)
    Tuple *boxc = dict_find(iterator, MESSAGE_KEY_BOX_COLOR);
    if (boxc) { s_box_color = GColorFromHEX(boxc->value->int32);
                persist_write_int(PERSIST_BOX_COLOR, (int)s_box_color.argb);
                APP_LOG(APP_LOG_LEVEL_INFO, "rx BOX_COLOR=0x%06x argb=0x%02x",
                        (unsigned)boxc->value->int32, (unsigned)s_box_color.argb); }
    Tuple *txtc = dict_find(iterator, MESSAGE_KEY_TEXT_COLOR);
    if (txtc) { s_text_color = GColorFromHEX(txtc->value->int32);
                persist_write_int(PERSIST_TEXT_COLOR, (int)s_text_color.argb); }
    Tuple *clkc = dict_find(iterator, MESSAGE_KEY_CLOCK_COLOR);
    if (clkc) { s_clock_color = GColorFromHEX(clkc->value->int32);
                persist_write_int(PERSIST_CLOCK_COLOR, (int)s_clock_color.argb); }

    Tuple *wl = dict_find(iterator, MESSAGE_KEY_WEATHER_LINE);
    if (wl) {
        safe_copy(s_weather_line, wl->value->cstring);
        parse_weather_line();
        persist_write_string(PERSIST_WEATHER, s_weather_line);
    }

    Tuple *wi = dict_find(iterator, MESSAGE_KEY_WEATHER_ICON);
    if (wi) {
        int idx = (int)wi->value->int32;
        s_weather_icon = (idx >= 0 && idx < 9) ? idx : -1;
        persist_write_int(PERSIST_WEATHER_ICON, s_weather_icon);
    }
    Tuple *wif = dict_find(iterator, MESSAGE_KEY_WEATHER_ICON_F);
    if (wif) {
        int idx = (int)wif->value->int32;
        s_weather_icon_f = (idx >= 0 && idx < 9) ? idx : -1;
        persist_write_int(PERSIST_WEATHER_ICON_F, s_weather_icon_f);
    }
    Tuple *wi1 = dict_find(iterator, MESSAGE_KEY_WEATHER_ICON_1);
    if (wi1) {
        int idx = (int)wi1->value->int32;
        s_weather_icon_1 = (idx >= 0 && idx < 9) ? idx : -1;
        persist_write_int(PERSIST_WEATHER_ICON_1, s_weather_icon_1);
    }
    Tuple *wi2 = dict_find(iterator, MESSAGE_KEY_WEATHER_ICON_2);
    if (wi2) {
        int idx = (int)wi2->value->int32;
        s_weather_icon_2 = (idx >= 0 && idx < 9) ? idx : -1;
        persist_write_int(PERSIST_WEATHER_ICON_2, s_weather_icon_2);
    }

    Tuple *sb = dict_find(iterator, MESSAGE_KEY_SHOW_BATTERY);
    if (sb) { s_show_battery  = (sb->value->int32 != 0); persist_write_int(PERSIST_SHOW_BATTERY,  s_show_battery  ? 1 : 0); }
    Tuple *ss = dict_find(iterator, MESSAGE_KEY_SHOW_STEPS);
    if (ss) { s_show_steps    = (ss->value->int32 != 0); persist_write_int(PERSIST_SHOW_STEPS,    s_show_steps    ? 1 : 0); }
    Tuple *sw = dict_find(iterator, MESSAGE_KEY_SHOW_WEATHER);
    if (sw) { s_show_weather  = (sw->value->int32 != 0); persist_write_int(PERSIST_SHOW_WEATHER,  s_show_weather  ? 1 : 0); }
    Tuple *sc = dict_find(iterator, MESSAGE_KEY_SHOW_CALENDAR);
    if (sc) { s_show_calendar = (sc->value->int32 != 0); persist_write_int(PERSIST_SHOW_CALENDAR, s_show_calendar ? 1 : 0); }

    Tuple *cr = dict_find(iterator, MESSAGE_KEY_CALENDAR_ROWS);
    if (cr) {
        int rows = (int)cr->value->int32;
        s_calendar_rows = (rows >= 1 && rows <= 5) ? rows : 3;
        persist_write_int(PERSIST_CALENDAR_ROWS, s_calendar_rows);
    }

    Tuple *df = dict_find(iterator, MESSAGE_KEY_DATE_FORMAT);
    if (df) {
        int fmt = (int)df->value->int32;
        s_date_format = (fmt >= 0 && fmt <= 3) ? fmt : 0;
        persist_write_int(PERSIST_DATE_FORMAT, s_date_format);
    }

    Tuple *wxc = dict_find(iterator, MESSAGE_KEY_WEATHER_COLS);
    if (wxc) {
        int cols = (int)wxc->value->int32;
        s_weather_cols = (cols >= 0 && cols <= 3) ? cols : 3;
        persist_write_int(PERSIST_WEATHER_COLS, s_weather_cols);
    }


    Tuple *po = dict_find(iterator, MESSAGE_KEY_PANEL_ORDER);
    if (po) {
        int ord = (int)po->value->int32;
        s_panel_order = (ord >= 0 && ord <= 5) ? ord : 0;
        persist_write_int(PERSIST_PANEL_ORDER, s_panel_order);
    }

    Tuple *lo = dict_find(iterator, MESSAGE_KEY_LAYOUT_ORDER);
    if (lo) {
        int ord = (int)lo->value->int32;
        s_layout_order = (ord >= 0 && ord <= 5) ? ord : 0;
        persist_write_int(PERSIST_LAYOUT_ORDER, s_layout_order);
    }

    Tuple *shc = dict_find(iterator, MESSAGE_KEY_SHOW_CLOCK);
    if (shc) {
        s_show_clock = (shc->value->int32 != 0);
        persist_write_int(PERSIST_SHOW_CLOCK, s_show_clock ? 1 : 0);
    }

    Tuple *shs = dict_find(iterator, MESSAGE_KEY_SHOW_SLEEP);
    if (shs) {
        s_show_sleep = (shs->value->int32 != 0);
        persist_write_int(PERSIST_SHOW_SLEEP, s_show_sleep ? 1 : 0);
    }

    Tuple *sg = dict_find(iterator, MESSAGE_KEY_STEP_GOAL);
    if (sg) {
        s_step_goal = (int)sg->value->int32;
        if (s_step_goal < 0) s_step_goal = 0;
        persist_write_int(PERSIST_STEP_GOAL, s_step_goal);
    }

    Tuple *swk = dict_find(iterator, MESSAGE_KEY_SHOW_WEEK);
    if (swk) {
        s_show_week = (swk->value->int32 != 0);
        persist_write_int(PERSIST_SHOW_WEEK, s_show_week ? 1 : 0);
    }

    Tuple *bsw = dict_find(iterator, MESSAGE_KEY_BATSTYLE_WATCH);
    if (bsw) { s_batstyle_watch = (bsw->value->int32 != 0); persist_write_int(PERSIST_BATSTYLE_WATCH, s_batstyle_watch ? 1 : 0); }
    Tuple *bsp = dict_find(iterator, MESSAGE_KEY_BATSTYLE_PHONE);
    if (bsp) { s_batstyle_phone = (bsp->value->int32 != 0); persist_write_int(PERSIST_BATSTYLE_PHONE, s_batstyle_phone ? 1 : 0); }

    // Panel card toggles
    Tuple *cbg = dict_find(iterator, MESSAGE_KEY_CLOCK_BG);
    if (cbg) { s_clock_bg = (cbg->value->int32 != 0); persist_write_int(PERSIST_CLOCK_BG, s_clock_bg ? 1 : 0); }
    Tuple *ibg = dict_find(iterator, MESSAGE_KEY_INFO_BG);
    if (ibg) { s_info_bg  = (ibg->value->int32 != 0); persist_write_int(PERSIST_INFO_BG,  s_info_bg  ? 1 : 0); }
    Tuple *abg = dict_find(iterator, MESSAGE_KEY_CAL_BG);
    if (abg) { s_cal_bg   = (abg->value->int32 != 0); persist_write_int(PERSIST_CAL_BG,   s_cal_bg   ? 1 : 0); }

    // Per-panel padding (px, 0-24)
    struct { uint32_t msg_key; uint32_t persist_key; int *dst; } pads[] = {
        { MESSAGE_KEY_CLOCK_PAD_X, PERSIST_CLOCK_PAD_X, &s_clock_pad_x },
        { MESSAGE_KEY_CLOCK_PAD_Y, PERSIST_CLOCK_PAD_Y, &s_clock_pad_y },
        { MESSAGE_KEY_INFO_PAD_X,  PERSIST_INFO_PAD_X,  &s_info_pad_x  },
        { MESSAGE_KEY_INFO_PAD_Y,  PERSIST_INFO_PAD_Y,  &s_info_pad_y  },
        { MESSAGE_KEY_CAL_PAD_X,   PERSIST_CAL_PAD_X,   &s_cal_pad_x   },
        { MESSAGE_KEY_CAL_PAD_Y,   PERSIST_CAL_PAD_Y,   &s_cal_pad_y   },
    };
    for (unsigned i = 0; i < sizeof(pads) / sizeof(pads[0]); i++) {
        Tuple *tp = dict_find(iterator, pads[i].msg_key);
        if (!tp) continue;
        int v = (int)tp->value->int32;
        if (v < 0) v = 0;
        if (v > 24) v = 24;
        *pads[i].dst = v;
        persist_write_int(pads[i].persist_key, v);
    }

    Tuple *ifp = dict_find(iterator, MESSAGE_KEY_INFO_FONT_PX);
    if (ifp) {
        int px = (int)ifp->value->int32;
        s_info_font_px = (px == 18 || px == 24) ? px : 14;
        persist_write_int(PERSIST_INFO_FONT_PX, s_info_font_px);
    }
    Tuple *cfp = dict_find(iterator, MESSAGE_KEY_CAL_FONT_PX);
    if (cfp) {
        int px = (int)cfp->value->int32;
        s_cal_font_px = (px == 18 || px == 24) ? px : 14;
        persist_write_int(PERSIST_CAL_FONT_PX, s_cal_font_px);
    }

    Tuple *bgc = dict_find(iterator, MESSAGE_KEY_BG_COLOR);
    if (bgc) {
        s_bg_color = GColorFromHEX(bgc->value->int32);
        persist_write_int(PERSIST_BG_COLOR, (int)s_bg_color.argb);
    }

    Tuple *e0s = dict_find(iterator, MESSAGE_KEY_EVENT_0_START);
    if (e0s) persist_write_int(PERSIST_EVENT_0_START, (int)e0s->value->int32);

    // Wallpaper PNG received in chunks from companion JS
    Tuple *wpt = dict_find(iterator, MESSAGE_KEY_WP_TOTAL);
    if (wpt) {
        uint32_t total = (uint32_t)wpt->value->int32;
        if (total > 0 && total <= WP_BUF_MAX) {
            wp_buf_release();  // abandon any half-finished transfer
            s_wp_buf = malloc(total);
            if (s_wp_buf) {
                s_wp_total = total;
                s_wp_pos   = 0;
                if (s_wp_timeout_timer) app_timer_cancel(s_wp_timeout_timer);
                s_wp_timeout_timer = app_timer_register(30000, wp_timeout_cb, NULL);
            } else {
                APP_LOG(APP_LOG_LEVEL_ERROR, "WP: out of memory for %lu byte buffer", (unsigned long)total);
            }
        }
    }

    Tuple *wpd = dict_find(iterator, MESSAGE_KEY_WP_DATA);
    if (wpd && wpd->type == TUPLE_BYTE_ARRAY && s_wp_buf && s_wp_total > 0) {
        uint16_t len = wpd->length;
        if (s_wp_pos + len <= s_wp_total) {
            memcpy(s_wp_buf + s_wp_pos, wpd->value->data, len);
            s_wp_pos += len;
        }
    }

    Tuple *wpf = dict_find(iterator, MESSAGE_KEY_WP_DONE);
    if (wpf && wpf->value->int32 && s_wp_buf && s_wp_pos > 0) {
        if (s_wp_timeout_timer) { app_timer_cancel(s_wp_timeout_timer); s_wp_timeout_timer = NULL; }
        // Free old bitmap BEFORE decoding so heap has room for both old + new
        if (s_background) { gbitmap_destroy(s_background); s_background = NULL; }
        GBitmap *bmp = gbitmap_create_from_png_data(s_wp_buf, s_wp_pos);
        if (bmp) {
            s_background = bmp;
            APP_LOG(APP_LOG_LEVEL_INFO, "WP: loaded %lu bytes OK", (unsigned long)s_wp_pos);
        } else {
            APP_LOG(APP_LOG_LEVEL_ERROR, "WP: decode failed (%lu bytes) — must be 200x228 indexed PNG", (unsigned long)s_wp_pos);
        }
        wp_buf_release();
    } else if (wpf && wpf->value->int32 && s_wp_pos == 0) {
        APP_LOG(APP_LOG_LEVEL_ERROR, "WP: WP_DONE received but no data (chunks lost?)");
    }

    // Settings may have just re-enabled a health row — refresh gated metrics
    // now instead of waiting up to a minute for the next tick.
    load_health();
    layer_mark_dirty(s_canvas_layer);
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped: %d", (int)reason);
}

static void outbox_failed_callback(DictionaryIterator *iterator,
                                    AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Outbox failed: %d", (int)reason);
}

// ============================================================================
// PERSISTENT DATA
// ============================================================================

static void load_persist(void) {
    if (persist_exists(PERSIST_PHONE_BATTERY)) {
        int val = persist_read_int(PERSIST_PHONE_BATTERY);
        if (val > 100) { s_phone_charging = true;  s_phone_battery = val - 100; }
        else           { s_phone_charging = false; s_phone_battery = val; }
    } else {
        s_phone_battery = -1;
    }

    s_show_battery  = !persist_exists(PERSIST_SHOW_BATTERY)  || persist_read_int(PERSIST_SHOW_BATTERY);
    s_show_steps    = !persist_exists(PERSIST_SHOW_STEPS)    || persist_read_int(PERSIST_SHOW_STEPS);
    s_show_weather  = !persist_exists(PERSIST_SHOW_WEATHER)  || persist_read_int(PERSIST_SHOW_WEATHER);
    s_show_calendar = !persist_exists(PERSIST_SHOW_CALENDAR) || persist_read_int(PERSIST_SHOW_CALENDAR);

    if (persist_exists(PERSIST_WEATHER)) {
        persist_read_string(PERSIST_WEATHER, s_weather_line, sizeof(s_weather_line));
        parse_weather_line();
    }
    s_weather_icon   = persist_exists(PERSIST_WEATHER_ICON)   ? persist_read_int(PERSIST_WEATHER_ICON)   : -1;
    s_weather_icon_f = persist_exists(PERSIST_WEATHER_ICON_F) ? persist_read_int(PERSIST_WEATHER_ICON_F) : -1;
    s_weather_icon_1 = persist_exists(PERSIST_WEATHER_ICON_1) ? persist_read_int(PERSIST_WEATHER_ICON_1) : -1;
    s_weather_icon_2 = persist_exists(PERSIST_WEATHER_ICON_2) ? persist_read_int(PERSIST_WEATHER_ICON_2) : -1;
    if (s_weather_icon   < 0 || s_weather_icon   >= 9) s_weather_icon   = -1;
    if (s_weather_icon_f < 0 || s_weather_icon_f >= 9) s_weather_icon_f = -1;
    if (s_weather_icon_1 < 0 || s_weather_icon_1 >= 9) s_weather_icon_1 = -1;
    if (s_weather_icon_2 < 0 || s_weather_icon_2 >= 9) s_weather_icon_2 = -1;

    s_box_color   = persist_exists(PERSIST_BOX_COLOR)
        ? (GColor){ .argb = (uint8_t)persist_read_int(PERSIST_BOX_COLOR) }   : (GColor)DEFAULT_BOX_COLOR;
    s_text_color  = persist_exists(PERSIST_TEXT_COLOR)
        ? (GColor){ .argb = (uint8_t)persist_read_int(PERSIST_TEXT_COLOR) }  : (GColor)DEFAULT_TEXT_COLOR;
    s_clock_color = persist_exists(PERSIST_CLOCK_COLOR)
        ? (GColor){ .argb = (uint8_t)persist_read_int(PERSIST_CLOCK_COLOR) } : (GColor)DEFAULT_CLOCK_COLOR;

    s_calendar_rows = persist_exists(PERSIST_CALENDAR_ROWS) ? persist_read_int(PERSIST_CALENDAR_ROWS) : 3;
    if (s_calendar_rows < 1 || s_calendar_rows > 5) s_calendar_rows = 3;

    s_date_format = persist_exists(PERSIST_DATE_FORMAT) ? persist_read_int(PERSIST_DATE_FORMAT) : 0;
    if (s_date_format < 0 || s_date_format > 3) s_date_format = 0;

    s_weather_cols = persist_exists(PERSIST_WEATHER_COLS) ? persist_read_int(PERSIST_WEATHER_COLS) : 3;
    if (s_weather_cols < 0 || s_weather_cols > 3) s_weather_cols = 3;


    s_panel_order = persist_exists(PERSIST_PANEL_ORDER) ? persist_read_int(PERSIST_PANEL_ORDER) : 0;
    if (s_panel_order < 0 || s_panel_order > 23) s_panel_order = 0;

    s_layout_order = persist_exists(PERSIST_LAYOUT_ORDER) ? persist_read_int(PERSIST_LAYOUT_ORDER) : 0;
    if (s_layout_order < 0 || s_layout_order > 5) s_layout_order = 0;

    s_show_clock  = persist_exists(PERSIST_SHOW_CLOCK)  ? (persist_read_int(PERSIST_SHOW_CLOCK)  != 0) : true;
    s_show_sleep  = persist_exists(PERSIST_SHOW_SLEEP)  ? (persist_read_int(PERSIST_SHOW_SLEEP)  != 0) : false;
    s_step_goal   = persist_exists(PERSIST_STEP_GOAL)   ? persist_read_int(PERSIST_STEP_GOAL)          : 0;
    if (s_step_goal < 0) s_step_goal = 0;

    s_show_week = persist_exists(PERSIST_SHOW_WEEK) ? (persist_read_int(PERSIST_SHOW_WEEK) != 0) : false;

    s_batstyle_watch = persist_exists(PERSIST_BATSTYLE_WATCH) ? (persist_read_int(PERSIST_BATSTYLE_WATCH) != 0) : false;
    s_batstyle_phone = persist_exists(PERSIST_BATSTYLE_PHONE) ? (persist_read_int(PERSIST_BATSTYLE_PHONE) != 0) : false;
    s_wbat_hist_pct  = persist_exists(PERSIST_WBAT_HIST_PCT)  ? persist_read_int(PERSIST_WBAT_HIST_PCT)  : -1;
    s_wbat_hist_time = persist_exists(PERSIST_WBAT_HIST_TIME) ? (time_t)persist_read_int(PERSIST_WBAT_HIST_TIME) : 0;
    s_wbat_rate      = persist_exists(PERSIST_WBAT_RATE)      ? persist_read_int(PERSIST_WBAT_RATE)      : 0;
    s_pbat_hist_pct  = persist_exists(PERSIST_PBAT_HIST_PCT)  ? persist_read_int(PERSIST_PBAT_HIST_PCT)  : -1;
    s_pbat_hist_time = persist_exists(PERSIST_PBAT_HIST_TIME) ? (time_t)persist_read_int(PERSIST_PBAT_HIST_TIME) : 0;
    s_pbat_rate      = persist_exists(PERSIST_PBAT_RATE)      ? persist_read_int(PERSIST_PBAT_RATE)      : 0;
    s_clock_bg  = persist_exists(PERSIST_CLOCK_BG)  ? (persist_read_int(PERSIST_CLOCK_BG)  != 0) : false;
    s_info_bg   = persist_exists(PERSIST_INFO_BG)   ? (persist_read_int(PERSIST_INFO_BG)   != 0) : true;
    s_cal_bg    = persist_exists(PERSIST_CAL_BG)    ? (persist_read_int(PERSIST_CAL_BG)    != 0) : true;

    struct { uint32_t key; int *dst; int def; } pads[] = {
        { PERSIST_CLOCK_PAD_X, &s_clock_pad_x, 4 }, { PERSIST_CLOCK_PAD_Y, &s_clock_pad_y, 2 },
        { PERSIST_INFO_PAD_X,  &s_info_pad_x,  8 }, { PERSIST_INFO_PAD_Y,  &s_info_pad_y,  6 },
        { PERSIST_CAL_PAD_X,   &s_cal_pad_x,   8 }, { PERSIST_CAL_PAD_Y,   &s_cal_pad_y,   1 },
    };
    for (unsigned i = 0; i < sizeof(pads) / sizeof(pads[0]); i++) {
        int v = persist_exists(pads[i].key) ? persist_read_int(pads[i].key) : pads[i].def;
        if (v < 0) v = 0;
        if (v > 24) v = 24;
        *pads[i].dst = v;
    }

    s_info_font_px = persist_exists(PERSIST_INFO_FONT_PX) ? persist_read_int(PERSIST_INFO_FONT_PX) : 14;
    if (s_info_font_px != 18 && s_info_font_px != 24) s_info_font_px = 14;
    s_cal_font_px = persist_exists(PERSIST_CAL_FONT_PX) ? persist_read_int(PERSIST_CAL_FONT_PX) : 14;
    if (s_cal_font_px != 18 && s_cal_font_px != 24) s_cal_font_px = 14;

    s_bg_color = persist_exists(PERSIST_BG_COLOR)
        ? (GColor){ .argb = (uint8_t)persist_read_int(PERSIST_BG_COLOR) } : GColorBlack;

    for (int i = 0; i < 5; i++) {
        if (persist_exists(PERSIST_EVENTS[i])) {
            persist_read_string(PERSIST_EVENTS[i], s_event_text[i],
                                sizeof(s_event_text[i]));
        } else {
            s_event_text[i][0] = '\0';
        }
    }
}

// ============================================================================
// WINDOW
// ============================================================================

static void glance_reload_cb(AppGlanceReloadSession *session, size_t limit, void *ctx) {
    if (limit < 1) return;
    static char glance_str[40];
    const char *src = s_event_text[0][0] ? s_event_text[0] :
                      (s_weather_line[0]  ? s_weather_line  : NULL);
    if (!src) return;
    safe_copy(glance_str, src);
    char *tab = strchr(glance_str, '\t');
    if (tab) *tab = '\0';
    time_t expiry = APP_GLANCE_SLICE_NO_EXPIRATION;
    if (s_event_text[0][0] && persist_exists(PERSIST_EVENT_0_START)) {
        time_t t = (time_t)persist_read_int(PERSIST_EVENT_0_START);
        if (t > 0) expiry = t;
    }
    AppGlanceSlice slice = {
        .expiration_time = expiry,
        .layout.icon = APP_GLANCE_SLICE_DEFAULT_ICON,
        .layout.subtitle_template_string = glance_str,
    };
    app_glance_add_slice(session, slice);
}

static void main_window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(root);
    // s_background starts NULL; wallpaper arrives via WP_* AppMessage chunks
    s_phone_bmp  = gbitmap_create_with_resource(RESOURCE_ID_PHONE_ICON);
    s_watch_bmp  = gbitmap_create_with_resource(RESOURCE_ID_WATCH_ICON);
    s_charge_bmp = gbitmap_create_with_resource(RESOURCE_ID_CHARGE_ICON);
    s_heart_bmp  = gbitmap_create_with_resource(RESOURCE_ID_HEART_ICON);
    for (int i = 0; i < 9; i++) s_wx_bmp[i] = gbitmap_create_with_resource(WX_IDS[i]);
    s_canvas_layer = layer_create(bounds);
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    layer_add_child(root, s_canvas_layer);
}

static void main_window_unload(Window *window) {
    app_glance_reload(glance_reload_cb, NULL);
    layer_destroy(s_canvas_layer);
    s_canvas_layer = NULL;
    if (s_background) { gbitmap_destroy(s_background); s_background = NULL; }
    if (s_phone_bmp)  { gbitmap_destroy(s_phone_bmp);  s_phone_bmp  = NULL; }
    if (s_watch_bmp)  { gbitmap_destroy(s_watch_bmp);  s_watch_bmp  = NULL; }
    if (s_charge_bmp) { gbitmap_destroy(s_charge_bmp); s_charge_bmp = NULL; }
    if (s_heart_bmp)  { gbitmap_destroy(s_heart_bmp);  s_heart_bmp  = NULL; }
    for (int i = 0; i < 9; i++) { if (s_wx_bmp[i]) { gbitmap_destroy(s_wx_bmp[i]); s_wx_bmp[i] = NULL; } }
}

// ============================================================================
// LIFECYCLE
// ============================================================================

static void status_timer_cb(void *ctx) { send_status_request(); }

static void init(void) {
    load_persist();

    s_main_window = window_create();
    window_set_background_color(s_main_window, GColorBlack);
    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load   = main_window_load,
        .unload = main_window_unload
    });
    window_stack_push(s_main_window, true);

    app_message_register_inbox_received(inbox_received_callback);
    app_message_register_inbox_dropped(inbox_dropped_callback);
    app_message_register_outbox_failed(outbox_failed_callback);
    // Largest inbound message is a 1800-byte wallpaper chunk (+ dict
    // overhead); the ~8KB maximum inbox would waste ~6KB of heap.
    app_message_open(2048, 256);
    // Request status (including WP_HAVE) shortly after startup so JS re-sends
    // wallpaper immediately if the watchface reloaded after a launcher visit.
    app_timer_register(500, status_timer_cb, NULL);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    battery_state_service_subscribe(battery_callback);
    battery_callback(battery_state_service_peek());

    connection_service_subscribe((ConnectionHandlers) {
        .pebble_app_connection_handler = connection_handler
    });
    s_phone_connected = connection_service_peek_pebble_app_connection();
    unobstructed_area_service_subscribe(
        (UnobstructedAreaHandlers){ .did_change = unobstructed_did_change }, NULL);

    // Poll health on the minute tick instead of subscribing to movement
    // events (which fire many times a second while walking). sum_today only
    // needs the activity service initialised, not an event subscription.
    load_health();
}

static void deinit(void) {
    if (s_wp_timeout_timer) { app_timer_cancel(s_wp_timeout_timer); s_wp_timeout_timer = NULL; }
    wp_buf_release();
    unobstructed_area_service_unsubscribe();
    tick_timer_service_unsubscribe();
    battery_state_service_unsubscribe();
    window_destroy(s_main_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
    return 0;
}
