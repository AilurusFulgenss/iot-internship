#include "ui_dev.h"
#include "calib.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

extern void set_app_mode(int mode);

lv_obj_t *scr_dev = NULL;

static const char *SN_NAMES[]    = {"TEMPERATURE", "HUMIDITY", "SOUND", "PM 2.5", "PM 10"};
static const char *SN_UNITS[]    = {"\xc2\xb0""C", "%", "dB", "ug/m3", "ug/m3"};
static const float SN_OFF_STEP[] = {0.1f, 0.1f, 1.0f, 0.1f, 0.1f};

typedef struct {
    lv_obj_t *lbl_raw;
    lv_obj_t *lbl_off_val;
    lv_obj_t *lbl_gain_val;
    lv_obj_t *lbl_cal;
} row_t;

static row_t  g_rows_sn[5];
static float  g_raw_sn[5] = {0, 0, 0, 0, 0};
static lv_obj_t *lbl_status = NULL;

// ── Calib helpers ─────────────────────────────────────────────────────────────

static sensor_calib_t *calib_gs(int idx)
{
    switch (idx) {
        case 0: return &g_calib.temp;
        case 1: return &g_calib.hum;
        case 2: return &g_calib.sound;
        case 3: return &g_calib.pm25;
        default: return &g_calib.pm10;
    }
}

static void refresh_row(int idx)
{
    char buf[16];
    sensor_calib_t *c = calib_gs(idx);
    row_t *r = &g_rows_sn[idx];
    snprintf(buf, sizeof(buf), "%+.2f", c->offset);
    if (r->lbl_off_val) lv_label_set_text(r->lbl_off_val, buf);
    snprintf(buf, sizeof(buf), "%.2f", c->gain);
    if (r->lbl_gain_val) lv_label_set_text(r->lbl_gain_val, buf);
    snprintf(buf, sizeof(buf), "%.1f", calib_apply(g_raw_sn[idx], c));
    if (r->lbl_cal) lv_label_set_text(r->lbl_cal, buf);
}

// ── Calib button callbacks ────────────────────────────────────────────────────
// code = (sensor << 2) | (is_gain << 1) | is_plus

static void adj_cb(lv_event_t *e)
{
    int code    = (int)(intptr_t)lv_event_get_user_data(e);
    int sensor  = (code >> 2) & 0x7;
    int is_gain = (code >> 1) & 0x1;
    int is_plus = code & 0x1;
    float sign  = is_plus ? 1.0f : -1.0f;

    sensor_calib_t *c = calib_gs(sensor);
    if (is_gain)
        c->gain   = fmaxf(0.1f,    fminf(5.0f,   c->gain   + sign * 0.01f));
    else
        c->offset = fmaxf(-200.0f, fminf(200.0f, c->offset + sign * SN_OFF_STEP[sensor]));

    refresh_row(sensor);
    if (lbl_status) {
        lv_label_set_text(lbl_status, "unsaved");
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(0xFF7700), 0);
    }
}

static void save_cb(lv_event_t *e)
{
    calib_save();
    if (lbl_status) {
        lv_label_set_text(lbl_status, "saved");
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x00CC66), 0);
    }
}

static void back_cb(lv_event_t *e)
{
    set_app_mode(0);
}

// ── Widget helpers ────────────────────────────────────────────────────────────

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

static void make_sensor_card(lv_obj_t *parent, int idx,
                              const char *name, const char *unit)
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

    row_t *r = &g_rows_sn[idx];
    make_lbl(card, name, 0x5577AA, &lv_font_montserrat_14, LV_ALIGN_TOP_LEFT, 0, 4);
    r->lbl_raw = make_lbl(card, "--", 0x778899, &lv_font_montserrat_24,
                          LV_ALIGN_TOP_LEFT, 160, 0);
    make_lbl(card, ">", 0x334455, &lv_font_montserrat_14, LV_ALIGN_TOP_MID, 0, 6);
    r->lbl_cal = make_lbl(card, "--", 0x00E5FF, &lv_font_montserrat_24,
                          LV_ALIGN_TOP_RIGHT, -38, 0);
    make_lbl(card, unit, 0x445566, &lv_font_montserrat_14, LV_ALIGN_TOP_RIGHT, 0, 6);

    int base = idx << 2;
    make_lbl(card, "OFF",  0x556677, &lv_font_montserrat_14, LV_ALIGN_BOTTOM_LEFT,   0, -6);
    make_adj_btn(card, "-", base + 0, LV_ALIGN_BOTTOM_LEFT,  40, 0);
    r->lbl_off_val = make_lbl(card, "+0.00", 0xFFFFFF, &lv_font_montserrat_24,
                               LV_ALIGN_BOTTOM_LEFT, 96, -2);
    make_adj_btn(card, "+", base + 1, LV_ALIGN_BOTTOM_LEFT, 174, 0);
    make_lbl(card, "GAIN", 0x556677, &lv_font_montserrat_14, LV_ALIGN_BOTTOM_LEFT, 244, -6);
    make_adj_btn(card, "-", base + 2, LV_ALIGN_BOTTOM_LEFT, 294, 0);
    r->lbl_gain_val = make_lbl(card, "1.00", 0xFFFFFF, &lv_font_montserrat_24,
                                LV_ALIGN_BOTTOM_LEFT, 350, -2);
    make_adj_btn(card, "+", base + 3, LV_ALIGN_BOTTOM_LEFT, 418, 0);
    refresh_row(idx);
}

