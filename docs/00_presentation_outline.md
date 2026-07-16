# แผนอธิบาย Firmware LIV-24 ให้ทีม Software
**วันที่นำเสนอ:** 14 กรกฎาคม 2026  
**ระยะเวลาแนะนำ:** 45–60 นาที  
**Path หลัก:** `C:\iot\week-02\liv24-pio\src\`

---

## พื้นฐานก่อนเข้า: ทีม software มาจาก Swift/Java

เมื่อชั้นอธิบาย ให้เทียบแนวคิด c/c++ กับสิ่งที่ทีมรู้แล้ว:

| แนวคิดใน C/ESP-IDF | เทียบกับ Swift/Java |
|---|---|
| `#include "file.h"` | `import Module` |
| `struct` | `struct` (Swift) / POJO |
| `void*` / `callback` | closure / lambda |
| `xTaskCreate(func, ...)` | `Task { ... }` / Thread |
| `static` variable | instance variable ที่ live ตลอด app |
| `ESP_LOGI(TAG, "...")` | `print()` / `NSLog()` / `Log.d()` |
| `#define RELAY1_GPIO 32` | `static let relay1Pin = 32` |
| `NVS` (Non-Volatile Storage) | `UserDefaults` (iOS) / `SharedPreferences` |
| `SPIFFS` filesystem | bundle resource / assets folder |

---

## โครงสร้างไฟล์ทั้งหมด — แยก Active vs Dead

```
src/
├── [A] CORE
│     main.cpp              ← จุดเริ่มต้น boot + orchestrator ทุกอย่าง
│                             (มี dead code ภายใน: create_sensors/scr[1]/cur_page)
│
├── [B] HARDWARE DRIVERS
│     sensor_config.h/.cpp  ← เลือกชนิด sensor + เก็บค่า slave ID
│     calib.h/.cpp          ← ค่าเทียบมาตรฐาน sensor (offset/scale)
│     eth_upload.h/.cpp     ← upload logo ผ่าน Ethernet + SPIFFS
│     btn_mode.h/.cpp       ← partial: set_app_mode() ใช้งาน / btn_mode_init() comment ออก
│                             (ปุ่ม BOOT GPIO35 ชนกับ Ethernet TXD1)
│
├── [C] CONNECTIVITY
│     wifi_mqtt.h/.cpp      ← เชื่อม MQTT กับ RPi / Home Assistant
│     history.h/.cpp        ← เก็บและ parse ข้อมูลย้อนหลัง 24h/7d
│
├── [D] UI: Active — หน้าจอที่ผู้ใช้เข้าถึงได้จริง
│     ui_home.h/.cpp        ← Home screen (นาฬิกา, สภาพอากาศ, สถานะห้อง)
│     ui_user.h/.cpp        ← USER mode: ค่า sensor 6 tab (PM/HHCC/EC/ORP/LEAK/TH)
│     ui_pm.h/.cpp          ← PM History: กราฟ 24h PM2.5/PM10
│     ui_alert.h/.cpp       ← Alert banner (แจ้งเตือนค่าเกิน threshold)
│     ui_exec.h/.cpp        ← EXEC overview (hidden: 5-tap top-center)
│     ui_exec_detail.h/.cpp ← EXEC detail: กราฟย้อนหลัง 7 วัน
│     ui_dev.h/.cpp         ← DEV mode (hidden: 5-tap top-left)
│
└── [E] Dead code — compile ได้แต่ไม่มีทางเข้าถึงใน runtime
      ui_booking.h/.cpp      ← Building picker  ┐ card2 บน Home ไม่มี
      ui_room_list.h/.cpp    ← Room list        │ click event แล้ว
      ui_room_detail.h/.cpp  ← Room detail      │ (display-only)
      ui_book_confirm.h/.cpp ← ยืนยันการจอง   ┘
      ui_th.h/.cpp           ← ไม่ถูก include จากที่ไหนเลย (รวมอยู่ใน ui_user แล้ว)
      ui_orp.h/.cpp          ← เหมือนกัน
      ui_flora.h/.cpp        ← เหมือนกัน
```

### Dead code ภายใน main.cpp

| Symbol | บรรทัด | เหตุผล |
|---|---|---|
| `create_sensors()` | 208–227 | สร้าง scr[1] แต่ไม่อยู่ใน navigation cycle |
| `add_sensor_row()` | 199–206 | ใช้แค่ใน create_sensors() |
| `lbl_temp/hum/sound/pm25/pm10_val` | 55–59 | pointer ของ scr[1] ไม่มีใคร update |
| `cur_page` | 52 | ตัวแปรจากระบบ paging เก่า ไม่มีใครอ่าน/เขียน |
| `ui_home_set_card2_cb(...)` | 899–902 | register callback ที่ card2 ไม่มี click event |
| booking nav callbacks | 904–936 | ทั้งหมดชี้ไปหน้าที่เข้าไม่ได้ |

