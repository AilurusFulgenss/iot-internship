#pragma once
#include "lvgl.h"

extern lv_obj_t *scr_dev;

// Call once (inside bsp_display_lock) during app_main UI init
void ui_dev_create(void);

// Call from sensor task (inside bsp_display_lock) on every sensor read
void ui_dev_update(float t_raw, float h_raw, float s_raw,
                   float p25_raw, float p10_raw);
