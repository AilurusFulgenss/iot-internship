#include "ui_pm.h"
#include "eth_upload.h"
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
static const char *TAB_LABEL[5]   = {"PM2.5",  "PM10",   "TEMP",  "HUM",      "SOUND"};

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

// s_live index: 0=PM2.5, 1=PM10, 2=TEMP, 3=HUM, 4=SOUND
static int   s_active = 0;
static float s_live[5] = {};

static lv_obj_t *s_tab_btn[5] = {};
static lv_obj_t *s_lbl_name   = NULL;
static lv_obj_t *s_lbl_val    = NULL;
static lv_obj_t *s_lbl_unit   = NULL;
static lv_obj_t *s_lbl_status = NULL;

// ── Helpers ──────────────────────────────────────────────────────────────────

static void refresh_current(void)
{
    if (!s_lbl_val) return;
    int   si = s_active;
    float v  = s_live[si];

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

    // ── Current value block (centered in remaining space y=120..720) ──────────
    s_lbl_name = lv_label_create(scr_pm);
    lv_label_set_text(s_lbl_name, "PM 2.5");
    lv_obj_set_style_text_color(s_lbl_name, lv_color_hex(0x7788AAu), 0);
    lv_obj_set_style_text_font(s_lbl_name, &lv_font_montserrat_32, 0);
    lv_obj_align(s_lbl_name, LV_ALIGN_CENTER, 0, -80);

    s_lbl_val = lv_label_create(scr_pm);
    lv_label_set_text(s_lbl_val, "--");
    lv_obj_set_style_text_color(s_lbl_val, lv_color_hex(0x334455u), 0);
    lv_obj_set_style_text_font(s_lbl_val, &lv_font_montserrat_48, 0);
    lv_obj_align(s_lbl_val, LV_ALIGN_CENTER, 0, -20);

    s_lbl_unit = lv_label_create(scr_pm);
    lv_label_set_text(s_lbl_unit, "ug/m3");
    lv_obj_set_style_text_color(s_lbl_unit, lv_color_hex(0x334455u), 0);
    lv_obj_set_style_text_font(s_lbl_unit, &lv_font_montserrat_24, 0);
    lv_obj_align(s_lbl_unit, LV_ALIGN_CENTER, 0, 50);

    s_lbl_status = lv_label_create(scr_pm);
    lv_label_set_text(s_lbl_status, "WAITING");
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0x334455u), 0);
    lv_obj_set_style_text_font(s_lbl_status, &lv_font_montserrat_24, 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_CENTER, 0, 90);
}

// ── Public API ────────────────────────────────────────────────────────────────

void ui_pm_update(float temp, float hum, float pm25, float pm10, float sound)
{
    if (!scr_pm) return;
    s_live[0] = pm25;
    s_live[1] = pm10;
    s_live[2] = temp;
    s_live[3] = hum;
    s_live[4] = sound;
    refresh_current();
}
