#pragma once
#include "lvgl.h"

extern lv_obj_t *scr_room_detail;

void ui_room_detail_create(void);
void ui_room_detail_set_back_cb(void (*cb)(void));
void ui_room_detail_set_slot_cb(void (*cb)(const char *room_id, const char *room_name,
                                            const char *start, const char *end));
void ui_room_detail_activate(const char *room_id);
