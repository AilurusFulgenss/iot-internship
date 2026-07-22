#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "nvs_flash.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include "lvgl.h"
#include "ui_user.h"
#include "ui_dev.h"
#include "ui_pin.h"
#include "calib.h"
#include <math.h>

static const char *TAG = "LIV24";

// ── RS485 / MODBUS ─────────────────────────────────────────────────────────────
#define RS485_TXD      GPIO_NUM_47
#define RS485_RXD      GPIO_NUM_48
#define RS485_UART     UART_NUM_1
#define MODBUS_BAUD    9600
#define MODBUS_TIMEOUT pdMS_TO_TICKS(300)

// SN-300BYH-M register map (FC03, start 0x0000, count 6)
// 0x0000=Hum*0.1%  0x0001=Temp*0.1°C  0x0003=PM10*0.1  0x0004=PM2.5*0.1  0x0005=Sound dB
#define SN300_SLAVE  1
#define SN300_START  0x0000
#define SN300_COUNT  6

// ── Mode ───────────────────────────────────────────────────────────────────────
typedef enum { MODE_USER = 0, MODE_DEV } app_mode_t;
volatile app_mode_t g_app_mode = MODE_USER;

void set_app_mode(int mode)
{
    g_app_mode = (app_mode_t)mode;
    if (bsp_display_lock(0)) {
        if (mode == MODE_USER)
            lv_scr_load_anim(scr_user, LV_SCR_LOAD_ANIM_FADE_IN, 400, 0, false);
        else
            lv_scr_load_anim(scr_dev,  LV_SCR_LOAD_ANIM_MOVE_LEFT, 400, 0, false);
        bsp_display_unlock();
    }
}

// ── RS485 helpers ──────────────────────────────────────────────────────────────
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

