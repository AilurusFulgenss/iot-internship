#include "ui_dev.h"
#include "wifi_mqtt.h"
#include "calib.h"
#include "esp_log.h"
#include "nvs.h"
#include "esp_system.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

lv_obj_t *scr_dev = NULL;

// ── Tab IDs ───────────────────────────────────────────────────────────────────
#define TAB_NET   0
#define TAB_SN300 1
#define N_TABS    2

static lv_obj_t *g_tab_cont[N_TABS];
static lv_obj_t *g_tab_btn[N_TABS];
static int        g_active_tab = TAB_NET;
static lv_obj_t  *lbl_status   = NULL;

// ── SN-300 metadata ───────────────────────────────────────────────────────────
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

// ── Network state ─────────────────────────────────────────────────────────────
static char    g_net_mode[8]  = "dhcp";
static uint8_t g_net_ip[4]   = {192, 168, 1, 100};
static uint8_t g_net_mask[4] = {255, 255, 255, 0};

static lv_obj_t *g_btn_dhcp       = NULL;
static lv_obj_t *g_btn_static_ip  = NULL;
static lv_obj_t *g_dhcp_section   = NULL;
static lv_obj_t *g_static_section = NULL;
static lv_obj_t *g_octet_ip[4];
static lv_obj_t *g_octet_mask[4];

// ── Numpad modal (octet entry) ────────────────────────────────────────────────
static lv_obj_t *g_num_overlay = NULL;
static int       g_num_target  = -1;
static char      g_num_buf[4]  = "";
static lv_obj_t *g_num_disp    = NULL;

// ── Confirmation modal ────────────────────────────────────────────────────────
static lv_obj_t *g_confirm_overlay = NULL;

// ── Network status labels (updated via ui_dev_update_network) ─────────────────
static lv_obj_t *g_lbl_status_ip   = NULL;
static lv_obj_t *g_lbl_status_mode = NULL;

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

// ── Octet numpad modal ────────────────────────────────────────────────────────

static void update_octet_label(int target, uint8_t value)
{
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", value);
    lv_obj_t *btn = (target < 4) ? g_octet_ip[target] : g_octet_mask[target - 4];
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (lbl) lv_label_set_text(lbl, buf);
}

static void num_close(void)
{
    if (g_num_overlay) { lv_obj_delete(g_num_overlay); g_num_overlay = NULL; }
    g_num_target = -1;
    g_num_buf[0] = 0;
    g_num_disp   = NULL;
}

static void num_key_cb(lv_event_t *e)
{
    int key = (int)(intptr_t)lv_event_get_user_data(e);

    if (key == -3) { num_close(); return; }

    if (key == -1) {
        int len = (int)strlen(g_num_buf);
        if (len > 0) g_num_buf[len - 1] = 0;
        if (g_num_disp) lv_label_set_text(g_num_disp, g_num_buf[0] ? g_num_buf : "0");
        return;
    }
    if (key == -2) {
        int val = atoi(g_num_buf);
        if (val < 0)   val = 0;
        if (val > 255) val = 255;
        if (g_num_target >= 0 && g_num_target < 4)
            g_net_ip[g_num_target] = (uint8_t)val;
        else if (g_num_target >= 4 && g_num_target < 8)
            g_net_mask[g_num_target - 4] = (uint8_t)val;
        update_octet_label(g_num_target, (uint8_t)val);
        num_close();
        return;
    }
    if ((int)strlen(g_num_buf) < 3) {
        int len = (int)strlen(g_num_buf);
        g_num_buf[len]     = (char)('0' + key);
        g_num_buf[len + 1] = 0;
    }
    if (g_num_disp) lv_label_set_text(g_num_disp, g_num_buf);
}

