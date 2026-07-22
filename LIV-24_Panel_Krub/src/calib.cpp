#include "calib.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG     = "CALIB";
static const char *NVS_NS  = "calib";
static const char *NVS_KEY = "v3";

calib_data_t g_calib = {
    {0.0f, 1.0f},   // temp
    {0.0f, 1.0f},   // hum
    {0.0f, 1.0f},   // sound
    {0.0f, 1.0f},   // pm25
    {0.0f, 1.0f},   // pm10
};

void calib_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed");
        return;
    }
    nvs_set_blob(h, NVS_KEY, &g_calib, sizeof(g_calib));
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "calib saved");
}

static void calib_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "no saved calib, using defaults");
        return;
    }
    size_t sz = sizeof(g_calib);
    if (nvs_get_blob(h, NVS_KEY, &g_calib, &sz) == ESP_OK)
        ESP_LOGI(TAG, "calib loaded");
    nvs_close(h);
}

void calib_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    calib_load();
}
