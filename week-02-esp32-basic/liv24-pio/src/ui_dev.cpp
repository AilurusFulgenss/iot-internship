#include "ui_dev.h"
#include "calib.h"
#include "esp_log.h"
#include <stdio.h>
#include <math.h>

lv_obj_t *scr_dev = NULL;

// ── Sensor metadata ──────────────────────────────────────────────────────────

static const char  *NAMES[5]    = {"TEMPERATURE", "HUMIDITY", "SOUND", "PM 2.5", "PM 10"};
static const char  *UNITS[5]    = {"\xc2\xb0""C", "%", "dB", "ug/m3", "ug/m3"};
static const float  OFF_STEP[5] = {0.1f, 0.1f, 1.0f, 0.1f, 0.1f};
static const float  GAIN_STEP   = 0.01f;

// ── Runtime state ────────────────────────────────────────────────────────────

static float g_raw[5]       = {};   // last raw values (to recalc cal on button press)
static lv_obj_t *lbl_status = NULL; // "unsaved" / "saved" indicator

// Per-sensor widget handles
typedef struct {
    lv_obj_t *lbl_raw;
    lv_obj_t *lbl_off_val;
    lv_obj_t *lbl_gain_val;
    lv_obj_t *lbl_cal;
} row_t;
static row_t g_rows[5];

// ── Calibration pointer by sensor index ──────────────────────────────────────

static sensor_calib_t *calib_ptr(int idx)
{
    switch (idx) {
        case 0: return &g_calib.temp;
        case 1: return &g_calib.hum;
        case 2: return &g_calib.sound;
        case 3: return &g_calib.pm25;
        default: return &g_calib.pm10;
    }
}

// ── Refresh one row after offset/gain change ─────────────────────────────────

static void refresh_row(int idx)
{
    char buf[16];
    sensor_calib_t *c = calib_ptr(idx);

    snprintf(buf, sizeof(buf), "%+.2f", c->offset);
    lv_label_set_text(g_rows[idx].lbl_off_val, buf);

    snprintf(buf, sizeof(buf), "%.2f", c->gain);
    lv_label_set_text(g_rows[idx].lbl_gain_val, buf);

    snprintf(buf, sizeof(buf), "%.1f", calib_apply(g_raw[idx], c));
    lv_label_set_text(g_rows[idx].lbl_cal, buf);
}

// ── Button callbacks ──────────────────────────────────────────────────────────
//
// user_data encoding: (sensor_idx * 4) + (is_gain * 2) + is_plus
//   sensor 0-4, is_gain 0/1, is_plus 0/1  →  values 0-19
//
static void adj_cb(lv_event_t *e)
{
    int code    = (int)(intptr_t)lv_event_get_user_data(e);
    int sensor  = code / 4;
    int is_gain = (code % 4) >= 2;
    int is_plus = (code % 2) != 0;
    float sign  = is_plus ? 1.0f : -1.0f;

    sensor_calib_t *c = calib_ptr(sensor);
    if (is_gain)
        c->gain   = fmaxf(0.1f, fminf(5.0f,  c->gain   + sign * GAIN_STEP));
    else
        c->offset = fmaxf(-50.0f, fminf(50.0f, c->offset + sign * OFF_STEP[sensor]));

    refresh_row(sensor);

    if (lbl_status) {
        lv_label_set_text(lbl_status, "  unsaved");
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(0xFF7700), 0);
    }
}

static void save_cb(lv_event_t *e)
{
    calib_save();
    if (lbl_status) {
        lv_label_set_text(lbl_status, "  saved");
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x00CC66), 0);
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static lv_obj_t *make_lbl(lv_obj_t *parent, const char *text,
                           uint32_t color, const lv_font_t *font,
                           lv_align_t align, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_align(l, align, x, y);
    return l;
}

static void make_adj_btn(lv_obj_t *parent, const char *txt,
                         int code, lv_align_t align, int x, int y)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 50, 38);
    lv_obj_align(btn, align, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x222238), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2A50), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x3A3A58), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, adj_cb, LV_EVENT_CLICKED, (void *)(intptr_t)code);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xAABBDD), 0);
    lv_obj_center(lbl);
}

