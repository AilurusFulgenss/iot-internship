# `history.cpp` — รับข้อมูลย้อนหลังจาก RPi แล้วเก็บไว้ให้กราฟ

ไฟล์นี้รับ JSON ที่ RPi ส่งมาผ่าน MQTT แปลงเป็น array ตัวเลข แล้วเก็บไว้ให้หน้า PM History และ EXEC Detail ดึงไปวาดกราฟ

---

## ภาพรวม: ข้อมูลไหลจาก RPi ถึงกราฟบนจอ

```
RPi (Home Assistant + Python script)
    │  ทุก 1 ชั่วโมง คำนวณค่าเฉลี่ย 24h/7d แล้ว publish ผ่าน MQTT
    │
    ▼
MQTT broker (Mosquitto บน RPi)
    topic: liv24/history/24h  retain=1
    topic: liv24/history/7d   retain=1
    │
    ▼
wifi_mqtt.cpp รับ → on_hist_24h() / on_hist_7d() ใน main.cpp
    │
    ▼
hist_parse_24h() / hist_parse_7d()   ← ไฟล์นี้
    │  แปลง JSON → float array
    ▼
g_hist_24h.d[][]  /  g_hist_7d.d[][]   ← เก็บในตัวแปร global
    │
    ▼
ui_pm_refresh_history()   ← หน้า PM History อ่านแล้ววาดกราฟ
ui_exec_detail_refresh()  ← หน้า EXEC Detail อ่านแล้ววาดกราฟ
```

---

## ส่วนที่ 1: โครงสร้างข้อมูล (history.h)

### ตัวเลข index ของแต่ละ channel

```c
#define HIST_TEMP      0   // อุณหภูมิ (°C)
#define HIST_HUM       1   // ความชื้น (%)
#define HIST_PM25      2   // PM2.5 (µg/m³)
#define HIST_PM10      3   // PM10
#define HIST_SOUND     4   // เสียง (dB)
#define HIST_EC        5   // ค่าน้ำ EC
#define HIST_TDS       6   // ค่าน้ำ TDS
#define HIST_ORP       7   // ค่าน้ำ ORP
#define HIST_ORP_TEMP  8   // อุณหภูมิน้ำ (จาก ORP sensor)
#define HIST_HHCC_MOIST 9  // ความชื้นดิน
#define HIST_HHCC_LIGHT 10 // แสง
#define HIST_HHCC_FERT  11 // ธาตุอาหาร
#define HIST_HHCC_TEMP  12 // อุณหภูมิต้นไม้
#define HIST_TH_TEMP   13  // temp จาก CWT-TH04S
#define HIST_TH_HUM    14  // hum จาก CWT-TH04S
#define HIST_LEAK      15  // จำนวนครั้งน้ำรั่วต่อวัน
```

ทำไมใช้ตัวเลขแทนชื่อ? เพราะเป็น index เข้า array 2 มิติ — ถ้าใช้ชื่อตรงๆ ต้องสร้าง map แปลงชื่อเป็นเลข ใช้ `#define` ง่ายกว่าและไม่มี runtime cost

### hist_24h_t — กราฟ 24 ชั่วโมง

```c
typedef struct {
    float d[HIST_SENS][HIST_24H_LEN];
    //       ↑ 5 channel  ↑ 24 จุด (1 จุดต่อชั่วโมง)
    int count;
} hist_24h_t;
```

`d[HIST_PM25][0..23]` คือ array ของค่า PM2.5 เฉลี่ยรายชั่วโมง 24 ชั่วโมงที่ผ่านมา เหมือนเก็บ 24 แท่งกราฟในความจำ

ในรูปแบบตาราง:
```
         ชม.0  ชม.1  ชม.2  ...  ชม.23
TEMP  → [ 25.1, 25.3, 25.0, ..., 26.2 ]
HUM   → [ 62.0, 61.5, 61.8, ..., 58.0 ]
PM25  → [ 12.0, 13.5, 11.0, ..., 15.2 ]
PM10  → [ 18.0, 19.0, 17.5, ..., 20.0 ]
SOUND → [ 45.0, 44.0, 46.0, ..., 42.0 ]
```

### hist_7d_t — กราฟ 7 วัน

