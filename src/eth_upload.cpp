#include "eth_upload.h"
#include "wifi_mqtt.h"
#include "sensor_config.h"
#include "esp_log.h"
#include "nvs.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_eth_mac_esp.h"
#include "esp_eth_phy.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "ETH_UPLOAD";
static bool s_has_logo = false;
static char s_gw_str[16] = "";  // filled by eth_start_background(); "" = DHCP

#define WEB_PASSWORD       "999999"
#define SESSION_LIFETIME_S (12 * 3600)
static char    s_sess_token[33]  = {};   // 32 hex chars + null; empty = no session
static int64_t s_sess_expiry_us  = 0;   // esp_timer_get_time() at expiry

void eth_get_net_gw(char *out, size_t len)
{
    strncpy(out, s_gw_str, len);
    out[len - 1] = '\0';
}
static lv_obj_t *s_ip_label = NULL;
static lv_obj_t *s_qr_obj   = NULL;
static esp_eth_handle_t s_eth_handle = NULL;

#define ETH_PHY_POWER_GPIO  51
#define ETH_PHY_ADDR        1

// ── SPIFFS init ───────────────────────────────────────────────────────────────

void eth_upload_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = "/spiffs",
        .partition_label        = NULL,
        .max_files              = 5,
        .format_if_mount_failed = true
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        s_has_logo = false;
        return;
    }
    struct stat st;
    s_has_logo = (stat(ETH_LOGO_SPIFFS, &st) == 0 && st.st_size > 0);
    ESP_LOGI(TAG, "SPIFFS mounted. Logo: %s (%ld bytes)",
             s_has_logo ? "found" : "not found",
             s_has_logo ? (long)st.st_size : 0L);
}

bool eth_upload_has_logo(void)
{
    return s_has_logo;
}

void eth_upload_clear_logo(void)
{
    // Mount SPIFFS if not already mounted, then delete the logo file
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = "/spiffs",
        .partition_label        = NULL,
        .max_files              = 1,
        .format_if_mount_failed = false
    };
    esp_vfs_spiffs_register(&conf);   // ignore error if already mounted
    remove(ETH_LOGO_SPIFFS);
    remove(ETH_LOGO_HD_SPIFFS);
    s_has_logo = false;
    ESP_LOGW(TAG, "Logo cleared from SPIFFS");
}

// ── Session helpers ───────────────────────────────────────────────────────────

static void gen_session(void)
{
    for (int i = 0; i < 4; i++) {
        uint32_t r = esp_random();
        snprintf(s_sess_token + i * 8, 9, "%08x", (unsigned)r);
    }
    s_sess_expiry_us = esp_timer_get_time()
                       + (int64_t)SESSION_LIFETIME_S * 1000000LL;
    ESP_LOGI(TAG, "Session created, expires in %d h", SESSION_LIFETIME_S / 3600);
}

static bool is_authenticated(httpd_req_t *req)
{
    if (s_sess_token[0] == '\0' || esp_timer_get_time() > s_sess_expiry_us)
        return false;
    char cookie[128] = {};
    size_t clen = httpd_req_get_hdr_value_len(req, "Cookie");
    if (clen == 0 || clen >= sizeof(cookie)) return false;
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) != ESP_OK)
        return false;
    char *p = strstr(cookie, "sess=");
    return (p && strncmp(p + 5, s_sess_token, 32) == 0);
}

