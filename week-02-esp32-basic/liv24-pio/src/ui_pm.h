#pragma once
#include "lvgl.h"

extern lv_obj_t *scr_pm;

void ui_pm_create(void);
void ui_pm_update(float pm25, float pm10);
