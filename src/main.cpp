#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include "lvgl.h"
#include "draw/lv_image_decoder_private.h"
#include "draw/lv_draw_buf_private.h"
#include "btn_mode.h"
#include "ui_user.h"
#include "ui_dev.h"
#include "ui_exec.h"
#include "ui_exec_detail.h"
#include "ui_pm.h"
#include "calib.h"
#include "eth_upload.h"
#include "wifi_mqtt.h"
#include "history.h"
#include "ui_alert.h"
#include "ui_home.h"
#include "sensor_config.h"
#include "cJSON.h"
#include <math.h>

static const char *TAG = "LIV24";

#define BOOT_BTN    GPIO_NUM_35
#define RELAY1_GPIO GPIO_NUM_32
#define RELAY2_GPIO GPIO_NUM_46
// ── RS485 / MODBUS ─────────────────────────────────────
#define RS485_TXD      GPIO_NUM_47
#define RS485_RXD      GPIO_NUM_48
#define RS485_UART     UART_NUM_1
#define MODBUS_BAUD    9600
#define MODBUS_TIMEOUT pdMS_TO_TICKS(300)

static const gpio_num_t RELAY_GPIO[2] = {RELAY1_GPIO, RELAY2_GPIO};

static lv_obj_t *scr[3];          // scr[0]=splash, scr[2]=relay
static lv_obj_t *scr_eth_setup = NULL;

// Relay state
static bool relay_on[2] = {false, false};
static lv_obj_t *relay_btn[2];
static lv_obj_t *relay_btn_lbl[2];

// ─── helpers ───────────────────────────────────────────

static lv_obj_t *make_hline(lv_obj_t *parent, int y, int w = 560)
{
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_set_size(line, w, 2);
    lv_obj_set_style_bg_color(line, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, y);
    return line;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                             uint32_t color, const lv_font_t *font,
                             lv_align_t align, int x, int y)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_align(lbl, align, x, y);
    return lbl;
}

// ─── Page 1: Splash ────────────────────────────────────
// Shows logo image (centered) if logo.png is on SPIFFS; otherwise falls back to text.

static void create_splash(void)
{
    scr[0] = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr[0], lv_color_hex(0x080808), 0);
    lv_obj_set_style_bg_opa(scr[0], LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr[0], 0, 0);

    if (eth_upload_has_logo()) {
        lv_obj_t *logo = lv_image_create(scr[0]);
        lv_image_set_src(logo, ETH_LOGO_HD_LVGL_PATH);
        lv_obj_align(logo, LV_ALIGN_CENTER, 0, -60);

        make_label(scr[0], "LIV-24",   0x00E5FF, &lv_font_montserrat_32,
                   LV_ALIGN_CENTER, 0,  80);
        make_label(scr[0], "IoT NODE", 0x334455, &lv_font_montserrat_14,
                   LV_ALIGN_CENTER, 0, 120);
    } else {
        make_label(scr[0], "LIV-24",   0x00ff44, &lv_font_montserrat_48,
                   LV_ALIGN_CENTER, 0, -120);
        make_label(scr[0], "IoT NODE", 0xffffff, &lv_font_montserrat_32,
                   LV_ALIGN_CENTER, 0,  -40);
        make_hline(scr[0], 340);
        make_label(scr[0], "Waveshare ESP32-P4",   0x555555, &lv_font_montserrat_14,
                   LV_ALIGN_CENTER, 0,  70);
        make_label(scr[0], "Device ID: ESP32-001", 0x777777, &lv_font_montserrat_14,
                   LV_ALIGN_CENTER, 0, 100);
        make_label(scr[0], "[ BOOT ]  next page  >", 0x335533, &lv_font_montserrat_14,
                   LV_ALIGN_BOTTOM_MID, 0, -30);
    }
}

// ─── Ethernet Setup screen ─────────────────────────────
// Shown when no logo found — prompts user to plug in Ethernet and browse to device IP.

