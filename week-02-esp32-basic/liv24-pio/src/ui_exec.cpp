#include "ui_exec.h"
#include "history.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <math.h>

static const char *TAG = "EXEC";

lv_obj_t *scr_exec = NULL;

// ── Color standards (same thresholds as ui_pm.cpp) ──────────────────────────

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

typedef uint32_t (*color_fn_t)(float);

// ── Card definitions ─────────────────────────────────────────────────────────

static const char     *CARD_TITLE[4] = {"TEMPERATURE", "HUMIDITY",  "PM 2.5",  "PM 10"};
static const char     *CARD_UNIT[4]  = {"C",           "%",         "ug/m3",   "ug/m3"};
static const int       CARD_HIST[4]  = {HIST_TEMP,     HIST_HUM,   HIST_PM25, HIST_PM10};
static const float     CARD_MAX[4]   = {50.0f,         100.0f,     150.0f,    300.0f};
static const color_fn_t CARD_COLOR[4] = {temp_color, hum_color, pm25_color, pm10_color};

static lv_obj_t *s_lbl_val[4]    = {};
static lv_obj_t *s_bars_7d[4][7] = {};

#define CARD_PAD    16
#define CARD_W      328
#define CARD_H      240
#define INNER_W     (CARD_W - 2*CARD_PAD)   // 296
#define INNER_H     (CARD_H - 2*CARD_PAD)   // 208
#define MAX_BAR_H   80
#define BAR_W       32
#define BAR_GAP     4

// ── make_card ────────────────────────────────────────────────────────────────

static void make_card(lv_obj_t *parent, int x, int y, int ci)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0D0D1Cu), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x252538u), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, CARD_PAD, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *lbl_title = lv_label_create(card);
    lv_label_set_text(lbl_title, CARD_TITLE[ci]);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x6688AAu), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 0, 0);

    // Current value
    s_lbl_val[ci] = lv_label_create(card);
    lv_label_set_text(s_lbl_val[ci], "--");
    lv_obj_set_style_text_color(s_lbl_val[ci], lv_color_hex(0x334455u), 0);
    lv_obj_set_style_text_font(s_lbl_val[ci], &lv_font_montserrat_32, 0);
    lv_obj_align(s_lbl_val[ci], LV_ALIGN_TOP_MID, 0, 20);

    // Unit
    lv_obj_t *lbl_unit = lv_label_create(card);
    lv_label_set_text(lbl_unit, CARD_UNIT[ci]);
    lv_obj_set_style_text_color(lbl_unit, lv_color_hex(0x445566u), 0);
    lv_obj_set_style_text_font(lbl_unit, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_unit, LV_ALIGN_TOP_MID, 0, 66);

    // "7D" sublabel
    lv_obj_t *lbl_hist = lv_label_create(card);
    lv_label_set_text(lbl_hist, "7D HISTORY");
    lv_obj_set_style_text_color(lbl_hist, lv_color_hex(0x2A3A4Au), 0);
    lv_obj_set_style_text_font(lbl_hist, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_hist, 0, INNER_H - MAX_BAR_H - 20);

    // 7 bars — 7*32+6*4=248 → start_x=(296-248)/2=24
    const int start_x = (INNER_W - (7 * BAR_W + 6 * BAR_GAP)) / 2;
    for (int i = 0; i < 7; i++) {
        lv_obj_t *bar = lv_obj_create(card);
        int h = 4;  // initial min height
        lv_obj_set_size(bar, BAR_W, h);
        lv_obj_set_pos(bar, start_x + i * (BAR_W + BAR_GAP), INNER_H - h);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x1E2A3Au), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bar, 3, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        s_bars_7d[ci][i] = bar;
    }
}

// ── ui_exec_create ────────────────────────────────────────────────────────────

void ui_exec_create(void)
{
    ESP_LOGI(TAG, "creating EXEC screen...");

    scr_exec = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_exec, lv_color_hex(0x06060Fu), 0);
    lv_obj_set_style_bg_opa(scr_exec, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr_exec, 0, 0);

    // Header
    lv_obj_t *lbl_hdr = lv_label_create(scr_exec);
    lv_label_set_text(lbl_hdr, "KPI OVERVIEW");
    lv_obj_set_style_text_color(lbl_hdr, lv_color_hex(0xFFAA00u), 0);
    lv_obj_set_style_text_font(lbl_hdr, &lv_font_montserrat_32, 0);
    lv_obj_align(lbl_hdr, LV_ALIGN_TOP_LEFT, 20, 14);

    lv_obj_t *sep = lv_obj_create(scr_exec);
    lv_obj_set_size(sep, 720, 3);
    lv_obj_set_pos(sep, 0, 60);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0xFFAA00u), 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_pad_all(sep, 0, 0);

    // 4 KPI cards: same grid positions as before
    make_card(scr_exec,  24, 113, 0);   // TEMP
    make_card(scr_exec, 368, 113, 1);   // HUM
    make_card(scr_exec,  24, 373, 2);   // PM2.5
    make_card(scr_exec, 368, 373, 3);   // PM10

    ESP_LOGI(TAG, "EXEC created OK");
}

// ── ui_exec_update ────────────────────────────────────────────────────────────

void ui_exec_update(float temp, float hum, float pm25, float pm10)
{
    if (!scr_exec) return;

    float vals[4] = {temp, hum, pm25, pm10};
    char buf[16];

    for (int ci = 0; ci < 4; ci++) {
        float v = vals[ci];
        if (isnan(v)) {
            snprintf(buf, sizeof(buf), "--");
            lv_obj_set_style_text_color(s_lbl_val[ci], lv_color_hex(0x334455u), 0);
        } else {
            snprintf(buf, sizeof(buf), "%.1f", v);
            lv_obj_set_style_text_color(s_lbl_val[ci], lv_color_hex(CARD_COLOR[ci](v)), 0);
        }
        lv_label_set_text(s_lbl_val[ci], buf);
    }
}

// ── ui_exec_update_history ────────────────────────────────────────────────────

void ui_exec_update_history(void)
{
    if (!scr_exec) return;
    const int start_x = (INNER_W - (7 * BAR_W + 6 * BAR_GAP)) / 2;

    for (int ci = 0; ci < 4; ci++) {
        int hi = CARD_HIST[ci];
        int n  = g_hist_7d.count;

        for (int i = 0; i < 7; i++) {
            // right-align: bar 6 = newest, bar 0 = oldest
            int data_idx  = i - (7 - n);
            bool has_data = (n > 0 && data_idx >= 0);
            float v = has_data ? g_hist_7d.d[hi][data_idx] : NAN;

            int h;
            uint32_t col;
            if (!has_data || isnan(v) || v < 0) {
                h   = 4;
                col = 0x1E2A3Au;
            } else {
                float ratio = v / CARD_MAX[ci];
                if (ratio > 1.0f) ratio = 1.0f;
                h = (int)(ratio * MAX_BAR_H);
                if (h < 4) h = 4;
                col = CARD_COLOR[ci](v);
            }

            lv_obj_t *bar = s_bars_7d[ci][i];
            lv_obj_set_size(bar, BAR_W, h);
            lv_obj_set_pos(bar, start_x + i * (BAR_W + BAR_GAP), INNER_H - h);
            lv_obj_set_style_bg_color(bar, lv_color_hex(col), 0);
        }
    }
}
