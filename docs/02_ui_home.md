# `ui_home.cpp` — หน้า Home: นาฬิกา, สภาพอากาศ, สถานะห้องประชุม

ไฟล์นี้สร้างและดูแลหน้าแรกที่ผู้ใช้เห็น — หน้า Home ที่แสดงนาฬิกา, greeting, อากาศกรุงเทพ และสถานะห้อง Meeting Room 1

---

## ภาพรวม: 3 งานที่รันพร้อมกันบนหน้า Home

```
หน้า Home โหลดขึ้นมา
    │
    ├─ LVGL timer (clock_timer_cb)   — อัปเดตนาฬิกา+ข้อความทักทาย ทุก 30 วินาที
    ├─ weather_task                  — ดึงอากาศจาก OpenWeather ทุก 10 นาที
    └─ room_status_task              — ดึงสถานะห้องจาก HA Calendar ทุก 30 วินาที
```

ทั้ง 3 ทำงานพร้อมกัน ไม่รอกัน — เหมือน 3 background task ใน Swift รัน concurrent กัน

---

## ส่วนที่ 1: ตัวแปร label (บรรทัด 17–36)

```c
static lv_obj_t *lbl_clock    = NULL;   // "Mon  09:30" มุมขวาบน
static lv_obj_t *lbl_greeting = NULL;   // "Good morning"
static lv_obj_t *lbl_weather  = NULL;   // "Bangkok  32°C  Clear"
static lv_obj_t *lbl_pm25_h   = NULL;   // ค่า PM2.5 ใน card Air Quality
static lv_obj_t *lbl_temp_h   = NULL;   // อุณหภูมิใน card
static lv_obj_t *lbl_hum_h    = NULL;   // ความชื้นใน card
```

ตัวแปรพวกนี้เป็น `static` — คือ **ชีวิตยาวตลอด** ตั้งแต่เปิดเครื่องจนปิดเครื่อง เพราะ task ทุกอันต้องการ pointer เพื่ออัปเดตค่าในภายหลัง ถ้าเป็น local variable ใน function เดียวแล้วถูก destroy task อื่นจะหา label ไม่เจอแล้ว crash

---

## ส่วนที่ 2: define ค่าคงที่ห้องประชุม (บรรทัด 25–30)

```c
#define ROOM_STATUS_ID   "M-MTG1"
#define ROOM_STATUS_NAME "Meeting Room 1"
#define HA_BASE_URL      "http://192.168.1.111:8123"
#define HA_CALENDAR_ID   "calendar.meeting_room_1"
#define HA_TOKEN         "eyJhbGci..."
#define RS_BUF           4096
```

ค่าพวกนี้ตั้งครั้งเดียว — ถ้าต้องการเปลี่ยนห้องประชุม หรือย้าย HA ไป IP อื่น แก้แค่นี้เท่านั้น โค้ดที่เหลือไม่ต้องแตะ

`RS_BUF 4096` — จอง RAM 4KB สำหรับเก็บ JSON ที่ดึงมาจาก HA Calendar API ก่อนแปล

---

## ส่วนที่ 3: emoji_update (บรรทัด 49–60)

วงกลมสีเล็กๆ ข้างคำทักทาย — เปลี่ยนสีตามช่วงเวลา:

| ช่วงเวลา | สีหน้า | ความหมาย |
|---|---|---|
| 05:00–11:59 | เหลือง `#FFCC00` | เช้า — ดวงอาทิตย์ |
| 12:00–16:59 | ส้ม `#FF8C00` | บ่าย — ร้อน |
| 17:00–21:59 | ส้มแดง `#FF7755` | เย็น — พระอาทิตย์ตก |
| 22:00–04:59 | น้ำเงิน `#334488` | กลางคืน |

ฟังก์ชันนี้ถูกเรียกจาก `clock_timer_cb` ทุกครั้งที่ update นาฬิกา — ทำให้ emoji เปลี่ยนสีโดยอัตโนมัติตามเวลาจริง

---

## ส่วนที่ 4: clock_timer_cb (บรรทัด 86–101)

```c
static void clock_timer_cb(lv_timer_t *)
{
    time_t now;
    struct tm t;
    time(&now);
    localtime_r(&now, &t);
    if (t.tm_year < 100) return;  // NTP ยังไม่ sync — ข้ามรอบนี้ไป
    ...
    snprintf(buf, sizeof(buf), "%s  %02d:%02d", days[t.tm_wday], t.tm_hour, t.tm_min);
    lv_label_set_text(lbl_clock, buf);
    lv_label_set_text(lbl_greeting, greeting_for(t.tm_hour));
    emoji_update(t.tm_hour);
}
```

