#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Call eth_start_background() first, then wifi_mqtt_set_relay_cb(), then this.
// MQTT client starts automatically once Ethernet gets a DHCP IP.
void wifi_mqtt_init(const char *broker_uri);

void wifi_mqtt_set_relay_cb(void (*cb)(int idx, bool on));
void wifi_mqtt_set_logo_url_cb(void (*cb)(const char *url));
bool wifi_mqtt_is_connected(void);

void wifi_mqtt_publish_sensors(float temp, float hum, int sound,
                               float pm25, float pm10);

#ifdef __cplusplus
}
#endif
