#pragma once
#include "lvgl.h"

extern lv_obj_t *scr_th;

void ui_th_create(void);
void ui_th_update(float temp, float hum);
