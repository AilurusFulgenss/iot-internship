# `main.cpp` — จุดเริ่มต้นทุกอย่างของบอร์ด LIV-24

ไฟล์นี้คือ "สมองหลัก" ของอุปกรณ์ ทุกอย่างเริ่มต้นที่นี่ ตั้งแต่เปิดเครื่องจนถึงแสดงผลบนจอและรับข้อมูลจาก sensor

---

## ภาพรวม flow ตั้งแต่เปิดเครื่อง

```
เปิดไฟ → app_main()
  ├─ เตรียม network stack, sensor config, RS485
  ├─ ตรวจสอบว่ากดปุ่ม BOOT ค้างไว้ไหม → ถ้าใช่ ลบ logo แล้ว restart
  ├─ เปิดจอ LVGL + เพิ่ม PSRAM pool 256KB
  ├─ ตรวจว่ามี logo ใน SPIFFS ไหม
  │   ├─ ไม่มี → เปิดหน้า Setup (QR code + รอ upload logo)
  │   └─ มี → สร้างทุก screen + แสดง splash 2.5s → เข้าหน้า Home
  ├─ สร้าง sensor_read_task (วนอ่าน RS485 ทุก 2 วินาที)
  └─ เชื่อม MQTT + เริ่ม Ethernet
```

---

## ส่วนที่ 1: #include และ define (บรรทัด 1–46)

### บรรทัด 1–31: รายการ include

ทุก `#include` คือการ "นำของเข้าห้อง" ก่อนใช้งาน:

| Include | ทำหน้าที่อะไรในโปรเจกต์เรา |
|---|---|
| `freertos/FreeRTOS.h` | ระบบ OS ของบอร์ด — ทำให้รัน sensor task กับ LVGL พร้อมกันได้ |
| `esp_log.h` | พิมพ์ log เช่น `ESP_LOGI(TAG, "T=%.1fC")` ใน Serial Monitor |
| `esp_heap_caps.h` | จอง RAM พิเศษ เช่น PSRAM สำหรับ LVGL buffer |
| `driver/gpio.h` | ควบคุม GPIO — ใช้เปิด/ปิด relay ที่ขา 32 และ 46 |
| `driver/uart.h` | ส่ง-รับข้อมูลผ่านสาย RS485 กับ sensor |
| `bsp/esp32_p4_wifi6_touch_lcd_4b.h` | BSP ของบอร์ด Waveshare — เปิดจอ, เปิด backlight, lock LVGL |
| `lvgl.h` | ไลบรารีวาดหน้าจอ — ปุ่ม, label, สี, animation |
| `ui_user.h` … `ui_home.h` | แต่ละไฟล์นี้คือ 1 หน้าจอ เช่น `ui_home.h` = หน้า Home |
| `wifi_mqtt.h` | เชื่อมต่อ MQTT broker บน RPi |
| `cJSON.h` | แปลง JSON ที่รับจาก MQTT เป็น struct ใช้งานได้ |

### บรรทัด 33–46: define ค่าคงที่

```c
#define BOOT_BTN    GPIO_NUM_35   // ขาปุ่ม BOOT บนบอร์ด
#define RELAY1_GPIO GPIO_NUM_32   // สายไฟ relay ตัวที่ 1
#define RELAY2_GPIO GPIO_NUM_46   // สายไฟ relay ตัวที่ 2
#define NUM_PAGES   3             // จำนวนหน้าจอในโหมด USER (Sensor, PM, Relay)

#define RS485_TXD      GPIO_NUM_47  // ขาส่งข้อมูลไปหา sensor
#define RS485_RXD      GPIO_NUM_48  // ขารับข้อมูลจาก sensor
#define RS485_UART     UART_NUM_1   // ใช้ UART port หมายเลข 1
#define MODBUS_BAUD    9600         // ความเร็วสื่อสาร 9600 bit/s
#define MODBUS_TIMEOUT pdMS_TO_TICKS(300)  // รอ sensor ตอบกลับ 300 มิลลิวินาที
```

---

## ส่วนที่ 2: ตัวแปร global (บรรทัด 48–64)

```c
static lv_obj_t *scr[NUM_PAGES];    // เก็บ pointer ของ 3 หน้าจอ USER
lv_obj_t *lbl_temp_val;             // label แสดงค่าอุณหภูมิบนหน้า Sensor
static bool relay_on[2] = {false, false};  // สถานะ relay ปัจจุบัน
```