// ── Build one sensor card ─────────────────────────────────────────────────────
//
//  Card layout (688 × 130 px, pad 14):
//
//    ┌────────────────────────────────────────────────────────────────┐
//    │ TEMPERATURE           RAW: 24.2          >      CAL: 23.7 °C  │  ← line 1
//    │ OFF [−][+0.00][+]              GAIN [−][1.00][+]              │  ← line 2
//    └────────────────────────────────────────────────────────────────┘
//
static void make_sensor_card(lv_obj_t *parent, int idx)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 688, 130);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x12121C), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2A2A42), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Line 1 ──────────────────────────────────────────────
    make_lbl(card, NAMES[idx], 0x5577AA, &lv_font_montserrat_14,
             LV_ALIGN_TOP_LEFT, 0, 4);

    // RAW value (gray — before calibration)
    g_rows[idx].lbl_raw = make_lbl(card, "--", 0x778899, &lv_font_montserrat_24,
                                    LV_ALIGN_TOP_LEFT, 160, 0);
    make_lbl(card, ">", 0x334455, &lv_font_montserrat_14,
             LV_ALIGN_TOP_MID, 0, 6);

    // CAL value (cyan — after calibration)
    g_rows[idx].lbl_cal = make_lbl(card, "--", 0x00E5FF, &lv_font_montserrat_24,
                                    LV_ALIGN_TOP_RIGHT, -38, 0);
    make_lbl(card, UNITS[idx], 0x445566, &lv_font_montserrat_14,
             LV_ALIGN_TOP_RIGHT, 0, 6);

    // Line 2 — OFFSET controls ────────────────────────────
    int base = idx * 4;

    make_lbl(card, "OFF", 0x556677, &lv_font_montserrat_14,
             LV_ALIGN_BOTTOM_LEFT, 0, -6);
    make_adj_btn(card, "-", base + 0, LV_ALIGN_BOTTOM_LEFT,  40, 0);

    g_rows[idx].lbl_off_val = make_lbl(card, "+0.00", 0xFFFFFF, &lv_font_montserrat_24,
                                        LV_ALIGN_BOTTOM_LEFT, 96, -2);
    make_adj_btn(card, "+", base + 1, LV_ALIGN_BOTTOM_LEFT, 174, 0);

    // Line 2 — GAIN controls ──────────────────────────────
    make_lbl(card, "GAIN", 0x556677, &lv_font_montserrat_14,
             LV_ALIGN_BOTTOM_LEFT, 244, -6);
    make_adj_btn(card, "-", base + 2, LV_ALIGN_BOTTOM_LEFT, 294, 0);

    g_rows[idx].lbl_gain_val = make_lbl(card, "1.00", 0xFFFFFF, &lv_font_montserrat_24,
                                         LV_ALIGN_BOTTOM_LEFT, 350, -2);
    make_adj_btn(card, "+", base + 3, LV_ALIGN_BOTTOM_LEFT, 418, 0);

    // Populate labels with current g_calib values
    refresh_row(idx);
}

// ── ui_dev_create ─────────────────────────────────────────────────────────────

void ui_dev_create(void)
{
    scr_dev = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_dev, lv_color_hex(0x080812), 0);
    lv_obj_set_style_bg_opa(scr_dev, LV_OPA_COVER, 0);

    // ── Header (fixed, 64 px) ─────────────────────────────
    lv_obj_t *hdr = lv_obj_create(scr_dev);
    lv_obj_set_size(hdr, 720, 64);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x0E0E1A), 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 22, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bar = lv_obj_create(hdr);
    lv_obj_set_size(bar, 720, 3);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 40 );
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0088FF), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);

    make_lbl(hdr, "DEV MODE", 0x5577AA, &lv_font_montserrat_32,
             LV_ALIGN_LEFT_MID, 0, 0);

    // ── Scrollable card container ─────────────────────────
    lv_obj_t *cont = lv_obj_create(scr_dev);
    lv_obj_set_size(cont, 720, 656);   // 720 - 64
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_left(cont,   16, 0);
    lv_obj_set_style_pad_right(cont,  16, 0);
    lv_obj_set_style_pad_top(cont,    30, 0);
    lv_obj_set_style_pad_bottom(cont, 20, 0);
    lv_obj_set_style_pad_row(cont,    10, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

    // One card per sensor
    for (int i = 0; i < 5; i++)
        make_sensor_card(cont, i);

    // ── Save section ──────────────────────────────────────
    lv_obj_t *save_row = lv_obj_create(cont);
    lv_obj_set_size(save_row, 688, 72);
    lv_obj_set_style_bg_opa(save_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(save_row, 0, 0);
    lv_obj_set_style_pad_all(save_row, 0, 0);
    lv_obj_clear_flag(save_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_save = lv_btn_create(save_row);
    lv_obj_set_size(btn_save, 320, 52);
    lv_obj_align(btn_save, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x003388), 0);
    lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x0044AA), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_save, 10, 0);
    lv_obj_set_style_shadow_width(btn_save, 0, 0);
    lv_obj_add_event_cb(btn_save, save_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_btn = lv_label_create(btn_save);
    lv_label_set_text(lbl_btn, "SAVE TO NVS");
    lv_obj_set_style_text_font(lbl_btn, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_btn, lv_color_hex(0xCCDDFF), 0);
    lv_obj_center(lbl_btn);

    lbl_status = make_lbl(save_row, "", 0x445566, &lv_font_montserrat_14,
                           LV_ALIGN_RIGHT_MID, 0, 0);
}

// ── ui_dev_update (called from sensor task with display lock held) ────────────

void ui_dev_update(float t_raw, float h_raw, float s_raw,
                   float p25_raw, float p10_raw)
{
    if (!scr_dev) return;

    float raws[5] = {t_raw, h_raw, s_raw, p25_raw, p10_raw};
    char buf[16];

    for (int i = 0; i < 5; i++) {
        g_raw[i] = raws[i];

        snprintf(buf, sizeof(buf), "%.1f", raws[i]);
        if (g_rows[i].lbl_raw) lv_label_set_text(g_rows[i].lbl_raw, buf);

        snprintf(buf, sizeof(buf), "%.1f", calib_apply(raws[i], calib_ptr(i)));
        if (g_rows[i].lbl_cal) lv_label_set_text(g_rows[i].lbl_cal, buf);
    }
}