#include "ui_pm.h"
#include "eth_upload.h"
#include "lvgl.h"
#include <stdio.h>

lv_obj_t *scr_pm = NULL;

#define CLR_GOOD       0x009966u
#define CLR_MODERATE   0xFFDE33u
#define CLR_SENSITIVE  0xFF9933u
#define CLR_UNHEALTHY  0xCC0033u
#define CLR_VERY_UH    0x660099u
#define CLR_HAZARDOUS  0x7E0023u

static uint32_t pm_aqi_color(float v)
{
    if (v <= 12.0f)  return CLR_GOOD;
    if (v <= 35.4f)  return CLR_MODERATE;
    if (v <= 55.4f)  return CLR_SENSITIVE;
    if (v <= 150.4f) return CLR_UNHEALTHY;
    if (v <= 250.4f) return CLR_VERY_UH;
    return CLR_HAZARDOUS;
}

static const char *pm_aqi_label(float v)
{
    if (v <= 12.0f)  return "GOOD";
    if (v <= 35.4f)  return "MODERATE";
    if (v <= 55.4f)  return "SENSITIVE";
    if (v <= 150.4f) return "UNHEALTHY";
    if (v <= 250.4f) return "VERY UNHEALTHY";
    return "HAZARDOUS";
}

static lv_obj_t *s_lbl_cur   = NULL;
static lv_obj_t *s_lbl_aqi   = NULL;
static lv_obj_t *s_dot_today = NULL;
static lv_obj_t *s_val_today = NULL;

