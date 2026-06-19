# Weekly Report

## ชื่อผู้ฝึกงาน
- กุดา ทองนาม (Kuda Tonnam)

## Week
- Week 2 — Begin IoT: ESP32-P4 Display, Sensor & Control (9 – 19 มิถุนายน 2569)

## หัวข้อที่ฝึก
- GPIO: relay control และ RS485 UART
- LVGL 9: UI design บน display 720×720 MIPI DSI
- Modbus RTU: อ่านค่าจาก sensor SN-300BYH-M ผ่าน RS485
- MQTT + Ethernet: ส่งข้อมูล sensor ขึ้น Home Assistant
- Home Assistant: dashboard, automation, LINE Messaging API alert

## สิ่งที่ทำสำเร็จ
1. สร้าง UI หลักบน ESP32-P4 display (Air Quality, PM History, Relay Control) พร้อม touch navigation
2. อ่านค่า Temp / Humidity / PM2.5 / PM10 / Sound จาก SN-300BYH-M ผ่าน RS485 Modbus RTU และแสดงผลแบบ real-time
3. เชื่อมต่อ Ethernet (static IP 192.168.1.200) และส่งข้อมูล sensor ขึ้น Home Assistant ผ่าน MQTT ทุก 2 วินาที
4. ควบคุม Relay 2 ตัวได้ทั้งจากหน้าจอ ESP และจาก HA dashboard พร้อม feedback state
5. ระบบ Alert แสดง banner บนหน้าจอเมื่อค่าเกิน threshold (Temp >35°C, Hum <30%, PM2.5 AQI)
6. ส่ง LINE notification ผ่าน LINE Messaging API เมื่อมี alert (debounce 2 นาที)
7. SNTP time sync: PM History cards แสดงเวลาจริง (เช่น 15:00, 14:00)
8. Setup mode: หน้าเว็บบน ESP สำหรับ upload logo และเลือก sensor model + Slave ID
9. Sensor model registry: รองรับหลาย RS485 sensor model เปลี่ยนได้ผ่านเว็บ ไม่ต้อง flash ใหม่

## ปัญหาที่พบ
1. ESP32-P4 BOOT button อยู่ที่ GPIO35 (ไม่ใช่ GPIO0 ตาม datasheet ทั่วไป) ทำให้ detect button ไม่ได้ในตอนแรก
2. RS485 multi-drop: ไม่เข้าใจว่าจะต่อ sensor 2 ตัวบน port A+/B- เส้นเดียวกันได้อย่างไร

## วิธีแก้ไข
1. ตรวจสอบ schematic ของ Waveshare ESP32-P4-86-Panel แล้วพบว่า BOOT = GPIO35, Relay = GPIO32/46, RS485 = GPIO47(TX)/48(RX)
2. RS485 เป็น bus protocol รองรับ multi-drop: ต่อ sensor หลายตัว parallel บนสาย A+/B- เส้นเดียว โดยกำหนด Modbus Slave ID ต่างกัน

## สิ่งที่ได้เรียนรู้
1. LVGL 9: ใช้ `lv_layer_top()` สร้าง overlay ที่แสดงทุกหน้าจอโดยไม่ถูก screen transition ทับ
2. Modbus RTU: protocol structure (slave ID, function code, CRC16), register map, และการอ่าน holding registers
3. MQTT retained message: broker เก็บค่าล่าสุดไว้ให้ subscriber ใหม่รับได้ทันทีตอน connect
4. Home Assistant: สามารถใช้แทน Node-RED + Grafana + SQLite ได้ในโปรเจกต์ระดับนี้ ง่ายกว่าและ integrate กับ LINE ได้โดยตรง
5. NVS (Non-Volatile Storage): บันทึก config ลง flash ของ ESP32 ให้คงอยู่หลัง reboot

## Link GitHub / Code
- https://github.com/AilurusFulgenss/iot-internship/tree/main/week-02-esp32-basic/liv24-pio

## รูปภาพ / Screenshot / Video Demo
- (รอเพิ่มรูปถ่าย wiring และวิดีโอ demo 1 นาที)

## แผนงานสัปดาห์ถัดไป
1. เพิ่ม sensor model ที่ 2 (temp/humidity sensor ใหม่ที่พี่เตรียมให้วันจันทร์) กรอก register map จริงใน sensor_config.cpp
2. ทดสอบ RS485 multi-drop: ต่อ sensor 2 ตัวบนสาย A+/B- เส้นเดียวกัน อ่านค่าพร้อมกัน
3. เพิ่มหน้าแสดงผล sensor ตัวที่ 2 บน display
________________________________________