static void add_save_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 688, 72);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn = lv_btn_create(row);
    lv_obj_set_size(btn, 320, 52);
    lv_obj_align(btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x003388), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0044AA), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "SAVE TO NVS");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xCCDDFF), 0);
    lv_obj_center(lbl);
}

// ── ui_dev_create ─────────────────────────────────────────────────────────────

void ui_dev_create(void)
{
    scr_dev = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_dev, lv_color_hex(0x080812), 0);
    lv_obj_set_style_bg_opa(scr_dev, LV_OPA_COVER, 0);
    // ── Header ───────────────────────────────────────────────────────────────
    lv_obj_t *hdr = lv_obj_create(scr_dev);
    lv_obj_set_size(hdr, 720, 64);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x0E0E1A), 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 22, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hdr, LV_OBJ_FLAG_FLOATING);

    lv_obj_t *bar = lv_obj_create(hdr);
    lv_obj_set_size(bar, 720, 3);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0088FF), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);

    // Back button (left side)
    lv_obj_t *btn_back = lv_btn_create(hdr);
    lv_obj_set_size(btn_back, 80, 38);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x2A2A40), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_back, lv_color_hex(0x334455), 0);
    lv_obj_set_style_border_width(btn_back, 1, 0);
    lv_obj_set_style_radius(btn_back, 8, 0);
    lv_obj_set_style_shadow_width(btn_back, 0, 0);
    lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "< Back");
    lv_obj_set_style_text_color(lbl_back, lv_color_hex(0x5577AA), 0);
    lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl_back);

    make_lbl(hdr, "DEV MODE", 0x5577AA, &lv_font_montserrat_32, LV_ALIGN_CENTER, 0, 0);
    lbl_status = make_lbl(hdr, "", 0x445566, &lv_font_montserrat_14, LV_ALIGN_RIGHT_MID, 0, 0);

    // ── SN-300 Calib content ─────────────────────────────────────────────────
    lv_obj_t *cont = lv_obj_create(scr_dev);
    lv_obj_set_pos(cont, 0, 64);
    lv_obj_set_size(cont, 720, 1280 - 64);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_left(cont,   16, 0);
    lv_obj_set_style_pad_right(cont,  16, 0);
    lv_obj_set_style_pad_top(cont,    16, 0);
    lv_obj_set_style_pad_bottom(cont, 20, 0);
    lv_obj_set_style_pad_row(cont,    10, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

    for (int i = 0; i < 5; i++)
        make_sensor_card(cont, i, SN_NAMES[i], SN_UNITS[i]);

    add_save_row(cont);

    lv_obj_move_foreground(hdr);
}

// ── ui_dev_update_sn300 (call from inside bsp_display_lock) ─────────────────

void ui_dev_update_sn300(float t_raw, float h_raw, float s_raw,
                         float p25_raw, float p10_raw)
{
    if (!scr_dev) return;
    float raws[5] = {t_raw, h_raw, s_raw, p25_raw, p10_raw};
    char buf[16];
    for (int i = 0; i < 5; i++) {
        g_raw_sn[i] = raws[i];
        snprintf(buf, sizeof(buf), "%.1f", raws[i]);
        if (g_rows_sn[i].lbl_raw) lv_label_set_text(g_rows_sn[i].lbl_raw, buf);
        snprintf(buf, sizeof(buf), "%.1f", calib_apply(raws[i], calib_gs(i)));
        if (g_rows_sn[i].lbl_cal) lv_label_set_text(g_rows_sn[i].lbl_cal, buf);
    }
}
