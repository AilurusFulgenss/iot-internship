#include "ui_alert.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static lv_obj_t *s_banner = NULL;
static lv_obj_t *s_lbl    = NULL;

static void msg_cat(char *dst, size_t sz, const char *src)
{
    if (dst[0] != '\0') strncat(dst, "     ", sz - strlen(dst) - 1);
    strncat(dst, src, sz - strlen(dst) - 1);
}

void ui_alert_init(void)
{
    // lv_layer_top() stays on top across all screen transitions
    s_banner = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_banner, 720, 52);
    lv_obj_set_pos(s_banner, 0, 0);
    lv_obj_set_style_radius(s_banner, 0, 0);
    lv_obj_set_style_border_width(s_banner, 0, 0);
    lv_obj_set_style_pad_all(s_banner, 0, 0);
    lv_obj_set_style_bg_opa(s_banner, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_banner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);

    s_lbl = lv_label_create(s_banner);
    lv_obj_set_style_text_font(s_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_lbl, lv_color_white(), 0);
    lv_label_set_long_mode(s_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_lbl, 680);
    lv_obj_align(s_lbl, LV_ALIGN_CENTER, 0, 0);
}

void ui_alert_check(float temp, float hum, float pm25, float pm10, float sound)
{
    if (!s_banner) return;

    char msg[256] = "";
    char part[64];
    int  sev = 0;   // 1=yellow  2=orange  3=red

    if (!isnan(temp) && temp > 35.0f) {
        snprintf(part, sizeof(part), "! TEMP %.1fC > 35", temp);
        msg_cat(msg, sizeof(msg), part);
        if (sev < 2) sev = 2;
    }
    if (!isnan(hum) && hum < 30.0f) {
        snprintf(part, sizeof(part), "! HUM %.0f%% < 30", hum);
        msg_cat(msg, sizeof(msg), part);
        if (sev < 1) sev = 1;
    }
    if (!isnan(pm25) && pm25 > 150.4f) {
        snprintf(part, sizeof(part), "! PM2.5 %.1f UNHEALTHY", pm25);
        msg_cat(msg, sizeof(msg), part);
        if (sev < 3) sev = 3;
    } else if (!isnan(pm25) && pm25 > 35.4f) {
        snprintf(part, sizeof(part), "! PM2.5 %.1f MODERATE", pm25);
        msg_cat(msg, sizeof(msg), part);
        if (sev < 2) sev = 2;
    }
    if (!isnan(pm10) && pm10 > 254.0f) {
        snprintf(part, sizeof(part), "! PM10 %.0f UNHEALTHY", pm10);
        msg_cat(msg, sizeof(msg), part);
        if (sev < 2) sev = 2;
    }

    if (msg[0] == '\0') {
        lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    static const uint32_t SEV_COL[4] = {0, 0xE6B800u, 0xFF6D00u, 0xCC0033u};
    lv_label_set_text(s_lbl, msg);
    lv_obj_set_style_bg_color(s_banner, lv_color_hex(SEV_COL[sev]), 0);
    lv_obj_clear_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
}
