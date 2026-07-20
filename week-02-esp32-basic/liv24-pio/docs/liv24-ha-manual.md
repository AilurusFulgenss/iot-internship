# Smart Environment Monitor by LIV24
## Home Assistant Integration Manual — คู่มือการตั้งค่า Home Assistant

**Document No.:** LIV24-MAN-002
**Version:** 1.0
**Date:** June 2026
**Product:** Smart Environment Monitor (LIV24)

---

## Table of Contents / สารบัญ

1. Introduction / บทนำ
2. Prerequisites / สิ่งที่ต้องมีก่อนเริ่มต้น
3. Network Setup / การตั้งค่าเครือข่าย
4. MQTT Broker Setup / การตั้งค่า MQTT Broker
5. HA Configuration File / การตั้งค่า configuration.yaml
6. Automation Setup / การตั้งค่า Automation
7. Telegram Bot Setup / การตั้งค่า Telegram Bot
8. Viewing Sensor Data / การดูข้อมูล Sensor
9. Troubleshooting / การแก้ไขปัญหาเบื้องต้น
10. Appendix: Full YAML Reference / ภาคผนวก

---

## 1. Introduction / บทนำ

คู่มือนี้อธิบายขั้นตอนการเชื่อมต่อ **Smart Environment Monitor** (LIV24) เข้ากับ Home Assistant (HA) ผ่าน MQTT Protocol รวมถึงการตั้งค่า Automation สำหรับแจ้งเตือนผ่าน Telegram

**System Architecture**

```
LIV24 Device
    |
    | MQTT (Ethernet)
    |
Mosquitto Broker (HA Add-on)
    |
    | HA Internal
    |
Home Assistant Core
    |
    | Telegram Bot API
    |
Telegram Bot (@liv24alert_bot)
```

---

## 2. Prerequisites / สิ่งที่ต้องมีก่อนเริ่มต้น

- **Home Assistant OS** ติดตั้งบน Raspberry Pi 4 หรือสูงกว่า
- **Add-on:** Mosquitto MQTT Broker (ติดตั้งจาก HA Add-on Store)
- **Add-on:** File Editor หรือ SSH & Web Terminal (สำหรับแก้ไขไฟล์)
- **Telegram Account** สำหรับรับการแจ้งเตือน
- **Network:** LIV24 และ HA ต้องอยู่ใน Subnet เดียวกัน (`192.168.1.x`)

---

## 3. Network Setup / การตั้งค่าเครือข่าย

Smart Environment Monitor รับ IP จาก DHCP อัตโนมัติ — IP จะแสดงบนหน้าจอ **Setup Screen** เฉพาะตอนที่อุปกรณ์ยังไม่มี Logo (First Boot หรือหลัง Boot-Hold Reset) เท่านั้น หลัง boot ปกติ IP ไม่แสดงบนหน้าจอ ให้ตรวจสอบจาก Router DHCP lease table แทน อุปกรณ์ทั้งหมดในระบบต้องอยู่ใน Subnet เดียวกัน

ตัวอย่าง IP ที่แนะนำ:
- **Router:** `192.168.1.1`
- **Raspberry Pi (HA):** `192.168.1.100` (หรือตามที่กำหนด)
- **Smart Environment Monitor:** ดูจากหน้าจออุปกรณ์

---

## 4. MQTT Broker Setup / การตั้งค่า MQTT Broker

### ติดตั้ง Mosquitto Add-on

1. HA Dashboard → **Settings** → **Add-ons** → **Add-on Store**
2. ค้นหา **Mosquitto broker** → **Install**
3. หลัง Install กด **Start** และเปิด **Start on boot**

### ตั้งค่า MQTT Integration

1. **Settings** → **Devices & Services** → **Add Integration**
2. ค้นหา **MQTT** → เลือก **MQTT**
3. Broker: `localhost` หรือ IP ของ HA, Port: `1883`
4. กด **Submit**

---

## 5. HA Configuration File / การตั้งค่า configuration.yaml

เปิดไฟล์ `/config/configuration.yaml` ด้วย File Editor แล้วเพิ่ม configuration ต่อไปนี้

