#include "ui_pm.h"
#include "eth_upload.h"
#include "history.h"
#include "lvgl.h"
#include <stdio.h>
#include <math.h>

lv_obj_t *scr_pm = NULL;

// ── Color standards ──────────────────────────────────────────────────────────

static uint32_t pm25_color(float v) {
    if (v <= 12.0f)  return 0x009966u;
    if (v <= 35.4f)  return 0xFFDE33u;
    if (v <= 55.4f)  return 0xFF9933u;
    if (v <= 150.4f) return 0xCC0033u;
    if (v <= 250.4f) return 0x660099u;
    return 0x7E0023u;
}
static uint32_t pm10_color(float v) {
    if (v <= 54)   return 0x009966u;
    if (v <= 154)  return 0xFFDE33u;
    if (v <= 254)  return 0xFF9933u;
    if (v <= 354)  return 0xCC0033u;
    return 0x660099u;
}
static uint32_t temp_color(float v) {
    if (v < 18)  return 0x1565C0u;
    if (v < 22)  return 0x0097A7u;
    if (v < 27)  return 0x00C853u;
    if (v < 30)  return 0xFFD600u;
    if (v < 35)  return 0xFF6D00u;
    return 0xD50000u;
}
static uint32_t hum_color(float v) {
    if (v < 30)  return 0xFFD600u;
    if (v < 60)  return 0x00C853u;
    if (v < 75)  return 0x0097A7u;
    return 0x1565C0u;
}
static uint32_t sound_color(float v) {
    if (v < 40)  return 0x00C853u;
    if (v < 60)  return 0xFFD600u;
    if (v < 75)  return 0xFF6D00u;
    return 0xD50000u;
}

typedef uint32_t (*color_fn_t)(float);
static const color_fn_t COLOR_FN[5] = {
    pm25_color, pm10_color, temp_color, hum_color, sound_color
};

static const char *SENS_NAME[5]   = {"PM 2.5", "PM 10",  "TEMP",  "HUMIDITY", "SOUND"};
static const char *SENS_UNIT[5]   = {"ug/m3",  "ug/m3",  "C",     "%",        "dB"};
static const char *TAB_LABEL[5]   = {"PM2.5",  "PM10",     "TEMP",    "HUM",    "SOUND"};
static const int   HIST_IDX[5]    = {HIST_PM25, HIST_PM10, HIST_TEMP, HIST_HUM, HIST_SOUND};

static const char *sens_status(int si, float v) {
    switch (si) {
        case 0: // PM2.5
            if (v <= 12.0f)  return "GOOD";
            if (v <= 35.4f)  return "MODERATE";
            if (v <= 55.4f)  return "SENSITIVE";
            if (v <= 150.4f) return "UNHEALTHY";
            return "VERY UNHEALTHY";
        case 1: // PM10
            if (v <= 54)   return "GOOD";
            if (v <= 154)  return "MODERATE";
            if (v <= 254)  return "SENSITIVE";
            return "UNHEALTHY";
        case 2: // Temp
            if (v < 18) return "COLD";
            if (v < 22) return "COOL";
            if (v < 27) return "COMFORTABLE";
            if (v < 30) return "WARM";
            if (v < 35) return "HOT";
            return "VERY HOT";
        case 3: // Hum
            if (v < 30) return "DRY";
            if (v < 60) return "COMFORTABLE";
            if (v < 75) return "HUMID";
            return "VERY HUMID";
        case 4: // Sound
            if (v < 40) return "QUIET";
            if (v < 60) return "MODERATE";
            if (v < 75) return "LOUD";
            return "HARMFUL";
    }
    return "";
}

// ── State ────────────────────────────────────────────────────────────────────

static int     s_active = 0;           // tab index: PM2.5=0,PM10=1,TEMP=2,HUM=3,SND=4
static float   s_live[5] = {};         // latest live values

