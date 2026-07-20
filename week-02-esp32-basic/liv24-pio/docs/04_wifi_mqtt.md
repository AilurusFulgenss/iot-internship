# `wifi_mqtt.cpp` — ESP32 คุยกับ RPi และ Home Assistant ยังไง

ไฟล์นี้จัดการการเชื่อมต่อ MQTT ทั้งหมด — ทั้งส่งค่า sensor ออกไป และรับคำสั่งกลับเข้ามา

---

## ภาพรวม: ข้อมูลไหลไปทางไหน

```
┌─────────────┐   RS485    ┌──────────────┐   Ethernet   ┌──────────────┐
│  Sensor     │ ─────────► │   ESP32-P4   │ ────────────► │  RPi + HAOS  │
│  SN-300     │            │  (LIV-24)    │              │  Mosquitto   │
│  CWT-TH04S  │            │              │ ◄──────────── │  broker      │
└─────────────┘            └──────────────┘   คำสั่ง      └──────────────┘
                                                              │
                                                              ▼
                                                     Home Assistant
                                                     (dashboard, automation)
```

**ESP32 ส่ง (publish):**
- `liv24/sensors` → temp, hum, PM2.5, PM10, sound ทุก 2 วินาที
- `liv24/ec`, `liv24/orp`, `liv24/th`, `liv24/leak` → sensor อื่นๆ
- `liv24/relay/1/state`, `liv24/relay/2/state` → สถานะ relay ตอนนี้

**ESP32 รับ (subscribe):**
- `liv24/relay/1/set`, `liv24/relay/2/set` → HA สั่งเปิด/ปิด relay
- `liv24/history/24h`, `liv24/history/7d` → RPi ส่งข้อมูลย้อนหลังมาให้จอ
- `liv24/flora` → ข้อมูลเซ็นเซอร์ต้นไม้ HHCC จาก RPi
- `liv24/logo/url` → URL สำหรับ download logo ใหม่
- `liv24/test/alert` → ทดสอบ alert banner

---

## ส่วนที่ 1: callback functions (บรรทัด 20–31)

```c
static void (*s_relay_cb)(int, bool)             = NULL;
static void (*s_logo_url_cb)(const char *)       = NULL;
static void (*s_hist_24h_cb)(const char *, int)  = NULL;
// ...
```

ตัวแปรพวกนี้คือ **function pointer** — เก็บ address ของ function ไว้เรียกทีหลัง

ใน Swift เทียบได้กับ:
```swift
var relayCb: ((Int, Bool) -> Void)?
var logoUrlCb: ((String) -> Void)?
```

**ทำไมต้องใช้ callback แทนการเรียก function ตรงๆ?**

`wifi_mqtt.cpp` ไม่รู้จัก `relay_set_state()` หรือ `on_hist_24h()` — ฟังก์ชันเหล่านั้นอยู่ใน `main.cpp` ถ้า include กันตรงๆ จะเกิด circular dependency

แทนที่จะทำแบบนั้น main.cpp "ลงทะเบียน" ฟังก์ชันไว้ก่อน boot:
```c
// ใน main.cpp บรรทัด 950–955
wifi_mqtt_set_relay_cb(relay_set_state);
wifi_mqtt_set_history_cb(on_hist_24h, on_hist_7d);
// ...
wifi_mqtt_init(MQTT_BROKER_URI);  // เริ่ม MQTT หลังจากลงทะเบียนครบ
```

เมื่อ MQTT รับข้อความมา จะเรียกผ่าน pointer → ไม่ต้องรู้ว่าเจ้าของ function นั้นอยู่ที่ไหน

---

## ส่วนที่ 2: wifi_mqtt_init (บรรทัด 260–268)

```c
void wifi_mqtt_init(const char *broker_uri)
{
    strncpy(s_broker_uri, broker_uri, sizeof(s_broker_uri) - 1);

    // ลงทะเบียนรับ event เมื่อ Ethernet ได้รับ IP
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                               eth_got_ip_handler, NULL);

    // task สำรอง: poll IP ทุก 5 วิ เผื่อ event พลาด
    xTaskCreate(ip_poll_task, "ip_poll", 3072, NULL, 3, NULL);
}
```