### MQTT Sensors

```yaml
mqtt:
  sensor:
    - name: "LIV24 Temperature"
      unique_id: "liv24_temp"
      state_topic: "liv24/sensors"
      value_template: "{{ value_json.temp | default(none) }}"
      unit_of_measurement: "°C"
      device_class: temperature
      state_class: measurement

    - name: "LIV24 Humidity"
      unique_id: "liv24_hum"
      state_topic: "liv24/sensors"
      value_template: "{{ value_json.hum | default(none) }}"
      unit_of_measurement: "%"
      device_class: humidity
      state_class: measurement

    - name: "LIV24 PM2.5"
      unique_id: "liv24_pm25"
      state_topic: "liv24/sensors"
      value_template: "{{ value_json.pm25 | default(none) }}"
      unit_of_measurement: "µg/m³"
      device_class: pm25
      state_class: measurement

    - name: "LIV24 PM10"
      unique_id: "liv24_pm10"
      state_topic: "liv24/sensors"
      value_template: "{{ value_json.pm10 | default(none) }}"
      unit_of_measurement: "µg/m³"
      device_class: pm10
      state_class: measurement

    - name: "LIV24 Sound"
      unique_id: "liv24_sound"
      state_topic: "liv24/sensors"
      value_template: "{{ value_json.sound | default(none) }}"
      unit_of_measurement: "dB"
      state_class: measurement

    - name: "LIV24 EC/TDS"
      unique_id: "liv24_ec"
      state_topic: "liv24/sensors"
      value_template: "{{ value_json.ec | default(none) }}"
      unit_of_measurement: "µS/cm"
      state_class: measurement
      icon: mdi:water-percent

    - name: "LIV24 TDS"
      unique_id: "liv24_tds"
      state_topic: "liv24/sensors"
      value_template: >
        {{ (value_json.ec * 0.5) | round(1)
           if value_json.ec is defined else none }}
      unit_of_measurement: "mg/L"
      state_class: measurement
      icon: mdi:water-check

  switch:
    - name: "LIV24 Relay 1"
      unique_id: liv24_relay_1
      command_topic: "liv24/relay/1/set"
      state_topic: "liv24/relay/1/state"
      payload_on: "ON"
      payload_off: "OFF"
      retain: false

    - name: "LIV24 Relay 2"
      unique_id: liv24_relay_2
      command_topic: "liv24/relay/2/set"
      state_topic: "liv24/relay/2/state"
      payload_on: "ON"
      payload_off: "OFF"
      retain: false

  binary_sensor:
    - name: "LIV24 Leak Detector"
      unique_id: "liv24_leak"
      state_topic: "liv24/sensors"
      value_template: >
        {{ 'ON' if value_json.leak | default(false) else 'OFF' }}
      payload_on: "ON"
      payload_off: "OFF"
      device_class: moisture
```

### Statistics Sensors (1-Hour Average)

```yaml
sensor:
  - platform: statistics
    name: "LIV24 Temp 1h"
    entity_id: sensor.liv24_temperature
    state_characteristic: mean
    max_age:
      hours: 1
    sampling_size: 20

  - platform: statistics
    name: "LIV24 Hum 1h"
    entity_id: sensor.liv24_humidity
    state_characteristic: mean
    max_age:
      hours: 1
    sampling_size: 20

  - platform: statistics
    name: "LIV24 PM25 1h"
    entity_id: sensor.liv24_pm2_5
    state_characteristic: mean
    max_age:
      hours: 1
    sampling_size: 20

  - platform: statistics
    name: "LIV24 PM10 1h"
    entity_id: sensor.liv24_pm10
    state_characteristic: mean
    max_age:
      hours: 1
    sampling_size: 20

  - platform: statistics
    name: "LIV24 Sound 1h"
    entity_id: sensor.liv24_sound
    state_characteristic: mean
    max_age:
      hours: 1
    sampling_size: 20
```

### Input Text Helpers (History Storage)