static lv_obj_t *s_tab_btn[5]     = {};
static lv_obj_t *s_lbl_name       = NULL;
static lv_obj_t *s_lbl_val        = NULL;
static lv_obj_t *s_lbl_unit       = NULL;
static lv_obj_t *s_lbl_status     = NULL;
static lv_obj_t *s_card_dot[24]   = {};
static lv_obj_t *s_card_val[24]   = {};
static lv_obj_t *s_card_hr[24]    = {};
static lv_obj_t *s_strip_title    = NULL;

// ── Helpers ──────────────────────────────────────────────────────────────────

static void refresh_current(void)
{
    if (!s_lbl_val) return;
    int  si  = s_active;
    int  hi  = HIST_IDX[si];
    float v  = s_live[hi];

    char buf[16];
    if (isnan(v)) snprintf(buf, sizeof(buf), "--");
    else          snprintf(buf, sizeof(buf), "%.1f", v);

    lv_label_set_text(s_lbl_name,   SENS_NAME[si]);
    lv_label_set_text(s_lbl_val,    buf);
    lv_label_set_text(s_lbl_unit,   SENS_UNIT[si]);
    lv_label_set_text(s_lbl_status, isnan(v) ? "WAITING" : sens_status(si, v));

    uint32_t col = isnan(v) ? 0x334455u : COLOR_FN[si](v);
    lv_obj_set_style_text_color(s_lbl_val,    lv_color_hex(col), 0);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(col), 0);

    // Strip title
    char tbuf[32];
    snprintf(tbuf, sizeof(tbuf), "%s | 24H HISTORY", SENS_NAME[si]);
    if (s_strip_title) lv_label_set_text(s_strip_title, tbuf);
}

static void refresh_cards(void)
{
    int si = s_active;
    int hi = HIST_IDX[si];
    int n  = g_hist_24h.count;  // 0..24

    for (int i = 0; i < 24; i++) {
        // right-align: card 23 = newest (Now), card 0 = 23h ago
        // e.g. n=6 → cards 0-17 empty, cards 18-23 have data
        int data_idx   = i - (24 - n);         // <0 means no data
        bool has_data  = (n > 0 && data_idx >= 0);
        int  hours_ago = has_data ? (n - 1 - data_idx) : -1;
        float v        = has_data ? g_hist_24h.d[hi][data_idx] : NAN;

        // Hour label
        char hbuf[8];
        if (has_data && hours_ago == 0) snprintf(hbuf, sizeof(hbuf), "Now");
        else                            snprintf(hbuf, sizeof(hbuf), "-%dh", 23 - i);
        lv_label_set_text(s_card_hr[i], hbuf);
        lv_obj_set_style_text_color(s_card_hr[i],
            lv_color_hex((has_data && hours_ago == 0) ? 0x00E5FFu : 0x445566u), 0);

        // Value label
        char vbuf[8];
        if (!has_data || isnan(v)) snprintf(vbuf, sizeof(vbuf), "--");
        else                       snprintf(vbuf, sizeof(vbuf), "%.0f", v);
        lv_label_set_text(s_card_val[i], vbuf);

        // Dot color
        uint32_t col = (has_data && !isnan(v)) ? COLOR_FN[si](v) : 0x1E2A3Au;
        lv_obj_set_style_bg_color(s_card_dot[i], lv_color_hex(col), 0);
    }
}

// ── Tab button callback ───────────────────────────────────────────────────────

static void tab_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_active = idx;
    for (int i = 0; i < 5; i++) {
        bool active = (i == idx);
        lv_obj_set_style_bg_color(s_tab_btn[i],
            lv_color_hex(active ? 0x00E5FFu : 0x1A1A28u), 0);
        lv_obj_t *lbl = lv_obj_get_child(s_tab_btn[i], 0);
        lv_obj_set_style_text_color(lbl,
            lv_color_hex(active ? 0x0A0A12u : 0x556677u), 0);
    }
    refresh_current();
    refresh_cards();
}

