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
static status_t st_temp(float v) {
    if (isnan(v))  return {"--",   0x2A3A4Au};
    if (v<18)      return {"COLD", 0x1565C0u};
    if (v<27)      return {"GOOD", 0x00C853u};
    if (v<35)      return {"WARM", 0xFFD600u};
    return               {"HOT",  0xCC0033u};
}
// ── Card geometry ─────────────────────────────────────────────────────────────
// Screen 720×720 | header 72px | outer pad 16px
// Single full-width card

#define CARD_W   688
#define CARD_H   220
#define CARD_PAD  16

static lv_obj_t *s_card[1]     = {};
static lv_obj_t *s_lbl_stat[1] = {};
static lv_obj_t *s_lbl_prim[1] = {};
static lv_obj_t *s_lbl_unit[1] = {};
static lv_obj_t *s_lbl_sec[1]  = {};

static const uint32_t ACCENT[1] = { 0x00E5FFu };  // PM — cyan
static const char *CARD_TITLE[1] = { "PM2.5 - SN-300" };
static const char *CARD_UNIT[1]  = { "ug/m3" };

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

    // ── Single PM card, full-width ────────────────────────────────────────────
    make_exec_card(0, 16, 88);

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

