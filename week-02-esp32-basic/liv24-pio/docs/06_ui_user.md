# `ui_user.cpp` — หน้าจอหลักแสดงค่า sensor แบบ real-time

ไฟล์นี้สร้างและอัปเดต **หน้า USER** ซึ่งเป็นหน้าจอที่ผู้ใช้จริงเห็นทุกวัน มี 6 tab ให้สลับดูค่าจาก sensor แต่ละชนิด และอัปเดตค่าทุก 2 วินาทีอัตโนมัติ

---

## ภาพรวม: หน้านี้ทำอะไร

```
┌─────────────────────────────────────┐
│  Header: LIV-24  [โลโก้]  AQI ⬤   │  ← 72px สูง
├──────┬──────┬──────┬──────┬──────┬─┤
│PM2.5 │ HHCC │EC/TDS│ ORP  │ LEAK │TH│  ← Nav bar 6 tabs 56px
├─────────────────────────────────────┤
│                                     │
│     Content area (แสดงทีละ tab)    │  ← 592px สูง
│     สลับได้โดยแตะที่ tab           │
└─────────────────────────────────────┘
```

หน้าจอ 720×800 px แบ่งเป็น 3 ชั้นแนวตั้ง:
- **Header** (72px) — ชื่ออุปกรณ์ + โลโก้ + toggle AQI
- **Nav bar** (56px) — 6 ปุ่ม tab พร้อมค่าย่อ
- **Content** (672px) — พื้นที่แสดงค่าหลัก (ซ่อน/แสดงทีละ tab)

---

## ส่วนที่ 1: ทำไมต้องมี 6 tab?

ระบบนี้รองรับ sensor 5 โมเดล (จาก `SENSOR_MODELS[]` ที่อ่านไปใน 03):

| โมเดล | sensor | tab หลัก |
|---|---|---|
| SN-300BYH-M | อากาศ: temp, hum, PM2.5, PM10, sound | PM2.5 |
| CWT-EC/TDS | น้ำ: ค่าการนำไฟฟ้า | EC/TDS |
| LD100-LEAK | น้ำรั่ว: alarm/normal | LEAK |
| CWT-TH04S | อุณหภูมิ+ความชื้น (separate) | TH |
| BH-485-ORP | น้ำ: ค่าออกซิเดชัน | ORP |

HHCC คือ Bluetooth Plant Sensor (ไม่ใช่ RS485) — มาจาก MQTT ที่ RPi ส่งมา ไม่ได้อ่านโดยตรง

**Tab ทั้ง 6 แสดงในหน้าเดียวกันตลอด** แต่ content area ซ่อน 5 tab และแสดง 1 tab ตามที่เลือก ไม่ได้เป็น "หน้าคนละหน้า" — เปรียบเหมือน `TabView` ใน SwiftUI

---

## ส่วนที่ 2: AQI Color System (บรรทัด 7–12, 94–165)

### มาตรฐานสีที่ใช้

```c
#define CLR_GOOD       0x009966u  // เขียว    PM2.5 ≤ 12
#define CLR_MODERATE   0xFFDE33u  // เหลือง   PM2.5 ≤ 35.4
#define CLR_SENSITIVE  0xFF9933u  // ส้ม      PM2.5 ≤ 55.4
#define CLR_UNHEALTHY  0xCC0033u  // แดง      PM2.5 ≤ 150.4
#define CLR_VERY_UH    0x660099u  // ม่วง     PM2.5 ≤ 250.4
#define CLR_HAZARDOUS  0x7E0023u  // น้ำตาลแดง PM2.5 > 250.4
```

นี่คือมาตรฐาน AQI ของ US EPA ที่ใช้ทั่วโลก ค่าตัดทุกอันตรงตามมาตรฐานจริง

### ฟังก์ชันกำหนดสี

```c
static uint32_t aqi_color_pm25(float v)
{
    if (v <= 12.0f)  return CLR_GOOD;
    if (v <= 35.4f)  return CLR_MODERATE;
    if (v <= 55.4f)  return CLR_SENSITIVE;
    if (v <= 150.4f) return CLR_UNHEALTHY;
    if (v <= 250.4f) return CLR_VERY_UH;
    return CLR_HAZARDOUS;      // ถ้าเกินทุก threshold ตกมาที่นี่
}
```