---

## แผนอธิบาย: เริ่มจากไหน เจาะตรงไหน

### ช่วงที่ 1 — "ภาษาและสภาพแวดล้อม" (5 นาที)
**เจาะ:** ไม่ต้องอ่าน code  
อธิบาย stack:
- ภาษา: **C++17** (ใช้ C เป็นหลัก มี C++ เฉพาะ lambda/auto)
- Framework: **ESP-IDF** (เหมือน iOS SDK แต่สำหรับ ESP32)
- UI Library: **LVGL 9** (วาดหน้าจอแบบ immediate-mode ไม่มี DOM)
- OS: **FreeRTOS** (mini OS บน chip — จัดการ task หลายอัน)
- Build: **PlatformIO** (เหมือน npm แต่สำหรับ embedded)

---

### ช่วงที่ 2 — "Boot flow: จากเปิดไฟจนขึ้นจอ" (10 นาที)
**เจาะ code:** `main.cpp` บรรทัดหลัก

**ทำไมสำคัญ:** นี่คือจุดที่บอร์ด ESP32 จะ **boot loop** ถ้าทำผิดลำดับ

```
app_main()  [main.cpp:789]
   │
   ├─ esp_netif_init()          ← ต้องมาก่อนทุกอย่างที่ใช้ network
   ├─ sensor_config_init()      ← โหลด config จาก NVS flash
   ├─ calib_init()              ← โหลด calibration จาก NVS flash
   ├─ rs485_init()              ← เปิด UART สำหรับ sensor
   │
   ├─ ตรวจ BOOT button          ← กด BOOT ค้าง = reset setup
   │
   ├─ bsp_display_start()       ← เปิดจอ ← *** ต้องก่อน xTaskCreate ทุกอัน ***
   ├─ lv_mem_add_pool(PSRAM)    ← เพิ่ม RAM 256KB ← *** ถ้าไม่มีตรงนี้ = boot loop ***
   ├─ eth_upload_init()         ← mount SPIFFS ตรวจว่ามี logo ไหม
   │
   ├─ xTaskCreate(sensor_read_task)  ← สร้าง task อ่าน sensor
   │
   ├─ ถ้าไม่มี logo → Setup screen → return (หยุดที่นี่)
   │
   ├─ bsp_display_lock()
   │     สร้างทุก screen ใน lock block เดียว  ← *** ป้องกัน ESP32-P4 deadlock ***
   │     lv_scr_load(splash)
   │   bsp_display_unlock()
   │
   ├─ vTaskDelay(2500ms)        ← รอ splash 2.5 วินาที
   ├─ fade เข้า Home screen
   │
   ├─ register MQTT callbacks   ← ต้องก่อน eth_start เพราะ DHCP เสร็จเร็ว
   └─ eth_start_background()    ← เริ่ม Ethernet
```

**เจาะ line-by-line ตรงนี้:**
- `main.cpp:828–851` — LVGL init + PSRAM pool (อธิบาย why boot loop ถ้าขาดตรงนี้)
- `main.cpp:879–938` — ทำไมสร้าง screen ใน lock block เดียว

---

### ช่วงที่ 3 — "ทำไม boot loop? — PSRAM + lock" (10 นาที)
**เจาะ:** นี่คือส่วนที่ทีม software จะถามมากที่สุด

**root cause ของ boot loop บน ESP32 display:**

```
❌ ผิด: สร้าง task ก่อน display init
     xTaskCreate(sensor_task)     ← task เริ่มรัน
     bsp_display_start()          ← ยัง init ไม่เสร็จ
     sensor_task เรียก ui_update() ← bsp_display_lock() crash = boot loop

✅ ถูก:
     bsp_display_start()          ← init จอก่อน
     lv_mem_add_pool(psram, 256KB)← RAM พอ ← ถ้าไม่มี LVGL จะ alloc fail = boot loop
     xTaskCreate(sensor_task)     ← สร้าง task หลัง display พร้อม
```

**root cause ที่ 2: LVGL ไม่ thread-safe**
```c
// ❌ ผิด: แตะ LVGL object จาก task อื่นตรงๆ
lv_label_set_text(lbl, "25.3");   // crash ถ้า LVGL task กำลัง redraw อยู่

// ✅ ถูก: lock ก่อนเสมอ
bsp_display_lock(0);
lv_label_set_text(lbl, "25.3");
bsp_display_unlock();
```

