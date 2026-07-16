#pragma once
#include <stdbool.h>
#include "lvgl.h"

#define ETH_LOGO_LVGL_PATH     "A:/logo.jpg"
#define ETH_LOGO_SPIFFS        "/spiffs/logo.jpg"
#define ETH_LOGO_HD_LVGL_PATH  "A:/logo_hd.jpg"
#define ETH_LOGO_HD_SPIFFS     "/spiffs/logo_hd.jpg"

#ifdef __cplusplus
extern "C" {
#endif

void eth_upload_init(void);
bool eth_upload_has_logo(void);
void eth_upload_clear_logo(void);
bool eth_logo_fetch_from_url(const char *url);
void eth_upload_start(lv_obj_t *qr_obj, lv_obj_t *ip_label);

// Start Ethernet (DHCP or static from NVS) without the HTTP upload server.
// Returns immediately; fires IP_EVENT_ETH_GOT_IP when IP is assigned.
void eth_start_background(void);

// Returns the derived gateway string (e.g. "192.168.1.1") when static IP is
// active, or an empty string when DHCP is used.  Valid after eth_start_background().
void eth_get_net_gw(char *out, size_t len);

#ifdef __cplusplus
}
#endif
