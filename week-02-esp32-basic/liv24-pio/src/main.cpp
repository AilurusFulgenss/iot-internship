#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include "lvgl.h"
#include "btn_mode.h"
#include "ui_user.h"
#include "ui_dev.h"
#include "calib.h"

static const char *TAG = "LIV24";

#define BOOT_BTN    GPIO_NUM_35
#define RELAY1_GPIO GPIO_NUM_32
#define RELAY2_GPIO GPIO_NUM_46
#define NUM_PAGES   3

// scr_dev is defined in ui_dev.cpp — exec placeholder is local
static lv_obj_t *scr_exec = NULL;

// ── RS485 / MODBUS ─────────────────────────────────────
#define RS485_TXD      GPIO_NUM_47
#define RS485_RXD      GPIO_NUM_48
#define RS485_UART     UART_NUM_1
#define MODBUS_BAUD    9600
#define MODBUS_SLAVE   0x01
#define MODBUS_TIMEOUT pdMS_TO_TICKS(300)

static const gpio_num_t RELAY_GPIO[2] = {RELAY1_GPIO, RELAY2_GPIO};

static lv_obj_t *scr[NUM_PAGES];
static int cur_page = 0;

// Page 2: sensor value labels (updated by sensor task later)
lv_obj_t *lbl_temp_val;
lv_obj_t *lbl_hum_val;
lv_obj_t *lbl_sound_val;
lv_obj_t *lbl_pm25_val;
lv_obj_t *lbl_pm10_val;

// Page 3: relay state
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

static void create_splash(void)
{
    scr[0] = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr[0], lv_color_hex(0x080808), 0);
    lv_obj_set_style_bg_opa(scr[0], LV_OPA_COVER, 0);

    make_label(scr[0], "LIV-24", 0x00ff44, &lv_font_montserrat_48,
               LV_ALIGN_CENTER, 0, -120);
    make_label(scr[0], "IoT NODE", 0xffffff, &lv_font_montserrat_32,
               LV_ALIGN_CENTER, 0, -40);

    make_hline(scr[0], 340);

    make_label(scr[0], "Waveshare ESP32-P4", 0x555555, &lv_font_montserrat_14,
               LV_ALIGN_CENTER, 0, 70);
    make_label(scr[0], "Device ID: ESP32-001", 0x777777, &lv_font_montserrat_14,
               LV_ALIGN_CENTER, 0, 100);

    make_label(scr[0], "[ BOOT ]  next page  >", 0x335533, &lv_font_montserrat_14,
               LV_ALIGN_BOTTOM_MID, 0, -30);
}

// ─── Page 2: Sensor Dashboard ─────────────────────────

static void add_sensor_row(lv_obj_t *parent, const char *name, const char *unit,
                            lv_obj_t **val_lbl, int y)
{
    make_label(parent, name, 0x888888, &lv_font_montserrat_14, LV_ALIGN_TOP_LEFT, 60, y);
    *val_lbl = make_label(parent, "--", 0x00ff88, &lv_font_montserrat_24,
                          LV_ALIGN_TOP_MID, 0, y - 6);
    make_label(parent, unit, 0x555555, &lv_font_montserrat_14, LV_ALIGN_TOP_RIGHT, -60, y);
}

static void create_sensors(void)
{
    scr[1] = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr[1], lv_color_hex(0x080808), 0);
    lv_obj_set_style_bg_opa(scr[1], LV_OPA_COVER, 0);

    make_label(scr[1], "SENSOR DATA", 0x00ff44, &lv_font_montserrat_24,
               LV_ALIGN_TOP_MID, 0, 30);
    make_hline(scr[1], 80);

    add_sensor_row(scr[1], "Temperature", "\xC2\xB0""C",  &lbl_temp_val,  120);
    add_sensor_row(scr[1], "Humidity",    "%",             &lbl_hum_val,   210);
    add_sensor_row(scr[1], "Sound",       "dB",            &lbl_sound_val, 300);
    add_sensor_row(scr[1], "PM2.5",       "ug/m3",         &lbl_pm25_val,  390);
    add_sensor_row(scr[1], "PM10",        "ug/m3",         &lbl_pm10_val,  480);

    make_hline(scr[1], 580);
    make_label(scr[1], "[ BOOT ]  next page  >", 0x00E5FF, &lv_font_montserrat_14,
               LV_ALIGN_BOTTOM_MID, 0, -30);
}

// ─── Page 3: Relay Control ─────────────────────────────

static void relay_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    relay_on[idx] = !relay_on[idx];

    lv_label_set_text(relay_btn_lbl[idx], relay_on[idx] ? "ON" : "OFF");
    lv_obj_set_style_bg_color(relay_btn[idx],
        relay_on[idx] ? lv_color_hex(0x00E5FF) : lv_color_hex(0x222222), 0);
    lv_obj_set_style_text_color(relay_btn_lbl[idx],
        relay_on[idx] ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x555555), 0);

    gpio_set_level(RELAY_GPIO[idx], relay_on[idx] ? 1 : 0);
    ESP_LOGI(TAG, "Relay %d -> %s  (GPIO%d=%d)",
             idx + 1, relay_on[idx] ? "ON" : "OFF",
             RELAY_GPIO[idx], relay_on[idx] ? 1 : 0);
}