static lv_obj_t *create_eth_setup_screen(lv_obj_t **out_qr, lv_obj_t **out_ip_label)
{
    scr_eth_setup = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_eth_setup, lv_color_hex(0x0A0A12), 0);
    lv_obj_set_style_bg_opa(scr_eth_setup, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr_eth_setup, 0, 0);

    // Top bar
    lv_obj_t *bar = lv_obj_create(scr_eth_setup);
    lv_obj_set_size(bar, 720, 6);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);

    make_label(scr_eth_setup, "SETUP MODE",
               0x00E5FF, &lv_font_montserrat_32, LV_ALIGN_TOP_MID, 0, 26);

    // Divider between columns
    lv_obj_t *div = lv_obj_create(scr_eth_setup);
    lv_obj_set_size(div, 2, 520);
    lv_obj_set_pos(div, 359, 100);
    lv_obj_set_style_bg_color(div, lv_color_hex(0x1A2A3A), 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_set_style_pad_all(div, 0, 0);

    // ── Left column: Logo upload ──────────────────────────────────────
    make_label(scr_eth_setup, "LOGO UPLOAD",
               0x445566, &lv_font_montserrat_14, LV_ALIGN_TOP_MID, -180, 95);

    lv_obj_t *qr = lv_qrcode_create(scr_eth_setup);
    lv_qrcode_set_size(qr, 200);
    lv_qrcode_set_dark_color(qr, lv_color_hex(0x00E5FF));
    lv_qrcode_set_light_color(qr, lv_color_hex(0x0A0A12));
    lv_obj_align(qr, LV_ALIGN_CENTER, -170, -20);
    lv_obj_add_flag(qr, LV_OBJ_FLAG_HIDDEN);
    *out_qr = qr;

    lv_obj_t *ip_lbl = lv_label_create(scr_eth_setup);
    lv_label_set_text(ip_lbl, "Waiting for IP...");
    lv_obj_set_style_text_color(ip_lbl, lv_color_hex(0xFFAA00), 0);
    lv_obj_set_style_text_font(ip_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(ip_lbl, LV_ALIGN_CENTER, -170, 120);
    *out_ip_label = ip_lbl;

    make_label(scr_eth_setup, "Plug in Ethernet\nthen scan to upload logo",
               0x334455, &lv_font_montserrat_14, LV_ALIGN_BOTTOM_MID, -180, -20);

    // ── Right column: Telegram add bot ───────────────────────────────
    make_label(scr_eth_setup, "ADD TELEGRAM BOT",
               0x1565C0, &lv_font_montserrat_14, LV_ALIGN_TOP_MID, 180, 95);

    lv_obj_t *tg_qr = lv_qrcode_create(scr_eth_setup);
    lv_qrcode_set_size(tg_qr, 200);
    lv_qrcode_set_dark_color(tg_qr, lv_color_hex(0x2CA5E0));
    lv_qrcode_set_light_color(tg_qr, lv_color_hex(0x0A0A12));
    lv_obj_align(tg_qr, LV_ALIGN_CENTER, 170, -20);
    const char *tg_url = "https://t.me/liv24alert_bot";
    lv_qrcode_update(tg_qr, tg_url, strlen(tg_url));

    make_label(scr_eth_setup, "@liv24alert_bot",
               0x2CA5E0, &lv_font_montserrat_14, LV_ALIGN_CENTER, 170, 120);

    make_label(scr_eth_setup, "Scan to receive alerts\nvia Telegram",
               0x0D3B5E, &lv_font_montserrat_14, LV_ALIGN_BOTTOM_MID, 180, -20);

    return scr_eth_setup;
}

// ─── Page 2: Relay Control ─────────────────────────────
// Styled header bar (72px) matching USER/PM screens.
// Relay buttons shifted +72px down from previous layout.

// Called from both button tap and MQTT — safe from any task via bsp_display_lock.
void relay_set_state(int idx, bool on)
{
    if (idx < 0 || idx > 1) return;
    relay_on[idx] = on;
    gpio_set_level(RELAY_GPIO[idx], on ? 1 : 0);
    ESP_LOGI(TAG, "Relay %d -> %s (GPIO%d=%d)",
             idx + 1, on ? "ON" : "OFF", RELAY_GPIO[idx], on ? 1 : 0);

    if (bsp_display_lock(0)) {
        lv_label_set_text(relay_btn_lbl[idx], on ? "ON" : "OFF");
        lv_obj_set_style_bg_color(relay_btn[idx],
            on ? lv_color_hex(0x00E5FF) : lv_color_hex(0x222222), 0);
        lv_obj_set_style_text_color(relay_btn_lbl[idx],
            on ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x555555), 0);
        bsp_display_unlock();
    }
    wifi_mqtt_publish_relay_state(idx, on);
}

static void relay_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    relay_set_state(idx, !relay_on[idx]);
}

