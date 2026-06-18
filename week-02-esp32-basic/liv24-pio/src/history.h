#pragma once
#include <math.h>

#define HIST_SENS      5
#define HIST_24H_LEN  24
#define HIST_7D_LEN    7

#define HIST_TEMP   0
#define HIST_HUM    1
#define HIST_PM25   2
#define HIST_PM10   3
#define HIST_SOUND  4

typedef struct { float d[HIST_SENS][HIST_24H_LEN]; int count; } hist_24h_t;
typedef struct { float d[HIST_SENS][HIST_7D_LEN];  int count; } hist_7d_t;

extern hist_24h_t g_hist_24h;
extern hist_7d_t  g_hist_7d;

void hist_parse_24h(const char *json, int len);
void hist_parse_7d (const char *json, int len);
