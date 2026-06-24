#include "ui_user.h"
#include "eth_upload.h"
#include "sensor_config.h"
#include <stdio.h>
#include <cmath>

#define CLR_GOOD       0x009966u
#define CLR_MODERATE   0xFFDE33u
#define CLR_SENSITIVE  0xFF9933u
#define CLR_UNHEALTHY  0xCC0033u
#define CLR_VERY_UH    0x660099u
#define CLR_HAZARDOUS  0x7E0023u
#define CLR_CARD       0x1A1A26u

lv_obj_t *scr_user = NULL;

static bool  g_color_on  = true;
static float g_pm25_last = 0.0f;
static float g_pm10_last = 0.0f;

// PM tab widgets
static lv_obj_t *lbl_temp       = NULL;
static lv_obj_t *lbl_hum        = NULL;
static lv_obj_t *lbl_pm25       = NULL;
static lv_obj_t *lbl_pm10       = NULL;
static lv_obj_t *lbl_sound      = NULL;
static lv_obj_t *card_pm25      = NULL;
static lv_obj_t *card_pm10      = NULL;
static lv_obj_t *lbl_pm25_st    = NULL;
static lv_obj_t *lbl_pm10_st    = NULL;
static lv_obj_t *lbl_pm25_title = NULL;
static lv_obj_t *lbl_pm10_title = NULL;

// Nav bar (6 tabs)
static lv_obj_t *nav_pm25_val = NULL;
static lv_obj_t *nav_hhcc_val = NULL;
static lv_obj_t *nav_ec_val   = NULL;
static lv_obj_t *nav_orp_val  = NULL;
static lv_obj_t *nav_leak_val = NULL;
static lv_obj_t *nav_th_val   = NULL;
static lv_obj_t *chip_pm      = NULL;
static lv_obj_t *chip_hhcc    = NULL;
static lv_obj_t *chip_ec      = NULL;
static lv_obj_t *chip_orp     = NULL;
static lv_obj_t *chip_leak    = NULL;
static lv_obj_t *chip_th      = NULL;
static lv_obj_t *ind_pm       = NULL;
static lv_obj_t *ind_hhcc     = NULL;
static lv_obj_t *ind_ec       = NULL;
static lv_obj_t *ind_orp      = NULL;
static lv_obj_t *ind_leak     = NULL;
static lv_obj_t *ind_th       = NULL;

// Content containers (show one at a time)
// view 0=PM, 1=HHCC, 2=EC/TDS, 3=ORP, 4=LEAK, 5=TH
static lv_obj_t *cont_pm   = NULL;
static lv_obj_t *cont_hhcc = NULL;
static lv_obj_t *cont_ec   = NULL;
static lv_obj_t *cont_orp  = NULL;
static lv_obj_t *cont_leak = NULL;
static lv_obj_t *cont_th   = NULL;

// EC tab widgets
static lv_obj_t *lbl_ec_big  = NULL;
static lv_obj_t *lbl_tds_big = NULL;

// ORP tab widgets
static lv_obj_t *lbl_orp_big      = NULL;
static lv_obj_t *lbl_orp_temp_big = NULL;

// HHCC tab widgets
static lv_obj_t *lbl_hhcc_temp    = NULL;
static lv_obj_t *lbl_hhcc_moist   = NULL;
static lv_obj_t *lbl_hhcc_light   = NULL;
static lv_obj_t *lbl_hhcc_fert    = NULL;
static lv_obj_t *lbl_hhcc_bat_pct = NULL;
static lv_obj_t *bat_bar_hhcc     = NULL;

// LEAK tab widgets
static lv_obj_t *card_leak_big = NULL;
static lv_obj_t *lbl_leak_big  = NULL;

// TH tab widgets (CWT-TH04S — separate from PM SN-300)
static lv_obj_t *lbl_th_temp = NULL;
static lv_obj_t *lbl_th_hum  = NULL;

// AQI toggle
static lv_obj_t *sw_aqi      = NULL;
static lv_obj_t *lbl_aqi     = NULL;
static lv_obj_t *bat_wrap_hdr = NULL;

// ── AQI helpers ───────────────────────────────────────────────────────────────

static uint32_t aqi_color_pm25(float v)
{
    if (v <= 12.0f)  return CLR_GOOD;
    if (v <= 35.4f)  return CLR_MODERATE;
    if (v <= 55.4f)  return CLR_SENSITIVE;
    if (v <= 150.4f) return CLR_UNHEALTHY;
    if (v <= 250.4f) return CLR_VERY_UH;
    return CLR_HAZARDOUS;
}

static uint32_t aqi_color_pm10(float v)
{
    if (v <= 54.0f)  return CLR_GOOD;
    if (v <= 154.0f) return CLR_MODERATE;
    if (v <= 254.0f) return CLR_SENSITIVE;
    if (v <= 354.0f) return CLR_UNHEALTHY;
    if (v <= 424.0f) return CLR_VERY_UH;
    return CLR_HAZARDOUS;
}

static const char *aqi_label_pm25(float v)
{
    if (v <= 12.0f)  return "GOOD";
    if (v <= 35.4f)  return "MODERATE";
    if (v <= 55.4f)  return "SENSITIVE";
    if (v <= 150.4f) return "UNHEALTHY";
    if (v <= 250.4f) return "VERY UNHEALTHY";
    return "HAZARDOUS";
}

static const char *aqi_label_pm10(float v)
{
    if (v <= 54.0f)  return "GOOD";
    if (v <= 154.0f) return "MODERATE";
    if (v <= 254.0f) return "SENSITIVE";
    if (v <= 354.0f) return "UNHEALTHY";
    if (v <= 424.0f) return "VERY UNHEALTHY";
    return "HAZARDOUS";
}

