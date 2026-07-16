#pragma once
#include "lvgl.h"

extern lv_obj_t *scr_booking;   // building picker screen

void ui_booking_create(void);
void ui_booking_set_back_cb(void (*cb)(void));
void ui_booking_set_select_cb(void (*cb)(const char *building_id));
void ui_booking_activate(void);