Pattern นี้เรียกว่า "early return cascade" — แต่ละ `if` คือด่าน ถ้าผ่านค่าใดก็คืนสีทันทีโดยไม่ต้องเช็คต่อ

PM10 ใช้ค่าตัดคนละชุด (เกณฑ์ PM10 สูงกว่าเพราะอนุภาคใหญ่กว่า):

```c
static uint32_t aqi_color_pm10(float v)
{
    if (v <= 54.0f)  return CLR_GOOD;      // ← threshold ต่างจาก PM2.5
    if (v <= 154.0f) return CLR_MODERATE;
    ...
}
```

### ทำไมต้องตรวจสีตัวหนังสือด้วย?

```c
static uint32_t text_on_bg(uint32_t bg)
{
    return (bg == CLR_GOOD || bg == CLR_MODERATE) ? 0x111111u : 0xFFFFFFu;
}
```

- GOOD (เขียว) และ MODERATE (เหลือง) เป็นสีอ่อน → ตัวหนังสือต้องดำ (`0x111111`)
- สีอื่น (ส้ม แดง ม่วง) เป็นสีเข้ม → ตัวหนังสือต้องขาว (`0xFFFFFF`)

ถ้าไม่มีตรงนี้: ข้อความสีขาวบนพื้นเหลืองจะอ่านไม่ออก

### apply_pm_colors — นำสีไปใส่จริง (บรรทัด 139–165)

```c
static void apply_pm_colors(void)
{
    if (!card_pm25 || !card_pm10) return;   // guard: ถ้า widget ยังไม่ถูกสร้าง → ข้าม

    if (g_color_on) {
        uint32_t c25 = aqi_color_pm25(g_pm25_last);  // คำนวณสีจากค่าล่าสุด
        uint32_t c10 = aqi_color_pm10(g_pm10_last);

        // เปลี่ยนสีพื้นหลัง card
        lv_obj_set_style_bg_color(card_pm25, lv_color_hex(c25), 0);
        lv_obj_set_style_bg_color(card_pm10, lv_color_hex(c10), 0);

        // เปลี่ยนสีตัวเลขให้อ่านออก
        lv_obj_set_style_text_color(lbl_pm25, lv_color_hex(text_on_bg(c25)), 0);

        // แสดง label เช่น "UNHEALTHY"
        if (lbl_pm25_st) lv_label_set_text(lbl_pm25_st, aqi_label_pm25(g_pm25_last));

    } else {
        // โหมด off: card สีเทาเข้มทั้งหมด ไม่มีสีเตือน
        lv_obj_set_style_bg_color(card_pm25, lv_color_hex(CLR_CARD), 0);
        ...
        if (lbl_pm25_st) lv_label_set_text(lbl_pm25_st, "");   // ลบ label status
    }
}
```

`g_pm25_last` เป็น global ที่เก็บค่า PM2.5 ล่าสุดไว้ — ทุกครั้งที่ toggle AQI switch สามารถ re-apply สีได้โดยไม่ต้องรอค่าใหม่จาก sensor

---

## ส่วนที่ 3: Helper สร้าง UI (บรรทัด 225–348)

เพื่อไม่ให้เขียนโค้ดซ้ำ 6 card ต่อ tab มีฟังก์ชัน factory ช่วย:

### make_card — สร้าง card มาตรฐาน

```c
typedef struct {
    lv_obj_t *card;       // ตัว card ทั้งก้อน
    lv_obj_t *lbl_val;    // label ตัวเลขใหญ่
    lv_obj_t *lbl_status; // label "GOOD" / "UNHEALTHY" (optional)
    lv_obj_t *lbl_title;  // label ชื่อ เช่น "PM 2.5"
    lv_obj_t *lbl_unit;   // label หน่วย เช่น "ug/m3"
} card_out_t;

static void make_card(lv_obj_t *parent, int w, int h,
                      const char *title, const char *unit,
                      bool has_status, card_out_t *out)
```

เรียกใช้แบบนี้:
```c
card_out_t c = {};
make_card(cont_pm, 338, 190, "PM 2.5", "ug/m3", true, &c);
card_pm25     = c.card;
lbl_pm25      = c.lbl_val;    // ← pointer นี้ใช้อัปเดตค่าทีหลัง
lbl_pm25_st   = c.lbl_status;
lbl_pm25_title = c.lbl_title;
```

แนวคิดเดียวกับ `@IBOutlet` ใน Swift — สร้าง view แล้วเก็บ pointer ไว้เพื่ออัปเดตทีหลัง