ทำไมต้องเก็บ `relay_on[]` ไว้?
→ เพราะเวลากด toggle relay ต้องรู้ว่าตอนนี้ ON หรือ OFF เพื่อสลับสถานะ และเวลา MQTT ส่งคำสั่งมาก็อ่านค่านี้เพื่ออัปเดตปุ่มบนจอด้วย

---

## ส่วนที่ 3: make_hline และ make_label (บรรทัด 68–89)

ฟังก์ชัน helper ที่ไม่มีชื่อในไฟล์อื่น แต่ใช้ซ้ำหลายที่ใน main.cpp:

**`make_hline(parent, y)`** — วาดเส้นแนวนอนสีฟ้า `#00E5FF` กว้าง 560px
- ใช้ในหน้า Sensor Dashboard และ Splash screen ขีดเส้นคั่นส่วนต่างๆ

**`make_label(parent, text, color, font, align, x, y)`** — สร้าง label สีและฟอนต์ตามต้องการ
- ใช้ทุกที่ในหน้าจอ เพราะ LVGL ต้องการ 5-6 บรรทัดต่อ 1 label จึง wrap ไว้เป็นฟังก์ชันเดียว

---

## ส่วนที่ 4: create_splash (บรรทัด 94–123)

หน้าจอแรกที่ผู้ใช้เห็นเมื่อเปิดเครื่อง — แสดง logo ของ Sansiri/VOLTIVA

```c
if (eth_upload_has_logo()) {
    // มี logo.jpg ใน SPIFFS → แสดงภาพ + ข้อความ LIV-24
} else {
    // ยังไม่มี logo → แสดงข้อความ fallback สีเขียว
}
```

ในสถานที่จริง: เมื่อ setup ครั้งแรกเสร็จและ upload logo ผ่าน QR code แล้ว ทุก reboot จะเห็นโลโก้ที่ upload ไว้

---

## ส่วนที่ 5: create_eth_setup_screen (บรรทัด 128–195)

หน้า Setup Mode — ใช้ครั้งแรกที่ติดตั้งอุปกรณ์ หรือกด BOOT ค้างตอนเปิดเครื่อง

แบ่งเป็น 2 คอลัมน์:
- **ซ้าย:** QR code → browse ไปอัปโหลด logo ผ่าน IP ของบอร์ด
- **ขวา:** QR code → เพิ่ม Telegram bot `@liv24alert_bot` เพื่อรับแจ้งเตือน

`lv_obj_add_flag(qr, LV_OBJ_FLAG_HIDDEN)` — QR ซ้ายซ่อนอยู่ก่อน รอจน Ethernet ได้รับ IP แล้วค่อยแสดง (callback จาก `eth_upload_start`)

---

## ส่วนที่ 6: create_sensors (บรรทัด 208–227)

สร้างหน้า Sensor Dashboard (scr[1]) — แสดงค่า temp, humidity, sound, PM2.5, PM10

`add_sensor_row()` วางชื่อ + ค่า + หน่วยให้เรียงกันโดยอัตโนมัติ ตัวเลขค่าวัดเป็น label `lbl_temp_val` ฯลฯ ที่จะถูก update จาก `sensor_read_task` ทุก 2 วินาที

---

## ส่วนที่ 7: relay_set_state (บรรทัด 234–251)

ฟังก์ชันหลักในการเปิด/ปิด relay — เรียกได้จากทั้ง 2 ที่:

1. **ผู้ใช้กดปุ่มบนจอ** (relay_cb → relay_set_state)
2. **MQTT รับคำสั่งจาก RPi/HA** (wifi_mqtt callback → relay_set_state)

```c
gpio_set_level(RELAY_GPIO[idx], on ? 1 : 0);   // เปิด/ปิดไฟจริงๆ ผ่าน GPIO
lv_label_set_text(relay_btn_lbl[idx], on ? "ON" : "OFF");  // อัปเดตปุ่มบนจอ
wifi_mqtt_publish_relay_state(idx, on);          // บอก HA ว่า relay เปลี่ยนสถานะแล้ว
```

`bsp_display_lock(0)` / `bsp_display_unlock()` — จำเป็นเสมอก่อนแตะ LVGL object จาก task อื่น (ไม่ใช่ LVGL task) ป้องกัน crash แบบ race condition

---

## ส่วนที่ 8: crc16 และ modbus_read (บรรทัด 331–415)

### crc16 (บรรทัด 331–340)

CRC = Cyclic Redundancy Check — สูตรคำนวณเลขตรวจสอบความถูกต้องของข้อมูล

