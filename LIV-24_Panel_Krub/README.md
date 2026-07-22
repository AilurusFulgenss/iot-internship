# LIV-24 Panel — Standalone Firmware

ESP32-P4 firmware สำหรับ Waveshare 86-Panel แบบ standalone (ไม่ต้องพึ่ง network หรือ MQTT)
อ่านค่าจาก SN-300BYH-M ผ่าน RS485/Modbus แล้วแสดงผลบนหน้าจอสัมผัส

---

## Hardware

| Component | รายละเอียด |
|---|---|
| MCU | ESP32-P4 |
| Panel | Waveshare ESP32-P4-86-Panel-ETH-2RO (720×1280, MIPI DSI) |
| Touch | GT911 (I2C) |
| Sensor | SN-300BYH-M — Temp / Hum / PM2.5 / PM10 / Sound |
| Interface | RS485 Half-Duplex, Modbus RTU (UART1, 9600 baud) |
| Pin RS485 | TXD=GPIO47, RXD=GPIO48 |

---

## หน้าจอ

### User Screen
- แสดงค่า Temp, Humidity, PM2.5, PM10, Sound แบบ real-time
- AQI color toggle — สลับระหว่างแสดงสีตาม AQI tier กับสีคงที่
- ปุ่ม Brightness
- กด 5 ครั้งที่มุมบนซ้ายภายใน 3 วินาที → เข้า PIN modal

### Dev Screen (PIN: 999999)
- Calibration ของ SN-300: offset และ gain สำหรับทุก sensor
- บันทึกลง NVS ด้วยปุ่ม SAVE TO NVS
- ปุ่ม Back กลับหน้า User

---

## Build

```bash
# ต้องใช้ PlatformIO + pioarduino platform
pio run -e esp32p4
```

> **หมายเหตุ:** build path ต้องไม่มี space (ใช้ `C:\iot\...` แทน OneDrive path)

---

## Flash

```bash
pio run -e esp32p4 --target upload
```

---

## Dependencies

จัดการโดย ESP-IDF Component Manager (`dependencies.lock`):

- `waveshare/esp32_p4_wifi6_touch_lcd_4b` — BSP
- `lvgl/lvgl` 9.x — UI framework
- `espressif/esp_lvgl_port` — LVGL + ESP-IDF glue
- `espressif/esp_lcd_touch_gt911` — touch driver

---

## Project Structure

```
src/
├── main.cpp          # app_main, sensor task, mode switching
├── ui_user.cpp/h     # หน้าแสดงค่า sensor + AQI
├── ui_dev.cpp/h      # หน้า calibration (DEV mode)
├── ui_pin.cpp/h      # PIN entry modal
├── calib.cpp/h       # calibration apply + NVS save/load
└── logo_img.h        # RGB565 logo image (hardcoded)
```
