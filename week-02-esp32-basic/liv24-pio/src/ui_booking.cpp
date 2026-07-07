// Building Picker screen — entry point of the booking flow
// GET /api/buildings → show 5 buildings A-E with available-room count

#include "ui_booking.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "BLD_PICKER";

#define API_URL  "http://192.168.1.105:5000/api/buildings"
#define HTTP_BUF 2048

lv_obj_t *scr_booking = NULL;

static void (*s_back_cb)(void)               = NULL;
static void (*s_select_cb)(const char *)     = NULL;

void ui_booking_set_back_cb(void (*cb)(void))             { s_back_cb   = cb; }
void ui_booking_set_select_cb(void (*cb)(const char *))   { s_select_cb = cb; }

static const char     *BLD_IDS[]    = {"M", "S", "E"};
static const char     *BLD_NAMES[]  = {"Meeting & Work", "Social & Events", "Entertainment"};
static const uint32_t  BLD_COLORS[] = {0x00AACC, 0xFF7744, 0x9966FF};

// 3 full-width stacked cards, each 664×150
static const int CARD_X[3] = {28, 28, 28};
static const int CARD_Y[3] = {108, 274, 440};

static lv_obj_t *s_avail_lbl[3];
static lv_obj_t *s_status_lbl;

// ── HTTP ──────────────────────────────────────────────────────────────────────

static char s_buf[HTTP_BUF];
static int  s_len = 0;
static bool s_fetching = false;

static esp_err_t http_ev(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        int rem = HTTP_BUF - s_len - 1;
        int n   = evt->data_len < rem ? evt->data_len : rem;
        if (n > 0) { memcpy(s_buf + s_len, evt->data, n); s_len += n; }
    }
    return ESP_OK;
}

static void fetch_task(void *)
{
    s_len = 0; memset(s_buf, 0, HTTP_BUF);

    esp_http_client_config_t cfg = {};
    cfg.url           = API_URL;
    cfg.event_handler = http_ev;
    cfg.timeout_ms    = 8000;

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_perform(c);
    esp_http_client_cleanup(c);
    s_fetching = false;

    if (err != ESP_OK || s_len == 0) {
        ESP_LOGW(TAG, "fetch failed: %s", esp_err_to_name(err));
        if (bsp_display_lock(0)) {
            lv_label_set_text(s_status_lbl, "No connection");
            bsp_display_unlock();
        }
        vTaskDelete(NULL);
        return;
    }

    s_buf[s_len] = '\0';
    cJSON *root = cJSON_Parse(s_buf);
    if (!root) { ESP_LOGW(TAG, "JSON error"); vTaskDelete(NULL); return; }

    char texts[3][24];
    for (int i = 0; i < 3; i++) snprintf(texts[i], 24, "- / - available");

    cJSON *item;
    cJSON_ArrayForEach(item, root) {
        cJSON *id_j  = cJSON_GetObjectItem(item, "id");
        cJSON *tot_j = cJSON_GetObjectItem(item, "rooms");
        cJSON *av_j  = cJSON_GetObjectItem(item, "available");
        if (!cJSON_IsString(id_j)) continue;
        for (int i = 0; i < 3; i++) {
            if (strcmp(id_j->valuestring, BLD_IDS[i]) == 0) {
                int tot = cJSON_IsNumber(tot_j) ? (int)tot_j->valuedouble : 0;
                int av  = cJSON_IsNumber(av_j)  ? (int)av_j->valuedouble  : 0;
                snprintf(texts[i], 24, "%d / %d available", av, tot);
            }
        }
    }
    cJSON_Delete(root);

    if (bsp_display_lock(0)) {
        for (int i = 0; i < 3; i++)
            lv_label_set_text(s_avail_lbl[i], texts[i]);
        lv_label_set_text(s_status_lbl, "");
        bsp_display_unlock();
    }
    vTaskDelete(NULL);
}

void ui_booking_activate(void)
{
    if (s_fetching) return;
    s_fetching = true;
    if (bsp_display_lock(0)) {
        for (int i = 0; i < 3; i++)
            lv_label_set_text(s_avail_lbl[i], "loading...");
        lv_label_set_text(s_status_lbl, "Fetching...");
        bsp_display_unlock();
    }
    xTaskCreate(fetch_task, "bld_fetch", 6144, NULL, 2, NULL);
}

