#pragma once
#include "lvgl.h"

void ui_alert_init(void);
void ui_alert_check(float temp, float hum, float pm25, float pm10, float sound);