```c
typedef struct {
    float d[HIST_SENS_7D][HIST_7D_LEN];
    //       ↑ 16 channel   ↑ 7 จุด (1 จุดต่อวัน)
    int count;
    int cnt_ec;
    int cnt_orp;
    int cnt_hhcc;
    int cnt_th;
    int cnt_leak;
} hist_7d_t;
```

ทำไม 7d ต้องมี `cnt_ec`, `cnt_orp` แยกกัน? เพราะ sensor แต่ละชนิดส่งข้อมูลต่างเวลากัน อาจได้ 7 วัน บางตัวได้แค่ 5 วัน — แต่ละ counter เก็บจำนวนจุดที่มีจริงของ sensor ตัวนั้น กราฟใช้ค่านี้รู้ว่าวาดถึงจุดไหน

---

## ส่วนที่ 2: parse_arr — ฟังก์ชัน helper (บรรทัด 13–24)

```c
static int parse_arr(cJSON *root, const char *key, float *buf, int maxlen)
{
    cJSON *arr = cJSON_GetObjectItem(root, key);       // หา key ใน JSON
    if (!arr || !cJSON_IsArray(arr)) return 0;         // ถ้าไม่เจอหรือไม่ใช่ array → 0

    int n = cJSON_GetArraySize(arr);
    if (n > maxlen) n = maxlen;                        // ไม่เกิน buffer ที่มี

    for (int i = 0; i < n; i++) {
        cJSON *it = cJSON_GetArrayItem(arr, i);
        buf[i] = cJSON_IsNumber(it) ? (float)it->valuedouble : NAN;
        //       ถ้าเป็นตัวเลข → เก็บค่า  ถ้าเป็น null → เก็บ NAN
    }
    return n;   // คืนจำนวนจุดที่ parse ได้จริง
}
```

ฟังก์ชันนี้ใช้ซ้ำทุกที่ — รับ JSON root + ชื่อ key + buffer ปลายทาง แล้ว fill ค่าลงไป

ทำไมต้องแปลง `null` เป็น `NAN`? RPi อาจส่ง `null` ถ้า sensor นั้นไม่ได้อ่านค่าในชั่วโมงนั้น กราฟจะ skip จุดที่เป็น `NAN` แทนที่จะวาดเป็น 0 ซึ่งทำให้ผิดความจริง

---

## ส่วนที่ 3: hist_parse_24h (บรรทัด 26–42)

JSON ที่ RPi ส่งมา:
```json
{
  "temp":  [25.1, 25.3, 25.0, 24.8, 25.2, 25.5, 26.0, 26.2, ...],
  "hum":   [62.0, 61.5, 61.8, 63.0, 62.5, 61.0, 60.5, 58.0, ...],
  "pm25":  [12.0, 13.5, 11.0, 10.5, 14.0, 15.2, 13.8, 12.1, ...],
  "pm10":  [18.0, 19.0, 17.5, 16.0, 20.0, 21.0, 19.5, 18.5, ...],
  "sound": [45.0, 44.0, 46.0, 43.0, 47.0, 42.0, 44.5, 43.0, ...]
}
```

โค้ด:
```c
void hist_parse_24h(const char *json, int len)
{
    char *buf = strndup(json, len);   // copy JSON มาก่อน เพราะ ev->data ใน MQTT ไม่มี null terminator
    cJSON *root = cJSON_Parse(buf);
    free(buf);                        // คืน memory ทันทีหลัง parse เสร็จ

    parse_arr(root, "hum",   g_hist_24h.d[HIST_HUM],   24);
    parse_arr(root, "pm25",  g_hist_24h.d[HIST_PM25],  24);
    parse_arr(root, "pm10",  g_hist_24h.d[HIST_PM10],  24);
    parse_arr(root, "sound", g_hist_24h.d[HIST_SOUND], 24);
    g_hist_24h.count = parse_arr(root, "temp", g_hist_24h.d[HIST_TEMP], 24);
    // count ใช้ค่าจาก "temp" เป็น master count — ถ้า temp มี 20 จุด กราฟวาด 20 แท่ง

    cJSON_Delete(root);   // คืน memory ของ JSON tree ทั้งก้อน
}
```

**ทำไมต้อง `strndup` ก่อน?**

`json` pointer ที่รับมาชี้ไปที่ internal buffer ของ MQTT client — `cJSON_Parse` ต้องการ null-terminated string แต่ MQTT buffer ไม่มี `\0` ท้าย `strndup` สร้าง copy ใหม่พร้อม `\0` แล้วคืน pointer ที่ปลอดภัย