ในโปรเจกต์นี้: sensor SN-300 ใช้ Modbus RTU ซึ่งกำหนดว่าทุก packet ต้องมี CRC 2 byte ท้ายสุด ถ้า CRC ไม่ตรง = ข้อมูลเสีย ทิ้งทิ้งทิ้ง

### modbus_read (บรรทัด 386–415)

ส่งคำถามไปหา sensor ผ่าน RS485 และรอคำตอบ:

```
บอร์ด → [slave_id][fc][reg_start][count][CRC] → sensor SN-300
sensor → [slave_id][fc][byte_count][data...][CRC] → บอร์ด
```

- `slave_id` = หมายเลขของ sensor บนสายเดียวกัน (ตั้งค่าใน DEV screen)
- `fc` = Function Code เช่น 0x03 = อ่าน holding register
- `start_reg` = register แรกที่ต้องการอ่าน
- `count` = จำนวน register ที่อ่าน

ถ้า sensor ไม่ตอบใน 300ms → คืน `false` → log ว่า "sensor: no response"

---

## ส่วนที่ 9: cwt_th_set_baud_9600 (บรรทัด 346–364)

sensor รุ่น CWT-TH04S จากโรงงานมาด้วยความเร็ว 4800 baud แต่โปรเจกต์ใช้ 9600 baud

ฟังก์ชันนี้รันครั้งเดียวตอน boot แรก: สั่งให้ sensor เปลี่ยนความเร็วตัวเอง แล้ว boot ครั้งต่อๆ ไป sensor จะพร้อมที่ 9600 แล้ว (ฟังก์ชันนี้ timeout เฉยๆ ไม่ error)

---

## ส่วนที่ 10: rs485_init (บรรทัด 366–381)

ตั้งค่า UART สำหรับ RS485:

```c
uart_driver_install(RS485_UART, 256, 0, 0, NULL, 0);   // buffer 256 byte
uart_param_config(RS485_UART, &cfg);                    // 9600 baud, 8N1
uart_set_pin(RS485_UART, GPIO_NUM_47, GPIO_NUM_48, ...); // TX=47, RX=48
uart_set_mode(RS485_UART, UART_MODE_RS485_HALF_DUPLEX); // ส่งหรือรับทีละอย่าง
```

Half-duplex = สายเดียว ส่งได้ทีละทิศทาง เหมือนวิทยุสื่อสาร "over"

---

## ส่วนที่ 11: sensor_read_task (บรรทัด 417–529)

Task ที่วนทำงานตลอดชีพของบอร์ด ทุกๆ 2 วินาที:

```
อ่านค่าจาก sensor (modbus_read)
    ↓
แปลงค่า raw register → float (หาร scale)
    ↓
ส่งเข้า calib_apply() เพื่อเทียบมาตรฐาน
    ↓
lock LVGL → อัปเดตทุก screen ที่เกี่ยวข้อง → unlock
    ↓
publish ค่า calibrated ไปยัง MQTT
    ↓
รอ 2 วินาที
```

รองรับ sensor 5 ประเภท:

| type | sensor ที่ใช้ | ค่าที่ได้ |
|---|---|---|
| `SENSOR_TYPE_PM` | SN-300BYH-M | temp, hum, PM2.5, PM10, sound |
| `SENSOR_TYPE_EC` | EC/TDS sensor | ค่าสื่อนำไฟฟ้าของน้ำ |
| `SENSOR_TYPE_LEAK` | leak detector | มีน้ำรั่วหรือเปล่า |
| `SENSOR_TYPE_TH` | CWT-TH04S | temp + humidity (อาคาร) |
| `SENSOR_TYPE_ORP` | BH-485-ORP | ค่า ORP น้ำสระว่ายน้ำ |

ผลลัพธ์: ค่าที่ calibrated จะไปขึ้นทุกหน้าจอพร้อมกัน และ publish ไปที่ MQTT topic `liv24/sensors`, `liv24/ec`, `liv24/orp` ฯลฯ ใน HA

---

## ส่วนที่ 12: touch_nav_init (บรรทัด 533–618)

ตั้งค่าการนำทางบนหน้าจอ touch ทั้งหมด 3 ส่วน:

### ส่วน A: ปุ่ม ▶ ล่างขวา (บรรทัด 537–557)
มีในหน้า USER, PM, Relay — กดแล้วเลื่อนไปหน้าถัดไปตาม cycle

