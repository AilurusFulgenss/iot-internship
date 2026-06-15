#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
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
#include "ui_pm.h"
#include "calib.h"
#include "eth_upload.h"

static const char *TAG = "LIV24";

#define BOOT_BTN    GPIO_NUM_35
#define RELAY1_GPIO GPIO_NUM_32
#define RELAY2_GPIO GPIO_NUM_46
#define NUM_PAGES   3


// ── RS485 / MODBUS ─────────────────────────────────────
#define RS485_TXD      GPIO_NUM_47
#define RS485_RXD      GPIO_NUM_48
#define RS485_UART     UART_NUM_1
#define MODBUS_BAUD    9600
#define MODBUS_SLAVE   0x01
#define MODBUS_TIMEOUT pdMS_TO_TICKS(300)

static const gpio_num_t RELAY_GPIO[2] = {RELAY1_GPIO, RELAY2_GPIO};

static lv_obj_t *scr[NUM_PAGES];
static lv_obj_t *scr_eth_setup = NULL;
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
// Shows logo image (centered) if logo.png is on SPIFFS; otherwise falls back to text.

static void create_splash(void)
{
    scr[0] = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr[0], lv_color_hex(0x080808), 0);
    lv_obj_set_style_bg_opa(scr[0], LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr[0], 0, 0);

    if (eth_upload_has_logo()) {
        lv_obj_t *logo = lv_image_create(scr[0]);
        lv_image_set_src(logo, ETH_LOGO_LVGL_PATH);
        lv_obj_set_size(logo, 240, 240);
        lv_image_set_inner_align(logo, LV_IMAGE_ALIGN_STRETCH);
        lv_obj_align(logo, LV_ALIGN_CENTER, 0, -60);
        // Transparent PNGs would be invisible on the dark splash background.
        // Setting bg_color+bg_opa makes the widget fill with white first, so
        // transparent pixels in the PNG composite against white instead of black.
        lv_obj_set_style_bg_color(logo, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(logo, LV_OPA_COVER, 0);

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

    lv_obj_t *bar = lv_obj_create(scr_eth_setup);
    lv_obj_set_size(bar, 720, 6);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);

    make_label(scr_eth_setup, "SETUP MODE",
               0x00E5FF, &lv_font_montserrat_32, LV_ALIGN_TOP_MID, 0, 26);
    make_label(scr_eth_setup, "1.  Plug in Ethernet cable",
               0x556677, &lv_font_montserrat_24, LV_ALIGN_TOP_MID, 0, 90);
    make_label(scr_eth_setup, "2.  Scan QR to upload logo",
               0x445566, &lv_font_montserrat_24, LV_ALIGN_TOP_MID, 0, 135);

    // QR code — hidden until DHCP assigns IP, then updated with the URL
    lv_obj_t *qr = lv_qrcode_create(scr_eth_setup);
    lv_qrcode_set_size(qr, 260);
    lv_qrcode_set_dark_color(qr, lv_color_hex(0x00E5FF));
    lv_qrcode_set_light_color(qr, lv_color_hex(0x0A0A12));
    lv_obj_align(qr, LV_ALIGN_CENTER, 0, 20);
    lv_obj_add_flag(qr, LV_OBJ_FLAG_HIDDEN);
    *out_qr = qr;

    // IP URL label — shown below QR, updated alongside the QR
    lv_obj_t *ip_lbl = lv_label_create(scr_eth_setup);
    lv_label_set_text(ip_lbl, "Waiting for IP...");
    lv_obj_set_style_text_color(ip_lbl, lv_color_hex(0xFFAA00), 0);
    lv_obj_set_style_text_font(ip_lbl, &lv_font_montserrat_24, 0);
    lv_obj_align(ip_lbl, LV_ALIGN_BOTTOM_MID, 0, -60);
    *out_ip_label = ip_lbl;

    make_label(scr_eth_setup, "Upload PNG — device restarts automatically",
               0x334455, &lv_font_montserrat_14, LV_ALIGN_BOTTOM_MID, 0, -20);

    return scr_eth_setup;
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
// Styled header bar (72px) matching USER/PM screens.
// Relay buttons shifted +72px down from previous layout.

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
        lv_image_set_inner_align(logo_img, LV_IMAGE_ALIGN_STRETCH);
        lv_obj_align(logo_img, LV_ALIGN_LEFT_MID, 0, 0);
        title_x = 58;
    }

    lv_obj_t *lbl_title = lv_label_create(hdr);
    lv_label_set_text(lbl_title, "RELAY CONTROL");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_32, 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, title_x, 0);

    lv_obj_t *lbl_nav = lv_label_create(hdr);
    lv_label_set_text(lbl_nav, "< PM");
    lv_obj_set_style_text_color(lbl_nav, lv_color_hex(0x3A4A5A), 0);
    lv_obj_set_style_text_font(lbl_nav, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_nav, LV_ALIGN_RIGHT_MID, 0, 0);

    // ── Relay buttons — shifted +72px for header ──────────
    // Button 1: y=252 (was 180), Button 2: y=492 (was 420)
    // Button 2 bottom: 492+140=632, margin to screen bottom: 88px ✓
    const char *names[] = {"RELAY  1", "RELAY  2"};
    const int   ys[]    = {252, 492};

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

    make_label(scr[2], "Hold 7s = Dev", 0x223344, &lv_font_montserrat_14,
               LV_ALIGN_BOTTOM_MID, 0, -16);
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