```yaml
input_text:
  liv24_24h_temp:
    max: 255
  liv24_24h_hum:
    max: 255
  liv24_24h_pm25:
    max: 255
  liv24_24h_pm10:
    max: 255
  liv24_24h_sound:
    max: 255
  liv24_7d_temp:
    max: 255
  liv24_7d_hum:
    max: 255
  liv24_7d_pm25:
    max: 255
  liv24_7d_pm10:
    max: 255
  liv24_7d_sound:
    max: 255
```

### Apply Configuration

หลังแก้ไขไฟล์เสร็จ:
1. **Developer Tools** → **Check Configuration** → ต้องผ่านโดยไม่มี Error
2. **Settings** → **System** → **Restart Home Assistant**

---

## 6. Automation Setup / การตั้งค่า Automation

เปิดไฟล์ `/config/automations.yaml` แล้วเพิ่ม Automation ต่อไปนี้

### 6.1 Hourly History Update

บันทึกค่าเฉลี่ย 1 ชั่วโมงทุกต้นชั่วโมง เพื่อสร้าง History 24 ชั่วโมง

```yaml
- alias: LIV24 Hourly History Update
  trigger:
    - platform: time_pattern
      minutes: "0"
  action:
    - variables:
        nt:   "{{ states('sensor.liv24_temp_1h')  | float(0) | round(1) }}"
        nh:   "{{ states('sensor.liv24_hum_1h')   | float(0) | round(1) }}"
        np25: "{{ states('sensor.liv24_pm25_1h')  | float(0) | round(1) }}"
        np10: "{{ states('sensor.liv24_pm10_1h')  | float(0) | round(1) }}"
        nsnd: "{{ states('sensor.liv24_sound_1h') | float(0) | round(1) }}"
        at: >
          {% set s = states('input_text.liv24_24h_temp') %}
          {% set a = s | from_json if s not in ['','unknown','unavailable'] else [] %}
          {{ (a + [nt | float])[-24:] | to_json }}
        ah: >
          {% set s = states('input_text.liv24_24h_hum') %}
          {% set a = s | from_json if s not in ['','unknown','unavailable'] else [] %}
          {{ (a + [nh | float])[-24:] | to_json }}
        ap25: >
          {% set s = states('input_text.liv24_24h_pm25') %}
          {% set a = s | from_json if s not in ['','unknown','unavailable'] else [] %}
          {{ (a + [np25 | float])[-24:] | to_json }}
        ap10: >
          {% set s = states('input_text.liv24_24h_pm10') %}
          {% set a = s | from_json if s not in ['','unknown','unavailable'] else [] %}
          {{ (a + [np10 | float])[-24:] | to_json }}
        asnd: >
          {% set s = states('input_text.liv24_24h_sound') %}
          {% set a = s | from_json if s not in ['','unknown','unavailable'] else [] %}
          {{ (a + [nsnd | float])[-24:] | to_json }}
    - service: input_text.set_value
      target: { entity_id: input_text.liv24_24h_temp }
      data:   { value: "{{ at }}" }
    - service: input_text.set_value
      target: { entity_id: input_text.liv24_24h_hum }
      data:   { value: "{{ ah }}" }
    - service: input_text.set_value
      target: { entity_id: input_text.liv24_24h_pm25 }
      data:   { value: "{{ ap25 }}" }
    - service: input_text.set_value
      target: { entity_id: input_text.liv24_24h_pm10 }
      data:   { value: "{{ ap10 }}" }
    - service: input_text.set_value
      target: { entity_id: input_text.liv24_24h_sound }
      data:   { value: "{{ asnd }}" }
    - service: mqtt.publish
      data:
        topic: "liv24/history/24h"
        retain: true
        payload: >
          {"temp":{{ at }},"hum":{{ ah }},"pm25":{{ ap25 }},"pm10":{{ ap10 }},"sound":{{ asnd }}}
```

### 6.2 Daily History Update

คำนวณค่าเฉลี่ยรายวัน บันทึกสะสม 7 วัน ทำงานทุก 00:01 น.