ทุก `ui_xxx_update()` call ใน sensor_read_task ผ่าน lock ทั้งหมด

---

### ช่วงที่ 4 — "Sensor pipeline: จากสาย RS485 จนขึ้นจอ" (10 นาที)
**เจาะ code:** `main.cpp:417–529` (sensor_read_task)

```
sensor อยู่บนสาย RS485
    │
    ↓ ทุก 2 วินาที
modbus_read(slave_id, reg_start, count)    [main.cpp:386]
    │  ส่ง binary packet: [ID][FC][REG_H][REG_L][COUNT][CRC]
    │  รอ 300ms ถ้าไม่มีตอบ = "no response"
    ↓
raw uint16_t registers[]
    │  เช่น reg[1] = 253 หมายถึง 25.3°C (หาร scale 10.0)
    ↓
calib_apply(raw_value, &g_calib.temp)      [calib.cpp]
    │  linear: calibrated = raw * scale + offset
    ↓
bsp_display_lock(0)
    ui_user_update(temp, hum, pm25, pm10, sound)
    ui_pm_update(...)
    ui_home_update_sensors(...)
    ui_alert_check(...)       ← ตรวจว่า PM เกิน threshold ไหม
bsp_display_unlock()
    ↓
wifi_mqtt_publish_sensors(...)  ← ส่งไป RPi → HA
```

**เจาะ line-by-line:**
- `main.cpp:386–415` — modbus_read: อธิบาย binary protocol
- `main.cpp:440–444` — calib_apply: เหมือน y = mx + b
- `main.cpp:446–454` — lock block: ทำไมต้องมี

---

### ช่วงที่ 5 — "MQTT: ข้อมูลไปถึง HA ยังไง" (5 นาที)
**เจาะ:** `wifi_mqtt.cpp` — แค่ overview ไม่ต้องลงลึก

```
ESP32  →  MQTT publish  →  Mosquitto (RPi)  →  Home Assistant
       ←  MQTT subscribe ←  relay command   ←  HA automation
```

Topics หลัก:
- `liv24/sensors` → temp, hum, PM2.5, PM10, sound
- `liv24/relay/1/set` → ON/OFF relay 1
- `liv24/history/24h` → RPi ส่ง historical data มาให้จอ

---

### ช่วงที่ 6 — "หน้าจอ ui_home: ทำงานยังไง" (5 นาที)
**เจาะ:** `ui_home.cpp` — ตรงนี้เป็น recent work

3 task ที่รันพร้อมกันบน ui_home:
1. **clock_task** — update เวลาทุก 1 วินาที
2. **weather_task** — ดึงอากาศจาก OpenWeather API ทุก 10 นาที
3. **room_status_task** — ดึงสถานะห้องจาก HA Calendar API ทุก 60 วินาที

แต่ละ task ใช้ pattern เดียวกัน: `for(;;) { ... vTaskDelay(60s); }`

---

### ช่วงที่ 7 — "DEV/EXEC: hidden mode สำหรับเจ้าหน้าที่" (3 นาที)
**เจาะ:** `main.cpp:559–596`

กด 5 ครั้งภายใน 3 วินาทีที่มุมบนซ้าย = DEV mode
→ เปิดหน้า calibration ที่ผู้ใช้ทั่วไปไม่เห็น
→ ปุ่มลับ เพราะหน้าจอนี้ไม่ควรมีปุ่มชัดเจนให้กดผิด

---

## ส่วนที่ไม่จำเป็นต้องเจาะ line-by-line (ข้ามได้)

| ไฟล์ | เหตุผลที่ข้ามได้ |
|---|---|
| `ui_booking.cpp`, `ui_room_*.cpp`, `ui_book_confirm.cpp` | **Dead code** — card2 บน Home ไม่มี click event แล้ว เข้าถึงไม่ได้ใน runtime |
| `ui_th.cpp`, `ui_orp.cpp`, `ui_flora.cpp` | **Dead code** — ไม่มีใคร include เลย ฟังก์ชันรวมอยู่ใน ui_user.cpp แล้ว |
| `ui_exec_detail.cpp` | graph rendering ธรรมดา ไม่มี logic พิเศษ |
| `btn_mode.cpp` | partial: `set_app_mode()` ยังใช้ แต่ physical button (GPIO35) ปิดไว้เพราะชน Ethernet TXD1 |
| `create_sensors()` ใน main.cpp | **Dead code** ภายใน main.cpp — scr[1] ไม่อยู่ใน navigation cycle |

---

## 5 คำถามที่ทีม Software จะถามมากที่สุด

