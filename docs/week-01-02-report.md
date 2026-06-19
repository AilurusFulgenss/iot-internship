# Weekly Report

## ชื่อผู้ฝึกงาน
- กุดา ทองนาม (Kuda Tonnam)

## Week
- Week 1–2 (2 – 19 มิถุนายน 2569)

## หัวข้อที่ฝึก
- การพัฒนา firmware บน ESP32-P4 ด้วย ESP-IDF + PlatformIO
- UI บน display 720×720 MIPI DSI ด้วย LVGL 9
- การอ่านข้อมูล sensor ผ่าน RS485 Modbus RTU
- การเชื่อมต่อเครือข่ายผ่าน Ethernet และสื่อสารด้วย MQTT
- การ integrate กับ Home Assistant บน Raspberry Pi
- ระบบ alert และ LINE notification

## สิ่งที่ทำสำเร็จ

1. **ระบบ UI บนจอ ESP32-P4** — สร้าง 3 หน้าหลักด้วย LVGL 9 ได้แก่ Air Quality (แสดงค่า sensor แบบ real-time พร้อม AQI color), PM History (กราฟค่าเฉลี่ยรายชั่วโมง 5 ค่า ย้อนหลัง 24 ชั่วโมง และ 7 วัน), และ Relay Control (ปุ่ม toggle relay 2 ตัว) สลับหน้าด้วย touch navigation

2. **อ่านค่า sensor จาก SN-300BYH-M ผ่าน RS485 Modbus RTU** — กระบวนการรับค่าและแปลงเป็นตัวเลขมีขั้นตอนดังนี้
   - **การเชื่อมต่อ:** ESP32-P4 ต่อกับ sensor ผ่านสาย RS485 (A+/B-) โดยใช้ GPIO47 (TX) และ GPIO48 (RX) กำหนด baud rate 9600, 8N1, Half-Duplex
   - **การส่งคำสั่ง (Request):** ESP ส่ง Modbus RTU request ทุก 2 วินาที ขนาด 8 bytes เพื่อขอให้ sensor ส่งค่ากลับมา เช่น `01 03 00 00 00 06 C5 C8` หมายความว่า: Slave ID=01, Function=03 (Read Holding Registers), เริ่มที่ address 0x0000, อ่าน 6 registers, ตามด้วย CRC16 2 bytes
   - **ข้อมูลที่วิ่งมาบนสาย (Raw Bytes):** sensor ตอบกลับมาเป็น byte stream เช่น `01 03 0C 01 0C 00 F8 00 00 00 00 01 26 00 00 XX XX` ซึ่งเป็น binary ดิบยังอ่านไม่รู้เรื่อง
   - **การ verify ด้วย CRC16:** ESP ตรวจสอบ 2 bytes สุดท้ายที่เป็น CRC16 checksum ก่อน ถ้าไม่ตรงหมายความว่าข้อมูลเสียหายระหว่างส่ง ทิ้งค่านั้นไปและรอรอบถัดไป
   - **การ decode register เป็นตัวเลข:** หลังผ่าน CRC แล้ว แต่ละ 2 bytes รวมกันเป็น 1 register (16-bit integer) แล้วนำไปแปลงตาม register map ของ SN-300BYH-M
     - Register 0x0000 → Humidity: หารด้วย 10 → `0x00F8 = 248 ÷ 10 = 24.8%`
     - Register 0x0001 → Temperature: หารด้วย 10 → `0x010C = 268 ÷ 10 = 26.8°C`
     - Register 0x0003 → PM10: หารด้วย 10 → `µg/m³`
     - Register 0x0004 → PM2.5: หารด้วย 10 → `µg/m³`
     - Register 0x0005 → Sound: ค่าดิบไม่ต้องหาร → `dB`
   - **แสดงผลบนหน้าจอ:** ค่าที่ได้จะ update label บน LVGL display ทุก 2 วินาที