// ── Create ────────────────────────────────────────────────────────────────────

void ui_booking_create(void)
{
    scr_booking = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_booking, lv_color_hex(0x080808), 0);
    lv_obj_set_style_bg_opa(scr_booking, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr_booking, 0, 0);

    // Header
    lv_obj_t *hdr = lv_obj_create(scr_booking);
    lv_obj_set_size(hdr, 720, 72);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x12121E), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 22, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "ROOM BOOKING");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *back_btn = lv_btn_create(hdr);
    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_align(back_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0x2C3D52), 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_radius(back_btn, 6, 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *) {
        if (s_back_cb) s_back_cb();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " HOME");
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0x445566), 0);
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(back_lbl);

    // Accent line — direct child to avoid hdr pad_hor clipping
    lv_obj_t *accent = lv_obj_create(scr_booking);
    lv_obj_set_size(accent, 720, 3);
    lv_obj_set_pos(accent, 0, 72);
    lv_obj_set_style_bg_color(accent, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_style_pad_all(accent, 0, 0);
    lv_obj_set_style_radius(accent, 0, 0);

    lv_obj_t *sub = lv_label_create(scr_booking);
    lv_label_set_text(sub, "SELECT CATEGORY");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x334455), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 28, 88);

    s_status_lbl = lv_label_create(scr_booking);
    lv_label_set_text(s_status_lbl, "");
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0x334455), 0);
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_BOTTOM_RIGHT, -28, -20);

    // Category cards — 3 full-width cards stacked
    for (int i = 0; i < 3; i++) {
        lv_obj_t *card = lv_obj_create(scr_booking);
        lv_obj_set_size(card, 664, 150);
        lv_obj_set_pos(card, CARD_X[i], CARD_Y[i]);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x12121E), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1A2A3A), LV_STATE_PRESSED);
        lv_obj_set_style_radius(card, 16, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0x1A2A3A), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

        // Left accent bar in category color
        lv_obj_t *bar = lv_obj_create(card);
        lv_obj_set_size(bar, 8, 150);
        lv_obj_set_pos(bar, 0, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(BLD_COLORS[i]), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

        // Category letter icon
        lv_obj_t *ltr = lv_label_create(card);
        char letter[2] = {BLD_IDS[i][0], 0};
        lv_label_set_text(ltr, letter);
        lv_obj_set_style_text_font(ltr, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(ltr, lv_color_hex(BLD_COLORS[i]), 0);
        lv_obj_align(ltr, LV_ALIGN_LEFT_MID, 28, 0);

        // Divider
        lv_obj_t *div = lv_obj_create(card);
        lv_obj_set_size(div, 1, 90);
        lv_obj_align(div, LV_ALIGN_LEFT_MID, 90, 0);
        lv_obj_set_style_bg_color(div, lv_color_hex(0x1C2C3C), 0);
        lv_obj_set_style_border_width(div, 0, 0);
        lv_obj_set_style_pad_all(div, 0, 0);

        // Category name
        lv_obj_t *name = lv_label_create(card);
        lv_label_set_text(name, BLD_NAMES[i]);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 106, -18);

        // Available count
        s_avail_lbl[i] = lv_label_create(card);
        lv_label_set_text(s_avail_lbl[i], "- / - available");
        lv_obj_set_style_text_font(s_avail_lbl[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_avail_lbl[i], lv_color_hex(0x445566), 0);
        lv_obj_align(s_avail_lbl[i], LV_ALIGN_LEFT_MID, 106, 14);

        // Arrow
        lv_obj_t *arr = lv_label_create(card);
        lv_label_set_text(arr, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(arr, lv_color_hex(BLD_COLORS[i]), 0);
        lv_obj_set_style_text_font(arr, &lv_font_montserrat_24, 0);
        lv_obj_align(arr, LV_ALIGN_RIGHT_MID, -20, 0);

        lv_obj_add_event_cb(card, [](lv_event_t *e) {
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            if (s_select_cb) s_select_cb(BLD_IDS[idx]);
        }, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}