static void num_open(int target, uint8_t current_val)
{
    if (g_num_overlay) return;
    g_num_target = target;
    snprintf(g_num_buf, sizeof(g_num_buf), "%d", current_val);

    lv_display_t *disp = lv_display_get_default();
    int32_t sw = lv_display_get_horizontal_resolution(disp);
    int32_t sh = lv_display_get_vertical_resolution(disp);
    const int CW = 500, CH = 460;  // same as PIN dialog

    lv_obj_clear_flag(lv_layer_top(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(lv_layer_top(), LV_DIR_NONE);

    g_num_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_num_overlay, sw, sh);
    lv_obj_set_pos(g_num_overlay, 0, 0);
    lv_obj_set_style_bg_color(g_num_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_num_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(g_num_overlay, 0, 0);
    lv_obj_set_style_radius(g_num_overlay, 0, 0);
    lv_obj_set_style_pad_all(g_num_overlay, 0, 0);
    lv_obj_clear_flag(g_num_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(g_num_overlay, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(g_num_overlay, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *card = lv_obj_create(g_num_overlay);
    lv_obj_set_size(card, CW, CH);
    lv_obj_set_pos(card, (sw - CW) / 2, (sh - CH) / 2);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0D0D1A), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "Enter value (0-255)");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *disp_box = lv_obj_create(card);
    lv_obj_set_size(disp_box, 420, 52);
    lv_obj_set_pos(disp_box, (CW - 420) / 2, 60);
    lv_obj_set_style_bg_color(disp_box, lv_color_hex(0x060610), 0);
    lv_obj_set_style_bg_opa(disp_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(disp_box, lv_color_hex(0x2A3A5A), 0);
    lv_obj_set_style_border_width(disp_box, 1, 0);
    lv_obj_set_style_radius(disp_box, 6, 0);
    lv_obj_set_style_pad_all(disp_box, 0, 0);
    lv_obj_clear_flag(disp_box, LV_OBJ_FLAG_SCROLLABLE);

    g_num_disp = lv_label_create(disp_box);
    lv_label_set_text(g_num_disp, g_num_buf);
    lv_obj_set_style_text_color(g_num_disp, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_num_disp, &lv_font_montserrat_32, 0);
    lv_obj_align(g_num_disp, LV_ALIGN_RIGHT_MID, -12, 0);

    // 3×4 numpad — same sizing as PIN dialog
    const int BW = 140, BH = 58, BGAP = 8;
    const int numpad_w = 3 * BW + 2 * BGAP;
    const int nx0 = (CW - numpad_w) / 2;
    const int ny0 = 128;

    static const char *labels[12] = {"1","2","3","4","5","6","7","8","9","DEL","0","OK"};
    static const int   keys[12]   = {  1,  2,  3,  4,  5,  6,  7,  8,  9,  -1,  0,  -2};

    for (int i = 0; i < 12; i++) {
        int row = i / 3, col = i % 3;
        lv_obj_t *btn = lv_btn_create(card);
        lv_obj_set_size(btn, BW, BH);
        lv_obj_set_pos(btn, nx0 + col * (BW + BGAP), ny0 + row * (BH + BGAP));
        lv_obj_set_style_bg_color(btn, lv_color_hex(keys[i] == -2 ? 0x005A5F : 0x13131F), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x2A2A4A), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(keys[i] == -2 ? 0x00E5FF : 0xCCCCCC), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, num_key_cb, LV_EVENT_CLICKED, (void *)(intptr_t)keys[i]);
    }

    // Cancel — positioned below numpad with fixed gap
    // last numpad row bottom: ny0 + 4*(BH+BGAP) - BGAP = 128 + 4*66 - 8 = 384
    lv_obj_t *cancel = lv_btn_create(card);
    lv_obj_set_size(cancel, 180, 38);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(0x1A0808), 0);
    lv_obj_set_style_bg_opa(cancel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(cancel, lv_color_hex(0x552222), 0);
    lv_obj_set_style_border_width(cancel, 1, 0);
    lv_obj_set_style_radius(cancel, 8, 0);
    lv_obj_set_style_shadow_width(cancel, 0, 0);
    lv_obj_t *cl = lv_label_create(cancel);
    lv_label_set_text(cl, "Cancel");
    lv_obj_set_style_text_color(cl, lv_color_hex(0x885555), 0);
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_24, 0);
    lv_obj_center(cl);
    lv_obj_add_event_cb(cancel, num_key_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-3);
}

// ── Confirmation dialog ───────────────────────────────────────────────────────

static void confirm_close(void)
{
    if (g_confirm_overlay) { lv_obj_delete(g_confirm_overlay); g_confirm_overlay = NULL; }
}

static void confirm_yes_cb(lv_event_t *e)
{
    nvs_handle_t h;
    if (nvs_open("eth_cfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "mode", g_net_mode);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
                 g_net_ip[0], g_net_ip[1], g_net_ip[2], g_net_ip[3]);
        nvs_set_str(h, "ip", buf);
        snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
                 g_net_mask[0], g_net_mask[1], g_net_mask[2], g_net_mask[3]);
        nvs_set_str(h, "mask", buf);
        nvs_commit(h);
        nvs_close(h);
    }
    confirm_close();
    esp_restart();
}

static void confirm_cancel_cb(lv_event_t *e) { confirm_close(); }

static void net_apply_cb(lv_event_t *e)
{
    if (g_confirm_overlay) return;

    lv_display_t *disp = lv_display_get_default();
    int32_t sw = lv_display_get_horizontal_resolution(disp);
    int32_t sh = lv_display_get_vertical_resolution(disp);
    const int CW = 460, CH = 200;

    lv_obj_clear_flag(lv_layer_top(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(lv_layer_top(), LV_DIR_NONE);

    g_confirm_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_confirm_overlay, sw, sh);
    lv_obj_set_pos(g_confirm_overlay, 0, 0);
    lv_obj_set_style_bg_color(g_confirm_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_confirm_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(g_confirm_overlay, 0, 0);
    lv_obj_set_style_radius(g_confirm_overlay, 0, 0);
    lv_obj_set_style_pad_all(g_confirm_overlay, 0, 0);
    lv_obj_clear_flag(g_confirm_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(g_confirm_overlay, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(g_confirm_overlay, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *card = lv_obj_create(g_confirm_overlay);
    lv_obj_set_size(card, CW, CH);
    lv_obj_set_pos(card, (sw - CW) / 2, (sh - CH) / 2);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0D0D1A), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xFF8800), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "Apply Network Config?");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFF8800), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *sub = lv_label_create(card);
    lv_label_set_text(sub, "Device will reboot.");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x778899), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 54);

    lv_obj_t *btn_yes = lv_btn_create(card);
    lv_obj_set_size(btn_yes, 180, 52);
    lv_obj_set_pos(btn_yes, 20, CH - 70);
    lv_obj_set_style_bg_color(btn_yes, lv_color_hex(0x003388), 0);
    lv_obj_set_style_radius(btn_yes, 10, 0);
    lv_obj_set_style_shadow_width(btn_yes, 0, 0);
    lv_obj_add_event_cb(btn_yes, confirm_yes_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_yes = lv_label_create(btn_yes);
    lv_label_set_text(lbl_yes, "Confirm");
    lv_obj_set_style_text_font(lbl_yes, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_yes, lv_color_hex(0xCCDDFF), 0);
    lv_obj_center(lbl_yes);

    lv_obj_t *btn_no = lv_btn_create(card);
    lv_obj_set_size(btn_no, 180, 52);
    lv_obj_set_pos(btn_no, CW - 200, CH - 70);
    lv_obj_set_style_bg_color(btn_no, lv_color_hex(0x1A0808), 0);
    lv_obj_set_style_border_color(btn_no, lv_color_hex(0x552222), 0);
    lv_obj_set_style_border_width(btn_no, 1, 0);
    lv_obj_set_style_radius(btn_no, 10, 0);
    lv_obj_set_style_shadow_width(btn_no, 0, 0);
    lv_obj_add_event_cb(btn_no, confirm_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_no = lv_label_create(btn_no);
    lv_label_set_text(lbl_no, "Cancel");
    lv_obj_set_style_text_font(lbl_no, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_no, lv_color_hex(0x885555), 0);
    lv_obj_center(lbl_no);
}

// ── Network tab ────────────────────────────────────────────────────────────────

static void update_net_toggle(void)
{
    bool is_static = (strcmp(g_net_mode, "static") == 0);
    lv_obj_set_style_bg_color(g_btn_dhcp,
        lv_color_hex(is_static ? 0x0E0E1A : 0x0A2A4A), 0);
    lv_obj_set_style_bg_color(g_btn_static_ip,
        lv_color_hex(is_static ? 0x0A2A4A : 0x0E0E1A), 0);
    lv_obj_t *dl = lv_obj_get_child(g_btn_dhcp, 0);
    lv_obj_t *sl = lv_obj_get_child(g_btn_static_ip, 0);
    if (dl) lv_obj_set_style_text_color(dl, lv_color_hex(is_static ? 0x445566 : 0x00E5FF), 0);
    if (sl) lv_obj_set_style_text_color(sl, lv_color_hex(is_static ? 0x00E5FF : 0x445566), 0);

    if (is_static) {
        lv_obj_clear_flag(g_static_section, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_dhcp_section, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_static_section, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_dhcp_section, LV_OBJ_FLAG_HIDDEN);
    }
}

static void mode_dhcp_cb(lv_event_t *e)   { strcpy(g_net_mode, "dhcp");   update_net_toggle(); }
static void mode_static_cb(lv_event_t *e) { strcpy(g_net_mode, "static"); update_net_toggle(); }

static void octet_cb(lv_event_t *e)
{
    int target = (int)(intptr_t)lv_event_get_user_data(e);
    uint8_t cur = (target < 4) ? g_net_ip[target] : g_net_mask[target - 4];
    num_open(target, cur);
}

static void make_net_tab(lv_obj_t *parent)
{
    // ── Current Status card ───────────────────────────────────────────────────
    lv_obj_t *sc = lv_obj_create(parent);
    lv_obj_set_size(sc, 688, 114);
    lv_obj_set_style_bg_color(sc, lv_color_hex(0x0B1220), 0);
    lv_obj_set_style_bg_opa(sc, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sc, 12, 0);
    lv_obj_set_style_border_color(sc, lv_color_hex(0x1E3A58), 0);
    lv_obj_set_style_border_width(sc, 1, 0);
    lv_obj_set_style_pad_all(sc, 14, 0);
    lv_obj_clear_flag(sc, LV_OBJ_FLAG_SCROLLABLE);

    make_lbl(sc, "CURRENT STATUS", 0x2A4A6A, &lv_font_montserrat_14,
             LV_ALIGN_TOP_LEFT, 0, 0);

    char id_buf[36];
    snprintf(id_buf, sizeof(id_buf), "ID: %s", wifi_mqtt_get_device_id());
    make_lbl(sc, id_buf, 0x4A6A90, &lv_font_montserrat_24,
             LV_ALIGN_TOP_LEFT, 0, 18);

    char mode_buf[24];
    snprintf(mode_buf, sizeof(mode_buf), "Mode: %s",
             strcmp(g_net_mode, "static") == 0 ? "STATIC" : "DHCP");
    g_lbl_status_mode = make_lbl(sc, mode_buf, 0x5577AA, &lv_font_montserrat_24,
                                 LV_ALIGN_TOP_MID, 0, 18);

    g_lbl_status_ip = make_lbl(sc, "IP: --", 0x00CC88, &lv_font_montserrat_24,
                               LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // ── Separator ─────────────────────────────────────────────────────────────
    lv_obj_t *sep = lv_obj_create(parent);
    lv_obj_set_size(sep, 688, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x1A2A3A), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);

    // ── DHCP / STATIC toggle (fill full 688px width) ──────────────────────────
    lv_obj_t *toggle_row = lv_obj_create(parent);
    lv_obj_set_size(toggle_row, 688, 68);
    lv_obj_set_style_bg_opa(toggle_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(toggle_row, 0, 0);
    lv_obj_set_style_pad_all(toggle_row, 0, 0);
    lv_obj_clear_flag(toggle_row, LV_OBJ_FLAG_SCROLLABLE);

    g_btn_dhcp = lv_btn_create(toggle_row);
    lv_obj_set_size(g_btn_dhcp, 330, 58);
    lv_obj_align(g_btn_dhcp, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(g_btn_dhcp, lv_color_hex(0x0A2A4A), 0);
    lv_obj_set_style_border_color(g_btn_dhcp, lv_color_hex(0x2A4A6A), 0);
    lv_obj_set_style_border_width(g_btn_dhcp, 1, 0);
    lv_obj_set_style_radius(g_btn_dhcp, 8, 0);
    lv_obj_set_style_shadow_width(g_btn_dhcp, 0, 0);
    lv_obj_add_event_cb(g_btn_dhcp, mode_dhcp_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dl = lv_label_create(g_btn_dhcp);
    lv_label_set_text(dl, "DHCP");
    lv_obj_set_style_text_font(dl, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(dl, lv_color_hex(0x00E5FF), 0);
    lv_obj_center(dl);

    g_btn_static_ip = lv_btn_create(toggle_row);
    lv_obj_set_size(g_btn_static_ip, 330, 58);
    lv_obj_align(g_btn_static_ip, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(g_btn_static_ip, lv_color_hex(0x0E0E1A), 0);
    lv_obj_set_style_border_color(g_btn_static_ip, lv_color_hex(0x2A4A6A), 0);
    lv_obj_set_style_border_width(g_btn_static_ip, 1, 0);
    lv_obj_set_style_radius(g_btn_static_ip, 8, 0);
    lv_obj_set_style_shadow_width(g_btn_static_ip, 0, 0);
    lv_obj_add_event_cb(g_btn_static_ip, mode_static_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(g_btn_static_ip);
    lv_label_set_text(sl, "STATIC");
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(sl, lv_color_hex(0x445566), 0);
    lv_obj_center(sl);

    // ── DHCP section ──────────────────────────────────────────────────────────
    g_dhcp_section = lv_obj_create(parent);
    lv_obj_set_size(g_dhcp_section, 688, 80);
    lv_obj_set_style_bg_opa(g_dhcp_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_dhcp_section, 0, 0);
    lv_obj_set_style_pad_all(g_dhcp_section, 0, 0);
    lv_obj_clear_flag(g_dhcp_section, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *dhcp_lbl = lv_label_create(g_dhcp_section);
    lv_label_set_text(dhcp_lbl, "Mode: DHCP\nIP assigned automatically");
    lv_obj_set_style_text_color(dhcp_lbl, lv_color_hex(0x5577AA), 0);
    lv_obj_set_style_text_font(dhcp_lbl, &lv_font_montserrat_24, 0);
    lv_obj_align(dhcp_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    // ── STATIC section ────────────────────────────────────────────────────────
    g_static_section = lv_obj_create(parent);
    lv_obj_set_size(g_static_section, 688, 180);
    lv_obj_set_style_bg_opa(g_static_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_static_section, 0, 0);
    lv_obj_set_style_pad_all(g_static_section, 0, 0);
    lv_obj_clear_flag(g_static_section, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_static_section, LV_OBJ_FLAG_HIDDEN);

    // octet buttons: 88px wide, step 102px (88+14 for dot gap)
    const int OCT_X0 = 180, OCT_W = 88, OCT_H = 60, OCT_STEP = 102;

    // IP row
    lv_obj_t *ip_lbl = lv_label_create(g_static_section);
    lv_label_set_text(ip_lbl, "IP Address");
    lv_obj_set_style_text_color(ip_lbl, lv_color_hex(0x5577AA), 0);
    lv_obj_set_style_text_font(ip_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(ip_lbl, 0, 14);

    for (int i = 0; i < 4; i++) {
        g_octet_ip[i] = lv_btn_create(g_static_section);
        lv_obj_set_size(g_octet_ip[i], OCT_W, OCT_H);
        lv_obj_set_pos(g_octet_ip[i], OCT_X0 + i * OCT_STEP, 0);
        lv_obj_set_style_bg_color(g_octet_ip[i], lv_color_hex(0x0E1A2E), 0);
        lv_obj_set_style_bg_opa(g_octet_ip[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(g_octet_ip[i], lv_color_hex(0x2A4A6A), 0);
        lv_obj_set_style_border_width(g_octet_ip[i], 1, 0);
        lv_obj_set_style_radius(g_octet_ip[i], 8, 0);
        lv_obj_set_style_shadow_width(g_octet_ip[i], 0, 0);
        lv_obj_add_event_cb(g_octet_ip[i], octet_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        char buf[4]; snprintf(buf, sizeof(buf), "%d", g_net_ip[i]);
        lv_obj_t *lbl = lv_label_create(g_octet_ip[i]);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0);
        lv_obj_center(lbl);
        if (i < 3) {
            lv_obj_t *dot = lv_label_create(g_static_section);
            lv_label_set_text(dot, ".");
            lv_obj_set_style_text_color(dot, lv_color_hex(0x778899), 0);
            lv_obj_set_style_text_font(dot, &lv_font_montserrat_32, 0);
            lv_obj_set_pos(dot, OCT_X0 + i * OCT_STEP + OCT_W + 2, 12);
        }
    }

    // Mask row
    lv_obj_t *mask_lbl = lv_label_create(g_static_section);
    lv_label_set_text(mask_lbl, "Subnet Mask");
    lv_obj_set_style_text_color(mask_lbl, lv_color_hex(0x5577AA), 0);
    lv_obj_set_style_text_font(mask_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(mask_lbl, 0, 108);

    for (int i = 0; i < 4; i++) {
        g_octet_mask[i] = lv_btn_create(g_static_section);
        lv_obj_set_size(g_octet_mask[i], OCT_W, OCT_H);
        lv_obj_set_pos(g_octet_mask[i], OCT_X0 + i * OCT_STEP, 90);
        lv_obj_set_style_bg_color(g_octet_mask[i], lv_color_hex(0x0E1A2E), 0);
        lv_obj_set_style_bg_opa(g_octet_mask[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(g_octet_mask[i], lv_color_hex(0x2A4A6A), 0);
        lv_obj_set_style_border_width(g_octet_mask[i], 1, 0);
        lv_obj_set_style_radius(g_octet_mask[i], 8, 0);
        lv_obj_set_style_shadow_width(g_octet_mask[i], 0, 0);
        lv_obj_add_event_cb(g_octet_mask[i], octet_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(4 + i));
        char buf[4]; snprintf(buf, sizeof(buf), "%d", g_net_mask[i]);
        lv_obj_t *lbl = lv_label_create(g_octet_mask[i]);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0);
        lv_obj_center(lbl);
        if (i < 3) {
            lv_obj_t *dot = lv_label_create(g_static_section);
            lv_label_set_text(dot, ".");
            lv_obj_set_style_text_color(dot, lv_color_hex(0x778899), 0);
            lv_obj_set_style_text_font(dot, &lv_font_montserrat_32, 0);
            lv_obj_set_pos(dot, OCT_X0 + i * OCT_STEP + OCT_W + 2, 102);
        }
    }

    // ── Apply and Reboot ──────────────────────────────────────────────────────
    lv_obj_t *apply_row = lv_obj_create(parent);
    lv_obj_set_size(apply_row, 688, 80);
    lv_obj_set_style_bg_opa(apply_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(apply_row, 0, 0);
    lv_obj_set_style_pad_all(apply_row, 0, 0);
    lv_obj_clear_flag(apply_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_apply = lv_btn_create(apply_row);
    lv_obj_set_size(btn_apply, 420, 60);
    lv_obj_align(btn_apply, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_apply, lv_color_hex(0x003A22), 0);
    lv_obj_set_style_bg_color(btn_apply, lv_color_hex(0x004A2A), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_apply, lv_color_hex(0x00AA66), 0);
    lv_obj_set_style_border_width(btn_apply, 1, 0);
    lv_obj_set_style_radius(btn_apply, 10, 0);
    lv_obj_set_style_shadow_width(btn_apply, 0, 0);
    lv_obj_add_event_cb(btn_apply, net_apply_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *apply_lbl = lv_label_create(btn_apply);
    lv_label_set_text(apply_lbl, "Apply and Reboot");
    lv_obj_set_style_text_font(apply_lbl, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(apply_lbl, lv_color_hex(0x00CC88), 0);
    lv_obj_center(apply_lbl);
}

// ── NVS load ──────────────────────────────────────────────────────────────────
static void net_nvs_load(void)
{
    nvs_handle_t h;
    if (nvs_open("eth_cfg", NVS_READONLY, &h) != ESP_OK) return;
    char tmp[16];
    size_t len;
    len = sizeof(g_net_mode);
    nvs_get_str(h, "mode", g_net_mode, &len);
    memset(tmp, 0, sizeof(tmp)); len = sizeof(tmp);
    if (nvs_get_str(h, "ip", tmp, &len) == ESP_OK && tmp[0])
        sscanf(tmp, "%hhu.%hhu.%hhu.%hhu",
               &g_net_ip[0], &g_net_ip[1], &g_net_ip[2], &g_net_ip[3]);
    memset(tmp, 0, sizeof(tmp)); len = sizeof(tmp);
    if (nvs_get_str(h, "mask", tmp, &len) == ESP_OK && tmp[0])
        sscanf(tmp, "%hhu.%hhu.%hhu.%hhu",
               &g_net_mask[0], &g_net_mask[1], &g_net_mask[2], &g_net_mask[3]);
    nvs_close(h);
}

// ── ui_dev_create ─────────────────────────────────────────────────────────────
void ui_dev_create(void)
{
    net_nvs_load();

    scr_dev = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_dev, lv_color_hex(0x080812), 0);
    lv_obj_set_style_bg_opa(scr_dev, LV_OPA_COVER, 0);

    // ── Header ───────────────────────────────────────────────────────────────
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

    make_lbl(hdr, "DEV MODE", 0x5577AA, &lv_font_montserrat_32, LV_ALIGN_LEFT_MID, 0, 0);
    lbl_status = make_lbl(hdr, "", 0x445566, &lv_font_montserrat_14, LV_ALIGN_RIGHT_MID, 0, 0);

    // ── Tab bar (2 tabs, 360px each) ─────────────────────────────────────────
    static const char *tab_names[N_TABS] = {"NETWORK", "SN-300"};

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
        lv_obj_set_size(btn, 360, 52);
        lv_obj_align(btn, LV_ALIGN_LEFT_MID, i * 360, 0);
        lv_obj_set_style_bg_color(btn,
            i == TAB_NET ? lv_color_hex(0x0A2A4A) : lv_color_hex(0x0E0E1A), 0);
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
            i == TAB_NET ? lv_color_hex(0x00E5FF) : lv_color_hex(0x445566), 0);
        lv_obj_center(lbl);
        g_tab_btn[i] = btn;
    }

    // ── Tab 0: NETWORK ────────────────────────────────────────────────────────
    g_tab_cont[TAB_NET] = make_tab_cont(scr_dev);
    make_net_tab(g_tab_cont[TAB_NET]);
    update_net_toggle();

    // ── Tab 1: SN-300 ─────────────────────────────────────────────────────────
    g_tab_cont[TAB_SN300] = make_tab_cont(scr_dev);
    for (int i = 0; i < 5; i++)
        make_sensor_card(g_tab_cont[TAB_SN300], i, SN_NAMES[i], SN_UNITS[i]);
    add_save_row(g_tab_cont[TAB_SN300]);
    lv_obj_add_flag(g_tab_cont[TAB_SN300], LV_OBJ_FLAG_HIDDEN);
}

// ── ui_dev_update_network (call from inside bsp_display_lock) ─────────────────
void ui_dev_update_network(const char *ip_str)
{
    if (!scr_dev || !ip_str) return;
    if (g_lbl_status_ip) {
        char buf[24];
        snprintf(buf, sizeof(buf), "IP: %s", ip_str);
        lv_label_set_text(g_lbl_status_ip, buf);
        lv_obj_set_style_text_color(g_lbl_status_ip, lv_color_hex(0x00E5FF), 0);
    }
}

// ── Update (called from sensor task inside bsp_display_lock) ─────────────────
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
