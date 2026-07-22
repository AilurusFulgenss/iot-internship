#pragma once

typedef struct {
    float offset;
    float gain;
} sensor_calib_t;

typedef struct {
    sensor_calib_t temp;
    sensor_calib_t hum;
    sensor_calib_t sound;
    sensor_calib_t pm25;
    sensor_calib_t pm10;
} calib_data_t;

extern calib_data_t g_calib;

void calib_init(void);
void calib_save(void);

static inline float calib_apply(float raw, const sensor_calib_t *c)
{
    return (raw + c->offset) * c->gain;
}