static esp_err_t redirect_to_login(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/login");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ── HTTP handlers ─────────────────────────────────────────────────────────────

static const char HTML_LOGIN[] =
"<!DOCTYPE html><html lang='en'><head>"
"<meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>LIV24 Login</title><style>"
"body{background:#0a0a12;color:#eee;font-family:sans-serif;"
"max-width:320px;margin:80px auto;padding:20px;text-align:center}"
"h2{color:#00e5ff;margin:0 0 16px}"
"input{width:100%;padding:14px;background:#111;color:#eee;"
"border:1px solid #334455;border-radius:8px;font-size:22px;"
"letter-spacing:8px;text-align:center;box-sizing:border-box;margin:8px 0}"
"button{padding:14px 0;background:#00e5ff;color:#111;border:none;"
"border-radius:10px;font-size:16px;font-weight:bold;cursor:pointer;"
"width:100%;margin-top:8px}"
"#err{color:#ff4444;font-size:14px;margin-top:12px;min-height:20px}"
"</style></head><body>"
"<h2>LIV24 Setup</h2>"
"<p style='color:#556677'>Enter password to continue</p>"
"<form method='POST' action='/login'>"
"<input type='password' name='pass' maxlength='16' autofocus>"
"<button type='submit'>Unlock</button>"
"</form>"
"<div id='err'></div>"
"<script>if(location.search.indexOf('e=1')>=0)"
"document.getElementById('err').textContent='Incorrect password';</script>"
"</body></html>";

static const char HTML[] =
"<!DOCTYPE html>"
"<html lang='en'>"
"<head>"
"<meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>LIV24 Setup</title>"
"<style>"
"body{background:#0a0a12;color:#eee;font-family:sans-serif;"
"max-width:420px;margin:40px auto;padding:20px;text-align:center}"
"h2{color:#00e5ff;margin:0 0 8px}"
"p{color:#556677;font-size:14px;margin:4px 0}"
"#zone{border:2px dashed #00e5ff;border-radius:16px;padding:36px 20px;"
"margin:20px 0;cursor:pointer;transition:background .2s}"
"#zone.on{background:#00e5ff18}"
"#preview{max-width:180px;max-height:180px;border-radius:12px;"
"display:none;margin:12px auto;border:2px solid #1a2a3a}"
"button{padding:14px 0;background:#00e5ff;color:#111;border:none;"
"border-radius:10px;font-size:16px;font-weight:bold;cursor:pointer;"
"width:100%;margin-top:8px}"
"button:disabled{background:#1a2a3a;color:#445566;cursor:default}"
"#st{margin-top:16px;min-height:22px;font-size:15px;color:#00cc44}"
"</style>"
"</head>"
"<body>"
"<h2>LIV24 Logo Setup</h2>"
"<p>Connected via <b style='color:#00ff88'>Ethernet</b> — upload your logo below.</p>"
"<p style='color:#334455;font-size:12px'>PNG or JPEG — browser resizes to 128×128 before upload</p>"
"<div id='zone' onclick=\"document.getElementById('f').click()\">"
"<p style='color:#445566'>Tap to choose image</p>"
"<input type='file' id='f' accept='image/*' style='display:none' onchange='picked(this)'>"
"</div>"
"<img id='preview' alt=''>"
"<button id='btn' disabled onclick='doUpload()'>Upload &amp; Restart</button>"
"<div id='st'></div>"
"<script>"
"var b48=null,b192=null;"
"function mkBlob(img,sz,cb){"
"  var c=document.createElement('canvas');c.width=sz;c.height=sz;"
"  var x=c.getContext('2d');"
"  x.fillStyle='#fff';x.fillRect(0,0,sz,sz);"
"  var s=Math.min(sz/img.width,sz/img.height);"
"  x.drawImage(img,(sz-img.width*s)/2,(sz-img.height*s)/2,img.width*s,img.height*s);"
"  c.toBlob(function(b){cb(b,c);},'image/jpeg',0.9);"
"}"
"function picked(inp){"
"  var file=inp.files[0];if(!file)return;"
"  var img=new Image();"
"  img.onload=function(){"
"    mkBlob(img,48,function(b){b48=b;"
"      mkBlob(img,192,function(b2,c){b192=b2;"
"        document.getElementById('preview').src=c.toDataURL();"
"        document.getElementById('preview').style.display='block';"
"        document.getElementById('zone').innerHTML='<p style=\\'color:#00e5ff\\'>'+file.name+'</p>';"
"        document.getElementById('btn').disabled=false;"
"      });"
"    });"
"  };"
"  img.src=URL.createObjectURL(file);"
"}"
"function doUpload(){"
"  if(!b48||!b192)return;"
"  var btn=document.getElementById('btn'),st=document.getElementById('st');"
"  btn.disabled=true;st.style.color='#00e5ff';st.innerText='Uploading...';"
"  fetch('/upload_hd',{method:'POST',headers:{'Content-Type':'image/jpeg'},body:b192})"
"  .then(function(){return fetch('/upload',{method:'POST',headers:{'Content-Type':'image/jpeg'},body:b48});})"
"  .then(function(r){return r.text();})"
"  .then(function(t){st.style.color='#00cc44';st.innerText=t;})"
"  .catch(function(e){st.style.color='#ff4444';st.innerText='Error: '+e;btn.disabled=false;});"
"}"
"</script>"
"<hr style='border:1px solid #1a2a3a;margin:28px 0'>"
"<h2>MQTT Config</h2>"
"<p id='dev_p' style='color:#445566;font-size:12px'>Loading device info...</p>"
"<div style='display:flex;align-items:center;gap:12px;margin:8px 0'>"
"<span style='color:#556677;font-size:14px;white-space:nowrap'>Broker IP</span>"
"<input type='text' id='mhost' placeholder='192.168.1.111'"
" style='flex:1;padding:10px;background:#111;color:#eee;"
"border:1px solid #334455;border-radius:8px;font-size:15px'>"
"</div>"
"<div style='display:flex;align-items:center;gap:12px;margin:8px 0'>"
"<span style='color:#556677;font-size:14px;white-space:nowrap'>Port</span>"
"<input type='number' id='mport' min='1' max='65535' value='1883'"
" style='flex:1;padding:10px;background:#111;color:#eee;"
"border:1px solid #334455;border-radius:8px;font-size:15px'>"
"</div>"
"<button onclick='saveMqtt()'>Save MQTT Config</button>"
"<div id='mst' style='margin-top:10px;min-height:20px;font-size:14px;color:#00cc44'></div>"
"<script>"
"fetch('/mqtt_info').then(function(r){return r.json();}).then(function(d){"
"  document.getElementById('mhost').value=d.host||'';"
"  document.getElementById('mport').value=d.port||1883;"
"  document.getElementById('dev_p').textContent="
"    'Device ID: '+d.device_id+'  |  Topic prefix: '+d.prefix;"
"}).catch(function(){});"
"function saveMqtt(){"
"  var h=document.getElementById('mhost').value.trim();"
"  var p=document.getElementById('mport').value;"
"  var st=document.getElementById('mst');"
"  fetch('/mqtt_cfg',{method:'POST',"
"    headers:{'Content-Type':'application/x-www-form-urlencoded'},"
"    body:'host='+encodeURIComponent(h)+'&port='+p})"
"  .then(function(r){return r.text();})"
"  .then(function(t){st.style.color='#00cc44';st.textContent=t;})"
"  .catch(function(e){st.style.color='#ff4444';st.textContent='Error: '+e;});}"
"</script>"
"<hr style='border:1px solid #1a2a3a;margin:20px 0'>"
"<h2>HA Config</h2>"
"<div style='display:flex;align-items:center;gap:12px;margin:8px 0'>"
"<span style='color:#556677;font-size:14px;white-space:nowrap'>HA Base URL</span>"
"<input type='text' id='haurl' placeholder='http://10.24.1.104:8123'"
" style='flex:1;padding:10px;background:#111;color:#eee;"
"border:1px solid #334455;border-radius:8px;font-size:15px'>"
"</div>"
"<button onclick='saveHA()'>Save HA Config</button>"
"<div id='hast' style='margin-top:10px;min-height:20px;font-size:14px;color:#00cc44'></div>"
"<script>"
"fetch('/ha_info').then(function(r){return r.json();})"
".then(function(d){document.getElementById('haurl').value=d.url||'';}).catch(function(){});"
"function saveHA(){"
"  var u=document.getElementById('haurl').value.trim();"
"  var st=document.getElementById('hast');"
"  fetch('/ha_cfg',{method:'POST',"
"    headers:{'Content-Type':'application/x-www-form-urlencoded'},"
"    body:'url='+encodeURIComponent(u)})"
"  .then(function(r){return r.text();})"
"  .then(function(t){st.style.color='#00cc44';st.textContent=t;})"
"  .catch(function(e){st.style.color='#ff4444';st.textContent='Error: '+e;});}"
"</script>"
"</body>"
"</html>";

static esp_err_t get_login_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, HTML_LOGIN);
}

