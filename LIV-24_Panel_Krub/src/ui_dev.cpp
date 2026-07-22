#include "ui_dev.h"
#include "calib.h"
#include <stdio.h>

lv_obj_t *scr_dev = NULL;

// One row per sensor field: label, offset display, gain display
typedef struct {
    const char   *name;
    sensor_calib_t *field;
    lv_obj_t     *lbl_offset;
    lv_obj_t     *lbl_gain;
} calib_row_t;

static calib_row_t s_rows[5];

static void refresh_labels(void)
{
    char buf[16];
    for (int i = 0; i < 5; i++) {
        snprintf(buf, sizeof(buf), "%+.2f", s_rows[i].field->offset);
        lv_label_set_text(s_rows[i].lbl_offset, buf);
        snprintf(buf, sizeof(buf), "%.3f", s_rows[i].field->gain);
        lv_label_set_text(s_rows[i].lbl_gain, buf);
    }
}

// user_data encoding: row*10 + type (0=offset-, 1=offset+, 2=gain-, 3=gain+)
static void on_adj(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_event_get_user_data(e);
    int row  = code / 10;
    int type = code % 10;

    sensor_calib_t *f = s_rows[row].field;
    switch (type) {
        case 0: f->offset -= 0.1f; break;
        case 1: f->offset += 0.1f; break;
        case 2: f->gain   -= 0.01f; if (f->gain < 0.01f) f->gain = 0.01f; break;
        case 3: f->gain   += 0.01f; break;
    }
    refresh_labels();
}

static void on_save(lv_event_t *)
{
    calib_save();
}

// ── Widget helpers ────────────────────────────────────────────────────────────

static lv_obj_t *make_adj_btn(lv_obj_t *parent, const char *txt, int code,
                               uint32_t color)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 52, 44);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x334455), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xCCCCCC), 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, on_adj, LV_EVENT_CLICKED, (void *)(intptr_t)code);
    return btn;
}

static lv_obj_t *make_val_lbl(lv_obj_t *parent, const char *init)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, init);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_width(lbl, 90);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    return lbl;
}

// ── Public ───────────────────────────────────────────────────────────────────

