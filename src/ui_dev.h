#pragma once
#include "lvgl.h"

extern lv_obj_t *scr_dev;

void ui_dev_create(void);

void ui_dev_update_sn300(float t_raw, float h_raw, float s_raw,
                         float p25_raw, float p10_raw);
void ui_dev_update_th(float t_raw, float h_raw);
void ui_dev_update_ec(float ec_raw);
void ui_dev_update_orp(float orp_raw, float temp_raw);