### make_nav_chip — สร้าง tab ใน nav bar

```c
static lv_obj_t *make_nav_chip(lv_obj_t *nav, const char *title,
                                const char *unit, int x, int chip_w,
                                lv_obj_t **out_chip, lv_obj_t **out_ind)
```

แต่ละ chip ประกอบด้วย 3 ส่วน:
1. **chip** — กล่อง clickable ทั้งหมด
2. **ind** (indicator) — เส้นขีดสีฟ้าใต้ tab ที่ active (숨긴 โดย default)
3. **lbl_v** — label ค่าย่อที่ nav (เช่น PM2.5 = "12.3")

return ค่าคือ `lbl_v` เพื่อให้โค้ดข้างนอกเก็บ pointer อัปเดตค่าย่อได้

### make_cont — สร้าง content container

```c
static lv_obj_t *make_cont(lv_obj_t *parent)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, 720, 592);               // เต็มพื้นที่ content
    lv_obj_align(c, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0); // โปร่งใส (สีพื้นหลังจาก scr_user)
    ...
    return c;
}
```

ทุก tab ใช้ `make_cont` สร้าง container เดียวกัน แต่เนื้อหาข้างในต่างกัน

---

## ส่วนที่ 4: ui_user_create (บรรทัด 389–773)

นี่คือฟังก์ชันหลักที่ main.cpp เรียกตอน boot (ในสาย lock block) เพื่อสร้างหน้าจอทั้งหมด

### 4.1 ดึง sensor model ที่ active

```c
void ui_user_create(void)
{
    sensor_config_t      cfg = sensor_config_get();        // อ่านจาก NVS
    const sensor_model_t *m  = &SENSOR_MODELS[cfg.model_idx];  // pointer ไป model ที่ใช้งาน
```

ข้อมูลนี้ใช้แสดงชื่อ model ใน header:
```c
snprintf(sub_buf, sizeof(sub_buf), "IoT NODE  %s", m->name);
// → "IoT NODE  SN-300BYH-M"
```

### 4.2 Header (บรรทัด 399–494)

```
┌──────────────────────────────────────────┐
│ [โลโก้] LIV-24  IoT NODE SN-300BYH-M  AQI ⬛│
│─────────────────────── accent line (สีฟ้า) ─│
```

โลโก้จะแสดงก็ต่อเมื่อมีไฟล์อัปโหลดอยู่ใน SPIFFS:
```c
if (eth_upload_has_logo()) {                     // ตรวจว่ามีไฟล์โลโก้ไหม
    lv_obj_t *logo_img = lv_image_create(hdr);
    lv_image_set_src(logo_img, ETH_LOGO_LVGL_PATH);
    title_x = 58;                                // เลื่อนชื่อให้ไม่ทับโลโก้
}
```

AQI Toggle Switch อยู่มุมขวาของ header:
```c
sw_aqi = lv_switch_create(hdr);
lv_obj_add_state(sw_aqi, LV_STATE_CHECKED);           // เปิดโดย default
lv_obj_add_event_cb(sw_aqi, color_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);
```

ใน `color_toggle_cb`:
```c
static void color_toggle_cb(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);  // ดึง widget ที่ trigger
    g_color_on = lv_obj_has_state(sw, LV_STATE_CHECKED); // อ่านสถานะ on/off
    apply_pm_colors();                                     // นำไปใส่ทันที
}
```

### 4.3 HHCC Battery Bar (บรรทัด 453–494)

widget นี้อยู่ในตำแหน่ง **เดียวกับ AQI switch** แต่ซ่อนอยู่ — แสดงเฉพาะเมื่อเปิด HHCC tab:

```
header right side:
  [AQI Color ⬛]   ← แสดงเฉพาะ PM tab
  [78% ▓▓▓▓▓░░░]  ← แสดงเฉพาะ HHCC tab (battery icon)
```

Battery bar แสดงสี 3 ระดับ (ตั้งค่าใน `ui_user_update_hhcc`):
- ≥ 50% → เขียว `0x44FF88`
- 20–49% → เหลือง `0xFFDD44`
- < 20% → แดง `0xFF4444` (border และตัวเลขเปลี่ยนสีด้วย)

### 4.4 Nav Bar (บรรทัด 497–531)

