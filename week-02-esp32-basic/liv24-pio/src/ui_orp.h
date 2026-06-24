#pragma once
#include "lvgl.h"

extern lv_obj_t *scr_orp;

void ui_orp_create(void);
void ui_orp_update(float orp, float temp);