static uint32_t text_on_bg(uint32_t bg)
{
    return (bg == CLR_GOOD || bg == CLR_MODERATE) ? 0x111111u : 0xFFFFFFu;
}

static void apply_pm_colors(void)
{
    if (!card_pm25 || !card_pm10) return;
    if (g_color_on) {
        uint32_t c25 = aqi_color_pm25(g_pm25_last);
        uint32_t c10 = aqi_color_pm10(g_pm10_last);
        lv_obj_set_style_bg_color(card_pm25, lv_color_hex(c25), 0);
        lv_obj_set_style_bg_color(card_pm10, lv_color_hex(c10), 0);
        lv_obj_set_style_text_color(lbl_pm25, lv_color_hex(text_on_bg(c25)), 0);
        lv_obj_set_style_text_color(lbl_pm10, lv_color_hex(text_on_bg(c10)), 0);
        if (lbl_pm25_st)    lv_label_set_text(lbl_pm25_st, aqi_label_pm25(g_pm25_last));
        if (lbl_pm10_st)    lv_label_set_text(lbl_pm10_st, aqi_label_pm10(g_pm10_last));
        if (lbl_pm25_st)    lv_obj_set_style_text_color(lbl_pm25_st,    lv_color_hex(text_on_bg(c25)), 0);
        if (lbl_pm10_st)    lv_obj_set_style_text_color(lbl_pm10_st,    lv_color_hex(text_on_bg(c10)), 0);
        if (lbl_pm25_title) lv_obj_set_style_text_color(lbl_pm25_title, lv_color_hex(text_on_bg(c25)), 0);
        if (lbl_pm10_title) lv_obj_set_style_text_color(lbl_pm10_title, lv_color_hex(text_on_bg(c10)), 0);
    } else {
        lv_obj_set_style_bg_color(card_pm25, lv_color_hex(CLR_CARD), 0);
        lv_obj_set_style_bg_color(card_pm10, lv_color_hex(CLR_CARD), 0);
        lv_obj_set_style_text_color(lbl_pm25, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(lbl_pm10, lv_color_hex(0xFFFFFF), 0);
        if (lbl_pm25_st)    lv_label_set_text(lbl_pm25_st, "");
        if (lbl_pm10_st)    lv_label_set_text(lbl_pm10_st, "");
        if (lbl_pm25_title) lv_obj_set_style_text_color(lbl_pm25_title, lv_color_hex(0x7788AA), 0);
        if (lbl_pm10_title) lv_obj_set_style_text_color(lbl_pm10_title, lv_color_hex(0x7788AA), 0);
    }
}

static void color_toggle_cb(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    g_color_on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    apply_pm_colors();
}

// ── View switcher (6 views) ───────────────────────────────────────────────────

static void switch_view(int idx)
{
    lv_obj_t *conts[6] = { cont_pm, cont_hhcc, cont_ec, cont_orp, cont_leak, cont_th };
    for (int i = 0; i < 6; i++) {
        if (!conts[i]) continue;
        if (i == idx) lv_obj_clear_flag(conts[i], LV_OBJ_FLAG_HIDDEN);
        else          lv_obj_add_flag  (conts[i], LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *inds[6] = { ind_pm, ind_hhcc, ind_ec, ind_orp, ind_leak, ind_th };
    for (int i = 0; i < 6; i++) {
        if (!inds[i]) continue;
        if (i == idx) lv_obj_clear_flag(inds[i], LV_OBJ_FLAG_HIDDEN);
        else          lv_obj_add_flag  (inds[i], LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *vals[6] = { nav_pm25_val, nav_hhcc_val, nav_ec_val, nav_orp_val, nav_leak_val, nav_th_val };
    for (int i = 0; i < 6; i++) {
        if (!vals[i]) continue;
        lv_obj_set_style_text_color(vals[i],
            lv_color_hex(i == idx ? 0xE0EEFFu : 0x6677AAu), 0);
    }

    if (sw_aqi) {
        if (idx == 0) {
            lv_obj_clear_flag(sw_aqi,  LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(lbl_aqi, LV_OBJ_FLAG_HIDDEN);
            if (bat_wrap_hdr) lv_obj_add_flag(bat_wrap_hdr, LV_OBJ_FLAG_HIDDEN);
        } else if (idx == 1) {
            lv_obj_add_flag(sw_aqi,  LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_aqi, LV_OBJ_FLAG_HIDDEN);
            if (bat_wrap_hdr) lv_obj_clear_flag(bat_wrap_hdr, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(sw_aqi,  LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_aqi, LV_OBJ_FLAG_HIDDEN);
            if (bat_wrap_hdr) lv_obj_add_flag(bat_wrap_hdr, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void nav_pm_cb  (lv_event_t *) { switch_view(0); }
static void nav_hhcc_cb(lv_event_t *) { switch_view(1); }
static void nav_ec_cb  (lv_event_t *) { switch_view(2); }
static void nav_orp_cb (lv_event_t *) { switch_view(3); }
static void nav_leak_cb(lv_event_t *) { switch_view(4); }
static void nav_th_cb  (lv_event_t *) { switch_view(5); }

// ── Card builder ──────────────────────────────────────────────────────────────

typedef struct {
    lv_obj_t *card;
    lv_obj_t *lbl_val;
    lv_obj_t *lbl_status;
    lv_obj_t *lbl_title;
    lv_obj_t *lbl_unit;
} card_out_t;

static void make_card(lv_obj_t *parent, int w, int h,
                      const char *title, const char *unit,
                      bool has_status, card_out_t *out)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 18, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(0x2C2C3C), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_pad_all(c, 16, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_t = lv_label_create(c);
    lv_label_set_text(lbl_t, title);
    lv_obj_set_style_text_color(lbl_t, lv_color_hex(0x7788AA), 0);
    lv_obj_set_style_text_font(lbl_t, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_t, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_v = lv_label_create(c);
    lv_label_set_text(lbl_v, "--");
    lv_obj_set_style_text_color(lbl_v, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_v, &lv_font_montserrat_48, 0);
    lv_obj_align(lbl_v, LV_ALIGN_LEFT_MID, 0, has_status ? -10 : 0);

    lv_obj_t *lbl_u = lv_label_create(c);
    lv_label_set_text(lbl_u, unit);
    lv_obj_set_style_text_color(lbl_u, lv_color_hex(0x4D5F78), 0);
    lv_obj_set_style_text_font(lbl_u, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_u, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    out->lbl_status = NULL;
    if (has_status) {
        lv_obj_t *lbl_s = lv_label_create(c);
        lv_label_set_text(lbl_s, "");
        lv_obj_set_style_text_color(lbl_s, lv_color_hex(0xDDDDDD), 0);
        lv_obj_set_style_text_font(lbl_s, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_s, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        out->lbl_status = lbl_s;
    }

    out->card      = c;
    out->lbl_val   = lbl_v;
    out->lbl_title = lbl_t;
    out->lbl_unit  = lbl_u;
}

// ── Nav chip builder — chip_w = 144 for 5-tab layout ─────────────────────────

static lv_obj_t *make_nav_chip(lv_obj_t *nav, const char *title,
                                const char *unit, int x, int chip_w,
                                lv_obj_t **out_chip, lv_obj_t **out_ind)
{
    lv_obj_t *chip = lv_obj_create(nav);
    lv_obj_set_size(chip, chip_w, 56);
    lv_obj_set_pos(chip, x, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_pad_left(chip,  10, 0);
    lv_obj_set_style_pad_right(chip,  6, 0);
    lv_obj_set_style_pad_top(chip,    4, 0);
    lv_obj_set_style_pad_bottom(chip, 4, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    *out_chip = chip;

    lv_obj_t *ind = lv_obj_create(nav);
    lv_obj_set_size(ind, chip_w - 16, 3);
    lv_obj_set_pos(ind, x + 8, 53);
    lv_obj_set_style_bg_color(ind, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(ind, 0, 0);
    lv_obj_set_style_pad_all(ind, 0, 0);
    lv_obj_set_style_radius(ind, 2, 0);
    lv_obj_add_flag(ind, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ind, LV_OBJ_FLAG_CLICKABLE);
    *out_ind = ind;

    lv_obj_t *lbl_t = lv_label_create(chip);
    lv_label_set_text(lbl_t, title);
    lv_obj_set_style_text_color(lbl_t, lv_color_hex(0x445566), 0);
    lv_obj_set_style_text_font(lbl_t, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_t, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_v = lv_label_create(chip);
    lv_label_set_text(lbl_v, "--");
    lv_obj_set_style_text_color(lbl_v, lv_color_hex(0x6677AAu), 0);
    lv_obj_set_style_text_font(lbl_v, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_v, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    if (unit[0]) {
        lv_obj_t *lbl_u = lv_label_create(chip);
        lv_label_set_text(lbl_u, unit);
        lv_obj_set_style_text_color(lbl_u, lv_color_hex(0x2A3A4A), 0);
        lv_obj_set_style_text_font(lbl_u, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_u, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }

    return lbl_v;
}

// ── Content container factory ─────────────────────────────────────────────────

static lv_obj_t *make_cont(lv_obj_t *parent)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, 720, 592);
    lv_obj_align(c, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_left(c,   16, 0);
    lv_obj_set_style_pad_right(c,  16, 0);
    lv_obj_set_style_pad_top(c,    16, 0);
    lv_obj_set_style_pad_bottom(c, 16, 0);
    lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_OFF);
    return c;
}

// ── HHCC card (centered value) ────────────────────────────────────────────────

static lv_obj_t *make_hhcc_card(lv_obj_t *parent, const char *title,
                                  const char *unit, uint32_t val_color)
{
    const int CW = 330, CH = 200;
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, CW, CH);
    lv_obj_set_style_bg_color(c, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 18, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(0x2C2C3C), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_pad_all(c, 16, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_t = lv_label_create(c);
    lv_label_set_text(lbl_t, title);
    lv_obj_set_style_text_color(lbl_t, lv_color_hex(0x445566), 0);
    lv_obj_set_style_text_font(lbl_t, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_t, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_v = lv_label_create(c);
    lv_label_set_text(lbl_v, "--");
    lv_obj_set_style_text_color(lbl_v, lv_color_hex(val_color), 0);
    lv_obj_set_style_text_font(lbl_v, &lv_font_montserrat_48, 0);
    lv_obj_align(lbl_v, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *lbl_u = lv_label_create(c);
    lv_label_set_text(lbl_u, unit);
    lv_obj_set_style_text_color(lbl_u, lv_color_hex(0x2A3A4A), 0);
    lv_obj_set_style_text_font(lbl_u, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_u, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    return lbl_v;
}

// ── ui_user_create ────────────────────────────────────────────────────────────

void ui_user_create(void)
{
    sensor_config_t      cfg = sensor_config_get();
    const sensor_model_t *m  = &SENSOR_MODELS[cfg.model_idx];

    scr_user = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_user, lv_color_hex(0x0A0A12), 0);
    lv_obj_set_style_bg_opa(scr_user, LV_OPA_COVER, 0);

    // ── Header (72 px) ────────────────────────────────────
    lv_obj_t *hdr = lv_obj_create(scr_user);
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

    lv_obj_t *lbl_name = lv_label_create(hdr);
    lv_label_set_text(lbl_name, "LIV-24");
    lv_obj_set_style_text_color(lbl_name, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_32, 0);
    lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, title_x, 0);

    char sub_buf[48];
    snprintf(sub_buf, sizeof(sub_buf), "IoT NODE  %s", m->name);
    lv_obj_t *lbl_sub = lv_label_create(hdr);
    lv_label_set_text(lbl_sub, sub_buf);
    lv_obj_set_style_text_color(lbl_sub, lv_color_hex(0x3A4A5A), 0);
    lv_obj_set_style_text_font(lbl_sub, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_sub, LV_ALIGN_LEFT_MID, 112 + title_x, 12);

    lbl_aqi = lv_label_create(hdr);
    lv_label_set_text(lbl_aqi, "AQI Color");
    lv_obj_set_style_text_color(lbl_aqi, lv_color_hex(0x556677), 0);
    lv_obj_set_style_text_font(lbl_aqi, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_aqi, LV_ALIGN_RIGHT_MID, -74, 0);

    sw_aqi = lv_switch_create(hdr);
    lv_obj_set_size(sw_aqi, 54, 28);
    lv_obj_align(sw_aqi, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_add_state(sw_aqi, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw_aqi,
        lv_color_hex(0x00E5FF), (lv_style_selector_t)(LV_PART_INDICATOR | LV_STATE_CHECKED));
    lv_obj_add_event_cb(sw_aqi, color_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // ── HHCC battery (header, same slot as AQI switch, hidden until HHCC tab) ──
    bat_wrap_hdr = lv_obj_create(hdr);
    lv_obj_set_size(bat_wrap_hdr, 120, 28);
    lv_obj_align(bat_wrap_hdr, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_style_bg_opa(bat_wrap_hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bat_wrap_hdr, 0, 0);
    lv_obj_set_style_pad_all(bat_wrap_hdr, 0, 0);
    lv_obj_clear_flag(bat_wrap_hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bat_wrap_hdr, LV_OBJ_FLAG_HIDDEN);

    lbl_hhcc_bat_pct = lv_label_create(bat_wrap_hdr);
    lv_label_set_text(lbl_hhcc_bat_pct, "--%");
    lv_obj_set_style_text_color(lbl_hhcc_bat_pct, lv_color_hex(0x778899), 0);
    lv_obj_set_style_text_font(lbl_hhcc_bat_pct, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_hhcc_bat_pct, LV_ALIGN_LEFT_MID, 0, 0);

    bat_bar_hhcc = lv_bar_create(bat_wrap_hdr);
    lv_obj_set_size(bat_bar_hhcc, 52, 22);
    lv_obj_align(bat_bar_hhcc, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_bar_set_range(bat_bar_hhcc, 0, 100);
    lv_bar_set_value(bat_bar_hhcc, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bat_bar_hhcc, lv_color_hex(0x0A0A12), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bat_bar_hhcc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bat_bar_hhcc, 4, LV_PART_MAIN);
    lv_obj_set_style_border_color(bat_bar_hhcc, lv_color_hex(0x556677), LV_PART_MAIN);
    lv_obj_set_style_border_width(bat_bar_hhcc, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bat_bar_hhcc, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bat_bar_hhcc, lv_color_hex(0x44FF88), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bat_bar_hhcc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bat_bar_hhcc, 2, LV_PART_INDICATOR);

    {
        lv_obj_t *bat_nub = lv_obj_create(bat_wrap_hdr);
        lv_obj_set_size(bat_nub, 5, 12);
        lv_obj_align(bat_nub, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(bat_nub, lv_color_hex(0x556677), 0);
        lv_obj_set_style_bg_opa(bat_nub, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bat_nub, 0, 0);
        lv_obj_set_style_radius(bat_nub, 2, 0);
        lv_obj_clear_flag(bat_nub, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(bat_nub, LV_OBJ_FLAG_CLICKABLE);
    }

    // ── Sensor Nav Bar (56 px at y=72) — 6 tabs × 120px ──
    lv_obj_t *nav = lv_obj_create(scr_user);
    lv_obj_set_size(nav, 720, 56);
    lv_obj_align(nav, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_set_style_bg_color(nav, lv_color_hex(0x0D0D1A), 0);
    lv_obj_set_style_bg_opa(nav, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(nav, 0, 0);
    lv_obj_set_style_border_width(nav, 0, 0);
    lv_obj_set_style_pad_all(nav, 0, 0);
    lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);

    const int CW = 120;  // 720 / 6
    nav_pm25_val = make_nav_chip(nav, "PM2.5",  "ug",         0*CW, CW, &chip_pm,   &ind_pm);
    nav_hhcc_val = make_nav_chip(nav, "HHCC",   "%",          1*CW, CW, &chip_hhcc, &ind_hhcc);
    nav_ec_val   = make_nav_chip(nav, "EC/TDS", "uS",         2*CW, CW, &chip_ec,   &ind_ec);
    nav_orp_val  = make_nav_chip(nav, "ORP",    "mV",         3*CW, CW, &chip_orp,  &ind_orp);
    nav_leak_val = make_nav_chip(nav, "LEAK",   "",           4*CW, CW, &chip_leak, &ind_leak);
    nav_th_val   = make_nav_chip(nav, "TH",     "\xc2\xb0""C", 5*CW, CW, &chip_th,  &ind_th);

    lv_obj_add_event_cb(chip_pm,   nav_pm_cb,   LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(chip_hhcc, nav_hhcc_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(chip_ec,   nav_ec_cb,   LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(chip_orp,  nav_orp_cb,  LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(chip_leak, nav_leak_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(chip_th,   nav_th_cb,   LV_EVENT_CLICKED, NULL);

    for (int dx = CW; dx < 720; dx += CW) {
        lv_obj_t *div = lv_obj_create(nav);
        lv_obj_set_size(div, 1, 36);
        lv_obj_set_pos(div, dx, 10);
        lv_obj_set_style_bg_color(div, lv_color_hex(0x1E2030), 0);
        lv_obj_set_style_border_width(div, 0, 0);
        lv_obj_set_style_pad_all(div, 0, 0);
        lv_obj_clear_flag(div, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(div, LV_OBJ_FLAG_SCROLLABLE);
    }

    lv_obj_t *sep = lv_obj_create(scr_user);
    lv_obj_set_size(sep, 720, 1);
    lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, 128);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x1A1E2A), 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_pad_all(sep, 0, 0);

    // ── view 0: PM2.5 content ─────────────────────────────
    cont_pm = make_cont(scr_user);
    lv_obj_set_layout(cont_pm, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont_pm, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(cont_pm,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(cont_pm, 12, 0);
    lv_obj_set_style_pad_row(cont_pm,    16, 0);
    lv_obj_set_scroll_dir(cont_pm, LV_DIR_VER);
    {
        const int PW = 338;
        card_out_t c = {};
        make_card(cont_pm, PW, 160, "TEMPERATURE", "\xc2\xb0""C", false, &c);
        lbl_temp = c.lbl_val;
        make_card(cont_pm, PW, 160, "HUMIDITY", "%", false, &c);
        lbl_hum = c.lbl_val;
        make_card(cont_pm, PW, 190, "PM 2.5", "ug/m3", true, &c);
        card_pm25 = c.card; lbl_pm25 = c.lbl_val;
        lbl_pm25_st = c.lbl_status; lbl_pm25_title = c.lbl_title;
        make_card(cont_pm, PW, 190, "PM 10", "ug/m3", true, &c);
        card_pm10 = c.card; lbl_pm10 = c.lbl_val;
        lbl_pm10_st = c.lbl_status; lbl_pm10_title = c.lbl_title;

        lv_obj_t *card_snd = lv_obj_create(cont_pm);
        lv_obj_set_size(card_snd, 688, 120);
        lv_obj_set_style_bg_color(card_snd, lv_color_hex(CLR_CARD), 0);
        lv_obj_set_style_bg_opa(card_snd, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card_snd, 18, 0);
        lv_obj_set_style_border_color(card_snd, lv_color_hex(0x2C2C3C), 0);
        lv_obj_set_style_border_width(card_snd, 1, 0);
        lv_obj_set_style_pad_all(card_snd, 16, 0);
        lv_obj_clear_flag(card_snd, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *lbl_snd_t = lv_label_create(card_snd);
        lv_label_set_text(lbl_snd_t, "SOUND LEVEL");
        lv_obj_set_style_text_color(lbl_snd_t, lv_color_hex(0x7788AA), 0);
        lv_obj_set_style_text_font(lbl_snd_t, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_snd_t, LV_ALIGN_LEFT_MID, 0, 0);
        lbl_sound = lv_label_create(card_snd);
        lv_label_set_text(lbl_sound, "--");
        lv_obj_set_style_text_color(lbl_sound, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl_sound, &lv_font_montserrat_48, 0);
        lv_obj_align(lbl_sound, LV_ALIGN_RIGHT_MID, -52, 0);
        lv_obj_t *lbl_snd_u = lv_label_create(card_snd);
        lv_label_set_text(lbl_snd_u, "dB");
        lv_obj_set_style_text_color(lbl_snd_u, lv_color_hex(0x4D5F78), 0);
        lv_obj_set_style_text_font(lbl_snd_u, &lv_font_montserrat_24, 0);
        lv_obj_align(lbl_snd_u, LV_ALIGN_RIGHT_MID, -8, 10);
    }

    // ── view 1: HHCC Flora content ────────────────────────
    cont_hhcc = make_cont(scr_user);
    lv_obj_set_layout(cont_hhcc, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont_hhcc, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(cont_hhcc,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(cont_hhcc, 12, 0);
    lv_obj_set_style_pad_row(cont_hhcc,     8, 0);
    lv_obj_set_style_pad_top(cont_hhcc,    48, 0);
    lv_obj_clear_flag(cont_hhcc, LV_OBJ_FLAG_SCROLLABLE);
    lbl_hhcc_temp  = make_hhcc_card(cont_hhcc, "TEMPERATURE", "\xc2\xb0""C", 0x44FFCCu);
    lbl_hhcc_moist = make_hhcc_card(cont_hhcc, "MOISTURE",    "%",           0x44BBFFu);
    lbl_hhcc_light = make_hhcc_card(cont_hhcc, "LIGHT",       "lux",         0xFFDD44u);
    lbl_hhcc_fert  = make_hhcc_card(cont_hhcc, "FERTILITY",   "uS/cm",       0xFF8844u);

    // ── view 2: EC/TDS content ────────────────────────────
    cont_ec = make_cont(scr_user);
    {
        lv_obj_t *card_ec = lv_obj_create(cont_ec);
        lv_obj_set_size(card_ec, 560, 410);
        lv_obj_align(card_ec, LV_ALIGN_TOP_MID, 0, 56);
        lv_obj_set_style_bg_color(card_ec, lv_color_hex(CLR_CARD), 0);
        lv_obj_set_style_bg_opa(card_ec, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card_ec, 24, 0);
        lv_obj_set_style_border_color(card_ec, lv_color_hex(0x1A3A44), 0);
        lv_obj_set_style_border_width(card_ec, 1, 0);
        lv_obj_set_style_pad_all(card_ec, 24, 0);
        lv_obj_clear_flag(card_ec, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *lbl_ec_title = lv_label_create(card_ec);
        lv_label_set_text(lbl_ec_title, "EC / TDS VALUE");
        lv_obj_set_style_text_color(lbl_ec_title, lv_color_hex(0x7788AA), 0);
        lv_obj_set_style_text_font(lbl_ec_title, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_ec_title, LV_ALIGN_TOP_LEFT, 0, 0);
        lbl_ec_big = lv_label_create(card_ec);
        lv_label_set_text(lbl_ec_big, "--");
        lv_obj_set_style_text_color(lbl_ec_big, lv_color_hex(0x00E5FF), 0);
        lv_obj_set_style_text_font(lbl_ec_big, &lv_font_montserrat_48, 0);
        lv_obj_align(lbl_ec_big, LV_ALIGN_CENTER, 0, -24);
        lv_obj_t *lbl_ec_unit = lv_label_create(card_ec);
        lv_label_set_text(lbl_ec_unit, "uS/cm");
        lv_obj_set_style_text_color(lbl_ec_unit, lv_color_hex(0x4D5F78), 0);
        lv_obj_set_style_text_font(lbl_ec_unit, &lv_font_montserrat_24, 0);
        lv_obj_align(lbl_ec_unit, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        lbl_tds_big = lv_label_create(card_ec);
        lv_label_set_text(lbl_tds_big, "TDS  --  mg/L");
        lv_obj_set_style_text_color(lbl_tds_big, lv_color_hex(0x446677), 0);
        lv_obj_set_style_text_font(lbl_tds_big, &lv_font_montserrat_24, 0);
        lv_obj_align(lbl_tds_big, LV_ALIGN_CENTER, 0, 44);
    }

    // ── view 3: ORP content ───────────────────────────────
    cont_orp = make_cont(scr_user);
    {
        lv_obj_t *card_orp = lv_obj_create(cont_orp);
        lv_obj_set_size(card_orp, 560, 410);
        lv_obj_align(card_orp, LV_ALIGN_TOP_MID, 0, 56);
        lv_obj_set_style_bg_color(card_orp, lv_color_hex(CLR_CARD), 0);
        lv_obj_set_style_bg_opa(card_orp, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card_orp, 24, 0);
        lv_obj_set_style_border_color(card_orp, lv_color_hex(0x3A2A1C), 0);
        lv_obj_set_style_border_width(card_orp, 1, 0);
        lv_obj_set_style_pad_all(card_orp, 24, 0);
        lv_obj_clear_flag(card_orp, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *lbl_orp_title = lv_label_create(card_orp);
        lv_label_set_text(lbl_orp_title, "ORP VALUE");
        lv_obj_set_style_text_color(lbl_orp_title, lv_color_hex(0x7788AA), 0);
        lv_obj_set_style_text_font(lbl_orp_title, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_orp_title, LV_ALIGN_TOP_LEFT, 0, 0);
        lbl_orp_big = lv_label_create(card_orp);
        lv_label_set_text(lbl_orp_big, "--");
        lv_obj_set_style_text_color(lbl_orp_big, lv_color_hex(0xFFAA44), 0);
        lv_obj_set_style_text_font(lbl_orp_big, &lv_font_montserrat_48, 0);
        lv_obj_align(lbl_orp_big, LV_ALIGN_CENTER, 0, -24);
        lv_obj_t *lbl_orp_unit = lv_label_create(card_orp);
        lv_label_set_text(lbl_orp_unit, "mV");
        lv_obj_set_style_text_color(lbl_orp_unit, lv_color_hex(0x4D5F78), 0);
        lv_obj_set_style_text_font(lbl_orp_unit, &lv_font_montserrat_24, 0);
        lv_obj_align(lbl_orp_unit, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        lbl_orp_temp_big = lv_label_create(card_orp);
        lv_label_set_text(lbl_orp_temp_big, "Temp  --  \xc2\xb0""C");
        lv_obj_set_style_text_color(lbl_orp_temp_big, lv_color_hex(0x446677), 0);
        lv_obj_set_style_text_font(lbl_orp_temp_big, &lv_font_montserrat_24, 0);
        lv_obj_align(lbl_orp_temp_big, LV_ALIGN_CENTER, 0, 44);
    }

    // ── view 4: LEAK content ──────────────────────────────
    cont_leak = make_cont(scr_user);
    {
        card_leak_big = lv_obj_create(cont_leak);
        lv_obj_set_size(card_leak_big, 560, 410);
        lv_obj_align(card_leak_big, LV_ALIGN_TOP_MID, 0, 56);
        lv_obj_set_style_bg_color(card_leak_big, lv_color_hex(0x092B18), 0);
        lv_obj_set_style_bg_opa(card_leak_big, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card_leak_big, 24, 0);
        lv_obj_set_style_border_width(card_leak_big, 0, 0);
        lv_obj_set_style_pad_all(card_leak_big, 24, 0);
        lv_obj_clear_flag(card_leak_big, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *lbl_leak_title = lv_label_create(card_leak_big);
        lv_label_set_text(lbl_leak_title, "LEAK DETECTOR");
        lv_obj_set_style_text_color(lbl_leak_title, lv_color_hex(0x4A8A60), 0);
        lv_obj_set_style_text_font(lbl_leak_title, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_leak_title, LV_ALIGN_TOP_LEFT, 0, 0);
        lbl_leak_big = lv_label_create(card_leak_big);
        lv_label_set_text(lbl_leak_big, "--");
        lv_obj_set_style_text_color(lbl_leak_big, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl_leak_big, &lv_font_montserrat_48, 0);
        lv_obj_align(lbl_leak_big, LV_ALIGN_CENTER, 0, 0);
    }

    // ── view 5: TH content (CWT-TH04S — teal theme) ──────
    cont_th = make_cont(scr_user);
    lv_obj_set_layout(cont_th, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont_th, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont_th,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cont_th, 20, 0);
    lv_obj_set_style_pad_bottom(cont_th, 56, 0);
    lv_obj_clear_flag(cont_th, LV_OBJ_FLAG_SCROLLABLE);
    {
        const int TW = 330, TH_H = 295;

        // Temperature card
        lv_obj_t *card_t = lv_obj_create(cont_th);
        lv_obj_set_size(card_t, TW, TH_H);
        lv_obj_set_style_bg_color(card_t, lv_color_hex(0x0D1E24), 0);
        lv_obj_set_style_bg_opa(card_t, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card_t, 20, 0);
        lv_obj_set_style_border_color(card_t, lv_color_hex(0x1A3A44), 0);
        lv_obj_set_style_border_width(card_t, 1, 0);
        lv_obj_set_style_pad_all(card_t, 20, 0);
        lv_obj_clear_flag(card_t, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *lbl_tt = lv_label_create(card_t);
        lv_label_set_text(lbl_tt, "TEMPERATURE");
        lv_obj_set_style_text_color(lbl_tt, lv_color_hex(0x3A7A8A), 0);
        lv_obj_set_style_text_font(lbl_tt, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_tt, LV_ALIGN_TOP_LEFT, 0, 0);
        lbl_th_temp = lv_label_create(card_t);
        lv_label_set_text(lbl_th_temp, "--");
        lv_obj_set_style_text_color(lbl_th_temp, lv_color_hex(0x44FFE0), 0);
        lv_obj_set_style_text_font(lbl_th_temp, &lv_font_montserrat_48, 0);
        lv_obj_align(lbl_th_temp, LV_ALIGN_CENTER, 0, 0);
        lv_obj_t *lbl_tu = lv_label_create(card_t);
        lv_label_set_text(lbl_tu, "\xc2\xb0""C");
        lv_obj_set_style_text_color(lbl_tu, lv_color_hex(0x1A4A55), 0);
        lv_obj_set_style_text_font(lbl_tu, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_tu, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

        // Humidity card
        lv_obj_t *card_h = lv_obj_create(cont_th);
        lv_obj_set_size(card_h, TW, TH_H);
        lv_obj_set_style_bg_color(card_h, lv_color_hex(0x0D1E24), 0);
        lv_obj_set_style_bg_opa(card_h, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card_h, 20, 0);
        lv_obj_set_style_border_color(card_h, lv_color_hex(0x1A3A44), 0);
        lv_obj_set_style_border_width(card_h, 1, 0);
        lv_obj_set_style_pad_all(card_h, 20, 0);
        lv_obj_clear_flag(card_h, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *lbl_ht = lv_label_create(card_h);
        lv_label_set_text(lbl_ht, "HUMIDITY");
        lv_obj_set_style_text_color(lbl_ht, lv_color_hex(0x3A7A8A), 0);
        lv_obj_set_style_text_font(lbl_ht, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_ht, LV_ALIGN_TOP_LEFT, 0, 0);
        lbl_th_hum = lv_label_create(card_h);
        lv_label_set_text(lbl_th_hum, "--");
        lv_obj_set_style_text_color(lbl_th_hum, lv_color_hex(0x44BBFFu), 0);
        lv_obj_set_style_text_font(lbl_th_hum, &lv_font_montserrat_48, 0);
        lv_obj_align(lbl_th_hum, LV_ALIGN_CENTER, 0, 0);
        lv_obj_t *lbl_hu = lv_label_create(card_h);
        lv_label_set_text(lbl_hu, "%");
        lv_obj_set_style_text_color(lbl_hu, lv_color_hex(0x1A4A55), 0);
        lv_obj_set_style_text_font(lbl_hu, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_hu, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }

    // ── Default view based on active model ────────────────
    // model 0 (PM) → 0, model 1 (EC) → 2, model 2 (LEAK) → 4, model 3 (TH) → 5, model 4 (ORP) → 3
    int default_view = 0;
    switch (cfg.model_idx) {
        case 1:  default_view = 2; break;
        case 2:  default_view = 4; break;
        case 3:  default_view = 5; break;
        case 4:  default_view = 3; break;
        default: default_view = 0; break;
    }
    switch_view(default_view);
}

// ── ui_user_update ────────────────────────────────────────────────────────────

void ui_user_update(float temp, float hum, float pm25, float pm10, int sound)
{
    if (!scr_user) return;
    g_pm25_last = pm25;
    g_pm10_last = pm10;
    char buf[16];

    snprintf(buf, sizeof(buf), "%.1f", temp);
    if (lbl_temp) lv_label_set_text(lbl_temp, buf);
    snprintf(buf, sizeof(buf), "%.1f", hum);
    if (lbl_hum) lv_label_set_text(lbl_hum, buf);
    snprintf(buf, sizeof(buf), "%.1f", pm25);
    if (lbl_pm25)     lv_label_set_text(lbl_pm25, buf);
    if (nav_pm25_val) lv_label_set_text(nav_pm25_val, buf);
    snprintf(buf, sizeof(buf), "%.1f", pm10);
    if (lbl_pm10) lv_label_set_text(lbl_pm10, buf);
    snprintf(buf, sizeof(buf), "%d", sound);
    if (lbl_sound) lv_label_set_text(lbl_sound, buf);

    apply_pm_colors();
}

// ── ui_user_update_ec ─────────────────────────────────────────────────────────

void ui_user_update_ec(float ec)
{
    if (!scr_user) return;
    char buf[32];
    if (!std::isnan(ec)) {
        snprintf(buf, sizeof(buf), "%.0f", ec);
        if (nav_ec_val) lv_label_set_text(nav_ec_val, buf);
        if (lbl_ec_big) lv_label_set_text(lbl_ec_big, buf);
        snprintf(buf, sizeof(buf), "TDS  %.0f  mg/L", ec * 0.5f);
        if (lbl_tds_big) lv_label_set_text(lbl_tds_big, buf);
    } else {
        if (nav_ec_val)  lv_label_set_text(nav_ec_val,  "--");
        if (lbl_ec_big)  lv_label_set_text(lbl_ec_big,  "--");
        if (lbl_tds_big) lv_label_set_text(lbl_tds_big, "TDS  --  mg/L");
    }
}

// ── ui_user_update_leak ───────────────────────────────────────────────────────

void ui_user_update_leak(bool alarm)
{
    if (!scr_user) return;
    if (nav_leak_val) {
        lv_label_set_text(nav_leak_val, alarm ? "ALARM" : "OK");
        lv_obj_set_style_text_color(nav_leak_val,
            alarm ? lv_color_hex(0xFF4444u) : lv_color_hex(0x00CC44u), 0);
    }
    if (card_leak_big) {
        lv_obj_set_style_bg_color(card_leak_big,
            alarm ? lv_color_hex(0x4A0808u) : lv_color_hex(0x092B18u), 0);
    }
    if (lbl_leak_big) {
        lv_label_set_text(lbl_leak_big, alarm ? "ALARM" : "NORMAL");
        lv_obj_set_style_text_color(lbl_leak_big,
            alarm ? lv_color_hex(0xFF6666u) : lv_color_hex(0x44FF88u), 0);
    }
}

// ── ui_user_update_th ─────────────────────────────────────────────────────────

void ui_user_update_th(float temp, float hum)
{
    if (!scr_user) return;
    char buf[16];
    if (!std::isnan(temp)) {
        snprintf(buf, sizeof(buf), "%.1f", temp);
        if (lbl_th_temp) lv_label_set_text(lbl_th_temp, buf);
        if (nav_th_val)  lv_label_set_text(nav_th_val, buf);
    }
    if (!std::isnan(hum)) {
        snprintf(buf, sizeof(buf), "%.1f", hum);
        if (lbl_th_hum) lv_label_set_text(lbl_th_hum, buf);
    }
}

// ── ui_user_update_orp ────────────────────────────────────────────────────────

void ui_user_update_orp(float orp, float temp)
{
    if (!scr_user) return;
    char buf[32];
    if (!std::isnan(orp)) {
        snprintf(buf, sizeof(buf), "%.1f", orp);
        if (nav_orp_val) lv_label_set_text(nav_orp_val, buf);
        if (lbl_orp_big) lv_label_set_text(lbl_orp_big, buf);
    } else {
        if (nav_orp_val) lv_label_set_text(nav_orp_val, "--");
        if (lbl_orp_big) lv_label_set_text(lbl_orp_big, "--");
    }
    if (!std::isnan(temp)) {
        snprintf(buf, sizeof(buf), "Temp  %.1f  \xc2\xb0""C", temp);
        if (lbl_orp_temp_big) lv_label_set_text(lbl_orp_temp_big, buf);
    } else {
        if (lbl_orp_temp_big) lv_label_set_text(lbl_orp_temp_big, "Temp  --  \xc2\xb0""C");
    }
}

// ── ui_user_update_hhcc ───────────────────────────────────────────────────────

void ui_user_update_hhcc(float temp, float moisture, float light, float fertility, float battery)
{
    if (!scr_user) return;
    char buf[16];
    if (!std::isnan(moisture)) {
        snprintf(buf, sizeof(buf), "%.0f", moisture);
        if (nav_hhcc_val)   lv_label_set_text(nav_hhcc_val, buf);
        if (lbl_hhcc_moist) lv_label_set_text(lbl_hhcc_moist, buf);
    }
    if (!std::isnan(temp)) {
        snprintf(buf, sizeof(buf), "%.1f", temp);
        if (lbl_hhcc_temp) lv_label_set_text(lbl_hhcc_temp, buf);
    }
    if (!std::isnan(light)) {
        snprintf(buf, sizeof(buf), "%.0f", light);
        if (lbl_hhcc_light) lv_label_set_text(lbl_hhcc_light, buf);
    }
    if (!std::isnan(fertility)) {
        snprintf(buf, sizeof(buf), "%.0f", fertility);
        if (lbl_hhcc_fert) lv_label_set_text(lbl_hhcc_fert, buf);
    }
    if (!std::isnan(battery) && battery >= 0.0f) {
        int pct = (int)battery;
        snprintf(buf, sizeof(buf), "%d%%", pct);
        if (lbl_hhcc_bat_pct) lv_label_set_text(lbl_hhcc_bat_pct, buf);
        if (bat_bar_hhcc) {
            lv_bar_set_value(bat_bar_hhcc, pct, LV_ANIM_OFF);
            uint32_t fill_clr = pct >= 50 ? 0x44FF88u : pct >= 20 ? 0xFFDD44u : 0xFF4444u;
            uint32_t bord_clr = pct < 20 ? 0xFF4444u : 0x556677u;
            lv_obj_set_style_bg_color(bat_bar_hhcc, lv_color_hex(fill_clr), LV_PART_INDICATOR);
            lv_obj_set_style_border_color(bat_bar_hhcc, lv_color_hex(bord_clr), LV_PART_MAIN);
            lv_obj_set_style_text_color(lbl_hhcc_bat_pct,
                lv_color_hex(pct < 20 ? 0xFF4444u : 0x445566u), 0);
        }
    }
}