static esp_err_t post_login_handler(httpd_req_t *req)
{
    char body[64] = {};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len > 0) body[len] = '\0';

    char pass[32] = {};
    char *p = strstr(body, "pass=");
    if (p) {
        strncpy(pass, p + 5, sizeof(pass) - 1);
        char *end = strchr(pass, '&');
        if (end) *end = '\0';
    }

    if (strcmp(pass, WEB_PASSWORD) == 0) {
        gen_session();
        char cookie[96];
        snprintf(cookie, sizeof(cookie),
                 "sess=%s; Max-Age=%d; Path=/; HttpOnly",
                 s_sess_token, SESSION_LIFETIME_S);
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Set-Cookie", cookie);
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
        ESP_LOGI(TAG, "Web login OK");
    } else {
        ESP_LOGW(TAG, "Web login failed (wrong password)");
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/login?e=1");
        httpd_resp_send(req, NULL, 0);
    }
    return ESP_OK;
}

static esp_err_t get_root_handler(httpd_req_t *req)
{
    if (!is_authenticated(req)) return redirect_to_login(req);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t save_to_path(httpd_req_t *req, const char *path)
{
    const size_t MAX_SIZE = 512 * 1024;
    if (req->content_len == 0 || req->content_len > MAX_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad size");
        return ESP_FAIL;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "File open failed");
        return ESP_FAIL;
    }
    uint8_t buf[512];
    int remaining = (int)req->content_len;
    bool ok = true;
    while (remaining > 0) {
        int to_read = remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf);
        int received = httpd_req_recv(req, (char *)buf, to_read);
        if (received <= 0) { ok = false; break; }
        if (fwrite(buf, 1, received, f) != (size_t)received) { ok = false; break; }
        remaining -= received;
    }
    fclose(f);
    if (!ok) { remove(path); return ESP_FAIL; }
    return ESP_OK;
}

