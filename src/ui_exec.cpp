#include "ui_exec.h"
#include "ui_exec_detail.h"
#include "history.h"
#include "eth_upload.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <math.h>

static const char *TAG = "EXEC";
lv_obj_t *scr_exec = NULL;

// ── Status helpers ────────────────────────────────────────────────────────────

typedef struct { const char *text; uint32_t color; } status_t;

static status_t st_pm25(float v) {
    if (isnan(v)||v<0) return {"--",   0x2A3A4Au};
    if (v<=12.0f)      return {"GOOD", 0x00C853u};
    if (v<=35.4f)      return {"MOD",  0xFFD600u};
    if (v<=55.4f)      return {"SENS", 0xFF6D00u};
    return                    {"HIGH", 0xCC0033u};
}
static status_t st_ec(float v) {
    if (isnan(v)||v<0) return {"--",   0x2A3A4Au};
    if (v<=300)        return {"GOOD", 0x00C853u};
    if (v<=500)        return {"WARN", 0xFFD600u};
    return                    {"HIGH", 0xCC0033u};
}
static status_t st_orp(float v) {
    if (isnan(v))  return {"--",   0x2A3A4Au};
    if (v<200)     return {"LOW",  0xFF6D00u};
    if (v<=400)    return {"GOOD", 0x00C853u};
    return               {"HIGH", 0xFF6D00u};
}
static status_t st_temp(float v) {
    if (isnan(v))  return {"--",   0x2A3A4Au};
    if (v<18)      return {"COLD", 0x1565C0u};
    if (v<27)      return {"GOOD", 0x00C853u};
    if (v<35)      return {"WARM", 0xFFD600u};
    return               {"HOT",  0xCC0033u};
}
static status_t st_moist(float v) {
    if (isnan(v)||v<0) return {"--",  0x2A3A4Au};
    if (v<20)          return {"DRY", 0xFF6D00u};
    if (v<=70)         return {"OK",  0x00C853u};
    return                    {"WET", 0x1565C0u};
}

// ── Card geometry ─────────────────────────────────────────────────────────────
// Screen 720×720 | header 72px | outer pad 16px
// 2 cols × 3 rows  |  card 338×197  |  gap 12px
//
// col0 x=16   col1 x=366
// row0 y=88   row1 y=297   row2 y=506

#define CARD_W   338
#define CARD_H   197
#define CARD_PAD  16

// Card indices: 0=PM  1=HHCC  2=EC  3=ORP  4=LEAK  5=TH

static lv_obj_t *s_card[6]     = {};
static lv_obj_t *s_lbl_stat[6] = {};
static lv_obj_t *s_lbl_prim[6] = {};
static lv_obj_t *s_lbl_unit[6] = {};
static lv_obj_t *s_lbl_sec[6]  = {};

static const uint32_t ACCENT[6] = {
    0x00E5FFu,  // PM   — cyan
    0x44FF88u,  // HHCC — green
    0x00C4FFu,  // EC   — sky blue
    0xFFAA44u,  // ORP  — amber
    0x44FF88u,  // LEAK — green (changes red on alarm)
    0x44FFE0u,  // TH   — teal
};
static const char *CARD_TITLE[6] = {
    "PM2.5 - SN-300",
    "HHCC Flora",
    "EC / TDS",
    "ORP Sensor",
    "Leak Detector",
    "TH - CWT-TH04S",
};
static const char *CARD_UNIT[6] = {
    "ug/m3",
    "% moisture",
    "uS/cm",
    "mV",
    "",
    "\xc2\xb0""C",
};

// ── Card factory ──────────────────────────────────────────────────────────────

