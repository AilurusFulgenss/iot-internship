#pragma once
#include "lvgl.h"

extern lv_obj_t *scr_exec_detail;

// Call once during UI init (inside bsp_display_lock)
void ui_exec_detail_create(void);

// Call before navigating to the screen — sets up charts for the given sensor
// sensor_idx: 0=PM, 1=HHCC, 2=EC, 3=ORP, 4=LEAK, 5=TH
void ui_exec_detail_open(int sensor_idx);

// Call when new sensor history arrives via MQTT to update bars if screen is active
void ui_exec_detail_refresh(void);