static esp_err_t post_upload_hd_handler(httpd_req_t *req)
{
    if (!is_authenticated(req)) return redirect_to_login(req);
    if (save_to_path(req, ETH_LOGO_HD_SPIFFS) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "HD upload failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "logo_hd.jpg saved (%d bytes)", (int)req->content_len);
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

static esp_err_t post_upload_handler(httpd_req_t *req)
{
    if (!is_authenticated(req)) return redirect_to_login(req);
    const size_t MAX_SIZE = 512 * 1024;

    if (req->content_len == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty file");
        return ESP_FAIL;
    }
    if (req->content_len > MAX_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File too large (max 512 KB)");
        return ESP_FAIL;
    }

    FILE *f = fopen(ETH_LOGO_SPIFFS, "wb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "File open failed");
        return ESP_FAIL;
    }

    uint8_t buf[512];
    int remaining = (int)req->content_len;
    bool ok = true;

    while (remaining > 0) {
        int to_read = (remaining < (int)sizeof(buf)) ? remaining : (int)sizeof(buf);
        int received = httpd_req_recv(req, (char *)buf, to_read);
        if (received <= 0) { ok = false; break; }
        if (fwrite(buf, 1, received, f) != (size_t)received) { ok = false; break; }
        remaining -= received;
    }
    fclose(f);

    if (!ok) {
        remove(ETH_LOGO_SPIFFS);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Logo saved (%d bytes). Restarting...", (int)req->content_len);
    httpd_resp_sendstr(req, "Logo uploaded! Restarting device...");
    vTaskDelay(pdMS_TO_TICKS(300));
    // Stop Ethernet DMA before reset — active EMAC DMA during SW_CPU_RESET
    // overwrites bootloader code in SRAM causing "Illegal instruction" crash.
    if (s_eth_handle) esp_eth_stop(s_eth_handle);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return ESP_OK;
}

// ── URL decode helper (for x-www-form-urlencoded bodies) ─────────────────────

static void url_decode(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (*r == '%' && r[1] && r[2]) {
            char hex[3] = {r[1], r[2], '\0'};
            *w++ = (char)strtol(hex, NULL, 16);
            r += 3;
        } else if (*r == '+') {
            *w++ = ' '; r++;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

// ── MQTT config handlers ──────────────────────────────────────────────────────

static esp_err_t get_mqtt_info_handler(httpd_req_t *req)
{
    if (!is_authenticated(req)) return redirect_to_login(req);
    char host[64] = "";
    uint16_t port = 1883;
    {
        nvs_handle_t h;
        if (nvs_open("mqtt_cfg", NVS_READONLY, &h) == ESP_OK) {
            size_t len = sizeof(host);
            nvs_get_str(h, "host", host, &len);
            nvs_get_u16(h, "port", &port);
            nvs_close(h);
        }
    }
    const char *dev_id = wifi_mqtt_get_device_id();
    char prefix[52];
    snprintf(prefix, sizeof(prefix), "esp32_p4_86/%s", dev_id);
    char json[256];
    snprintf(json, sizeof(json),
             "{\"host\":\"%s\",\"port\":%d,\"device_id\":\"%s\",\"prefix\":\"%s\"}",
             host, (int)port, dev_id, prefix);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t post_mqtt_cfg_handler(httpd_req_t *req)
{
    if (!is_authenticated(req)) return redirect_to_login(req);
    char body[128] = {};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[len] = '\0';

    char host[64] = "";
    uint16_t port = 1883;
    char *p = strstr(body, "host=");
    if (p) {
        strncpy(host, p + 5, sizeof(host) - 1);
        char *end = strchr(host, '&'); if (end) *end = '\0';
        // decode '+' as space (simple URL decode)
        for (char *c = host; *c; c++) if (*c == '+') *c = ' ';
    }
    p = strstr(body, "port=");
    if (p) { int v = atoi(p + 5); port = (v > 0 && v < 65536) ? (uint16_t)v : 1883; }

    nvs_handle_t h;
    if (nvs_open("mqtt_cfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "host", host);
        nvs_set_u16(h, "port", port);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "MQTT config saved: %s:%d", host, (int)port);
    }
    httpd_resp_sendstr(req, "MQTT config saved! Restart device to apply.");
    return ESP_OK;
}

// ── HA config handlers ────────────────────────────────────────────────────────

static esp_err_t get_ha_info_handler(httpd_req_t *req)
{
    if (!is_authenticated(req)) return redirect_to_login(req);
    char url[72] = "http://10.24.1.104:8123";
    nvs_handle_t h;
    if (nvs_open("ha_cfg", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(url);
        nvs_get_str(h, "url", url, &len);
        nvs_close(h);
    }
    char json[128];
    snprintf(json, sizeof(json), "{\"url\":\"%s\"}", url);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t post_ha_cfg_handler(httpd_req_t *req)
{
    if (!is_authenticated(req)) return redirect_to_login(req);
    char body[128] = {};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[len] = '\0';

    char url[72] = "";
    char *p = strstr(body, "url=");
    if (p) {
        strncpy(url, p + 4, sizeof(url) - 1);
        url_decode(url);
    }

    nvs_handle_t h;
    if (nvs_open("ha_cfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "url", url);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "HA config saved: %s", url);
    }
    httpd_resp_sendstr(req, "HA config saved! Restart device to apply.");
    return ESP_OK;
}

// ── Ethernet event handler ────────────────────────────────────────────────────

static void eth_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == ETH_EVENT) {
        if (id == ETHERNET_EVENT_CONNECTED)
            ESP_LOGI(TAG, "Ethernet link up");
        else if (id == ETHERNET_EVENT_DISCONNECTED)
            ESP_LOGW(TAG, "Ethernet link down");
    } else if (base == IP_EVENT && id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        char ip_str[48];
        snprintf(ip_str, sizeof(ip_str), "http://" IPSTR "/",
                 IP2STR(&ev->ip_info.ip));
        ESP_LOGI(TAG, "Got IP: %s", ip_str);

        if (bsp_display_lock(500)) {
            if (s_qr_obj) {
                lv_qrcode_update(s_qr_obj, ip_str, strlen(ip_str));
                lv_obj_clear_flag(s_qr_obj, LV_OBJ_FLAG_HIDDEN);
            }
            if (s_ip_label)
                lv_label_set_text(s_ip_label, ip_str);
            bsp_display_unlock();
        }
    }
}

// ── Shared Ethernet hardware init ────────────────────────────────────────────

static void eth_hw_start(esp_netif_t *eth_netif)
{
    // Power up IP101 PHY
    gpio_config_t pwr = {};
    pwr.pin_bit_mask = (1ULL << ETH_PHY_POWER_GPIO);
    pwr.mode         = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&pwr));
    ESP_ERROR_CHECK(gpio_set_level((gpio_num_t)ETH_PHY_POWER_GPIO, 1));
    vTaskDelay(pdMS_TO_TICKS(150));

    // MAC — field-by-field init (ETH_ESP32_EMAC_DEFAULT_CONFIG() macro has wrong field
    // order for C++ designated initializers; values are identical to the P4 defaults).
    eth_esp32_emac_config_t emac_cfg = {};
    emac_cfg.smi_gpio.mdc_num  = 31;
    emac_cfg.smi_gpio.mdio_num = 52;
    emac_cfg.interface         = EMAC_DATA_INTERFACE_RMII;
    emac_cfg.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac_cfg.clock_config.rmii.clock_gpio = (emac_rmii_clock_gpio_t)50;
    emac_cfg.dma_burst_len     = ETH_DMA_BURST_LEN_32;
    emac_cfg.intr_priority     = 0;
    emac_cfg.emac_dataif_gpio.rmii.tx_en_num  = 49;
    emac_cfg.emac_dataif_gpio.rmii.txd0_num   = 34;
    emac_cfg.emac_dataif_gpio.rmii.txd1_num   = 35;
    emac_cfg.emac_dataif_gpio.rmii.crs_dv_num = 28;
    emac_cfg.emac_dataif_gpio.rmii.rxd0_num   = 29;
    emac_cfg.emac_dataif_gpio.rmii.rxd1_num   = 30;
    emac_cfg.clock_config_out_in.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac_cfg.clock_config_out_in.rmii.clock_gpio = (emac_rmii_clock_gpio_t)-1;
    emac_cfg.mdc_freq_hz = 0;
    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.phy_addr       = ETH_PHY_ADDR;
    phy_cfg.reset_gpio_num = -1;

    esp_eth_mac_t   *mac = esp_eth_mac_new_esp32(&emac_cfg, &mac_cfg);
    esp_eth_phy_t   *phy = esp_eth_phy_new_ip101(&phy_cfg);
    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_cfg, &s_eth_handle));
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(s_eth_handle)));
    ESP_ERROR_CHECK(esp_eth_start(s_eth_handle));
}

// ── HTTP config server (shared by background and upload-mode) ────────────────

static void start_http_server(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.stack_size       = 8192;
    http_cfg.max_uri_handlers = 11;

    if (httpd_start(&server, &http_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed");
        return;
    }

    httpd_uri_t uri_login_get   = { "/login",       HTTP_GET,  get_login_handler,       NULL };
    httpd_uri_t uri_login_post  = { "/login",       HTTP_POST, post_login_handler,      NULL };
    httpd_uri_t uri_root        = { "/",            HTTP_GET,  get_root_handler,        NULL };
    httpd_uri_t uri_upload      = { "/upload",      HTTP_POST, post_upload_handler,     NULL };
    httpd_uri_t uri_upload_hd   = { "/upload_hd",   HTTP_POST, post_upload_hd_handler,  NULL };
    httpd_uri_t uri_mqtt_info   = { "/mqtt_info",   HTTP_GET,  get_mqtt_info_handler,   NULL };
    httpd_uri_t uri_mqtt_cfg    = { "/mqtt_cfg",    HTTP_POST, post_mqtt_cfg_handler,   NULL };
    httpd_uri_t uri_ha_info     = { "/ha_info",     HTTP_GET,  get_ha_info_handler,     NULL };
    httpd_uri_t uri_ha_cfg      = { "/ha_cfg",      HTTP_POST, post_ha_cfg_handler,     NULL };
    httpd_register_uri_handler(server, &uri_login_get);
    httpd_register_uri_handler(server, &uri_login_post);
    httpd_register_uri_handler(server, &uri_root);
    httpd_register_uri_handler(server, &uri_upload);
    httpd_register_uri_handler(server, &uri_upload_hd);
    httpd_register_uri_handler(server, &uri_mqtt_info);
    httpd_register_uri_handler(server, &uri_mqtt_cfg);
    httpd_register_uri_handler(server, &uri_ha_info);
    httpd_register_uri_handler(server, &uri_ha_cfg);
    ESP_LOGI(TAG, "HTTP server ready on port 80");
}

// ── Normal-mode Ethernet (MQTT background) ───────────────────────────────────

bool eth_logo_fetch_from_url(const char *url)
{
    ESP_LOGI(TAG, "Fetching logo from: %s", url);
    esp_http_client_config_t cfg = {};
    cfg.url         = url;
    cfg.timeout_ms  = 15000;
    cfg.buffer_size = 1024;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;

    if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        return false;
    }

    int content_len = esp_http_client_fetch_headers(client);
    int status      = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Logo fetch: HTTP %d  len=%d", status, content_len);

    if (status != 200 || content_len <= 0 || content_len > 512 * 1024) {
        esp_http_client_cleanup(client);
        return false;
    }

    FILE *f = fopen(ETH_LOGO_SPIFFS, "wb");
    if (!f) { esp_http_client_cleanup(client); return false; }

    char buf[512];
    int total = 0, rlen;
    while ((rlen = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, rlen, f);
        total += rlen;
    }
    fclose(f);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total < 10) { remove(ETH_LOGO_SPIFFS); return false; }

    s_has_logo = true;
    ESP_LOGI(TAG, "Logo saved: %d bytes → %s", total, ETH_LOGO_SPIFFS);
    return true;
}