3. **เชื่อม MQTT กับ Home Assistant** — ESP ส่งข้อมูล sensor ขึ้น HA ทุก 2 วินาที ผ่าน Ethernet (static IP 192.168.1.200) โดย broker คือ Mosquitto ที่รันบน Raspberry Pi (192.168.1.111) สร้าง dashboard บน HA แสดงค่า sensor ครบทุกตัว

4. **ควบคุม Relay จาก HA dashboard** — กด switch บน HA → ส่ง MQTT command → ESP toggle relay จริง และ ESP ส่ง state กลับ HA ทุกครั้งที่มีการเปลี่ยนแปลง (ไม่ว่าจะกดจากหน้าจอ ESP หรือจาก HA) ทำให้ state sync กันตลอด

5. **ระบบ Alert บนหน้าจอ ESP** — ใช้ `lv_layer_top()` สร้าง banner overlay ที่แสดงทับทุกหน้าจอโดยไม่หายเมื่อสลับหน้า โดยมีเงื่อนไขดังนี้
   - ESP อ่านค่า sensor ทุก 2 วินาที และตรวจสอบทันทีว่าเกิน threshold หรือไม่
   - **ถ้าค่าเกิน** → banner แสดงขึ้นทันที ไม่มีการรอ พร้อมข้อความบอกว่าค่าใดเกินและเกินเท่าไหร่ เช่น `! PM2.5 42.3 MODERATE` หรือ `! TEMP 36.1C > 35`
   - **ถ้ามีหลายค่าเกินพร้อมกัน** → ข้อความจะต่อกันในบรรทัดเดียวและ scroll วิ่งไปเรื่อยๆ เช่น `! TEMP 36.1C > 35     ! PM2.5 42.3 MODERATE`
   - **สีของ banner บอกความรุนแรง** — เหลือง (Humidity ต่ำ), ส้ม (Temp สูง หรือ PM2.5 Moderate), แดง (PM2.5 Unhealthy >150.4 หรือ PM10 >254)
   - **ถ้าค่ากลับมาปกติ** → banner หายทันทีในรอบการอ่านถัดไป (ภายใน 2 วินาที)
   - threshold ที่ใช้: Temp >35°C, Humidity <30%, PM2.5 >35.4 µg/m³ (Moderate) / >150.4 µg/m³ (Unhealthy), PM10 >254 µg/m³

6. **LINE Notification** — เมื่อค่า sensor เกิน threshold จะมีการแจ้งเตือนผ่าน LINE ตามเงื่อนไขดังนี้
   - ESP ส่งค่า sensor ขึ้น Home Assistant ทุก 2 วินาที
   - HA ตรวจสอบว่าค่าเกิน threshold หรือไม่ (เช่น PM2.5 > 35.4 µg/m³ หรือ Temp > 35°C)
   - **ถ้าค่าเกินต่อเนื่องครบ 2 นาที** → HA ส่ง LINE message 1 ครั้ง ผ่าน LINE Messaging API (`api.line.me/v2/bot/message/push`)
   - ถ้าค่ากลับมาปกติก่อนครบ 2 นาที → ยกเลิก ไม่ส่ง LINE
   - หลังส่งแล้ว ถ้าค่ายังเกินต่อเนื่อง → **ไม่ส่งซ้ำ** จนกว่าค่าจะกลับมาปกติก่อน แล้วเกินใหม่อีกครั้ง + รออีก 2 นาที ถึงจะส่งครั้งต่อไป
   - วิธีนี้ป้องกันข้อความ spam กรณีค่า sensor กระเพื่อมขึ้นลงรอบ threshold ตลอดเวลา

7. **Setup Mode บน Web Browser** — เมื่อยังไม่มี logo ESP จะเปิด HTTP server ที่ IP ตัวเอง ให้ผู้ใช้เปิด browser เข้ามา upload logo บริษัท, เลือก sensor model, และตั้ง Modbus Slave ID โดยข้อมูลบันทึกลง flash (NVS + SPIFFS) ไม่หายหลัง reboot

