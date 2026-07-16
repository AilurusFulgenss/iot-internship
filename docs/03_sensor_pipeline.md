# `sensor_config.cpp` + `calib.cpp` — เลือก Sensor และปรับค่าให้ถูกต้อง

ไฟล์ทั้งสองนี้ทำงานร่วมกันในขั้นตอนเดียวกัน: **รู้ว่าจะคุยกับ sensor ตัวไหน** และ **ปรับค่าที่อ่านได้ให้ถูกต้อง** ก่อนส่งขึ้นจอและ MQTT

---

## ภาพรวม: sensor เดินทางตั้งแต่ตั้งค่าจนแสดงผล

```
DEV screen (เจ้าหน้าที่เลือก)
    │
    ↓ sensor_config_set(model, slave_id)
    │  บันทึกลง NVS flash (ไม่หายเมื่อปิดเครื่อง)
    │
boot ครั้งต่อไป
    │
    ↓ sensor_config_init()   ← โหลด model + slave_id จาก NVS
    ↓ calib_init()           ← โหลด offset/gain ของทุก sensor จาก NVS
    │
ทุก 2 วินาที (sensor_read_task)
    │
    ↓ sensor_config_get()    ← ดึง config ที่โหลดไว้
    ↓ modbus_read(slave_id, fc, reg_start, reg_count)
    ↓ raw register values: เช่น [253, 312, 0, 185, 192, 48]
    ↓ calib_apply(raw, &g_calib.temp)   ← ปรับค่า
    ↓ ค่าที่ถูกต้อง → จอ + MQTT
```

---

## ไฟล์ที่ 1: sensor_config — รู้จัก sensor แต่ละชนิด

### SENSOR_MODELS[] (sensor_config.cpp บรรทัด 11–89)

ทะเบียน sensor ทั้ง 5 รุ่นที่รองรับ — เก็บเป็น array คงที่ใน flash (ไม่เปลี่ยนระหว่าง runtime)

```c
const sensor_model_t SENSOR_MODELS[5] = {
    { "SN-300BYH-M", SENSOR_TYPE_PM,  reg_start=0x0000, count=6, fc=0x03,
      idx_temp=1, idx_hum=0, idx_pm10=3, idx_pm25=4, idx_sound=5,
      scale=10.0f },
    { "CWT-EC/TDS",  SENSOR_TYPE_EC,  reg_start=0x0001, count=1, fc=0x03,
      idx_ec=0, scale=1.0f },
    { "LD100-LEAK",  SENSOR_TYPE_LEAK, ... },
    { "CWT-TH04S",  SENSOR_TYPE_TH,   ... },
    { "BH-485-ORP", SENSOR_TYPE_ORP,  ... },
};
```

**ทำไมต้องมี array นี้?**

sensor แต่ละรุ่นเก็บข้อมูลคนละ register คนละตำแหน่ง เช่น:
- SN-300: อุณหภูมิอยู่ที่ register index 1 ค่า 253 = 25.3°C (หาร 10)
- CWT-TH04S: อุณหภูมิอยู่ที่ register index 1 เช่นกัน แต่อาจเป็นค่า signed (ติดลบได้)
- CWT-EC: ไม่มีอุณหภูมิเลย → `idx_temp = -1`

แทนที่จะเขียน `if model == "SN300" { อ่าน reg[1] } else if ...` ซ้ำทุกที่ เราเก็บ "แผนที่ register" ไว้ใน struct แล้วโค้ดอ่านผ่าน index เดียวกันทุกรุ่น

**`idx_temp = -1` หมายความว่าอะไร?**

sensor รุ่นนั้นไม่มีค่านี้ — ใน sensor_read_task ตรวจก่อนใช้เสมอ:
```c
float temp = (m->idx_temp >= 0) ? regs[m->idx_temp] / m->scale : NAN;
//            ถ้า idx ไม่ใช่ -1    อ่านค่านั้น              ไม่มีก็ใส่ NAN
```

`NAN` (Not a Number) คือสัญลักษณ์ว่า "ไม่มีค่านี้" — จอจะแสดง `--` แทนเลข 0 ซึ่งจะทำให้เข้าใจผิด

### sensor_model_t struct (sensor_config.h บรรทัด 19–34)

```c
typedef struct {
    const char    *name;       // "SN-300BYH-M" — ชื่อที่โชว์ใน DEV screen
    sensor_type_t  type;       // PM / EC / LEAK / TH / ORP
    uint16_t       reg_start;  // register แรกที่อ่าน
    uint8_t        reg_count;  // จำนวน register ที่อ่านต่อครั้ง
    uint8_t        fc;         // Function Code: 0x03 = read holding, 0x04 = read input
    int8_t         idx_temp;   // register ไหนคืออุณหภูมิ (-1 ถ้าไม่มี)
    int8_t         idx_hum;    // register ไหนคือความชื้น (-1 ถ้าไม่มี)
    // ... idx อื่นๆ
    float          scale;      // ตัวหาร เช่น 10.0 แปลง 253 → 25.3
} sensor_model_t;
```

