#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Call eth_start_background() first, then wifi_mqtt_set_relay_cb(), then this.
// MQTT client starts automatically once Ethernet gets a DHCP IP.
void wifi_mqtt_init(const char *broker_uri);

void wifi_mqtt_set_relay_cb(void (*cb)(int idx, bool on));
void wifi_mqtt_set_relay_mode_cb(void (*cb)(bool remote));
void wifi_mqtt_set_logo_url_cb(void (*cb)(const char *url));
void wifi_mqtt_set_test_alert_cb(void (*cb)(const char *json, int len));
void wifi_mqtt_set_ip_cb(void (*cb)(const char *ip));
bool wifi_mqtt_is_connected(void);
const char *wifi_mqtt_get_device_id(void);

void wifi_mqtt_publish_sensors(float temp, float hum, int sound,
                               float pm25, float pm10);
void wifi_mqtt_publish_relay_state(int idx, bool on);
void wifi_mqtt_publish_relay_mode_state(bool remote);

#ifdef __cplusplus
}
#endif
