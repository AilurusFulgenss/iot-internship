#pragma once
#include "lvgl.h"

extern lv_obj_t *scr_exec;

void ui_exec_create(void);
void ui_exec_update_pm(float temp, float hum, float pm25, float pm10);
void ui_exec_update_hhcc(float temp, float moisture, float light, float fertility, float battery);
void ui_exec_update_ec(float ec);
void ui_exec_update_orp(float orp, float temp);
void ui_exec_update_leak(bool alarm);
void ui_exec_update_th(float temp, float hum);
void ui_exec_update_history(void);  // reserved for future drill-down