ในภาษา Swift เทียบได้กับ:
```swift
struct SensorModel {
    let name: String
    let type: SensorType
    let regStart: UInt16
    // ...
    let idxTemp: Int8      // -1 = ไม่มี
    let scale: Float
}
```

### sensor_config_t — config ที่เจ้าหน้าที่ตั้ง (sensor_config.h บรรทัด 41–44)

```c
typedef struct {
    uint8_t model_idx;   // เลือก sensor รุ่นไหน (0–4)
    uint8_t slave_id;    // หมายเลขของ sensor บนสาย RS485 (1–247)
} sensor_config_t;
```

แค่ 2 ค่าเท่านั้น — แต่ข้อมูลนี้สำคัญมาก เพราะถ้าผิดค่าใดค่าหนึ่ง sensor_read_task จะอ่านค่าไม่ได้เลย

### NVS — บันทึกค่าที่ตั้งไม่ให้หาย (sensor_config.cpp บรรทัด 96–155)

**NVS คืออะไร?**

NVS (Non-Volatile Storage) = พื้นที่เก็บข้อมูลใน flash ของชิป — ข้อมูลไม่หายเมื่อปิดไฟ เหมือน `UserDefaults` ใน iOS หรือ `SharedPreferences` ใน Android

**โหลดตอน boot:**
```c
void sensor_config_init(void)
{
    nvs_flash_init();   // เปิด NVS storage

    nvs_handle_t h;
    nvs_open("sensor_cfg", NVS_READONLY, &h);   // เปิด "namespace" ชื่อ sensor_cfg
    nvs_get_u8(h, "model", &s_cfg.model_idx);   // อ่านค่า key "model"
    nvs_get_u8(h, "slave", &s_cfg.slave_id);    // อ่านค่า key "slave"
    nvs_close(h);
}
```

**บันทึกเมื่อเจ้าหน้าที่เปลี่ยนค่าใน DEV screen:**
```c
void sensor_config_set(uint8_t model_idx, uint8_t slave_id)
{
    s_cfg.model_idx = model_idx;
    s_cfg.slave_id  = slave_id;

    nvs_handle_t h;
    nvs_open("sensor_cfg", NVS_READWRITE, &h);
    nvs_set_u8(h, "model", model_idx);
    nvs_set_u8(h, "slave", slave_id);
    nvs_commit(h);   // ← สำคัญ: commit = เขียนลง flash จริงๆ ถ้าไม่ commit ค่าหาย
    nvs_close(h);
}
```

**ค่า default ถ้าไม่เคยตั้ง:**
```c
static sensor_config_t s_cfg = { 0, 1 };
//                               ↑  ↑
//                       model SN-300  slave_id=1
```

### sensor_store_raw / sensor_get_raw (บรรทัด 124–135)

```c
void sensor_store_raw(const uint16_t *regs, uint8_t count)
{
    memcpy(s_last_raw.regs, regs, count * sizeof(uint16_t));
    s_last_raw.valid = true;
}
```

หลังจาก modbus_read สำเร็จ บรรทัดดิบจาก sensor จะถูก snapshot ไว้ก่อนแปลงค่า — DEV screen ใช้ดึงข้อมูลนี้เพื่อแสดงเลข raw ให้เจ้าหน้าที่เทียบมือ ไม่ต้องใช้ oscilloscope

---

## ไฟล์ที่ 2: calib — ปรับค่าให้ถูกต้อง

### ปัญหาที่ calib แก้

sensor อุตสาหกรรมแบบ RS485 ไม่ได้แม่นยำ 100% ตัวอย่างจริง:
- SN-300 อ่านอุณหภูมิได้ 27.2°C แต่เทียบกับเทอร์โมมิเตอร์มาตรฐานแสดง 26.8°C
- PM2.5 อ่านได้ 45 µg/m³ แต่เทียบกับเครื่องมาตรฐานได้ 42 µg/m³

ต้องมีระบบ "ปรับศูนย์" ให้ตรงกับมาตรฐาน — นั่นคือหน้าที่ของ calib

### สูตรการปรับ (calib.h บรรทัด 33–36)

```c
static inline float calib_apply(float raw, const sensor_calib_t *c)
{
    return (raw + c->offset) * c->gain;
}
```

สูตร: `ค่าที่แก้แล้ว = (ค่าดิบ + offset) × gain`

ตัวอย่างจริง:
- ค่าดิบจาก sensor = 27.2°C
- เจ้าหน้าที่ตั้ง offset = -0.4, gain = 1.0
- ผลลัพธ์ = (27.2 + (-0.4)) × 1.0 = **26.8°C** ✓

ตัวอย่างที่ต้องปรับทั้ง offset และ gain:
- ค่าดิบ PM2.5 = 45 µg/m³
- offset = -3.0, gain = 1.0
- ผลลัพธ์ = (45 + (-3)) × 1.0 = **42 µg/m³** ✓

