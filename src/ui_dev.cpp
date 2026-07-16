#include "ui_dev.h"
#include "calib.h"
#include "esp_log.h"
#include <stdio.h>
#include <math.h>

lv_obj_t *scr_dev = NULL;

// ── Tab IDs ───────────────────────────────────────────────────────────────────

#define TAB_SN300  0
#define TAB_TH     1
#define TAB_EC     2
#define TAB_ORP    3
#define N_TABS     4

static lv_obj_t *g_tab_cont[N_TABS];
static lv_obj_t *g_tab_btn[N_TABS];
static int        g_active_tab = 0;
static lv_obj_t  *lbl_status   = NULL;

// ── Per-sensor metadata ───────────────────────────────────────────────────────

static const char *SN_NAMES[]    = {"TEMPERATURE", "HUMIDITY", "SOUND", "PM 2.5", "PM 10"};
static const char *SN_UNITS[]    = {"\xc2\xb0""C", "%", "dB", "ug/m3", "ug/m3"};
static const float SN_OFF_STEP[] = {0.1f, 0.1f, 1.0f, 0.1f, 0.1f};

static const char *TH_NAMES[]    = {"TEMPERATURE", "HUMIDITY"};
static const char *TH_UNITS[]    = {"\xc2\xb0""C", "%"};
static const float TH_OFF_STEP[] = {0.1f, 0.1f};

static const char *EC_NAMES[]    = {"EC"};
static const char *EC_UNITS[]    = {"uS/cm"};
static const float EC_OFF_STEP[] = {1.0f};

static const char *ORP_NAMES[]    = {"ORP", "TEMPERATURE"};
static const char *ORP_UNITS[]    = {"mV", "\xc2\xb0""C"};
static const float ORP_OFF_STEP[] = {1.0f, 0.1f};

// ── Row widget handles ────────────────────────────────────────────────────────

typedef struct {
    lv_obj_t *lbl_raw;
    lv_obj_t *lbl_off_val;
    lv_obj_t *lbl_gain_val;
    lv_obj_t *lbl_cal;
} row_t;

static row_t  g_rows_sn[5];   static float g_raw_sn[5]  = {0,0,0,0,0};
static row_t  g_rows_th[2];   static float g_raw_th[2]  = {0,0};
static row_t  g_rows_ec[1];   static float g_raw_ec[1]  = {0};
static row_t  g_rows_orp[2];  static float g_raw_orp[2] = {0,0};

// ── Lookup helpers ────────────────────────────────────────────────────────────

static sensor_calib_t *calib_gs(int group, int idx)
{
    switch (group) {
        case TAB_SN300:
            switch (idx) {
                case 0: return &g_calib.temp;
                case 1: return &g_calib.hum;
                case 2: return &g_calib.sound;
                case 3: return &g_calib.pm25;
                default: return &g_calib.pm10;
            }
        case TAB_TH:
            return (idx == 0) ? &g_calib.th_temp : &g_calib.th_hum;
        case TAB_EC:
            return &g_calib.ec;
        default:
            return (idx == 0) ? &g_calib.orp : &g_calib.orp_temp;
    }
}

static row_t *row_gs(int group, int idx)
{
    switch (group) {
        case TAB_SN300: return &g_rows_sn[idx];
        case TAB_TH:    return &g_rows_th[idx];
        case TAB_EC:    return &g_rows_ec[idx];
        default:        return &g_rows_orp[idx];
    }
}

static float raw_gs(int group, int idx)
{
    switch (group) {
        case TAB_SN300: return g_raw_sn[idx];
        case TAB_TH:    return g_raw_th[idx];
        case TAB_EC:    return g_raw_ec[idx];
        default:        return g_raw_orp[idx];
    }
}

static float off_step_gs(int group, int idx)
{
    switch (group) {
        case TAB_SN300: return SN_OFF_STEP[idx];
        case TAB_TH:    return TH_OFF_STEP[idx];
        case TAB_EC:    return EC_OFF_STEP[idx];
        default:        return ORP_OFF_STEP[idx];
    }
}

// ── Refresh one row ───────────────────────────────────────────────────────────

static void refresh_row(int group, int idx)
{
    char buf[16];
    sensor_calib_t *c = calib_gs(group, idx);
    row_t *r          = row_gs(group, idx);

    snprintf(buf, sizeof(buf), "%+.2f", c->offset);
    if (r->lbl_off_val) lv_label_set_text(r->lbl_off_val, buf);

    snprintf(buf, sizeof(buf), "%.2f", c->gain);
    if (r->lbl_gain_val) lv_label_set_text(r->lbl_gain_val, buf);

    snprintf(buf, sizeof(buf), "%.1f", calib_apply(raw_gs(group, idx), c));
    if (r->lbl_cal) lv_label_set_text(r->lbl_cal, buf);
}

