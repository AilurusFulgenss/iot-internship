#pragma once
#include "lvgl.h"

extern lv_obj_t *scr_home;

void ui_home_create(void);
void ui_home_set_card1_cb(void (*cb)(void));
void ui_home_update_sensors(float pm25, float temp, float hum);