### คำถามที่ 1: "ทำไมต้อง lock/unlock ก่อนอัปเดตหน้าจอ? ใน iOS/Android ไม่เห็นต้องทำ"

**เหตุผล:** LVGL บน ESP-IDF รัน render loop ใน task แยกต่างหาก ถ้า task อื่น (เช่น sensor task) ไปแก้ label ตรงๆ โดยไม่ lock อาจชนกัน (race condition) crash ทันที — iOS/Android มี main thread ที่รับรองว่า UI updates ปลอดภัย แต่ FreeRTOS ไม่มีกลไกแบบนั้น built-in

**คำตอบสั้น:** "เหมือน `DispatchQueue.main.async { }` ใน Swift — ต้อง push UI update กลับมาที่ LVGL thread เสมอ แต่ใน ESP เราใช้ lock แทน"

---

### คำถามที่ 2: "ทำไม boot loop? ต่างกับ app crash ยังไง?"

**เหตุผล:** ESP32 มี watchdog timer — ถ้า app crash หรือ hang นานกว่า timeout → บอร์ด reset ตัวเองอัตโนมัติ → boot ใหม่ → crash อีก → วนไปเรื่อยๆ = boot loop

สาเหตุหลักในโปรเจกต์นี้คือ:
1. LVGL alloc fail เพราะ RAM ไม่พอ → เพิ่ม PSRAM pool 256KB แก้ได้
2. task แตะ LVGL ก่อน display init → จัดลำดับใน app_main แก้ได้
3. ESP32-P4 ROM bug: unlock display ระหว่างสร้าง screen → สร้างทุก screen ใน lock block เดียว

---

### คำถามที่ 3: "ทำไมใช้ C++ แต่เขียนแบบ C? ทำไมไม่ใช้ class?"

**เหตุผล:** Embedded development ส่วนใหญ่เลือก "C with C++ features" ไม่ใช่ full OOP เพราะ:
- `class` + vtable เพิ่ม binary size
- dynamic allocation (`new/delete`) บน embedded อันตราย — memory fragmentation crash
- `lambda` และ `auto` ใช้ได้ เพราะ zero-cost abstraction
- ใน codebase นี้ใช้ lambda เฉพาะ callback สั้น เช่น `[](lv_event_t *) { on_short_press(); }`

---

### คำถามที่ 4: "RS485 / Modbus RTU คืออะไร? ทำไมไม่ใช้ WiFi หรือ BLE กับ sensor?"

**เหตุผล:** 
- **RS485** = protocol สายทองแดงแบบ industrial ทนสัญญาณรบกวน ระยะ 1200m
- **Modbus RTU** = ภาษากลางที่ sensor อุตสาหกรรมทุกยี่ห้อพูดได้ เหมือน REST API แต่ระดับ hardware
- WiFi/BLE sensor มักแพงกว่าและ battery-dependent ส่วน RS485 sensor ถูกและทน — เหมาะกับ permanent install

**อธิบาย binary packet:**
```
ส่ง: [0x01][0x03][0x00][0x00][0x00][0x06][CRC_L][CRC_H]
      slave  FC   reg_start         count   checksum
รับ:  [0x01][0x03][0x0C][data x6 registers][CRC]
```

---

### คำถามที่ 5: "ตรงไหนที่ logic ยากและซับซ้อนที่สุด?"

**คำตอบ:** มี 3 จุด:

**จุดที่ 1 — PSRAM pool + lock ordering** (`main.cpp:844–863`)  
ลำดับที่ต้องถูกต้องเป๊ะมาก: display_start → add_pool → xTaskCreate → ถ้าผิดลำดับ = boot loop หาสาเหตุยากมาก

**จุดที่ 2 — room_status_task ใน ui_home.cpp**  
ต้องรอ NTP sync ก่อน, parse ISO 8601 datetime, handle 4 กรณีแสดงผล, ทั้งหมดอยู่ใน background task ที่ lock LVGL ตอน update

**จุดที่ 3 — MQTT callback chaining**  
RPi ส่ง historical JSON มา → `on_hist_24h()` → `hist_parse_24h()` → `ui_pm_refresh_history()` → LVGL chart update — callback chain 3 ชั้น ถ้า lock ไม่ถูก crash ไม่มี error message

---

## เส้นทางแนะนำสำหรับ presentation วันที่ 14

```
1. [5  min] ภาษา + stack overview (Swift analogy table)
2. [10 min] Boot flow diagram + show main.cpp
3. [10 min] ทำไม boot loop + PSRAM + lock (สำคัญที่สุด)
4. [10 min] Sensor pipeline: RS485 → calib → UI → MQTT
5. [5  min] MQTT ↔ HA integration overview
6. [5  min] ui_home: 3 concurrent tasks
7. [5  min] DEV/EXEC hidden mode
8. [10 min] Q&A (ใช้ 5 คำถามด้านบนเตรียมตอบ)
```