```c
const int CW = 120;    // 720px / 6 tab = 120px ต่อ tab
nav_pm25_val = make_nav_chip(nav, "PM2.5",  "ug",   0*CW, CW, &chip_pm,   &ind_pm);
nav_hhcc_val = make_nav_chip(nav, "HHCC",   "%",    1*CW, CW, &chip_hhcc, &ind_hhcc);
nav_ec_val   = make_nav_chip(nav, "EC/TDS", "uS",   2*CW, CW, &chip_ec,   &ind_ec);
nav_orp_val  = make_nav_chip(nav, "ORP",    "mV",   3*CW, CW, &chip_orp,  &ind_orp);
nav_leak_val = make_nav_chip(nav, "LEAK",   "",     4*CW, CW, &chip_leak, &ind_leak);
nav_th_val   = make_nav_chip(nav, "TH",     "°C",   5*CW, CW, &chip_th,   &ind_th);
```

Divider เส้นบาง 1px ระหว่าง tab สร้างแบบ loop:
```c
for (int dx = CW; dx < 720; dx += CW) {    // dx = 120, 240, 360, 480, 600
    lv_obj_t *div = lv_obj_create(nav);
    lv_obj_set_size(div, 1, 36);            // เส้น 1px สูง 36px
    lv_obj_set_pos(div, dx, 10);            // ชิดซ้ายของแต่ละ tab
    ...
}
```

### 4.5 Content Views (บรรทัด 541–761)

6 view สร้างซ้อนกันทั้งหมด — ทุก view อยู่ใน position เดียวกัน (`LV_ALIGN_BOTTOM_MID`) แต่ซ่อนกันอยู่:

```
scr_user (screen)
├── hdr (header)
├── nav (nav bar)
├── cont_pm   ← view 0 (ค่าอากาศ)
├── cont_hhcc ← view 1 (ต้นไม้)
├── cont_ec   ← view 2 (น้ำ EC)
├── cont_orp  ← view 3 (น้ำ ORP)
├── cont_leak ← view 4 (น้ำรั่ว)
└── cont_th   ← view 5 (temp/hum แยก)
```

**View 0 (PM)** — ใช้ LVGL Flex Layout จัดการ card อัตโนมัติ:
```c
lv_obj_set_layout(cont_pm, LV_LAYOUT_FLEX);
lv_obj_set_flex_flow(cont_pm, LV_FLEX_FLOW_ROW_WRAP);  // วางแถว แล้ว wrap ไปบรรทัดถัดไป
```
→ card 338px 2 ใบ = 676px + gap 12px = 688px → พอดีแถว 2 ใบ  
→ Sound card 688px เป็น full-width

**View 1 (HHCC)** — ใช้ `make_hhcc_card` ที่ text อยู่กลาง card:
```c
lbl_hhcc_temp  = make_hhcc_card(cont_hhcc, "TEMPERATURE", "°C",    0x44FFCCu);
lbl_hhcc_moist = make_hhcc_card(cont_hhcc, "MOISTURE",    "%",     0x44BBFFu);
lbl_hhcc_light = make_hhcc_card(cont_hhcc, "LIGHT",       "lux",   0xFFDD44u);
lbl_hhcc_fert  = make_hhcc_card(cont_hhcc, "FERTILITY",   "uS/cm", 0xFF8844u);
```
แต่ละ card มีสีตัวเลขต่างกัน (ไม่ใช่ AQI — แค่สี theme ต่างกันเพื่อแยกค่า)

**View 2 (EC/TDS)** — card เดียวใหญ่กลางจอ + แสดง TDS คำนวณ:
```c
lbl_tds_big = lv_label_create(card_ec);
lv_label_set_text(lbl_tds_big, "TDS  --  mg/L");
```
ค่า TDS คำนวณจาก EC ตอน update: `TDS = EC × 0.5` (empirical formula สำหรับน้ำทั่วไป)

**View 4 (LEAK)** — card เปลี่ยนสีพื้นหลังทั้งก้อนตาม alarm:
```c
card_leak_big = lv_obj_create(cont_leak);
lv_obj_set_style_bg_color(card_leak_big, lv_color_hex(0x092B18), 0);  // เขียวเข้ม = ปกติ
// เมื่อ alarm: เปลี่ยนเป็น 0x4A0808 (แดงเข้ม) ใน ui_user_update_leak()
```

### 4.6 Default View ตาม Sensor Model (บรรทัด 764–773)

