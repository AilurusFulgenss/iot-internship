#include "ui_flora.h"
#include "eth_upload.h"
#include <stdio.h>
#include <cmath>

lv_obj_t *scr_flora = NULL;

static lv_obj_t *lbl_flora_temp     = NULL;
static lv_obj_t *lbl_flora_moisture = NULL;
static lv_obj_t *lbl_flora_light    = NULL;
static lv_obj_t *lbl_flora_fert     = NULL;

// Returns the value label
static lv_obj_t *make_flora_card(lv_obj_t *parent, const char *title,
                                  const char *unit, uint32_t val_color)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, 338, 530);
    lv_obj_set_style_bg_color(c, lv_color_hex(0x111A11), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 20, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(0x1C3A1C), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_pad_all(c, 20, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_t = lv_label_create(c);
    lv_label_set_text(lbl_t, title);
    lv_obj_set_style_text_color(lbl_t, lv_color_hex(0x4A7A4A), 0);
    lv_obj_set_style_text_font(lbl_t, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_t, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_v = lv_label_create(c);
    lv_label_set_text(lbl_v, "--");
    lv_obj_set_style_text_color(lbl_v, lv_color_hex(val_color), 0);
    lv_obj_set_style_text_font(lbl_v, &lv_font_montserrat_48, 0);
    lv_obj_align(lbl_v, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *lbl_u = lv_label_create(c);
    lv_label_set_text(lbl_u, unit);
    lv_obj_set_style_text_color(lbl_u, lv_color_hex(0x2A4A2A), 0);
    lv_obj_set_style_text_font(lbl_u, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_u, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    return lbl_v;
}

void ui_flora_create(void)
{
    scr_flora = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_flora, lv_color_hex(0x0A0A12), 0);
    lv_obj_set_style_bg_opa(scr_flora, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr_flora, 0, 0);

    // ── Header (72px) ──────────────────────────────────────
    lv_obj_t *hdr = lv_obj_create(scr_flora);
    lv_obj_set_size(hdr, 720, 72);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x0C150C), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 22, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *accent = lv_obj_create(hdr);
    lv_obj_set_size(accent, 720, 3);
    lv_obj_align(accent, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_color(accent, lv_color_hex(0x44CC44), 0);
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

    lv_obj_t *lbl_title = lv_label_create(hdr);
    lv_label_set_text(lbl_title, "FLORA");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x44CC44), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_32, 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, title_x, 0);

    lv_obj_t *lbl_sub = lv_label_create(hdr);
    lv_label_set_text(lbl_sub, "HHCC Flower Care");
    lv_obj_set_style_text_color(lbl_sub, lv_color_hex(0x2A4A2A), 0);
    lv_obj_set_style_text_font(lbl_sub, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_sub, LV_ALIGN_LEFT_MID, 96 + title_x, 12);

    // ── 2×2 card grid ──────────────────────────────────────
    lv_obj_t *cont = lv_obj_create(scr_flora);
    lv_obj_set_size(cont, 688, 1150);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 82);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_left(cont,   0, 0);
    lv_obj_set_style_pad_right(cont,  0, 0);
    lv_obj_set_style_pad_top(cont,   16, 0);
    lv_obj_set_style_pad_bottom(cont, 16, 0);
    lv_obj_set_style_pad_column(cont, 12, 0);
    lv_obj_set_style_pad_row(cont,    12, 0);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

    lbl_flora_temp     = make_flora_card(cont, "TEMPERATURE", "\xc2\xb0""C", 0x44FF88u);
    lbl_flora_moisture = make_flora_card(cont, "MOISTURE",    "%",           0x44BBFFu);
    lbl_flora_light    = make_flora_card(cont, "LIGHT",       "lux",         0xFFDD44u);
    lbl_flora_fert     = make_flora_card(cont, "FERTILITY",   "uS/cm",       0xFF8844u);
}

void ui_flora_update(float temp, float moisture, float light, float fertility)
{
    if (!scr_flora) return;
    char buf[16];

    if (!std::isnan(temp)) {
        snprintf(buf, sizeof(buf), "%.1f", temp);
        if (lbl_flora_temp) lv_label_set_text(lbl_flora_temp, buf);
    }
    if (!std::isnan(moisture)) {
        snprintf(buf, sizeof(buf), "%.0f", moisture);
        if (lbl_flora_moisture) lv_label_set_text(lbl_flora_moisture, buf);
    }
    if (!std::isnan(light)) {
        snprintf(buf, sizeof(buf), "%.0f", light);
        if (lbl_flora_light) lv_label_set_text(lbl_flora_light, buf);
    }
    if (!std::isnan(fertility)) {
        snprintf(buf, sizeof(buf), "%.0f", fertility);
        if (lbl_flora_fert) lv_label_set_text(lbl_flora_fert, buf);
    }
}
