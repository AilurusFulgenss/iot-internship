// Room List screen — shows rooms in a selected building
// GET /api/rooms?building=X → up to 4 room cards with live status

#include "ui_room_list.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ROOM_LIST";

#define API_BASE "http://192.168.1.105:5000"
#define HTTP_BUF 4096
#define MAX_ROOMS 4

lv_obj_t *scr_room_list = NULL;

static void (*s_back_cb)(void)               = NULL;
static void (*s_select_cb)(const char *)     = NULL;

void ui_room_list_set_back_cb(void (*cb)(void))           { s_back_cb   = cb; }
void ui_room_list_set_select_cb(void (*cb)(const char *)) { s_select_cb = cb; }

// ── State ─────────────────────────────────────────────────────────────────────

static char s_building_id[4]          = "M";
static char s_room_ids[MAX_ROOMS][12] = {};
static int  s_room_count              = 0;
static bool s_fetching                = false;

// ── Widgets ───────────────────────────────────────────────────────────────────

static lv_obj_t *lbl_bld_title;
static lv_obj_t *lbl_page_status;

static lv_obj_t *room_card[MAX_ROOMS];
static lv_obj_t *lbl_rname[MAX_ROOMS];
static lv_obj_t *lbl_rinfo[MAX_ROOMS];
static lv_obj_t *lbl_rstatus[MAX_ROOMS];
static lv_obj_t *lbl_rmeeting[MAX_ROOMS];
static lv_obj_t *dot[MAX_ROOMS];

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
    snprintf(url, sizeof(url), API_BASE "/api/rooms?building=%s", s_building_id);

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
        if (bsp_display_lock(0)) {
            lv_label_set_text(lbl_page_status, "No connection");
            bsp_display_unlock();
        }
        vTaskDelete(NULL);
        return;
    }

    s_buf[s_len] = '\0';
    cJSON *root = cJSON_Parse(s_buf);
    if (!cJSON_IsArray(root)) {
        if (root) cJSON_Delete(root);
        vTaskDelete(NULL);
        return;
    }

    // Parse up to MAX_ROOMS rooms
    struct RoomData {
        char id[12]; char name[40]; char floor[6]; char cap[8];
        bool booked;
        char cur_title[40]; char cur_time[16];
        char nxt_title[40]; char nxt_time[16];
    };
    RoomData rooms[MAX_ROOMS] = {};
    int count = 0;

    cJSON *item;
    cJSON_ArrayForEach(item, root) {
        if (count >= MAX_ROOMS) break;
        RoomData &r = rooms[count];
        snprintf(r.id,    sizeof(r.id),    "%s", jstr(item, "id"));
        snprintf(r.name,  sizeof(r.name),  "%s", jstr(item, "name"));
        snprintf(r.floor, sizeof(r.floor), "%s", jstr(item, "floor"));
        int cap = 0;
        cJSON *cap_j = cJSON_GetObjectItem(item, "capacity");
        if (cJSON_IsNumber(cap_j)) cap = (int)cap_j->valuedouble;
        snprintf(r.cap, sizeof(r.cap), "cap %d", cap);

        const char *status = jstr(item, "status");
        r.booked = (strcmp(status, "booked") == 0);

        cJSON *cur = cJSON_GetObjectItem(item, "current");
        if (r.booked && cJSON_IsObject(cur)) {
            snprintf(r.cur_title, sizeof(r.cur_title), "%s", jstr(cur, "title"));
            snprintf(r.cur_time,  sizeof(r.cur_time),  "%s-%s",
                     jstr(cur, "start"), jstr(cur, "end"));
        } else {
            snprintf(r.cur_title, sizeof(r.cur_title), "Available now");
            r.cur_time[0] = '\0';
        }

        cJSON *nxt = cJSON_GetObjectItem(item, "next");
        if (cJSON_IsObject(nxt)) {
            snprintf(r.nxt_title, sizeof(r.nxt_title), "Next: %s", jstr(nxt, "title"));
            snprintf(r.nxt_time,  sizeof(r.nxt_time),  "%s", jstr(nxt, "start"));
        } else {
            snprintf(r.nxt_title, sizeof(r.nxt_title), "No upcoming");
            r.nxt_time[0] = '\0';
        }
        count++;
    }
    cJSON_Delete(root);

    if (bsp_display_lock(0)) {
        s_room_count = count;
        for (int i = 0; i < MAX_ROOMS; i++) {
            if (i < count) {
                snprintf(s_room_ids[i], sizeof(s_room_ids[i]), "%s", rooms[i].id);

                lv_label_set_text(lbl_rname[i], rooms[i].name);
                char info[24];
                snprintf(info, sizeof(info), "%s  ·  %s", rooms[i].floor, rooms[i].cap);
                lv_label_set_text(lbl_rinfo[i], info);

                if (rooms[i].booked) {
                    lv_obj_set_style_bg_color(dot[i], lv_color_hex(0xFF6644), 0);
                    lv_label_set_text(lbl_rstatus[i], "BOOKED");
                    lv_obj_set_style_text_color(lbl_rstatus[i], lv_color_hex(0xFF6644), 0);
                    lv_label_set_text(lbl_rmeeting[i], rooms[i].cur_title);
                } else {
                    lv_obj_set_style_bg_color(dot[i], lv_color_hex(0x00CC77), 0);
                    lv_label_set_text(lbl_rstatus[i], "AVAILABLE");
                    lv_obj_set_style_text_color(lbl_rstatus[i], lv_color_hex(0x00CC77), 0);
                    lv_label_set_text(lbl_rmeeting[i], rooms[i].nxt_title);
                }
                lv_obj_clear_flag(room_card[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(room_card[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        lv_label_set_text(lbl_page_status, "");
        bsp_display_unlock();
    }
    vTaskDelete(NULL);
}

void ui_room_list_activate(const char *building_id)
{
    snprintf(s_building_id, sizeof(s_building_id), "%s", building_id);
    s_room_count = 0;

    if (bsp_display_lock(0)) {
        // Map category ID to display name
        const char *cat_name = building_id;
        if (strcmp(building_id, "M") == 0)      cat_name = "MEETING & WORK";
        else if (strcmp(building_id, "S") == 0) cat_name = "SOCIAL & EVENTS";
        else if (strcmp(building_id, "E") == 0) cat_name = "ENTERTAINMENT";
        lv_label_set_text(lbl_bld_title, cat_name);
        lv_label_set_text(lbl_page_status, "Loading...");
        for (int i = 0; i < MAX_ROOMS; i++) {
            lv_label_set_text(lbl_rname[i], "...");
            lv_label_set_text(lbl_rstatus[i], "");
            lv_label_set_text(lbl_rmeeting[i], "");
            lv_obj_clear_flag(room_card[i], LV_OBJ_FLAG_HIDDEN);
        }
        bsp_display_unlock();
    }

    if (!s_fetching) {
        s_fetching = true;
        xTaskCreate(fetch_task, "room_list", 8192, NULL, 2, NULL);
    }
}

// ── Create ────────────────────────────────────────────────────────────────────

void ui_room_list_create(void)
{
    scr_room_list = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_room_list, lv_color_hex(0x080808), 0);
    lv_obj_set_style_bg_opa(scr_room_list, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr_room_list, 0, 0);

    // Header
    lv_obj_t *hdr = lv_obj_create(scr_room_list);
    lv_obj_set_size(hdr, 720, 72);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x12121E), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 22, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lbl_bld_title = lv_label_create(hdr);
    lv_label_set_text(lbl_bld_title, "---");
    lv_obj_set_style_text_color(lbl_bld_title, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(lbl_bld_title, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_bld_title, LV_ALIGN_LEFT_MID, 0, 0);

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
    lv_obj_t *accent = lv_obj_create(scr_room_list);
    lv_obj_set_size(accent, 720, 3);
    lv_obj_set_pos(accent, 0, 72);
    lv_obj_set_style_bg_color(accent, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_style_pad_all(accent, 0, 0);
    lv_obj_set_style_radius(accent, 0, 0);

    lbl_page_status = lv_label_create(scr_room_list);
    lv_label_set_text(lbl_page_status, "");
    lv_obj_set_style_text_color(lbl_page_status, lv_color_hex(0x334455), 0);
    lv_obj_set_style_text_font(lbl_page_status, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_page_status, LV_ALIGN_BOTTOM_RIGHT, -28, -20);

    // Room cards stacked vertically — y=88, step=158px (card h=148, gap=10)
    for (int i = 0; i < MAX_ROOMS; i++) {
        int cy = 88 + i * 158;

        lv_obj_t *card = lv_obj_create(scr_room_list);
        room_card[i] = card;
        lv_obj_set_size(card, 664, 148);
        lv_obj_align(card, LV_ALIGN_TOP_MID, 0, cy);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x12121E), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1A2A3A), LV_STATE_PRESSED);
        lv_obj_set_style_radius(card, 14, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0x1A2A3A), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_pad_all(card, 16, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

        // Status dot (top-left)
        dot[i] = lv_obj_create(card);
        lv_obj_set_size(dot[i], 12, 12);
        lv_obj_align(dot[i], LV_ALIGN_TOP_LEFT, 0, 4);
        lv_obj_set_style_bg_color(dot[i], lv_color_hex(0x334455), 0);
        lv_obj_set_style_radius(dot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot[i], 0, 0);
        lv_obj_set_style_pad_all(dot[i], 0, 0);

        // Room name
        lbl_rname[i] = lv_label_create(card);
        lv_label_set_text(lbl_rname[i], "...");
        lv_obj_set_style_text_font(lbl_rname[i], &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(lbl_rname[i], lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(lbl_rname[i], LV_ALIGN_TOP_LEFT, 22, 0);

        // Floor · capacity
        lbl_rinfo[i] = lv_label_create(card);
        lv_label_set_text(lbl_rinfo[i], "");
        lv_obj_set_style_text_font(lbl_rinfo[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_rinfo[i], lv_color_hex(0x445566), 0);
        lv_obj_align(lbl_rinfo[i], LV_ALIGN_TOP_LEFT, 22, 32);

        // Horizontal divider
        lv_obj_t *hdiv = lv_obj_create(card);
        lv_obj_set_size(hdiv, 600, 1);
        lv_obj_align(hdiv, LV_ALIGN_TOP_LEFT, 0, 56);
        lv_obj_set_style_bg_color(hdiv, lv_color_hex(0x1A2A3A), 0);
        lv_obj_set_style_border_width(hdiv, 0, 0);
        lv_obj_set_style_pad_all(hdiv, 0, 0);

        // Status badge (AVAILABLE / BOOKED)
        lbl_rstatus[i] = lv_label_create(card);
        lv_label_set_text(lbl_rstatus[i], "");
        lv_obj_set_style_text_font(lbl_rstatus[i], &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_rstatus[i], LV_ALIGN_TOP_RIGHT, 0, 0);

        // Current meeting / next info
        lbl_rmeeting[i] = lv_label_create(card);
        lv_label_set_text(lbl_rmeeting[i], "");
        lv_obj_set_style_text_font(lbl_rmeeting[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_rmeeting[i], lv_color_hex(0x778899), 0);
        lv_obj_align(lbl_rmeeting[i], LV_ALIGN_TOP_LEFT, 22, 68);

        // Arrow right
        lv_obj_t *arr = lv_label_create(card);
        lv_label_set_text(arr, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(arr, lv_color_hex(0x2A3A4A), 0);
        lv_obj_set_style_text_font(arr, &lv_font_montserrat_24, 0);
        lv_obj_align(arr, LV_ALIGN_RIGHT_MID, 0, 0);

        lv_obj_add_event_cb(card, [](lv_event_t *e) {
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            if (idx < s_room_count && s_select_cb)
                s_select_cb(s_room_ids[idx]);
        }, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}