```c
int default_view = 0;
switch (cfg.model_idx) {
    case 1:  default_view = 2; break;  // EC sensor → เปิด tab EC/TDS ก่อน
    case 2:  default_view = 4; break;  // LEAK sensor → เปิด tab LEAK ก่อน
    case 3:  default_view = 5; break;  // TH sensor → เปิด tab TH ก่อน
    case 4:  default_view = 3; break;  // ORP sensor → เปิด tab ORP ก่อน
    default: default_view = 0; break;  // PM sensor (หรืออื่นๆ) → tab PM2.5
}
switch_view(default_view);
```

ทำไมต้องทำแบบนี้? ถ้าติดตั้ง sensor น้ำ (EC) แต่เปิดมาเจอ tab PM ซึ่งแสดง "--" ทั้งหมด ผู้ใช้จะงง — เปิด tab ที่ตรงกับ sensor ที่ติดตั้งจริงจะเข้าใจทันที

---

## ส่วนที่ 5: switch_view — ระบบสลับ tab (บรรทัด 176–214)

```c
static void switch_view(int idx)
{
    // 1. ซ่อน/แสดง content container
    lv_obj_t *conts[6] = { cont_pm, cont_hhcc, cont_ec, cont_orp, cont_leak, cont_th };
    for (int i = 0; i < 6; i++) {
        if (!conts[i]) continue;
        if (i == idx) lv_obj_clear_flag(conts[i], LV_OBJ_FLAG_HIDDEN);  // แสดง
        else          lv_obj_add_flag  (conts[i], LV_OBJ_FLAG_HIDDEN);  // ซ่อน
    }

    // 2. แสดง/ซ่อน indicator (เส้นขีดสีฟ้า) ใต้ tab ที่ active
    lv_obj_t *inds[6] = { ind_pm, ind_hhcc, ind_ec, ind_orp, ind_leak, ind_th };
    for (int i = 0; i < 6; i++) {
        if (i == idx) lv_obj_clear_flag(inds[i], LV_OBJ_FLAG_HIDDEN);
        else          lv_obj_add_flag  (inds[i], LV_OBJ_FLAG_HIDDEN);
    }

    // 3. เปลี่ยนสีตัวหนังสือ tab ใน nav bar (active = สว่าง, inactive = หม่น)
    lv_obj_t *vals[6] = { nav_pm25_val, ... };
    for (int i = 0; i < 6; i++) {
        lv_obj_set_style_text_color(vals[i],
            lv_color_hex(i == idx ? 0xE0EEFFu : 0x6677AAu), 0);
    }

    // 4. สลับ AQI switch / HHCC battery bar ตาม tab
    if (idx == 0) {
        // PM tab: แสดง AQI switch, ซ่อน battery
        lv_obj_clear_flag(sw_aqi,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_aqi, LV_OBJ_FLAG_HIDDEN);
        if (bat_wrap_hdr) lv_obj_add_flag(bat_wrap_hdr, LV_OBJ_FLAG_HIDDEN);
    } else if (idx == 1) {
        // HHCC tab: ซ่อน AQI switch, แสดง battery bar
        lv_obj_add_flag(sw_aqi,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_aqi, LV_OBJ_FLAG_HIDDEN);
        if (bat_wrap_hdr) lv_obj_clear_flag(bat_wrap_hdr, LV_OBJ_FLAG_HIDDEN);
    } else {
        // tab อื่น: ซ่อนทั้งสอง
        lv_obj_add_flag(sw_aqi,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_aqi, LV_OBJ_FLAG_HIDDEN);
        if (bat_wrap_hdr) lv_obj_add_flag(bat_wrap_hdr, LV_OBJ_FLAG_HIDDEN);
    }
}
```

ทำไม `switch_view` จัดการ header ด้วย? เพราะ header ต้องการแสดง context ที่ต่างกันตาม tab — PM tab ต้องการ AQI toggle, HHCC tab ต้องการดู battery — เป็นการ "contextualize" header ตาม content ที่เห็น

---

## ส่วนที่ 6: ฟังก์ชัน Update (บรรทัด 778–916)

ทุกฟังก์ชัน update ถูกเรียกจาก `sensor_read_task` ใน `main.cpp` ทุก 2 วินาที (ใน `bsp_display_lock` block)

### 6.1 ui_user_update — ค่าอากาศหลัก