void ui_dev_create(void)
{
    scr_dev = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_dev, lv_color_hex(0x080810), 0);
    lv_obj_set_style_bg_opa(scr_dev, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr_dev, 0, 0);
    lv_obj_clear_flag(scr_dev, LV_OBJ_FLAG_SCROLLABLE);

    // ── Header ───────────────────────────────────────────────────────────────
    lv_obj_t *hdr = lv_obj_create(scr_dev);
    lv_obj_set_size(hdr, 720, 72);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x0D0D1A), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 22, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "CALIBRATION");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    // Back button
    lv_obj_t *back = lv_btn_create(hdr);
    lv_obj_set_size(back, 100, 44);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_border_color(back, lv_color_hex(0x2C3D52), 0);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, [](lv_event_t *) {
        extern void set_app_mode(int);
        set_app_mode(0);   // MODE_USER = 0
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " HOME");
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0x556677), 0);
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(back_lbl);

    // Cyan accent
    lv_obj_t *accent = lv_obj_create(scr_dev);
    lv_obj_set_size(accent, 720, 3);
    lv_obj_set_pos(accent, 0, 72);
    lv_obj_set_style_bg_color(accent, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_style_pad_all(accent, 0, 0);

    // ── Column headers ────────────────────────────────────────────────────────
    // [Sensor 180] [OFFSET: - val +] [GAIN: - val +]
    const int ROW_Y0 = 92;
    const int ROW_H  = 64;
    const int X_OFF  = 210;   // offset group x
    const int X_GAIN = 460;   // gain group x

    lv_obj_t *h_sensor = lv_label_create(scr_dev);
    lv_label_set_text(h_sensor, "SENSOR");
    lv_obj_set_style_text_color(h_sensor, lv_color_hex(0x445566), 0);
    lv_obj_set_style_text_font(h_sensor, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(h_sensor, 28, ROW_Y0 - 20);

    lv_obj_t *h_off = lv_label_create(scr_dev);
    lv_label_set_text(h_off, "OFFSET  (±0.1)");
    lv_obj_set_style_text_color(h_off, lv_color_hex(0x445566), 0);
    lv_obj_set_style_text_font(h_off, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(h_off, X_OFF, ROW_Y0 - 20);

    lv_obj_t *h_gain = lv_label_create(scr_dev);
    lv_label_set_text(h_gain, "GAIN  (±0.01)");
    lv_obj_set_style_text_color(h_gain, lv_color_hex(0x445566), 0);
    lv_obj_set_style_text_font(h_gain, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(h_gain, X_GAIN, ROW_Y0 - 20);

    // ── Sensor rows ───────────────────────────────────────────────────────────
    const char *names[5] = {"TEMP", "HUMIDITY", "SOUND", "PM2.5", "PM10"};
    sensor_calib_t *fields[5] = {
        &g_calib.temp, &g_calib.hum, &g_calib.sound, &g_calib.pm25, &g_calib.pm10
    };

    char buf[16];
    for (int i = 0; i < 5; i++) {
        s_rows[i].name  = names[i];
        s_rows[i].field = fields[i];

        int y = ROW_Y0 + i * ROW_H;

        // Sensor name
        lv_obj_t *nm = lv_label_create(scr_dev);
        lv_label_set_text(nm, names[i]);
        lv_obj_set_style_text_color(nm, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_text_font(nm, &lv_font_montserrat_24, 0);
        lv_obj_set_pos(nm, 28, y + 10);

        // Offset: [-] [val] [+]
        lv_obj_t *bom = make_adj_btn(scr_dev, "-", i * 10 + 0, 0x1A1A2E);
        lv_obj_set_pos(bom, X_OFF, y + 6);

        snprintf(buf, sizeof(buf), "%+.2f", fields[i]->offset);
        s_rows[i].lbl_offset = make_val_lbl(scr_dev, buf);
        lv_obj_set_pos(s_rows[i].lbl_offset, X_OFF + 56, y + 10);

        lv_obj_t *bop = make_adj_btn(scr_dev, "+", i * 10 + 1, 0x0D2010);
        lv_obj_set_pos(bop, X_OFF + 152, y + 6);

        // Gain: [-] [val] [+]
        lv_obj_t *bgm = make_adj_btn(scr_dev, "-", i * 10 + 2, 0x1A1A2E);
        lv_obj_set_pos(bgm, X_GAIN, y + 6);

        snprintf(buf, sizeof(buf), "%.3f", fields[i]->gain);
        s_rows[i].lbl_gain = make_val_lbl(scr_dev, buf);
        lv_obj_set_pos(s_rows[i].lbl_gain, X_GAIN + 56, y + 10);

        lv_obj_t *bgp = make_adj_btn(scr_dev, "+", i * 10 + 3, 0x0D2010);
        lv_obj_set_pos(bgp, X_GAIN + 152, y + 6);

        // Divider line
        lv_obj_t *div = lv_obj_create(scr_dev);
        lv_obj_set_size(div, 680, 1);
        lv_obj_set_pos(div, 20, y + ROW_H - 2);
        lv_obj_set_style_bg_color(div, lv_color_hex(0x1E1E30), 0);
        lv_obj_set_style_border_width(div, 0, 0);
        lv_obj_set_style_pad_all(div, 0, 0);
    }

    // ── Save button ───────────────────────────────────────────────────────────
    lv_obj_t *save = lv_btn_create(scr_dev);
    lv_obj_set_size(save, 220, 56);
    lv_obj_align(save, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_set_style_bg_color(save, lv_color_hex(0x004455), 0);
    lv_obj_set_style_bg_opa(save, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(save, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(save, 1, 0);
    lv_obj_set_style_radius(save, 10, 0);
    lv_obj_set_style_shadow_width(save, 0, 0);
    lv_obj_add_event_cb(save, on_save, LV_EVENT_CLICKED, NULL);
    lv_obj_t *save_lbl = lv_label_create(save);
    lv_label_set_text(save_lbl, LV_SYMBOL_SAVE "  SAVE");
    lv_obj_set_style_text_color(save_lbl, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(save_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(save_lbl);
}
