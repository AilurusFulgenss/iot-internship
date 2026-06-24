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

// JSON: {"ec":[...7 values...], "tds":[...7 values...]}
void hist_parse_ec(const char *json, int len)
{
    char *buf = strndup(json, len);
    if (!buf) return;
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) { ESP_LOGW(TAG, "ec 7d JSON parse failed"); return; }

    int n_ec  = parse_arr(root, "ec",  g_hist_7d.d[HIST_EC],  HIST_7D_LEN);
    int n_tds = parse_arr(root, "tds", g_hist_7d.d[HIST_TDS], HIST_7D_LEN);
    g_hist_7d.cnt_ec = n_ec > 0 ? n_ec : n_tds;

    ESP_LOGI(TAG, "ec 7d parsed: %d points", g_hist_7d.cnt_ec);
    cJSON_Delete(root);
}

// JSON: {"orp":[...], "temp":[...]}
void hist_parse_orp(const char *json, int len)
{
    char *buf = strndup(json, len);
    if (!buf) return;
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) { ESP_LOGW(TAG, "orp 7d JSON parse failed"); return; }

    parse_arr(root, "orp",  g_hist_7d.d[HIST_ORP],      HIST_7D_LEN);
    g_hist_7d.cnt_orp = parse_arr(root, "temp", g_hist_7d.d[HIST_ORP_TEMP], HIST_7D_LEN);

    ESP_LOGI(TAG, "orp 7d parsed: %d points", g_hist_7d.cnt_orp);
    cJSON_Delete(root);
}

// JSON: {"moist":[...], "light":[...], "fert":[...], "temp":[...]}
void hist_parse_hhcc(const char *json, int len)
{
    char *buf = strndup(json, len);
    if (!buf) return;
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) { ESP_LOGW(TAG, "hhcc 7d JSON parse failed"); return; }

    parse_arr(root, "moist", g_hist_7d.d[HIST_HHCC_MOIST], HIST_7D_LEN);
    parse_arr(root, "light", g_hist_7d.d[HIST_HHCC_LIGHT], HIST_7D_LEN);
    parse_arr(root, "fert",  g_hist_7d.d[HIST_HHCC_FERT],  HIST_7D_LEN);
    g_hist_7d.cnt_hhcc = parse_arr(root, "temp", g_hist_7d.d[HIST_HHCC_TEMP], HIST_7D_LEN);

    ESP_LOGI(TAG, "hhcc 7d parsed: %d points", g_hist_7d.cnt_hhcc);
    cJSON_Delete(root);
}

// JSON: {"temp":[...], "hum":[...]}
void hist_parse_th(const char *json, int len)
{
    char *buf = strndup(json, len);
    if (!buf) return;
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) { ESP_LOGW(TAG, "th 7d JSON parse failed"); return; }

    parse_arr(root, "hum",  g_hist_7d.d[HIST_TH_HUM],  HIST_7D_LEN);
    g_hist_7d.cnt_th = parse_arr(root, "temp", g_hist_7d.d[HIST_TH_TEMP], HIST_7D_LEN);

    ESP_LOGI(TAG, "th 7d parsed: %d points", g_hist_7d.cnt_th);
    cJSON_Delete(root);
}

// JSON: {"count":[...7 daily alarm counts...]}
void hist_parse_leak(const char *json, int len)
{
    char *buf = strndup(json, len);
    if (!buf) return;
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) { ESP_LOGW(TAG, "leak 7d JSON parse failed"); return; }

    g_hist_7d.cnt_leak = parse_arr(root, "count", g_hist_7d.d[HIST_LEAK], HIST_7D_LEN);

    ESP_LOGI(TAG, "leak 7d parsed: %d points", g_hist_7d.cnt_leak);
    cJSON_Delete(root);
}
