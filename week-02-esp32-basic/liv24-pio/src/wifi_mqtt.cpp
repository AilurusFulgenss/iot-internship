#include "wifi_mqtt.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "lwip/ip4_addr.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t s_mqtt       = NULL;
static volatile bool             s_mqtt_ready = false;
static void (*s_relay_cb)(int, bool)         = NULL;
static void (*s_logo_url_cb)(const char *)   = NULL;
static char s_broker_uri[128];

// ── MQTT events ───────────────────────────────────────────────────────────────

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)data;
    switch ((esp_mqtt_event_id_t)id) {

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected → %s", s_broker_uri);
        s_mqtt_ready = true;
        esp_mqtt_client_subscribe(s_mqtt, "liv24/relay/1/set", 0);
        esp_mqtt_client_subscribe(s_mqtt, "liv24/relay/2/set", 0);
        esp_mqtt_client_subscribe(s_mqtt, "liv24/logo/url",    1);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected (auto-retry)");
        s_mqtt_ready = false;
        break;

    case MQTT_EVENT_DATA:
        if (!s_relay_cb || ev->topic_len == 0 || ev->data_len == 0) break;
        {
            char topic[32] = {};
            int tlen = ev->topic_len < (int)sizeof(topic) - 1
                       ? ev->topic_len : (int)sizeof(topic) - 1;
            memcpy(topic, ev->topic, tlen);

            if (strcmp(topic, "liv24/logo/url") == 0) {
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
            if      (strcmp(topic, "liv24/relay/1/set") == 0) relay_idx = 0;
            else if (strcmp(topic, "liv24/relay/2/set") == 0) relay_idx = 1;
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
#ifdef STATIC_GW_ADDR
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock >= 0) {
        struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        struct sockaddr_in gw = {};
        gw.sin_family = AF_INET;
        gw.sin_port   = htons(80);
        ip4addr_aton(STATIC_GW_ADDR, (ip4_addr_t *)&gw.sin_addr);
        bool ok = (connect(sock, (struct sockaddr *)&gw, sizeof(gw)) == 0);
        close(sock);
        if (ok)
            ESP_LOGI(TAG, "  gateway probe PASS — ARP cache warm, starting MQTT");
        else
            ESP_LOGW(TAG, "  gateway probe FAIL — Ethernet RX may be broken");
    }
#endif
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

#if defined(STATIC_IP_ADDR) && defined(STATIC_DNS_ADDR)
    // Set DNS here (in IP event) so it's configured right before MQTT resolves
    esp_netif_dns_info_t dns = {};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    ip4addr_aton(STATIC_DNS_ADDR, (ip4_addr_t *)&dns.ip.u_addr.ip4);
    esp_netif_set_dns_info(ev->esp_netif, ESP_NETIF_DNS_MAIN, &dns);
    ip4addr_aton("8.8.8.8", (ip4_addr_t *)&dns.ip.u_addr.ip4);
    esp_netif_set_dns_info(ev->esp_netif, ESP_NETIF_DNS_BACKUP, &dns);
    ESP_LOGI(TAG, "DNS set: primary=" STATIC_DNS_ADDR " backup=8.8.8.8");
#elif defined(STATIC_IP_ADDR)
    // Fallback: use gateway as DNS + Google backup
    esp_netif_dns_info_t dns = {};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    ip4addr_aton(STATIC_GW_ADDR, (ip4_addr_t *)&dns.ip.u_addr.ip4);
    esp_netif_set_dns_info(ev->esp_netif, ESP_NETIF_DNS_MAIN, &dns);
    ip4addr_aton("8.8.8.8", (ip4_addr_t *)&dns.ip.u_addr.ip4);
    esp_netif_set_dns_info(ev->esp_netif, ESP_NETIF_DNS_BACKUP, &dns);
    ESP_LOGI(TAG, "DNS set: primary=" STATIC_GW_ADDR " backup=8.8.8.8");
#endif

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

void wifi_mqtt_init(const char *broker_uri)
{
    strncpy(s_broker_uri, broker_uri, sizeof(s_broker_uri) - 1);
    // esp_netif_init() + esp_event_loop_create_default() called once in app_main.
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                               eth_got_ip_handler, NULL));
    xTaskCreate(ip_poll_task, "ip_poll", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "MQTT ready — broker: %s (waiting for Ethernet IP)", broker_uri);
}

void wifi_mqtt_set_relay_cb(void (*cb)(int idx, bool on))
{
    s_relay_cb = cb;
}

void wifi_mqtt_set_logo_url_cb(void (*cb)(const char *url))
{
    s_logo_url_cb = cb;
}

bool wifi_mqtt_is_connected(void)
{
    return s_mqtt_ready;
}

void wifi_mqtt_publish_sensors(float temp, float hum, int sound,
                               float pm25, float pm10)
{
    if (!s_mqtt_ready) return;
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"temp\":%.1f,\"hum\":%.1f,\"sound\":%d,\"pm25\":%.1f,\"pm10\":%.1f}",
             temp, hum, sound, pm25, pm10);
    esp_mqtt_client_publish(s_mqtt, "liv24/sensors", payload, 0, 0, 0);
}
