#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Sensor model definition ───────────────────────────────────────────────────
// Describes a RS485 sensor: which Modbus registers to read and how to parse them.
// idx_* = position in the register array (-1 means that value is not available)
// scale = divide raw register value by this to get real unit (e.g. 10 → ÷10)

typedef struct {
    const char *name;
    uint16_t    reg_start;
    uint8_t     reg_count;
    int8_t      idx_temp;
    int8_t      idx_hum;
    int8_t      idx_pm10;
    int8_t      idx_pm25;
    int8_t      idx_sound;
    float       scale;
} sensor_model_t;

#define SENSOR_MODEL_COUNT 2
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