ฟังก์ชันนี้ **ไม่ได้เชื่อม MQTT ทันที** — แค่บอกว่า "พอ Ethernet ได้ IP แล้วค่อยเริ่ม MQTT" เพราะตอนเรียกฟังก์ชันนี้ Ethernet ยังไม่ได้ IP จาก router

มี 2 กลไกสำรองกัน:
1. **Event handler** (`eth_got_ip_handler`) — รับแจ้งทันทีเมื่อ DHCP เสร็จ → เร็วที่สุด
2. **ip_poll_task** — วนตรวจทุก 5 วินาที เผื่อ event หลุด → ป้องกัน edge case

---

## ส่วนที่ 3: eth_got_ip_handler (บรรทัด 189–226)

เรียกเมื่อ Ethernet ได้ IP — ทำ 2 งานพร้อมกัน:

### งานที่ 1 — เริ่ม NTP (sync เวลา)

```c
esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
esp_sntp_setservername(0, "pool.ntp.org");
esp_sntp_setservername(1, "time.cloudflare.com");
esp_sntp_init();
```

ทำไมต้อง sync เวลาตรงนี้? เพราะ room_status_task ใน ui_home.cpp ต้องใช้วันที่ปัจจุบันสร้าง URL — ถ้าเวลาผิด URL ผิด → ดึงข้อมูลห้องไม่ได้

NTP sync เกิดขึ้น background อัตโนมัติ `t.tm_year < 100` guard ใน ui_home.cpp ป้องกันไม่ให้ใช้เวลาก่อน sync เสร็จ

### งานที่ 2 — เริ่ม MQTT

```c
xTaskCreate(mqtt_start_task, "mqtt_start", 4096, NULL, 3, NULL);
```

ทำไมสร้าง task แยก แทนที่จะเรียก `start_mqtt_client()` ตรงๆ?

เพราะ `mqtt_start_task` มีการ probe TCP ไปยัง gateway ก่อน (ส่วน `STATIC_GW_ADDR`) เพื่อ "อุ่น" ARP cache — ถ้าไม่ทำ DNS จะ timeout เพราะ ARP request ไม่ทันตอบก่อน DNS query หมดเวลา event handler ไม่ควรบล็อกนาน จึงต้องแยกเป็น task

---

## ส่วนที่ 4: start_mqtt_client (บรรทัด 140–157)

```c
static void start_mqtt_client(void)
{
    if (s_mqtt) {
        esp_mqtt_client_reconnect(s_mqtt);   // ถ้า client มีอยู่แล้ว แค่ reconnect
        return;
    }
    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = s_broker_uri;   // เช่น "mqtt://192.168.1.112:1883"
    cfg.buffer.size        = 2048;           // buffer รับข้อความ 2KB

    s_mqtt = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_mqtt, MQTT_EVENT_ANY,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt);
}
```

`cfg.buffer.size = 2048` — ข้อความ history JSON จาก RPi อาจยาว ต้อง buffer พอรับทั้งก้อน

---

## ส่วนที่ 5: mqtt_event_handler (บรรทัด 35–138)

หัวใจของไฟล์นี้ — รับ event ทุกชนิดจาก MQTT client

### MQTT_EVENT_CONNECTED

```c
case MQTT_EVENT_CONNECTED:
    s_mqtt_ready = true;
    esp_mqtt_client_subscribe(s_mqtt, "liv24/relay/1/set", 0);
    esp_mqtt_client_subscribe(s_mqtt, "liv24/relay/2/set", 0);
    esp_mqtt_client_subscribe(s_mqtt, "liv24/logo/url",    1);  // qos=1
    esp_mqtt_client_subscribe(s_mqtt, "liv24/history/24h", 1);
    // ...
```