static void create_relay_ctrl(void)
{
    scr[2] = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr[2], lv_color_hex(0x0A0A12), 0);
    lv_obj_set_style_bg_opa(scr[2], LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr[2], 0, 0);

    // ── Header bar (72px) — same style as USER/PM screens ──
    lv_obj_t *hdr = lv_obj_create(scr[2]);
    lv_obj_set_size(hdr, 720, 72);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x12121E), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 22, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *accent = lv_obj_create(hdr);
    lv_obj_set_size(accent, 720, 3);
    lv_obj_align(accent, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_color(accent, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_style_pad_all(accent, 0, 0);

    // Logo (left side, if available) — user-uploaded PNG
    int title_x = 0;
    if (eth_upload_has_logo()) {
        lv_obj_t *logo_img = lv_image_create(hdr);
        lv_image_set_src(logo_img, ETH_LOGO_LVGL_PATH);
        lv_obj_set_size(logo_img, 48, 48);
        lv_obj_align(logo_img, LV_ALIGN_LEFT_MID, 0, 0);
        title_x = 58;
    }

    lv_obj_t *lbl_title = lv_label_create(hdr);
    lv_label_set_text(lbl_title, "RELAY CONTROL");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_32, 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, title_x, 0);

    // ── Relay buttons — shifted +72px for header ──────────
    // Button 1: y=252 (was 180), Button 2: y=492 (was 420)
    // Button 2 bottom: 492+140=632, margin to screen bottom: 88px ✓
    const char *names[] = {"RELAY  1", "RELAY  2"};
    const int   ys[]    = {212, 452};  // shifted up 40px from original

    for (int i = 0; i < 2; i++) {
        make_label(scr[2], names[i], 0x888888, &lv_font_montserrat_14,
                   LV_ALIGN_TOP_MID, 0, ys[i] - 50);

        relay_btn[i] = lv_btn_create(scr[2]);
        lv_obj_set_size(relay_btn[i], 400, 140);
        lv_obj_set_style_bg_color(relay_btn[i], lv_color_hex(0x222222), 0);
        lv_obj_set_style_radius(relay_btn[i], 12, 0);
        lv_obj_set_style_border_color(relay_btn[i], lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(relay_btn[i], 2, 0);
        lv_obj_align(relay_btn[i], LV_ALIGN_TOP_MID, 0, ys[i]);
        lv_obj_add_event_cb(relay_btn[i], relay_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        relay_btn_lbl[i] = lv_label_create(relay_btn[i]);
        lv_label_set_text(relay_btn_lbl[i], "OFF");
        lv_obj_set_style_text_font(relay_btn_lbl[i], &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(relay_btn_lbl[i], lv_color_hex(0x555555), 0);
        lv_obj_center(relay_btn_lbl[i]);
    }

}

// ─── RS485 / MODBUS RTU ────────────────────────────────

static uint16_t crc16(const uint8_t *buf, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
    return crc;
}

// Sent once at boot when CWT-TH04S is selected: changes its baud from 4800 → 9600.
// After the sensor stores the new baud in its flash, every subsequent boot
// will timeout here (sensor is already at 9600 and won't hear 4800 traffic),
// then re-init at 9600 and continue normally. ~500 ms penalty per boot.
static void cwt_th_set_baud_9600(void)
{
    uart_set_baudrate(RS485_UART, 4800);
    vTaskDelay(pdMS_TO_TICKS(100));

    // FC06 write reg 0x07D1 = 0x0002 (baud 9600)
    uint8_t req[8] = {0x01, 0x06, 0x07, 0xD1, 0x00, 0x02, 0x00, 0x00};
    uint16_t crc = crc16(req, 6);
    req[6] = crc & 0xFF;
    req[7] = crc >> 8;

    uart_flush_input(RS485_UART);
    uart_write_bytes(RS485_UART, req, sizeof(req));
    vTaskDelay(pdMS_TO_TICKS(300));

    uart_set_baudrate(RS485_UART, MODBUS_BAUD);
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "CWT-TH04S: baud set to 9600");
}

static void rs485_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = MODBUS_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(RS485_UART, 256, 0, 0, NULL, 0);
    uart_param_config(RS485_UART, &cfg);
    uart_set_pin(RS485_UART, RS485_TXD, RS485_RXD,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_set_mode(RS485_UART, UART_MODE_RS485_HALF_DUPLEX);
}

// REG MAP (SN-300BYH-M, all /10 except sound):
// 0x0000=Humidity*0.1%  0x0001=Temp*0.1C
// 0x0003=PM10*0.1ug/m3  0x0004=PM2.5*0.1ug/m3  0x0005=Sound dB
static bool modbus_read(uint8_t slave_id, uint8_t fc, uint16_t start_reg, uint8_t count, uint16_t *out)
{
    if (count == 0 || count > 30) return false;

    uint8_t req[8];
    req[0] = slave_id;
    req[1] = fc;
    req[2] = start_reg >> 8;
    req[3] = start_reg & 0xFF;
    req[4] = 0x00;
    req[5] = count;
    uint16_t crc = crc16(req, 6);
    req[6] = crc & 0xFF;
    req[7] = crc >> 8;

    uart_flush_input(RS485_UART);
    uart_write_bytes(RS485_UART, req, sizeof(req));

    int expected = 5 + 2 * count;
    uint8_t resp[64] = {};
    int got = uart_read_bytes(RS485_UART, resp, expected, MODBUS_TIMEOUT);
    if (got < expected) return false;

    uint16_t resp_crc = resp[got - 2] | (resp[got - 1] << 8);
    if (crc16(resp, got - 2) != resp_crc) return false;

    for (int i = 0; i < count; i++)
        out[i] = (resp[3 + i * 2] << 8) | resp[4 + i * 2];
    return true;
}

static void sensor_read_task(void *arg)
{
    uint16_t regs[32];
    char buf[16];

    while (1) {
        sensor_config_t       cfg = sensor_config_get();
        const sensor_model_t *m   = &SENSOR_MODELS[cfg.model_idx];

        if (modbus_read(cfg.slave_id, m->fc, m->reg_start, m->reg_count, regs)) {
            sensor_store_raw(regs, m->reg_count);

            switch (m->type) {
            case SENSOR_TYPE_PM: {
                float temp = (m->idx_temp  >= 0) ? (int16_t)regs[m->idx_temp]  / m->scale : NAN;
                float hum  = (m->idx_hum   >= 0) ? regs[m->idx_hum]            / m->scale : NAN;
                float pm10 = (m->idx_pm10  >= 0) ? regs[m->idx_pm10]  / m->scale : NAN;
                float pm25 = (m->idx_pm25  >= 0) ? regs[m->idx_pm25]  / m->scale : NAN;
                uint16_t snd = (m->idx_sound >= 0) ? regs[m->idx_sound] : 0;

                ESP_LOGI(TAG, "T=%.1fC H=%.1f%% Sound=%ddB PM2.5=%.1f PM10=%.1f",
                         temp, hum, snd, pm25, pm10);

                float t_cal   = calib_apply(temp,       &g_calib.temp);
                float h_cal   = calib_apply(hum,        &g_calib.hum);
                float p25_cal = calib_apply(pm25,       &g_calib.pm25);
                float p10_cal = calib_apply(pm10,       &g_calib.pm10);
                float s_cal   = calib_apply((float)snd, &g_calib.sound);

                bsp_display_lock(0);
                ui_alert_set_mqtt_status(wifi_mqtt_is_connected());
                ui_user_update(t_cal, h_cal, p25_cal, p10_cal, (int)s_cal);
                ui_pm_update(t_cal, h_cal, p25_cal, p10_cal, (float)s_cal);
                ui_dev_update_sn300(temp, hum, (float)snd, pm25, pm10);
                ui_exec_update_pm(t_cal, h_cal, p25_cal, p10_cal);
                ui_home_update_sensors(p25_cal, t_cal, h_cal);
                ui_alert_check(t_cal, h_cal, p25_cal, p10_cal, s_cal);
                bsp_display_unlock();

                wifi_mqtt_publish_sensors(t_cal, h_cal, (int)s_cal, p25_cal, p10_cal);
                break;
            }
            case SENSOR_TYPE_EC: {
                float ec     = (m->idx_ec >= 0) ? regs[m->idx_ec] / m->scale : NAN;
                float ec_cal = calib_apply(ec, &g_calib.ec);
                ESP_LOGI(TAG, "EC=%.1f uS/cm (cal=%.1f)", ec, ec_cal);

                bsp_display_lock(0);
                ui_alert_set_mqtt_status(wifi_mqtt_is_connected());
                ui_user_update_ec(ec_cal);
                ui_exec_update_ec(ec_cal);
                ui_dev_update_ec(ec);
                bsp_display_unlock();

                wifi_mqtt_publish_ec(ec_cal);
                break;
            }
            case SENSOR_TYPE_LEAK: {
                bool alarm = (m->idx_leak >= 0) ? (regs[m->idx_leak] == 0x0002) : false;
                ESP_LOGI(TAG, "Leak=%s (raw=0x%04X)", alarm ? "ALARM" : "NORMAL",
                         m->idx_leak >= 0 ? regs[m->idx_leak] : 0);

                bsp_display_lock(0);
                ui_alert_set_mqtt_status(wifi_mqtt_is_connected());
                ui_user_update_leak(alarm);
                ui_exec_update_leak(alarm);
                bsp_display_unlock();

                wifi_mqtt_publish_leak(alarm);
                break;
            }
            case SENSOR_TYPE_TH: {
                float temp     = (m->idx_temp >= 0) ? (int16_t)regs[m->idx_temp] / m->scale : NAN;
                float hum      = (m->idx_hum  >= 0) ? regs[m->idx_hum]           / m->scale : NAN;
                float t_cal    = calib_apply(temp, &g_calib.th_temp);
                float h_cal    = calib_apply(hum,  &g_calib.th_hum);
                ESP_LOGI(TAG, "TH: T=%.1fC H=%.1f%% (cal T=%.1f H=%.1f)", temp, hum, t_cal, h_cal);

                bsp_display_lock(0);
                ui_alert_set_mqtt_status(wifi_mqtt_is_connected());
                ui_user_update_th(t_cal, h_cal);
                ui_exec_update_th(t_cal, h_cal);
                ui_dev_update_th(temp, hum);
                ui_home_update_sensors(NAN, t_cal, h_cal);
                bsp_display_unlock();

                wifi_mqtt_publish_th(t_cal, h_cal);
                break;
            }
            case SENSOR_TYPE_ORP: {
                float orp      = (m->idx_orp  >= 0) ? (int16_t)regs[m->idx_orp]  / m->scale : NAN;
                float temp     = (m->idx_temp >= 0) ? (int16_t)regs[m->idx_temp] / m->scale : NAN;
                float orp_cal  = calib_apply(orp,  &g_calib.orp);
                float t_cal    = calib_apply(temp, &g_calib.orp_temp);
                ESP_LOGI(TAG, "ORP: %.1f mV T=%.1fC (cal ORP=%.1f T=%.1f)", orp, temp, orp_cal, t_cal);

                bsp_display_lock(0);
                ui_alert_set_mqtt_status(wifi_mqtt_is_connected());
                ui_user_update_orp(orp_cal, t_cal);
                ui_exec_update_orp(orp_cal, t_cal);
                ui_dev_update_orp(orp, temp);
                bsp_display_unlock();

                wifi_mqtt_publish_orp(orp_cal, t_cal);
                break;
            }
            }
        } else {
            ESP_LOGW(TAG, "sensor: no response");
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ─── Touch navigation: replaces BOOT button (GPIO35 = EMAC TXD1 conflict) ────

static void touch_nav_init(void)
{
    // ── ▶ next-page button at bottom-right of USER-mode screens ──────────────
    // Strip on PM screen moved up to -72 (from -48), so ▶ at -8 clears it with 8px gap.
    lv_obj_t *cycle_scrns[] = {scr_user, scr_pm, scr[2]};
    int       cycle_y[]     = {-8, -8, -8};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_btn_create(cycle_scrns[i]);
        lv_obj_set_size(btn, 88, 56);
        lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -8, cycle_y[i]);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x00E5FF), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_10, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x00E5FF), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_opa(btn, LV_OPA_30, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, [](lv_event_t *) { on_short_press(); },
                            LV_EVENT_CLICKED, NULL);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x00E5FF), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_center(lbl);
    }

    // ── DEV / EXEC hidden zones — 5-tap on top corners, no visible UI ───────
    // top-left   × 5  → DEV (calibrate)
    // top-center × 5  → EXEC (overview)
    {
        struct TapZone { uint32_t t[5]; int n; app_mode_t mode; };
        static TapZone tz_dev  = {{}, 0, MODE_DEV};
        static TapZone tz_exec = {{}, 0, MODE_EXEC};

        auto tap_cb = [](lv_event_t *e) {
            auto *tz     = static_cast<TapZone *>(lv_event_get_user_data(e));
            uint32_t now = lv_tick_get();
            if (tz->n > 0 && lv_tick_elaps(tz->t[tz->n - 1]) > 600)
                tz->n = 0;
            if (tz->n < 5) tz->t[tz->n] = now;
            tz->n++;
            if (tz->n >= 5) {
                if (lv_tick_elaps(tz->t[0]) <= 3000)
                    set_app_mode(tz->mode);
                tz->n = 0;
            }
        };

        TapZone *zones[2]    = {&tz_dev, &tz_exec};
        lv_align_t aligns[2] = {LV_ALIGN_TOP_LEFT, LV_ALIGN_TOP_MID};
        int x_off[2]         = {0, 0};
        int y_off[2]         = {0, 0};
        for (int i = 0; i < 2; i++) {
            lv_obj_t *z = lv_obj_create(scr_user);
            lv_obj_set_size(z, 90, 90);
            lv_obj_align(z, aligns[i], x_off[i], y_off[i]);
            lv_obj_set_style_bg_opa(z, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(z, 0, 0);
            lv_obj_set_style_shadow_width(z, 0, 0);
            lv_obj_add_flag(z, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(z, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(z, tap_cb, LV_EVENT_CLICKED, zones[i]);
        }
    }

    // ── ← USER exit button on DEV and EXEC screens ───────────────────────────
    lv_obj_t *mode_scrns[] = {scr_dev, scr_exec};
    for (int i = 0; i < 2; i++) {
        lv_obj_t *btn = lv_btn_create(mode_scrns[i]);
        lv_obj_set_size(btn, 88, 40);
        lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -8, 12);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A1A2E), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x2C3D52), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, [](lv_event_t *) { set_app_mode(MODE_USER); },
                            LV_EVENT_CLICKED, NULL);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, LV_SYMBOL_LEFT " USER");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x445566), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(lbl);
    }
}

