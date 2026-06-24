// Button timing + mode switching — no UI code here
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

// ─── Mode definitions ─────────────────────────────────────────────────────────

typedef enum {
    MODE_USER = 0,   // default: public-facing UI
    MODE_DEV,        // developer: raw values + calibration
    MODE_EXEC,       // executive: summary dashboard
} app_mode_t;

// Global mode — read anywhere, set only via set_app_mode()
extern volatile app_mode_t g_app_mode;

// ─── Timing thresholds ────────────────────────────────────────────────────────

#define HOLD_MS_DEV   7000   // hold 7 s  → MODE_DEV
#define HOLD_MS_EXEC  10000  // hold 10 s → MODE_EXEC
#define HOLD_MS_EXIT  3000   // hold 3 s from DEV/EXEC → back to USER
#define DEBOUNCE_MS   50

// ─── Public API ───────────────────────────────────────────────────────────────

// Call once from app_main — configures GPIO and spawns the button task
void btn_mode_init(gpio_num_t gpio);

// Change mode and fire on_mode_changed()
void set_app_mode(app_mode_t new_mode);

// ─── Callbacks (implement in main.cpp) ────────────────────────────────────────

// Called on short press (<1 s) while in USER mode
extern void on_short_press(void);

// Called whenever mode changes — wire your UI switch here
extern void on_mode_changed(app_mode_t new_mode);