void eth_start_background(void)
{
    ESP_LOGI(TAG, "Starting Ethernet (background / MQTT mode)...");
    // esp_netif_init() + esp_event_loop_create_default() called once in app_main.
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);

    // Read network mode from NVS (written by DEV → Network tab)
    {
        char mode[8]     = "dhcp";
        char ip_str[16]  = "";
        char mask_str[16] = "";
        nvs_handle_t h;
        if (nvs_open("eth_cfg", NVS_READONLY, &h) == ESP_OK) {
            size_t len;
            len = sizeof(mode);      nvs_get_str(h, "mode", mode,      &len);
            len = sizeof(ip_str);    nvs_get_str(h, "ip",   ip_str,    &len);
            len = sizeof(mask_str);  nvs_get_str(h, "mask", mask_str,  &len);
            nvs_close(h);
        }

        if (strcmp(mode, "static") == 0 && ip_str[0]) {
            // Derive gateway: same first 3 octets, last octet = 1
            uint8_t o[4] = {192, 168, 1, 1};
            sscanf(ip_str, "%hhu.%hhu.%hhu.%hhu", &o[0], &o[1], &o[2], &o[3]);
            snprintf(s_gw_str, sizeof(s_gw_str), "%d.%d.%d.1", o[0], o[1], o[2]);

            esp_netif_dhcpc_stop(eth_netif);
            esp_netif_ip_info_t ip_info = {};
            ip4addr_aton(ip_str,    (ip4_addr_t *)&ip_info.ip);
            ip4addr_aton(mask_str[0] ? mask_str : "255.255.255.0",
                         (ip4_addr_t *)&ip_info.netmask);
            ip4addr_aton(s_gw_str,  (ip4_addr_t *)&ip_info.gw);
            ESP_ERROR_CHECK(esp_netif_set_ip_info(eth_netif, &ip_info));
            ESP_LOGI(TAG, "Static IP: %s  mask %s  gw %s", ip_str, mask_str, s_gw_str);
        } else {
            s_gw_str[0] = '\0';
            ESP_LOGI(TAG, "Ethernet started — waiting for DHCP lease...");
        }
    }

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                               eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                               eth_event_handler, NULL));
    eth_hw_start(eth_netif);
    start_http_server();
}

// ── Setup-mode Ethernet + HTTP upload server ──────────────────────────────────

void eth_upload_start(lv_obj_t *qr_obj, lv_obj_t *ip_label)
{
    s_ip_label = ip_label;
    s_qr_obj   = qr_obj;
    ESP_LOGI(TAG, "Starting Ethernet (upload mode)...");
    // esp_netif_init() + esp_event_loop_create_default() called once in app_main.

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT,  ESP_EVENT_ANY_ID,    eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,   IP_EVENT_ETH_GOT_IP, eth_event_handler, NULL));

    eth_hw_start(eth_netif);

    ESP_LOGI(TAG, "Ethernet started — waiting for IP (DHCP)...");

    start_http_server();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
