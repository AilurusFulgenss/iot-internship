#pragma once
#include "lvgl.h"

extern lv_obj_t *scr_dev;

void ui_dev_create(void);

void ui_dev_update_sn300(float t_raw, float h_raw, float s_raw,
                         float p25_raw, float p10_raw);
void ui_dev_update_network(const char *ip_str);
