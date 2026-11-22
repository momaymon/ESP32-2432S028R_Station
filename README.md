# CYD Weather Station — ลำลูกกา

สถานีพยากรณ์อากาศบนบอร์ด **ESP32-2432S028R** ("Cheap Yellow Display" จอสัมผัส 2.8") ดึงข้อมูลจาก
[Open-Meteo](https://open-meteo.com/) (ฟรี ไม่ต้องใช้ API key) อัปเดตทุก 10 นาที

- หน้า 1: สภาพอากาศปัจจุบัน + รายชั่วโมง
- หน้า 2: พยากรณ์ 5 วัน
- แตะจอเพื่อสลับหน้า

## ติดตั้ง

1. Arduino IDE → Board: **ESP32 Dev Module**
2. ลงไลบรารี: Adafruit ILI9341 · Adafruit GFX · XPT2046_Touchscreen · ArduinoJson (v7)
3. คัดลอก `secrets.example.h` เป็น `secrets.h` แล้วใส่ชื่อและรหัส WiFi
4. แก้พิกัด `LAT` / `LON` / `PLACE` ใน `.ino` ถ้าต้องการที่อื่น
5. Upload

`secrets.h` อยู่ใน `.gitignore` รหัส WiFi จะไม่ขึ้น GitHub