// ─── btn_mode callbacks ────────────────────────────────

// Short press cycle: USER → PM History → Relay → USER
void on_short_press(void)
{
    bsp_display_lock(0);
    lv_obj_t *active = lv_scr_act();
    if (active == scr_exec) {
        // EXEC mode — short press does nothing
    } else if (active == scr_home) {
        // Home has no nav button — do nothing
    } else if (active == scr_user) {
        lv_scr_load_anim(scr_pm, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
        ESP_LOGI(TAG, "-> PM History");
    } else if (active == scr_pm) {
        lv_scr_load_anim(scr[2], LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
        ESP_LOGI(TAG, "-> Relay page");
    } else {
        lv_scr_load_anim(scr_home, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
        ESP_LOGI(TAG, "-> Home");
    }
    bsp_display_unlock();
}

void on_mode_changed(app_mode_t new_mode)
{
    bsp_display_lock(0);
    switch (new_mode) {
        case MODE_USER:
            lv_scr_load_anim(scr_user, LV_SCR_LOAD_ANIM_FADE_IN, 400, 0, false);
            break;
        case MODE_DEV:
            lv_scr_load_anim(scr_dev,  LV_SCR_LOAD_ANIM_MOVE_LEFT, 400, 0, false);
            break;
        case MODE_EXEC:
            lv_scr_load_anim(scr_exec, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
            break;
    }
    bsp_display_unlock();
}

// ─── app_main ──────────────────────────────────────────

static void on_test_alert(const char *json, int len)
{
    char buf[128];
    int  n = len < (int)sizeof(buf) - 1 ? len : (int)sizeof(buf) - 1;
    memcpy(buf, json, n); buf[n] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) return;

    float temp  = cJSON_IsNumber(cJSON_GetObjectItem(root, "temp"))  ? (float)cJSON_GetObjectItem(root, "temp")->valuedouble  : NAN;
    float hum   = cJSON_IsNumber(cJSON_GetObjectItem(root, "hum"))   ? (float)cJSON_GetObjectItem(root, "hum")->valuedouble   : NAN;
    float pm25  = cJSON_IsNumber(cJSON_GetObjectItem(root, "pm25"))  ? (float)cJSON_GetObjectItem(root, "pm25")->valuedouble  : NAN;
    float pm10  = cJSON_IsNumber(cJSON_GetObjectItem(root, "pm10"))  ? (float)cJSON_GetObjectItem(root, "pm10")->valuedouble  : NAN;
    float sound = cJSON_IsNumber(cJSON_GetObjectItem(root, "sound")) ? (float)cJSON_GetObjectItem(root, "sound")->valuedouble : NAN;
    cJSON_Delete(root);

    if (bsp_display_lock(0)) {
        ui_alert_check(temp, hum, pm25, pm10, sound);
        bsp_display_unlock();
    }

    // Auto-dismiss after 15 s — prevents retained broker messages from
    // leaving a permanent banner when no PM sensor is connected to clear it.
    xTaskCreate([](void *) {
        vTaskDelay(pdMS_TO_TICKS(15000));
        if (bsp_display_lock(0)) {
            ui_alert_check(NAN, NAN, NAN, NAN, NAN);
            bsp_display_unlock();
        }
        vTaskDelete(NULL);
    }, "alert_clr", 2048, NULL, 2, NULL);
}

static void on_hist_24h(const char *d, int len)
{
    hist_parse_24h(d, len);
    if (bsp_display_lock(0)) {
        ui_pm_refresh_history();
        bsp_display_unlock();
    }
}

static void on_hist_7d(const char *d, int len)
{
    hist_parse_7d(d, len);
    if (bsp_display_lock(0)) {
        ui_exec_detail_refresh();
        bsp_display_unlock();
    }
}

static void on_hist_ec(const char *d, int len)
{
    hist_parse_ec(d, len);
    if (bsp_display_lock(0)) { ui_exec_detail_refresh(); bsp_display_unlock(); }
}

static void on_hist_orp(const char *d, int len)
{
    hist_parse_orp(d, len);
    if (bsp_display_lock(0)) { ui_exec_detail_refresh(); bsp_display_unlock(); }
}

static void on_hist_hhcc(const char *d, int len)
{
    hist_parse_hhcc(d, len);
    if (bsp_display_lock(0)) { ui_exec_detail_refresh(); bsp_display_unlock(); }
}

static void on_hist_th(const char *d, int len)
{
    hist_parse_th(d, len);
    if (bsp_display_lock(0)) { ui_exec_detail_refresh(); bsp_display_unlock(); }
}

static void on_hist_leak(const char *d, int len)
{
    hist_parse_leak(d, len);
    if (bsp_display_lock(0)) { ui_exec_detail_refresh(); bsp_display_unlock(); }
}

static void on_flora(const char *json, int len)
{
    char buf[128];
    int n = len < (int)sizeof(buf) - 1 ? len : (int)sizeof(buf) - 1;
    memcpy(buf, json, n); buf[n] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) return;

    float temp      = cJSON_IsNumber(cJSON_GetObjectItem(root, "temp"))      ? (float)cJSON_GetObjectItem(root, "temp")->valuedouble      : NAN;
    float moisture  = cJSON_IsNumber(cJSON_GetObjectItem(root, "moisture"))  ? (float)cJSON_GetObjectItem(root, "moisture")->valuedouble  : NAN;
    float light     = cJSON_IsNumber(cJSON_GetObjectItem(root, "light"))     ? (float)cJSON_GetObjectItem(root, "light")->valuedouble     : NAN;
    float fertility = cJSON_IsNumber(cJSON_GetObjectItem(root, "fertility")) ? (float)cJSON_GetObjectItem(root, "fertility")->valuedouble : NAN;
    float battery   = cJSON_IsNumber(cJSON_GetObjectItem(root, "battery"))   ? (float)cJSON_GetObjectItem(root, "battery")->valuedouble   : NAN;
    cJSON_Delete(root);


    if (bsp_display_lock(0)) {
        ui_user_update_hhcc(temp, moisture, light, fertility, battery);
        ui_exec_update_hhcc(temp, moisture, light, fertility, battery);
        bsp_display_unlock();
    }
}

static void logo_url_received(const char *url)
{
    ESP_LOGI(TAG, "Logo URL: %s", url);
    char *url_copy = strdup(url);
    xTaskCreate([](void *arg) {
        const char *u = (const char *)arg;
        if (eth_logo_fetch_from_url(u)) {
            vTaskDelay(pdMS_TO_TICKS(300));
            esp_restart();
        } else {
            ESP_LOGE(TAG, "Logo fetch failed");
        }
        free((void *)u);
        vTaskDelete(NULL);
    }, "logo_fetch", 8192, url_copy, 3, NULL);
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "LIV-24 starting...");

    // Init network stack once, at the very top — both eth_start_background()
    // and eth_upload_start() rely on these being called exactly once before use.
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    sensor_config_init();
    calib_init();
    rs485_init();

    // CWT-TH04S ships at 4800 baud — change to 9600 on first boot with this model
    if (sensor_config_get().model_idx == 3) {
        cwt_th_set_baud_9600();
    }

    // Relay GPIO init — default OFF
    gpio_config_t relay_cfg = {};
    relay_cfg.pin_bit_mask = (1ULL << RELAY1_GPIO) | (1ULL << RELAY2_GPIO);
    relay_cfg.mode         = GPIO_MODE_OUTPUT;
    relay_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    relay_cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    relay_cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&relay_cfg);
    gpio_set_level(RELAY1_GPIO, 0);
    gpio_set_level(RELAY2_GPIO, 0);

    // ── Boot-hold reset: hold BOOT button at power-on → clears logo → setup mode ──
    gpio_set_direction(BOOT_BTN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BOOT_BTN, GPIO_PULLUP_ONLY);
    vTaskDelay(pdMS_TO_TICKS(150));
    if (gpio_get_level(BOOT_BTN) == 0) {
        eth_upload_clear_logo();
        ESP_LOGW(TAG, "Boot-hold: restarting into setup mode");
        esp_restart();
    }

    {
        bsp_display_cfg_t disp_cfg = {
            .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
            .buffer_size   = BSP_LCD_DRAW_BUFF_SIZE,
            .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
            .flags = {
                .buff_dma    = true,
                .buff_spiram = false,
                .sw_rotate   = true,
            },
        };
        disp_cfg.lvgl_port_cfg.task_stack = 16384;
        bsp_display_start_with_config(&disp_cfg);
    }
    // Kconfig LV_MEM_POOL_EXPAND_SIZE_KILOBYTES only relaxes TLSF size limit —
    // the extra PSRAM pool must be registered explicitly via lv_mem_add_pool().
    static void *s_lvgl_psram_pool = heap_caps_malloc(256 * 1024, MALLOC_CAP_SPIRAM);
    if (s_lvgl_psram_pool) {
        bsp_display_lock(0);
        lv_mem_add_pool(s_lvgl_psram_pool, 256 * 1024);
        bsp_display_unlock();
        ESP_LOGI(TAG, "LVGL PSRAM pool: 256KB @%p", s_lvgl_psram_pool);
    }
    bsp_display_backlight_on();

    // Initialize LVGL POSIX filesystem driver (maps drive 'A' → /spiffs)
    // Must be called after bsp_display_start() initializes LVGL.
    lv_fs_posix_init();
    lv_tjpgd_init();

    // Mount SPIFFS and check if logo.jpg exists
    eth_upload_init();

    // Sensor task created HERE — after bsp_display_start() — so bsp_display_lock()
    // is never called before lvgl_port_init (would assert-fail in esp_lvgl_port).
    xTaskCreate(sensor_read_task, "sensor", 4096, NULL, 5, NULL);

    if (!eth_upload_has_logo()) {
        lv_obj_t *qr_obj, *ip_label;
        bsp_display_lock(0);
        create_eth_setup_screen(&qr_obj, &ip_label);
        lv_scr_load(scr_eth_setup);
        bsp_display_unlock();
        eth_upload_start(qr_obj, ip_label);
        return;
    }

    // ── Normal boot: logo exists ─────────────────────────────
    // One lock block: create ALL screens + load splash first.
    // taskLVGL blocked for entire duration → no dirty PSRAM L2 cache lines
    // at unlock → avoids ESP32-P4 Rev 1.3 ROM deadlock.
    bsp_display_lock(0);
    create_splash();          // shows logo centered on dark bg
    create_relay_ctrl();
    ui_user_create();
    ui_pm_create();
    ui_dev_create();
    ui_exec_create();
    ui_exec_detail_create();
    ui_home_create();
    touch_nav_init();
    ui_alert_init();
    ui_home_set_card1_cb([]() {
        lv_scr_load_anim(scr_user, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
    });
    lv_scr_load(scr[0]);     // show logo splash — inside the same lock block
    bsp_display_unlock();

    // Hold logo splash for 2.5s, then fade into USER screen
    vTaskDelay(pdMS_TO_TICKS(2500));

    bsp_display_lock(0);
    lv_scr_load_anim(scr_home, LV_SCR_LOAD_ANIM_FADE_IN, 600, 0, false);
    bsp_display_unlock();

    // Register MQTT handler BEFORE starting Ethernet — esp_eth_start() blocks
    // during PHY autonegotiation; DHCP can complete during that block so the
    // IP_EVENT_ETH_GOT_IP handler must already be registered when it fires.
    wifi_mqtt_set_relay_cb(relay_set_state);
    wifi_mqtt_set_logo_url_cb(logo_url_received);
    wifi_mqtt_set_history_cb(on_hist_24h, on_hist_7d);
    wifi_mqtt_set_test_alert_cb(on_test_alert);
    wifi_mqtt_set_flora_cb(on_flora);
    wifi_mqtt_set_hist_sensor_cbs(on_hist_ec, on_hist_orp, on_hist_hhcc, on_hist_th, on_hist_leak);
    wifi_mqtt_init(MQTT_BROKER_URI);
    eth_start_background();

    // btn_mode_init(BOOT_BTN);
    // GPIO35 = EMAC RMII TXD1. gpio_config(INPUT) from btn_mode_init clears the
    // GPIO matrix output-enable on GPIO35, disabling TXD1 and corrupting all
    // Ethernet TX frames. Button disabled until a non-EMAC GPIO is identified.
}
