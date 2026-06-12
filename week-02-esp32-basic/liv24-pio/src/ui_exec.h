#pragma once
#include "lvgl.h"

extern lv_obj_t *scr_exec;

void ui_exec_create(void);
void ui_exec_update(float pm25, float pm10, int sound);
