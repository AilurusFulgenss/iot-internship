#include "ui_exec_detail.h"
#include "ui_exec.h"
#include "history.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <math.h>

static const char *TAG = "EXEC_DET";
lv_obj_t *scr_exec_detail = NULL;

// ── Bar chart card ────────────────────────────────────────────────────────────

typedef struct {
    lv_obj_t *card;
    lv_obj_t *lbl_title;
    lv_obj_t *lbl_current;
    lv_obj_t *lbl_unit;
    lv_obj_t *bars[7];
} chart_card_t;

// Layout A: 4 charts in 2×2 grid — for PM (si=0) and HHCC (si=1)
// Positions: (16,84),(366,84),(16,402),(366,402)  size: 338×306
static chart_card_t s_chart_a[4];

// Layout B: up to 2 charts stacked — for EC (si=2), ORP (si=3), LEAK (si=4), TH (si=5)
// Positions: (16,84),(16,402)  size: 688×306
static chart_card_t s_chart_b[2];

static lv_obj_t *s_lbl_hdr  = NULL;
static int       s_cur_si   = 0;

// ── Geometry constants ────────────────────────────────────────────────────────

#define CARD_PAD   12
#define CARD_RAD   14
#define INNER_H   266    // card height 290 − 2×12 pad
#define BAR_BOT   258    // INNER_H − 8 bottom gap

// Layout A bars (inner width 296px = 320 − 2×12)
#define A_BAR_W      38
#define A_BAR_GAP     4
#define A_START_X     3   // (296 − 7×38 − 6×4) / 2 = 3

// Layout B bars (inner width 630px = 654 − 2×12)
#define B_BAR_W      84
#define B_BAR_GAP     6
#define B_START_X     3   // (630 − 7×84 − 6×6) / 2 = 3

#define MAX_BAR_H   140

// ── Color helpers ─────────────────────────────────────────────────────────────

typedef uint32_t (*color_fn_t)(float);

static uint32_t col_pm25(float v)  { if(isnan(v))return 0x1A2A3Au; if(v<=12)return 0x00C853u; if(v<=35)return 0xFFD600u; if(v<=55)return 0xFF6D00u; return 0xCC0033u; }
static uint32_t col_pm10(float v)  { if(isnan(v))return 0x1A2A3Au; if(v<=54)return 0x00C853u; if(v<=154)return 0xFFD600u; return 0xCC0033u; }
static uint32_t col_temp(float v)  { if(isnan(v))return 0x1A2A3Au; if(v<18)return 0x1565C0u; if(v<27)return 0x00C853u; if(v<35)return 0xFFD600u; return 0xCC0033u; }
static uint32_t col_hum(float v)   { if(isnan(v))return 0x1A2A3Au; if(v<30||v>70)return 0xFF6D00u; return 0x00C853u; }
static uint32_t col_ec(float v)    { if(isnan(v))return 0x1A2A3Au; if(v<=300)return 0x00C853u; if(v<=500)return 0xFFD600u; return 0xCC0033u; }
static uint32_t col_tds(float v)   { if(isnan(v))return 0x1A2A3Au; if(v<=200)return 0x00C853u; if(v<=350)return 0xFFD600u; return 0xCC0033u; }
static uint32_t col_orp(float v)   { if(isnan(v))return 0x1A2A3Au; if(v<200)return 0xFF6D00u; if(v<=400)return 0x00C853u; return 0xFF6D00u; }
static uint32_t col_moist(float v) { if(isnan(v))return 0x1A2A3Au; if(v<20)return 0xFF6D00u; if(v<=70)return 0x00C853u; return 0x1565C0u; }
static uint32_t col_light(float v) { (void)v; return 0xFFAA44u; }
static uint32_t col_fert(float v)  { if(isnan(v))return 0x1A2A3Au; if(v<100)return 0xFF6D00u; return 0x00C853u; }
static uint32_t col_leak(float v)  { if(isnan(v)||v==0)return 0x00C853u; return 0xCC0033u; }

// ── Sensor tables ─────────────────────────────────────────────────────────────

