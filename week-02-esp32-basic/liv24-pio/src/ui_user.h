#pragma once
#include "lvgl.h"

extern lv_obj_t *scr_user;

// Call once (inside bsp_display_lock) during app_main UI init
void ui_user_create(void);

// Call from sensor task (inside bsp_display_lock) on every sensor read
void ui_user_update(float temp, float hum, float pm25, float pm10, int sound);
void ui_user_update_ec(float ec);
void ui_user_update_leak(bool alarm);
void ui_user_update_th(float temp, float hum);
void ui_user_update_orp(float orp, float temp);
