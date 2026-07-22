#include "ui_pin.h"
#include <string.h>
#include <functional>

#define PIN_SECRET "999999"
#define PIN_LEN    6

static std::function<void()> s_success_cb;
static char       s_entered[PIN_LEN + 1];
static int        s_len        = 0;
static lv_obj_t  *s_overlay    = NULL;
static lv_obj_t  *s_dots[PIN_LEN];
static lv_timer_t *s_err_timer = NULL;

static void update_dots(void) {
    for (int i = 0; i < PIN_LEN; i++) {
        if (!s_dots[i]) continue;
        lv_obj_set_style_bg_color(s_dots[i],
            (i < s_len) ? lv_color_hex(0x00E5FF) : lv_color_hex(0x333355), 0);
    }
}

static void close_modal(void) {
    if (s_err_timer) { lv_timer_delete(s_err_timer); s_err_timer = NULL; }
    if (s_overlay)   { lv_obj_delete(s_overlay); s_overlay = NULL; }
    s_len = 0;
    memset(s_entered, 0, sizeof(s_entered));
    for (int i = 0; i < PIN_LEN; i++) s_dots[i] = NULL;
}

static void err_timer_cb(lv_timer_t *t) {
    s_err_timer = NULL;
    s_len = 0;
    memset(s_entered, 0, sizeof(s_entered));
    if (s_overlay) update_dots();
    lv_timer_delete(t);
}

// user_data: 0-9 = digit, -1 = backspace, -2 = OK, -3 = cancel
static void on_key(lv_event_t *e) {
    int key = (int)(intptr_t)lv_event_get_user_data(e);

    if (key == -3) { close_modal(); return; }
    if (s_err_timer) return;

    if (key == -1) {
        if (s_len > 0) { s_len--; s_entered[s_len] = 0; }
        update_dots();
        return;
    }
    if (key == -2) {
        if (s_len < PIN_LEN) return;
        if (strcmp(s_entered, PIN_SECRET) == 0) {
            auto cb = s_success_cb;
            close_modal();
            if (cb) cb();
        } else {
            for (int i = 0; i < PIN_LEN; i++)
                if (s_dots[i]) lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(0xFF3333), 0);
            s_err_timer = lv_timer_create(err_timer_cb, 600, NULL);
            lv_timer_set_repeat_count(s_err_timer, 1);
        }
        return;
    }
    if (s_len < PIN_LEN) {
        s_entered[s_len++] = (char)('0' + key);
        s_entered[s_len]   = 0;
        update_dots();
    }
}

void ui_pin_show(std::function<void()> on_success) {
    if (s_overlay) return;
    s_success_cb = on_success;
    s_len = 0;
    memset(s_entered, 0, sizeof(s_entered));

    const int CW = 500, CH = 460;
    lv_display_t *disp = lv_display_get_default();
    int32_t sw = lv_display_get_horizontal_resolution(disp);
    int32_t sh = lv_display_get_vertical_resolution(disp);

    lv_obj_clear_flag(lv_layer_top(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(lv_layer_top(), LV_DIR_NONE);

    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_overlay, sw, sh);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(s_overlay);
    lv_obj_set_size(card, CW, CH);
    lv_obj_set_pos(card, (sw - CW) / 2, (sh - CH) / 2);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0D0D1A), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "CALIBRATION ACCESS");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *sub = lv_label_create(card);
    lv_label_set_text(sub, "Enter 6-digit PIN");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x445566), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 60);

    const int DOT = 36, DOT_GAP = 12;
    const int dots_w = PIN_LEN * DOT + (PIN_LEN - 1) * DOT_GAP;
    int dot_x0 = (CW - dots_w) / 2;
    for (int i = 0; i < PIN_LEN; i++) {
        s_dots[i] = lv_obj_create(card);
        lv_obj_set_size(s_dots[i], DOT, DOT);
        lv_obj_set_pos(s_dots[i], dot_x0 + i * (DOT + DOT_GAP), 88);
        lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(0x333355), 0);
        lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_dots[i], 0, 0);
        lv_obj_set_style_shadow_width(s_dots[i], 0, 0);
        lv_obj_clear_flag(s_dots[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    const int BW = 140, BH = 58, BGAP = 8;
    const int numpad_w = 3 * BW + 2 * BGAP;
    const int nx0 = (CW - numpad_w) / 2;
    const int ny0 = 140;

    const char *labels[12] = {"1","2","3","4","5","6","7","8","9","DEL","0","OK"};
    const int   keys[12]   = {  1,  2,  3,  4,  5,  6,  7,  8,  9,  -1,  0,  -2};

    for (int i = 0; i < 12; i++) {
        int row = i / 3, col = i % 3;
        lv_obj_t *btn = lv_btn_create(card);
        lv_obj_set_size(btn, BW, BH);
        lv_obj_set_pos(btn, nx0 + col * (BW + BGAP), ny0 + row * (BH + BGAP));
        lv_obj_set_style_bg_color(btn,
            lv_color_hex(keys[i] == -2 ? 0x005A5F : 0x13131F), 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x2A2A4A), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_set_style_text_color(lbl,
            lv_color_hex(keys[i] == -2 ? 0x00E5FF : 0xCCCCCC), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, on_key, LV_EVENT_CLICKED, (void *)(intptr_t)keys[i]);
    }

    lv_obj_t *cancel_btn = lv_btn_create(card);
    lv_obj_set_size(cancel_btn, 180, 38);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x1A0808), 0);
    lv_obj_set_style_border_color(cancel_btn, lv_color_hex(0x552222), 0);
    lv_obj_set_style_border_width(cancel_btn, 1, 0);
    lv_obj_set_style_radius(cancel_btn, 8, 0);
    lv_obj_set_style_shadow_width(cancel_btn, 0, 0);
    lv_obj_t *cl = lv_label_create(cancel_btn);
    lv_label_set_text(cl, "Cancel");
    lv_obj_set_style_text_color(cl, lv_color_hex(0x885555), 0);
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_24, 0);
    lv_obj_center(cl);
    lv_obj_add_event_cb(cancel_btn, on_key, LV_EVENT_CLICKED, (void *)(intptr_t)-3);
}