// ── Button callbacks ──────────────────────────────────────────────────────────
// code = (group << 5) | (sensor << 2) | (is_gain << 1) | is_plus

static void adj_cb(lv_event_t *e)
{
    int code    = (int)(intptr_t)lv_event_get_user_data(e);
    int group   = code >> 5;
    int sensor  = (code >> 2) & 0x7;
    int is_gain = (code >> 1) & 0x1;
    int is_plus = code & 0x1;
    float sign  = is_plus ? 1.0f : -1.0f;

    sensor_calib_t *c = calib_gs(group, sensor);
    if (is_gain)
        c->gain   = fmaxf(0.1f,   fminf(5.0f,   c->gain   + sign * 0.01f));
    else
        c->offset = fmaxf(-200.0f, fminf(200.0f, c->offset + sign * off_step_gs(group, sensor)));

    refresh_row(group, sensor);

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

// ── Tab switching ─────────────────────────────────────────────────────────────

static void switch_tab(int tab)
{
    if (tab == g_active_tab) return;

    lv_obj_add_flag(g_tab_cont[g_active_tab], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(g_tab_btn[g_active_tab], lv_color_hex(0x0E0E1A), 0);
    lv_obj_t *old_lbl = lv_obj_get_child(g_tab_btn[g_active_tab], 0);
    if (old_lbl) lv_obj_set_style_text_color(old_lbl, lv_color_hex(0x445566), 0);

    g_active_tab = tab;
    lv_obj_clear_flag(g_tab_cont[tab], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(g_tab_btn[tab], lv_color_hex(0x0A2A4A), 0);
    lv_obj_t *new_lbl = lv_obj_get_child(g_tab_btn[tab], 0);
    if (new_lbl) lv_obj_set_style_text_color(new_lbl, lv_color_hex(0x00E5FF), 0);
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

static void make_sensor_card(lv_obj_t *parent, int group, int idx,
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

    row_t *r = row_gs(group, idx);

    make_lbl(card, name, 0x5577AA, &lv_font_montserrat_14, LV_ALIGN_TOP_LEFT, 0, 4);
    r->lbl_raw = make_lbl(card, "--", 0x778899, &lv_font_montserrat_24,
                          LV_ALIGN_TOP_LEFT, 160, 0);
    make_lbl(card, ">", 0x334455, &lv_font_montserrat_14, LV_ALIGN_TOP_MID, 0, 6);
    r->lbl_cal = make_lbl(card, "--", 0x00E5FF, &lv_font_montserrat_24,
                          LV_ALIGN_TOP_RIGHT, -38, 0);
    make_lbl(card, unit, 0x445566, &lv_font_montserrat_14, LV_ALIGN_TOP_RIGHT, 0, 6);

    int base = (group << 5) | (idx << 2);

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

    refresh_row(group, idx);
}

static lv_obj_t *make_tab_cont(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 720, 604);
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, 0);
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
    return cont;
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

    // ── Header 64px ──────────────────────────────────────────────────────────
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
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0088FF), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);

    make_lbl(hdr, "DEV MODE", 0x5577AA, &lv_font_montserrat_32,
             LV_ALIGN_LEFT_MID, 0, 0);

    lbl_status = make_lbl(hdr, "", 0x445566, &lv_font_montserrat_14,
                           LV_ALIGN_RIGHT_MID, 0, 0);

    // ── Tab bar 52px ──────────────────────────────────────────────────────────
    static const char *tab_names[N_TABS] = {"SN-300", "TH", "EC", "ORP"};

    lv_obj_t *tabbar = lv_obj_create(scr_dev);
    lv_obj_set_size(tabbar, 720, 52);
    lv_obj_align(tabbar, LV_ALIGN_TOP_MID, 0, 64);
    lv_obj_set_style_bg_color(tabbar, lv_color_hex(0x0A0A14), 0);
    lv_obj_set_style_border_width(tabbar, 0, 0);
    lv_obj_set_style_radius(tabbar, 0, 0);
    lv_obj_set_style_pad_all(tabbar, 0, 0);
    lv_obj_clear_flag(tabbar, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < N_TABS; i++) {
        lv_obj_t *btn = lv_btn_create(tabbar);
        lv_obj_set_size(btn, 180, 52);
        lv_obj_align(btn, LV_ALIGN_LEFT_MID, i * 180, 0);
        lv_obj_set_style_bg_color(btn, i == 0 ? lv_color_hex(0x0A2A4A)
                                               : lv_color_hex(0x0E0E1A), 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            switch_tab((int)(intptr_t)lv_event_get_user_data(e));
        }, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, tab_names[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl,
            i == 0 ? lv_color_hex(0x00E5FF) : lv_color_hex(0x445566), 0);
        lv_obj_center(lbl);

        g_tab_btn[i] = btn;
    }

    // ── Tab contents (604px each, stacked, hidden except first) ──────────────

    // TAB 0 — SN-300
    g_tab_cont[TAB_SN300] = make_tab_cont(scr_dev);
    for (int i = 0; i < 5; i++)
        make_sensor_card(g_tab_cont[TAB_SN300], TAB_SN300, i, SN_NAMES[i], SN_UNITS[i]);
    add_save_row(g_tab_cont[TAB_SN300]);

    // TAB 1 — TH
    g_tab_cont[TAB_TH] = make_tab_cont(scr_dev);
    for (int i = 0; i < 2; i++)
        make_sensor_card(g_tab_cont[TAB_TH], TAB_TH, i, TH_NAMES[i], TH_UNITS[i]);
    add_save_row(g_tab_cont[TAB_TH]);
    lv_obj_add_flag(g_tab_cont[TAB_TH], LV_OBJ_FLAG_HIDDEN);

    // TAB 2 — EC
    g_tab_cont[TAB_EC] = make_tab_cont(scr_dev);
    make_sensor_card(g_tab_cont[TAB_EC], TAB_EC, 0, EC_NAMES[0], EC_UNITS[0]);
    add_save_row(g_tab_cont[TAB_EC]);
    lv_obj_add_flag(g_tab_cont[TAB_EC], LV_OBJ_FLAG_HIDDEN);

    // TAB 3 — ORP
    g_tab_cont[TAB_ORP] = make_tab_cont(scr_dev);
    for (int i = 0; i < 2; i++)
        make_sensor_card(g_tab_cont[TAB_ORP], TAB_ORP, i, ORP_NAMES[i], ORP_UNITS[i]);
    add_save_row(g_tab_cont[TAB_ORP]);
    lv_obj_add_flag(g_tab_cont[TAB_ORP], LV_OBJ_FLAG_HIDDEN);
}