static void create_relay_ctrl(void)
{
    scr[2] = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr[2], lv_color_hex(0x080808), 0);
    lv_obj_set_style_bg_opa(scr[2], LV_OPA_COVER, 0);

    make_label(scr[2], "RELAY CONTROL", 0x00E5FF, &lv_font_montserrat_24,
               LV_ALIGN_TOP_MID, 0, 30);
    make_hline(scr[2], 80);

    const char *names[] = {"RELAY  1", "RELAY  2"};
    const int   ys[]    = {180, 420};

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

    make_label(scr[2], "[ BOOT ]  back to home", 0xFFFFFF, &lv_font_montserrat_14,
               LV_ALIGN_BOTTOM_MID, 0, -30);
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

// ส่ง Read Holding Registers (FC03), คืน true ถ้าสำเร็จ
static bool modbus_read(uint16_t start_reg, uint8_t count, uint16_t *out)
{
    if (count == 0 || count > 30) return false;

    uint8_t req[8];
    req[0] = MODBUS_SLAVE;
    req[1] = 0x03;
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

// REG MAP (SN-300BYH-M, all ÷10 except sound):
// 0x0000=Humidity×0.1%  0x0001=Temp×0.1°C
// 0x0003=PM10×0.1μg/m³  0x0004=PM2.5×0.1μg/m³  0x0005=Sound dB
static void sensor_read_task(void *arg)
{
    uint16_t regs[6];
    char buf[16];

    while (1) {
        if (modbus_read(0x0000, 6, regs)) {
            float hum   = regs[0] / 10.0f;
            float temp  = regs[1] / 10.0f;
            float pm10  = regs[3] / 10.0f;
            float pm25  = regs[4] / 10.0f;
            uint16_t snd = regs[5];

            ESP_LOGI(TAG, "T=%.1f°C H=%.1f%% Sound=%ddB PM2.5=%.1f PM10=%.1f",
                     temp, hum, snd, pm25, pm10);

            // Apply calibration
            float t_cal   = calib_apply(temp,        &g_calib.temp);
            float h_cal   = calib_apply(hum,         &g_calib.hum);
            float p25_cal = calib_apply(pm25,        &g_calib.pm25);
            float p10_cal = calib_apply(pm10,        &g_calib.pm10);
            float s_cal   = calib_apply((float)snd,  &g_calib.sound);

            bsp_display_lock(0);
            // USER screen: calibrated values
            ui_user_update(t_cal, h_cal, p25_cal, p10_cal, (int)s_cal);
            // DEV screen: raw values (it calculates cal internally)
            ui_dev_update(temp, hum, (float)snd, pm25, pm10);
            bsp_display_unlock();
        } else {
            ESP_LOGW(TAG, "sensor: no response");
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ─── Placeholder screens for DEV / EXEC modes ─────────

static void create_placeholder(lv_obj_t **out, const char *title,
                                uint32_t accent, const char *hint)
{
    *out = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(*out, lv_color_hex(0x080808), 0);
    lv_obj_set_style_bg_opa(*out, LV_OPA_COVER, 0);

    // Accent bar across top
    lv_obj_t *bar = lv_obj_create(*out);
    lv_obj_set_size(bar, 720, 8);
    lv_obj_set_style_bg_color(bar, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);

    make_label(*out, title, accent, &lv_font_montserrat_48,
               LV_ALIGN_CENTER, 0, -60);
    make_label(*out, hint, 0x555555, &lv_font_montserrat_24,
               LV_ALIGN_CENTER, 0, 40);
    make_label(*out, "Hold 3s to exit", 0x333333, &lv_font_montserrat_14,
               LV_ALIGN_BOTTOM_MID, 0, -30);
}

// ─── btn_mode callbacks ────────────────────────────────

// Short press in USER mode: toggle between user screen and relay page
void on_short_press(void)
{
    bsp_display_lock(0);
    lv_obj_t *active = lv_scr_act();
    if (active == scr_user) {
        lv_scr_load_anim(scr[2], LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
        ESP_LOGI(TAG, "-> Relay page");
    } else {
        lv_scr_load_anim(scr_user, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
        ESP_LOGI(TAG, "-> User screen");
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
            lv_scr_load_anim(scr_exec, LV_SCR_LOAD_ANIM_MOVE_LEFT, 400, 0, false);
            break;
    }
    bsp_display_unlock();
}

// ─── app_main ──────────────────────────────────────────

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "LIV-24 starting...");

    calib_init();   // NVS flash init + load saved calibration offsets
    rs485_init();
    xTaskCreate(sensor_read_task, "sensor", 4096, NULL, 5, NULL);

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

    bsp_display_start();
    bsp_display_backlight_on();

    bsp_display_lock(0);
    create_splash();       // scr[0]
    create_sensors();      // scr[1] — legacy sensor list
    create_relay_ctrl();   // scr[2] — relay page (short press from user screen)
    ui_user_create();      // scr_user — USER mode
    ui_dev_create();       // scr_dev  — DEV mode (calibration)
    create_placeholder(&scr_exec, "EXEC MODE", 0xffaa00, "Dashboard coming soon");
    lv_scr_load(scr_user); // default screen is the new user UI
    bsp_display_unlock();

    btn_mode_init(BOOT_BTN);
    // btn_mode_init launches a FreeRTOS task and returns immediately.
    // app_main can return — sensor task + button task keep running.
}
