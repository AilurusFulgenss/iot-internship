#pragma once
#include "lvgl.h"

extern lv_obj_t *scr_flora;

void ui_flora_create(void);
void ui_flora_update(float temp, float moisture, float light, float fertility);