// ── Update functions (called from sensor task inside bsp_display_lock) ────────

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
        snprintf(buf, sizeof(buf), "%.1f", calib_apply(raws[i], calib_gs(TAB_SN300, i)));
        if (g_rows_sn[i].lbl_cal) lv_label_set_text(g_rows_sn[i].lbl_cal, buf);
    }
}

void ui_dev_update_th(float t_raw, float h_raw)
{
    if (!scr_dev) return;
    float raws[2] = {t_raw, h_raw};
    char buf[16];
    for (int i = 0; i < 2; i++) {
        g_raw_th[i] = raws[i];
        snprintf(buf, sizeof(buf), "%.1f", raws[i]);
        if (g_rows_th[i].lbl_raw) lv_label_set_text(g_rows_th[i].lbl_raw, buf);
        snprintf(buf, sizeof(buf), "%.1f", calib_apply(raws[i], calib_gs(TAB_TH, i)));
        if (g_rows_th[i].lbl_cal) lv_label_set_text(g_rows_th[i].lbl_cal, buf);
    }
}

void ui_dev_update_ec(float ec_raw)
{
    if (!scr_dev) return;
    g_raw_ec[0] = ec_raw;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", ec_raw);
    if (g_rows_ec[0].lbl_raw) lv_label_set_text(g_rows_ec[0].lbl_raw, buf);
    snprintf(buf, sizeof(buf), "%.1f", calib_apply(ec_raw, &g_calib.ec));
    if (g_rows_ec[0].lbl_cal) lv_label_set_text(g_rows_ec[0].lbl_cal, buf);
}

void ui_dev_update_orp(float orp_raw, float temp_raw)
{
    if (!scr_dev) return;
    float raws[2] = {orp_raw, temp_raw};
    char buf[16];
    for (int i = 0; i < 2; i++) {
        g_raw_orp[i] = raws[i];
        snprintf(buf, sizeof(buf), "%.1f", raws[i]);
        if (g_rows_orp[i].lbl_raw) lv_label_set_text(g_rows_orp[i].lbl_raw, buf);
        snprintf(buf, sizeof(buf), "%.1f", calib_apply(raws[i], calib_gs(TAB_ORP, i)));
        if (g_rows_orp[i].lbl_cal) lv_label_set_text(g_rows_orp[i].lbl_cal, buf);
    }
}