```yaml
- alias: LIV24 Daily History Update
  trigger:
    - platform: time
      at: "00:01:00"
  action:
    - variables:
        nt: >
          {% set s = states('input_text.liv24_24h_temp') %}
          {% set a = s | from_json if s not in ['','unknown','unavailable'] else [] %}
          {{ ((a | sum) / (a | length)) | round(1) if a | length > 0 else 0.0 }}
        nh: >
          {% set s = states('input_text.liv24_24h_hum') %}
          {% set a = s | from_json if s not in ['','unknown','unavailable'] else [] %}
          {{ ((a | sum) / (a | length)) | round(1) if a | length > 0 else 0.0 }}
        np25: >
          {% set s = states('input_text.liv24_24h_pm25') %}
          {% set a = s | from_json if s not in ['','unknown','unavailable'] else [] %}
          {{ ((a | sum) / (a | length)) | round(1) if a | length > 0 else 0.0 }}
        np10: >
          {% set s = states('input_text.liv24_24h_pm10') %}
          {% set a = s | from_json if s not in ['','unknown','unavailable'] else [] %}
          {{ ((a | sum) / (a | length)) | round(1) if a | length > 0 else 0.0 }}
        nsnd: >
          {% set s = states('input_text.liv24_24h_sound') %}
          {% set a = s | from_json if s not in ['','unknown','unavailable'] else [] %}
          {{ ((a | sum) / (a | length)) | round(1) if a | length > 0 else 0.0 }}
    - variables:
        at: >
          {% set s = states('input_text.liv24_7d_temp') %}
          {% set raw = s | from_json if s not in ['','unknown','unavailable'] else [] %}
          {% set a = raw if raw is sequence and raw is not string else [] %}
          {{ (a + [nt | float])[-7:] | to_json }}
        ah: >
          {% set s = states('input_text.liv24_7d_hum') %}
          {% set raw = s | from_json if s not in ['','unknown','unavailable'] else [] %}
          {% set a = raw if raw is sequence and raw is not string else [] %}
          {{ (a + [nh | float])[-7:] | to_json }}
        ap25: >
          {% set s = states('input_text.liv24_7d_pm25') %}
          {% set raw = s | from_json if s not in ['','unknown','unavailable'] else [] %}
          {% set a = raw if raw is sequence and raw is not string else [] %}
          {{ (a + [np25 | float])[-7:] | to_json }}
        ap10: >
          {% set s = states('input_text.liv24_7d_pm10') %}
          {% set raw = s | from_json if s not in ['','unknown','unavailable'] else [] %}
          {% set a = raw if raw is sequence and raw is not string else [] %}
          {{ (a + [np10 | float])[-7:] | to_json }}
        asnd: >
          {% set s = states('input_text.liv24_7d_sound') %}
          {% set raw = s | from_json if s not in ['','unknown','unavailable'] else [] %}
          {% set a = raw if raw is sequence and raw is not string else [] %}
          {{ (a + [nsnd | float])[-7:] | to_json }}
    - service: input_text.set_value
      target: { entity_id: input_text.liv24_7d_temp }
      data:   { value: "{{ at }}" }
    - service: input_text.set_value
      target: { entity_id: input_text.liv24_7d_hum }
      data:   { value: "{{ ah }}" }
    - service: input_text.set_value
      target: { entity_id: input_text.liv24_7d_pm25 }
      data:   { value: "{{ ap25 }}" }
    - service: input_text.set_value
      target: { entity_id: input_text.liv24_7d_pm10 }
      data:   { value: "{{ ap10 }}" }
    - service: input_text.set_value
      target: { entity_id: input_text.liv24_7d_sound }
      data:   { value: "{{ asnd }}" }
    - service: mqtt.publish
      data:
        topic: "liv24/history/7d"
        retain: true
        payload: >
          {"temp":{{ at }},"hum":{{ ah }},"pm25":{{ ap25 }},"pm10":{{ ap10 }},"sound":{{ asnd }}}
```

### 6.3 Alert Automations

แจ้งเตือนเมื่อค่าเกิน threshold ผ่าน Telegram Bot