**`time_t` และ `struct tm` คืออะไร?**
- `time(&now)` — ดึงเวลาปัจจุบันในรูปตัวเลข (นับวินาทีจาก 1 ม.ค. 1970)
- `localtime_r(&now, &t)` — แปลงตัวเลขนั้นเป็น struct ที่มีฟิลด์ `tm_hour`, `tm_min`, `tm_wday`

**`t.tm_year < 100` คืออะไร?**
- `tm_year` เก็บค่าปีลบ 1900 → ปี 2026 จะเป็น 126
- ถ้าค่าน้อยกว่า 100 แปลว่าปีเป็น 1900–1999 = NTP ยังไม่ sync เวลาจริงเข้ามา → ไม่แสดงเวลาผิดๆ ก่อน

**LVGL timer ≠ task** — `lv_timer_create(clock_timer_cb, 30000, NULL)` บรรทัด 574 สร้าง timer ที่รันภายใน LVGL task เอง ไม่ต้องสร้าง thread แยก ไม่ต้องใช้ `bsp_display_lock/unlock` เพราะรันอยู่ใน LVGL thread อยู่แล้ว

---

## ส่วนที่ 5: weather_task (บรรทัด 124–181)

### โครงสร้าง

```c
static void weather_task(void *)
{
    for (;;) {
        vTaskDelay(12000ms);   // รอ 12 วิตอน boot ให้ network พร้อม

        // 1. ดึง JSON จาก OpenWeather API
        // 2. แปลง JSON
        // 3. lock → อัปเดต label → unlock

        vTaskDelay(588000ms);  // รอ ~10 นาทีก่อนดึงครั้งต่อไป
    }
}
```

### ขั้นตอน 1 — ดึงข้อมูลจาก OpenWeather

```c
esp_http_client_config_t cfg = {};
cfg.url           = OW_URL;          // URL ที่รวม API key และเมืองไว้แล้ว
cfg.event_handler = ow_http_cb;      // callback รับข้อมูลทีละ chunk
cfg.timeout_ms    = 8000;            // รอสูงสุด 8 วินาที

esp_http_client_handle_t c = esp_http_client_init(&cfg);
esp_http_client_perform(c);          // ส่ง GET request และรอ response
esp_http_client_cleanup(c);          // คืน memory
```

`ow_http_cb` (บรรทัด 111) — รับข้อมูลที่กลับมาทีละส่วน (chunk) แล้วเก็บต่อกันใน `s_ow_buf` เหมือน `URLSession dataTask` ใน Swift ที่รับ data ทีละ packet

### ขั้นตอน 2 — แปลง JSON

OpenWeather ตอบกลับมาเป็น JSON แบบนี้:
```json
{
  "main": { "temp": 32.5 },
  "weather": [{ "main": "Clear" }]
}
```

โค้ดแปลงด้วย cJSON:
```c
cJSON *main_j = cJSON_GetObjectItem(root, "main");
cJSON *tj     = cJSON_GetObjectItem(main_j, "temp");
float temp    = (float)tj->valuedouble;   // 32.5

cJSON *weather_j = cJSON_GetObjectItem(root, "weather");
cJSON *w         = cJSON_GetArrayItem(weather_j, 0);   // element แรก
cJSON *mj        = cJSON_GetObjectItem(w, "main");
const char *cond = mj->valuestring;   // "Clear"
```

### ขั้นตอน 3 — แสดงผล

```c
snprintf(wbuf, sizeof(wbuf), "Bangkok  %.0f°C   #%s %s#",
         temp, weather_color_str(cond), cond);
// ผลลัพธ์: "Bangkok  32°C   #FFD700 Clear#"
```

`#FFD700 Clear#` — syntax พิเศษของ LVGL (`lv_label_set_recolor`) ทำให้คำว่า "Clear" แสดงด้วยสีเหลือง `#FFD700` ข้อความนอก `#...#` ใช้สีปกติ

`weather_color_str(cond)` (บรรทัด 64) — return รหัสสีตาม weather condition:
- "Clear" → `FFD700` (เหลือง)
- "Rain" → `4488FF` (น้ำเงิน)
- "Thunder" → `FF8833` (ส้ม)

---

## ส่วนที่ 6: room_status_task (บรรทัด 201–358)

นี่คือ logic ที่ซับซ้อนที่สุดในไฟล์นี้ — ดึงข้อมูลการจอง Meeting Room 1 จาก Home Assistant Calendar และตัดสินใจว่าจะแสดงอะไรบนจอ

### ขั้นตอน A — รอ NTP และ Network พร้อม

```c
vTaskDelay(15000ms);   // รอ 15 วิตอน boot

// จากนั้นทุกรอบตรวจว่า NTP sync แล้วไหม
if (t.tm_year < 100) {
    vTaskDelay(5000ms);
    continue;   // วนกลับขึ้นไปตรวจใหม่
}
```

