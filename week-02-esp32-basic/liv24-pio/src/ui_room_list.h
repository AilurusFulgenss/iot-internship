#pragma once
#include "lvgl.h"

extern lv_obj_t *scr_room_list;

void ui_room_list_create(void);
void ui_room_list_set_back_cb(void (*cb)(void));
void ui_room_list_set_select_cb(void (*cb)(const char *room_id));
void ui_room_list_activate(const char *building_id);
