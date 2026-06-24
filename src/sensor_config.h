#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Sensor model definition ───────────────────────────────────────────────────

typedef enum {
    SENSOR_TYPE_PM,    // temp / hum / PM2.5 / PM10 / sound  (FC03)
    SENSOR_TYPE_EC,    // EC/TDS conductivity                 (FC03)
    SENSOR_TYPE_LEAK,  // liquid leak status                  (FC04)
    SENSOR_TYPE_TH,    // temp / hum only                     (FC03)
    SENSOR_TYPE_ORP,   // ORP mV + temp                       (FC03)
} sensor_type_t;

typedef struct {
    const char    *name;
    sensor_type_t  type;
    uint16_t       reg_start;
    uint8_t        reg_count;
    uint8_t        fc;        // Modbus function code (0x03 or 0x04)
    int8_t         idx_temp;
    int8_t         idx_hum;
    int8_t         idx_pm10;
    int8_t         idx_pm25;
    int8_t         idx_sound;
    int8_t         idx_ec;    // EC/TDS register index (-1 if N/A)
    int8_t         idx_leak;  // leak status register index (-1 if N/A)
    int8_t         idx_orp;   // ORP register index (-1 if N/A)
    float          scale;
} sensor_model_t;

#define SENSOR_MODEL_COUNT 5
extern const sensor_model_t SENSOR_MODELS[SENSOR_MODEL_COUNT];

// ── Saved config (persisted to NVS flash) ────────────────────────────────────

typedef struct {
    uint8_t model_idx;   // index into SENSOR_MODELS[]
    uint8_t slave_id;    // Modbus slave address (1–247)
} sensor_config_t;

void            sensor_config_init(void);
sensor_config_t sensor_config_get(void);
void            sensor_config_set(uint8_t model_idx, uint8_t slave_id);

// ── Last raw register snapshot (updated after every successful Modbus read) ────

typedef struct {
    bool     valid;
    uint16_t regs[8];
    uint8_t  count;
} sensor_last_raw_t;

void              sensor_store_raw(const uint16_t *regs, uint8_t count);
sensor_last_raw_t sensor_get_raw(void);

#ifdef __cplusplus
}
#endif