ทำไมต้องรอ? เพราะต้องใช้วันที่ปัจจุบันสร้าง URL — ถ้า NTP ยังไม่ sync ปีอาจเป็น 1970 แล้วสร้าง URL ผิด

### ขั้นตอน B — สร้าง URL และดึงข้อมูล

```c
char date[11];
strftime(date, sizeof(date), "%Y-%m-%d", &t);   // เช่น "2026-07-14"

char url[256];
snprintf(url, sizeof(url),
    HA_BASE_URL "/api/calendars/" HA_CALENDAR_ID
    "?start=%sT00:00:00%%2B07:00&end=%sT23:59:59%%2B07:00",
    date, date);
// ผลลัพธ์: http://192.168.1.111:8123/api/calendars/calendar.meeting_room_1?start=2026-07-14T00:00:00+07:00&end=2026-07-14T23:59:59+07:00
```

`%%2B` = encode ของ `+` ใน URL (บวกต้อง encode เพราะ URL parser แปลง `+` เป็น space)

HA Calendar API ตอบกลับเป็น JSON array ของ event ทั้งหมดในวันนั้น:
```json
[
  { "summary": "Budget Review",
    "start": { "dateTime": "2026-07-14T09:00:00+07:00" },
    "end":   { "dateTime": "2026-07-14T10:00:00+07:00" } },
  { "summary": "Team Sync",
    "start": { "dateTime": "2026-07-14T14:00:00+07:00" },
    "end":   { "dateTime": "2026-07-14T15:00:00+07:00" } }
]
```

### ขั้นตอน C — วิเคราะห์ว่าห้องว่างหรือไม่

```c
int now_min = t.tm_hour * 60 + t.tm_min;   // เวลาตอนนี้ในรูป "นาทีนับจากเที่ยงคืน"
// เช่น 09:30 = 9*60 + 30 = 570
```

วนผ่านทุก event:

```c
cJSON_ArrayForEach(ev, root) {
    // แปลง "2026-07-14T09:00:00+07:00" → เอาแค่ "09:00"
    const char *s_t_ptr = strchr(s_dt->valuestring, 'T');  // หาตัว T แล้วอ่านต่อจากนั้น
    int sh = atoi(s_t_ptr + 1);   // 09
    int sm = atoi(s_t_ptr + 4);   // 00
    int start_min = sh * 60 + sm; // 540

    if (start_min <= now_min && now_min < end_min) {
        booked = true;   // ตอนนี้อยู่ในช่วงเวลาของ event นี้
        ...
    } else if (start_min > now_min && !found_next) {
        found_next = true;   // event นี้อยู่หลังเวลาตอนนี้ → คือ "การจองถัดไป"
        ...
    }
}
```

`strchr(str, 'T')` — หาตำแหน่งของตัวอักษร `T` ใน string — ใช้ตัด datetime ออกเป็น 2 ส่วน:
- ก่อน T = วันที่ → ไม่ใช้
- หลัง T = เวลา → ใช้

### ขั้นตอน D — 4 กรณีที่เป็นไปได้

| booked | found_next | สิ่งที่แสดงบนจอ |
|---|---|---|
| ❌ ไม่ | ❌ ไม่ | AVAILABLE เขียว ไม่มีอะไรเพิ่ม |
| ❌ ไม่ | ✅ มี | header: "Next 14:00-15:00" สีเหลือง + AVAILABLE เขียว |
| ✅ ใช่ | ❌ ไม่ | header: BOOKED แดง + ชื่อประชุม + เวลา |
| ✅ ใช่ | ✅ มี | header: "Next 14:00-15:00" สีเหลือง + "BOOKED 09:00-10:00" แดง + ชื่อประชุม |

```c
if (booked && found_next) {
    // Case 4
    lv_label_set_text(s_rs_status_lbl, "Next  14:00-15:00");
    lv_obj_set_style_text_color(s_rs_status_lbl, lv_color_hex(0xFFCC44), 0); // เหลือง
    lv_label_set_text(s_rs_topic_lbl, "BOOKED  09:00-10:00");
    lv_obj_set_style_text_color(s_rs_topic_lbl, lv_color_hex(0xFF4444), 0);  // แดง
    lv_label_set_text(s_rs_organizer_lbl, "Budget Review");                  // ชื่อประชุม
```

---

## ส่วนที่ 7: ui_home_create (บรรทัด 362–578)

ฟังก์ชันนี้สร้าง layout ทั้งหมดของหน้า Home — เรียกครั้งเดียวตอน boot ใน app_main

### โครงสร้างของหน้า (top → bottom)

