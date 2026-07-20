#include "ui_home.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include "eth_upload.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <string.h>
#include <math.h>

static const char *TAG = "HOME";

lv_obj_t *scr_home = NULL;

static lv_obj_t *lbl_clock    = NULL;
static lv_obj_t *lbl_greeting = NULL;
static lv_obj_t *lbl_weather  = NULL;
static lv_obj_t *lbl_pm25_h   = NULL;
static lv_obj_t *lbl_temp_h   = NULL;
static lv_obj_t *lbl_hum_h    = NULL;

// Room status card — configure per device
#define ROOM_STATUS_ID   "M-MTG1"
#define ROOM_STATUS_NAME "Meeting Room 1"
#define HA_BASE_URL_DEFAULT "http://10.24.1.104:8123"
#define HA_CALENDAR_ID   "calendar.meeting_room_1"
#define HA_TOKEN         "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiI0YTg1NTFiNTcwZWM0NmQ3OWE3YmVlMDYyYjg5YmU2ZSIsImlhdCI6MTc4MzM5MjMyMSwiZXhwIjoyMDk4NzUyMzIxfQ.TgtuxlkOBuvaDkN_dGVD83ehKfWDpfbJ0iVugpBPOIY"
#define RS_BUF           4096

static lv_obj_t *s_rs_status_lbl    = NULL;
static lv_obj_t *s_rs_topic_lbl     = NULL;
static lv_obj_t *s_rs_organizer_lbl = NULL;
static lv_obj_t *s_rs_next_lbl      = NULL;
static lv_obj_t *s_rs_nexttitle_lbl = NULL;

static lv_obj_t *s_emoji_face = NULL;
static lv_obj_t *s_emoji_eye1 = NULL;
static lv_obj_t *s_emoji_eye2 = NULL;

static void (*s_card1_cb)(void) = NULL;
static void (*s_card2_cb)(void) = NULL;
void ui_home_set_card1_cb(void (*cb)(void)) { s_card1_cb = cb; }
void ui_home_set_card2_cb(void (*cb)(void)) { s_card2_cb = cb; }

// ─── Emoji face ───────────────────────────────────────────────

static void emoji_update(int hour)
{
    if (!s_emoji_face) return;
    lv_color_t fc, ec;
    if      (hour >= 5  && hour < 12) { fc = lv_color_hex(0xFFCC00); ec = lv_color_hex(0x443300); }
    else if (hour >= 12 && hour < 17) { fc = lv_color_hex(0xFF8C00); ec = lv_color_hex(0x220000); }
    else if (hour >= 17 && hour < 22) { fc = lv_color_hex(0xFF7755); ec = lv_color_hex(0x331111); }
    else                              { fc = lv_color_hex(0x334488); ec = lv_color_hex(0xBBCCEE); }
    lv_obj_set_style_bg_color(s_emoji_face, fc, 0);
    lv_obj_set_style_bg_color(s_emoji_eye1, ec, 0);
    lv_obj_set_style_bg_color(s_emoji_eye2, ec, 0);
}

// ─── Weather color ────────────────────────────────────────────

static const char *weather_color_str(const char *cond)
{
    if (!cond || !cond[0]) return "556677";
    if (strstr(cond, "Clear"))       return "FFD700";
    if (strstr(cond, "Cloud"))       return "99AABB";
    if (strstr(cond, "Rain"))        return "4488FF";
    if (strstr(cond, "Drizzle"))     return "4488FF";
    if (strstr(cond, "Thunder"))     return "FF8833";
    if (strstr(cond, "Snow"))        return "BBDDFF";
    return "889999";  // Mist/Fog/Haze/Smoke
}

// ─── Time ─────────────────────────────────────────────────────

static const char *greeting_for(int hour)
{
    if (hour >= 5  && hour < 12) return "Good morning";
    if (hour >= 12 && hour < 17) return "Good afternoon";
    if (hour >= 17 && hour < 22) return "Good evening";
    return "Good night";
}

