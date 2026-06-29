#include "ui_home.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include "eth_upload.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
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

static void (*s_card1_cb)(void) = NULL;
void ui_home_set_card1_cb(void (*cb)(void)) { s_card1_cb = cb; }

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

                char wbuf[48];
                if (!isnan(temp))
                    snprintf(wbuf, sizeof(wbuf), "Bangkok  %.0f\xC2\xB0""C  %s", temp, cond);
                else
                    snprintf(wbuf, sizeof(wbuf), "Bangkok  --  %s", cond);

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
    lv_obj_set_style_text_color(lbl_weather, lv_color_hex(0x445566), 0);
    lv_obj_set_style_text_font(lbl_weather, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_weather, LV_ALIGN_TOP_LEFT, 28, 158);

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

    // ── Card 2: Room Booking ──────────────────────────────────
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

    lv_obj_t *c2_title = lv_label_create(card2);
    lv_label_set_text(c2_title, "ROOM BOOKING");
    lv_obj_set_style_text_color(c2_title, lv_color_hex(0x445566), 0);
    lv_obj_set_style_text_font(c2_title, &lv_font_montserrat_14, 0);
    lv_obj_align(c2_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *c2_soon = lv_label_create(card2);
    lv_label_set_text(c2_soon, "COMING SOON");
    lv_obj_set_style_text_color(c2_soon, lv_color_hex(0x2A3A4A), 0);
    lv_obj_set_style_text_font(c2_soon, &lv_font_montserrat_32, 0);
    lv_obj_align(c2_soon, LV_ALIGN_CENTER, 0, 8);

    // Set Bangkok timezone — SNTP itself is started by wifi_mqtt when IP arrives
    setenv("TZ", "ICT-7", 1);
    tzset();

    // ── LVGL timer: update clock every 30s ────────────────────
    lv_timer_create(clock_timer_cb, 30000, NULL);

    xTaskCreate(weather_task, "weather", 8192, NULL, 2, NULL);
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