// Layout A: sensor_idx 0=PM, 1=HHCC
static const char *A_TITLE[2][4] = {
    {"PM 2.5",    "PM 10",     "TEMPERATURE", "HUMIDITY"},
    {"MOISTURE",  "LIGHT",     "FERTILITY",   "TEMP"},
};
static const char *A_UNIT[2][4] = {
    {"ug/m3", "ug/m3", "\xc2\xb0""C", "%"},
    {"%",     "lux",  "uS",          "\xc2\xb0""C"},
};
static const int A_CH[2][4] = {
    {HIST_PM25,        HIST_PM10,        HIST_TEMP,      HIST_HUM},
    {HIST_HHCC_MOIST,  HIST_HHCC_LIGHT,  HIST_HHCC_FERT, HIST_HHCC_TEMP},
};
static const float A_CMAX[2][4] = {
    {150.0f,  300.0f,  50.0f,   100.0f},
    {100.0f,  5000.0f, 200.0f,  50.0f},
};
static const color_fn_t A_COL[2][4] = {
    {col_pm25,  col_pm10,  col_temp,  col_hum},
    {col_moist, col_light, col_fert,  col_temp},
};

// Layout B: b_idx 0=EC, 1=ORP, 2=TH, 3=LEAK
static const char *B_TITLE[4][2] = {
    {"EC",           "TDS"},
    {"ORP",          "TEMPERATURE"},
    {"TEMPERATURE",  "HUMIDITY"},
    {"ALARM EVENTS", ""},
};
static const char *B_UNIT[4][2] = {
    {"uS/cm", "mg/L"},
    {"mV",             "\xc2\xb0""C"},
    {"\xc2\xb0""C",    "%"},
    {"events/day",     ""},
};
static const int B_CH[4][2] = {
    {HIST_EC,      HIST_TDS},
    {HIST_ORP,     HIST_ORP_TEMP},
    {HIST_TH_TEMP, HIST_TH_HUM},
    {HIST_LEAK,    -1},
};
static const float B_CMAX[4][2] = {
    {1000.0f, 1500.0f},
    {600.0f,  50.0f},
    {50.0f,   100.0f},
    {10.0f,   0.0f},
};
static const color_fn_t B_COL[4][2] = {
    {col_ec,   col_tds},
    {col_orp,  col_temp},
    {col_temp, col_hum},
    {col_leak, NULL},
};

// sensor_idx → layout: 0=A, 1=B
static const int SENSOR_LAYOUT[6] = {0, 0, 1, 1, 1, 1};

// sensor_idx → number of charts in drill-down
static const int SENSOR_NCHARTS[6] = {4, 4, 2, 2, 1, 2};

// sensor_idx → b_idx (only valid for layout-B sensors)
static int s_to_b(int si) {
    switch (si) {
        case 2: return 0;  // EC
        case 3: return 1;  // ORP
        case 4: return 3;  // LEAK
        default: return 2; // TH (si=5)
    }
}

static const char *HDR_NAME[6] = {
    "PM2.5 - SN-300",
    "HHCC Flora",
    "EC / TDS",
    "ORP Sensor",
    "Leak Detector",
    "TH - CWT-TH04S",
};

// ── hist count helper ─────────────────────────────────────────────────────────

static int hist_count_ch(int ch) {
    if (ch < 0)                return 0;
    if (ch <= HIST_SOUND)      return g_hist_7d.count;
    if (ch <= HIST_TDS)        return g_hist_7d.cnt_ec;
    if (ch <= HIST_ORP_TEMP)   return g_hist_7d.cnt_orp;
    if (ch <= HIST_HHCC_TEMP)  return g_hist_7d.cnt_hhcc;
    if (ch <= HIST_TH_HUM)     return g_hist_7d.cnt_th;
    return g_hist_7d.cnt_leak;
}

// ── Card factory ──────────────────────────────────────────────────────────────