static void clock_timer_cb(lv_timer_t *)
{
    time_t now;
    struct tm t;
    time(&now);
    localtime_r(&now, &t);
    if (t.tm_year < 100) return; // NTP not synced yet (year < 2000)

    static const char *days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    char buf[24];
    snprintf(buf, sizeof(buf), "%s  %02d:%02d", days[t.tm_wday], t.tm_hour, t.tm_min);
    lv_label_set_text(lbl_clock, buf);
    lv_label_set_text(lbl_greeting, greeting_for(t.tm_hour));
    lv_obj_align_to(s_emoji_face, lbl_greeting, LV_ALIGN_OUT_RIGHT_MID, 14, 0);
    emoji_update(t.tm_hour);
}

// ─── Weather ──────────────────────────────────────────────────

#define OW_URL "http://api.openweathermap.org/data/2.5/weather?q=Bangkok,TH&appid=d21bc4dea21e625cf204a47a89f13b11&units=metric"
#define OW_BUF 1024

static char s_ow_buf[OW_BUF];
static int  s_ow_len = 0;

static esp_err_t ow_http_cb(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        int rem = OW_BUF - s_ow_len - 1;
        if (rem > 0) {
            int n = evt->data_len < rem ? evt->data_len : rem;
            memcpy(s_ow_buf + s_ow_len, evt->data, n);
            s_ow_len += n;
        }
    }
    return ESP_OK;
}

static void weather_task(void *)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(12000)); // initial delay — let network settle

        s_ow_len = 0;
        memset(s_ow_buf, 0, OW_BUF);

        esp_http_client_config_t cfg = {};
        cfg.url           = OW_URL;
        cfg.event_handler = ow_http_cb;
        cfg.timeout_ms    = 8000;

        esp_http_client_handle_t c = esp_http_client_init(&cfg);
        esp_err_t err = esp_http_client_perform(c);
        esp_http_client_cleanup(c);

        if (err == ESP_OK && s_ow_len > 0) {
            s_ow_buf[s_ow_len] = '\0';
            cJSON *root = cJSON_Parse(s_ow_buf);
            if (root) {
                float      temp = NAN;
                const char *cond = "";

                cJSON *main_j = cJSON_GetObjectItem(root, "main");
                if (cJSON_IsObject(main_j)) {
                    cJSON *tj = cJSON_GetObjectItem(main_j, "temp");
                    if (cJSON_IsNumber(tj)) temp = (float)tj->valuedouble;
                }
                cJSON *weather_j = cJSON_GetObjectItem(root, "weather");
                if (cJSON_IsArray(weather_j) && cJSON_GetArraySize(weather_j) > 0) {
                    cJSON *w = cJSON_GetArrayItem(weather_j, 0);
                    cJSON *mj = cJSON_GetObjectItem(w, "main");
                    if (cJSON_IsString(mj)) cond = mj->valuestring;
                }

                char wbuf[64];
                if (!isnan(temp))
                    snprintf(wbuf, sizeof(wbuf), "Bangkok  %.0f\xC2\xB0""C   #%s %s#",
                             temp, weather_color_str(cond), cond);
                else
                    snprintf(wbuf, sizeof(wbuf), "Bangkok  --   #%s %s#",
                             weather_color_str(cond), cond);

                if (bsp_display_lock(0)) {
                    lv_label_set_text(lbl_weather, wbuf);
                    bsp_display_unlock();
                }
                ESP_LOGI(TAG, "Weather: %s", wbuf);
                cJSON_Delete(root);
            }
        } else {
            ESP_LOGW(TAG, "Weather fetch failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(588000)); // ~10 min until next fetch
    }
}

// ─── Room status fetch ────────────────────────────────────────

static char s_rs_buf[RS_BUF];
static int  s_rs_len = 0;

static esp_err_t rs_http_cb(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        int rem = RS_BUF - s_rs_len - 1;
        if (rem > 0) {
            int n = evt->data_len < rem ? evt->data_len : rem;
            memcpy(s_rs_buf + s_rs_len, evt->data, n);
            s_rs_len += n;
        }
    }
    return ESP_OK;
}

