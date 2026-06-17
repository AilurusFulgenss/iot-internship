#include "ui_user.h"
#include "eth_upload.h"
#include <stdio.h>

// ── IQAir AQI color palette ─────────────────────────────────────────────────
#define CLR_GOOD       0x009966u   // 0–12    μg/m³
#define CLR_MODERATE   0xFFDE33u   // 12–35.4
#define CLR_SENSITIVE  0xFF9933u   // 35.5–55.4
#define CLR_UNHEALTHY  0xCC0033u   // 55.5–150.4
#define CLR_VERY_UH    0x660099u   // 150.5–250.4
#define CLR_HAZARDOUS  0x7E0023u   // 250.5+
#define CLR_CARD       0x1A1A26u   // neutral card bg (no AQI color)

// ── Global screen handle ─────────────────────────────────────────────────────
lv_obj_t *scr_user = NULL;

// ── Runtime state ────────────────────────────────────────────────────────────
static bool  g_color_on  = true;
static float g_pm25_last = 0.0f;
static float g_pm10_last = 0.0f;

// Widget handles that need to be updated on each sensor read
static lv_obj_t *lbl_temp    = NULL;
static lv_obj_t *lbl_hum     = NULL;
static lv_obj_t *lbl_pm25    = NULL;
static lv_obj_t *lbl_pm10    = NULL;
static lv_obj_t *lbl_sound   = NULL;
static lv_obj_t *card_pm25   = NULL;
static lv_obj_t *card_pm10   = NULL;
static lv_obj_t *lbl_pm25_st    = NULL;
static lv_obj_t *lbl_pm10_st    = NULL;
static lv_obj_t *lbl_pm25_title = NULL;
static lv_obj_t *lbl_pm10_title = NULL;

// ── AQI helpers ──────────────────────────────────────────────────────────────

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

// Use dark text on bright BG (good/moderate), white text on dark BG
static uint32_t text_on_bg(uint32_t bg)
{
    return (bg == CLR_GOOD || bg == CLR_MODERATE) ? 0x111111u : 0xFFFFFFu;
}

// ── Apply/remove AQI colors ──────────────────────────────────────────────────

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

        if (lbl_pm25_st) lv_label_set_text(lbl_pm25_st, aqi_label_pm25(g_pm25_last));
        if (lbl_pm10_st) lv_label_set_text(lbl_pm10_st, aqi_label_pm10(g_pm10_last));
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

// ── Toggle switch callback ────────────────────────────────────────────────────

static void color_toggle_cb(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    g_color_on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    apply_pm_colors();
}