static void make_chart_card(chart_card_t *cc, lv_obj_t *parent,
                             int x, int y, int w, int h,
                             int bar_w, int bar_gap, int start_x)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0D0D1Cu), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, CARD_RAD, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x1A2A40u), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, CARD_PAD, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    cc->card = card;

    cc->lbl_title = lv_label_create(card);
    lv_label_set_text(cc->lbl_title, "");
    lv_obj_set_style_text_color(cc->lbl_title, lv_color_hex(0x4488AAu), 0);
    lv_obj_set_style_text_font(cc->lbl_title, &lv_font_montserrat_14, 0);
    lv_obj_align(cc->lbl_title, LV_ALIGN_TOP_LEFT, 0, 0);

    cc->lbl_current = lv_label_create(card);
    lv_label_set_text(cc->lbl_current, "--");
    lv_obj_set_style_text_color(cc->lbl_current, lv_color_hex(0x00C853u), 0);
    lv_obj_set_style_text_font(cc->lbl_current, &lv_font_montserrat_24, 0);
    lv_obj_align(cc->lbl_current, LV_ALIGN_TOP_RIGHT, 0, 0);

    cc->lbl_unit = lv_label_create(card);
    lv_label_set_text(cc->lbl_unit, "");
    lv_obj_set_style_text_color(cc->lbl_unit, lv_color_hex(0x334455u), 0);
    lv_obj_set_style_text_font(cc->lbl_unit, &lv_font_montserrat_14, 0);
    lv_obj_align(cc->lbl_unit, LV_ALIGN_TOP_RIGHT, 0, 28);

    // 7 bars (placeholder at minimum height, bottom-aligned)
    for (int i = 0; i < 7; i++) {
        lv_obj_t *bar = lv_obj_create(card);
        lv_obj_set_size(bar, bar_w, 4);
        lv_obj_set_pos(bar, start_x + i * (bar_w + bar_gap), BAR_BOT - 4);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x1A2A3Au), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        lv_obj_add_flag(bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
        cc->bars[i] = bar;
    }
}

// ── Bar refresh ───────────────────────────────────────────────────────────────

static void refresh_one(chart_card_t *cc,
                         int bar_w, int bar_gap, int start_x,
                         int hist_ch, float max_val, color_fn_t col_fn)
{
    if (!cc->card || hist_ch < 0) return;

    int n = hist_count_ch(hist_ch);

    for (int i = 0; i < 7; i++) {
        // Right-align bars: bar[6] = newest data point
        int di = n - (7 - i);
        bool has = (n > 0 && di >= 0 && di < HIST_7D_LEN);
        float v  = has ? g_hist_7d.d[hist_ch][di] : NAN;

        int      bh;
        uint32_t col;
        if (!has || isnan(v)) {
            bh  = 4;
            col = 0x1A2A3Au;
        } else {
            float ratio = (max_val > 0.0f) ? (v / max_val) : 0.0f;
            if (ratio > 1.0f) ratio = 1.0f;
            bh = (int)(ratio * MAX_BAR_H);
            if (bh < 4) bh = 4;
            col = col_fn ? col_fn(v) : 0x00C853u;
        }

        lv_obj_set_size(cc->bars[i], bar_w, bh);
        lv_obj_set_pos(cc->bars[i], start_x + i * (bar_w + bar_gap), BAR_BOT - bh);
        lv_obj_set_style_bg_color(cc->bars[i], lv_color_hex(col), 0);
    }

    // Update current value label from newest available point
    if (n > 0) {
        float v = g_hist_7d.d[hist_ch][n - 1];
        if (!isnan(v)) {
            char buf[16];
            // integers for counts/large values, one decimal otherwise
            if (v >= 100.0f || v == (int)v)
                snprintf(buf, sizeof(buf), "%.0f", v);
            else
                snprintf(buf, sizeof(buf), "%.1f", v);
            lv_label_set_text(cc->lbl_current, buf);
            if (col_fn) lv_obj_set_style_text_color(cc->lbl_current,
                            lv_color_hex(col_fn(v)), 0);
        }
    }
}

