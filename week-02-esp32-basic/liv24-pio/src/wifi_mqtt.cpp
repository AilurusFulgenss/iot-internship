#include "wifi_mqtt.h"
#include "eth_upload.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "esp_sntp.h"
#include "esp_mac.h"
#include "nvs.h"
#include "lwip/ip4_addr.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t s_mqtt       = NULL;
static volatile bool             s_mqtt_ready = false;
static void (*s_relay_cb)(int, bool)             = NULL;
static void (*s_relay_mode_cb)(bool)             = NULL;
static void (*s_logo_url_cb)(const char *)       = NULL;
static void (*s_test_alert_cb)(const char *, int) = NULL;
static char s_broker_uri[128];

static char   s_device_id[13]        = {};   // 12 hex chars + null
static char   s_topic_prefix[40]     = {};   // "esp32_p4_86/XXXXXXXXXXXX"
static void (*s_ip_cb)(const char *) = NULL;

// Prebuilt topic strings (filled in wifi_mqtt_init)
static char T_RELAY1_SET[52]   = {};
static char T_RELAY2_SET[52]   = {};
static char T_RELAY_MODE[52]   = {};
static char T_LOGO_URL[52]     = {};
static char T_TEST_ALERT[52]   = {};
static char T_SENSORS[52]          = {};
static char T_RELAY1_STATE[52]     = {};
static char T_RELAY2_STATE[52]     = {};
static char T_RELAY_MODE_STATE[56] = {};

// ── MQTT events ───────────────────────────────────────────────────────────────

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)data;
    switch ((esp_mqtt_event_id_t)id) {

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected → %s", s_broker_uri);
        s_mqtt_ready = true;
        esp_mqtt_client_subscribe(s_mqtt, T_RELAY1_SET,   0);
        esp_mqtt_client_subscribe(s_mqtt, T_RELAY2_SET,   0);
        esp_mqtt_client_subscribe(s_mqtt, T_RELAY_MODE,   1);
        esp_mqtt_client_subscribe(s_mqtt, T_LOGO_URL,     1);
        esp_mqtt_client_subscribe(s_mqtt, T_TEST_ALERT,   0);
        // broker delivers retained messages automatically on subscribe — no request needed
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected (auto-retry)");
        s_mqtt_ready = false;
        break;

    case MQTT_EVENT_DATA:
        if (!s_relay_cb || ev->topic_len == 0 || ev->data_len == 0) break;
        {
            char topic[56] = {};
            int tlen = ev->topic_len < (int)sizeof(topic) - 1
                       ? ev->topic_len : (int)sizeof(topic) - 1;
            memcpy(topic, ev->topic, tlen);

            if (strcmp(topic, T_TEST_ALERT) == 0) {
                if (s_test_alert_cb) s_test_alert_cb(ev->data, ev->data_len);
                break;
            }
            if (strcmp(topic, T_RELAY_MODE) == 0) {
                bool remote = (ev->data_len >= 6 &&
                               strncmp(ev->data, "remote", 6) == 0);
                ESP_LOGI(TAG, "Relay mode -> %s", remote ? "remote" : "local");
                if (s_relay_mode_cb) s_relay_mode_cb(remote);
                break;
            }
            if (strcmp(topic, T_LOGO_URL) == 0) {
                if (s_logo_url_cb && ev->data_len > 0) {
                    char url[256] = {};
                    int ulen = ev->data_len < (int)sizeof(url) - 1
                               ? ev->data_len : (int)sizeof(url) - 1;
                    memcpy(url, ev->data, ulen);
                    s_logo_url_cb(url);
                }
                break;
            }

            int relay_idx = -1;
            if      (strcmp(topic, T_RELAY1_SET) == 0) relay_idx = 0;
            else if (strcmp(topic, T_RELAY2_SET) == 0) relay_idx = 1;
            if (relay_idx < 0) break;

            bool on = (ev->data_len >= 2 &&
                       ev->data[0] == 'O' && ev->data[1] == 'N');
            ESP_LOGI(TAG, "Relay %d → %s", relay_idx + 1, on ? "ON" : "OFF");
            s_relay_cb(relay_idx, on);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;

    default:
        break;
    }
}

static void start_mqtt_client(void)
{
    if (s_mqtt) {
        esp_mqtt_client_reconnect(s_mqtt);
        return;
    }
    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = s_broker_uri;
    cfg.buffer.size        = 2048;
#ifdef MQTT_USERNAME
    cfg.credentials.username = MQTT_USERNAME;
    cfg.credentials.authentication.password = MQTT_PASSWORD;
#endif
    s_mqtt = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_mqtt, MQTT_EVENT_ANY,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt);
}

