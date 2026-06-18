# SN-300BYH-M Environmental Sensor — User Manual

**Sensor:** SN-300BYH-M Multi-parameter Environmental Monitor  
**Measures:** Temperature · Humidity · PM2.5 · PM10 · Sound Level  
**Interface:** RS485 Modbus RTU  
**Prepared by:** IoT Internship — VOLTIVA / Sansiri PCL  

---

## 1. Overview

The SN-300BYH-M is a 5-in-1 indoor air quality sensor that measures particulate matter (PM2.5, PM10), temperature, relative humidity, and ambient sound level via a single RS485 bus using Modbus RTU protocol.

---

## 2. Technical Specifications

| Parameter | Value |
|---|---|
| Power supply | 10–30 V DC |
| Communication | RS485, Modbus RTU |
| Default baud rate | 9600 bps |
| Default slave address | 0x01 |
| Data bits / Parity / Stop | 8 / None / 1 |
| Operating temperature | −20 °C to +60 °C |
| Operating humidity | 0–95 % RH (non-condensing) |

### 2.1 Measurement Range & Resolution

| Sensor | Range | Resolution | Accuracy |
|---|---|---|---|
| Temperature | −20 to +60 °C | 0.1 °C | ±0.5 °C |
| Humidity | 0–100 % RH | 0.1 % | ±3 % RH |
| PM2.5 | 0–999 µg/m³ | 0.1 µg/m³ | ±10 % |
| PM10 | 0–999 µg/m³ | 0.1 µg/m³ | ±10 % |
| Sound | 30–130 dB | 1 dB | ±1.5 dB |

---

## 3. Wiring / Hardware Connection

### RS485 Pinout

| Sensor Wire | Color (typical) | Connect to |
|---|---|---|
| Power (+) | Red | 12–24 V DC |
| Power (−) | Black | GND |
| RS485 A+ | Yellow | RS485 A+ on host |
| RS485 B− | Blue | RS485 B− on host |

### ESP32-P4 Connection (this project)

| Signal | ESP32-P4 GPIO |
|---|---|
| RS485 TX (to A+) | GPIO 47 |
| RS485 RX (from B−) | GPIO 48 |

> **Note:** Use a MAX485 or equivalent RS485 transceiver IC between ESP32 (3.3 V TTL) and the sensor (RS485 differential signal).

---

## 4. Modbus RTU Communication

### 4.1 Request Frame (Function Code 0x03 — Read Holding Registers)

| Byte | Field | Example |
|---|---|---|
| 0 | Slave address | `0x01` |
| 1 | Function code | `0x03` |
| 2–3 | Start register (big-endian) | `0x00 0x00` |
| 4–5 | Register count | `0x00 0x06` |
| 6–7 | CRC16 (little-endian) | calculated |

### 4.2 Register Map

| Register | Parameter | Unit | Scale |
|---|---|---|---|
| 0x0000 | Humidity | % RH | ÷ 10 |
| 0x0001 | Temperature | °C | ÷ 10 |
| 0x0002 | (reserved) | — | — |
| 0x0003 | PM10 | µg/m³ | ÷ 10 |
| 0x0004 | PM2.5 | µg/m³ | ÷ 10 |
| 0x0005 | Sound | dB | raw (no scale) |

**Example:** Register 0x0001 returns `0x00F5` = 245 → Temperature = 245 ÷ 10 = **24.5 °C**

### 4.3 CRC16 (Modbus)

```
CRC = 0xFFFF
for each byte:
    CRC ^= byte
    for 8 bits:
        if LSB == 1: CRC = (CRC >> 1) XOR 0xA001
        else:        CRC = CRC >> 1
```

---

## 5. International Standards & Thresholds

### 5.1 PM2.5 — US EPA Air Quality Index (AQI)

| AQI Category | PM2.5 (24h avg µg/m³) | Color | Health Guidance |
|---|---|---|---|
| Good | 0.0 – 12.0 | Green | Air quality satisfactory |
| Moderate | 12.1 – 35.4 | Yellow | Acceptable; sensitive persons may be affected |
| Unhealthy for Sensitive Groups | 35.5 – 55.4 | Orange | Sensitive groups should reduce outdoor activity |
| Unhealthy | 55.5 – 150.4 | Red | Everyone may begin to feel health effects |
| Very Unhealthy | 150.5 – 250.4 | Purple | Health alert: serious effects for everyone |
| Hazardous | > 250.5 | Maroon | Emergency conditions |

**WHO 2021 Guideline:** Annual mean ≤ 5 µg/m³ · 24h mean ≤ 15 µg/m³  
**Thai NAAQS:** Annual ≤ 25 µg/m³ · 24h ≤ 50 µg/m³ *(Pollution Control Department)*

### 5.2 PM10 — US EPA AQI

| AQI Category | PM10 (24h avg µg/m³) | Color |
|---|---|---|
| Good | 0 – 54 | Green |
| Moderate | 55 – 154 | Yellow |
| Unhealthy for Sensitive Groups | 155 – 254 | Orange |
| Unhealthy | 255 – 354 | Red |
| Very Unhealthy | 355 – 424 | Purple |
| Hazardous | > 425 | Maroon |