static bool modbus_read(uint8_t slave, uint16_t start, uint8_t count, uint16_t *out)
{
    uint8_t req[8];
    req[0] = slave; req[1] = 0x03;
    req[2] = start >> 8; req[3] = start & 0xFF;
    req[4] = 0x00; req[5] = count;
    uint16_t crc = crc16(req, 6);
    req[6] = crc & 0xFF; req[7] = crc >> 8;

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

// ── Sensor task ────────────────────────────────────────────────────────────────
static void sensor_task(void *)
{
    uint16_t regs[SN300_COUNT];
    while (1) {
        if (modbus_read(SN300_SLAVE, SN300_START, SN300_COUNT, regs)) {
            float hum   = regs[0] / 10.0f;
            float temp  = (int16_t)regs[1] / 10.0f;
            float pm10  = regs[3] / 10.0f;
            float pm25  = regs[4] / 10.0f;
            int   sound = regs[5];

            float t_cal  = calib_apply(temp,        &g_calib.temp);
            float h_cal  = calib_apply(hum,         &g_calib.hum);
            float p25    = calib_apply(pm25,         &g_calib.pm25);
            float p10    = calib_apply(pm10,         &g_calib.pm10);
            float s_cal  = calib_apply((float)sound, &g_calib.sound);

            ESP_LOGI(TAG, "T=%.1f H=%.1f PM2.5=%.1f PM10=%.1f Sound=%d",
                     t_cal, h_cal, p25, p10, (int)s_cal);

            if (bsp_display_lock(0)) {
                ui_user_update(t_cal, h_cal, p25, p10, (int)s_cal);
                ui_dev_update_sn300(temp, hum, (float)sound, pm25, pm10);
                bsp_display_unlock();
            }
        } else {
            ESP_LOGW(TAG, "sensor: no response");
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ── Splash (hardcoded placeholder logo) ───────────────────────────────────────
static void create_splash(lv_obj_t **out_scr)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x080810), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    // Placeholder logo: cyan rounded rectangle
    lv_obj_t *badge = lv_obj_create(scr);
    lv_obj_set_size(badge, 280, 160);
    lv_obj_align(badge, LV_ALIGN_CENTER, 0, -80);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0x00252A), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(badge, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(badge, 2, 0);
    lv_obj_set_style_radius(badge, 20, 0);
    lv_obj_set_style_shadow_width(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_main = lv_label_create(badge);
    lv_label_set_text(lbl_main, "LIV-24");
    lv_obj_set_style_text_color(lbl_main, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(lbl_main, &lv_font_montserrat_48, 0);
    lv_obj_align(lbl_main, LV_ALIGN_CENTER, 0, -12);

    lv_obj_t *lbl_sub = lv_label_create(badge);
    lv_label_set_text(lbl_sub, "IoT PANEL");
    lv_obj_set_style_text_color(lbl_sub, lv_color_hex(0x337788), 0);
    lv_obj_set_style_text_font(lbl_sub, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_sub, LV_ALIGN_CENTER, 0, 36);

    lv_obj_t *lbl_boot = lv_label_create(scr);
    lv_label_set_text(lbl_boot, "Initializing...");
    lv_obj_set_style_text_color(lbl_boot, lv_color_hex(0x223344), 0);
    lv_obj_set_style_text_font(lbl_boot, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_boot, LV_ALIGN_CENTER, 0, 80);

    *out_scr = scr;
}

// ── Hidden zone: 5-tap top-left → PIN → Calib ─────────────────────────────────
static void init_hidden_zone(void)
{
    struct TapZone { uint32_t t[5]; int n; };
    static TapZone tz = {{}, 0};

    auto tap_cb = [](lv_event_t *e) {
        auto *tz     = static_cast<TapZone *>(lv_event_get_user_data(e));
        uint32_t now = lv_tick_get();
        if (tz->n > 0 && lv_tick_elaps(tz->t[tz->n - 1]) > 600) tz->n = 0;
        if (tz->n < 5) tz->t[tz->n] = now;
        tz->n++;
        if (tz->n >= 5) {
            if (lv_tick_elaps(tz->t[0]) <= 3000)
                ui_pin_show([]() { set_app_mode(MODE_DEV); });
            tz->n = 0;
        }
    };

    lv_obj_t *zone = lv_obj_create(scr_user);
    lv_obj_set_size(zone, 90, 90);
    lv_obj_align(zone, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(zone, 0, 0);
    lv_obj_set_style_shadow_width(zone, 0, 0);
    lv_obj_add_flag(zone, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(zone, tap_cb, LV_EVENT_CLICKED, &tz);
}

// ── app_main ───────────────────────────────────────────────────────────────────
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "LIV-24 Panel starting...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    calib_init();
    rs485_init();

    // Display
    bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size   = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = { .buff_dma = true, .buff_spiram = false, .sw_rotate = true },
    };
    disp_cfg.lvgl_port_cfg.task_stack = 16384;
    bsp_display_start_with_config(&disp_cfg);

    static void *s_lvgl_psram = heap_caps_malloc(256 * 1024, MALLOC_CAP_SPIRAM);
    if (s_lvgl_psram) {
        bsp_display_lock(0);
        lv_mem_add_pool(s_lvgl_psram, 256 * 1024);
        bsp_display_unlock();
    }
    bsp_display_backlight_on();

    // Build all screens inside one lock
    bsp_display_lock(0);
    lv_obj_t *scr_splash;
    create_splash(&scr_splash);
    ui_user_create();
    ui_dev_create();
    init_hidden_zone();
    lv_scr_load(scr_splash);
    bsp_display_unlock();

    // Splash hold 2.5 s
    vTaskDelay(pdMS_TO_TICKS(2500));

    bsp_display_lock(0);
    lv_scr_load_anim(scr_user, LV_SCR_LOAD_ANIM_FADE_IN, 600, 0, false);
    bsp_display_unlock();

    xTaskCreate(sensor_task, "sensor", 4096, NULL, 5, NULL);
}