// ── Card builder helper ───────────────────────────────────────────────────────
//
//  Each card layout:
//    ┌─────────────────────────────┐
//    │ TITLE              (14 pt)  │
//    │                             │
//    │  value             (48 pt)  │
//    │                             │
//    │ STATUS ●          UNIT(14)  │
//    └─────────────────────────────┘
//
typedef struct {
    lv_obj_t *card;
    lv_obj_t *lbl_val;
    lv_obj_t *lbl_status;   // NULL unless has_status=true
    lv_obj_t *lbl_title;
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

    // Title label — top left
    lv_obj_t *lbl_t = lv_label_create(c);
    lv_label_set_text(lbl_t, title);
    lv_obj_set_style_text_color(lbl_t, lv_color_hex(0x7788AA), 0);
    lv_obj_set_style_text_font(lbl_t, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_t, LV_ALIGN_TOP_LEFT, 0, 0);

    // Value — large, vertically centred, nudged up a bit when status exists
    lv_obj_t *lbl_v = lv_label_create(c);
    lv_label_set_text(lbl_v, "--");
    lv_obj_set_style_text_color(lbl_v, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_v, &lv_font_montserrat_48, 0);
    lv_obj_align(lbl_v, LV_ALIGN_LEFT_MID, 0, has_status ? -10 : 0);

    // Unit — bottom right, small
    lv_obj_t *lbl_u = lv_label_create(c);
    lv_label_set_text(lbl_u, unit);
    lv_obj_set_style_text_color(lbl_u, lv_color_hex(0x4D5F78), 0);
    lv_obj_set_style_text_font(lbl_u, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_u, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    // Status badge — bottom left (PM cards only)
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
}

// ── ui_user_create ───────────────────────────────────────────────────────────

void ui_user_create(void)
{
    scr_user = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_user, lv_color_hex(0x0A0A12), 0);
    lv_obj_set_style_bg_opa(scr_user, LV_OPA_COVER, 0);

    // ── Header bar (fixed, 72 px) ─────────────────────────
    lv_obj_t *hdr = lv_obj_create(scr_user);
    lv_obj_set_size(hdr, 720, 72);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x12121E), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 22, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    // Cyan accent line at very top
    lv_obj_t *accent = lv_obj_create(hdr);
    lv_obj_set_size(accent, 720, 3);
    lv_obj_align(accent, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_color(accent, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_style_pad_all(accent, 0, 0);

    // Logo (left side, if available) — user-uploaded PNG from SPIFFS
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

    lv_obj_t *lbl_sub = lv_label_create(hdr);
    lv_label_set_text(lbl_sub, "IoT NODE");
    lv_obj_set_style_text_color(lbl_sub, lv_color_hex(0x3A4A5A), 0);
    lv_obj_set_style_text_font(lbl_sub, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_sub, LV_ALIGN_LEFT_MID, 112 + title_x, 12);

    // AQI Color toggle — right side of header
    lv_obj_t *lbl_sw = lv_label_create(hdr);
    lv_label_set_text(lbl_sw, "AQI Color");
    lv_obj_set_style_text_color(lbl_sw, lv_color_hex(0x556677), 0);
    lv_obj_set_style_text_font(lbl_sw, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_sw, LV_ALIGN_RIGHT_MID, -74, 0);

    lv_obj_t *sw = lv_switch_create(hdr);
    lv_obj_set_size(sw, 54, 28);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw,
        lv_color_hex(0x00E5FF), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, color_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // ── Scrollable card container (below header) ──────────
    //    Width 720, height 648 (= 720 - 72)
    //    Flex row-wrap, 16 px side padding, 12 px gap between columns/rows
    //    Card width per column = (720 - 16*2 - 12) / 2 = 338 px
    const int CW = 338;

    lv_obj_t *cont = lv_obj_create(scr_user);
    lv_obj_set_size(cont, 720, 648);
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_left(cont,   16, 0);
    lv_obj_set_style_pad_right(cont,  16, 0);
    lv_obj_set_style_pad_top(cont,    50, 0);
    lv_obj_set_style_pad_bottom(cont, 24, 0);
    lv_obj_set_style_pad_column(cont, 12, 0);
    lv_obj_set_style_pad_row(cont,    16, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(cont,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // ── Card 1: Temperature ───────────────────────────────
    card_out_t c = {};
    make_card(cont, CW, 160, "TEMPERATURE", "\xc2\xb0" "C", false, &c);
    lbl_temp = c.lbl_val;

    // ── Card 2: Humidity ──────────────────────────────────
    make_card(cont, CW, 160, "HUMIDITY", "%", false, &c);
    lbl_hum = c.lbl_val;

    // ── Card 3: PM2.5 (IQAir color) ──────────────────────
    make_card(cont, CW, 190, "PM 2.5", "ug/m3", true, &c);
    card_pm25       = c.card;
    lbl_pm25        = c.lbl_val;
    lbl_pm25_st     = c.lbl_status;
    lbl_pm25_title  = c.lbl_title;

    // ── Card 4: PM10 (IQAir color) ────────────────────────
    make_card(cont, CW, 190, "PM 10", "ug/m3", true, &c);
    card_pm10       = c.card;
    lbl_pm10        = c.lbl_val;
    lbl_pm10_st     = c.lbl_status;
    lbl_pm10_title  = c.lbl_title;

    // ── Card 5: Sound Level (full width) ──────────────────
    //    Full inner width = 720 - 32 = 688; spans both columns
    lv_obj_t *card_snd = lv_obj_create(cont);
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

    // Footer hint
    lv_obj_t *lbl_hint = lv_label_create(cont);
    lv_label_set_text(lbl_hint, "Hold 7s = Dev  |  Hold 10s = Exec");
    lv_obj_set_style_text_color(lbl_hint, lv_color_hex(0x222233), 0);
    lv_obj_set_style_text_font(lbl_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_width(lbl_hint, 688);
    lv_obj_set_style_text_align(lbl_hint, LV_TEXT_ALIGN_CENTER, 0);
}

// ── ui_user_update (called from sensor task with display lock held) ──────────

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
    if (lbl_pm25) lv_label_set_text(lbl_pm25, buf);

    snprintf(buf, sizeof(buf), "%.1f", pm10);
    if (lbl_pm10) lv_label_set_text(lbl_pm10, buf);

    snprintf(buf, sizeof(buf), "%d", sound);
    if (lbl_sound) lv_label_set_text(lbl_sound, buf);

    apply_pm_colors();
}
