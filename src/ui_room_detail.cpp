// Room Detail screen — current status + available 30-min slot buttons
// GET /api/rooms/<room_id> → status card + scrollable slot grid

#include "ui_room_detail.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ROOM_DETAIL";

#define API_BASE "http://192.168.1.105:5000"
#define HTTP_BUF 4096
#define MAX_SLOTS 20

lv_obj_t *scr_room_detail = NULL;

static void (*s_back_cb)(void)                                                    = NULL;
static void (*s_slot_cb)(const char *, const char *, const char *, const char *) = NULL;

void ui_room_detail_set_back_cb(void (*cb)(void))         { s_back_cb = cb; }
void ui_room_detail_set_slot_cb(void (*cb)(const char *, const char *,
                                            const char *, const char *))
                                                          { s_slot_cb = cb; }

// ── State ─────────────────────────────────────────────────────────────────────

static char s_room_id[12]   = "A-M1";
static char s_room_name[40] = "";
static bool s_fetching      = false;
static lv_timer_t *s_refresh_timer = NULL;

struct Slot { char start[6]; char end[6]; };
static Slot   s_slots[MAX_SLOTS];
static int    s_slot_count = 0;

// ── Widgets ───────────────────────────────────────────────────────────────────

static lv_obj_t *lbl_room_title;
static lv_obj_t *lbl_room_floor;
static lv_obj_t *lbl_status_val;
static lv_obj_t *lbl_cur_title;
static lv_obj_t *lbl_cur_org;
static lv_obj_t *lbl_cur_time;
static lv_obj_t *lbl_slots_hdr;
static lv_obj_t *s_slot_roller;  // drum-roll slot picker
static lv_obj_t *s_btn_book;     // confirm button

static char s_roller_opts[MAX_SLOTS * 15 + 16] = {};

// ── HTTP ──────────────────────────────────────────────────────────────────────

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

static const char *jstr(cJSON *obj, const char *key)
{
    cJSON *j = cJSON_GetObjectItem(obj, key);
    return (cJSON_IsString(j) && j->valuestring) ? j->valuestring : "";
}

