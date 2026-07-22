#include "ui_user.h"
#include <stdio.h>

#define CLR_GOOD      0x009966u
#define CLR_MODERATE  0xFFDE33u
#define CLR_SENSITIVE 0xFF9933u
#define CLR_UNHEALTHY 0xCC0033u
#define CLR_CARD      0x1A1A26u

lv_obj_t *scr_user = NULL;

static lv_obj_t *lbl_temp  = NULL;
static lv_obj_t *lbl_hum   = NULL;
static lv_obj_t *lbl_pm25  = NULL;
static lv_obj_t *lbl_pm10  = NULL;
static lv_obj_t *lbl_sound = NULL;
static lv_obj_t *card_pm25 = NULL;
static lv_obj_t *card_pm10 = NULL;
static lv_obj_t *lbl_pm25_st = NULL;
static lv_obj_t *lbl_pm10_st = NULL;

static lv_color_t pm_color(float val)
{
    if (val <= 12.0f)  return lv_color_hex(CLR_GOOD);
    if (val <= 35.4f)  return lv_color_hex(CLR_MODERATE);
    if (val <= 55.4f)  return lv_color_hex(CLR_SENSITIVE);
    return lv_color_hex(CLR_UNHEALTHY);
}

static const char *pm_label(float val)
{
    if (val <= 12.0f)  return "Good";
    if (val <= 35.4f)  return "Moderate";
    if (val <= 55.4f)  return "Sensitive";
    return "Unhealthy";
}

// ── Card factory ─────────────────────────────────────────────────────────────

static lv_obj_t *make_card(lv_obj_t *parent, int w, int h,
                            const char *title, const char *unit,
                            lv_obj_t **out_val, lv_obj_t **out_sub)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x252535), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, lv_color_hex(0x778899), 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *v = lv_label_create(card);
    lv_label_set_text(v, "--");
    lv_obj_set_style_text_color(v, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_48, 0);
    lv_obj_align(v, LV_ALIGN_LEFT_MID, 0, 4);
    *out_val = v;

    lv_obj_t *u = lv_label_create(card);
    lv_label_set_text(u, unit);
    lv_obj_set_style_text_color(u, lv_color_hex(0x556677), 0);
    lv_obj_set_style_text_font(u, &lv_font_montserrat_14, 0);
    lv_obj_align_to(u, v, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -4);

    if (out_sub) {
        lv_obj_t *s = lv_label_create(card);
        lv_label_set_text(s, "");
        lv_obj_set_style_text_font(s, &lv_font_montserrat_14, 0);
        lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        *out_sub = s;
    }

    return card;
}

// ── Public ───────────────────────────────────────────────────────────────────

void ui_user_create(void)
{
    scr_user = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_user, lv_color_hex(0x0A0A12), 0);
    lv_obj_set_style_bg_opa(scr_user, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr_user, 0, 0);
    lv_obj_clear_flag(scr_user, LV_OBJ_FLAG_SCROLLABLE);

    // ── Header ───────────────────────────────────────────────────────────────
    lv_obj_t *hdr = lv_obj_create(scr_user);
    lv_obj_set_size(hdr, 720, 72);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x12121E), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 22, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_name = lv_label_create(hdr);
    lv_label_set_text(lbl_name, "LIV-24");
    lv_obj_set_style_text_color(lbl_name, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_32, 0);
    lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 0, 0);

    // Cyan accent line
    lv_obj_t *accent = lv_obj_create(scr_user);
    lv_obj_set_size(accent, 720, 3);
    lv_obj_set_pos(accent, 0, 72);
    lv_obj_set_style_bg_color(accent, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_style_pad_all(accent, 0, 0);

    // ── Card container ────────────────────────────────────────────────────────
    lv_obj_t *cont = lv_obj_create(scr_user);
    lv_obj_set_size(cont, 720, 620);
    lv_obj_set_pos(cont, 0, 84);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 16, 0);
    lv_obj_set_style_pad_row(cont, 12, 0);
    lv_obj_set_style_pad_column(cont, 12, 0);
    lv_obj_set_style_shadow_width(cont, 0, 0);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    // TEMP — 338×150
    lv_obj_t *dummy1;
    make_card(cont, 338, 150, "TEMPERATURE", "°C", &lbl_temp, &dummy1);

    // HUMIDITY — 338×150
    lv_obj_t *dummy2;
    make_card(cont, 338, 150, "HUMIDITY", "%", &lbl_hum, &dummy2);

    // PM2.5 — 338×180
    make_card(cont, 338, 180, "PM2.5", "µg/m³", &lbl_pm25, &lbl_pm25_st);
    card_pm25 = lv_obj_get_parent(lbl_pm25);

    // PM10 — 338×180
    make_card(cont, 338, 180, "PM10", "µg/m³", &lbl_pm10, &lbl_pm10_st);
    card_pm10 = lv_obj_get_parent(lbl_pm10);

    // SOUND — 688×120 (full width)
    lv_obj_t *dummy3;
    make_card(cont, 688, 120, "SOUND LEVEL", "dB", &lbl_sound, &dummy3);
}

void ui_user_update(float temp, float hum, float pm25, float pm10, int sound)
{
    char buf[16];

    if (lbl_temp)  { snprintf(buf, sizeof(buf), "%.1f", temp);  lv_label_set_text(lbl_temp,  buf); }
    if (lbl_hum)   { snprintf(buf, sizeof(buf), "%.1f", hum);   lv_label_set_text(lbl_hum,   buf); }
    if (lbl_sound) { snprintf(buf, sizeof(buf), "%d",   sound); lv_label_set_text(lbl_sound, buf); }

    if (lbl_pm25) {
        snprintf(buf, sizeof(buf), "%.1f", pm25);
        lv_label_set_text(lbl_pm25, buf);
        lv_color_t c = pm_color(pm25);
        lv_obj_set_style_text_color(lbl_pm25, c, 0);
        if (card_pm25) lv_obj_set_style_border_color(card_pm25, c, 0);
        if (lbl_pm25_st) {
            lv_label_set_text(lbl_pm25_st, pm_label(pm25));
            lv_obj_set_style_text_color(lbl_pm25_st, c, 0);
        }
    }
    if (lbl_pm10) {
        snprintf(buf, sizeof(buf), "%.1f", pm10);
        lv_label_set_text(lbl_pm10, buf);
        lv_color_t c = pm_color(pm10);
        lv_obj_set_style_text_color(lbl_pm10, c, 0);
        if (card_pm10) lv_obj_set_style_border_color(card_pm10, c, 0);
        if (lbl_pm10_st) {
            lv_label_set_text(lbl_pm10_st, pm_label(pm10));
            lv_obj_set_style_text_color(lbl_pm10_st, c, 0);
        }
    }
}