### ส่วน B: Hidden zone 5-tap (บรรทัด 562–596)
ช่องกดลับ ไม่มีปุ่มให้เห็น — สำหรับเจ้าหน้าที่เท่านั้น
- **มุมบนซ้าย กด 5 ครั้งภายใน 3 วินาที** → เข้า DEV mode (ปรับ calibration)
- **กลางบนสุด กด 5 ครั้งภายใน 3 วินาที** → เข้า EXEC mode (ดู executive overview)

### ส่วน C: ปุ่ม ◀ USER (บรรทัด 599–617)
ปุ่มออกจากหน้า DEV และ EXEC กลับสู่ USER mode

---

## ส่วนที่ 13: on_short_press (บรรทัด 623–646)

ตัดสินใจว่าจะไปหน้าไหนเมื่อกดปุ่ม ▶:

```
ถ้าอยู่หน้า USER  → ไป PM History
ถ้าอยู่หน้า PM   → ไป Relay Control
ถ้าอยู่หน้าอื่น  → กลับ Home
ถ้าอยู่หน้า EXEC → ไม่ทำอะไร (short press ใช้ไม่ได้ใน EXEC)
```

---

## ส่วนที่ 14: on_mode_changed (บรรทัด 648–663)

เมื่อ mode เปลี่ยน (ผ่านปุ่มลับหรือปุ่ม ◀ USER) → โหลดหน้าจอที่ถูกต้อง:

- `MODE_USER` → scr_user (fade in)
- `MODE_DEV` → scr_dev (slide จากขวา)
- `MODE_EXEC` → scr_exec (ไม่มี animation ให้เร็ว)

---

## ส่วนที่ 15: MQTT callback functions (บรรทัด 667–787)

ฟังก์ชันที่รอรับข้อมูลจาก MQTT broker บน RPi:

| ฟังก์ชัน | รับจาก topic | ทำอะไร |
|---|---|---|
| `on_test_alert` | `liv24/test_alert` | แสดง alert banner ทดสอบ 15 วินาที |
| `on_hist_24h` | `liv24/history/24h` | อัปเดตกราฟ 24 ชั่วโมงในหน้า PM |
| `on_hist_7d` | `liv24/history/7d` | อัปเดตกราฟ 7 วันในหน้า EXEC Detail |
| `on_hist_ec/orp/hhcc/th/leak` | `liv24/history/...` | ข้อมูลย้อนหลังของ sensor ชนิดอื่นๆ |
| `on_flora` | `liv24/flora` | อัปเดตข้อมูลเซ็นเซอร์ต้นไม้ HHCC |
| `logo_url_received` | `liv24/logo_url` | download logo ใหม่แล้ว restart บอร์ด |

ทุกฟังก์ชันใช้ pattern เดียวกัน:
```c
if (bsp_display_lock(0)) {
    // อัปเดต UI
    bsp_display_unlock();
}
```

---

## ส่วนที่ 16: app_main (บรรทัด 789–963)

จุดเริ่มต้นของโปรแกรม — ESP-IDF เรียก `app_main()` หลัง boot

### ขั้นตอน 1: เตรียมพื้นฐาน (บรรทัด 795–805)
```c
esp_netif_init();                // เตรียม network stack
esp_event_loop_create_default(); // เตรียม event system
sensor_config_init();            // โหลดค่า sensor ที่บันทึกไว้ (model, slave ID)
calib_init();                    // โหลดค่า calibration จาก NVS flash
rs485_init();                    // เริ่ม UART RS485
```

### ขั้นตอน 2: ตรวจปุ่ม BOOT (บรรทัด 818–826)
ถ้ากด BOOT ค้างตอนเปิดเครื่อง → ลบ logo.jpg → restart → เข้า Setup Mode
→ ใช้เมื่อต้องการเปลี่ยนโลโก้หรือ reset setup

### ขั้นตอน 3: เปิดจอ (บรรทัด 828–851)
```c
bsp_display_start_with_config(&disp_cfg);  // เปิดจอ 720x1280 + LVGL
lv_mem_add_pool(psram_pool, 256*1024);     // เพิ่ม PSRAM 256KB ให้ LVGL
bsp_display_backlight_on();                // เปิด backlight
```

PSRAM pool จำเป็นเพราะ LVGL ต้องการ RAM จำนวนมากสำหรับ image + animation ESP32-P4 มี internal RAM ไม่พอ

### ขั้นตอน 4: ตรวจ logo (บรรทัด 865–873)
ถ้าไม่มี logo → เปิดหน้า Setup แล้ว return ออก (ไม่สร้างหน้าจออื่น)