8. **Sensor Model Registry** — รองรับ sensor หลาย model บน RS485 bus เดียวกัน (multi-drop) แต่ละ model มี register map ต่างกัน เปลี่ยน model และ Slave ID ได้ผ่านหน้าเว็บโดยไม่ต้อง flash firmware ใหม่

9. **SNTP Time Sync** — ESP sync เวลากับ NTP server หลังได้ Ethernet IP โดยอัตโนมัติ ทำให้ PM History cards แสดงเป็นเวลาจริง เช่น 15:00, 14:00 แทนการนับถอยหลัง -1h, -2h

## ปัญหาที่พบ

1. **GPIO ไม่ตรงกับ datasheet ทั่วไป** — BOOT button, Relay, และ RS485 อยู่คนละ GPIO กับที่ระบุใน ESP32 datasheet ทั่วไป ทำให้ตอนแรก peripheral ไม่ทำงานเลย

2. **Build error: LVGL 9 API เปลี่ยนจาก v8** — เรียกใช้ `lv_qrcode_set_src()` แล้ว compiler ฟ้อง `not declared` เพราะ LVGL 9 เปลี่ยน API ใหม่หมด ต้องค้นหา function ที่ถูกต้องใหม่ทุกครั้ง

3. **Task Watchdog (WDT) crash ตอน Setup Mode** — หลังเพิ่มระบบ Alert เสร็จ พอเข้า Setup Mode ESP บูตแล้วค้าง ขึ้น log `Task watchdog got triggered` และ reboot วนไม่หยุด สาเหตุคือ `ui_alert_check()` ถูกเรียกจาก sensor task แม้ตอน Setup Mode แต่ `ui_alert_init()` ยังไม่ถูกเรียก ทำให้ `s_banner = NULL` แล้วโค้ดพยายาม access NULL pointer ข้างใน display lock ทำให้ LVGL hang และ IDLE task ถูก starve จน WDT ดัง

4. **อัป logo รูปแล้ว ESP crash ตอน restart** — หลัง upload รูปเสร็จ ESP เรียก `esp_restart()` ทันที แต่ปรากฏว่า Ethernet DMA ยังทำงานอยู่ระหว่าง reset ทำให้ DMA เขียนทับ bootloader code ใน SRAM และขึ้น `Illegal instruction` crash บูตไม่ขึ้น ต้องกด flash ใหม่ทุกครั้งที่อัปรูป

5. **PM History กราฟ 7 วันทำไม่ได้บน ESP อย่างเดียว** — ตอนแรกออกแบบให้ ESP เก็บค่าเฉลี่ยรายวันไว้ใน RAM เอง แต่ติดปัญหาคือข้อมูลหายทุกครั้งที่ reboot และ ESP ไม่มี RTC จริงๆ ทำให้ไม่รู้ว่าแต่ละวันคือวันไหน ข้อมูลจึงไม่ถูกต้อง

6. **Relay state ไม่ sync กับ HA** — ตอนแรก HA ส่ง command มาควบคุม relay ได้ แต่ถ้ากด relay จากหน้าจอ ESP โดยตรง HA ไม่รู้ว่า state เปลี่ยน dashboard ยังแสดงค่าเก่าอยู่ และถ้ากดจาก HA อีกครั้ง state จะสลับผิดทิศทาง

7. **DNS ล้มเหลวตอนต่อ MQTT ครั้งแรก** — หลังได้ IP จาก Ethernet แล้วพยายาม connect MQTT broker ด้วย hostname แต่ `getaddrinfo()` ฟ้อง `EAI_AGAIN` ทุกครั้ง เพราะ ARP cache ยังไม่มี entry ของ gateway ทำให้ DNS query แรกไม่ได้รับ reply ก่อน timeout

## วิธีแก้ไข

1. ดู schematic ของ Waveshare ESP32-P4-86-Panel โดยตรงและทดสอบ GPIO ทีละตัว พบว่า BOOT=GPIO35, Relay=GPIO32/46, RS485=GPIO47(TX)/48(RX)

