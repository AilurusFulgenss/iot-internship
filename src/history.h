#pragma once
#include <math.h>

#define HIST_24H_LEN  24
#define HIST_7D_LEN    7

// PM sensor channels (used in both 24h and 7d history)
#define HIST_TEMP      0
#define HIST_HUM       1
#define HIST_PM25      2
#define HIST_PM10      3
#define HIST_SOUND     4

// Water quality
#define HIST_EC        5
#define HIST_TDS       6
#define HIST_ORP       7
#define HIST_ORP_TEMP  8

// HHCC Flora
#define HIST_HHCC_MOIST  9
#define HIST_HHCC_LIGHT 10
#define HIST_HHCC_FERT  11
#define HIST_HHCC_TEMP  12

// CWT-TH04S
#define HIST_TH_TEMP   13
#define HIST_TH_HUM    14

// Leak (daily alarm event count)
#define HIST_LEAK      15

#define HIST_SENS      5   // 24h history tracks only PM channels
#define HIST_SENS_7D  16   // 7d history tracks all sensor channels

typedef struct {
    float d[HIST_SENS][HIST_24H_LEN];
    int count;
} hist_24h_t;

typedef struct {
    float d[HIST_SENS_7D][HIST_7D_LEN];
    int count;       // PM 7d count
    int cnt_ec;
    int cnt_orp;
    int cnt_hhcc;
    int cnt_th;
    int cnt_leak;
} hist_7d_t;

extern hist_24h_t g_hist_24h;
extern hist_7d_t  g_hist_7d;

void hist_parse_24h (const char *json, int len);
void hist_parse_7d  (const char *json, int len);
void hist_parse_ec  (const char *json, int len);
void hist_parse_orp (const char *json, int len);
void hist_parse_hhcc(const char *json, int len);
void hist_parse_th  (const char *json, int len);
void hist_parse_leak(const char *json, int len);
