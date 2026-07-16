// Booking Confirm screen — keyboard input for title + organizer, POST /api/bookings

#include "ui_book_confirm.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "BOOK_CONFIRM";

#define API_URL  "http://192.168.1.105:5000/api/bookings"
#define HTTP_BUF 512

lv_obj_t *scr_book_confirm = NULL;

static void (*s_back_cb)(void) = NULL;
static void (*s_done_cb)(void) = NULL;

void ui_book_confirm_set_back_cb(void (*cb)(void)) { s_back_cb = cb; }
void ui_book_confirm_set_done_cb(void (*cb)(void)) { s_done_cb = cb; }

// ── State ─────────────────────────────────────────────────────────────────────

static char s_room_id[12]   = "";
static char s_room_name[40] = "";
static char s_start[6]      = "";
static char s_end[6]        = "";
static char s_title[64]     = "";
static char s_organizer[64] = "";
static bool s_posting       = false;

// ── Widgets ───────────────────────────────────────────────────────────────────

static lv_obj_t *lbl_conf_room;
static lv_obj_t *lbl_conf_time;
static lv_obj_t *lbl_conf_result;
static lv_obj_t *btn_confirm;
static lv_obj_t *lbl_confirm_btn;
static lv_obj_t *ta_title;
static lv_obj_t *ta_org;
static lv_obj_t *kb;

// ── Keyboard callbacks ────────────────────────────────────────────────────────

static void kb_hide(void)
{
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(btn_confirm, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(lbl_conf_result, "");
}

static void kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL)
        kb_hide();
}

// user_data holds the textarea pointer so we know which one to attach
static void ta_focus_cb(lv_event_t *e)
{
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_user_data(e);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(btn_confirm, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(lbl_conf_result, "");
}

// ── HTTP POST ──────────────────────────────────────────────────────────────────

static char s_buf[HTTP_BUF];
static int  s_len = 0;

static esp_err_t http_ev(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        int rem = HTTP_BUF - s_len - 1;
        int n   = evt->data_len < rem ? evt->data_len : rem;
        if (n > 0) { memcpy(s_buf + s_len, evt->data, n); s_len += n; }
    }
    return ESP_OK;
}

