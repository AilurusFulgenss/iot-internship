#pragma once
#include "lvgl.h"

extern lv_obj_t *scr_book_confirm;

void ui_book_confirm_create(void);
void ui_book_confirm_set_back_cb(void (*cb)(void));
void ui_book_confirm_set_done_cb(void (*cb)(void));
void ui_book_confirm_activate(const char *room_id, const char *room_name,
                               const char *start, const char *end);
