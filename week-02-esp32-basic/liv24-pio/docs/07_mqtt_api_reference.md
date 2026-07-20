# LIV-24 MQTT & Web API Reference

## MQTT

### Topic Prefix

```
esp32_p4_86/{device_id}
```

`device_id` = 12-char hex ของ base MAC address ตัวอย่าง `30eda0ea5d0f`  
ดูได้ที่หน้า DEV Screen → Network tab

---

### Subscribe Topics (HA/Broker → ESP32)

| Topic | QoS | Payload | Retain | หน้าที่ |
|-------|-----|---------|--------|---------|
| `{prefix}/relay/1/set` | 0 | `ON` \| `OFF` | — | สั่ง Relay 1 เปิด/ปิด |
| `{prefix}/relay/2/set` | 0 | `ON` \| `OFF` | — | สั่ง Relay 2 เปิด/ปิด |
| `{prefix}/relay/mode` | 1 | `remote` \| `local` | ✓ | สลับโหมด Remote/Local control |
| `{prefix}/logo/url` | 1 | URL string | ✓ | URL ของรูป logo ที่ ESP32 จะ fetch มาเก็บใน SPIFFS |
| `{prefix}/test/alert` | 0 | JSON object | — | ฉีด sensor data เพื่อทดสอบ alert banner บนหน้าจอ |

#### Payload: `test/alert`
```json
{
  "temp":  38.5,
  "hum":   25.0,
  "pm25":  36.0,
  "pm10":  45.0,
  "sound": 85
}
```
ค่าที่ไม่ต้องการทดสอบให้ละเว้น (จะเป็น NaN → ไม่ trigger alert)

---

### Publish Topics (ESP32 → Broker)

| Topic | QoS | Payload | Retain | หน้าที่ |
|-------|-----|---------|--------|---------|
| `{prefix}/sensors` | 0 | JSON object | — | ค่า sensor อัปเดตทุก 2 วินาที |
| `{prefix}/relay/1/state` | 1 | `ON` \| `OFF` | ✓ | สถานะจริงของ Relay 1 (ยืนยันทุกครั้งที่เปลี่ยน) |
| `{prefix}/relay/2/state` | 1 | `ON` \| `OFF` | ✓ | สถานะจริงของ Relay 2 (ยืนยันทุกครั้งที่เปลี่ยน) |
| `{prefix}/relay/mode/state` | 1 | `remote` \| `local` | ✓ | โหมดที่จอยืนยันหลัง apply (bidirectional confirmation) |

#### Payload: `sensors`
```json
{
  "temperature": 26.5,
  "humidity":    62.3,
  "sound":       48,
  "pm2_5":       12.4,
  "pm10":        18.7
}
```
ค่า calibration offset ถูกใส่แล้วก่อน publish (ดู `calib.h`)

---

### State Sync Flow

```
HA toggle switch
  → publish  relay/{1,2}/set = "ON"
  → ESP32 รับ → set GPIO → update UI
  → publish  relay/{1,2}/state = "ON"   ← ยืนยันกลับ (retain)
  → HA switch.liv24_relay_1 อ่านจาก state_topic → แสดง ON

HA toggle Remote Mode
  → publish  relay/mode = "remote"       (retain)
  → ESP32 รับ → disable buttons → update label
  → publish  relay/mode/state = "remote" ← ยืนยันกลับ (retain)
  → HA sensor.liv24_relay_mode แสดง "remote"

ESP32 reconnect หลัง offline
  → broker ส่ง retained messages: relay/mode, relay/1/state, relay/2/state
  → ESP32 sync state จาก broker ทันที
```

---

## Web Server (HTTP)

เข้าได้ที่ `http://{device_ip}/` — เปิดในช่วง setup mode (ก่อนอัป logo) และ normal mode ทุกครั้ง

### Authentication

- PIN: `999999` (ตั้งค่าใน `WEB_PASSWORD` ใน `eth_upload.cpp`)
- หลัง login สำเร็จ: ได้รับ session cookie `sess=<32 hex>` อายุ 12 ชั่วโมง
- ทุก endpoint ยกเว้น `/login` ต้องมี cookie — ถ้าไม่มีจะ redirect `302 → /login`

---

### Endpoints

| Method | Path | Auth | หน้าที่ |
|--------|------|------|---------|
| `GET` | `/login` | — | แสดงหน้า PIN login |
| `POST` | `/login` | — | ตรวจ PIN → ออก session cookie → redirect `/` |
| `GET` | `/` | ✓ | หน้า setup หลัก (Logo + MQTT + HA config) |
| `POST` | `/upload` | ✓ | อัปโหลด logo 48×48 JPEG → บันทึกใน SPIFFS → restart |
| `POST` | `/upload_hd` | ✓ | อัปโหลด logo 192×192 JPEG → บันทึกใน SPIFFS (ใช้ใน splash/header) |
| `GET` | `/mqtt_info` | ✓ | ดึง MQTT config ปัจจุบัน (host, port, device_id, prefix) |
| `POST` | `/mqtt_cfg` | ✓ | บันทึก MQTT broker host+port ลง NVS |
| `GET` | `/ha_info` | ✓ | ดึง HA URL ปัจจุบัน |
| `POST` | `/ha_cfg` | ✓ | บันทึก HA base URL ลง NVS |

---

### Endpoint Details

#### `POST /login`
```
Content-Type: application/x-www-form-urlencoded
Body: pass=999999
```
Response: `302 Found` + `Set-Cookie: sess=<token>; Max-Age=43200; Path=/; HttpOnly`

---

#### `POST /upload` และ `POST /upload_hd`
```
Content-Type: image/jpeg
Body: <raw JPEG bytes>
Max size: 512 KB (server-side) / 10 MB (client-side file picker)
```
Browser ทำ canvas resize ก่อนส่ง → `/upload_hd` ได้ 192×192 JPEG, `/upload` ได้ 48×48 JPEG  
หลัง `/upload` สำเร็จ: ESP32 restart อัตโนมัติ  
Client-side: ถ้าไฟล์ต้นฉบับ > 10 MB ปุ่ม "Upload & Restart" จะ disabled

---

#### `GET /mqtt_info` — Response
```json
{
  "host":      "192.168.1.111",
  "port":      1883,
  "device_id": "30eda0ea5d0f",
  "prefix":    "esp32_p4_86/30eda0ea5d0f"
}
```

#### `POST /mqtt_cfg`
```
Content-Type: application/x-www-form-urlencoded
Body: host=192.168.1.111&port=1883
```
ต้อง restart device เพื่อ apply

---

#### `GET /ha_info` — Response
```json
{ "url": "http://10.24.1.104:8123" }
```

#### `POST /ha_cfg`
```
Content-Type: application/x-www-form-urlencoded
Body: url=http%3A%2F%2F10.24.1.104%3A8123
```
ต้อง restart device เพื่อ apply

---

## NVS Namespaces

| Namespace | Key | Type | ค่า |
|-----------|-----|------|-----|
| `mqtt_cfg` | `host` | string | MQTT broker IP |
| `mqtt_cfg` | `port` | uint16 | MQTT broker port (default 1883) |
| `ha_cfg` | `url` | string | HA base URL |
| `eth_cfg` | `mode` | string | `"dhcp"` \| `"static"` |
| `eth_cfg` | `ip` | string | Static IP (dot-notation) |
| `eth_cfg` | `mask` | string | Subnet mask |

ค่า NVS ถูกเขียนจาก Web UI และ DEV Screen → Network tab
