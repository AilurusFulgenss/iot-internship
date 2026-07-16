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
void wifi_mqtt_set_history_cb(void (*cb24h)(const char *d, int len),
                              void (*cb7d )(const char *d, int len));
void wifi_mqtt_set_test_alert_cb(void (*cb)(const char *json, int len));
bool wifi_mqtt_is_connected(void);

void wifi_mqtt_publish_sensors(float temp, float hum, int sound,
                               float pm25, float pm10);
void wifi_mqtt_publish_relay_state(int idx, bool on);

#ifdef __cplusplus
}
#endif