ถ้าไม่ต้องปรับ: offset = 0.0, gain = 1.0 → `(raw + 0) × 1 = raw` ค่าไม่เปลี่ยน

### ค่า default (calib.cpp บรรทัด 11–22)

```c
calib_data_t g_calib = {
    {0.0f, 1.0f},   // temp   → offset=0, gain=1 (ไม่ปรับ)
    {0.0f, 1.0f},   // hum
    {0.0f, 1.0f},   // sound
    {0.0f, 1.0f},   // pm25
    {0.0f, 1.0f},   // pm10
    ...
};
```

`g_calib` เป็น global variable — ทุกไฟล์ที่ include `calib.h` เข้าถึงได้โดยตรง ไม่ต้อง pass ผ่าน function argument

### บันทึกและโหลดผ่าน NVS (calib.cpp บรรทัด 24–59)

pattern เดียวกับ sensor_config แต่เก็บ struct ทั้งก้อนแทนที่จะเก็บทีละค่า:

```c
// บันทึก: เขียน struct ทั้งก้อนลง key "v2"
nvs_set_blob(h, "v2", &g_calib, sizeof(g_calib));

// โหลด: อ่าน struct ทั้งก้อนกลับมา
size_t sz = sizeof(g_calib);
nvs_get_blob(h, "v2", &g_calib, &sz);
```

`blob` = binary large object — เก็บ bytes ดิบๆ โดยไม่ต้องสนใจ format เหมาะสำหรับ struct ที่มีหลาย field

**ทำไม key ชื่อ "v2"?** เมื่อก่อนเคยใช้ key "v1" ที่มี struct format ต่างกัน พอเปลี่ยน struct แล้วอัปเดต firmware บน device ที่มี "v1" อยู่แล้ว จะโหลดข้อมูล format เก่าเข้า struct ใหม่ → ค่า calib ผิดหมด เปลี่ยนชื่อ key เป็น "v2" ทำให้ "v1" ถูกมองข้าม → ใช้ default แทน ปลอดภัย

### calib_init (บรรทัด 50–59)

```c
void calib_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();   // พื้นที่ NVS เสีย → ลบแล้วสร้างใหม่
        nvs_flash_init();
    }
    calib_load();   // โหลดค่าที่บันทึกไว้
}
```

`ESP_ERR_NVS_NO_FREE_PAGES` — พื้นที่ NVS เต็มหรือ corrupt → ลบทิ้งแล้วเริ่มใหม่ ค่า calib จะกลับเป็น default แต่ดีกว่าบอร์ดบูตไม่ขึ้น

---

## วิธีที่สองไฟล์นี้ทำงานร่วมกันทุก 2 วินาที

ใน `sensor_read_task` (main.cpp):

```c
sensor_config_t       cfg = sensor_config_get();       // ดึง model + slave_id
const sensor_model_t *m   = &SENSOR_MODELS[cfg.model_idx]; // ดึง "แผนที่ register"

modbus_read(cfg.slave_id, m->fc, m->reg_start, m->reg_count, regs);
// regs[] = [0x0138, 0x013C, ...] = [312, 316, ...] raw values

sensor_store_raw(regs, m->reg_count);   // snapshot ไว้ให้ DEV screen ดู

// แปลงเป็น float
float temp = (m->idx_temp >= 0) ? (int16_t)regs[m->idx_temp] / m->scale : NAN;
//                                  ↑ cast เป็น signed เพราะอุณหภูมิติดลบได้
//           = (int16_t)316 / 10.0 = 31.6°C

// ปรับด้วย calibration
float t_cal = calib_apply(temp, &g_calib.temp);
//           = (31.6 + 0.0) * 1.0 = 31.6 (ถ้ายังไม่ตั้ง calib)

// ส่งขึ้นจอ + MQTT
ui_user_update(t_cal, ...);
wifi_mqtt_publish_sensors(t_cal, ...);
```

---

## สรุปสั้น: ทำไมต้องแยกเป็น 2 ไฟล์

| ไฟล์ | คำถามที่ตอบ | เปลี่ยนได้เมื่อไหร่ |
|---|---|---|
| `sensor_config` | **ใช้ sensor รุ่นไหน** บน slave address อะไร | เจ้าหน้าที่เปลี่ยนใน DEV screen |
| `calib` | **ค่าที่ได้ถูกต้องไหม** ต้องปรับแค่ไหน | เจ้าหน้าที่เปลี่ยนใน DEV screen |

ทั้งสองค่าบันทึกใน NVS — เปลี่ยนครั้งเดียว ใช้ได้ทุก boot ไม่ต้อง flash firmware ใหม่

ไฟล์ถัดไป: `04_wifi_mqtt.md` — วิธีที่ ESP32 คุยกับ RPi และส่งค่า sensor ไป Home Assistant
