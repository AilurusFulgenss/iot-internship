# Weekly Report

## ชื่อผู้ฝึกงาน
- กุดา ทองนาม (Kuda Tonnam)

## Week
- Week 1 — IoT Foundation (2 – 6 มิถุนายน 2569)

## หัวข้อที่ฝึก
- IoT concepts และ architecture (sensor → gateway → cloud)
- Git & GitHub: version control, branching, commit, push
- MQTT protocol: publish/subscribe, broker, topic, QoS
- การติดตั้ง tools: VS Code, PlatformIO, Git, MQTT Explorer

## สิ่งที่ทำสำเร็จ
1. ติดตั้ง development environment ครบ (VS Code + PlatformIO + Git)
2. สร้าง GitHub repository `iot-internship` และ push โค้ดครั้งแรกได้
3. ทดสอบ MQTT ผ่าน MQTT Explorer: publish/subscribe topic ทดสอบได้สำเร็จ

## ปัญหาที่พบ
1. PlatformIO ติดตั้ง toolchain ช้า เนื่องจาก package มีขนาดใหญ่
2. Git push ครั้งแรก authentication ไม่ผ่าน (HTTPS vs SSH)

## วิธีแก้ไข
1. รอ download ให้เสร็จ และใช้ offline package cache ครั้งถัดไป
2. ใช้ Personal Access Token (PAT) แทน password สำหรับ HTTPS

## สิ่งที่ได้เรียนรู้
1. IoT architecture แบ่งเป็น 3 layer: Edge (sensor/actuator), Gateway, Cloud/Server
2. MQTT เป็น lightweight protocol เหมาะกับ IoT เพราะใช้ bandwidth น้อย และรองรับ retained message
3. Git workflow: clone → branch → commit → push → pull request ช่วยให้ทำงานเป็นทีมได้

## Link GitHub / Code
- https://github.com/AilurusFulgenss/iot-internship

## รูปภาพ / Screenshot / Video Demo
- (รอเพิ่ม screenshot MQTT Explorer และ PlatformIO)

## แผนงานสัปดาห์ถัดไป
1. ทดลองใช้ ESP32-P4 กับ PlatformIO: build และ flash โปรแกรมแรก
2. ศึกษาการใช้งาน LVGL บน display 720×720 MIPI DSI
3. ต่อ relay และ RS485 sensor ทดสอบ GPIO
________________________________________