static void room_status_task(void *)
{
    ESP_LOGI(TAG, "room_status_task started — waiting 15s for NTP");
    vTaskDelay(pdMS_TO_TICKS(15000)); // wait for network + NTP

    char ha_base_url[72] = HA_BASE_URL_DEFAULT;
    {
        nvs_handle_t h;
        if (nvs_open("ha_cfg", NVS_READONLY, &h) == ESP_OK) {
            size_t len = sizeof(ha_base_url);
            nvs_get_str(h, "url", ha_base_url, &len);
            nvs_close(h);
        }
    }
    ESP_LOGI(TAG, "HA base URL: %s", ha_base_url);

    for (;;) {
        time_t now_t;
        struct tm t;
        time(&now_t);
        localtime_r(&now_t, &t);

        if (t.tm_year < 100) { // NTP not ready yet
            ESP_LOGW(TAG, "NTP not ready (year=%d), retry in 5s", t.tm_year + 1900);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        // Build HA Calendar API URL with today's date range
        char date[11];
        strftime(date, sizeof(date), "%Y-%m-%d", &t);
        char url[256];
        snprintf(url, sizeof(url),
            "%s/api/calendars/" HA_CALENDAR_ID
            "?start=%sT00:00:00%%2B07:00&end=%sT23:59:59%%2B07:00",
            ha_base_url, date, date);

        s_rs_len = 0; memset(s_rs_buf, 0, RS_BUF);
        esp_http_client_config_t cfg = {};
        cfg.url           = url;
        cfg.event_handler = rs_http_cb;
        cfg.timeout_ms    = 8000;

        ESP_LOGI(TAG, "Fetching calendar: %s", url);
        esp_http_client_handle_t c = esp_http_client_init(&cfg);
        esp_http_client_set_header(c, "Authorization", "Bearer " HA_TOKEN);
        esp_err_t err = esp_http_client_perform(c);
        int http_status = esp_http_client_get_status_code(c);
        esp_http_client_cleanup(c);

        ESP_LOGI(TAG, "Calendar HTTP %d len=%d err=%s", http_status, s_rs_len, esp_err_to_name(err));

        if (err == ESP_OK && s_rs_len > 0) {
            s_rs_buf[s_rs_len] = '\0';
            cJSON *root = cJSON_Parse(s_rs_buf);
            if (root && cJSON_IsArray(root)) {
                int now_min = t.tm_hour * 60 + t.tm_min;

                bool booked        = false;
                char topic[64]     = "";
                char organizer[64] = "";
                char cur_time[32]  = "";
                char next_time[32] = "";
                bool found_next    = false;

                cJSON *ev;
                cJSON_ArrayForEach(ev, root) {
                    cJSON *s_obj = cJSON_GetObjectItem(ev, "start");
                    cJSON *e_obj = cJSON_GetObjectItem(ev, "end");
                    cJSON *summ  = cJSON_GetObjectItem(ev, "summary");
                    cJSON *desc  = cJSON_GetObjectItem(ev, "description");
                    if (!cJSON_IsObject(s_obj) || !cJSON_IsObject(e_obj)) continue;

                    cJSON *s_dt = cJSON_GetObjectItem(s_obj, "dateTime");
                    cJSON *e_dt = cJSON_GetObjectItem(e_obj, "dateTime");
                    if (!cJSON_IsString(s_dt) || !cJSON_IsString(e_dt)) continue;

                    // Parse HH:MM from "2026-07-07T14:00:00+07:00"
                    const char *s_t_ptr = strchr(s_dt->valuestring, 'T');
                    const char *e_t_ptr = strchr(e_dt->valuestring, 'T');
                    if (!s_t_ptr || !e_t_ptr) continue;

                    int sh = atoi(s_t_ptr + 1), sm = atoi(s_t_ptr + 4);
                    int eh = atoi(e_t_ptr + 1), em = atoi(e_t_ptr + 4);
                    int start_min = sh * 60 + sm;
                    int end_min   = eh * 60 + em;

                    if (start_min <= now_min && now_min < end_min) {
                        // Current booking
                        booked = true;
                        snprintf(cur_time, sizeof(cur_time), "%02d:%02d-%02d:%02d",
                                 sh, sm, eh, em);
                        if (cJSON_IsString(summ))
                            snprintf(topic, sizeof(topic), "%s", summ->valuestring);
                        if (cJSON_IsString(desc) && desc->valuestring[0])
                            snprintf(organizer, sizeof(organizer), "%s", desc->valuestring);
                    } else if (start_min > now_min && !found_next) {
                        // Next upcoming booking (earliest after now)
                        snprintf(next_time, sizeof(next_time), "%02d:%02d-%02d:%02d",
                                 sh, sm, eh, em);
                        found_next = true;
                    }
                }
                cJSON_Delete(root);

                if (bsp_display_lock(0)) {
                    if (booked && found_next) {
                        // Case 4: Booked + has next — next time in header, BOOKED+time in body
                        char next_hdr[48];
                        snprintf(next_hdr, sizeof(next_hdr), "Next  %s", next_time);
                        lv_label_set_text(s_rs_status_lbl, next_hdr);
                        lv_obj_set_style_text_color(s_rs_status_lbl, lv_color_hex(0xFFCC44), 0);
                        char booked_str[48];
                        snprintf(booked_str, sizeof(booked_str), "BOOKED  %s", cur_time);
                        lv_label_set_text(s_rs_topic_lbl, booked_str);
                        lv_obj_set_style_text_color(s_rs_topic_lbl, lv_color_hex(0xFF4444), 0);
                        lv_obj_set_style_text_font(s_rs_topic_lbl, &lv_font_montserrat_24, 0);
                        lv_label_set_text(s_rs_organizer_lbl, topic);
                        lv_obj_align(s_rs_organizer_lbl, LV_ALIGN_TOP_LEFT, 0, 112);
                        lv_label_set_text(s_rs_nexttitle_lbl, "");
                        lv_label_set_text(s_rs_next_lbl, "");
                    } else if (booked && !found_next) {
                        // Case 3: Booked, no next — show current booking time below
                        lv_label_set_text(s_rs_status_lbl, "BOOKED");
                        lv_obj_set_style_text_color(s_rs_status_lbl, lv_color_hex(0xFF4444), 0);
                        lv_label_set_text(s_rs_topic_lbl, topic);
                        lv_obj_set_style_text_color(s_rs_topic_lbl, lv_color_hex(0xFFFFFF), 0);
                        lv_obj_set_style_text_font(s_rs_topic_lbl, &lv_font_montserrat_24, 0);
                        lv_label_set_text(s_rs_organizer_lbl, organizer);
                        lv_obj_align(s_rs_organizer_lbl, LV_ALIGN_TOP_LEFT, 0, 112);
                        lv_label_set_text(s_rs_nexttitle_lbl, "");
                        lv_label_set_text(s_rs_next_lbl, cur_time);
                        lv_obj_set_style_text_font(s_rs_next_lbl, &lv_font_montserrat_24, 0);
                        lv_obj_set_style_text_color(s_rs_next_lbl, lv_color_hex(0xFF4444), 0);
                    } else if (!booked && found_next) {
                        // Case 2: Available + has next — next time in header
                        char next_hdr[48];
                        snprintf(next_hdr, sizeof(next_hdr), "Next  %s", next_time);
                        lv_label_set_text(s_rs_status_lbl, next_hdr);
                        lv_obj_set_style_text_color(s_rs_status_lbl, lv_color_hex(0xFFCC44), 0);
                        lv_label_set_text(s_rs_topic_lbl, "AVAILABLE");
                        lv_obj_set_style_text_color(s_rs_topic_lbl, lv_color_hex(0x00CC66), 0);
                        lv_obj_set_style_text_font(s_rs_topic_lbl, &lv_font_montserrat_24, 0);
                        lv_label_set_text(s_rs_organizer_lbl, "");
                        lv_label_set_text(s_rs_nexttitle_lbl, "");
                        lv_label_set_text(s_rs_next_lbl, "");
                    } else {
                        // Case 1: Available, no next event
                        lv_label_set_text(s_rs_status_lbl, "");
                        lv_label_set_text(s_rs_topic_lbl, "AVAILABLE");
                        lv_obj_set_style_text_color(s_rs_topic_lbl, lv_color_hex(0x00CC66), 0);
                        lv_obj_set_style_text_font(s_rs_topic_lbl, &lv_font_montserrat_24, 0);
                        lv_label_set_text(s_rs_organizer_lbl, "");
                        lv_label_set_text(s_rs_nexttitle_lbl, "");
                        lv_label_set_text(s_rs_next_lbl, "");
                    }
                    bsp_display_unlock();
                }
                ESP_LOGI(TAG, "Room %s: %s | next: %s", ROOM_STATUS_ID,
                         booked ? "BOOKED" : "AVAILABLE", next_time);
            } else {
                ESP_LOGW(TAG, "Response not array: %.200s", s_rs_buf);
            }
        } else {
            ESP_LOGW(TAG, "HA calendar fetch failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(30000)); // refresh every 30s
    }
}

// ─── Create ───────────────────────────────────────────────────

void ui_home_create(void)
{
    scr_home = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_home, lv_color_hex(0x080808), 0);
    lv_obj_set_style_bg_opa(scr_home, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr_home, 0, 0);

    // ── Header (72px) ─────────────────────────────────────────
    lv_obj_t *hdr = lv_obj_create(scr_home);
    lv_obj_set_size(hdr, 720, 72);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x12121E), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 22, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    // accent as direct child of scr_home — avoids hdr pad_hor clipping
    lv_obj_t *accent = lv_obj_create(scr_home);
    lv_obj_set_size(accent, 720, 3);
    lv_obj_set_pos(accent, 0, 72);
    lv_obj_set_style_bg_color(accent, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_style_pad_all(accent, 0, 0);
    lv_obj_set_style_radius(accent, 0, 0);

    int title_x = 0;
    if (eth_upload_has_logo()) {
        lv_obj_t *logo = lv_image_create(hdr);
        lv_image_set_src(logo, ETH_LOGO_LVGL_PATH);
        lv_obj_set_size(logo, 48, 48);
        lv_obj_align(logo, LV_ALIGN_LEFT_MID, 0, 0);
        title_x = 58;
    }
    lv_obj_t *lbl_title = lv_label_create(hdr);
    lv_label_set_text(lbl_title, "LIV-24");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, title_x, 0);

    lbl_clock = lv_label_create(hdr);
    lv_label_set_text(lbl_clock, "---  --:--");
    lv_obj_set_style_text_color(lbl_clock, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(lbl_clock, &lv_font_montserrat_32, 0);
    lv_obj_align(lbl_clock, LV_ALIGN_RIGHT_MID, 0, 0);

    // ── Greeting ──────────────────────────────────────────────
    lbl_greeting = lv_label_create(scr_home);
    lv_label_set_text(lbl_greeting, "Good morning");
    lv_obj_set_style_text_color(lbl_greeting, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_greeting, &lv_font_montserrat_48, 0);
    lv_obj_align(lbl_greeting, LV_ALIGN_TOP_LEFT, 28, 92);

    lbl_weather = lv_label_create(scr_home);
    lv_label_set_text(lbl_weather, "Bangkok  --\xC2\xB0""C");
    lv_label_set_recolor(lbl_weather, true);
    lv_obj_set_style_text_color(lbl_weather, lv_color_hex(0x445566), 0);
    lv_obj_set_style_text_font(lbl_weather, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_weather, LV_ALIGN_TOP_LEFT, 28, 158);

    // ── Emoji face (right side of greeting row) ───────────────
    s_emoji_face = lv_obj_create(scr_home);
    lv_obj_set_size(s_emoji_face, 52, 52);
    lv_obj_align_to(s_emoji_face, lbl_greeting, LV_ALIGN_OUT_RIGHT_MID, 14, 0);
    lv_obj_set_style_bg_color(s_emoji_face, lv_color_hex(0xFFCC00), 0);
    lv_obj_set_style_bg_opa(s_emoji_face, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_emoji_face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_emoji_face, 0, 0);
    lv_obj_set_style_pad_all(s_emoji_face, 0, 0);
    lv_obj_clear_flag(s_emoji_face, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_emoji_face, LV_OBJ_FLAG_CLICKABLE);

    s_emoji_eye1 = lv_obj_create(s_emoji_face);
    lv_obj_set_size(s_emoji_eye1, 9, 9);
    lv_obj_set_pos(s_emoji_eye1, 10, 16);
    lv_obj_set_style_bg_color(s_emoji_eye1, lv_color_hex(0x443300), 0);
    lv_obj_set_style_radius(s_emoji_eye1, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_emoji_eye1, 0, 0);
    lv_obj_clear_flag(s_emoji_eye1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_emoji_eye1, LV_OBJ_FLAG_CLICKABLE);

    s_emoji_eye2 = lv_obj_create(s_emoji_face);
    lv_obj_set_size(s_emoji_eye2, 9, 9);
    lv_obj_set_pos(s_emoji_eye2, 33, 16);
    lv_obj_set_style_bg_color(s_emoji_eye2, lv_color_hex(0x443300), 0);
    lv_obj_set_style_radius(s_emoji_eye2, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_emoji_eye2, 0, 0);
    lv_obj_clear_flag(s_emoji_eye2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_emoji_eye2, LV_OBJ_FLAG_CLICKABLE);

    // ── Card 1: Air Quality ───────────────────────────────────
    lv_obj_t *card1 = lv_obj_create(scr_home);
    lv_obj_set_size(card1, 664, 224);
    lv_obj_align(card1, LV_ALIGN_TOP_MID, 0, 204);
    lv_obj_set_style_bg_color(card1, lv_color_hex(0x12121E), 0);
    lv_obj_set_style_bg_opa(card1, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card1, 16, 0);
    lv_obj_set_style_border_color(card1, lv_color_hex(0x1A2A3A), 0);
    lv_obj_set_style_border_width(card1, 1, 0);
    lv_obj_set_style_pad_all(card1, 20, 0);
    lv_obj_clear_flag(card1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card1, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card1, [](lv_event_t *) {
        if (s_card1_cb) s_card1_cb();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *c1_title = lv_label_create(card1);
    lv_label_set_text(c1_title, "AIR QUALITY");
    lv_obj_set_style_text_color(c1_title, lv_color_hex(0x445566), 0);
    lv_obj_set_style_text_font(c1_title, &lv_font_montserrat_14, 0);
    lv_obj_align(c1_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *c1_arrow = lv_label_create(card1);
    lv_label_set_text(c1_arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(c1_arrow, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(c1_arrow, &lv_font_montserrat_24, 0);
    lv_obj_align(c1_arrow, LV_ALIGN_TOP_RIGHT, 0, -4);

    lbl_pm25_h = lv_label_create(card1);
    lv_label_set_text(lbl_pm25_h, "--");
    lv_obj_set_style_text_color(lbl_pm25_h, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(lbl_pm25_h, &lv_font_montserrat_48, 0);
    lv_obj_align(lbl_pm25_h, LV_ALIGN_TOP_LEFT, 0, 26);

    lv_obj_t *c1_unit = lv_label_create(card1);
    lv_label_set_text(c1_unit, "PM2.5  \xC2\xB5g/m\xC2\xB3");
    lv_obj_set_style_text_color(c1_unit, lv_color_hex(0x334455), 0);
    lv_obj_set_style_text_font(c1_unit, &lv_font_montserrat_14, 0);
    lv_obj_align(c1_unit, LV_ALIGN_TOP_LEFT, 0, 90);

    lbl_temp_h = lv_label_create(card1);
    lv_label_set_text(lbl_temp_h, "--\xC2\xB0""C");
    lv_obj_set_style_text_color(lbl_temp_h, lv_color_hex(0xFFAA44), 0);
    lv_obj_set_style_text_font(lbl_temp_h, &lv_font_montserrat_32, 0);
    lv_obj_align(lbl_temp_h, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lbl_hum_h = lv_label_create(card1);
    lv_label_set_text(lbl_hum_h, "--%");
    lv_obj_set_style_text_color(lbl_hum_h, lv_color_hex(0x44AAFF), 0);
    lv_obj_set_style_text_font(lbl_hum_h, &lv_font_montserrat_32, 0);
    lv_obj_align(lbl_hum_h, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    // ── Card 2: Room Status (display-only, no tap) ────────────
    lv_obj_t *card2 = lv_obj_create(scr_home);
    lv_obj_set_size(card2, 664, 176);
    lv_obj_align(card2, LV_ALIGN_TOP_MID, 0, 456);
    lv_obj_set_style_bg_color(card2, lv_color_hex(0x12121E), 0);
    lv_obj_set_style_bg_opa(card2, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card2, 16, 0);
    lv_obj_set_style_border_color(card2, lv_color_hex(0x1A2A3A), 0);
    lv_obj_set_style_border_width(card2, 1, 0);
    lv_obj_set_style_pad_all(card2, 20, 0);
    lv_obj_clear_flag(card2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(card2, LV_OBJ_FLAG_CLICKABLE);

    // Row 1: Room name (left) + status badge (right)
    lv_obj_t *c2_name = lv_label_create(card2);
    lv_label_set_text(c2_name, ROOM_STATUS_NAME);
    lv_obj_set_style_text_color(c2_name, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(c2_name, &lv_font_montserrat_24, 0);
    lv_obj_align(c2_name, LV_ALIGN_TOP_LEFT, 0, 14);

    s_rs_status_lbl = lv_label_create(card2);
    lv_label_set_text(s_rs_status_lbl, "");
    lv_obj_set_style_text_color(s_rs_status_lbl, lv_color_hex(0x445566), 0);
    lv_obj_set_style_text_font(s_rs_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_rs_status_lbl, LV_ALIGN_TOP_RIGHT, 0, 20);

    // Separator
    lv_obj_t *c2_sep = lv_obj_create(card2);
    lv_obj_set_size(c2_sep, LV_PCT(100), 1);
    lv_obj_align(c2_sep, LV_ALIGN_TOP_LEFT, 0, 54);
    lv_obj_set_style_bg_color(c2_sep, lv_color_hex(0x1C2C3C), 0);
    lv_obj_set_style_border_width(c2_sep, 0, 0);
    lv_obj_set_style_pad_all(c2_sep, 0, 0);
    lv_obj_clear_flag(c2_sep, LV_OBJ_FLAG_CLICKABLE);

    // Row 2 left: topic (or "AVAILABLE" when free) — y=80 centers font-24 between sep and card bottom
    s_rs_topic_lbl = lv_label_create(card2);
    lv_label_set_text(s_rs_topic_lbl, "");
    lv_obj_set_style_text_color(s_rs_topic_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_rs_topic_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_rs_topic_lbl, LV_ALIGN_TOP_LEFT, 0, 80);

    // Row 2 right: next booking time
    s_rs_next_lbl = lv_label_create(card2);
    lv_label_set_text(s_rs_next_lbl, "");
    lv_obj_set_style_text_color(s_rs_next_lbl, lv_color_hex(0x00CC66), 0);
    lv_obj_set_style_text_font(s_rs_next_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_rs_next_lbl, LV_ALIGN_TOP_RIGHT, 0, 80);

    // Row 3 left: organizer
    s_rs_organizer_lbl = lv_label_create(card2);
    lv_label_set_text(s_rs_organizer_lbl, "");
    lv_obj_set_style_text_color(s_rs_organizer_lbl, lv_color_hex(0x445566), 0);
    lv_obj_set_style_text_font(s_rs_organizer_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_rs_organizer_lbl, LV_ALIGN_TOP_LEFT, 0, 104);

    // Row 3 right: unused (next title skipped — may be Thai)
    s_rs_nexttitle_lbl = lv_label_create(card2);
    lv_label_set_text(s_rs_nexttitle_lbl, "");
    lv_obj_set_style_text_font(s_rs_nexttitle_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_rs_nexttitle_lbl, LV_ALIGN_TOP_RIGHT, 0, 96);

    // Set Bangkok timezone — SNTP itself is started by wifi_mqtt when IP arrives
    setenv("TZ", "ICT-7", 1);
    tzset();

    emoji_update(8); // default morning state; clock_timer_cb will correct once NTP syncs

    // ── LVGL timer: update clock every 30s ────────────────────
    lv_timer_create(clock_timer_cb, 30000, NULL);

    xTaskCreate(weather_task,     "weather",  8192, NULL, 2, NULL);
    xTaskCreate(room_status_task, "room_st",  6144, NULL, 2, NULL);
}

// ─── Sensor update (called from sensor task under bsp_display_lock) ──

void ui_home_update_sensors(float pm25, float temp, float hum)
{
    char buf[16];

    if (lbl_pm25_h) {
        if (isnan(pm25)) lv_label_set_text(lbl_pm25_h, "--");
        else { snprintf(buf, sizeof(buf), "%.0f", pm25); lv_label_set_text(lbl_pm25_h, buf); }
    }
    if (lbl_temp_h) {
        if (isnan(temp)) lv_label_set_text(lbl_temp_h, "--\xC2\xB0""C");
        else { snprintf(buf, sizeof(buf), "%.1f\xC2\xB0""C", temp); lv_label_set_text(lbl_temp_h, buf); }
    }
    if (lbl_hum_h) {
        if (isnan(hum)) lv_label_set_text(lbl_hum_h, "--%");
        else { snprintf(buf, sizeof(buf), "%.0f%%", hum); lv_label_set_text(lbl_hum_h, buf); }
    }
}
