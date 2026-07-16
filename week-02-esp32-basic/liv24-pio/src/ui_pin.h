#pragma once
#include "lvgl.h"
#include <functional>

// Show PIN gate modal on lv_layer_top() (always on top, no screen switch).
// Must be called from LVGL event context (already locked).
// on_success is invoked when the correct PIN is entered.
void ui_pin_show(std::function<void()> on_success);