**tip:** เปิด Serial Monitor พร้อมกันจะเห็น log real-time ระหว่าง present → ช่วยให้ทีมเห็นว่า code ทำงานจริง ไม่ใช่แค่ slide

---

## ไฟล์ .md ที่จะเขียนต่อ (ทีละไฟล์)

> ตัดไฟล์ dead code ออกจากแผน — ไม่จำเป็นต้องอธิบาย ui_th/orp/flora และ booking flow
> เพราะ software team จะถามถึงสิ่งที่ทำงานจริงเท่านั้น

| ลำดับ | ไฟล์ | สถานะ |
|---|---|---|
| 01 | `01_main.md` | ✅ เสร็จแล้ว |
| 02 | `02_ui_home.md` | ✅ เสร็จแล้ว |
| 03 | `03_sensor_pipeline.md` (sensor_config + calib) | ✅ เสร็จแล้ว |
| 04 | `04_wifi_mqtt.md` | ✅ เสร็จแล้ว |
| 05 | `05_history.md` | ✅ เสร็จแล้ว |
| 06 | `06_ui_user.md` | ✅ เสร็จแล้ว |
| 07 | `07_ui_pm.md` | รอ |
| 08 | `08_ui_alert.md` | รอ |
| 09 | `09_ui_exec.md` | รอ |
| 10 | `10_ui_dev.md` | รอ |
| 11 | `11_eth_upload.md` | รอ |
| ~~12~~ | ~~`12_ui_booking_legacy.md`~~ | ❌ ตัดออก — dead code ทั้งหมด ไม่ต้องอธิบาย |

**Dead code ที่ควรพูดถึงแค่สั้นๆ ระหว่าง present (ไม่ต้องเขียน .md แยก):**
- `ui_booking/room_list/room_detail/book_confirm` → "เคยทำ booking UI ไว้ แต่เปลี่ยน design ให้ Home card แสดงสถานะอย่างเดียว ยังไม่ได้ลบโค้ดออก"
- `ui_th/orp/flora` → "prototype แยกไฟล์ก่อน ตอนนี้รวมอยู่ใน ui_user.cpp แล้ว ไฟล์เก่ายังค้างอยู่"
- `create_sensors()` ใน main.cpp → "หน้า sensor แบบเก่าก่อน refactor ยังอยู่ใน code แต่ไม่มีทาง navigate ไปถึง"

---

---

# Script คร่าวๆ สำหรับ Present

> สิ่งที่พูดในวงเล็บ `(...)` คือ action — ชี้โค้ด เปิดไฟล์ หรือ pause

---

## [0:00] เปิด — ภาษาและ Stack (5 นาที)

"สวัสดีครับ วันนี้จะมาเล่าให้ฟังว่า firmware ของ LIV-24 ทำงานยังไง ตั้งแต่เปิดไฟจนค่า sensor ขึ้นจอและส่งไปถึง Home Assistant"

"ก่อนอื่นเลย ภาษาที่ใช้คือ C++ ครับ แต่จริงๆ เขียนแบบ C เป็นหลัก มี C++ เฉพาะที่ใช้ lambda กับ auto บางจุด — ถ้าใครคุ้นกับ Swift หรือ Java จะพอ follow ได้ เดี๋ยวจะเทียบให้ตลอด"

"Framework ที่ใช้บน chip เรียกว่า ESP-IDF — เหมือน iOS SDK แต่สำหรับ ESP32 ข้างในมี OS จิ๋วชื่อ FreeRTOS ที่ให้เราสร้าง task หลายอันรันพร้อมกันได้ แบบเดียวกับ Thread ใน Java หรือ Task ใน Swift"

"UI ที่วาดบนจอใช้ library ชื่อ LVGL — มันไม่มี DOM ไม่มี layout engine อย่าง UIKit นะครับ วาด pixel เองตรงๆ เหมือนเขียน SpriteKit แต่ primitive กว่ามาก"

"Build system ใช้ PlatformIO — เหมือน npm หรือ CocoaPods แต่สำหรับ embedded ใครเคยใช้ CMake จะคุ้น"

*(pause ถามว่ามีคำถาม stack ไหม ก่อนข้ามไป)*

---

## [0:05] Boot Flow (10 นาที)

`(เปิด main.cpp บรรทัด 789)`

"นี่คือ `app_main` — entry point ของทุกอย่าง เหมือน `main()` ใน Java หรือ `@main` struct ใน Swift"

