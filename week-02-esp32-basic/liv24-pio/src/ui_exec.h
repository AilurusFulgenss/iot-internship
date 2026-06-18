#pragma once
#include "lvgl.h"

extern lv_obj_t *scr_exec;

void ui_exec_create(void);
void ui_exec_update(float temp, float hum, float pm25, float pm10);
void ui_exec_update_history(void);