// REG MAP (SN-300BYH-M, all /10 except sound):
// 0x0000=Humidity*0.1%  0x0001=Temp*0.1C
// 0x0003=PM10*0.1ug/m3  0x0004=PM2.5*0.1ug/m3  0x0005=Sound dB
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

            ESP_LOGI(TAG, "T=%.1fC H=%.1f%% Sound=%ddB PM2.5=%.1f PM10=%.1f",
                     temp, hum, snd, pm25, pm10);

            float t_cal   = calib_apply(temp,        &g_calib.temp);
            float h_cal   = calib_apply(hum,         &g_calib.hum);
            float p25_cal = calib_apply(pm25,        &g_calib.pm25);
            float p10_cal = calib_apply(pm10,        &g_calib.pm10);
            float s_cal   = calib_apply((float)snd,  &g_calib.sound);

            bsp_display_lock(0);
            ui_user_update(t_cal, h_cal, p25_cal, p10_cal, (int)s_cal);
            ui_pm_update(p25_cal, p10_cal);
            ui_dev_update(temp, hum, (float)snd, pm25, pm10);
            ui_exec_update(p25_cal, p10_cal, (int)s_cal);
            bsp_display_unlock();
        } else {
            ESP_LOGW(TAG, "sensor: no response");
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ─── btn_mode callbacks ────────────────────────────────

// Short press: USER -> PM -> RELAY -> USER  |  EXEC has no sub-pages
void on_short_press(void)
{
    bsp_display_lock(0);
    lv_obj_t *active = lv_scr_act();
    if (active == scr_exec) {
        // EXEC mode — short press does nothing
    } else if (active == scr_user) {
        lv_scr_load_anim(scr_pm, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
        ESP_LOGI(TAG, "-> PM History");
    } else if (active == scr_pm) {
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
            lv_scr_load_anim(scr_exec, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
            break;
    }
    bsp_display_unlock();
}

// ─── app_main ──────────────────────────────────────────

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "LIV-24 starting...");

    calib_init();
    rs485_init();

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
    bsp_display_backlight_on();

    // Initialize LVGL POSIX filesystem driver (maps drive 'A' → /spiffs)
    // Must be called after bsp_display_start() initializes LVGL.
    lv_fs_posix_init();
    lv_lodepng_init();
    lv_tjpgd_init();

    // Route PNG pixel-data allocations to PSRAM.
    // Without this, lv_draw_buf_create_ex pulls from the 128KB LVGL internal heap
    // which can't hold a 65KB file buffer + 160KB pixel buffer simultaneously.
    // With PSRAM: file buffer stays on LVGL heap (≤65KB), pixel buffer goes to 32MB SPIRAM.
    {
        lv_draw_buf_handlers_t *h = lv_draw_buf_get_image_handlers();
        h->buf_malloc_cb = [](size_t sz, lv_color_format_t) -> void * {
            return heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
        };
        h->buf_free_cb = [](void *p) { heap_caps_free(p); };
    }

    // Mount SPIFFS and check if logo.png exists
    eth_upload_init();

    // ── Logo diagnostic ───────────────────────────────────────────────────────
    if (eth_upload_has_logo()) {
        // Test 1: LVGL POSIX FS — open + read PNG magic bytes
        lv_fs_file_t fsf;
        lv_fs_res_t fsres = lv_fs_open(&fsf, ETH_LOGO_LVGL_PATH, LV_FS_MODE_RD);
        if (fsres == LV_FS_RES_OK) {
            uint8_t fhdr[8] = {};
            uint32_t br = 0;
            lv_fs_read(&fsf, fhdr, 8, &br);
            lv_fs_close(&fsf);
            bool is_png = (br >= 4 && fhdr[0] == 0x89 && fhdr[1] == 0x50 &&
                           fhdr[2] == 0x4E && fhdr[3] == 0x47);
            ESP_LOGI(TAG, "DIAG FS  : OK  %02X%02X%02X%02X  PNG=%s",
                     fhdr[0], fhdr[1], fhdr[2], fhdr[3], is_png ? "YES" : "NO");
        } else {
            ESP_LOGE(TAG, "DIAG FS  : FAIL res=%d", fsres);
        }

        // Test 2: image decoder header — needs display lock
        bsp_display_lock(0);
        lv_image_header_t img_hdr = {};
        lv_result_t dec_res = lv_image_decoder_get_info(ETH_LOGO_LVGL_PATH, &img_hdr);
        if (dec_res == LV_RESULT_OK) {
            ESP_LOGI(TAG, "DIAG DEC : OK  %dx%d  cf=%d",
                     img_hdr.w, img_hdr.h, (int)img_hdr.cf);
        } else {
            ESP_LOGE(TAG, "DIAG DEC : FAIL  (decoder not registered / file corrupt)");
        }

        // Test 3: full pixel decode — DIAG DEC only reads the PNG IHDR (header bytes),
        // not pixels. If lodepng errors during full decode the widget silently shows nothing.
        lv_image_decoder_dsc_t dec_dsc = {};
        lv_result_t open_res = lv_image_decoder_open(&dec_dsc, ETH_LOGO_LVGL_PATH, NULL);
        if (open_res == LV_RESULT_OK && dec_dsc.decoded && dec_dsc.decoded->data) {
            const uint8_t *p = dec_dsc.decoded->data;   // BGRA byte order in memory
            ESP_LOGI(TAG, "DIAG OPEN: OK  px0=A%02X R%02X G%02X B%02X",
                     p[3], p[2], p[1], p[0]);
            lv_image_decoder_close(&dec_dsc);
        } else {
            ESP_LOGE(TAG, "DIAG OPEN: FAIL  res=%d  decoded=%p",
                     (int)open_res, (void *)dec_dsc.decoded);
            if (open_res == LV_RESULT_OK) lv_image_decoder_close(&dec_dsc);
        }

        bsp_display_unlock();
    }

    // Sensor task created HERE — after bsp_display_start() — so bsp_display_lock()
    // is never called before lvgl_port_init (would assert-fail in esp_lvgl_port).
    xTaskCreate(sensor_read_task, "sensor", 4096, NULL, 5, NULL);

    if (!eth_upload_has_logo()) {
        // ── Ethernet Setup mode: no logo yet — show QR + IP and wait for upload ──
        lv_obj_t *qr_obj, *ip_label;
        bsp_display_lock(0);
        create_eth_setup_screen(&qr_obj, &ip_label);
        lv_scr_load(scr_eth_setup);
        bsp_display_unlock();

        // eth_upload_start() starts Ethernet+DHCP+HTTP, blocks until esp_restart()
        eth_upload_start(qr_obj, ip_label);
        return;
    }

    // ── Normal boot: logo exists ─────────────────────────────
    // One lock block: create ALL screens + load splash first.
    // taskLVGL blocked for entire duration → no dirty PSRAM L2 cache lines
    // at unlock → avoids ESP32-P4 Rev 1.3 ROM deadlock.
    bsp_display_lock(0);
    create_splash();          // shows logo centered on dark bg
    create_sensors();
    create_relay_ctrl();
    ui_user_create();
    ui_pm_create();
    ui_dev_create();
    ui_exec_create();
    lv_scr_load(scr[0]);     // show logo splash — inside the same lock block
    bsp_display_unlock();

    // Hold logo splash for 2.5s, then fade into USER screen
    vTaskDelay(pdMS_TO_TICKS(2500));

    bsp_display_lock(0);
    lv_scr_load_anim(scr_user, LV_SCR_LOAD_ANIM_FADE_IN, 600, 0, false);
    bsp_display_unlock();

    btn_mode_init(BOOT_BTN);
}
