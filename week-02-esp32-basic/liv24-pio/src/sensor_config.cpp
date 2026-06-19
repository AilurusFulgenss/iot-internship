#include "sensor_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SENSOR_CFG";

// ── Model registry ────────────────────────────────────────────────────────────

const sensor_model_t SENSOR_MODELS[SENSOR_MODEL_COUNT] = {
    {
        // Model 0: SN-300BYH-M — temp / hum / PM2.5 / PM10 / sound
        // Registers: [0]=Hum*10  [1]=Temp*10  [3]=PM10*10  [4]=PM2.5*10  [5]=Sound
        "SN-300BYH-M",
        0x0000, 6,
        /*idx_temp*/  1,
        /*idx_hum*/   0,
        /*idx_pm10*/  3,
        /*idx_pm25*/  4,
        /*idx_sound*/ 5,
        /*scale*/     10.0f,
    },
    {
        // Model 1: placeholder — spec arrives Monday
        // Fill in reg_start / reg_count / idx_* once datasheet is available
        "Model-2 (TBD)",
        0x0000, 4,
        /*idx_temp*/   1,
        /*idx_hum*/    0,
        /*idx_pm10*/  -1,
        /*idx_pm25*/  -1,
        /*idx_sound*/ -1,
        /*scale*/     10.0f,
    },
};

// ── In-memory config + last raw snapshot ─────────────────────────────────────

static sensor_config_t  s_cfg      = { 0, 1 };
static sensor_last_raw_t s_last_raw = {};

void sensor_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    nvs_handle_t h;
    if (nvs_open("sensor_cfg", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "model", &s_cfg.model_idx);
        nvs_get_u8(h, "slave", &s_cfg.slave_id);
        nvs_close(h);
    }

    if (s_cfg.model_idx >= SENSOR_MODEL_COUNT) s_cfg.model_idx = 0;
    if (s_cfg.slave_id == 0)                   s_cfg.slave_id  = 1;

    ESP_LOGI(TAG, "Loaded: model=%s  slave_id=%d",
             SENSOR_MODELS[s_cfg.model_idx].name, s_cfg.slave_id);
}

sensor_config_t sensor_config_get(void)
{
    return s_cfg;
}

void sensor_store_raw(const uint16_t *regs, uint8_t count)
{
    if (count > 8) count = 8;
    memcpy(s_last_raw.regs, regs, count * sizeof(uint16_t));
    s_last_raw.count = count;
    s_last_raw.valid = true;
}

sensor_last_raw_t sensor_get_raw(void)
{
    return s_last_raw;
}

void sensor_config_set(uint8_t model_idx, uint8_t slave_id)
{
    if (model_idx >= SENSOR_MODEL_COUNT) model_idx = 0;
    if (slave_id  == 0)                  slave_id  = 1;

    s_cfg.model_idx = model_idx;
    s_cfg.slave_id  = slave_id;

    nvs_handle_t h;
    if (nvs_open("sensor_cfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "model", model_idx);
        nvs_set_u8(h, "slave", slave_id);
        nvs_commit(h);
        nvs_close(h);
    }

    ESP_LOGI(TAG, "Saved: model=%s  slave_id=%d",
             SENSOR_MODELS[s_cfg.model_idx].name, s_cfg.slave_id);
}