```yaml
- alias: "LIV24 High Temperature Alert"
  trigger:
    - platform: numeric_state
      entity_id: sensor.liv24_temperature
      above: 35
      for:
        minutes: 2
  action:
    - service: telegram_bot.send_message
      data:
        chat_id: YOUR_CHAT_ID
        message: "⚠️ LIV24: Temp {{ states('sensor.liv24_temperature') }}°C เกิน 35°C"

- alias: "LIV24 Low Humidity Alert"
  trigger:
    - platform: numeric_state
      entity_id: sensor.liv24_humidity
      below: 30
      for:
        minutes: 2
  action:
    - service: telegram_bot.send_message
      data:
        chat_id: YOUR_CHAT_ID
        message: "⚠️ LIV24: ความชื้น {{ states('sensor.liv24_humidity') }}% ต่ำกว่า 30%"

- alias: "LIV24 High PM2.5 Alert"
  trigger:
    - platform: numeric_state
      entity_id: sensor.liv24_pm2_5
      above: 35.4
      for:
        minutes: 2
  action:
    - service: telegram_bot.send_message
      data:
        chat_id: YOUR_CHAT_ID
        message: "⚠️ LIV24: PM2.5 {{ states('sensor.liv24_pm2_5') }} µg/m³ — คุณภาพอากาศแย่"

- alias: "LIV24 Leak Detected Alert"
  trigger:
    - platform: state
      entity_id: binary_sensor.liv24_leak_detector
      to: "on"
  action:
    - service: telegram_bot.send_message
      data:
        chat_id: YOUR_CHAT_ID
        message: "🚨 LIV24: ตรวจพบน้ำรั่ว! ตรวจสอบทันที"

- alias: "LIV24 High EC Alert"
  trigger:
    - platform: numeric_state
      entity_id: sensor.liv24_ec_tds
      above: 500
      for:
        minutes: 2
  action:
    - service: telegram_bot.send_message
      data:
        chat_id: YOUR_CHAT_ID
        message: "⚠️ LIV24: ค่าน้ำ EC {{ states('sensor.liv24_ec_tds') }} µS/cm — คุณภาพน้ำแย่"
```

> แทนที่ `YOUR_CHAT_ID` ด้วย Telegram Chat ID ของผู้ใช้ (ดูวิธีหาใน Section 7)

### Alert Thresholds Reference

| Sensor | เงื่อนไข | Default Threshold | ปรับได้ใน |
|---|---|---|---|
| Temperature | above | 35°C | automations.yaml |
| Humidity | below | 30% | automations.yaml |
| PM2.5 | above | 35.4 µg/m³ (WHO Moderate) | automations.yaml |
| Leak | state = ON | ทันที | — |
| EC/TDS | above | 500 µS/cm | automations.yaml |

---

## 7. Telegram Bot Setup / การตั้งค่า Telegram Bot

### 7.1 สร้าง Bot

1. เปิด Telegram → ค้นหา **@BotFather** → กด **Start**
2. พิมพ์ `/newbot`
3. ตั้งชื่อ Bot (ชื่อแสดงผล) เช่น `Smart Environment Monitor Alert`
4. ตั้ง Username (ต้องลงท้ายด้วย `bot`) เช่น `liv24alert_bot`
5. บันทึก **Bot Token** ที่ได้รับ เช่น `7123456789:AAF...`

### 7.2 หา Chat ID

1. ค้นหา **@userinfobot** ใน Telegram → กด **Start**
2. Bot จะตอบ `Id: XXXXXXXXX` — นี่คือ Chat ID

### 7.3 เชื่อมต่อกับ Home Assistant

1. **Settings** → **Devices & Services** → **Add Integration**
2. ค้นหา **Telegram Bot** → เลือก
3. กรอก **Bot Token** และ **Chat ID**
4. กด **Submit**

### 7.4 ทดสอบการส่งข้อความ

1. **Developer Tools** → **Actions**
2. ค้นหา `telegram_bot.send_message`
3. กรอก:
```yaml
action: telegram_bot.send_message
data:
  chat_id: YOUR_CHAT_ID
  message: "test จาก HA"
```
4. กด **Perform Action** → ต้องได้รับข้อความใน Telegram

---

## 8. Viewing Sensor Data / การดูข้อมูล Sensor

### Entities ที่มีในระบบ