```
y=0   ┌─────────────────────────────────────┐
      │  [logo] LIV-24        Mon  09:30    │  header 72px
y=72  ├─────────────────────────────────────┤  เส้นสีฟ้า 3px
y=92  │  Good morning 😊                    │  greeting + emoji
y=158 │  Bangkok  32°C  Clear               │  weather
y=204 ├─────────────────────────────────────┤
      │  AIR QUALITY                     ▶  │
      │  25                                 │  card 1: Air Quality
      │  PM2.5                              │  224px
      │  25.3°C                     60%     │ 
y=428 ├─────────────────────────────────────┤
y=456 ├─────────────────────────────────────┤
      │  Meeting Room 1       Next 14:00    │
      │  ─────────────────────────────────  │  card 2: Room Status
      │  AVAILABLE                          │  176px
y=632 └─────────────────────────────────────┘
```

### การสร้าง card (บรรทัด 454–503)

```c
lv_obj_t *card1 = lv_obj_create(scr_home);
lv_obj_set_size(card1, 664, 224);                     // กว้าง 664px สูง 224px
lv_obj_align(card1, LV_ALIGN_TOP_MID, 0, 204);       // จัดไว้ที่ y=204
lv_obj_set_style_bg_color(card1, lv_color_hex(0x12121E), 0);  // สีพื้นหลัง
lv_obj_set_style_radius(card1, 16, 0);                // มุมโค้ง

lv_obj_add_flag(card1, LV_OBJ_FLAG_CLICKABLE);        // ให้กดได้
lv_obj_add_event_cb(card1, [](lv_event_t *) {
    if (s_card1_cb) s_card1_cb();                     // เรียก callback ที่ main.cpp ผูกไว้
}, LV_EVENT_CLICKED, NULL);
```

`s_card1_cb` คือ function pointer ที่ main.cpp ส่งมาให้ตอน boot:
```c
// ใน main.cpp บรรทัด 895
ui_home_set_card1_cb([]() {
    lv_scr_load_anim(scr_user, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
});
```
→ กด Card 1 = เปิดหน้า USER (Sensor Dashboard) ด้วย animation เลื่อนซ้าย

### Timezone (บรรทัด 568–569)

```c
setenv("TZ", "ICT-7", 1);
tzset();
```

บอก C runtime ว่าเวลาท้องถิ่นคือ UTC+7 (Bangkok) — ทำให้ `localtime_r()` คืนค่าเป็นเวลาไทยถูกต้อง ไม่ใช่ UTC

### สร้าง task (บรรทัด 574–577)

```c
lv_timer_create(clock_timer_cb, 30000, NULL);         // นาฬิกา ทุก 30 วินาที (ใน LVGL thread)
xTaskCreate(weather_task,     "weather",  8192, NULL, 2, NULL);  // weather ใน task แยก
xTaskCreate(room_status_task, "room_st",  6144, NULL, 2, NULL);  // room status ใน task แยก
```

`8192` และ `6144` คือขนาด stack ของแต่ละ task (byte) — weather ต้องใหญ่กว่าเพราะ JSON ที่ parse มีหลาย level

---

## ส่วนที่ 8: ui_home_update_sensors (บรรทัด 582–597)

```c
void ui_home_update_sensors(float pm25, float temp, float hum)
{
    if (isnan(pm25)) lv_label_set_text(lbl_pm25_h, "--");
    else { snprintf(buf, sizeof(buf), "%.0f", pm25); lv_label_set_text(lbl_pm25_h, buf); }
    ...
}
```

ฟังก์ชันนี้ไม่ได้อยู่ใน task ตัวเอง — มันถูกเรียกจาก `sensor_read_task` ใน main.cpp ซึ่ง lock ไว้แล้ว จึงไม่ต้อง lock ซ้ำในนี้

`isnan(pm25)` — ตรวจว่าค่าเป็น "ไม่ใช่ตัวเลข" หรือเปล่า — ถ้า sensor ตอบผิดพลาดหรือยังไม่ได้ข้อมูล ค่าจะเป็น `NAN` แทนที่จะแสดง 0 ผิดๆ

---

## สรุป: ui_home.cpp ทำอะไรบ้าง

| งาน | วิธี | ความถี่ |
|---|---|---|
| อัปเดตนาฬิกา + ทักทาย | LVGL timer (`clock_timer_cb`) | ทุก 30 วินาที |
| เปลี่ยนสี emoji | ถูกเรียกจาก clock timer | ทุก 30 วินาที |
| ดึงสภาพอากาศ | `weather_task` → HTTP GET → cJSON | ทุก 10 นาที |
| ดึงสถานะห้อง | `room_status_task` → HTTP GET → cJSON → 4 กรณี | ทุก 30 วินาที |
| อัปเดต PM2.5/temp/hum | `ui_home_update_sensors()` ถูกเรียกจาก sensor_read_task | ทุก 2 วินาที |

ไฟล์ถัดไป: `03_sensor_pipeline.md` — `sensor_config.cpp` + `calib.cpp` — วิธีเลือกชนิด sensor และปรับค่าให้ถูกต้อง