// ── ui_pm_create — 31 objects total ──────────────────────────────────────────
void ui_pm_create(void)
{
    static const float PM_HIST[7] = {42.f, 38.f, 34.f, 28.f, 22.f, 17.f, 0.f};
    static const char * const PM_DAY[7] = {"-6d","-5d","-4d","-3d","-2d","-1d","Today"};
    const int SW = 93;  // column slot width

    scr_pm = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_pm, lv_color_hex(0x0A0A12), 0);
    lv_obj_set_style_bg_opa(scr_pm, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr_pm, 0, 0);

    // ── Header ──────────────────────────────────────────────
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
    lv_obj_align(accent, LV_ALIGN_TOP_MID, 0, 48);   // match USER screen header
    lv_obj_set_style_bg_color(accent, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_style_pad_all(accent, 0, 0);

    // Logo (left side, if available) — user-uploaded PNG from SPIFFS
    int title_x = 0;
    if (eth_upload_has_logo()) {
        lv_obj_t *logo_img = lv_image_create(hdr);
        lv_image_set_src(logo_img, ETH_LOGO_LVGL_PATH);
        lv_obj_set_size(logo_img, 48, 48);
        lv_obj_align(logo_img, LV_ALIGN_LEFT_MID, 0, 0);
        title_x = 58;
    }

    lv_obj_t *lbl_title = lv_label_create(hdr);
    lv_label_set_text(lbl_title, "PM HISTORY");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_32, 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, title_x, 0);

    lv_obj_t *lbl_nav = lv_label_create(hdr);
    lv_label_set_text(lbl_nav, "< USER RELAY >");
    lv_obj_set_style_text_color(lbl_nav, lv_color_hex(0x3A4A5A), 0);
    lv_obj_set_style_text_font(lbl_nav, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_nav, LV_ALIGN_RIGHT_MID, 0, 0);

    // ── Current PM2.5 (live, color-coded) ───────────────────
    // Strip: 688×175, bottom edge at 696 → top at 521.
    // Available between header bottom (72) and strip top (521): 449px.
    // Block height: ~132px → top at 72 + (449-132)/2 = 230.
    s_lbl_cur = lv_label_create(scr_pm);
    lv_label_set_text(s_lbl_cur, "--");
    lv_obj_set_style_text_color(s_lbl_cur, lv_color_hex(0x334455), 0);
    lv_obj_set_style_text_font(s_lbl_cur, &lv_font_montserrat_48, 0);
    lv_obj_align(s_lbl_cur, LV_ALIGN_TOP_MID, 0, 228);

    lv_obj_t *lbl_unit = lv_label_create(scr_pm);
    lv_label_set_text(lbl_unit, "ug/m3");
    lv_obj_set_style_text_color(lbl_unit, lv_color_hex(0x334455), 0);
    lv_obj_set_style_text_font(lbl_unit, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_unit, LV_ALIGN_TOP_MID, 0, 298);

    s_lbl_aqi = lv_label_create(scr_pm);
    lv_label_set_text(s_lbl_aqi, "PM 2.5 | WAITING");
    lv_obj_set_style_text_color(s_lbl_aqi, lv_color_hex(0x334455), 0);
    lv_obj_set_style_text_font(s_lbl_aqi, &lv_font_montserrat_24, 0);
    lv_obj_align(s_lbl_aqi, LV_ALIGN_TOP_MID, 0, 334);

    // ── 7-day strip card ─────────────────────────────────────
    lv_obj_t *strip = lv_obj_create(scr_pm);
    lv_obj_set_size(strip, 688, 175);
    lv_obj_align(strip, LV_ALIGN_BOTTOM_MID, 0, -72);  // -72: clears 56px ▶ button with 8px gap
    lv_obj_set_style_bg_color(strip, lv_color_hex(0x1A1A26), 0);
    lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(strip, 18, 0);
    lv_obj_set_style_border_color(strip, lv_color_hex(0x2C2C3C), 0);
    lv_obj_set_style_border_width(strip, 1, 0);
    lv_obj_set_style_pad_all(strip, 0, 0);
    lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_st = lv_label_create(strip);
    lv_label_set_text(lbl_st, "PM 2.5 | 7 DAYS");
    lv_obj_set_style_text_color(lbl_st, lv_color_hex(0x7788AA), 0);
    lv_obj_set_style_text_font(lbl_st, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_st, 16, 14);

    for (int i = 0; i < 7; i++) {
        int sx = 16 + i * SW;
        bool is_today = (i == 6);

        lv_obj_t *lbl_d = lv_label_create(strip);
        lv_label_set_text(lbl_d, PM_DAY[i]);
        lv_obj_set_size(lbl_d, SW, LV_SIZE_CONTENT);
        lv_obj_set_pos(lbl_d, sx, 38);
        lv_obj_set_style_text_align(lbl_d, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(lbl_d, lv_color_hex(is_today ? 0x00E5FFu : 0x445566u), 0);
        lv_obj_set_style_text_font(lbl_d, &lv_font_montserrat_24, 0);

        float val = PM_HIST[i];
        uint32_t col = is_today ? 0x2A2A3Au : pm_aqi_color(val);
        lv_obj_t *dot = lv_obj_create(strip);
        lv_obj_set_size(dot, 30, 30);
        lv_obj_set_pos(dot, sx + SW / 2 - 15, 78);
        lv_obj_set_style_bg_color(dot, lv_color_hex(col), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

        char vbuf[8];
        if (is_today) snprintf(vbuf, sizeof(vbuf), "--");
        else          snprintf(vbuf, sizeof(vbuf), "%.0f", val);
        lv_obj_t *lbl_v = lv_label_create(strip);
        lv_label_set_text(lbl_v, vbuf);
        lv_obj_set_size(lbl_v, SW, LV_SIZE_CONTENT);
        lv_obj_set_pos(lbl_v, sx, 118);
        lv_obj_set_style_text_align(lbl_v, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(lbl_v, lv_color_hex(is_today ? 0x00E5FFu : 0x889AAAu), 0);
        lv_obj_set_style_text_font(lbl_v, &lv_font_montserrat_24, 0);

        if (is_today) {
            s_dot_today = dot;
            s_val_today = lbl_v;
        }
    }
}

// ── ui_pm_update (called from sensor task with display lock held) ─────────────
void ui_pm_update(float pm25, float pm10)
{
    if (!scr_pm) return;

    char buf[16];

    if (s_lbl_cur) {
        snprintf(buf, sizeof(buf), "%.1f", pm25);
        lv_label_set_text(s_lbl_cur, buf);
        lv_obj_set_style_text_color(s_lbl_cur, lv_color_hex(pm_aqi_color(pm25)), 0);
    }
    if (s_lbl_aqi) {
        char abuf[32];
        snprintf(abuf, sizeof(abuf), "PM 2.5 | %s", pm_aqi_label(pm25));
        lv_label_set_text(s_lbl_aqi, abuf);
        lv_obj_set_style_text_color(s_lbl_aqi, lv_color_hex(pm_aqi_color(pm25)), 0);
    }
    if (s_dot_today)
        lv_obj_set_style_bg_color(s_dot_today, lv_color_hex(pm_aqi_color(pm25)), 0);
    if (s_val_today) {
        snprintf(buf, sizeof(buf), "%.1f", pm25);
        lv_label_set_text(s_val_today, buf);
    }
}
