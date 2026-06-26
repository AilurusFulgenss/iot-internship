#pragma once

// Calibration per sensor: cal = (raw + offset) * gain
typedef struct {
    float offset;   // additive correction, default 0.0
    float gain;     // multiplicative correction, default 1.0
} sensor_calib_t;

typedef struct {
    sensor_calib_t temp;
    sensor_calib_t hum;
    sensor_calib_t sound;
    sensor_calib_t pm25;
    sensor_calib_t pm10;
    // CWT-TH04S
    sensor_calib_t th_temp;
    sensor_calib_t th_hum;
    // CWT-EC/TDS
    sensor_calib_t ec;
    // BH-485-ORP
    sensor_calib_t orp;
    sensor_calib_t orp_temp;
} calib_data_t;

extern calib_data_t g_calib;

// Call once in app_main before display init (initialises NVS + loads saved values)
void calib_init(void);

// Write current g_calib to NVS flash
void calib_save(void);

static inline float calib_apply(float raw, const sensor_calib_t *c)
{
    return (raw + c->offset) * c->gain;
}