### ขั้นตอน 5: สร้างทุก screen (บรรทัด 879–938)
ทำภายใน lock block เดียว เพื่อป้องกัน bug ของ ESP32-P4 Rev 1.3 ที่อาจ deadlock ถ้า unlock ระหว่างสร้าง screen

```c
create_splash()         // scr[0]: โลโก้
create_sensors()        // scr[1]: Sensor Dashboard
create_relay_ctrl()     // scr[2]: Relay Control
ui_user_create()        // หน้าหลัก USER
ui_pm_create()          // PM History
ui_dev_create()         // DEV calibration
ui_exec_create()        // EXEC overview
ui_exec_detail_create() // EXEC detail graph
ui_home_create()        // หน้า Home
ui_booking_create()     // Building picker (legacy)
ui_room_list_create()   // Room list (legacy)
ui_room_detail_create() // Room detail (legacy)
ui_book_confirm_create()// Booking confirm (legacy)
touch_nav_init()        // ปุ่ม ▶ และ hidden zones
ui_alert_init()         // ระบบ alert banner
```

> หมายเหตุ: หน้า booking/room_list/room_detail/book_confirm ยังอยู่ใน firmware แต่ booking ย้ายไปใช้ HA Calendar แทนแล้ว หน้าเหล่านี้ยังเชื่อมกันอยู่แต่ผู้ใช้ไม่เข้าถึงได้จากหน้า Home ปกติ

### ขั้นตอน 6: กำหนด navigation callbacks (บรรทัด 895–936)
เชื่อมโยงการกดปุ่มระหว่างหน้าจอต่างๆ เช่น:
- กด Card 1 บน Home → ไปหน้า USER
- กด Card 2 บน Home → ไปหน้า Building Picker (legacy booking flow)

### ขั้นตอน 7: แสดง Splash แล้วเข้า Home (บรรทัด 937–945)
```c
lv_scr_load(scr[0]);               // แสดง splash ทันที
bsp_display_unlock();
vTaskDelay(pdMS_TO_TICKS(2500));   // รอ 2.5 วินาที
lv_scr_load_anim(scr_home, LV_SCR_LOAD_ANIM_FADE_IN, 600, 0, false);  // fade เข้า Home
```

### ขั้นตอน 8: เชื่อม MQTT + เริ่ม Ethernet (บรรทัด 950–957)
Register callback ก่อนเปิด Ethernet เพราะ DHCP อาจเสร็จในระหว่าง `eth_start_background()` — ถ้า register หลัง จะพลาด IP event

```c
wifi_mqtt_set_relay_cb(relay_set_state);     // เมื่อ MQTT สั่ง relay
wifi_mqtt_set_logo_url_cb(logo_url_received); // เมื่อ MQTT ส่ง URL โลโก้ใหม่
// ... callbacks อื่นๆ
wifi_mqtt_init(MQTT_BROKER_URI);    // เริ่ม MQTT client
eth_start_background();             // เริ่ม Ethernet ในอีก task
```

### ทำไม BOOT button ถูก comment ออก (บรรทัด 959–963)
```c
// btn_mode_init(BOOT_BTN);
// GPIO35 = EMAC RMII TXD1
```
GPIO35 ที่บอร์ดใช้เป็น BOOT button นั้น ถูก Ethernet chip ใช้เป็น TXD1 ด้วย ถ้า configure เป็น input จะทำให้ Ethernet ส่งข้อมูลเสียหายทุก packet — จึงปิดการใช้งานปุ่ม BOOT ไว้ก่อน และใช้ touch zone บนจอแทน

---

## สรุป: main.cpp ทำอะไรบ้าง

1. เปิดเครื่อง → ตรวจ logo → เลือก boot path (setup หรือ normal)
2. สร้างทุกหน้าจอ และเชื่อม navigation ระหว่างกัน
3. รัน `sensor_read_task` — วนอ่าน sensor RS485 ทุก 2 วินาที อัปเดตจอและ MQTT
4. เชื่อม MQTT callbacks — รับคำสั่งจาก RPi/HA (relay, history, alert, logo)
5. ดู hidden tap zones ไว้ให้เจ้าหน้าที่เข้า DEV/EXEC mode

ไฟล์ถัดไปที่ควรดูคือ `ui_home.cpp` — หน้า Home ที่แสดงนาฬิกา, สภาพอากาศ, และสถานะห้องประชุม