**QoS 0 vs QoS 1 คืออะไร?**
- `QoS 0` = ส่งครั้งเดียว ไม่รับประกัน — ใช้กับ relay command เพราะ user จะกดใหม่ถ้าไม่ work
- `QoS 1` = รับประกันว่าถึง อย่างน้อย 1 ครั้ง — ใช้กับ history และ logo เพราะ RPi ส่งครั้งเดียว ถ้าหลุดไม่ได้ข้อมูล

**retained message** — topic ที่ qos=1 เช่น `liv24/history/24h` broker เก็บข้อความล่าสุดไว้ พอ ESP32 connect แล้ว subscribe ปุ๊บ broker ส่งข้อความที่เก็บไว้ให้ทันที ไม่ต้องรอ RPi ส่งใหม่

### MQTT_EVENT_DATA

```c
case MQTT_EVENT_DATA:
    char topic[32] = {};
    memcpy(topic, ev->topic, tlen);   // copy topic มาเพราะ ev->topic ไม่มี null terminator

    if (strcmp(topic, "liv24/history/24h") == 0) {
        if (s_hist_24h_cb) s_hist_24h_cb(ev->data, ev->data_len);
        break;
    }
    // ... topic อื่นๆ

    // relay เช็คสุดท้าย
    int relay_idx = -1;
    if (strcmp(topic, "liv24/relay/1/set") == 0) relay_idx = 0;
    bool on = (ev->data[0] == 'O' && ev->data[1] == 'N');   // ตรวจว่าข้อความขึ้นต้น "ON"
    s_relay_cb(relay_idx, on);
```

**ทำไมต้อง `memcpy` topic แทนอ่านตรงๆ?**

`ev->topic` ไม่มี null terminator (`\0`) ท้าย string เพราะ MQTT protocol ไม่ได้กำหนดไว้ ถ้าใช้ `strcmp` กับ `ev->topic` โดยตรง อาจอ่านเกินขอบเขต memory crash ได้

### MQTT_EVENT_DISCONNECTED

```c
case MQTT_EVENT_DISCONNECTED:
    s_mqtt_ready = false;
    // auto-retry เกิดจาก esp_mqtt_client เอง — ไม่ต้องสั่งเพิ่ม
```

`s_mqtt_ready = false` → publish functions ทุกอันตรวจค่านี้ก่อน ป้องกัน publish ขณะยังไม่ได้เชื่อมต่อ

---

## ส่วนที่ 6: publish functions (บรรทัด 316–364)

ทุกฟังก์ชันมี pattern เดียวกัน:

```c
void wifi_mqtt_publish_sensors(float temp, float hum, int sound, float pm25, float pm10)
{
    if (!s_mqtt_ready) return;   // ← guard: ไม่ publish ถ้ายังไม่ได้เชื่อม

    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"temp\":%.1f,\"hum\":%.1f,\"sound\":%d,\"pm25\":%.1f,\"pm10\":%.1f}",
             temp, hum, sound, pm25, pm10);
    // ผลลัพธ์: {"temp":25.3,"hum":60.0,"sound":45,"pm25":12.5,"pm10":18.2}

    esp_mqtt_client_publish(s_mqtt, "liv24/sensors", payload, 0, 0, 0);
    //                                topic           data    len qos retain
}
```

`len=0` — บอกให้ MQTT client นับ length เอง (ใช้ strlen)  
`qos=0` — sensor data ส่งทุก 2 วินาทีอยู่แล้ว ถ้าหลุดรอบหน้าก็มา  
`retain=0` — ไม่เก็บบน broker เพราะ sensor data เปลี่ยนตลอด ข้อมูลเก่าไม่มีประโยชน์

**relay state ต่างกัน:**
```c
esp_mqtt_client_publish(s_mqtt, topic, on ? "ON" : "OFF", 0, 1, 1);
//                                                          qos=1 retain=1
```
`retain=1` — broker เก็บสถานะ relay ล่าสุดไว้ ถ้า HA restart แล้ว subscribe ใหม่ จะรู้ว่า relay อยู่สถานะอะไรทันที ไม่ต้องรอ ESP32 ส่งอีกครั้ง

---