```c
void ui_user_update(float temp, float hum, float pm25, float pm10, int sound)
{
    if (!scr_user) return;   // guard: ถ้าหน้าจอยังไม่ถูกสร้าง → ข้าม
    g_pm25_last = pm25;      // บันทึกไว้เพื่อ AQI color toggle
    g_pm10_last = pm10;

    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", temp);     // format เป็น string "25.3"
    if (lbl_temp) lv_label_set_text(lbl_temp, buf);

    snprintf(buf, sizeof(buf), "%.1f", pm25);
    if (lbl_pm25)     lv_label_set_text(lbl_pm25, buf);      // ค่าใหญ่ใน card
    if (nav_pm25_val) lv_label_set_text(nav_pm25_val, buf);  // ค่าย่อใน nav bar

    apply_pm_colors();   // อัปเดตสี AQI ตามค่าใหม่
}
```

สังเกตว่าอัปเดต **ทั้ง card และ nav bar** ในการเรียกครั้งเดียวกัน — nav bar แสดงค่า PM2.5 ย่อๆ ให้เห็นแม้อยู่ tab อื่น

### 6.2 ui_user_update_ec — ค่าน้ำ EC + คำนวณ TDS

```c
void ui_user_update_ec(float ec)
{
    if (!scr_user) return;
    char buf[32];
    if (!std::isnan(ec)) {                              // ถ้ามีค่าจริง (ไม่ใช่ NAN)
        snprintf(buf, sizeof(buf), "%.0f", ec);
        if (nav_ec_val) lv_label_set_text(nav_ec_val, buf);
        if (lbl_ec_big) lv_label_set_text(lbl_ec_big, buf);

        snprintf(buf, sizeof(buf), "TDS  %.0f  mg/L", ec * 0.5f);  // EC × 0.5 = TDS
        if (lbl_tds_big) lv_label_set_text(lbl_tds_big, buf);
    } else {
        // sensor ไม่มีค่า EC (model อื่น) → แสดง "--"
        if (nav_ec_val)  lv_label_set_text(nav_ec_val,  "--");
        if (lbl_ec_big)  lv_label_set_text(lbl_ec_big,  "--");
        if (lbl_tds_big) lv_label_set_text(lbl_tds_big, "TDS  --  mg/L");
    }
}
```

`std::isnan(ec)` เพราะ `sensor_read_task` ใส่ `NAN` ในช่อง EC เมื่อ sensor model ที่ใช้ไม่มี EC register (index = -1) — ป้องกันการแสดงเลขขยะ

### 6.3 ui_user_update_leak — เปลี่ยนสีตาม alarm

```c
void ui_user_update_leak(bool alarm)
{
    if (!scr_user) return;

    // nav bar: ข้อความ + สี
    if (nav_leak_val) {
        lv_label_set_text(nav_leak_val, alarm ? "ALARM" : "OK");
        lv_obj_set_style_text_color(nav_leak_val,
            alarm ? lv_color_hex(0xFF4444u) : lv_color_hex(0x00CC44u), 0);
    }

    // card ใหญ่: เปลี่ยนสีพื้นหลังทั้งก้อน
    if (card_leak_big) {
        lv_obj_set_style_bg_color(card_leak_big,
            alarm ? lv_color_hex(0x4A0808u) : lv_color_hex(0x092B18u), 0);
        //          ↑ แดงเข้ม (alarm)           ↑ เขียวเข้ม (ปกติ)
    }

    // ตัวหนังสือใหญ่กลางจอ
    if (lbl_leak_big) {
        lv_label_set_text(lbl_leak_big, alarm ? "ALARM" : "NORMAL");
        lv_obj_set_style_text_color(lbl_leak_big,
            alarm ? lv_color_hex(0xFF6666u) : lv_color_hex(0x44FF88u), 0);
    }
}
```

LEAK ต่างจาก PM ตรงที่ไม่มีระดับ — แค่ **on/off** แต่เปลี่ยน visual ทั้งหมด 3 จุดพร้อมกัน (nav + card bg + text) เพื่อให้เห็นชัดแม้มองจากไกล

### 6.4 ui_user_update_hhcc — ต้นไม้ + battery