---

## ส่วนที่ 4: hist_parse_7d (บรรทัด 44–60)

Pattern เดียวกับ 24h แต่เก็บลง `g_hist_7d` และ array ยาวแค่ 7 จุด:

```json
{
  "temp":  [25.8, 26.1, 25.5, 26.3, 27.0, 26.8, 25.9],
  "pm25":  [14.2, 13.8, 15.0, 12.5, 16.1, 14.8, 13.2],
  ...
}
```

7 ค่า = 7 วัน ค่าเฉลี่ยรายวัน — กราฟ 7 แท่ง

---

## ส่วนที่ 5: hist_parse_ec / orp / th / leak (บรรทัด 63–148)

sensor เหล่านี้อยู่บน MQTT topic แยก (`liv24/hist/ec` ฯลฯ) แต่เขียนลง `g_hist_7d` ตัวเดียวกัน เพียงแต่ใช้ channel index ที่ต่างกัน

ตัวที่น่าสนใจคือ **hist_parse_hhcc** — มีการแปลง °F → °C ก่อนเก็บ:

```c
// HA BLE Integration ส่ง HHCC temp เป็น °F
for (int i = 0; i < g_hist_7d.cnt_hhcc; i++) {
    float *v = &g_hist_7d.d[HIST_HHCC_TEMP][i];
    if (!isnan(*v)) *v = (*v - 32.0f) * 5.0f / 9.0f;
}
```

ทำไม HHCC ส่งเป็น °F? เซ็นเซอร์ต้นไม้ HHCC เชื่อมผ่าน Bluetooth กับ RPi ซึ่ง Home Assistant BLE integration แปลงค่ามาเป็น °F ก่อน publish — แก้ที่ฝั่ง ESP32 ในขั้นตอน parse แทนที่จะให้ RPi แปลงใหม่

**hist_parse_leak — ต่างจากชาวบ้าน:**

```json
{ "count": [0, 1, 0, 0, 2, 0, 0] }
```

leak ไม่ได้เก็บค่าต่อเนื่อง แต่เก็บ **จำนวนครั้งที่เกิด alarm ต่อวัน** — วันไหน 0 คือปกติ วันไหน 1+ คือเกิดน้ำรั่ว

---

## ส่วนที่ 6: global variables (บรรทัด 10–11)

```c
hist_24h_t g_hist_24h = {};   // ← {} คือ zero-initialize ทุก field
hist_7d_t  g_hist_7d  = {};
```

ทั้งสองตัวเป็น global — ทุกไฟล์ที่ `#include "history.h"` เข้าถึงได้ ไม่ต้องส่งผ่าน function parameter

ขนาดในหน่วย memory:
- `g_hist_24h` = 5 channels × 24 จุด × 4 bytes (float) = **480 bytes**
- `g_hist_7d` = 16 channels × 7 จุด × 4 bytes (float) = **448 bytes**

รวมกันไม่ถึง 1KB — เล็กมากเมื่อเทียบกับ PSRAM 256KB ที่มีอยู่

---

## สรุป: ไฟล์นี้ทำอะไร

| ฟังก์ชัน | รับจาก topic | เก็บใน | ใช้โดย |
|---|---|---|---|
| `hist_parse_24h` | `liv24/history/24h` | `g_hist_24h` | ui_pm (กราฟ 24h) |
| `hist_parse_7d` | `liv24/history/7d` | `g_hist_7d` (channel 0–4) | ui_exec_detail |
| `hist_parse_ec` | `liv24/hist/ec` | `g_hist_7d` (channel 5–6) | ui_exec_detail |
| `hist_parse_orp` | `liv24/hist/orp` | `g_hist_7d` (channel 7–8) | ui_exec_detail |
| `hist_parse_hhcc` | `liv24/hist/hhcc` | `g_hist_7d` (channel 9–12) | ui_exec_detail |
| `hist_parse_th` | `liv24/hist/th` | `g_hist_7d` (channel 13–14) | ui_exec_detail |
| `hist_parse_leak` | `liv24/hist/leak` | `g_hist_7d` (channel 15) | ui_exec_detail |

ไฟล์ถัดไป: `06_ui_user.md` — หน้าหลักที่ผู้ใช้เห็นค่า sensor แบบ real-time
