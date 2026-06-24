#include "btn_mode.h"
#include "esp_log.h"

static const char *TAG = "BTN";

volatile app_mode_t g_app_mode = MODE_USER;

void set_app_mode(app_mode_t new_mode)
{
    if (g_app_mode == new_mode) return;
    const char *names[] = {"USER", "DEV", "EXEC"};
    ESP_LOGI(TAG, "Mode: %s -> %s", names[g_app_mode], names[new_mode]);
    g_app_mode = new_mode;
    on_mode_changed(new_mode);
}

// ─── Button task ──────────────────────────────────────────────────────────────
//
// Hold timing state machine:
//
//   idle ──(falling edge)──▶ counting
//     counting ──(7 s)──▶ trigger DEV
//     counting ──(10 s)──▶ trigger EXEC        (overrides DEV trigger)
//     counting ──(released, <1 s)──▶ short press
//     counting ──(released, 1–7 s)──▶ ignored
//
//   DEV / EXEC ──(hold 3 s from any mode != USER)──▶ back to USER
//
static gpio_num_t s_btn_gpio = GPIO_NUM_35;   // set by btn_mode_init

static void button_task(void *arg)
{
    gpio_num_t gpio = s_btn_gpio;
    bool prev_level      = true;   // pull-up: idle = 1
    TickType_t press_tick = 0;
    bool in_press        = false;
    bool dev_fired       = false;
    bool exec_fired      = false;

    while (1) {
        bool level = (bool)gpio_get_level(gpio);

        // ── Falling edge: button just pressed ────────────────────────────────
        if (prev_level && !level) {
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            if (!gpio_get_level(gpio)) {               // still held after debounce
                in_press   = true;
                press_tick = xTaskGetTickCount();
                dev_fired  = false;
                exec_fired = false;
                ESP_LOGI(TAG, "press start");
            }
        }
        // ── Rising edge: button just released ────────────────────────────────
        else if (!prev_level && level) {
            if (in_press) {
                uint32_t held_ms = (xTaskGetTickCount() - press_tick)
                                   * portTICK_PERIOD_MS;
                ESP_LOGI(TAG, "released after %lu ms", held_ms);

                if (!dev_fired && !exec_fired && held_ms < 1000) {
                    // Short tap in USER mode → cycle page
                    if (g_app_mode == MODE_USER) on_short_press();
                    // Short tap in DEV/EXEC → nothing (reserved)
                }
                // Modes >= DEV: hold 3 s on release → back to USER
                if (g_app_mode != MODE_USER && held_ms >= HOLD_MS_EXIT
                    && !dev_fired && !exec_fired) {
                    set_app_mode(MODE_USER);
                }
                in_press = false;
            }
        }
        // ── While held: check progressive thresholds ─────────────────────────
        else if (in_press) {
            uint32_t held_ms = (xTaskGetTickCount() - press_tick)
                               * portTICK_PERIOD_MS;

            if (!exec_fired && held_ms >= HOLD_MS_EXEC) {
                exec_fired = true;
                dev_fired  = true;           // prevent double-trigger on release
                ESP_LOGI(TAG, "10 s hold → EXEC");
                set_app_mode(MODE_EXEC);
            } else if (!dev_fired && held_ms >= HOLD_MS_DEV) {
                dev_fired = true;
                ESP_LOGI(TAG, "7 s hold → DEV");
                set_app_mode(MODE_DEV);
            }

            // Optional: log progress every second while holding
            if (held_ms < HOLD_MS_EXEC) {
                uint32_t rem_dev  = (held_ms < HOLD_MS_DEV)
                                    ? (HOLD_MS_DEV  - held_ms) / 1000 : 0;
                uint32_t rem_exec = (HOLD_MS_EXEC - held_ms) / 1000;
                (void)rem_dev; (void)rem_exec;
                // ESP_LOGD(TAG, "held %lu ms | dev in %lus | exec in %lus",
                //          held_ms, rem_dev, rem_exec);
            }
        }

        prev_level = level;
        vTaskDelay(pdMS_TO_TICKS(10));   // 10 ms poll = 100 Hz, plenty for 7 s hold
    }
}

// ─── Public init ──────────────────────────────────────────────────────────────

void btn_mode_init(gpio_num_t gpio)
{
    s_btn_gpio = gpio;
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << gpio;
    cfg.mode         = GPIO_MODE_INPUT;
    cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&cfg);

    xTaskCreate(button_task, "btn", 8192, NULL, 4, NULL);
    ESP_LOGI(TAG, "btn_mode_init done (GPIO%d)", gpio);
}