**WHO 2021 Guideline:** Annual mean ≤ 15 µg/m³ · 24h mean ≤ 45 µg/m³  
**Thai NAAQS:** Annual ≤ 50 µg/m³ · 24h ≤ 120 µg/m³

### 5.3 Temperature — Thermal Comfort

| Range (°C) | Comfort Level | Standard |
|---|---|---|
| < 18 | Cold | ISO 7730 |
| 18 – 21 | Cool | ISO 7730 |
| 22 – 26 | Comfortable ✓ | ASHRAE 55 / ISO 7730 |
| 27 – 29 | Warm | ASHRAE 55 |
| 30 – 34 | Hot | — |
| ≥ 35 | Very Hot / Alert | — |

**ASHRAE Standard 55:** Comfort zone 20–26 °C at 50 % RH for sedentary activity  
**ISO 7730:** Operative temperature 22–26 °C (summer), 20–24 °C (winter)

### 5.4 Humidity — Relative Humidity Comfort

| Range (% RH) | Condition | Standard |
|---|---|---|
| < 30 | Too dry — skin/eye irritation | ASHRAE 55 |
| 30 – 60 | Comfortable ✓ | ASHRAE 55 / WHO |
| 61 – 75 | Humid | ASHRAE 55 |
| > 75 | Too humid — mold risk | WHO |

**ASHRAE Standard 55:** Acceptable RH range 30–60 %  
**WHO:** Recommends indoor RH 40–70 % for respiratory health

### 5.5 Sound Level — Noise Exposure

| Level (dB) | Environment | Standard |
|---|---|---|
| < 35 | Very quiet (library) | — |
| 35 – 50 | Quiet office / residential | WHO Environmental Noise |
| 51 – 65 | Normal conversation | — |
| 66 – 70 | Elevated — sustained exposure undesirable | WHO |
| 71 – 85 | Loud — hearing protection advised for long exposure | ISO 9612 |
| > 85 | Hazardous — legal limit for 8h work shift | Thai Labor Law (กฎกระทรวง ฉ.2 พ.ศ. 2563) |

**WHO Environmental Noise Guidelines (2018):** Indoor residential < 35 dB(A) for sleep, < 45 dB(A) daytime  
**Thai Labor Law (กฎกระทรวงกำหนดมาตรฐาน ฉบับที่ 2 พ.ศ. 2563):** 85 dB(A) TWA 8h limit; 140 dB peak  
**ISO 9612:2009:** Measurement of occupational noise exposure

---

## 6. Alert Thresholds Used in This System

The LIV-24 IoT Node uses the following alert thresholds, derived from the standards above:

| Parameter | Alert Condition | Severity | Basis |
|---|---|---|---|
| Temperature | > 35 °C | Orange | Exceeds ASHRAE 55 comfort upper limit |
| Humidity | < 30 % RH | Yellow | Below ASHRAE 55 lower limit |
| PM2.5 | > 35.4 µg/m³ | Orange | US EPA AQI "Unhealthy for Sensitive Groups" |
| PM2.5 | > 150.4 µg/m³ | Red | US EPA AQI "Unhealthy" |
| PM10 | > 254 µg/m³ | Orange | US EPA AQI "Unhealthy for Sensitive Groups" |

---

## 7. Reading Data — Code Example (ESP32 / ESP-IDF)

```c
// Request 6 registers starting at 0x0000
uint8_t req[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x06, 0xC4, 0x0B};
uart_write_bytes(UART_NUM_1, req, sizeof(req));

uint8_t resp[17];
uart_read_bytes(UART_NUM_1, resp, sizeof(resp), pdMS_TO_TICKS(300));

float humidity    = ((resp[3]  << 8) | resp[4])  / 10.0f;  // %
float temperature = ((resp[5]  << 8) | resp[6])  / 10.0f;  // °C
float pm10        = ((resp[9]  << 8) | resp[10]) / 10.0f;  // µg/m³
float pm25        = ((resp[11] << 8) | resp[12]) / 10.0f;  // µg/m³
uint16_t sound    = ((resp[13] << 8) | resp[14]);           // dB
```

---

## 8. References

| Standard | Description |
|---|---|
| US EPA AQI | [airnow.gov](https://www.airnow.gov/aqi/aqi-basics/) |
| WHO Air Quality Guidelines 2021 | World Health Organization Global Air Quality Guidelines |
| WHO Environmental Noise Guidelines 2018 | WHO Regional Office for Europe |
| ASHRAE Standard 55-2020 | Thermal Environmental Conditions for Human Occupancy |
| ISO 7730:2005 | Ergonomics — Analytical determination of thermal comfort |
| ISO 9612:2009 | Acoustics — Determination of occupational noise exposure |
| Thai NAAQS | Pollution Control Department, Ministry of Natural Resources and Environment |
| Thai Labor Law | กฎกระทรวงกำหนดมาตรฐานในการบริหาร จัดการ และดำเนินการด้านความปลอดภัย ฉบับที่ 2 พ.ศ. 2563 |