// ── ui_pm_create ─────────────────────────────────────────────────────────────

void ui_pm_create(void)
{
    scr_pm = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_pm, lv_color_hex(0x0A0A12), 0);
    lv_obj_set_style_bg_opa(scr_pm, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr_pm, 0, 0);

    // ── Header ───────────────────────────────────────────────────────────────
    lv_obj_t *hdr = lv_obj_create(scr_pm);
    lv_obj_set_size(hdr, 720, 72);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x12121E), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 22, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *accent = lv_obj_create(hdr);
    lv_obj_set_size(accent, 720, 3);
    lv_obj_align(accent, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_color(accent, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_style_pad_all(accent, 0, 0);

    int title_x = 0;
    if (eth_upload_has_logo()) {
        lv_obj_t *logo_img = lv_image_create(hdr);
        lv_image_set_src(logo_img, ETH_LOGO_LVGL_PATH);
        lv_obj_set_size(logo_img, 48, 48);
        lv_obj_align(logo_img, LV_ALIGN_LEFT_MID, 0, 0);
        title_x = 58;
    }
    lv_obj_t *lbl_title = lv_label_create(hdr);
    lv_label_set_text(lbl_title, "AIR QUALITY");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_32, 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, title_x, 0);

    lv_obj_t *lbl_nav = lv_label_create(hdr);
    lv_label_set_text(lbl_nav, "< USER RELAY >");
    lv_obj_set_style_text_color(lbl_nav, lv_color_hex(0x3A4A5A), 0);
    lv_obj_set_style_text_font(lbl_nav, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_nav, LV_ALIGN_RIGHT_MID, 0, 0);

    // ── Tab bar (y=72, h=48) ──────────────────────────────────────────────────
    const int TW = 144;  // 720/5
    for (int i = 0; i < 5; i++) {
        lv_obj_t *btn = lv_obj_create(scr_pm);
        lv_obj_set_size(btn, TW, 48);
        lv_obj_set_pos(btn, i * TW, 72);
        lv_obj_set_style_bg_color(btn, lv_color_hex(i == 0 ? 0x00E5FFu : 0x1A1A28u), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, TAB_LABEL[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(i == 0 ? 0x0A0A12u : 0x556677u), 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, tab_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_tab_btn[i] = btn;
    }

    // ── Current value block (y=120, h=220) ───────────────────────────────────
    s_lbl_name = lv_label_create(scr_pm);
    lv_label_set_text(s_lbl_name, "PM 2.5");
    lv_obj_set_style_text_color(s_lbl_name, lv_color_hex(0x7788AAu), 0);
    lv_obj_set_style_text_font(s_lbl_name, &lv_font_montserrat_32, 0);
    lv_obj_align(s_lbl_name, LV_ALIGN_TOP_MID, 0, 196);

    s_lbl_val = lv_label_create(scr_pm);
    lv_label_set_text(s_lbl_val, "--");
    lv_obj_set_style_text_color(s_lbl_val, lv_color_hex(0x334455u), 0);
    lv_obj_set_style_text_font(s_lbl_val, &lv_font_montserrat_48, 0);
    lv_obj_align(s_lbl_val, LV_ALIGN_TOP_MID, 0, 242);

    s_lbl_unit = lv_label_create(scr_pm);
    lv_label_set_text(s_lbl_unit, "ug/m3");
    lv_obj_set_style_text_color(s_lbl_unit, lv_color_hex(0x334455u), 0);
    lv_obj_set_style_text_font(s_lbl_unit, &lv_font_montserrat_24, 0);
    lv_obj_align(s_lbl_unit, LV_ALIGN_TOP_MID, 0, 314);

    s_lbl_status = lv_label_create(scr_pm);
    lv_label_set_text(s_lbl_status, "WAITING");
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0x334455u), 0);
    lv_obj_set_style_text_font(s_lbl_status, &lv_font_montserrat_24, 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_TOP_MID, 0, 354);

    // ── 24h strip (688×220, bottom at 720-72=648) ────────────────────────────
    lv_obj_t *strip = lv_obj_create(scr_pm);
    lv_obj_set_size(strip, 688, 220);
    lv_obj_align(strip, LV_ALIGN_BOTTOM_MID, 0, -72);
    lv_obj_set_style_bg_color(strip, lv_color_hex(0x1A1A26u), 0);
    lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(strip, 18, 0);
    lv_obj_set_style_border_color(strip, lv_color_hex(0x2C2C3Cu), 0);
    lv_obj_set_style_border_width(strip, 1, 0);
    lv_obj_set_style_pad_all(strip, 0, 0);
    lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);

    s_strip_title = lv_label_create(strip);
    lv_label_set_text(s_strip_title, "PM 2.5 | 24H HISTORY");
    lv_obj_set_style_text_color(s_strip_title, lv_color_hex(0x7788AAu), 0);
    lv_obj_set_style_text_font(s_strip_title, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(s_strip_title, 16, 12);

    // ── Horizontal scroll container ───────────────────────────────────────────
    lv_obj_t *scroll = lv_obj_create(strip);
    lv_obj_set_size(scroll, 688, 178);
    lv_obj_set_pos(scroll, 0, 38);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, 0, 0);
    lv_obj_set_style_pad_left(scroll, 12, 0);
    lv_obj_set_style_pad_column(scroll, 4, 0);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(scroll, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(scroll, LV_SCROLL_SNAP_NONE);

    // ── 24 cards ─────────────────────────────────────────────────────────────
    const int CW = 76, CH = 168;
    for (int i = 0; i < 24; i++) {
        lv_obj_t *card = lv_obj_create(scroll);
        lv_obj_set_size(card, CW, CH);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x141420u), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        s_card_hr[i] = lv_label_create(card);
        lv_label_set_text(s_card_hr[i], "---");
        lv_obj_set_style_text_font(s_card_hr[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_card_hr[i], lv_color_hex(0x445566u), 0);
        lv_obj_align(s_card_hr[i], LV_ALIGN_TOP_MID, 0, 38);

        s_card_dot[i] = lv_obj_create(card);
        lv_obj_set_size(s_card_dot[i], 38, 38);
        lv_obj_align(s_card_dot[i], LV_ALIGN_TOP_MID, 0, 62);
        lv_obj_set_style_bg_color(s_card_dot[i], lv_color_hex(0x1E2A3Au), 0);
        lv_obj_set_style_bg_opa(s_card_dot[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_card_dot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_card_dot[i], 0, 0);
        lv_obj_set_style_pad_all(s_card_dot[i], 0, 0);
        lv_obj_clear_flag(s_card_dot[i], LV_OBJ_FLAG_SCROLLABLE);

        s_card_val[i] = lv_label_create(card);
        lv_label_set_text(s_card_val[i], "--");
        lv_obj_set_style_text_font(s_card_val[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_card_val[i], lv_color_hex(0x889AAAu), 0);
        lv_obj_align(s_card_val[i], LV_ALIGN_TOP_MID, 0, 108);
    }

    // Scroll to newest (right end)
    lv_obj_scroll_to_x(scroll, LV_COORD_MAX, LV_ANIM_OFF);
}

// ── Public API ────────────────────────────────────────────────────────────────

void ui_pm_update(float temp, float hum, float pm25, float pm10, float sound)
{
    if (!scr_pm) return;
    s_live[HIST_TEMP]  = temp;
    s_live[HIST_HUM]   = hum;
    s_live[HIST_PM25]  = pm25;
    s_live[HIST_PM10]  = pm10;
    s_live[HIST_SOUND] = sound;
    refresh_current();
}

void ui_pm_refresh_history(void)
{
    if (!scr_pm) return;
    refresh_cards();
}