// ── Gateway TCP probe → warms up ARP cache so DNS works immediately ───────────
// eth-test proved: MQTT connects in <1 s after gateway ARP is in cache.
// Without this, getaddrinfo() times out (EAI_AGAIN / 202) because the first
// ARP for the DNS server never gets a reply before lwIP's DNS timeout fires.

static void mqtt_start_task(void *arg)
{
    char gw_str[16];
    eth_get_net_gw(gw_str, sizeof(gw_str));
    if (gw_str[0]) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock >= 0) {
            struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            struct sockaddr_in gw = {};
            gw.sin_family = AF_INET;
            gw.sin_port   = htons(80);
            ip4addr_aton(gw_str, (ip4_addr_t *)&gw.sin_addr);
            bool ok = (connect(sock, (struct sockaddr *)&gw, sizeof(gw)) == 0);
            close(sock);
            if (ok)
                ESP_LOGI(TAG, "  gateway probe PASS — ARP cache warm, starting MQTT");
            else
                ESP_LOGW(TAG, "  gateway probe FAIL — Ethernet RX may be broken");
        }
    }
    start_mqtt_client();
    vTaskDelete(NULL);
}

// ── Ethernet IP event → start MQTT ────────────────────────────────────────────

static void eth_got_ip_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "Ethernet IP: " IPSTR " — starting MQTT client",
             IP2STR(&ev->ip_info.ip));

    if (s_ip_cb) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ev->ip_info.ip));
        s_ip_cb(ip_str);
    }

    {
        char gw_str[16];
        eth_get_net_gw(gw_str, sizeof(gw_str));
        if (gw_str[0]) {
            // Static IP — use derived gateway as primary DNS, Google as backup
            esp_netif_dns_info_t dns = {};
            dns.ip.type = ESP_IPADDR_TYPE_V4;
            ip4addr_aton(gw_str, (ip4_addr_t *)&dns.ip.u_addr.ip4);
            esp_netif_set_dns_info(ev->esp_netif, ESP_NETIF_DNS_MAIN, &dns);
            ip4addr_aton("8.8.8.8", (ip4_addr_t *)&dns.ip.u_addr.ip4);
            esp_netif_set_dns_info(ev->esp_netif, ESP_NETIF_DNS_BACKUP, &dns);
            ESP_LOGI(TAG, "DNS set: primary=%s backup=8.8.8.8", gw_str);
        }
    }

    // Sync time via NTP — Bangkok timezone (UTC+7)
    setenv("TZ", "ICT-7", 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.cloudflare.com");
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started — syncing time");

    xTaskCreate(mqtt_start_task, "mqtt_start", 4096, NULL, 3, NULL);
}