"ลำดับที่ทำตอน boot มีความสำคัญมากครับ ถ้าทำผิดลำดับบอร์ดจะ boot loop — เดี๋ยวอธิบายว่า boot loop คืออะไร"

`(ชี้บรรทัด 796–800)`

"อันดับแรกสุด init network stack กับโหลด config ที่เซฟไว้ใน flash — config คือว่า sensor ที่ต่ออยู่เป็น model อะไร slave address คืออะไร เก็บแบบ key-value เหมือน UserDefaults ใน iOS"

`(ชี้บรรทัด 840)`

"จากนั้น init จอ — ตรงนี้สำคัญมาก ทุกอย่างที่จะแตะ UI ต้องมาหลังบรรทัดนี้เท่านั้น"

`(ชี้บรรทัด 844–847)`

"แล้วก็เพิ่ม RAM พิเศษจาก PSRAM เข้า LVGL — เดี๋ยวอธิบายว่าทำไมต้องทำตรงนี้โดยเฉพาะ"

`(ชี้บรรทัด 863)`

"สร้าง sensor task ที่นี่ — หลัง display init เสมอ ไม่งั้น crash"

`(ชี้บรรทัด 865–873)`

"ถ้าไม่มีโลโก้ในเครื่อง — แสดง setup screen พร้อม QR code แล้ว return ออก ไม่บูทต่อ เพราะยังไม่ configure"

`(ชี้บรรทัด 879–938)`

"ถ้ามีโลโก้ — สร้างทุก screen ใน lock block เดียว อธิบายว่าทำไมตอนถัดไปครับ"

`(ชี้บรรทัด 941–944)`

"รอ 2.5 วินาทีให้ splash แสดง แล้ว fade เข้า Home screen"

`(ชี้บรรทัด 950–957)`

"ลงทะเบียน MQTT callback ก่อน start Ethernet — เหตุผลคือ DHCP อาจเสร็จเร็วมาก ถ้า register callback หลัง start Ethernet อาจพลาด event IP_GOT ไป แล้ว MQTT ไม่เชื่อมต่อ"

---

## [0:15] ทำไม Boot Loop (10 นาที)

"เดี๋ยวมาอธิบาย boot loop ก่อนนะครับ — บน iOS ถ้า app crash มันแค่ปิดแล้วหยุด แต่บน ESP32 มี watchdog timer ถ้า app hang หรือ crash นานเกินกำหนด บอร์ด reset ตัวเองแล้ว boot ใหม่ crash อีก วนไปเรื่อยๆ หน้าจอกระพริบ debug ยากมากเพราะไม่มี error popup"

"โปรเจคนี้เจอ boot loop สาเหตุสามอย่างครับ"

**สาเหตุที่ 1 — RAM ไม่พอ**

`(ชี้บรรทัด 844–847)`

"ESP32 มี RAM ในตัว 768KB ฟังดูเยอะ แต่พอสร้างหน้าจอ 13 หน้าพร้อม widget เต็มๆ LVGL alloc fail เงียบๆ แล้ว crash"

"บอร์ดนี้มี PSRAM ต่ออยู่ด้วย 8MB แต่ LVGL ไม่รู้จักมันเอง ต้องบอกมันด้วย 2 บรรทัดนี้ — จอง 256KB จาก PSRAM แล้ว register ให้ LVGL รู้ว่ามี RAM เพิ่ม"

"ถ้าลบ 2 บรรทัดนี้ออก บูทได้แค่ 6–7 หน้าจอ พอถึงหน้า 8 memory หมด crash เงียบๆ"

**สาเหตุที่ 2 — task เร็วเกินไป**

`(ชี้บรรทัด 861–863 พร้อม comment)`

"sensor task รัน infinite loop ทุก 2 วินาที ข้างในมีการ lock จอ ถ้า task เริ่มก่อน display init เสร็จ มัน lock จอที่ยังไม่มีอยู่ — assert fail → restart comment นี้ตั้งใจเขียนไว้เตือนตัวเองเพราะเคยย้ายบรรทัดนี้ขึ้นไปแล้วเจอ boot loop จริงๆ"

**สาเหตุที่ 3 — ESP32-P4 ROM Bug**

`(ชี้ comment บรรทัด 876–878)`

"อันนี้พิเศษหน่อยครับ — เป็น hardware bug ใน chip รุ่นนี้โดยเฉพาะ Rev 1.3 มี bug ใน ROM ที่ถ้า unlock จอระหว่างสร้าง LVGL object ที่อยู่ใน PSRAM บางสถานการณ์ L2 cache dirty แล้ว deadlock ทำให้ต้องสร้างทุก screen ในช่วง lock ต่อเนื่องยาวๆ ไม่มีการ unlock กลางทาง"