static void card_tap_cb(lv_event_t *e)
{
    int ci = (int)(intptr_t)lv_event_get_user_data(e);
    ui_exec_detail_open(ci);
    lv_scr_load_anim(scr_exec_detail, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

static void make_exec_card(int ci, int x, int y)
{
    lv_obj_t *c = lv_obj_create(scr_exec);
    lv_obj_set_size(c, CARD_W, CARD_H);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_style_bg_color(c, lv_color_hex(0x0D0D1Cu), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 14, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(0x252538u), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_pad_all(c, CARD_PAD, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(c, card_tap_cb, LV_EVENT_CLICKED, (void *)(intptr_t)ci);
    s_card[ci] = c;

    // Sensor title (top-left, accent colour at 60% opacity)
    lv_obj_t *lbl_n = lv_label_create(c);
    lv_label_set_text(lbl_n, CARD_TITLE[ci]);
    lv_obj_set_style_text_color(lbl_n, lv_color_hex(ACCENT[ci]), 0);
    lv_obj_set_style_text_opa(lbl_n, LV_OPA_60, 0);
    lv_obj_set_style_text_font(lbl_n, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_n, LV_ALIGN_TOP_LEFT, 0, 0);

    // Status badge (top-right, dynamically coloured)
    s_lbl_stat[ci] = lv_label_create(c);
    lv_label_set_text(s_lbl_stat[ci], "--");
    lv_obj_set_style_text_color(s_lbl_stat[ci], lv_color_hex(0x2A3A4Au), 0);
    lv_obj_set_style_text_font(s_lbl_stat[ci], &lv_font_montserrat_14, 0);
    lv_obj_align(s_lbl_stat[ci], LV_ALIGN_TOP_RIGHT, 0, 0);

    // Primary value (large, top-left with vertical offset)
    s_lbl_prim[ci] = lv_label_create(c);
    lv_label_set_text(s_lbl_prim[ci], "--");
    lv_obj_set_style_text_color(s_lbl_prim[ci], lv_color_hex(0x2A3A4Au), 0);
    lv_obj_set_style_text_font(s_lbl_prim[ci], &lv_font_montserrat_48, 0);
    lv_obj_align(s_lbl_prim[ci], LV_ALIGN_TOP_LEFT, 0, 22);

    // Unit (small, bottom-right of primary area)
    s_lbl_unit[ci] = lv_label_create(c);
    lv_label_set_text(s_lbl_unit[ci], CARD_UNIT[ci]);
    lv_obj_set_style_text_color(s_lbl_unit[ci], lv_color_hex(0x2A3A4Au), 0);
    lv_obj_set_style_text_font(s_lbl_unit[ci], &lv_font_montserrat_14, 0);
    lv_obj_align(s_lbl_unit[ci], LV_ALIGN_TOP_RIGHT, 0, 90);

    // Secondary info (bottom-left, dimmed)
    s_lbl_sec[ci] = lv_label_create(c);
    lv_label_set_text(s_lbl_sec[ci], "");
    lv_obj_set_style_text_color(s_lbl_sec[ci], lv_color_hex(0x2A3A4Au), 0);
    lv_obj_set_style_text_font(s_lbl_sec[ci], &lv_font_montserrat_14, 0);
    lv_obj_align(s_lbl_sec[ci], LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

// ── ui_exec_create ────────────────────────────────────────────────────────────

void ui_exec_create(void)
{
    ESP_LOGI(TAG, "creating EXEC screen...");

    scr_exec = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_exec, lv_color_hex(0x06060Fu), 0);
    lv_obj_set_style_bg_opa(scr_exec, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr_exec, 0, 0);

    // ── Header 72px (same structure as USER/PM screens) ───────────────────────
    lv_obj_t *hdr = lv_obj_create(scr_exec);
    lv_obj_set_size(hdr, 720, 72);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x12121Eu), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 22, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *accent_line = lv_obj_create(hdr);
    lv_obj_set_size(accent_line, 720, 3);
    lv_obj_align(accent_line, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_color(accent_line, lv_color_hex(0xFFAA00u), 0);
    lv_obj_set_style_border_width(accent_line, 0, 0);
    lv_obj_set_style_pad_all(accent_line, 0, 0);

    int title_x = 0;
    if (eth_upload_has_logo()) {
        lv_obj_t *logo = lv_image_create(hdr);
        lv_image_set_src(logo, ETH_LOGO_LVGL_PATH);
        lv_obj_set_size(logo, 48, 48);
        lv_obj_align(logo, LV_ALIGN_LEFT_MID, 0, 0);
        title_x = 58;
    }

    lv_obj_t *lbl_title = lv_label_create(hdr);
    lv_label_set_text(lbl_title, "EXEC");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xFFAA00u), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_32, 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, title_x, 0);

    lv_obj_t *lbl_sub = lv_label_create(hdr);
    lv_label_set_text(lbl_sub, "All Sensors Overview");
    lv_obj_set_style_text_color(lbl_sub, lv_color_hex(0x3A3510u), 0);
    lv_obj_set_style_text_font(lbl_sub, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_sub, LV_ALIGN_LEFT_MID, 90 + title_x, 12);

    // <- USER button added by touch_nav_init at TOP_RIGHT,-8,12 (88×40px)

    // ── 6 sensor cards: 2 cols × 3 rows ──────────────────────────────────────
    //   col0 x=16    col1 x=366
    //   row0 y=88    row1 y=297    row2 y=506
    make_exec_card(0,  16,  88);   // PM2.5  (SN-300)
    make_exec_card(1, 366,  88);   // HHCC Flora
    make_exec_card(2,  16, 297);   // EC / TDS
    make_exec_card(3, 366, 297);   // ORP
    make_exec_card(4,  16, 506);   // Leak Detector
    make_exec_card(5, 366, 506);   // TH (CWT-TH04S)

    // LEAK card: use font_32 + centre for status text (no numeric value)
    lv_obj_set_style_text_font(s_lbl_prim[4], &lv_font_montserrat_32, 0);
    lv_obj_align(s_lbl_prim[4], LV_ALIGN_CENTER, 0, 8);

    ESP_LOGI(TAG, "EXEC created OK");
}

// ── Shared helper ─────────────────────────────────────────────────────────────

static void apply_status(int ci, status_t st)
{
    lv_label_set_text(s_lbl_stat[ci], st.text);
    lv_obj_set_style_text_color(s_lbl_stat[ci], lv_color_hex(st.color), 0);
    lv_obj_set_style_text_color(s_lbl_prim[ci], lv_color_hex(st.color), 0);
    lv_obj_set_style_text_color(s_lbl_unit[ci], lv_color_hex(st.color | 0x111111u), 0);
    lv_obj_set_style_text_color(s_lbl_sec[ci],  lv_color_hex(st.color >> 1 & 0x7F7F7Fu), 0);
}

// ── Update functions ──────────────────────────────────────────────────────────

void ui_exec_update_pm(float temp, float hum, float pm25, float pm10)
{
    if (!scr_exec) return;
    char buf[48];
    if (!isnan(pm25) && pm25 >= 0) {
        snprintf(buf, sizeof(buf), "%.1f", pm25);
        lv_label_set_text(s_lbl_prim[0], buf);
        apply_status(0, st_pm25(pm25));
    }
    if (!isnan(pm10) && !isnan(temp) && !isnan(hum)) {
        snprintf(buf, sizeof(buf), "PM10:%.0f  T:%.0f\xc2\xb0  H:%.0f%%",
                 pm10, temp, hum);
        lv_label_set_text(s_lbl_sec[0], buf);
    }
}

void ui_exec_update_hhcc(float temp, float moisture, float light,
                          float fertility, float battery)
{
    if (!scr_exec) return;
    char buf[64];
    if (!isnan(moisture) && moisture >= 0) {
        snprintf(buf, sizeof(buf), "%.0f", moisture);
        lv_label_set_text(s_lbl_prim[1], buf);
        apply_status(1, st_moist(moisture));
    }
    snprintf(buf, sizeof(buf), "T:%.1f\xc2\xb0  Lux:%.0f  F:%.0f  Bat:%.0f%%",
             isnan(temp)?0.f:temp, isnan(light)?0.f:light,
             isnan(fertility)?0.f:fertility, isnan(battery)?0.f:battery);
    lv_label_set_text(s_lbl_sec[1], buf);
}

void ui_exec_update_ec(float ec)
{
    if (!scr_exec) return;
    char buf[32];
    if (!isnan(ec) && ec >= 0) {
        snprintf(buf, sizeof(buf), "%.0f", ec);
        lv_label_set_text(s_lbl_prim[2], buf);
        apply_status(2, st_ec(ec));
        snprintf(buf, sizeof(buf), "TDS: %.0f mg/L", ec * 0.67f);
        lv_label_set_text(s_lbl_sec[2], buf);
    }
}

void ui_exec_update_orp(float orp, float temp)
{
    if (!scr_exec) return;
    char buf[32];
    if (!isnan(orp)) {
        snprintf(buf, sizeof(buf), "%.0f", orp);
        lv_label_set_text(s_lbl_prim[3], buf);
        apply_status(3, st_orp(orp));
    }
    if (!isnan(temp)) {
        snprintf(buf, sizeof(buf), "Temp: %.1f\xc2\xb0""C", temp);
        lv_label_set_text(s_lbl_sec[3], buf);
    }
}

void ui_exec_update_leak(bool alarm)
{
    if (!scr_exec) return;
    lv_label_set_text(s_lbl_prim[4], alarm ? "ALARM!" : "CLEAR");
    status_t st = alarm
        ? status_t{"ALARM", 0xCC0033u}
        : status_t{"CLEAR", 0x00C853u};
    apply_status(4, st);
    // Pulse the card border red on alarm
    lv_obj_set_style_border_color(s_card[4],
        lv_color_hex(alarm ? 0xCC0033u : 0x252538u), 0);
    lv_obj_set_style_border_width(s_card[4], alarm ? 2 : 1, 0);
}

void ui_exec_update_th(float temp, float hum)
{
    if (!scr_exec) return;
    char buf[32];
    if (!isnan(temp)) {
        snprintf(buf, sizeof(buf), "%.1f", temp);
        lv_label_set_text(s_lbl_prim[5], buf);
        apply_status(5, st_temp(temp));
    }
    if (!isnan(hum)) {
        snprintf(buf, sizeof(buf), "Humidity: %.1f%%", hum);
        lv_label_set_text(s_lbl_sec[5], buf);
    }
}

void ui_exec_update_history(void)
{
    // Reserved — future bar-chart drill-down will use g_hist_7d here
}