static void refresh_charts_for_sensor(int si)
{
    if (SENSOR_LAYOUT[si] == 0) {
        for (int slot = 0; slot < 4; slot++) {
            refresh_one(&s_chart_a[slot],
                        A_BAR_W, A_BAR_GAP, A_START_X,
                        A_CH[si][slot], A_CMAX[si][slot], A_COL[si][slot]);
        }
    } else {
        int b = s_to_b(si);
        int n = SENSOR_NCHARTS[si];
        for (int slot = 0; slot < n; slot++) {
            refresh_one(&s_chart_b[slot],
                        B_BAR_W, B_BAR_GAP, B_START_X,
                        B_CH[b][slot], B_CMAX[b][slot], B_COL[b][slot]);
        }
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void ui_exec_detail_create(void)
{
    ESP_LOGI(TAG, "creating EXEC detail screen...");

    scr_exec_detail = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_exec_detail, lv_color_hex(0x06060Fu), 0);
    lv_obj_set_style_bg_opa(scr_exec_detail, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr_exec_detail, 0, 0);

    // Header 72px
    lv_obj_t *hdr = lv_obj_create(scr_exec_detail);
    lv_obj_set_size(hdr, 720, 72);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x12121Eu), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 22, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *accent = lv_obj_create(hdr);
    lv_obj_set_size(accent, 720, 3);
    lv_obj_align(accent, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_color(accent, lv_color_hex(0xFFAA00u), 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_style_pad_all(accent, 0, 0);

    s_lbl_hdr = lv_label_create(hdr);
    lv_label_set_text(s_lbl_hdr, "7D HISTORY");
    lv_obj_set_style_text_color(s_lbl_hdr, lv_color_hex(0xFFAA00u), 0);
    lv_obj_set_style_text_font(s_lbl_hdr, &lv_font_montserrat_24, 0);
    lv_obj_align(s_lbl_hdr, LV_ALIGN_LEFT_MID, 0, 0);

    // ← EXEC back button
    lv_obj_t *btn = lv_btn_create(scr_exec_detail);
    lv_obj_set_size(btn, 88, 40);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -8, 12);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A1A2Eu), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x2C3D52u), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, [](lv_event_t *) {
        lv_scr_load_anim(scr_exec, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, LV_SYMBOL_LEFT " EXEC");
    lv_obj_set_style_text_color(btn_lbl, lv_color_hex(0x445566u), 0);
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(btn_lbl);

    // Layout A cards (2×2 grid, each 320×290, gap 18px col / 20px row)
    static const int AX[4] = { 31, 369,  31, 369};
    static const int AY[4] = { 84,  84, 394, 394};
    for (int i = 0; i < 4; i++) {
        make_chart_card(&s_chart_a[i], scr_exec_detail,
                        AX[i], AY[i], 320, 290,
                        A_BAR_W, A_BAR_GAP, A_START_X);
        lv_obj_add_flag(s_chart_a[i].card, LV_OBJ_FLAG_HIDDEN);
    }

    // Layout B cards (stacked, each 654×290, gap 20px row)
    static const int BY[2] = {84, 394};
    for (int i = 0; i < 2; i++) {
        make_chart_card(&s_chart_b[i], scr_exec_detail,
                        33, BY[i], 654, 290,
                        B_BAR_W, B_BAR_GAP, B_START_X);
        lv_obj_add_flag(s_chart_b[i].card, LV_OBJ_FLAG_HIDDEN);
    }

    ESP_LOGI(TAG, "EXEC detail screen created OK");
}

void ui_exec_detail_open(int sensor_idx)
{
    if (sensor_idx < 0 || sensor_idx > 5) return;
    s_cur_si = sensor_idx;

    // Update header
    char buf[48];
    snprintf(buf, sizeof(buf), "%s  |  7D", HDR_NAME[sensor_idx]);
    lv_label_set_text(s_lbl_hdr, buf);

    // Hide all cards
    for (int i = 0; i < 4; i++) lv_obj_add_flag(s_chart_a[i].card, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 2; i++) lv_obj_add_flag(s_chart_b[i].card, LV_OBJ_FLAG_HIDDEN);

    if (SENSOR_LAYOUT[sensor_idx] == 0) {
        // Layout A
        for (int slot = 0; slot < 4; slot++) {
            lv_label_set_text(s_chart_a[slot].lbl_title,   A_TITLE[sensor_idx][slot]);
            lv_label_set_text(s_chart_a[slot].lbl_unit,    A_UNIT[sensor_idx][slot]);
            lv_label_set_text(s_chart_a[slot].lbl_current, "--");
            lv_obj_clear_flag(s_chart_a[slot].card, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        // Layout B
        int b = s_to_b(sensor_idx);
        int n = SENSOR_NCHARTS[sensor_idx];
        for (int slot = 0; slot < n; slot++) {
            lv_label_set_text(s_chart_b[slot].lbl_title,   B_TITLE[b][slot]);
            lv_label_set_text(s_chart_b[slot].lbl_unit,    B_UNIT[b][slot]);
            lv_label_set_text(s_chart_b[slot].lbl_current, "--");
            lv_obj_clear_flag(s_chart_b[slot].card, LV_OBJ_FLAG_HIDDEN);
        }
    }

    refresh_charts_for_sensor(sensor_idx);
}

void ui_exec_detail_refresh(void)
{
    if (lv_scr_act() != scr_exec_detail) return;
    refresh_charts_for_sensor(s_cur_si);
}
