#include "history.h"
#include "cJSON.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *TAG = "HIST";

hist_24h_t g_hist_24h = {};
hist_7d_t  g_hist_7d  = {};

static int parse_arr(cJSON *root, const char *key, float *buf, int maxlen)
{
    cJSON *arr = cJSON_GetObjectItem(root, key);
    if (!arr || !cJSON_IsArray(arr)) return 0;
    int n = cJSON_GetArraySize(arr);
    if (n > maxlen) n = maxlen;
    for (int i = 0; i < n; i++) {
        cJSON *it = cJSON_GetArrayItem(arr, i);
        buf[i] = cJSON_IsNumber(it) ? (float)it->valuedouble : NAN;
    }
    return n;
}

void hist_parse_24h(const char *json, int len)
{
    char *buf = strndup(json, len);
    if (!buf) return;
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) { ESP_LOGW(TAG, "24h JSON parse failed"); return; }

    parse_arr(root, "hum",   g_hist_24h.d[HIST_HUM],   HIST_24H_LEN);
    parse_arr(root, "pm25",  g_hist_24h.d[HIST_PM25],  HIST_24H_LEN);
    parse_arr(root, "pm10",  g_hist_24h.d[HIST_PM10],  HIST_24H_LEN);
    parse_arr(root, "sound", g_hist_24h.d[HIST_SOUND], HIST_24H_LEN);
    g_hist_24h.count = parse_arr(root, "temp", g_hist_24h.d[HIST_TEMP], HIST_24H_LEN);

    ESP_LOGI(TAG, "24h parsed: %d points", g_hist_24h.count);
    cJSON_Delete(root);
}

void hist_parse_7d(const char *json, int len)
{
    char *buf = strndup(json, len);
    if (!buf) return;
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) { ESP_LOGW(TAG, "7d JSON parse failed"); return; }

    parse_arr(root, "hum",   g_hist_7d.d[HIST_HUM],   HIST_7D_LEN);
    parse_arr(root, "pm25",  g_hist_7d.d[HIST_PM25],  HIST_7D_LEN);
    parse_arr(root, "pm10",  g_hist_7d.d[HIST_PM10],  HIST_7D_LEN);
    parse_arr(root, "sound", g_hist_7d.d[HIST_SOUND], HIST_7D_LEN);
    g_hist_7d.count = parse_arr(root, "temp", g_hist_7d.d[HIST_TEMP], HIST_7D_LEN);

    ESP_LOGI(TAG, "7d parsed: %d points", g_hist_7d.count);
    cJSON_Delete(root);
}