static void fetch_task(void *)
{
    char url[80];
    snprintf(url, sizeof(url), API_BASE "/api/rooms/%s", s_room_id);

    s_len = 0; memset(s_buf, 0, HTTP_BUF);

    esp_http_client_config_t cfg = {};
    cfg.url           = url;
    cfg.event_handler = http_ev;
    cfg.timeout_ms    = 8000;

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_perform(c);
    esp_http_client_cleanup(c);
    s_fetching = false;

    if (err != ESP_OK || s_len == 0) {
        ESP_LOGW(TAG, "fetch failed");
        vTaskDelete(NULL);
        return;
    }

    s_buf[s_len] = '\0';
    cJSON *root = cJSON_Parse(s_buf);
    if (!root) { vTaskDelete(NULL); return; }

    // Parse status
    bool booked = strcmp(jstr(root, "status"), "booked") == 0;

    char name[40]; snprintf(name, sizeof(name), "%s", jstr(root, "name"));
    char floor[8]; snprintf(floor, sizeof(floor), "%s", jstr(root, "floor"));

    char cur_title[48] = ""; char cur_org[40] = ""; char cur_time[20] = "";
    cJSON *cur = cJSON_GetObjectItem(root, "current");
    if (booked && cJSON_IsObject(cur)) {
        snprintf(cur_title, sizeof(cur_title), "%s", jstr(cur, "title"));
        snprintf(cur_org,   sizeof(cur_org),   "%s", jstr(cur, "organizer"));
        snprintf(cur_time,  sizeof(cur_time),  "%s - %s", jstr(cur, "start"), jstr(cur, "end"));
    }

    // Parse available slots
    cJSON *slots_j = cJSON_GetObjectItem(root, "slots");
    int   slot_count = 0;
    Slot  slots[MAX_SLOTS] = {};
    if (cJSON_IsArray(slots_j)) {
        cJSON *s;
        cJSON_ArrayForEach(s, slots_j) {
            if (slot_count >= MAX_SLOTS) break;
            snprintf(slots[slot_count].start, 6, "%s", jstr(s, "start"));
            snprintf(slots[slot_count].end,   6, "%s", jstr(s, "end"));
            slot_count++;
        }
    }

    cJSON_Delete(root);

    // Store for click callbacks (safe — fetch_task holds no lock yet)
    memcpy(s_slots, slots, sizeof(Slot) * slot_count);
    s_slot_count = slot_count;
    snprintf(s_room_name, sizeof(s_room_name), "%s", name);

    if (!bsp_display_lock(0)) { vTaskDelete(NULL); return; }

    lv_label_set_text(lbl_room_title, name);
    lv_label_set_text(lbl_room_floor, floor);

    if (booked) {
        lv_label_set_text(lbl_status_val, "BOOKED");
        lv_obj_set_style_text_color(lbl_status_val, lv_color_hex(0xFF6644), 0);
        lv_label_set_text(lbl_cur_title, cur_title);
        lv_label_set_text(lbl_cur_org,   cur_org);
        lv_label_set_text(lbl_cur_time,  cur_time);
        lv_obj_clear_flag(lbl_cur_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_cur_org,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_cur_time,  LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(lbl_status_val, "AVAILABLE");
        lv_obj_set_style_text_color(lbl_status_val, lv_color_hex(0x00CC77), 0);
        lv_obj_add_flag(lbl_cur_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_cur_org,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_cur_time,  LV_OBJ_FLAG_HIDDEN);
    }

    // Build roller options string (no lock needed — pure string work)
    s_roller_opts[0] = '\0';
    for (int i = 0; i < slot_count; i++) {
        char line[16];
        snprintf(line, sizeof(line), "%s - %s", slots[i].start, slots[i].end);
        if (i > 0) strncat(s_roller_opts, "\n", sizeof(s_roller_opts) - strlen(s_roller_opts) - 1);
        strncat(s_roller_opts, line, sizeof(s_roller_opts) - strlen(s_roller_opts) - 1);
    }

    // Update roller + book button
    if (slot_count == 0) {
        lv_roller_set_options(s_slot_roller, "No slots\navailable\ntoday", LV_ROLLER_MODE_NORMAL);
        lv_label_set_text(lbl_slots_hdr, "NO SLOTS AVAILABLE");
        lv_obj_add_flag(s_btn_book, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_roller_set_options(s_slot_roller, s_roller_opts, LV_ROLLER_MODE_NORMAL);
        lv_label_set_text(lbl_slots_hdr, "SELECT TIME SLOT");
        lv_obj_clear_flag(s_btn_book, LV_OBJ_FLAG_HIDDEN);
    }

    bsp_display_unlock();
    vTaskDelete(NULL);
}

// Auto-refresh: called by lv_timer every 60 s while screen is active
static void refresh_timer_cb(lv_timer_t *)
{
    if (lv_scr_act() != scr_room_detail) return;
    if (!s_fetching) {
        s_fetching = true;
        xTaskCreate(fetch_task, "room_det_r", 8192, NULL, 2, NULL);
    }
}

void ui_room_detail_activate(const char *room_id)
{
    if (room_id && room_id[0])
        snprintf(s_room_id, sizeof(s_room_id), "%s", room_id);
    s_slot_count = 0;

    if (bsp_display_lock(0)) {
        lv_label_set_text(lbl_room_title, s_room_id);
        lv_label_set_text(lbl_room_floor, "");
        lv_label_set_text(lbl_status_val, "---");
        lv_obj_set_style_text_color(lbl_status_val, lv_color_hex(0x334455), 0);
        lv_obj_add_flag(lbl_cur_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_cur_org,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_cur_time,  LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lbl_slots_hdr, "LOADING SLOTS...");
        lv_roller_set_options(s_slot_roller, "loading...", LV_ROLLER_MODE_NORMAL);
        lv_obj_add_flag(s_btn_book, LV_OBJ_FLAG_HIDDEN);
        // Reset + resume 60-second refresh timer
        lv_timer_reset(s_refresh_timer);
        lv_timer_resume(s_refresh_timer);
        bsp_display_unlock();
    }

    if (!s_fetching) {
        s_fetching = true;
        xTaskCreate(fetch_task, "room_det", 8192, NULL, 2, NULL);
    }
}

// ── Create ────────────────────────────────────────────────────────────────────

void ui_room_detail_create(void)
{
    scr_room_detail = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_room_detail, lv_color_hex(0x080808), 0);
    lv_obj_set_style_bg_opa(scr_room_detail, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr_room_detail, 0, 0);

    // Header
    lv_obj_t *hdr = lv_obj_create(scr_room_detail);
    lv_obj_set_size(hdr, 720, 72);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x12121E), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 22, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lbl_room_title = lv_label_create(hdr);
    lv_label_set_text(lbl_room_title, "---");
    lv_obj_set_style_text_color(lbl_room_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_room_title, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_room_title, LV_ALIGN_LEFT_MID, 0, 0);

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
        lv_timer_pause(s_refresh_timer);  // stop polling when leaving screen
        if (s_back_cb) s_back_cb();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " BACK");
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0x445566), 0);
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(back_lbl);

    // Accent line
    lv_obj_t *accent = lv_obj_create(scr_room_detail);
    lv_obj_set_size(accent, 720, 3);
    lv_obj_set_pos(accent, 0, 72);
    lv_obj_set_style_bg_color(accent, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_style_pad_all(accent, 0, 0);
    lv_obj_set_style_radius(accent, 0, 0);

    // Floor label (top-right under header)
    lbl_room_floor = lv_label_create(scr_room_detail);
    lv_label_set_text(lbl_room_floor, "");
    lv_obj_set_style_text_color(lbl_room_floor, lv_color_hex(0x445566), 0);
    lv_obj_set_style_text_font(lbl_room_floor, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_room_floor, LV_ALIGN_TOP_RIGHT, -28, 84);

    // Status card (y=82, h=160)
    lv_obj_t *status_card = lv_obj_create(scr_room_detail);
    lv_obj_set_size(status_card, 664, 160);
    lv_obj_align(status_card, LV_ALIGN_TOP_MID, 0, 82);
    lv_obj_set_style_bg_color(status_card, lv_color_hex(0x12121E), 0);
    lv_obj_set_style_bg_opa(status_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(status_card, 16, 0);
    lv_obj_set_style_border_color(status_card, lv_color_hex(0x1A2A3A), 0);
    lv_obj_set_style_border_width(status_card, 1, 0);
    lv_obj_set_style_pad_all(status_card, 20, 0);
    lv_obj_clear_flag(status_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *status_hdr = lv_label_create(status_card);
    lv_label_set_text(status_hdr, "STATUS");
    lv_obj_set_style_text_color(status_hdr, lv_color_hex(0x334455), 0);
    lv_obj_set_style_text_font(status_hdr, &lv_font_montserrat_14, 0);
    lv_obj_align(status_hdr, LV_ALIGN_TOP_LEFT, 0, 0);

    lbl_status_val = lv_label_create(status_card);
    lv_label_set_text(lbl_status_val, "---");
    lv_obj_set_style_text_color(lbl_status_val, lv_color_hex(0x334455), 0);
    lv_obj_set_style_text_font(lbl_status_val, &lv_font_montserrat_32, 0);
    lv_obj_align(lbl_status_val, LV_ALIGN_TOP_LEFT, 0, 20);

    lbl_cur_title = lv_label_create(status_card);
    lv_label_set_text(lbl_cur_title, "");
    lv_obj_set_style_text_color(lbl_cur_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_cur_title, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_cur_title, LV_ALIGN_TOP_LEFT, 0, 64);
    lv_obj_add_flag(lbl_cur_title, LV_OBJ_FLAG_HIDDEN);

    lbl_cur_org = lv_label_create(status_card);
    lv_label_set_text(lbl_cur_org, "");
    lv_obj_set_style_text_color(lbl_cur_org, lv_color_hex(0x445566), 0);
    lv_obj_set_style_text_font(lbl_cur_org, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_cur_org, LV_ALIGN_TOP_LEFT, 0, 84);
    lv_obj_add_flag(lbl_cur_org, LV_OBJ_FLAG_HIDDEN);

    lbl_cur_time = lv_label_create(status_card);
    lv_label_set_text(lbl_cur_time, "");
    lv_obj_set_style_text_color(lbl_cur_time, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(lbl_cur_time, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_cur_time, LV_ALIGN_TOP_RIGHT, 0, 64);
    lv_obj_add_flag(lbl_cur_time, LV_OBJ_FLAG_HIDDEN);

    // Slots section header (y=254)
    lbl_slots_hdr = lv_label_create(scr_room_detail);
    lv_label_set_text(lbl_slots_hdr, "AVAILABLE SLOTS");
    lv_obj_set_style_text_color(lbl_slots_hdr, lv_color_hex(0x334455), 0);
    lv_obj_set_style_text_font(lbl_slots_hdr, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_slots_hdr, LV_ALIGN_TOP_LEFT, 28, 256);

    // Slot roller (drum-roll picker)
    s_slot_roller = lv_roller_create(scr_room_detail);
    lv_roller_set_options(s_slot_roller, "loading...", LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_slot_roller, 5);
    lv_obj_set_width(s_slot_roller, 640);
    lv_obj_align(s_slot_roller, LV_ALIGN_TOP_MID, 0, 278);

    // Roller body (unselected rows)
    lv_obj_set_style_bg_color(s_slot_roller, lv_color_hex(0x0D1A26), 0);
    lv_obj_set_style_bg_opa(s_slot_roller, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_slot_roller, lv_color_hex(0x1A2A3A), 0);
    lv_obj_set_style_border_width(s_slot_roller, 1, 0);
    lv_obj_set_style_radius(s_slot_roller, 14, 0);
    lv_obj_set_style_text_color(s_slot_roller, lv_color_hex(0x334455), 0);
    lv_obj_set_style_text_font(s_slot_roller, &lv_font_montserrat_14, 0);

    // Selected row highlight
    lv_obj_set_style_bg_color(s_slot_roller, lv_color_hex(0x0A2840), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(s_slot_roller, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_color(s_slot_roller, lv_color_hex(0x00E5FF), LV_PART_SELECTED);
    lv_obj_set_style_text_font(s_slot_roller, &lv_font_montserrat_24, LV_PART_SELECTED);
    lv_obj_set_style_border_color(s_slot_roller, lv_color_hex(0x00E5FF), LV_PART_SELECTED);
    lv_obj_set_style_border_width(s_slot_roller, 1, LV_PART_SELECTED);

    // BOOK button
    s_btn_book = lv_btn_create(scr_room_detail);
    lv_obj_set_size(s_btn_book, 440, 60);
    lv_obj_align(s_btn_book, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_style_bg_color(s_btn_book, lv_color_hex(0x00AA55), 0);
    lv_obj_set_style_bg_color(s_btn_book, lv_color_hex(0x008844), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(s_btn_book, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_btn_book, 12, 0);
    lv_obj_set_style_shadow_width(s_btn_book, 0, 0);
    lv_obj_add_flag(s_btn_book, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_btn_book, [](lv_event_t *) {
        uint16_t sel = lv_roller_get_selected(s_slot_roller);
        if (sel < (uint16_t)s_slot_count && s_slot_cb)
            s_slot_cb(s_room_id, s_room_name, s_slots[sel].start, s_slots[sel].end);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *book_lbl = lv_label_create(s_btn_book);
    lv_label_set_text(book_lbl, "BOOK THIS SLOT");
    lv_obj_set_style_text_color(book_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(book_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(book_lbl);

    // Auto-refresh timer — starts paused, activated by _activate()
    s_refresh_timer = lv_timer_create(refresh_timer_cb, 60000, NULL);
    lv_timer_pause(s_refresh_timer);
}
