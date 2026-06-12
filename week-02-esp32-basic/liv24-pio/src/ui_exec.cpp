#include "ui_exec.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "EXEC";

lv_obj_t *scr_exec = NULL;

// KPI widgets
static lv_obj_t *s_bar_up    = NULL;  static lv_obj_t *s_lbl_up  = NULL;
static lv_obj_t *s_bar_aq    = NULL;  static lv_obj_t *s_lbl_aq  = NULL;
static lv_obj_t *s_bar_pm10  = NULL;  static lv_obj_t *s_lbl_p10 = NULL;
static lv_obj_t *s_bar_noise = NULL;  static lv_obj_t *s_lbl_ns  = NULL;

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

// card + title + pct_label + subtitle + bar  (5 objects each)
static void make_kpi(lv_obj_t *parent, int x, int y,
                     const char *title, const char *sub,
                     uint32_t bar_col, int init_pct,
                     lv_obj_t **out_bar, lv_obj_t **out_pct)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 328, 240);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0D0D1C), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x252538), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    make_lbl(card, title, 0x6688AA, &lv_font_montserrat_14, LV_ALIGN_TOP_LEFT,  0,  0);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", init_pct);
    lv_obj_t *pl = make_lbl(card, buf, 0xEEEEEE, &lv_font_montserrat_48, LV_ALIGN_CENTER, 0, 0);
    if (out_pct) *out_pct = pl;

    make_lbl(card, sub, 0x334455, &lv_font_montserrat_14, LV_ALIGN_BOTTOM_LEFT, 0, -20);

    lv_obj_t *bar = lv_bar_create(card);
    lv_obj_set_size(bar, 288, 10);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(bar_col), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 5, 0);
    lv_obj_set_style_radius(bar, 5, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_bar_set_value(bar, init_pct, LV_ANIM_OFF);
    if (out_bar) *out_bar = bar;
}

// ── ui_exec_create ────────────────────────────────────────────────────────────
// 1 screen + 2 header labels + 1 gold bar + 4 × 5 KPI objects = 24 objects total
void ui_exec_create(void)
{
    ESP_LOGI(TAG, "creating EXEC (4 KPI cards)...");

    scr_exec = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_exec, lv_color_hex(0x06060F), 0);
    lv_obj_set_style_bg_opa(scr_exec, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr_exec, 0, 0);

    // header (2 labels + 1 gold bar = 3 obj)
    make_lbl(scr_exec, "KPI OVERVIEW",
             0xFFAA00, &lv_font_montserrat_32, LV_ALIGN_TOP_LEFT,  20, 14);
    make_lbl(scr_exec, "Hold 3s to exit",
             0x334455, &lv_font_montserrat_14, LV_ALIGN_TOP_RIGHT, -20, 22);

    lv_obj_t *sep = lv_obj_create(scr_exec);
    lv_obj_set_size(sep, 720, 3);
    lv_obj_set_pos(sep, 0, 60);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0xFFAA00), 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_pad_all(sep, 0, 0);

    // 4 KPI cards at (24,76), (368,76), (24,336), (368,336)
    // card 328×240, h-gap=16, v-gap=20, margins: top=76 bottom=24
    make_kpi(scr_exec,  24,  76, "SYSTEM UPTIME",       "target: 7-day continuous",     0x0099FF,   0, &s_bar_up,    &s_lbl_up);
    make_kpi(scr_exec, 368,  76, "AIR QUALITY (PM2.5)", "headroom to Sensitive groups", 0x00CC66, 100, &s_bar_aq,    &s_lbl_aq);
    make_kpi(scr_exec,  24, 336, "PM10 HEALTH",         "headroom to 100 \xce\xbcg/m\xc2\xb3", 0xFF9933, 100, &s_bar_pm10,  &s_lbl_p10);
    make_kpi(scr_exec, 368, 336, "NOISE SCORE",         "ambient noise level",          0xAA44FF, 100, &s_bar_noise, &s_lbl_ns);

    ESP_LOGI(TAG, "EXEC (24 obj) created OK");
}

// ── ui_exec_update ────────────────────────────────────────────────────────────
void ui_exec_update(float pm25, float pm10, int sound)
{
    if (!scr_exec) return;

    char buf[12];

    uint64_t up_s = esp_timer_get_time() / 1000000ULL;
    int up_pct = (int)((up_s * 100ULL) / 604800ULL);
    if (up_pct > 100) up_pct = 100;
    lv_bar_set_value(s_bar_up, up_pct, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%d%%", up_pct);
    lv_label_set_text(s_lbl_up, buf);

    int aq_pct = (int)((55.4f - pm25) / 55.4f * 100.0f);
    if (aq_pct < 0) aq_pct = 0;
    if (aq_pct > 100) aq_pct = 100;
    lv_bar_set_value(s_bar_aq, aq_pct, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%d%%", aq_pct);
    lv_label_set_text(s_lbl_aq, buf);

    int p10_pct = (int)((100.0f - pm10) / 100.0f * 100.0f);
    if (p10_pct < 0) p10_pct = 0;
    if (p10_pct > 100) p10_pct = 100;
    lv_bar_set_value(s_bar_pm10, p10_pct, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%d%%", p10_pct);
    lv_label_set_text(s_lbl_p10, buf);

    int ns_pct = 100 - sound;
    if (ns_pct < 0) ns_pct = 0;
    if (ns_pct > 100) ns_pct = 100;
    lv_bar_set_value(s_bar_noise, ns_pct, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%d%%", ns_pct);
    lv_label_set_text(s_lbl_ns, buf);
}