## ส่วนที่ 7: ip_poll_task (บรรทัด 229–256)

```c
static void ip_poll_task(void *arg)
{
    for (;;) {
        vTaskDelay(5000ms);
        if (s_mqtt) { vTaskDelete(NULL); return; }   // MQTT เริ่มแล้ว ออกได้

        esp_netif_ip_info_t ip = {};
        esp_netif_get_ip_info(netif, &ip);

        if (ip.ip.addr != 0) {
            start_mqtt_client();
            vTaskDelete(NULL);
        }
    }
}
```

task นี้เป็น "insurance" — ถ้า `IP_EVENT_ETH_GOT_IP` หลุดไปด้วยเหตุใด (timing race, driver bug) task นี้จะ catch ได้ภายใน 5–10 วินาที แล้ว delete ตัวเองเมื่อไม่จำเป็นแล้ว

---

## สรุป: ลำดับการเชื่อมต่อตั้งแต่ boot

```
app_main()
    │
    ├─ wifi_mqtt_set_relay_cb(...)      ← ลงทะเบียน callback ก่อน
    ├─ wifi_mqtt_set_history_cb(...)
    ├─ ...
    ├─ wifi_mqtt_init("mqtt://192.168.1.112:1883")   ← register event + สร้าง ip_poll_task
    └─ eth_start_background()           ← เริ่ม Ethernet

         ↓ (ไม่กี่วินาที)

    Ethernet ได้ IP จาก router
         │
         ├─ eth_got_ip_handler() เรียกอัตโนมัติ
         │    ├─ เริ่ม NTP sync
         │    └─ สร้าง mqtt_start_task
         │
         └─ mqtt_start_task()
              └─ start_mqtt_client() → เชื่อม broker

                   ↓

         MQTT_EVENT_CONNECTED
              └─ subscribe ทุก topic
              └─ broker ส่ง retained messages (history) ให้ทันที
              └─ s_mqtt_ready = true → publish functions พร้อมใช้
```

---

## ตาราง topic สรุป

| Topic | ทิศทาง | QoS | Retain | ทำอะไร |
|---|---|---|---|---|
| `liv24/sensors` | ESP→RPi | 0 | ไม่ | temp/hum/PM2.5/PM10/sound ทุก 2 วิ |
| `liv24/ec` | ESP→RPi | 0 | ไม่ | ค่า EC น้ำ |
| `liv24/orp` | ESP→RPi | 0 | ไม่ | ค่า ORP น้ำ |
| `liv24/th` | ESP→RPi | 0 | ไม่ | temp/hum จาก CWT-TH04S |
| `liv24/leak` | ESP→RPi | 0 | ไม่ | สถานะน้ำรั่ว |
| `liv24/relay/1/state` | ESP→RPi | 1 | ✅ | สถานะ relay 1 |
| `liv24/relay/2/state` | ESP→RPi | 1 | ✅ | สถานะ relay 2 |
| `liv24/relay/1/set` | RPi→ESP | 0 | ไม่ | คำสั่งเปิด/ปิด relay 1 |
| `liv24/relay/2/set` | RPi→ESP | 0 | ไม่ | คำสั่งเปิด/ปิด relay 2 |
| `liv24/history/24h` | RPi→ESP | 1 | ✅ | ข้อมูลย้อนหลัง 24h |
| `liv24/history/7d` | RPi→ESP | 1 | ✅ | ข้อมูลย้อนหลัง 7 วัน |
| `liv24/flora` | RPi→ESP | 1 | ✅ | ข้อมูลเซ็นเซอร์ต้นไม้ |
| `liv24/logo/url` | RPi→ESP | 1 | ✅ | URL สำหรับ download logo |
| `liv24/test/alert` | RPi→ESP | 0 | ไม่ | ทดสอบ alert banner |

ไฟล์ถัดไป: `05_history.md` — RPi ส่ง JSON ข้อมูลย้อนหลัง 24h/7d มาให้จอยังไง และ ESP32 parse แล้วแสดงเป็นกราฟยังไง