"คิดเหมือน database transaction ครับ — เปิด transaction ทำทุกอย่างจบแล้ว commit ครั้งเดียว ระหว่างนั้นไม่มีใครแทรกได้"

**Lock คืออะไร**

"แล้ว lock ที่ว่านี้คืออะไร — LVGL มี task ของตัวเองที่คอยวาดหน้าจอตลอดเวลา sensor task ก็รันอยู่ด้วย สอง task นี้แย่งใช้ LVGL data structure ตัวเดียวกัน ถ้าชนกันข้อมูลฉีก crash"

"`bsp_display_lock()` คือขอ mutex ครับ — ใครได้ก่อนทำงานได้คนเดียว อีกคนรอ สิ่งที่เราทำใน lock คือแค่อัปเดตค่า label ไม่ได้วาดเอง — การวาดจริงเกิดใน LVGL task หลัง unlock เสมอ"

```
Sensor task:  [lock] set_text("12.3") — ปักธง dirty [unlock]
LVGL task:    .................................[lock] เห็น dirty → วาด [unlock]
```

"เทียบกับ Swift ได้เลย — `DispatchQueue.main.async { label.text = "12.3" }` คือ pattern เดียวกัน แค่บน embedded เราใช้ mutex แทน dispatch queue"

*(pause ถามว่ามีคำถามไหม ก่อนข้ามไปช่วง 4)*

---

## [0:25] Sensor Pipeline (10 นาที)

"ตอนนี้บอร์ดบูทขึ้นมาได้แล้ว คำถามคือค่า PM2.5 ที่โชว์บนจอ มาจากไหน ผ่านอะไรบ้าง"

`(วาดบนกระดานหรือชี้ whiteboard)`

```
Sensor → สาย RS485 → อ่านค่าดิบ → calibrate → อัปเดต UI → ส่ง MQTT
```

**RS485 / Modbus**

`(ชี้ main.cpp บรรทัด 386–414)`

"Sensor เชื่อมด้วยสาย RS485 ครับ — protocol อุตสาหกรรมที่ใช้กันมา 40 ปีแล้ว ทนสัญญาณรบกวนได้ดี sensor พวกนี้ราคาถูกกว่า WiFi sensor มาก เหมาะกับ install ถาวร"

"การอ่านค่าใช้ Modbus RTU — ส่ง binary packet 8 bytes บอก sensor ว่า 'ขอ register 0x0000 ถึง 0x0005' sensor ตอบกลับมาเป็นตัวเลข รอ 300ms ถ้าไม่ตอบ log แล้วข้ามรอบนี้ไป"

`(ชี้บรรทัด 431–435)`

**แปลงค่าดิบ**

"ค่าที่ได้มาเป็น integer ดิบ เช่น 253 หมายถึง 25.3°C เพราะ Modbus ไม่มี float ต้องหาร scale เอง"

"ทำไมต้อง cast เป็น `int16_t` ก่อนหาร — เพราะถ้าอุณหภูมิติดลบ sensor ส่งมาเป็น two's complement ถ้าไม่ cast จะได้เลขบวกผิดๆ แทน เรียนรู้จากห้องเย็นจริงครับ"

`(ชี้บรรทัด 440–444)`

**Calibration**

"ค่าดิบไม่แม่นยำ 100% เพราะ sensor มี tolerance ทางการผลิต แก้ด้วยสูตร `(raw + offset) × gain` — เหมือน y = mx + b ค่า offset กับ gain เก็บใน flash ปรับผ่านหน้า DEV mode ได้โดยไม่ต้อง reflash"

`(ชี้บรรทัด 446–454)`

**อัปเดต UI**

"หลัง calibrate — lock จอ แล้วอัปเดตพร้อมกัน 5 หน้าในครั้งเดียว USER, PM History, Home card, DEV, EXEC แล้ว unlock จอ"

"ทำไมอัปเดตทีเดียว 5 หน้า — เพราะทุกหน้าแสดงค่าเดิม ถ้า unlock แล้ว lock ใหม่ทีละหน้า user อาจเห็นหน้าไม่ sync กัน แตก 1 lock block เดียวปลอดภัยกว่า"

`(ชี้บรรทัด 456)`

**ส่ง MQTT**

"MQTT publish อยู่นอก lock block — ตั้งใจ เพราะ network I/O อาจ block นานไม่รู้ ถ้า lock จอค้าง LVGL หยุดวาด หน้าจอค้าง"

`(ชี้บรรทัด 527)`

