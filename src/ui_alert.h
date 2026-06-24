#pragma once
#include "lvgl.h"

void ui_alert_init(void);
void ui_alert_check(float temp, float hum, float pm25, float pm10, float sound);
void ui_alert_set_mqtt_status(bool connected);