| Entity ID | ชื่อ | ประเภท |
|---|---|---|
| `sensor.liv24_temperature` | LIV24 Temperature | Sensor (°C) |
| `sensor.liv24_humidity` | LIV24 Humidity | Sensor (%) |
| `sensor.liv24_pm2_5` | LIV24 PM2.5 | Sensor (µg/m³) |
| `sensor.liv24_pm10` | LIV24 PM10 | Sensor (µg/m³) |
| `sensor.liv24_sound` | LIV24 Sound | Sensor (dB) |
| `sensor.liv24_ec_tds` | LIV24 EC/TDS | Sensor (µS/cm) |
| `sensor.liv24_tds` | LIV24 TDS | Sensor (mg/L) |
| `binary_sensor.liv24_leak_detector` | LIV24 Leak Detector | Binary Sensor |
| `switch.liv24_relay_1` | LIV24 Relay 1 | Switch |
| `switch.liv24_relay_2` | LIV24 Relay 2 | Switch |

### ดูค่า Realtime

**Developer Tools** → **States** → ค้นหา `liv24`

### ดู History

**History** → เลือก Entity ที่ต้องการ → เลือกช่วงเวลา

### สร้าง Dashboard Card

#### Entities Card — แสดงค่า Sensor ทั้งหมดในหน้าเดียว

1. HA Dashboard → กดไอคอน **Edit** (ดินสอ มุมบนขวา)
2. กด **+ Add Card** → เลือก **Entities**
3. สลับเป็น **YAML mode** แล้ว paste config นี้:

```yaml
type: entities
title: LIV24 Sensor Status
entities:
  - sensor.liv24_temperature
  - sensor.liv24_humidity
  - sensor.liv24_pm2_5
  - sensor.liv24_pm10
  - sensor.liv24_sound
  - sensor.liv24_ec_tds
  - binary_sensor.liv24_leak_detector
  - switch.liv24_relay_1
  - switch.liv24_relay_2
```

#### Gauge Card — แสดงค่าเป็นหน้าปัด เหมาะสำหรับ PM2.5

```yaml
type: gauge
title: PM2.5
entity: sensor.liv24_pm2_5
min: 0
max: 150
severity:
  green: 0
  yellow: 35
  red: 75
```

> ระดับสี Gauge: เขียว = ปลอดภัย (0–35), เหลือง = ระวัง (35–75), แดง = อันตราย (75+)

#### History Graph Card — แสดงกราฟย้อนหลัง

```yaml
type: history-graph
title: Temperature & Humidity
entities:
  - sensor.liv24_temperature
  - sensor.liv24_humidity
hours_to_show: 24
```

---

## 9. Troubleshooting / การแก้ไขปัญหาเบื้องต้น

### Sensor แสดง "unavailable"

- ตรวจสอบว่า LIV24 เชื่อมต่อ Ethernet และ Power อยู่
- ตรวจสอบ MQTT Broker ว่า Running (**Settings** → **Add-ons** → **Mosquitto**)
- ตรวจสอบ IP ของอุปกรณ์จากหน้าจอ Boot Screen แล้วลอง `ping <IP>` จาก PC

### Automation ไม่ทำงาน

- **Settings** → **Automations** → ตรวจสอบว่า Automation เป็น Active
- ตรวจสอบ syntax ของ YAML ใน `automations.yaml` ผ่าน **Developer Tools** → **Check Configuration**

### Telegram ส่งข้อความไม่ได้

- ตรวจสอบว่ากด **Start** กับ Bot ใน Telegram แล้ว
- ตรวจสอบ Chat ID ถูกต้อง
- ลอง Test ผ่าน Developer Tools → Actions

### Check Configuration Error

หลังแก้ไข YAML ให้ทำ **Developer Tools** → **Check Configuration** ก่อน Restart เสมอ เพื่อป้องกัน HA ไม่ Start ขึ้น

### Sensor ขึ้น "unknown" (ไม่ใช่ "unavailable")

ความแตกต่าง:
- `unavailable` = HA ไม่ได้รับ MQTT message เลย (Device ออฟไลน์หรือ Broker หยุด)
- `unknown` = รับ message แล้ว แต่ parse JSON ไม่ได้