"แล้ว sleep 2000ms — FreeRTOS pause task นี้ไว้ CPU ไปทำงานอื่นแทน วนซ้ำแบบนี้ตลอด"

---

## [0:35] MQTT ↔ HA (5 นาที)

"ค่าที่ส่งออกไปทาง MQTT ไปถึง RPi ที่รัน Home Assistant ครับ"

`(เปิด wifi_mqtt.cpp หรือวาด diagram)`

```
ESP32  →  liv24/sensors  →  Mosquitto (RPi)  →  HA automation
       ←  liv24/relay/1/set  ←  HA script    ←  user กดใน HA
       ←  liv24/history/24h  ←  Python script ←  คำนวณค่าเฉลี่ยทุกชม.
```

"ทิศทางสำคัญ — ESP32 ส่ง sensor data ออก HA ส่ง relay command กลับมา RPi ส่ง historical data มาให้จอวาดกราฟ"

"Pattern ที่ใช้คือ function pointer callback — register ก่อน init เหมือน delegate pattern ใน Swift ป้องกัน circular dependency ระหว่าง main.cpp กับ wifi_mqtt.cpp"

---

## [0:40] ui_home — 3 Concurrent Tasks (5 นาที)

`(เปิด ui_home.cpp)`

"Home screen น่าสนใจหน่อยครับ เพราะมี 3 อย่างรันพร้อมกันบนหน้าเดียว"

"อันแรกคือ clock — เป็น LVGL timer ไม่ใช่ FreeRTOS task วิ่งอยู่ใน LVGL thread เอง ไม่ต้อง lock"

"อันสองคือ weather — FreeRTOS task ดึง OpenWeather API ทุก 10 นาที HTTP GET ธรรมดา parse JSON แล้ว lock อัปเดต label"

"อันสามคือ room status — task ที่ซับซ้อนที่สุด ดึงจาก HA Local Calendar API ทุก 30 วินาที ต้องรอ NTP sync ก่อน ถ้ายังไม่ sync ปีจะเป็น 1970 แล้ว URL ที่ build จะผิด"

"logic แสดงผลมี 4 case จากสอง flag — ห้องถูกจองอยู่ไหม และมีการจองถัดไปไหม สี่ combination ให้ text layout ที่ต่างกัน"

---

## [0:45] DEV/EXEC Hidden Mode (5 นาที)

`(ชี้ main.cpp บรรทัด 559–596)`

"หน้า DEV กับ EXEC เข้าไม่ได้จากหน้าจอปกติครับ — ต้องแตะมุมบนซ้าย 5 ครั้งใน 3 วินาที"

"ทำไมซ่อน — เพราะ DEV mode ให้ปรับ calibration ได้ ถ้าผู้ใช้ทั่วไปเข้าไปปรับเองค่าผิดหมด ไม่มีปุ่มชัดเจนก็ไม่มีใครกดผิด"

"EXEC mode รวม sensor ทุกตัวไว้หน้าเดียวพร้อมกราฟ 7 วัน สำหรับ executive หรือช่างที่ต้องดู overview"

`(ถ้ามีเวลา demo จริงบนบอร์ด — กดมุม 5 ครั้ง)`

---

## [0:50] Dead Code — พูดสั้นๆ ถ้ามีคนถาม

"อีกอย่างที่ควรรู้คือมี code บางส่วนที่ compile อยู่แต่ไม่ได้ใช้งานจริงครับ"

"มีหน้า booking UI ที่เคยทำไว้ — Building picker, Room list, Room detail, Booking confirm — แต่ตอนนี้ Home card เปลี่ยนเป็นแสดงสถานะห้องอย่างเดียว ไม่มี click event แล้ว เลยเข้าไม่ได้"

"แล้วก็มีไฟล์ ui_th, ui_orp, ui_flora ที่เป็น prototype เก่า ตอนนี้รวมอยู่ใน ui_user.cpp แล้วทั้งหมด ไฟล์เก่ายังค้างอยู่แต่ไม่มีใคร include"

"ทั้งหมดนี้ยังไม่ได้ลบเพราะยังอยู่ระหว่าง development ครับ"

---

## [0:55] Q&A (10 นาที)

*(ใช้ 5 คำถามที่เตรียมไว้ด้านบน)*

**ถ้าเงียบ — kick start ด้วยคำถามตัวเอง:**

> "ผมเองก็อยากรู้ว่าจากมุมมอง software team มีส่วนไหนที่ดู over-engineered หรือน่าจะทำง่ายกว่านี้ได้บ้างครับ?"

*(คำถามนี้ดี เพราะเปิดให้ทีมวิจารณ์ได้สบาย และแสดงว่าเราพร้อมรับ feedback)*
