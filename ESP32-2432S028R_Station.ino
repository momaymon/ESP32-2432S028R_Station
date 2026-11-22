/*
 * CYD Weather Station - ลำลูกกา
 * ESP32-2432S028R + Open-Meteo API (ฟรี ไม่ต้องใช้ key)
 *
 * ไลบรารี: Adafruit ILI9341, Adafruit GFX, XPT2046_Touchscreen, ArduinoJson (v7)
 * Board: ESP32 Dev Module
 * แตะจอเพื่อสลับหน้า: ปัจจุบัน/รายชั่วโมง <-> พยากรณ์ 5 วัน
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <time.h>

// ================== ตั้งค่า ==================
#include "secrets.h"              // WIFI_SSID / WIFI_PASS (ไม่ขึ้น git · ดู secrets.example.h)
const float LAT = 13.93;          // ลำลูกกา
const float LON = 100.75;
const char* PLACE = "Lam Luk Ka";
const unsigned long REFRESH_MS = 10UL * 60 * 1000;  // อัปเดตทุก 10 นาที

// ================== ขา CYD ==================
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_BL   21
#define T_CLK  25
#define T_MISO 39
#define T_MOSI 32
#define T_CS   33
#define T_IRQ  36
#define LED_R 4
#define LED_G 16
#define LED_B 17

// ================== สี ==================
#define BG        0x10A2
#define CARD      0x2945
#define HEADER    0x0010
#define GREY      0xA514
#define RAINBLUE  0x5D9F
#define ORANGE    0xFD20
#define YELLOW    0xFFE0
#define WHITE     0xFFFF
#define CLOUDCOL  0xDEFB
#define DARKCLOUD 0x7BEF

SPIClass tftSPI(HSPI);
SPIClass touchSPI(VSPI);
Adafruit_ILI9341 tft(&tftSPI, TFT_DC, TFT_CS, -1);
XPT2046_Touchscreen ts(T_CS, T_IRQ);

struct Weather {
  bool ok = false;
  float temp, feels, wind;
  int hum, windDir, code;
  bool isDay;
  String hTime[6];
  int hTemp[6], hRain[6], hCode[6];
  int dMax[5], dMin[5], dRain[5], dCode[5];
} w;

int page = 0;
unsigned long lastFetch = 0, lastClock = 0;
const char* DAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// ================== ข้อความสภาพอากาศ (WMO code) ==================
const char* condText(int c) {
  if (c == 0) return "Clear";
  if (c <= 2) return "Partly Cloudy";
  if (c == 3) return "Cloudy";
  if (c == 45 || c == 48) return "Fog";
  if (c >= 51 && c <= 57) return "Drizzle";
  if (c >= 61 && c <= 67) return "Rain";
  if (c >= 80 && c <= 82) return "Showers";
  if (c >= 95) return "Thunderstorm";
  return "---";
}

const char* windDirText(int deg) {
  const char* d[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return d[((deg + 22) / 45) % 8];
}

// ================== ไอคอน ==================
void drawCloud(int cx, int cy, int r, uint16_t col) {
  tft.fillCircle(cx - r / 2, cy + r / 6, r / 2, col);
  tft.fillCircle(cx + r / 2, cy + r / 6, r / 2, col);
  tft.fillCircle(cx, cy - r / 6, r * 6 / 10, col);
  tft.fillRect(cx - r / 2, cy + r / 6, r, r / 2, col);
}

void drawSun(int cx, int cy, int r) {
  tft.fillCircle(cx, cy, r / 2, YELLOW);
  for (int a = 0; a < 8; a++) {
    float rad = a * PI / 4;
    tft.drawLine(cx + cos(rad) * r * 0.65, cy + sin(rad) * r * 0.65,
                 cx + cos(rad) * r, cy + sin(rad) * r, YELLOW);
  }
}

void drawMoon(int cx, int cy, int r) {
  tft.fillCircle(cx, cy, r / 2, CLOUDCOL);
  tft.fillCircle(cx + r / 4, cy - r / 5, r / 2, BG);
}

void drawIcon(int cx, int cy, int r, int code, bool day) {
  if (code == 0) {
    day ? drawSun(cx, cy, r) : drawMoon(cx, cy, r);
  } else if (code <= 2) {
    day ? drawSun(cx - r / 3, cy - r / 3, r * 2 / 3) : drawMoon(cx - r / 3, cy - r / 3, r * 2 / 3);
    drawCloud(cx + r / 6, cy + r / 6, r * 3 / 4, CLOUDCOL);
  } else if (code == 3 || code == 45 || code == 48) {
    drawCloud(cx, cy, r, CLOUDCOL);
  } else if (code >= 95) {
    drawCloud(cx, cy - r / 4, r, DARKCLOUD);
    tft.fillTriangle(cx + r / 6, cy + r / 6, cx - r / 4, cy + r * 3 / 5, cx, cy + r * 3 / 5, YELLOW);
    tft.fillTriangle(cx - r / 12, cy + r / 2, cx + r / 5, cy + r / 2, cx - r / 8, cy + r, YELLOW);
  } else {  // ฝน / ฝนปรอย / ฝนซู่
    drawCloud(cx, cy - r / 4, r, CLOUDCOL);
    for (int i = -1; i <= 1; i++) {
      int x = cx + i * r / 3;
      tft.drawLine(x, cy + r / 2, x - r / 6, cy + r * 9 / 10, RAINBLUE);
      tft.drawLine(x + 1, cy + r / 2, x + 1 - r / 6, cy + r * 9 / 10, RAINBLUE);
    }
  }
}

// พิมพ์อุณหภูมิพร้อมสัญลักษณ์องศา คืนค่าตำแหน่ง x ท้ายข้อความ
int printTemp(int x, int y, int val, int size, uint16_t col, bool withC) {
  tft.setTextSize(size);
  tft.setTextColor(col);
  tft.setCursor(x, y);
  tft.print(val);
  int cx = tft.getCursorX();
  tft.drawCircle(cx + size * 2, y + size * 2, size, col);
  int end = cx + size * 5;
  if (withC) {
    tft.setCursor(end, y);
    tft.print("C");
    end = tft.getCursorX();
  }
  return end;
}

// ================== ดึงข้อมูล ==================
bool fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    return false;
  }

  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(LAT, 4) +
               "&longitude=" + String(LON, 4) +
               "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,"
               "weather_code,wind_speed_10m,wind_direction_10m"
               "&hourly=temperature_2m,precipitation_probability,weather_code"
               "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
               "&timezone=Asia%2FBangkok&forecast_days=5&forecast_hours=12";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(client, url);
  int httpCode = http.GET();
  if (httpCode != 200) {
    Serial.printf("HTTP error: %d\n", httpCode);
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    Serial.println("JSON error");
    return false;
  }

  JsonObject cur = doc["current"];
  w.temp    = cur["temperature_2m"];
  w.feels   = cur["apparent_temperature"];
  w.hum     = cur["relative_humidity_2m"];
  w.wind    = cur["wind_speed_10m"];
  w.windDir = cur["wind_direction_10m"];
  w.code    = cur["weather_code"];
  w.isDay   = cur["is_day"].as<int>() == 1;

  JsonObject h = doc["hourly"];
  for (int i = 0; i < 6; i++) {
    int k = i * 2;  // ทุก 2 ชั่วโมง
    w.hTime[i] = h["time"][k].as<String>().substring(11, 16);
    w.hTemp[i] = round(h["temperature_2m"][k].as<float>());
    w.hRain[i] = h["precipitation_probability"][k] | 0;
    w.hCode[i] = h["weather_code"][k] | 0;
  }

  JsonObject d = doc["daily"];
  for (int i = 0; i < 5; i++) {
    w.dMax[i]  = round(d["temperature_2m_max"][i].as<float>());
    w.dMin[i]  = round(d["temperature_2m_min"][i].as<float>());
    w.dRain[i] = d["precipitation_probability_max"][i] | 0;
    w.dCode[i] = d["weather_code"][i] | 0;
  }

  w.ok = true;
  Serial.printf("Updated: %.1fC, code %d\n", w.temp, w.code);
  return true;
}

// ================== วาดหน้าจอ ==================
void drawHeader() {
  tft.fillRect(0, 0, 320, 24, HEADER);
  tft.setTextSize(2);
  tft.setTextColor(WHITE);
  tft.setCursor(6, 5);
  tft.print(page == 0 ? PLACE : "5-Day Forecast");

  struct tm t;
  if (getLocalTime(&t, 50)) {
    char buf[6];
    strftime(buf, sizeof(buf), "%H:%M", &t);
    tft.setCursor(254, 5);
    tft.print(buf);
  }
}

void drawNoData() {
  tft.setTextSize(2);
  tft.setTextColor(GREY);
  tft.setCursor(60, 110);
  tft.print("Loading data...");
}

void drawPage1() {
  tft.fillScreen(BG);
  drawHeader();
  if (!w.ok) { drawNoData(); return; }

  // ปัจจุบัน
  drawIcon(52, 66, 40, w.code, w.isDay);
  printTemp(108, 34, round(w.temp), 5, WHITE, true);
  tft.setTextSize(2);
  tft.setTextColor(CLOUDCOL);
  tft.setCursor(108, 84);
  tft.print(condText(w.code));

  // กล่องรายละเอียด
  const char* labels[3] = {"FEELS LIKE", "HUMIDITY", "WIND km/h"};
  for (int i = 0; i < 3; i++) {
    int bx = 4 + i * 105;
    tft.fillRoundRect(bx, 106, 100, 38, 4, CARD);
    tft.setTextSize(1);
    tft.setTextColor(GREY);
    tft.setCursor(bx + 6, 110);
    tft.print(labels[i]);
  }
  printTemp(10, 124, round(w.feels), 2, ORANGE, true);
  tft.setTextSize(2);
  tft.setTextColor(RAINBLUE);
  tft.setCursor(115, 124);
  tft.print(String(w.hum) + "%");
  tft.setTextColor(WHITE);
  tft.setCursor(220, 124);
  tft.print(String((int)round(w.wind)) + " " + windDirText(w.windDir));

  // รายชั่วโมง
  for (int i = 0; i < 6; i++) {
    int cx = i * 53 + 26;
    int hh = w.hTime[i].substring(0, 2).toInt();
    tft.setTextSize(1);
    tft.setTextColor(GREY);
    tft.setCursor(cx - 15, 152);
    tft.print(w.hTime[i]);
    drawIcon(cx, 176, 18, w.hCode[i], hh >= 6 && hh < 18);
    printTemp(cx - 14, 198, w.hTemp[i], 2, WHITE, false);
    tft.setTextSize(1);
    tft.setTextColor(RAINBLUE);
    tft.setCursor(cx - 10, 222);
    tft.print(String(w.hRain[i]) + "%");
  }
}

void drawPage2() {
  tft.fillScreen(BG);
  drawHeader();
  if (!w.ok) { drawNoData(); return; }

  struct tm t;
  bool haveTime = getLocalTime(&t, 50);

  for (int i = 0; i < 5; i++) {
    int y = 28 + i * 42;
    if (i > 0) tft.drawFastHLine(8, y - 2, 304, CARD);

    tft.setTextSize(2);
    tft.setTextColor(WHITE);
    tft.setCursor(10, y + 12);
    if (i == 0) tft.print("Today");
    else if (haveTime) tft.print(DAYS[(t.tm_wday + i) % 7]);
    else tft.print("+" + String(i));

    drawIcon(100, y + 18, 22, w.dCode[i], true);

    int ex = printTemp(135, y + 12, w.dMax[i], 2, ORANGE, false);
    tft.setTextColor(GREY);
    tft.setCursor(ex + 2, y + 12);
    tft.print("/");
    printTemp(ex + 18, y + 12, w.dMin[i], 2, RAINBLUE, false);

    tft.setTextColor(RAINBLUE);
    tft.setCursor(262, y + 12);
    tft.print(String(w.dRain[i]) + "%");
  }
}

void drawPage() {
  page == 0 ? drawPage1() : drawPage2();
}

// ================== setup / loop ==================
void setup() {
  Serial.begin(115200);

  pinMode(LED_R, OUTPUT); digitalWrite(LED_R, HIGH);
  pinMode(LED_G, OUTPUT); digitalWrite(LED_G, HIGH);
  pinMode(LED_B, OUTPUT); digitalWrite(LED_B, HIGH);
  pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);

  tftSPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin(40000000);
  tft.setRotation(1);

  touchSPI.begin(T_CLK, T_MISO, T_MOSI, T_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  tft.fillScreen(BG);
  tft.setTextSize(2);
  tft.setTextColor(WHITE);
  tft.setCursor(40, 110);
  tft.print("Connecting WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? "\nWiFi OK" : "\nWiFi FAIL");

  configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");  // เวลาไทย UTC+7

  fetchWeather();
  lastFetch = millis();
  lastClock = millis();
  drawPage();
}

void loop() {
  // แตะจอเพื่อสลับหน้า
  if (ts.tirqTouched() && ts.touched()) {
    page ^= 1;
    drawPage();
    while (ts.touched()) delay(10);
    delay(150);
  }

  // อัปเดตข้อมูล (ถ้าล้มเหลวจะลองใหม่ใน 1 นาที)
  if (millis() - lastFetch > REFRESH_MS) {
    if (fetchWeather()) {
      lastFetch = millis();
      drawPage();
    } else {
      lastFetch = millis() - REFRESH_MS + 60000;
    }
  }

  // อัปเดตนาฬิกาทุก 20 วินาที
  if (millis() - lastClock > 20000) {
    lastClock = millis();
    drawHeader();
  }
}