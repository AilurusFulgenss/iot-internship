#pragma once
#include "lvgl.h"

extern lv_obj_t *scr_user;

void ui_user_create(void);
void ui_user_update(float temp, float hum, float pm25, float pm10, int sound);