static void post_task(void *)
{
    // s_title, s_organizer already copied before task was spawned
    time_t now; struct tm t;
    time(&now); localtime_r(&now, &t);
    char today[12];
    snprintf(today, sizeof(today), "%04d-%02d-%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);

    char body[320];
    snprintf(body, sizeof(body),
             "{\"room_id\":\"%s\",\"title\":\"%s\","
             "\"organizer\":\"%s\","
             "\"date\":\"%s\",\"start\":\"%s\",\"end\":\"%s\"}",
             s_room_id, s_title, s_organizer, today, s_start, s_end);

    s_len = 0; memset(s_buf, 0, HTTP_BUF);

    esp_http_client_config_t cfg = {};
    cfg.url           = API_URL;
    cfg.event_handler = http_ev;
    cfg.timeout_ms    = 8000;
    cfg.method        = HTTP_METHOD_POST;

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_post_field(c, body, strlen(body));
    esp_err_t err  = esp_http_client_perform(c);
    int status     = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    s_posting = false;

    if (!bsp_display_lock(0)) { vTaskDelete(NULL); return; }

    if (err == ESP_OK && status == 201) {
        lv_label_set_text(lbl_conf_result, "Booked!");
        lv_obj_set_style_text_color(lbl_conf_result, lv_color_hex(0x00CC77), 0);
        lv_obj_add_flag(btn_confirm, LV_OBJ_FLAG_HIDDEN);
        bsp_display_unlock();

        vTaskDelay(pdMS_TO_TICKS(1500));
        if (bsp_display_lock(0)) {
            lv_label_set_text(lbl_conf_result, "");
            lv_obj_clear_flag(btn_confirm, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(lbl_confirm_btn, "CONFIRM");
            bsp_display_unlock();
        }
        if (s_done_cb) s_done_cb();
    } else {
        char errmsg[80] = "Booking failed";
        if (s_len > 0) {
            s_buf[s_len] = '\0';
            cJSON *root = cJSON_Parse(s_buf);
            if (root) {
                cJSON *ej = cJSON_GetObjectItem(root, "error");
                if (cJSON_IsString(ej))
                    snprintf(errmsg, sizeof(errmsg), "%s", ej->valuestring);
                cJSON_Delete(root);
            }
        }
        lv_label_set_text(lbl_conf_result, errmsg);
        lv_obj_set_style_text_color(lbl_conf_result, lv_color_hex(0xFF4444), 0);
        lv_label_set_text(lbl_confirm_btn, "CONFIRM");
        bsp_display_unlock();
    }
    vTaskDelete(NULL);
}

void ui_book_confirm_activate(const char *room_id, const char *room_name,
                               const char *start, const char *end)
{
    snprintf(s_room_id,   sizeof(s_room_id),   "%s", room_id);
    snprintf(s_room_name, sizeof(s_room_name), "%s", room_name);
    snprintf(s_start,     sizeof(s_start),     "%s", start);
    snprintf(s_end,       sizeof(s_end),       "%s", end);

    if (bsp_display_lock(0)) {
        lv_label_set_text(lbl_conf_room, room_name);
        char tbuf[20];
        snprintf(tbuf, sizeof(tbuf), "%s  -  %s", start, end);
        lv_label_set_text(lbl_conf_time, tbuf);
        lv_label_set_text(lbl_conf_result, "");
        lv_label_set_text(lbl_confirm_btn, "CONFIRM");
        lv_obj_clear_flag(btn_confirm, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(ta_title, "");  // clear title each time
        // keep organizer text so user can reuse their name
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        bsp_display_unlock();
    }
}

// ── Helper: styled textarea ───────────────────────────────────────────────────

static lv_obj_t *make_ta(lv_obj_t *parent, int y, const char *placeholder)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, 664, 56);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, y);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_max_length(ta, 60);

    lv_obj_set_style_bg_color(ta, lv_color_hex(0x12121E), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_14, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(0x2C3D52), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_radius(ta, 10, 0);
    lv_obj_set_style_pad_hor(ta, 14, 0);
    lv_obj_set_style_pad_ver(ta, 12, 0);
    // Highlighted border when focused
    lv_obj_set_style_border_color(ta, lv_color_hex(0x00E5FF), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ta, 2, LV_STATE_FOCUSED);
    return ta;
}

// ── Create ────────────────────────────────────────────────────────────────────

void ui_book_confirm_create(void)
{
    scr_book_confirm = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_book_confirm, lv_color_hex(0x080808), 0);
    lv_obj_set_style_bg_opa(scr_book_confirm, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr_book_confirm, 0, 0);

    // Header
    lv_obj_t *hdr = lv_obj_create(scr_book_confirm);
    lv_obj_set_size(hdr, 720, 72);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x12121E), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 22, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_hdr = lv_label_create(hdr);
    lv_label_set_text(title_hdr, "CONFIRM BOOKING");
    lv_obj_set_style_text_color(title_hdr, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(title_hdr, &lv_font_montserrat_24, 0);
    lv_obj_align(title_hdr, LV_ALIGN_LEFT_MID, 0, 0);

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
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " BACK");
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0x445566), 0);
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(back_lbl);

    // Accent line
    lv_obj_t *accent = lv_obj_create(scr_book_confirm);
    lv_obj_set_size(accent, 720, 3);
    lv_obj_set_pos(accent, 0, 72);
    lv_obj_set_style_bg_color(accent, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_style_pad_all(accent, 0, 0);
    lv_obj_set_style_radius(accent, 0, 0);

    // Room name
    lbl_conf_room = lv_label_create(scr_book_confirm);
    lv_label_set_text(lbl_conf_room, "---");
    lv_obj_set_style_text_color(lbl_conf_room, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_conf_room, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_conf_room, LV_ALIGN_TOP_LEFT, 28, 84);

    // Time slot (font_32 — compact but readable)
    lbl_conf_time = lv_label_create(scr_book_confirm);
    lv_label_set_text(lbl_conf_time, "--:--  -  --:--");
    lv_obj_set_style_text_color(lbl_conf_time, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(lbl_conf_time, &lv_font_montserrat_32, 0);
    lv_obj_align(lbl_conf_time, LV_ALIGN_TOP_LEFT, 28, 116);

    // ── Input fields ──────────────────────────────────────────────────────────

    lv_obj_t *lbl_ti = lv_label_create(scr_book_confirm);
    lv_label_set_text(lbl_ti, "Meeting Title");
    lv_obj_set_style_text_color(lbl_ti, lv_color_hex(0x445566), 0);
    lv_obj_set_style_text_font(lbl_ti, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_ti, LV_ALIGN_TOP_LEFT, 28, 168);

    ta_title = make_ta(scr_book_confirm, 188, "e.g. New Employee Seminar");
    lv_obj_add_event_cb(ta_title, ta_focus_cb, LV_EVENT_FOCUSED, ta_title);

    lv_obj_t *lbl_org = lv_label_create(scr_book_confirm);
    lv_label_set_text(lbl_org, "Organizer Name");
    lv_obj_set_style_text_color(lbl_org, lv_color_hex(0x445566), 0);
    lv_obj_set_style_text_font(lbl_org, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_org, LV_ALIGN_TOP_LEFT, 28, 258);

    ta_org = make_ta(scr_book_confirm, 278, "e.g. Kuda Visavaplanont");
    lv_obj_add_event_cb(ta_org, ta_focus_cb, LV_EVENT_FOCUSED, ta_org);

    // Result / error label
    lbl_conf_result = lv_label_create(scr_book_confirm);
    lv_label_set_text(lbl_conf_result, "");
    lv_obj_set_style_text_font(lbl_conf_result, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_conf_result, LV_ALIGN_TOP_MID, 0, 346);
    lv_label_set_long_mode(lbl_conf_result, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_conf_result, 620);

    // CONFIRM button
    btn_confirm = lv_btn_create(scr_book_confirm);
    lv_obj_set_size(btn_confirm, 400, 64);
    lv_obj_align(btn_confirm, LV_ALIGN_TOP_MID, 0, 368);
    lv_obj_set_style_bg_color(btn_confirm, lv_color_hex(0x006644), 0);
    lv_obj_set_style_bg_opa(btn_confirm, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn_confirm, lv_color_hex(0x009966), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_confirm, 14, 0);
    lv_obj_set_style_shadow_width(btn_confirm, 0, 0);
    lv_obj_set_style_border_width(btn_confirm, 0, 0);

    lbl_confirm_btn = lv_label_create(btn_confirm);
    lv_label_set_text(lbl_confirm_btn, "CONFIRM");
    lv_obj_set_style_text_color(lbl_confirm_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_confirm_btn, &lv_font_montserrat_32, 0);
    lv_obj_center(lbl_confirm_btn);

    lv_obj_add_event_cb(btn_confirm, [](lv_event_t *) {
        if (s_posting) return;
        // Read while in LVGL context (safe — this callback runs in LVGL task)
        const char *rt = lv_textarea_get_text(ta_title);
        const char *ro = lv_textarea_get_text(ta_org);
        snprintf(s_title,     sizeof(s_title),
                 "%s", (rt && rt[0]) ? rt : "Quick Booking");
        snprintf(s_organizer, sizeof(s_organizer),
                 "%s", (ro && ro[0]) ? ro : "LIV-24 Display");
        s_posting = true;
        lv_label_set_text(lbl_confirm_btn, "...");
        lv_label_set_text(lbl_conf_result, "");
        xTaskCreate(post_task, "bk_post", 6144, NULL, 2, NULL);
    }, LV_EVENT_CLICKED, NULL);

    // ── LVGL Keyboard — created last so it renders on top ────────────────────
    kb = lv_keyboard_create(scr_book_confirm);
    lv_obj_set_size(kb, 720, 300);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x0E0E1A), 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_CANCEL, NULL);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
}