สาเหตุที่พบบ่อย: key ใน JSON payload ไม่ตรงกับ `value_json.xxx` ใน `value_template`

วิธีตรวจ:
1. **Settings** → **Devices & Services** → **MQTT** → **Configure**
2. กด **Listen to a topic** → พิมพ์ `liv24/sensors` → **Start Listening**
3. ดู Payload ที่ได้รับ เปรียบเทียบ key กับที่กำหนดใน `configuration.yaml`

### Automation ทำงานแล้วแต่ History ไม่อัพเดทบนหน้าจออุปกรณ์

สาเหตุ: MQTT publish ใน Automation ขาด `retain: true`

เมื่อ ESP32 reboot จะ subscribe topic `liv24/history/24h` ใหม่ — ถ้าไม่มี `retain` Broker จะไม่ส่งข้อมูลล่าสุดซ้ำให้ ผลคือหน้าจอแสดงว่างเปล่า

ตรวจสอบ: ใน `automations.yaml` Section 6.1 และ 6.2 ให้มี `retain: true` ใน `mqtt.publish` ทุก action

### 24h Strip ว่างเปล่าหลัง Setup เสร็จ

เป็นเรื่องปกติ — Automation ทำงานทุก :00 นาที ต้องรอ 1+ ชั่วโมงเพื่อให้มีค่าแรก

ถ้าต้องการทดสอบทันที ให้ Trigger ด้วยมือ (ดูหัวข้อถัดไป)

### วิธี Trigger Automation ด้วยมือ (สำหรับทดสอบ)

1. **Settings** → **Automations & Scenes**
2. ค้นหา `LIV24 Hourly History Update`
3. กดจุดสามจุด **(⋮)** → **Run**
4. ตรวจสอบผลลัพธ์: **Developer Tools** → **States** → ค้นหา `input_text.liv24_24h_temp`
   - ถ้าสำเร็จจะมีค่าเป็น JSON array เช่น `[28.5]`
   - ถ้ายังว่างหรือ `unknown` ให้ตรวจสอบ YAML syntax ใน `automations.yaml` อีกครั้ง

---

## 10. Appendix: Full YAML Reference / ภาคผนวก

### MQTT Topics Reference

| Topic | Direction | Payload Format | หมายเหตุ |
|---|---|---|---|
| `liv24/sensors` | LIV24 → HA | JSON | Sensor data ทั้งหมด |
| `liv24/relay/1/set` | HA → LIV24 | `ON` / `OFF` | ควบคุม Relay 1 |
| `liv24/relay/1/state` | LIV24 → HA | `ON` / `OFF` | สถานะ Relay 1 |
| `liv24/relay/2/set` | HA → LIV24 | `ON` / `OFF` | ควบคุม Relay 2 |
| `liv24/relay/2/state` | LIV24 → HA | `ON` / `OFF` | สถานะ Relay 2 |
| `liv24/history/24h` | HA → LIV24 | JSON Object | HA publish → Device รับเพื่อแสดง 24h strip |
| `liv24/history/7d` | HA → LIV24 | JSON Object | HA publish → Device รับเพื่อแสดง 7D chart |
| `liv24/test/alert` | HA → LIV24 | JSON | ส่งค่า sensor จำลองเพื่อทดสอบ Alert Banner บนหน้าจอ |

### Payload Examples

**liv24/sensors — PM Sensor (SN-300BYH-M)**
```json
{"temp":28.5,"hum":65.2,"pm25":30.5,"pm10":50.4,"sound":63.8}
```

**liv24/sensors — EC Sensor (CWT-EC/TDS)**
```json
{"ec":148.0}
```

**liv24/sensors — Leak Detector (LD100)**
```json
{"leak":false}
```

**liv24/history/24h**
```json
{"temp":[28.5,29.1,...],"hum":[65.2,64.8,...],"pm25":[30.5,31.2,...],"pm10":[50.4,51.0,...],"sound":[63.8,64.2,...]}
```

---

*Smart Environment Monitor by LIV24 — Home Assistant Integration Manual v1.0*
*Developed by: Kuda Visavaplanont (Intern) — Sansiri Public Company Limited*