// Fallback: poll every 5 s — logs DHCP state each cycle for diagnostics
static void ip_poll_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (s_mqtt) { vTaskDelete(NULL); return; }

        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
        if (!netif) {
            ESP_LOGW(TAG, "ip_poll: ETH_DEF netif not found");
            continue;
        }

        esp_netif_dhcp_status_t dhcp_status = ESP_NETIF_DHCP_INIT;
        esp_netif_dhcpc_get_status(netif, &dhcp_status);

        esp_netif_ip_info_t ip = {};
        esp_netif_get_ip_info(netif, &ip);

        // dhcp_status: 0=INIT 1=STARTED 2=STOPPED
        ESP_LOGI(TAG, "ip_poll: dhcp=%d ip=" IPSTR, (int)dhcp_status, IP2STR(&ip.ip));

        if (ip.ip.addr != 0) {
            ESP_LOGI(TAG, "IP poll got: " IPSTR " — starting MQTT", IP2STR(&ip.ip));
            start_mqtt_client();
            vTaskDelete(NULL);
        }
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void wifi_mqtt_init(const char *broker_uri_fallback)
{
    // Build device ID from base MAC (lazy-safe: get_device_id() may already have it)
    if (s_device_id[0] == '\0') {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_BASE);
        snprintf(s_device_id, sizeof(s_device_id), "%02x%02x%02x%02x%02x%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    snprintf(s_topic_prefix, sizeof(s_topic_prefix), "esp32_p4_86/%s", s_device_id);

    snprintf(T_RELAY1_SET,   sizeof(T_RELAY1_SET),   "%s/relay/1/set",   s_topic_prefix);
    snprintf(T_RELAY2_SET,   sizeof(T_RELAY2_SET),   "%s/relay/2/set",   s_topic_prefix);
    snprintf(T_RELAY_MODE,   sizeof(T_RELAY_MODE),   "%s/relay/mode",    s_topic_prefix);
    snprintf(T_LOGO_URL,     sizeof(T_LOGO_URL),     "%s/logo/url",      s_topic_prefix);
    snprintf(T_TEST_ALERT,   sizeof(T_TEST_ALERT),   "%s/test/alert",    s_topic_prefix);
    snprintf(T_SENSORS,      sizeof(T_SENSORS),      "%s/sensors",       s_topic_prefix);
    snprintf(T_RELAY1_STATE,     sizeof(T_RELAY1_STATE),     "%s/relay/1/state",    s_topic_prefix);
    snprintf(T_RELAY2_STATE,     sizeof(T_RELAY2_STATE),     "%s/relay/2/state",    s_topic_prefix);
    snprintf(T_RELAY_MODE_STATE, sizeof(T_RELAY_MODE_STATE), "%s/relay/mode/state", s_topic_prefix);

    ESP_LOGI(TAG, "Device ID: %s  prefix: %s", s_device_id, s_topic_prefix);

    // Read custom broker from NVS; fall back to build-flag URI
    {
        char host[64] = "";
        uint16_t port = 1883;
        nvs_handle_t h;
        if (nvs_open("mqtt_cfg", NVS_READONLY, &h) == ESP_OK) {
            size_t len = sizeof(host);
            nvs_get_str(h, "host", host, &len);
            nvs_get_u16(h, "port", &port);
            nvs_close(h);
        }
        if (host[0]) {
            snprintf(s_broker_uri, sizeof(s_broker_uri), "mqtt://%s:%u", host, port);
            ESP_LOGI(TAG, "MQTT broker from NVS: %s", s_broker_uri);
        } else {
            strncpy(s_broker_uri, broker_uri_fallback, sizeof(s_broker_uri) - 1);
        }
    }

    // esp_netif_init() + esp_event_loop_create_default() called once in app_main.
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                               eth_got_ip_handler, NULL));
    xTaskCreate(ip_poll_task, "ip_poll", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "MQTT ready — broker: %s (waiting for Ethernet IP)", s_broker_uri);
}

void wifi_mqtt_set_relay_cb(void (*cb)(int idx, bool on))
{
    s_relay_cb = cb;
}

void wifi_mqtt_set_relay_mode_cb(void (*cb)(bool remote))
{
    s_relay_mode_cb = cb;
}

void wifi_mqtt_set_logo_url_cb(void (*cb)(const char *url))
{
    s_logo_url_cb = cb;
}

void wifi_mqtt_set_test_alert_cb(void (*cb)(const char *json, int len))
{
    s_test_alert_cb = cb;
}

bool wifi_mqtt_is_connected(void)
{
    return s_mqtt_ready;
}

void wifi_mqtt_publish_relay_state(int idx, bool on)
{
    if (!s_mqtt_ready) return;
    const char *topic = (idx == 0) ? T_RELAY1_STATE : T_RELAY2_STATE;
    esp_mqtt_client_publish(s_mqtt, topic, on ? "ON" : "OFF", 0, 1, 1);
}

void wifi_mqtt_publish_relay_mode_state(bool remote)
{
    if (!s_mqtt_ready) return;
    esp_mqtt_client_publish(s_mqtt, T_RELAY_MODE_STATE,
                            remote ? "remote" : "local", 0, 1, 1);
}

void wifi_mqtt_publish_sensors(float temp, float hum, int sound,
                               float pm25, float pm10)
{
    if (!s_mqtt_ready) return;
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"temperature\":%.1f,\"humidity\":%.1f,\"sound\":%d,\"pm2_5\":%.1f,\"pm10\":%.1f}",
             temp, hum, sound, pm25, pm10);
    esp_mqtt_client_publish(s_mqtt, T_SENSORS, payload, 0, 0, 0);
}

const char *wifi_mqtt_get_device_id(void)
{
    if (s_device_id[0] == '\0') {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_BASE);
        snprintf(s_device_id, sizeof(s_device_id), "%02x%02x%02x%02x%02x%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    return s_device_id;
}

void wifi_mqtt_set_ip_cb(void (*cb)(const char *ip))
{
    s_ip_cb = cb;
}