```c
void ui_user_update_hhcc(float temp, float moisture, float light,
                          float fertility, float battery)
{
    if (!scr_user) return;
    char buf[16];

    // อัปเดตแต่ละค่า ตรวจ NAN ก่อนทุกตัว
    if (!std::isnan(moisture)) {
        snprintf(buf, sizeof(buf), "%.0f", moisture);
        if (nav_hhcc_val)   lv_label_set_text(nav_hhcc_val, buf);  // nav แสดง moisture
        if (lbl_hhcc_moist) lv_label_set_text(lbl_hhcc_moist, buf);
    }
    ...

    // Battery bar — มี logic แยกสี 3 ระดับ
    if (!std::isnan(battery) && battery >= 0.0f) {
        int pct = (int)battery;
        snprintf(buf, sizeof(buf), "%d%%", pct);
        if (lbl_hhcc_bat_pct) lv_label_set_text(lbl_hhcc_bat_pct, buf);

        if (bat_bar_hhcc) {
            lv_bar_set_value(bat_bar_hhcc, pct, LV_ANIM_OFF);
            // เลือกสี fill ตามเปอร์เซ็นต์
            uint32_t fill_clr = pct >= 50 ? 0x44FF88u   // เขียว
                              : pct >= 20 ? 0xFFDD44u    // เหลือง
                              :             0xFF4444u;   // แดง
            uint32_t bord_clr = pct < 20 ? 0xFF4444u : 0x556677u;
            lv_obj_set_style_bg_color(bat_bar_hhcc, lv_color_hex(fill_clr), LV_PART_INDICATOR);
            lv_obj_set_style_border_color(bat_bar_hhcc, lv_color_hex(bord_clr), LV_PART_MAIN);
            lv_obj_set_style_text_color(lbl_hhcc_bat_pct,
                lv_color_hex(pct < 20 ? 0xFF4444u : 0x445566u), 0);
        }
    }
}
```

ทำไมต้อง `&& battery >= 0.0f`? RPi อาจส่ง battery = -1 เมื่อ BLE ยังไม่ sync — ป้องกันการแสดง "-1%"

---

## ส่วนที่ 7: Null Guard ทุกที่

สังเกตว่าทุกฟังก์ชัน update เริ่มด้วย `if (!scr_user) return` และก่อนใช้ widget pointer ทุกตัวจะตรวจ `if (lbl_xxx)` ก่อน

เหตุผล:
1. `sensor_read_task` เริ่มทำงานหลัง `ui_user_create()` — แต่ในทางทฤษฎีถ้า task เริ่มเร็วเกินไป widget อาจยังไม่พร้อม
2. sensor model บางชนิดไม่มี widget บางอัน (EC sensor ไม่มี `lbl_pm25`) — pointer เหล่านั้นจะเป็น `NULL` ไปตลอด

Pattern เดียวกับ optional chaining ใน Swift: `lbl_pm25?.text = buf` ← C ไม่มี `?` เลยต้องเขียน `if (lbl_pm25) lv_label_set_text(...)` แทน

---

## สรุป: การไหลของข้อมูลในไฟล์นี้

```
boot:  main.cpp → ui_user_create()
                   ├─ อ่าน sensor model จาก NVS
                   ├─ สร้าง header + nav bar + 6 content views
                   └─ switch_view(default_view ตาม model)

ทุก 2 วินาที:
sensor_read_task → bsp_display_lock()
                   ├─ ui_user_update(temp, hum, pm25, pm10, sound)
                   │   └─ อัปเดต PM tab + nav + AQI color
                   ├─ ui_user_update_ec(ec)
                   ├─ ui_user_update_leak(alarm)
                   ├─ ui_user_update_th(temp, hum)
                   ├─ ui_user_update_orp(orp, temp)
                   └─ ui_user_update_hhcc(temp, moist, light, fert, bat)
                   bsp_display_unlock()

user tap:  chip_pm → nav_pm_cb() → switch_view(0)
           chip_ec → nav_ec_cb() → switch_view(2)
           ...
```

| widget | อัปเดตโดย | ทุกกี่วินาที |
|---|---|---|
| ค่าตัวเลขใน card | `ui_user_update_xxx()` | 2 วินาที |
| ค่าย่อใน nav bar | ฟังก์ชันเดียวกัน | 2 วินาที |
| AQI color ใน card | `apply_pm_colors()` | 2 วินาที (หรือทันทีเมื่อ toggle) |
| ค่า battery HHCC | `ui_user_update_hhcc()` | ทุกครั้งที่ MQTT ส่งมา |
| tab ที่แสดง | `switch_view()` | เมื่อผู้ใช้แตะ tab |