2. ค้นหาใน LVGL 9 source code และ changelog พบว่าต้องใช้ `lv_qrcode_update(obj, data, len)` แทน `lv_qrcode_set_src()` และ API หลายตัวมีการเปลี่ยนชื่อใหม่ทั้งหมด

3. เพิ่ม NULL check `if (!s_banner) return;` ไว้ต้นฟังก์ชัน `ui_alert_check()` เพื่อให้ return ออกทันทีถ้า `ui_alert_init()` ยังไม่ถูกเรียก ทำให้ Setup Mode ทำงานได้ปกติ

4. แก้โดยเรียก `esp_eth_stop()` ก่อนแล้วรอ 200ms ให้ DMA หยุดทำงานก่อนค่อยเรียก `esp_restart()` ทำให้ ESP restart สะอาดโดยไม่ crash

5. ย้ายการเก็บ history ไปไว้บน Home Assistant (Raspberry Pi) แทน โดย HA บันทึกค่าเฉลี่ยรายชั่วโมงลง database ตัวเอง แล้วส่งกลับมาให้ ESP ผ่าน MQTT เมื่อ ESP connect HA จะส่ง retained message ที่เก็บไว้มาให้ทันที ทำให้ได้ข้อมูลย้อนหลังที่ถูกต้องแม้ ESP reboot

6. แก้โดยให้ ESP publish relay state กลับไปที่ MQTT topic `liv24/relay/N/state` ทุกครั้งที่มีการเปลี่ยน state ไม่ว่าจะมาจากหน้าจอหรือจาก HA ทำให้ HA รับรู้ state จริงตลอดเวลา

7. แก้โดยเพิ่ม TCP probe ไปที่ gateway ก่อนเริ่ม MQTT client เพื่อ warm up ARP cache ให้ lwIP มี entry ของ gateway พร้อมก่อนที่ DNS query จะส่งออก

## สิ่งที่ได้เรียนรู้

1. **LVGL 9 overlay** — `lv_layer_top()` คือ layer พิเศษที่อยู่เหนือทุก screen ใช้สร้าง alert banner หรือ status indicator ที่ต้องแสดงตลอดเวลาโดยไม่ถูก screen transition ทับ
2. **RS485 multi-drop** — RS485 เป็น bus protocol สามารถต่อ device หลายตัวบนสาย A+/B- เส้นเดียว โดยแยกกันด้วย Modbus Slave ID ไม่ต้องมีพอร์ตเพิ่ม
3. **MQTT retained message** — broker เก็บค่าล่าสุดของ topic ไว้ ทำให้ ESP ได้รับ state ล่าสุดทันทีที่ connect โดยไม่ต้องรอ publish ใหม่
4. **Home Assistant แทน Node-RED + Grafana** — สำหรับ project ระดับนี้ HA ทำได้ครบกว่า ง่ายกว่า และ maintain ง่ายกว่าการ setup แยกหลาย service
5. **NVS (Non-Volatile Storage)** — วิธีบันทึก config เล็กๆ เช่น sensor model หรือ Slave ID ลง flash ของ ESP32 ให้คงอยู่หลัง reboot โดยไม่ต้อง flash firmware ใหม่

## Link GitHub / Code
- https://github.com/AilurusFulgenss/iot-internship/tree/main/week-02-esp32-basic/liv24-pio

## รูปภาพ / Screenshot / Video Demo
- (รอเพิ่มรูปถ่าย wiring และวิดีโอ demo 1 นาที)

## แผนงานสัปดาห์ถัดไป
1. เพิ่ม sensor model ที่ 2 (temp/humidity sensor ที่พี่เตรียมให้) กรอก register map จริงใน sensor_config.cpp
2. ทดสอบ RS485 multi-drop ต่อ sensor 2 ตัวบนสาย A+/B- เส้นเดียว อ่านค่าพร้อมกัน
3. เพิ่มหน้าแสดงผล sensor ตัวที่ 2 บน display
________________________________________
